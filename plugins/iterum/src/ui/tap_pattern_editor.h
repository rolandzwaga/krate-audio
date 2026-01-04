// =============================================================================
// TapPatternEditor - Custom Tap Pattern Visual Editor
// =============================================================================
// Spec 046: Custom Tap Pattern Editor
// Visual editor for creating custom delay tap patterns by dragging tap
// positions (time) and levels. Uses CControl for VST3 parameter binding.
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

namespace Iterum {

class TapPatternEditor : public VSTGUI::CControl {
public:
    explicit TapPatternEditor(const VSTGUI::CRect& size);
    ~TapPatternEditor() override = default;

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
    int32_t onKeyDown(VstKeyCode& keyCode) override;

    // Tap data accessors
    void setTapTimeRatio(size_t tapIndex, float ratio);
    void setTapLevel(size_t tapIndex, float level);
    float getTapTimeRatio(size_t tapIndex) const;
    float getTapLevel(size_t tapIndex) const;

    // Tap count
    void setActiveTapCount(size_t count);
    size_t getActiveTapCount() const { return activeTapCount_; }

    // Parameter update callback (called when user drags a tap)
    using ParameterCallback = std::function<void(
        Steinberg::Vst::ParamID paramId, float normalizedValue)>;
    void setParameterCallback(ParameterCallback cb) { paramCallback_ = std::move(cb); }

    // Notify when pattern is updated externally
    void invalidatePattern() { invalid(); }

    // Pattern change notification (T031.7)
    // Called when timing pattern changes - cancels drag if pattern != Custom
    void onPatternChanged(int patternIndex);

    // Grid snapping (Phase 5 - User Story 3)
    void setSnapDivision(SnapDivision division) { snapDivision_ = division; }
    SnapDivision getSnapDivision() const { return snapDivision_; }

    // Check if currently in Custom pattern mode
    static constexpr int kCustomPatternIndex = 19;

    CLASS_METHODS(TapPatternEditor, CControl)

private:
    // Drawing helpers
    void drawBackground(VSTGUI::CDrawContext* context);
    void drawGridLines(VSTGUI::CDrawContext* context);
    void drawTaps(VSTGUI::CDrawContext* context);
    void drawLabels(VSTGUI::CDrawContext* context);

    // Coordinate conversion (using logic functions)
    float xToTimeRatio(float x) const;
    float yToLevel(float y) const;
    float timeRatioToX(float ratio) const;
    float levelToY(float level) const;

    // Hit testing
    int hitTestTapAtPoint(float x, float y) const;

    // Parameter notification
    void notifyTimeRatioChanged(size_t tapIndex, float ratio);
    void notifyLevelChanged(size_t tapIndex, float level);

    // Cancel current drag operation
    void cancelDrag();

    // Tap data
    std::array<float, kMaxPatternTaps> tapTimeRatios_{};
    std::array<float, kMaxPatternTaps> tapLevels_{};
    size_t activeTapCount_ = 4;

    // Drag state
    int selectedTap_ = -1;
    bool isDragging_ = false;
    float preDragTimeRatio_ = 0.0f;
    float preDragLevel_ = 0.0f;
    float dragStartX_ = 0.0f;
    float dragStartY_ = 0.0f;

    // Callback for parameter updates
    ParameterCallback paramCallback_;

    // Grid snapping (Phase 5)
    SnapDivision snapDivision_ = SnapDivision::Off;

    // Colors
    static constexpr VSTGUI::CColor kBackgroundColor{35, 35, 38, 255};
    static constexpr VSTGUI::CColor kBorderColor{60, 60, 65, 255};
    static constexpr VSTGUI::CColor kGridColor{50, 50, 55, 255};
    static constexpr VSTGUI::CColor kTapColor{80, 140, 200, 255};
    static constexpr VSTGUI::CColor kTapSelectedColor{120, 180, 240, 255};
    static constexpr VSTGUI::CColor kTextColor{180, 180, 185, 255};
};

} // namespace Iterum
