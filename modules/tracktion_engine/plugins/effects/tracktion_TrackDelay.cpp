/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2024
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \   Tracktion Software
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |       Corporation
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com

    Tracktion Engine uses a GPL/commercial licence - see LICENCE.md for details.
*/

namespace tracktion::inline engine {

const char* TrackDelayPlugin::xmlTypeName ("trackDelay");

TrackDelayPlugin::TrackDelayPlugin (PluginCreationInfo info)
    : Plugin (info)
{
    delayMs.setConstrainer ([] (double in) { return juce::jlimit (-maxDelayMs, maxDelayMs, in); });
    delayMs.referTo (state, IDs::delay, getUndoManager(), 0.0);

    // The latency it reports changes the graph, so it's rebuilt for a new delay
    playbackRestartTimer.setCallback ([this]
                                      {
                                          playbackRestartTimer.stopTimer();
                                          edit.restartPlayback();
                                      });
}

TrackDelayPlugin::~TrackDelayPlugin()
{
    notifyListenersOfDeletion();
}

juce::ValueTree TrackDelayPlugin::create()
{
    return createValueTree (IDs::PLUGIN,
                            IDs::type, xmlTypeName);
}

void TrackDelayPlugin::initialise (const PluginInitialisationInfo& info)
{
    const int maxDelaySamples = (int) std::ceil (maxDelayMs / 1000.0 * info.sampleRate);
    delayLine.setSize (2, maxDelaySamples + info.blockSizeSamples + 1);
    reset();
}

void TrackDelayPlugin::deinitialise()
{
    delayLine.setSize (0, 0);
}

void TrackDelayPlugin::reset()
{
    delayLine.clear();
    writePosition = 0;
}

void TrackDelayPlugin::applyToBuffer (const PluginRenderContext& rc)
{
    if (rc.destBuffer == nullptr || delayLine.getNumSamples() == 0)
        return;

    const int delaySamples = juce::roundToInt (std::max (0.0, delayMs.get()) / 1000.0 * sampleRate);

    if (delaySamples <= 0)
        return;

    const int size = delayLine.getNumSamples();
    const int numChannels = std::min (rc.destBuffer->getNumChannels(), delayLine.getNumChannels());
    int position = writePosition;

    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto* samples = rc.destBuffer->getWritePointer (channel, rc.bufferStartSample);
        auto* line = delayLine.getWritePointer (channel);
        position = writePosition;

        for (int i = 0; i < rc.bufferNumSamples; ++i)
        {
            line[position] = samples[i];

            auto readPosition = position - std::min (delaySamples, size - 1);

            if (readPosition < 0)
                readPosition += size;

            samples[i] = line[readPosition];

            if (++position == size)
                position = 0;
        }
    }

    writePosition = position;
}

void TrackDelayPlugin::restorePluginStateFromValueTree (const juce::ValueTree& v)
{
    if (v.hasProperty (IDs::delay))
        delayMs = v[IDs::delay];
}

void TrackDelayPlugin::valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& i)
{
    if (v == state && i == IDs::delay)
    {
        delayMs.forceUpdateOfCachedValue();
        playbackRestartTimer.startTimer (50);
    }

    Plugin::valueTreePropertyChanged (v, i);
}

} // namespace tracktion::inline engine
