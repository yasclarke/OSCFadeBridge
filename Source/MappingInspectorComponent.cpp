#include "MappingInspectorComponent.h"

//==============================================================================
OutputRowComponent::OutputRowComponent (BridgeEngine& engineToUse, juce::String mappingIdToUse, int outputIndexToUse,
                                         std::function<void()> onStructuralChange, std::function<void()> onEdited)
    : engine (engineToUse), mappingId (std::move (mappingIdToUse)), outputIndex (outputIndexToUse)
{
    auto* mapping = engine.findMappingById (mappingId);
    const auto& out = mapping->outputs[(size_t) outputIndex];

    addressEditor.setText (out.address, false);
    minEditor.setText (juce::String (out.outMin), false);
    maxEditor.setText (juce::String (out.outMax), false);

    addAndMakeVisible (addressEditor);
    addAndMakeVisible (minEditor);
    addAndMakeVisible (maxEditor);
    addAndMakeVisible (removeButton);

    refreshDuplicateWarning();

    auto commit = [this, onEdited]
    {
        engine.updateMapping (mappingId, [this] (Mapping& m)
        {
            auto& o = m.outputs[(size_t) outputIndex];
            o.address = addressEditor.getText().trim();
            o.outMin = (float) minEditor.getText().getDoubleValue();
            o.outMax = (float) maxEditor.getText().getDoubleValue();
        });
        if (onEdited)
            onEdited();
    };

    addressEditor.onFocusLost = commit;
    addressEditor.onReturnKey = commit;
    minEditor.onFocusLost = commit;
    minEditor.onReturnKey = commit;
    maxEditor.onFocusLost = commit;
    maxEditor.onReturnKey = commit;

    removeButton.onClick = [this, onStructuralChange]
    {
        engine.removeOutput (mappingId, outputIndex);
        if (onStructuralChange)
            onStructuralChange();
    };
}

void OutputRowComponent::refreshDuplicateWarning()
{
    const bool duplicated = engine.isOutputAddressDuplicated (mappingId, outputIndex);
    const auto warningColour = juce::Colours::orange;

    if (duplicated)
    {
        addressEditor.setColour (juce::TextEditor::outlineColourId, warningColour);
        addressEditor.setColour (juce::TextEditor::focusedOutlineColourId, warningColour);
        addressEditor.setTooltip ("Another output already sends to this address");
    }
    else
    {
        addressEditor.removeColour (juce::TextEditor::outlineColourId);
        addressEditor.removeColour (juce::TextEditor::focusedOutlineColourId);
        addressEditor.setTooltip ({});
    }
}

void OutputRowComponent::resized()
{
    auto area = getLocalBounds();
    removeButton.setBounds (area.removeFromRight (24));
    area.removeFromRight (4);
    maxEditor.setBounds (area.removeFromRight (70));
    area.removeFromRight (4);
    minEditor.setBounds (area.removeFromRight (70));
    area.removeFromRight (4);
    addressEditor.setBounds (area);
}

//==============================================================================
MappingInspectorComponent::MappingInspectorComponent (BridgeEngine& engineToUse, juce::String mappingIdToUse,
                                                        std::function<void()> onStructuralChange,
                                                        std::function<void()> onFieldsChanged)
    : engine (engineToUse), mappingId (std::move (mappingIdToUse)), notifyStructuralChange (onStructuralChange),
      notifyFieldsChanged (onFieldsChanged)
{
    auto* mapping = engine.findMappingById (mappingId);

    inputAddressEditor.setText (mapping->inputAddress, false);
    inMinEditor.setText (juce::String (mapping->inMin), false);
    inMaxEditor.setText (juce::String (mapping->inMax), false);

    addAndMakeVisible (inputAddressLabel);
    addAndMakeVisible (inputAddressEditor);
    addAndMakeVisible (inRangeLabel);
    addAndMakeVisible (inMinEditor);
    addAndMakeVisible (inMaxEditor);
    addAndMakeVisible (currentValueLabel);
    addAndMakeVisible (outputsLabel);
    addAndMakeVisible (outputsColumnHeader);
    addAndMakeVisible (outputsRangeHeader);
    addAndMakeVisible (addOutputButton);
    addAndMakeVisible (removeButton);

    inputAddressLabel.setFont (juce::Font (juce::FontOptions (13.0f)));
    inRangeLabel.setFont (juce::Font (juce::FontOptions (13.0f)));
    outputsColumnHeader.setFont (juce::Font (juce::FontOptions (12.0f)));
    outputsRangeHeader.setFont (juce::Font (juce::FontOptions (12.0f)));
    for (auto* l : { &inputAddressLabel, &inRangeLabel })
        l->setColour (juce::Label::textColourId, juce::Colours::grey);
    for (auto* l : { &outputsColumnHeader, &outputsRangeHeader })
        l->setColour (juce::Label::textColourId, juce::Colours::grey);
    outputsRangeHeader.setJustificationType (juce::Justification::centredRight);

    outputsLabel.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));

    currentValueLabel.setJustificationType (juce::Justification::centredRight);
    currentValueLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    refreshCurrentValueLabel();

    removeButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe07a6b));

    auto commitFields = [this]
    {
        const auto requestedAddress = inputAddressEditor.getText().trim();
        const bool addressAvailable = requestedAddress.isNotEmpty()
                                         && ! engine.isInputAddressInUse (requestedAddress, mappingId);

        engine.updateMapping (mappingId, [this, requestedAddress, addressAvailable] (Mapping& m)
        {
            if (addressAvailable)
                m.inputAddress = requestedAddress;
            m.inMin = (float) inMinEditor.getText().getDoubleValue();
            m.inMax = (float) inMaxEditor.getText().getDoubleValue();
        });

        if (! addressAvailable)
            if (auto* m = engine.findMappingById (mappingId))
                inputAddressEditor.setText (m->inputAddress, false);

        if (notifyFieldsChanged)
            notifyFieldsChanged();
    };

    for (auto* editor : { &inputAddressEditor, &inMinEditor, &inMaxEditor })
    {
        editor->onFocusLost = commitFields;
        editor->onReturnKey = commitFields;
    }

    removeButton.onClick = [this]
    {
        juce::String address = mappingId;
        if (auto* m = engine.findMappingById (mappingId))
            address = m->inputAddress;

        auto options = juce::MessageBoxOptions::makeOptionsOkCancel (
            juce::MessageBoxIconType::WarningIcon, "Remove Mapping",
            "Remove the mapping for \"" + address + "\"? This can be undone with Cmd+Z.",
            "Remove", "Cancel");

        // The dialog is async and this inspector may be destroyed before the
        // user responds (e.g. selecting a different mapping) - capture only
        // values/pointers that outlive it, never `this`.
        BridgeEngine* enginePtr = &engine;
        juce::String idToRemove = mappingId;
        auto onRemoved = notifyStructuralChange;

        juce::NativeMessageBox::showAsync (options, [enginePtr, idToRemove, onRemoved] (int result)
        {
            if (result == 0) // button index 0 = the first button passed to makeOptionsOkCancel ("Remove")
            {
                enginePtr->removeMapping (idToRemove);
                if (onRemoved)
                    onRemoved();
            }
        });
    };

    addOutputButton.onClick = [this]
    {
        engine.addOutput (mappingId);
        if (notifyStructuralChange)
            notifyStructuralChange();
    };

    for (int i = 0; i < (int) mapping->outputs.size(); ++i)
    {
        auto* row = outputRows.add (new OutputRowComponent (engine, mappingId, i, notifyStructuralChange,
                                                              [this] { refreshOutputWarnings(); }));
        addAndMakeVisible (row);
    }
}

void MappingInspectorComponent::refreshOutputWarnings()
{
    for (auto* row : outputRows)
        row->refreshDuplicateWarning();
}

void MappingInspectorComponent::refreshCurrentValueLabel()
{
    if (auto* m = engine.findMappingById (mappingId))
    {
        currentValueLabel.setText (m->hasLiveValue ? ("Current value: " + juce::String (m->liveValue, 3))
                                                     : "Current value: --",
                                    juce::dontSendNotification);
    }
}

int MappingInspectorComponent::getPreferredHeight() const
{
    const int fieldHeight = 26;
    const int labelHeight = 16;
    const int gap = 6;
    const int padding = 24;

    return padding
             + labelHeight + 2 + fieldHeight + gap     // input address
             + labelHeight + 2 + fieldHeight + 16      // input range
             + 22 + 4                                  // outputs label
             + labelHeight + 2                         // outputs column header
             + (int) outputRows.size() * (fieldHeight + gap)
             + 26 + 20                                 // add output button
             + 28;                                     // remove button
}

void MappingInspectorComponent::resized()
{
    auto area = getLocalBounds().reduced (12);
    const int labelHeight = 16;
    const int fieldHeight = 26;
    const int gap = 6;

    {
        auto row = area.removeFromTop (labelHeight);
        inputAddressLabel.setBounds (row.removeFromLeft (200));
        currentValueLabel.setBounds (row);
        area.removeFromTop (2);
        inputAddressEditor.setBounds (area.removeFromTop (fieldHeight));
        area.removeFromTop (gap);
    }

    {
        inRangeLabel.setBounds (area.removeFromTop (labelHeight));
        area.removeFromTop (2);
        auto row = area.removeFromTop (fieldHeight);
        const int fieldWidth = (row.getWidth() - 8) / 2;
        inMinEditor.setBounds (row.removeFromLeft (fieldWidth));
        row.removeFromLeft (8);
        inMaxEditor.setBounds (row.removeFromLeft (fieldWidth));
        area.removeFromTop (16);
    }

    outputsLabel.setBounds (area.removeFromTop (22));
    area.removeFromTop (4);

    {
        auto row = area.removeFromTop (labelHeight);
        row.removeFromRight (24 + 4 + 70 + 4); // align with remove/max column below
        outputsRangeHeader.setBounds (row.removeFromRight (70 + 4 + 70));
        outputsColumnHeader.setBounds (row);
        area.removeFromTop (2);
    }

    for (auto* row : outputRows)
    {
        row->setBounds (area.removeFromTop (fieldHeight));
        area.removeFromTop (gap);
    }

    addOutputButton.setBounds (area.removeFromTop (26).removeFromLeft (140));
    area.removeFromTop (20);

    removeButton.setBounds (area.removeFromTop (28).removeFromLeft (160));
}
