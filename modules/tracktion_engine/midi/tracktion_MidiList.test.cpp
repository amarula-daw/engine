/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2024
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \   Tracktion Software
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |       Corporation
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com

    Tracktion Engine uses a GPL/commercial licence - see LICENCE.md for details.
*/

#if TRACKTION_UNIT_TESTS && ENGINE_UNIT_TESTS_MIDILIST

#include <tracktion_engine/../3rd_party/doctest/tracktion_doctest.hpp>
#include "../utilities/tracktion_TestUtilities.h"

namespace tracktion::inline engine {

TEST_SUITE ("tracktion_engine")
{
    TEST_CASE ("MidiList: Expression edits refresh looped playback without replacing notes")
    {
        using namespace tracktion::engine::test_utilities;

        auto edit = createTestEdit (*Engine::getEngines()[0]);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

        auto clip = insertMIDIClip (*getAudioTracks (*edit)[0], { 0_tp, 4_tp });
        clip->setLoopRangeBeats ({ 0_bp, 2_bp });
        auto& list = clip->getSequence();
        auto note = list.addNote (60, 0_bp, 1_bd, 100, 0, nullptr);
        auto noteState = note->state;
        auto um = &edit->getUndoManager();

        for (auto type : { IDs::PITCHBEND, IDs::PRESSURE, IDs::TIMBRE })
        {
            CAPTURE (type.toString().toStdString());

            auto checkPlayback = [&] (bool hasPoint, double beat, float value)
            {
                CHECK (list.getNote (0) == note);
                CHECK (list.getNote (0)->state == noteState);

                auto& looped = clip->getSequenceLooped();
                REQUIRE (looped.getNumNotes() > 1);

                // Check every repetition, not only the source note's live ValueTree.
                for (auto repeatedNote : looped.getNotes())
                {
                    auto point = repeatedNote->state.getChildWithName (type);
                    CHECK (point.isValid() == hasPoint);

                    if (hasPoint)
                    {
                        CHECK (double (point[IDs::b]) == doctest::Approx (beat));
                        CHECK (float (point[IDs::v]) == doctest::Approx (value));
                    }
                }

                auto sequence = looped.exportToPlaybackMidiSequence (*clip, MidiList::TimeBase::beats, true);
                int numPoints = 0;

                for (int i = 0; i < sequence.getNumEvents(); ++i)
                {
                    auto& message = sequence.getEventPointer (i)->message;
                    auto matches = type == IDs::PITCHBEND ? message.isPitchWheel()
                                 : type == IDs::PRESSURE ? message.isChannelPressure()
                                                        : message.isController() && message.getControllerNumber() == 74;

                    if (matches && std::abs (message.getTimeStamp() - beat) < 0.00001)
                    {
                        ++numPoints;
                        auto expected = type == IDs::PITCHBEND ? juce::MidiMessage::pitchbendToPitchwheelPos (value, 48.0f)
                                                             : int (juce::MidiMessage::floatValueToMidiByte (value));
                        auto actual = type == IDs::PITCHBEND ? message.getPitchWheelValue()
                                    : type == IDs::PRESSURE ? message.getChannelPressureValue()
                                                           : message.getControllerValue();
                        CHECK (actual == expected);
                    }
                }

                CHECK (numPoints == (hasPoint ? 1 : 0));
            };

            checkPlayback (false, 0.25, 0.25f); // Prime the cache before the first child is added.
            auto point = createValueTree (type, IDs::b, 0.25, IDs::v, 0.25f);
            um->beginNewTransaction();
            noteState.addChild (point, -1, um);
            checkPlayback (true, 0.25, 0.25f);

            um->beginNewTransaction();
            MidiExpression (point).setValue (0.75f, um);
            checkPlayback (true, 0.25, 0.75f);
            REQUIRE (um->undo());
            checkPlayback (true, 0.25, 0.25f);
            REQUIRE (um->redo());
            checkPlayback (true, 0.25, 0.75f);

            MidiExpression (point).setBeatPosition (0.5_bp, um);
            checkPlayback (true, 0.5, 0.75f);

            um->beginNewTransaction();
            noteState.removeChild (point, um);
            checkPlayback (false, 0.5, 0.75f);
            REQUIRE (um->undo());
            checkPlayback (true, 0.5, 0.75f);
            REQUIRE (um->redo());
            checkPlayback (false, 0.5, 0.75f);
        }
    }

    TEST_CASE ("MidiList: Undo note length change")
    {
        using namespace tracktion::graph::test_utilities;
        using namespace tracktion::engine::test_utilities;

        auto& engine = *Engine::getEngines()[0];
        auto edit = createTestEdit (engine);

        // We need to pump the dispatch loop here to ensure the Edit has attached the undo manager
        // This may be handled better in a more syncronous way in the future
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

        auto um = &edit->getUndoManager();
        auto track = getAudioTracks (*edit)[0];
        auto mc = insertMIDIClip (*track, { 0_tp, 4_tp });

        auto& list = mc->getSequence();
        auto note = list.addNote (60, 0_bp, 1_bd, 127, 0, um);

        edit->resetChangedStatus();
        CHECK (! edit->hasChangedSinceSaved());

        note->setStartAndLength (note->getStartBeat(), note->getLengthBeats() + 1_bd, um);

        // Flush the change message before checking the status
        um->dispatchPendingMessages();
        CHECK (edit->hasChangedSinceSaved());
    }
}

} // namespace tracktion::inline engine

#endif
