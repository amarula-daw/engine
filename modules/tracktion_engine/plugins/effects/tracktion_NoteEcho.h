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
    A MIDI effect repeating notes: each note plays, then again every time (a
    tempo-synced division) for a number of repeats, each as long as the note,
    quieter by the decay and moved by the transpose each time.
*/
class NoteEchoPlugin   : public MidiEffectPlugin
{
public:
    NoteEchoPlugin (PluginCreationInfo);
    ~NoteEchoPlugin() override;

    //==============================================================================
    static constexpr int maxRepeats = 8;

    AutomatableParameter::Ptr time, repeats, decay, transpose;

    //==============================================================================
    static const char* getPluginName()      { return NEEDS_TRANS("Note Echo"); }
    static const char* xmlTypeName;

    juce::String getName() const override                   { return TRANS("Note Echo"); }
    juce::String getPluginType() override                   { return xmlTypeName; }
    juce::String getShortName (int) override                { return TRANS("Echo"); }
    juce::String getSelectableDescription() override        { return TRANS("Note Echo Plugin"); }

protected:
    void processMidi (const MidiMessageArray&, MidiMessageArray&, const Block&) override;
    void resetMidiState() override;

private:
    /** A repeat's note-on or note-off, waiting for its beat. */
    struct Echo
    {
        double beat = 0.0;
        MPESourceID source = {};
        int8_t channel = 1, note = 0;
        uint8_t velocity = 0;
        bool isNoteOn = false;
    };

    /** The repeats an input note started, so its note-off can end them. */
    struct Input
    {
        uint16_t repeats = 0; // bit k for the repeat k + 1, if its note-on was scheduled
        int8_t transpose = 0;
        float spacing = 0.0f; // beats
    };

    static constexpr int maxPending = 512;
    Echo pending[maxPending];
    int numPending = 0;
    Input inputs[maxMidiChannel][128];

    void schedule (const Echo&);
    void playDue (MidiMessageArray&, const Block&, double beforeBeat);
    void endRepeats (int channel, int note, double beat, MPESourceID);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoteEchoPlugin)
};

} // namespace tracktion::inline engine
