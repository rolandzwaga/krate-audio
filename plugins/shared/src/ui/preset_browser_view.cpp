#include "preset_browser_view.h"
#include "preset_browser_logic.h"
#include "preset_data_source.h"
#include "category_tab_bar.h"
#include "../preset/preset_manager.h"

#include "vstgui/lib/cfileselector.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/controls/ctextedit.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace Krate::Plugins {

// =============================================================================
// DialogButton - Custom button that doesn't consume Enter/Escape events
// =============================================================================

class DialogButton : public VSTGUI::CTextButton {
public:
    using CTextButton::CTextButton;

    void onKeyboardEvent(VSTGUI::KeyboardEvent& event) override {
        if (event.virt == VSTGUI::VirtualKey::Return ||
            event.virt == VSTGUI::VirtualKey::Enter ||
            event.virt == VSTGUI::VirtualKey::Escape) {
            return;  // Skip, don't consume
        }
        CTextButton::onKeyboardEvent(event);
    }
};

// =============================================================================
// Layout Constants
// =============================================================================

namespace Layout {
    constexpr float kContentMargin = 40.0f;
    constexpr float kTitleBarHeight = 32.0f;
    constexpr float kSearchHeight = 28.0f;
    constexpr float kButtonBarHeight = 36.0f;
    constexpr float kModeTabWidth = 100.0f;
    constexpr float kButtonWidth = 80.0f;
    constexpr float kButtonSpacing = 8.0f;
    constexpr float kInnerPadding = 8.0f;
}

// =============================================================================
// Construction / Destruction
// =============================================================================

PresetBrowserView::PresetBrowserView(const VSTGUI::CRect& size,
                                     PresetManager* presetManager,
                                     std::vector<std::string> tabLabels)
    : CViewContainer(size)
    , presetManager_(presetManager)
    , tabLabels_(std::move(tabLabels))
{
    createChildViews();
}

PresetBrowserView::~PresetBrowserView() {
    stopSearchPolling();
    unregisterKeyboardHook();
    delete dataSource_;
}

// =============================================================================
// Lifecycle
// =============================================================================

void PresetBrowserView::open(const std::string& currentSubcategory) {
    currentSubcategoryFilter_ = currentSubcategory;
    isOpen_ = true;
    setVisible(true);

    registerKeyboardHook();

    // Set category tab to match the current subcategory
    if (categoryTabBar_) {
        int tabIndex = 0; // Default to "All"
        if (!currentSubcategory.empty()) {
            for (size_t i = 1; i < tabLabels_.size(); ++i) {
                if (tabLabels_[i] == currentSubcategory) {
                    tabIndex = static_cast<int>(i);
                    break;
                }
            }
        }
        categoryTabBar_->setSelectedTab(tabIndex);
    }

    refreshPresetList();
    updateButtonStates();
}

void PresetBrowserView::openWithSaveDialog(const std::string& currentSubcategory) {
    open(currentSubcategory);
    showSaveDialog();
}

void PresetBrowserView::close() {
    stopSearchPolling();

    if (searchDebouncer_.hasPendingFilter()) {
        auto query = searchDebouncer_.consumePendingFilter();
        onSearchTextChanged(query);
    }

    unregisterKeyboardHook();

    isOpen_ = false;
    setVisible(false);
}

// =============================================================================
// Drawing
// =============================================================================

void PresetBrowserView::drawBackgroundRect(VSTGUI::CDrawContext* context, const VSTGUI::CRect& /*rect*/) {
    auto viewSize = getViewSize();
    context->setFillColor(VSTGUI::CColor(0, 0, 0, 180));
    context->drawRect(viewSize, VSTGUI::kDrawFilled);

    auto contentRect = VSTGUI::CRect(
        viewSize.left + Layout::kContentMargin,
        viewSize.top + Layout::kContentMargin,
        viewSize.right - Layout::kContentMargin,
        viewSize.bottom - Layout::kContentMargin
    );

    context->setFillColor(VSTGUI::CColor(50, 50, 55, 255));
    context->drawRect(contentRect, VSTGUI::kDrawFilled);

    context->setFrameColor(VSTGUI::CColor(80, 80, 85));
    context->setLineWidth(1.0);
    context->drawRect(contentRect, VSTGUI::kDrawStroked);

    auto titleRect = contentRect;
    titleRect.bottom = titleRect.top + Layout::kTitleBarHeight;
    context->setFillColor(VSTGUI::CColor(35, 35, 40, 255));
    context->drawRect(titleRect, VSTGUI::kDrawFilled);

    context->setFontColor(VSTGUI::CColor(255, 255, 255));
    auto titleTextRect = titleRect;
    titleTextRect.inset(12, 0);
    context->drawString("Preset Browser", titleTextRect, VSTGUI::kLeftText);
}

void PresetBrowserView::draw(VSTGUI::CDrawContext* context) {
    CViewContainer::draw(context);
}

// =============================================================================
// Event Handling
// =============================================================================

VSTGUI::CMouseEventResult PresetBrowserView::onMouseDown(
    VSTGUI::CPoint& where,
    const VSTGUI::CButtonState& buttons
) {
    auto viewSize = getViewSize();
    auto contentRect = VSTGUI::CRect(
        viewSize.left + Layout::kContentMargin,
        viewSize.top + Layout::kContentMargin,
        viewSize.right - Layout::kContentMargin,
        viewSize.bottom - Layout::kContentMargin
    );

    bool anyDialogVisible = ((saveDialogOverlay_ != nullptr) && saveDialogOverlay_->isVisible()) ||
                            ((deleteDialogOverlay_ != nullptr) && deleteDialogOverlay_->isVisible()) ||
                            ((overwriteDialogOverlay_ != nullptr) && overwriteDialogOverlay_->isVisible());

    if (!anyDialogVisible && !contentRect.pointInside(where)) {
        close();
        return VSTGUI::kMouseEventHandled;
    }

    if (presetList_ && dataSource_) {
        dataSource_->capturePreClickSelection(presetList_);
    }

    if (!anyDialogVisible && presetList_ && dataSource_) {
        auto listBounds = presetList_->getViewSize();
        if (listBounds.pointInside(where)) {
            auto localY = where.y - listBounds.top;
            auto numRows = dataSource_->dbGetNumRows(presetList_);
            auto rowHeight = dataSource_->dbGetRowHeight(presetList_);
            auto contentHeight = numRows * rowHeight;

            if (localY >= contentHeight) {
                presetList_->unselectAll();
                selectedPresetIndex_ = -1;
                updateButtonStates();
                return VSTGUI::kMouseEventHandled;
            }
        }
    }

    return CViewContainer::onMouseDown(where, buttons);
}

void PresetBrowserView::onKeyboardEvent(VSTGUI::KeyboardEvent& event, VSTGUI::CFrame* /*frame*/) {
    if (!isOpen_) return;
    if (event.type != VSTGUI::EventType::KeyDown) return;

    KeyCode keyCode = KeyCode::Other;
    if (event.virt == VSTGUI::VirtualKey::Escape) {
        keyCode = KeyCode::Escape;
    } else if (event.virt == VSTGUI::VirtualKey::Return ||
               event.virt == VSTGUI::VirtualKey::Enter) {
        keyCode = KeyCode::Enter;
    }

    bool saveVisible = ((saveDialogOverlay_ != nullptr) && saveDialogOverlay_->isVisible());
    bool deleteVisible = ((deleteDialogOverlay_ != nullptr) && deleteDialogOverlay_->isVisible());
    bool overwriteVisible = ((overwriteDialogOverlay_ != nullptr) && overwriteDialogOverlay_->isVisible());

    KeyAction action = determineKeyAction(keyCode, saveVisible, deleteVisible, overwriteVisible);

    switch (action) {
        case KeyAction::CloseBrowser:
            close();
            event.consumed = true;
            return;
        case KeyAction::ConfirmSaveDialog:
            onSaveDialogConfirm();
            event.consumed = true;
            return;
        case KeyAction::CancelSaveDialog:
            hideSaveDialog();
            event.consumed = true;
            return;
        case KeyAction::ConfirmDeleteDialog:
            onDeleteDialogConfirm();
            event.consumed = true;
            return;
        case KeyAction::CancelDeleteDialog:
            hideDeleteDialog();
            event.consumed = true;
            return;
        case KeyAction::ConfirmOverwriteDialog:
            onOverwriteDialogConfirm();
            event.consumed = true;
            return;
        case KeyAction::CancelOverwriteDialog:
            hideOverwriteDialog();
            event.consumed = true;
            return;
        case KeyAction::None:
        default:
            break;
    }
}

// =============================================================================
// IControlListener
// =============================================================================

void PresetBrowserView::valueChanged(VSTGUI::CControl* control) {
    if (!control) return;

    switch (control->getTag()) {
        case kSaveButtonTag:
            onSaveClicked();
            break;
        case kSearchFieldTag:
            if (searchField_) {
                std::string currentText = searchField_->getText().getString();
                bool applyNow = searchDebouncer_.onTextChanged(currentText, getSystemTimeMs());
                if (applyNow) {
                    onSearchTextChanged("");
                }
            }
            break;
        case kImportButtonTag:
            onImportClicked();
            break;
        case kDeleteButtonTag:
            onDeleteClicked();
            break;
        case kCloseButtonTag:
            onCloseClicked();
            break;
        case kSaveDialogSaveTag:
            onSaveDialogConfirm();
            break;
        case kSaveDialogCancelTag:
            hideSaveDialog();
            break;
        case kDeleteDialogConfirmTag:
            onDeleteDialogConfirm();
            break;
        case kDeleteDialogCancelTag:
            hideDeleteDialog();
            break;
        case kOverwriteDialogConfirmTag:
            onOverwriteDialogConfirm();
            break;
        case kOverwriteDialogCancelTag:
            hideOverwriteDialog();
            break;
        default:
            break;
    }
}

// =============================================================================
// Callbacks
// =============================================================================

void PresetBrowserView::onCategoryTabChanged(int newFilterIndex) {
    // Convert filter index to subcategory string
    // -1 = "All" (empty string), 0+ = index into subcategory names
    if (newFilterIndex < 0) {
        currentSubcategoryFilter_.clear();
    } else {
        // tabLabels_[0] = "All", tabLabels_[1+] = subcategories
        // newFilterIndex maps to tabLabels_[newFilterIndex + 1]
        size_t labelIdx = static_cast<size_t>(newFilterIndex) + 1;
        if (labelIdx < tabLabels_.size()) {
            currentSubcategoryFilter_ = tabLabels_[labelIdx];
        }
    }

    if (dataSource_) {
        dataSource_->setSubcategoryFilter(currentSubcategoryFilter_);
        dataSource_->clearSelectionState();
    }
    if (presetList_) {
        presetList_->unselectAll();
        presetList_->recalculateLayout(true);
        presetList_->invalid();
    }
    selectedPresetIndex_ = -1;
    updateButtonStates();
}

void PresetBrowserView::onSearchTextChanged(const std::string& text) {
    if (dataSource_) {
        dataSource_->setSearchFilter(text);
    }
    if (presetList_) {
        presetList_->recalculateLayout(true);
        presetList_->invalid();
    }
    selectedPresetIndex_ = -1;
    updateButtonStates();
}

void PresetBrowserView::onPresetSelected(int rowIndex) {
    selectedPresetIndex_ = rowIndex;
    updateButtonStates();
}

void PresetBrowserView::onPresetDoubleClicked(int rowIndex) {
    if (!presetManager_ || !dataSource_) return;

    const PresetInfo* preset = dataSource_->getPresetAtRow(rowIndex);
    if (!preset) return;

    if (presetManager_->loadPreset(*preset)) {
        close();
    } else {
        std::string error = "Preset load failed: " + presetManager_->getLastError();
        error += " | Path: " + preset->path.string() + "\n";
#ifdef _WIN32
        OutputDebugStringA(error.c_str());
#endif
    }
}

void PresetBrowserView::onSaveClicked() {
    if (selectedPresetIndex_ >= 0 && dataSource_) {
        const PresetInfo* preset = dataSource_->getPresetAtRow(selectedPresetIndex_);
        if (preset && !preset->isFactory) {
            showConfirmOverwrite();
            return;
        }
    }
    showSaveDialog();
}

void PresetBrowserView::onImportClicked() {
    auto *frame = getFrame();
    if (!frame) return;

    auto selector = VSTGUI::owned(VSTGUI::CNewFileSelector::create(
        frame,
        VSTGUI::CNewFileSelector::kSelectFile
    ));

    if (!selector) return;

    selector->setTitle("Import Preset");
    VSTGUI::CFileExtension vstPresetExt("VST3 Preset", "vstpreset");
    selector->setDefaultExtension(vstPresetExt);
    selector->addFileExtension(vstPresetExt);

    selector->run([this](VSTGUI::CNewFileSelector* sel) {
        if (sel->getNumSelectedFiles() > 0) {
            auto path = std::filesystem::path(sel->getSelectedFile(0));
            if (presetManager_ && presetManager_->importPreset(path)) {
                refreshPresetList();
            }
        }
    });
}

void PresetBrowserView::onDeleteClicked() {
    showConfirmDelete();
}

void PresetBrowserView::onCloseClicked() {
    close();
}

// =============================================================================
// Private Methods
// =============================================================================

void PresetBrowserView::createChildViews() {
    auto viewSize = getViewSize();

    auto contentRect = VSTGUI::CRect(
        viewSize.left + Layout::kContentMargin,
        viewSize.top + Layout::kContentMargin,
        viewSize.right - Layout::kContentMargin,
        viewSize.bottom - Layout::kContentMargin
    );

    auto innerTop = contentRect.top + Layout::kTitleBarHeight + Layout::kInnerPadding;
    auto innerBottom = contentRect.bottom - Layout::kButtonBarHeight - Layout::kInnerPadding;

    // Category Tab Bar (left side)
    auto tabBarRect = VSTGUI::CRect(
        contentRect.left + Layout::kInnerPadding,
        innerTop,
        contentRect.left + Layout::kInnerPadding + Layout::kModeTabWidth,
        innerBottom
    );
    categoryTabBar_ = new CategoryTabBar(tabBarRect, tabLabels_);
    categoryTabBar_->setSelectionCallback([this](int filterIndex) {
        onCategoryTabChanged(filterIndex);
    });
    addView(categoryTabBar_);

    // Search Field
    auto browserLeft = tabBarRect.right + Layout::kInnerPadding;
    auto browserRight = contentRect.right - Layout::kInnerPadding;

    auto searchRect = VSTGUI::CRect(
        browserLeft,
        innerTop,
        browserRight,
        innerTop + Layout::kSearchHeight
    );
    searchField_ = new VSTGUI::CTextEdit(searchRect, this, kSearchFieldTag, "");
    searchField_->setBackColor(VSTGUI::CColor(35, 35, 40));
    searchField_->setFontColor(VSTGUI::CColor(200, 200, 200));
    searchField_->setFrameColor(VSTGUI::CColor(70, 70, 75));
    searchField_->setStyle(VSTGUI::CTextEdit::kRoundRectStyle);
    searchField_->setPlaceholderString("Search presets...");
    searchField_->setImmediateTextChange(true);
    searchField_->registerTextEditListener(this);
    addView(searchField_);

    // Preset List
    auto listRect = VSTGUI::CRect(
        browserLeft,
        searchRect.bottom + Layout::kInnerPadding,
        browserRight,
        innerBottom
    );

    dataSource_ = new PresetDataSource();
    dataSource_->setSelectionCallback([this](int row) {
        onPresetSelected(row);
    });
    dataSource_->setDoubleClickCallback([this](int row) {
        onPresetDoubleClicked(row);
    });

    presetList_ = new VSTGUI::CDataBrowser(
        listRect,
        dataSource_,
        VSTGUI::CDataBrowser::kDrawRowLines | VSTGUI::CDataBrowser::kDrawColumnLines,
        VSTGUI::CScrollView::kAutoHideScrollbars
    );
    presetList_->setBackgroundColor(VSTGUI::CColor(40, 40, 45));
    addView(presetList_);

    // Button Bar
    auto buttonY = contentRect.bottom - Layout::kButtonBarHeight;
    auto buttonHeight = Layout::kButtonBarHeight - Layout::kInnerPadding;
    auto currentX = contentRect.left + Layout::kInnerPadding;

    saveButton_ = new VSTGUI::CTextButton(
        VSTGUI::CRect(currentX, buttonY, currentX + Layout::kButtonWidth, buttonY + buttonHeight),
        this, kSaveButtonTag, "Save");
    saveButton_->setFrameColor(VSTGUI::CColor(80, 80, 85));
    saveButton_->setTextColor(VSTGUI::CColor(255, 255, 255));
    addView(saveButton_);
    currentX += Layout::kButtonWidth + Layout::kButtonSpacing;

    importButton_ = new VSTGUI::CTextButton(
        VSTGUI::CRect(currentX, buttonY, currentX + Layout::kButtonWidth, buttonY + buttonHeight),
        this, kImportButtonTag, "Import...");
    importButton_->setFrameColor(VSTGUI::CColor(80, 80, 85));
    importButton_->setTextColor(VSTGUI::CColor(255, 255, 255));
    addView(importButton_);
    currentX += Layout::kButtonWidth + Layout::kButtonSpacing;

    deleteButton_ = new VSTGUI::CTextButton(
        VSTGUI::CRect(currentX, buttonY, currentX + Layout::kButtonWidth, buttonY + buttonHeight),
        this, kDeleteButtonTag, "Delete");
    deleteButton_->setFrameColor(VSTGUI::CColor(120, 60, 60));
    deleteButton_->setTextColor(VSTGUI::CColor(255, 255, 255));
    addView(deleteButton_);

    closeButton_ = new VSTGUI::CTextButton(
        VSTGUI::CRect(contentRect.right - Layout::kInnerPadding - Layout::kButtonWidth, buttonY,
                       contentRect.right - Layout::kInnerPadding, buttonY + buttonHeight),
        this, kCloseButtonTag, "Close");
    closeButton_->setFrameColor(VSTGUI::CColor(80, 80, 85));
    closeButton_->setTextColor(VSTGUI::CColor(255, 255, 255));
    addView(closeButton_);

    // Dialog Overlays
    createDialogViews();

    setVisible(false);
}

void PresetBrowserView::refreshPresetList() {
    if (!presetManager_ || !dataSource_) return;

    auto presets = presetManager_->scanPresets();
    dataSource_->setPresets(presets);
    dataSource_->setSubcategoryFilter(currentSubcategoryFilter_);

    if (presetList_) {
        presetList_->recalculateLayout(true);
        presetList_->invalid();
    }
}

void PresetBrowserView::updateButtonStates() {
    bool hasSelection = (selectedPresetIndex_ >= 0);
    bool isFactoryPreset = false;

    if (hasSelection && dataSource_) {
        const PresetInfo* preset = dataSource_->getPresetAtRow(selectedPresetIndex_);
        if (preset) {
            isFactoryPreset = preset->isFactory;
        }
    }

    if (deleteButton_) {
        bool canDelete = hasSelection && !isFactoryPreset;
        deleteButton_->setMouseEnabled(canDelete);
        deleteButton_->setAlphaValue(canDelete ? 1.0f : 0.4f);
    }

    if (saveButton_) {
        saveButton_->setMouseEnabled(true);
    }
}

void PresetBrowserView::createDialogViews() {
    auto viewSize = getViewSize();
    auto centerX = viewSize.getWidth() / 2.0f;
    auto centerY = viewSize.getHeight() / 2.0f;

    constexpr float kDialogWidth = 300.0f;
    constexpr float kPadding = 12.0f;
    constexpr float kButtonHeight = 28.0f;
    constexpr float kButtonWidth = 80.0f;
    constexpr float kButtonGap = 10.0f;

    // Save Dialog
    {
        constexpr float kDialogHeight = 120.0f;
        constexpr float kFieldHeight = 28.0f;

        auto dialogRect = VSTGUI::CRect(
            centerX - kDialogWidth / 2.0f,
            centerY - kDialogHeight / 2.0f,
            centerX + kDialogWidth / 2.0f,
            centerY + kDialogHeight / 2.0f
        );

        saveDialogOverlay_ = new VSTGUI::CViewContainer(dialogRect);
        saveDialogOverlay_->setBackgroundColor(VSTGUI::CColor(50, 50, 55));
        saveDialogOverlay_->setVisible(false);

        auto titleRect = VSTGUI::CRect(kPadding, 8.0f, kDialogWidth - kPadding, 26.0f);
        auto* titleLabel = new VSTGUI::CTextLabel(titleRect, "Save Preset");
        titleLabel->setFontColor(VSTGUI::CColor(255, 255, 255));
        titleLabel->setBackColor(VSTGUI::CColor(0, 0, 0, 0));
        titleLabel->setFrameColor(VSTGUI::CColor(0, 0, 0, 0));
        auto titleFont = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 12, VSTGUI::kBoldFace);
        titleLabel->setFont(titleFont);
        saveDialogOverlay_->addView(titleLabel);

        auto fieldRect = VSTGUI::CRect(kPadding, 32.0f, kDialogWidth - kPadding, 32.0f + kFieldHeight);
        saveDialogNameField_ = new VSTGUI::CTextEdit(fieldRect, this, kSaveDialogNameFieldTag, "New Preset");
        saveDialogNameField_->setBackColor(VSTGUI::CColor(35, 35, 40));
        saveDialogNameField_->setFontColor(VSTGUI::CColor(220, 220, 220));
        saveDialogNameField_->setFrameColor(VSTGUI::CColor(80, 80, 85));
        saveDialogNameField_->setStyle(VSTGUI::CTextEdit::kRoundRectStyle);
        saveDialogOverlay_->addView(saveDialogNameField_);

        auto buttonY = kDialogHeight - kPadding - kButtonHeight;
        auto buttonsWidth = kButtonWidth * 2 + kButtonGap;
        auto buttonsLeft = (kDialogWidth - buttonsWidth) / 2.0f;

        saveDialogSaveButton_ = new VSTGUI::CTextButton(
            VSTGUI::CRect(buttonsLeft, buttonY, buttonsLeft + kButtonWidth, buttonY + kButtonHeight),
            this, kSaveDialogSaveTag, "Save");
        saveDialogSaveButton_->setFrameColor(VSTGUI::CColor(60, 120, 180));
        saveDialogSaveButton_->setTextColor(VSTGUI::CColor(255, 255, 255));
        saveDialogOverlay_->addView(saveDialogSaveButton_);

        saveDialogCancelButton_ = new VSTGUI::CTextButton(
            VSTGUI::CRect(buttonsLeft + kButtonWidth + kButtonGap, buttonY,
                           buttonsLeft + kButtonWidth * 2 + kButtonGap, buttonY + kButtonHeight),
            this, kSaveDialogCancelTag, "Cancel");
        saveDialogCancelButton_->setFrameColor(VSTGUI::CColor(80, 80, 85));
        saveDialogCancelButton_->setTextColor(VSTGUI::CColor(255, 255, 255));
        saveDialogOverlay_->addView(saveDialogCancelButton_);

        addView(saveDialogOverlay_);
    }

    // Delete Confirmation Dialog
    {
        constexpr float kDialogHeight = 100.0f;

        auto dialogRect = VSTGUI::CRect(
            centerX - kDialogWidth / 2.0f,
            centerY - kDialogHeight / 2.0f,
            centerX + kDialogWidth / 2.0f,
            centerY + kDialogHeight / 2.0f
        );

        deleteDialogOverlay_ = new VSTGUI::CViewContainer(dialogRect);
        deleteDialogOverlay_->setBackgroundColor(VSTGUI::CColor(50, 50, 55));
        deleteDialogOverlay_->setVisible(false);

        auto titleRect = VSTGUI::CRect(kPadding, 8.0f, kDialogWidth - kPadding, 26.0f);
        auto* titleLabel = new VSTGUI::CTextLabel(titleRect, "Delete Preset?");
        titleLabel->setFontColor(VSTGUI::CColor(255, 200, 200));
        titleLabel->setBackColor(VSTGUI::CColor(0, 0, 0, 0));
        titleLabel->setFrameColor(VSTGUI::CColor(0, 0, 0, 0));
        auto titleFont = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 12, VSTGUI::kBoldFace);
        titleLabel->setFont(titleFont);
        deleteDialogOverlay_->addView(titleLabel);

        auto labelRect = VSTGUI::CRect(kPadding, 32.0f, kDialogWidth - kPadding, 50.0f);
        deleteDialogLabel_ = new VSTGUI::CTextLabel(labelRect, "");
        deleteDialogLabel_->setFontColor(VSTGUI::CColor(200, 200, 200));
        deleteDialogLabel_->setBackColor(VSTGUI::CColor(0, 0, 0, 0));
        deleteDialogLabel_->setFrameColor(VSTGUI::CColor(0, 0, 0, 0));
        auto labelFont = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 11);
        deleteDialogLabel_->setFont(labelFont);
        deleteDialogOverlay_->addView(deleteDialogLabel_);

        auto buttonY = kDialogHeight - kPadding - kButtonHeight;
        auto buttonsWidth = kButtonWidth * 2 + kButtonGap;
        auto buttonsLeft = (kDialogWidth - buttonsWidth) / 2.0f;

        deleteDialogConfirmButton_ = new DialogButton(
            VSTGUI::CRect(buttonsLeft, buttonY, buttonsLeft + kButtonWidth, buttonY + kButtonHeight),
            this, kDeleteDialogConfirmTag, "Delete");
        deleteDialogConfirmButton_->setFrameColor(VSTGUI::CColor(180, 60, 60));
        deleteDialogConfirmButton_->setTextColor(VSTGUI::CColor(255, 255, 255));
        deleteDialogOverlay_->addView(deleteDialogConfirmButton_);

        deleteDialogCancelButton_ = new DialogButton(
            VSTGUI::CRect(buttonsLeft + kButtonWidth + kButtonGap, buttonY,
                           buttonsLeft + kButtonWidth * 2 + kButtonGap, buttonY + kButtonHeight),
            this, kDeleteDialogCancelTag, "Cancel");
        deleteDialogCancelButton_->setFrameColor(VSTGUI::CColor(80, 80, 85));
        deleteDialogCancelButton_->setTextColor(VSTGUI::CColor(255, 255, 255));
        deleteDialogOverlay_->addView(deleteDialogCancelButton_);

        addView(deleteDialogOverlay_);
    }

    // Overwrite Confirmation Dialog
    {
        constexpr float kDialogHeight = 100.0f;

        auto dialogRect = VSTGUI::CRect(
            centerX - kDialogWidth / 2.0f,
            centerY - kDialogHeight / 2.0f,
            centerX + kDialogWidth / 2.0f,
            centerY + kDialogHeight / 2.0f
        );

        overwriteDialogOverlay_ = new VSTGUI::CViewContainer(dialogRect);
        overwriteDialogOverlay_->setBackgroundColor(VSTGUI::CColor(50, 50, 55));
        overwriteDialogOverlay_->setVisible(false);

        auto titleRect = VSTGUI::CRect(kPadding, 8.0f, kDialogWidth - kPadding, 26.0f);
        auto* titleLabel = new VSTGUI::CTextLabel(titleRect, "Overwrite Preset?");
        titleLabel->setFontColor(VSTGUI::CColor(255, 220, 150));
        titleLabel->setBackColor(VSTGUI::CColor(0, 0, 0, 0));
        titleLabel->setFrameColor(VSTGUI::CColor(0, 0, 0, 0));
        auto titleFont = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 12, VSTGUI::kBoldFace);
        titleLabel->setFont(titleFont);
        overwriteDialogOverlay_->addView(titleLabel);

        auto labelRect = VSTGUI::CRect(kPadding, 32.0f, kDialogWidth - kPadding, 50.0f);
        overwriteDialogLabel_ = new VSTGUI::CTextLabel(labelRect, "");
        overwriteDialogLabel_->setFontColor(VSTGUI::CColor(200, 200, 200));
        overwriteDialogLabel_->setBackColor(VSTGUI::CColor(0, 0, 0, 0));
        overwriteDialogLabel_->setFrameColor(VSTGUI::CColor(0, 0, 0, 0));
        auto labelFont = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 11);
        overwriteDialogLabel_->setFont(labelFont);
        overwriteDialogOverlay_->addView(overwriteDialogLabel_);

        auto buttonY = kDialogHeight - kPadding - kButtonHeight;
        auto buttonsWidth = kButtonWidth * 2 + kButtonGap;
        auto buttonsLeft = (kDialogWidth - buttonsWidth) / 2.0f;

        overwriteDialogConfirmButton_ = new DialogButton(
            VSTGUI::CRect(buttonsLeft, buttonY, buttonsLeft + kButtonWidth, buttonY + kButtonHeight),
            this, kOverwriteDialogConfirmTag, "Overwrite");
        overwriteDialogConfirmButton_->setFrameColor(VSTGUI::CColor(180, 140, 60));
        overwriteDialogConfirmButton_->setTextColor(VSTGUI::CColor(255, 255, 255));
        overwriteDialogOverlay_->addView(overwriteDialogConfirmButton_);

        overwriteDialogCancelButton_ = new DialogButton(
            VSTGUI::CRect(buttonsLeft + kButtonWidth + kButtonGap, buttonY,
                           buttonsLeft + kButtonWidth * 2 + kButtonGap, buttonY + kButtonHeight),
            this, kOverwriteDialogCancelTag, "Cancel");
        overwriteDialogCancelButton_->setFrameColor(VSTGUI::CColor(80, 80, 85));
        overwriteDialogCancelButton_->setTextColor(VSTGUI::CColor(255, 255, 255));
        overwriteDialogOverlay_->addView(overwriteDialogCancelButton_);

        addView(overwriteDialogOverlay_);
    }
}

void PresetBrowserView::showSaveDialog() {
    if (saveDialogOverlay_) {
        if (saveDialogNameField_) {
            saveDialogNameField_->setText("New Preset");
        }
        saveDialogOverlay_->setVisible(true);
        saveDialogVisible_ = true;

        if (auto *frame = getFrame()) {
            frame->setFocusView(saveDialogNameField_);
        }

        invalid();
    }
}

void PresetBrowserView::hideSaveDialog() {
    if (saveDialogOverlay_) {
        saveDialogOverlay_->setVisible(false);
        saveDialogVisible_ = false;
        invalid();
    }
}

void PresetBrowserView::onSaveDialogConfirm() {
    if (!presetManager_ || !saveDialogNameField_) {
        hideSaveDialog();
        return;
    }

    if (auto *frame = getFrame()) {
        frame->setFocusView(nullptr);
    }

    const auto& text = saveDialogNameField_->getText();
    std::string name = text.empty() ? "New Preset" : text.getString();

    size_t start = name.find_first_not_of(" \t");
    size_t end = name.find_last_not_of(" \t");
    if (start != std::string::npos && end != std::string::npos) {
        name = name.substr(start, end - start + 1);
    }

    if (name.empty()) {
        name = "New Preset";
    }

    // Use string subcategory directly
    std::string subcategory = currentSubcategoryFilter_;
    if (subcategory.empty() && !tabLabels_.empty() && tabLabels_.size() > 1) {
        // Default to first non-"All" subcategory
        subcategory = tabLabels_[1];
    }

    presetManager_->savePreset(name, subcategory, "");

    hideSaveDialog();
    refreshPresetList();
}

void PresetBrowserView::showConfirmDelete() {
    if (selectedPresetIndex_ < 0 || !dataSource_ || !presetManager_) return;

    const PresetInfo* preset = dataSource_->getPresetAtRow(selectedPresetIndex_);
    if (!preset || preset->isFactory) return;

    if (deleteDialogLabel_) {
        std::string labelText = "\"" + preset->name + "\"";
        deleteDialogLabel_->setText(labelText.c_str());
    }

    if (deleteDialogOverlay_) {
        deleteDialogOverlay_->setVisible(true);
        invalid();
    }
}

void PresetBrowserView::hideDeleteDialog() {
    if (deleteDialogOverlay_) {
        deleteDialogOverlay_->setVisible(false);
        invalid();
    }
}

void PresetBrowserView::onDeleteDialogConfirm() {
    if (selectedPresetIndex_ < 0 || !dataSource_ || !presetManager_) {
        hideDeleteDialog();
        return;
    }

    const PresetInfo* preset = dataSource_->getPresetAtRow(selectedPresetIndex_);
    if (preset && !preset->isFactory) {
        if (presetManager_->deletePreset(*preset)) {
            refreshPresetList();
            selectedPresetIndex_ = -1;
            updateButtonStates();
        }
    }

    hideDeleteDialog();
}

void PresetBrowserView::showConfirmOverwrite() {
    if (selectedPresetIndex_ < 0 || !dataSource_ || !presetManager_) return;

    const PresetInfo* preset = dataSource_->getPresetAtRow(selectedPresetIndex_);
    if (!preset || preset->isFactory) return;

    overwriteTargetIndex_ = selectedPresetIndex_;

    if (overwriteDialogLabel_) {
        std::string labelText = "\"" + preset->name + "\"";
        overwriteDialogLabel_->setText(labelText.c_str());
    }

    if (overwriteDialogOverlay_) {
        overwriteDialogOverlay_->setVisible(true);
        invalid();
    }
}

void PresetBrowserView::hideOverwriteDialog() {
    if (overwriteDialogOverlay_) {
        overwriteDialogOverlay_->setVisible(false);
        overwriteTargetIndex_ = -1;
        invalid();
    }
}

void PresetBrowserView::onOverwriteDialogConfirm() {
    if (overwriteTargetIndex_ < 0 || !dataSource_ || !presetManager_) {
        hideOverwriteDialog();
        return;
    }

    const PresetInfo* preset = dataSource_->getPresetAtRow(overwriteTargetIndex_);
    if (preset && !preset->isFactory) {
        if (presetManager_->overwritePreset(*preset)) {
            refreshPresetList();
        }
    }

    hideOverwriteDialog();
}

void PresetBrowserView::registerKeyboardHook() {
    if (keyboardHookRegistered_) return;

    auto *frame = getFrame();
    if (frame) {
        frame->registerKeyboardHook(this);
        keyboardHookRegistered_ = true;
    }
}

void PresetBrowserView::unregisterKeyboardHook() {
    if (!keyboardHookRegistered_) return;

    auto *frame = getFrame();
    if (frame) {
        frame->unregisterKeyboardHook(this);
    }
    keyboardHookRegistered_ = false;
}

// =============================================================================
// ITextEditListener
// =============================================================================

void PresetBrowserView::onTextEditPlatformControlTookFocus(VSTGUI::CTextEdit* textEdit) {
    if (textEdit != searchField_) return;

    isSearchFieldFocused_ = true;
    startSearchPolling();
}

void PresetBrowserView::onTextEditPlatformControlLostFocus(VSTGUI::CTextEdit* textEdit) {
    if (textEdit != searchField_) return;

    isSearchFieldFocused_ = false;
    stopSearchPolling();

    if (searchDebouncer_.hasPendingFilter()) {
        auto query = searchDebouncer_.consumePendingFilter();
        onSearchTextChanged(query);
    }
}

// =============================================================================
// Search Polling Timer
// =============================================================================

void PresetBrowserView::startSearchPolling() {
    if (searchPollTimer_) return;

    constexpr uint32_t kPollIntervalMs = 50;
    searchPollTimer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
        [this](VSTGUI::CVSTGUITimer* /*timer*/) {
            onSearchPollTimer();
        },
        kPollIntervalMs,
        true
    );
}

void PresetBrowserView::stopSearchPolling() {
    if (searchPollTimer_) {
        searchPollTimer_->stop();
        searchPollTimer_ = nullptr;
    }
    isSearchFieldFocused_ = false;
}

void PresetBrowserView::onSearchPollTimer() {
    if (searchDebouncer_.shouldApplyFilter(getSystemTimeMs())) {
        auto query = searchDebouncer_.consumePendingFilter();
        onSearchTextChanged(query);
    }
}

uint64_t PresetBrowserView::getSystemTimeMs() {
#ifdef _WIN32
    return static_cast<uint64_t>(GetTickCount64());
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000 + static_cast<uint64_t>(ts.tv_nsec) / 1000000;
#endif
}

std::string PresetBrowserView::tabIndexToSubcategory(int tabIndex) const {
    if (tabIndex <= 0 || static_cast<size_t>(tabIndex) >= tabLabels_.size()) {
        return ""; // "All"
    }
    return tabLabels_[static_cast<size_t>(tabIndex)];
}

} // namespace Krate::Plugins
