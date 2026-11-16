/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2024
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \   Tracktion Software
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |       Corporation
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com

    Tracktion Engine uses a GPL/commercial licence - see LICENCE.md for details.
*/

namespace tracktion::inline engine {

// JUCE Changed the format of PluginDescription::createIdentifierString
// breaking all our saved files. This reverts to the old format
juce::String createIdentifierString (const juce::PluginDescription&);

#if TRACKTION_ENABLE_ARA
class ARAPluginBinding;
#endif


/** Wraps a juce::AudioPluginInstance (VST2, VST3, AU, etc.) as a tracktion Plugin.

    Handles lifecycle (async loading, state persistence), parameter bridging,
    bus layout management, and dry/wet mixing for hosted third-party plugins.
*/
class ExternalPlugin  : public Plugin
{
public:
    ExternalPlugin (PluginCreationInfo);
    ~ExternalPlugin() override;

    juce::String getIdentifierString() override { return createIdentifierString (desc); }

    using Ptr = juce::ReferenceCountedObjectPtr<ExternalPlugin>;

    void selectableAboutToBeDeleted() override;

    /** Creates the ValueTree state for inserting an ExternalPlugin into an Edit. */
    static juce::ValueTree create (Engine&, const juce::PluginDescription&);

    void processingChanged() override;

    //==============================================================================
    // Initialisation & lifecycle

    void initialiseFully() override;
    void forceFullReinitialise();

    /// Returns an error message if the plugin failed to load
    juce::String getLoadError();

    /** Returns true if the plugin has started initialising but not completed yet.
        This happens in the case of some external formats like AUv3. You can use
        this to show an indicator in your UI.
    */
    bool isInitialisingAsync() const;

    static const char* xmlTypeName;

    //==============================================================================
    // State persistence

    void flushPluginStateToValueTree() override;

    /** Writes the current bus layout to the plugin's ValueTree state. */
    void flushBusesLayoutToValueTree();

    void restorePluginStateFromValueTree (const juce::ValueTree&) override;

    /** Extracts the raw plugin state blob from the ValueTree. */
    void getPluginStateFromTree (juce::MemoryBlock&);

    //==============================================================================
    // Undo

    /** Puts the plugin's current settings (its state, its parameters' values and its program)
        into its state as a step in the Edit's undo history, if they differ from the ones last
        written there. Returns true if they did. A drag's changes become one step.

        Plugins keep their own settings, so changes made in a plugin's window, or through its
        parameters, don't reach the undo history by themselves: this puts them there, and undoing
        or redoing the change puts those settings back into the plugin. Changes the user makes in
        the plugin's window call EngineBehaviour::pluginSettingsChangedByUser, which calls this.
    */
    bool captureStateForUndo();

    /** True if the plugin's current settings differ from the ones last written to its state. */
    bool hasUncapturedStateChange();

    //==============================================================================
    // Processing & runtime overrides

    void updateFromMirroredPluginIfNeeded (Plugin&) override;

    void initialise (const PluginInitialisationInfo&) override;
    void deinitialise() override;
    void reset() override;
    void midiPanic() override;
    void setEnabled (bool enabled) override;

    juce::Array<Exportable::ReferencedItem> getReferencedItems() override;
    void reassignReferencedItem (const ReferencedItem&, ProjectItemRef newRef, double newStartTime) override;

    void applyToBuffer (const PluginRenderContext&) override;

    //==============================================================================
    // Channel configuration
    //
    // Main-bus overrides report only the main I/O bus for track routing.
    // getChannelNames() reports ALL channels including sidechain.

    bool producesAudioWhenNoAudioInput() override   { return isAutomationNeeded() || isSynth() || ! noTail(); }
    int getNumOutputChannelsGivenInputs (int numInputs) override;
    BusLayout getBusses() const override;
    void getChannelNames (juce::StringArray* ins, juce::StringArray* outs) override;

    bool isVST() const noexcept             { return desc.pluginFormatName == "VST"; }
    bool isVST3() const noexcept            { return desc.pluginFormatName == "VST3"; }
    bool isAU() const noexcept              { return desc.pluginFormatName == "AudioUnit"; }
    juce::String getName() const override   { return desc.name; }
    juce::String getVendor() override       { return desc.manufacturerName; }
    juce::String getTooltip() override      { return getName() + "$vstfilter"; }
    juce::String getPluginType() override   { return xmlTypeName; }
    bool isSynth() override                 { return desc.isInstrument; }
    bool takesMidiInput() override;
    bool takesAudioInput() override         { return (! isSynth()) || (mixParam->getCurrentValue() < 1.0f); }
    bool isMissing() override;
    bool isDisabled() override;
    double getLatencySeconds() override     { return latencySeconds; }
    bool noTail() override;
    double getTailLength() const override;
    void trackPropertiesChanged() override;

    juce::AudioProcessor* getWrappedAudioProcessor() const override     { return getAudioPluginInstance(); }
    void deleteFromParent() override;

    //==============================================================================
    // selectable stuff
    juce::String getSelectableDescription() override;

    //==============================================================================
    juce::File getFile() const;
    juce::String getPluginUID() const           { return juce::String::toHexString (desc.deprecatedUid); }

    const char* getDebugName() const noexcept   { return debugName.toUTF8(); }

    //==============================================================================
    // Bus layout

    /** Attempts to change the full bus layout of the wrapped plugin.
        Temporarily stops playback to safely reconfigure. */
    bool setBusesLayout (juce::AudioProcessor::BusesLayout);

    /** Attempts to change a single bus layout. */
    bool setBusLayout (juce::AudioChannelSet, bool isInput, int busIndex);

    //==============================================================================
    int getNumPrograms() const;
    int getCurrentProgram() const;
    juce::String getProgramName (int index);
    juce::String getNumberedProgramName (int i);
    juce::String getCurrentProgramName();
    void setCurrentProgram (int index, bool sendChangeMessage);
    void setCurrentProgramName (const juce::String& name);
    bool hasNameForMidiProgram (int programNum, int bank, juce::String& name) override;
    bool hasNameForMidiNoteNumber (int note, int midiChannel, juce::String& name) override;

    //==============================================================================
    const VSTXML* getVSTXML() const noexcept        { return vstXML.get(); }

    //==============================================================================
    // Plugin instance access

    /** Returns the underlying juce::AudioPluginInstance, or nullptr if not yet loaded. */
    juce::AudioPluginInstance* getAudioPluginInstance() const;

    /** If enabled, the wrapped juce::AudioPluginInstance is destroyed synchronously when
        this plugin is deleted rather than via the shared asynchronous deleter. ARA-bound
        instances need this: the ARA spec requires a bound plugin instance to be destroyed
        before the document controller it is bound to. */
    void setDeletesPluginInstanceSynchronously (bool shouldDeleteSynchronously) noexcept    { deleteInstanceSynchronously = shouldDeleteSynchronously; }

   #if TRACKTION_ENABLE_ARA
    /** Takes ownership of this plugin's ARA document binding
        (see ARADocumentHolder::bindPluginToDocument). The binding is destroyed -
        unregistering from the ARA document - just before the wrapped instance is
        deleted, so it can never dangle or outlive the plugin. */
    void setARADocumentBinding (std::unique_ptr<ARAPluginBinding>);

    /** Returns true if this plugin owns an ARA document binding, i.e. it was bound via
        ARADocumentHolder::bindPluginToDocument (the browser/panel case). The bind lasts
        for the lifetime of the wrapped instance, so a plugin returned from the
        PluginCache may already be bound.
        NB: clip-player instances are ARA-bound too, but they're owned by their
        ARAClipPlayer and never pass through the PluginCache, so they don't show here. */
    bool isBoundToARADocument() const noexcept          { return araBinding != nullptr; }
   #endif

    /** The plugin's description (format, name, manufacturer, file path, etc.). */
    juce::PluginDescription desc;

    /** The wet/dry mix and its automatable parameter: from 0 (only the input, dry) to 1
        (only the plugin's output, wet). The output plays at the mix and the input at 1 - mix.

        Edits saved with separate dry and wet levels load with the mix they make,
        wet / (wet + dry), which sounds the same wherever the two add up to 1.
    */
    juce::CachedValue<float> mixValue;
    AutomatableParameter::Ptr mixParam;

    /** The mix that separate dry and wet levels make, as older edits stored them. */
    static float getMixForLevels (float dry, float wet);

    ActiveNoteList getActiveNotes() const           { return activeNotes; }

    //==============================================================================
    std::unique_ptr<EditorComponent> createEditor() override;

    //==============================================================================
    // Deprecated

    /** @deprecated Use getAudioPluginInstance()->getTotalNumInputChannels() instead. */
    [[deprecated ("Use getAudioPluginInstance()->getTotalNumInputChannels() instead")]]
    int getTotalNumInputChannels() const;

private:
    //==============================================================================
    juce::CriticalSection processMutex;
    juce::String debugName, identiferString, loadError;

    class ProcessorChangedManager;
    class LoadedInstance;
    std::unique_ptr<LoadedInstance> loadedInstance;
    std::atomic<bool> hasLoadedInstance { false }, isInstancePrepared { false }, isAsyncInitialising { false };
    bool deleteInstanceSynchronously = false;

   #if TRACKTION_ENABLE_ARA
    std::unique_ptr<ARAPluginBinding> araBinding;
   #endif

    std::unique_ptr<VSTXML> vstXML;
    int latencySamples = 0;
    double latencySeconds = 0;

    double lastSampleRate = 0.0;
    int lastBlockSizeSamples = 0;

    juce::MidiBuffer midiBuffer;
    MPESourceID midiSourceID = createUniqueMPESourceID();
    std::atomic<bool> midiPanicNeeded { false };

    ActiveNoteList activeNotes;

    class PluginPlayHead;
    std::unique_ptr<PluginPlayHead> playhead;

    bool fullyInitialised = false, supportsMPE = false, isFlushingLayoutToState = false, isWritingState = false;
    juce::uint32 ignoreSettingsChangesUntil = 0;

    struct MPEChannelRemapper;
    std::unique_ptr<MPEChannelRemapper> mpeRemapper;

    void prepareIncomingMidiMessages (MidiMessageArray& incoming, int numSamples, bool isPlaying);

    /** A plugin's settings as the Edit keeps them: its state, its parameters' values and its program. */
    struct Settings
    {
        juce::String chunk, parameterValues;
        juce::var program;
    };

    class SettingsAction;
    std::optional<Settings> pendingSettings;    // read for settingsChangedByUser, for the capture it leads to
    juce::String initialParameterValues;        // for a plugin from a saved Edit, which has none stored

    Settings getCurrentSettings();
    juce::String getCurrentParameterValues();
    Settings getStoredSettings() const;
    void writeSettings (const Settings&);
    void restoreStateAfterUndo();
    void settingsChangedByUser();

    juce::Array<ExternalAutomatableParameter*> autoParamForParamNumbers;

    //==============================================================================
    void startPluginInstanceCreation (const juce::PluginDescription&);
    void completePluginInstanceCreation (std::unique_ptr<juce::AudioPluginInstance>);
    void deletePluginInstance();

    //==============================================================================
    void buildParameterTree() const override;
    void buildParameterTree (const VSTXML::Group*, AutomatableParameterTree::TreeNode*, juce::SortedSet<int>&) const;

    //==============================================================================
    void doFullInitialisation();
    void buildParameterList();
    void refreshParameterValues();
    void updateDebugName();
    void processPluginBlock (juce::AudioPluginInstance&, const PluginRenderContext&, bool processedBypass);

    std::unique_ptr<juce::PluginDescription> findMatchingPlugin() const;

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;

    //==============================================================================
    static bool requiresAsyncInstantiation (Engine&, const juce::PluginDescription&);

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExternalPlugin)
};

//==============================================================================
/** Specialised AutomatableParameter for the wet/dry mix.
    Having a subclass just lets it label itself more nicely, as a percentage.
 */
struct PluginWetDryAutomatableParam  : public AutomatableParameter
{
    PluginWetDryAutomatableParam (const juce::String& xmlTag, const juce::String& name, Plugin&);
    ~PluginWetDryAutomatableParam() override;

    juce::String valueToString (float value) override;
    float stringToValue (const juce::String& s) override;

    PluginWetDryAutomatableParam() = delete;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginWetDryAutomatableParam)
};

} // namespace tracktion::inline engine
