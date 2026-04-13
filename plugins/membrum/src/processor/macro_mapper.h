// ==============================================================================
// MacroMapper -- Control-Rate Macro-to-Parameter Driver (Phase 6)
// ==============================================================================
// Spec: specs/141-membrum-phase6-ui/spec.md (FR-022, FR-023)
// Contract: specs/141-membrum-phase6-ui/contracts/macro_mapper.h
// Data model: specs/141-membrum-phase6-ui/data-model.md section 3
//
// Maps the five per-pad macros (Tightness, Brightness, Body Size, Punch,
// Complexity) onto underlying voice parameters using delta-from-registered-
// default arithmetic. macro == 0.5 -> zero delta. Pure arithmetic, no
// allocations/locks/exceptions.
// ==============================================================================

#pragma once

#include "dsp/pad_config.h"

#include <array>
#include <cstdint>

namespace Membrum {

struct RegisteredDefaultsTable
{
    // Indexed by PadParamOffset (0..kPadActiveParamCountV6-1).
    std::array<float, 64> byOffset{};
};

class MacroMapper
{
public:
    MacroMapper() noexcept = default;

    /// UI-thread: cache the registered-default table. Must be called before
    /// any apply() / reapplyAll(). Also resets the per-pad macro cache so
    /// the next apply() forces a full recompute.
    void prepare(const RegisteredDefaultsTable& defaults) noexcept;

    /// Audio-thread: apply pad `padIndex`'s macros to its PadConfig. Early-
    /// outs if cached macro values equal live values. No allocations.
    void apply(int padIndex, PadConfig& padConfig) noexcept;

    /// Audio-thread (or during kit-preset load): force a full refresh
    /// across all 32 pads.
    void reapplyAll(std::array<PadConfig, kNumPads>& pads) noexcept;

    /// Audio-thread: true if padIndex's cached macros differ from its
    /// current PadConfig macro fields.
    [[nodiscard]] bool isDirty(int padIndex, const PadConfig& padConfig) const noexcept;

    /// UI-thread (tests only): reset the cache to "never applied".
    void invalidateCache() noexcept;

    /// Test-only: true if prepare() has been called.
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

private:
    struct PadCache
    {
        float tightness  = -1.0f;  // sentinel: force first-apply
        float brightness = -1.0f;
        float bodySize   = -1.0f;
        float punch      = -1.0f;
        float complexity = -1.0f;
    };

    RegisteredDefaultsTable      defaults_{};
    std::array<PadCache, kNumPads> cache_{};
    bool                         prepared_ = false;

    void applyTightness(PadConfig& cfg) noexcept;
    void applyBrightness(PadConfig& cfg) noexcept;
    void applyBodySize(PadConfig& cfg) noexcept;
    void applyPunch(PadConfig& cfg) noexcept;
    void applyComplexity(PadConfig& cfg) noexcept;
};

// ==============================================================================
// Delta curve constants (data-model.md section 3 table)
// ==============================================================================

namespace MacroCurves {
    // Tightness
    inline constexpr float kTightnessMaterialSpan    = 0.30f;
    inline constexpr float kTightnessDecaySpan       = 0.25f;
    inline constexpr float kTightnessDecaySkewSpan   = 1.00f;

    // Brightness
    inline constexpr float kBrightnessCutoffSpan     = 0.40f;
    inline constexpr float kBrightnessModeInjectSpan = 0.30f;

    // Body Size
    inline constexpr float kBodySizeSizeSpan         = 0.40f;
    inline constexpr float kBodySizeStretchSpan      = 0.20f;
    inline constexpr float kBodySizeDecayScaleSpan   = 0.20f;

    // Punch
    inline constexpr float kPunchPitchEnvDepthSpan   = 0.50f;
    inline constexpr float kPunchPitchEnvTimeSpan    = 0.40f;
    inline constexpr float kPunchExciterAttackSpan   = 0.30f;

    // Complexity
    inline constexpr float kComplexityCouplingSpan   = 0.50f;
    inline constexpr float kComplexityNonlinearSpan  = 0.30f;
    inline constexpr float kComplexityModeInjectSpan = 0.30f;
}

} // namespace Membrum
