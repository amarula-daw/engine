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
    A MIDI effect changing notes' velocities, then keeping them between a minimum
    and a maximum. The amount (-127 to 127) depends on the mode:
    - scale: multiplies them, from x0 at -127 through x1 at 0 to x2 at 127
    - add: adds it
    - fixed: plays every note at it (1 to 127)
    - compress: draws them toward the middle (64), all the way at 127; below 0 it spreads them out
*/
class VelocityPlugin   : public MidiEffectPlugin
{
public:
    VelocityPlugin (PluginCreationInfo);
    ~VelocityPlugin() override;

    //==============================================================================
    enum Mode { scale, add, fixed, compress };

    AutomatableParameter::Ptr mode, amount, minimum, maximum;

    /** A velocity after a mode with an amount, within the minimum and maximum. */
    static int apply (int velocity, int mode, int amount, int minimum, int maximum);

    //==============================================================================
    static const char* getPluginName()      { return NEEDS_TRANS("Velocity"); }
    static const char* xmlTypeName;

    juce::String getName() const override                   { return TRANS("Velocity"); }
    juce::String getPluginType() override                   { return xmlTypeName; }
    juce::String getShortName (int) override                { return TRANS("Velocity"); }
    juce::String getSelectableDescription() override        { return TRANS("Velocity Plugin"); }

protected:
    void processMidi (const MidiMessageArray&, MidiMessageArray&, const Block&) override;
    void resetMidiState() override {}

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VelocityPlugin)
};

} // namespace tracktion::inline engine
