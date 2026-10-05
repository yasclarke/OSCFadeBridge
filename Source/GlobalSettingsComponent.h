#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "BridgeEngine.h"

// One send target: name, kind (OSC/MIDI), then host + port or MIDI
// device, with a dot showing whether its connection is open.
class TargetRowComponent : public juce::Component, public juce::SettableTooltipClient
{
public:
    TargetRowComponent (BridgeEngine& engineToUse, juce::String targetIdToUse);

    void paint (juce::Graphics& g) override;
    void resized() override;

    // Re-reads all fields (and the MIDI device list) from the engine.
    void refreshFromEngine();

    // Column layout shared with the header labels above the rows.
    static constexpr int statusWidth = 16, nameWidth = 150, kindWidth = 80, hostWidth = 180,
                         portWidth = 80, removeWidth = 24, gap = 6;

private:
    void commit();

    BridgeEngine& engine;
    juce::String targetId;

    juce::TextEditor nameEditor;
    juce::ComboBox kindBox;
    juce::TextEditor hostEditor;
    juce::TextEditor portEditor;
    juce::ComboBox deviceBox;
    juce::StringArray deviceChoices;   // deviceBox item id N+2 = deviceChoices[N]
    juce::TextButton removeButton { "x" };
};

// Editor for the receive port, the fade update frequency, and the list
// of send targets that outputs can be routed to.
class GlobalSettingsComponent : public juce::Component, private juce::AsyncUpdater
{
public:
    explicit GlobalSettingsComponent (BridgeEngine& engineToUse);

    void resized() override;
    int getPreferredHeight() const;

    // Re-reads all fields from the engine (e.g. after an undo/redo or
    // project load changed settings out from under the UI). If targets
    // were added or removed, the rows are rebuilt asynchronously - this
    // can be called from inside one of the rows' own callbacks.
    void refreshFromEngine();

    // Called after the rows are rebuilt, so the owner can re-layout.
    std::function<void()> onPreferredHeightChanged;

private:
    void handleAsyncUpdate() override;
    void rebuildRows();
    juce::String computeRowSignature() const;
    void applyClicked();
    void layoutField (juce::Rectangle<int>& row, juce::Label& label, juce::TextEditor& editor, int width);

    BridgeEngine& engine;

    juce::Label receivePortLabel { {}, "Receive Port" };
    juce::TextEditor receivePortEditor;

    juce::Label updateHzLabel { {}, "Update Rate (Hz)" };
    juce::TextEditor updateHzEditor;

    juce::Label targetsLabel { {}, "Send Targets" };
    juce::TextButton addTargetButton { "+ Add Target" };
    juce::Label nameHeader { {}, "Name" };
    juce::Label kindHeader { {}, "Type" };
    juce::Label destinationHeader { {}, "Host / MIDI Device" };
    juce::Label portHeader { {}, "Port" };

    juce::OwnedArray<TargetRowComponent> rows;
    juce::String rowSignature;
};
