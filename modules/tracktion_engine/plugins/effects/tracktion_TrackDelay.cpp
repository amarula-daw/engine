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
    reportedLatency = getLatencySeconds();

    // The latency it reports changes the graph, so it's rebuilt when that changes. Only an earlier
    // shift is latency; a later one just reads further back in the line.
    playbackRestartTimer.setCallback ([this]
                                      {
                                          playbackRestartTimer.stopTimer();

                                          if (auto latency = getLatencySeconds(); latency != reportedLatency)
                                          {
                                              reportedLatency = latency;
                                              edit.restartPlayback();
                                          }
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
    currentDelaySamples = juce::roundToInt (std::max (0.0, delayMs.get()) / 1000.0 * sampleRate);
}

void TrackDelayPlugin::applyToBuffer (const PluginRenderContext& rc)
{
    if (rc.destBuffer == nullptr || delayLine.getNumSamples() == 0)
        return;

    // The line is always written, so a delay changed reads what really came before; after being
    // bypassed it starts again from silence
    if (clearLine.exchange (false))
    {
        delayLine.clear();
        currentDelaySamples = juce::roundToInt (std::max (0.0, delayMs.get()) / 1000.0 * sampleRate);
    }

    const int size = delayLine.getNumSamples();
    const int target = std::min (juce::roundToInt (std::max (0.0, delayMs.get()) / 1000.0 * sampleRate), size - 1);
    const int previous = std::min (currentDelaySamples, size - 1);
    const int numChannels = std::min (rc.destBuffer->getNumChannels(), delayLine.getNumChannels());
    int position = writePosition;

    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto* samples = rc.destBuffer->getWritePointer (channel, rc.bufferStartSample);
        auto* line = delayLine.getWritePointer (channel);
        position = writePosition;

        auto read = [&] (int delay)
        {
            auto readPosition = position - delay;
            return line[readPosition < 0 ? readPosition + size : readPosition];
        };

        for (int i = 0; i < rc.bufferNumSamples; ++i)
        {
            line[position] = samples[i];

            if (target == previous)
            {
                samples[i] = read (target);
            }
            else
            {
                // A new delay crossfades from the old one over the block, so it doesn't click
                auto amount = (float) (i + 1) / (float) rc.bufferNumSamples;
                samples[i] = read (previous) * (1.0f - amount) + read (target) * amount;
            }

            if (++position == size)
                position = 0;
        }
    }

    writePosition = position;
    currentDelaySamples = target;
}

void TrackDelayPlugin::restorePluginStateFromValueTree (const juce::ValueTree& v)
{
    if (v.hasProperty (IDs::delay))
        delayMs = v[IDs::delay];
}

void TrackDelayPlugin::valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& i)
{
    Plugin::valueTreePropertyChanged (v, i);

    if (v == state && (i == IDs::delay || i == IDs::enabled))
    {
        delayMs.forceUpdateOfCachedValue();

        if (i == IDs::enabled)
            clearLine = true;

        playbackRestartTimer.startTimer (50);
    }
}

} // namespace tracktion::inline engine
