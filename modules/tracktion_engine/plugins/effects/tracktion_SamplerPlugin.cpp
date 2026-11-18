/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2024
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \   Tracktion Software
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |       Corporation
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com

    Tracktion Engine uses a GPL/commercial licence - see LICENCE.md for details.
*/

namespace tracktion::inline engine {

// this must be high enough for low freq sounds not to click
static constexpr int minimumSamplesToPlayWhenStopping = 8;
static constexpr int maximumSimultaneousNotes = 32;
// how quickly a sound fades out when another in its choke group starts
static constexpr double chokeFadeSeconds = 0.005;


struct SamplerPlugin::SampledNote   : public ReferenceCountedObject
{
public:
    SampledNote (int midiNote, int keyNote_,
                 float velocity_,
                 const AudioFile& file,
                 double sampleRate,
                 int sampleDelayFromBufferStart,
                 const juce::AudioBuffer<float>& data,
                 int lengthInSamples_,
                 bool openEnded_,
                 const SamplerSound& sound_)
       : note (midiNote),
         offset (-sampleDelayFromBufferStart),
         audioData (data),
         openEnded (openEnded_),
         sound (&sound_),
         chokeGroup (sound_.chokeGroup),
         keyNote (keyNote_),
         velocity (velocity_),
         lengthInSamples (lengthInSamples_),
         fileToOutputRatio (file.getSampleRate() / sampleRate),
         releaseSamples (juce::roundToInt (sound_.releaseSeconds * sampleRate))
    {
        if (const auto attackSamples = juce::roundToInt (sound_.attackSeconds * sampleRate); attackSamples > 0)
        {
            envelope = 0.0f;
            attackStep = 1.0f / (float) attackSamples;
        }

        resampler[0].reset();
        resampler[1].reset();

        updateFromSound();
    }

    /** Follows the sound's gain, pan and pitch, which its parameters can move while it plays. */
    void updateFromSound()
    {
        auto valueOf = [] (const AutomatableParameter::Ptr& param, float fallback)
        {
            return param != nullptr ? param->getCurrentValue() : fallback;
        };

        const float gainDb = valueOf (sound->parameters.gain, sound->gainDb);
        const float pan = juce::jlimit (-1.0f, 1.0f, valueOf (sound->parameters.pan, sound->pan));
        const float pitch = valueOf (sound->parameters.pitch, sound->pitch);

        if (gainDb != lastGainDb || pan != lastPan)
        {
            lastGainDb = gainDb;
            lastPan = pan;
            const float volumeSliderPos = decibelsToVolumeFaderPosition (gainDb - (20.0f * (1.0f - velocity)));
            getGainsFromVolumeFaderPositionAndPan (volumeSliderPos, pan, getDefaultPanLaw(), gains[0], gains[1]);
        }

        if (pitch != lastPitch)
        {
            lastPitch = pitch;
            playbackRatio = std::pow (2.0, (note + (double) pitch - keyNote) / 12.0) * fileToOutputRatio;

            // What's left of the sample plays out at the new speed, unless the note's been cut short
            if (! cutShort)
            {
                const int samplesLeftInFile = lengthInSamples - std::max (0, offset);
                samplesLeftToPlay = playbackRatio > 0 ? std::max (0, 1 + (int) (samplesLeftInFile / playbackRatio)) : 0;
            }
        }
    }

    /** Ends the note at a sample in the current block (just after, so it doesn't click). */
    void cutAt (int sampleInBlock)
    {
        cutShort = true;
        samplesLeftToPlay = std::min (samplesLeftToPlay, std::max (minimumSamplesToPlayWhenStopping, sampleInBlock));
    }

    void addNextBlock (juce::AudioBuffer<float>& outBuffer, int startSamp, int numSamples)
    {
        jassert (! isFinished);

        if (envelope >= 1.0f && fadeDelay < 0 && fadeStep <= 0.0f)
        {
            render (outBuffer, startSamp, numSamples);
            return;
        }

        // Shaped by the envelope: render into a scratch buffer, then apply it sample by sample
        const int numChans = std::min (2, outBuffer.getNumChannels());
        const int firstAudibleSample = juce::jlimit (0, numSamples, -offset);

        AudioScratchBuffer scratch (numChans, numSamples);
        scratch.buffer.clear();
        render (scratch.buffer, 0, numSamples);

        auto chans = scratch.buffer.getArrayOfWritePointers();

        for (int i = 0; i < numSamples; ++i)
        {
            if (i == fadeDelay)
            {
                // fades from wherever the envelope is, so a note cut during its attack doesn't jump
                fadeStep = std::max (fadeStep, std::max (envelope, 1.0e-4f) / (float) fadeLength);
                fadeDelay = -1;
            }

            if (i >= firstAudibleSample)
            {
                if (fadeStep > 0.0f)
                    envelope = std::max (0.0f, envelope - fadeStep);
                else if (envelope < 1.0f)
                    envelope = std::min (1.0f, envelope + attackStep);
            }

            for (int c = 0; c < numChans; ++c)
                chans[c][i] *= envelope;

            if (envelope <= 0.0f && fadeStep > 0.0f)
            {
                isFinished = true;

                for (int c = 0; c < numChans; ++c)
                    juce::FloatVectorOperations::clear (chans[c] + i, numSamples - i);

                break;
            }
        }

        if (fadeDelay >= 0)
            fadeDelay = std::max (0, fadeDelay - numSamples);

        for (int c = 0; c < numChans; ++c)
            outBuffer.addFrom (c, startSamp, scratch.buffer, c, 0, numSamples);
    }

    /** Stops the note from a sample in the current block: over its release, or almost at once without one. */
    void stop (int sampleInBlock)
    {
        if (releaseSamples > 0)
            fadeOut (sampleInBlock, releaseSamples);
        else
            cutAt (sampleInBlock);
    }

    /** Fades the note out over a number of samples, from a sample in the current block.
        A fade that's already under way only gets quicker.
    */
    void fadeOut (int sampleInBlock, int numSamples)
    {
        numSamples = std::max (1, numSamples);

        if (fadeDelay >= 0 && fadeDelay <= sampleInBlock && fadeLength <= numSamples)
            return;

        fadeDelay = std::max (0, sampleInBlock);
        fadeLength = numSamples;
    }

    juce::LagrangeInterpolator resampler[2];
    int note;
    int offset, samplesLeftToPlay = 0;
    float gains[2];
    double playbackRatio = 1.0;
    const juce::AudioBuffer<float>& audioData;
    float lastVals[4] = { 0, 0, 0, 0 };
    float startFade = 1.0f;
    bool openEnded, isFinished = false;
    const SamplerSound* sound;
    int chokeGroup = 0;

private:
    int keyNote;
    float velocity;
    int lengthInSamples;
    double fileToOutputRatio;
    float lastGainDb = std::numeric_limits<float>::quiet_NaN(), lastPan = 0.0f;
    float lastPitch = std::numeric_limits<float>::quiet_NaN();
    bool cutShort = false;
    int releaseSamples = 0;
    float envelope = 1.0f, attackStep = 0.0f, fadeStep = 0.0f;
    int fadeDelay = -1, fadeLength = 0;

    void render (juce::AudioBuffer<float>& outBuffer, int startSamp, int numSamples)
    {
        if (offset < 0)
        {
            const int num = std::min (-offset, numSamples);
            startSamp += num;
            numSamples -= num;
            offset += num;
        }

        auto numSamps = std::min (numSamples, samplesLeftToPlay);

        if (numSamps > 0)
        {
            int numUsed = 0;

            for (int i = std::min (2, outBuffer.getNumChannels()); --i >= 0;)
            {
                numUsed = resampler[i]
                            .processAdding (playbackRatio,
                                            audioData.getReadPointer (std::min (i, audioData.getNumChannels() - 1), offset),
                                            outBuffer.getWritePointer (i, startSamp),
                                            numSamps,
                                            gains[i]);
            }

            offset += numUsed;
            samplesLeftToPlay -= numSamps;

            jassert (offset <= audioData.getNumSamples());
        }

        if (numSamples > numSamps && startFade > 0.0f)
        {
            startSamp += numSamps;
            numSamps = numSamples - numSamps;
            float endFade;

            if (numSamps > 100)
            {
                endFade = 0.0f;
                numSamps = 100;
            }
            else
            {
                endFade = std::max (0.0f, startFade - float (numSamps) * 0.01f);
            }

            const int numSampsNeeded = 2 + juce::roundToInt ((numSamps + 2) * playbackRatio);
            AudioScratchBuffer scratch (audioData.getNumChannels(), numSampsNeeded + 8);

            if (offset + numSampsNeeded < audioData.getNumSamples())
            {
                for (int i = scratch.buffer.getNumChannels(); --i >= 0;)
                    scratch.buffer.copyFrom (i, 0, audioData, i, offset, numSampsNeeded);
            }
            else
            {
                scratch.buffer.clear();
            }

            if (numSampsNeeded > 2)
                AudioFadeCurve::applyCrossfadeSection (scratch.buffer, 0, numSampsNeeded - 2,
                                                       AudioFadeCurve::linear, startFade, endFade);

            startFade = endFade;

            int numUsed = 0;

            for (int i = std::min (2, outBuffer.getNumChannels()); --i >= 0;)
                numUsed = resampler[i].processAdding (playbackRatio,
                                                      scratch.buffer.getReadPointer (std::min (i, scratch.buffer.getNumChannels() - 1)),
                                                      outBuffer.getWritePointer (i, startSamp),
                                                      numSamps, gains[i]);

            offset += numUsed;

            if (startFade <= 0.0f)
                isFinished = true;
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampledNote)
};

//==============================================================================
/** A sound's parameters, attached to its state's gain, pan and pitch. */
struct SamplerPlugin::SoundParameterSet
{
    SoundParameterSet (SamplerPlugin& owner, const juce::ValueTree& soundState, int soundID)
        : state (soundState), id (soundID)
    {
        auto um = owner.getUndoManager();
        gainDb.referTo (state, IDs::gainDb, um);
        pan.referTo (state, IDs::pan, um);
        pitch.referTo (state, IDs::pitch, um);

        auto prefix = "sound" + juce::String (id) + " ";
        auto name = [&] (const juce::String& property) { return state[IDs::name].toString() + " " + property; };

        parameters.gain = new AutomatableParameter (prefix + "gain", name (TRANS("Gain")), owner, { -48.0f, 48.0f });
        parameters.pan = new AutomatableParameter (prefix + "pan", name (TRANS("Pan")), owner, { -1.0f, 1.0f });
        parameters.pitch = new AutomatableParameter (prefix + "pitch", name (TRANS("Pitch")), owner,
                                                     { -maxPitchSemitones, maxPitchSemitones });

        parameters.gain->attachToCurrentValue (gainDb);
        parameters.pan->attachToCurrentValue (pan);
        parameters.pitch->attachToCurrentValue (pitch);
    }

    ~SoundParameterSet()
    {
        for (auto& param : { parameters.gain, parameters.pan, parameters.pitch })
            param->detachFromCurrentValue();
    }

    juce::ValueTree state;
    const int id;
    juce::CachedValue<float> gainDb, pan, pitch;
    SoundParameters parameters;
};

//==============================================================================
SamplerPlugin::SamplerPlugin (PluginCreationInfo info)  : Plugin (info)
{
    triggerAsyncUpdate();
}

SamplerPlugin::~SamplerPlugin()
{
    notifyListenersOfDeletion();
}

const char* SamplerPlugin::xmlTypeName = "sampler";

void SamplerPlugin::valueTreeChanged()
{
    if (! soundsUnchanged)
        triggerAsyncUpdate();

    Plugin::valueTreeChanged();
}

bool SamplerPlugin::isSoundStructureChange (const juce::ValueTree& parent, const juce::ValueTree& child) const
{
    // Sounds coming and going; automation curves and modifier assignments don't change them
    return parent == state && child.hasType (IDs::SOUND);
}

void SamplerPlugin::valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& i)
{
    // A sound's gain, pan and pitch play through its parameters, so moving them (or automating
    // them, which writes them back) mustn't rebuild the sounds, which cuts off playing notes.
    // Nor does anything inside the automation curves.
    const bool isSoundSetting = v.getParent() == state && v.hasType (IDs::SOUND)
                                  && (i == IDs::gainDb || i == IDs::pan || i == IDs::pitch || i == IDs::id);
    const juce::ScopedValueSetter<bool> svs (soundsUnchanged, v != state && (isSoundSetting || ! v.hasType (IDs::SOUND)));
    Plugin::valueTreePropertyChanged (v, i);
}

void SamplerPlugin::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    const juce::ScopedValueSetter<bool> svs (soundsUnchanged, ! isSoundStructureChange (parent, child));
    Plugin::valueTreeChildAdded (parent, child);
}

void SamplerPlugin::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int index)
{
    const juce::ScopedValueSetter<bool> svs (soundsUnchanged, ! isSoundStructureChange (parent, child));
    Plugin::valueTreeChildRemoved (parent, child, index);
}

void SamplerPlugin::updateSoundParameters()
{
    TRACKTION_ASSERT_MESSAGE_THREAD
    std::vector<std::unique_ptr<SoundParameterSet>> updated;
    std::set<int> usedIDs;
    int nextID = 1;

    for (auto v : state)
        if (v.hasType (IDs::SOUND) && v.hasProperty (IDs::id))
            nextID = std::max (nextID, static_cast<int> (v[IDs::id]) + 1);

    for (auto v : state)
    {
        if (! v.hasType (IDs::SOUND))
            continue;

        // Sounds from older edits (or copies with a clashing id) get one of their own.
        // It isn't an undoable change: it names the sound, it doesn't change it.
        auto id = static_cast<int> (v[IDs::id]);

        if (! v.hasProperty (IDs::id) || id <= 0 || usedIDs.count (id) > 0)
        {
            id = nextID++;
            v.setProperty (IDs::id, id, nullptr);
        }

        usedIDs.insert (id);

        auto existing = std::find_if (soundParameters.begin(), soundParameters.end(),
                                      [&] (auto& set) { return set != nullptr && set->id == id && set->state == v; });

        if (existing != soundParameters.end())
            updated.push_back (std::move (*existing));
        else
            updated.push_back (std::make_unique<SoundParameterSet> (*this, v, id));
    }

    bool listChanged = false;

    for (auto& old : soundParameters)
    {
        if (old != nullptr)
        {
            for (auto& param : { old->parameters.gain, old->parameters.pan, old->parameters.pitch })
                deleteParameter (param.get());

            listChanged = true;
        }
    }

    for (auto& set : updated)
    {
        if (indexOfAutomatableParameter (set->parameters.gain) < 0)
        {
            for (auto& param : { set->parameters.gain, set->parameters.pan, set->parameters.pitch })
                addAutomatableParameter (param);

            listChanged = true;
        }
    }

    soundParameters = std::move (updated);

    if (listChanged)
        if (auto t = getOwnerTrack())
            t->refreshCurrentAutoParam();
}

void SamplerPlugin::flushPendingUpdates()
{
    TRACKTION_ASSERT_MESSAGE_THREAD
    handleUpdateNowIfNeeded();
}

void SamplerPlugin::handleAsyncUpdate()
{
    updateSoundParameters();

    juce::OwnedArray<SamplerSound> newSounds;

    auto numSounds = state.getNumChildren();

    for (int i = 0; i < numSounds; ++i)
    {
        auto v = getSound (i);

        if (v.hasType (IDs::SOUND))
        {
            auto s = new SamplerSound (*this,
                                       v[IDs::source].toString(),
                                       v[IDs::name],
                                       v[IDs::startTime],
                                       v[IDs::length],
                                       v[IDs::gainDb]);

            s->keyNote      = juce::jlimit (0, 127, static_cast<int> (v[IDs::keyNote]));
            s->minNote      = juce::jlimit (0, 127, static_cast<int> (v[IDs::minNote]));
            s->maxNote      = juce::jlimit (0, 127, static_cast<int> (v[IDs::maxNote]));
            s->pan          = juce::jlimit (-1.0f, 1.0f, static_cast<float> (v[IDs::pan]));
            s->openEnded    = v[IDs::openEnded];
            s->chokeGroup   = juce::jlimit (0, maxChokeGroup, static_cast<int> (v[IDs::chokeGroup]));
            s->minVelocity  = juce::jlimit (1, 127, static_cast<int> (v.getProperty (IDs::minVelocity, 1)));
            s->maxVelocity  = juce::jlimit (s->minVelocity, 127, static_cast<int> (v.getProperty (IDs::maxVelocity, 127)));
            s->roundRobin   = juce::jlimit (0, maxRoundRobin, static_cast<int> (v[IDs::roundRobin]));
            s->attackSeconds  = juce::jlimit (0.0, maxAttackSeconds, static_cast<double> (v[IDs::attack]));
            s->releaseSeconds = juce::jlimit (0.0, maxReleaseSeconds, static_cast<double> (v[IDs::release]));
            s->pitch        = juce::jlimit (-maxPitchSemitones, maxPitchSemitones, static_cast<float> (v[IDs::pitch]));

            for (auto& set : soundParameters)
                if (set->state == v)
                    s->parameters = set->parameters;

            newSounds.add (s);
        }
    }

    for (auto newSound : newSounds)
    {
        for (auto s : soundList)
        {
            if (s->source == newSound->source
                && s->startTime == newSound->startTime
                && s->length == newSound->length)
            {
                newSound->audioFile = s->audioFile;
                newSound->fileStartSample = s->fileStartSample;
                newSound->fileLengthSamples = s->fileLengthSamples;
                newSound->audioData = s->audioData;
            }
        }
    }

    {
        const juce::ScopedLock sl (lock);
        allNotesOff();
        soundList.swapWith (newSounds);

        sourceMediaChanged();
    }

    newSounds.clear();
    changed();
}

void SamplerPlugin::initialise (const PluginInitialisationInfo&)
{
    const juce::ScopedLock sl (lock);
    allNotesOff();
    roundRobinTurns = {};
}

void SamplerPlugin::deinitialise()
{
    allNotesOff();
}

//==============================================================================
template <typename Fn>
void SamplerPlugin::forEachSoundToPlay (int note, int velocity, Fn&& fn)
{
    // Sounds without audio (missing or still loading) don't take a turn
    auto plays = [note, velocity] (const SamplerSound& ss)
    {
        return ss.minNote <= note && ss.maxNote >= note
            && ss.minVelocity <= velocity && ss.maxVelocity >= velocity
            && ss.audioData.getNumSamples() > 0;
    };

    auto& turns = roundRobinTurns;
    const auto key = (size_t) juce::jlimit (0, 127, note);

    // How many sounds each round robin has for this note, so the turn picks one of them
    std::array<int, maxRoundRobin + 1> candidates {}, seen {};

    for (auto ss : soundList)
        if (ss->roundRobin > 0 && plays (*ss))
            ++candidates[(size_t) ss->roundRobin];

    for (auto ss : soundList)
    {
        if (! plays (*ss))
            continue;

        if (const auto group = (size_t) ss->roundRobin; group > 0)
            if ((uint32_t) seen[group]++ != turns[group][key] % (uint32_t) candidates[group])
                continue;

        fn (*ss);
    }

    for (size_t group = 1; group < candidates.size(); ++group)
        if (candidates[group] > 0)
            ++turns[group][key];
}

void SamplerPlugin::playNotes (const juce::BigInteger& keysDown)
{
    const juce::ScopedLock sl (lock);

    if (highlightedNotes != keysDown)
    {
        for (int i = playingNotes.size(); --i >= 0;)
            if ((! keysDown [playingNotes.getUnchecked(i)->note])
                 && highlightedNotes [playingNotes.getUnchecked(i)->note]
                 && ! playingNotes.getUnchecked(i)->openEnded)
                playingNotes.getUnchecked(i)->stop (0);

        for (int note = 128; --note >= 0;)
        {
            if (keysDown [note] && ! highlightedNotes [note])
            {
                constexpr float velocity = 0.75f;

                forEachSoundToPlay (note, juce::roundToInt (velocity * 127.0f), [&] (SamplerSound& ss)
                {
                    if (ss.audioData.getNumSamples() > 0
                         && (! ss.audioFile.isNull())
                         && playingNotes.size() < maximumSimultaneousNotes)
                    {
                        chokeOtherSounds (ss, 0);

                        playingNotes.add (new SampledNote (note,
                                                           ss.keyNote,
                                                           velocity,
                                                           ss.audioFile,
                                                           sampleRate,
                                                           0,
                                                           ss.audioData,
                                                           ss.fileLengthSamples,
                                                           ss.openEnded,
                                                           ss));
                    }
                });
            }
        }

        highlightedNotes = keysDown;
    }
}

void SamplerPlugin::chokeOtherSounds (const SamplerSound& sound, int sampleInBlock)
{
    if (sound.chokeGroup <= 0)
        return;

    const int fadeSamples = std::max (1, juce::roundToInt (chokeFadeSeconds * sampleRate));

    for (auto playingNote : playingNotes)
        if (playingNote->chokeGroup == sound.chokeGroup && playingNote->sound != &sound)
            playingNote->fadeOut (sampleInBlock, fadeSamples);
}

void SamplerPlugin::allNotesOff()
{
    const juce::ScopedLock sl (lock);
    playingNotes.clear();
    highlightedNotes.clear();
}

void SamplerPlugin::applyToBuffer (const PluginRenderContext& fc)
{
    if (fc.destBuffer != nullptr)
    {
        SCOPED_REALTIME_CHECK

        const juce::ScopedLock sl (lock);

        clearChannels (*fc.destBuffer, 2, -1, fc.bufferStartSample, fc.bufferNumSamples);

        if (fc.bufferForMidiMessages != nullptr)
        {
            if (fc.bufferForMidiMessages->isAllNotesOff)
            {
                playingNotes.clear();
                highlightedNotes.clear();
            }

            for (auto& m : *fc.bufferForMidiMessages)
            {
                if (m.isNoteOn())
                {
                    const int note = m.getNoteNumber();
                    const int noteTimeSample = juce::roundToInt (m.getTimeStamp() * sampleRate);

                    for (auto playingNote : playingNotes)
                    {
                        if (playingNote->note == note && ! playingNote->openEnded)
                        {
                            playingNote->cutAt (noteTimeSample);
                            highlightedNotes.clearBit (note);
                        }
                    }

                    forEachSoundToPlay (note, std::max (1, (int) m.getVelocity()), [&] (SamplerSound& ss)
                    {
                        if (ss.audioData.getNumSamples() > 0
                            && playingNotes.size() < maximumSimultaneousNotes)
                        {
                            highlightedNotes.setBit (note);
                            chokeOtherSounds (ss, noteTimeSample);

                            playingNotes.add (new SampledNote (note,
                                                               ss.keyNote,
                                                               m.getVelocity() / 127.0f,
                                                               ss.audioFile,
                                                               sampleRate,
                                                               noteTimeSample,
                                                               ss.audioData,
                                                               ss.fileLengthSamples,
                                                               ss.openEnded,
                                                               ss));
                        }
                    });
                }
                else if (m.isNoteOff())
                {
                    const int note = m.getNoteNumber();
                    const int noteTimeSample = juce::roundToInt (m.getTimeStamp() * sampleRate);

                    for (auto playingNote : playingNotes)
                    {
                        if (playingNote->note == note && ! playingNote->openEnded)
                        {
                            playingNote->stop (noteTimeSample);
                            highlightedNotes.clearBit (note);
                        }
                    }
                }
                else if (m.isAllNotesOff() || m.isAllSoundOff())
                {
                    playingNotes.clear();
                    highlightedNotes.clear();
                }
            }
        }

        for (int i = playingNotes.size(); --i >= 0;)
        {
            auto sn = playingNotes.getUnchecked (i);

            sn->updateFromSound();
            sn->addNextBlock (*fc.destBuffer, fc.bufferStartSample, fc.bufferNumSamples);

            if (sn->isFinished)
                playingNotes.remove (i);
        }
    }
}

//==============================================================================
int SamplerPlugin::getNumSounds() const
{
    return std::accumulate (state.begin(), state.end(), 0,
                            [] (int total, auto v) { return total + (v.hasType (IDs::SOUND) ? 1 : 0); });
}

juce::String SamplerPlugin::getSoundName (int index) const
{
    return getSound (index)[IDs::name];
}

void SamplerPlugin::setSoundName (int index, const juce::String& n)
{
    getSound (index).setProperty (IDs::name, n, getUndoManager());
}

bool SamplerPlugin::hasNameForMidiNoteNumber (int note, int, juce::String& noteName)
{
    juce::String s;

    {
        const juce::ScopedLock sl (lock);

        for (auto ss : soundList)
        {
            if (ss->minNote <= note && ss->maxNote >= note)
            {
                if (s.isNotEmpty())
                    s << " + " << ss->name;
                else
                    s = ss->name;
            }
        }
    }

    noteName = s;
    return true;
}

AudioFile SamplerPlugin::getSoundFile (int index) const
{
    const juce::ScopedLock sl (lock);

    if (auto s = soundList[index])
        return s->audioFile;

    return AudioFile (edit.engine);
}

juce::String SamplerPlugin::getSoundMedia (int index) const
{
    const juce::ScopedLock sl (lock);

    if (auto s = soundList[index])
        return s->source;

    return {};
}

int SamplerPlugin::getKeyNote (int index) const             { return getSound (index)[IDs::keyNote]; }
int SamplerPlugin::getMinKey (int index) const              { return getSound (index)[IDs::minNote]; }
int SamplerPlugin::getMaxKey (int index) const              { return getSound (index)[IDs::maxNote]; }
float SamplerPlugin::getSoundGainDb (int index) const       { return getSound (index)[IDs::gainDb]; }
float SamplerPlugin::getSoundPan (int index) const          { return getSound (index)[IDs::pan]; }
double SamplerPlugin::getSoundStartTime (int index) const   { return getSound (index)[IDs::startTime]; }
bool SamplerPlugin::isSoundOpenEnded (int index) const      { return getSound (index)[IDs::openEnded]; }
int SamplerPlugin::getSoundChokeGroup (int index) const     { return getSound (index)[IDs::chokeGroup]; }
double SamplerPlugin::getSoundAttack (int index) const      { return getSound (index)[IDs::attack]; }
double SamplerPlugin::getSoundRelease (int index) const     { return getSound (index)[IDs::release]; }
// As the sounds play them: a range that's back to front plays its lowest velocity only
int SamplerPlugin::getSoundMinVelocity (int index) const    { return juce::jlimit (1, 127, static_cast<int> (getSound (index).getProperty (IDs::minVelocity, 1))); }
int SamplerPlugin::getSoundMaxVelocity (int index) const    { return juce::jlimit (getSoundMinVelocity (index), 127, static_cast<int> (getSound (index).getProperty (IDs::maxVelocity, 127))); }
int SamplerPlugin::getSoundRoundRobin (int index) const     { return juce::jlimit (0, maxRoundRobin, static_cast<int> (getSound (index)[IDs::roundRobin])); }

double SamplerPlugin::getSoundLength (int index) const
{
    const double l = getSound (index)[IDs::length];

    if (l == 0.0)
    {
        const juce::ScopedLock sl (lock);

        if (auto s = soundList[index])
            return s->length;
    }

    return l;
}

juce::String SamplerPlugin::addSound (const juce::String& source, const juce::String& name,
                                      double startTime, double length, float gainDb)
{
    const int maxNumSamples = 64;

    if (getNumSounds() >= maxNumSamples)
        return TRANS("Can't load any more samples");

    int nextID = 1;

    for (auto sound : state)
        if (sound.hasType (IDs::SOUND))
            nextID = std::max (nextID, static_cast<int> (sound[IDs::id]) + 1);

    auto v = createValueTree (IDs::SOUND,
                              IDs::id, nextID,
                              IDs::source, source,
                              IDs::name, name,
                              IDs::startTime, startTime,
                              IDs::length, length,
                              IDs::keyNote, 72,
                              IDs::minNote, 72 - 24,
                              IDs::maxNote, 72 + 24,
                              IDs::gainDb, gainDb,
                              IDs::pan, (double) 0);

    state.addChild (v, -1, getUndoManager());
    return {};
}

void SamplerPlugin::removeSound (int index)
{
    auto um = getUndoManager();
    auto sound = getSound (index);

    if (! sound.isValid())
        return;

    // Its parameters go with it, so their automation and modifier assignments do too
    if (sound.hasProperty (IDs::id))
    {
        auto prefix = "sound" + sound[IDs::id].toString() + " ";
        auto isSounds = [&] (const juce::ValueTree& v) { return v[IDs::paramID].toString().startsWith (prefix); };

        for (int i = state.getNumChildren(); --i >= 0;)
        {
            auto child = state.getChild (i);

            if (child.hasType (IDs::AUTOMATIONCURVE) && isSounds (child))
                state.removeChild (i, um);
            else if (child.hasType (IDs::MODIFIERASSIGNMENTS))
                for (int j = child.getNumChildren(); --j >= 0;)
                    if (isSounds (child.getChild (j)))
                        child.removeChild (j, um);
        }
    }

    state.removeChild (sound, um);

    const juce::ScopedLock sl (lock);
    playingNotes.clear();
    highlightedNotes.clear();
}

void SamplerPlugin::setSoundParams (int index, int keyNote, int minNote, int maxNote)
{
    auto um = getUndoManager();

    auto v = getSound (index);
    v.setProperty (IDs::keyNote, juce::jlimit (0, 127, keyNote), um);
    v.setProperty (IDs::minNote, juce::jlimit (0, 127, std::min (minNote, maxNote)), um);
    v.setProperty (IDs::maxNote, juce::jlimit (0, 127, std::max (minNote, maxNote)), um);
}

void SamplerPlugin::setSoundGains (int index, float gainDb, float pan)
{
    auto um = getUndoManager();

    auto v = getSound (index);
    v.setProperty (IDs::gainDb, juce::jlimit (-48.0f, 48.0f, gainDb), um);
    v.setProperty (IDs::pan,    juce::jlimit (-1.0f,  1.0f,  pan), um);
}

void SamplerPlugin::setSoundExcerpt (int index, double start, double length)
{
    auto um = getUndoManager();

    auto v = getSound (index);
    v.setProperty (IDs::startTime, start, um);
    v.setProperty (IDs::length, length, um);
}

void SamplerPlugin::setSoundOpenEnded (int index, bool b)
{
    auto um = getUndoManager();

    auto v = getSound (index);
    v.setProperty (IDs::openEnded, b, um);
}

void SamplerPlugin::setSoundChokeGroup (int index, int chokeGroup)
{
    getSound (index).setProperty (IDs::chokeGroup, juce::jlimit (0, maxChokeGroup, chokeGroup), getUndoManager());
}

void SamplerPlugin::setSoundVelocityRange (int index, int minVelocity, int maxVelocity)
{
    auto um = getUndoManager();

    auto v = getSound (index);
    v.setProperty (IDs::minVelocity, juce::jlimit (1, 127, std::min (minVelocity, maxVelocity)), um);
    v.setProperty (IDs::maxVelocity, juce::jlimit (1, 127, std::max (minVelocity, maxVelocity)), um);
}

void SamplerPlugin::setSoundRoundRobin (int index, int roundRobin)
{
    getSound (index).setProperty (IDs::roundRobin, juce::jlimit (0, maxRoundRobin, roundRobin), getUndoManager());
}

void SamplerPlugin::setSoundEnvelope (int index, double attackSeconds, double releaseSeconds)
{
    auto um = getUndoManager();

    auto v = getSound (index);
    v.setProperty (IDs::attack,  juce::jlimit (0.0, maxAttackSeconds, attackSeconds), um);
    v.setProperty (IDs::release, juce::jlimit (0.0, maxReleaseSeconds, releaseSeconds), um);
}

float SamplerPlugin::getSoundPitch (int index) const
{
    return getSound (index)[IDs::pitch];
}

void SamplerPlugin::setSoundPitch (int index, float semitones)
{
    getSound (index).setProperty (IDs::pitch, juce::jlimit (-maxPitchSemitones, maxPitchSemitones, semitones), getUndoManager());
}

int SamplerPlugin::getSoundID (int index) const
{
    return getSound (index)[IDs::id];
}

SamplerPlugin::SoundParameters SamplerPlugin::getSoundParameters (int index)
{
    flushPendingUpdates();

    for (auto& set : soundParameters)
        if (set->state == getSound (index))
            return set->parameters;

    return {};
}

void SamplerPlugin::setSoundMedia (int index, const juce::String& source)
{
    auto v = getSound (index);
    v.setProperty (IDs::source, source, getUndoManager());
    triggerAsyncUpdate();
}

juce::ValueTree SamplerPlugin::getSound (int soundIndex) const
{
    int index = 0;

    for (auto v : state)
        if (v.hasType (IDs::SOUND))
            if (index++ == soundIndex)
                return v;

    return {};
}

//==============================================================================
juce::Array<Exportable::ReferencedItem> SamplerPlugin::getReferencedItems()
{
    juce::Array<ReferencedItem> results;

    // must be careful to generate this list in the right order..
    for (int i = 0; i < getNumSounds(); ++i)
    {
        auto v = getSound (i);

        Exportable::ReferencedItem ref;
        ref.itemRef = ProjectItemID::fromProperty (v, IDs::source);
        ref.firstTimeUsed = v[IDs::startTime];
        ref.lengthUsed = v[IDs::length];
        results.add (ref);
    }

    return results;
}

void SamplerPlugin::reassignReferencedItem (const ReferencedItem& item, ProjectItemRef newRef, double newStartTime)
{
    auto index = getReferencedItems().indexOf (item);

    if (index >= 0)
    {
        auto um = getUndoManager();
        auto v = getSound (index);

        if (newRef.isProjectItemID())
        {
            v.setProperty (IDs::source, newRef.toString(), um);
            v.setProperty (IDs::startTime, static_cast<double> (v[IDs::startTime]) - newStartTime, um);
        }
        else
        {
            auto newFile = newRef.resolve (edit.engine);
            v.setProperty (IDs::source, newFile.getFullPathName(), um);
        }
    }
    else
    {
        jassertfalse;
    }
}

void SamplerPlugin::sourceMediaChanged()
{
    const juce::ScopedLock sl (lock);

    for (auto s : soundList)
        s->refreshFile();
}

void SamplerPlugin::restorePluginStateFromValueTree (const juce::ValueTree& v)
{
    copyValueTree (state, v, getUndoManager());
}

//==============================================================================
SamplerPlugin::SamplerSound::SamplerSound (SamplerPlugin& sf,
                                           const juce::String& source_,
                                           const juce::String& name_,
                                           const double startTime_,
                                           const double length_,
                                           const float gainDb_)
    : owner (sf),
      source (source_),
      name (name_),
      gainDb (juce::jlimit (-48.0f, 48.0f, gainDb_)),
      startTime (startTime_),
      length (length_),
      audioFile (owner.edit.engine, SourceFileReference::findFileFromString (owner.edit, source))
{
    setExcerpt (startTime_, length_);

    keyNote = audioFile.getInfo().loopInfo.getRootNote();

    if (keyNote < 0)
        keyNote = 72;

    maxNote = keyNote + 24;
    minNote = keyNote - 24;
}

void SamplerPlugin::SamplerSound::setExcerpt (double startTime_, double length_)
{
    CRASH_TRACER

    if (! audioFile.isValid())
    {
        audioFile = AudioFile (owner.edit.engine, SourceFileReference::findFileFromString (owner.edit, source));

       #if JUCE_DEBUG
        if (! audioFile.isValid() && ProjectItemID (source).isValid())
            DBG ("Failed to find media: " << source);
       #endif
    }

    if (audioFile.isValid())
    {
        const double minLength = 32.0 / audioFile.getSampleRate();

        startTime = juce::jlimit (0.0, audioFile.getLength() - minLength, startTime_);

        if (length_ > 0)
            length = juce::jlimit (minLength, audioFile.getLength() - startTime, length_);
        else
            length = audioFile.getLength();

        fileStartSample   = juce::roundToInt (startTime * audioFile.getSampleRate());
        fileLengthSamples = juce::roundToInt (length * audioFile.getSampleRate());

        if (auto reader = owner.engine.getAudioFileManager().cache.createReader (audioFile))
        {
            audioData.setSize (audioFile.getNumChannels(), fileLengthSamples + 32);
            audioData.clear();

            auto audioDataChannels = ChannelConfiguration::canonical (audioFile.getNumChannels());
            auto channelsToUse = ChannelConfiguration::stereo();

            int total = fileLengthSamples;
            int offset = 0;

            while (total > 0)
            {
                const int numThisTime = std::min (8192, total);
                reader->setReadPosition (fileStartSample + offset);

                if (! reader->readSamples (numThisTime, audioData, audioDataChannels, offset, channelsToUse, 2000))
                {
                    jassertfalse;
                    break;
                }

                offset += numThisTime;
                total -= numThisTime;
            }
        }
        else
        {
            audioData.clear();
        }

        // add a quick fade-in if needed..
        int fadeLen = 0;
        for (int i = audioData.getNumChannels(); --i >= 0;)
        {
            const float* d = audioData.getReadPointer (i);

            if (std::abs (*d) > 0.01f)
                fadeLen = 30;
        }

        if (fadeLen > 0)
            AudioFadeCurve::applyCrossfadeSection (audioData, 0, fadeLen, AudioFadeCurve::concave, 0.0f, 1.0f);
    }
    else
    {
        audioFile = AudioFile (owner.edit.engine);
    }
}

void SamplerPlugin::SamplerSound::refreshFile()
{
    audioFile = AudioFile (owner.edit.engine);
    setExcerpt (startTime, length);
}

} // namespace tracktion::inline engine
