// ==============================================================================
// Layer 2: DSP Processor — Profundum Phase 1 (specs/profundum-phase1-harmonic-core)
// ==============================================================================
// SpectralShapeRecipe: the harmonic-amplitude recipe (shape law, low-note guard
// and cap mask) that drives ProfundumCore's additive bank.
//   Spec:  specs/profundum-phase1-harmonic-core/spec.md
//   Plan:  specs/profundum-phase1-harmonic-core/plan.md (S3)
//   Tasks: specs/profundum-phase1-harmonic-core/tasks.md
//
// Stateless, header-only, pure math (FR-001): every entry point is static,
// noexcept, allocation-free and touches no global state (FR-004).
//
// The Low Note Guard is a fixed property of the recipe (FR-030): no entry
// point takes a guard argument and Controls holds no note-behaviour field
// (FR-002).
//
// Calibration model: specs/profundum-phase1-harmonic-core/recipe-model.js
// (plan S3.6). Every constant below is mirrored there.
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>              // detail::isFinite (L0)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>

namespace Krate::DSP {

/// Largest partial count the recipe writes. Layer 2 may not include the bank's
/// processors/harmonic_types.h (same layer), so the value is restated here and
/// ProfundumCore (Layer 3) static_asserts it against kMaxPartials.
inline constexpr std::size_t kRecipeMaxPartials = 96;

class SpectralShapeRecipe {
public:
    /// FR-002: exactly six fields. Plain aggregate (Phase 8 interpolates it).
    struct Controls {
        float depth = 0.5f;          // [0, 1]
        float body = 0.5f;           // [0, 1]
        float bodyCurvature = 0.5f;  // [0, 1]
        float bodyEmphasis = 0.0f;   // [-1, 0]
        float edge = 0.5f;           // [0, 1]
        float shift = 0.0f;          // [-1, +1]
    };

    // ---- normalisation (FR-005, FR-049) ----
    static constexpr float kCoreOutputRmsDb = -12.0f;
    static constexpr float kCenterPanGain = 0.7071067811865476f;   // == bank reset() literal
    static constexpr float kPowerTarget = 0.25238293779207720f;     // P0 = 4·10^(-1.2)
    // ---- base law (FR-018) ----
    static constexpr float kDepthTriangle = 0.5f;                   // p(0.5) == 2 exactly
    static constexpr float kSineResidual = 1.0e-3f;                 // R(1)
    // ---- envelopes (FR-011..FR-016) ----
    static constexpr float kBodyCentreOct = 1.75f;
    static constexpr float kEdgeCentreOct = 3.80f;
    static constexpr float kShiftCentreOct = 0.75f;
    static constexpr float kShiftGainOct = 3.0f;
    static constexpr float kBodyWidthBroadOct = 0.45f;
    static constexpr float kBodyWidthNarrowOct = 0.15f;
    static constexpr float kBodyWidthRefOct = 0.60f;
    static constexpr float kBodyPeakExponent = 1.5f;
    static constexpr float kEdgeFlankOct = 0.35f;
    static constexpr float kBodyGain = 18.0f;
    static constexpr float kEdgeGain = 50.0f;
    // ---- mask (FR-030, FR-032) ----
    static constexpr float kLowNoteGuardOnsetHz = 32.70f;           // C1
    static constexpr float kGuardSlopePerOctave = 0.5f;
    static constexpr float kNyquistCapFraction = 0.8f;              // == bank kAntiAliasFadeStart
    static constexpr float kCapTaperCents = 1430.0f;                // taper width below capHz (C-8)
    static constexpr float kCapTailCents = 10.0f;                   // final linear-amplitude segment to 0
    static constexpr float kCapTaperDbPerCent = 0.09f;              // dB-linear slope above the tail (SC-011(c) bar 0.1)
    // ---- documented Lipschitz ceilings (FR-006), in units of sqrt(P0) ----
    static constexpr float kLipschitzControlCeiling = 8.0f;         // measured 7.13
    static constexpr float kLipschitzOctaveCeiling = 8.0f;          // measured 4.78

    // Named coordinates (FR-014, FR-020) -- declared here, defined constexpr after the
    // class (plan S3.5). Clang and GCC reject in-class nested-aggregate NSDMIs (plan R-7).
    static const Controls kDefaultControls;
    static const Controls kSineAnchor, kTriangleAnchor, kSawAnchor;
    static const Controls kHeavy, kHollow, kGrowl;
    static const Controls kBodyRound, kBodyHollow, kBodyWoody, kBodyNasal, kBodyThick;

    /// Clamp to ranges; any non-finite field -> kDefaultControls' field (FR-002).
    /// @note Real-time safe
    [[nodiscard]] static Controls sanitize(const Controls& c) noexcept;
    /// p(depth) = 1 + 2*depth (FR-018).
    [[nodiscard]] static constexpr float baseExponent(float depth) noexcept { return 1.0f + 2.0f * depth; }
    /// kNyquistCapFraction * fs / 2 -- also ProfundumCore's f0 clamp ceiling (FR-062).
    [[nodiscard]] static float capFrequency(double sampleRate) noexcept {
        return kNyquistCapFraction * static_cast<float>(sampleRate) * 0.5f;
    }

    /// Stage 1 (FR-051): P0-normalised shape, no guard, no cap, f0-independent. out.size() = N in [1, 96].
    /// Writes min(out.size(), kRecipeMaxPartials) elements; anything beyond is left untouched.
    /// @note Real-time safe
    static void evaluateShape(const Controls& c, std::span<float> out) noexcept {
        const std::size_t n = std::min(out.size(), kRecipeMaxPartials);
        if (n == 0)
            return;
        const Controls s = sanitize(c);

        // Plan S3.2, per-vector terms.
        const float p = baseExponent(s.depth);
        const float x = std::clamp((s.depth - kDepthTriangle) / (1.0f - kDepthTriangle), 0.0f, 1.0f);
        const float residual = 1.0f - (1.0f - kSineResidual) * (x * x * (3.0f - 2.0f * x));
        const float b = s.body * s.body;
        const float uB = kBodyCentreOct + kShiftCentreOct * s.shift;
        const float uE = kEdgeCentreOct + kShiftCentreOct * s.shift;
        const float sigmaB = kBodyWidthBroadOct + (kBodyWidthNarrowOct - kBodyWidthBroadOct) * s.bodyCurvature;
        const float comp = std::exp2(kShiftGainOct * s.shift);
        const float peak = std::pow(kBodyWidthRefOct / sigmaB, kBodyPeakExponent);
        const float bodyScale = kBodyGain * b * peak;
        const float edgeScale = kEdgeGain * s.edge;
        const float bodyDen = 2.0f * sigmaB * sigmaB;
        constexpr float kEdgeDen = 2.0f * kEdgeFlankOct * kEdgeFlankOct;
        const float evenPar = 1.0f + s.bodyEmphasis;

        // g1 = 1 (h1 floor > 0, FR-005); g_n for n = 2..N.
        out[0] = 1.0f;
        double power = 1.0;
        for (std::size_t i = 1; i < n; ++i) {
            const std::size_t harmonic = i + 1;
            const float u = std::log2(static_cast<float>(harmonic));
            const float duB = u - uB;
            const float gB = std::exp(-(duB * duB) / bodyDen);
            const float duE = u - uE;
            const float gE = u >= uE ? 1.0f : std::exp(-(duE * duE) / kEdgeDen);
            const float env = 1.0f + comp * (bodyScale * gB + edgeScale * gE);
            const float par = (harmonic % 2 == 0) ? evenPar : 1.0f;
            const float g = residual * std::exp2(-p * u) * env * par;
            out[i] = g;
            power += static_cast<double>(g) * static_cast<double>(g);
        }

        // a_n = g_n * sqrt(P0 / sum g^2), the sum in double (power >= 1, never 0).
        const double scale = std::sqrt(static_cast<double>(kPowerTarget) / power);
        for (std::size_t i = 0; i < n; ++i)
            out[i] = static_cast<float>(static_cast<double>(out[i]) * scale);
    }
    /// Stage 2 factors in [0, 1]: guard taper x cap taper; h1 factor is always 1 (FR-030, FR-032).
    /// Plan S3.3. A non-finite or non-positive f0Hz is treated as the guard onset (identity
    /// guard; the cap still applies at that f0): ProfundumCore sanitises f0 first, this only
    /// guarantees the recipe never writes NaN. Writes min(out.size(), kRecipeMaxPartials) elements.
    /// @note Real-time safe
    static void evaluateMask(float f0Hz, double sampleRate, std::span<float> out) noexcept {
        const std::size_t n = std::min(out.size(), kRecipeMaxPartials);
        if (n == 0)
            return;
        const float f0 = (detail::isFinite(f0Hz) && f0Hz > 0.0f) ? f0Hz : kLowNoteGuardOnsetHz;

        // Guard: octBelow = max(0, log2(onset / f0)); 0 at and above C1 -> identity (FR-030/031).
        const float octBelow = std::max(0.0f, std::log2(kLowNoteGuardOnsetHz / f0));
        const bool guarded = octBelow > 0.0f;

        // Cap: dB-linear taper over kCapTaperCents below capHz, linear-amplitude tail over the
        // last kCapTailCents, exactly 0 at and above capHz (FR-032, plan S1 C-8).
        const float capHz = capFrequency(sampleRate);
        const float taperStartHz = capHz * std::exp2(-kCapTaperCents / 1200.0f);
        const float tailFloor = std::pow(10.0f, -kCapTaperDbPerCent * (kCapTaperCents - kCapTailCents) / 20.0f);

        out[0] = 1.0f;  // h1 exempt from both factors
        for (std::size_t i = 1; i < n; ++i) {
            const std::size_t harmonic = i + 1;
            const float nf = static_cast<float>(harmonic) * f0;
            float m = 1.0f;
            if (guarded)
                m = std::exp2(-kGuardSlopePerOctave * octBelow * std::log2(static_cast<float>(harmonic)));
            if (nf >= capHz) {
                m = 0.0f;
            } else if (nf > taperStartHz) {
                const float y = 1200.0f * std::log2(capHz / nf);  // cents below the cap, >= 0
                float cap = 1.0f;
                if (y < kCapTaperCents) {
                    cap = y > kCapTailCents
                              ? std::pow(10.0f, -kCapTaperDbPerCent * (kCapTaperCents - y) / 20.0f)
                              : tailFloor * (std::max(0.0f, y) / kCapTailCents);
                }
                m *= cap;
            }
            out[i] = m;
        }
    }
    /// out[n] = shape[n] * mask[n], renormalised to P0 (sum in double). out may alias shape.
    /// Writes min(shape.size(), mask.size(), out.size(), kRecipeMaxPartials) elements.
    /// @note Real-time safe
    static void applyMask(std::span<const float> shape, std::span<const float> mask,
                          std::span<float> out) noexcept {
        const std::size_t n = std::min({shape.size(), mask.size(), out.size(), kRecipeMaxPartials});
        if (n == 0)
            return;
        double power = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const float o = shape[i] * mask[i];
            out[i] = o;
            power += static_cast<double>(o) * static_cast<double>(o);
        }
        // Recipe shapes have shape[0] > 0 and mask[0] == 1, so power >= shape[0]^2 > 0.
        if (!(power > 0.0))
            return;
        const double scale = std::sqrt(static_cast<double>(kPowerTarget) / power);
        for (std::size_t i = 0; i < n; ++i)
            out[i] = static_cast<float>(static_cast<double>(out[i]) * scale);
    }
    /// FR-001 full evaluation, literally evaluateShape -> evaluateMask -> applyMask. The mask
    /// lives in a stack std::array<float, kRecipeMaxPartials> (no allocation).
    /// @note Real-time safe
    static void evaluate(const Controls& c, float f0Hz, double sampleRate, std::span<float> out) noexcept {
        const std::size_t n = std::min(out.size(), kRecipeMaxPartials);
        if (n == 0)
            return;
        const std::span<float> head = out.first(n);
        evaluateShape(c, head);
        std::array<float, kRecipeMaxPartials> mask{};
        const std::span<float> maskHead(mask.data(), n);
        evaluateMask(f0Hz, sampleRate, maskHead);
        applyMask(head, maskHead, head);
    }
};

// ---- Named coordinates (plan S3.5; order: depth, body, bodyCurvature, bodyEmphasis, edge, shift) ----

/// = mid grid; FR-002 non-finite default.
inline constexpr SpectralShapeRecipe::Controls SpectralShapeRecipe::kDefaultControls{
    .depth = 0.5f, .body = 0.5f, .bodyCurvature = 0.5f, .bodyEmphasis = 0.0f, .edge = 0.5f, .shift = 0.0f};

/// SC-006: rest -77.6 dB re h1 (model).
inline constexpr SpectralShapeRecipe::Controls SpectralShapeRecipe::kSineAnchor{
    .depth = 1.0f, .body = 0.0f, .bodyCurvature = 0.5f, .bodyEmphasis = 0.0f, .edge = 0.0f, .shift = 0.0f};
/// SC-006: exact odd 1/n^2.
inline constexpr SpectralShapeRecipe::Controls SpectralShapeRecipe::kTriangleAnchor{
    .depth = 0.5f, .body = 0.0f, .bodyCurvature = 0.5f, .bodyEmphasis = -1.0f, .edge = 0.0f, .shift = 0.0f};
/// SC-006: exact 1/n.
inline constexpr SpectralShapeRecipe::Controls SpectralShapeRecipe::kSawAnchor{
    .depth = 0.0f, .body = 0.0f, .bodyCurvature = 0.5f, .bodyEmphasis = 0.0f, .edge = 0.0f, .shift = 0.0f};

/// SC-007: sub > body > pres, model margin 7.5 dB.
inline constexpr SpectralShapeRecipe::Controls SpectralShapeRecipe::kHeavy{
    .depth = 0.5f, .body = 0.3f, .bodyCurvature = 0.5f, .bodyEmphasis = 0.0f, .edge = 0.0f, .shift = -1.0f};
/// SC-007: body > sub > pres, model margin 25.7 dB.
inline constexpr SpectralShapeRecipe::Controls SpectralShapeRecipe::kHollow{
    .depth = 0.5f, .body = 1.0f, .bodyCurvature = 1.0f, .bodyEmphasis = 0.0f, .edge = 0.0f, .shift = 0.5f};
/// SC-007: pres > body > sub, model margin 12.8 dB.
inline constexpr SpectralShapeRecipe::Controls SpectralShapeRecipe::kGrowl{
    .depth = 0.0f, .body = 0.5f, .bodyCurvature = 0.0f, .bodyEmphasis = 0.0f, .edge = 1.0f, .shift = 1.0f};

/// SC-019: lowest C, +0.375 oct (model).
inline constexpr SpectralShapeRecipe::Controls SpectralShapeRecipe::kBodyRound{
    .depth = 0.2f, .body = 0.0f, .bodyCurvature = 0.1f, .bodyEmphasis = -0.8f, .edge = 0.1f, .shift = -0.4f};
/// SC-019: highest odd/even, +18.9 dB (model).
inline constexpr SpectralShapeRecipe::Controls SpectralShapeRecipe::kBodyHollow{
    .depth = 0.0f, .body = 0.25f, .bodyCurvature = 0.6f, .bodyEmphasis = -1.0f, .edge = 0.9f, .shift = 0.85f};
/// SC-019: pairwise distance.
inline constexpr SpectralShapeRecipe::Controls SpectralShapeRecipe::kBodyWoody{
    .depth = 0.65f, .body = 0.5f, .bodyCurvature = 0.95f, .bodyEmphasis = -0.1f, .edge = 0.3f, .shift = 0.05f};
/// SC-019: smallest sigma, +0.064 oct (model).
inline constexpr SpectralShapeRecipe::Controls SpectralShapeRecipe::kBodyNasal{
    .depth = 0.6f, .body = 0.7f, .bodyCurvature = 0.95f, .bodyEmphasis = -0.2f, .edge = 0.3f, .shift = -0.95f};
/// SC-019: highest R_body, +1.27 dB (model).
inline constexpr SpectralShapeRecipe::Controls SpectralShapeRecipe::kBodyThick{
    .depth = 0.5f, .body = 0.9f, .bodyCurvature = 0.05f, .bodyEmphasis = 0.0f, .edge = 0.0f, .shift = -0.05f};

// ---- Out-of-class definitions (need the named coordinates above) ----

inline SpectralShapeRecipe::Controls SpectralShapeRecipe::sanitize(const Controls& c) noexcept {
    const auto field = [](float x, float fallback, float lo, float hi) noexcept {
        return std::clamp(detail::isFinite(x) ? x : fallback, lo, hi);
    };
    const Controls& d = kDefaultControls;
    return Controls{
        .depth = field(c.depth, d.depth, 0.0f, 1.0f),
        .body = field(c.body, d.body, 0.0f, 1.0f),
        .bodyCurvature = field(c.bodyCurvature, d.bodyCurvature, 0.0f, 1.0f),
        .bodyEmphasis = field(c.bodyEmphasis, d.bodyEmphasis, -1.0f, 0.0f),
        .edge = field(c.edge, d.edge, 0.0f, 1.0f),
        .shift = field(c.shift, d.shift, -1.0f, 1.0f),
    };
}

}  // namespace Krate::DSP
