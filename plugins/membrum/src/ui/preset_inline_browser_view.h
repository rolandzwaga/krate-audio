#pragma once

#include "ui/membrum_buttons.h"
#include "preset/preset_info.h"
#include "preset/preset_manager.h"

#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfont.h"

#include <algorithm>
#include <filesystem>
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

    /// Names the current preset without a path: Prev / Next then match by name.
    void setCurrentPresetName(const std::string& name)
    {
        setCurrentPreset(name, {});
    }

    /// MB-103: the current preset by name (the label) and path (the identity
    /// Prev / Next cycle from, since a user and a factory preset can share a name).
    void setCurrentPreset(const std::string& name, const std::filesystem::path& path)
    {
        currentName_ = name;
        currentPath_ = path;
        if (nameLabel_) {
            const std::string display = name.empty() ? kEmptyLabel : name;
            nameLabel_->setText(VSTGUI::UTF8String(display));
        }
    }

    [[nodiscard]] const std::string& currentPresetName() const noexcept
    {
        return currentName_;
    }

    [[nodiscard]] const std::filesystem::path& currentPresetPath() const noexcept
    {
        return currentPath_;
    }

    /// The index Prev (-1) / Next (+1) loads. The current preset is found by
    /// path when `currentPath` is not empty, by name otherwise; with no match
    /// (or nothing current) Next starts at 0 and Prev at the last preset.
    [[nodiscard]] static int cycleIndex(const std::vector<Krate::Plugins::PresetInfo>& presets,
                                        const std::filesystem::path& currentPath,
                                        const std::string& currentName,
                                        int direction) noexcept
    {
        const auto n = static_cast<int>(presets.size());
        if (n == 0) return 0;
        const int edge = (direction > 0) ? 0 : (n - 1);
        if (currentPath.empty() && currentName.empty()) return edge;

        const auto wanted = currentPath.lexically_normal();
        const auto it = std::find_if(presets.begin(), presets.end(),
            [&](const Krate::Plugins::PresetInfo& p) {
                return currentPath.empty() ? p.name == currentName
                                           : p.path.lexically_normal() == wanted;
            });
        if (it == presets.end()) return edge;
        const int index = static_cast<int>(it - presets.begin());
        return ((index + direction) % n + n) % n;
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

        const int index = cycleIndex(presets, currentPath_, currentName_, direction);

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
    std::filesystem::path currentPath_;
};

} // namespace Membrum::UI
