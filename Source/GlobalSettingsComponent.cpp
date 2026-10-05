#include "GlobalSettingsComponent.h"

namespace
{
    constexpr int topRowHeight = 46;
    constexpr int sectionGap = 12;
    constexpr int targetsHeaderHeight = 24;
    constexpr int columnHeaderHeight = 16;
    constexpr int rowHeight = 26;
    constexpr int rowGap = 4;

    void styleCaption (juce::Label& label)
    {
        label.setFont (juce::Font (juce::FontOptions (12.0f)));
        label.setColour (juce::Label::textColourId, juce::Colours::grey);
        label.setJustificationType (juce::Justification::centredLeft);
    }
}

//==============================================================================
TargetRowComponent::TargetRowComponent (BridgeEngine& engineToUse, juce::String targetIdToUse)
    : engine (engineToUse), targetId (std::move (targetIdToUse))
{
    kindBox.addItem ("OSC", 1);
    kindBox.addItem ("MIDI", 2);

    for (auto* c : std::initializer_list<juce::Component*> { &nameEditor, &kindBox, &hostEditor, &portEditor,
                                                              &deviceBox, &removeButton })
        addAndMakeVisible (c);

    portEditor.setInputRestrictions (5, "0123456789");
    deviceBox.setTextWhenNothingSelected ("Choose MIDI output...");

    for (auto* editor : { &nameEditor, &hostEditor, &portEditor })
    {
        editor->onFocusLost = [this] { commit(); };
        editor->onReturnKey = [this] { commit(); };
    }
    kindBox.onChange = [this] { commit(); };
    deviceBox.onChange = [this] { commit(); };

    removeButton.setTooltip ("Remove target - outputs using it will be left unassigned");
    removeButton.onClick = [this]
    {
        // Rows are rebuilt asynchronously, so `this` survives this call.
        engine.removeTarget (targetId);
    };

    refreshFromEngine();
}

void TargetRowComponent::refreshFromEngine()
{
    auto* t = engine.getSettings().findTarget (targetId);
    if (t == nullptr)
        return;

    const bool isMidi = t->kind == TargetKind::midi;

    nameEditor.setText (t->name, false);
    kindBox.setSelectedId (isMidi ? 2 : 1, juce::dontSendNotification);
    hostEditor.setText (t->host, false);
    portEditor.setText (juce::String (t->port), false);

    deviceChoices = engine.getMidiOutputDeviceNames();
    const bool deviceMissing = t->midiDevice.isNotEmpty() && ! deviceChoices.contains (t->midiDevice);
    if (deviceMissing)
        deviceChoices.add (t->midiDevice);

    deviceBox.clear (juce::dontSendNotification);
    for (int i = 0; i < deviceChoices.size(); ++i)
    {
        const bool isMissing = deviceMissing && i == deviceChoices.size() - 1;
        deviceBox.addItem (deviceChoices[i] + (isMissing ? " (not connected)" : ""), i + 2);
    }
    if (t->midiDevice.isNotEmpty())
        deviceBox.setSelectedId (deviceChoices.indexOf (t->midiDevice) + 2, juce::dontSendNotification);

    hostEditor.setVisible (! isMidi);
    portEditor.setVisible (! isMidi);
    deviceBox.setVisible (isMidi);

    const bool connected = engine.isTargetConnected (targetId);
    setTooltip (connected ? "Connected"
                          : (isMidi ? "Not connected - choose an available MIDI output" : "Not connected - check host and port"));
    repaint();
}

void TargetRowComponent::commit()
{
    const auto name = nameEditor.getText().trim();
    const auto host = hostEditor.getText().trim();
    const int port = portEditor.getText().getIntValue();
    const bool isMidi = kindBox.getSelectedId() == 2;
    const int deviceIndex = deviceBox.getSelectedId() - 2;
    const auto device = juce::isPositiveAndBelow (deviceIndex, deviceChoices.size()) ? deviceChoices[deviceIndex]
                                                                                     : juce::String();

    engine.updateTarget (targetId, [&] (SendTarget& t)
    {
        if (name.isNotEmpty())
            t.name = name;
        if (host.isNotEmpty())
            t.host = host;
        if (port > 0 && port <= 65535)
            t.port = port;
        t.kind = isMidi ? TargetKind::midi : TargetKind::osc;
        if (device.isNotEmpty())
            t.midiDevice = device;
    });

    // Rejected text (e.g. an empty name) is put back by the engine's
    // change notification; refresh here too in case nothing changed.
    refreshFromEngine();
}

void TargetRowComponent::paint (juce::Graphics& g)
{
    const bool connected = engine.isTargetConnected (targetId);
    auto dot = getLocalBounds().removeFromLeft (statusWidth).toFloat().withSizeKeepingCentre (8.0f, 8.0f);
    g.setColour (connected ? juce::Colour (0xff5cc46b) : juce::Colour (0xffe07a6b));
    g.fillEllipse (dot);
}

void TargetRowComponent::resized()
{
    auto area = getLocalBounds();
    area.removeFromLeft (statusWidth);

    nameEditor.setBounds (area.removeFromLeft (nameWidth));
    area.removeFromLeft (gap);
    kindBox.setBounds (area.removeFromLeft (kindWidth));
    area.removeFromLeft (gap);

    removeButton.setBounds (area.removeFromRight (removeWidth));
    area.removeFromRight (gap);

    deviceBox.setBounds (area.withWidth (juce::jmin (area.getWidth(), hostWidth + gap + portWidth)));
    hostEditor.setBounds (area.removeFromLeft (hostWidth));
    area.removeFromLeft (gap);
    portEditor.setBounds (area.removeFromLeft (portWidth));
}

//==============================================================================
GlobalSettingsComponent::GlobalSettingsComponent (BridgeEngine& engineToUse)
    : engine (engineToUse)
{
    for (auto* label : { &receivePortLabel, &updateHzLabel, &nameHeader, &kindHeader, &destinationHeader, &portHeader })
    {
        styleCaption (*label);
        addAndMakeVisible (label);
    }

    targetsLabel.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    targetsLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible (targetsLabel);

    addAndMakeVisible (addTargetButton);
    addTargetButton.onClick = [this] { engine.addTarget(); };

    for (auto* editor : { &receivePortEditor, &updateHzEditor })
    {
        addAndMakeVisible (editor);
        editor->onFocusLost = [this] { applyClicked(); };
        editor->onReturnKey = [this] { applyClicked(); };
    }

    rebuildRows();
}

juce::String GlobalSettingsComponent::computeRowSignature() const
{
    juce::StringArray ids;
    for (auto& t : engine.getSettings().targets)
        ids.add (t.id);
    return ids.joinIntoString (",");
}

void GlobalSettingsComponent::refreshFromEngine()
{
    const auto& s = engine.getSettings();
    receivePortEditor.setText (juce::String (s.receivePort), false);
    updateHzEditor.setText (juce::String (s.updateFrequencyHz), false);

    if (computeRowSignature() != rowSignature)
    {
        triggerAsyncUpdate();
        return;
    }

    for (auto* row : rows)
        row->refreshFromEngine();
}

void GlobalSettingsComponent::handleAsyncUpdate()
{
    rebuildRows();
    if (onPreferredHeightChanged)
        onPreferredHeightChanged();
}

void GlobalSettingsComponent::rebuildRows()
{
    rows.clear();

    for (auto& t : engine.getSettings().targets)
        addAndMakeVisible (rows.add (new TargetRowComponent (engine, t.id)));

    rowSignature = computeRowSignature();
    refreshFromEngine();
    resized();
}

void GlobalSettingsComponent::applyClicked()
{
    auto s = engine.getSettings();
    const int receivePort = receivePortEditor.getText().getIntValue();

    if (receivePort > 0 && receivePort <= 65535)
        s.receivePort = receivePort;
    s.updateFrequencyHz = juce::jmax (1.0, updateHzEditor.getText().getDoubleValue());

    engine.setSettings (s);
    refreshFromEngine();
}

int GlobalSettingsComponent::getPreferredHeight() const
{
    return topRowHeight + sectionGap + targetsHeaderHeight + rowGap + columnHeaderHeight + 2
             + rows.size() * (rowHeight + rowGap);
}

void GlobalSettingsComponent::layoutField (juce::Rectangle<int>& row, juce::Label& label, juce::TextEditor& editor, int width)
{
    auto field = row.removeFromLeft (width);
    label.setBounds (field.removeFromTop (16));
    field.removeFromTop (2);
    editor.setBounds (field.removeFromTop (28));
    row.removeFromLeft (14);
}

void GlobalSettingsComponent::resized()
{
    auto area = getLocalBounds();

    {
        auto row = area.removeFromTop (topRowHeight);
        layoutField (row, receivePortLabel, receivePortEditor, 100);
        layoutField (row, updateHzLabel, updateHzEditor, 130);
    }

    area.removeFromTop (sectionGap);

    {
        auto row = area.removeFromTop (targetsHeaderHeight);
        addTargetButton.setBounds (row.removeFromRight (120));
        targetsLabel.setBounds (row);
    }

    area.removeFromTop (rowGap);

    {
        using R = TargetRowComponent;
        auto row = area.removeFromTop (columnHeaderHeight);
        row.removeFromLeft (R::statusWidth);
        nameHeader.setBounds (row.removeFromLeft (R::nameWidth));
        row.removeFromLeft (R::gap);
        kindHeader.setBounds (row.removeFromLeft (R::kindWidth));
        row.removeFromLeft (R::gap);
        destinationHeader.setBounds (row.removeFromLeft (R::hostWidth));
        row.removeFromLeft (R::gap);
        portHeader.setBounds (row.removeFromLeft (R::portWidth));
    }

    area.removeFromTop (2);

    for (auto* row : rows)
    {
        row->setBounds (area.removeFromTop (rowHeight));
        area.removeFromTop (rowGap);
    }
}
