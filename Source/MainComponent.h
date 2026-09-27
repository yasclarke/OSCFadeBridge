#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "BridgeEngine.h"
#include "GlobalSettingsComponent.h"
#include "MappingInspectorComponent.h"

// One compact, single-line summary row in the mapping list: input
// address (doubles as the mapping's name) and its current live value.
// Click to select for editing in the inspector pane.
class MappingSummaryRow : public juce::Component
{
public:
    MappingSummaryRow (BridgeEngine& engineToUse, juce::String mappingIdToUse,
                        std::function<void (const juce::String&)> onSelected);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    const juce::String& getMappingId() const { return mappingId; }
    void setSelected (bool shouldBeSelected);
    void refresh();

private:
    BridgeEngine& engine;
    juce::String mappingId;
    std::function<void (const juce::String&)> onSelected;

    juce::Label addressLabel;
    juce::Label valueLabel;

    bool selected = false;
    bool hovered = false;
};

// The master list: a header row plus a scrollable stack of
// MappingSummaryRow, tracking which mapping (if any) is selected.
class MappingListComponent : public juce::Component
{
public:
    explicit MappingListComponent (BridgeEngine& engineToUse);

    void rebuild();
    void resized() override;
    void refreshRows();
    void setSelectedId (const juce::String& id);

    std::function<void (const juce::String&)> onSelectionChanged;

private:
    void updateSelectionHighlight();

    BridgeEngine& engine;
    juce::String selectedId;
    juce::OwnedArray<MappingSummaryRow> rows;
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

class MainComponent : public juce::Component, private juce::Timer
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
    void timerCallback() override;
    void appendLog (const juce::String& text);
    void handleProjectChanged();
    void handleFieldsChanged();
    void selectMapping (const juce::String& id);
    void rebuildInspector();
    void drawSection (juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title);
    void updateTitle();

    BridgeEngine engine;
    juce::String selectedMappingId;

    MainMenuModel menuModel { *this };
    std::unique_ptr<juce::FileChooser> fileChooser;

    GlobalSettingsComponent settingsComponent { engine };

    juce::Viewport mappingsViewport;
    MappingListComponent mappingsList { engine };
    juce::Label listHeaderAddress { {}, "Input Address" };
    juce::Label listHeaderValue { {}, "Value" };
    juce::TextButton addMappingButton { "+ Add Mapping" };

    juce::Viewport inspectorViewport;
    std::unique_ptr<MappingInspectorComponent> inspector;
    juce::Label inspectorPlaceholder { {}, "Select a mapping on the left to edit its details." };

    juce::TextButton logToggleButton { "Hide" };
    juce::TextButton logClearButton { "Clear" };
    juce::Label logLabel { {}, "Activity Log" };
    juce::TextEditor logBox;
    bool logVisible = true;

    // Section outline rectangles, computed in resized(), painted in paint().
    juce::Rectangle<int> settingsBounds, mappingsBounds, inspectorBounds, logBounds;

    juce::TooltipWindow tooltipWindow { this };
};
