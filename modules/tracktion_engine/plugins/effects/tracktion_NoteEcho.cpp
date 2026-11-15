/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2024
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \   Tracktion Software
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |       Corporation
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com

    Tracktion Engine uses a GPL/commercial licence - see LICENCE.md for details.
*/

namespace tracktion::inline engine {

const char* NoteEchoPlugin::xmlTypeName ("noteEcho");

NoteEchoPlugin::NoteEchoPlugin (PluginCreationInfo info)  : MidiEffectPlugin (info)
{
    time = addChoiceParam ("time", TRANS("Time"), 1.0f, getDivisionNames());
    repeats = addIntParam ("repeats", TRANS("Repeats"), 3.0f, { 1.0f, (float) maxRepeats },
                           [] (float v) { return juce::String (juce::roundToInt (v)); });
    decay = addFloatParam ("decay", TRANS("Decay"), 30.0f, { 0.0f, 100.0f },
                           [] (float v) { return juce::String (juce::roundToInt (v)) + "%"; });
    transpose = addIntParam ("transpose", TRANS("Transpose"), 0.0f, { -12.0f, 12.0f },
                             [] (float v)
                             {
                                 auto semitones = juce::roundToInt (v);
                                 return (semitones > 0 ? "+" : "") + juce::String (semitones) + " st";
                             });
}

NoteEchoPlugin::~NoteEchoPlugin()
{
}

void NoteEchoPlugin::resetMidiState()
{
    numPending = 0;

    for (auto& channel : inputs)
        for (auto& input : channel)
            input = {};
}

void NoteEchoPlugin::schedule (const Echo& echo)
{
    if (numPending < maxPending)
        pending[numPending++] = echo;
}

void NoteEchoPlugin::playDue (MidiMessageArray& output, const Block& block, double beforeBeat)
{
    for (;;)
    {
        // The earliest due; at the same beat note-ons go first, so one repeat
        // running into the next on the same note doesn't cut the next short
        int next = -1;

        for (int i = 0; i < numPending; ++i)
            if (pending[i].beat < beforeBeat
                 && (next < 0 || pending[i].beat < pending[next].beat
                      || (pending[i].beat == pending[next].beat && pending[i].isNoteOn && ! pending[next].isNoteOn)))
                next = i;

        if (next < 0)
            return;

        const auto echo = pending[next];
        pending[next] = pending[--numPending];
        const auto when = block.getTime (echo.beat);

        if (echo.isNoteOn)
            sendNoteOn (output, echo.channel, echo.note, echo.velocity, when, echo.source);
        else
            sendNoteOff (output, echo.channel, echo.note, when, echo.source);
    }
}

void NoteEchoPlugin::endRepeats (int channel, int note, double beat, MPESourceID source)
{
    auto& input = inputs[channel - 1][note];

    for (int k = 1; k <= maxRepeats; ++k)
        if ((input.repeats & (1 << (k - 1))) != 0)
            schedule ({ beat + k * (double) input.spacing, source, (int8_t) channel, (int8_t) (note + k * input.transpose), 0, false });

    input.repeats = 0;
}

void NoteEchoPlugin::processMidi (const MidiMessageArray& input, MidiMessageArray& output, const Block& block)
{
    const auto spacing = getDivisionBeats (getInt (*time));
    const auto numRepeats = juce::jlimit (1, maxRepeats, getInt (*repeats));
    const auto keep = 1.0 - juce::jlimit (0.0f, 100.0f, decay->getCurrentValue()) / 100.0;
    const auto step = juce::jlimit (-12, 12, getInt (*transpose));

    for (auto& m : input)
    {
        const auto when = m.getTimeStamp();
        const auto beat = block.getBeat (when);
        playDue (output, block, beat);

        const auto channel = m.getChannel();
        const bool ownsChannel = channel >= 1 && channel <= maxMidiChannel;

        if (m.isNoteOn() && ownsChannel)
        {
            const int note = m.getNoteNumber();

            // Played again before its note-off
            endRepeats (channel, note, beat, m.mpeSourceID);
            startNotes (output, m, &note, 1, m.getVelocity(), when);

            auto& started = inputs[channel - 1][note];
            started = { 0, (int8_t) step, (float) spacing };
            double velocity = m.getVelocity();

            for (int k = 1; k <= numRepeats; ++k)
            {
                velocity *= keep;

                if (juce::roundToInt (velocity) < 1)
                    break;

                const int echoNote = note + k * step;

                if (! juce::isPositiveAndBelow (echoNote, 128))
                    continue;

                // Leaves room for every note-off of the note-ons waiting
                if (numPending >= maxPending / 2)
                    break;

                schedule ({ beat + k * spacing, m.mpeSourceID, (int8_t) channel, (int8_t) echoNote,
                            (uint8_t) juce::roundToInt (velocity), true });
                started.repeats |= (uint16_t) (1 << (k - 1));
            }
        }
        else if (m.isNoteOff() && ownsChannel)
        {
            const bool wasStarted = endNotes (output, m, when);
            endRepeats (channel, m.getNoteNumber(), beat, m.mpeSourceID);

            if (! wasStarted)
                output.add (m, when);
        }
        else
        {
            output.add (m, when);
        }
    }

    playDue (output, block, block.endBeat);
}

} // namespace tracktion::inline engine
