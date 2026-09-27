#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "BridgeEngine.h"

// Editor for the global OSC ports/host and the fade update frequency.
class GlobalSettingsComponent : public juce::Component
{
public:
    explicit GlobalSettingsComponent (BridgeEngine& engineToUse);

    void resized() override;
    static int getPreferredHeight() { return 46; }

    // Re-reads all fields from the engine (e.g. after an undo/redo or
    // project load changed settings out from under the UI).
    void refreshFromEngine();

private:
    void applyClicked();
    void layoutField (juce::Rectangle<int>& row, juce::Label& label, juce::TextEditor& editor, int width);

    BridgeEngine& engine;

    juce::Label receivePortLabel { {}, "Receive Port" };
    juce::TextEditor receivePortEditor;

    juce::Label sendHostLabel { {}, "Send Host" };
    juce::TextEditor sendHostEditor;

    juce::Label sendPortLabel { {}, "Send Port" };
    juce::TextEditor sendPortEditor;

    juce::Label updateHzLabel { {}, "Update Rate (Hz)" };
    juce::TextEditor updateHzEditor;
};
