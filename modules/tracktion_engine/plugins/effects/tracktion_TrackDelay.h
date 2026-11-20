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
    Shifts a track earlier or later in time, e.g. to line up a part recorded
    late or a sound with a slow attack.

    A later track is delayed by a delay line. An earlier one isn't: the plugin
    reports the shift as latency, so the graph's delay compensation holds the
    rest of the Edit back by that much instead.
*/
class TrackDelayPlugin  : public Plugin
{
public:
    TrackDelayPlugin (PluginCreationInfo);
    ~TrackDelayPlugin() override;

    //==============================================================================
    static const char* getPluginName()                      { return NEEDS_TRANS("Track Delay"); }
    static const char* xmlTypeName;
    static juce::ValueTree create();

    /** The furthest a track can be shifted either way. */
    static constexpr double maxDelayMs = 500.0;

    juce::String getName() const override                   { return getPluginName(); }
    juce::String getPluginType() override                   { return xmlTypeName; }
    juce::String getSelectableDescription() override        { return getName(); }
    BusLayout getBusses() const override                    { return BusLayout::singleStereoInOut(); }
    bool canBeAddedToClip() override                        { return false; }
    bool canBeAddedToRack() override                        { return false; }

    void initialise (const PluginInitialisationInfo&) override;
    void deinitialise() override;
    void reset() override;
    void applyToBuffer (const PluginRenderContext&) override;

    /** How far an earlier track is shifted, as latency for the rest of the Edit to wait for. */
    double getLatencySeconds() override                     { return isEnabled() ? std::max (0.0, -delayMs.get()) / 1000.0 : 0.0; }

    void restorePluginStateFromValueTree (const juce::ValueTree&) override;

    //==============================================================================
    /** Milliseconds: positive plays the track later, negative earlier. */
    ConstrainedCachedValue<double> delayMs;

private:
    juce::AudioBuffer<float> delayLine;
    int writePosition = 0, currentDelaySamples = 0;
    std::atomic<bool> clearLine { false };
    double reportedLatency = 0.0;
    LambdaTimer playbackRestartTimer;

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackDelayPlugin)
};

} // namespace tracktion::inline engine
