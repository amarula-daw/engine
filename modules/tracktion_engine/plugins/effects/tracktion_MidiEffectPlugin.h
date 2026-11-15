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
    The base of the MIDI effects (arpeggiator, chord, scale, note echo, randomize,
    velocity): plugins that sit before an instrument and change the notes it gets.

    They take MIDI and pass audio through untouched. The base splits each block's
    messages out, works out where the block lies in beats (from the Edit's tempo
    sequence while playing, or a free-running clock while stopped, so live playing
    still works), and keeps count of the notes the effect has sent, so that when
    playback stops, loops or jumps, or the input says all notes off, everything it
    started gets its note-off and it starts again from nothing.

    Nothing here allocates or locks on the audio thread.
*/
class MidiEffectPlugin   : public Plugin
{
public:
    MidiEffectPlugin (PluginCreationInfo);
    ~MidiEffectPlugin() override;

    //==============================================================================
    BusLayout getBusses() const override                        { return {}; }
    bool takesMidiInput() override                              { return true; }
    bool takesAudioInput() override                             { return false; }
    bool producesAudioWhenNoAudioInput() override               { return false; }
    int getNumOutputChannelsGivenInputs (int) override          { return 0; }
    void getChannelNames (juce::StringArray*, juce::StringArray*) override {}
    double getLatencySeconds() override                         { return 0.0; }
    bool canBeAddedToClip() override                            { return false; }
    bool canBeAddedToMaster() override                          { return false; }
    bool canBeAddedToFolderTrack() override                     { return false; }

    void initialise (const PluginInitialisationInfo&) override;
    void deinitialise() override;
    void applyToBuffer (const PluginRenderContext&) override;
    void restorePluginStateFromValueTree (const juce::ValueTree&) override;

    //==============================================================================
    /** The note lengths tempo-synced effects step in: 1/4 to 1/32 and their triplets. */
    static juce::StringArray getDivisionNames();
    /** A division's length in beats (quarter notes). */
    static double getDivisionBeats (int division);

protected:
    //==============================================================================
    /** Where a block lies in time. Times are seconds from the block's start. */
    struct Block
    {
        double numSeconds = 0.0;
        double startBeat = 0.0, endBeat = 0.0;
        bool playing = false;

        /** The beat at a time in the block. */
        double getBeat (double seconds) const noexcept;
        /** The time in the block of a beat, clamped to it. */
        double getTime (double beat) const noexcept;
    };

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;

    /** Processes a block: every input message, in time order, adding what it plays to the output. */
    virtual void processMidi (const MidiMessageArray& input, MidiMessageArray& output, const Block&) = 0;

    /** Forgets held notes and anything pending. Called on the audio thread; the base has already ended the notes sent. */
    virtual void resetMidiState() = 0;

    //==============================================================================
    /** Sends a note-on, counting it. A note already sounding is ended first, so it retriggers. */
    void sendNoteOn (MidiMessageArray&, int channel, int note, int velocity, double time, MPESourceID);
    /** Sends a note's note-off once every note-on it was sent is over; ignored for notes it never sent. */
    void sendNoteOff (MidiMessageArray&, int channel, int note, double time, MPESourceID);
    /** Ends every note sent. */
    void releaseAll (MidiMessageArray&, double time);

    /** The most notes one input note can become. */
    static constexpr int maxNotesPerInput = 5;

    /** Plays an input note-on as some notes (none drops it), remembering them so its note-off ends the same ones. */
    void startNotes (MidiMessageArray&, const MidiMessageWithSource& noteOn, const int* notes, int numNotes,
                     int velocity, double time);
    /** Ends what an input note-off's note started; false if it started nothing, so isn't this effect's to end. */
    bool endNotes (MidiMessageArray&, const MidiMessageWithSource& noteOff, double time);

    /*  Parameters are kept in the plugin's state under their ids, so they save, undo (with the
        Edit's UndoManager) and automate like the other built-in plugins' parameters.
    */
    /** Adds a choice parameter, its value the choice's index. */
    AutomatableParameter* addChoiceParam (const juce::String& paramID, const juce::String& name,
                                          float defaultValue, const juce::StringArray& choices);
    /** Adds a parameter in whole steps of one, shown with valueToString. */
    AutomatableParameter* addIntParam (const juce::String& paramID, const juce::String& name, float defaultValue,
                                       juce::Range<float> range, std::function<juce::String (float)> valueToString);
    /** Adds a continuous parameter. */
    AutomatableParameter* addFloatParam (const juce::String& paramID, const juce::String& name, float defaultValue,
                                         juce::Range<float> range, std::function<juce::String (float)> valueToString);

    /** A parameter's current value (automation and modulation included) as a whole number. */
    static int getInt (AutomatableParameter&);

    static constexpr int maxMidiChannel = 16;

private:
    //==============================================================================
    juce::OwnedArray<juce::CachedValue<float>> values;
    MidiMessageArray input;
    uint8_t soundingCounts[maxMidiChannel][128] = {};
    int numSounding = 0;

    struct Started
    {
        int8_t notes[maxNotesPerInput] = {};
        uint8_t numNotes = 0;
        bool held = false;
    };

    Started started[maxMidiChannel][128];

    tempo::Sequence::Position tempoPosition { createPosition (edit.tempoSequence) };
    double lastEditEnd = 0.0, freeBeat = 0.0;
    bool wasPlaying = false;

    void releaseAndReset (MidiMessageArray&);
    void forgetStarted();
    AutomatableParameter* addParameter (AutomatableParameter*, float defaultValue);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiEffectPlugin)
};

} // namespace tracktion::inline engine
