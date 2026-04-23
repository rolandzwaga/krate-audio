// ==============================================================================
// PadGridView -- Phase 6 implementation (Spec 141, US3)
// ==============================================================================

#include "ui/pad_grid_view.h"

#include "dsp/pad_glow_publisher.h"
#include "dsp/pad_config.h"
#include "dsp/pad_category.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cgradient.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cstring.h"

#include <algorithm>
#include <cstdio>

namespace Membrum::UI {

using VSTGUI::CCoord;

namespace {

// Audition velocity for shift-click / right-click (FR-013).
constexpr float kAuditionVelocity = 100.0f / 127.0f;

// 30 Hz poll timer period (ms).
constexpr std::uint32_t kGlowTimerMs = 33;

// Draw colours (chosen to match the wider Krate palette).
constexpr VSTGUI::CColor kBgColor       = { 18,  18,  22, 255 };
constexpr VSTGUI::CColor kCellColor     = { 34,  34,  42, 255 };
constexpr VSTGUI::CColor kCellBorder    = { 70,  70,  80, 255 };
constexpr VSTGUI::CColor kSelectedColor = {230, 200,  90, 255 };
constexpr VSTGUI::CColor kLabelColor    = {210, 210, 215, 255 };

} // anonymous namespace

// ------------------------------------------------------------------------------
// Text helpers (FR-011).
// ------------------------------------------------------------------------------
std::string chokeGroupIndicatorText(std::uint8_t chokeGroup)
{
    if (chokeGroup == 0)
        return {};
    char buf[8] = {};
    std::snprintf(buf, sizeof(buf), "CG%u", static_cast<unsigned>(chokeGroup));
    return std::string{buf};
}

std::string outputBusIndicatorText(std::uint8_t outputBus)
{
    if (outputBus == 0)
        return {};
    char buf[8] = {};
    std::snprintf(buf, sizeof(buf), "BUS%u", static_cast<unsigned>(outputBus));
    return std::string{buf};
}

// ------------------------------------------------------------------------------
// GM Percussion Key Map (MIDI 36..67). Short names kept <= 6 chars so they fit
// in a pad cell at typical 1x / 1.5x DPI. Source: General MIDI Level 1 spec.
// ------------------------------------------------------------------------------
std::string gmDrumNameForNote(int midiNote)
{
    static constexpr const char* kNames[] = {
        "Kick2",   // 36 Bass Drum 1 (kick)
        "Stick",   // 37 Side Stick
        "Snare",   // 38 Acoustic Snare
        "Clap",    // 39 Hand Clap
        "Snare2",  // 40 Electric Snare
        "LoTomF",  // 41 Low Floor Tom
        "HHClsd",  // 42 Closed Hi-Hat
        "HiTomF",  // 43 High Floor Tom
        "HHPed",   // 44 Pedal Hi-Hat
        "LoTom",   // 45 Low Tom
        "HHOpen",  // 46 Open Hi-Hat
        "LoMTom",  // 47 Low-Mid Tom
        "HiMTom",  // 48 Hi-Mid Tom
        "Crash",   // 49 Crash Cymbal 1
        "HiTom",   // 50 High Tom
        "Ride",    // 51 Ride Cymbal 1
        "China",   // 52 Chinese Cymbal
        "RideB",   // 53 Ride Bell
        "Tamb",    // 54 Tambourine
        "Splash",  // 55 Splash Cymbal
        "Cowbel",  // 56 Cowbell
        "Crash2",  // 57 Crash Cymbal 2
        "Vibra",   // 58 Vibraslap
        "Ride2",   // 59 Ride Cymbal 2
        "HiBong",  // 60 Hi Bongo
        "LoBong",  // 61 Low Bongo
        "HiCong",  // 62 Mute Hi Conga
        "OpCong",  // 63 Open Hi Conga
        "LoCong",  // 64 Low Conga
        "HiTim",   // 65 High Timbale
        "LoTim",   // 66 Low Timbale
        "HiAgo",   // 67 High Agogo
    };
    if (midiNote < 36 || midiNote > 67)
        return {};
    return std::string{ kNames[midiNote - 36] };
}

// ------------------------------------------------------------------------------
// Category glyph (single-character indicator derived from PadCategory).
// Uses the same classifier as the coupling matrix (FR-033) so the UI and DSP
// agree on category assignment.
// ------------------------------------------------------------------------------
std::string categoryGlyphForConfig(const PadConfig& cfg)
{
    switch (classifyPad(cfg))
    {
        case PadCategory::Kick:      return "K";
        case PadCategory::Snare:     return "S";
        case PadCategory::HatCymbal: return "H";
        case PadCategory::Tom:       return "T";
        case PadCategory::Perc:      return "P";
        case PadCategory::kCount:    break;
    }
    return {};
}

// ------------------------------------------------------------------------------
// PadGridView
// ------------------------------------------------------------------------------
PadGridView::PadGridView(const VSTGUI::CRect& size,
                         PadGlowPublisher*    glowPublisher,
                         PadMetaProvider      metaProvider) noexcept
    : CView(size)
    , glowPublisher_(glowPublisher)
    , metaProvider_(std::move(metaProvider))
{
    glowBuckets_.fill(0);
    // Note: the 30 Hz poll timer is lazily created when the view is attached
    // to a live frame. Constructing one here would require a platform
    // message loop, which unit tests do not provide.
}

PadGridView::~PadGridView()
{
    if (pollTimer_)
    {
        pollTimer_->stop();
        pollTimer_ = nullptr;
    }
}

void PadGridView::setSelectedPadIndex(int padIndex) noexcept
{
    if (padIndex < 0 || padIndex >= kNumPads)
        return;
    if (padIndex == selectedPad_)
        return;
    selectedPad_ = padIndex;
    invalid();
}

void PadGridView::notifyMetaChanged(int padIndex) noexcept
{
    // FR-065 / FR-066 (Phase 8): the meta-provider is already the source of
    // truth; we just invalidate so the next draw picks up the new indicator
    // text. padIndex is accepted for future per-cell dirty-rect optimisation
    // but not required for correctness today.
    if (padIndex < 0 || padIndex >= kNumPads)
        return;
    invalid();
}

PadGridView::CellGeom PadGridView::cellGeom() const noexcept
{
    const auto& r = getViewSize();
    CellGeom g;
    g.cellW = static_cast<float>(r.getWidth())  / static_cast<float>(kPadGridColumns);
    g.cellH = static_cast<float>(r.getHeight()) / static_cast<float>(kPadGridRows);
    return g;
}

int PadGridView::padIndexFromPoint(VSTGUI::CPoint p) const noexcept
{
    const auto& r = getViewSize();
    const double localX = p.x - static_cast<double>(r.left);
    const double localY = p.y - static_cast<double>(r.top);

    if (localX < 0.0 || localY < 0.0)
        return -1;
    if (localX >= r.getWidth() || localY >= r.getHeight())
        return -1;

    const auto g = cellGeom();
    if (g.cellW <= 0.0f || g.cellH <= 0.0f)
        return -1;

    int col = static_cast<int>(localX / static_cast<double>(g.cellW));
    int row = static_cast<int>(localY / static_cast<double>(g.cellH));
    col = std::clamp(col, 0, kPadGridColumns - 1);
    row = std::clamp(row, 0, kPadGridRows - 1);
    return padIndexFromCell(col, row);
}

int PadGridView::handleMouseDown(VSTGUI::CPoint localPoint,
                                 bool           isShift,
                                 bool           isRight) noexcept
{
    const int padIdx = padIndexFromPoint(localPoint);
    if (padIdx < 0)
        return -1;

    if (isShift || isRight)
    {
        // FR-013: audition without changing selection.
        if (auditionCallback_)
            auditionCallback_(padIdx, kAuditionVelocity);
    }
    else
    {
        // Regular click: select AND audition -- hearing the sound is the
        // primary affordance of the pad.
        if (selectCallback_)
            selectCallback_(padIdx);
        selectedPad_ = padIdx;
        invalid();
        if (auditionCallback_)
            auditionCallback_(padIdx, kAuditionVelocity);
    }
    return padIdx;
}

void PadGridView::pollGlow() noexcept
{
    if (!glowPublisher_)
        return;

    std::array<std::uint8_t, kNumPads> fresh{};
    glowPublisher_->snapshot(fresh);

    // Dirty-rect optimisation: only call invalid() when at least one cell
    // changed bucket.
    bool anyChanged = false;
    for (int i = 0; i < kNumPads; ++i)
    {
        if (fresh[static_cast<std::size_t>(i)]
            != glowBuckets_[static_cast<std::size_t>(i)])
        {
            anyChanged = true;
            break;
        }
    }
    glowBuckets_ = fresh;
    if (anyChanged)
        invalid();
}

bool PadGridView::attached(VSTGUI::CView* parent)
{
    const bool ok = CView::attached(parent);
    if (ok && glowPublisher_ && !pollTimer_)
    {
        pollTimer_ = VSTGUI::owned(new VSTGUI::CVSTGUITimer(
            [this](VSTGUI::CVSTGUITimer* /*t*/) { pollGlow(); },
            kGlowTimerMs,
            true /* fire immediately */));
    }
    return ok;
}

bool PadGridView::removed(VSTGUI::CView* parent)
{
    // SC-014: cancel the timer before the view is torn down so no tick
    // dereferences a dying view.
    if (pollTimer_)
    {
        pollTimer_->stop();
        pollTimer_ = nullptr;
    }
    return CView::removed(parent);
}

void PadGridView::onMouseEnterEvent(VSTGUI::MouseEnterEvent& event)
{
    if (auto* frame = getFrame())
        frame->setCursor(VSTGUI::kCursorHand);
    event.consumed = true;
}

void PadGridView::onMouseExitEvent(VSTGUI::MouseExitEvent& event)
{
    if (auto* frame = getFrame())
        frame->setCursor(VSTGUI::kCursorDefault);
    event.consumed = true;
}

void PadGridView::onMouseDownEvent(VSTGUI::MouseDownEvent& event)
{
    const bool isShift = event.modifiers.has(VSTGUI::ModifierKey::Shift);
    const bool isRight = (event.buttonState == VSTGUI::MouseButton::Right);
    const int pad = handleMouseDown(event.mousePosition, isShift, isRight);
    if (pad >= 0)
        event.consumed = true;
}

// ------------------------------------------------------------------------------
// draw() -- renders each cell with MIDI note label, optional indicators, glow.
// ------------------------------------------------------------------------------
void PadGridView::draw(VSTGUI::CDrawContext* ctx)
{
    if (!ctx)
        return;

    const auto& viewRect = getViewSize();
    ctx->setFillColor(kBgColor);
    ctx->drawRect(viewRect, VSTGUI::kDrawFilled);

    const auto geom = cellGeom();
    if (geom.cellW <= 0.0f || geom.cellH <= 0.0f)
        return;

    auto* font = VSTGUI::kNormalFont;
    ctx->setFont(font);

    for (int row = 0; row < kPadGridRows; ++row)
    {
        for (int col = 0; col < kPadGridColumns; ++col)
        {
            const int padIdx = padIndexFromCell(col, row);

            VSTGUI::CRect cell;
            cell.left   = viewRect.left + col * geom.cellW;
            cell.top    = viewRect.top  + row * geom.cellH;
            cell.right  = cell.left + geom.cellW;
            cell.bottom = cell.top  + geom.cellH;

            VSTGUI::CRect inset = cell;
            inset.inset(2.0, 2.0);

            constexpr double kCornerRadius = 4.0;

            // Cell background (rounded).
            if (auto path = VSTGUI::owned(ctx->createGraphicsPath()))
            {
                path->addRoundRect(inset, kCornerRadius);
                ctx->setFillColor(kCellColor);
                ctx->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathFilled);

                // Glow overlay -- radial "thwack" gradient: bright warm core at
                // the cell centre fading rapidly to transparent. Overall alpha
                // is modulated by the pad's current amplitude bucket so the
                // hotspot pulses in lockstep with the audio envelope.
                const float intensity =
                    glowIntensityFromBucket(glowBuckets_[static_cast<std::size_t>(padIdx)]);
                if (intensity > 0.0f)
                {
                    if (!glowGradient_)
                    {
                        VSTGUI::GradientColorStopMap stops;
                        stops.emplace(0.00, VSTGUI::CColor{255, 250, 220, 255});
                        stops.emplace(0.22, VSTGUI::CColor{255, 210, 130, 220});
                        stops.emplace(0.55, VSTGUI::CColor{255, 170,  70,  80});
                        stops.emplace(1.00, VSTGUI::CColor{255, 140,  40,   0});
                        glowGradient_ = VSTGUI::owned(VSTGUI::CGradient::create(stops));
                    }
                    if (glowGradient_)
                    {
                        const CCoord radius = std::min(inset.getWidth(),
                                                       inset.getHeight()) * 0.70;
                        const VSTGUI::CPoint center{
                            inset.left + inset.getWidth()  * 0.5,
                            inset.top  + inset.getHeight() * 0.5};
                        const float prevAlpha = ctx->getGlobalAlpha();
                        ctx->setGlobalAlpha(prevAlpha * intensity);
                        ctx->fillRadialGradient(path, *glowGradient_,
                                                center, radius);
                        ctx->setGlobalAlpha(prevAlpha);
                    }
                }

                // Selection border.
                if (padIdx == selectedPad_)
                {
                    ctx->setFrameColor(kSelectedColor);
                    ctx->setLineWidth(2.0);
                }
                else
                {
                    ctx->setFrameColor(kCellBorder);
                    ctx->setLineWidth(1.0);
                }
                ctx->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathStroked);
            }

            // MIDI-note label (top-left, smaller) + GM drum name (top-right).
            const int midi = midiNoteForPad(padIdx);
            char midiLabel[16] = {};
            std::snprintf(midiLabel, sizeof(midiLabel), "%d", midi);
            ctx->setFontColor(kLabelColor);

            VSTGUI::CRect topRow = inset;
            topRow.bottom = topRow.top + 14.0;

            VSTGUI::CRect midiRect = topRow;
            midiRect.right = midiRect.left + geom.cellW * 0.4f;
            midiRect.offset(1.0, 1.0);
            ctx->setFont(VSTGUI::kNormalFontVerySmall);
            ctx->drawString(midiLabel, midiRect, VSTGUI::kLeftText, true);

            const auto gmName = gmDrumNameForNote(midi);
            if (!gmName.empty())
            {
                VSTGUI::CRect gmRect = topRow;
                gmRect.left = gmRect.right - geom.cellW * 0.6f;
                gmRect.offset(-1.0, 1.0);
                ctx->setFont(font);
                ctx->drawString(gmName.c_str(), gmRect, VSTGUI::kRightText, true);
            }

            // Category glyph (centered). Single-char indicator derived from
            // the same PadCategory classifier used by the coupling matrix.
            if (const PadConfig* pcCat = padConfigFor(padIdx))
            {
                const auto glyph = categoryGlyphForConfig(*pcCat);
                if (!glyph.empty())
                {
                    VSTGUI::CRect glyphRect = inset;
                    glyphRect.top    = inset.top    + inset.getHeight() * 0.30;
                    glyphRect.bottom = inset.top    + inset.getHeight() * 0.70;
                    ctx->drawString(glyph.c_str(), glyphRect,
                                    VSTGUI::kCenterText, true);
                }
            }

            // Indicators (choke / bus)
            if (const PadConfig* pc = padConfigFor(padIdx))
            {
                const auto chokeText = chokeGroupIndicatorText(pc->chokeGroup);
                const auto busText   = outputBusIndicatorText(pc->outputBus);

                VSTGUI::CRect indRect = inset;
                indRect.top = indRect.bottom - 14.0;

                if (!chokeText.empty())
                {
                    VSTGUI::CRect left = indRect;
                    left.right = left.left + geom.cellW * 0.5f;
                    ctx->drawString(chokeText.c_str(), left,
                                    VSTGUI::kLeftText, true);
                }
                if (!busText.empty())
                {
                    VSTGUI::CRect right = indRect;
                    right.left = right.right - geom.cellW * 0.5f;
                    ctx->drawString(busText.c_str(), right,
                                    VSTGUI::kRightText, true);
                }
            }
        }
    }

    setDirty(false);
}

} // namespace Membrum::UI
