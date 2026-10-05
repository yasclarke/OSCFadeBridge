#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "BridgeEngine.h"

// One row in a mapping's output list: send target, then either an OSC
// address or a MIDI message (type/channel/number) depending on the
// target's kind, then the output range and a remove button.
class OutputRowComponent : public juce::Component
{
public:
    OutputRowComponent (BridgeEngine& engineToUse, juce::String mappingIdToUse, int outputIndexToUse);

    void resized() override;

    // Re-reads all fields (and the target list) from the engine.
    void refreshFromEngine();

    // Highlights the destination if some other output (in any mapping)
    // sends the same thing to the same target.
    void refreshDuplicateWarning();

    // Column layout shared with the header labels above the rows.
    static constexpr int targetWidth = 130, rangeWidth = 64, removeWidth = 24, gap = 4;

private:
    void commitFields();
    const OutputTarget* getOutput() const;

    BridgeEngine& engine;
    juce::String mappingId;
    int outputIndex;

    juce::ComboBox targetBox;
    juce::StringArray targetIds;   // targetBox item id N+2 = targetIds[N]; id 1 = none
    juce::TextEditor addressEditor;
    juce::ComboBox midiTypeBox;
    juce::ComboBox channelBox;
    juce::TextEditor numberEditor;
    juce::TextEditor minEditor;
    juce::TextEditor maxEditor;
    juce::TextButton removeButton { "x" };
};

// The detail/edit panel for a single selected Mapping or Scaler: input
// source (OSC address or MIDI message), input range and current (live)
// value, plus - for mappings only - its scaler and list of outputs. Lives
// in the inspector pane; swapped out whenever list selection changes or
// outputs are added or removed, and refreshed in place for any other edit.
class MappingInspectorComponent : public juce::Component
{
public:
    MappingInspectorComponent (BridgeEngine& engineToUse, juce::String inputIdToUse, bool isScalerToUse);

    void resized() override;

    const juce::String& getInputId() const { return inputId; }
    bool isForScaler() const { return isScaler; }

    // False if the input is gone or its shape changed (outputs added/removed), so
    // this component must be rebuilt rather than refreshed in place.
    bool canRefreshInPlace() const;
    void refreshFromEngine();

    void refreshLiveDisplay();
    int getPreferredHeight() const;

private:
    void commitInputFields();
    void commitScaleFields();
    void updateInputVisibility();
    void refreshLearnButton();

    BridgeEngine& engine;
    juce::String inputId;
    bool isScaler;

    juce::Label nameLabel { {}, "Name" };   // scalers only
    juce::TextEditor nameEditor;

    juce::Label sourceLabel { {}, "Source" };
    juce::ComboBox sourceBox;
    juce::Label currentValueLabel;

    // OSC input
    juce::Label inputAddressLabel { {}, "Input Address" };
    juce::TextEditor inputAddressEditor;

    // MIDI input
    juce::Label midiDeviceLabel { {}, "Device" };
    juce::ComboBox midiDeviceBox;
    juce::StringArray midiDeviceChoices;   // item id N+2 = midiDeviceChoices[N]; id 1 = any
    juce::Label midiTypeLabel { {}, "Message" };
    juce::ComboBox midiTypeBox;
    juce::Label midiChannelLabel { {}, "Ch" };
    juce::ComboBox midiChannelBox;
    juce::Label midiNumberLabel { {}, "No." };
    juce::TextEditor midiNumberEditor;
    juce::Label midiFadeLabel { {}, "Fade (s)" };
    juce::TextEditor midiFadeEditor;
    juce::TextButton learnButton { "Learn" };

    juce::Label inRangeLabel { {}, "Input Range (min / max)" };
    juce::TextEditor inMinEditor;
    juce::TextEditor inMaxEditor;

    juce::Label scaledByLabel { {}, "Scaled By" };
    juce::ComboBox scaledByBox;
    juce::StringArray scaledByIds;   // item id N+2 = scaledByIds[N]; id 1 = none
    juce::Label scaleRangeLabel { {}, "Scaler Range (at 0 / at 1)" };
    juce::TextEditor scaleMinEditor;
    juce::TextEditor scaleMaxEditor;
    juce::Label scaledByHint { {}, "The scaler's 0-1 is mapped onto this range, then multiplies this input" };

    juce::Label outputsLabel { {}, "Outputs" };
    juce::Label outputsTargetHeader { {}, "Target" };
    juce::Label outputsColumnHeader { {}, "Address / MIDI Message" };
    juce::Label outputsRangeHeader { {}, "Out Range" };
    juce::OwnedArray<OutputRowComponent> outputRows;
    juce::TextButton addOutputButton { "+ Add Output" };

    juce::TextButton removeButton;
};
