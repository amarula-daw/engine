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
    A MIDI effect keeping notes in a scale: notes outside it move to the nearest
    note in it (the lower one when two are as near).

    It can follow a key set from outside (the song's, with setFollowedKey) instead
    of its own root and scale, which it uses while there's no such key.
*/
class ScalePlugin   : public MidiEffectPlugin
{
public:
    ScalePlugin (PluginCreationInfo);
    ~ScalePlugin() override;

    //==============================================================================
    AutomatableParameter::Ptr root, scale, followKey;

    /** The scales, in the order of the scale parameter's choices. */
    static juce::StringArray getScaleNames();
    static juce::StringArray getRootNames();

    /** The note in a scale (an index into getScaleNames()) on a root (0 is C) nearest a note, the lower one on a tie. */
    static int snapToScale (int note, int root, int scale);

    /** Sets the key followed while followKey is on: a root (0 is C) and scale, or -1s for none. Safe from any thread. */
    void setFollowedKey (int root, int scale);
    /** The key followed, as set with setFollowedKey; -1s for none. */
    std::pair<int, int> getFollowedKey() const;

    //==============================================================================
    static const char* getPluginName()      { return NEEDS_TRANS("Scale"); }
    static const char* xmlTypeName;

    juce::String getName() const override                   { return TRANS("Scale"); }
    juce::String getPluginType() override                   { return xmlTypeName; }
    juce::String getShortName (int) override                { return TRANS("Scale"); }
    juce::String getSelectableDescription() override        { return TRANS("Scale Plugin"); }

protected:
    void processMidi (const MidiMessageArray&, MidiMessageArray&, const Block&) override;
    void resetMidiState() override {}

private:
    std::atomic<int> followedKey { -1 }; // root * 16 + scale

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScalePlugin)
};

} // namespace tracktion::inline engine
