/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2024
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \   Tracktion Software
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |       Corporation
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com

    Tracktion Engine uses a GPL/commercial licence - see LICENCE.md for details.
*/

namespace tracktion::inline engine {

const char* ChordPlugin::xmlTypeName ("midiChord");

ChordPlugin::ChordPlugin (PluginCreationInfo info)  : MidiEffectPlugin (info)
{
    // A major triad to start with
    const float defaults[numIntervals] = { 4.0f, 7.0f, 0.0f, 0.0f };

    for (int i = 0; i < numIntervals; ++i)
        intervals[i] = addIntParam ("interval" + juce::String (i + 1), TRANS("Interval") + " " + juce::String (i + 1),
                                    defaults[i], { (float) -maxInterval, (float) maxInterval },
                                    [] (float v)
                                    {
                                        auto semitones = juce::roundToInt (v);
                                        return semitones == 0 ? TRANS("Off")
                                                              : (semitones > 0 ? "+" : "") + juce::String (semitones) + " st";
                                    });
}

ChordPlugin::~ChordPlugin()
{
}

void ChordPlugin::processMidi (const MidiMessageArray& input, MidiMessageArray& output, const Block&)
{
    for (auto& m : input)
    {
        const auto time = m.getTimeStamp();

        if (m.isNoteOn())
        {
            int notes[numIntervals + 1] = { m.getNoteNumber() };
            int numNotes = 1;

            for (auto& interval : intervals)
                if (auto semitones = getInt (*interval); semitones != 0)
                    notes[numNotes++] = m.getNoteNumber() + semitones;

            startNotes (output, m, notes, numNotes, m.getVelocity(), time);
        }
        else if (! (m.isNoteOff() && endNotes (output, m, time)))
        {
            output.add (m, time);
        }
    }
}

} // namespace tracktion::inline engine
