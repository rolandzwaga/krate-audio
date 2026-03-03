// ==============================================================================
// StepPatternEditor API Contract (046-step-pattern-editor)
// ==============================================================================
// This file documents the public API of the StepPatternEditor component.
// It is NOT compiled -- it serves as the implementation contract.
// ==============================================================================

#pragma once

#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cvstguitimer.h"

#include <array>
#include <bitset>
#include <cstdint>
#include <functional>
#include <random>

namespace Krate::Plugins {

class StepPatternEditor : public VSTGUI::CControl {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr int kMaxSteps = 32;
    static constexpr int kMinSteps = 2;

    // =========================================================================
    // Construction
    // =========================================================================

    /// @brief Create a new StepPatternEditor
    /// @param size View rectangle
    /// @param listener Control listener (typically nullptr for shared components)
    /// @param tag Control tag (typically -1; multi-param via callback)
    StepPatternEditor(const VSTGUI::CRect& size,
                      VSTGUI::IControlListener* listener,
                      int32_t tag);

    /// @brief Copy constructor (required by VSTGUI ViewCreator)
    StepPatternEditor(const StepPatternEditor& other);

    // =========================================================================
    // Step Level API (FR-001, FR-005, FR-006, FR-012)
    // =========================================================================

    /// @brief Set a single step's level
    /// @param index Step index [0, kMaxSteps)
    /// @param level Gain level [0.0, 1.0]
    void setStepLevel(int index, float level);

    /// @brief Get a single step's level
    /// @param index Step index [0, kMaxSteps)
    /// @return Level [0.0, 1.0]
    [[nodiscard]] float getStepLevel(int index) const;

    // =========================================================================
    // Step Count API (FR-013, FR-015, FR-016)
    // =========================================================================

    /// @brief Set the active step count
    /// @param count Number of active steps [kMinSteps, kMaxSteps]
    void setNumSteps(int count);

    /// @brief Get the active step count
    [[nodiscard]] int getNumSteps() const;

    // =========================================================================
    // Playback API (FR-024, FR-025, FR-026, FR-027)
    // =========================================================================

    /// @brief Set the currently playing step index
    /// @param step Step index [0, numSteps-1], or -1 for none
    void setPlaybackStep(int step);

    /// @brief Set transport playing state (starts/stops refresh timer)
    /// @param playing true if transport is playing
    void setPlaying(bool playing);

    // =========================================================================
    // Phase Offset API (FR-028)
    // =========================================================================

    /// @brief Set the phase offset for start position indicator
    /// @param offset Normalized offset [0.0, 1.0]
    void setPhaseOffset(float offset);

    // =========================================================================
    // Euclidean Mode API (FR-018 through FR-023)
    // =========================================================================

    /// @brief Enable or disable Euclidean mode
    void setEuclideanEnabled(bool enabled);

    /// @brief Set Euclidean hit count
    /// @param hits Number of pulses [0, numSteps]
    void setEuclideanHits(int hits);

    /// @brief Set Euclidean rotation
    /// @param rotation Rotation offset [0, numSteps-1]
    void setEuclideanRotation(int rotation);

    // =========================================================================
    // Parameter Callback (FR-012, FR-037)
    // =========================================================================

    /// @brief Callback type for parameter change notifications
    /// @param paramId The parameter ID that changed
    /// @param normalizedValue The new normalized value [0.0, 1.0]
    using ParameterCallback = std::function<void(
        Steinberg::Vst::ParamID paramId, float normalizedValue)>;

    /// @brief Set the callback for parameter changes
    /// @details Called during user interaction for each step level change.
    ///          The controller wires this to performEdit() on the appropriate
    ///          parameter IDs.
    void setParameterCallback(ParameterCallback cb);

    /// @brief Callback type for begin/end edit notifications
    /// @param paramId The parameter ID
    using EditCallback = std::function<void(Steinberg::Vst::ParamID paramId)>;

    /// @brief Set callbacks for beginEdit/endEdit notifications
    void setBeginEditCallback(EditCallback cb);
    void setEndEditCallback(EditCallback cb);

    /// @brief Set the base parameter ID for step levels
    /// @details Step i uses paramId = baseId + i
    void setStepLevelBaseParamId(Steinberg::Vst::ParamID baseId);

    // =========================================================================
    // Preset / Transform API (FR-029, FR-030, FR-031)
    // Called by external quick-action buttons via controller wiring.
    // Each method issues beginEdit/performEdit/endEdit for all affected steps.
    // =========================================================================

    void applyPresetAll();        ///< All steps to 1.0
    void applyPresetOff();        ///< All steps to 0.0
    void applyPresetAlternate();  ///< Alternate 1.0/0.0
    void applyPresetRampUp();     ///< Linear 0.0 to 1.0
    void applyPresetRampDown();   ///< Linear 1.0 to 0.0
    void applyPresetRandom();     ///< Uniform random 0.0-1.0
    void applyTransformInvert();  ///< Each level = 1.0 - level
    void applyTransformShiftRight(); ///< Circular rotation right
    void applyTransformShiftLeft();  ///< Circular rotation left

    /// @brief Reset to pure Euclidean pattern (FR-023)
    /// Called by external Regen button.
    void regenerateEuclidean();

    // =========================================================================
    // Color Configuration (FR-036)
    // Configurable via ViewCreator attributes for uidesc theming.
    // Default colors from roadmap: accent=rgb(220,170,60), normal=rgb(80,140,200),
    // ghost=rgb(60,90,120), silent=rgb(50,50,55), background=rgb(35,35,38)
    // =========================================================================

    void setBarColorAccent(VSTGUI::CColor color);   ///< Default: rgb(220,170,60)
    [[nodiscard]] VSTGUI::CColor getBarColorAccent() const;

    void setBarColorNormal(VSTGUI::CColor color);   ///< Default: rgb(80,140,200)
    [[nodiscard]] VSTGUI::CColor getBarColorNormal() const;

    void setBarColorGhost(VSTGUI::CColor color);    ///< Default: rgb(60,90,120)
    [[nodiscard]] VSTGUI::CColor getBarColorGhost() const;

    void setSilentOutlineColor(VSTGUI::CColor color); ///< Default: rgb(50,50,55)
    [[nodiscard]] VSTGUI::CColor getSilentOutlineColor() const;

    void setGridColor(VSTGUI::CColor color);
    [[nodiscard]] VSTGUI::CColor getGridColor() const;

    void setEditorBackgroundColor(VSTGUI::CColor color); ///< Default: rgb(35,35,38)
    [[nodiscard]] VSTGUI::CColor getEditorBackgroundColor() const;

    void setPlaybackColor(VSTGUI::CColor color);
    [[nodiscard]] VSTGUI::CColor getPlaybackColor() const;

    void setTextColor(VSTGUI::CColor color);
    [[nodiscard]] VSTGUI::CColor getTextColor() const;

    // =========================================================================
    // CControl Overrides
    // =========================================================================

    void draw(VSTGUI::CDrawContext* context) override;

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;
    VSTGUI::CMouseEventResult onMouseMoved(
        VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;
    VSTGUI::CMouseEventResult onMouseUp(
        VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;
    VSTGUI::CMouseEventResult onMouseCancel() override;

    bool onWheel(const VSTGUI::CPoint& where, const VSTGUI::CMouseWheelAxis& axis,
                 const float& distance, const VSTGUI::CButtonState& buttons) override;

    int32_t onKeyDown(VstKeyCode& keyCode) override;

    CLASS_METHODS(StepPatternEditor, CControl)
};

} // namespace Krate::Plugins
