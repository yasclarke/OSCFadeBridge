#include "BridgeEngine.h"

namespace
{
    // Captures a before/after snapshot of the whole project and lets the
    // UndoManager flip between them - one class covers every mutation
    // (add/remove mapping, add/remove output, field edits, settings).
    class SnapshotAction : public juce::UndoableAction
    {
    public:
        SnapshotAction (BridgeEngine& engineToUse,
                         std::vector<Mapping> beforeMappingsIn, GlobalSettings beforeSettingsIn,
                         std::vector<Mapping> afterMappingsIn, GlobalSettings afterSettingsIn)
            : engine (engineToUse),
              before (std::move (beforeMappingsIn)), after (std::move (afterMappingsIn)),
              beforeSettings (beforeSettingsIn), afterSettings (afterSettingsIn)
        {
        }

        bool perform() override { engine.restoreSnapshot (after, afterSettings); return true; }
        bool undo() override { engine.restoreSnapshot (before, beforeSettings); return true; }

    private:
        BridgeEngine& engine;
        std::vector<Mapping> before, after;
        GlobalSettings beforeSettings, afterSettings;
    };
}

BridgeEngine::BridgeEngine()
{
    loadFromFile (getDefaultProjectFile());
    restartReceiver();
    restartSender();
    restartTimer();
}

BridgeEngine::~BridgeEngine()
{
    receiver.removeListener (this);
    stopTimer();
}

//==============================================================================
void BridgeEngine::performSnapshotChange (const std::vector<Mapping>& before, const GlobalSettings& beforeSettings,
                                           const std::vector<Mapping>& after, const GlobalSettings& afterSettings,
                                           const juce::String& transactionName)
{
    undoManager.beginNewTransaction (transactionName);
    undoManager.perform (new SnapshotAction (*this, before, beforeSettings, after, afterSettings));
}

void BridgeEngine::setSettings (const GlobalSettings& newSettings)
{
    performSnapshotChange (mappings, settings, mappings, newSettings, "Change Settings");
}

Mapping* BridgeEngine::findMappingById (const juce::String& id)
{
    for (auto& m : mappings)
        if (m.id == id)
            return &m;
    return nullptr;
}

juce::String BridgeEngine::generateUniqueInputAddress() const
{
    for (int n = 1; ; ++n)
    {
        auto candidate = "/bridge/fader" + juce::String (n);
        if (! isInputAddressInUse (candidate, {}))
            return candidate;
    }
}

bool BridgeEngine::isInputAddressInUse (const juce::String& address, const juce::String& excludingMappingId) const
{
    for (auto& m : mappings)
        if (m.id != excludingMappingId && m.inputAddress == address)
            return true;
    return false;
}

bool BridgeEngine::isOutputAddressDuplicated (const juce::String& mappingId, int outputIndex) const
{
    auto* mapping = const_cast<BridgeEngine*> (this)->findMappingById (mappingId);
    if (mapping == nullptr || ! juce::isPositiveAndBelow (outputIndex, (int) mapping->outputs.size()))
        return false;

    const auto& address = mapping->outputs[(size_t) outputIndex].address;

    for (auto& m : mappings)
        for (int i = 0; i < (int) m.outputs.size(); ++i)
        {
            if (m.id == mappingId && i == outputIndex)
                continue;
            if (m.outputs[(size_t) i].address == address)
                return true;
        }

    return false;
}

juce::String BridgeEngine::addMapping()
{
    auto after = mappings;
    Mapping m;
    m.inputAddress = generateUniqueInputAddress();
    m.outputs.push_back (OutputTarget {});
    const auto newId = m.id;
    after.push_back (std::move (m));

    performSnapshotChange (mappings, settings, after, settings, "Add Mapping");
    return newId;
}

void BridgeEngine::removeMapping (const juce::String& id)
{
    auto after = mappings;
    after.erase (std::remove_if (after.begin(), after.end(),
                     [&] (const Mapping& m) { return m.id == id; }),
                 after.end());

    performSnapshotChange (mappings, settings, after, settings, "Remove Mapping");
}

void BridgeEngine::addOutput (const juce::String& mappingId)
{
    auto after = mappings;
    for (auto& m : after)
        if (m.id == mappingId)
        {
            m.outputs.push_back (OutputTarget {});
            break;
        }

    performSnapshotChange (mappings, settings, after, settings, "Add Output");
}

void BridgeEngine::removeOutput (const juce::String& mappingId, int outputIndex)
{
    auto after = mappings;
    for (auto& m : after)
        if (m.id == mappingId && juce::isPositiveAndBelow (outputIndex, (int) m.outputs.size()))
        {
            m.outputs.erase (m.outputs.begin() + outputIndex);
            break;
        }

    performSnapshotChange (mappings, settings, after, settings, "Remove Output");
}

void BridgeEngine::updateMapping (const juce::String& id, const std::function<void (Mapping&)>& mutator)
{
    auto after = mappings;
    for (auto& m : after)
        if (m.id == id)
        {
            mutator (m);
            break;
        }

    performSnapshotChange (mappings, settings, after, settings, "Edit Mapping");
}

//==============================================================================
bool BridgeEngine::canUndo() const { return undoManager.canUndo(); }
bool BridgeEngine::canRedo() const { return undoManager.canRedo(); }
void BridgeEngine::undo() { undoManager.undo(); }
void BridgeEngine::redo() { undoManager.redo(); }

void BridgeEngine::restoreSnapshot (const std::vector<Mapping>& newMappings, const GlobalSettings& newSettings)
{
    const bool receivePortChanged = newSettings.receivePort != settings.receivePort;
    const bool sendChanged        = newSettings.sendHost != settings.sendHost
                                      || newSettings.sendPort != settings.sendPort;
    const bool freqChanged        = std::abs (newSettings.updateFrequencyHz - settings.updateFrequencyHz) > 1.0e-9;

    mappings = newMappings;
    settings = newSettings;

    activeFades.erase (std::remove_if (activeFades.begin(), activeFades.end(),
                            [this] (const ActiveFade& f) { return findMappingById (f.mappingId) == nullptr; }),
                        activeFades.end());

    saveProject();

    if (receivePortChanged)
        restartReceiver();
    if (sendChanged)
        restartSender();
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
    activeFades.clear();
    settings = GlobalSettings();

    saveProject();
    restartReceiver();
    restartSender();
    restartTimer();

    if (onProjectChanged)
        onProjectChanged();
}

bool BridgeEngine::openProject (const juce::File& file)
{
    if (! file.existsAsFile())
        return false;

    auto parsed = juce::JSON::parse (file);
    if (! parsed.isObject())
        return false;

    currentProjectFile = file;
    undoManager.clearUndoHistory();

    settings = GlobalSettings::fromVar (parsed.getProperty ("settings", juce::var()));
    mappings.clear();
    if (auto* arr = parsed.getProperty ("mappings", juce::var()).getArray())
        for (auto& v : *arr)
            mappings.push_back (Mapping::fromVar (v));
    activeFades.clear();

    restartReceiver();
    restartSender();
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
    auto* root = new juce::DynamicObject();
    root->setProperty ("settings", settings.toVar());

    juce::Array<juce::var> mapArr;
    for (auto& m : mappings)
        mapArr.add (m.toVar());
    root->setProperty ("mappings", mapArr);

    auto file = getAutoSaveTarget();
    file.getParentDirectory().createDirectory();
    return file.replaceWithText (juce::JSON::toString (juce::var (root)));
}

juce::String BridgeEngine::getCurrentProjectDisplayName() const
{
    return currentProjectFile == juce::File() ? "Untitled" : currentProjectFile.getFileNameWithoutExtension();
}

//==============================================================================
void BridgeEngine::oscMessageReceived (const juce::OSCMessage& message)
{
    const auto address = message.getAddressPattern().toString();

    for (auto& m : mappings)
    {
        if (m.inputAddress != address)
            continue;

        if (message.size() < 2 || ! (message[0].isFloat32() || message[0].isInt32())
                                 || ! (message[1].isFloat32() || message[1].isInt32()))
        {
            log ("RX " + address + "  ignored: expected 2 numeric args (x, y)");
            continue;
        }

        const float x = message[0].isFloat32() ? message[0].getFloat32() : (float) message[0].getInt32();
        const float y = message[1].isFloat32() ? message[1].getFloat32() : (float) message[1].getInt32();

        const float start = m.hasLastValue ? m.lastValue : x;
        m.lastValue = x;
        m.hasLastValue = true;

        activeFades.erase (std::remove_if (activeFades.begin(), activeFades.end(),
                                [&] (const ActiveFade& f) { return f.mappingId == m.id; }),
                            activeFades.end());

        log ("RX " + address + "  x=" + juce::String (x, 3) + "  y=" + juce::String (y, 3) + "s");

        if (y <= 0.0f)
        {
            sendMappingValue (m, x);
            log ("Fade complete: " + describeOutputs (m, x));
        }
        else
        {
            ActiveFade f;
            f.mappingId = m.id;
            f.startValue = start;
            f.targetValue = x;
            f.startTime = juce::Time::getMillisecondCounterHiRes() / 1000.0;
            f.duration = (double) y;
            activeFades.push_back (f);
        }
    }
}

void BridgeEngine::timerCallback()
{
    if (activeFades.empty())
        return;

    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;

    for (auto it = activeFades.begin(); it != activeFades.end(); )
    {
        auto* mapping = findMappingById (it->mappingId);
        if (mapping == nullptr)
        {
            it = activeFades.erase (it);
            continue;
        }

        double t = it->duration > 0.0 ? (now - it->startTime) / it->duration : 1.0;
        const bool finished = t >= 1.0;
        t = juce::jlimit (0.0, 1.0, t);

        const float value = it->startValue + (float) t * (it->targetValue - it->startValue);
        sendMappingValue (*mapping, value);

        if (finished)
        {
            log ("Fade complete: " + describeOutputs (*mapping, value));
            it = activeFades.erase (it);
        }
        else
        {
            ++it;
        }
    }
}

void BridgeEngine::sendMappingValue (Mapping& m, float rawValue)
{
    const float lo = juce::jmin (m.inMin, m.inMax);
    const float hi = juce::jmax (m.inMin, m.inMax);
    const float clamped = juce::jlimit (lo, hi, rawValue);

    m.liveValue = clamped;
    m.hasLiveValue = true;

    for (auto& out : m.outputs)
    {
        const float scaled = juce::jmap (clamped, m.inMin, m.inMax, out.outMin, out.outMax);
        if (! sender.send (out.address, scaled))
            log ("TX FAILED " + out.address);
    }
}

juce::String BridgeEngine::describeOutputs (const Mapping& m, float rawValue) const
{
    const float lo = juce::jmin (m.inMin, m.inMax);
    const float hi = juce::jmax (m.inMin, m.inMax);
    const float clamped = juce::jlimit (lo, hi, rawValue);

    juce::StringArray parts;
    for (auto& out : m.outputs)
    {
        const float scaled = juce::jmap (clamped, m.inMin, m.inMax, out.outMin, out.outMax);
        parts.add (out.address + "=" + juce::String (scaled, 4));
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

void BridgeEngine::restartSender()
{
    sender.disconnect();

    if (sender.connect (settings.sendHost, settings.sendPort))
        log ("Sending OSC to " + settings.sendHost + ":" + juce::String (settings.sendPort));
    else
        log ("ERROR: failed to connect sender to " + settings.sendHost + ":" + juce::String (settings.sendPort));
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

void BridgeEngine::loadFromFile (const juce::File& file)
{
    if (! file.existsAsFile())
        return;

    auto parsed = juce::JSON::parse (file);
    if (! parsed.isObject())
        return;

    settings = GlobalSettings::fromVar (parsed.getProperty ("settings", juce::var()));

    mappings.clear();
    if (auto* arr = parsed.getProperty ("mappings", juce::var()).getArray())
        for (auto& v : *arr)
            mappings.push_back (Mapping::fromVar (v));
}
