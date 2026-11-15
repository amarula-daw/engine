/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2024
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \   Tracktion Software
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |       Corporation
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com

    Tracktion Engine uses a GPL/commercial licence - see LICENCE.md for details.
*/

namespace tracktion::inline engine {

namespace ScalePluginHelpers
{
    /** Each scale's notes as bits, from the root: bit 0 is the root, bit 1 a semitone above it… */
    constexpr uint16_t scaleMasks[] =
    {
        0b101010110101, // major: 0 2 4 5 7 9 11
        0b010110101101, // natural minor: 0 2 3 5 7 8 10
        0b100110101101, // harmonic minor: 0 2 3 5 7 8 11
        0b011010101101, // dorian: 0 2 3 5 7 9 10
        0b011010110101, // mixolydian: 0 2 4 5 7 9 10
        0b001010010101, // major pentatonic: 0 2 4 7 9
        0b010010101001, // minor pentatonic: 0 3 5 7 10
        0b010011101001, // blues: 0 3 5 6 7 10
        0b111111111111, // chromatic
    };

    constexpr int numScales = (int) std::size (scaleMasks);
}

const char* ScalePlugin::xmlTypeName ("midiScale");

ScalePlugin::ScalePlugin (PluginCreationInfo info)  : MidiEffectPlugin (info)
{
    root = addChoiceParam ("root", TRANS("Root"), 0.0f, getRootNames());
    scale = addChoiceParam ("scale", TRANS("Scale"), 0.0f, getScaleNames());
    followKey = addChoiceParam ("followKey", TRANS("Follow Key"), 1.0f, { TRANS("Off"), TRANS("On") });
}

ScalePlugin::~ScalePlugin()
{
}

juce::StringArray ScalePlugin::getScaleNames()
{
    return { "Major", "Natural Minor", "Harmonic Minor", "Dorian", "Mixolydian",
             "Major Pentatonic", "Minor Pentatonic", "Blues", "Chromatic" };
}

juce::StringArray ScalePlugin::getRootNames()
{
    return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

int ScalePlugin::snapToScale (int note, int rootNote, int scaleIndex)
{
    const auto mask = ScalePluginHelpers::scaleMasks[juce::jlimit (0, ScalePluginHelpers::numScales - 1, scaleIndex)];
    auto isInScale = [&] (int n) { return n >= 0 && n < 128 && (mask & (1 << ((n - rootNote + 120) % 12))) != 0; };

    for (int distance = 0; distance < 12; ++distance)
    {
        if (isInScale (note - distance))
            return note - distance;

        if (isInScale (note + distance))
            return note + distance;
    }

    return note;
}

void ScalePlugin::setFollowedKey (int newRoot, int newScale)
{
    followedKey = (newRoot >= 0 && newRoot < 12 && newScale >= 0 && newScale < ScalePluginHelpers::numScales) ? newRoot * 16 + newScale : -1;
}

std::pair<int, int> ScalePlugin::getFollowedKey() const
{
    const auto key = followedKey.load();
    return key < 0 ? std::pair (-1, -1) : std::pair (key / 16, key % 16);
}

void ScalePlugin::processMidi (const MidiMessageArray& input, MidiMessageArray& output, const Block&)
{
    auto rootToUse = juce::jlimit (0, 11, getInt (*root));
    auto scaleToUse = juce::jlimit (0, ScalePluginHelpers::numScales - 1, getInt (*scale));

    if (getInt (*followKey) != 0)
    {
        if (auto [followedRoot, followedScale] = getFollowedKey(); followedRoot >= 0)
        {
            rootToUse = followedRoot;
            scaleToUse = followedScale;
        }
    }

    for (auto& m : input)
    {
        const auto time = m.getTimeStamp();

        if (m.isNoteOn())
        {
            const int note = snapToScale (m.getNoteNumber(), rootToUse, scaleToUse);
            startNotes (output, m, &note, 1, m.getVelocity(), time);
        }
        else if (! (m.isNoteOff() && endNotes (output, m, time)))
        {
            output.add (m, time);
        }
    }
}

} // namespace tracktion::inline engine
