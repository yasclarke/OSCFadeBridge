#pragma once

#include <functional>
#include <map>
#include <optional>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_osc/juce_osc.h>
#include "Mapping.h"

// One in-flight fade: ramping an input (mapping or scaler) from startValue
// to targetValue over duration seconds, ticked by the engine's timer.
struct ActiveFade
{
    juce::String inputId;
    float startValue = 0.0f;
    float targetValue = 0.0f;
    double startTime = 0.0;
    double duration = 0.0;
};

// Everything an undo step captures.
struct ProjectState
{
    std::vector<Mapping> mappings;
    std::vector<Scaler> scalers;
    GlobalSettings settings;
};

// Receives values on OSC addresses ([x, y] = value, fade seconds) or MIDI
// messages, tracks the last value seen per input, ramps toward each new
// value, and forwards the scaled, ramped value of each mapping to every
// output it targets - each output going to one of the project's named
// send targets (an OSC host:port or a MIDI output port). Scalers are
// inputs with no outputs that multiply the mappings scaled by them.
//
// All mutations go through an undo/redo stack, and the whole project can
// be saved to, or loaded from, an arbitrary file - see newProject()/
// openProject()/saveProjectAs().
class BridgeEngine : private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>,
                      private juce::MidiInputCallback,
                      private juce::AsyncUpdater,
                      private juce::Timer
{
public:
    BridgeEngine();
    ~BridgeEngine() override;

    const GlobalSettings& getSettings() const { return settings; }
    void setSettings (const GlobalSettings& newSettings);

    const std::vector<Mapping>& getMappings() const { return mappings; }
    const std::vector<Scaler>& getScalers() const { return scalers; }
    Mapping* findMappingById (const juce::String& id);
    const Mapping* findMappingById (const juce::String& id) const;
    const Scaler* findScalerById (const juce::String& id) const;

    // A mapping or a scaler.
    InputChannel* findInputById (const juce::String& id);
    const InputChannel* findInputById (const juce::String& id) const;

    // True if some OTHER OSC input - mapping or scaler - (id != excludingId)
    // already uses this address. Pass an empty excludingId to check against all.
    bool isInputAddressInUse (const juce::String& address, const juce::String& excludingId) const;

    // True if some other output - in this mapping or any other - already
    // sends the same address/MIDI message to the same target as
    // outputs[outputIndex] of mappingId.
    bool isOutputDuplicated (const juce::String& mappingId, int outputIndex) const;

    // Structural changes.
    juce::String addMapping();
    void removeMapping (const juce::String& id);
    void addOutput (const juce::String& mappingId);
    void removeOutput (const juce::String& mappingId, int outputIndex);
    juce::String addScaler();
    void removeScaler (const juce::String& id);   // mappings it scaled become unscaled

    // In-place field edits. updateInput() edits the input fields of either
    // a mapping or a scaler.
    void updateMapping (const juce::String& id, const std::function<void (Mapping&)>& mutator);
    void updateInput (const juce::String& id, const std::function<void (InputChannel&)>& mutator);

    // Send targets. Removing a target leaves its outputs unassigned.
    juce::String addTarget();
    void removeTarget (const juce::String& id);
    void updateTarget (const juce::String& id, const std::function<void (SendTarget&)>& mutator);
    bool isTargetConnected (const juce::String& id) const;

    // The scaler's normalised value mapped onto the mapping's scale range
    // (1 if unscaled, or if the scaler hasn't received a value yet).
    float getScaleFactor (const Mapping& m) const;

    // MIDI learn: the next CC or pitch bend received sets the input's MIDI
    // device/channel/message (and switches it to MIDI). A CC 0-31 followed
    // straight away by its +32 partner is learned as 14-bit.
    void startMidiLearn (const juce::String& inputId);
    void cancelMidiLearn();
    const juce::String& getMidiLearnInputId() const { return learnInputId; }

    juce::StringArray getMidiInputDeviceNames() const;
    juce::StringArray getMidiOutputDeviceNames() const;

    // Undo/redo - every mutation above is one undo step.
    bool canUndo() const;
    bool canRedo() const;
    void undo();
    void redo();

    // Project files. An empty (default-constructed) file means "untitled" -
    // the project still auto-saves, but to a scratch location rather than
    // a file the user chose.
    void newProject();
    bool openProject (const juce::File& file);
    bool saveProjectAs (const juce::File& file);
    bool saveProject();
    juce::File getCurrentProjectFile() const { return currentProjectFile; }
    juce::String getCurrentProjectDisplayName() const;

    // Used internally by the undo action to jump to a snapshot; public so
    // the (anonymous-namespace) UndoableAction can call it.
    void restoreSnapshot (const ProjectState& state);

    // GUI hooks.
    std::function<void (const juce::String&)> onLogMessage;
    std::function<void()> onProjectChanged;
    std::function<void()> onMidiDevicesChanged;

private:
    void oscMessageReceived (const juce::OSCMessage& message) override;
    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;

    template <typename Fn> void forEachInput (Fn&& fn)
    {
        for (auto& m : mappings) fn (static_cast<InputChannel&> (m));
        for (auto& s : scalers)  fn (static_cast<InputChannel&> (s));
    }

    std::optional<float> decodeMidiValue (InputChannel& input, const juce::MidiMessage& message);
    void flushPendingMsbs (double now);
    void handleMidiLearn (const juce::String& device, const juce::MidiMessage& message, double now);
    void finishMidiLearn (const juce::String& device, const MidiSpec& spec);

    // Starts a fade (or, for fadeSeconds <= 0, jumps) to x. Returns true if
    // the value was applied immediately. Call flushDirty() afterwards to send.
    bool applyInputValue (InputChannel& input, float x, double fadeSeconds);
    void setLiveValue (InputChannel& input, float rawValue);
    void flushDirty();
    void sendOutputs (const Mapping& m);
    void sendToOutput (const OutputTarget& out, float value);
    float getOutputValue (const Mapping& m, const OutputTarget& out) const;
    juce::String describeResult (const InputChannel& input) const;

    void restartReceiver();
    void restartOscSenders();
    void restartMidiOutputs();
    void restartMidiInputs();
    void restartTimer();
    void log (const juce::String& text);

    ProjectState captureState() const;
    void performSnapshotChange (const ProjectState& after, const juce::String& transactionName);

    juce::File getDefaultProjectFile() const;
    juce::File getAutoSaveTarget() const;
    bool loadFromFile (const juce::File& file);
    juce::String generateUniqueInputAddress (const juce::String& stem) const;

    GlobalSettings settings = GlobalSettings::makeDefault();
    std::vector<Mapping> mappings;
    std::vector<Scaler> scalers;
    std::vector<ActiveFade> activeFades;
    juce::StringArray dirtyInputIds;
    juce::File currentProjectFile;
    juce::UndoManager undoManager;

    juce::OSCReceiver receiver;
    std::map<juce::String, std::unique_ptr<juce::OSCSender>> oscSenders;     // by target id
    std::map<juce::String, std::unique_ptr<juce::MidiOutput>> midiOutputs;   // by target id
    std::vector<std::unique_ptr<juce::MidiInput>> midiInputs;
    juce::MidiDeviceListConnection midiDeviceListConnection;

    // A CC 0-31 seen while learning, waiting to see if its LSB follows.
    struct PendingLearn { juce::String device; int channel = 1; int number = 0; double time = 0.0; };
    juce::String learnInputId;
    std::optional<PendingLearn> pendingLearn;

    // MIDI arrives on a CoreMIDI thread; it's queued here and handled on
    // the message thread in handleAsyncUpdate().
    juce::CriticalSection pendingMidiLock;
    std::vector<std::pair<juce::String, juce::MidiMessage>> pendingMidi;
};
