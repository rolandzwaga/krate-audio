#pragma once

// ==============================================================================
// Custom Buttons: Outline-style buttons for preset browser and save
// ==============================================================================

#include "ui/outline_button.h"

#include <string>

namespace Disrumpo {

class Controller;  // Forward declaration

// Shared dark-theme outline button base (CView + virtual onClick()).
using Krate::Plugins::OutlineButton;

// ==============================================================================
// PresetBrowserButton: Opens the preset browser modal (Spec 010)
// ==============================================================================

class PresetBrowserButton : public OutlineButton {
public:
    PresetBrowserButton(const VSTGUI::CRect& size, Controller* controller)
        : OutlineButton(size, "Presets")
        , controller_(controller) {}
protected:
    void onClick() override;
private:
    Controller* controller_ = nullptr;
};

// ==============================================================================
// SavePresetButton: Opens the save preset dialog (Spec 010)
// ==============================================================================

class SavePresetButton : public OutlineButton {
public:
    SavePresetButton(const VSTGUI::CRect& size, Controller* controller)
        : OutlineButton(size, "Save")
        , controller_(controller) {}
protected:
    void onClick() override;
private:
    Controller* controller_ = nullptr;
};

} // namespace Disrumpo
