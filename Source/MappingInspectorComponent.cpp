#include "MappingInspectorComponent.h"

namespace
{
    constexpr int labelHeight = 16;
    constexpr int fieldHeight = 26;
    constexpr int gap = 6;

    void populateMidiTypeBox (juce::ComboBox& box)
    {
        box.addItem ("CC", 1);
        box.addItem ("14-bit CC", 2);
        box.addItem ("Pitch Bend", 3);
    }

    int midiTypeToItemId (MidiMessageType t)
    {
        switch (t)
        {
            case MidiMessageType::controlChange14Bit: return 2;
            case MidiMessageType::pitchBend:          return 3;
            case MidiMessageType::controlChange:      break;
        }
        return 1;
    }

    MidiMessageType itemIdToMidiType (int itemId)
    {
        if (itemId == 2) return MidiMessageType::controlChange14Bit;
        if (itemId == 3) return MidiMessageType::pitchBend;
        return MidiMessageType::controlChange;
    }

    void populateChannelBox (juce::ComboBox& box)
    {
        for (int ch = 1; ch <= 16; ++ch)
            box.addItem (juce::String (ch), ch);
    }

    // 14-bit CC uses number for the MSB and number + 32 for the LSB.
    int clampMidiNumber (MidiMessageType type, int number)
    {
        return juce::jlimit (0, type == MidiMessageType::controlChange14Bit ? 31 : 127, number);
    }

    void styleCaption (juce::Label& label)
    {
        label.setFont (juce::Font (juce::FontOptions (12.0f)));
        label.setColour (juce::Label::textColourId, juce::Colours::grey);
    }
}

//==============================================================================
OutputRowComponent::OutputRowComponent (BridgeEngine& engineToUse, juce::String mappingIdToUse, int outputIndexToUse)
    : engine (engineToUse), mappingId (std::move (mappingIdToUse)), outputIndex (outputIndexToUse)
{
    populateMidiTypeBox (midiTypeBox);
    populateChannelBox (channelBox);
    numberEditor.setInputRestrictions (3, "0123456789");
    channelBox.setTooltip ("MIDI channel");
    numberEditor.setTooltip ("Controller number (MSB controller for 14-bit CC)");

    for (auto* c : std::initializer_list<juce::Component*> { &targetBox, &addressEditor, &midiTypeBox, &channelBox,
                                                              &numberEditor, &minEditor, &maxEditor, &removeButton })
        addAndMakeVisible (c);

    targetBox.onChange = [this]
    {
        const int index = targetBox.getSelectedId() - 2;
        const auto newTargetId = juce::isPositiveAndBelow (index, targetIds.size()) ? targetIds[index] : juce::String();
        const auto& settings = engine.getSettings();

        engine.updateMapping (mappingId, [&] (Mapping& m)
        {
            if (! juce::isPositiveAndBelow (outputIndex, (int) m.outputs.size()))
                return;

            auto& o = m.outputs[(size_t) outputIndex];
            auto* oldTarget = settings.findTarget (o.targetId);
            auto* newTarget = settings.findTarget (newTargetId);
            const bool wasMidi = oldTarget != nullptr && oldTarget->kind == TargetKind::midi;
            const bool isMidi  = newTarget != nullptr && newTarget->kind == TargetKind::midi;

            o.targetId = newTargetId;

            // OSC and MIDI ranges are in different units - a 0-1 OSC range
            // would barely move a 0-127 CC, so reset to the new kind's full range.
            if (wasMidi != isMidi)
            {
                o.outMin = 0.0f;
                o.outMax = isMidi ? o.midi.maxValue() : 1.0f;
            }
        });
    };

    midiTypeBox.onChange = [this]
    {
        const auto type = itemIdToMidiType (midiTypeBox.getSelectedId());

        engine.updateMapping (mappingId, [&] (Mapping& m)
        {
            if (! juce::isPositiveAndBelow (outputIndex, (int) m.outputs.size()))
                return;

            auto& o = m.outputs[(size_t) outputIndex];
            o.midi.type = type;
            o.midi.number = clampMidiNumber (type, o.midi.number);
            o.outMin = 0.0f;
            o.outMax = o.midi.maxValue();
        });
    };

    channelBox.onChange = [this] { commitFields(); };

    for (auto* editor : { &addressEditor, &numberEditor, &minEditor, &maxEditor })
    {
        editor->onFocusLost = [this] { commitFields(); };
        editor->onReturnKey = [this] { commitFields(); };
    }

    removeButton.onClick = [this]
    {
        // The inspector is rebuilt asynchronously, so `this` survives this call.
        engine.removeOutput (mappingId, outputIndex);
    };

    refreshFromEngine();
}

const OutputTarget* OutputRowComponent::getOutput() const
{
    auto* m = engine.findMappingById (mappingId);
    if (m == nullptr || ! juce::isPositiveAndBelow (outputIndex, (int) m->outputs.size()))
        return nullptr;
    return &m->outputs[(size_t) outputIndex];
}

void OutputRowComponent::commitFields()
{
    const auto address = addressEditor.getText().trim();
    const int channel = channelBox.getSelectedId();
    const int number = numberEditor.getText().getIntValue();
    const auto outMin = (float) minEditor.getText().getDoubleValue();
    const auto outMax = (float) maxEditor.getText().getDoubleValue();

    engine.updateMapping (mappingId, [&] (Mapping& m)
    {
        if (! juce::isPositiveAndBelow (outputIndex, (int) m.outputs.size()))
            return;

        auto& o = m.outputs[(size_t) outputIndex];
        if (address.isNotEmpty())
            o.address = address;
        if (channel >= 1 && channel <= 16)
            o.midi.channel = channel;
        o.midi.number = clampMidiNumber (o.midi.type, number);
        o.outMin = outMin;
        o.outMax = outMax;
    });
}

void OutputRowComponent::refreshFromEngine()
{
    auto* out = getOutput();
    if (out == nullptr)
        return;

    const auto& settings = engine.getSettings();

    targetIds.clear();
    targetBox.clear (juce::dontSendNotification);
    targetBox.addItem ("(none)", 1);
    for (auto& t : settings.targets)
    {
        targetBox.addItem (t.name + (t.kind == TargetKind::midi ? " (MIDI)" : ""), targetIds.size() + 2);
        targetIds.add (t.id);
    }

    const int targetIndex = targetIds.indexOf (out->targetId);
    targetBox.setSelectedId (targetIndex >= 0 ? targetIndex + 2 : 1, juce::dontSendNotification);

    auto* target = settings.findTarget (out->targetId);
    const bool isMidi = target != nullptr && target->kind == TargetKind::midi;

    addressEditor.setText (out->address, false);
    midiTypeBox.setSelectedId (midiTypeToItemId (out->midi.type), juce::dontSendNotification);
    channelBox.setSelectedId (out->midi.channel, juce::dontSendNotification);
    numberEditor.setText (juce::String (out->midi.number), false);
    minEditor.setText (juce::String (out->outMin), false);
    maxEditor.setText (juce::String (out->outMax), false);

    addressEditor.setVisible (! isMidi);
    addressEditor.setEnabled (target != nullptr);
    midiTypeBox.setVisible (isMidi);
    channelBox.setVisible (isMidi);
    numberEditor.setVisible (isMidi && out->midi.type != MidiMessageType::pitchBend);

    refreshDuplicateWarning();
}

void OutputRowComponent::refreshDuplicateWarning()
{
    const bool duplicated = engine.isOutputDuplicated (mappingId, outputIndex);
    const auto warningColour = juce::Colours::orange;

    for (auto* editor : { &addressEditor, &numberEditor })
    {
        if (duplicated)
        {
            editor->setColour (juce::TextEditor::outlineColourId, warningColour);
            editor->setColour (juce::TextEditor::focusedOutlineColourId, warningColour);
            editor->setTooltip ("Another output already sends this to the same target");
        }
        else
        {
            editor->removeColour (juce::TextEditor::outlineColourId);
            editor->removeColour (juce::TextEditor::focusedOutlineColourId);
            editor->setTooltip ({});
        }
    }
}

void OutputRowComponent::resized()
{
    auto area = getLocalBounds();

    targetBox.setBounds (area.removeFromLeft (targetWidth));
    area.removeFromLeft (gap);

    removeButton.setBounds (area.removeFromRight (removeWidth));
    area.removeFromRight (gap);
    maxEditor.setBounds (area.removeFromRight (rangeWidth));
    area.removeFromRight (gap);
    minEditor.setBounds (area.removeFromRight (rangeWidth));
    area.removeFromRight (gap);

    addressEditor.setBounds (area);

    midiTypeBox.setBounds (area.removeFromLeft (100));
    area.removeFromLeft (gap);
    channelBox.setBounds (area.removeFromLeft (54));
    area.removeFromLeft (gap);
    numberEditor.setBounds (area.removeFromLeft (50));
}

//==============================================================================
MappingInspectorComponent::MappingInspectorComponent (BridgeEngine& engineToUse, juce::String inputIdToUse,
                                                        bool isScalerToUse)
    : engine (engineToUse), inputId (std::move (inputIdToUse)), isScaler (isScalerToUse)
{
    sourceBox.addItem ("OSC", 1);
    sourceBox.addItem ("MIDI", 2);
    populateMidiTypeBox (midiTypeBox);
    populateChannelBox (midiChannelBox);
    midiNumberEditor.setInputRestrictions (3, "0123456789");
    midiFadeEditor.setInputRestrictions (8, "0123456789.");
    midiFadeEditor.setTooltip ("Fade time for each new MIDI value. 0 = follow instantly (e.g. a hardware fader)");
    midiNumberEditor.setTooltip ("Controller number (MSB controller for 14-bit CC; LSB is this + 32)");

    for (auto* c : std::initializer_list<juce::Component*> {
             &nameLabel, &nameEditor, &learnButton, &scaleRangeLabel, &scaleMinEditor, &scaleMaxEditor,
             &sourceLabel, &sourceBox, &currentValueLabel,
             &inputAddressLabel, &inputAddressEditor,
             &midiDeviceLabel, &midiDeviceBox, &midiTypeLabel, &midiTypeBox, &midiChannelLabel, &midiChannelBox,
             &midiNumberLabel, &midiNumberEditor, &midiFadeLabel, &midiFadeEditor,
             &inRangeLabel, &inMinEditor, &inMaxEditor,
             &scaledByLabel, &scaledByBox, &scaledByHint,
             &outputsLabel, &outputsTargetHeader, &outputsColumnHeader, &outputsRangeHeader,
             &addOutputButton, &removeButton })
        addAndMakeVisible (c);

    for (auto* l : { &nameLabel, &scaleRangeLabel, &sourceLabel, &inputAddressLabel, &midiDeviceLabel, &midiTypeLabel, &midiChannelLabel,
                     &midiNumberLabel, &midiFadeLabel, &inRangeLabel, &scaledByLabel, &scaledByHint,
                     &outputsTargetHeader, &outputsColumnHeader, &outputsRangeHeader })
        styleCaption (*l);
    outputsRangeHeader.setJustificationType (juce::Justification::centredRight);

    outputsLabel.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));

    currentValueLabel.setJustificationType (juce::Justification::centredRight);
    currentValueLabel.setColour (juce::Label::textColourId, juce::Colours::grey);

    removeButton.setButtonText (isScaler ? "Remove Scaler" : "Remove Mapping");
    removeButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe07a6b));

    nameLabel.setVisible (isScaler);
    nameEditor.setVisible (isScaler);

    for (auto* c : std::initializer_list<juce::Component*> { &scaledByLabel, &scaledByBox, &scaledByHint, &outputsLabel,
                                                              &scaleRangeLabel, &scaleMinEditor, &scaleMaxEditor,
                                                              &outputsTargetHeader, &outputsColumnHeader,
                                                              &outputsRangeHeader, &addOutputButton })
        c->setVisible (! isScaler);

    sourceBox.onChange = [this]
    {
        const auto newSource = sourceBox.getSelectedId() == 2 ? InputSource::midi : InputSource::osc;

        engine.updateInput (inputId, [newSource] (InputChannel& m)
        {
            if (m.source == newSource)
                return;

            // MIDI values are raw (0-127 / 0-16383), OSC is usually 0-1.
            m.source = newSource;
            m.inMin = 0.0f;
            m.inMax = newSource == InputSource::midi ? m.midiIn.maxValue() : 1.0f;
        });
    };

    midiTypeBox.onChange = [this]
    {
        const auto type = itemIdToMidiType (midiTypeBox.getSelectedId());

        engine.updateInput (inputId, [type] (InputChannel& m)
        {
            m.midiIn.type = type;
            m.midiIn.number = clampMidiNumber (type, m.midiIn.number);
            m.inMin = 0.0f;
            m.inMax = m.midiIn.maxValue();
        });
    };

    for (auto* box : { &midiDeviceBox, &midiChannelBox })
        box->onChange = [this] { commitInputFields(); };

    learnButton.setTooltip ("Listen for the next MIDI CC or pitch bend and use it for this input");
    learnButton.onClick = [this]
    {
        if (engine.getMidiLearnInputId() == inputId)
            engine.cancelMidiLearn();
        else
            engine.startMidiLearn (inputId);
        refreshLearnButton();
    };

    auto commitName = [this]
    {
        const auto name = nameEditor.getText().trim();
        if (name.isEmpty())
        {
            refreshFromEngine();
            return;
        }

        engine.updateInput (inputId, [&] (InputChannel& input)
        {
            if (auto* scaler = dynamic_cast<Scaler*> (&input))
                scaler->name = name;
        });
    };
    nameEditor.onFocusLost = commitName;
    nameEditor.onReturnKey = commitName;

    for (auto* editor : { &scaleMinEditor, &scaleMaxEditor })
    {
        editor->setTooltip ("What the scaler multiplies this input by when it's at 0 / at 1. "
                            "E.g. 0.9 / 1 lets it trim this input only slightly");
        editor->onFocusLost = [this] { commitScaleFields(); };
        editor->onReturnKey = [this] { commitScaleFields(); };
    }

    scaledByBox.onChange = [this]
    {
        const int index = scaledByBox.getSelectedId() - 2;
        const auto scalerId = juce::isPositiveAndBelow (index, scaledByIds.size()) ? scaledByIds[index] : juce::String();
        engine.updateMapping (inputId, [&] (Mapping& m) { m.scaledById = scalerId; });
    };

    for (auto* editor : { &inputAddressEditor, &midiNumberEditor, &midiFadeEditor, &inMinEditor, &inMaxEditor })
    {
        editor->onFocusLost = [this] { commitInputFields(); };
        editor->onReturnKey = [this] { commitInputFields(); };
    }

    removeButton.onClick = [this]
    {
        juce::String name = inputId;
        if (auto* input = engine.findInputById (inputId))
            name = input->getDisplayName();

        const juce::String kind = isScaler ? "scaler" : "mapping";
        auto options = juce::MessageBoxOptions::makeOptionsOkCancel (
            juce::MessageBoxIconType::WarningIcon, removeButton.getButtonText(),
            "Remove the " + kind + " for \"" + name + "\"? This can be undone with Cmd+Z.",
            "Remove", "Cancel");

        // The dialog is async and this inspector may be destroyed before the
        // user responds (e.g. selecting a different mapping) - capture only
        // values/pointers that outlive it, never `this`.
        BridgeEngine* enginePtr = &engine;
        juce::String idToRemove = inputId;
        const bool removingScaler = isScaler;

        juce::NativeMessageBox::showAsync (options, [enginePtr, idToRemove, removingScaler] (int result)
        {
            if (result != 0) // button index 0 = the first button passed to makeOptionsOkCancel ("Remove")
                return;

            if (removingScaler)
                enginePtr->removeScaler (idToRemove);
            else
                enginePtr->removeMapping (idToRemove);
        });
    };

    addOutputButton.onClick = [this] { engine.addOutput (inputId); };

    if (auto* mapping = engine.findMappingById (inputId); mapping != nullptr && ! isScaler)
        for (int i = 0; i < (int) mapping->outputs.size(); ++i)
            addAndMakeVisible (outputRows.add (new OutputRowComponent (engine, inputId, i)));

    refreshFromEngine();
}

void MappingInspectorComponent::commitInputFields()
{
    const auto requestedAddress = inputAddressEditor.getText().trim();
    const bool addressAvailable = requestedAddress.isNotEmpty()
                                     && ! engine.isInputAddressInUse (requestedAddress, inputId);

    const int deviceIndex = midiDeviceBox.getSelectedId() - 2;
    const auto device = juce::isPositiveAndBelow (deviceIndex, midiDeviceChoices.size()) ? midiDeviceChoices[deviceIndex]
                                                                                         : juce::String();
    const int channel = midiChannelBox.getSelectedId();
    const int number = midiNumberEditor.getText().getIntValue();
    const double fadeSeconds = juce::jmax (0.0, midiFadeEditor.getText().getDoubleValue());
    const auto inMin = (float) inMinEditor.getText().getDoubleValue();
    const auto inMax = (float) inMaxEditor.getText().getDoubleValue();

    // A rejected address is put back by the refresh that follows the update.
    engine.updateInput (inputId, [&] (InputChannel& m)
    {
        if (addressAvailable)
            m.inputAddress = requestedAddress;
        m.midiDevice = device;
        if (channel >= 1 && channel <= 16)
            m.midiIn.channel = channel;
        m.midiIn.number = clampMidiNumber (m.midiIn.type, number);
        m.midiFadeSeconds = fadeSeconds;
        m.inMin = inMin;
        m.inMax = inMax;
    });
}

void MappingInspectorComponent::commitScaleFields()
{
    const auto scaleMin = (float) scaleMinEditor.getText().getDoubleValue();
    const auto scaleMax = (float) scaleMaxEditor.getText().getDoubleValue();

    engine.updateMapping (inputId, [&] (Mapping& m)
    {
        m.scaleMin = scaleMin;
        m.scaleMax = scaleMax;
    });
}

void MappingInspectorComponent::refreshLearnButton()
{
    const bool learning = engine.getMidiLearnInputId() == inputId;
    learnButton.setButtonText (learning ? "Listening..." : "Learn");
    learnButton.setToggleState (learning, juce::dontSendNotification);
    learnButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd9a441));
}

bool MappingInspectorComponent::canRefreshInPlace() const
{
    if (isScaler)
        return engine.findScalerById (inputId) != nullptr;

    auto* m = engine.findMappingById (inputId);
    return m != nullptr && (int) m->outputs.size() == outputRows.size();
}

void MappingInspectorComponent::refreshFromEngine()
{
    auto* m = engine.findInputById (inputId);
    if (m == nullptr)
        return;

    if (auto* scaler = engine.findScalerById (inputId))
        nameEditor.setText (scaler->name, false);

    sourceBox.setSelectedId (m->source == InputSource::midi ? 2 : 1, juce::dontSendNotification);
    inputAddressEditor.setText (m->inputAddress, false);

    midiDeviceChoices = engine.getMidiInputDeviceNames();
    const bool deviceMissing = m->midiDevice.isNotEmpty() && ! midiDeviceChoices.contains (m->midiDevice);
    if (deviceMissing)
        midiDeviceChoices.add (m->midiDevice);

    midiDeviceBox.clear (juce::dontSendNotification);
    midiDeviceBox.addItem ("Any device", 1);
    for (int i = 0; i < midiDeviceChoices.size(); ++i)
    {
        const bool isMissing = deviceMissing && i == midiDeviceChoices.size() - 1;
        midiDeviceBox.addItem (midiDeviceChoices[i] + (isMissing ? " (not connected)" : ""), i + 2);
    }
    midiDeviceBox.setSelectedId (m->midiDevice.isEmpty() ? 1 : midiDeviceChoices.indexOf (m->midiDevice) + 2,
                                 juce::dontSendNotification);

    midiTypeBox.setSelectedId (midiTypeToItemId (m->midiIn.type), juce::dontSendNotification);
    midiChannelBox.setSelectedId (m->midiIn.channel, juce::dontSendNotification);
    midiNumberEditor.setText (juce::String (m->midiIn.number), false);
    midiFadeEditor.setText (juce::String (m->midiFadeSeconds), false);

    inMinEditor.setText (juce::String (m->inMin), false);
    inMaxEditor.setText (juce::String (m->inMax), false);

    if (auto* mapping = engine.findMappingById (inputId))
    {
        scaledByIds.clear();
        scaledByBox.clear (juce::dontSendNotification);
        scaledByBox.addItem ("(none)", 1);
        for (auto& scaler : engine.getScalers())
        {
            scaledByBox.addItem (scaler.getDisplayName(), scaledByIds.size() + 2);
            scaledByIds.add (scaler.id);
        }
        const int scalerIndex = scaledByIds.indexOf (mapping->scaledById);
        scaledByBox.setSelectedId (scalerIndex >= 0 ? scalerIndex + 2 : 1, juce::dontSendNotification);

        scaleMinEditor.setText (juce::String (mapping->scaleMin), false);
        scaleMaxEditor.setText (juce::String (mapping->scaleMax), false);
        scaleMinEditor.setEnabled (scalerIndex >= 0);
        scaleMaxEditor.setEnabled (scalerIndex >= 0);
    }

    for (auto* row : outputRows)
        row->refreshFromEngine();

    updateInputVisibility();
    refreshLiveDisplay();
}

void MappingInspectorComponent::updateInputVisibility()
{
    auto* m = engine.findInputById (inputId);
    if (m == nullptr)
        return;

    const bool isMidi = m->source == InputSource::midi;
    const bool hasNumber = m->midiIn.type != MidiMessageType::pitchBend;

    inputAddressLabel.setVisible (! isMidi);
    inputAddressEditor.setVisible (! isMidi);

    for (auto* c : std::initializer_list<juce::Component*> { &midiDeviceLabel, &midiDeviceBox, &midiTypeLabel, &midiTypeBox,
                                                              &midiChannelLabel, &midiChannelBox, &midiFadeLabel, &midiFadeEditor })
        c->setVisible (isMidi);

    midiNumberLabel.setVisible (isMidi && hasNumber);
    midiNumberEditor.setVisible (isMidi && hasNumber);
}

void MappingInspectorComponent::refreshLiveDisplay()
{
    auto* input = engine.findInputById (inputId);
    if (input == nullptr)
        return;

    juce::String text = "Current value: --";
    if (input->hasLiveValue)
    {
        text = "Current value: " + juce::String (input->liveValue, 3);
        if (auto* m = engine.findMappingById (inputId); m != nullptr && m->scaledById.isNotEmpty())
            text << "  (scaled x" << juce::String (engine.getScaleFactor (*m), 2) << ")";
    }
    currentValueLabel.setText (text, juce::dontSendNotification);

    // Learning finishes (or is cancelled) without this component's involvement.
    refreshLearnButton();

    for (auto* row : outputRows)
        row->refreshDuplicateWarning();
}

int MappingInspectorComponent::getPreferredHeight() const
{
    const int padding = 24;
    const int fieldBlock = labelHeight + 2 + fieldHeight;

    if (isScaler)
        return padding
                 + fieldBlock + 10                         // name
                 + 20 + 4                                  // source + current value
                 + fieldBlock + 10                         // input address / MIDI fields
                 + fieldBlock + 20                         // input range
                 + 28;                                     // remove button

    return padding
             + 20 + 4                                      // source + current value
             + fieldBlock + 10                             // input address / MIDI fields
             + fieldBlock + 10                             // input range
             + fieldBlock + 16                             // scaled by
             + 22 + 4                                      // outputs label
             + labelHeight + 2                             // outputs column header
             + outputRows.size() * (fieldHeight + gap)
             + 26 + 20                                     // add output button
             + 28;                                         // remove button
}

void MappingInspectorComponent::resized()
{
    auto area = getLocalBounds().reduced (12);

    if (isScaler)
    {
        nameLabel.setBounds (area.removeFromTop (labelHeight));
        area.removeFromTop (2);
        nameEditor.setBounds (area.removeFromTop (fieldHeight).removeFromLeft (juce::jmin (300, area.getWidth())));
        area.removeFromTop (10);
    }

    {
        auto row = area.removeFromTop (20);
        currentValueLabel.setBounds (row.removeFromRight (240));
        area.removeFromTop (4);
    }

    {
        auto captions = area.removeFromTop (labelHeight);
        area.removeFromTop (2);
        auto fields = area.removeFromTop (fieldHeight);
        area.removeFromTop (10);

        // Lays out one captioned column in both the caption and field rows.
        auto column = [&] (juce::Label& caption, juce::Component& field, int width, bool fromRight)
        {
            caption.setBounds (fromRight ? captions.removeFromRight (width) : captions.removeFromLeft (width));
            field.setBounds (fromRight ? fields.removeFromRight (width) : fields.removeFromLeft (width));
            if (fromRight) { captions.removeFromRight (gap); fields.removeFromRight (gap); }
            else           { captions.removeFromLeft (gap);  fields.removeFromLeft (gap); }
        };

        column (sourceLabel, sourceBox, 80, false);

        // Learn sits at the far right whichever source is showing - learning
        // switches an OSC input to MIDI.
        learnButton.setBounds (fields.removeFromRight (84));
        fields.removeFromRight (gap);
        captions.removeFromRight (84 + gap);

        // OSC and MIDI fields share the same space; only one set is visible.
        const auto oscCaptions = captions, oscFields = fields;
        inputAddressLabel.setBounds (oscCaptions);
        inputAddressEditor.setBounds (oscFields);

        column (midiFadeLabel, midiFadeEditor, 60, true);
        column (midiNumberLabel, midiNumberEditor, 50, true);
        column (midiChannelLabel, midiChannelBox, 54, true);
        column (midiTypeLabel, midiTypeBox, 104, true);
        midiDeviceLabel.setBounds (captions);
        midiDeviceBox.setBounds (fields);
    }

    {
        inRangeLabel.setBounds (area.removeFromTop (labelHeight));
        area.removeFromTop (2);
        auto row = area.removeFromTop (fieldHeight);
        const int fieldWidth = (row.getWidth() - 8) / 2;
        inMinEditor.setBounds (row.removeFromLeft (fieldWidth));
        row.removeFromLeft (8);
        inMaxEditor.setBounds (row.removeFromLeft (fieldWidth));
        area.removeFromTop (10);
    }

    if (isScaler)
    {
        area.removeFromTop (10);
        removeButton.setBounds (area.removeFromTop (28).removeFromLeft (160));
        return;
    }

    {
        auto captions = area.removeFromTop (labelHeight);
        area.removeFromTop (2);
        auto row = area.removeFromTop (fieldHeight);

        const int comboWidth = juce::jmin (220, row.getWidth() / 3);
        scaledByLabel.setBounds (captions.removeFromLeft (comboWidth));
        scaledByBox.setBounds (row.removeFromLeft (comboWidth));
        captions.removeFromLeft (gap);
        row.removeFromLeft (gap);

        scaleRangeLabel.setBounds (captions);
        scaleMinEditor.setBounds (row.removeFromLeft (64));
        row.removeFromLeft (gap);
        scaleMaxEditor.setBounds (row.removeFromLeft (64));
        row.removeFromLeft (gap * 2);
        scaledByHint.setBounds (row);
        area.removeFromTop (16);
    }

    outputsLabel.setBounds (area.removeFromTop (22));
    area.removeFromTop (4);

    {
        using R = OutputRowComponent;
        auto row = area.removeFromTop (labelHeight);
        outputsTargetHeader.setBounds (row.removeFromLeft (R::targetWidth));
        row.removeFromLeft (R::gap);
        row.removeFromRight (R::removeWidth + R::gap);
        outputsRangeHeader.setBounds (row.removeFromRight (R::rangeWidth * 2 + R::gap));
        row.removeFromRight (R::gap);
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
