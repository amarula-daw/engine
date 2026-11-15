/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2024
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \   Tracktion Software
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |       Corporation
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com

    Tracktion Engine uses a GPL/commercial licence - see LICENCE.md for details.
*/

namespace tracktion::inline engine {

class SamplerPlugin  : public Plugin,
                       private juce::AsyncUpdater
{
public:
    SamplerPlugin (PluginCreationInfo);
    ~SamplerPlugin() override;

    //==============================================================================
    int getNumSounds() const;
    juce::String getSoundName (int index) const;
    void setSoundName (int index, const juce::String& name);
    AudioFile getSoundFile (int index) const;
    juce::String getSoundMedia (int index) const;
    int getKeyNote (int index) const;
    int getMinKey (int index) const;
    int getMaxKey (int index) const;
    float getSoundGainDb (int index) const;
    float getSoundPan (int index) const;
    bool isSoundOpenEnded (int index) const;
    int getSoundChokeGroup (int index) const;
    double getSoundAttack (int index) const;
    double getSoundRelease (int index) const;
    double getSoundStartTime (int index) const;
    double getSoundLength (int index) const;
    void setSoundExcerpt (int index, double start, double length);

    // returns an error
    juce::String addSound (const juce::String& sourcePathOrProjectID, const juce::String& name,
                           double startTime, double length, float gainDb);
    void removeSound (int index);
    void setSoundParams (int index, int keyNote, int minNote, int maxNote);
    void setSoundGains (int index, float gainDb, float pan);
    void setSoundOpenEnded (int index, bool isOpenEnded);

    /** Puts a sound in a choke group (1 to maxChokeGroup), or none (0).
        Starting a sound quickly fades out any other sound playing in its group,
        like an open hi-hat cut off by a closed one.
    */
    void setSoundChokeGroup (int index, int chokeGroup);

    /** Sets how long a sound takes to fade in when it starts, and to fade out
        once its note ends (sounds that aren't open-ended only), in seconds.
        A release of 0 stops the sound almost at once, as it always has.
    */
    void setSoundEnvelope (int index, double attackSeconds, double releaseSeconds);

    static constexpr int maxChokeGroup = 16;
    static constexpr double maxAttackSeconds = 2.0;
    static constexpr double maxReleaseSeconds = 5.0;

    void setSoundMedia (int index, const juce::String& sourcePathOrProjectID);

    void playNotes (const juce::BigInteger& keysDown);
    void allNotesOff();

    /// If the state has changed and an update is pending, do it synchronously now
    void flushPendingUpdates();

    //==============================================================================
    static const char* getPluginName()                  { return NEEDS_TRANS("Sampler"); }
    static const char* xmlTypeName;

    juce::String getName() const override               { return TRANS("Sampler"); }
    juce::String getPluginType() override               { return xmlTypeName; }
    juce::String getShortName (int) override            { return "Smplr"; }
    juce::String getSelectableDescription() override    { return TRANS("Sampler"); }
    bool isSynth() override                             { return true; }

    int getNumOutputChannelsGivenInputs (int) override                 { return 2; }
    BusLayout getBusses() const override   { return BusLayout::singleStereoInOut(); }
    void initialise (const PluginInitialisationInfo&) override;
    void deinitialise() override;
    void applyToBuffer (const PluginRenderContext&) override;
    void midiPanic() override                           { allNotesOff(); }

    //==============================================================================
    bool takesMidiInput() override                      { return true; }
    bool takesAudioInput() override                     { return true; }
    bool producesAudioWhenNoAudioInput() override       { return true; }
    bool hasNameForMidiNoteNumber (int note, int midiChannel, juce::String& name) override;

    juce::Array<ReferencedItem> getReferencedItems() override;
    void reassignReferencedItem (const ReferencedItem&, ProjectItemRef newRef, double newStartTime) override;
    void sourceMediaChanged() override;

    void restorePluginStateFromValueTree (const juce::ValueTree&) override;

    //==============================================================================
    struct SamplerSound
    {
        SamplerSound (SamplerPlugin&, const juce::String& sourcePathOrProjectID, const juce::String& name,
                      double startTime, double length, float gainDb);

        void setExcerpt (double startTime, double length);
        void refreshFile();

        SamplerPlugin& owner;
        juce::String source;
        juce::String name;
        int keyNote = -1, minNote = 0, maxNote = 0;
        int fileStartSample = 0, fileLengthSamples = 0;
        bool openEnded = false;
        int chokeGroup = 0;
        double attackSeconds = 0, releaseSeconds = 0;
        float gainDb = 0, pan = 0;
        double startTime = 0, length = 0;
        AudioFile audioFile;
        juce::AudioBuffer<float> audioData { 2, 64 };

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplerSound)
    };

private:
    //==============================================================================
    struct SampledNote;

    juce::Colour colour;
    juce::CriticalSection lock;
    juce::ReferenceCountedArray<SampledNote> playingNotes;
    juce::OwnedArray<SamplerSound> soundList;
    juce::BigInteger highlightedNotes;

    juce::ValueTree getSound (int index) const;
    void chokeOtherSounds (const SamplerSound&, int sampleInBlock);

    void valueTreeChanged() override;
    void handleAsyncUpdate() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplerPlugin)
};

} // namespace tracktion::inline engine
