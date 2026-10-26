#pragma once

// ==============================================================================
// Profundum Phase 12 - Profundum::UI::HarmonicBarView (FR-070, FR-073, FR-091)
// ==============================================================================
// Spec:  specs/profundum-phase12-ui/spec.md  (FR-070, FR-073, FR-074, FR-091; SC-026)
// Plan:  specs/profundum-phase12-ui/plan.md  (section 4.8; 3.1 "Spectrum drawing")
// Design: specs/profundum-phase12-ui/design/advanced-page.md section 3 (D2)
// Tasks: specs/profundum-phase12-ui/tasks.md (T016)
//
// The measured harmonic bars: one bar per partial n = 1 .. numPartials on a
// log2 n axis, Sub (n = 1) / Body (2 - 8) / Presence (9 - 64) shading, and the
// Anchor in its own lane on the left. Heights are dB relative to the frame's
// strongest component (the Anchor included), 0 dB at the top, -60 dB at the
// floor. On a silent frame every bar decays linearly to 0 over
// kSilenceDecaySeconds (the D2 ruling, 300 ms).
//
// DISPLAY ONLY. A CView, not a CControl: it has no tag and no listener, so it can
// never issue performEdit, and every mouse handler returns not-handled (SC-024).
//
// THREADING. Constructed, ticked and drawn on the UI thread only. It reads only
// the controller's cached frame through the pointer given at construction; the
// 33 ms timer lives from attached() to removed() (FR-091), so it can never read
// the frame after the editor closes. Nothing allocates per tick or per draw.
// ==============================================================================

#include "processor/spectrum_frame.h"

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cpoint.h"
#include "vstgui/lib/crect.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/vstguifwd.h"

#include <array>
#include <chrono>
#include <cstdint>

namespace Profundum::UI {

class HarmonicBarView : public VSTGUI::CView {
public:
    /// Main draws the ANCHOR / MASS split; Core is the Advanced-page copy.
    enum class Variant : std::uint8_t { Main, Core };

    using Heights = std::array<float, kSpectrumMaxPartials>;

    static constexpr float kSilenceDecaySeconds = 0.300f;  // D2 ruling (SC-026)
    static constexpr float kFloorDb = -60.0f;              // D2 vertical axis floor
    static constexpr std::uint32_t kTimerMs = 33;          // Vorago ecosystem_view.h:88

    /// @p frame is the controller's cached frame; it must outlive every tick,
    /// which removed() guarantees by destroying the timer.
    HarmonicBarView(const VSTGUI::CRect& size, const ProfundumSpectrumFrame* frame, Variant v);
    ~HarmonicBarView() override;

    HarmonicBarView(const HarmonicBarView&) = delete;
    HarmonicBarView& operator=(const HarmonicBarView&) = delete;
    HarmonicBarView(HarmonicBarView&&) = delete;
    HarmonicBarView& operator=(HarmonicBarView&&) = delete;

    void draw(VSTGUI::CDrawContext* context) override;
    bool attached(VSTGUI::CView* parent) override;  // creates the 33 ms timer
    bool removed(VSTGUI::CView* parent) override;   // stops + releases it

    VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;
    VSTGUI::CMouseEventResult onMouseMoved(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;
    VSTGUI::CMouseEventResult onMouseUp(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;

    [[nodiscard]] Variant variant() const noexcept { return variant_; }

    // Test surface (plan 4.8).
    [[nodiscard]] const Heights& barHeightsForTest() const noexcept { return heights_; }
    [[nodiscard]] float anchorHeightForTest() const noexcept { return anchorHeight_; }
    void onTickForTest(float dtSeconds) { onTick(dtSeconds); }
    [[nodiscard]] bool hasTimerForTest() const noexcept { return timer_ != nullptr; }

    /// h = clamp((10 log10(P / Pref) + 60) / 60, 0, 1); 0 for P <= 0 or Pref <= 0.
    [[nodiscard]] static float heightFromPower(float power, float referencePower) noexcept;

private:
    void tick();
    void onTick(float dtSeconds);
    void resolvePalette();

    const ProfundumSpectrumFrame* frame_;
    Variant variant_;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
    Heights heights_{};
    float anchorHeight_ = 0.0f;
    std::uint8_t drawnPartials_ = 0;
    std::uint32_t lastSeq_ = 0;
    bool decaying_ = false;
    std::chrono::steady_clock::time_point lastTick_{};

    // D7 tokens (accessibility-scaling.md): panel, track, text-dim, anchor, mass.
    // Fixed fallbacks, replaced by the named uidesc colours the first time the
    // view draws inside an editor.
    VSTGUI::CColor panel_;
    VSTGUI::CColor track_;
    VSTGUI::CColor textDim_;
    VSTGUI::CColor anchor_;
    VSTGUI::CColor mass_;
    VSTGUI::SharedPointer<VSTGUI::CFontDesc> font_;
    bool paletteResolved_ = false;
};

}  // namespace Profundum::UI
