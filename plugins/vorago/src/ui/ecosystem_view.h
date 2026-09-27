#pragma once

// ==============================================================================
// Vorago Phase 13 - Vorago::UI::EcosystemView (spec C-6, FR-050, FR-051)
// ==============================================================================
// Spec:  specs/vorago-phase13-ui/spec.md  (C-6, FR-050, FR-051, SC-014 - SC-016)
// Plan:  specs/vorago-phase13-ui/plan.md  (section 6)
// Tasks: specs/vorago-phase13-ui/tasks.md (T003)
//
// The live habitat of the focus voice's ecosystem: agents as glowing points,
// energy exchange as fading links on the torus, a dim grid when empty, and a
// small active-voice readout. It owns NO DSP and NO parameter - it reads only
// the controller's cached EcosystemFrame through the pointer it is given at
// construction, and never a processor header (FR-020, FR-051).
//
// Every draw decision is made by a static, CFrame-free function (FR-050):
// buildDrawList() turns (frame, fade table, rect) into a preallocated DrawList,
// and draw() only replays it. Nothing here allocates per tick or per draw.
//
// THREADING. Constructed, ticked and drawn on the UI thread only. The 30 Hz
// timer is created in attached() and destroyed in removed() (C-6 clause 5), so
// it can never outlive the view or read the frame after the editor closes.
// ==============================================================================

#include "processor/ecosystem_frame.h"

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cpoint.h"
#include "vstgui/lib/crect.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/vstguifwd.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace Vorago::UI {

class EcosystemView : public VSTGUI::CView {
public:
    /// @p frame is the controller's cached frame; it must outlive every tick,
    /// which removed() guarantees by destroying the timer.
    EcosystemView(const VSTGUI::CRect& size, const EcosystemFrame* frame);
    ~EcosystemView() override;

    EcosystemView(const EcosystemView&) = delete;
    EcosystemView& operator=(const EcosystemView&) = delete;
    EcosystemView(EcosystemView&&) = delete;
    EcosystemView& operator=(EcosystemView&&) = delete;

    void draw(VSTGUI::CDrawContext* context) override;
    bool attached(VSTGUI::CView* parent) override;  // creates the 30 Hz timer
    bool removed(VSTGUI::CView* parent) override;   // stops + releases it

    [[nodiscard]] bool hasTimerForTest() const noexcept { return timer_ != nullptr; }

    /// The tick body with an injected dt (SC-015). Returns the max fade alpha it
    /// computed, so a test can observe the clear predicate on a live view.
    [[nodiscard]] float onTimerForTest(float dtSeconds);

    struct Style {
        bool filled;
        float alpha;
        float radius;
    };
    struct Segment {
        VSTGUI::CPoint a, b;
    };
    struct Segments {
        std::array<Segment, 4> pieces{};
        std::size_t count = 0;
    };
    /// Upper triangle (i < j) used, indexed by fadeIndex(i, j).
    using FadeTable = std::array<float, kMaxFrameAgents * kMaxFrameAgents>;

    static constexpr float kAgentOutlineMinAlpha = 0.25f;
    static constexpr float kAgentMinRadius = 2.0f;  // px, 400-px habitat
    static constexpr float kAgentMaxRadius = 9.0f;
    static constexpr float kLinkFadeSeconds = 0.6f;
    static constexpr float kLinkMaxWidth = 2.5f;
    static constexpr float kFadeFloor = 1.0f / 255.0f;
    static constexpr float kMaxTickSeconds = 0.25f;
    static constexpr std::uint32_t kTimerMs = 33;  // ~30 Hz (Membrum pad_grid_view.h:30-37)

    [[nodiscard]] static constexpr std::size_t fadeIndex(std::size_t i, std::size_t j) noexcept {
        return (i * kMaxFrameAgents) + j;
    }

    [[nodiscard]] static VSTGUI::CPoint mapToView(float x, float y, const VSTGUI::CRect& r) noexcept;
    [[nodiscard]] static Style agentStyle(float glow, bool dormant) noexcept;
    [[nodiscard]] static float linkAlpha(float strength, float flowScale) noexcept;
    [[nodiscard]] static Segments torusLinkSegments(float ax, float ay, float bx, float by,
                                                    const VSTGUI::CRect& r) noexcept;
    [[nodiscard]] static bool isDrawableLink(const EcosystemFrame& f, std::size_t l) noexcept;
    [[nodiscard]] static std::array<Segment, 6> emptyHabitatGridLines(const VSTGUI::CRect& r) noexcept;
    [[nodiscard]] static bool needsRedraw(std::uint32_t prevSeq, std::uint32_t seq,
                                          float maxFadeAlpha) noexcept;
    [[nodiscard]] static float habitatBrightness(float voiceLevel) noexcept;
    [[nodiscard]] static std::string activeVoicesText(std::uint8_t activeVoices) noexcept;
    /// C-6 clause 4. Returns the max alpha left in the table (the needsRedraw input).
    static float stepLinkFade(FadeTable& table, const EcosystemFrame& f, bool clear,
                              float dtSeconds) noexcept;

    /// FR-051 draw contract as data. draw() only replays this list.
    enum class DrawKind : std::uint8_t { Grid, Link, Agent, Text };
    struct DrawItem {
        DrawKind kind = DrawKind::Grid;
        Segments segments{};           // Grid: 1 piece; Link: torusLinkSegments pieces
        VSTGUI::CPoint centre{};       // Agent centre / Text anchor
        float radius = 0.0f;           // Agent
        std::uint8_t colourIndex = 0;  // Agent: Kind 0..4; Link: 5 (eco-link); Grid: 6 (eco-grid); Text: 7 (text-dim)
        float alpha = 0.0f;
        float width = 0.0f;            // Link / Grid line width
        bool filled = false;           // Agent
        std::uint8_t agentA = 0;       // Link endpoint A / Agent index
        std::uint8_t agentB = 0;       // Link endpoint B
        std::array<char, 4> text{};    // Text: activeVoicesText, NUL-terminated (<= 3 digits)
    };
    static constexpr std::size_t kMaxFadePairs = kMaxFrameAgents * (kMaxFrameAgents - 1) / 2;  // 1128
    static constexpr std::size_t kMaxDrawItems = 6 + kMaxFadePairs + kMaxFrameAgents + 1;     // 1183
    struct DrawList {
        std::array<DrawItem, kMaxDrawItems> items{};
        std::size_t count = 0;
    };

    /// Pure, CFrame-free. Clears @p out, then appends: agentCount == 0 -> exactly
    /// the 6 emptyHabitatGridLines and nothing else; otherwise one Link item per
    /// non-zero (i<j) fade entry with i, j < agentCount (alpha = e *
    /// habitatBrightness(voiceLevel), width = kLinkMaxWidth * e), then one Agent
    /// item per i < agentCount (alpha = agentStyle(..).alpha *
    /// habitatBrightness(voiceLevel)), then one Text item = activeVoicesText.
    static void buildDrawList(const EcosystemFrame& f, const FadeTable& fade, const VSTGUI::CRect& r,
                              DrawList& out) noexcept;

private:
    static constexpr std::size_t kPaletteSize = 8;

    void tick();
    void resolvePalette();

    const EcosystemFrame* frame_;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
    FadeTable fade_{};
    std::unique_ptr<DrawList> drawList_;  // ~0.2 MB, allocated once in the ctor, never per tick
    std::uint32_t lastSeq_ = 0;
    std::uint8_t lastFocus_ = 0;
    std::uint8_t lastAgentCount_ = 0;
    bool haveSeen_ = false;
    std::chrono::steady_clock::time_point lastTick_{};

    // eco-partial, eco-resonator, eco-noise, eco-feedback, eco-ghost, eco-link,
    // eco-grid, text-dim - fixed fallbacks, replaced by the named uidesc colours
    // the first time the view draws inside an editor.
    std::array<VSTGUI::CColor, kPaletteSize> palette_{};
    VSTGUI::CColor background_{};
    VSTGUI::SharedPointer<VSTGUI::CFontDesc> font_;
    bool paletteResolved_ = false;
};

}  // namespace Vorago::UI
