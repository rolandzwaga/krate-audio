// ==============================================================================
// Vorago Phase 13 - EcosystemView tests (FR-050, FR-051, SC-014, SC-015, SC-016 b)
// ==============================================================================
// Spec:  specs/vorago-phase13-ui/spec.md  (C-6, FR-050, FR-051, SC-014 - SC-016)
// Plan:  specs/vorago-phase13-ui/plan.md  (sections 6, 9.2)
// Tasks: specs/vorago-phase13-ui/tasks.md (T003)
//
// Every draw decision of the view lives in a static, CFrame-free function, so
// the arithmetic (mapping, agent style, torus link geometry, link alpha, fade,
// habitat brightness, readout text, redraw predicate) and the full draw list are
// asserted here without a platform window.
//
// Vorago_EcosystemView_AttachRemove carries [lifecycle]: the ASan / valgrind
// lanes select cases by that tag (SC-016 b). No platform message pump runs
// (ruling R-5): the case proves the timer is DESTROYED by removed(), and the
// variant frees the frame the timer callback would read, idles 150 ms, then
// destroys the view - any late access is an ASan report.
//
// habitatBrightness(0.5): the spec literal 0.899657 is the rounding of the R-3
// formula's value (20*log10(0.5) + 60) / 60 = 0.8996566681...; the case asserts
// both the formula value and the spec literal at epsilon = 1e-6.
//
// DrawList (a): tasks.md writes the brightness factor as "* 0.5f" (the pre-R-3
// linear map of voiceLevel 0.5). Under R-3 the factor is habitatBrightness(0.5f),
// so the case multiplies by that function's value.
//
// NEVER name a kPlatformType* constant here (lint-platform-type-literals.js).
// ==============================================================================

#include "test_helpers/editor_lifecycle_harness.h"

#include "processor/ecosystem_frame.h"
#include "ui/ecosystem_view.h"

#include "vstgui/lib/cpoint.h"
#include "vstgui/lib/crect.h"
#include "vstgui/lib/cviewcontainer.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <thread>

using Vorago::EcosystemFrame;
using Vorago::UI::EcosystemView;

namespace {

const VSTGUI::CRect kRect(350, 36, 750, 436);

/// Test-local torus delta (mirrors ecosystem_engine.h:1111-1119, which is private).
double td(double d) {
    if (d > 0.5) {
        return d - 1.0;
    }
    if (d < -0.5) {
        return d + 1.0;
    }
    return d;
}

bool insideTol(const VSTGUI::CPoint& p, const VSTGUI::CRect& r) {
    constexpr double kTol = 1e-9;
    return p.x >= r.left - kTol && p.x <= r.right + kTol && p.y >= r.top - kTol &&
           p.y <= r.bottom + kTol;
}

double segLength(const EcosystemView::Segment& s) {
    const double dx = s.b.x - s.a.x;
    const double dy = s.b.y - s.a.y;
    return std::sqrt((dx * dx) + (dy * dy));
}

/// Checks one torusLinkSegments case; returns the piece count.
std::size_t checkTorusCase(float ax, float ay, float bx, float by) {
    const auto segs = EcosystemView::torusLinkSegments(ax, ay, bx, by, kRect);
    REQUIRE(segs.count <= segs.pieces.size());
    double total = 0.0;
    for (std::size_t k = 0; k < segs.count; ++k) {
        REQUIRE(insideTol(segs.pieces[k].a, kRect));
        REQUIRE(insideTol(segs.pieces[k].b, kRect));
        total += segLength(segs.pieces[k]);
    }
    const double dx = td(static_cast<double>(bx) - static_cast<double>(ax));
    const double dy = td(static_cast<double>(by) - static_cast<double>(ay));
    const double expected = std::sqrt((dx * dx) + (dy * dy)) * kRect.getWidth();
    REQUIRE(std::fabs(total - expected) <= 1.0);
    return segs.count;
}

EcosystemFrame oneLinkFrame() {
    EcosystemFrame f{};
    f.agentCount = 8;
    f.linkCount = 1;
    f.linkA[0] = 1;
    f.linkB[0] = 2;
    f.linkStrength[0] = 1.0f;
    f.linkFlowScale = 1.0f;
    return f;
}

std::size_t countKind(const EcosystemView::DrawList& list, EcosystemView::DrawKind kind) {
    std::size_t n = 0;
    for (std::size_t k = 0; k < list.count; ++k) {
        if (list.items[k].kind == kind) {
            ++n;
        }
    }
    return n;
}

}  // namespace

// ==============================================================================
// SC-014 - view arithmetic
// ==============================================================================

TEST_CASE("Vorago_EcosystemView_Mapping", "[vorago][ui][ecosystem]") {
    const VSTGUI::CRect r = kRect;

    SECTION("CornersMapInside") {
        REQUIRE(r.pointInside(EcosystemView::mapToView(0.0f, 0.0f, r)));
        REQUIRE(r.pointInside(EcosystemView::mapToView(1.0f - 1e-6f, 1.0f - 1e-6f, r)));
    }

    SECTION("AgentStyle") {
        REQUIRE(EcosystemView::kAgentOutlineMinAlpha > 0.0f);
        const auto zero = EcosystemView::agentStyle(0.0f, false);
        REQUIRE_FALSE(zero.filled);
        REQUIRE(zero.alpha >= EcosystemView::kAgentOutlineMinAlpha);

        float prevAlpha = -1.0f;
        float prevRadius = -1.0f;
        for (int k = 0; k < 256; ++k) {
            const float g = static_cast<float>(k) / 255.0f;
            const auto s = EcosystemView::agentStyle(g, false);
            REQUIRE(s.alpha >= prevAlpha);
            REQUIRE(s.radius >= prevRadius);
            prevAlpha = s.alpha;
            prevRadius = s.radius;
            REQUIRE_FALSE(EcosystemView::agentStyle(g, true).filled);
        }
    }

    SECTION("TorusLinkSegments") {
        REQUIRE(checkTorusCase(0.4f, 0.4f, 0.6f, 0.5f) == 1);
        REQUIRE(checkTorusCase(0.95f, 0.5f, 0.05f, 0.5f) == 2);
        REQUIRE(checkTorusCase(0.5f, 0.95f, 0.5f, 0.05f) == 2);
        REQUIRE(checkTorusCase(0.95f, 0.95f, 0.05f, 0.05f) >= 2);
    }

    SECTION("NeedsRedraw") {
        const std::uint32_t s = 7u;
        REQUIRE_FALSE(EcosystemView::needsRedraw(s, s, 0.0f));
        REQUIRE(EcosystemView::needsRedraw(s, s + 1u, 0.0f));
        REQUIRE(EcosystemView::needsRedraw(s, s, 1e-3f));
    }

    SECTION("IsDrawableLink") {
        EcosystemFrame f{};
        f.agentCount = 8;
        f.linkCount = 1;
        f.linkA[0] = 1;
        f.linkB[0] = 2;
        REQUIRE(EcosystemView::isDrawableLink(f, 0));
        REQUIRE_FALSE(EcosystemView::isDrawableLink(f, 1));  // l >= linkCount

        EcosystemFrame fa = f;
        fa.linkA[0] = 8;  // == agentCount
        REQUIRE_FALSE(EcosystemView::isDrawableLink(fa, 0));

        EcosystemFrame fb = f;
        fb.linkB[0] = 8;
        REQUIRE_FALSE(EcosystemView::isDrawableLink(fb, 0));

        EcosystemFrame fs = f;
        fs.linkB[0] = 1;  // linkA == linkB
        REQUIRE_FALSE(EcosystemView::isDrawableLink(fs, 0));
    }

    SECTION("EmptyHabitatGridLines") {
        const auto lines = EcosystemView::emptyHabitatGridLines(r);
        REQUIRE(lines.size() == 6);
        std::set<double> xs;
        std::set<double> ys;
        std::size_t vertical = 0;
        std::size_t horizontal = 0;
        for (const auto& l : lines) {
            if (l.a.x == l.b.x) {
                ++vertical;
                xs.insert(l.a.x);
            } else if (l.a.y == l.b.y) {
                ++horizontal;
                ys.insert(l.a.y);
            }
        }
        REQUIRE(vertical == 3);
        REQUIRE(horizontal == 3);
        REQUIRE(xs == std::set<double>{450.0, 550.0, 650.0});
        REQUIRE(ys == std::set<double>{136.0, 236.0, 336.0});
    }

    SECTION("LinkAlpha") {
        float prev = -1.0f;
        for (int k = 0; k < 256; ++k) {
            const float s = 200.0f * static_cast<float>(k) / 255.0f;
            const float a = EcosystemView::linkAlpha(s, 2.0f);
            REQUIRE(a >= prev);
            prev = a;
        }
        REQUIRE(EcosystemView::linkAlpha(2.0f, 2.0f) == 0.5f);
        REQUIRE(EcosystemView::linkAlpha(96.0f, 2.0f) < 1.0f);
        REQUIRE(EcosystemView::linkAlpha(0.0f, 2.0f) == 0.0f);
        REQUIRE(EcosystemView::linkAlpha(-1.0f, 2.0f) == 0.0f);
        REQUIRE(EcosystemView::linkAlpha(1.0f, 0.0f) == 0.0f);
        REQUIRE(EcosystemView::linkAlpha(1.0f, -1.0f) == 0.0f);
    }

    SECTION("HabitatBrightness") {
        constexpr double kEps = 1e-6;
        const double halfExpected = (20.0 * std::log10(0.5) + 60.0) / 60.0;  // 0.8996566681...
        REQUIRE(std::fabs(EcosystemView::habitatBrightness(0.01f) - (1.0 / 3.0)) <= kEps);
        REQUIRE(std::fabs(EcosystemView::habitatBrightness(0.1f) - (2.0 / 3.0)) <= kEps);
        REQUIRE(std::fabs(EcosystemView::habitatBrightness(0.5f) - halfExpected) <= kEps);
        REQUIRE(std::fabs(EcosystemView::habitatBrightness(0.5f) - 0.899657) <= kEps);
        REQUIRE(EcosystemView::habitatBrightness(1.0f) == 1.0f);
        REQUIRE(EcosystemView::habitatBrightness(0.0f) == 0.0f);
        REQUIRE(EcosystemView::habitatBrightness(-1.0f) == 0.0f);
        REQUIRE(EcosystemView::habitatBrightness(1e-3f) == 0.0f);
        REQUIRE(EcosystemView::habitatBrightness(2.0f) == 1.0f);

        float prev = -1.0f;
        for (int k = 0; k < 256; ++k) {
            const float v = -1.0f + (3.0f * static_cast<float>(k) / 255.0f);
            const float b = EcosystemView::habitatBrightness(v);
            REQUIRE(b >= prev);
            prev = b;
        }
    }

    SECTION("ActiveVoicesText") {
        for (const int n : {0, 1, 6, 255}) {
            REQUIRE(EcosystemView::activeVoicesText(static_cast<std::uint8_t>(n)) ==
                    std::to_string(n));
        }
    }
}

// ==============================================================================
// SC-015 - link fade
// ==============================================================================

TEST_CASE("Vorago_EcosystemView_LinkFade", "[vorago][ui][ecosystem]") {
    const auto idx12 = EcosystemView::fadeIndex(1, 2);

    SECTION("PresentThenAbsent") {
        EcosystemFrame f = oneLinkFrame();
        EcosystemView::FadeTable t{};
        const float m = EcosystemView::stepLinkFade(t, f, false, 0.0f);
        REQUIRE(EcosystemView::linkAlpha(1.0f, 1.0f) == 0.5f);
        REQUIRE(t[idx12] >= EcosystemView::linkAlpha(1.0f, 1.0f));
        REQUIRE(m == t[idx12]);

        f.linkCount = 0;
        const float dt = 1.0f / 30.0f;
        bool sawZero = false;
        for (int n = 1; n <= 150; ++n) {
            (void)EcosystemView::stepLinkFade(t, f, false, dt);
            const double tt = static_cast<double>(n) * static_cast<double>(dt);
            const double ideal = std::exp(-tt / 0.6) * 0.5;
            const float e = t[idx12];
            REQUIRE(static_cast<double>(e) <= ideal + 1e-6);
            if (e != 0.0f) {
                REQUIRE(e >= EcosystemView::kFadeFloor);
            }
            if (ideal < static_cast<double>(EcosystemView::kFadeFloor) - 1e-6) {
                REQUIRE(e == 0.0f);
            }
            sawZero = sawZero || (e == 0.0f);
        }
        REQUIRE(sawZero);
    }

    SECTION("PresentLinkNeverBelowLinkAlpha") {
        EcosystemFrame f = oneLinkFrame();
        EcosystemView::FadeTable t{};
        for (int n = 0; n < 60; ++n) {
            (void)EcosystemView::stepLinkFade(t, f, false, 1.0f / 30.0f);
            REQUIRE(t[idx12] >= EcosystemView::linkAlpha(f.linkStrength[0], f.linkFlowScale));
        }
    }

    SECTION("DtIsClamped") {
        EcosystemFrame f = oneLinkFrame();
        EcosystemView::FadeTable ta{};
        EcosystemView::FadeTable tb{};
        (void)EcosystemView::stepLinkFade(ta, f, false, 0.0f);
        (void)EcosystemView::stepLinkFade(tb, f, false, 0.0f);
        f.linkCount = 0;
        const float ma = EcosystemView::stepLinkFade(ta, f, false, 1.0f);
        const float mb = EcosystemView::stepLinkFade(tb, f, false, 0.25f);
        REQUIRE(ma == mb);
        REQUIRE(ta == tb);
        REQUIRE(ta[idx12] > 0.0f);
    }

    SECTION("ClearZeroesEveryEntry") {
        EcosystemFrame f = oneLinkFrame();
        f.linkCount = 3;
        f.linkA[1] = 0;
        f.linkB[1] = 7;
        f.linkStrength[1] = 2.0f;
        f.linkA[2] = 5;
        f.linkB[2] = 3;
        f.linkStrength[2] = 4.0f;
        EcosystemView::FadeTable t{};
        REQUIRE(EcosystemView::stepLinkFade(t, f, false, 0.0f) > 0.0f);
        f.linkCount = 0;
        REQUIRE(EcosystemView::stepLinkFade(t, f, true, 0.0f) == 0.0f);
        for (const float e : t) {
            REQUIRE(e == 0.0f);
        }
    }

    SECTION("LiveViewClearsOnFocusAndCountChange") {
        EcosystemFrame f = oneLinkFrame();  // focus 0, agentCount 8, link 1-2
        auto* v = new EcosystemView(kRect, &f);

        const float a0 = v->onTimerForTest(0.0f);
        REQUIRE(a0 > 0.0f);

        ++f.sequence;
        f.linkCount = 0;
        const float a1 = v->onTimerForTest(1e-6f);
        REQUIRE(a1 >= 0.99f * a0);  // decay only: the non-vacuity control

        f.focusVoice = 1;
        REQUIRE(v->onTimerForTest(1e-6f) == 0.0f);  // cleared, not decayed

        // Fresh link on the (now) current focus/count, then an agentCount change.
        ++f.sequence;
        f.linkCount = 1;
        const float a2 = v->onTimerForTest(0.0f);
        REQUIRE(a2 > 0.0f);

        ++f.sequence;
        f.linkCount = 0;
        f.agentCount = 9;
        REQUIRE(v->onTimerForTest(1e-6f) == 0.0f);

        v->forget();
    }
}

// ==============================================================================
// FR-051 - the draw list (C-6 clauses 2, 3, 6, 7, 8)
// ==============================================================================

TEST_CASE("Vorago_EcosystemView_DrawList", "[vorago][ui][ecosystem]") {
    const VSTGUI::CRect r = kRect;
    auto list = std::make_unique<EcosystemView::DrawList>();

    EcosystemFrame f{};
    f.agentCount = 8;
    f.activeVoices = 3;
    for (std::size_t i = 0; i < 8; ++i) {
        f.agentX[i] = 0.1f + (0.1f * static_cast<float>(i));
        f.agentY[i] = 0.2f + (0.07f * static_cast<float>(i));
        f.agentGlow[i] = static_cast<float>(i) / 7.0f;
        f.agentDormant[i] = static_cast<std::uint8_t>((i % 3 == 0) ? 1 : 0);
        f.agentKind[i] = static_cast<std::uint8_t>(i % 5);
    }
    f.linkCount = 3;
    f.linkA[0] = 0;
    f.linkB[0] = 1;
    f.linkStrength[0] = 0.5f;
    f.linkA[1] = 5;
    f.linkB[1] = 2;
    f.linkStrength[1] = 1.0f;
    f.linkA[2] = 3;
    f.linkB[2] = 7;
    f.linkStrength[2] = 2.0f;
    f.linkFlowScale = 1.0f;

    EcosystemView::FadeTable fade{};
    (void)EcosystemView::stepLinkFade(fade, f, false, 0.0f);

    SECTION("AgentsAndLinksScaledByBrightness") {
        for (const float level : {0.5f, 0.0f, 2.0f}) {
            f.voiceLevel = level;
            const float b = EcosystemView::habitatBrightness(level);
            EcosystemView::buildDrawList(f, fade, r, *list);

            REQUIRE(countKind(*list, EcosystemView::DrawKind::Agent) == 8);
            REQUIRE(countKind(*list, EcosystemView::DrawKind::Link) == 3);

            for (std::size_t k = 0; k < list->count; ++k) {
                const auto& item = list->items[k];
                if (item.kind == EcosystemView::DrawKind::Agent) {
                    const std::size_t i = item.agentA;
                    REQUIRE(i < 8);
                    const auto s = EcosystemView::agentStyle(f.agentGlow[i], f.agentDormant[i] != 0);
                    REQUIRE(item.alpha == s.alpha * b);
                    REQUIRE(item.filled == s.filled);
                    REQUIRE(item.colourIndex == f.agentKind[i]);
                    REQUIRE(item.centre == EcosystemView::mapToView(f.agentX[i], f.agentY[i], r));
                    if (level == 0.0f) {
                        REQUIRE(item.alpha == 0.0f);
                    }
                    if (level == 2.0f) {
                        REQUIRE(item.alpha == s.alpha);
                    }
                } else if (item.kind == EcosystemView::DrawKind::Link) {
                    REQUIRE(item.agentA < item.agentB);
                    const float e = fade[EcosystemView::fadeIndex(item.agentA, item.agentB)];
                    REQUIRE(e > 0.0f);
                    REQUIRE(item.alpha == e * b);
                    REQUIRE(item.width == EcosystemView::kLinkMaxWidth * e);
                    if (level == 0.0f) {
                        REQUIRE(item.alpha == 0.0f);
                    }
                    if (level == 2.0f) {
                        REQUIRE(item.alpha == e);
                    }
                }
            }
        }
    }

    SECTION("EmptyHabitatIsGridOnly") {
        f.agentCount = 0;  // stale fade entries and frame links remain
        f.voiceLevel = 1.0f;
        EcosystemView::buildDrawList(f, fade, r, *list);
        REQUIRE(list->count == 6);
        const auto grid = EcosystemView::emptyHabitatGridLines(r);
        for (std::size_t k = 0; k < 6; ++k) {
            const auto& item = list->items[k];
            REQUIRE(item.kind == EcosystemView::DrawKind::Grid);
            REQUIRE(item.segments.count == 1);
            REQUIRE(item.segments.pieces[0].a == grid[k].a);
            REQUIRE(item.segments.pieces[0].b == grid[k].b);
        }
    }

    SECTION("InvalidLinksNeverDraw") {
        EcosystemFrame g = f;
        g.voiceLevel = 1.0f;
        g.linkCount = 2;
        g.linkA[0] = 1;
        g.linkB[0] = g.agentCount;  // endpoint out of range
        g.linkStrength[0] = 1.0f;
        g.linkA[1] = 4;
        g.linkB[1] = 4;  // self-link
        g.linkStrength[1] = 1.0f;

        EcosystemView::FadeTable t{};
        REQUIRE(EcosystemView::stepLinkFade(t, g, false, 0.0f) == 0.0f);
        for (const float e : t) {
            REQUIRE(e == 0.0f);
        }
        EcosystemView::buildDrawList(g, t, r, *list);
        REQUIRE(countKind(*list, EcosystemView::DrawKind::Link) == 0);

        // A hand-set entry whose indices are beyond agentCount draws nothing.
        EcosystemFrame h = f;
        h.voiceLevel = 1.0f;
        h.agentCount = 32;
        h.linkCount = 0;
        EcosystemView::FadeTable th{};
        th[EcosystemView::fadeIndex(40, 45)] = 0.5f;
        EcosystemView::buildDrawList(h, th, r, *list);
        REQUIRE(countKind(*list, EcosystemView::DrawKind::Link) == 0);
        REQUIRE(countKind(*list, EcosystemView::DrawKind::Agent) == 32);
    }

    SECTION("ActiveVoicesReadout") {
        f.voiceLevel = 1.0f;
        for (const int n : {0, 3, 6}) {
            f.activeVoices = static_cast<std::uint8_t>(n);
            EcosystemView::buildDrawList(f, fade, r, *list);
            REQUIRE(countKind(*list, EcosystemView::DrawKind::Text) == 1);
            for (std::size_t k = 0; k < list->count; ++k) {
                const auto& item = list->items[k];
                if (item.kind == EcosystemView::DrawKind::Text) {
                    REQUIRE(std::string(item.text.data()) ==
                            EcosystemView::activeVoicesText(f.activeVoices));
                }
            }
        }
    }
}

// ==============================================================================
// SC-016 (b) - timer lifecycle under attach / remove
// ==============================================================================

TEST_CASE("Vorago_EcosystemView_AttachRemove", "[vorago][ui][ecosystem][lifecycle]") {
    Krate::TestSupport::ensureVstguiInitialized();
    auto parent = VSTGUI::makeOwned<VSTGUI::CViewContainer>(VSTGUI::CRect(0, 0, 1100, 760));
    auto frame = std::make_unique<EcosystemFrame>();
    auto* view = new EcosystemView(kRect, frame.get());

    SECTION("TenCycles") {
        for (int cycle = 0; cycle < 10; ++cycle) {
            REQUIRE(view->attached(parent.get()));
            REQUIRE(view->hasTimerForTest());
            REQUIRE(view->removed(parent.get()));
            REQUIRE_FALSE(view->hasTimerForTest());
        }
        view->forget();
    }

    SECTION("FrameFreedAfterRemove") {
        for (int cycle = 0; cycle < 10; ++cycle) {
            REQUIRE(view->attached(parent.get()));
            REQUIRE(view->hasTimerForTest());
            REQUIRE(view->removed(parent.get()));
            REQUIRE_FALSE(view->hasTimerForTest());
        }
        frame.reset();  // the memory the timer callback reads is gone
        for (int slice = 0; slice < 15; ++slice) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        view->forget();  // ASan run (T024) proves no access happened
    }
}
