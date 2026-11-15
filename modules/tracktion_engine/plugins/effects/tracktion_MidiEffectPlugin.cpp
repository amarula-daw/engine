/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2024
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \   Tracktion Software
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |       Corporation
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com

    Tracktion Engine uses a GPL/commercial licence - see LICENCE.md for details.
*/

namespace tracktion::inline engine {

//==============================================================================
double MidiEffectPlugin::Block::getBeat (double seconds) const noexcept
{
    if (numSeconds <= 0.0)
        return startBeat;

    return startBeat + (endBeat - startBeat) * (seconds / numSeconds);
}

double MidiEffectPlugin::Block::getTime (double beat) const noexcept
{
    if (endBeat <= startBeat)
        return 0.0;

    // Events stay inside the block, so they're never dropped as being past its end
    auto last = std::max (0.0, numSeconds - 1.0e-6);
    return juce::jlimit (0.0, last, (beat - startBeat) / (endBeat - startBeat) * numSeconds);
}

//==============================================================================
MidiEffectPlugin::MidiEffectPlugin (PluginCreationInfo info)  : Plugin (info)
{
}

MidiEffectPlugin::~MidiEffectPlugin()
{
    notifyListenersOfDeletion();

    for (auto p : getAutomatableParameters())
        p->detachFromCurrentValue();
}

juce::StringArray MidiEffectPlugin::getDivisionNames()
{
    return { "1/4", "1/8", "1/16", "1/32", "1/4T", "1/8T", "1/16T", "1/32T" };
}

double MidiEffectPlugin::getDivisionBeats (int division)
{
    division = juce::jlimit (0, 7, division);
    auto beats = 1.0 / (double) (1 << (division % 4));
    return division >= 4 ? beats * 2.0 / 3.0 : beats;
}

void MidiEffectPlugin::initialise (const PluginInitialisationInfo&)
{
    // Room for a busy block, so adding to it doesn't allocate on the audio thread
    input.reserve (512);
}

void MidiEffectPlugin::deinitialise()
{
}

//==============================================================================
AutomatableParameter* MidiEffectPlugin::addParameter (AutomatableParameter* p, float defaultValue)
{
    const juce::Identifier property (p->paramID);

    // Stored from the start, so undoing the first change sets it back rather than removing it
    if (! state.hasProperty (property))
        state.setProperty (property, defaultValue, nullptr);

    auto value = values.add (new juce::CachedValue<float>());
    value->referTo (state, property, getUndoManager(), defaultValue);
    addAutomatableParameter (p);
    p->attachToCurrentValue (*value);
    return p;
}

AutomatableParameter* MidiEffectPlugin::addChoiceParam (const juce::String& paramID, const juce::String& name,
                                                        float defaultValue, const juce::StringArray& choices)
{
    auto p = new DiscreteLabelledParameter (paramID, name, *this, { 0.0f, (float) (choices.size() - 1) },
                                            choices.size(), choices);
    p->valueToStringFunction = [choices] (float v)  { return choices[juce::jlimit (0, choices.size() - 1, juce::roundToInt (v))]; };
    p->stringToValueFunction = [choices] (const juce::String& s)  { return (float) std::max (0, choices.indexOf (s, true)); };
    return addParameter (p, defaultValue);
}

AutomatableParameter* MidiEffectPlugin::addIntParam (const juce::String& paramID, const juce::String& name, float defaultValue,
                                                     juce::Range<float> range, std::function<juce::String (float)> valueToString)
{
    auto p = new AutomatableParameter (paramID, name, *this, { range.getStart(), range.getEnd(), 1.0f });
    p->valueToStringFunction = [valueToString] (float v)  { return valueToString (std::round (v)); };
    p->stringToValueFunction = [] (const juce::String& s)  { return (float) s.getIntValue(); };
    return addParameter (p, defaultValue);
}

AutomatableParameter* MidiEffectPlugin::addFloatParam (const juce::String& paramID, const juce::String& name, float defaultValue,
                                                       juce::Range<float> range, std::function<juce::String (float)> valueToString)
{
    auto p = new AutomatableParameter (paramID, name, *this, { range.getStart(), range.getEnd() });
    p->valueToStringFunction = std::move (valueToString);
    p->stringToValueFunction = [] (const juce::String& s)  { return s.getFloatValue(); };
    return addParameter (p, defaultValue);
}

int MidiEffectPlugin::getInt (AutomatableParameter& p)
{
    return juce::roundToInt (p.getCurrentValue());
}

void MidiEffectPlugin::restorePluginStateFromValueTree (const juce::ValueTree& v)
{
    for (auto value : values)
    {
        if (auto prop = v.getPropertyPointer (value->getPropertyID()))
            *value = static_cast<float> (*prop);
        else
            value->resetToDefault();
    }

    for (auto p : getAutomatableParameters())
        p->updateFromAttachedValue();
}

void MidiEffectPlugin::valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& i)
{
    // Undo and redo change the stored values, which the parameters follow (unless automation is playing them).
    // The tree's listeners run in no particular order, so the value's cache may not have heard yet.
    if (v == state)
    {
        for (auto value : values)
            if (value->getPropertyID() == i)
                value->forceUpdateOfCachedValue();

        for (auto p : getAutomatableParameters())
            if (p->paramID == i.toString() && ! p->hasAutomationPoints())
                p->updateFromAttachedValue();
    }

    Plugin::valueTreePropertyChanged (v, i);
}

//==============================================================================
void MidiEffectPlugin::sendNoteOn (MidiMessageArray& out, int channel, int note, int velocity, double time, MPESourceID source)
{
    if (! juce::isPositiveAndBelow (note, 128) || channel < 1 || channel > maxMidiChannel)
        return;

    auto& count = soundingCounts[channel - 1][note];

    if (count > 0)
        out.addMidiMessage (juce::MidiMessage::noteOff (channel, note), time, source);
    else
        ++numSounding;

    if (count < 255)
        ++count;

    out.addMidiMessage (juce::MidiMessage::noteOn (channel, note, (juce::uint8) juce::jlimit (1, 127, velocity)), time, source);
}

void MidiEffectPlugin::sendNoteOff (MidiMessageArray& out, int channel, int note, double time, MPESourceID source)
{
    if (! juce::isPositiveAndBelow (note, 128) || channel < 1 || channel > maxMidiChannel)
        return;

    auto& count = soundingCounts[channel - 1][note];

    if (count == 0)
        return;

    if (--count == 0)
    {
        --numSounding;
        out.addMidiMessage (juce::MidiMessage::noteOff (channel, note), time, source);
    }
}

void MidiEffectPlugin::releaseAll (MidiMessageArray& out, double time)
{
    if (numSounding == 0)
        return;

    for (int channel = 0; channel < maxMidiChannel; ++channel)
    {
        for (int note = 0; note < 128; ++note)
        {
            if (soundingCounts[channel][note] > 0)
            {
                soundingCounts[channel][note] = 0;
                out.addMidiMessage (juce::MidiMessage::noteOff (channel + 1, note), time, {});
            }
        }
    }

    numSounding = 0;
}

void MidiEffectPlugin::startNotes (MidiMessageArray& out, const MidiMessageWithSource& noteOn, const int* notes, int numNotes,
                                   int velocity, double time)
{
    const auto channel = noteOn.getChannel();
    const auto inputNote = noteOn.getNoteNumber();

    if (channel < 1 || channel > maxMidiChannel)
        return out.add (noteOn, time);

    // Played again before its note-off: what it started last time ends first
    endNotes (out, noteOn, time);

    auto& entry = started[channel - 1][inputNote];
    entry.held = true;
    entry.numNotes = 0;

    for (int i = 0; i < numNotes && entry.numNotes < maxNotesPerInput; ++i)
    {
        if (! juce::isPositiveAndBelow (notes[i], 128))
            continue;

        bool duplicate = false;

        for (int j = 0; j < entry.numNotes; ++j)
            duplicate = duplicate || entry.notes[j] == notes[i];

        if (duplicate)
            continue;

        entry.notes[entry.numNotes++] = (int8_t) notes[i];
        sendNoteOn (out, channel, notes[i], velocity, time, noteOn.mpeSourceID);
    }
}

bool MidiEffectPlugin::endNotes (MidiMessageArray& out, const MidiMessageWithSource& noteOff, double time)
{
    const auto channel = noteOff.getChannel();

    if (channel < 1 || channel > maxMidiChannel)
        return false;

    auto& entry = started[channel - 1][noteOff.getNoteNumber()];

    if (! entry.held)
        return false;

    for (int i = 0; i < entry.numNotes; ++i)
        sendNoteOff (out, channel, entry.notes[i], time, noteOff.mpeSourceID);

    entry.held = false;
    entry.numNotes = 0;
    return true;
}

void MidiEffectPlugin::releaseAndReset (MidiMessageArray& out)
{
    releaseAll (out, 0.0);
    forgetStarted();
    resetMidiState();
}

void MidiEffectPlugin::forgetStarted()
{
    for (auto& channel : started)
        for (auto& entry : channel)
            entry = {};
}

//==============================================================================
void MidiEffectPlugin::applyToBuffer (const PluginRenderContext& fc)
{
    if (fc.bufferForMidiMessages == nullptr)
        return;

    auto& buffer = *fc.bufferForMidiMessages;

    // Takes the block's messages out, so the buffer becomes what the effect plays
    input.clear();

    for (auto& m : buffer)
        input.add (m, m.getTimeStamp() + fc.midiBufferOffset);

    input.sortByTimestamp();
    const bool allNotesOff = buffer.isAllNotesOff;
    buffer.clear();
    buffer.isAllNotesOff = allNotesOff;

    Block block;
    block.numSeconds = fc.bufferNumSamples / std::max (1.0, sampleRate);
    block.playing = fc.isPlaying && ! fc.editTime.isEmpty();
    bool jumped = false;

    if (block.playing)
    {
        const auto start = fc.editTime.getStart();
        const auto end = fc.editTime.getEnd();

        tempoPosition.set (start);
        block.startBeat = tempoPosition.getBeats().inBeats();
        tempoPosition.set (end);
        block.endBeat = tempoPosition.getBeats().inBeats();
        block.numSeconds = fc.editTime.getLength().inSeconds();

        // A loop or a move of the playhead: what was held there isn't held here
        jumped = wasPlaying && std::abs (start.inSeconds() - lastEditEnd) > 1.0e-3;
        lastEditEnd = end.inSeconds();
        freeBeat = block.endBeat;
    }
    else
    {
        // Stopped, live playing follows a clock of its own at the tempo where the playhead is
        tempoPosition.set (fc.editTime.getStart());
        block.startBeat = freeBeat;
        block.endBeat = freeBeat + block.numSeconds * tempoPosition.getTempo() / 60.0;
        freeBeat = block.endBeat;
    }

    if (allNotesOff)
    {
        // Everything downstream is being stopped already
        std::memset (soundingCounts, 0, sizeof (soundingCounts));
        numSounding = 0;
        forgetStarted();
        resetMidiState();
    }
    else if (jumped || block.playing != wasPlaying)
    {
        releaseAndReset (buffer);
    }

    wasPlaying = block.playing;

    processMidi (input, buffer, block);
    buffer.sortByTimestamp();
}

} // namespace tracktion::inline engine
