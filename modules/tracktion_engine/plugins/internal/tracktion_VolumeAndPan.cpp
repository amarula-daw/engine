/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2024
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \   Tracktion Software
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |       Corporation
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com

    Tracktion Engine uses a GPL/commercial licence - see LICENCE.md for details.
*/

namespace tracktion::inline engine {

struct VolAutomatableParameter : public AutomatableParameter
{
    VolAutomatableParameter (const juce::String& xmlTag, const juce::String& name,
                             Plugin& owner, juce::Range<float> r)
        : AutomatableParameter (xmlTag, name, owner, r)
    {
    }

    ~VolAutomatableParameter() override
    {
        notifyListenersOfDeletion();
    }

    juce::String valueToString (float value) override
    {
        return juce::Decibels::toString (volumeFaderPositionToDB (value) + 0.001);
    }

    float stringToValue (const juce::String& str) override
    {
        return decibelsToVolumeFaderPosition (dbStringToDb (str));
    }
};

//==============================================================================
struct PanAutomatableParameter : public AutomatableParameter
{
    PanAutomatableParameter (const juce::String& xmlTag, const juce::String& name,
                             Plugin& owner, juce::Range<float> r)
        : AutomatableParameter (xmlTag, name, owner, r)
    {
    }

    ~PanAutomatableParameter() override
    {
        notifyListenersOfDeletion();
    }

    juce::String valueToString (float value) override
    {
        return getPanString (value);
    }

    float stringToValue (const juce::String& str) override
    {
        const float v = str.retainCharacters ("0123456789.-").getFloatValue();
        return str.contains (TRANS("Left")) ? -v : v;
    }
};

//==============================================================================
struct TrimAutomatableParameter : public AutomatableParameter
{
    TrimAutomatableParameter (const juce::String& xmlTag, const juce::String& name,
                              Plugin& owner, juce::Range<float> r)
        : AutomatableParameter (xmlTag, name, owner, r)
    {
    }

    ~TrimAutomatableParameter() override
    {
        notifyListenersOfDeletion();
    }

    juce::String valueToString (float value) override
    {
        return juce::Decibels::toString (value + 0.001);
    }

    float stringToValue (const juce::String& str) override
    {
        return dbStringToDb (str);
    }
};

//==============================================================================
struct WidthAutomatableParameter : public AutomatableParameter
{
    WidthAutomatableParameter (const juce::String& xmlTag, const juce::String& name,
                               Plugin& owner, juce::Range<float> r)
        : AutomatableParameter (xmlTag, name, owner, r)
    {
    }

    ~WidthAutomatableParameter() override
    {
        notifyListenersOfDeletion();
    }

    juce::String valueToString (float value) override
    {
        return juce::String (juce::roundToInt (value * 100.0f)) + "%";
    }

    float stringToValue (const juce::String& str) override
    {
        return str.retainCharacters ("0123456789.-").getFloatValue() / 100.0f;
    }
};

//==============================================================================
struct MasterVolParameter  : public VolAutomatableParameter
{
    MasterVolParameter (const juce::String& xmlTag, const juce::String& name,
                        Plugin& owner, juce::Range<float> r)
        : VolAutomatableParameter (xmlTag, name, owner, r)
    {
    }

    ~MasterVolParameter() override
    {
        notifyListenersOfDeletion();
    }

    juce::String getPluginAndParamName() const override        { return getParameterName(); }
    juce::String getFullName() const override                  { return getParameterName(); }
};

//==============================================================================
struct MasterPanParameter  : public PanAutomatableParameter
{
    MasterPanParameter (const juce::String& xmlTag, const juce::String& name,
                        Plugin& owner, juce::Range<float> r)
        : PanAutomatableParameter (xmlTag, name, owner, r)
    {
    }

    ~MasterPanParameter() override
    {
        notifyListenersOfDeletion();
    }

    juce::String getPluginAndParamName() const override        { return getParameterName(); }
    juce::String getFullName() const override                  { return getParameterName(); }
};

//==============================================================================
VolumeAndPanPlugin::VolumeAndPanPlugin (PluginCreationInfo info, bool isMasterVolumeNode)
    : Plugin (info),
      isMasterVolume (isMasterVolumeNode)
{
    auto um = getUndoManager();

    volume.referTo (state, IDs::volume, um, decibelsToVolumeFaderPosition (0));
    pan.referTo (state, IDs::pan, um);
    applyToMidi.referTo (state, IDs::applyToMidi, um);
    ignoreVca.referTo (state, IDs::ignoreVca, um);
    polarity.referTo (state, IDs::polarity, um);
    panLaw.referTo (state, IDs::panLaw, um);
    trim.referTo (state, IDs::trim, um);
    width.referTo (state, IDs::width, um, 1.0f);
    mono.referTo (state, IDs::mono, um);
    muted.referTo (state, IDs::mute, um);

    if (isMasterVolumeNode)
    {
        addAutomatableParameter (volParam = new MasterVolParameter ("master volume", TRANS("Master volume"), *this, { 0.0f, 1.0f }));
        addAutomatableParameter (panParam = new MasterPanParameter ("master pan", TRANS("Master pan"), *this, { -1.0f, 1.0f }));
    }
    else
    {
        addAutomatableParameter (volParam = new VolAutomatableParameter ("volume", TRANS("Volume"), *this, { 0.0f, 1.0f }));
        addAutomatableParameter (panParam = new PanAutomatableParameter ("pan", TRANS("Pan"), *this, { -1.0f, 1.0f }));
    }

    addAutomatableParameter (trimParam = new TrimAutomatableParameter ("trim", TRANS("Trim"), *this, { -24.0f, 24.0f }));
    addAutomatableParameter (widthParam = new WidthAutomatableParameter ("width", TRANS("Width"), *this, { 0.0f, 2.0f }));

    volParam->attachToCurrentValue (volume);
    panParam->attachToCurrentValue (pan);
    trimParam->attachToCurrentValue (trim);
    widthParam->attachToCurrentValue (width);
}

VolumeAndPanPlugin::VolumeAndPanPlugin (Edit& ed, const juce::ValueTree& v, bool isMaster)
    : VolumeAndPanPlugin (PluginCreationInfo (ed, v, false), isMaster)
{
}

VolumeAndPanPlugin::~VolumeAndPanPlugin()
{
    notifyListenersOfDeletion();

    volParam->detachFromCurrentValue();
    panParam->detachFromCurrentValue();
    trimParam->detachFromCurrentValue();
    widthParam->detachFromCurrentValue();
}

juce::ValueTree VolumeAndPanPlugin::create()
{
    juce::ValueTree v (IDs::PLUGIN);
    v.setProperty (IDs::type, xmlTypeName, nullptr);
    return v;
}

const char* VolumeAndPanPlugin::xmlTypeName = "volume";

//==============================================================================
void VolumeAndPanPlugin::initialise (const PluginInitialisationInfo& info)
{
    refreshVCATrack();

    setSmoothedValueTargets (info.startTime, true);

    smoothedGainL.reset (info.sampleRate, smoothingRampTimeSeconds);
    smoothedGainR.reset (info.sampleRate, smoothingRampTimeSeconds);
    smoothedGain.reset (info.sampleRate, smoothingRampTimeSeconds);
    smoothedWidth.reset (info.sampleRate, smoothingRampTimeSeconds);
    smoothedWidth.setCurrentAndTargetValue (mono ? 0.0f : getWidth());
}

void VolumeAndPanPlugin::initialiseWithoutStopping (const PluginInitialisationInfo&)
{
    TRACKTION_ASSERT_MESSAGE_THREAD
    refreshVCATrack();
}

void VolumeAndPanPlugin::deinitialise()
{
    juce::ReferenceCountedObjectPtr<AudioTrack> deleter;
    std::vector<juce::ReferenceCountedObjectPtr<FolderTrack>> vcaDeleter;

    {
        const std::scoped_lock sl (vcaTrackLock);
        std::swap (vcaTrack, deleter);
        std::swap (assignedVcas, vcaDeleter);
    }
}

//==============================================================================
static float getParentVcaDb (Track& track, TimePosition time)
{
    float posOffset = 0.0f;

    for (FolderTrack::Ptr p = track.getParentFolderTrack(); p != nullptr; p = p->getParentFolderTrack())
    {
        float db = p->getVcaDb (time);

        if (db < -96.0f)
            return -100.0f;

        posOffset += decibelsToVolumeFaderPosition (db)
                       - decibelsToVolumeFaderPosition (0.0f);
    }

    return volumeFaderPositionToDB (decibelsToVolumeFaderPosition (0) + posOffset);
}

float VolumeAndPanPlugin::applyVCAs (float sliderPos, TimePosition time)
{
    const std::scoped_lock sl (vcaTrackLock);

    if (vcaTrack == nullptr)
        return sliderPos;

    // Parent folders' VCAs move the fader's position, as if it were pushed along
    sliderPos += decibelsToVolumeFaderPosition (getParentVcaDb (*vcaTrack, time))
                   - decibelsToVolumeFaderPosition (0.0f);

    if (assignedVcas.empty())
        return sliderPos;

    // A fader all the way down stays silent, whatever the VCAs add
    if (sliderPos <= 0.0f || volumeFaderPositionToDB (sliderPos) < -96.0f)
        return 0.0f;

    // Assigned VCAs add their decibels, so -6 dB on one is 6 dB down wherever the fader is
    auto db = volumeFaderPositionToDB (sliderPos);

    for (auto& vca : assignedVcas)
    {
        auto vcaDb = vca->getVcaDb (time);

        // One all the way down silences the channel
        if (vcaDb < -96.0f)
            return 0.0f;

        db += vcaDb;
    }

    return decibelsToVolumeFaderPosition (db);
}

void VolumeAndPanPlugin::setSmoothedValueTargets (TimePosition time, bool updateGain)
{
    auto sliderPos = applyVCAs (getSliderPos(), time);

    float gainL, gainR;
    getGainsFromVolumeFaderPositionAndPan (sliderPos, getPan(), getPanLaw(), gainL, gainR);

    // The trim goes on top of the fader, polarity inverts the whole channel, and muting silences it
    auto trimGain = muted ? 0.0f : juce::Decibels::decibelsToGain (getTrimDb());

    if (polarity)
        trimGain = -trimGain;

    smoothedGainL.setTargetValue (gainL * trimGain);
    smoothedGainR.setTargetValue (gainR * trimGain);
    smoothedWidth.setTargetValue (mono ? 0.0f : getWidth());

    if (updateGain)
        smoothedGain.setTargetValue (volumeFaderPositionToGain (sliderPos) * trimGain);
}

void VolumeAndPanPlugin::applyToBuffer (const PluginRenderContext& fc)
{
    if (isEnabled())
    {
        SCOPED_REALTIME_CHECK

        if (auto buffer = fc.destBuffer)
        {
            const auto numChansIn = buffer->getNumChannels();

            if (numChansIn < 1)
                return;

            setSmoothedValueTargets (fc.editTime.getStart(), numChansIn > 2);

            if (numChansIn > 1)
                applyWidth (*buffer, fc.bufferStartSample, fc.bufferNumSamples);

            smoothedGainL.applyGain (buffer->getWritePointer (0, fc.bufferStartSample), fc.bufferNumSamples);

            if (numChansIn > 1)
            {
                smoothedGainR.applyGain (buffer->getWritePointer (1, fc.bufferStartSample), fc.bufferNumSamples);

                // If the number of channels is greater than two, apply volume to the rest
                if (numChansIn > 2)
                {
                    auto originalGain = smoothedGain;

                    for (int i = 2; i < numChansIn; ++i)
                    {
                        smoothedGain = originalGain;
                        smoothedGain.applyGain (buffer->getWritePointer (i, fc.bufferStartSample), fc.bufferNumSamples);
                    }
                }
            }
        }

        if (applyToMidi && fc.bufferForMidiMessages != nullptr)
            fc.bufferForMidiMessages->multiplyVelocities (volumeFaderPositionToGain (getSliderPos()));
    }
}

void VolumeAndPanPlugin::applyWidth (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    if (! smoothedWidth.isSmoothing() && smoothedWidth.getTargetValue() == 1.0f)
        return;

    // Scales the difference between the sides, before the pan: the middle stays, the sides follow the width
    auto l = buffer.getWritePointer (0, startSample);
    auto r = buffer.getWritePointer (1, startSample);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto w = smoothedWidth.getNextValue();
        const auto mid = (l[i] + r[i]) * 0.5f;
        const auto side = (l[i] - r[i]) * 0.5f * w;
        l[i] = mid + side;
        r[i] = mid - side;
    }
}

void VolumeAndPanPlugin::refreshVCATrack()
{
    juce::ReferenceCountedObjectPtr<AudioTrack> newVcaTrack (ignoreVca ? nullptr : dynamic_cast<AudioTrack*> (getOwnerTrack()));
    std::vector<juce::ReferenceCountedObjectPtr<FolderTrack>> newAssignedVcas;

    if (newVcaTrack != nullptr)
    {
        for (auto& id : juce::StringArray::fromTokens (newVcaTrack->state[IDs::vcaIDs].toString(), ",", {}))
        {
            if (auto folder = dynamic_cast<FolderTrack*> (findTrackForID (edit, EditItemID::fromString (id))))
            {
                // A parent folder's VCA already counts
                if (folder->getVCAPlugin() == nullptr || newVcaTrack->isAChildOf (*folder))
                    continue;

                if (std::find (newAssignedVcas.begin(), newAssignedVcas.end(), folder) == newAssignedVcas.end())
                    newAssignedVcas.push_back (folder);
            }
        }
    }

    {
        const std::scoped_lock sl (vcaTrackLock);
        std::swap (vcaTrack, newVcaTrack);
        std::swap (assignedVcas, newAssignedVcas);
    }
}

float VolumeAndPanPlugin::getVolumeDb() const
{
    return volumeFaderPositionToDB (volParam->getCurrentValue());
}

void VolumeAndPanPlugin::setVolumeDb (float vol)
{
    setSliderPos (decibelsToVolumeFaderPosition (vol));
}

void VolumeAndPanPlugin::setSliderPos (float newV)
{
    volParam->setParameter (juce::jlimit (0.0f, 1.0f, newV), juce::sendNotification);
}

void VolumeAndPanPlugin::setTrimDb (float db)
{
    trimParam->setParameter (juce::jlimit (-24.0f, 24.0f, db), juce::sendNotification);
}

void VolumeAndPanPlugin::setWidth (float newWidth)
{
    widthParam->setParameter (juce::jlimit (0.0f, 2.0f, newWidth), juce::sendNotification);
}

void VolumeAndPanPlugin::setPan (float p)
{
    if (p >= -0.005f && p <= 0.005f)
        p = 0.0f;

    panParam->setParameter (juce::jlimit (-1.0f, 1.0f, p), juce::sendNotification);
}

PanLaw VolumeAndPanPlugin::getPanLaw() const noexcept
{
    PanLaw p = (PanLaw) panLaw.get();
    return p == PanLawDefault ? getDefaultPanLaw() : p;
}

//==============================================================================
void VolumeAndPanPlugin::setAppliedToMidiVolumes (bool b)
{
    if (b != applyToMidi)
    {
        applyToMidi = b;
        changed();
    }
}

void VolumeAndPanPlugin::muteOrUnmute()
{
    if (getVolumeDb() > -90.0f)
    {
        lastVolumeBeforeMute = getVolumeDb();
        setVolumeDb (lastVolumeBeforeMute - 0.01f); // needed so that automation is recorded correctly
        setVolumeDb (-100.0f);
    }
    else
    {
        if (lastVolumeBeforeMute < -100.0f)
            lastVolumeBeforeMute = 0.0f;

        setVolumeDb (getVolumeDb() + 0.01f); // needed so that automation is recorded correctly
        setVolumeDb (lastVolumeBeforeMute);
    }
}

void VolumeAndPanPlugin::restorePluginStateFromValueTree (const juce::ValueTree& v)
{
    copyPropertiesToCachedValues (v, volume, pan, panLaw, applyToMidi, ignoreVca, polarity, trim, width, mono, muted);

    for (auto p : getAutomatableParameters())
        p->updateFromAttachedValue();
}

} // namespace tracktion::inline engine
