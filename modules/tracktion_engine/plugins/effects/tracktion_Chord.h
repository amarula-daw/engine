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
    A MIDI effect playing each note as a chord: the note itself plus up to four
    intervals above or below it, in semitones (0 adds nothing).
*/
class ChordPlugin   : public MidiEffectPlugin
{
public:
    ChordPlugin (PluginCreationInfo);
    ~ChordPlugin() override;

    //==============================================================================
    static constexpr int numIntervals = 4;
    static constexpr int maxInterval = 24;

    AutomatableParameter::Ptr intervals[numIntervals];

    //==============================================================================
    static const char* getPluginName()      { return NEEDS_TRANS("Chord"); }
    static const char* xmlTypeName;

    juce::String getName() const override                   { return TRANS("Chord"); }
    juce::String getPluginType() override                   { return xmlTypeName; }
    juce::String getShortName (int) override                { return TRANS("Chord"); }
    juce::String getSelectableDescription() override        { return TRANS("Chord Plugin"); }

protected:
    void processMidi (const MidiMessageArray&, MidiMessageArray&, const Block&) override;
    void resetMidiState() override {}

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordPlugin)
};

} // namespace tracktion::inline engine
