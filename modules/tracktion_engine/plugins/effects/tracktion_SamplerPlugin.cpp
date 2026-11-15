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
    SampledNote (int midiNote, int keyNote,
                 float velocity,
                 const AudioFile& file,
                 double sampleRate,
                 int sampleDelayFromBufferStart,
                 const juce::AudioBuffer<float>& data,
                 int lengthInSamples,
                 float gainDb,
                 float pan,
                 bool openEnded_,
                 const SamplerSound& sound_)
       : note (midiNote),
         offset (-sampleDelayFromBufferStart),
         audioData (data),
         openEnded (openEnded_),
         sound (&sound_),
         chokeGroup (sound_.chokeGroup),
         releaseSamples (juce::roundToInt (sound_.releaseSeconds * sampleRate))
    {
        if (const auto attackSamples = juce::roundToInt (sound_.attackSeconds * sampleRate); attackSamples > 0)
        {
            envelope = 0.0f;
            attackStep = 1.0f / (float) attackSamples;
        }

        resampler[0].reset();
        resampler[1].reset();

        const float volumeSliderPos = decibelsToVolumeFaderPosition (gainDb - (20.0f * (1.0f - velocity)));
        getGainsFromVolumeFaderPositionAndPan (volumeSliderPos, pan, getDefaultPanLaw(), gains[0], gains[1]);

        const double hz = juce::MidiMessage::getMidiNoteInHertz (midiNote);
        playbackRatio = hz / juce::MidiMessage::getMidiNoteInHertz (keyNote);
        playbackRatio *= file.getSampleRate() / sampleRate;
        samplesLeftToPlay = playbackRatio > 0 ? (1 + (int) (lengthInSamples / playbackRatio)) : 0;
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
            samplesLeftToPlay = std::min (samplesLeftToPlay, std::max (minimumSamplesToPlayWhenStopping, sampleInBlock));
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
    triggerAsyncUpdate();
    Plugin::valueTreeChanged();
}

void SamplerPlugin::flushPendingUpdates()
{
    TRACKTION_ASSERT_MESSAGE_THREAD
    handleUpdateNowIfNeeded();
}

void SamplerPlugin::handleAsyncUpdate()
{
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
            s->attackSeconds  = juce::jlimit (0.0, maxAttackSeconds, static_cast<double> (v[IDs::attack]));
            s->releaseSeconds = juce::jlimit (0.0, maxReleaseSeconds, static_cast<double> (v[IDs::release]));

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
}

void SamplerPlugin::deinitialise()
{
    allNotesOff();
}

//==============================================================================
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
                for (auto ss : soundList)
                {
                    if (ss->minNote <= note
                         && ss->maxNote >= note
                         && ss->audioData.getNumSamples() > 0
                         && (! ss->audioFile.isNull())
                         && playingNotes.size() < maximumSimultaneousNotes)
                    {
                        chokeOtherSounds (*ss, 0);

                        playingNotes.add (new SampledNote (note,
                                                           ss->keyNote,
                                                           0.75f,
                                                           ss->audioFile,
                                                           sampleRate,
                                                           0,
                                                           ss->audioData,
                                                           ss->fileLengthSamples,
                                                           ss->gainDb,
                                                           ss->pan,
                                                           ss->openEnded,
                                                           *ss));
                    }
                }
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
                            playingNote->samplesLeftToPlay = std::min (playingNote->samplesLeftToPlay,
                                                                       std::max (minimumSamplesToPlayWhenStopping,
                                                                                 noteTimeSample));
                            highlightedNotes.clearBit (note);
                        }
                    }

                    for (auto ss : soundList)
                    {
                        if (ss->minNote <= note
                            && ss->maxNote >= note
                            && ss->audioData.getNumSamples() > 0
                            && playingNotes.size() < maximumSimultaneousNotes)
                        {
                            highlightedNotes.setBit (note);
                            chokeOtherSounds (*ss, noteTimeSample);

                            playingNotes.add (new SampledNote (note,
                                                               ss->keyNote,
                                                               m.getVelocity() / 127.0f,
                                                               ss->audioFile,
                                                               sampleRate,
                                                               noteTimeSample,
                                                               ss->audioData,
                                                               ss->fileLengthSamples,
                                                               ss->gainDb,
                                                               ss->pan,
                                                               ss->openEnded,
                                                               *ss));
                        }
                    }
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

    auto v = createValueTree (IDs::SOUND,
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
    state.removeChild (index, getUndoManager());

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

void SamplerPlugin::setSoundEnvelope (int index, double attackSeconds, double releaseSeconds)
{
    auto um = getUndoManager();

    auto v = getSound (index);
    v.setProperty (IDs::attack,  juce::jlimit (0.0, maxAttackSeconds, attackSeconds), um);
    v.setProperty (IDs::release, juce::jlimit (0.0, maxReleaseSeconds, releaseSeconds), um);
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
