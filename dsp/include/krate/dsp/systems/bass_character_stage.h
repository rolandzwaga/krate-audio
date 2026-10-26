// ==============================================================================
// Layer 3: System - Profundum Phase 5 (specs/profundum-phase5-shape-character)
// ==============================================================================
// BassCharacterStage: the CHARACTER stage, nested enum class Algorithm
// { Tube, Fold, Rectify, Bite, Crush, Fracture } (spec New components; plan S5).
//   Carries: FR-020..FR-028, FR-030..FR-036, FR-050..FR-057, FR-066, FR-067, FR-069, FR-072;
//            SC-005..SC-013, SC-021, SC-022, SC-024..SC-029.
//   Spec:  specs/profundum-phase5-shape-character/spec.md
//   Plan:  specs/profundum-phase5-shape-character/plan.md (S2, S5.1-S5.7, C-16..C-19, C-21, C-23, C-24)
//   Tests: dsp/tests/unit/systems/bass_character_stage_test.cpp (+ the spectral / r-law / chain /
//          nonfinite / perf TUs); probe: dsp/tests/unit/systems/bass_character_stage_probe.h
//
// T009 (this skeleton): algorithm switching (S5.6, C-21), Amount-0 bypass (FR-022, FR-054),
// parameter smoothing, per-channel offsets (FR-027, C-17), the f0 feed with the re-key rule
// (FR-052, S5.3 step 1), the mono link (S2), the r observer (S5.7, C-18) and the Rectify law
// (S5.4, C-19). T011: the Bite and Tube laws (S5.4, C-8, C-10, C-11; tasks.md deviation 9) with
// their prepare-time h1 tables c_bite[65] / c_tube[65]. T014: the Fold law (WavefolderProcessor
// Serge, theta_max 2.5 rad, C-12; c_fold[65]) and the Crush law (two parity-keyed hold engines with
// 3-slot B-spline step rings and two BitCrushers, FR-051, D-6). T015: the Fracture law (C-5 combined
// polarity alternation + bias-flip tanh on a ÷2 phasor, TanhADAA; C-24 rotating phasor and rational
// tanh, no std::sin / std::tanh per sample; c_frac[65]). All six algorithms carry their laws.
//
// C-23 (REQUIRED by the T001 epoch-2 decision): only each branch's nonlinearity runs at F·fs.
// Pre-tilts (Tube, Bite) run at the base rate before the upsample; every post tilt runs at the
// base rate on the round-tripped output after the downsample. Because two running branches have
// different pre and post tilts, each running branch owns a SLOT: its own OversampledResidualPath
// (one up, two down). Steady state runs one slot (one round trip, the plan's cost); a 10 ms
// algorithm crossfade runs two identical round trips and mixes their post-tilted outputs with the
// C-16 gains, so both sides carry the same delay and the mix never combs (plan S2 "shared
// domain"). The incoming slot's path is reset at start(), where its gain is 0 (T009 run-log).
//
// Branch API (tasks.md deviation 10, adapted to C-23): prepare(double baseRate, double
// processRate), reset(), setFundamentalHz(float), configure(float amount, float tone),
// float pre(float x) [base rate], BranchOut process(float u) [F·fs; y BEFORE the post tilt,
// lin = the branch's linear reference], float post(float z) [base rate].
//
// Includes L0-L2 only; never includes shape_morph.h (the other Phase 5 L3 header).
// RT: heap-free, fixed-size members, every audio-thread method noexcept, no std::function.
// Precondition: FTZ/DAZ on (halfband sections are not flushed per call).
// ==============================================================================

#pragma once

#include <krate/dsp/core/closed_form_adaa2.h>
#include <krate/dsp/core/crossfade_utils.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/sigmoid.h>
#include <krate/dsp/primitives/bit_crusher.h>
#include <krate/dsp/primitives/polyphase_halfband_oversampler.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/primitives/tanh_adaa.h>
#include <krate/dsp/processors/harmonic_fold.h>
#include <krate/dsp/processors/keytracked_tilt.h>
#include <krate/dsp/processors/oversampled_residual_path.h>
#include <krate/dsp/processors/rectifier.h>
#include <krate/dsp/processors/tube_stage.h>
#include <krate/dsp/processors/wavefolder_processor.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace Krate::DSP {

namespace detail {
struct BassCharacterStageProbe;
}  // namespace detail

/// The CHARACTER stage (FR-020): six algorithms, Amount + Tone, zero latency, in place.
/// `right == left` processes one channel (HarmonicFold convention).
class BassCharacterStage {
public:
    enum class Algorithm : std::uint8_t { Tube, Fold, Rectify, Bite, Crush, Fracture };  // nested (spec ODR row)

    static constexpr std::size_t kNumAlgorithms = 6;
    static constexpr float kSwitchCrossfadeMs = 10.0f;         // D-7
    static constexpr float kBypassCrossfadeMs = 5.0f;          // FR-054, C-16
    static constexpr float kSwitchCrossfadeExponent = 1.493f;  // C-16: -4.5 dB midpoint
    static constexpr float kParamSmoothMs = 10.0f;             // Amount, Tone, offsets (OnePoleSmoother)
    static constexpr float kMaxChannelOffset = 0.15f;          // D-12 (gated value; the setter clamps to [-1, 1])
    static constexpr float kOutputCeiling = 6.0f;              // FR-069
    static constexpr float kMinFundamentalHz = 8.0f;
    static constexpr float kMaxFundamentalHz = 20000.0f;
    static constexpr float kDefaultFundamentalHz = 65.40639f;  // C2 (tasks.md deviation 13)

    // Tone (FR-055, C-8): pre tilt pivot 1·f0 (Tube, Bite), post tilt pivot 2·f0 (all six).
    static constexpr float kPreTiltPivot = 1.0f;
    static constexpr float kPostTiltPivot = 2.0f;
    static constexpr float kTubeBiteTiltDb = 6.0f;   // Tube: pre and post ±6 dB; Bite: post ±6 dB
    static constexpr float kBitePreTiltDb = 9.0f;    // Bite: pre ±9 dB (user ruling 2026-10-16, SC-028(d))
    static constexpr float kPostTiltDb = 12.0f;      // Fold, Rectify, Crush, Fracture: post ±12 dB

    // Rectify (S5.4): wet = min(1, A / kRectHalfEnd); b = smoothstep((A - kRectHalfEnd) / (1 - kRectHalfEnd)).
    static constexpr float kRectHalfEnd = 0.4f;

    // Fold (S5.4, C-12): theta = kFoldThetaMin + (kFoldThetaMax - kFoldThetaMin)·A rad at the calibration peak.
    static constexpr float kFoldThetaMin = 0.1f;  // == WavefolderProcessor::kMinFoldAmount
    static constexpr float kFoldThetaMax = 2.5f;  // C-12: 3.0 fails SC-010 by 0.07 dB

    // Crush (S5.4, FR-051, D-6): hold multiple m(A) = Lo·(Hi/Lo)^A = 64·(64/3)^-A; bits B(A) = Lo + (Hi - Lo)·A
    // = 12 - 8·A; q = P/kCrushPreScale·BitCrusher(kCrushPreScale·value/P).
    static constexpr float kCrushMultipleLo = 64.0f;  // at A = 0
    static constexpr float kCrushMultipleHi = 3.0f;   // at A = 1 (user ruling 2026-10-16, round 7: m = 3; was 2.5, 2, 4)
    static constexpr float kCrushBitsLo = 12.0f;      // at A = 0
    static constexpr float kCrushBitsHi = 4.0f;       // at A = 1
    static constexpr float kCrushPreScale = 0.9f;
    static constexpr std::size_t kCrushRingSlots = 3;  // two active steps at most (S5.4 bound) + one spare
    /// Hold-grid phase (T022): the m sample points per period sit at θ = (k + kCrushGridOffset)/m, i.e. half a
    /// hold step away from θ = 0, where the core's waveforms (sums of sine-phase partials) have their edge.
    static constexpr double kCrushGridOffset = 0.5;
    /// Per-slot quantiser hysteresis (FR-051 as amended, Clarifications "Crush threshold flicker" and "Group 9
    /// rulings, round 4"): a hold slot stores the held input value v_ref at which it last quantised and the level
    /// q_ref that produced; a new held value v re-quantises (q_ref = quantise(v), v_ref = v) only when the slot is
    /// invalid or |v - v_ref| > kCrushHysteresisSteps·step, otherwise the slot outputs q_ref. An interpolation-error
    /// wobble of a hold point near a threshold is far below a quarter step, so it cannot flicker between periods;
    /// a slot whose input moved by more than a quarter step re-quantises, so no warm-up level latches (round 4:
    /// the earlier band around the level held a start-up level whenever the steady input sat within 0.75 step of
    /// it). Noiseless: no dither, no jitter.
    static constexpr float kCrushHysteresisSteps = 0.25f;
    /// Slot memory is dropped when the bit depth has moved by this much since the memory was keyed (an Amount
    /// move: the smoothed bits change continuously, so a fixed fraction of a bit rather than any change).
    static constexpr float kCrushHysteresisRekeyBits = 1.0f / 16.0f;
    /// Slot memory per engine: m_hi reaches m(0) + 1 = 65 at Amount 0.
    static constexpr std::size_t kCrushMaxSlots = static_cast<std::size_t>(kCrushMultipleLo) + 1;

    // Fracture (S5.4, C-5): g = Lo + (Hi - Lo)·A = 0.5 + 1.5·A; β = kFractureBias·A = 0.5·A;
    // σ(φ) = tanh(kFractureSquareSharpness·sin πφ) / tanh(kFractureSquareSharpness).
    static constexpr float kFractureDriveLo = 0.5f;          // g at A = 0
    static constexpr float kFractureDriveHi = 2.0f;          // g at A = 1
    static constexpr float kFractureBias = 0.5f;             // β at A = 1
    static constexpr float kFractureSquareSharpness = 4.0f;  // σ's tanh argument scale

    /// r-observer hysteresis edges (S5.7, C-4, C-5) at Tone {0, 0.25, 0.5, 0.75, 1}.
    struct FlipEdges {
        float lo;
        float hi;
    };

    // HARVEST-BEGIN kRectifyFlipEdges
    static constexpr std::array<FlipEdges, 5> kRectifyFlipEdges{{
        {.lo = 0.83984375f, .hi = 0.91015625f},
        {.lo = 0.83203125f, .hi = 0.90234375f},
        {.lo = 0.78515625f, .hi = 0.87109375f},
        {.lo = 0.73046875f, .hi = 0.83203125f},
        {.lo = 0.72265625f, .hi = 0.82421875f},
    }};
    // HARVEST-END kRectifyFlipEdges

    // HARVEST-BEGIN kFractureFlipEdges
    static constexpr std::array<FlipEdges, 5> kFractureFlipEdges{{
        {.lo = 0.19140625f, .hi = 0.33203125f},
        {.lo = 0.21484375f, .hi = 0.34765625f},
        {.lo = 0.33203125f, .hi = 0.44140625f},
        {.lo = 0.44921875f, .hi = 0.53515625f},
        {.lo = 0.47265625f, .hi = 0.55859375f},
    }};
    // HARVEST-END kFractureFlipEdges

    /// One F·fs sample of a branch: y before the post tilt, lin = its linear reference (plan S2).
    struct BranchOut {
        float y;
        float lin;
    };

    // -------------------------------------------------------------------------
    // Control thread
    // -------------------------------------------------------------------------

    /// Prepares every member at its rate (S5.2), then reset(). maxBlockSize is unused: the core is
    /// per-sample, so any numSamples is accepted (spec Edge Cases "Block size").
    void prepare(double sampleRate, [[maybe_unused]] std::size_t maxBlockSize) noexcept {
        sampleRate_ = sampleRate;
        const auto fs = static_cast<float>(sampleRate);
        for (std::size_t c = 0; c < ch_.size(); ++c) {
            auto& ch = ch_[c];
            for (auto& s : ch.slot)
                s.path.prepare(sampleRate);
            const double processRate = sampleRate * static_cast<double>(ch.slot[0].path.factor());
            ch.tube.prepare(sampleRate, processRate);
            ch.fold.prepare(sampleRate, processRate);
            ch.rect.prepare(sampleRate, processRate);
            ch.bite.prepare(sampleRate, processRate);
            ch.crush.prepare(sampleRate, processRate, static_cast<std::uint32_t>(c));
            ch.frac.prepare(sampleRate, processRate);
            ch.amountSm.configure(kParamSmoothMs, fs);
            ch.toneSm.configure(kParamSmoothMs, fs);
        }
        engage_.configure(kBypassCrossfadeMs, fs);
        xfadeInc_ = crossfadeIncrement(kSwitchCrossfadeMs, sampleRate);
        xfadeLen_ = static_cast<std::size_t>(std::ceil(1.0 / static_cast<double>(xfadeInc_)));
        prepared_ = true;
        reset();
    }

    /// Restores every branch, path, smoother (snapped to its target), the switch machine
    /// (current = target, nothing pending), the bypass ramp, the peak and the hysteresis, then
    /// re-keys every branch with the current f0 (S5.3 step 1). Re-links the channels when the
    /// offsets are 0.
    void reset() noexcept {
        if (xfading_ || prerolling_)
            current_ = target_;
        target_ = current_;
        xfading_ = false;
        prerolling_ = false;
        prerollCount_ = 0;
        engagePreroll_ = false;
        engageCount_ = 0;
        hasPending_ = false;
        xfadeCount_ = 0;
        curSlot_ = 0;
        phaseSynced_ = false;  // FR-072: reset() clears the sync and restores phase 0
        syncCycles_ = 0.0;
        for (auto& ch : ch_) {
            for (auto& s : ch.slot)
                resetSlot(s);
            for (std::size_t a = 0; a < kNumAlgorithms; ++a)
                startBranch(ch, static_cast<Algorithm>(a));
            ch.amountSm.snapToTarget();
            ch.toneSm.snapToTarget();
        }
        const bool engaged = anyAmountTargetNonZero();
        engage_.snapTo(engaged ? 1.0f : 0.0f);
        bypassed_ = !engaged;
        peak_ = 0.0f;
        linked_ = offsetsZero();
        flippedRect_ = false;
        flippedFrac_ = false;
        updateObserver();
    }

    // -------------------------------------------------------------------------
    // Setters (audio thread; O(1) except a channel copy on the first unlink)
    // -------------------------------------------------------------------------

    /// D-7 / C-21 switch machine (S5.6). Out-of-range values are ignored.
    void setAlgorithm(Algorithm a) noexcept {
        if (static_cast<std::size_t>(a) >= kNumAlgorithms)
            return;
        if (inFlight()) {  // pre-roll or crossfade (FR-026)
            if (a == target_)
                hasPending_ = false;  // C-21: last request wins; starts nothing
            else {
                pending_ = a;
                hasPending_ = true;
            }
        } else if (a != current_) {
            start(a);
        }
        updateObserver();
    }

    /// Centre Amount in [0, 1] (sanitised: non-finite keeps the previous value, denormal -> 0, clamp).
    void setAmount(float a) noexcept {
        amount_ = sanitizeControl(a, amount_, 0.0f, 1.0f);
        retarget();
        updateObserver();
    }

    /// Centre Tone in [0, 1]; 0.5 is neutral.
    void setTone(float t) noexcept {
        tone_ = sanitizeControl(t, tone_, 0.0f, 1.0f);
        retarget();
        updateObserver();
    }

    /// FR-027: L = centre + offset, R = centre - offset, each clamped to [0, 1]. Offsets in [-1, 1].
    /// A nonzero offset unlinks the channels (they stay unlinked until reset()). Never moves r (FR-036).
    void setChannelOffsets(float amountOffset, float toneOffset) noexcept {
        amountOffset_ = sanitizeControl(amountOffset, amountOffset_, -1.0f, 1.0f);
        toneOffset_ = sanitizeControl(toneOffset, toneOffset_, -1.0f, 1.0f);
        if (linked_ && !offsetsZero()) {
            ch_[1] = ch_[0];
            linked_ = false;
        }
        retarget();
    }

    /// The per-block f0 fallback of FR-052: [8, 20000] Hz, non-finite keeps the previous value.
    /// Pushes to running branches only (S5.3 step 1 re-key rule), and only on a changed value (bit
    /// compare, as the f0PerSample feed does): an unchanged or rejected f0 is a no-op (FR-066), so
    /// Fracture's rotor is never re-seeded by a call that does not move f0.
    void setFundamentalHz(float hz) noexcept {
        const float f = sanitizeControl(hz, f0_, kMinFundamentalHz, kMaxFundamentalHz);
        if (std::bit_cast<std::uint32_t>(f) == std::bit_cast<std::uint32_t>(f0_))
            return;
        f0_ = f;
        pushF0ToRunning();
    }

    /// FR-072 phasor sync to the core: coreCycles is the core's h1 phase in cycles at the next sample
    /// processed (0 where its sine-phase partials start, i.e. where the waveforms have their edge). Sets
    /// Crush's hold phasor θ = frac(coreCycles) and Fracture's ÷2 phasor φ = coreCycles mod 2 (rotor
    /// re-seeded from φ) on both channels, whether or not the branch is running. From then on a stage
    /// clock (syncCycles_, mod 2) integrates f0 / fs every sample, bypassed or not, and startBranch()
    /// takes θ and φ from it instead of 0, so a switch or a bypass exit keeps the core's phase. reset()
    /// clears the sync. Non-finite coreCycles is ignored (FR-066). Call after prepare().
    void syncPhase(float coreCycles) noexcept {
        if (!detail::isFinite(coreCycles))
            return;
        const auto c = static_cast<double>(coreCycles);
        syncCycles_ = c - 2.0 * std::floor(0.5 * c);
        if (syncCycles_ >= 2.0)
            syncCycles_ = 0.0;
        phaseSynced_ = true;
        for (auto& ch : ch_) {
            ch.crush.setPhase(syncCycles_);
            ch.frac.setPhase(syncCycles_);
        }
    }

    /// FR-030, D-8: the input carries odd harmonics only. Default false.
    void setInputHalfWaveSymmetric(bool symmetric) noexcept {
        halfWaveSymmetric_ = symmetric;
        updateObserver();
    }

    [[nodiscard]] Algorithm algorithm() const noexcept { return target_; }
    [[nodiscard]] float amount() const noexcept { return amount_; }
    [[nodiscard]] float tone() const noexcept { return tone_; }
    [[nodiscard]] float fundamentalHz() const noexcept { return f0_; }

    // -------------------------------------------------------------------------
    // Audio thread
    // -------------------------------------------------------------------------

    /// In place. Before prepare, with a null channel or with 0 samples the buffers are left
    /// untouched. f0PerSample (optional, FR-052): one f0 per sample; a non-finite entry keeps the
    /// previous value, others clamp to [8, 20000] Hz.
    void processBlock(float* left, float* right, std::size_t numSamples,
                      const float* f0PerSample = nullptr) noexcept {
        if (!prepared_ || left == nullptr || right == nullptr || numSamples == 0)
            return;
        const std::size_t numChannels = (left == right) ? std::size_t{1} : std::size_t{2};
        const std::array<float*, 2> io{left, right};

        for (std::size_t n = 0; n < numSamples; ++n) {
            // (1) f0 (value-driven, so partition-invariant: SC-018).
            if (f0PerSample != nullptr) {
                const float f = sanitizeControl(f0PerSample[n], f0_, kMinFundamentalHz, kMaxFundamentalHz);
                if (std::bit_cast<std::uint32_t>(f) != std::bit_cast<std::uint32_t>(f0_)) {
                    f0_ = f;
                    pushF0ToRunning();
                }
            }

            // (2) sanitise; mono link (S2).
            std::array<float, 2> x{};
            for (std::size_t c = 0; c < numChannels; ++c)
                x[c] = detail::isFinite(io[c][n]) ? io[c][n] : 0.0f;
            if (linked_ && numChannels == 2
                && std::bit_cast<std::uint32_t>(x[0]) != std::bit_cast<std::uint32_t>(x[1])) {
                ch_[1] = ch_[0];
                linked_ = false;
            }
            const std::size_t active = linked_ ? std::size_t{1} : numChannels;

            // (4, first half) smoothers and the switch machine advance on every sample.
            std::array<float, 2> amt{};
            std::array<float, 2> tn{};
            for (std::size_t c = 0; c < active; ++c) {
                amt[c] = ch_[c].amountSm.process();
                tn[c] = ch_[c].toneSm.process();
            }
            const bool xf = xfading_;
            const bool pre = prerolling_;  // FR-026 pre-roll: the incoming branch runs unmixed
            float gOut = 1.0f;
            float gIn = 0.0f;
            if (xf) {
                ++xfadeCount_;
                const float pos = std::min(1.0f, static_cast<float>(xfadeCount_) * xfadeInc_);
                gOut = std::pow(std::max(0.0f, std::cos(pos * kHalfPi)), kSwitchCrossfadeExponent);
                gIn = std::pow(std::max(0.0f, std::sin(pos * kHalfPi)), kSwitchCrossfadeExponent);
            }

            // (3) bypass (FR-022, FR-054, C-16, C-17): e ramps to 0 once every active channel's
            // effective Amount target and smoothed value are 0; at e == 0 the output is the copy.
            bool wantEngaged = false;
            for (std::size_t c = 0; c < active; ++c)
                wantEngaged = wantEngaged || ch_[c].amountSm.getTarget() != 0.0f || amt[c] != 0.0f;
            const float eTarget = wantEngaged ? 1.0f : 0.0f;
            if (eTarget != engage_.getTarget()) {
                if (bypassed_ && wantEngaged)
                    leaveBypass();  // starts the FR-054 pre-roll; e stays 0 until it ends
                if (!engagePreroll_) {
                    if (seamHardBypass_)
                        engage_.snapTo(eTarget);
                    else
                        engage_.setTarget(eTarget);
                }
            }
            float e = 0.0f;
            if (!bypassed_) {
                e = engage_.process();
                if (e == 0.0f && !wantEngaged) {
                    bypassed_ = true;
                    engagePreroll_ = false;
                }
            }

            if (bypassed_) {
                for (std::size_t c = 0; c < active; ++c) {
                    peak_ = std::max(peak_, std::abs(x[c]));
                    io[c][n] = x[c];
                }
            } else {
                const std::size_t running = (xf || pre) ? std::size_t{2} : std::size_t{1};
                maxRunningBranches_ = std::max(maxRunningBranches_, running);
                const std::size_t inSlot = 1 - curSlot_;
                for (std::size_t c = 0; c < active; ++c) {
                    auto& ch = ch_[c];
                    float z = runSlot(ch, curSlot_, current_, x[c], amt[c], tn[c]);
                    if (engagePreroll_)
                        ch.slot[curSlot_].prerollSum += static_cast<double>(ch.slot[curSlot_].path.dcInput());
                    if (xf) {
                        z = gOut * z + gIn * runSlot(ch, inSlot, target_, x[c], amt[c], tn[c]);
                    } else if (pre) {  // heard: the outgoing branch alone; the incoming one warms up
                        static_cast<void>(runSlot(ch, inSlot, target_, x[c], amt[c], tn[c]));
                        ch.slot[inSlot].prerollSum += static_cast<double>(ch.slot[inSlot].path.dcInput());
                    }
                    const float y = (1.0f - e) * x[c] + e * z;
                    peak_ = std::max(peak_, std::abs(y));
                    io[c][n] = std::clamp(y, -kOutputCeiling, kOutputCeiling);
                }
                // FR-026: pre-roll end primes the incoming slot's residual DC blocker, then the crossfade starts.
                if (pre && ++prerollCount_ >= prerollLen_) {
                    for (std::size_t c = 0; c < active; ++c)
                        primeSlot(ch_[c].slot[inSlot], prerollLen_);
                    prerolling_ = false;
                    xfading_ = true;
                    xfadeCount_ = 0;
                }
                // FR-054: bypass-exit pre-roll end primes the running slot, then e ramps (or snaps) up.
                if (engagePreroll_ && ++engageCount_ >= engageLen_) {
                    for (std::size_t c = 0; c < active; ++c)
                        primeSlot(ch_[c].slot[curSlot_], engageLen_);
                    engagePreroll_ = false;
                    if (seamHardBypass_)
                        engage_.snapTo(1.0f);
                    else
                        engage_.setTarget(1.0f);
                }
            }
            if (active < numChannels)
                io[1][n] = io[0][n];

            // (S5.6) crossfade end: the last sample above used pos = 1.
            if (xf && xfadeCount_ >= xfadeLen_)
                finishCrossfade();

            // FR-072: the synced clock follows the f0 this sample ran at (the branches' own phasors
            // advance by the same f0 / fs over their F sub-samples).
            if (phaseSynced_) {
                syncCycles_ += static_cast<double>(f0_) / sampleRate_;
                if (syncCycles_ >= 2.0)
                    syncCycles_ -= 2.0;
            }
        }
    }

    /// FR-030 / FR-036 / C-18: r for target_ (the incoming algorithm during a crossfade, D-8) from
    /// the centre targets; offsets never enter.
    [[nodiscard]] float perceivedPitchRatio() const noexcept {
        switch (target_) {
            case Algorithm::Rectify:
                return (halfWaveSymmetric_ && flippedRect_) ? 2.0f : 1.0f;
            case Algorithm::Fracture:
                return flippedFrac_ ? 0.5f : 1.0f;
            case Algorithm::Tube:
            case Algorithm::Fold:
            case Algorithm::Bite:
            case Algorithm::Crush:
                break;
        }
        return 1.0f;
    }

    /// True when every piece of recurrent state is finite.
    [[nodiscard]] bool stateFinite() const noexcept {
        if (!detail::isFinite(engage_.getCurrentValue()))
            return false;
        for (const auto& ch : ch_) {
            if (!detail::isFinite(ch.amountSm.getCurrentValue()) || !detail::isFinite(ch.toneSm.getCurrentValue()))
                return false;
            for (const auto& s : ch.slot) {
                if (!s.path.stateFinite() || !detail::isFinite(static_cast<float>(s.prerollSum)))
                    return false;
                for (const float h : s.uHist) {
                    if (!detail::isFinite(h))
                        return false;
                }
            }
            if (!ch.tube.stateFinite() || !ch.fold.stateFinite() || !ch.rect.stateFinite()
                || !ch.bite.stateFinite() || !ch.crush.stateFinite() || !ch.frac.stateFinite())
                return false;
        }
        return true;
    }

    [[nodiscard]] static constexpr std::size_t latencySamples() noexcept { return 0; }

    /// Largest |output| before the kOutputCeiling clamp since prepare / reset / clear (SC-019).
    [[nodiscard]] float preCeilingPeak() const noexcept { return peak_; }
    void clearPreCeilingPeak() noexcept { peak_ = 0.0f; }

    [[nodiscard]] std::size_t oversamplingFactor() const noexcept { return ch_[0].slot[0].path.factor(); }

private:
    friend struct detail::BassCharacterStageProbe;  // SC-027 identity seam, SC-028 tone filters, SC-029 instance count

    static constexpr std::size_t kMaxFactor = OversampledResidualPath::kMaxFactor;

    // Calibration peak P (harmonic_fold.h:83) and the prepare-time Amount tables (S5.2, S5.4).
    static constexpr float kP = HarmonicFold::kCalibrationPeak;
    static constexpr float kInvP = 1.0f / kP;
    static constexpr std::size_t kTablePoints = 65;         // c_tube, c_bite over Amount (S5.2)
    static constexpr std::size_t kTablePhasePoints = 256;   // phase points per table entry (S5.4)
    static constexpr double kTwoPiD = 6.283185307179586476925286766559;
    static constexpr float kTubeGainLoDb = -6.0f;           // C-10: input gain -6..+12 dB
    static constexpr float kTubeGainHiDb = 12.0f;
    static constexpr float kTubeOutputGainLimitDb = 24.0f;  // S5.4: output gain clamped ±24 dB
    static constexpr float kBiteDriveLoDb = -12.0f;         // S5.4: g = 10^{(-12 + 30A)/20}
    static constexpr float kBiteDriveSpanDb = 30.0f;
    static constexpr double kPiD = 3.14159265358979323846264338327950;
    /// 1 / tanh(kFractureSquareSharpness) (tanh(4) = 0.99932929973906703); std::tanh is not constexpr.
    static constexpr float kFractureInvTanhSharpness = 1.0006711504016825f;
    static_assert(kFractureSquareSharpness == 4.0f, "kFractureInvTanhSharpness is 1/tanh(4)");

    using AmountTable = std::array<float, kTablePoints>;

    /// Linear interpolation of a 65-point Amount table at A in [0, 1].
    [[nodiscard]] static float tableAt(const AmountTable& t, float amount) noexcept {
        const float pos = std::clamp(amount, 0.0f, 1.0f) * static_cast<float>(kTablePoints - 1);
        const std::size_t i = std::min(static_cast<std::size_t>(pos), kTablePoints - 2);
        const float f = pos - static_cast<float>(i);
        return t[i] + f * (t[i + 1] - t[i]);
    }

    /// PREPARE-TIME ONLY: c_tube(A_i) = the h1 coefficient of Asymmetric::tube(g·P·sin θ)/P over 256
    /// midpoint phase points, g = 10^{(kTubeGainLoDb + (kTubeGainHiDb - kTubeGainLoDb)·A_i)/20} (S5.4).
    [[nodiscard]] static AmountTable makeTubeTable() noexcept {
        AmountTable t{};
        for (std::size_t i = 0; i < kTablePoints; ++i) {
            const double a = static_cast<double>(i) / static_cast<double>(kTablePoints - 1);
            const double db = static_cast<double>(kTubeGainLoDb)
                              + static_cast<double>(kTubeGainHiDb - kTubeGainLoDb) * a;
            const double g = std::pow(10.0, db / 20.0);
            double sum = 0.0;
            for (std::size_t k = 0; k < kTablePhasePoints; ++k) {
                const double phi = kTwoPiD * (static_cast<double>(k) + 0.5) / static_cast<double>(kTablePhasePoints);
                const double sn = std::sin(phi);
                const auto x = static_cast<float>(g * static_cast<double>(kP) * sn);
                sum += (static_cast<double>(Asymmetric::tube(x)) / static_cast<double>(kP)) * sn;
            }
            t[i] = static_cast<float>((2.0 / static_cast<double>(kTablePhasePoints)) * sum);
        }
        return t;
    }

    /// Bite drive g(A) = 10^{(kBiteDriveLoDb + kBiteDriveSpanDb·A)/20} (S5.4), in double.
    [[nodiscard]] static double biteGainD(double amount) noexcept {
        return std::pow(10.0, (static_cast<double>(kBiteDriveLoDb) + static_cast<double>(kBiteDriveSpanDb) * amount)
                                  / 20.0);
    }

    /// PREPARE-TIME ONLY: c_bite(A_i) = diodeH1Coefficient(g(A_i)) (S4.1, S5.4).
    [[nodiscard]] static AmountTable makeBiteTable() noexcept {
        AmountTable t{};
        for (std::size_t i = 0; i < kTablePoints; ++i) {
            const double a = static_cast<double>(i) / static_cast<double>(kTablePoints - 1);
            t[i] = static_cast<float>(diodeH1Coefficient(biteGainD(a)));
        }
        return t;
    }

    /// Fold drive theta(A) = kFoldThetaMin + (kFoldThetaMax - kFoldThetaMin)·A (S5.4, C-12).
    [[nodiscard]] static float foldTheta(float amount) noexcept {
        return kFoldThetaMin + (kFoldThetaMax - kFoldThetaMin) * amount;
    }

    /// PREPARE-TIME ONLY: c_fold(A_i) = the h1 coefficient of sin(θ·sin φ) / sin(min(θ, π/2)) over 256
    /// midpoint phase points, θ = θ(A_i) — i.e. [h1 of P·sin(θ·sin φ)/sin(min(θ, π/2))] / P (S5.4).
    [[nodiscard]] static AmountTable makeFoldTable() noexcept {
        constexpr double kHalfPiD = 1.5707963267948966192313216916398;
        AmountTable t{};
        for (std::size_t i = 0; i < kTablePoints; ++i) {
            const double a = static_cast<double>(i) / static_cast<double>(kTablePoints - 1);
            const double theta = static_cast<double>(kFoldThetaMin)
                                 + static_cast<double>(kFoldThetaMax - kFoldThetaMin) * a;
            double sum = 0.0;
            for (std::size_t k = 0; k < kTablePhasePoints; ++k) {
                const double phi = kTwoPiD * (static_cast<double>(k) + 0.5) / static_cast<double>(kTablePhasePoints);
                const double sn = std::sin(phi);
                sum += std::sin(theta * sn) * sn;
            }
            const double h1 = (2.0 / static_cast<double>(kTablePhasePoints)) * sum;
            t[i] = static_cast<float>(h1 / std::sin(std::min(theta, kHalfPiD)));
        }
        return t;
    }

    /// Fracture drive g(A) = kFractureDriveLo + (kFractureDriveHi - kFractureDriveLo)·A (S5.4, C-5).
    [[nodiscard]] static float fractureDrive(float amount) noexcept {
        return kFractureDriveLo + (kFractureDriveHi - kFractureDriveLo) * amount;
    }

    /// PREPARE-TIME ONLY: c_frac(A_i) = the h1 coefficient of (P/g)·tanh(g·sin φ) re P, i.e. of
    /// tanh(g·sin φ)/g, over 256 midpoint phase points, g = g(A_i) (S5.4).
    [[nodiscard]] static AmountTable makeFractureTable() noexcept {
        AmountTable t{};
        for (std::size_t i = 0; i < kTablePoints; ++i) {
            const double a = static_cast<double>(i) / static_cast<double>(kTablePoints - 1);
            const double g = static_cast<double>(kFractureDriveLo)
                             + static_cast<double>(kFractureDriveHi - kFractureDriveLo) * a;
            double sum = 0.0;
            for (std::size_t k = 0; k < kTablePhasePoints; ++k) {
                const double phi = kTwoPiD * (static_cast<double>(k) + 0.5) / static_cast<double>(kTablePhasePoints);
                const double sn = std::sin(phi);
                sum += (std::tanh(g * sn) / g) * sn;
            }
            t[i] = static_cast<float>((2.0 / static_cast<double>(kTablePhasePoints)) * sum);
        }
        return t;
    }

    /// C-24 rational tanh for Fracture's phasor functions (σ and the bias term, never the ADAA
    /// stage): Padé (7,6), the Lambert continued fraction truncated after 6 levels,
    ///   tanh(x) ≈ x·(135135 + 17325x² + 378x⁴ + x⁶) / (135135 + 62370x² + 3150x⁴ + 28x⁶),
    /// argument clamped to [-4, 4]. Max |error| vs std::tanh on a 10001-point grid over [-4, 4]:
    /// 1.50e-5 at |x| = 4 (T001 epoch 2 bench, float evaluation 1.499e-5; bar 1e-4).
    /// FastMath::fastTanh (Padé (5,4), saturating at 3.5) misses the 1e-4 bar near |x| = 3.5.
    [[nodiscard]] static float fractureTanh(float x) noexcept {
        const float xc = std::clamp(x, -4.0f, 4.0f);
        const float x2 = xc * xc;
        return xc * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)))
               / (135135.0f + x2 * (62370.0f + x2 * (3150.0f + 28.0f * x2)));
    }

    /// Integrated cubic B-spline step S(v) over v in [0, 4) (polyblep.h polynomial pieces, plan S5.4):
    /// 0 for v <= 0, 1 for v >= 4, C2-continuous in between.
    [[nodiscard]] static double bsplineStep(double v) noexcept {
        if (v <= 0.0)
            return 0.0;
        if (v < 1.0)
            return (v * v * v * v) / 24.0;
        if (v < 2.0) {
            const double w = 2.0 - v;
            return 0.5 - (3.0 * w * w * w * w - 8.0 * w * w * w + 16.0 * w) / 24.0;
        }
        if (v < 3.0) {
            const double w = v - 2.0;
            return 0.5 + (3.0 * w * w * w * w - 8.0 * w * w * w + 16.0 * w) / 24.0;
        }
        if (v < 4.0) {
            const double w = 4.0 - v;
            return 1.0 - (w * w * w * w) / 24.0;
        }
        return 1.0;
    }

    /// HarmonicFold::sanitizeControl's rule (private there): non-finite keeps `previous`, a
    /// denormal (or signed zero) bit pattern becomes +0, then clamp to [lo, hi].
    [[nodiscard]] static float sanitizeControl(float v, float previous, float lo, float hi) noexcept {
        if (!detail::isFinite(v))
            return previous;
        if ((std::bit_cast<std::uint32_t>(v) & 0x7F800000u) == 0u)
            v = 0.0f;
        return std::min(std::max(lo, v), hi);
    }

    // -------------------------------------------------------------------------
    // Branches (S5.2; one plain struct per algorithm, no union / variant)
    // -------------------------------------------------------------------------

    /// Tube (FR-021 composes TubeStage; C-8, C-10, S5.4):
    ///   pre (k = 1, ±6 dB, base rate) -> [F·fs] branch-local 2x up -> TubeStage::process(buf, 2)
    ///   -> 2x down -> [base rate] post (k = 2, ±6 dB). 8x total at F = 4.
    ///   Input gain kTubeGainLoDb + 18·A dB; output gain -20·log10 c_tube(A) clamped ±24 dB, so the
    ///   branch's h1 equals its input's on the calibration sine. Saturation pinned at 1 and bias 0
    ///   at prepare: the tube_stage.h:270 bypass threshold is never crossed (spec Edge Case).
    ///   lin = 1·u[n-2] (approximates the branch 2x's ≈ 1.9-sample group delay, C-15).
    struct TubeBranch {
        static constexpr int kAlignDelay = 2;  // l = u[n-2] (S5.4)
        KeytrackedTilt preTilt;
        KeytrackedTilt postTilt;
        PolyphaseHalfbandOversampler os;
        TubeStage stage;
        AmountTable cTube{};       // c_tube over Amount, filled in prepare only
        float u1 = 0.0f;           // u[n-1]
        float u2 = 0.0f;           // u[n-2]
        float lastAmount = -1.0f;  // configure() recomputes the gains only on a changed Amount
        bool fresh = true;         // the first configure after reset() snaps TubeStage's gain smoothers
        void prepare(double baseRate, double processRate) noexcept {
            preTilt.prepare(baseRate);
            preTilt.configure(kPreTiltPivot, kTubeBiteTiltDb);
            postTilt.prepare(baseRate);
            postTilt.configure(kPostTiltPivot, kTubeBiteTiltDb);
            os.setFactor(2);
            stage.prepare(2.0 * processRate, 2 * kMaxFactor);
            stage.setSaturationAmount(1.0f);
            stage.setBias(0.0f);
            cTube = makeTubeTable();
            lastAmount = -1.0f;
            fresh = true;
        }
        void reset() noexcept {
            preTilt.reset();
            postTilt.reset();
            os.reset();
            stage.reset();
            u1 = 0.0f;
            u2 = 0.0f;
            fresh = true;
        }
        void setFundamentalHz(float f0) noexcept {
            preTilt.setFundamentalHz(f0);
            postTilt.setFundamentalHz(f0);
        }
        void configure(float amount, float tone) noexcept {
            preTilt.setTone(tone);
            postTilt.setTone(tone);
            if (!fresh && std::bit_cast<std::uint32_t>(amount) == std::bit_cast<std::uint32_t>(lastAmount))
                return;
            lastAmount = amount;
            const float c = std::max(tableAt(cTube, amount), 1.0e-6f);
            stage.setInputGain(kTubeGainLoDb + (kTubeGainHiDb - kTubeGainLoDb) * amount);
            stage.setOutputGain(
                std::clamp(-20.0f * std::log10(c), -kTubeOutputGainLimitDb, kTubeOutputGainLimitDb));
            if (fresh) {  // just reset: start at the new gains, not a 5 ms glide from stale ones
                stage.reset();
                fresh = false;
            }
        }
        [[nodiscard]] float pre(float x) noexcept { return preTilt.process(x); }
        [[nodiscard]] BranchOut process(float u) noexcept {
            std::array<float, 2> buf{};
            os.upsample(u, buf.data());
            stage.process(buf.data(), buf.size());
            const float y = os.downsample(buf.data());
            const float l = u2;
            u2 = u1;
            u1 = u;
            return {.y = y, .lin = l};
        }
        [[nodiscard]] float post(float z) noexcept { return postTilt.process(z); }
        [[nodiscard]] float linearCoefficient() const noexcept { return 1.0f; }
        [[nodiscard]] float dryShare() const noexcept { return 0.0f; }
        [[nodiscard]] bool stateFinite() const noexcept {
            return os.stateFinite() && detail::isFinite(u1) && detail::isFinite(u2);
        }
    };

    /// Fold (FR-021 composes WavefolderProcessor; C-12, S5.4):
    ///   Serge model, symmetry 0, mix 1 at prepare; setFoldAmount(θ), θ = 0.1 + 2.4·A (θ_max 2.5 rad).
    ///   Per F-sample: buf = u/P -> process(buf, 1) -> ·P/sin(min(θ, π/2)) (peak-preserving makeup);
    ///   then post (k = 2, ±12 dB, base rate). lin = c_fold(A)·u (memoryless, no delay), c_fold from
    ///   the 65-point table filled in prepare.
    struct FoldBranch {
        static constexpr int kAlignDelay = 0;  // memoryless law (S5.4)
        KeytrackedTilt postTilt;
        WavefolderProcessor folder;
        AmountTable cFold{};       // c_fold over Amount, filled in prepare only
        float lastAmount = -1.0f;  // configure() recomputes only on a changed Amount
        float makeup = 1.0f;       // P / sin(min(θ, π/2))
        float linC = 1.0f;         // c_fold(A)
        bool fresh = true;         // the first configure after reset() snaps the folder's 5 ms smoothers
        void prepare(double baseRate, double processRate) noexcept {
            postTilt.prepare(baseRate);
            postTilt.configure(kPostTiltPivot, kPostTiltDb);
            folder.prepare(processRate, kMaxFactor);
            folder.setModel(WavefolderModel::Serge);
            folder.setSymmetry(0.0f);
            folder.setMix(1.0f);
            cFold = makeFoldTable();
            lastAmount = -1.0f;
            fresh = true;
        }
        void reset() noexcept {
            postTilt.reset();
            folder.reset();
            fresh = true;
        }
        void setFundamentalHz(float f0) noexcept { postTilt.setFundamentalHz(f0); }
        void configure(float amount, float tone) noexcept {
            postTilt.setTone(tone);
            if (!fresh && std::bit_cast<std::uint32_t>(amount) == std::bit_cast<std::uint32_t>(lastAmount))
                return;
            lastAmount = amount;
            const float theta = foldTheta(amount);
            folder.setFoldAmount(theta);
            makeup = kP / std::sin(std::min(theta, kHalfPi));
            linC = tableAt(cFold, amount);
            if (fresh) {  // just reset: start at θ(A), not a 5 ms glide from a stale θ
                folder.reset();
                fresh = false;
            }
        }
        [[nodiscard]] float pre(float x) const noexcept { return x; }
        [[nodiscard]] BranchOut process(float u) noexcept {
            float buf = u * kInvP;
            folder.process(&buf, 1);
            return {.y = buf * makeup, .lin = linC * u};
        }
        [[nodiscard]] float post(float z) noexcept { return postTilt.process(z); }
        [[nodiscard]] float linearCoefficient() const noexcept { return linC; }
        [[nodiscard]] float dryShare() const noexcept { return 0.0f; }
        [[nodiscard]] bool stateFinite() const noexcept { return detail::isFinite(makeup) && detail::isFinite(linC); }
    };

    /// Rectify (S5.4, C-19): composes Rectifier's laws (RampLaw, AbsLaw, sineMakeup,
    /// linearCoefficient) inside the stage's domain; never runs a Rectifier.
    ///   wet = min(1, A / 0.4); b = smoothstep((A - 0.4) / 0.6); mk = Rectifier::sineMakeup(b)
    ///   y   = (1 - wet)·u[n-1] + wet·mk·((1 - b)·ramp(u) + b·abs(u))   (ADAA2 in double, delay 1)
    ///   lin = ((1 - wet) + wet·mk·linearCoefficient(b))·u[n-1]
    struct RectifyBranch {
        static constexpr int kAlignDelay = 1;  // the ADAA2 one-sample delay
        ClosedFormAdaa2<Rectifier::RampLaw> ramp;
        ClosedFormAdaa2<Rectifier::AbsLaw> abs;
        KeytrackedTilt postTilt;
        float uPrev = 0.0f;
        float lastAmount = -1.0f;  // configure() recomputes only on a changed Amount
        float wet = 0.0f;
        float b = 0.0f;
        float mk = 1.0f;
        float linC = 1.0f;
        void prepare(double baseRate, [[maybe_unused]] double processRate) noexcept {
            postTilt.prepare(baseRate);
            postTilt.configure(kPostTiltPivot, kPostTiltDb);
            lastAmount = -1.0f;
        }
        void reset() noexcept {
            ramp.reset();
            abs.reset();
            postTilt.reset();
            uPrev = 0.0f;
        }
        void setFundamentalHz(float f0) noexcept { postTilt.setFundamentalHz(f0); }
        void configure(float amount, float tone) noexcept {
            postTilt.setTone(tone);
            if (std::bit_cast<std::uint32_t>(amount) == std::bit_cast<std::uint32_t>(lastAmount))
                return;
            lastAmount = amount;
            wet = std::min(1.0f, amount / kRectHalfEnd);
            const float s = std::clamp((amount - kRectHalfEnd) / (1.0f - kRectHalfEnd), 0.0f, 1.0f);
            b = s * s * (3.0f - 2.0f * s);
            mk = Rectifier::sineMakeup(b);
            linC = (1.0f - wet) + wet * mk * Rectifier::linearCoefficient(b);
        }
        [[nodiscard]] float pre(float x) const noexcept { return x; }
        [[nodiscard]] BranchOut process(float u) noexcept {
            const auto ud = static_cast<double>(u);
            const auto bd = static_cast<double>(b);
            // Exact by linearity of ADAA in the law: both laws always run (S4.3).
            const double law = (1.0 - bd) * ramp.process(ud) + bd * abs.process(ud);
            const double p = static_cast<double>(uPrev);
            const double y = (1.0 - static_cast<double>(wet)) * p
                             + static_cast<double>(wet) * static_cast<double>(mk) * law;
            uPrev = u;
            return {.y = static_cast<float>(y), .lin = static_cast<float>(static_cast<double>(linC) * p)};
        }
        [[nodiscard]] float post(float z) noexcept { return postTilt.process(z); }
        [[nodiscard]] float linearCoefficient() const noexcept { return linC; }
        [[nodiscard]] float dryShare() const noexcept { return 1.0f - wet; }
        [[nodiscard]] bool stateFinite() const noexcept {
            return ramp.stateFinite() && abs.stateFinite() && detail::isFinite(uPrev);
        }
    };

    /// Bite (C-8, C-11, S5.4; tasks.md deviation 9):
    ///   tp = pre (k = 1, ±9 dB per the 2026-10-16 ruling, base rate); v = g·tp/P, g = 10^{(-12 + 30A)/20};
    ///   y = P·ClosedFormAdaa2<DiodeAdaaLaw>(v) / c_bite(A) at F·fs, c_bite = diodeH1Coefficient(g)
    ///   from the 65-point table filled in prepare, so the branch's h1 equals the input's on the
    ///   calibration sine; then post (k = 2, ±6 dB, base rate). lin = 1·u[n-1] (the ADAA2 delay).
    struct BiteBranch {
        static constexpr int kAlignDelay = 1;  // l = u[n-1] (S5.4)
        KeytrackedTilt preTilt;
        KeytrackedTilt postTilt;
        ClosedFormAdaa2<DiodeAdaaLaw> adaa;
        AmountTable cBite{};       // c_bite over Amount, filled in prepare only
        float uPrev = 0.0f;
        float lastAmount = -1.0f;  // configure() recomputes only on a changed Amount
        float gainOverP = 0.0f;    // g(A) / P
        float outScale = 1.0f;     // P / c_bite(A)
        void prepare(double baseRate, [[maybe_unused]] double processRate) noexcept {
            preTilt.prepare(baseRate);
            preTilt.configure(kPreTiltPivot, kBitePreTiltDb);
            postTilt.prepare(baseRate);
            postTilt.configure(kPostTiltPivot, kTubeBiteTiltDb);
            cBite = makeBiteTable();
            lastAmount = -1.0f;
        }
        void reset() noexcept {
            preTilt.reset();
            postTilt.reset();
            adaa.reset();
            uPrev = 0.0f;
        }
        void setFundamentalHz(float f0) noexcept {
            preTilt.setFundamentalHz(f0);
            postTilt.setFundamentalHz(f0);
        }
        void configure(float amount, float tone) noexcept {
            preTilt.setTone(tone);
            postTilt.setTone(tone);
            if (std::bit_cast<std::uint32_t>(amount) == std::bit_cast<std::uint32_t>(lastAmount))
                return;
            lastAmount = amount;
            gainOverP = static_cast<float>(biteGainD(static_cast<double>(amount))) * kInvP;
            outScale = kP / std::max(tableAt(cBite, amount), 1.0e-6f);
        }
        [[nodiscard]] float pre(float x) noexcept { return preTilt.process(x); }
        [[nodiscard]] BranchOut process(float u) noexcept {
            const double v = static_cast<double>(gainOverP) * static_cast<double>(u);
            const double y = static_cast<double>(outScale) * adaa.process(v);
            const float l = uPrev;
            uPrev = u;
            return {.y = static_cast<float>(y), .lin = l};
        }
        /// seamNaiveBaseRate_ (SC-010 positive control): y = P·Asymmetric::diode(g·pre(x)/P)/c_bite
        /// at the base rate, no path, no ADAA (x arrives already pre-tilted from runSlot).
        [[nodiscard]] float naive(float x) const noexcept { return outScale * Asymmetric::diode(gainOverP * x); }
        [[nodiscard]] float post(float z) noexcept { return postTilt.process(z); }
        [[nodiscard]] float linearCoefficient() const noexcept { return 1.0f; }
        [[nodiscard]] float dryShare() const noexcept { return 0.0f; }
        [[nodiscard]] bool stateFinite() const noexcept { return adaa.stateFinite() && detail::isFinite(uPrev); }
    };

    /// One pitch-synchronous hold at m·f0 (Crush, S5.4): the held value plus a ring of band-limited
    /// step corrections. A step of height d scheduled at t_k contributes -d·(1 - S(n - t_k)) while
    /// its age n - t_k is in [0, 4); ring slots hold the active steps oldest first.
    struct HoldEngine {
        struct Step {
            double age;  // n - t_k at the current F-sample
            float d;     // step height q - held
        };
        std::array<Step, kCrushRingSlots> ring{};
        std::size_t count = 0;  // active steps in ring[0, count)
        float held = 0.0f;
        int m = 0;              // the integer multiple this engine runs (keyed by parity)
        /// Per-slot hysteresis memory (kCrushHysteresisSteps): for hold slot k (k = crossing index mod m) the
        /// level q_ref it last quantised to (slotLevel) and the held input v_ref it quantised (slotInput), valid
        /// where slotValid[k]; keyed by memM and memBits.
        std::array<float, kCrushMaxSlots> slotLevel{};
        std::array<float, kCrushMaxSlots> slotInput{};
        std::array<bool, kCrushMaxSlots> slotValid{};
        int memM = 0;          // m the slot memory was keyed with
        float memBits = 0.0f;  // bit depth the slot memory was keyed with
        void reset() noexcept {
            ring = {};
            count = 0;
            held = 0.0f;
            m = 0;
            forgetSlots(0, 0.0f);
        }
        /// Drops every slot level and keys the memory to (mKey, bits).
        void forgetSlots(int mKey, float bits) noexcept {
            slotLevel = {};
            slotInput = {};
            slotValid = {};
            memM = mKey;
            memBits = bits;
        }
        /// Re-keys the slot memory when this engine's m changed (an integer crossing of m_eff re-keys or
        /// retires it) or the bit depth moved by kCrushHysteresisRekeyBits or more since it was keyed.
        void keySlots(float bits) noexcept {
            if (m != memM || std::abs(bits - memBits) >= kCrushHysteresisRekeyBits)
                forgetSlots(m, bits);
        }
        /// The level slot k outputs for held value v whose plain quantisation is q (step: one quantiser step
        /// in the same units): re-quantises only when the slot is invalid or v moved from v_ref by more than
        /// kCrushHysteresisSteps·step; otherwise keeps q_ref.
        [[nodiscard]] float hysteresis(std::size_t k, float v, float q, float step) noexcept {
            if (!slotValid[k] || std::abs(v - slotInput[k]) > kCrushHysteresisSteps * step) {
                slotLevel[k] = q;
                slotInput[k] = v;
                slotValid[k] = true;
            }
            return slotLevel[k];
        }
        /// Advance every active step by one F-sample; retire those whose correction reached 0 (age >= 4).
        void advance() noexcept {
            std::size_t kept = 0;
            for (std::size_t k = 0; k < count; ++k) {
                Step s = ring[k];
                s.age += 1.0;
                if (s.age < 4.0)
                    ring[kept++] = s;
            }
            count = kept;
        }
        /// Schedule a step; ring-full policy (unreachable under the S5.4 spacing bound): retire the
        /// oldest, dropping its remaining correction, and count it.
        void push(float d, double age, std::size_t& retirements) noexcept {
            if (count == ring.size()) {
                for (std::size_t k = 1; k < count; ++k)
                    ring[k - 1] = ring[k];
                --count;
                ++retirements;
            }
            ring[count++] = Step{.age = age, .d = d};
        }
        [[nodiscard]] float output() const noexcept {
            double y = static_cast<double>(held);
            for (std::size_t k = 0; k < count; ++k)
                y -= static_cast<double>(ring[k].d) * (1.0 - bsplineStep(ring[k].age));
            return static_cast<float>(y);
        }
        [[nodiscard]] bool stateFinite() const noexcept {
            if (!detail::isFinite(held))
                return false;
            for (std::size_t k = 0; k < count; ++k) {
                if (!detail::isFinite(ring[k].d))
                    return false;
            }
            for (const float v : slotLevel) {
                if (!detail::isFinite(v))
                    return false;
            }
            for (const float v : slotInput) {
                if (!detail::isFinite(v))
                    return false;
            }
            return true;
        }
    };

    /// Crush (FR-051, D-6, S5.4): pitch-synchronous, band-limited, noiseless.
    ///   m(A) = 64·(64/3)^-A (3 at A = 1); m_eff = min(m(A), F·fs/(4·f0)); m_lo = ⌊m_eff⌋, m_hi = m_lo + 1,
    ///   frac = m_eff - m_lo. One phasor θ ∈ [0, 1) in cycles of f0 (θ += f0/(F·fs), 0 at reset)
    ///   drives two HoldEngines keyed by the parity of m (engine m mod 2 runs m), so an integer
    ///   crossing of m_eff keeps the outgoing m_hi engine's state as the new m_lo. Crossings are
    ///   ⌊θ·m + ½⌋ - ⌊θ_prev·m + ½⌋ (kCrushGridOffset: sample points at θ = (k + ½)/m, never on the
    ///   period start where the core's waveforms have their edge; T022) with the current m on both
    ///   sides (θ unwrapped), so a change of m alone never schedules a step. At a crossing
    ///   τ ∈ (0, 1]: value = u_prev + τ·(u - u_prev),
    ///   q = P/0.9·BitCrusher(0.9·value/P) at B(A) = 12 - 8·A bits (dither 0), through the per-slot
    ///   hysteresis (slot k = crossing index mod m; kCrushHysteresisSteps), step q - held at age 1 - τ. y = (1 - frac)·Hold(m_lo) + frac·Hold(m_hi); then post (k = 2, ±12 dB).
    ///   lin = 0: everything goes to the residual. The phasor follows f0 by rate, never by phase.
    struct CrushBranch {
        static constexpr int kAlignDelay = 1;
        KeytrackedTilt postTilt;
        std::array<BitCrusher, 2> crusher{};  // one per engine
        std::array<HoldEngine, 2> engine{};
        double processRate = 0.0;
        double phase = 0.0;        // θ ∈ [0, 1), cycles of f0
        double inc = 0.0;          // f0 / (F·fs)
        double mCap = 1.0;         // F·fs / (4·f0)
        double mAmount = 64.0;     // m(A)
        float bits = kCrushBitsLo;  // B(A)
        float qStep = 0.0f;         // one quantiser step at B(A), in the units of the held value
        float uPrev = 0.0f;
        float lastAmount = -1.0f;  // configure() recomputes only on a changed Amount
        std::size_t occupancyPeak = 0;  // largest active-step count of one engine since the stage read it
        std::size_t retirements = 0;    // ring-full retirements since the stage read them
        void prepare(double baseRate, double processRateIn, std::uint32_t channel) noexcept {
            postTilt.prepare(baseRate);
            postTilt.configure(kPostTiltPivot, kPostTiltDb);
            for (auto& bc : crusher) {
                bc.prepare(processRateIn);
                bc.setDither(0.0f);
                bc.setSeed(0x5F3759DFu ^ channel);  // plan S8: seeded although dither is 0
            }
            processRate = processRateIn;
            lastAmount = -1.0f;
            setFundamentalHz(kDefaultFundamentalHz);
        }
        void reset() noexcept {
            postTilt.reset();
            for (auto& bc : crusher)
                bc.reset();
            for (auto& e : engine)
                e.reset();
            phase = 0.0;
            uPrev = 0.0f;
            occupancyPeak = 0;
            retirements = 0;
        }
        void setFundamentalHz(float f0) noexcept {
            postTilt.setFundamentalHz(f0);
            if (processRate <= 0.0)
                return;
            const auto f = static_cast<double>(f0);
            inc = f / processRate;
            mCap = processRate / (4.0 * f);
        }
        void configure(float amount, float tone) noexcept {
            postTilt.setTone(tone);
            if (std::bit_cast<std::uint32_t>(amount) == std::bit_cast<std::uint32_t>(lastAmount))
                return;
            lastAmount = amount;
            const auto a = static_cast<double>(amount);
            mAmount = static_cast<double>(kCrushMultipleLo)
                      * std::pow(static_cast<double>(kCrushMultipleHi / kCrushMultipleLo), a);
            bits = kCrushBitsLo + (kCrushBitsHi - kCrushBitsLo) * amount;
            for (auto& bc : crusher)
                bc.setBitDepth(bits);
            // BitCrusher's step 2/levels, levels = 2^bits - 1 (bit_crusher.h), mapped to the held value's units.
            const float levels = std::pow(2.0f, crusher[0].getBitDepth()) - 1.0f;
            qStep = (kP / kCrushPreScale) * 2.0f / levels;
        }
        /// FR-072: θ = frac(cycles), cycles of f0 (the crossing count's convention, sample points at
        /// θ = (k + ½)/m).
        void setPhase(double cycles) noexcept { phase = cycles - std::floor(cycles); }
        [[nodiscard]] float pre(float x) const noexcept { return x; }
        [[nodiscard]] BranchOut process(float u) noexcept {
            const double mEff = std::max(1.0, std::min(mAmount, mCap));
            const auto mLo = static_cast<int>(std::floor(mEff));
            const int mHi = mLo + 1;
            const auto frac = static_cast<float>(mEff - static_cast<double>(mLo));
            const std::size_t eLo = static_cast<std::size_t>(mLo) % 2;  // mLo >= 1
            const std::size_t eHi = 1 - eLo;
            engine[eLo].m = mLo;
            engine[eHi].m = mHi;

            const double thPrev = phase;
            const double th = phase + inc;  // unwrapped across the period wrap
            const auto up = static_cast<double>(uPrev);
            const auto ud = static_cast<double>(u);
            std::array<float, 2> out{};
            for (std::size_t e = 0; e < engine.size(); ++e) {
                HoldEngine& h = engine[e];
                h.advance();
                h.keySlots(bits);
                const auto md = static_cast<double>(h.m);
                // Sample points at θ = (k + 1/2)/m: half a hold step off the period start, where the
                // core's waveforms put their edge (a point on a band-limited edge makes the held value
                // hypersensitive to sub-sample timing; T022 run log).
                const double kPrev = std::floor(thPrev * md + kCrushGridOffset);
                const double crossings = std::floor(th * md + kCrushGridOffset) - kPrev;
                const auto nCross =
                    static_cast<std::size_t>(std::clamp(crossings, 0.0, static_cast<double>(kCrushRingSlots)));
                for (std::size_t j = 1; j <= nCross; ++j) {
                    const double tc = (kPrev + static_cast<double>(j) - kCrushGridOffset) / md;
                    const double tau = std::clamp((tc - thPrev) / inc, 0.0, 1.0);
                    const double value = up + tau * (ud - up);
                    const float qPlain = (kP / kCrushPreScale)
                                         * crusher[e].process(static_cast<float>(
                                             static_cast<double>(kCrushPreScale) * value / static_cast<double>(kP)));
                    const auto slot = static_cast<std::size_t>(kPrev + static_cast<double>(j))
                                      % static_cast<std::size_t>(h.m);
                    const float q = h.hysteresis(slot, static_cast<float>(value), qPlain, qStep);
                    h.push(q - h.held, 1.0 - tau, retirements);
                    h.held = q;
                }
                occupancyPeak = std::max(occupancyPeak, h.count);
                out[e] = h.output();
            }
            phase = (th >= 1.0) ? th - 1.0 : th;
            uPrev = u;
            return {.y = (1.0f - frac) * out[eLo] + frac * out[eHi], .lin = 0.0f};
        }
        [[nodiscard]] float post(float z) noexcept { return postTilt.process(z); }
        [[nodiscard]] float linearCoefficient() const noexcept { return 0.0f; }
        [[nodiscard]] float dryShare() const noexcept { return 0.0f; }
        [[nodiscard]] bool stateFinite() const noexcept {
            return detail::isFinite(static_cast<float>(phase)) && detail::isFinite(uPrev) && engine[0].stateFinite()
                   && engine[1].stateFinite();
        }
    };

    /// Fracture (FR-050, C-5 combined law, C-24; S5.4):
    ///   φ ∈ [0, 2) is the ÷2 phasor in periods of f0 (φ = 0 at reset; each F-sample uses φ, then
    ///   φ += f0/(F·fs)); s = sin πφ; σ = tanh(4s)/tanh 4; a = A; g = 0.5 + 1.5A; β = 0.5A;
    ///   v = g·(u·((1 - a) + a·σ)/P + β·s);
    ///   y = (P/g)·(TanhADAA[drive 1](v) - tanh(g·β·s_mid)), s_mid = sin π(φ - Δφ/2) (ADAA1's
    ///   half-sample alignment); then post (k = 2, ±12 dB, base rate).
    ///   lin = (1 - a)·c_frac(A)·(u + u_prev)/2, c_frac from the 65-point table filled in prepare.
    ///   C-24: (cos, sin)(πφ) is a double-precision rotor advanced by (cos, sin)(πΔφ) and
    ///   re-normalised every F-sample; σ and the bias term use fractureTanh. std::sin / std::cos run
    ///   only in setFundamentalHz (the re-key), which re-seeds the rotor from φ: f0 moves the phasor's
    ///   rate, never its phase (spec Edge Cases).
    struct FractureBranch {
        static constexpr int kAlignDelay = 1;
        KeytrackedTilt postTilt;
        TanhADAA adaa;
        AmountTable cFrac{};       // c_frac over Amount, filled in prepare only
        double processRate = 0.0;  // F·fs
        double phase = 0.0;        // φ ∈ [0, 2), periods of f0
        double inc = 0.0;          // Δφ = f0 / (F·fs)
        double rotC = 1.0;         // cos πφ
        double rotS = 0.0;         // sin πφ
        double stepC = 1.0;        // cos πΔφ
        double stepS = 0.0;        // sin πΔφ
        double halfC = 1.0;        // cos (πΔφ/2)
        double halfS = 0.0;        // sin (πΔφ/2)
        float uPrev = 0.0f;
        float lastAmount = -1.0f;  // configure() recomputes only on a changed Amount
        float a = 0.0f;            // polarity-alternation depth a = A
        float g = kFractureDriveLo;
        float beta = 0.0f;
        float outScale = kP / kFractureDriveLo;  // P / g
        float biasGain = 0.0f;                    // g·β
        float linHalf = 0.0f;                     // (1 - a)·c_frac(A) / 2
        void prepare(double baseRate, double processRateIn) noexcept {
            postTilt.prepare(baseRate);
            postTilt.configure(kPostTiltPivot, kPostTiltDb);
            adaa.setDrive(1.0f);
            cFrac = makeFractureTable();
            processRate = processRateIn;
            lastAmount = -1.0f;
            setFundamentalHz(kDefaultFundamentalHz);
        }
        void reset() noexcept {
            postTilt.reset();
            adaa.reset();
            phase = 0.0;
            rotC = 1.0;
            rotS = 0.0;
            uPrev = 0.0f;
        }
        /// The re-key (S5.3 step 1): new increment, rotor re-seeded from φ (phase kept).
        void setFundamentalHz(float f0) noexcept {
            postTilt.setFundamentalHz(f0);
            if (processRate <= 0.0)
                return;
            inc = static_cast<double>(f0) / processRate;
            const double d = kPiD * inc;
            stepC = std::cos(d);
            stepS = std::sin(d);
            halfC = std::cos(0.5 * d);
            halfS = std::sin(0.5 * d);
            rotC = std::cos(kPiD * phase);
            rotS = std::sin(kPiD * phase);
        }
        /// FR-072: φ ∈ [0, 2) in periods of f0; the rotor is re-seeded from φ (std::cos / std::sin run
        /// only here and in the re-key).
        void setPhase(double phi) noexcept {
            phase = phi;
            rotC = std::cos(kPiD * phase);
            rotS = std::sin(kPiD * phase);
        }
        void configure(float amount, float tone) noexcept {
            postTilt.setTone(tone);
            if (std::bit_cast<std::uint32_t>(amount) == std::bit_cast<std::uint32_t>(lastAmount))
                return;
            lastAmount = amount;
            a = amount;
            g = fractureDrive(amount);
            beta = kFractureBias * amount;
            outScale = kP / g;
            biasGain = g * beta;
            linHalf = 0.5f * (1.0f - a) * tableAt(cFrac, amount);
        }
        [[nodiscard]] float pre(float x) const noexcept { return x; }
        [[nodiscard]] BranchOut process(float u) noexcept {
            const auto s = static_cast<float>(rotS);
            const auto sMid = static_cast<float>(rotS * halfC - rotC * halfS);  // sin π(φ - Δφ/2)
            const float sigma = fractureTanh(kFractureSquareSharpness * s) * kFractureInvTanhSharpness;
            const float v = g * (u * ((1.0f - a) + a * sigma) * kInvP + beta * s);
            const float y = outScale * (adaa.process(v) - fractureTanh(biasGain * sMid));
            const float l = linHalf * (u + uPrev);
            uPrev = u;
            // Advance φ and the rotor by Δφ; re-normalise the rotor (first-order, |r| -> 1).
            phase += inc;
            if (phase >= 2.0)
                phase -= 2.0;
            const double c = rotC * stepC - rotS * stepS;
            const double sn = rotS * stepC + rotC * stepS;
            const double k = 1.5 - 0.5 * (c * c + sn * sn);
            rotC = c * k;
            rotS = sn * k;
            return {.y = y, .lin = l};
        }
        [[nodiscard]] float post(float z) noexcept { return postTilt.process(z); }
        [[nodiscard]] float linearCoefficient() const noexcept { return (1.0f - a) * tableAt(cFrac, a); }
        [[nodiscard]] float dryShare() const noexcept { return 0.0f; }
        [[nodiscard]] bool stateFinite() const noexcept {
            return detail::isFinite(phase) && detail::isFinite(rotC) && detail::isFinite(rotS)
                   && detail::isFinite(uPrev);
        }
    };

    /// One running branch's round trip (C-23): its own path and the F-rate input history used by
    /// the identity / raw-dry seams (the plan's uDelay[3]).
    struct Slot {
        OversampledResidualPath path;
        std::array<float, 3> uHist{};  // u[n-1], u[n-2], u[n-3] at F·fs
        double prerollSum = 0.0;       // FR-026 / FR-054 pre-roll: sum of path.dcInput() over the period
    };

    struct Channel {
        std::array<Slot, 2> slot{};
        TubeBranch tube;
        FoldBranch fold;
        RectifyBranch rect;
        BiteBranch bite;
        CrushBranch crush;
        FractureBranch frac;
        OnePoleSmoother amountSm;  // effective (centre ± offset) Amount
        OnePoleSmoother toneSm;    // effective Tone
    };

    /// Calls fn(branch) for algorithm a (one predictable switch).
    template <class ChannelT, class Fn>
    static decltype(auto) visitBranch(ChannelT& ch, Algorithm a, Fn&& fn) noexcept {
        switch (a) {
            case Algorithm::Fold:
                return fn(ch.fold);
            case Algorithm::Rectify:
                return fn(ch.rect);
            case Algorithm::Bite:
                return fn(ch.bite);
            case Algorithm::Crush:
                return fn(ch.crush);
            case Algorithm::Fracture:
                return fn(ch.frac);
            case Algorithm::Tube:
                break;
        }
        return fn(ch.tube);
    }

    static void resetSlot(Slot& s) noexcept {
        s.path.reset();
        s.uHist = {};
        s.prerollSum = 0.0;
    }

    /// Reset a branch, then re-key it with the current f0 before its first sample (S5.3 step 1).
    /// FR-072: after a syncPhase() since reset(), Crush and Fracture start at the synced clock's phase
    /// instead of 0 (the re-key then seeds Fracture's rotor from it).
    void startBranch(Channel& ch, Algorithm a) noexcept {
        const float f0 = f0_;
        const bool synced = phaseSynced_;
        const double cycles = syncCycles_;
        visitBranch(ch, a, [f0, synced, cycles](auto& br) noexcept {
            using BranchT = std::remove_cvref_t<decltype(br)>;
            br.reset();
            if constexpr (std::is_same_v<BranchT, CrushBranch> || std::is_same_v<BranchT, FractureBranch>) {
                if (synced)
                    br.setPhase(cycles);
            }
            br.setFundamentalHz(f0);
        });
    }

    void keyBranch(Channel& ch, Algorithm a) noexcept {
        const float f0 = f0_;
        visitBranch(ch, a, [f0](auto& br) noexcept { br.setFundamentalHz(f0); });
    }

    /// Running = current (and target during a crossfade) while not bypassed (S5.3 step 1).
    void pushF0ToRunning() noexcept {
        if (!prepared_ || bypassed_)
            return;
        for (auto& ch : ch_) {
            keyBranch(ch, current_);
            if (inFlight())
                keyBranch(ch, target_);
        }
    }

    /// FR-026 / FR-054 pre-roll length: one period of the current f0 (round(fs / f0), at least 1).
    [[nodiscard]] std::size_t prerollSamples() const noexcept {
        const double p = std::round(sampleRate_ / static_cast<double>(f0_));
        return static_cast<std::size_t>(std::max(1.0, p));
    }

    /// A transition is in flight from the request to the crossfade end (pre-roll, then crossfade).
    [[nodiscard]] bool inFlight() const noexcept { return prerolling_ || xfading_; }

    /// Pre-roll end: prime the slot's residual DC blocker to the measured residual mean.
    static void primeSlot(Slot& s, std::size_t len) noexcept {
        s.path.primeDc(static_cast<float>(s.prerollSum / static_cast<double>(len)));
        s.prerollSum = 0.0;
    }

    /// S5.6 start(a).
    void start(Algorithm a) noexcept {
        if (seamHardSwitch_) {  // positive control: the law switches at once, path kept warm
            for (auto& ch : ch_)
                startBranch(ch, a);
            current_ = a;
            target_ = a;
            return;
        }
        target_ = a;
        const std::size_t incoming = 1 - curSlot_;
        for (auto& ch : ch_) {
            resetSlot(ch.slot[incoming]);
            startBranch(ch, a);
        }
        // FR-026 pre-roll (user ruling 2026-10-16, round 8): the incoming branch runs unmixed for one
        // period of f0, its residual DC blocker is primed to the measured mean, then the crossfade starts.
        prerolling_ = true;
        prerollCount_ = 0;
        prerollLen_ = prerollSamples();
        xfading_ = false;
        xfadeCount_ = 0;
    }

    void finishCrossfade() noexcept {
        current_ = target_;
        curSlot_ = 1 - curSlot_;
        xfading_ = false;
        xfadeCount_ = 0;
        if (hasPending_) {
            hasPending_ = false;
            if (pending_ != current_)
                start(pending_);
        }
    }

    /// S5.3 step 3: leaving bypass resets the running branches and paths and re-keys them.
    void leaveBypass() noexcept {
        bypassed_ = false;
        for (auto& ch : ch_) {
            for (auto& s : ch.slot)
                resetSlot(s);
            startBranch(ch, current_);
            if (inFlight())
                startBranch(ch, target_);
        }
        // FR-054 pre-roll (user ruling 2026-10-16, round 8): the output stays the copy (e = 0) for one
        // period of f0 while the running branch warms up; its residual DC blocker is then primed and e ramps.
        // The hard-bypass seam (SC-021 positive control) switches the whole law off: no pre-roll, no ramp.
        if (seamHardBypass_)
            return;
        engagePreroll_ = true;
        engageCount_ = 0;
        engageLen_ = prerollSamples();
    }

    /// One slot's base sample: pre tilt, upsample, branch at F·fs (lin / res split), downsample,
    /// post tilt (C-23). The seams are probe-only; each is one predictable branch.
    [[nodiscard]] float runSlot(Channel& ch, std::size_t slotIndex, Algorithm a, float x, float amount,
                                float tone) noexcept {
        Slot& s = ch.slot[slotIndex];
        return visitBranch(ch, a, [&](auto& br) noexcept -> float {
            using BranchT = std::remove_cvref_t<decltype(br)>;
            br.configure(amount, tone);
            const float xin = seamPreToneNeutral_ ? x : br.pre(x);
            if constexpr (std::is_same_v<BranchT, BiteBranch>) {
                if (seamNaiveBaseRate_) {
                    const float yn = br.naive(xin);
                    return seamPostToneNeutral_ ? yn : br.post(yn);
                }
            }
            std::array<float, kMaxFactor> u{};
            std::array<float, kMaxFactor> lin{};
            std::array<float, kMaxFactor> res{};
            s.path.upsample(xin, u.data());
            const std::size_t factor = s.path.factor();
            const float dry = seamRawDry_ ? br.dryShare() : 0.0f;
            for (std::size_t i = 0; i < factor; ++i) {
                BranchOut o{};
                if (seamIdentityLaw_) {
                    const float al = aligned(s, u[i], BranchT::kAlignDelay);
                    o = BranchOut{.y = al, .lin = al};
                } else {
                    o = br.process(u[i]);
                }
                if (seamRawDry_) {  // remove the in-path dry share; it is re-added at the base rate
                    const float d = dry * aligned(s, u[i], BranchT::kAlignDelay);
                    o.y -= d;
                    o.lin -= d;
                }
                s.uHist[2] = s.uHist[1];
                s.uHist[1] = s.uHist[0];
                s.uHist[0] = u[i];
                lin[i] = o.lin;
                res[i] = o.y - o.lin;
            }
            if constexpr (std::is_same_v<BranchT, CrushBranch>) {  // probe counters (S5.4 unit case)
                crushMaxOccupancy_ = std::max(crushMaxOccupancy_, br.occupancyPeak);
                crushRetirements_ += br.retirements;
                br.occupancyPeak = 0;
                br.retirements = 0;
            }
            float zr = s.path.downsample(lin.data(), res.data());
            if (seamRawDry_)
                zr += dry * x;
            return seamPostToneNeutral_ ? zr : br.post(zr);
        });
    }

    [[nodiscard]] static float aligned(const Slot& s, float u, int delay) noexcept {
        if (delay <= 0)
            return u;
        return s.uHist[static_cast<std::size_t>(std::min(delay, 3) - 1)];
    }

    [[nodiscard]] bool offsetsZero() const noexcept { return amountOffset_ == 0.0f && toneOffset_ == 0.0f; }

    [[nodiscard]] bool anyAmountTargetNonZero() const noexcept {
        return ch_[0].amountSm.getTarget() != 0.0f || ch_[1].amountSm.getTarget() != 0.0f;
    }

    /// Effective per-channel targets (FR-027, C-17): L = c + o, R = c - o, clamped to [0, 1].
    void retarget() noexcept {
        const std::array<float, 2> sign{1.0f, -1.0f};
        for (std::size_t c = 0; c < ch_.size(); ++c) {
            auto& ch = ch_[c];
            ch.amountSm.setTarget(std::clamp(amount_ + sign[c] * amountOffset_, 0.0f, 1.0f));
            ch.toneSm.setTarget(std::clamp(tone_ + sign[c] * toneOffset_, 0.0f, 1.0f));
            if (seamUnsmoothed_) {
                ch.amountSm.snapToTarget();
                ch.toneSm.snapToTarget();
            }
        }
    }

    /// Linear interpolation of a hysteresis table in Tone.
    [[nodiscard]] static FlipEdges edgesAt(const std::array<FlipEdges, 5>& table, float tone) noexcept {
        const float pos = std::clamp(tone, 0.0f, 1.0f) * 4.0f;
        const std::size_t i = std::min(static_cast<std::size_t>(pos), std::size_t{3});
        const float f = pos - static_cast<float>(i);
        return {.lo = table[i].lo + f * (table[i + 1].lo - table[i].lo),
                .hi = table[i].hi + f * (table[i + 1].hi - table[i].hi)};
    }

    /// S5.7 update rule on the centre targets (C-18); both flips are tracked on every change.
    void updateObserver() noexcept {
        const FlipEdges r = edgesAt(kRectifyFlipEdges, tone_);
        if (!flippedRect_ && amount_ >= r.hi)
            flippedRect_ = true;
        else if (flippedRect_ && amount_ <= r.lo)
            flippedRect_ = false;
        const FlipEdges f = edgesAt(kFractureFlipEdges, tone_);
        if (!flippedFrac_ && amount_ >= f.hi)
            flippedFrac_ = true;
        else if (flippedFrac_ && amount_ <= f.lo)
            flippedFrac_ = false;
    }

    // -------------------------------------------------------------------------
    // State
    // -------------------------------------------------------------------------
    std::array<Channel, 2> ch_{};
    LinearRamp engage_;  // e: Amount-0 bypass crossfade (FR-054)
    double sampleRate_ = 0.0;

    // Switch machine (S5.6).
    Algorithm current_ = Algorithm::Tube;
    Algorithm target_ = Algorithm::Tube;
    Algorithm pending_ = Algorithm::Tube;
    bool hasPending_ = false;
    bool xfading_ = false;
    std::size_t curSlot_ = 0;
    std::size_t xfadeCount_ = 0;
    std::size_t xfadeLen_ = 1;
    float xfadeInc_ = 1.0f;
    bool prerolling_ = false;        // FR-026: incoming branch warming up, outgoing alone heard
    std::size_t prerollCount_ = 0;
    std::size_t prerollLen_ = 1;
    bool engagePreroll_ = false;     // FR-054: running branch warming up while the copy is heard
    std::size_t engageCount_ = 0;
    std::size_t engageLen_ = 1;

    // Controls (centre targets, sanitised).
    float amount_ = 0.0f;
    float tone_ = 0.5f;
    float amountOffset_ = 0.0f;
    float toneOffset_ = 0.0f;
    float f0_ = kDefaultFundamentalHz;
    bool phaseSynced_ = false;  // FR-072: syncPhase() supplied since reset()
    double syncCycles_ = 0.0;   // FR-072: the synced core phase in cycles, mod 2, at the next sample

    // r observer (S5.7).
    bool halfWaveSymmetric_ = false;
    bool flippedRect_ = false;
    bool flippedFrac_ = false;

    float peak_ = 0.0f;
    bool linked_ = true;
    bool bypassed_ = true;
    bool prepared_ = false;

    // Seams (probe-only, default false; tasks.md T009).
    bool seamIdentityLaw_ = false;      // SC-027: y = lin = the branch's aligned input
    bool seamRawDry_ = false;           // SC-027 control: dry share mixed from the base-rate input
    bool seamNaiveBaseRate_ = false;    // SC-010 control: Bite at base rate, no path, no ADAA
    bool seamHardSwitch_ = false;       // switch without crossfade
    bool seamUnsmoothed_ = false;       // smoothers snap on every setter
    bool seamHardBypass_ = false;       // bypass edge without the 5 ms ramp
    bool seamPreToneNeutral_ = false;   // SC-028(d)
    bool seamPostToneNeutral_ = false;  // SC-028(d)

    // Counters (probe-only).
    std::size_t maxRunningBranches_ = 0;
    std::size_t crushMaxOccupancy_ = 0;  // largest active-step count of one Crush hold engine (S5.4 bound: 2)
    std::size_t crushRetirements_ = 0;   // Crush ring-full retirements (S5.4: must stay 0)
};

}  // namespace Krate::DSP
