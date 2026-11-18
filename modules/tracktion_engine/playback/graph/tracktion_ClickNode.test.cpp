/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2024
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \   Tracktion Software
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |       Corporation
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com

    Tracktion Engine uses a GPL/commercial licence - see LICENCE.md for details.
*/

#if TRACKTION_UNIT_TESTS && ENGINE_UNIT_TESTS_CLICKNODE

#include <tracktion_engine/../3rd_party/doctest/tracktion_doctest.hpp>
#include <tracktion_engine/testing/tracktion_EnginePlayer.h>
#include <tracktion_engine/utilities/tracktion_TestUtilities.h>

namespace tracktion::inline engine
{
    TEST_SUITE ("tracktion_engine")
    {
        TEST_CASE("ClickNode")
        {
            auto& engine = *Engine::getEngines()[0];

            for (auto srbs : std::array<SampleRateAndBlockSize, 4>{{ { 44100.0, 2048 },
                                                                     { 44100.0, 512 },
                                                                     { 44100.0, 64 },
                                                                     { 96000.0, 2048 } }})
            {
                HostedAudioDeviceInterface::Parameters params { .sampleRate = srbs.sampleRate, .blockSize = srbs.blockSize,
                                                                .inputChannels = 0, .outputChannels = 1,
                                                                .inputNames = {}, .outputNames = {} };
                test_utilities::EnginePlayer player (engine, params);

                auto edit = engine::test_utilities::createTestEdit (engine, 1, Edit::EditRole::forEditing);
                auto& tc = edit->getTransport();

                edit->clickTrackEnabled = true;
                edit->setClickTrackVolume (1.0f);
                CHECK_EQ (edit->clickTrackGain.get(), 1.0f);
                tc.play (false);

                const auto totalNumSamples = toSamples (60_td, params.sampleRate);
                auto resultBuffer = player.process (totalNumSamples);

                auto f = graph::test_utilities::writeToTemporaryFile<juce::WavAudioFormat> (toBufferView (resultBuffer), params.sampleRate);

                CHECK_GT (resultBuffer.getRMSLevel (0, 0, resultBuffer.getNumSamples()), 0.01f);
                CHECK_GT (resultBuffer.getMagnitude (0, 0, resultBuffer.getNumSamples()), 0.5f);
            }
        }

        TEST_CASE("ClickNode subdivisions")
        {
            auto& engine = *Engine::getEngines()[0];

            // Counts the clicks: each one starts after a stretch of silence
            auto findClicks = [] (const juce::AudioBuffer<float>& buffer, double sampleRate)
            {
                const auto minGap = static_cast<int> (sampleRate * 0.03);
                std::vector<std::pair<int, float>> clicks; // start and peak
                int silentFor = minGap;

                for (int i = 0; i < buffer.getNumSamples(); ++i)
                {
                    const auto level = std::abs (buffer.getSample (0, i));

                    if (level < 1.0e-4f)
                    {
                        ++silentFor;
                        continue;
                    }

                    if (silentFor >= minGap)
                        clicks.emplace_back (i, 0.0f);

                    if (! clicks.empty())
                        clicks.back().second = std::max (clicks.back().second, level);

                    silentFor = 0;
                }

                return clicks;
            };

            for (auto srbs : std::array<SampleRateAndBlockSize, 4>{{ { 44100.0, 2048 },
                                                                     { 44100.0, 512 },
                                                                     { 44100.0, 64 },
                                                                     { 96000.0, 2048 } }})
            {
                for (int clicksPerBeat : { 1, 2, 3, 4 })
                {
                    HostedAudioDeviceInterface::Parameters params { .sampleRate = srbs.sampleRate, .blockSize = srbs.blockSize,
                                                                    .inputChannels = 0, .outputChannels = 1,
                                                                    .inputNames = {}, .outputNames = {} };
                    test_utilities::EnginePlayer player (engine, params);

                    auto edit = engine::test_utilities::createTestEdit (engine, 1, Edit::EditRole::forEditing);
                    edit->tempoSequence.getTempo (0)->setBpm (60.0);
                    edit->clickTrackEnabled = true;
                    edit->clickTrackEmphasiseBars = false;
                    edit->clickTrackSubdivisions = clicksPerBeat;
                    edit->setClickTrackVolume (1.0f);
                    CHECK_EQ (edit->getClickTrackSubdivisions(), clicksPerBeat);
                    edit->getTransport().play (false);

                    // 8 beats at 60 BPM, plus a little so the last click is whole
                    auto resultBuffer = player.process (toSamples (TimeDuration::fromSeconds (8.1), params.sampleRate));
                    auto clicks = findClicks (resultBuffer, params.sampleRate);

                    CHECK_EQ (clicks.size(), size_t (8 * clicksPerBeat + 1));

                    for (size_t i = 0; i < clicks.size(); ++i)
                    {
                        const auto expectedStart = toSamples (TimeDuration::fromSeconds ((double) i / clicksPerBeat), params.sampleRate);
                        CHECK (std::abs (clicks[i].first - (int) expectedStart) <= 1);

                        // The clicks between the beats are quieter
                        if (i % (size_t) clicksPerBeat != 0)
                            CHECK_LT (clicks[i].second, clicks[0].second * 0.75f);
                    }
                }
            }
        }
    }
} // namespace tracktion::inline engine

#endif