#include "save_preset_dialog_view.h"
#include "../preset/preset_manager.h"
#include "../preset/preset_info.h"

#include "vstgui/lib/cfont.h"

namespace Krate::Plugins {


// =============================================================================
// Layout Constants
// =============================================================================
namespace {
    constexpr float kDialogWidth = 300.0f;
    constexpr float kDialogHeight = 140.0f;
    constexpr float kDialogHeightWithCategory = 170.0f;
    constexpr float kPadding = 16.0f;
    constexpr float kFieldHeight = 26.0f;
    constexpr float kButtonWidth = 80.0f;
    constexpr float kButtonHeight = 28.0f;
    constexpr float kButtonGap = 12.0f;
    constexpr float kCategoryRowY = 82.0f;
    constexpr float kCategoryLabelWidth = 65.0f;
}

// =============================================================================
// Construction / Destruction
// =============================================================================

SavePresetDialogView::SavePresetDialogView(const VSTGUI::CRect& size, PresetManager* presetManager,
                                           std::vector<std::string> categories)
    : CViewContainer(size)
    , presetManager_(presetManager)
    , categories_(std::move(categories))
{
    setBackgroundColor(VSTGUI::CColor(0, 0, 0, 180)); // Semi-transparent overlay
    setVisible(false);  // Start hidden, shown via open()
    createDialogViews();
}

SavePresetDialogView::~SavePresetDialogView() {
    unregisterKeyboardHook();
}

// =============================================================================
// Lifecycle
// =============================================================================

void SavePresetDialogView::open(const std::string& currentSubcategory) {
    currentSubcategory_ = currentSubcategory;
    isOpen_ = true;
    setVisible(true);

    registerKeyboardHook();

    // Reset name field and focus it
    if (nameField_) {
        nameField_->setText("New Preset");
    }

    // Pre-select category dropdown to match current subcategory
    if (categoryMenu_ && !categories_.empty()) {
        int32_t selectedIndex = 0;
        for (size_t i = 0; i < categories_.size(); ++i) {
            if (categories_[i] == currentSubcategory) {
                selectedIndex = static_cast<int32_t>(i);
                break;
            }
        }
        categoryMenu_->setValue(static_cast<float>(selectedIndex));
        categoryMenu_->invalid();
    }

    if (auto *frame = getFrame()) {
        frame->setFocusView(nameField_);
    }

    invalid();
}

void SavePresetDialogView::close() {
    unregisterKeyboardHook();
    isOpen_ = false;
    setVisible(false);
}

// =============================================================================
// Drawing
// =============================================================================

void SavePresetDialogView::draw(VSTGUI::CDrawContext* context) {
    CViewContainer::draw(context);
}

VSTGUI::CMouseEventResult SavePresetDialogView::onMouseDown(
    VSTGUI::CPoint& where,
    const VSTGUI::CButtonState& buttons
) {
    // Click outside dialog closes it
    if (dialogBox_) {
        auto dialogBounds = dialogBox_->getViewSize();
        if (!dialogBounds.pointInside(where)) {
            close();
            return VSTGUI::kMouseEventHandled;
        }
    }

    return CViewContainer::onMouseDown(where, buttons);
}

// =============================================================================
// IKeyboardHook
// =============================================================================

void SavePresetDialogView::onKeyboardEvent(VSTGUI::KeyboardEvent& event, VSTGUI::CFrame* /*frame*/) {
    if (!isOpen_) return;
    if (event.type != VSTGUI::EventType::KeyDown) return;

    if (event.virt == VSTGUI::VirtualKey::Escape) {
        close();
        event.consumed = true;
    } else if (event.virt == VSTGUI::VirtualKey::Return ||
               event.virt == VSTGUI::VirtualKey::Enter) {
        onSaveConfirm();
        event.consumed = true;
    }
}

// =============================================================================
// IControlListener
// =============================================================================

void SavePresetDialogView::valueChanged(VSTGUI::CControl* control) {
    if (!control) return;

    auto tag = control->getTag();
    switch (tag) {
        case kSavePresetDialogSaveTag:
            onSaveConfirm();
            break;
        case kSavePresetDialogCancelTag:
            close();
            break;
        default:
            break;
    }
}

// =============================================================================
// Dialog Creation
// =============================================================================

void SavePresetDialogView::createDialogViews() {
    auto viewSize = getViewSize();
    bool hasCategories = !categories_.empty();
    float dialogHeight = hasCategories ? kDialogHeightWithCategory : kDialogHeight;

    // Center the dialog
    auto dialogLeft = (viewSize.getWidth() - kDialogWidth) / 2.0f;
    auto dialogTop = (viewSize.getHeight() - dialogHeight) / 2.0f;

    auto dialogRect = VSTGUI::CRect(
        dialogLeft, dialogTop,
        dialogLeft + kDialogWidth, dialogTop + dialogHeight
    );

    dialogBox_ = new VSTGUI::CViewContainer(dialogRect);
    dialogBox_->setBackgroundColor(VSTGUI::CColor(45, 45, 50));
    addView(dialogBox_);

    // Title
    auto titleRect = VSTGUI::CRect(kPadding, kPadding, kDialogWidth - kPadding, kPadding + 24.0f);
    titleLabel_ = new VSTGUI::CTextLabel(titleRect, "Save Preset");
    titleLabel_->setFont(VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 14, VSTGUI::kBoldFace));
    titleLabel_->setFontColor(VSTGUI::CColor(255, 255, 255));
    titleLabel_->setBackColor(VSTGUI::CColor(0, 0, 0, 0));
    titleLabel_->setFrameColor(VSTGUI::CColor(0, 0, 0, 0));
    titleLabel_->setHoriAlign(VSTGUI::kLeftText);
    dialogBox_->addView(titleLabel_);

    // Name field
    auto fieldY = kPadding + 32.0f;
    auto fieldRect = VSTGUI::CRect(
        kPadding, fieldY,
        kDialogWidth - kPadding, fieldY + kFieldHeight
    );
    nameField_ = new VSTGUI::CTextEdit(fieldRect, this, kSavePresetDialogNameFieldTag, "New Preset");
    nameField_->setFont(VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 12));
    nameField_->setFontColor(VSTGUI::CColor(255, 255, 255));
    nameField_->setBackColor(VSTGUI::CColor(30, 30, 35));
    nameField_->setFrameColor(VSTGUI::CColor(80, 80, 85));
    nameField_->setTextInset(VSTGUI::CPoint(6, 0));
    dialogBox_->addView(nameField_);

    // Category dropdown (only when categories are provided)
    if (hasCategories) {
        auto labelRect = VSTGUI::CRect(
            kPadding, kCategoryRowY,
            kPadding + kCategoryLabelWidth, kCategoryRowY + kFieldHeight
        );
        categoryLabel_ = new VSTGUI::CTextLabel(labelRect, "Category:");
        categoryLabel_->setFont(VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 12));
        categoryLabel_->setFontColor(VSTGUI::CColor(200, 200, 200));
        categoryLabel_->setBackColor(VSTGUI::CColor(0, 0, 0, 0));
        categoryLabel_->setFrameColor(VSTGUI::CColor(0, 0, 0, 0));
        categoryLabel_->setHoriAlign(VSTGUI::kLeftText);
        dialogBox_->addView(categoryLabel_);

        auto menuRect = VSTGUI::CRect(
            kPadding + kCategoryLabelWidth, kCategoryRowY,
            kDialogWidth - kPadding, kCategoryRowY + kFieldHeight
        );
        categoryMenu_ = new VSTGUI::COptionMenu(menuRect, this, kSavePresetDialogCategoryTag);
        categoryMenu_->setFont(VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 12));
        categoryMenu_->setFontColor(VSTGUI::CColor(255, 255, 255));
        categoryMenu_->setBackColor(VSTGUI::CColor(30, 30, 35));
        categoryMenu_->setFrameColor(VSTGUI::CColor(80, 80, 85));

        for (const auto& cat : categories_) {
            categoryMenu_->addEntry(cat.c_str());
        }

        dialogBox_->addView(categoryMenu_);
    }

    // Buttons
    auto buttonY = dialogHeight - kPadding - kButtonHeight;
    auto buttonsWidth = kButtonWidth * 2 + kButtonGap;
    auto buttonsLeft = (kDialogWidth - buttonsWidth) / 2.0f;

    auto saveRect = VSTGUI::CRect(
        buttonsLeft, buttonY,
        buttonsLeft + kButtonWidth, buttonY + kButtonHeight
    );
    saveButton_ = new OutlineBrowserButton(saveRect, this, kSavePresetDialogSaveTag, "Save",
        VSTGUI::CColor(60, 120, 180));
    dialogBox_->addView(saveButton_);

    auto cancelRect = VSTGUI::CRect(
        buttonsLeft + kButtonWidth + kButtonGap, buttonY,
        buttonsLeft + kButtonWidth * 2 + kButtonGap, buttonY + kButtonHeight
    );
    cancelButton_ = new OutlineBrowserButton(cancelRect, this, kSavePresetDialogCancelTag, "Cancel");
    dialogBox_->addView(cancelButton_);
}

// =============================================================================
// Save Logic
// =============================================================================

void SavePresetDialogView::onSaveConfirm() {
    if (!presetManager_ || !nameField_) {
        close();
        return;
    }

    // Commit text from platform control
    if (auto *frame = getFrame()) {
        frame->setFocusView(nullptr);
    }

    // Get preset name
    const auto& text = nameField_->getText();
    std::string name = text.empty() ? "New Preset" : text.getString();

    // Trim whitespace
    size_t start = name.find_first_not_of(" \t");
    size_t end = name.find_last_not_of(" \t");
    if (start != std::string::npos && end != std::string::npos) {
        name = name.substr(start, end - start + 1);
    }

    if (name.empty()) {
        name = "New Preset";
    }

    // Read selected category from dropdown (if present)
    if (categoryMenu_ && !categories_.empty()) {
        auto selectedIndex = static_cast<size_t>(categoryMenu_->getValue());
        if (selectedIndex < categories_.size()) {
            currentSubcategory_ = categories_[selectedIndex];
        }
    }

    // Save via preset manager using string subcategory
    presetManager_->savePreset(name, currentSubcategory_, "");

    close();
}

// =============================================================================
// Keyboard Hook Registration
// =============================================================================

void SavePresetDialogView::registerKeyboardHook() {
    if (keyboardHookRegistered_) return;

    if (auto *frame = getFrame()) {
        frame->registerKeyboardHook(this);
        keyboardHookRegistered_ = true;
    }
}

void SavePresetDialogView::unregisterKeyboardHook() {
    if (!keyboardHookRegistered_) return;

    if (auto *frame = getFrame()) {
        frame->unregisterKeyboardHook(this);
    }
    keyboardHookRegistered_ = false;
}

} // namespace Krate::Plugins
