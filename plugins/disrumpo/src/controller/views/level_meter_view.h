#pragma once

// ==============================================================================
// LevelMeterView: input, output or band level meter (VSTGUI primitives only)
// ==============================================================================
// Input / Output: a vertical stereo meter (L and R bars, one clip LED above the
// pair). Band 0..3: a horizontal mono bar with a clip dot at its right end.
// Scale -60 ... +6 dBFS. The controller's editor timer feeds every meter view the
// processor's latest window (pushWindow) and advances it (advance); each view owns
// its ballistics (MeterBallistics). A latched clip clears on a click.
// ==============================================================================

#include "controller/meter_ballistics.h"
#include "meter_window.h"

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cview.h"

#include <array>
#include <cstdint>

namespace Disrumpo {

class LevelMeterView : public VSTGUI::CView {
public:
    enum class Source : std::uint8_t { Input, Output, Band0, Band1, Band2, Band3 };

    /// Colours from the uidesc colour table
    struct Colors {
        VSTGUI::CColor track;  ///< empty bar and unlit clip LED
        VSTGUI::CColor low;    ///< below -18 dBFS (stereo meters)
        VSTGUI::CColor mid;    ///< -18 ... 0 dBFS (stereo meters)
        VSTGUI::CColor clip;   ///< at or above 0 dBFS, and a latched clip LED
        VSTGUI::CColor band;   ///< band meter fill
    };

    static constexpr float kScaleMinDb = -60.0f;
    static constexpr float kScaleMaxDb = 6.0f;
    static constexpr float kMidDb = -18.0f;
    static constexpr float kClipDb = 0.0f;

    LevelMeterView(const VSTGUI::CRect& size, Source source, Colors colors);

    [[nodiscard]] Source source() const noexcept { return source_; }
    [[nodiscard]] bool isStereo() const noexcept { return source_ == Source::Input || source_ == Source::Output; }

    /// This meter's values from one processor window (UI thread)
    void pushWindow(const MeterWindow& window, double sampleRate) noexcept;
    /// Advances the ballistics by @p dtSeconds; repaints when the display moved
    void advance(double dtSeconds);
    /// Clears the latched clip of every channel
    void clearClip();

    [[nodiscard]] const MeterBallistics& channel(int c) const noexcept { return meters_[static_cast<size_t>(c)]; }
    [[nodiscard]] bool clipLatched() const noexcept;

    void draw(VSTGUI::CDrawContext* context) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& event) override;

private:
    void drawStereo(VSTGUI::CDrawContext* context) const;
    void drawBand(VSTGUI::CDrawContext* context) const;

    Source source_;
    Colors colors_;
    std::array<MeterBallistics, 2> meters_{};
    std::array<float, 2> drawnPeakDb_{MeterBallistics::kFloorDb, MeterBallistics::kFloorDb};
    std::array<float, 2> drawnRmsDb_{MeterBallistics::kFloorDb, MeterBallistics::kFloorDb};
    bool drawnClip_ = false;
};

}  // namespace Disrumpo
