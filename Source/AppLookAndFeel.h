#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// App-wide look: Helvetica Neue for all sans-serif text, and tabs with
// some breathing room either side of their labels.
class AppLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AppLookAndFeel()
    {
        setDefaultSansSerifTypefaceName ("Helvetica Neue");
    }

    int getTabButtonBestWidth (juce::TabBarButton& button, int tabDepth) override
    {
        constexpr int horizontalPadding = 18;
        return LookAndFeel_V4::getTabButtonBestWidth (button, tabDepth) + horizontalPadding * 2;
    }
};
