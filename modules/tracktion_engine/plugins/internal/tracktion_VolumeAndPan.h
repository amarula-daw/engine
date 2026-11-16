/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2024
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \   Tracktion Software
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |       Corporation
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com

    Tracktion Engine uses a GPL/commercial licence - see LICENCE.md for details.
*/

namespace tracktion::inline engine {

/**
    The built-in Tracktion volume/pan plugin.
*/
class VolumeAndPanPlugin  : public Plugin
{
public:
    VolumeAndPanPlugin (Edit&, const juce::ValueTree&, bool isMasterVolumeNode);
    VolumeAndPanPlugin (PluginCreationInfo, bool isMasterVolumeNode = false);
    ~VolumeAndPanPlugin() override;

    using Ptr = juce::ReferenceCountedObjectPtr<VolumeAndPanPlugin>;

    static const char* getPluginName()                      { return NEEDS_TRANS("Volume and Pan"); }
    static juce::ValueTree create();

    bool isMasterVolAndPan()                                { return isMasterVolume; }
    bool canBeAddedToRack() override                        { return ! isMasterVolume; }
    bool canBeMoved() override                              { return ! isMasterVolume; }

    //==============================================================================
    float getVolumeDb() const;
    float getSliderPos() const                              { return volParam->getCurrentValue(); }
    void setVolumeDb (float vol);
    void setSliderPos (float pos);

    float getPan() const                                    { return panParam->getCurrentValue(); }
    void setPan (float pan);

    void setPanLaw (PanLaw newPanLaw)                       { panLaw = (int) newPanLaw; }
    PanLaw getPanLaw() const noexcept;

    /** A gain in dB before the fader, from -24 to +24, for setting a level without moving the fader or its automation. */
    float getTrimDb() const                                 { return trimParam->getCurrentValue(); }
    void setTrimDb (float db);

    /** Stereo width: 0 is mono, 1 leaves it as it is, 2 doubles the difference between the sides. */
    float getWidth() const                                  { return widthParam->getCurrentValue(); }
    void setWidth (float newWidth);

    void setAppliedToMidiVolumes (bool);
    bool isAppliedToMidiVolumes() const                     { return applyToMidi; }

    void muteOrUnmute();

    /** Silences the channel (ramping, like a fader move) without moving its fader, undoably and saved with it. */
    bool isMuted() const noexcept                           { return muted.get(); }
    void setMuted (bool shouldBeMuted)                      { muted = shouldBeMuted; }

    bool shouldMeasureCpuUsage() const noexcept final       { return false; }

    //==============================================================================
    static const char* xmlTypeName;

    juce::String getName() const override                   { return TRANS("Volume & Pan Plugin"); }
    juce::String getPluginType() override                   { return xmlTypeName; }
    juce::String getShortName (int) override                { return "VolPan"; }
    juce::String getSelectableDescription() override        { return getName(); }

    void initialise (const PluginInitialisationInfo&) override;
    void initialiseWithoutStopping (const PluginInitialisationInfo&) override;
    void deinitialise() override;
    void applyToBuffer (const PluginRenderContext&) override;
    int getNumOutputChannelsGivenInputs (int numInputs) override    { return numInputs; }
    BusLayout getBusses() const override   { return BusLayout::singleInOut (ChannelConfiguration::stereo(), ChannelConfiguration::none()); }

    void restorePluginStateFromValueTree (const juce::ValueTree&) override;

    //==============================================================================
    juce::CachedValue<float> volume, pan, trim, width;
    juce::CachedValue<bool> applyToMidi, ignoreVca, polarity, mono, muted;
    juce::CachedValue<int> panLaw;

    // NB the units used here are slider position
    AutomatableParameter::Ptr volParam, panParam;
    // In dB, and as a width (see getWidth)
    AutomatableParameter::Ptr trimParam, widthParam;

    /// This is the time over which volume changes will be ramped, to avoid
    /// zipper noise.
    double smoothingRampTimeSeconds = 0.015;

private:
    float lastVolumeBeforeMute = 0.0f;
    juce::SmoothedValue<float> smoothedGainL, smoothedGainR, smoothedGain, smoothedWidth;

    RealTimeSpinLock vcaTrackLock;
    juce::ReferenceCountedObjectPtr<AudioTrack> vcaTrack;
    const bool isMasterVolume = false;

    void setSmoothedValueTargets (TimePosition, bool);
    void applyWidth (juce::AudioBuffer<float>&, int startSample, int numSamples);
    void refreshVCATrack();
    float getVCAPosDelta (TimePosition);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VolumeAndPanPlugin)
};

} // namespace tracktion::inline engine
