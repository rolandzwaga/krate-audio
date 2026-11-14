// ==============================================================================
// LevelMeterView implementation
// ==============================================================================

#include "controller/views/level_meter_view.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Disrumpo {

namespace {

constexpr double kBarWidth = 6.0;     // stereo meter: each bar
constexpr double kLedWidth = 6.0;     // clip LED / dot
constexpr double kLedHeight = 4.0;
constexpr double kLedGap = 2.0;       // between the LED and the bars, the bar and the dot
constexpr float kRedrawStepDb = 0.1f;

/// Position of @p db on the -60 ... +6 dB scale, 0 at the bottom (left), 1 at the top (right)
double scalePos(float db) {
    const float clamped = std::clamp(db, LevelMeterView::kScaleMinDb, LevelMeterView::kScaleMaxDb);
    return static_cast<double>((clamped - LevelMeterView::kScaleMinDb) /
                               (LevelMeterView::kScaleMaxDb - LevelMeterView::kScaleMinDb));
}

}  // namespace

LevelMeterView::LevelMeterView(const VSTGUI::CRect& size, Source source, Colors colors)
    : CView(size), source_(source), colors_(std::move(colors)) {}

void LevelMeterView::pushWindow(const MeterWindow& window, double sampleRate) noexcept {
    ++windowsReceived_;
    const uint32_t n = window.numSamples;
    const uint32_t clip = window.clipFlags;
    switch (source_) {
        case Source::Input:
            for (int c = 0; c < 2; ++c)
                meters_[static_cast<size_t>(c)].addWindow(window.inPeak[c], window.inMeanSq[c], n,
                                                          (clip & (kMeterClipInL << c)) != 0u, sampleRate);
            break;
        case Source::Output:
            for (int c = 0; c < 2; ++c)
                meters_[static_cast<size_t>(c)].addWindow(window.outPeak[c], window.outMeanSq[c], n,
                                                          (clip & (kMeterClipOutL << c)) != 0u, sampleRate);
            break;
        default: {
            const int b = static_cast<int>(source_) - static_cast<int>(Source::Band0);
            meters_[0].addWindow(window.bandPeak[b], 0.0f, n, (clip & (kMeterClipBand0 << b)) != 0u, sampleRate);
            break;
        }
    }
}

void LevelMeterView::advance(double dtSeconds) {
    bool moved = clipLatched() != drawnClip_;
    for (size_t c = 0; c < meters_.size(); ++c) {
        meters_[c].tick(dtSeconds);
        moved = moved || std::fabs(meters_[c].peakDb() - drawnPeakDb_[c]) >= kRedrawStepDb ||
                std::fabs(meters_[c].rmsDb() - drawnRmsDb_[c]) >= kRedrawStepDb;
    }
    if (moved) {
        for (size_t c = 0; c < meters_.size(); ++c) {
            drawnPeakDb_[c] = meters_[c].peakDb();
            drawnRmsDb_[c] = meters_[c].rmsDb();
        }
        drawnClip_ = clipLatched();
        invalid();
    }
}

void LevelMeterView::clearClip() {
    for (auto& m : meters_) m.clearClip();
    drawnClip_ = false;
    invalid();
}

bool LevelMeterView::clipLatched() const noexcept {
    return meters_[0].clipLatched() || (isStereo() && meters_[1].clipLatched());
}

void LevelMeterView::onMouseDownEvent(VSTGUI::MouseDownEvent& event) {
    if (!event.buttonState.isLeft()) return;
    clearClip();
    event.consumed = true;
}

void LevelMeterView::draw(VSTGUI::CDrawContext* context) {
    if (context == nullptr) return;
    context->setDrawMode(VSTGUI::kAliasing);
    if (isStereo())
        drawStereo(context);
    else
        drawBand(context);
    setDirty(false);
}

void LevelMeterView::drawStereo(VSTGUI::CDrawContext* context) const {
    const VSTGUI::CRect& r = getViewSize();

    // Clip LED, centred above the pair
    const double ledLeft = r.left + std::floor((r.getWidth() - kLedWidth) * 0.5);
    context->setFillColor(clipLatched() ? colors_.clip : colors_.track);
    context->drawRect(VSTGUI::CRect(ledLeft, r.top, ledLeft + kLedWidth, r.top + kLedHeight), VSTGUI::kDrawFilled);

    const double barTop = r.top + kLedHeight + kLedGap;
    const double barHeight = r.bottom - barTop;
    const auto yOf = [&](float db) { return r.bottom - (scalePos(db) * barHeight); };
    const auto zoneColor = [&](float db) {
        if (db >= kClipDb) return colors_.clip;
        return db >= kMidDb ? colors_.mid : colors_.low;
    };

    for (size_t c = 0; c < 2; ++c) {
        const double left = (c == 0) ? r.left : r.right - kBarWidth;
        const double right = left + kBarWidth;
        context->setFillColor(colors_.track);
        context->drawRect(VSTGUI::CRect(left, barTop, right, r.bottom), VSTGUI::kDrawFilled);

        // RMS bar, coloured by zone: low below -18, mid to 0, clip above
        const float rms = meters_[c].rmsDb();
        if (rms > kScaleMinDb) {
            const double top = yOf(rms);
            const double midY = yOf(kMidDb);
            const double clipY = yOf(kClipDb);
            const auto band = [&](double from, double to, const VSTGUI::CColor& col) {
                const double t = std::max(from, top);
                if (t < to) {
                    context->setFillColor(col);
                    context->drawRect(VSTGUI::CRect(left, t, right, to), VSTGUI::kDrawFilled);
                }
            };
            band(midY, r.bottom, colors_.low);
            band(clipY, midY, colors_.mid);
            band(barTop, clipY, colors_.clip);
        }

        // Peak line (held, then falling)
        const float peak = meters_[c].peakDb();
        if (peak > kScaleMinDb) {
            const double y = std::clamp(std::floor(yOf(peak)), barTop, r.bottom - 1.0);
            context->setFillColor(zoneColor(peak));
            context->drawRect(VSTGUI::CRect(left, y, right, y + 1.0), VSTGUI::kDrawFilled);
        }
    }
}

void LevelMeterView::drawBand(VSTGUI::CDrawContext* context) const {
    const VSTGUI::CRect& r = getViewSize();
    const double barRight = r.right - kLedWidth - kLedGap;

    context->setFillColor(colors_.track);
    context->drawRect(VSTGUI::CRect(r.left, r.top, barRight, r.bottom), VSTGUI::kDrawFilled);

    // The band's peak (held, then falling) as the bar
    const float peak = meters_[0].peakDb();
    if (peak > kScaleMinDb) {
        const double x = r.left + (scalePos(peak) * (barRight - r.left));
        context->setFillColor(colors_.band);
        context->drawRect(VSTGUI::CRect(r.left, r.top, x, r.bottom), VSTGUI::kDrawFilled);
    }

    // Clip dot at the right end
    context->setFillColor(clipLatched() ? colors_.clip : colors_.track);
    context->drawRect(VSTGUI::CRect(r.right - kLedWidth, r.top, r.right, r.bottom), VSTGUI::kDrawFilled);
}

}  // namespace Disrumpo
