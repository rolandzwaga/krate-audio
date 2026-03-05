#pragma once

// ==============================================================================
// UpdateBannerView - Non-intrusive Update Notification Banner
// ==============================================================================
// VSTGUI custom view that polls UpdateChecker and shows a banner when a newer
// plugin version is available. Uses CVSTGUITimer for periodic polling.
//
// Banner layout: "PluginName vX.Y.Z is available  [Download]  [X]"
//
// Registered as "UpdateBanner" custom view (created via createCustomView).
// ==============================================================================

#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cgraphicspath.h"

#include "update/update_checker.h"

#include "public.sdk/source/common/openurl.h"

#include <functional>
#include <string>

namespace Krate::Plugins {

class UpdateBannerView : public VSTGUI::CView {
public:
    UpdateBannerView(const VSTGUI::CRect& size, UpdateChecker* checker)
        : CView(size)
        , checker_(checker)
    {
        setVisible(false);
        setTransparency(false);
    }

    ~UpdateBannerView() override {
        stopPolling();
    }

    /// Start polling for results. Call after view is attached to frame.
    void startPolling() {
        if (pollTimer_)
            return;

        pollTimer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
            [this](VSTGUI::CVSTGUITimer*) { onPollTimer(); },
            500 // 500ms interval
        );
    }

    /// Stop polling. Call before view is detached.
    void stopPolling() {
        pollTimer_ = nullptr;
    }

    // =========================================================================
    // CView overrides
    // =========================================================================

    void draw(VSTGUI::CDrawContext* context) override {
        if (!visible_ || message_.empty())
            return;

        auto r = getViewSize();
        constexpr VSTGUI::CCoord radius = 8.0;
        constexpr VSTGUI::CCoord inset = 4.0;

        // Banner rect: flat top (flush with edge), rounded bottom corners
        auto bannerRect = VSTGUI::CRect(
            r.left + inset, r.top,
            r.right - inset, r.bottom - inset);

        // Drop shadow
        auto shadowRect = bannerRect;
        shadowRect.offset(1, 2);
        if (auto* shadowPath = makeBottomRoundedPath(context, shadowRect, radius)) {
            context->setFillColor(VSTGUI::CColor(0, 0, 0, 100));
            context->drawGraphicsPath(shadowPath, VSTGUI::CDrawContext::kPathFilled);
            shadowPath->forget();
        }

        // Main background
        if (auto* bgPath = makeBottomRoundedPath(context, bannerRect, radius)) {
            context->setFillColor(VSTGUI::CColor(50, 50, 55, 255));
            context->drawGraphicsPath(bgPath, VSTGUI::CDrawContext::kPathFilled);
            context->setFrameColor(VSTGUI::CColor(80, 80, 90, 200));
            context->setLineWidth(1.0);
            context->drawGraphicsPath(bgPath, VSTGUI::CDrawContext::kPathStroked);
            bgPath->forget();
        }

        // Left accent bar
        auto accentRect = VSTGUI::CRect(
            bannerRect.left, bannerRect.top,
            bannerRect.left + 3, bannerRect.bottom - radius);
        context->setFillColor(VSTGUI::CColor(80, 160, 255, 220));
        context->drawRect(accentRect, VSTGUI::kDrawFilled);

        // Message text (vertically centered)
        auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 12);
        context->setFont(font);
        context->setFontColor(VSTGUI::CColor(220, 220, 220));

        auto textRect = bannerRect;
        textRect.left += 14;
        textRect.right -= 120;
        context->drawString(message_.c_str(), textRect, VSTGUI::kLeftText);

        // "Download" button (rounded, vertically centered)
        auto btnH = 24.0;
        auto btnY = bannerRect.top + (bannerRect.getHeight() - btnH) / 2.0;
        downloadRect_ = VSTGUI::CRect(
            bannerRect.right - 112, btnY,
            bannerRect.right - 30, btnY + btnH);
        if (auto* dlPath = context->createRoundRectGraphicsPath(downloadRect_, 4.0)) {
            context->setFillColor(downloadHover_
                ? VSTGUI::CColor(100, 180, 255, 255)
                : VSTGUI::CColor(80, 160, 255, 200));
            context->drawGraphicsPath(dlPath, VSTGUI::CDrawContext::kPathFilled);
            dlPath->forget();
        }
        context->setFontColor(VSTGUI::CColor(255, 255, 255));
        context->drawString("Download", downloadRect_, VSTGUI::kCenterText);

        // "X" dismiss button (vertically centered)
        dismissRect_ = VSTGUI::CRect(
            bannerRect.right - 24, btnY,
            bannerRect.right - 4, btnY + btnH);
        context->setFontColor(dismissHover_
            ? VSTGUI::CColor(255, 100, 100)
            : VSTGUI::CColor(160, 160, 160));
        context->drawString("X", dismissRect_, VSTGUI::kCenterText);
    }

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        [[maybe_unused]] const VSTGUI::CButtonState& buttons) override
    {
        if (!visible_)
            return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;

        if (downloadRect_.pointInside(where) && !downloadUrl_.empty()) {
            Steinberg::openURLInDefaultApplication(Steinberg::String(downloadUrl_.c_str()));
            return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;
        }

        if (dismissRect_.pointInside(where) && checker_) {
            checker_->dismissVersion(latestVersion_);
            hideBanner();
            return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;
        }

        return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;
    }

    VSTGUI::CMouseEventResult onMouseMoved(
        VSTGUI::CPoint& where,
        [[maybe_unused]] const VSTGUI::CButtonState& buttons) override
    {
        if (!visible_)
            return VSTGUI::kMouseEventNotHandled;

        bool newDownloadHover = downloadRect_.pointInside(where);
        bool newDismissHover = dismissRect_.pointInside(where);

        if (newDownloadHover != downloadHover_ || newDismissHover != dismissHover_) {
            downloadHover_ = newDownloadHover;
            dismissHover_ = newDismissHover;
            invalid();
        }

        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseExited(
        [[maybe_unused]] VSTGUI::CPoint& where,
        [[maybe_unused]] const VSTGUI::CButtonState& buttons) override
    {
        if (downloadHover_ || dismissHover_) {
            downloadHover_ = false;
            dismissHover_ = false;
            invalid();
        }
        return VSTGUI::kMouseEventHandled;
    }

private:
    /// Build a path with flat top edge and rounded bottom-left/bottom-right corners.
    static VSTGUI::CGraphicsPath* makeBottomRoundedPath(
        VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& rect, VSTGUI::CCoord r)
    {
        auto* path = ctx->createGraphicsPath();
        if (!path)
            return nullptr;

        // Start at top-left (sharp corner)
        path->beginSubpath(rect.left, rect.top);
        // Top edge → top-right (sharp corner)
        path->addLine(rect.right, rect.top);
        // Right edge → bottom-right arc
        path->addLine(rect.right, rect.bottom - r);
        path->addArc(
            VSTGUI::CRect(rect.right - 2 * r, rect.bottom - 2 * r, rect.right, rect.bottom),
            0, 90, true);
        // Bottom edge → bottom-left arc
        path->addLine(rect.left + r, rect.bottom);
        path->addArc(
            VSTGUI::CRect(rect.left, rect.bottom - 2 * r, rect.left + 2 * r, rect.bottom),
            90, 180, true);
        // Left edge back to top
        path->closeSubpath();
        return path;
    }

    void onPollTimer() {
        if (!checker_ || !checker_->hasResult())
            return;

        auto result = checker_->getResult();
        if (result.updateAvailable && !checker_->isVersionDismissed(result.latestVersion)) {
            showBanner(result);
        } else if (visible_) {
            hideBanner();
        }
    }

    void showBanner(const UpdateCheckResult& result) {
        latestVersion_ = result.latestVersion;
        downloadUrl_ = result.downloadUrl;
        message_ = "Version " + result.latestVersion + " is available";
        if (!result.releaseNotes.empty()) {
            message_ += " — " + result.releaseNotes;
        }
        visible_ = true;
        setVisible(true);
        invalid();
    }

    void hideBanner() {
        visible_ = false;
        setVisible(false);
        invalid();
    }

    UpdateChecker* checker_ = nullptr;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> pollTimer_;

    // Banner state
    bool visible_ = false;
    std::string message_;
    std::string latestVersion_;
    std::string downloadUrl_;

    // Button hit rects (computed during draw)
    VSTGUI::CRect downloadRect_;
    VSTGUI::CRect dismissRect_;
    bool downloadHover_ = false;
    bool dismissHover_ = false;
};

// ==============================================================================
// CheckForUpdatesButton - Manual "Check for Updates" trigger
// ==============================================================================
// Small text button placed in the plugin header/top bar area.
// Clicking triggers checkForUpdate(true) to force a check regardless of cooldown.
// Shows brief feedback: "Checking..." while in progress, "Up to date" if no update.
//
// Registered as "CheckForUpdatesButton" custom view (created via createCustomView).
// ==============================================================================

class CheckForUpdatesButton : public VSTGUI::CView {
public:
    CheckForUpdatesButton(const VSTGUI::CRect& size, UpdateChecker* checker)
        : CView(size)
        , checker_(checker)
    {}

    ~CheckForUpdatesButton() override {
        feedbackTimer_ = nullptr;
    }

    void draw(VSTGUI::CDrawContext* context) override {
        auto r = getViewSize();

        // Background on hover
        if (hover_) {
            context->setFillColor(VSTGUI::CColor(255, 255, 255, 20));
            context->drawRect(r, VSTGUI::kDrawFilled);
        }

        auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 10);
        context->setFont(font);

        if (checking_) {
            context->setFontColor(VSTGUI::CColor(180, 180, 180));
            context->drawString("Checking...", r, VSTGUI::kCenterText);
        } else if (showUpToDate_) {
            context->setFontColor(VSTGUI::CColor(100, 200, 100));
            context->drawString("Up to date", r, VSTGUI::kCenterText);
        } else {
            context->setFontColor(hover_
                ? VSTGUI::CColor(80, 160, 255)
                : VSTGUI::CColor(140, 140, 140));
            context->drawString("Check for Updates", r, VSTGUI::kCenterText);
        }
    }

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        [[maybe_unused]] const VSTGUI::CButtonState& buttons) override
    {
        if (!checker_ || checking_)
            return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;

        auto r = getViewSize();
        if (!r.pointInside(where))
            return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;

        // Force a check
        checker_->clearResult();
        checker_->checkForUpdate(true);
        checking_ = true;
        showUpToDate_ = false;
        invalid();

        // Poll for completion
        feedbackTimer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
            [this](VSTGUI::CVSTGUITimer*) {
                if (!checker_) {
                    feedbackTimer_ = nullptr;
                    return;
                }
                if (checker_->hasResult()) {
                    checking_ = false;
                    auto result = checker_->getResult();
                    if (!result.updateAvailable) {
                        // Show "Up to date" for 3 seconds
                        showUpToDate_ = true;
                        upToDateTimer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
                            [this](VSTGUI::CVSTGUITimer*) {
                                showUpToDate_ = false;
                                upToDateTimer_ = nullptr;
                                invalid();
                            },
                            3000, // 3 seconds
                            false // one-shot
                        );
                    }
                    feedbackTimer_ = nullptr;
                    invalid();
                }
            },
            200 // 200ms poll interval
        );

        return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;
    }

    VSTGUI::CMouseEventResult onMouseMoved(
        VSTGUI::CPoint& where,
        [[maybe_unused]] const VSTGUI::CButtonState& buttons) override
    {
        auto r = getViewSize();
        bool newHover = r.pointInside(where);
        if (newHover != hover_) {
            hover_ = newHover;
            invalid();
        }
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseExited(
        [[maybe_unused]] VSTGUI::CPoint& where,
        [[maybe_unused]] const VSTGUI::CButtonState& buttons) override
    {
        if (hover_) {
            hover_ = false;
            invalid();
        }
        return VSTGUI::kMouseEventHandled;
    }

private:
    UpdateChecker* checker_ = nullptr;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> feedbackTimer_;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> upToDateTimer_;
    bool hover_ = false;
    bool checking_ = false;
    bool showUpToDate_ = false;
};

// ==============================================================================
// VersionLabel - Dynamic version display
// ==============================================================================
// Displays "vX.Y.Z" using the version string passed at construction.
// Created as "VersionLabel" custom view — each plugin passes its VERSION_STR.
// ==============================================================================

class VersionLabel : public VSTGUI::CView {
public:
    VersionLabel(const VSTGUI::CRect& size, std::string version)
        : CView(size)
        , text_("v" + std::move(version))
    {}

    void draw(VSTGUI::CDrawContext* context) override {
        auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 10);
        context->setFont(font);
        context->setFontColor(VSTGUI::CColor(140, 140, 140));
        context->drawString(text_.c_str(), getViewSize(), VSTGUI::kRightText);
    }

private:
    std::string text_;
};

} // namespace Krate::Plugins
