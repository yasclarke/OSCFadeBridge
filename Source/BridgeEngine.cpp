#include "BridgeEngine.h"

namespace
{
    // Captures a before/after snapshot of the whole project and lets the
    // UndoManager flip between them - one class covers every mutation.
    class SnapshotAction : public juce::UndoableAction
    {
    public:
        SnapshotAction (BridgeEngine& engineToUse, ProjectState beforeIn, ProjectState afterIn)
            : engine (engineToUse), before (std::move (beforeIn)), after (std::move (afterIn))
        {
        }

        bool perform() override { engine.restoreSnapshot (after); return true; }
        bool undo() override { engine.restoreSnapshot (before); return true; }

    private:
        BridgeEngine& engine;
        ProjectState before, after;
    };

    juce::var toVar (const ProjectState& state)
    {
        auto* root = new juce::DynamicObject();
        root->setProperty ("settings", state.settings.toVar());

        juce::Array<juce::var> mapArr;
        for (auto& m : state.mappings)
            mapArr.add (m.toVar());
        root->setProperty ("mappings", mapArr);

        juce::Array<juce::var> scalerArr;
        for (auto& s : state.scalers)
            scalerArr.add (s.toVar());
        root->setProperty ("scalers", scalerArr);

        return juce::var (root);
    }

    // True if a and b would open identical connections for targets of this kind.
    bool sameConnections (const GlobalSettings& a, const GlobalSettings& b, TargetKind kind)
    {
        auto ofKind = [kind] (const GlobalSettings& s)
        {
            std::vector<SendTarget> result;
            for (auto& t : s.targets)
                if (t.kind == kind)
                    result.push_back (t);
            return result;
        };

        const auto ta = ofKind (a), tb = ofKind (b);
        return std::equal (ta.begin(), ta.end(), tb.begin(), tb.end(),
                           [] (const SendTarget& x, const SendTarget& y) { return x.sameConnectionAs (y); });
    }

    // Snapshots carry whatever runtime state existed when they were taken -
    // keep the current live values rather than jumping back to those.
    template <typename T>
    void keepRuntimeState (std::vector<T>& incoming, const std::vector<T>& current)
    {
        for (auto& item : incoming)
            for (auto& existing : current)
                if (existing.id == item.id)
                {
                    item.copyRuntimeStateFrom (existing);
                    break;
                }
    }

    juce::StringArray deviceNames (const juce::Array<juce::MidiDeviceInfo>& devices)
    {
        juce::StringArray names;
        for (auto& d : devices)
            names.add (d.name);
        return names;
    }
}

BridgeEngine::BridgeEngine()
{
    loadFromFile (getDefaultProjectFile());
    restartReceiver();
    restartOscSenders();
    restartMidiOutputs();
    restartMidiInputs();
    restartTimer();

    midiDeviceListConnection = juce::MidiDeviceListConnection::make ([this]
    {
        log ("MIDI devices changed");
        restartMidiInputs();
        restartMidiOutputs();
        if (onMidiDevicesChanged)
            onMidiDevicesChanged();
    });
}

BridgeEngine::~BridgeEngine()
{
    midiDeviceListConnection.reset();
    midiInputs.clear();
    cancelPendingUpdate();
    receiver.removeListener (this);
    stopTimer();
}

//==============================================================================
ProjectState BridgeEngine::captureState() const
{
    return { mappings, scalers, settings };
}

void BridgeEngine::performSnapshotChange (const ProjectState& after, const juce::String& transactionName)
{
    auto before = captureState();

    // Text fields commit on every focus loss - don't fill the undo stack
    // with steps that change nothing. Still notify, so the UI can revert
    // any text that was rejected.
    if (juce::JSON::toString (toVar (before)) == juce::JSON::toString (toVar (after)))
    {
        if (onProjectChanged)
            onProjectChanged();
        return;
    }

    undoManager.beginNewTransaction (transactionName);
    undoManager.perform (new SnapshotAction (*this, std::move (before), after));
}

void BridgeEngine::setSettings (const GlobalSettings& newSettings)
{
    auto after = captureState();
    after.settings = newSettings;
    performSnapshotChange (after, "Change Settings");
}

Mapping* BridgeEngine::findMappingById (const juce::String& id)
{
    for (auto& m : mappings)
        if (m.id == id)
            return &m;
    return nullptr;
}

const Mapping* BridgeEngine::findMappingById (const juce::String& id) const
{
    return const_cast<BridgeEngine*> (this)->findMappingById (id);
}

const Scaler* BridgeEngine::findScalerById (const juce::String& id) const
{
    for (auto& s : scalers)
        if (s.id == id)
            return &s;
    return nullptr;
}

InputChannel* BridgeEngine::findInputById (const juce::String& id)
{
    if (auto* m = findMappingById (id))
        return m;
    for (auto& s : scalers)
        if (s.id == id)
            return &s;
    return nullptr;
}

const InputChannel* BridgeEngine::findInputById (const juce::String& id) const
{
    return const_cast<BridgeEngine*> (this)->findInputById (id);
}

juce::String BridgeEngine::generateUniqueInputAddress (const juce::String& stem) const
{
    for (int n = 1; ; ++n)
    {
        auto candidate = stem + juce::String (n);
        if (! isInputAddressInUse (candidate, {}))
            return candidate;
    }
}

bool BridgeEngine::isInputAddressInUse (const juce::String& address, const juce::String& excludingId) const
{
    auto uses = [&] (const InputChannel& input)
    {
        return input.id != excludingId && input.source == InputSource::osc && input.inputAddress == address;
    };

    return std::any_of (mappings.begin(), mappings.end(), uses)
        || std::any_of (scalers.begin(), scalers.end(), uses);
}

bool BridgeEngine::isOutputDuplicated (const juce::String& mappingId, int outputIndex) const
{
    auto* mapping = findMappingById (mappingId);
    if (mapping == nullptr || ! juce::isPositiveAndBelow (outputIndex, (int) mapping->outputs.size()))
        return false;

    const auto& out = mapping->outputs[(size_t) outputIndex];
    auto* target = settings.findTarget (out.targetId);
    if (target == nullptr)
        return false;

    for (auto& m : mappings)
        for (int i = 0; i < (int) m.outputs.size(); ++i)
        {
            if (m.id == mappingId && i == outputIndex)
                continue;

            const auto& other = m.outputs[(size_t) i];
            if (other.targetId != out.targetId)
                continue;

            if (target->kind == TargetKind::osc ? other.address == out.address : other.midi == out.midi)
                return true;
        }

    return false;
}

juce::String BridgeEngine::addMapping()
{
    auto after = captureState();
    Mapping m;
    m.inputAddress = generateUniqueInputAddress ("/bridge/fader");

    OutputTarget out;
    if (! settings.targets.empty())
        out.targetId = settings.targets.front().id;
    m.outputs.push_back (out);

    const auto newId = m.id;
    after.mappings.push_back (std::move (m));

    performSnapshotChange (after, "Add Mapping");
    return newId;
}

void BridgeEngine::removeMapping (const juce::String& id)
{
    auto after = captureState();
    after.mappings.erase (std::remove_if (after.mappings.begin(), after.mappings.end(),
                              [&] (const Mapping& m) { return m.id == id; }),
                          after.mappings.end());

    performSnapshotChange (after, "Remove Mapping");
}

void BridgeEngine::addOutput (const juce::String& mappingId)
{
    auto after = captureState();
    for (auto& m : after.mappings)
        if (m.id == mappingId)
        {
            // Default to the same target as the previous output - outputs
            // of one mapping usually go to the same place.
            OutputTarget out;
            if (! m.outputs.empty())
                out.targetId = m.outputs.back().targetId;
            else if (! settings.targets.empty())
                out.targetId = settings.targets.front().id;

            if (auto* target = settings.findTarget (out.targetId); target != nullptr && target->kind == TargetKind::midi)
                out.outMax = out.midi.maxValue();

            m.outputs.push_back (out);
            break;
        }

    performSnapshotChange (after, "Add Output");
}

void BridgeEngine::removeOutput (const juce::String& mappingId, int outputIndex)
{
    auto after = captureState();
    for (auto& m : after.mappings)
        if (m.id == mappingId && juce::isPositiveAndBelow (outputIndex, (int) m.outputs.size()))
        {
            m.outputs.erase (m.outputs.begin() + outputIndex);
            break;
        }

    performSnapshotChange (after, "Remove Output");
}

juce::String BridgeEngine::addScaler()
{
    auto after = captureState();
    Scaler s;
    s.inputAddress = generateUniqueInputAddress ("/bridge/scaler");
    for (int n = (int) scalers.size() + 1; ; ++n)
    {
        s.name = "Scaler " + juce::String (n);
        if (std::none_of (scalers.begin(), scalers.end(), [&] (const Scaler& other) { return other.name == s.name; }))
            break;
    }

    const auto newId = s.id;
    after.scalers.push_back (std::move (s));

    performSnapshotChange (after, "Add Scaler");
    return newId;
}

void BridgeEngine::removeScaler (const juce::String& id)
{
    auto after = captureState();
    after.scalers.erase (std::remove_if (after.scalers.begin(), after.scalers.end(),
                             [&] (const Scaler& s) { return s.id == id; }),
                         after.scalers.end());

    for (auto& m : after.mappings)
        if (m.scaledById == id)
            m.scaledById.clear();

    performSnapshotChange (after, "Remove Scaler");
}

void BridgeEngine::updateMapping (const juce::String& id, const std::function<void (Mapping&)>& mutator)
{
    auto after = captureState();
    for (auto& m : after.mappings)
        if (m.id == id)
        {
            mutator (m);
            break;
        }

    performSnapshotChange (after, "Edit Mapping");
}

void BridgeEngine::updateInput (const juce::String& id, const std::function<void (InputChannel&)>& mutator)
{
    auto after = captureState();
    juce::String transactionName = "Edit Mapping";

    for (auto& m : after.mappings)
        if (m.id == id)
            mutator (m);

    for (auto& s : after.scalers)
        if (s.id == id)
        {
            mutator (s);
            transactionName = "Edit Scaler";
        }

    performSnapshotChange (after, transactionName);
}

//==============================================================================
juce::String BridgeEngine::addTarget()
{
    auto after = captureState();

    SendTarget t;
    for (int n = (int) after.settings.targets.size() + 1; ; ++n)
    {
        t.name = "Target " + juce::String (n);
        if (std::none_of (after.settings.targets.begin(), after.settings.targets.end(),
                          [&] (const SendTarget& existing) { return existing.name == t.name; }))
            break;
    }

    const auto newId = t.id;
    after.settings.targets.push_back (t);

    performSnapshotChange (after, "Add Target");
    return newId;
}

void BridgeEngine::removeTarget (const juce::String& id)
{
    auto after = captureState();
    auto& targets = after.settings.targets;
    targets.erase (std::remove_if (targets.begin(), targets.end(), [&] (const SendTarget& t) { return t.id == id; }),
                   targets.end());

    for (auto& m : after.mappings)
        for (auto& out : m.outputs)
            if (out.targetId == id)
                out.targetId.clear();

    performSnapshotChange (after, "Remove Target");
}

void BridgeEngine::updateTarget (const juce::String& id, const std::function<void (SendTarget&)>& mutator)
{
    auto after = captureState();
    for (auto& t : after.settings.targets)
        if (t.id == id)
        {
            mutator (t);
            break;
        }

    performSnapshotChange (after, "Edit Target");
}

bool BridgeEngine::isTargetConnected (const juce::String& id) const
{
    return oscSenders.count (id) > 0 || midiOutputs.count (id) > 0;
}

//==============================================================================
float BridgeEngine::getScaleFactor (const Mapping& m) const
{
    auto* scaler = findScalerById (m.scaledById);
    if (scaler == nullptr || ! scaler->hasLiveValue)
        return 1.0f;

    return juce::jmap (scaler->getNormalisedValue(), m.scaleMin, m.scaleMax);
}

void BridgeEngine::startMidiLearn (const juce::String& inputId)
{
    learnInputId = inputId;
    pendingLearn.reset();

    if (auto* input = findInputById (inputId))
        log ("MIDI learn: move a control to assign it to " + input->getDisplayName());
}

void BridgeEngine::cancelMidiLearn()
{
    learnInputId.clear();
    pendingLearn.reset();
}

void BridgeEngine::handleMidiLearn (const juce::String& device, const juce::MidiMessage& message, double now)
{
    MidiSpec spec;
    spec.channel = message.getChannel();

    if (message.isPitchWheel())
    {
        spec.type = MidiMessageType::pitchBend;
        finishMidiLearn (device, spec);
        return;
    }

    const int number = message.getControllerNumber();

    if (pendingLearn.has_value() && pendingLearn->device == device && pendingLearn->channel == spec.channel
          && number == pendingLearn->number + 32)
    {
        spec.type = MidiMessageType::controlChange14Bit;
        spec.number = pendingLearn->number;
        finishMidiLearn (device, spec);
        return;
    }

    // A CC 0-31 might be the MSB of a 14-bit pair - give the LSB a moment
    // to arrive before settling on a plain CC (see timerCallback()).
    if (number < 32)
    {
        pendingLearn = PendingLearn { device, spec.channel, number, now };
        return;
    }

    spec.number = number;
    finishMidiLearn (device, spec);
}

void BridgeEngine::finishMidiLearn (const juce::String& device, const MidiSpec& spec)
{
    const auto inputId = learnInputId;
    cancelMidiLearn();

    log ("MIDI learn: " + spec.describe() + " from " + device);

    updateInput (inputId, [&] (InputChannel& input)
    {
        const bool rangeUnitsChanged = input.source != InputSource::midi || input.midiIn.type != spec.type;

        input.source = InputSource::midi;
        input.midiDevice = device;
        input.midiIn = spec;

        if (rangeUnitsChanged)
        {
            input.inMin = 0.0f;
            input.inMax = spec.maxValue();
        }
    });
}

juce::StringArray BridgeEngine::getMidiInputDeviceNames() const
{
    return deviceNames (juce::MidiInput::getAvailableDevices());
}

juce::StringArray BridgeEngine::getMidiOutputDeviceNames() const
{
    return deviceNames (juce::MidiOutput::getAvailableDevices());
}

//==============================================================================
bool BridgeEngine::canUndo() const { return undoManager.canUndo(); }
bool BridgeEngine::canRedo() const { return undoManager.canRedo(); }
void BridgeEngine::undo() { undoManager.undo(); }
void BridgeEngine::redo() { undoManager.redo(); }

void BridgeEngine::restoreSnapshot (const ProjectState& state)
{
    const auto& newSettings = state.settings;
    const bool receivePortChanged = newSettings.receivePort != settings.receivePort;
    const bool oscChanged         = ! sameConnections (newSettings, settings, TargetKind::osc);
    const bool midiChanged        = ! sameConnections (newSettings, settings, TargetKind::midi);
    const bool freqChanged        = std::abs (newSettings.updateFrequencyHz - settings.updateFrequencyHz) > 1.0e-9;

    auto newMappings = state.mappings;
    auto newScalers = state.scalers;
    keepRuntimeState (newMappings, mappings);
    keepRuntimeState (newScalers, scalers);

    mappings = std::move (newMappings);
    scalers = std::move (newScalers);
    settings = newSettings;

    activeFades.erase (std::remove_if (activeFades.begin(), activeFades.end(),
                            [this] (const ActiveFade& f) { return findInputById (f.inputId) == nullptr; }),
                        activeFades.end());

    saveProject();

    if (receivePortChanged)
        restartReceiver();
    if (oscChanged)
        restartOscSenders();
    if (midiChanged)
        restartMidiOutputs();
    if (freqChanged)
        restartTimer();

    if (onProjectChanged)
        onProjectChanged();
}

//==============================================================================
void BridgeEngine::newProject()
{
    currentProjectFile = juce::File();
    undoManager.clearUndoHistory();

    mappings.clear();
    scalers.clear();
    activeFades.clear();
    settings = GlobalSettings::makeDefault();

    saveProject();
    restartReceiver();
    restartOscSenders();
    restartMidiOutputs();
    restartTimer();

    if (onProjectChanged)
        onProjectChanged();
}

bool BridgeEngine::openProject (const juce::File& file)
{
    if (! loadFromFile (file))
        return false;

    currentProjectFile = file;
    undoManager.clearUndoHistory();
    activeFades.clear();

    restartReceiver();
    restartOscSenders();
    restartMidiOutputs();
    restartTimer();

    if (onProjectChanged)
        onProjectChanged();

    return true;
}

bool BridgeEngine::saveProjectAs (const juce::File& file)
{
    currentProjectFile = file;
    const bool ok = saveProject();
    if (onProjectChanged)
        onProjectChanged();
    return ok;
}

bool BridgeEngine::saveProject()
{
    auto file = getAutoSaveTarget();
    file.getParentDirectory().createDirectory();
    return file.replaceWithText (juce::JSON::toString (toVar (captureState())));
}

juce::String BridgeEngine::getCurrentProjectDisplayName() const
{
    return currentProjectFile == juce::File() ? "Untitled" : currentProjectFile.getFileNameWithoutExtension();
}

//==============================================================================
void BridgeEngine::oscMessageReceived (const juce::OSCMessage& message)
{
    const auto address = message.getAddressPattern().toString();

    forEachInput ([&] (InputChannel& input)
    {
        if (input.source != InputSource::osc || input.inputAddress != address)
            return;

        if (message.size() < 2 || ! (message[0].isFloat32() || message[0].isInt32())
                                 || ! (message[1].isFloat32() || message[1].isInt32()))
        {
            log ("RX " + address + "  ignored: expected 2 numeric args (x, y)");
            return;
        }

        const float x = message[0].isFloat32() ? message[0].getFloat32() : (float) message[0].getInt32();
        const float y = message[1].isFloat32() ? message[1].getFloat32() : (float) message[1].getInt32();

        log ("RX " + address + "  x=" + juce::String (x, 3) + "  y=" + juce::String (y, 3) + "s");

        if (applyInputValue (input, x, (double) y))
        {
            flushDirty();
            log ("Fade complete: " + describeResult (input));
        }
    });
}

void BridgeEngine::handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message)
{
    if (! (message.isController() || message.isPitchWheel()))
        return;

    {
        const juce::ScopedLock sl (pendingMidiLock);
        pendingMidi.emplace_back (source != nullptr ? source->getName() : juce::String(), message);
    }

    triggerAsyncUpdate();
}

void BridgeEngine::handleAsyncUpdate()
{
    std::vector<std::pair<juce::String, juce::MidiMessage>> batch;
    {
        const juce::ScopedLock sl (pendingMidiLock);
        batch.swap (pendingMidi);
    }

    if (learnInputId.isNotEmpty())
    {
        const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
        for (auto& [device, message] : batch)
            if (learnInputId.isNotEmpty())
                handleMidiLearn (device, message, now);
        return;
    }

    // Only the newest value per input in this batch matters.
    std::map<juce::String, float> latest;

    for (auto& entry : batch)
        forEachInput ([&] (InputChannel& input)
        {
            const auto& device = entry.first;
            const auto& message = entry.second;

            if (input.source != InputSource::midi || message.getChannel() != input.midiIn.channel)
                return;
            if (input.midiDevice.isNotEmpty() && input.midiDevice != device)
                return;

            if (auto value = decodeMidiValue (input, message))
                latest[input.id] = *value;
        });

    for (auto& [id, value] : latest)
    {
        auto* input = findInputById (id);
        if (input == nullptr)
            continue;

        // Instant-follow inputs (e.g. a hardware fader) would flood the log.
        if (input->midiFadeSeconds > 0.0)
            log ("RX " + input->getDisplayName() + "  value=" + juce::String ((int) value)
                   + "  fade=" + juce::String (input->midiFadeSeconds, 3) + "s");

        applyInputValue (*input, value, input->midiFadeSeconds);
    }

    flushDirty();
}

std::optional<float> BridgeEngine::decodeMidiValue (InputChannel& input, const juce::MidiMessage& message)
{
    switch (input.midiIn.type)
    {
        case MidiMessageType::controlChange:
            if (message.isController() && message.getControllerNumber() == input.midiIn.number)
                return (float) message.getControllerValue();
            break;

        case MidiMessageType::controlChange14Bit:
            if (message.isController())
            {
                // An MSB is held until its LSB arrives, so a fade doesn't
                // start toward the half-received value. Senders may omit an
                // LSB of 0 (a new MSB resets it, per the spec), so if none
                // follows, flushPendingMsbs() applies the MSB on its own.
                if (message.getControllerNumber() == input.midiIn.number)
                {
                    input.midiMsb = message.getControllerValue();
                    input.midiMsbPendingSince = juce::Time::getMillisecondCounterHiRes() / 1000.0;
                }
                else if (message.getControllerNumber() == input.midiIn.number + 32)
                {
                    input.midiMsbPendingSince = -1.0;
                    return (float) ((input.midiMsb << 7) | message.getControllerValue());
                }
            }
            break;

        case MidiMessageType::pitchBend:
            if (message.isPitchWheel())
                return (float) message.getPitchWheelValue();
            break;
    }

    return std::nullopt;
}

bool BridgeEngine::applyInputValue (InputChannel& input, float x, double fadeSeconds)
{
    const float start = input.hasLastValue ? input.lastValue : x;
    input.lastValue = x;
    input.hasLastValue = true;

    activeFades.erase (std::remove_if (activeFades.begin(), activeFades.end(),
                            [&] (const ActiveFade& f) { return f.inputId == input.id; }),
                        activeFades.end());

    if (fadeSeconds <= 0.0 || juce::approximatelyEqual (start, x))
    {
        setLiveValue (input, x);
        return true;
    }

    ActiveFade f;
    f.inputId = input.id;
    f.startValue = start;
    f.targetValue = x;
    f.startTime = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    f.duration = fadeSeconds;
    activeFades.push_back (f);
    return false;
}

void BridgeEngine::flushPendingMsbs (double now)
{
    // Long enough for an LSB sent straight after its MSB to have arrived.
    constexpr double lsbWaitSeconds = 0.02;

    forEachInput ([&] (InputChannel& input)
    {
        if (input.midiMsbPendingSince >= 0.0 && now - input.midiMsbPendingSince >= lsbWaitSeconds)
        {
            input.midiMsbPendingSince = -1.0;
            applyInputValue (input, (float) (input.midiMsb << 7), input.midiFadeSeconds);
        }
    });
}

void BridgeEngine::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    flushPendingMsbs (now);

    // No LSB followed a learned CC 0-31, so it's a plain 7-bit CC.
    if (pendingLearn.has_value() && now - pendingLearn->time >= 0.1)
    {
        MidiSpec spec;
        spec.channel = pendingLearn->channel;
        spec.number = pendingLearn->number;
        finishMidiLearn (pendingLearn->device, spec);
    }

    if (activeFades.empty())
    {
        flushDirty();
        return;
    }

    juce::StringArray completed;

    for (auto it = activeFades.begin(); it != activeFades.end(); )
    {
        auto* input = findInputById (it->inputId);
        if (input == nullptr)
        {
            it = activeFades.erase (it);
            continue;
        }

        double t = it->duration > 0.0 ? (now - it->startTime) / it->duration : 1.0;
        const bool finished = t >= 1.0;
        t = juce::jlimit (0.0, 1.0, t);

        setLiveValue (*input, it->startValue + (float) t * (it->targetValue - it->startValue));

        if (finished)
        {
            completed.add (input->id);
            it = activeFades.erase (it);
        }
        else
        {
            ++it;
        }
    }

    flushDirty();

    for (auto& id : completed)
        if (auto* input = findInputById (id))
            log ("Fade complete: " + describeResult (*input));
}

//==============================================================================
void BridgeEngine::setLiveValue (InputChannel& input, float rawValue)
{
    const float lo = juce::jmin (input.inMin, input.inMax);
    const float hi = juce::jmax (input.inMin, input.inMax);

    input.liveValue = juce::jlimit (lo, hi, rawValue);
    input.hasLiveValue = true;
    dirtyInputIds.addIfNotAlreadyThere (input.id);
}

void BridgeEngine::flushDirty()
{
    // A changed scaler means re-sending every mapping it scales.
    for (auto& m : mappings)
        if (m.scaledById.isNotEmpty() && dirtyInputIds.contains (m.scaledById))
            dirtyInputIds.addIfNotAlreadyThere (m.id);

    for (auto& m : mappings)
        if (m.hasLiveValue && dirtyInputIds.contains (m.id))
            sendOutputs (m);

    dirtyInputIds.clearQuick();
}

float BridgeEngine::getOutputValue (const Mapping& m, const OutputTarget& out) const
{
    // A scale range above 1 can boost, but never past the output range.
    const float normalised = juce::jlimit (0.0f, 1.0f, m.getNormalisedValue() * getScaleFactor (m));
    return juce::jmap (normalised, out.outMin, out.outMax);
}

void BridgeEngine::sendOutputs (const Mapping& m)
{
    for (auto& out : m.outputs)
        sendToOutput (out, getOutputValue (m, out));
}

void BridgeEngine::sendToOutput (const OutputTarget& out, float value)
{
    auto* target = settings.findTarget (out.targetId);
    if (target == nullptr)
        return;

    if (target->kind == TargetKind::osc)
    {
        auto it = oscSenders.find (target->id);
        if (it == oscSenders.end() || ! it->second->send (out.address, value))
            log ("TX FAILED " + target->name + " " + out.address);
        return;
    }

    auto it = midiOutputs.find (target->id);
    if (it == midiOutputs.end())
        return; // device missing - reported once when the outputs were (re)opened

    auto& device = *it->second;
    const int channel = juce::jlimit (1, 16, out.midi.channel);
    const int raw = juce::roundToInt (juce::jlimit (0.0f, out.midi.maxValue(), value));

    switch (out.midi.type)
    {
        case MidiMessageType::controlChange:
            device.sendMessageNow (juce::MidiMessage::controllerEvent (channel, juce::jlimit (0, 127, out.midi.number), raw));
            break;

        case MidiMessageType::controlChange14Bit:
        {
            const int number = juce::jlimit (0, 31, out.midi.number);
            device.sendMessageNow (juce::MidiMessage::controllerEvent (channel, number, raw >> 7));
            device.sendMessageNow (juce::MidiMessage::controllerEvent (channel, number + 32, raw & 0x7f));
            break;
        }

        case MidiMessageType::pitchBend:
            device.sendMessageNow (juce::MidiMessage::pitchWheel (channel, raw));
            break;
    }
}

juce::String BridgeEngine::describeResult (const InputChannel& input) const
{
    auto* m = findMappingById (input.id);
    if (m == nullptr)
        return "scaler " + input.getDisplayName() + "=" + juce::String (input.liveValue, 4);

    juce::StringArray parts;
    for (auto& out : m->outputs)
    {
        auto* target = settings.findTarget (out.targetId);
        if (target == nullptr)
            continue;

        const float value = getOutputValue (*m, out);

        if (target->kind == TargetKind::osc)
            parts.add (target->name + " " + out.address + "=" + juce::String (value, 4));
        else
            parts.add (target->name + " " + out.midi.describe() + "="
                         + juce::String (juce::roundToInt (juce::jlimit (0.0f, out.midi.maxValue(), value))));
    }
    return parts.joinIntoString (", ");
}

//==============================================================================
void BridgeEngine::restartReceiver()
{
    receiver.removeListener (this);
    receiver.disconnect();

    if (receiver.connect (settings.receivePort))
    {
        receiver.addListener (this);
        log ("Listening for OSC on port " + juce::String (settings.receivePort));
    }
    else
    {
        log ("ERROR: failed to bind receive port " + juce::String (settings.receivePort));
    }
}

void BridgeEngine::restartOscSenders()
{
    oscSenders.clear();

    for (auto& t : settings.targets)
    {
        if (t.kind != TargetKind::osc)
            continue;

        auto sender = std::make_unique<juce::OSCSender>();
        if (sender->connect (t.host, t.port))
        {
            log ("Sending OSC to " + t.name + " (" + t.host + ":" + juce::String (t.port) + ")");
            oscSenders[t.id] = std::move (sender);
        }
        else
        {
            log ("ERROR: failed to connect " + t.name + " to " + t.host + ":" + juce::String (t.port));
        }
    }
}

void BridgeEngine::restartMidiOutputs()
{
    midiOutputs.clear();
    const auto available = juce::MidiOutput::getAvailableDevices();

    for (auto& t : settings.targets)
    {
        if (t.kind != TargetKind::midi || t.midiDevice.isEmpty())
            continue;

        auto info = std::find_if (available.begin(), available.end(),
                                  [&] (const juce::MidiDeviceInfo& d) { return d.name == t.midiDevice; });

        if (info == available.end())
        {
            log ("MIDI output for " + t.name + " not found: " + t.midiDevice);
            continue;
        }

        if (auto device = juce::MidiOutput::openDevice (info->identifier))
        {
            log ("Sending MIDI to " + t.name + " (" + t.midiDevice + ")");
            midiOutputs[t.id] = std::move (device);
        }
        else
        {
            log ("ERROR: failed to open MIDI output " + t.midiDevice);
        }
    }
}

void BridgeEngine::restartMidiInputs()
{
    // Every available input is opened - inputs filter by device name, and
    // CoreMIDI inputs aren't exclusive, so this costs other apps nothing.
    midiInputs.clear();

    for (auto& info : juce::MidiInput::getAvailableDevices())
        if (auto input = juce::MidiInput::openDevice (info.identifier, this))
        {
            input->start();
            midiInputs.push_back (std::move (input));
        }
}

void BridgeEngine::restartTimer()
{
    stopTimer();
    const double hz = juce::jmax (1.0, settings.updateFrequencyHz);
    startTimer ((int) juce::jmax (1.0, std::round (1000.0 / hz)));
}

void BridgeEngine::log (const juce::String& text)
{
    if (onLogMessage)
        onLogMessage (text);
}

//==============================================================================
juce::File BridgeEngine::getDefaultProjectFile() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile ("Application Support")
             .getChildFile ("OSCFadeBridge")
             .getChildFile ("settings.json");
}

juce::File BridgeEngine::getAutoSaveTarget() const
{
    return currentProjectFile == juce::File() ? getDefaultProjectFile() : currentProjectFile;
}

bool BridgeEngine::loadFromFile (const juce::File& file)
{
    if (! file.existsAsFile())
        return false;

    auto parsed = juce::JSON::parse (file);
    if (! parsed.isObject())
        return false;

    auto settingsVar = parsed.getProperty ("settings", juce::var());
    settings = GlobalSettings::fromVar (settingsVar);

    mappings.clear();
    if (auto* arr = parsed.getProperty ("mappings", juce::var()).getArray())
        for (auto& v : *arr)
            mappings.push_back (Mapping::fromVar (v));

    scalers.clear();
    if (auto* arr = parsed.getProperty ("scalers", juce::var()).getArray())
        for (auto& v : *arr)
            scalers.push_back (Scaler::fromVar (v));

    // Only scalers can scale - drop anything else (e.g. a reference to a
    // mapping, which an unreleased build allowed).
    for (auto& m : mappings)
        if (findScalerById (m.scaledById) == nullptr)
            m.scaledById.clear();

    // Projects from before send targets existed: every output went to the
    // single global host/port, which fromVar() turned into the one target.
    if (settingsVar.getProperty ("targets", juce::var()).getArray() == nullptr && ! settings.targets.empty())
        for (auto& m : mappings)
            for (auto& out : m.outputs)
                out.targetId = settings.targets.front().id;

    return true;
}
