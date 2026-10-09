// ==============================================================================
// Vorago Phase 13 - EcosystemView implementation (spec C-6, FR-050, FR-051)
// ==============================================================================
// Spec:  specs/vorago-phase13-ui/spec.md  (C-6)
// Plan:  specs/vorago-phase13-ui/plan.md  (sections 6.2, 6.3)
// ==============================================================================

#include "ui/ecosystem_view.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cdrawdefs.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "vstgui/uidescription/uidescription.h"

#include <algorithm>
#include <cmath>

namespace Vorago::UI {

namespace {

// Fixed fallbacks, in palette order: eco-partial, eco-resonator, eco-noise,
// eco-feedback, eco-ghost, eco-link, eco-grid, text-dim.
constexpr std::array<const char*, 8> kPaletteNames{
    "eco-partial", "eco-resonator", "eco-noise", "eco-feedback",
    "eco-ghost",   "eco-link",      "eco-grid",  "text-dim"};
const std::array<VSTGUI::CColor, 8> kFallbackPalette{
    VSTGUI::CColor{150, 190, 255, 255},  // eco-partial
    VSTGUI::CColor{120, 230, 200, 255},  // eco-resonator
    VSTGUI::CColor{200, 200, 210, 255},  // eco-noise
    VSTGUI::CColor{255, 150, 90, 255},   // eco-feedback
    VSTGUI::CColor{190, 140, 255, 255},  // eco-ghost
    VSTGUI::CColor{170, 180, 220, 255},  // eco-link
    VSTGUI::CColor{50, 54, 66, 255},     // eco-grid
    VSTGUI::CColor{120, 124, 136, 255},  // text-dim
};
const VSTGUI::CColor kFallbackBackground{10, 11, 16, 255};

constexpr std::uint8_t kLinkColour = 5;
constexpr std::uint8_t kGridColour = 6;
constexpr std::uint8_t kTextColour = 7;
constexpr std::uint8_t kMaxKindColour = 4;
constexpr float kGridWidth = 1.0f;
constexpr float kOutlineWidth = 1.0f;
constexpr double kMinPieceLength = 1e-9;  // unit coordinates

/// Short-way torus delta. Mirrors EcosystemEngine::wrapDelta
/// (ecosystem_engine.h:1111-1119, private) - kept local so this UI TU never
/// includes the Layer 3 engine header (plan section 0 item 7).
double td(double d) noexcept {
    if (d > 0.5) {
        return d - 1.0;
    }
    if (d < -0.5) {
        return d + 1.0;
    }
    return d;
}

/// Which unit-square edge an end coordinate crossed: +1 past 1, -1 below 0.
int wrapSign(double e) noexcept {
    if (e >= 1.0) {
        return 1;
    }
    if (e < 0.0) {
        return -1;
    }
    return 0;
}

/// Liang-Barsky clip of p0 -> p1 to [0,1]^2. Returns false when nothing is left.
bool clipUnit(double& x0, double& y0, double& x1, double& y1) noexcept {
    const double dx = x1 - x0;
    const double dy = y1 - y0;
    double t0 = 0.0;
    double t1 = 1.0;
    const auto edge = [&](double p, double q) noexcept {
        if (p == 0.0) {
            return q >= 0.0;
        }
        const double t = q / p;
        if (p < 0.0) {
            if (t > t1) {
                return false;
            }
            t0 = std::max(t0, t);
        } else {
            if (t < t0) {
                return false;
            }
            t1 = std::min(t1, t);
        }
        return true;
    };
    if (!edge(-dx, x0) || !edge(dx, 1.0 - x0) || !edge(-dy, y0) || !edge(dy, 1.0 - y0)) {
        return false;
    }
    const double sx = x0;
    const double sy = y0;
    x0 = std::clamp(sx + (t0 * dx), 0.0, 1.0);
    y0 = std::clamp(sy + (t0 * dy), 0.0, 1.0);
    x1 = std::clamp(sx + (t1 * dx), 0.0, 1.0);
    y1 = std::clamp(sy + (t1 * dy), 0.0, 1.0);
    return true;
}

VSTGUI::CPoint mapUnit(double x, double y, const VSTGUI::CRect& r) noexcept {
    return {r.left + (x * r.getWidth()), r.top + (y * r.getHeight())};
}

std::size_t effectiveAgents(const EcosystemFrame& f) noexcept {
    return std::min<std::size_t>(f.agentCount, kMaxFrameAgents);
}

VSTGUI::CColor withAlpha(VSTGUI::CColor c, float alpha) noexcept {
    const float a = std::clamp(alpha, 0.0f, 1.0f) * static_cast<float>(c.alpha);
    c.alpha = static_cast<std::uint8_t>(a + 0.5f);
    return c;
}

}  // namespace

// ==============================================================================
// Construction / lifecycle (C-6 clause 5)
// ==============================================================================

EcosystemView::EcosystemView(const VSTGUI::CRect& size, const EcosystemFrame* frame)
    : VSTGUI::CView(size),
      frame_(frame),
      drawList_(std::make_unique<DrawList>()),
      palette_(kFallbackPalette),
      background_(kFallbackBackground) {}

EcosystemView::~EcosystemView() {
    if (timer_) {
        timer_->stop();
        timer_ = nullptr;
    }
}

bool EcosystemView::attached(VSTGUI::CView* parent) {
    const bool ok = VSTGUI::CView::attached(parent);
    if (ok && !timer_) {
        lastTick_ = std::chrono::steady_clock::now();
        timer_ = VSTGUI::owned(new VSTGUI::CVSTGUITimer(
            [this](VSTGUI::CVSTGUITimer* /*t*/) { tick(); }, kTimerMs, true));
    }
    return ok;
}

bool EcosystemView::removed(VSTGUI::CView* parent) {
    // Cancel BEFORE teardown so no tick reads the controller's frame afterwards.
    if (timer_) {
        timer_->stop();
        timer_ = nullptr;
    }
    return VSTGUI::CView::removed(parent);
}

void EcosystemView::tick() {
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - lastTick_).count();
    lastTick_ = now;
    (void)onTimerForTest(dt);
}

float EcosystemView::onTimerForTest(float dtSeconds) {
    if (frame_ == nullptr) {
        return 0.0f;
    }
    const EcosystemFrame& f = *frame_;
    const bool clear =
        haveSeen_ && (f.focusVoice != lastFocus_ || f.agentCount != lastAgentCount_);
    const float maxA = stepLinkFade(fade_, f, clear, dtSeconds);
    if (!haveSeen_ || needsRedraw(lastSeq_, f.sequence, maxA)) {
        invalid();
    }
    lastSeq_ = f.sequence;
    lastFocus_ = f.focusVoice;
    lastAgentCount_ = f.agentCount;
    haveSeen_ = true;
    return maxA;
}

// ==============================================================================
// The math (plan section 6.2, normative)
// ==============================================================================

VSTGUI::CPoint EcosystemView::mapToView(float x, float y, const VSTGUI::CRect& r) noexcept {
    return mapUnit(static_cast<double>(x), static_cast<double>(y), r);
}

EcosystemView::Style EcosystemView::agentStyle(float glow, bool dormant) noexcept {
    // `!(glow > 0)` also sends a NaN to 0 without naming a predicate.
    const float g = !(glow > 0.0f) ? 0.0f : std::min(glow, 1.0f);
    const float alpha = kAgentOutlineMinAlpha + ((1.0f - kAgentOutlineMinAlpha) * g);
    const float radius = kAgentMinRadius + ((kAgentMaxRadius - kAgentMinRadius) * std::sqrt(g));
    return Style{.filled = !dormant && g > 0.0f, .alpha = alpha, .radius = radius};
}

float EcosystemView::linkAlpha(float strength, float flowScale) noexcept {
    return (strength > 0.0f && flowScale > 0.0f) ? strength / (strength + flowScale) : 0.0f;
}

float EcosystemView::habitatBrightness(float voiceLevel) noexcept {
    // R-3: dB-normalized, floor at -60 dB (1e-3). The explicit floor test is the
    // clamp's own lower branch written first, so 1e-3 is exactly 0 regardless of
    // log10 rounding, and a NaN level draws dark.
    if (!(voiceLevel > 1e-3f)) {
        return 0.0f;
    }
    if (voiceLevel >= 1.0f) {
        return 1.0f;
    }
    const float b = ((20.0f * std::log10(voiceLevel)) + 60.0f) / 60.0f;
    return std::clamp(b, 0.0f, 1.0f);
}

// NOLINTNEXTLINE(bugprone-exception-escape) -- <= 3 digits fit every SSO buffer: no allocation, no throw
std::string EcosystemView::activeVoicesText(std::uint8_t activeVoices) noexcept {
    return std::to_string(activeVoices);  // <= 3 digits: inside every SSO buffer
}

bool EcosystemView::needsRedraw(std::uint32_t prevSeq, std::uint32_t seq,
                                float maxFadeAlpha) noexcept {
    return seq != prevSeq || maxFadeAlpha > 0.0f;
}

bool EcosystemView::isDrawableLink(const EcosystemFrame& f, std::size_t l) noexcept {
    // Counts are also bounded by the array sizes, so a corrupt count can never
    // index past the frame's arrays (FR-051).
    const std::size_t links = std::min<std::size_t>(f.linkCount, kMaxFrameLinks);
    if (l >= links) {
        return false;
    }
    const std::size_t agents = effectiveAgents(f);
    return f.linkA[l] < agents && f.linkB[l] < agents && f.linkA[l] != f.linkB[l];
}

std::array<EcosystemView::Segment, 6> EcosystemView::emptyHabitatGridLines(
    const VSTGUI::CRect& r) noexcept {
    std::array<Segment, 6> lines{};
    const double w = r.getWidth();
    const double h = r.getHeight();
    for (std::size_t k = 1; k <= 3; ++k) {
        const double x = r.left + (static_cast<double>(k) * w / 4.0);
        const double y = r.top + (static_cast<double>(k) * h / 4.0);
        lines[k - 1] = Segment{.a = VSTGUI::CPoint(x, r.top), .b = VSTGUI::CPoint(x, r.bottom)};
        lines[k + 2] = Segment{.a = VSTGUI::CPoint(r.left, y), .b = VSTGUI::CPoint(r.right, y)};
    }
    return lines;
}

EcosystemView::Segments EcosystemView::torusLinkSegments(float ax, float ay, float bx, float by,
                                                         const VSTGUI::CRect& r) noexcept {
    const double pax = static_cast<double>(ax);
    const double pay = static_cast<double>(ay);
    const double dx = td(static_cast<double>(bx) - pax);
    const double dy = td(static_cast<double>(by) - pay);
    const double ex = pax + dx;
    const double ey = pay + dy;
    const int sx = wrapSign(ex);
    const int sy = wrapSign(ey);
    const std::array<int, 2> txs{0, -sx};
    const std::array<int, 2> tys{0, -sy};
    const std::size_t nx = sx == 0 ? 1 : 2;
    const std::size_t ny = sy == 0 ? 1 : 2;

    Segments out{};
    for (std::size_t ix = 0; ix < nx; ++ix) {
        for (std::size_t iy = 0; iy < ny; ++iy) {
            const double tx = static_cast<double>(txs[ix]);
            const double ty = static_cast<double>(tys[iy]);
            double x0 = pax + tx;
            double y0 = pay + ty;
            double x1 = ex + tx;
            double y1 = ey + ty;
            if (!clipUnit(x0, y0, x1, y1)) {
                continue;
            }
            const double lx = x1 - x0;
            const double ly = y1 - y0;
            if (std::sqrt((lx * lx) + (ly * ly)) <= kMinPieceLength) {
                continue;  // a corner wrap's degenerate middle piece
            }
            out.pieces[out.count++] = Segment{.a = mapUnit(x0, y0, r), .b = mapUnit(x1, y1, r)};
        }
    }
    return out;
}

float EcosystemView::stepLinkFade(FadeTable& table, const EcosystemFrame& f, bool clear,
                                  float dtSeconds) noexcept {
    if (clear) {
        table.fill(0.0f);
    }
    // `!(dt > 0)` also maps a NaN dt to 0.
    const float dt = !(dtSeconds > 0.0f) ? 0.0f : std::min(dtSeconds, kMaxTickSeconds);
    const float decay = std::exp(-dt / kLinkFadeSeconds);

    // Decay before raise (SC-015): a present link never ends below its linkAlpha.
    for (std::size_t i = 0; i < kMaxFrameAgents; ++i) {
        for (std::size_t j = i + 1; j < kMaxFrameAgents; ++j) {
            float& e = table[fadeIndex(i, j)];
            e *= decay;
            if (e < kFadeFloor) {
                e = 0.0f;
            }
        }
    }

    const std::size_t links = std::min<std::size_t>(f.linkCount, kMaxFrameLinks);
    for (std::size_t l = 0; l < links; ++l) {
        if (!isDrawableLink(f, l)) {
            continue;
        }
        const std::size_t i = std::min(f.linkA[l], f.linkB[l]);
        const std::size_t j = std::max(f.linkA[l], f.linkB[l]);
        float& e = table[fadeIndex(i, j)];
        e = std::max(e, linkAlpha(f.linkStrength[l], f.linkFlowScale));
    }

    float maxA = 0.0f;
    for (std::size_t i = 0; i < kMaxFrameAgents; ++i) {
        for (std::size_t j = i + 1; j < kMaxFrameAgents; ++j) {
            maxA = std::max(maxA, table[fadeIndex(i, j)]);
        }
    }
    return maxA;
}

// ==============================================================================
// Draw list (FR-051, plan section 6.3) - pure, CFrame-free
// ==============================================================================

void EcosystemView::buildDrawList(const EcosystemFrame& f, const FadeTable& fade,
                                  const VSTGUI::CRect& r, DrawList& out) noexcept {
    out.count = 0;

    const std::size_t agents = effectiveAgents(f);
    if (agents == 0) {
        // C-6 clause 6: background + dim grid only.
        for (const Segment& line : emptyHabitatGridLines(r)) {
            DrawItem& item = out.items[out.count++];
            item = DrawItem{};
            item.kind = DrawKind::Grid;
            item.segments.pieces[0] = line;
            item.segments.count = 1;
            item.colourIndex = kGridColour;
            item.alpha = 1.0f;
            item.width = kGridWidth;
        }
        return;
    }

    const float b = habitatBrightness(f.voiceLevel);

    for (std::size_t i = 0; i < agents; ++i) {
        for (std::size_t j = i + 1; j < agents; ++j) {
            const float e = fade[fadeIndex(i, j)];
            if (!(e > 0.0f)) {
                continue;
            }
            DrawItem& item = out.items[out.count++];
            item = DrawItem{};
            item.kind = DrawKind::Link;
            item.segments = torusLinkSegments(f.agentX[i], f.agentY[i], f.agentX[j], f.agentY[j], r);
            item.colourIndex = kLinkColour;
            item.alpha = e * b;
            item.width = kLinkMaxWidth * e;
            item.agentA = static_cast<std::uint8_t>(i);
            item.agentB = static_cast<std::uint8_t>(j);
        }
    }

    for (std::size_t i = 0; i < agents; ++i) {
        const Style s = agentStyle(f.agentGlow[i], f.agentDormant[i] != 0);
        DrawItem& item = out.items[out.count++];
        item = DrawItem{};
        item.kind = DrawKind::Agent;
        item.centre = mapToView(f.agentX[i], f.agentY[i], r);
        item.radius = s.radius;
        item.filled = s.filled;
        item.colourIndex = std::min(f.agentKind[i], kMaxKindColour);
        item.alpha = s.alpha * b;
        item.width = kOutlineWidth;
        item.agentA = static_cast<std::uint8_t>(i);
    }

    DrawItem& text = out.items[out.count++];
    text = DrawItem{};
    text.kind = DrawKind::Text;
    text.centre = VSTGUI::CPoint(r.left + 6.0, r.top + 14.0);
    text.colourIndex = kTextColour;
    text.alpha = 1.0f;
    const std::string s = activeVoicesText(f.activeVoices);
    for (std::size_t k = 0; k < s.size() && k + 1 < text.text.size(); ++k) {
        text.text[k] = s[k];
    }
}

// ==============================================================================
// Draw (replay only)
// ==============================================================================

void EcosystemView::resolvePalette() {
    paletteResolved_ = true;
    auto* frame = getFrame();
    if (frame == nullptr) {
        return;
    }
    auto* editor = dynamic_cast<VSTGUI::VST3Editor*>(frame->getEditor());
    if (editor == nullptr) {
        return;
    }
    const VSTGUI::UIDescription* desc = editor->getUIDescription();
    if (desc == nullptr) {
        return;
    }
    for (std::size_t k = 0; k < kPaletteSize; ++k) {
        VSTGUI::CColor c;
        if (desc->getColor(kPaletteNames[k], c)) {
            palette_[k] = c;
        }
    }
    VSTGUI::CColor bg;
    if (desc->getColor("eco-bg", bg)) {
        background_ = bg;
    }
    if (VSTGUI::CFontRef f = desc->getFont("label-font")) {
        font_ = f;
    }
}

void EcosystemView::draw(VSTGUI::CDrawContext* context) {
    if (!paletteResolved_) {
        resolvePalette();
    }
    const VSTGUI::CRect r = getViewSize();

    context->setDrawMode(VSTGUI::kAntiAliasing | VSTGUI::kNonIntegralMode);
    context->setFillColor(background_);
    context->drawRect(r, VSTGUI::kDrawFilled);

    if (frame_ != nullptr) {
        buildDrawList(*frame_, fade_, r, *drawList_);
        for (std::size_t k = 0; k < drawList_->count; ++k) {
            const DrawItem& item = drawList_->items[k];
            if (!(item.alpha > 0.0f)) {
                continue;
            }
            const VSTGUI::CColor colour = withAlpha(palette_[item.colourIndex], item.alpha);
            switch (item.kind) {
                case DrawKind::Grid:
                case DrawKind::Link:
                    context->setFrameColor(colour);
                    context->setLineWidth(item.width);
                    for (std::size_t p = 0; p < item.segments.count; ++p) {
                        context->drawLine(item.segments.pieces[p].a, item.segments.pieces[p].b);
                    }
                    break;
                case DrawKind::Agent: {
                    const VSTGUI::CCoord rad = item.radius;
                    const VSTGUI::CRect dot(item.centre.x - rad, item.centre.y - rad,
                                            item.centre.x + rad, item.centre.y + rad);
                    if (item.filled) {
                        context->setFillColor(colour);
                        context->drawEllipse(dot, VSTGUI::kDrawFilled);
                    } else {
                        context->setFrameColor(colour);
                        context->setLineWidth(item.width);
                        context->drawEllipse(dot, VSTGUI::kDrawStroked);
                    }
                    break;
                }
                case DrawKind::Text:
                    context->setFont(font_ ? font_.get() : VSTGUI::kNormalFontSmall);
                    context->setFontColor(colour);
                    context->drawString(item.text.data(), item.centre);
                    break;
            }
        }
    }
    setDirty(false);
}

}  // namespace Vorago::UI
