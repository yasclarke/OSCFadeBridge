#pragma once

#include <functional>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_osc/juce_osc.h>
#include "Mapping.h"

// One in-flight fade: ramping a mapping's outputs from startValue to
// targetValue over duration seconds, ticked by the engine's timer.
struct ActiveFade
{
    juce::String mappingId;
    float startValue = 0.0f;
    float targetValue = 0.0f;
    double startTime = 0.0;
    double duration = 0.0;
};

// Receives OSC [x, y] messages on custom addresses, tracks the last x
// seen per mapping, ramps toward the new x over y seconds, and forwards
// the scaled, ramped value to every output address the mapping targets.
//
// All mapping/settings mutations go through an undo/redo stack, and the
// whole project (mappings + settings) can be saved to, or loaded from,
// an arbitrary file - see newProject()/openProject()/saveProjectAs().
class BridgeEngine : private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>,
                      private juce::Timer
{
public:
    BridgeEngine();
    ~BridgeEngine() override;

    const GlobalSettings& getSettings() const { return settings; }
    void setSettings (const GlobalSettings& newSettings);

    const std::vector<Mapping>& getMappings() const { return mappings; }
    Mapping* findMappingById (const juce::String& id);

    // True if some OTHER mapping (id != excludingMappingId) already uses this
    // input address. Pass an empty excludingMappingId to check against all.
    bool isInputAddressInUse (const juce::String& address, const juce::String& excludingMappingId) const;

    // True if some other output - in this mapping or any other - already
    // sends to the same address as outputs[outputIndex] of mappingId.
    bool isOutputAddressDuplicated (const juce::String& mappingId, int outputIndex) const;

    // Structural changes (rebuild the mapping list UI + persist).
    juce::String addMapping();
    void removeMapping (const juce::String& id);
    void addOutput (const juce::String& mappingId);
    void removeOutput (const juce::String& mappingId, int outputIndex);

    // In-place field edits (persist only, no UI rebuild needed).
    void updateMapping (const juce::String& id, const std::function<void (Mapping&)>& mutator);

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
    void restoreSnapshot (const std::vector<Mapping>& newMappings, const GlobalSettings& newSettings);

    // GUI hooks.
    std::function<void (const juce::String&)> onLogMessage;
    std::function<void()> onProjectChanged;

private:
    void oscMessageReceived (const juce::OSCMessage& message) override;
    void timerCallback() override;

    void sendMappingValue (Mapping& m, float rawValue);
    juce::String describeOutputs (const Mapping& m, float rawValue) const;
    void restartReceiver();
    void restartSender();
    void restartTimer();
    void log (const juce::String& text);
    void performSnapshotChange (const std::vector<Mapping>& before, const GlobalSettings& beforeSettings,
                                 const std::vector<Mapping>& after, const GlobalSettings& afterSettings,
                                 const juce::String& transactionName);

    juce::File getDefaultProjectFile() const;
    juce::File getAutoSaveTarget() const;
    void loadFromFile (const juce::File& file);
    juce::String generateUniqueInputAddress() const;

    GlobalSettings settings;
    std::vector<Mapping> mappings;
    std::vector<ActiveFade> activeFades;
    juce::File currentProjectFile;
    juce::UndoManager undoManager;

    juce::OSCReceiver receiver;
    juce::OSCSender sender;
};
