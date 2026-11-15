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
    A MIDI effect playing the held notes one at a time, in steps of a tempo-synced
    rate on the beat grid: up, down, up and down, in the order played, or at random,
    over one to four octaves, each step sounding for the gate's share of it.
*/
class ArpeggiatorPlugin   : public MidiEffectPlugin
{
public:
    ArpeggiatorPlugin (PluginCreationInfo);
    ~ArpeggiatorPlugin() override;

    //==============================================================================
    enum Mode { up, down, upDown, asPlayed, randomOrder };

    static constexpr int maxOctaves = 4;
    static constexpr int maxHeld = 32;

    AutomatableParameter::Ptr rate, mode, octaves, gate;

    //==============================================================================
    static const char* getPluginName()      { return NEEDS_TRANS("Arpeggiator"); }
    static const char* xmlTypeName;

    juce::String getName() const override                   { return TRANS("Arpeggiator"); }
    juce::String getPluginType() override                   { return xmlTypeName; }
    juce::String getShortName (int) override                { return TRANS("Arp"); }
    juce::String getSelectableDescription() override        { return TRANS("Arpeggiator Plugin"); }

protected:
    void processMidi (const MidiMessageArray&, MidiMessageArray&, const Block&) override;
    void resetMidiState() override;

private:
    struct Held
    {
        MPESourceID source = {};
        int8_t channel = 1, note = 0;
        uint8_t velocity = 0;
    };

    struct Step
    {
        int8_t note = 0;
        int8_t held = 0; // which held note it's from
    };

    Held held[maxHeld];
    int numHeld = 0;

    Step pattern[maxHeld * maxOctaves * 2];
    int patternLength = 0;

    juce::Random random;
    Held sounding;
    bool isSounding = false;
    double gateEndBeat = 0.0, nextStepBeat = -1.0, lastStepBeat = -1.0e9, pendingStartBeat = -1.0, stepBeats = 0.0;
    int stepIndex = 0;

    void buildPattern (int mode, int numOctaves);
    void playStep (MidiMessageArray&, const Block&, double beat, int mode, int numOctaves, double gateBeats);
    void advance (MidiMessageArray&, const Block&, double beforeBeat, int mode, int numOctaves, double gateBeats);
    void stopSounding (MidiMessageArray&, double time);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArpeggiatorPlugin)
};

} // namespace tracktion::inline engine
