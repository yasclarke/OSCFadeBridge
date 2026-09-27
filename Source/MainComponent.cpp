#include "MainComponent.h"

//==============================================================================
juce::StringArray MainMenuModel::getMenuBarNames()
{
    return { "File", "Edit" };
}

juce::PopupMenu MainMenuModel::getMenuForIndex (int topLevelMenuIndex, const juce::String&)
{
    juce::PopupMenu menu;

    if (topLevelMenuIndex == 0) // File
    {
        auto addItem = [&menu] (int itemID, const juce::String& text, const juce::String& shortcut, bool enabled = true)
        {
            juce::PopupMenu::Item item;
            item.itemID = itemID;
            item.text = text;
            item.shortcutKeyDescription = shortcut;
            item.isEnabled = enabled;
            menu.addItem (item);
        };

        addItem (1, "New Project", "Cmd+N");
        addItem (2, "Open...", "Cmd+O");
        menu.addSeparator();
        addItem (3, "Save", "Cmd+S");
        addItem (4, "Save As...", "Cmd+Shift+S");
    }
    else if (topLevelMenuIndex == 1) // Edit
    {
        juce::PopupMenu::Item undoItem;
        undoItem.itemID = 5;
        undoItem.text = "Undo";
        undoItem.shortcutKeyDescription = "Cmd+Z";
        undoItem.isEnabled = owner.canUndo();
        menu.addItem (undoItem);

        juce::PopupMenu::Item redoItem;
        redoItem.itemID = 6;
        redoItem.text = "Redo";
        redoItem.shortcutKeyDescription = "Cmd+Shift+Z";
        redoItem.isEnabled = owner.canRedo();
        menu.addItem (redoItem);
    }

    return menu;
}

void MainMenuModel::menuItemSelected (int menuItemID, int)
{
    switch (menuItemID)
    {
        case 1: owner.newProject();   break;
        case 2: owner.openProject();  break;
        case 3: owner.save();          break;
        case 4: owner.saveProjectAs(); break;
        case 5: owner.undo();         break;
        case 6: owner.redo();         break;
        default: break;
    }
}

namespace
{
    constexpr int valueColumnWidth = 80;
    constexpr int columnGap = 8;
}

//==============================================================================
MappingSummaryRow::MappingSummaryRow (BridgeEngine& engineToUse, juce::String mappingIdToUse,
                                       std::function<void (const juce::String&)> onSelectedIn)
    : engine (engineToUse), mappingId (std::move (mappingIdToUse)), onSelected (std::move (onSelectedIn))
{
    addAndMakeVisible (addressLabel);
    addAndMakeVisible (valueLabel);

    valueLabel.setJustificationType (juce::Justification::centredRight);
    valueLabel.setColour (juce::Label::textColourId, juce::Colours::grey);

    for (auto* l : { &addressLabel, &valueLabel })
        l->setInterceptsMouseClicks (false, false);

    refresh();
}

void MappingSummaryRow::refresh()
{
    auto* m = engine.findMappingById (mappingId);
    if (m == nullptr)
        return;

    addressLabel.setText (m->inputAddress, juce::dontSendNotification);
    valueLabel.setText (m->hasLiveValue ? juce::String (m->liveValue, 3) : "--", juce::dontSendNotification);
}

void MappingSummaryRow::setSelected (bool shouldBeSelected)
{
    if (selected != shouldBeSelected)
    {
        selected = shouldBeSelected;
        repaint();
    }
}

void MappingSummaryRow::mouseDown (const juce::MouseEvent&)
{
    if (onSelected)
        onSelected (mappingId);
}

void MappingSummaryRow::mouseEnter (const juce::MouseEvent&)
{
    hovered = true;
    repaint();
}

void MappingSummaryRow::mouseExit (const juce::MouseEvent&)
{
    hovered = false;
    repaint();
}

void MappingSummaryRow::paint (juce::Graphics& g)
{
    if (selected)
    {
        g.setColour (findColour (juce::TextEditor::focusedOutlineColourId).withAlpha (0.25f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
    }
    else if (hovered)
    {
        g.setColour (findColour (juce::ResizableWindow::backgroundColourId).contrasting (0.06f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
    }
}

void MappingSummaryRow::resized()
{
    auto area = getLocalBounds().reduced (6, 0);
    valueLabel.setBounds (area.removeFromRight (valueColumnWidth));
    area.removeFromRight (columnGap);
    addressLabel.setBounds (area);
}

//==============================================================================
MappingListComponent::MappingListComponent (BridgeEngine& engineToUse)
    : engine (engineToUse)
{
    rebuild();
}

void MappingListComponent::rebuild()
{
    rows.clear();

    for (auto& m : engine.getMappings())
    {
        auto* row = rows.add (new MappingSummaryRow (engine, m.id, [this] (const juce::String& id)
        {
            selectedId = id;
            updateSelectionHighlight();
            if (onSelectionChanged)
                onSelectionChanged (id);
        }));
        addAndMakeVisible (row);
    }

    updateSelectionHighlight();
    resized();
}

void MappingListComponent::refreshRows()
{
    for (auto* row : rows)
        row->refresh();
}

void MappingListComponent::setSelectedId (const juce::String& id)
{
    selectedId = id;
    updateSelectionHighlight();
}

void MappingListComponent::updateSelectionHighlight()
{
    for (auto* row : rows)
        row->setSelected (row->getMappingId() == selectedId);
}

void MappingListComponent::resized()
{
    const int width = getWidth();
    const int rowHeight = 28;
    int y = 0;

    for (auto* row : rows)
    {
        row->setBounds (0, y, width, rowHeight);
        y += rowHeight;
    }

    setSize (width, juce::jmax (y, getParentHeight()));
}

//==============================================================================
MainComponent::MainComponent()
{
    setWantsKeyboardFocus (true);
    juce::MenuBarModel::setMacMainMenu (&menuModel);

    addAndMakeVisible (settingsComponent);

    addAndMakeVisible (listHeaderAddress);
    addAndMakeVisible (listHeaderValue);
    for (auto* l : { &listHeaderAddress, &listHeaderValue })
    {
        l->setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        l->setColour (juce::Label::textColourId, juce::Colours::grey);
    }
    listHeaderValue.setJustificationType (juce::Justification::centredRight);

    addAndMakeVisible (mappingsViewport);
    mappingsViewport.setViewedComponent (&mappingsList, false);
    mappingsList.setSize (300, 10);
    mappingsList.onSelectionChanged = [this] (const juce::String& id) { selectMapping (id); };

    addAndMakeVisible (addMappingButton);
    addMappingButton.onClick = [this]
    {
        auto newId = engine.addMapping();
        selectedMappingId = newId;
    };

    addAndMakeVisible (inspectorViewport);
    inspectorPlaceholder.setJustificationType (juce::Justification::centred);
    inspectorPlaceholder.setColour (juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible (inspectorPlaceholder);

    engine.onProjectChanged = [this] { handleProjectChanged(); };
    engine.onLogMessage = [this] (const juce::String& text) { appendLog (text); };

    addAndMakeVisible (logLabel);
    addAndMakeVisible (logToggleButton);
    addAndMakeVisible (logClearButton);
    logToggleButton.onClick = [this]
    {
        logVisible = ! logVisible;
        logToggleButton.setButtonText (logVisible ? "Hide" : "Show");
        logBox.setVisible (logVisible);
        resized();
        repaint();
    };
    logClearButton.onClick = [this] { logBox.clear(); };

    logBox.setMultiLine (true);
    logBox.setReadOnly (true);
    logBox.setCaretVisible (false);
    logBox.setScrollbarsShown (true);
    logBox.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));
    addAndMakeVisible (logBox);

    rebuildInspector();
    updateTitle();

    setSize (860, 820);
    startTimerHz (10);
    grabKeyboardFocus();
}

MainComponent::~MainComponent()
{
    juce::MenuBarModel::setMacMainMenu (nullptr);
    engine.onProjectChanged = nullptr;
    engine.onLogMessage = nullptr;
}

void MainComponent::timerCallback()
{
    mappingsList.refreshRows();
    if (inspector != nullptr)
    {
        inspector->refreshCurrentValueLabel();
        inspector->refreshOutputWarnings();
    }
}

void MainComponent::handleProjectChanged()
{
    mappingsList.rebuild();

    if (selectedMappingId.isNotEmpty() && engine.findMappingById (selectedMappingId) == nullptr)
        selectedMappingId.clear();

    mappingsList.setSelectedId (selectedMappingId);
    rebuildInspector();

    settingsComponent.refreshFromEngine();
    updateTitle();
    menuModel.menuItemsChanged();
}

void MainComponent::handleFieldsChanged()
{
    mappingsList.refreshRows();
}

void MainComponent::updateTitle()
{
    if (onTitleRequested)
        onTitleRequested ("OSC Fade Bridge - " + engine.getCurrentProjectDisplayName());
}

void MainComponent::newProject()
{
    auto options = juce::MessageBoxOptions::makeOptionsOkCancel (
        juce::MessageBoxIconType::QuestionIcon, "New Project",
        "Start a new, empty project? Unsaved changes to the current project's file location will be kept on disk, "
        "but this window will move to a blank project.",
        "New Project", "Cancel");

    juce::NativeMessageBox::showAsync (options, [this] (int result)
    {
        if (result == 0) // button index 0 = the first button passed to makeOptionsOkCancel ("New Project")
            engine.newProject();
    });
}

void MainComponent::openProject()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Open OSC Fade Bridge Project", juce::File(), "*.json");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& chooser)
        {
            auto file = chooser.getResult();
            if (file != juce::File() && ! engine.openProject (file))
            {
                juce::NativeMessageBox::showAsync (
                    juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::WarningIcon,
                        "Couldn't Open Project", "\"" + file.getFileName() + "\" isn't a valid project file."),
                    nullptr);
            }
        });
}

void MainComponent::saveProjectAs()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Save OSC Fade Bridge Project As",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Untitled.json"), "*.json");

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                 | juce::FileBrowserComponent::warnAboutOverwriting,
        [this] (const juce::FileChooser& chooser)
        {
            auto file = chooser.getResult();
            if (file != juce::File())
                engine.saveProjectAs (file.withFileExtension ("json"));
        });
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    const auto cmd = juce::ModifierKeys::commandModifier;

    if (key == juce::KeyPress ('z', juce::ModifierKeys (cmd), 0))
    {
        engine.undo();
        return true;
    }
    if (key == juce::KeyPress ('z', juce::ModifierKeys (cmd | juce::ModifierKeys::shiftModifier), 0))
    {
        engine.redo();
        return true;
    }
    if (key == juce::KeyPress ('n', juce::ModifierKeys (cmd), 0))
    {
        newProject();
        return true;
    }
    if (key == juce::KeyPress ('o', juce::ModifierKeys (cmd), 0))
    {
        openProject();
        return true;
    }
    if (key == juce::KeyPress ('s', juce::ModifierKeys (cmd | juce::ModifierKeys::shiftModifier), 0))
    {
        saveProjectAs();
        return true;
    }
    if (key == juce::KeyPress ('s', juce::ModifierKeys (cmd), 0))
    {
        engine.saveProject();
        return true;
    }

    return false;
}

void MainComponent::selectMapping (const juce::String& id)
{
    selectedMappingId = id;
    rebuildInspector();
}

void MainComponent::rebuildInspector()
{
    inspector.reset();

    if (selectedMappingId.isNotEmpty() && engine.findMappingById (selectedMappingId) != nullptr)
    {
        inspector = std::make_unique<MappingInspectorComponent> (engine, selectedMappingId,
            [this] { handleProjectChanged(); },
            [this] { handleFieldsChanged(); });
        inspectorViewport.setViewedComponent (inspector.get(), false);
        inspectorPlaceholder.setVisible (false);
    }
    else
    {
        inspectorViewport.setViewedComponent (nullptr, false);
        inspectorPlaceholder.setVisible (true);
    }

    resized();
}

void MainComponent::appendLog (const juce::String& text)
{
    const auto timestamp = juce::Time::getCurrentTime().toString (false, true, true, true);
    logBox.moveCaretToEnd();
    logBox.insertTextAtCaret ("[" + timestamp + "] " + text + juce::newLine);

    if (logBox.getTotalNumChars() > 200000)
        logBox.setText (logBox.getText().substring (100000), false);
}

void MainComponent::drawSection (juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title)
{
    auto titleArea = bounds.removeFromTop (20);
    g.setColour (juce::Colours::grey);
    g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    g.drawText (title, titleArea, juce::Justification::centredLeft);

    g.setColour (findColour (juce::ResizableWindow::backgroundColourId).contrasting (0.18f).withAlpha (0.6f));
    g.drawRoundedRectangle (bounds.toFloat(), 6.0f, 1.2f);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (findColour (juce::ResizableWindow::backgroundColourId));

    drawSection (g, settingsBounds, "Global Settings");
    drawSection (g, mappingsBounds, "Mappings");
    drawSection (g, inspectorBounds, "Mapping Details");
    drawSection (g, logBounds, "Activity Log");
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (10);
    const int sectionGap = 10;

    settingsBounds = area.removeFromTop (20 + 20 + GlobalSettingsComponent::getPreferredHeight());
    area.removeFromTop (sectionGap);

    const int logChromeHeight = 20 /* title */ + 20 /* reduced top+bottom */ + 24 /* header row */;
    const int logHeight = logVisible ? 190 : logChromeHeight;
    logBounds = area.removeFromBottom (logHeight);
    area.removeFromBottom (sectionGap);

    {
        auto content = settingsBounds;
        content.removeFromTop (20);
        content = content.reduced (10);
        settingsComponent.setBounds (content);
    }

    {
        auto content = logBounds;
        content.removeFromTop (20);
        content = content.reduced (10);

        auto header = content.removeFromTop (24);
        logLabel.setBounds (header.removeFromLeft (150));
        logClearButton.setBounds (header.removeFromRight (60));
        header.removeFromRight (6);
        logToggleButton.setBounds (header.removeFromRight (60));

        content.removeFromTop (6);
        logBox.setBounds (logVisible ? content : juce::Rectangle<int>());
    }

    const int mappingsWidth = 340;
    mappingsBounds = area.removeFromLeft (mappingsWidth);
    area.removeFromLeft (sectionGap);
    inspectorBounds = area;

    {
        auto content = mappingsBounds;
        content.removeFromTop (20);
        content = content.reduced (10);

        auto addButtonArea = content.removeFromBottom (30);
        addMappingButton.setBounds (addButtonArea.removeFromLeft (150));
        content.removeFromBottom (6);

        auto header = content.removeFromTop (18);
        auto headerInset = header.reduced (6, 0);
        listHeaderValue.setBounds (headerInset.removeFromRight (valueColumnWidth));
        headerInset.removeFromRight (columnGap);
        listHeaderAddress.setBounds (headerInset);

        content.removeFromTop (4);
        mappingsViewport.setBounds (content);
        mappingsList.setSize (mappingsViewport.getWidth() - mappingsViewport.getScrollBarThickness(),
                               mappingsList.getHeight());
        mappingsList.resized();
    }

    {
        auto content = inspectorBounds;
        content.removeFromTop (20);
        content = content.reduced (10);

        inspectorViewport.setBounds (content);
        inspectorPlaceholder.setBounds (content);

        if (inspector != nullptr)
        {
            const int w = inspectorViewport.getWidth() - inspectorViewport.getScrollBarThickness();
            inspector->setSize (w, inspector->getPreferredHeight());
        }
    }
}
