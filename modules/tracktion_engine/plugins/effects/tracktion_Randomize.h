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
/**
    A MIDI effect adding chance to notes: whether each plays at all, and how far
    its velocity and pitch stray, at random within the spreads.
*/
class RandomizePlugin   : public MidiEffectPlugin
{
public:
    RandomizePlugin (PluginCreationInfo);
    ~RandomizePlugin() override;

    //==============================================================================
    AutomatableParameter::Ptr chance, velocitySpread, pitchSpread;

    //==============================================================================
    static const char* getPluginName()      { return NEEDS_TRANS("Randomize"); }
    static const char* xmlTypeName;

    juce::String getName() const override                   { return TRANS("Randomize"); }
    juce::String getPluginType() override                   { return xmlTypeName; }
    juce::String getShortName (int) override                { return TRANS("Randomize"); }
    juce::String getSelectableDescription() override        { return TRANS("Randomize Plugin"); }

protected:
    void processMidi (const MidiMessageArray&, MidiMessageArray&, const Block&) override;
    void resetMidiState() override {}

private:
    juce::Random random;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RandomizePlugin)
};

} // namespace tracktion::inline engine
