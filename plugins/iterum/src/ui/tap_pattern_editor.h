// =============================================================================
// TapPatternEditor - Custom Tap Pattern Visual Editor
// =============================================================================
// Spec 046: Custom Tap Pattern Editor, redesigned by specs/iterum-tap-editor/design.md.
// Visual editor for the MultiTap custom delay pattern: drag a tap's time and level. The view is tagged 920, which
// is not a parameter: every write goes through the TapEditSink the controller installs (begin / perform / end on
// the tap's own time or level parameter, design 3.5). The controller pushes every parameter change into the view
// through its parameter bridge (design 3.6). For the 19 preset patterns the view shows the pattern read-only
// (design 5.9).
// =============================================================================

#pragma once

#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/vstkeycode.h"
#include "tap_pattern_editor_logic.h"
#include "plugin_ids.h"
#include <array>
#include <functional>
#include <string>

namespace VSTGUI {
class CTextLabel;
}

namespace Iterum {

/// The zone of a tap under a point (design 4.1).
enum class HitZone {
    None,    ///< no tap
    Body,    ///< a tall tap's bar below its handle: time drag
    Handle,  ///< a tall tap's top: level drag
    Grip     ///< a short or silent tap's single grip: the first move picks the axis
};

/// The tap and zone the hit test of design 4.1 finds.
struct TapHit {
    int tap = -1;
    HitZone zone = HitZone::None;
};

/// The three edit callbacks createCustomView installs (design 3.5); all run on the UI thread.
struct TapEditSink {
    std::function<void(Steinberg::Vst::ParamID)> begin;
    std::function<void(Steinberg::Vst::ParamID)> end;
    std::function<void(Steinberg::Vst::ParamID, float)> perform;
};

class TapPatternEditor : public VSTGUI::CControl {
public:
    /// Colour and font tokens, read from the uidesc by createCustomView (design 5.7). The defaults are the
    /// design-system 5.2 tokens, used when a description has no such token.
    struct Palette {
        VSTGUI::CColor sliderTrack{0x27, 0x27, 0x30, 0xff};
        VSTGUI::CColor border{0x30, 0x30, 0x3a, 0xff};
        VSTGUI::CColor sliderFrame{0x3a, 0x3a, 0x46, 0xff};
        VSTGUI::CColor accent{0x5a, 0xa0, 0xe6, 0xff};
        VSTGUI::CColor accentDim{0x2e, 0x5a, 0x88, 0xff};
        VSTGUI::CColor text{0xe6, 0xe6, 0xec, 0xff};
        VSTGUI::CColor textDim{0x9c, 0x9c, 0xaa, 0xff};
        VSTGUI::CColor textLight{0xff, 0xff, 0xff, 0xff};
        VSTGUI::CColor background{0x12, 0x12, 0x16, 0xff};
        VSTGUI::SharedPointer<VSTGUI::CFontDesc> valueFont;  ///< null: an 11 pt Arial is used
    };

    explicit TapPatternEditor(const VSTGUI::CRect& size);
    ~TapPatternEditor() override;

    // CControl overrides
    void draw(VSTGUI::CDrawContext* context) override;
    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override;
    VSTGUI::CMouseEventResult onMouseMoved(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override;
    VSTGUI::CMouseEventResult onMouseUp(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override;
    VSTGUI::CMouseEventResult onMouseCancel() override;
    VSTGUI::CMouseEventResult onMouseExited(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override;
    int32_t onKeyDown(VstKeyCode& keyCode) override;
    bool removed(VSTGUI::CView* parent) override;

    /// Ctrl/Cmd-click resets a tap's level, ahead of CControl's default-value listener (design 4.2, ruling 5)
    void dispatchEvent(VSTGUI::Event& event) override;

    // Tap data (pushed by the controller's bridge; a set of the dragged tap's own ids is ignored, design 3.6)
    void setTapTimeRatio(size_t tapIndex, float ratio);
    void setTapLevel(size_t tapIndex, float level);
    float getTapTimeRatio(size_t tapIndex) const;
    float getTapLevel(size_t tapIndex) const;

    /// Reveal or hide taps: writes nothing (design 3.2, 3.3)
    void setActiveTapCount(size_t count);
    size_t getActiveTapCount() const { return activeTapCount_; }

    /// The taps one user gesture on the Taps slider revealed, [oldCount, newCount): a tap whose stored place is
    /// taken moves to the nearest free grid line, bracketed (design 3.3)
    void placeRevealedTaps(size_t oldCount, size_t newCount);

    /// The edit callbacks (design 3.5)
    void setEditSink(TapEditSink sink) { sink_ = std::move(sink); }

    /// Colour and font tokens (design 5.7)
    void setPalette(const Palette& palette) { palette_ = palette; invalid(); }
    const Palette& getPalette() const { return palette_; }

    // Grid snapping
    void setSnapDivision(SnapDivision division) { snapDivision_ = division; invalid(); }
    SnapDivision getSnapDivision() const { return snapDivision_; }

    /// The custom pattern's span (design 2.1)
    void setCustomLength(CustomLength length);
    CustomLength getCustomLength() const { return customLength_; }

    /// The tempo the processor last reported (MultiTap Tempo, 915); 120 BPM until the first value (design 2.4)
    void setTempo(double bpm);
    double getTempo() const { return tempoBpm_; }

    /// The read-only display of a preset pattern, on the Length Copy would choose (design 5.9)
    void setPresetPattern(const std::array<float, kMaxPatternTaps>& ratios,
                          const std::array<float, kMaxPatternTaps>& levels, size_t count, CustomLength length);
    CustomLength getPresetLength() const { return presetLength_; }
    size_t getPresetTapCount() const { return presetCount_; }
    float getPresetTapTimeRatio(size_t tapIndex) const;
    /// The preset tap's time in ms at the view's tempo, capped at 5000 ms
    float getPresetTapTimeMs(size_t tapIndex) const;
    float getPresetTapLevel(size_t tapIndex) const;

    /// Editable for Custom, read-only for every other pattern (design 5.9)
    void setEditable(bool editable);
    bool isEditable() const { return editable_; }

    /// Reset: every active tap to D(i, n) at 0 dB, both parameters bracketed; inactive taps untouched (design 3.4)
    void resetToDefault();

    /// The tap and zone under a view-local point (design 4.1)
    TapHit hitZoneAt(float x, float y) const;

    /// The cursor of a zone (design 4.3): body horizontal, handle vertical, grip all-way, else and read-only default
    static VSTGUI::CCursorType cursorForZone(HitZone zone, bool editable);

    CLASS_METHODS(TapPatternEditor, CControl)

private:
    /// What a drag moves (design 4.2): a grip waits for its first move to pick Time or Level.
    enum class DragMode { None, Time, Level, GripPending };

    // Drawing helpers (view-local coordinates)
    void drawBackground(VSTGUI::CDrawContext* context);
    void drawLevelAxis(VSTGUI::CDrawContext* context) const;
    void drawGrid(VSTGUI::CDrawContext* context, CustomLength length, bool withSnap);
    void drawRuler(VSTGUI::CDrawContext* context, CustomLength length, bool withSnap);
    void drawTaps(VSTGUI::CDrawContext* context);
    void drawTempo(VSTGUI::CDrawContext* context) const;

    /// The font every text uses (value-font, or the held fallback)
    VSTGUI::CFontDesc* font() const;

    /// The view-local point of a point in the parent's coordinates (what the mouse handlers receive)
    VSTGUI::CPoint toLocal(const VSTGUI::CPoint& where) const;

    /// The 4.1 hit test over any tap set
    static TapHit hitTest(const float* ratios, const float* levels, size_t count, float x, float y);

    // Hover, cursor and readout
    void updateHover(float x, float y);
    void showReadout(size_t tapIndex);
    void hideReadout();

    // Drag
    void applyDragMove(float x, float y, bool shift);
    void finishDrag();
    void cancelDrag();

    // Writes through the sink
    void sinkBegin(Steinberg::Vst::ParamID id);
    void sinkPerform(Steinberg::Vst::ParamID id, float value);
    void sinkEnd(Steinberg::Vst::ParamID id);
    void writeBracketed(Steinberg::Vst::ParamID id, float value);

    [[nodiscard]] static Steinberg::Vst::ParamID timeId(size_t i) {
        return kMultiTapCustomTime0Id + static_cast<Steinberg::Vst::ParamID>(i);
    }
    [[nodiscard]] static Steinberg::Vst::ParamID levelId(size_t i) {
        return kMultiTapCustomLevel0Id + static_cast<Steinberg::Vst::ParamID>(i);
    }

    // Tap data
    std::array<float, kMaxPatternTaps> tapTimeRatios_{};
    std::array<float, kMaxPatternTaps> tapLevels_{};
    size_t activeTapCount_ = 4;

    // Read-only display of a preset pattern
    std::array<float, kMaxPatternTaps> presetRatios_{};
    std::array<float, kMaxPatternTaps> presetLevels_{};
    size_t presetCount_ = 0;
    CustomLength presetLength_ = CustomLength::Free;
    bool editable_ = true;

    // Time axis
    CustomLength customLength_ = CustomLength::N1;
    double tempoBpm_ = 120.0;

    // Drag state (design 4.2)
    int dragTap_ = -1;
    DragMode dragMode_ = DragMode::None;
    bool timeBegun_ = false;
    bool levelBegun_ = false;
    bool deadZoneCrossed_ = false;
    bool shiftHeld_ = false;
    float downX_ = 0.0f;           ///< press point, view-local: the dead zone and the grip axis
    float downY_ = 0.0f;
    float baseX_ = 0.0f;           ///< re-based on a Shift change
    float baseY_ = 0.0f;
    float baseRatio_ = 0.0f;
    float baseLevel_ = 0.0f;
    float preDragTimeRatio_ = 0.0f;
    float preDragLevel_ = 0.0f;

    // Hover state
    int hoveredTap_ = -1;
    HitZone hoveredZone_ = HitZone::None;

    // The popup readout (design 4.3): a label added to the frame
    VSTGUI::CTextLabel* readout_ = nullptr;

    // Edit callbacks
    TapEditSink sink_;

    // Colour and font tokens
    Palette palette_;
    VSTGUI::SharedPointer<VSTGUI::CFontDesc> fallbackFont_;

    // Grid snapping
    SnapDivision snapDivision_ = SnapDivision::N4;
};

} // namespace Iterum
