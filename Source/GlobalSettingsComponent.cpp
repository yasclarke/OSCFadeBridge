#include "GlobalSettingsComponent.h"

GlobalSettingsComponent::GlobalSettingsComponent (BridgeEngine& engineToUse)
    : engine (engineToUse)
{
    for (auto* label : { &receivePortLabel, &sendHostLabel, &sendPortLabel, &updateHzLabel })
    {
        label->setFont (juce::Font (juce::FontOptions (12.0f)));
        label->setColour (juce::Label::textColourId, juce::Colours::grey);
        label->setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (label);
    }

    for (auto* editor : { &receivePortEditor, &sendHostEditor, &sendPortEditor, &updateHzEditor })
    {
        addAndMakeVisible (editor);
        editor->onFocusLost = [this] { applyClicked(); };
        editor->onReturnKey = [this] { applyClicked(); };
    }

    refreshFromEngine();
}

void GlobalSettingsComponent::refreshFromEngine()
{
    const auto& s = engine.getSettings();
    receivePortEditor.setText (juce::String (s.receivePort), false);
    sendHostEditor.setText (s.sendHost, false);
    sendPortEditor.setText (juce::String (s.sendPort), false);
    updateHzEditor.setText (juce::String (s.updateFrequencyHz), false);
}

void GlobalSettingsComponent::applyClicked()
{
    GlobalSettings s;
    s.receivePort = receivePortEditor.getText().getIntValue();
    s.sendHost = sendHostEditor.getText().trim();
    s.sendPort = sendPortEditor.getText().getIntValue();
    s.updateFrequencyHz = juce::jmax (1.0, updateHzEditor.getText().getDoubleValue());

    if (s.receivePort <= 0 || s.receivePort > 65535 || s.sendPort <= 0 || s.sendPort > 65535 || s.sendHost.isEmpty())
    {
        refreshFromEngine();
        return;
    }

    engine.setSettings (s);
    refreshFromEngine();
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
    auto row = getLocalBounds();

    layoutField (row, receivePortLabel, receivePortEditor, 100);
    layoutField (row, sendHostLabel, sendHostEditor, 140);
    layoutField (row, sendPortLabel, sendPortEditor, 100);
    layoutField (row, updateHzLabel, updateHzEditor, 130);
}
