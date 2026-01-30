#pragma once

// ==============================================================================
// PresetBrowserView - Modal Popup for Preset Management (Shared)
// ==============================================================================
// Modal overlay containing category tabs, preset list, search, and action buttons.
// Generalized from Iterum: accepts tab labels and string subcategory.
//
// Constitution Compliance:
// - Principle V: Uses VSTGUI components only
// - Principle VI: Cross-platform (no native code)
// ==============================================================================

#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cdatabrowser.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/controls/ctextedit.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/icontrollistener.h"
#include "vstgui/lib/controls/itexteditlistener.h"
#include "vstgui/lib/cvstguitimer.h"
#include "search_debouncer.h"

namespace Krate::Plugins {

class PresetManager;
class PresetDataSource;
class CategoryTabBar;

// Button tag constants for IControlListener
enum PresetBrowserButtonTags {
    kSaveButtonTag = 1,
    kSearchFieldTag = 2,
    kImportButtonTag = 3,
    kDeleteButtonTag = 4,
    kCloseButtonTag = 5,
    // Save dialog buttons
    kSaveDialogSaveTag = 10,
    kSaveDialogCancelTag = 11,
    kSaveDialogNameFieldTag = 12,
    // Delete confirmation dialog buttons
    kDeleteDialogConfirmTag = 20,
    kDeleteDialogCancelTag = 21,
    // Overwrite confirmation dialog buttons
    kOverwriteDialogConfirmTag = 30,
    kOverwriteDialogCancelTag = 31
};

class PresetBrowserView : public VSTGUI::CViewContainer,
                          public VSTGUI::IControlListener,
                          public VSTGUI::IKeyboardHook,
                          public VSTGUI::ITextEditListener {
public:
    PresetBrowserView(const VSTGUI::CRect& size,
                      PresetManager* presetManager,
                      std::vector<std::string> tabLabels);
    ~PresetBrowserView() override;

    // Lifecycle
    void open(const std::string& currentSubcategory);
    void openWithSaveDialog(const std::string& currentSubcategory);
    void close();
    bool isOpen() const { return isOpen_; }

    // CView overrides
    void draw(VSTGUI::CDrawContext* context) override;
    void drawBackgroundRect(VSTGUI::CDrawContext* context, const VSTGUI::CRect& rect) override;
    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons
    ) override;

    // IControlListener
    void valueChanged(VSTGUI::CControl* control) override;

    // IKeyboardHook
    void onKeyboardEvent(VSTGUI::KeyboardEvent& event, VSTGUI::CFrame* frame) override;

    // ITextEditListener
    void onTextEditPlatformControlTookFocus(VSTGUI::CTextEdit* textEdit) override;
    void onTextEditPlatformControlLostFocus(VSTGUI::CTextEdit* textEdit) override;

    // Callbacks
    void onCategoryTabChanged(int newFilterIndex);
    void onSearchTextChanged(const std::string& text);
    void onPresetSelected(int rowIndex);
    void onPresetDoubleClicked(int rowIndex);
    void onSaveClicked();
    void onImportClicked();
    void onDeleteClicked();
    void onCloseClicked();

private:
    PresetManager* presetManager_ = nullptr;
    std::vector<std::string> tabLabels_;

    // Child views (owned by CViewContainer)
    CategoryTabBar* categoryTabBar_ = nullptr;
    VSTGUI::CDataBrowser* presetList_ = nullptr;
    VSTGUI::CTextEdit* searchField_ = nullptr;
    VSTGUI::CTextButton* saveButton_ = nullptr;
    VSTGUI::CTextButton* importButton_ = nullptr;
    VSTGUI::CTextButton* deleteButton_ = nullptr;
    VSTGUI::CTextButton* closeButton_ = nullptr;

    // Data source (owns this)
    PresetDataSource* dataSource_ = nullptr;

    // State
    std::string currentSubcategoryFilter_;  // empty = "All"
    int selectedPresetIndex_ = -1;
    bool isOpen_ = false;

    // Save dialog components (inline overlay)
    VSTGUI::CViewContainer* saveDialogOverlay_ = nullptr;
    VSTGUI::CTextEdit* saveDialogNameField_ = nullptr;
    VSTGUI::CTextButton* saveDialogSaveButton_ = nullptr;
    VSTGUI::CTextButton* saveDialogCancelButton_ = nullptr;
    bool saveDialogVisible_ = false;

    // Delete confirmation dialog components
    VSTGUI::CViewContainer* deleteDialogOverlay_ = nullptr;
    VSTGUI::CTextLabel* deleteDialogLabel_ = nullptr;
    VSTGUI::CTextButton* deleteDialogConfirmButton_ = nullptr;
    VSTGUI::CTextButton* deleteDialogCancelButton_ = nullptr;

    // Overwrite confirmation dialog components
    VSTGUI::CViewContainer* overwriteDialogOverlay_ = nullptr;
    VSTGUI::CTextLabel* overwriteDialogLabel_ = nullptr;
    VSTGUI::CTextButton* overwriteDialogConfirmButton_ = nullptr;
    VSTGUI::CTextButton* overwriteDialogCancelButton_ = nullptr;
    int overwriteTargetIndex_ = -1;

    void createChildViews();
    void createDialogViews();
    void refreshPresetList();
    void updateButtonStates();
    void showSaveDialog();
    void hideSaveDialog();
    void onSaveDialogConfirm();
    void showConfirmDelete();
    void hideDeleteDialog();
    void onDeleteDialogConfirm();
    void showConfirmOverwrite();
    void hideOverwriteDialog();
    void onOverwriteDialogConfirm();

    // Keyboard hook registration
    void registerKeyboardHook();
    void unregisterKeyboardHook();
    bool keyboardHookRegistered_ = false;

    // Search debounce
    SearchDebouncer searchDebouncer_;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> searchPollTimer_;
    bool isSearchFieldFocused_ = false;

    void startSearchPolling();
    void stopSearchPolling();
    void onSearchPollTimer();
    static uint64_t getSystemTimeMs();

    // Convert tab index to subcategory string
    std::string tabIndexToSubcategory(int tabIndex) const;
};

} // namespace Krate::Plugins
