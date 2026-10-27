// ==============================================================================
// Layer 3: System Component — Profundum Phase 1 (specs/profundum-phase1-harmonic-core)
// ==============================================================================
// ProfundumCore: the zero-latency harmonic core of the Profundum bass
// instrument (SpectralShapeRecipe -> HarmonicOscillatorBank). Includes Layer 0,
// Layer 1 and Layer 2 only.
//   Spec:  specs/profundum-phase1-harmonic-core/spec.md
//   Plan:  specs/profundum-phase1-harmonic-core/plan.md (S4)
//   Tasks: specs/profundum-phase1-harmonic-core/tasks.md
//   Phase 2: specs/profundum-phase2-edge-fold (edge excitation, plan S5)
//
// Header-only, no heap allocation: every buffer is a fixed-size member (FR-040).
// prepare() and reset() are control-thread calls; every other method is
// audio-thread safe and a no-op before prepare (FR-041, FR-061).
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>                         // detail::isFinite (L0)
#include <krate/dsp/primitives/biquad.h>                     // L1, Phase 2
#include <krate/dsp/primitives/dc_blocker.h>                 // L1, Phase 2
#include <krate/dsp/primitives/lowpass_biquad_d.h>           // L1, Phase 9 close (split LPs in double)
#include <krate/dsp/primitives/polyphase_halfband_oversampler.h>   // L1, Phase 2
#include <krate/dsp/primitives/smoother.h>                   // L1, Phase 2
#include <krate/dsp/processors/harmonic_oscillator_bank.h>   // L2
#include <krate/dsp/processors/harmonic_types.h>             // L2
#include <krate/dsp/processors/spectral_shape_recipe.h>      // L2

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>

namespace Krate::DSP {

static_assert(kRecipeMaxPartials == kMaxPartials,
              "SpectralShapeRecipe restates the bank partial ceiling; keep them equal");

namespace detail {

/// Phase 2 excitation tables (plan S5.4): 49 log2 D points over [-4, 2] (step 1/8) x 9 bias points
/// over [0, 0.5] (step 1/16), stored t[i * 9 + j] with i the drive index and j the bias index.
inline constexpr std::size_t kExcitationTableDrivePoints = 49;
inline constexpr std::size_t kExcitationTableBiasPoints = 9;
inline constexpr std::size_t kExcitationTableSize = kExcitationTableDrivePoints * kExcitationTableBiasPoints;

/// Lower drive cell of the clamped bilinear lookup (plan S5.3): pi = clamp((log2 D + 4)·8, 0, 48);
/// the result is <= 47, so the lookup's i + 1 read stays inside the table.
[[nodiscard]] constexpr std::size_t excitationDriveIndex(float pi) noexcept {
    if (!(pi > 0.0f))
        return 0;
    const float c = pi < 48.0f ? pi : 48.0f;
    const auto i = static_cast<std::size_t>(c);
    return i < 47 ? i : std::size_t{47};
}

/// Lower bias cell of the clamped bilinear lookup (plan S5.3): pj = clamp(b·16, 0, 8); the result
/// is <= 7, so the lookup's j + 1 read stays inside the table.
[[nodiscard]] constexpr std::size_t excitationBiasIndex(float pj) noexcept {
    if (!(pj > 0.0f))
        return 0;
    const float c = pj < 8.0f ? pj : 8.0f;
    const auto j = static_cast<std::size_t>(c);
    return j < 7 ? j : std::size_t{7};
}

/// Clamped bilinear read of an excitation table at (log2 D, b) (plan S5.3). Both axes use the
/// (1 − t)·a + t·b form, so a grid point (the corners included) returns its cell exactly.
[[nodiscard]] inline float bilinearExcitationTable(const std::array<float, kExcitationTableSize>& table,
                                                   float l2, float b) noexcept {
    const float pi = std::clamp((l2 + 4.0f) * 8.0f, 0.0f, 48.0f);
    const float pj = std::clamp(b * 16.0f, 0.0f, 8.0f);
    const std::size_t i = excitationDriveIndex(pi);
    const std::size_t j = excitationBiasIndex(pj);
    const float ti = pi - static_cast<float>(i);
    const float tj = pj - static_cast<float>(j);
    constexpr std::size_t kCols = kExcitationTableBiasPoints;
    const float lo = (1.0f - ti) * table[i * kCols + j] + ti * table[(i + 1) * kCols + j];
    const float hi = (1.0f - ti) * table[i * kCols + j + 1] + ti * table[(i + 1) * kCols + j + 1];
    return (1.0f - tj) * lo + tj * hi;
}

}  // namespace detail

/// @note Precondition: the owner runs processBlock with FTZ/DAZ on (ScopedDenormalMode on x86, the
///       default FPCR on AArch64). Without it, zero-target bank lanes stall as denormals (S7).
///       Phase 2: the same holds for the excitation state (halfband sections, split/notch/DC filters, ADAA history).
class ProfundumCore {
public:
    enum class RetriggerPhase : std::uint8_t { Reset, FreeRunning };   // FR-046

    static constexpr int kDefaultPartials = 64;                 // FR-041 (OQ-3, S9.3)
    static constexpr std::size_t kControlInterval = 32;         // FR-042 (S9.4, D-4 ruling 2026-10-10)
    static constexpr std::size_t kPitchUpdateInterval = 16;     // FR-043 cadence U
    static constexpr float kMinF0Hz = 8.0f;                     // FR-062
    static constexpr float kDefaultF0Hz = 55.0f;                // A1, used until the first valid f0
    static constexpr float kMaxGainSlewPerSecOverSqrtP0 = 375.0f; // FR-050 (Q4)
    static constexpr float kSlewChordMargin = 0.999f;           // step chord = 0.999·Δmax (S4.5)
    static constexpr float kMaxBaseStepSemitones = 0.9f;        // < bank's 1-semitone crossfade threshold
    static constexpr double kMinSampleRate = 22050.0;
    static constexpr double kMaxSampleRate = 192000.0;
    /// SC-009: the bank rails at +-HarmonicOscillatorBank::kOutputClamp (2.0, bank :91), but the
    /// all-sine-phase-0 stack (D-3) at a high-edge state peaks above that (measured: the SC-009 state
    /// railed at 2.0 from MIDI ~20 to ~62, aliasing up to -54 dBFS). The core hands the bank its
    /// amplitudes divided by kBankHeadroom and multiplies the bank's output back. Both are exact
    /// powers of two, so every unclipped sample is bit-identical to an unscaled render. The core's
    /// peak is bounded by sqrt(N·P0) <= sqrt(96·P0) = 4.92 per channel (unit pan gain, Cauchy-
    /// Schwarz), below the effective 2·kBankHeadroom = 8, so the bank's clamp stays a pure
    /// divergence guard.
    static constexpr float kBankHeadroom = 4.0f;
    static_assert((HarmonicOscillatorBank::kOutputClamp * kBankHeadroom) * (HarmonicOscillatorBank::kOutputClamp * kBankHeadroom)
                      > static_cast<float>(kMaxPartials) * SpectralShapeRecipe::kPowerTarget,
                  "effective clamp must exceed the sqrt(N·P0) peak bound");

    ProfundumCore() noexcept = default;
    ProfundumCore(const ProfundumCore&) = delete;               // owns the non-copyable bank
    ProfundumCore& operator=(const ProfundumCore&) = delete;
    ProfundumCore(ProfundumCore&&) noexcept = default;
    ProfundumCore& operator=(ProfundumCore&&) noexcept = default;

    // ---- control thread ----

    /// @note NOT real-time safe: bank_.prepare (harmonic_oscillator_bank.h:128) runs the bank's
    ///       noise-calibration loop (:158-182).
    void prepare(double sampleRate, int numPartials = kDefaultPartials) noexcept {
        // S4.3 step 1: clamp (FR-041; prepare(fs, 128) == prepare(fs, 96), SC-016).
        const double sr = std::clamp(sampleRate, kMinSampleRate, kMaxSampleRate);
        sampleRate_ = sr;
        numPartials_ = std::clamp(numPartials, 1, static_cast<int>(kMaxPartials));

        // Step 2: bank_.prepare resets the bank.
        bank_.prepare(sr);
        // Phase 4 (2026-10-13, user ruling): pitch updates keep every partial's amplitude and
        // phase exact across the MCF coefficient change (bank opt-in; default off elsewhere).
        // Without it a 48-semitone Drop re-rolled partial levels by up to ±11 dB.
        bank_.setContinuousFrequencyUpdates(true);

        // Step 4: cap and the shape-step ceiling, Δmax = 375·√P0·kControlInterval/fs (0.125594 at 48 kHz).
        capHz_ = SpectralShapeRecipe::capFrequency(sr);
        maxStepPerInterval_ = static_cast<float>(
            static_cast<double>(kMaxGainSlewPerSecOverSqrtP0)
            * std::sqrt(static_cast<double>(SpectralShapeRecipe::kPowerTarget))
            * static_cast<double>(kControlInterval) / sr);

        // Phase 15: Stiffness 0 and Phase 0.
        stiffnessAmount_ = 0.0f;
        stiffnessNoteHz_ = static_cast<float>(kStiffnessReferenceHz);
        stiffnessB_ = 0.0;
        phaseAmount_ = 0.0f;
        refreshStretch();
        refreshPhaseTargets();

        // Steps 3, 5, 6.
        initialiseState(SpectralShapeRecipe::kDefaultControls);

        // Phase 2 (plan S5.6): excitation filters, factor, tables, smoothers, state.
        prepareExcitation(sr);
        initialiseExcitation();
        excitationIdle_ = true;   // a non-zero pending mix engages at the first latch
        excitationDirty_ = pendingExcitation_.mix > 0.0f;
    }

    /// @note Control thread; allocation-free and bounded (array fills + one evaluateShape), S4.3.
    ///       The RT-safe way to clear a sounding core is a Reset-policy noteOn.
    void reset() noexcept {
        if (!prepared_)
            return;
        bank_.reset();   // not bank_.prepare: coefficients, crossfade and noise calibration stay valid
        initialiseState(pendingControls_);   // step 5 latches the pending controls, not the defaults
        // Phase 2 (plan S5.6): pendingExcitation_ is kept, as pendingControls_ is.
        initialiseExcitation();
        excitationIdle_ = pendingExcitation_.mix == 0.0f;
    }

    // ---- audio thread (all noexcept, allocation-free; no-ops before prepare, FR-061) ----

    void setControls(const SpectralShapeRecipe::Controls& controls) noexcept {
        if (!prepared_)
            return;
        // S4.8: a non-finite field keeps the *pending* value (FR-062); otherwise clamp to FR-002.
        const SpectralShapeRecipe::Controls& p = pendingControls_;
        const SpectralShapeRecipe::Controls next{
            .depth = sanitizeField(controls.depth, p.depth, 0.0f, 1.0f),
            .body = sanitizeField(controls.body, p.body, 0.0f, 1.0f),
            .bodyCurvature = sanitizeField(controls.bodyCurvature, p.bodyCurvature, 0.0f, 1.0f),
            .bodyEmphasis = sanitizeField(controls.bodyEmphasis, p.bodyEmphasis, -1.0f, 0.0f),
            .edge = sanitizeField(controls.edge, p.edge, 0.0f, 1.0f),
            .shift = sanitizeField(controls.shift, p.shift, -1.0f, 1.0f),
        };
        // controlsDirty_ only on a bitwise change (FR-042 skip rule).
        if (sameBits(next.depth, p.depth) && sameBits(next.body, p.body)
            && sameBits(next.bodyCurvature, p.bodyCurvature) && sameBits(next.bodyEmphasis, p.bodyEmphasis)
            && sameBits(next.edge, p.edge) && sameBits(next.shift, p.shift))
            return;
        pendingControls_ = next;
        controlsDirty_ = true;
        shapeProvided_ = false;   // T032: a shape handed in for earlier controls no longer applies
    }

    /// Profundum Phase 9 T032 (append-only): setControls(controls) plus the shape those controls
    /// evaluate to, computed once by the owner for every voice it drives with the same controls
    /// (ProfundumEngine; evaluateShape over 64 partials per voice was 7 % of the engine's samples at
    /// 4 voices). Adopted at the next control update instead of evaluateTargetShape() when every
    /// field is finite — then the latched controls are exactly the clamped ones the owner evaluated,
    /// so the adopted shape is the one the core would have computed. Otherwise the core evaluates.
    void setControlsWithShape(const SpectralShapeRecipe::Controls& controls, std::span<const float> shape) noexcept {
        setControls(controls);
        if (!prepared_ || !controlsDirty_ || shape.size() < static_cast<std::size_t>(numPartials_))
            return;
        if (!(detail::isFinite(controls.depth) && detail::isFinite(controls.body)
              && detail::isFinite(controls.bodyCurvature) && detail::isFinite(controls.bodyEmphasis)
              && detail::isFinite(controls.edge) && detail::isFinite(controls.shift)))
            return;
        std::copy_n(shape.data(), static_cast<std::size_t>(numPartials_), providedShape_.data());
        shapeProvided_ = true;
    }

    void setFrequency(float f0Hz) noexcept {
        if (!prepared_)
            return;
        heldF0_ = sanitizeF0(f0Hz, heldF0_);
    }

    void setRetriggerPhase(RetriggerPhase policy) noexcept {
        if (!prepared_)
            return;
        policy_ = policy;
    }

    /// Effective at offset 0 of the next block.
    void noteOn(float f0Hz) noexcept {
        if (!prepared_)
            return;
        noteOnF0_ = sanitizeF0(f0Hz, heldF0_);   // the last call before a block wins (S4.7)
        noteOnPending_ = true;
    }

    /// FR-048 / S4.8: per-partial pan offsets, forwarded at the next control update (or noteOn).
    /// A non-finite element becomes 0.0f; an all-zero vector (-0.0 counts as zero) never reaches
    /// bank_.applyPanOffsets, so the Anchor path stays bit-identical L/R.
    void setPartialPanOffsets(const std::array<float, kMaxPartials>& offsets) noexcept {
        if (!prepared_)
            return;
        bool changed = false;
        bool nonZero = false;
        for (std::size_t k = 0; k < kMaxPartials; ++k) {
            const float o = detail::isFinite(offsets[k]) ? offsets[k] : 0.0f;
            if (!sameBits(o, panOffsets_[k])) {
                panOffsets_[k] = o;
                changed = true;
            }
            if (o != 0.0f)
                nonZero = true;
        }
        panNonZero_ = nonZero;
        if (changed)
            panDirty_ = true;
    }

    void processBlock(float* left, float* right, std::size_t numSamples,
                      const float* f0PerSample = nullptr) noexcept {
        if (left == nullptr || right == nullptr || numSamples == 0)
            return;   // E-2: no state change
        if (!prepared_) {   // FR-061
            std::fill(left, left + numSamples, 0.0f);
            std::fill(right, right + numSamples, 0.0f);
            return;
        }
        if (noteOnPending_)
            applyNoteOn();   // S4.7, at offset 0

        // S4.4 grid loop: a control update at every grid phase 0, a pitch update at every
        // other multiple of U; the bank renders in runs that never cross a U boundary.
        // FR-062: trajF0 is the sanitised f0 of element resolved - 1 (before the block: the previous
        // sample's f0), so a non-finite element holds the previous sample's f0, not the last sampled one.
        float trajF0 = prevSampleF0_;
        std::size_t resolved = 0;
        std::size_t i = 0;
        while (i < numSamples) {
            const std::size_t phaseInPitch = intervalPhase_ % kPitchUpdateInterval;
            if (phaseInPitch == 0) {
                float f = heldF0_;
                if (f0PerSample != nullptr) {
                    trajF0 = trajectoryF0(f0PerSample, resolved, i, trajF0);
                    resolved = i + 1;
                    f = trajF0;
                }
                if (intervalPhase_ == 0)
                    controlUpdate(f);
                else
                    pitchUpdate(f);
            }
            const std::size_t run = std::min(numSamples - i, kPitchUpdateInterval - phaseInPitch);
            bank_.processStereoBlock(left + i, right + i, run);
            // Undo the bank headroom (exact, SC-009), per run so the excitation sees the
            // rescaled signal (Phase 2 plan S5.3). An aliased left == right buffer is scaled once.
            for (std::size_t k = i; k < i + run; ++k)
                left[k] *= kBankHeadroom;
            if (right != left) {
                for (std::size_t k = i; k < i + run; ++k)
                    right[k] *= kBankHeadroom;
            }
            if (!excitationIdle_)
                exciteRun(left + i, right + i, run);   // Phase 2 (plan S5.4)
            else
                trackIdleSplit(left + i, right + i, run);   // plan S2.1: splitBase runs while idle
            i += run;
            intervalPhase_ = static_cast<std::uint32_t>(
                (static_cast<std::size_t>(intervalPhase_) + run) % kControlInterval);
        }
        if (f0PerSample != nullptr)   // FR-043: the scalar becomes the trajectory's last value
            heldF0_ = trajectoryF0(f0PerSample, resolved, numSamples - 1, trajF0);
        prevSampleF0_ = heldF0_;   // the f0 of this block's last sample (FR-062)
    }

    // ---- read-only observers (tests; Phase 3/7/9 diagnostics) ----

    [[nodiscard]] std::span<const float> shapeGains() const noexcept {   // FR-050, size numPartials
        return {shape_.data(), static_cast<std::size_t>(numPartials_)};
    }
    [[nodiscard]] std::span<const float> deliveredGains() const noexcept {   // FR-051, size numPartials
        return {delivered_.data(), static_cast<std::size_t>(numPartials_)};
    }
    [[nodiscard]] float maxShapeStepPerInterval() const noexcept { return maxStepPerInterval_; }   // Δmax, S4.5
    [[nodiscard]] float currentFrequency() const noexcept { return currentF0_; }   // f0 of the last pitch update
    [[nodiscard]] float maskFrequency() const noexcept { return maskF0_; }   // f0 of the last control update
    [[nodiscard]] float baseFrequency() const noexcept { return baseF0_; }   // bank targetPitch, S4.6
    [[nodiscard]] int numPartials() const noexcept { return numPartials_; }
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }
    /// bank_.stateFinite() && the core's own arrays and scalars finite (bit test, fast-math-immune).
    [[nodiscard]] bool stateFinite() const noexcept {
        if (!bank_.stateFinite())
            return false;
        const auto n = static_cast<std::size_t>(numPartials_);
        for (std::size_t k = 0; k < n; ++k) {
            if (!detail::isFinite(targetShape_[k]) || !detail::isFinite(shape_[k])
                || !detail::isFinite(mask_[k]) || !detail::isFinite(delivered_[k])
                || !detail::isFinite(detuneShadow_[k]) || !detail::isFinite(multiplierScratch_[k])
                || !detail::isFinite(epsBase_[k]))
                return false;
        }
        for (const float p : panOffsets_) {
            if (!detail::isFinite(p))
                return false;
        }
        return detail::isFinite(heldF0_) && detail::isFinite(prevSampleF0_) && detail::isFinite(currentF0_)
               && detail::isFinite(baseF0_)
               && detail::isFinite(maskF0_) && detail::isFinite(noteOnF0_)
               && excitationStateFinite();   // Phase 2 (plan S5.6)
    }

    // ==========================================================================================
    // Phase 2 — edge excitation (specs/profundum-phase2-edge-fold, plan S5.1). Append-only.
    // ==========================================================================================

    /// FR-002, D-5. Plain aggregate.
    struct ExcitationControls {
        float mix = 0.0f;         // [0, 1]
        float drive = 0.5f;       // [0, 1] -> excitationDriveLaw
        float asymmetry = 0.0f;   // [0, 1] -> bias asymmetry · kExcitationAsymmetryBias
    };
    /// Defined constexpr after the class (in-class nested-aggregate NSDMIs are rejected, plan R-7).
    static const ExcitationControls kDefaultExcitation;

    static constexpr float kEdgeDriveRangeDb = 12.0f;               // FR-004
    static constexpr float kEdgeDriveFloor = 0.251188643f;          // 10^(-12/20)
    static constexpr float kExcitationDriveMinDb = -12.0f;          // plan S1 C-7
    static constexpr float kExcitationDriveMaxDb = 12.0f;           // plan S1 C-7
    static constexpr float kExcitationAsymmetryBias = 0.5f;         // bias at asymmetry 1 (argument units)
    static constexpr float kExcitationSplitHz = 500.0f;
    static constexpr float kExcitationSplitEngageMix = 0.1f;
    static constexpr float kExcitationDcCornerHz = 2.0f;
    static constexpr float kExcitationSmoothMs = 5.0f;
    static constexpr float kExcitationCeiling = 6.0f;
    static_assert(kExcitationCeiling < 2.0f * kBankHeadroom, "SC-019");

    /// Stores the sanitised pending value (FR-043); latched at the next control update. No-op
    /// before prepare (Phase 1 FR-061 pattern).
    void setExcitation(const ExcitationControls& c) noexcept {
        if (!prepared_)
            return;
        const ExcitationControls& p = pendingExcitation_;
        pendingExcitation_ = ExcitationControls{
            .mix = sanitizeExcitationField(c.mix, p.mix, 0.0f, 1.0f),
            .drive = sanitizeExcitationField(c.drive, p.drive, 0.0f, 1.0f),
            .asymmetry = sanitizeExcitationField(c.asymmetry, p.asymmetry, 0.0f, 1.0f),
        };
        excitationDirty_ = true;
    }

    /// The pending (sanitised) excitation controls.
    [[nodiscard]] ExcitationControls excitation() const noexcept { return pendingExcitation_; }

    /// The latched target D_eff = excitationDriveLaw(drive) · edgeDriveGain(edge) (FR-004).
    [[nodiscard]] float effectiveExcitationDrive() const noexcept { return latchedDriveTarget_; }

    /// 10^((-12 + 24·d)/20), d clamped to [0, 1], computed in double (plan S1 C-7).
    [[nodiscard]] static float excitationDriveLaw(float drive) noexcept {
        const double d = static_cast<double>(std::clamp(drive, 0.0f, 1.0f));
        const double lo = static_cast<double>(kExcitationDriveMinDb);
        const double hi = static_cast<double>(kExcitationDriveMaxDb);
        return static_cast<float>(std::pow(10.0, (lo + (hi - lo) * d) / 20.0));
    }

    /// 10^(12·(e − 1)/20), e clamped to [0, 1], computed in double (FR-004).
    [[nodiscard]] static float edgeDriveGain(float edge) noexcept {
        const double e = static_cast<double>(std::clamp(edge, 0.0f, 1.0f));
        return static_cast<float>(std::pow(10.0, static_cast<double>(kEdgeDriveRangeDb) * (e - 1.0) / 20.0));
    }

    /// The excitation's oversampling factor: 4 for fs <= 100 kHz, 2 above (S3.3, FR-045).
    [[nodiscard]] std::size_t excitationOversamplingFactor() const noexcept {
        return excChannels_[0].os.factor();
    }

    /// Largest |excitation output| before the kExcitationCeiling clamp since prepare / reset /
    /// clearExcitationPreCeilingPeak (SC-019; one max per sample while the stage runs).
    [[nodiscard]] float excitationPreCeilingPeak() const noexcept { return excitationPreCeilingPeak_; }
    void clearExcitationPreCeilingPeak() noexcept { excitationPreCeilingPeak_ = 0.0f; }

    // ==========================================================================================
    // Phase 15 — Stiffness and Phase alignment (specs/Profundum-roadmap.md, Phase 15). Append-only:
    // at Stiffness 0 with no Phase glide running, every update takes the pre-phase code path.
    // ==========================================================================================

    static constexpr double kMaxStiffnessB = 0.01;          // B at Stiffness 1 on the reference note
    static constexpr double kStiffnessReferenceHz = 55.0;   // A1: B scales with f_note / 55 Hz
    /// A live Phase change slews each partial by at most pi per 100 ms: <= 5 Hz of frequency offset.
    static constexpr double kPhaseSlewRadPerSec = 31.415926535897932;

    /// r_n = n·√((1 + B·n²) / (1 + B)), partial n's frequency over f0 (the stiff-string law; r_1 = 1).
    [[nodiscard]] static double stretchedRatio(double b, int n) noexcept {
        const auto nn = static_cast<double>(n);
        if (n == 1 || !(b > 0.0))
            return nn;
        return nn * std::sqrt((1.0 + b * nn * nn) / (1.0 + b));
    }

    /// B = kMaxStiffnessB · x² · (noteHz / kStiffnessReferenceHz), x clamped to [0, 1].
    [[nodiscard]] static double stiffnessCoefficient(float amount, float noteHz) noexcept {
        const auto x = static_cast<double>(std::clamp(amount, 0.0f, 1.0f));
        return kMaxStiffnessB * x * x * (static_cast<double>(noteHz) / kStiffnessReferenceHz);
    }

    /// φ_n = x·π/2 for n >= 2, φ_1 = 0: every partial above h1 rotated by x·90° (roadmap ruling 2026-10-27).
    /// x = 0 is today's all-sine-phase stack (the smooth end); x = 1 the peakiest the spectrum allows.
    [[nodiscard]] static double alignmentPhase(float amount, int n) noexcept {
        if (n <= 1)
            return 0.0;
        return static_cast<double>(std::clamp(amount, 0.0f, 1.0f)) * (kPhasePi / 2.0);
    }

    /// Stiffness x in [0, 1] on a note at noteHz (non-finite keeps the previous value; noteHz <= 0
    /// keeps the previous note). Takes effect at the next control update or noteOn.
    void setStiffness(float amount, float noteHz) noexcept {
        if (!prepared_)
            return;
        stiffnessAmount_ = sanitizeField(amount, stiffnessAmount_, 0.0f, 1.0f);
        if (detail::isFinite(noteHz) && noteHz > 0.0f)
            stiffnessNoteHz_ = noteHz;
        const double b = stiffnessCoefficient(stiffnessAmount_, stiffnessNoteHz_);
        if (b == stiffnessB_)
            return;
        stiffnessB_ = b;
        refreshStretch();
        stretchDirty_ = true;
    }

    /// Phase alignment x in [0, 1] (non-finite keeps the previous value). A sounding core glides to
    /// it (kPhaseSlewRadPerSec); otherwise the next noteOn seeds it.
    void setPhaseAlignment(float amount) noexcept {
        if (!prepared_)
            return;
        const float x = sanitizeField(amount, phaseAmount_, 0.0f, 1.0f);
        if (sameBits(x, phaseAmount_))
            return;
        phaseAmount_ = x;
        refreshPhaseTargets();
        if (sounding_)
            phaseGliding_ = true;
        else
            phaseApplied_ = phaseTarget_;
    }

    [[nodiscard]] double stiffnessB() const noexcept { return stiffnessB_; }
    [[nodiscard]] float phaseAlignment() const noexcept { return phaseAmount_; }
    [[nodiscard]] bool phaseGliding() const noexcept { return phaseGliding_; }
    /// Largest |Phase-glide frequency offset| of any partial since prepare / reset / clearMaxPhaseGlideHz.
    [[nodiscard]] double maxPhaseGlideHz() const noexcept { return maxGlideHz_; }
    void clearMaxPhaseGlideHz() noexcept { maxGlideHz_ = 0.0; }

private:
    /// S4.3 steps 3, 5 and 6 (shared by prepare and reset). sampleRate_, numPartials_, capHz_ and
    /// maxStepPerInterval_ must already hold the prepared values.
    void initialiseState(const SpectralShapeRecipe::Controls& controls) noexcept {
        const auto n = static_cast<std::size_t>(numPartials_);

        // Step 3: the fixed frame (E-9: numPartials never shrinks; capped partials get amplitude 0).
        frame_ = HarmonicFrame{};
        for (std::size_t k = 0; k < n; ++k) {
            const float harmonic = static_cast<float>(k + 1);
            Partial& p = frame_.partials[k];
            p.harmonicIndex = static_cast<int>(k + 1);
            p.relativeFrequency = harmonic;
            p.inharmonicDeviation = 0.0f;
            p.bandwidth = 0.0f;
            p.phase = 0.0f;
            p.amplitude = 0.0f;
            p.frequency = harmonic * kDefaultF0Hz;
        }
        frame_.numPartials = numPartials_;

        // Step 5: latch, evaluate the target, start converged.
        pendingControls_ = controls;
        latchedControls_ = controls;
        targetShape_.fill(0.0f);
        SpectralShapeRecipe::evaluateShape(latchedControls_, std::span<float>(targetShape_.data(), n));
        shape_ = targetShape_;
        controlsDirty_ = false;
        shapeConverged_ = true;
        heldF0_ = kDefaultF0Hz;
        prevSampleF0_ = kDefaultF0Hz;
        currentF0_ = kDefaultF0Hz;
        baseF0_ = kDefaultF0Hz;
        noteOnF0_ = kDefaultF0Hz;
        // No mask has been built yet: 0 Hz can never equal a sanitised f0 (>= kMinF0Hz), so the
        // first control update's bitwise comparison (S4.4 step 4) always builds it.
        maskF0_ = 0.0f;
        mask_.fill(0.0f);
        delivered_.fill(0.0f);
        epsBase_.fill(0.0);
        multiplierScratch_.fill(1.0f);

        // Step 6.
        detuneShadow_.fill(1.0f);   // mirrors the bank's detuneMultiplier_ reset to 1.0f
        panDirty_ = panNonZero_;    // panOffsets_ keeps its value
        panAppliedNonZero_ = false; // bank reset/prepare restored the centre pan tables
        intervalPhase_ = 0;
        sounding_ = false;
        noteOnPending_ = false;
        prepared_ = true;

        // Phase 15: no glide; the next noteOn seeds the target alignment (maskF0_ = 0 rebuilds the mask).
        phaseApplied_ = phaseTarget_;
        glideHz_.fill(0.0);
        phaseGliding_ = false;
        seedStepsLeft_ = 0;
        stretchDirty_ = false;
        maxGlideHz_ = 0.0;
    }

    /// S4.4 normative order. Steady state (no dirty controls, converged slew, f bit-unchanged)
    /// does comparisons only and never calls loadFrame (FR-042 skip rule).
    void controlUpdate(float f0) noexcept {
        // 1. Latch.
        bool controlsLatched = false;   // Phase 2: step 1b re-latches the excitation (Edge may have moved)
        if (controlsDirty_) {
            latchedControls_ = pendingControls_;
            evaluateTargetShape();
            shapeConverged_ = std::memcmp(targetShape_.data(), shape_.data(), activeBytes()) == 0;
            controlsDirty_ = false;
            controlsLatched = true;
        }

        // 1b. Phase 2 excitation latch (plan S5.3): coordinates only, no bank state.
        if (excitationDirty_ || controlsLatched)
            latchExcitation();

        // 2. Slew (FR-050, S4.5).
        bool shapeChanged = false;
        if (!shapeConverged_) {
            slewShapeTowardTarget();
            shapeChanged = true;
        }

        // 3. Pitch: the base chases f by at most kMaxBaseStepSemitones per interval, so loadFrame
        //    never sees a > 1-semitone targetPitch step (bank crossfade, FR-043); the exact-ε
        //    multipliers carry the remainder (S4.6). Before the first note there is no base to chase.
        const bool fChanged = !sameBits(f0, currentF0_);
        currentF0_ = f0;
        const float baseNew = sounding_ ? chaseBase(f0) : f0;

        // 4. Mask: rebuilt only when f moved (bitwise) or the Stiffness stretch changed (Phase 15),
        //    never slewed (FR-051).
        bool maskChanged = false;
        if (!sameBits(f0, maskF0_) || stretchDirty_) {
            buildMask(f0);
            maskChanged = true;
        }

        // 5. Deliver.
        bool deliveredChanged = false;
        if (shapeChanged || maskChanged) {
            deliverMaskedShape();
            deliveredChanged = true;
        }

        // 6. Forward to the bank: multipliers first (exact ε relative to baseNew), then loadFrame,
        //    which recomputes the AA gains from n·targetPitch·detuneMultiplier (bank :1083). When
        //    only f moved, refresh the multipliers alone (no loadFrame).
        if (sounding_) {
            const bool glide = stepPhaseGlide();   // Phase 15
            const bool baseChanged = !sameBits(baseNew, baseF0_);
            if (deliveredChanged || baseChanged) {
                if (baseChanged)
                    setBase(baseNew);
                refreshMultipliers(f0);
                deliverToBank(baseChanged);
            } else if (fChanged || glide) {
                refreshMultipliers(f0);
            }
        }
        // 7. Pan (FR-048).
        forwardPan();
    }

    /// S4.6, U-cadence (non-control samples): mask and base stay untouched until the next control
    /// update.
    void pitchUpdate(float f0) noexcept {
        const bool glide = sounding_ && stepPhaseGlide();   // Phase 15
        if (sameBits(f0, currentF0_) && !glide)
            return;
        currentF0_ = f0;
        if (sounding_)
            refreshMultipliers(f0);
    }

    /// S4.7, at offset 0 of the block.
    void applyNoteOn() noexcept {
        // 1. Pitch.
        const float f = noteOnF0_;
        const bool fChanged = !sameBits(f, currentF0_);
        heldF0_ = f;
        prevSampleF0_ = f;   // a non-finite first trajectory element holds the note's f0 (FR-062)
        currentF0_ = f;

        if (policy_ == RetriggerPhase::Reset || !sounding_) {
            // 2. Latch the pending controls and evaluate the target.
            latchedControls_ = pendingControls_;
            evaluateTargetShape();
            controlsDirty_ = false;

            // 2b. Phase 2 (plan S5.3, FR-013): latch the excitation, snap its coordinates and
            //     reset its state with the ADAA history at the bias. No bank state is touched.
            latchExcitation();
            mixS_.snapToTarget();
            log2DriveS_.snapToTarget();
            biasS_.snapToTarget();
            resetExcitationChannels(biasS_.getCurrentValue());

            // 3. Reset branch (Reset policy, or the first note after prepare/reset).
            bank_.reset();                // array fills only (E-1); frameLoaded_ = false re-seeds phases
            detuneShadow_.fill(1.0f);     // mirrors the bank's detuneMultiplier_ reset
            shape_ = targetShape_;        // FR-050 exemption: no slew on a Reset noteOn
            shapeConverged_ = true;
            intervalPhase_ = 0;           // plan S1 C-9: the grid restarts at the note
            panDirty_ = panNonZero_;
            panAppliedNonZero_ = false;   // bank_.reset() restored the centre pan tables

            // 4. Mask at the new f0 and deliver (FR-051(d)).
            buildMask(f);
            deliverMaskedShape();

            // 5. Base = f, no chase clamp (E-7).
            setBase(f);

            // 6. Multipliers, then loadFrame: phases seeded from partial.phase, 0 at Phase 0, so the
            //    first sample is 0.0 there (FR-045); Phase 15 seeds the target alignment.
            seedPhaseAlignment();
            refreshMultipliers(f);
            deliverToBank(true);
        } else {
            // 3. FreeRunning while sounding: keep the MCF state, shape_ (the slew continues) and
            //    the grid. Every step is gated by a bit comparison, so a noteOn at an unchanged f0
            //    with unchanged controls touches no bank state (SC-012).
            bool controlsLatched = false;   // Phase 2: Edge may have moved the excitation drive
            if (controlsDirty_) {
                latchedControls_ = pendingControls_;
                evaluateTargetShape();
                shapeConverged_ = std::memcmp(targetShape_.data(), shape_.data(), activeBytes()) == 0;
                controlsDirty_ = false;
                controlsLatched = true;
            }

            // 3b. Phase 2 (plan S5.3): latch the excitation only if dirty; no snap, no reset (SC-014).
            if (excitationDirty_ || controlsLatched)
                latchExcitation();

            // 4. Mask at the new f0 and deliver (FR-051(d)).
            bool deliveredChanged = false;
            if (!sameBits(f, maskF0_) || stretchDirty_) {
                buildMask(f);
                deliverMaskedShape();
                deliveredChanged = true;
            }

            // 5-6. Base = f with no chase clamp (a > 1-semitone jump crossfades, E-7), then the
            //      multipliers, then loadFrame.
            const bool glide = stepPhaseGlide();   // Phase 15
            const bool baseChanged = !sameBits(f, baseF0_);
            if (deliveredChanged || baseChanged) {
                if (baseChanged)
                    setBase(f);
                refreshMultipliers(f);
                deliverToBank(baseChanged);
            } else if (fChanged || glide) {
                refreshMultipliers(f);
            }
        }

        // 7. Pan forwarding (FR-048).
        forwardPan();
        sounding_ = true;
        noteOnPending_ = false;
    }

    /// Profundum Phase 9 T032: the bank's constant-power pair for one offset, evaluated here instead
    /// of by HarmonicOscillatorBank::applyPanOffsets (96 std::cos + 96 std::sin per interval, 2.5 % of
    /// the engine's samples in the P-1 profile). The bank's law with its spread at 0 (this core never
    /// sets one): angle = pi/4 + clamp(o, -1, 1)·pi/4, left = cos(angle), right = sin(angle). With
    /// th = clamp(o)·pi/4 in [-pi/4, pi/4]: cos(pi/4 + th) = k·(cos th − sin th) and
    /// sin(pi/4 + th) = k·(cos th + sin th), k = kCenterPanGain; cos th and sin th by the Cephes
    /// single-precision minimax pair (<= 2 ulp on this range). An offset of 0 gives left == right ==
    /// kCenterPanGain exactly, the centre table's value.
    static void panPair(float offset, float& left, float& right) noexcept {
        constexpr float kQuarterPi = 0.78539816339744831f;
        const float th = std::clamp(offset, -1.0f, 1.0f) * kQuarterPi;
        const float t2 = th * th;
        const float s = ((-1.9515295891e-4f * t2 + 8.3321608736e-3f) * t2 - 1.6666654611e-1f) * t2 * th + th;
        const float c = ((2.443315711809948e-5f * t2 - 1.388731625493765e-3f) * t2 + 4.166664568298827e-2f) * t2 * t2
                        - 0.5f * t2 + 1.0f;
        left = HarmonicOscillatorBank::kCenterPanGain * (c - s);
        right = HarmonicOscillatorBank::kCenterPanGain * (c + s);
    }

    /// S4.4 step 7 (FR-048): non-zero offsets go through applyPanOffsets; the return to all-zero
    /// restores the exact centre tables with restoreCenterPan (pan tables only: phase, amplitude
    /// and detune state stay untouched, SC-014(c)). An all-zero vector that was never applied
    /// makes no bank call at all (SC-014(a)).
    void forwardPan() noexcept {
        if (!panDirty_)
            return;
        if (panNonZero_) {
            for (std::size_t k = 0; k < kMaxPartials; ++k)   // T032: the pair evaluated here (panPair)
                panPair(panOffsets_[k], panLeftCoef_[k], panRightCoef_[k]);
            bank_.setPanCoefficients(panLeftCoef_, panRightCoef_);
            panAppliedNonZero_ = true;
        } else if (panAppliedNonZero_) {
            bank_.restoreCenterPan();
            panAppliedNonZero_ = false;
        }
        panDirty_ = false;
    }

    /// S4.5: one great-circle step of chord 0.999·Δmax on the P0 sphere, in double; snaps (and
    /// converges) when the remaining chord is <= Δmax.
    void slewShapeTowardTarget() noexcept {
        const auto n = static_cast<std::size_t>(numPartials_);
        const double p0 = static_cast<double>(SpectralShapeRecipe::kPowerTarget);
        const double dMax = static_cast<double>(maxStepPerInterval_);

        double chord2 = 0.0;
        double dot = 0.0;
        for (std::size_t k = 0; k < n; ++k) {
            const double s = static_cast<double>(shape_[k]);
            const double t = static_cast<double>(targetShape_[k]);
            chord2 += (t - s) * (t - s);
            dot += s * t;
        }

        const double sqrtP0 = std::sqrt(p0);
        const double omega = std::acos(std::clamp(dot / p0, 0.0, 1.0));
        const double phi = 2.0 * std::asin(static_cast<double>(kSlewChordMargin) * dMax / (2.0 * sqrtP0));
        // chord > Δmax implies Ω > φ (S4.5); the second test only guards float rounding of the norms.
        if (std::sqrt(chord2) <= dMax || !(omega > phi)) {
            shape_ = targetShape_;
            shapeConverged_ = true;
            return;
        }

        const double sinOmega = std::sin(omega);
        const double ca = std::sin(omega - phi) / sinOmega;
        const double cb = std::sin(phi) / sinOmega;
        std::array<double, kMaxPartials> next{};
        double power = 0.0;
        for (std::size_t k = 0; k < n; ++k) {
            next[k] = ca * static_cast<double>(shape_[k]) + cb * static_cast<double>(targetShape_[k]);
            power += next[k] * next[k];
        }
        // Both coefficients >= 0 and shape_[0], targetShape_[0] > 0, so power > 0.
        const double scale = std::sqrt(p0 / power);
        for (std::size_t k = 0; k < n; ++k)
            shape_[k] = static_cast<float>(next[k] * scale);
    }

    /// S4.4 step 3: clamp f to baseF0_·2^(±kMaxBaseStepSemitones/12). The float bound is rounded
    /// toward baseF0_, so a step never exceeds the ceiling by a float ulp.
    [[nodiscard]] float chaseBase(float f0) const noexcept {
        const double ratio = std::exp2(static_cast<double>(kMaxBaseStepSemitones) / 12.0);
        const double base = static_cast<double>(baseF0_);
        const double hi = base * ratio;
        const double lo = base / ratio;
        const double f = static_cast<double>(f0);
        if (f > hi) {
            float b = static_cast<float>(hi);
            if (static_cast<double>(b) > hi)
                b = std::nextafter(b, 0.0f);
            return b;
        }
        if (f < lo) {
            float b = static_cast<float>(lo);
            if (static_cast<double>(b) < lo)
                b = std::nextafter(b, std::numeric_limits<float>::max());
            return b;
        }
        return f0;
    }

    /// baseF0_ = b, and the bank-ε estimate at it: ε_base_n = clamp(2·sin(π·n·b/fs), ±1.99) in
    /// double (mirrors the bank's recalculateFrequencies, :1057-1067). Recomputed only here, i.e.
    /// only when baseF0_ changes.
    void setBase(float b) noexcept {
        baseF0_ = b;
        const auto n = static_cast<std::size_t>(numPartials_);
        const double w = kPiDouble * static_cast<double>(b) / sampleRate_;
        for (std::size_t k = 0; k < n; ++k)
            epsBase_[k] = std::clamp(2.0 * std::sin(static_cast<double>(k + 1) * w), -kMaxBankEpsilon,
                                     kMaxBankEpsilon);
    }

    /// S4.6: multiplierScratch_[n-1] = dTarget_n / detuneShadow_n, where ε*_n = 2·sin(n·θ) with
    /// θ = π·f/fs, by the Chebyshev recurrence in double (1 sin + 1 cos per update), frozen at
    /// 2·sin(π·capHz/fs) for n·f >= capHz. Elements >= numPartials_ stay 1.0f.
    void computeEpsilonTargets(float f0) noexcept {
        if (stiffnessB_ > 0.0 || phaseGliding_) {
            computeStretchedEpsilonTargets(f0);   // Phase 15
            return;
        }
        const auto n = static_cast<std::size_t>(numPartials_);
        const double f = static_cast<double>(f0);
        const double capHz = static_cast<double>(capHz_);
        const double theta = kPiDouble * f / sampleRate_;
        const double c2 = 2.0 * std::cos(theta);
        double sPrev = 0.0;
        double sCur = std::sin(theta);
        double epsCap = 0.0;
        bool haveCap = false;
        for (std::size_t k = 0; k < n; ++k) {
            double epsStar = 2.0 * sCur;
            if (static_cast<double>(k + 1) * f >= capHz) {
                if (!haveCap) {
                    epsCap = 2.0 * std::sin(kPiDouble * capHz / sampleRate_);
                    haveCap = true;
                }
                epsStar = epsCap;
            }
            const double sNext = c2 * sCur - sPrev;
            sPrev = sCur;
            sCur = sNext;

            const double eb = epsBase_[k];
            const float dTarget = std::abs(eb) > 1e-9 ? static_cast<float>(epsStar / eb) : 1.0f;
            multiplierScratch_[k] = dTarget / detuneShadow_[k];
        }
    }

    /// S4.6: forward the multipliers, then mirror them with the same single float multiply, so
    /// detuneShadow_ stays bit-identical to the bank's detuneMultiplier_ (bank :670).
    void refreshMultipliers(float f0) noexcept {
        computeEpsilonTargets(f0);
        bank_.applyExternalFrequencyMultipliers(multiplierScratch_);
        const auto n = static_cast<std::size_t>(numPartials_);
        for (std::size_t k = 0; k < n; ++k)
            detuneShadow_[k] = detuneShadow_[k] * multiplierScratch_[k];
    }

    /// S4.4 step 6 / S4.7 step 6: write delivered_ (and, when the base moved, n·baseF0_) into the
    /// fixed frame and load it with skipNormalization (the recipe already normalised to P0).
    void deliverToBank(bool baseChanged) noexcept {
        const auto n = static_cast<std::size_t>(numPartials_);
        for (std::size_t k = 0; k < n; ++k) {
            Partial& p = frame_.partials[k];
            p.amplitude = delivered_[k] / kBankHeadroom;   // exact (power of two), SC-009
            if (baseChanged)
                p.frequency = static_cast<float>(k + 1) * baseF0_;
        }
        bank_.loadFrame(frame_, baseF0_, /*skipNormalization=*/true);
    }

    /// targetShape_ = evaluateShape(latchedControls_) over the first numPartials_ elements.
    void evaluateTargetShape() noexcept {
        if (shapeProvided_) {   // T032: the owner's evaluation of these same controls
            std::copy_n(providedShape_.data(), static_cast<std::size_t>(numPartials_), targetShape_.data());
            shapeProvided_ = false;
            return;
        }
        SpectralShapeRecipe::evaluateShape(
            latchedControls_, std::span<float>(targetShape_.data(), static_cast<std::size_t>(numPartials_)));
    }

    /// S4.4 step 4: mask_ = evaluateMask(f), maskF0_ = f.
    void buildMask(float f0) noexcept {
        const auto n = static_cast<std::size_t>(numPartials_);
        if (stiffnessB_ > 0.0)   // Phase 15: guard and cap on the stretched frequencies
            SpectralShapeRecipe::evaluateMaskStretched(f0, sampleRate_, std::span<const float>(stretchF_.data(), n),
                                                       std::span<float>(mask_.data(), n));
        else
            SpectralShapeRecipe::evaluateMask(f0, sampleRate_, std::span<float>(mask_.data(), n));
        maskF0_ = f0;
        stretchDirty_ = false;
    }

    // ---- Phase 15 helpers ----

    static constexpr double kPhasePi = 3.14159265358979323846;

    /// x wrapped to [−π, π].
    [[nodiscard]] static double wrapPi(double x) noexcept {
        constexpr double kTwoPhasePi = 2.0 * kPhasePi;
        return x - kTwoPhasePi * std::round(x / kTwoPhasePi);
    }

    /// stretch_ / stretchF_ = r_n at stiffnessB_ for every partial.
    void refreshStretch() noexcept {
        for (std::size_t k = 0; k < kMaxPartials; ++k) {
            stretch_[k] = stretchedRatio(stiffnessB_, static_cast<int>(k + 1));
            stretchF_[k] = static_cast<float>(stretch_[k]);
        }
    }

    /// phaseTarget_ = φ_n at phaseAmount_ over the prepared partial count.
    void refreshPhaseTargets() noexcept {
        for (std::size_t k = 0; k < kMaxPartials; ++k)
            phaseTarget_[k] = alignmentPhase(phaseAmount_, static_cast<int>(k + 1));
    }

    /// The Reset noteOn applies the target alignment at once. The bank seeds every partial at phase 0,
    /// its one exact MCF seed (sin/cos of any other phase is off the oscillator's ellipse and moves the
    /// partial's level by up to ±0.3 dB at 1 kHz); the core then rotates each partial forward to its
    /// target over the first kSeedSteps updates (64 samples, offsets < fs/64 Hz), while the partials
    /// still ramp up from silence. At Phase 0 there is nothing to rotate.
    void seedPhaseAlignment() noexcept {
        phaseApplied_.fill(0.0);
        glideHz_.fill(0.0);
        seedStepsLeft_ = 0;
        phaseGliding_ = false;
        if (phaseAmount_ == 0.0f)
            return;
        constexpr double kTwoPhasePi = 2.0 * kPhasePi;
        const auto n = static_cast<std::size_t>(numPartials_);
        for (std::size_t k = 0; k < n; ++k) {
            double d = std::fmod(phaseTarget_[k], kTwoPhasePi);
            if (d < 0.0)
                d += kTwoPhasePi;
            seedStep_[k] = d / static_cast<double>(kSeedSteps);
        }
        seedStepsLeft_ = kSeedSteps;
        phaseGliding_ = true;
    }

    /// Advances every partial's applied phase toward its target by at most kPhaseSlewRadPerSec over
    /// the next kPitchUpdateInterval samples (shortest wrapped path) and sets glideHz_ for them.
    /// Returns true when the multipliers must refresh: every step of a glide, and the one after it
    /// that removes the last offsets (then phaseGliding_ is false).
    bool stepPhaseGlide() noexcept {
        if (!phaseGliding_)
            return false;
        const double dt = static_cast<double>(kPitchUpdateInterval) / sampleRate_;
        const double maxStep = kPhaseSlewRadPerSec * dt;
        const double toHz = 1.0 / (2.0 * kPhasePi * dt);
        const auto n = static_cast<std::size_t>(numPartials_);
        if (seedStepsLeft_ > 0) {   // the note-on rotation (seedPhaseAlignment); not a live glide
            for (std::size_t k = 0; k < n; ++k) {
                glideHz_[k] = seedStep_[k] * toHz;
                phaseApplied_[k] = wrapPi(phaseApplied_[k] + seedStep_[k]);
            }
            --seedStepsLeft_;
            return true;
        }
        bool moving = false;
        for (std::size_t k = 0; k < n; ++k) {
            const double remaining = wrapPi(phaseTarget_[k] - phaseApplied_[k]);
            double step = remaining;
            if (std::abs(remaining) <= maxStep) {
                phaseApplied_[k] = phaseTarget_[k];
            } else {
                step = std::clamp(remaining, -maxStep, maxStep);
                phaseApplied_[k] = wrapPi(phaseApplied_[k] + step);
            }
            glideHz_[k] = step * toHz;
            maxGlideHz_ = std::max(maxGlideHz_, std::abs(glideHz_[k]));
            moving = moving || step != 0.0;
        }
        if (!moving)
            phaseGliding_ = false;   // glideHz_ is all 0: this refresh removes the last offsets
        return true;
    }

    /// computeEpsilonTargets for a stretched or gliding core: ε*_n = 2·sin(π·(r_n·f + g_n)/fs) per
    /// partial, frozen at the cap's ε for r_n·f >= capHz (the mask has removed those partials).
    void computeStretchedEpsilonTargets(float f0) noexcept {
        const auto n = static_cast<std::size_t>(numPartials_);
        const double f = static_cast<double>(f0);
        const double capHz = static_cast<double>(capHz_);
        const double w = kPiDouble / sampleRate_;
        const double epsCap = 2.0 * std::sin(w * capHz);
        const bool stretched = stiffnessB_ > 0.0;
        for (std::size_t k = 0; k < n; ++k) {
            const double hz = (stretched ? stretch_[k] : static_cast<double>(k + 1)) * f;
            const double epsStar = hz >= capHz ? epsCap : 2.0 * std::sin(w * (hz + glideHz_[k]));
            const double eb = epsBase_[k];
            const float dTarget = std::abs(eb) > 1e-9 ? static_cast<float>(epsStar / eb) : 1.0f;
            multiplierScratch_[k] = dTarget / detuneShadow_[k];
        }
    }

    /// S4.4 step 5: delivered_ = applyMask(shape_, mask_), then |x| < 1e-12f flushed to 0.0f
    /// (target hygiene only; the owner's FTZ/DAZ handles the decaying state, S7).
    void deliverMaskedShape() noexcept {
        const auto n = static_cast<std::size_t>(numPartials_);
        SpectralShapeRecipe::applyMask(std::span<const float>(shape_.data(), n),
                                       std::span<const float>(mask_.data(), n),
                                       std::span<float>(delivered_.data(), n));
        for (std::size_t k = 0; k < n; ++k) {
            if (std::abs(delivered_[k]) < 1e-12f)
                delivered_[k] = 0.0f;
        }
    }

    [[nodiscard]] std::size_t activeBytes() const noexcept {
        return static_cast<std::size_t>(numPartials_) * sizeof(float);
    }

    [[nodiscard]] static bool sameBits(float a, float b) noexcept {
        return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
    }

    /// S4.8 per-field rule: non-finite -> previous (pending) value; otherwise clamp.
    [[nodiscard]] static float sanitizeField(float x, float previous, float lo, float hi) noexcept {
        return detail::isFinite(x) ? std::clamp(x, lo, hi) : previous;
    }

    /// S4.8: non-finite -> previous; otherwise clamp to [kMinF0Hz, capHz_] (denormal and
    /// negative inputs clamp to 8 Hz).
    [[nodiscard]] float sanitizeF0(float f0, float previous) const noexcept {
        if (!detail::isFinite(f0))
            return previous;
        return std::clamp(f0, kMinF0Hz, capHz_);
    }

    /// FR-062 per-element rule: the sanitised f0 of trajectory element i, where a non-finite element
    /// holds the previous sample's f0. `previous` is the sanitised f0 of element from - 1 (of the
    /// sample before the block when from == 0); only elements [from, i] are scanned, newest first,
    /// and the scan stops at the first finite one, so a finite x[i] costs one test.
    [[nodiscard]] float trajectoryF0(const float* x, std::size_t from, std::size_t i, float previous) const noexcept {
        for (std::size_t j = i + 1; j-- > from;) {
            if (detail::isFinite(x[j]))
                return std::clamp(x[j], kMinF0Hz, capHz_);
        }
        return previous;
    }

    static constexpr double kPiDouble = 3.14159265358979323846;
    static constexpr double kMaxBankEpsilon = 1.99;   // the bank's kMaxEpsilon (:1060)

    // ---- S4.2 state (all fixed-size members) ----
    HarmonicOscillatorBank bank_;
    HarmonicFrame frame_{};

    SpectralShapeRecipe::Controls pendingControls_{SpectralShapeRecipe::kDefaultControls};
    SpectralShapeRecipe::Controls latchedControls_{SpectralShapeRecipe::kDefaultControls};
    bool controlsDirty_ = false;
    bool shapeProvided_ = false;                         // T032: providedShape_ holds pendingControls_'s shape
    std::array<float, kMaxPartials> providedShape_{};    // T032: setControlsWithShape
    bool shapeConverged_ = true;

    std::array<float, kMaxPartials> targetShape_{};
    std::array<float, kMaxPartials> shape_{};
    std::array<float, kMaxPartials> mask_{};
    std::array<float, kMaxPartials> delivered_{};
    std::array<double, kMaxPartials> epsBase_{};
    std::array<float, kMaxPartials> detuneShadow_{};
    std::array<float, kMaxPartials> multiplierScratch_{};

    float heldF0_ = kDefaultF0Hz;
    float prevSampleF0_ = kDefaultF0Hz;   // f0 of the last rendered sample (FR-062 trajectory hold)
    float currentF0_ = kDefaultF0Hz;
    float baseF0_ = kDefaultF0Hz;
    float maskF0_ = 0.0f;
    float capHz_ = 0.0f;

    std::array<float, kMaxPartials> panOffsets_{};
    bool panNonZero_ = false;
    bool panDirty_ = false;
    std::array<float, kMaxPartials> panLeftCoef_{};    // T032: the pair handed to the bank
    std::array<float, kMaxPartials> panRightCoef_{};
    bool panAppliedNonZero_ = false;

    RetriggerPhase policy_ = RetriggerPhase::Reset;

    bool noteOnPending_ = false;
    float noteOnF0_ = kDefaultF0Hz;
    bool sounding_ = false;

    std::uint32_t intervalPhase_ = 0;

    double sampleRate_ = 0.0;
    float maxStepPerInterval_ = 0.0f;

    int numPartials_ = 0;
    bool prepared_ = false;

    // ==========================================================================================
    // Phase 2 — edge excitation, private (plan S5.1, S5.2, S5.6). Append-only.
    // ==========================================================================================

    /// -12 dBFS RMS sine peak, 10^(-12/20)·√2: the calibration peak P of plan S5.4 (equal to
    /// HarmonicFold::kCalibrationPeak; harmonic_fold.h is deliberately not included).
    static constexpr float kExcitationCalibrationPeak = 0.3552343859f;
    static constexpr float kExcitationInvCalibrationPeak = 1.0f / kExcitationCalibrationPeak;   // 1/P
    /// Residual notches at the 2fs stage (plan S2.2): frequency as a fraction of fs, and Q.
    static constexpr std::array<float, 3> kExcitationNotchFractions{0.50f, 0.52f, 0.55f};
    static constexpr std::array<float, 3> kExcitationNotchQ{1.5f, 2.0f, 1.5f};
    static constexpr std::size_t kExcitationTablePhases = 512;   // K, plan S5.4 "Tables"

    /// FR-043: non-finite -> previous (pending) value; denormal (exponent bits 0) -> 0.0f;
    /// otherwise clamp. Phase 1's sanitizeField is unchanged.
    [[nodiscard]] static float sanitizeExcitationField(float x, float previous, float lo, float hi) noexcept {
        if (!detail::isFinite(x))
            return previous;
        if ((std::bit_cast<std::uint32_t>(x) & 0x7F800000u) == 0u)
            return 0.0f;
        return std::clamp(x, lo, hi);
    }

    /// prepare() only (control thread): filters, factor, smoother times, tables.
    void prepareExcitation(double sr) noexcept {
        const float fs = static_cast<float>(sr);
        const std::size_t factor = sr <= 100000.0 ? std::size_t{4} : std::size_t{2};
        const float osRate = fs * static_cast<float>(factor);
        const float twoXRate = 2.0f * fs;
        for (ExcitationChannel& ch : excChannels_) {
            ch.os.setFactor(factor);
            // Phase 9 close (2026-10-21): double-precision state. The float32 TDF2 at fc / fs = 0.01 .. 0.0026
            // rounded coherently with the input and left a -100 .. -118 dBFS quasi-periodic floor that the chain's
            // later stages lifted to -65 .. -78 dBFS at the voice sum (Phase 9 SC-021(a)).
            ch.splitBase.configure(kExcitationSplitHz, kButterworthQ, fs);
            ch.splitOs.configure(kExcitationSplitHz, kButterworthQ, osRate);
            for (std::size_t k = 0; k < ch.notch.size(); ++k)
                ch.notch[k].configure(FilterType::Notch, kExcitationNotchFractions[k] * fs, kExcitationNotchQ[k],
                                      0.0f, twoXRate);
            ch.dc.prepare(sr, kExcitationDcCornerHz);
        }
        mixS_.configure(kExcitationSmoothMs, fs);
        log2DriveS_.configure(kExcitationSmoothMs, fs);
        biasS_.configure(kExcitationSmoothMs, fs);
        computeExcitationTables();
    }

    /// Plan S5.4 "Tables", in double: per (log2 D, b) cell, over K phases, y_k = f(D·sin φ_k + b) − f(b)
    /// with f(v) = v/√(1 + v²) (the Sigmoid::recipSqrt law, core/sigmoid.h), mean removed;
    /// M = (P/√2)/rms(y); c = M·(2/K)·Σ y_k sin φ_k / P.
    void computeExcitationTables() noexcept {
        constexpr std::size_t kK = kExcitationTablePhases;
        std::array<double, kK> sinPhi{};
        for (std::size_t k = 0; k < kK; ++k)
            sinPhi[k] = std::sin(2.0 * kPiDouble * static_cast<double>(k) / static_cast<double>(kK));
        const double p = static_cast<double>(kExcitationCalibrationPeak);
        const auto f = [](double v) noexcept { return v / std::sqrt(1.0 + v * v); };
        std::array<double, kK> y{};
        for (std::size_t i = 0; i < detail::kExcitationTableDrivePoints; ++i) {
            const double drive = std::exp2(-4.0 + static_cast<double>(i) / 8.0);
            for (std::size_t j = 0; j < detail::kExcitationTableBiasPoints; ++j) {
                const double b = static_cast<double>(j) / 16.0;
                const double fb = f(b);
                double mean = 0.0;
                for (std::size_t k = 0; k < kK; ++k) {
                    y[k] = f(drive * sinPhi[k] + b) - fb;
                    mean += y[k];
                }
                mean /= static_cast<double>(kK);
                double sumSq = 0.0;
                double sumSin = 0.0;
                for (std::size_t k = 0; k < kK; ++k) {
                    const double v = y[k] - mean;
                    sumSq += v * v;
                    sumSin += v * sinPhi[k];
                }
                const double rms = std::sqrt(sumSq / static_cast<double>(kK));   // > 0: drive >= 1/16
                const double makeup = (p / std::sqrt(2.0)) / rms;
                const double h1 = makeup * (2.0 / static_cast<double>(kK)) * sumSin / p;
                const std::size_t cell = i * detail::kExcitationTableBiasPoints + j;
                excMakeup_[cell] = static_cast<float>(makeup);
                excH1_[cell] = static_cast<float>(h1);
            }
        }
    }

    /// prepare()/reset() (plan S5.6): latch the pending excitation, snap the three coordinate
    /// smoothers to it, reset every per-channel state (ADAA v1 = b, s1 = √(1 + b²)), clear the peak.
    void initialiseExcitation() noexcept {
        latchedExcitation_ = pendingExcitation_;
        excitationDirty_ = false;
        latchedDriveTarget_ = excitationDriveLaw(latchedExcitation_.drive) * edgeDriveGain(latchedControls_.edge);
        const float b = latchedExcitation_.asymmetry * kExcitationAsymmetryBias;
        mixS_.snapTo(latchedExcitation_.mix);
        log2DriveS_.snapTo(std::log2(latchedDriveTarget_));
        biasS_.snapTo(b);
        resetExcitationChannels(b);
        excitationPreCeilingPeak_ = 0.0f;
    }

    /// controlUpdate step 1b (plan S5.3): latch, D_eff, smoother targets (coordinates only; M and
    /// c are never smoothed). Engaging from idle (FR-007): the linear front end (`splitBase`, the
    /// interpolator, `splitOs`) kept running while idle, so it is valid at the first non-zero mix;
    /// only the parts that carried the residual, which is zero while idle, are cleared.
    void latchExcitation() noexcept {
        latchedExcitation_ = pendingExcitation_;
        excitationDirty_ = false;
        latchedDriveTarget_ = excitationDriveLaw(latchedExcitation_.drive) * edgeDriveGain(latchedControls_.edge);
        mixS_.setTarget(latchedExcitation_.mix);
        log2DriveS_.setTarget(std::log2(latchedDriveTarget_));
        biasS_.setTarget(latchedExcitation_.asymmetry * kExcitationAsymmetryBias);
        if (excitationIdle_ && latchedExcitation_.mix > 0.0f) {
            engageExcitationFromIdle();
            excitationIdle_ = false;
        }
    }

    /// Zero the oversampler, split, notch and DC state of both channels; ADAA history at bias b.
    void resetExcitationChannels(float b) noexcept {
        const float s = excitationRoot(b);   // the per-sample expression (R-2)
        for (std::size_t c = 0; c < excChannels_.size(); ++c) {
            ExcitationChannel& ch = excChannels_[c];
            ch.os.reset();
            ch.splitBase.reset();
            ch.splitOs.reset();
            clearExcitationResidual(c);
            ch.v1 = b;
            ch.s1 = s;
            ch.buf.fill(0.0f);
        }
    }

    /// The residual-carrying state: decimator, notches and DC blocker (their input is zero while idle).
    void clearExcitationResidual(std::size_t c) noexcept {
        ExcitationChannel& ch = excChannels_[c];
        ch.os.resetDownsampler();
        for (Biquad& n : ch.notch)
            n.reset();
        ch.dc.reset();
        ch.dcIn = 0.0f;
        ch.dcOut = 0.0f;
    }

    /// Engage from idle (FR-007): clear the residual-carrying state and seed the ADAA history from
    /// the last interpolated sample at the current smoothed coordinates, so the first non-idle
    /// sample sees a continuous argument. splitBase, the interpolator and splitOs are left warm.
    void engageExcitationFromIdle() noexcept {
        const std::size_t last = excChannels_[0].os.factor() - 1;
        const float dOverP = std::exp2(log2DriveS_.getCurrentValue()) * kExcitationInvCalibrationPeak;
        const float b = biasS_.getCurrentValue();
        for (std::size_t c = 0; c < excChannels_.size(); ++c) {
            clearExcitationResidual(c);
            ExcitationChannel& ch = excChannels_[c];
            ch.v1 = dOverP * ch.buf[last] + b;
            ch.s1 = excitationRoot(ch.v1);
        }
    }

    /// Idle tail (FR-007, returning to mix 0 click-free): the DC blocker output left over from the
    /// residual's DC decays at the 2 Hz corner; the stage goes idle only once it is below this.
    static constexpr float kExcitationIdleTail = 1.0e-6f;

    /// √(1 + v²): F1 of the closed-form ADAA1 (plan S5.4). The reset history, the static offset fb
    /// and the per-sample s all go through this one helper, so a silent input gives exactly 0.0
    /// whatever FP contraction a compiler applies (plan R-2, D-13).
    [[nodiscard]] static float excitationRoot(float v) noexcept { return std::sqrt(1.0f + v * v); }

    /// Closed-form ADAA1 of v/√(1 + v²): (v + v1)/(s + s1) with s = excitationRoot(v),
    /// s1 = excitationRoot(v1); no cancellation, no ε branch (plan S5.4). Also gives fb at v = v1 = b.
    [[nodiscard]] static float excitationAdaa(float v, float v1, float s, float s1) noexcept {
        return (v + v1) / (s + s1);
    }

    /// smoothstep(t) = t²(3 − 2t) on t clamped to [0, 1] (the split-engage weight w, plan S2.1).
    [[nodiscard]] static float excitationSmoothstep(float t) noexcept {
        t = std::min(std::max(t, 0.0f), 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    /// Idle: only the linear front end runs (`splitBase`, the interpolator into `buf`, `splitOs`), on
    /// the unmodified bank output, so it is valid when the stage engages (FR-007). The output is
    /// not touched (FR-006 bit-identity).
    void trackIdleSplit(const float* left, const float* right, std::size_t numSamples) noexcept {
        const std::size_t numChannels = (right == left) ? std::size_t{1} : std::size_t{2};
        const std::array<const float*, 2> in{left, right};
        const std::size_t factor = excChannels_[0].os.factor();
        for (std::size_t c = 0; c < numChannels; ++c) {
            ExcitationChannel& ch = excChannels_[c];
            for (std::size_t k = 0; k < numSamples; ++k) {
                const float x = in[c][k];
                static_cast<void>(ch.splitBase.process(x));
                ch.os.upsample(x, ch.buf.data());
                for (std::size_t i = 0; i < factor; ++i)
                    static_cast<void>(ch.splitOs.process(ch.buf[i]));
            }
        }
    }

    /// True once the mix sits exactly at 0 with target 0 and every active channel's DC tail is
    /// below kExcitationIdleTail.
    [[nodiscard]] bool excitationCanIdle(std::size_t numChannels) const noexcept {
        if (mixS_.getCurrentValue() != 0.0f || mixS_.getTarget() != 0.0f)
            return false;
        for (std::size_t c = 0; c < numChannels; ++c) {
            if (std::abs(excChannels_[c].dcOut) > kExcitationIdleTail)
                return false;
        }
        return true;
    }

    /// Plan S5.4, per sample, in place on one bank run. Both channels share the smoothed table
    /// coordinates and the looked-up M and c; right == left runs channel 0 only (FR-012).
    /// Idle (plan S2.1/S5.3): once the mix smoother sits exactly at 0 with target 0 and the DC tail
    /// has decayed (excitationCanIdle), the stage stops before the next sample and leaves the rest
    /// untouched (bit-exact bank output) apart from the front-end tracking. The test is
    /// per sample rather than per run, so the switch point never depends on the host partition
    /// (SC-017).
    void exciteRun(float* left, float* right, std::size_t numSamples) noexcept {
        const std::size_t numChannels = (right == left) ? std::size_t{1} : std::size_t{2};
        const std::array<float*, 2> io{left, right};
        const std::size_t factor = excChannels_[0].os.factor();
        for (std::size_t k = 0; k < numSamples; ++k) {
            if (excitationCanIdle(numChannels)) {
                excitationIdle_ = true;
                trackIdleSplit(left + k, right + k, numSamples - k);
                return;
            }
            const float m = mixS_.process();
            const float l2 = log2DriveS_.process();
            const float b = biasS_.process();
            const float dOverP = std::exp2(l2) * kExcitationInvCalibrationPeak;   // D · (1/P)
            const float mM = m * detail::bilinearExcitationTable(excMakeup_, l2, b);
            const float ce = detail::bilinearExcitationTable(excH1_, l2, b);
            const float cm = 1.0f - m + m * ce;
            const float oneMinusM = 1.0f - m;
            const float oneMinusW = 1.0f - excitationSmoothstep(m / kExcitationSplitEngageMix);
            const float sb = excitationRoot(b);
            const float fb = excitationAdaa(b, b, sb, sb);   // FR-009 static offset, same expression

            for (std::size_t c = 0; c < numChannels; ++c) {
                ExcitationChannel& ch = excChannels_[c];
                const float x = io[c][k];

                // Zero-delay linear base band.
                const float lo = ch.splitBase.process(x);
                const float base = cm * (lo + oneMinusW * (x - lo));

                // Oversampled residual: m·M·(Φ − fb) + (1 − m)·u − cm·(LPo(u) + (1 − w)(u − LPo(u))).
                ch.os.upsample(x, ch.buf.data());
                for (std::size_t i = 0; i < factor; ++i) {
                    const float u = ch.buf[i];
                    const float v = dOverP * u + b;
                    const float s = excitationRoot(v);
                    const float y = excitationAdaa(v, ch.v1, s, ch.s1);
                    ch.v1 = v;
                    ch.s1 = s;
                    const float ul = ch.splitOs.process(u);
                    ch.buf[i] = mM * (y - fb) + oneMinusM * u - cm * (ul + oneMinusW * (u - ul));
                }

                // Decimate through the 2fs notch hook (plan S2.2), then the 2 Hz DC blocker.
                auto& notch = ch.notch;
                const float r = ch.os.downsample(ch.buf.data(), [&notch](float t) noexcept {
                    for (Biquad& bq : notch)
                        t = bq.process(t);
                    return t;
                });
                ch.dcIn = r;
                ch.dcOut = ch.dc.process(r);

                const float pre = base + ch.dcOut;
                excitationPreCeilingPeak_ = std::max(excitationPreCeilingPeak_, std::abs(pre));
                io[c][k] = std::clamp(pre, -kExcitationCeiling, kExcitationCeiling);
            }
        }
    }

    /// stateFinite()'s Phase 2 terms (plan S5.6).
    [[nodiscard]] bool excitationStateFinite() const noexcept {
        for (const ExcitationChannel& ch : excChannels_) {
            if (!ch.os.stateFinite())
                return false;
            if (!ch.splitBase.stateFinite() || !ch.splitOs.stateFinite())
                return false;
            for (const Biquad& n : ch.notch) {
                if (!detail::isFinite(n.getZ1()) || !detail::isFinite(n.getZ2()))
                    return false;
            }
            if (!detail::isFinite(ch.dcIn) || !detail::isFinite(ch.dcOut) || !detail::isFinite(ch.v1)
                || !detail::isFinite(ch.s1))
                return false;
            for (const float v : ch.buf) {
                if (!detail::isFinite(v))
                    return false;
            }
        }
        return detail::isFinite(mixS_.getCurrentValue()) && detail::isFinite(log2DriveS_.getCurrentValue())
               && detail::isFinite(biasS_.getCurrentValue());
    }

    /// One channel of excitation state (plan S5.2).
    struct ExcitationChannel {
        PolyphaseHalfbandOversampler os;
        LowpassBiquadD splitBase;   // Phase 9 close: double state (see prepareExcitation)
        LowpassBiquadD splitOs;
        std::array<Biquad, 3> notch{};
        DCBlocker dc;
        float dcIn = 0.0f;    // last value passed to dc.process (== its private x1_, dc_blocker.h)
        float dcOut = 0.0f;   // last value dc.process returned (== its private y1_)
        float v1 = 0.0f;      // ADAA history: previous argument
        float s1 = 1.0f;      // ADAA history: √(1 + v1²)
        std::array<float, PolyphaseHalfbandOversampler::kMaxFactor> buf{};
    };

    std::array<ExcitationChannel, 2> excChannels_{};

    ExcitationControls pendingExcitation_{};
    ExcitationControls latchedExcitation_{};
    bool excitationDirty_ = false;
    bool excitationIdle_ = true;
    float excitationPreCeilingPeak_ = 0.0f;

    // ---- Phase 15 state ----
    float stiffnessAmount_ = 0.0f;
    float stiffnessNoteHz_ = static_cast<float>(kStiffnessReferenceHz);
    double stiffnessB_ = 0.0;
    bool stretchDirty_ = false;
    std::array<double, kMaxPartials> stretch_{};    // r_n at stiffnessB_
    std::array<float, kMaxPartials> stretchF_{};    // r_n as float, for the mask
    float phaseAmount_ = 0.0f;
    std::array<double, kMaxPartials> phaseTarget_{};
    std::array<double, kMaxPartials> phaseApplied_{};
    std::array<double, kMaxPartials> glideHz_{};    // frequency offset of the coming U samples
    bool phaseGliding_ = false;
    double maxGlideHz_ = 0.0;
    static constexpr int kSeedSteps = 4;            // note-on rotation: 4 · kPitchUpdateInterval samples
    int seedStepsLeft_ = 0;
    std::array<double, kMaxPartials> seedStep_{};

    OnePoleSmoother mixS_;
    OnePoleSmoother log2DriveS_;
    OnePoleSmoother biasS_;
    float latchedDriveTarget_ = 0.0f;   // what effectiveExcitationDrive() returns

    std::array<float, detail::kExcitationTableSize> excMakeup_{};
    std::array<float, detail::kExcitationTableSize> excH1_{};
};

inline constexpr ProfundumCore::ExcitationControls ProfundumCore::kDefaultExcitation{};

}  // namespace Krate::DSP
