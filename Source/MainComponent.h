#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "BridgeEngine.h"
#include "GlobalSettingsComponent.h"
#include "MappingInspectorComponent.h"

// The live value in an input list row, settable by hand for testing:
// drag up/right to raise it and down/left to lower it (Shift for fine
// control), or double-click to type a value.
class DraggableValueLabel : public juce::Label
{
public:
    DraggableValueLabel();

    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

    std::function<void()> onPressed;
    std::function<void (float normalisedDelta)> onDragged;   // a full-range drag is 1.0
    std::function<void()> onDragEnded;
    std::function<void (const juce::String&)> onValueTyped;

private:
    juce::Point<float> lastDragPosition;
    bool wasDragged = false;
};

// One compact, single-line summary row in the input list: the input's
// name (its address or MIDI message) and its current live value.
// Click to select for editing in the inspector pane.
class InputSummaryRow : public juce::Component
{
public:
    InputSummaryRow (BridgeEngine& engineToUse, juce::String inputIdToUse,
                     std::function<void (const juce::String&)> onSelected);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    const juce::String& getInputId() const { return inputId; }
    void setSelected (bool shouldBeSelected);
    void refresh();

private:
    BridgeEngine& engine;
    juce::String inputId;
    std::function<void (const juce::String&)> onSelected;

    juce::Label nameLabel;
    DraggableValueLabel valueLabel;
    float dragNormalised = 0.0f;   // where a drag has moved the value to, 0-1 within the input range

    bool selected = false;
    bool hovered = false;
};

// The master list: a scrollable stack of InputSummaryRow showing either
// the mappings or the scalers, tracking which one (if any) is selected.
class InputListComponent : public juce::Component
{
public:
    explicit InputListComponent (BridgeEngine& engineToUse);

    void setShowScalers (bool shouldShowScalers);
    void rebuild();
    void resized() override;
    void refreshRows();
    void setSelectedId (const juce::String& id);

    std::function<void (const juce::String&)> onSelectionChanged;

private:
    void updateSelectionHighlight();

    BridgeEngine& engine;
    bool showScalers = false;
    juce::String selectedId;
    juce::OwnedArray<InputSummaryRow> rows;
};

// A section title with a small arrow in front of it that rotates from
// pointing right (closed) to pointing down (open) when clicked.
class DisclosureButton : public juce::Button, private juce::Timer
{
public:
    explicit DisclosureButton (const juce::String& text);

    // Sets the open state without notifying listeners or animating.
    void setOpen (bool shouldBeOpen);

    void paintButton (juce::Graphics& g, bool isHighlighted, bool isDown) override;

private:
    void clicked() override;
    void timerCallback() override;
    float getTargetAngle() const;

    float angle = 0.0f;
};

class MainComponent;

// The native menu bar (File/Edit) that replaces the old in-window toolbar.
// Actions are still actually triggered by MainComponent::keyPressed for
// their keyboard shortcuts; this just gives them a discoverable, clickable
// home and shows the shortcut text.
class MainMenuModel : public juce::MenuBarModel
{
public:
    explicit MainMenuModel (MainComponent& ownerToUse) : owner (ownerToUse) {}

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected (int menuItemID, int topLevelMenuIndex) override;

private:
    MainComponent& owner;
};

class MainComponent : public juce::Component,
                      private juce::Timer,
                      private juce::AsyncUpdater,
                      private juce::ChangeListener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

    // Set by the owning window; called whenever the project (and so the
    // window title) changes. Call refreshTitle() once after wiring it up
    // to get the initial title (the constructor fires before it's set).
    std::function<void (const juce::String&)> onTitleRequested;
    void refreshTitle() { updateTitle(); }

    // Used by MainMenuModel.
    void newProject();
    void openProject();
    void saveProjectAs();
    void save() { engine.saveProject(); }
    void undo() { engine.undo(); }
    void redo() { engine.redo(); }
    bool canUndo() const { return engine.canUndo(); }
    bool canRedo() const { return engine.canRedo(); }

private:
    enum Page { mappingsPage = 0, scalersPage, settingsPage };

    void timerCallback() override;
    void handleAsyncUpdate() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    Page getCurrentPage() const { return (Page) tabs.getCurrentTabIndex(); }
    juce::String& currentSelection() { return getCurrentPage() == scalersPage ? selectedScalerId : selectedMappingId; }
    void showPage (Page page);

    void appendLog (const juce::String& text);
    void handleProjectChanged();
    void selectInput (const juce::String& id);
    void rebuildInspector();
    void drawSection (juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title);
    void updateTitle();

    BridgeEngine engine;
    juce::String selectedMappingId, selectedScalerId;

    MainMenuModel menuModel { *this };
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::TabbedButtonBar tabs { juce::TabbedButtonBar::TabsAtTop };

    juce::Viewport settingsViewport;
    GlobalSettingsComponent settingsComponent { engine };

    juce::Viewport listViewport;
    InputListComponent inputList { engine };
    juce::Label listHeaderName { {}, "Input" };
    juce::Label listHeaderValue { {}, "Value" };
    juce::TextButton addButton;

    juce::Viewport inspectorViewport;
    std::unique_ptr<MappingInspectorComponent> inspector;
    juce::Label inspectorPlaceholder;

    DisclosureButton logToggleButton { "Activity Log" };
    juce::TextButton logClearButton { "Clear" };
    juce::TextEditor logBox;
    bool logVisible = true;

    // Section outline rectangles, computed in resized(), painted in paint().
    juce::Rectangle<int> settingsBounds, listBounds, inspectorBounds, logBounds;

    juce::TooltipWindow tooltipWindow { this };
};
