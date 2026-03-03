#pragma once

// ==============================================================================
// SavePresetDialogView - Standalone Save Preset Dialog (Shared)
// ==============================================================================
// Standalone modal overlay for quick preset saving from the main UI.
// This is a simplified version of the save dialog in PresetBrowserView.
//
// Constitution Compliance:
// - Principle V: Uses VSTGUI components only
// - Principle VI: Cross-platform (no native code)
// ==============================================================================

#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/controls/ctextedit.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "outline_button.h"
#include "vstgui/lib/controls/icontrollistener.h"

#include <string>
#include <vector>

namespace Krate::Plugins {

class PresetManager;

// Button tag constants
enum SavePresetDialogTags {
    kSavePresetDialogSaveTag = 100,
    kSavePresetDialogCancelTag = 101,
    kSavePresetDialogNameFieldTag = 102,
    kSavePresetDialogCategoryTag = 103
};

class SavePresetDialogView : public VSTGUI::CViewContainer,
                              public VSTGUI::IControlListener,
                              public VSTGUI::IKeyboardHook {
public:
    SavePresetDialogView(const VSTGUI::CRect& size, PresetManager* presetManager,
                         std::vector<std::string> categories = {});
    ~SavePresetDialogView() override;

    // Lifecycle
    void open(const std::string& currentSubcategory);
    void close();
    bool isOpen() const { return isOpen_; }

    // CView overrides
    void draw(VSTGUI::CDrawContext* context) override;
    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons
    ) override;

    // IControlListener
    void valueChanged(VSTGUI::CControl* control) override;

    // IKeyboardHook
    using CViewContainer::onKeyboardEvent;
    void onKeyboardEvent(VSTGUI::KeyboardEvent& event, VSTGUI::CFrame* frame) override;

private:
    PresetManager* presetManager_ = nullptr;

    // Dialog components
    VSTGUI::CViewContainer* dialogBox_ = nullptr;
    VSTGUI::CTextLabel* titleLabel_ = nullptr;
    VSTGUI::CTextEdit* nameField_ = nullptr;
    VSTGUI::CTextLabel* categoryLabel_ = nullptr;
    VSTGUI::COptionMenu* categoryMenu_ = nullptr;
    OutlineBrowserButton* saveButton_ = nullptr;
    OutlineBrowserButton* cancelButton_ = nullptr;

    // State
    std::vector<std::string> categories_;
    std::string currentSubcategory_;
    bool isOpen_ = false;

    void createDialogViews();
    void onSaveConfirm();

    // Keyboard hook registration
    void registerKeyboardHook();
    void unregisterKeyboardHook();
    bool keyboardHookRegistered_ = false;
};

} // namespace Krate::Plugins
