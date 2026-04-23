#pragma once

// ==============================================================================
// PadGridView -- Phase 6 (Spec 141, US3) -- 4x8 pad grid editor custom view
// ==============================================================================
// FR-010..FR-015, SC-004, SC-005
//
// Layout:
//   4 columns x 8 rows = 32 pads. MIDI 36 sits at the bottom-left, MIDI 67 at
//   the top-right, matching the GM drum map ordering from Phase 4.
//
//   col 0   col 1   col 2   col 3
//   +-----+-----+-----+-----+
//   | 60  | 61  | 62  | 63  |  row 0 (top)      pads 24..27
//   +-----+-----+-----+-----+
//   | 56  | 57  | 58  | 59  |  row 1            pads 20..23
//   ...
//   | 40  | 41  | 42  | 43  |  row 5
//   | 36  | 37  | 38  | 39  |  row 6 (bottom)   pads 0..3
//   +-----+-----+-----+-----+
//
// Interaction (FR-012 / FR-013 / research.md section 9):
//   - Left click (no modifiers): select the pad -> drives `kSelectedPadId`
//     via `selectCallback_`.
//   - Shift + left click: audition at velocity 100 without changing selection.
//   - Right click: audition at velocity 100 without changing selection.
//
// Rendering:
//   - Reads pad category + choke group + output bus from a
//     `PadMetaProvider` callback so unit tests can stub the data without a
//     live Processor.
//   - Reads pad glow amplitude from a `PadGlowPublisher*` (Phase 6). The view
//     owns a 30 Hz `CVSTGUITimer` that snapshots the publisher and calls
//     `invalid()` on cells whose bucket changed (dirty-rect optimisation).
//
// Lifecycle:
//   - `removed()` cancels the timer (SC-014). No `IDependent` subscription is
//     held directly by this view; the controller wires publisher polling.
// ==============================================================================

#include "dsp/pad_config.h"

#include "vstgui/lib/cview.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/cgradient.h"
#include "vstgui/lib/crect.h"
#include "vstgui/lib/cpoint.h"
#include "vstgui/lib/events.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace Membrum {
class PadGlowPublisher;
struct PadConfig;
} // namespace Membrum

namespace Membrum::UI {

// ------------------------------------------------------------------------------
// Grid geometry (column-major layout; col index increases along MIDI note).
// ------------------------------------------------------------------------------
inline constexpr int kPadGridColumns = 4;
inline constexpr int kPadGridRows    = 8;

// Convert a (col, row) cell -- row 0 at the top -- into a MIDI note.
// MIDI 36 lives at (col=0, row=7); MIDI 67 lives at (col=3, row=0).
[[nodiscard]] constexpr int padIndexFromCell(int col, int row) noexcept
{
    // Bottom-left is pad 0 (MIDI 36). rows are drawn top->bottom so the
    // logical row 0 maps to the highest pads.
    const int bottomRow = (kPadGridRows - 1) - row;
    return bottomRow * kPadGridColumns + col;
}

[[nodiscard]] constexpr int midiNoteForPad(int padIndex) noexcept
{
    return static_cast<int>(kFirstDrumNote) + padIndex;
}

// ------------------------------------------------------------------------------
// Text helpers (exposed for unit tests -- FR-011).
// Returns empty string when the indicator should not be drawn.
// ------------------------------------------------------------------------------
[[nodiscard]] std::string chokeGroupIndicatorText(std::uint8_t chokeGroup);
[[nodiscard]] std::string outputBusIndicatorText(std::uint8_t outputBus);

/// GM Percussion Key Map (General MIDI) short name for a MIDI note in [36, 67].
/// Returns empty string for MIDI notes outside that range.
[[nodiscard]] std::string gmDrumNameForNote(int midiNote);

/// Single-character category glyph suitable for small-cell rendering (FR-011).
/// Returns a 1-char string:
///   Kick -> "K", Snare -> "S", HatCymbal -> "H", Tom -> "T", Perc -> "P".
/// Unclassified / out-of-range categories return empty string.
[[nodiscard]] std::string categoryGlyphForConfig(const PadConfig& cfg);

// Map a PadGlowPublisher amplitude bucket (0..31) to a normalised [0..1]
// brightness. Bucket 0 => 0.0 (silent); bucket 31 => 1.0 (maximum).
[[nodiscard]] constexpr float glowIntensityFromBucket(std::uint8_t bucket) noexcept
{
    return (bucket == 0)
         ? 0.0f
         : static_cast<float>(bucket) / 31.0f;
}

// ------------------------------------------------------------------------------
// PadGridView
// ------------------------------------------------------------------------------
class PadGridView : public VSTGUI::CView
{
public:
    /// Returns the pad's config (read-only snapshot); index in [0, kNumPads).
    /// Nullable config pointer means "pad inactive / unavailable".
    using PadMetaProvider = std::function<const PadConfig*(int padIndex)>;

    /// Fires when the user clicks a pad (no modifier / no right button).
    /// Implementations SHOULD drive `kSelectedPadId` via beginEdit/performEdit/endEdit.
    using SelectCallback   = std::function<void(int padIndex)>;

    /// Fires when the user shift-clicks or right-clicks a pad.
    /// `velocity` is normalised in [0, 1].
    using AuditionCallback = std::function<void(int padIndex, float velocity)>;

    PadGridView(const VSTGUI::CRect& size,
                PadGlowPublisher*    glowPublisher,
                PadMetaProvider      metaProvider) noexcept;

    ~PadGridView() override;

    PadGridView(const PadGridView&)            = delete;
    PadGridView& operator=(const PadGridView&) = delete;

    // --- callbacks ---------------------------------------------------------
    void setSelectCallback(SelectCallback cb)     noexcept { selectCallback_   = std::move(cb); }
    void setAuditionCallback(AuditionCallback cb) noexcept { auditionCallback_ = std::move(cb); }

    /// Drive the highlighted pad. Called by the controller when
    /// `kSelectedPadId` changes.
    void setSelectedPadIndex(int padIndex) noexcept;
    [[nodiscard]] int selectedPadIndex() const noexcept { return selectedPad_; }

    /// FR-065 (spec 141, Phase 8 / US7): notify the view that a pad's meta
    /// (outputBus / chokeGroup / category) has changed so it re-renders its
    /// BUS / CG indicator. The controller calls this from
    /// `setParamNormalized()` when a per-pad Output Bus or Choke Group
    /// parameter (global proxy or direct per-pad tag) changes. The view
    /// simply invalidates so the next draw pass picks up the new indicator
    /// text via the meta-provider callback.
    void notifyMetaChanged(int padIndex) noexcept;

    // --- testable helpers (public for unit tests) --------------------------

    /// Map a point (relative to view origin) into a pad index in [0, kNumPads),
    /// or -1 if the point is outside the grid.
    [[nodiscard]] int padIndexFromPoint(VSTGUI::CPoint p) const noexcept;

    /// Dispatch a mouse-down event. Returns the pad index that was hit
    /// (for tests). `isShift` and `isRight` follow research.md section 9.
    int handleMouseDown(VSTGUI::CPoint localPoint,
                        bool           isShift,
                        bool           isRight) noexcept;

    /// Snapshot the publisher into the internal glow-bucket cache. Normally
    /// called by the 30 Hz timer; tests call it directly.
    void pollGlow() noexcept;

    [[nodiscard]] std::uint8_t glowBucketForPad(int padIndex) const noexcept
    {
        return (padIndex < 0 || padIndex >= kNumPads)
             ? std::uint8_t{0}
             : glowBuckets_[static_cast<std::size_t>(padIndex)];
    }

    /// Read the meta provider (returns nullptr if unset/out-of-range).
    [[nodiscard]] const PadConfig* padConfigFor(int padIndex) const noexcept
    {
        if (!metaProvider_ || padIndex < 0 || padIndex >= kNumPads)
            return nullptr;
        return metaProvider_(padIndex);
    }

    // --- VSTGUI hooks ------------------------------------------------------
    void draw(VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& event) override;
    void onMouseEnterEvent(VSTGUI::MouseEnterEvent& event) override;
    void onMouseExitEvent(VSTGUI::MouseExitEvent& event) override;
    bool attached(VSTGUI::CView* parent) override;
    bool removed(VSTGUI::CView* parent) override;

private:
    // Geometry cache recomputed on construction and setViewSize().
    struct CellGeom
    {
        float cellW = 0.0f;
        float cellH = 0.0f;
    };
    [[nodiscard]] CellGeom cellGeom() const noexcept;

    PadGlowPublisher* glowPublisher_ = nullptr;
    PadMetaProvider   metaProvider_;
    SelectCallback    selectCallback_;
    AuditionCallback  auditionCallback_;

    int selectedPad_ = 0;

    std::array<std::uint8_t, kNumPads> glowBuckets_{};

    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> pollTimer_;
    VSTGUI::SharedPointer<VSTGUI::CGradient>    glowGradient_;
};

} // namespace Membrum::UI
