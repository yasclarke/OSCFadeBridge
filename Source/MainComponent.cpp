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

        menu.addSeparator();

        juce::PopupMenu::Item duplicateItem;
        duplicateItem.itemID = 7;
        duplicateItem.text = "Duplicate Mapping";
        duplicateItem.shortcutKeyDescription = "Cmd+D";
        duplicateItem.isEnabled = owner.canDuplicate();
        menu.addItem (duplicateItem);
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
        case 7: owner.duplicateSelectedMapping(); break;
        default: break;
    }
}

namespace
{
    constexpr int valueColumnWidth = 80;
    constexpr int columnGap = 8;
}

//==============================================================================
DraggableValueLabel::DraggableValueLabel()
{
    setEditable (false, true, false);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    setTooltip ("Drag to change the value (hold Shift for fine control), or double-click to type one");
    onTextChange = [this]
    {
        if (onValueTyped)
            onValueTyped (getText());
    };
}

void DraggableValueLabel::mouseDown (const juce::MouseEvent& e)
{
    lastDragPosition = e.position;
    wasDragged = false;

    if (onPressed)
        onPressed();
}

void DraggableValueLabel::mouseDrag (const juce::MouseEvent& e)
{
    // Pixels needed to sweep the whole input range.
    constexpr float fullRangePixels = 200.0f;

    const auto delta = e.position - lastDragPosition;
    lastDragPosition = e.position;
    wasDragged = true;

    const float scale = e.mods.isShiftDown() ? 0.1f : 1.0f;
    if (onDragged)
        onDragged ((delta.x - delta.y) / fullRangePixels * scale);
}

void DraggableValueLabel::mouseUp (const juce::MouseEvent&)
{
    if (wasDragged && onDragEnded)
        onDragEnded();
}

//==============================================================================
InputSummaryRow::InputSummaryRow (BridgeEngine& engineToUse, juce::String inputIdToUse,
                                  std::function<void (const juce::String&)> onSelectedIn)
    : engine (engineToUse), inputId (std::move (inputIdToUse)), onSelected (std::move (onSelectedIn))
{
    addAndMakeVisible (nameLabel);
    addAndMakeVisible (valueLabel);

    valueLabel.setJustificationType (juce::Justification::centredRight);
    valueLabel.setColour (juce::Label::textColourId, juce::Colours::grey);

    nameLabel.setInterceptsMouseClicks (false, false);

    valueLabel.onPressed = [this]
    {
        if (auto* input = engine.findInputById (inputId))
            dragNormalised = input->getNormalisedValue();
        if (onSelected)
            onSelected (inputId);
    };

    valueLabel.onDragged = [this] (float normalisedDelta)
    {
        auto* input = engine.findInputById (inputId);
        if (input == nullptr)
            return;

        dragNormalised = juce::jlimit (0.0f, 1.0f, dragNormalised + normalisedDelta);
        engine.setValueManually (inputId, juce::jmap (dragNormalised, input->inMin, input->inMax));
        refresh();
    };

    valueLabel.onDragEnded = [this] { engine.logManualValue (inputId); };

    valueLabel.onValueTyped = [this] (const juce::String& text)
    {
        // Anything that isn't a number (e.g. the "--" placeholder) is ignored.
        if (text.trim().containsOnly ("0123456789.-+") && text.trim().containsAnyOf ("0123456789"))
        {
            engine.setValueManually (inputId, (float) text.trim().getDoubleValue());
            engine.logManualValue (inputId);
        }
        refresh();
    };

    refresh();
}

void InputSummaryRow::refresh()
{
    auto* input = engine.findInputById (inputId);
    if (input == nullptr)
        return;

    nameLabel.setText (input->getDisplayName(), juce::dontSendNotification);

    // Setting the text would close the editor of a value being typed.
    if (! valueLabel.isBeingEdited())
        valueLabel.setText (input->hasLiveValue ? juce::String (input->liveValue, 3) : "--", juce::dontSendNotification);
}

void InputSummaryRow::setSelected (bool shouldBeSelected)
{
    if (selected != shouldBeSelected)
    {
        selected = shouldBeSelected;
        repaint();
    }
}

void InputSummaryRow::mouseDown (const juce::MouseEvent& e)
{
    if (onSelected)
        onSelected (inputId);

    if (e.mods.isPopupMenu())
    {
        if (onContextMenu)
            onContextMenu();
        return;
    }

    beginDragAutoRepeat (50);   // keeps the list auto-scrolling while held at an edge
}

void InputSummaryRow::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || (! reordering && e.getDistanceFromDragStart() < 5))
        return;

    if (! reordering)
    {
        reordering = true;
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setAlpha (0.5f);
    }

    if (onReorderDrag)
        onReorderDrag (e);
}

void InputSummaryRow::mouseUp (const juce::MouseEvent&)
{
    if (! reordering)
        return;

    reordering = false;
    setMouseCursor (juce::MouseCursor::NormalCursor);
    setAlpha (1.0f);

    if (onReorderDrop)
        onReorderDrop();
}

void InputSummaryRow::mouseEnter (const juce::MouseEvent&)
{
    hovered = true;
    repaint();
}

void InputSummaryRow::mouseExit (const juce::MouseEvent&)
{
    hovered = false;
    repaint();
}

void InputSummaryRow::paint (juce::Graphics& g)
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

void InputSummaryRow::resized()
{
    auto area = getLocalBounds().reduced (6, 0);
    valueLabel.setBounds (area.removeFromRight (valueColumnWidth));
    area.removeFromRight (columnGap);
    nameLabel.setBounds (area);
}

//==============================================================================
InputListComponent::InputListComponent (BridgeEngine& engineToUse)
    : engine (engineToUse)
{
    rebuild();
}

void InputListComponent::setShowScalers (bool shouldShowScalers)
{
    showScalers = shouldShowScalers;
    rebuild();
}

void InputListComponent::rebuild()
{
    // Any drag in progress belonged to a row that's about to go.
    draggingId.clear();
    dropIndex = -1;
    rows.clear();

    auto addRow = [this] (const InputChannel& input)
    {
        auto* row = rows.add (new InputSummaryRow (engine, input.id, [this] (const juce::String& id)
        {
            selectedId = id;
            updateSelectionHighlight();
            if (onSelectionChanged)
                onSelectionChanged (id);
        }));

        row->onReorderDrag = [this, row] (const juce::MouseEvent& e) { reorderDrag (*row, e); };
        row->onReorderDrop = [this] { reorderDrop(); };
        row->onContextMenu = [this]
        {
            if (onContextMenuRequested)
                onContextMenuRequested();
        };
        addAndMakeVisible (row);
    };

    if (showScalers)
        for (auto& s : engine.getScalers())
            addRow (s);
    else
        for (auto& m : engine.getMappings())
            addRow (m);

    updateSelectionHighlight();
    resized();
}

void InputListComponent::refreshRows()
{
    for (auto* row : rows)
        row->refresh();
}

void InputListComponent::setSelectedId (const juce::String& id)
{
    selectedId = id;
    updateSelectionHighlight();
}

void InputListComponent::updateSelectionHighlight()
{
    for (auto* row : rows)
        row->setSelected (row->getInputId() == selectedId);
}

int InputListComponent::indexOf (const juce::String& id) const
{
    for (int i = 0; i < rows.size(); ++i)
        if (rows[i]->getInputId() == id)
            return i;
    return -1;
}

void InputListComponent::reorderDrag (InputSummaryRow& row, const juce::MouseEvent& e)
{
    const auto pos = e.getEventRelativeTo (this).getPosition();
    const int from = indexOf (row.getInputId());
    const int before = juce::jlimit (0, rows.size(), (pos.y + rowHeight / 2) / rowHeight);

    draggingId = row.getInputId();
    dropIndex = (before == from || before == from + 1) ? -1 : before;   // dropping there wouldn't move it
    repaint();

    if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
    {
        const auto inViewport = viewport->getLocalPoint (this, pos);
        viewport->autoScroll (inViewport.x, inViewport.y, 20, 8);
    }
}

void InputListComponent::reorderDrop()
{
    const auto id = draggingId;
    const int before = dropIndex;
    draggingId.clear();
    dropIndex = -1;
    repaint();

    if (id.isEmpty() || before < 0)
        return;

    // Moving rebuilds this list, which would delete the row whose mouseUp
    // we're inside - so do it once that's finished.
    juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<InputListComponent> (this), id, before]
    {
        if (safeThis == nullptr)
            return;

        if (safeThis->showScalers)
            safeThis->engine.moveScaler (id, before);
        else
            safeThis->engine.moveMapping (id, before);
    });
}

void InputListComponent::paintOverChildren (juce::Graphics& g)
{
    if (dropIndex < 0)
        return;

    const int y = juce::jlimit (1, juce::jmax (1, getHeight() - 2), dropIndex * rowHeight);
    g.setColour (findColour (juce::TextEditor::focusedOutlineColourId));
    g.fillRoundedRectangle (juce::Rectangle<float> (2.0f, (float) y - 1.0f, (float) getWidth() - 4.0f, 3.0f), 1.5f);
}

void InputListComponent::resized()
{
    const int width = getWidth();
    int y = 0;

    for (auto* row : rows)
    {
        row->setBounds (0, y, width, rowHeight);
        y += rowHeight;
    }

    setSize (width, juce::jmax (y, getParentHeight()));
}

//==============================================================================
DisclosureButton::DisclosureButton (const juce::String& text)
    : juce::Button (text)
{
    setClickingTogglesState (true);
}

void DisclosureButton::setOpen (bool shouldBeOpen)
{
    setToggleState (shouldBeOpen, juce::dontSendNotification);
    angle = getTargetAngle();
    repaint();
}

float DisclosureButton::getTargetAngle() const
{
    return getToggleState() ? juce::MathConstants<float>::halfPi : 0.0f;
}

void DisclosureButton::clicked()
{
    startTimerHz (60);
}

void DisclosureButton::timerCallback()
{
    const float target = getTargetAngle();
    angle += (target - angle) * 0.35f;

    if (std::abs (target - angle) < 0.01f)
    {
        angle = target;
        stopTimer();
    }

    repaint();
}

void DisclosureButton::paintButton (juce::Graphics& g, bool isHighlighted, bool)
{
    auto area = getLocalBounds().toFloat();
    const auto colour = isHighlighted ? juce::Colours::lightgrey : juce::Colours::grey;

    // A right-pointing triangle centred on the origin, rotated into place.
    const auto arrowCentre = area.removeFromLeft (12.0f).getCentre();
    juce::Path arrow;
    arrow.addTriangle (-2.5f, -4.0f, -2.5f, 4.0f, 4.0f, 0.0f);
    arrow.applyTransform (juce::AffineTransform::rotation (angle).translated (arrowCentre));

    g.setColour (colour);
    g.fillPath (arrow);

    area.removeFromLeft (4.0f);
    g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    g.drawText (getButtonText(), area, juce::Justification::centredLeft);
}

//==============================================================================
MainComponent::MainComponent()
{
    setWantsKeyboardFocus (true);
    juce::MenuBarModel::setMacMainMenu (&menuModel);

    const auto tabColour = findColour (juce::ResizableWindow::backgroundColourId);
    tabs.addTab ("Mappings", tabColour, -1);
    tabs.addTab ("Scalers", tabColour, -1);
    tabs.addTab ("Settings", tabColour, -1);
    tabs.setCurrentTabIndex (mappingsPage, false);
    tabs.addChangeListener (this);
    addAndMakeVisible (tabs);

    addChildComponent (settingsViewport);
    settingsViewport.setViewedComponent (&settingsComponent, false);
    settingsViewport.setScrollBarsShown (true, false);
    settingsComponent.onPreferredHeightChanged = [this] { resized(); };

    addAndMakeVisible (listHeaderName);
    addAndMakeVisible (listHeaderValue);
    for (auto* l : { &listHeaderName, &listHeaderValue })
    {
        l->setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        l->setColour (juce::Label::textColourId, juce::Colours::grey);
    }
    listHeaderValue.setJustificationType (juce::Justification::centredRight);

    addAndMakeVisible (listViewport);
    listViewport.setViewedComponent (&inputList, false);
    inputList.setSize (300, 10);
    inputList.onSelectionChanged = [this] (const juce::String& id) { selectInput (id); };
    inputList.onContextMenuRequested = [this] { showInputContextMenu(); };

    addAndMakeVisible (addButton);
    addButton.onClick = [this]
    {
        selectNewInput (getCurrentPage() == scalersPage ? engine.addScaler() : engine.addMapping());
    };

    addAndMakeVisible (duplicateButton);
    duplicateButton.setTooltip ("Copy the selected mapping, with all its outputs, to a new input address (Cmd+D)");
    duplicateButton.onClick = [this] { duplicateSelectedMapping(); };

    addAndMakeVisible (inspectorViewport);
    inspectorPlaceholder.setJustificationType (juce::Justification::centred);
    inspectorPlaceholder.setColour (juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible (inspectorPlaceholder);

    engine.onProjectChanged = [this] { handleProjectChanged(); };
    engine.onLogMessage = [this] (const juce::String& text) { appendLog (text); };
    engine.onMidiDevicesChanged = [this] { handleProjectChanged(); };

    addAndMakeVisible (logToggleButton);
    addAndMakeVisible (logClearButton);
    logToggleButton.setOpen (logVisible);
    logToggleButton.onClick = [this]
    {
        logVisible = logToggleButton.getToggleState();
        logBox.setVisible (logVisible);
        logClearButton.setVisible (logVisible);
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

    setSize (1060, 820);
    showPage (mappingsPage);
    updateTitle();

    startTimerHz (10);
    grabKeyboardFocus();
}

MainComponent::~MainComponent()
{
    juce::MenuBarModel::setMacMainMenu (nullptr);
    tabs.removeChangeListener (this);
    engine.onProjectChanged = nullptr;
    engine.onLogMessage = nullptr;
    engine.onMidiDevicesChanged = nullptr;
}

void MainComponent::timerCallback()
{
    inputList.refreshRows();
    if (inspector != nullptr)
        inspector->refreshLiveDisplay();
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    showPage (getCurrentPage());
}

void MainComponent::showPage (Page page)
{
    const bool isSettings = page == settingsPage;

    settingsViewport.setVisible (isSettings);
    for (auto* c : std::initializer_list<juce::Component*> { &listViewport, &listHeaderName, &listHeaderValue,
                                                              &addButton, &inspectorViewport })
        c->setVisible (! isSettings);
    duplicateButton.setVisible (page == mappingsPage);

    if (! isSettings)
    {
        const bool scalers = page == scalersPage;
        addButton.setButtonText (scalers ? "+ Add Scaler" : "+ Add Mapping");
        inspectorPlaceholder.setText (scalers ? "Select a scaler on the left to edit its details."
                                              : "Select a mapping on the left to edit its details.",
                                      juce::dontSendNotification);
        inputList.setShowScalers (scalers);
        inputList.setSelectedId (currentSelection());
    }

    rebuildInspector();
    updateDuplicateButton();
    repaint();
}

void MainComponent::handleProjectChanged()
{
    inputList.rebuild();

    if (selectedMappingId.isNotEmpty() && engine.findMappingById (selectedMappingId) == nullptr)
        selectedMappingId.clear();
    if (selectedScalerId.isNotEmpty() && engine.findScalerById (selectedScalerId) == nullptr)
        selectedScalerId.clear();

    // This often runs from inside one of the inspector's own callbacks, so
    // it must never be deleted here: edits refresh it in place, and
    // anything that changes its shape rebuilds it asynchronously.
    if (getCurrentPage() != settingsPage)
    {
        inputList.setSelectedId (currentSelection());

        if (inspector != nullptr && inspector->getInputId() == currentSelection() && inspector->canRefreshInPlace())
            inspector->refreshFromEngine();
        else
            triggerAsyncUpdate();
    }

    settingsComponent.refreshFromEngine();
    updateTitle();
    updateDuplicateButton();
    menuModel.menuItemsChanged();
}

void MainComponent::handleAsyncUpdate()
{
    rebuildInspector();
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
    if (key == juce::KeyPress ('d', juce::ModifierKeys (cmd), 0))
    {
        duplicateSelectedMapping();
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


void MainComponent::duplicateSelectedMapping()
{
    if (! canDuplicate())
        return;

    // Often called from the inspector's own button, so rebuild it asynchronously.
    selectNewInput (engine.duplicateMapping (selectedMappingId));
}

void MainComponent::selectNewInput (const juce::String& id)
{
    if (id.isEmpty())
        return;

    // Adding it already refreshed the inspector in place for the old
    // selection; this may be inside one of its callbacks, so rebuild it for
    // the new one asynchronously.
    currentSelection() = id;
    inputList.setSelectedId (id);
    triggerAsyncUpdate();
    updateDuplicateButton();
    menuModel.menuItemsChanged();
}

void MainComponent::showInputContextMenu()
{
    if (getCurrentPage() != mappingsPage)
        return;

    juce::PopupMenu menu;
    menu.addItem (1, "Duplicate Mapping");

    menu.showMenuAsync (juce::PopupMenu::Options(), [safeThis = juce::Component::SafePointer<MainComponent> (this)] (int result)
    {
        if (safeThis != nullptr && result == 1)
            safeThis->duplicateSelectedMapping();
    });
}

void MainComponent::selectInput (const juce::String& id)
{
    // Clicking a row's value to drag it also selects the row - don't
    // rebuild the inspector if it's already showing that input.
    if (inspector != nullptr && inspector->getInputId() == id)
        return;

    currentSelection() = id;
    rebuildInspector();
    updateDuplicateButton();
    menuModel.menuItemsChanged();
}

void MainComponent::rebuildInspector()
{
    inspector.reset();

    const auto page = getCurrentPage();
    const bool isScaler = page == scalersPage;
    const auto& id = currentSelection();
    const bool exists = isScaler ? engine.findScalerById (id) != nullptr : engine.findMappingById (id) != nullptr;

    // Learning is tied to the input being edited - moving away cancels it.
    if (engine.getMidiLearnInputId().isNotEmpty() && (page == settingsPage || engine.getMidiLearnInputId() != id))
        engine.cancelMidiLearn();

    if (page != settingsPage && id.isNotEmpty() && exists)
    {
        inspector = std::make_unique<MappingInspectorComponent> (engine, id, isScaler);
        inspectorViewport.setViewedComponent (inspector.get(), false);
        inspectorPlaceholder.setVisible (false);
    }
    else
    {
        inspectorViewport.setViewedComponent (nullptr, false);
        inspectorPlaceholder.setVisible (page != settingsPage);
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

    switch (getCurrentPage())
    {
        case settingsPage:
            drawSection (g, settingsBounds, "Global Settings");
            break;
        case scalersPage:
            drawSection (g, listBounds, "Scalers");
            drawSection (g, inspectorBounds, "Scaler Details");
            break;
        case mappingsPage:
            drawSection (g, listBounds, "Mappings");
            drawSection (g, inspectorBounds, "Mapping Details");
            break;
    }

    // The log's title is its disclosure button, so only the outline is drawn.
    if (logVisible)
    {
        g.setColour (findColour (juce::ResizableWindow::backgroundColourId).contrasting (0.18f).withAlpha (0.6f));
        g.drawRoundedRectangle (logBounds.withTrimmedTop (20).toFloat(), 6.0f, 1.2f);
    }
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (10);
    const int sectionGap = 10;

    tabs.setBounds (area.removeFromTop (30));
    area.removeFromTop (sectionGap);

    logBounds = area.removeFromBottom (logVisible ? 170 : 20);
    area.removeFromBottom (sectionGap);

    {
        auto content = logBounds;
        auto title = content.removeFromTop (20);
        logToggleButton.setBounds (title.removeFromLeft (120));
        logClearButton.setBounds (title.removeFromRight (56));
        logBox.setBounds (content.reduced (10));
    }

    // Both layouts are computed every time; showPage() decides what's visible.
    {
        settingsBounds = area;
        auto content = settingsBounds;
        content.removeFromTop (20);
        content = content.reduced (10);

        settingsViewport.setBounds (content);
        settingsComponent.setSize (content.getWidth() - settingsViewport.getScrollBarThickness(),
                                   settingsComponent.getPreferredHeight());
    }

    const int listWidth = 340;
    listBounds = area.removeFromLeft (listWidth);
    area.removeFromLeft (sectionGap);
    inspectorBounds = area;

    {
        auto content = listBounds;
        content.removeFromTop (20);
        content = content.reduced (10);

        auto addButtonArea = content.removeFromBottom (30);
        addButton.setBounds (addButtonArea.removeFromLeft (130));
        addButtonArea.removeFromLeft (6);
        duplicateButton.setBounds (addButtonArea.removeFromLeft (100));
        content.removeFromBottom (6);

        auto header = content.removeFromTop (18);
        auto headerInset = header.reduced (6, 0);
        listHeaderValue.setBounds (headerInset.removeFromRight (valueColumnWidth));
        headerInset.removeFromRight (columnGap);
        listHeaderName.setBounds (headerInset);

        content.removeFromTop (4);
        listViewport.setBounds (content);
        inputList.setSize (listViewport.getWidth() - listViewport.getScrollBarThickness(), inputList.getHeight());
        inputList.resized();
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
