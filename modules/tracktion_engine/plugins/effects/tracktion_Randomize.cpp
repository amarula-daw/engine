/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2024
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \   Tracktion Software
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |       Corporation
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com

    Tracktion Engine uses a GPL/commercial licence - see LICENCE.md for details.
*/

namespace tracktion::inline engine {

const char* RandomizePlugin::xmlTypeName ("midiRandomize");

RandomizePlugin::RandomizePlugin (PluginCreationInfo info)  : MidiEffectPlugin (info)
{
    chance = addFloatParam ("chance", TRANS("Chance"), 100.0f, { 0.0f, 100.0f },
                            [] (float v) { return juce::String (juce::roundToInt (v)) + "%"; });
    velocitySpread = addIntParam ("velocity", TRANS("Velocity Spread"), 16.0f, { 0.0f, 64.0f },
                                  [] (float v) { return juce::String::charToString (0xb1) + juce::String (juce::roundToInt (v)); });
    pitchSpread = addIntParam ("pitch", TRANS("Pitch Spread"), 0.0f, { 0.0f, 12.0f },
                               [] (float v) { return juce::String::charToString (0xb1) + juce::String (juce::roundToInt (v)) + " st"; });
}

RandomizePlugin::~RandomizePlugin()
{
}

void RandomizePlugin::processMidi (const MidiMessageArray& input, MidiMessageArray& output, const Block&)
{
    const auto chanceToUse = chance->getCurrentValue();
    const auto velocityToUse = juce::jlimit (0, 64, getInt (*velocitySpread));
    const auto pitchToUse = juce::jlimit (0, 12, getInt (*pitchSpread));

    for (auto& m : input)
    {
        const auto time = m.getTimeStamp();

        if (m.isNoteOn())
        {
            if (random.nextFloat() * 100.0f >= chanceToUse)
            {
                // Dropped, but remembered so its note-off is dropped too
                startNotes (output, m, nullptr, 0, 0, time);
                continue;
            }

            const int note = m.getNoteNumber() + random.nextInt (pitchToUse * 2 + 1) - pitchToUse;
            const int velocity = m.getVelocity() + random.nextInt (velocityToUse * 2 + 1) - velocityToUse;
            const int clampedNote = juce::jlimit (0, 127, note);
            startNotes (output, m, &clampedNote, 1, velocity, time);
        }
        else if (! (m.isNoteOff() && endNotes (output, m, time)))
        {
            output.add (m, time);
        }
    }
}

} // namespace tracktion::inline engine
