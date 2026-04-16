#pragma once

#include "ui/outline_button.h"
#include "preset/preset_info.h"
#include "preset/preset_manager.h"

#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfont.h"

#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Membrum::UI {

class InlinePresetBrowserView : public VSTGUI::CViewContainer {
public:
    using BrowseCallback = std::function<void()>;

    InlinePresetBrowserView(const VSTGUI::CRect& size,
                            Krate::Plugins::PresetManager* manager,
                            BrowseCallback onBrowse)
        : CViewContainer(size)
        , manager_(manager)
        , onBrowse_(std::move(onBrowse))
    {
        buildChildren();
    }

    void setCurrentPresetName(const std::string& name)
    {
        currentName_ = name;
        if (nameLabel_) {
            const std::string display = name.empty() ? kEmptyLabel : name;
            nameLabel_->setText(VSTGUI::UTF8String(display));
        }
    }

    [[nodiscard]] const std::string& currentPresetName() const noexcept
    {
        return currentName_;
    }

private:
    static constexpr const char* kEmptyLabel = "\xE2\x80\x94"; // em-dash

    void buildChildren()
    {
        const auto full = getViewSize();
        const VSTGUI::CCoord width = full.getWidth();
        const VSTGUI::CCoord height = full.getHeight();

        constexpr VSTGUI::CCoord kPadding = 6.0;
        constexpr VSTGUI::CCoord kButtonHeight = 22.0;
        constexpr VSTGUI::CCoord kButtonGap = 4.0;

        const VSTGUI::CCoord nameTop = kPadding;
        const VSTGUI::CCoord buttonTop = height - kPadding - kButtonHeight;
        const VSTGUI::CCoord nameBottom = buttonTop - kPadding;

        VSTGUI::CRect nameRect(kPadding, nameTop,
                               width - kPadding, nameBottom);
        nameLabel_ = new VSTGUI::CTextLabel(nameRect,
                                            VSTGUI::UTF8String(kEmptyLabel));
        nameLabel_->setTransparency(true);
        nameLabel_->setFontColor(VSTGUI::CColor(220, 220, 225));
        nameLabel_->setHoriAlign(VSTGUI::kCenterText);
        auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>(
            *VSTGUI::kNormalFont);
        nameLabel_->setFont(font);
        addView(nameLabel_);

        const VSTGUI::CCoord innerWidth = width - 2.0 * kPadding;
        const VSTGUI::CCoord totalGap = 2.0 * kButtonGap;
        const VSTGUI::CCoord prevWidth = 40.0;
        const VSTGUI::CCoord nextWidth = 40.0;
        const VSTGUI::CCoord browseWidth = innerWidth - totalGap
                                           - prevWidth - nextWidth;

        VSTGUI::CCoord cursorX = kPadding;

        const VSTGUI::CRect prevRect(cursorX, buttonTop,
                                     cursorX + prevWidth,
                                     buttonTop + kButtonHeight);
        prevBtn_ = new OutlineActionButton(prevRect, "<",
                                           [this] { cyclePreset(-1); });
        addView(prevBtn_);
        cursorX += prevWidth + kButtonGap;

        const VSTGUI::CRect nextRect(cursorX, buttonTop,
                                     cursorX + nextWidth,
                                     buttonTop + kButtonHeight);
        nextBtn_ = new OutlineActionButton(nextRect, ">",
                                           [this] { cyclePreset(+1); });
        addView(nextBtn_);
        cursorX += nextWidth + kButtonGap;

        const VSTGUI::CRect browseRect(cursorX, buttonTop,
                                       cursorX + browseWidth,
                                       buttonTop + kButtonHeight);
        browseBtn_ = new OutlineActionButton(browseRect, "Browse",
                                             [this] {
                                                 if (onBrowse_) onBrowse_();
                                             });
        addView(browseBtn_);
    }

    void cyclePreset(int direction)
    {
        if (manager_ == nullptr || direction == 0) return;

        auto presets = manager_->scanPresets();
        if (presets.empty()) return;

        const auto n = static_cast<int>(presets.size());
        int index = 0;
        if (!currentName_.empty()) {
            const auto it = std::find_if(presets.begin(), presets.end(),
                [this](const Krate::Plugins::PresetInfo& p) {
                    return p.name == currentName_;
                });
            if (it != presets.end()) {
                index = static_cast<int>(it - presets.begin());
                index = ((index + direction) % n + n) % n;
            } else {
                index = (direction > 0) ? 0 : (n - 1);
            }
        } else {
            index = (direction > 0) ? 0 : (n - 1);
        }

        manager_->loadPreset(presets[static_cast<std::size_t>(index)]);
        // The load provider is responsible for calling setCurrentPresetName()
        // once the state is applied. If the load failed, we leave the name as-is.
    }

    Krate::Plugins::PresetManager* manager_ = nullptr;
    BrowseCallback onBrowse_;
    VSTGUI::CTextLabel* nameLabel_ = nullptr;
    OutlineActionButton* prevBtn_ = nullptr;
    OutlineActionButton* nextBtn_ = nullptr;
    OutlineActionButton* browseBtn_ = nullptr;
    std::string currentName_;
};

} // namespace Membrum::UI
