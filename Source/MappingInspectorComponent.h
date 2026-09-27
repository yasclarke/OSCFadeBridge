#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include "BridgeEngine.h"

// One row in an OutputTarget's editor: address + output range + remove.
class OutputRowComponent : public juce::Component
{
public:
    OutputRowComponent (BridgeEngine& engineToUse, juce::String mappingIdToUse, int outputIndexToUse,
                         std::function<void()> onStructuralChange, std::function<void()> onEdited);

    void resized() override;

    // Highlights the address field if some other output (in any mapping)
    // targets the same address.
    void refreshDuplicateWarning();

private:
    BridgeEngine& engine;
    juce::String mappingId;
    int outputIndex;

    juce::TextEditor addressEditor;
    juce::TextEditor minEditor;
    juce::TextEditor maxEditor;
    juce::TextButton removeButton { "x" };
};

// The detail/edit panel for a single selected Mapping: input address +
// range, current (live) value, and its list of outputs. Lives in the
// inspector pane; swapped out whenever list selection changes.
class MappingInspectorComponent : public juce::Component
{
public:
    // onStructuralChange: mapping removed, or an output added/removed (shape changed).
    // onFieldsChanged: a text field committed (address/range) - list row should refresh.
    MappingInspectorComponent (BridgeEngine& engineToUse, juce::String mappingIdToUse,
                                std::function<void()> onStructuralChange,
                                std::function<void()> onFieldsChanged);

    void resized() override;

    void refreshCurrentValueLabel();
    void refreshOutputWarnings();
    int getPreferredHeight() const;

private:
    BridgeEngine& engine;
    juce::String mappingId;
    std::function<void()> notifyStructuralChange;
    std::function<void()> notifyFieldsChanged;

    juce::Label inputAddressLabel { {}, "Input Address" };
    juce::TextEditor inputAddressEditor;

    juce::Label inRangeLabel { {}, "Input Range (min / max)" };
    juce::TextEditor inMinEditor;
    juce::TextEditor inMaxEditor;

    juce::Label currentValueLabel;

    juce::Label outputsLabel { {}, "Outputs" };
    juce::Label outputsColumnHeader { {}, "Address" };
    juce::Label outputsRangeHeader { {}, "Out Range" };
    juce::OwnedArray<OutputRowComponent> outputRows;
    juce::TextButton addOutputButton { "+ Add Output" };

    juce::TextButton removeButton { "Remove Mapping" };
};
