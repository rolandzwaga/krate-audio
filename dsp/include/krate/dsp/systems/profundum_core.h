// ==============================================================================
// Layer 3: System Component — Profundum Phase 1 (specs/profundum-phase1-harmonic-core)
// ==============================================================================
// ProfundumCore: the zero-latency harmonic core of the Profundum bass
// instrument (SpectralShapeRecipe -> HarmonicOscillatorBank). Includes Layer 0
// and Layer 2 only.
//   Spec:  specs/profundum-phase1-harmonic-core/spec.md
//   Plan:  specs/profundum-phase1-harmonic-core/plan.md (S4)
//   Tasks: specs/profundum-phase1-harmonic-core/tasks.md
//
// Header-only, no heap allocation: every buffer is a fixed-size member (FR-040).
// prepare() and reset() are control-thread calls; every other method is
// audio-thread safe and a no-op before prepare (FR-041, FR-061).
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>                         // detail::isFinite (L0)
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

/// @note Precondition: the owner runs processBlock with FTZ/DAZ on (ScopedDenormalMode on x86, the
///       default FPCR on AArch64). Without it, zero-target bank lanes stall as denormals (S7).
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

        // Step 4: cap and the shape-step ceiling, Δmax = 375·√P0·kControlInterval/fs (0.125594 at 48 kHz).
        capHz_ = SpectralShapeRecipe::capFrequency(sr);
        maxStepPerInterval_ = static_cast<float>(
            static_cast<double>(kMaxGainSlewPerSecOverSqrtP0)
            * std::sqrt(static_cast<double>(SpectralShapeRecipe::kPowerTarget))
            * static_cast<double>(kControlInterval) / sr);

        // Steps 3, 5, 6.
        initialiseState(SpectralShapeRecipe::kDefaultControls);
    }

    /// @note Control thread; allocation-free and bounded (array fills + one evaluateShape), S4.3.
    ///       The RT-safe way to clear a sounding core is a Reset-policy noteOn.
    void reset() noexcept {
        if (!prepared_)
            return;
        bank_.reset();   // not bank_.prepare: coefficients, crossfade and noise calibration stay valid
        initialiseState(pendingControls_);   // step 5 latches the pending controls, not the defaults
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
            i += run;
            intervalPhase_ = static_cast<std::uint32_t>(
                (static_cast<std::size_t>(intervalPhase_) + run) % kControlInterval);
        }
        // Undo the bank headroom (exact, SC-009). An aliased left == right buffer is scaled once.
        for (std::size_t k = 0; k < numSamples; ++k)
            left[k] *= kBankHeadroom;
        if (right != left) {
            for (std::size_t k = 0; k < numSamples; ++k)
                right[k] *= kBankHeadroom;
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
               && detail::isFinite(maskF0_) && detail::isFinite(noteOnF0_);
    }

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
    }

    /// S4.4 normative order. Steady state (no dirty controls, converged slew, f bit-unchanged)
    /// does comparisons only and never calls loadFrame (FR-042 skip rule).
    void controlUpdate(float f0) noexcept {
        // 1. Latch.
        if (controlsDirty_) {
            latchedControls_ = pendingControls_;
            evaluateTargetShape();
            shapeConverged_ = std::memcmp(targetShape_.data(), shape_.data(), activeBytes()) == 0;
            controlsDirty_ = false;
        }

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

        // 4. Mask: rebuilt only when f moved (bitwise), never slewed (FR-051).
        bool maskChanged = false;
        if (!sameBits(f0, maskF0_)) {
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
            const bool baseChanged = !sameBits(baseNew, baseF0_);
            if (deliveredChanged || baseChanged) {
                if (baseChanged)
                    setBase(baseNew);
                refreshMultipliers(f0);
                deliverToBank(baseChanged);
            } else if (fChanged) {
                refreshMultipliers(f0);
            }
        }
        // 7. Pan (FR-048).
        forwardPan();
    }

    /// S4.6, U-cadence (non-control samples): mask and base stay untouched until the next control
    /// update.
    void pitchUpdate(float f0) noexcept {
        if (sameBits(f0, currentF0_))
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

            // 6. Multipliers, then loadFrame: phases seeded from partial.phase = 0, so the first
            //    sample is 0.0 (FR-045).
            refreshMultipliers(f);
            deliverToBank(true);
        } else {
            // 3. FreeRunning while sounding: keep the MCF state, shape_ (the slew continues) and
            //    the grid. Every step is gated by a bit comparison, so a noteOn at an unchanged f0
            //    with unchanged controls touches no bank state (SC-012).
            if (controlsDirty_) {
                latchedControls_ = pendingControls_;
                evaluateTargetShape();
                shapeConverged_ = std::memcmp(targetShape_.data(), shape_.data(), activeBytes()) == 0;
                controlsDirty_ = false;
            }

            // 4. Mask at the new f0 and deliver (FR-051(d)).
            bool deliveredChanged = false;
            if (!sameBits(f, maskF0_)) {
                buildMask(f);
                deliverMaskedShape();
                deliveredChanged = true;
            }

            // 5-6. Base = f with no chase clamp (a > 1-semitone jump crossfades, E-7), then the
            //      multipliers, then loadFrame.
            const bool baseChanged = !sameBits(f, baseF0_);
            if (deliveredChanged || baseChanged) {
                if (baseChanged)
                    setBase(f);
                refreshMultipliers(f);
                deliverToBank(baseChanged);
            } else if (fChanged) {
                refreshMultipliers(f);
            }
        }

        // 7. Pan forwarding (FR-048).
        forwardPan();
        sounding_ = true;
        noteOnPending_ = false;
    }

    /// S4.4 step 7 (FR-048): non-zero offsets go through applyPanOffsets; the return to all-zero
    /// restores the exact centre tables with restoreCenterPan (pan tables only: phase, amplitude
    /// and detune state stay untouched, SC-014(c)). An all-zero vector that was never applied
    /// makes no bank call at all (SC-014(a)).
    void forwardPan() noexcept {
        if (!panDirty_)
            return;
        if (panNonZero_) {
            bank_.applyPanOffsets(panOffsets_);
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
        SpectralShapeRecipe::evaluateShape(
            latchedControls_, std::span<float>(targetShape_.data(), static_cast<std::size_t>(numPartials_)));
    }

    /// S4.4 step 4: mask_ = evaluateMask(f), maskF0_ = f.
    void buildMask(float f0) noexcept {
        SpectralShapeRecipe::evaluateMask(
            f0, sampleRate_, std::span<float>(mask_.data(), static_cast<std::size_t>(numPartials_)));
        maskF0_ = f0;
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
};

}  // namespace Krate::DSP
