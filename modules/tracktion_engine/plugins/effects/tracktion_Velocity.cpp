/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2024
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \   Tracktion Software
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |       Corporation
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com

    Tracktion Engine uses a GPL/commercial licence - see LICENCE.md for details.
*/

namespace tracktion::inline engine {

const char* VelocityPlugin::xmlTypeName ("midiVelocity");

VelocityPlugin::VelocityPlugin (PluginCreationInfo info)  : MidiEffectPlugin (info)
{
    mode = addChoiceParam ("mode", TRANS("Mode"), (float) scale, { TRANS("Scale"), TRANS("Add"), TRANS("Fixed"), TRANS("Compress") });

    amount = addIntParam ("amount", TRANS("Amount"), 0.0f, { -127.0f, 127.0f }, [this] (float v)
    {
        const auto value = juce::roundToInt (v);

        switch (mode != nullptr ? juce::roundToInt (mode->getCurrentValue()) : (int) scale)
        {
            case add:       return (value > 0 ? "+" : "") + juce::String (value);
            case fixed:     return juce::String (juce::jlimit (1, 127, value));
            case compress:  return value >= 0 ? TRANS("Compress") + " " + juce::String (juce::roundToInt (value * 100.0 / 127.0)) + "%"
                                              : TRANS("Expand") + " " + juce::String (juce::roundToInt (-value * 100.0 / 127.0)) + "%";
            case scale:
            default:        return juce::String::charToString (0xd7) + juce::String (1.0 + value / 127.0, 2);
        }
    });

    auto showVelocity = [] (float v) { return juce::String (juce::roundToInt (v)); };
    minimum = addIntParam ("min", TRANS("Minimum"), 1.0f, { 1.0f, 127.0f }, showVelocity);
    maximum = addIntParam ("max", TRANS("Maximum"), 127.0f, { 1.0f, 127.0f }, showVelocity);
}

VelocityPlugin::~VelocityPlugin()
{
}

int VelocityPlugin::apply (int velocity, int modeToUse, int amountToUse, int minimumToUse, int maximumToUse)
{
    double v = velocity;

    switch (modeToUse)
    {
        case add:       v = velocity + amountToUse; break;
        case fixed:     v = amountToUse; break;
        case compress:  v = 64.0 + (velocity - 64.0) * (1.0 - amountToUse / 127.0); break;
        case scale:
        default:        v = velocity * (1.0 + amountToUse / 127.0); break;
    }

    const auto low = std::max (1, std::min (minimumToUse, maximumToUse));
    const auto high = std::min (127, std::max (minimumToUse, maximumToUse));
    return juce::jlimit (low, high, juce::roundToInt (v));
}

void VelocityPlugin::processMidi (const MidiMessageArray& input, MidiMessageArray& output, const Block&)
{
    const auto modeToUse = getInt (*mode);
    const auto amountToUse = getInt (*amount);
    const auto minimumToUse = getInt (*minimum);
    const auto maximumToUse = getInt (*maximum);

    for (auto& m : input)
    {
        const auto time = m.getTimeStamp();

        if (m.isNoteOn())
        {
            const int note = m.getNoteNumber();
            startNotes (output, m, &note, 1, apply (m.getVelocity(), modeToUse, amountToUse, minimumToUse, maximumToUse), time);
        }
        else if (! (m.isNoteOff() && endNotes (output, m, time)))
        {
            output.add (m, time);
        }
    }
}

} // namespace tracktion::inline engine
