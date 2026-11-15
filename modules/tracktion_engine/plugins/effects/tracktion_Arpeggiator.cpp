/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2024
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \   Tracktion Software
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |       Corporation
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com

    Tracktion Engine uses a GPL/commercial licence - see LICENCE.md for details.
*/

namespace tracktion::inline engine {

const char* ArpeggiatorPlugin::xmlTypeName ("arpeggiator");

ArpeggiatorPlugin::ArpeggiatorPlugin (PluginCreationInfo info)  : MidiEffectPlugin (info)
{
    rate = addChoiceParam ("rate", TRANS("Rate"), 2.0f, getDivisionNames());
    mode = addChoiceParam ("mode", TRANS("Mode"), (float) up,
                           { TRANS("Up"), TRANS("Down"), TRANS("Up-Down"), TRANS("As Played"), TRANS("Random") });
    octaves = addIntParam ("octaves", TRANS("Octaves"), 1.0f, { 1.0f, (float) maxOctaves },
                           [] (float v) { return juce::String (juce::roundToInt (v)); });
    gate = addFloatParam ("gate", TRANS("Gate"), 50.0f, { 10.0f, 100.0f },
                          [] (float v) { return juce::String (juce::roundToInt (v)) + "%"; });
}

ArpeggiatorPlugin::~ArpeggiatorPlugin()
{
}

void ArpeggiatorPlugin::resetMidiState()
{
    numHeld = 0;
    patternLength = 0;
    isSounding = false;
    nextStepBeat = -1.0;
    lastStepBeat = -1.0e9;
    pendingStartBeat = -1.0;
    stepIndex = 0;
}

//==============================================================================
void ArpeggiatorPlugin::buildPattern (int modeToUse, int numOctaves)
{
    int order[maxHeld];

    for (int i = 0; i < numHeld; ++i)
        order[i] = i;

    // Lowest first, but in the order played for that mode
    if (modeToUse != asPlayed)
        std::sort (order, order + numHeld, [this] (int a, int b) { return held[a].note < held[b].note; });

    patternLength = 0;

    for (int octave = 0; octave < numOctaves; ++octave)
    {
        for (int i = 0; i < numHeld; ++i)
        {
            const int note = held[order[i]].note + 12 * octave;

            if (note < 128)
                pattern[patternLength++] = { (int8_t) note, (int8_t) order[i] };
        }
    }

    if (modeToUse == down)
    {
        std::reverse (pattern, pattern + patternLength);
    }
    else if (modeToUse == upDown)
    {
        // Back down again without playing the top and bottom twice
        for (int i = patternLength - 2; i > 0; --i)
            pattern[patternLength++] = pattern[i];
    }
}

void ArpeggiatorPlugin::stopSounding (MidiMessageArray& output, double time)
{
    if (isSounding)
    {
        sendNoteOff (output, sounding.channel, sounding.note, time, sounding.source);
        isSounding = false;
    }
}

void ArpeggiatorPlugin::playStep (MidiMessageArray& output, const Block& block, double beat,
                                  int modeToUse, int numOctaves, double gateBeats)
{
    const auto time = block.getTime (beat);
    stopSounding (output, time);
    lastStepBeat = beat;

    if (numHeld == 0)
        return;

    buildPattern (modeToUse, numOctaves);

    if (patternLength == 0)
        return;

    const auto index = modeToUse == randomOrder ? random.nextInt (patternLength) : stepIndex % patternLength;
    ++stepIndex;

    const auto& step = pattern[index];
    sounding = held[step.held];
    sounding.note = step.note;
    sendNoteOn (output, sounding.channel, sounding.note, sounding.velocity, time, sounding.source);
    isSounding = true;
    gateEndBeat = beat + gateBeats;
}

void ArpeggiatorPlugin::advance (MidiMessageArray& output, const Block& block, double beforeBeat,
                                 int modeToUse, int numOctaves, double gateBeats)
{
    for (;;)
    {
        if (pendingStartBeat >= 0.0 && pendingStartBeat < beforeBeat)
        {
            const auto beat = std::exchange (pendingStartBeat, -1.0);
            playStep (output, block, beat, modeToUse, numOctaves, gateBeats);
            continue;
        }

        const auto gateEnd = isSounding ? gateEndBeat : std::numeric_limits<double>::max();

        if (std::min (gateEnd, nextStepBeat) >= beforeBeat)
            return;

        // A gate of 100% ends right as the next step starts, so the note-off goes first
        if (gateEnd <= nextStepBeat)
        {
            stopSounding (output, block.getTime (gateEnd));
            continue;
        }

        playStep (output, block, nextStepBeat, modeToUse, numOctaves, gateBeats);
        nextStepBeat += stepBeats;
    }
}

void ArpeggiatorPlugin::processMidi (const MidiMessageArray& input, MidiMessageArray& output, const Block& block)
{
    const auto modeToUse = juce::jlimit (0, (int) randomOrder, getInt (*mode));
    const auto numOctaves = juce::jlimit (1, maxOctaves, getInt (*octaves));
    const auto newStepBeats = getDivisionBeats (getInt (*rate));

    // Steps fall on the beat grid, from wherever the block starts
    if (nextStepBeat < 0.0 || newStepBeats != stepBeats)
    {
        stepBeats = newStepBeats;
        nextStepBeat = std::ceil (block.startBeat / stepBeats - 1.0e-9) * stepBeats;
    }

    const auto gateBeats = stepBeats * juce::jlimit (10.0f, 100.0f, gate->getCurrentValue()) / 100.0;

    // A note landing just after a step it should have started (by rounding, or played a hair
    // late) starts straight away rather than a step late
    const auto lateBeats = block.numSeconds > 0.0 ? 0.01 * (block.endBeat - block.startBeat) / block.numSeconds : 0.02;

    for (auto& m : input)
    {
        const auto time = m.getTimeStamp();
        const auto beat = block.getBeat (time);
        advance (output, block, beat, modeToUse, numOctaves, gateBeats);

        const auto channel = m.getChannel();

        if (channel < 1 || channel > maxMidiChannel || ! (m.isNoteOn() || m.isNoteOff()))
        {
            output.add (m, time);
            continue;
        }

        int found = -1;

        for (int i = 0; i < numHeld; ++i)
            if (held[i].channel == channel && held[i].note == m.getNoteNumber())
                found = i;

        if (m.isNoteOn())
        {
            if (found >= 0)
            {
                held[found].velocity = m.getVelocity();
                continue;
            }

            if (numHeld == maxHeld)
                continue;

            const bool wasEmpty = numHeld == 0;
            held[numHeld++] = { m.mpeSourceID, (int8_t) channel, (int8_t) m.getNoteNumber(), m.getVelocity() };

            if (wasEmpty)
            {
                stepIndex = 0;
                const auto lastGridBeat = nextStepBeat - stepBeats;

                if (lastGridBeat < beat && beat - lastGridBeat < lateBeats && lastStepBeat < lastGridBeat)
                    pendingStartBeat = beat;
            }
        }
        else if (found >= 0)
        {
            for (int i = found; i < numHeld - 1; ++i)
                held[i] = held[i + 1];

            if (--numHeld == 0)
            {
                stopSounding (output, time);
                pendingStartBeat = -1.0;
            }
        }
        else
        {
            // Not one it's holding, e.g. a note started before the arpeggiator was added
            output.add (m, time);
        }
    }

    advance (output, block, block.endBeat, modeToUse, numOctaves, gateBeats);
}

} // namespace tracktion::inline engine
