// ==============================================================================
// MacroMapper -- Control-Rate Macro-to-Parameter Driver (Phase 6 Contract)
// ==============================================================================
// Spec: specs/141-membrum-phase6-ui/spec.md (FR-022, FR-023)
// Plan: specs/141-membrum-phase6-ui/plan.md
// Data model: specs/141-membrum-phase6-ui/data-model.md section 3
//
// Purpose:
//   For each pad, maps the five per-pad macro values (Tightness, Brightness,
//   Body Size, Punch, Complexity) onto documented target parameters using
//   delta-from-registered-default arithmetic (Clarification #1, 2026-04-12).
//
// Threading contract:
//   - prepare() is called on the component thread (during Processor::initialize(),
//     before setActive(true)). Not audio-thread safe.
//   - apply() / reapplyAll() are called ONLY on the audio thread inside
//     Processor::processParameterChanges(). They MUST NOT allocate, lock, or
//     throw.
//
// Delta semantics:
//   For every target parameter T of a macro M:
//     effective(T) = clamp(registeredDefault(T) + delta_M(macro), 0.0f, 1.0f)
//   macro == 0.5f  -> delta = 0  -> T sits at its registered default.
//   macro == 0.0f  -> defined negative delta.
//   macro == 1.0f  -> defined positive delta.
// ==============================================================================

#pragma once

#include "dsp/pad_config.h"
#include <array>
#include <cstdint>

namespace Membrum {

struct RegisteredDefaultsTable
{
    // Indexed by PadParamOffset (0..kPadActiveParamCountV6-1). Only entries
    // referenced by macros are actually read; others are ignored.
    std::array<float, 64> byOffset{};
};

class MacroMapper
{
public:
    MacroMapper() noexcept = default;

    // UI-thread: cache registered defaults for all macro target parameters.
    // Must be called before any apply() / reapplyAll().
    void prepare(const RegisteredDefaultsTable& defaults) noexcept;

    // Audio-thread: apply the five macros of pad `padIndex` to its PadConfig.
    // Early-outs if cached macro values equal live values (no work needed).
    // No allocations, no locks, no exceptions.
    void apply(int padIndex, PadConfig& padConfig) noexcept;

    // Audio-thread (or UI-thread during kit-preset load followed by processor
    // restart): force a full refresh across all 32 pads. Used after kit preset
    // load to recompute every underlying parameter from the loaded macro state.
    void reapplyAll(std::array<PadConfig, 32>& pads) noexcept;

    // Audio-thread: true if padIndex's cached macros differ from its current
    // PadConfig macros (i.e., apply() would do work).
    [[nodiscard]] bool isDirty(int padIndex) const noexcept;

    // UI-thread (tests only): reset the cache so the next apply() forces work.
    void invalidateCache() noexcept;

private:
    // Cache entries match PadConfig macro fields verbatim.
    struct PadCache
    {
        float tightness  = 0.5f;
        float brightness = 0.5f;
        float bodySize   = 0.5f;
        float punch      = 0.5f;
        float complexity = 0.5f;
    };

    RegisteredDefaultsTable defaults_{};
    std::array<PadCache, 32> cache_{};
    bool prepared_ = false;

    // Per-macro appliers. Each reads the macro value from `src`, computes
    // deltas against `defaults_`, and writes effective values into `dst` fields.
    // `src` and `dst` may refer to the same PadConfig (in-place update).
    void applyTightness(const PadConfig& src, PadConfig& dst) noexcept;
    void applyBrightness(const PadConfig& src, PadConfig& dst) noexcept;
    void applyBodySize(const PadConfig& src, PadConfig& dst) noexcept;
    void applyPunch(const PadConfig& src, PadConfig& dst) noexcept;
    void applyComplexity(const PadConfig& src, PadConfig& dst) noexcept;
};

// ==============================================================================
// Delta curve constants (final values tuned via listening tests; initial values
// documented in data-model.md section 3)
// ==============================================================================

namespace MacroCurves {
    // Tightness
    inline constexpr float kTightnessMaterialSpan    = 0.30f;  // linear
    inline constexpr float kTightnessDecaySpan       = 0.25f;  // exp, inverted
    inline constexpr float kTightnessDecaySkewSpan   = 1.00f;  // linear

    // Brightness
    inline constexpr float kBrightnessCutoffSpan     = 0.40f;  // exp in log-Hz
    inline constexpr float kBrightnessModeInjectSpan = 0.30f;  // linear

    // Body Size
    inline constexpr float kBodySizeSizeSpan         = 0.40f;
    inline constexpr float kBodySizeStretchSpan      = 0.20f;
    inline constexpr float kBodySizeDecayScaleSpan   = 0.20f;

    // Punch
    inline constexpr float kPunchPitchEnvDepthSpan   = 0.50f;  // exp
    inline constexpr float kPunchPitchEnvTimeSpan    = 0.40f;  // inverse-exp
    inline constexpr float kPunchExciterAttackSpan   = 0.30f;  // linear

    // Complexity
    inline constexpr float kComplexityCouplingSpan   = 0.50f;
    inline constexpr float kComplexityNonlinearSpan  = 0.30f;
    inline constexpr float kComplexityModeInjectSpan = 0.30f;  // proxy for mode count
}

} // namespace Membrum
