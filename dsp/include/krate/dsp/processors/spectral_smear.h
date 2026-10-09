// ==============================================================================
// Layer 2: DSP Processor - Spectral Smear
// ==============================================================================
// Stereo STFT "fog" stage for Vorago: a per-bin leaky integrator over magnitude
// (the smear) plus per-bin phase decorrelation (the decoherence), wrapped in a
// COLA-exact Hann / 75 % overlap analysis-synthesis round trip.
//
// Features:
// - Per-bin magnitude memory with a log-frequency time-constant law, so lows
//   smear longer than highs, and a tilt that pivots that law (FR-020 - FR-034)
// - Per-bin phase decorrelation with a per-channel deterministic RNG stream and
//   an incoherence make-up gain (FR-040 - FR-046)
// - Exact identity at smearAmount == 0 and decoherence == 0 (FR-021, FR-041)
// - Fixed, partition-independent latency of exactly fftSize samples (FR-014)
// - Opt-in: PrepareConfig::enabled defaults to FALSE and a disabled instance is
//   a bit-identical bypass owning no heap (FR-019)
// - Real-time safe: prepare() is the only allocator; processBlock is noexcept,
//   lock-free, allocation-free and accepts ANY block size (FR-003, FR-012)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept process, allocation in prepare())
// - Principle III: Modern C++ (C++20, RAII, move-only)
// - Principle IX: Layer 2 (depends on Layer 0-1 only)
// - Principle X: DSP Constraints (COLA windows, 75 % overlap, synthesis window)
// - Principle XII: Test-First Development
//
// NAMING HAZARD, RECORDED ONCE. `kMinFFTSize` / `kMaxFFTSize` (upper-case FFT,
// values 256 / 8192) are NAMESPACE-SCOPE constants in Krate::DSP
// (primitives/fft.h:44, :47). This component's own bounds are spelled
// `kMinFftSize` / `kMaxFftSize` (lower-case "ft"), are CLASS statics, and never
// shadow the namespace ones. Inside a member function an unqualified
// `kMaxFFTSize` silently resolves to the namespace constant 8192, so this
// header must never write that spelling except inside the static_assert that
// deliberately compares the two.
//
// Reference: specs/vorago-phase4-spectral-smear/spec.md
//            specs/vorago-phase4-spectral-smear/plan.md (S1-S12)
// ==============================================================================

#pragma once

// Layer 0: Core
#include <krate/dsp/core/audio_constants.h>   // kMaxAudioFreqHz
#include <krate/dsp/core/db_utils.h>          // detail::isFinite / isNaN / isInf
#include <krate/dsp/core/interpolation.h>     // Interpolation::cubicHermiteInterpolate
#include <krate/dsp/core/math_constants.h>    // kPi
#include <krate/dsp/core/random.h>            // Xorshift32, deriveStreamSeed
#include <krate/dsp/core/spectral_simd.h>     // batchLog10 / batchPow10 (Highway SIMD)
#include <krate/dsp/core/window_functions.h>  // WindowType

// Layer 1: Primitives
#include <krate/dsp/primitives/smoother.h>         // OnePoleSmoother
#include <krate/dsp/primitives/spectral_buffer.h>  // SpectralBuffer
#include <krate/dsp/primitives/stft.h>             // STFT, OverlapAdd (pulls fft.h)

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Krate::DSP {

// =============================================================================
// SpectralSmear
// =============================================================================

/// @brief Stereo spectral fog: per-bin magnitude smearing plus phase decoherence.
///
/// Move-only (it owns STFT / OverlapAdd / SpectralBuffer sub-objects, none of
/// which is copyable). prepare() is the only allocator; every other entry point
/// is real-time safe.
class SpectralSmear {
public:
    // -------------------------------------------------------------------------
    // Constants (public: the criteria in
    // dsp/tests/unit/processors/spectral_smear*_test.cpp assert against them)
    // -------------------------------------------------------------------------

    // --- Geometry (FR-010, FR-011) ---
    static constexpr std::size_t kMinFftSize     = 512;   // NOT 256: FR-011 / D-3
    static constexpr std::size_t kMaxFftSize     = 4096;
    static constexpr std::size_t kDefaultFftSize = 2048;
    static constexpr std::size_t kOverlapFactor  = 4;     // hop = fftSize / 4 (75 %)
    static_assert(kMinFftSize >= kMinFFTSize && kMaxFftSize <= kMaxFFTSize,
                  "geometry must sit inside the FFT primitive's own bounds (fft.h:44,:47)");
    static_assert(kMinFftSize % kOverlapFactor == 0, "hop must divide the smallest geometry");

    // --- Render chunking (FR-012) ---
    static constexpr std::size_t kProcessChunkSamples = 64;  // atmosphere_engine.h:271

    // --- Magnitude integrator (FR-020..FR-024) ---
    static constexpr float kMaxPole       = 0.99999f;  // defensive backstop, FR-023
    static constexpr float kDenormalFloor = 1.0e-20f;  // FR-024, ordered compare

    // --- Time-constant law (FR-030..FR-032) ---
    static constexpr float kTauAnchorLowHz    = 20.0f;
    static constexpr float kTauAnchorHighHz   = kMaxAudioFreqHz;  // audio_constants.h:25
    static constexpr float kTiltPivotHz       = 1000.0f;
    static constexpr float kTiltExponentRange = 0.75f;
    static constexpr float kMinSmearSeconds   = 0.02f;
    static constexpr float kMaxSmearSeconds   = 10.0f;
    static constexpr float kDefaultSmearTimeLow  = 3.0f;
    static constexpr float kDefaultSmearTimeHigh = 0.25f;

    // --- Control surface (FR-035, FR-050) ---
    static constexpr float kControlSmoothMs    = 50.0f;  // atmosphere_engine.h:282
    static constexpr float kDefaultSmearAmount = 0.0f;
    static constexpr float kDefaultDecoherence = 0.0f;
    static constexpr float kDefaultSmearTilt   = 0.0f;

    // --- Safety (FR-061) ---
    static constexpr float kOutputClamp = 4.0f;  // noise_organism.h:180

    // --- RNG salts (FR-044). APPEND ONLY: renumbering rewrites every render. ---
    static constexpr std::size_t kSaltDecohereL = 0x1000;
    static constexpr std::size_t kSaltDecohereR = 0x2000;
    static_assert(kSaltDecohereL != kSaltDecohereR,
                  "the two channels must not share a stream");
    static constexpr std::uint32_t kDefaultSeed = 0x5EED0004u;

    // --- Coherence make-up (FR-042). PUBLIC because SC-006 must divide it out. ---
    static constexpr std::size_t kCoherenceKnotCount = 5;
    static constexpr float kCoherenceMakeup[kCoherenceKnotCount] =
        {1.0000f, 1.0799f, 1.3435f, 1.7746f, 1.9996f};  // aether_reverb.h:2775; SC-006 re-measures

    // -------------------------------------------------------------------------
    // Prepare-time configuration
    // -------------------------------------------------------------------------

    /// Callers MUST use designated initialisers -
    /// `PrepareConfig{.fftSize = 1024, .enabled = true}` - so no narrowing
    /// conversion can hide in a positional brace init (Clang errors on narrowing
    /// where MSVC does not).
    struct PrepareConfig {
        /// Clamped to [kMinFftSize, kMaxFftSize], THEN bit_floor'd (FR-011).
        std::size_t fftSize = kDefaultFftSize;

        /// DEFAULTS TO false, DELIBERATELY DIVERGING FROM
        /// AtmosphereEngine::PrepareConfig::blurEnabled_ (= true,
        /// atmosphere_engine.h:371). That stage lives inside a voice layer the
        /// owner already chose; this one sits on the GLOBAL bus, where a
        /// Phase-10 owner who omits the field would otherwise silently buy
        /// 42.7 ms of latency and ~0.2 % of a core for a stage doing nothing
        /// (FR-019, D-11, Clarifications Q6). Fog is opt-in.
        bool enabled = false;
    };

    // -------------------------------------------------------------------------
    // Construction (move-only)
    // -------------------------------------------------------------------------

    SpectralSmear() noexcept = default;
    ~SpectralSmear() noexcept = default;
    SpectralSmear(const SpectralSmear&) = delete;
    SpectralSmear& operator=(const SpectralSmear&) = delete;
    SpectralSmear(SpectralSmear&&) noexcept = default;
    SpectralSmear& operator=(SpectralSmear&&) noexcept = default;

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    /// @brief Allocate and configure. THE ONLY ALLOCATING ENTRY POINT (FR-002).
    /// @param sampleRate Render rate in Hz. Non-finite or <= 0 leaves the
    ///        instance UNPREPARED with every geometry read at zero.
    /// @param config Geometry and the opt-in enable flag.
    /// @note NOT real-time safe. May be called again at a different rate or
    ///       size; every table, capacity, cursor and the warm-up counter is
    ///       re-derived (SC-010).
    void prepare(double sampleRate, const PrepareConfig& config) noexcept {
        // --- Step 1: rate guard. This ordered list, and nothing else. --------
        // The previously-allocated vectors are DELIBERATELY RETAINED: prepare()
        // may be re-entered from a thread the owner also renders on, and a
        // shrink is a deallocation. So `getAllocatedBytes() == 0` here means
        // "this instance will not render", NOT "this instance holds no heap"
        // (plan S2 step 1, S9).
        if (!detail::isFinite(sampleRate) || sampleRate <= 0.0) {
            sampleRate_       = 0.0;
            fftSize_          = 0;
            hopSize_          = 0;
            numBins_          = 0;
            frameRate_        = 0.0f;
            warmupRemaining_  = 0;
            allocatedBytes_   = 0;
            clampEngagements_ = 0;
            poisonEngagements_ = 0;
            prepared_         = false;
            enabled_          = false;
            return;
        }

        // --- Step 2: geometry. Clamp THEN bit_floor, in that order. ----------
        // Written whether or not `enabled`: FR-018 / Clarifications Q6 require a
        // disabled instance to answer geometry queries identically to an
        // enabled twin, so Phase 10 can lay out its bus before enabling.
        fftSize_ = std::bit_floor(std::clamp(config.fftSize, kMinFftSize, kMaxFftSize));
        hopSize_ = fftSize_ / kOverlapFactor;
        numBins_ = fftSize_ / 2 + 1;
        assert(hopSize_ * kOverlapFactor == fftSize_);
        sampleRate_ = sampleRate;
        frameRate_  = static_cast<float>(sampleRate_) / static_cast<float>(hopSize_);

        // --- Step 3: the disabled path allocates NOTHING. --------------------
        enabled_ = config.enabled;
        if (!enabled_) {
            prepared_          = true;
            allocatedBytes_    = 0;
            warmupRemaining_   = 0;
            clampEngagements_  = 0;
            poisonEngagements_ = 0;
            // Housekeeping only: it keeps getCurrentValue() from reporting a
            // stale value if this instance is later re-prepared ENABLED. It is
            // NOT the mechanism behind the disabled applied reads - those are
            // produced by the getters themselves (plan S9), because no frame
            // ever calls process() here, so a setter issued AFTER prepare()
            // would otherwise never reach current_ and SC-002 (e) would fail.
            smearSm_.snapTo(smearTarget_);
            decohereSm_.snapTo(decohereTarget_);
            tiltSm_.snapTo(tiltTarget_);
            return;
        }

        // --- Step 4: sub-objects. --------------------------------------------
        // applySynthesisWindow = true is MANDATORY, not preferred (stft.h:224-227):
        // it is one decision with hopSize_ == fftSize_/4 above, and the assert in
        // step 2 is the tripwire against an edit that changes one and not the other.
        for (std::size_t ch = 0; ch < 2; ++ch) {
            stft_[ch].prepare(fftSize_, hopSize_, WindowType::Hann);
            ola_[ch].prepare(fftSize_, hopSize_, WindowType::Hann, kDefaultKaiserBeta,
                             /*applySynthesisWindow=*/true);
            spectrum_[ch].prepare(fftSize_);
        }

        // --- Step 5: pole tables, eagerly. -----------------------------------
        // This is the allocating path anyway, and a prepared instance must never
        // render off a zero-filled table.
        poleNeg_.assign(numBins_, 0.0f);
        poleZero_.assign(numBins_, 0.0f);
        polePos_.assign(numBins_, 0.0f);
        poleScratch_.assign(numBins_, 0.0f);
        rebuildPoleTables();
        tiltScratchDirty_ = true;
        lastResolvedTilt_ = 0.0f;
        poleTablesDirty_  = false;

        // --- Step 6: magnitude memories (FR-022). ----------------------------
        for (std::size_t ch = 0; ch < 2; ++ch) {
            magState_[ch].assign(numBins_, 0.0f);
            primeNext_[ch] = true;
        }

        // --- Step 7: the output FIFO (FR-016). -------------------------------
        fifoCapacity_ = std::bit_ceil(fftSize_ + kProcessChunkSamples + hopSize_);
        fifoMask_     = fifoCapacity_ - 1;
        for (std::size_t ch = 0; ch < 2; ++ch) {
            fifo_[ch].assign(fifoCapacity_, 0.0f);
            hopScratch_[ch].assign(hopSize_, 0.0f);
        }
        fifoRead_ = fifoWrite_ = fifoCount_ = 0;

        // --- Step 8: the warm-up counter (FR-014). ---------------------------
        // THIS, not an emptiness test, is what makes the reported latency
        // exactly fftSize for every partition (plan S11 carries the proof).
        warmupRemaining_ = fftSize_;

        // --- Step 9: smoothers, on the FRAME clock. --------------------------
        // spectral_gate.h:184-187 precedent: the smoother is advanced once per
        // frame by process(), so it is configured at sampleRate/hopSize.
        smearSm_.configure(kControlSmoothMs, frameRate_);
        decohereSm_.configure(kControlSmoothMs, frameRate_);
        tiltSm_.configure(kControlSmoothMs, frameRate_);
        smearSm_.snapTo(smearTarget_);
        decohereSm_.snapTo(decohereTarget_);
        tiltSm_.snapTo(tiltTarget_);

        // --- Step 10: the make-up ramp origin (FR-046). ----------------------
        prevMakeup_ = coherenceMakeup(decohereTarget_);

        // --- Step 11: streams, LAST, so nothing written earlier discards them.
        setSeed(seed_);

        // --- Step 12: counters and the self-reported footprint (FR-017). -----
        clampEngagements_  = 0;
        poisonEngagements_ = 0;
        allocatedBytes_ = (4u * numBins_        // poleNeg_, poleZero_, polePos_, poleScratch_
                           + 2u * numBins_      // magState_[0..1]
                           + 2u * fifoCapacity_ // fifo_[0..1]
                           + 2u * hopSize_)     // hopScratch_[0..1]
                          * sizeof(float);
        prepared_ = true;
    }

    /// @brief Clear audio state. Control values, tables and the seed survive.
    /// @note Real-time safe. Output returns to exactly fftSize samples of
    ///       silence rather than resuming mid-tail - a discontinuity BY DESIGN
    ///       (the owner resets at note-off / transport boundaries, FR-004).
    void reset() noexcept {
        clampEngagements_  = 0;  // FR-054: zeroed by prepare() AND reset()
        poisonEngagements_ = 0;
        if (!prepared_ || !enabled_) return;

        for (std::size_t ch = 0; ch < 2; ++ch) {
            stft_[ch].reset();
            ola_[ch].reset();
            spectrum_[ch].reset();
            std::fill(magState_[ch].begin(), magState_[ch].end(), 0.0f);
            primeNext_[ch] = true;  // FR-022 re-arm
            std::fill(fifo_[ch].begin(), fifo_[ch].end(), 0.0f);
            std::fill(hopScratch_[ch].begin(), hopScratch_[ch].end(), 0.0f);
        }
        fifoRead_ = fifoWrite_ = fifoCount_ = 0;
        warmupRemaining_ = fftSize_;
        prevMakeup_ = coherenceMakeup(decohereSm_.getCurrentValue());
        setSeed(seed_);  // FR-044 re-derive
        // NOT touched: smearSm_/decohereSm_/tiltSm_ (values AND targets),
        //              tauLow_/tauHigh_, the three pole tables, seed_.
    }

    /// @brief Re-derive both decoherence streams from a base seed (FR-005, FR-044).
    /// @note Real-time safe. Does NOT touch audio state.
    void setSeed(std::uint32_t seed) noexcept {
        seed_ = seed;
        // deriveStreamSeed guarantees a non-zero result (random.h:112), which is
        // load-bearing: Xorshift32::seed() silently substitutes its own default
        // for 0 (random.h:73-74), so two salts hashing to 0 would collapse the
        // channels onto one stream. setSeed(0) is therefore live on both.
        rng_[0].seed(deriveStreamSeed(seed_, kSaltDecohereL));
        rng_[1].seed(deriveStreamSeed(seed_, kSaltDecohereR));
    }

    // -------------------------------------------------------------------------
    // Render
    // -------------------------------------------------------------------------

    /// @brief Render one stereo block in place. Accepts ANY block size (FR-012).
    /// @note Real-time safe: no allocation, no lock, no exception, no I/O.
    void processBlock(float* left, float* right, std::size_t numSamples) noexcept {
        // FR-003 entry guards. The buffers are left UNTOUCHED - a disabled or
        // unprepared instance is a true bypass, bit-identical in and out
        // (FR-019, SC-002 (d)), not a zero-parameter round trip.
        if (!prepared_ || !enabled_ || left == nullptr || right == nullptr || numSamples == 0) {
            return;
        }

        // THE DEFERRED REBUILD (plan S4.3). setSmearTimeLow/High only raise the
        // flag; the ~14 300-transcendental rebuild is paid here, at most ONCE
        // per block however many times those setters fired, and inside the path
        // SC-013 (d) measures. Allocation-free: three already-sized tables are
        // overwritten, never resized.
        if (poleTablesDirty_) {
            rebuildPoleTables();
        }

        // CHUNKING (FR-012). This bounds STFT::samplesAvailable_ to
        // fftSize + kProcessChunkSamples at all times, which is what makes the
        // UNGUARDED pushSamples ring (primitives/stft.h:104-124, capacity
        // fftSize * 8 at :78) structurally safe rather than merely large
        // enough. The component therefore accepts ANY numSamples - there is no
        // maxBlockSamples field and no caller-side contract.
        std::size_t pos = 0;
        while (pos < numSamples) {
            const std::size_t chunk = std::min(kProcessChunkSamples, numSamples - pos);
            pumpChunk(left + pos, right + pos, chunk);
            pos += chunk;
        }
    }

    // -------------------------------------------------------------------------
    // Control (FR-050). Non-finite input SUBSTITUTES the default (FR-009).
    // -------------------------------------------------------------------------

    void setSmearAmount(float amount) noexcept {
        smearTarget_ = std::clamp(sanitise(amount, kDefaultSmearAmount), 0.0f, 1.0f);
        smearSm_.setTarget(smearTarget_);
    }

    void setDecoherence(float amount) noexcept {
        decohereTarget_ = std::clamp(sanitise(amount, kDefaultDecoherence), 0.0f, 1.0f);
        decohereSm_.setTarget(decohereTarget_);
    }

    void setSmearTilt(float tilt) noexcept {
        tiltTarget_ = std::clamp(sanitise(tilt, kDefaultSmearTilt), -1.0f, 1.0f);
        tiltSm_.setTarget(tiltTarget_);
    }

    /// @brief Time constant at the 20 Hz anchor, in seconds. Range [0.02, 10].
    /// @note ALLOCATION-FREE BUT NOT FREE: this marks the pole tables dirty, and
    ///       the deferred rebuild costs `numBins` `log` + `3*numBins` `pow` +
    ///       `3*numBins` `exp` (plan S4.3) - about 14 300 transcendentals at
    ///       fftSize = 4096. The rebuild is DEFERRED to the top of the next
    ///       processBlock, so that cost lands inside the path SC-013 measures
    ///       and is paid at most once per block however many times these two
    ///       setters are called. FR-006 puts setters on the SAME thread as
    ///       processBlock, so there is no control thread to hide it on. Prefer
    ///       patch-load cadence; per-block calls are legal and SC-013 (d) is the
    ///       configuration that prices them.
    void setSmearTimeLow(float seconds) noexcept {
        tauLow_ = std::clamp(sanitise(seconds, kDefaultSmearTimeLow),
                             kMinSmearSeconds, kMaxSmearSeconds);
        poleTablesDirty_ = true;
    }

    /// @brief Time constant at the 20 kHz anchor, in seconds. Range [0.02, 10].
    /// @note See setSmearTimeLow: same deferred-rebuild cost and cadence.
    ///       The endpoints are NEVER silently swapped - tauLow < tauHigh simply
    ///       inverts the law (FR-031 Edge Cases).
    void setSmearTimeHigh(float seconds) noexcept {
        tauHigh_ = std::clamp(sanitise(seconds, kDefaultSmearTimeHigh),
                              kMinSmearSeconds, kMaxSmearSeconds);
        poleTablesDirty_ = true;
    }

    // -------------------------------------------------------------------------
    // Target reads (FR-051): what was SET
    // -------------------------------------------------------------------------

    [[nodiscard]] float getSmearAmount()   const noexcept { return smearTarget_; }
    [[nodiscard]] float getDecoherence()   const noexcept { return decohereTarget_; }
    [[nodiscard]] float getSmearTilt()     const noexcept { return tiltTarget_; }
    [[nodiscard]] float getSmearTimeLow()  const noexcept { return tauLow_; }
    [[nodiscard]] float getSmearTimeHigh() const noexcept { return tauHigh_; }
    [[nodiscard]] std::uint32_t getSeed()  const noexcept { return seed_; }

    // -------------------------------------------------------------------------
    // Applied reads (FR-053): what the LAST FRAME used
    // -------------------------------------------------------------------------

    /// @note The three applied getters produce the S9 read table THEMSELVES.
    ///       The prepare-time smoother snap is NOT the mechanism: on a disabled
    ///       instance nothing ever calls process(), so a target set AFTER
    ///       prepare() would never reach current_ and SC-002 (e)'s exact
    ///       equality would fail.
    [[nodiscard]] float getAppliedSmearAmount() const noexcept {
        if (!prepared_) return 0.0f;        // FR-018 documented neutral
        if (!enabled_)  return smearTarget_;  // FR-019: no frame runs - mirror the target
        return smearSm_.getCurrentValue();    // FR-053: what the LAST FRAME used
    }

    [[nodiscard]] float getAppliedDecoherence() const noexcept {
        if (!prepared_) return 0.0f;
        if (!enabled_)  return decohereTarget_;
        return decohereSm_.getCurrentValue();
    }

    [[nodiscard]] float getAppliedSmearTilt() const noexcept {
        if (!prepared_) return 0.0f;
        if (!enabled_)  return tiltTarget_;
        return tiltSm_.getCurrentValue();
    }

    // -------------------------------------------------------------------------
    // Query (FR-015, FR-017, FR-018, FR-054)
    // -------------------------------------------------------------------------

    [[nodiscard]] bool        isPrepared() const noexcept { return prepared_; }
    [[nodiscard]] bool        isEnabled()  const noexcept { return enabled_; }
    [[nodiscard]] std::size_t getFftSize() const noexcept { return fftSize_; }
    [[nodiscard]] std::size_t getHopSize() const noexcept { return hopSize_; }
    [[nodiscard]] std::size_t getNumBins() const noexcept { return numBins_; }
    [[nodiscard]] double      getSampleRate() const noexcept { return sampleRate_; }

    /// @brief Reported latency: exactly fftSize when prepared AND enabled, else 0.
    /// @note CONSTANT for a prepared instance - no control value moves it. The
    ///       only switch is the prepare-time `enabled` flag, which cannot move
    ///       mid-render (a latency that moves mid-render is a click plus a host
    ///       renegotiation, aether_reverb.h:614-616).
    [[nodiscard]] std::size_t getLatencySamples() const noexcept {
        return (prepared_ && enabled_) ? fftSize_ : 0u;
    }

    /// @brief This component's OWN vectors, in bytes (FR-017). Excludes the
    ///        STFT / OverlapAdd / SpectralBuffer / FFT sub-object heap, which is
    ///        their policy, not this component's (plan S10 reports the figure).
    ///        Zero means "this instance will not render", not "holds no heap".
    [[nodiscard]] std::size_t getAllocatedBytes() const noexcept { return allocatedBytes_; }

    [[nodiscard]] std::size_t getClampEngagements()  const noexcept { return clampEngagements_; }
    [[nodiscard]] std::size_t getPoisonEngagements() const noexcept { return poisonEngagements_; }

    // -------------------------------------------------------------------------
    // Public pure functions (FR-042, FR-023)
    // -------------------------------------------------------------------------

    /// @brief Incoherence make-up gain for a decoherence amount (FR-042).
    ///
    /// Independently randomised per-frame phases sum INCOHERENTLY, so
    /// OverlapAdd's COLA factor - computed once at prepare (stft.h:249-262) and
    /// applied unconditionally (:300-308) - is wrong by a level that grows with
    /// the amount, about 6 dB at 1.0 (aether_reverb.h:623-626). The make-up is a
    /// scalar on the pulled TIME-DOMAIN samples, never on the bins, so FR-020's
    /// unity-gain claim holds literally.
    ///
    /// PUBLIC because SC-006 has to divide the make-up back out; nothing else on
    /// the query surface exposes g(d), and without it that criterion is circular.
    [[nodiscard]] static float coherenceMakeup(float decoherence) noexcept {
        const float x = std::clamp(decoherence, 0.0f, 1.0f)
                        * static_cast<float>(kCoherenceKnotCount - 1u);
        const float floored = std::floor(x);
        auto  k = static_cast<std::size_t>(floored);
        float t = x - floored;
        if (k >= kCoherenceKnotCount - 1u) {
            k = kCoherenceKnotCount - 2u;
            t = 1.0f;
        }
        // End tangents clamped - this is what keeps g(0) exactly 1.0
        // (cubicHermiteInterpolate returns c0 == y0 at t == 0, interpolation.h:88).
        const float ym1 = kCoherenceMakeup[(k > 0u) ? k - 1u : 0u];
        const float y0  = kCoherenceMakeup[k];
        const float y1  = kCoherenceMakeup[k + 1u];
        const float y2  = kCoherenceMakeup[((k + 2u) < kCoherenceKnotCount)
                                               ? k + 2u
                                               : kCoherenceKnotCount - 1u];
        return Interpolation::cubicHermiteInterpolate(ym1, y0, y1, y2, t);
    }

    /// @brief The pole this component WOULD use for a bin whose tilted time
    ///        constant is `tauSeconds` at `hopSize` / `sampleRate`.
    ///
    /// Static and pure, so FR-023's white-box case can drive it to synthetic
    /// extremes no render can reach. (FR-018's enumerated query surface gains
    /// this entry, and FR-023's reference to a `poleTable()` member is corrected
    /// to name this function - spec correction C-6 (i). No member named
    /// poleTable() is public.)
    [[nodiscard]] static float poleForTau(float tauSeconds, std::size_t hopSize,
                                          double sampleRate) noexcept {
        const float denom = static_cast<float>(sampleRate) * tauSeconds;
        if (!(denom > 0.0f)) return 0.0f;  // ordered compare: NaN and <= 0 both go to 0
        const float p = std::exp(-static_cast<float>(hopSize) / denom);
        return std::clamp(p, 0.0f, kMaxPole);  // FR-023 backstop
    }

private:
    // -------------------------------------------------------------------------
    // Control helpers
    // -------------------------------------------------------------------------

    /// FR-009's SUBSTITUTION rule (atmosphere_engine.h:911's shape), and it is
    /// DELIBERATELY THE OPPOSITE of ResonanceDriftNetwork's FR-008 REJECTION
    /// rule (`if (!isFinite(v)) return;`, previous value stands). A reviewer
    /// arriving from Vorago Phase 3 will expect rejection; this component
    /// substitutes the default. Either way a non-finite write is inert rather
    /// than poisoning, which is the property that matters.
    /// std::isnan/std::isinf/std::isfinite appear nowhere in this header
    /// (FR-008; tools/lint-nonfinite-symbols.js enforces it).
    [[nodiscard]] static float sanitise(float v, float dflt) noexcept {
        return detail::isFinite(v) ? v : dflt;
    }

    // -------------------------------------------------------------------------
    // The time-constant law (FR-030 - FR-034)
    // -------------------------------------------------------------------------
    //
    // THE TILT LAW (FR-032) is `tau_t(f) = clamp(tau_0(f) * (pivot/max(f, 20))^(t
    // * 0.75), kMinSmearSeconds, kMaxSmearSeconds)`, and it lives inside
    // rebuildPoleTables() below rather than in a scalar helper of its own: the
    // rebuild evaluates it for all three tilt endpoints in bulk SIMD passes, so a
    // per-bin `pow` helper would have no caller. (Two private statics -
    // `tiltedTau` and `clampTau` - were removed when that rewrite landed, for
    // exactly that reason. `poleForTau` stays: it is PUBLIC and FR-023's
    // white-box criterion drives it directly.)
    //
    // tilt == 0 is the EXACT identity, the same "identity at zero, bounded
    // exponent range" shape as HarmonicCloud::setSpectralGravity
    // (harmonic_cloud.h:478).
    //
    // THE SATURATION IS NORMATIVE, NOT A BUG (Clarifications Q5). At the shipped
    // defaults and tilt = +1, tau(20 Hz) = 3.0 * 50^0.75 = 56.4 s, which clamps
    // to kMaxSmearSeconds = 10 s; solving tau_t(f) = 10 s gives f ~= 95 Hz, so
    // EVERY BIN BELOW ~95 Hz SITS ON THE CLAMP. kTiltExponentRange is NOT shrunk
    // to avoid it: that would cost tilt its audible travel and would still clamp
    // at a non-default tauLow. At tilt = -1 the same arithmetic gives
    // tau_t(20 Hz) = 0.160 s and tau_t(20 kHz) = 2.364 s - inverted, both inside
    // the clamp.

    /// Fill poleNeg_ / poleZero_ / polePos_ for the current geometry and the
    /// current tauLow_ / tauHigh_ (FR-030, FR-034).
    ///
    /// THE ONLY WRITER OF THE THREE TABLES - which is what makes the
    /// `t != lastResolvedTilt_` guard on the per-frame resolve safe.
    ///
    /// COST, counted from the code below rather than asserted: SEVEN BULK PASSES
    /// over numBins - one `batchLog10` and six `batchPow10`
    /// (core/spectral_simd.h, Highway runtime dispatch, the
    /// processors/formant_preserver.h:121,:223 precedent for calling them on the
    /// render thread) - plus three straight-line scalar passes with no library
    /// call in them. THE PER-BIN SCALAR FORM WAS MEASURED AND REPLACED, it was
    /// not refactored on taste: `pow(ratio, u)` + two `pow(pivot/f, +-0.75)` +
    /// three `exp` is ~7 scalar transcendentals per bin, 92 065 ns per rebuild at
    /// numBins = 2049 on the reference machine, which put SC-013 (d) 2.1x over
    /// its gate (108 326 ns/block measured, gate 51 000). Rewriting the same law
    /// with `exp`/`log` identities instead of `pow` only reached 60 115 ns - MSVC
    /// does not vectorise a loop carrying six math calls and a division - so the
    /// transcendentals were moved into the bulk SIMD primitives, where they cost
    /// ~0.37 ns per element instead of ~5 ns. FR-060's lever ladder mandates
    /// exactly this ("reduce cost, never raise the baseline").
    ///
    /// THE LAW IS UNCHANGED, and the algebra is the whole of the difference:
    ///   u(k)        = clamp((log10 f - log10 20) / log10 1000, 0, 1)   [FR-030]
    ///   tau_0(k)    = tauLow * (tauHigh/tauLow)^u  =>  1/tau_0 = 10^-(log10 tauLow + u*log10 r)
    ///   tau_t(k)    = tau_0 * (pivot/f)^(t*0.75)   =>  1/tau_t = (1/tau_0) * 10^(-t*T),
    ///                 T = 0.75 * (log10 pivot - log10 f)               [FR-032]
    ///   pole        = exp(-hop / (sampleRate * tau)) = 10^(-hop*log10(e)/sampleRate * (1/tau))
    /// Working in INVERSE tau is what removes every division from the per-bin
    /// glue: the clamp to [kMinSmearSeconds, kMaxSmearSeconds] becomes a clamp of
    /// 1/tau to [1/kMaxSmearSeconds, 1/kMinSmearSeconds] (same set, the map is
    /// monotone), and the pole exponent is then one multiply. Measured agreement
    /// with the scalar form it replaces: max |pole difference| 2.98e-07 over all
    /// 2049 bins at fftSize 4096, i.e. one float ULP at pole ~ 0.99.
    ///
    /// The deferral to the top of processBlock is unchanged (FR-036): N setter
    /// calls inside one block still cost ONE rebuild, a setter on an unprepared
    /// instance is still inert by construction, and the cost still lands inside
    /// the path SC-013 (d) measures.
    ///
    /// Allocation-free (FR-064): the three already-sized tables and the
    /// already-sized per-frame scratch are overwritten in place, never resized,
    /// and NO NEW TABLE IS ADDED - which is a requirement, not a preference,
    /// because SC-008 bounds the footprint at 128 KiB and the shipped figure at
    /// fftSize 4096 is 120.0 KiB, i.e. 28 bytes less headroom than one further
    /// numBins array would need. poleScratch_ is reused as the working buffer;
    /// it is regenerated before any read because the last statement here sets
    /// tiltScratchDirty_.
    void rebuildPoleTables() noexcept {
        poleTablesDirty_ = false;
        if (numBins_ == 0 || poleZero_.size() != numBins_) return;  // unprepared: inert

        float* const neg  = poleNeg_.data();
        float* const zero = poleZero_.data();
        float* const pos  = polePos_.data();
        float* const work = poleScratch_.data();

        // --- Pass 1: log10 of every bin centre, floored at the 20 Hz anchor. --
        // The floor is what makes bin 0 (and every sub-anchor bin at a coarse
        // geometry) take u = 0 and tilt about 20 Hz, exactly as the scalar form
        // did with its `(k == 0) ? kTauAnchorLowHz` special case and
        // the tilt law's own `max(f, kTauAnchorLowHz)`. There is no log(0)
        // to guard: the argument is never below 20.
        const float binHz = static_cast<float>(sampleRate_) / static_cast<float>(fftSize_);
        for (std::size_t k = 0; k < numBins_; ++k) {
            work[k] = std::max(static_cast<float>(k) * binHz, kTauAnchorLowHz);
        }
        batchLog10(work, work, numBins_);

        // --- Pass 2: the three exponents. ------------------------------------
        const float log10AnchorLow = std::log10(kTauAnchorLowHz);
        const float log10Span      = std::log10(kTauAnchorHighHz / kTauAnchorLowHz);
        const float log10Ratio     = std::log10(tauHigh_ / tauLow_);
        const float log10TauLow    = std::log10(tauLow_);
        const float log10Pivot     = std::log10(kTiltPivotHz);
        for (std::size_t k = 0; k < numBins_; ++k) {
            const float l = work[k];
            const float u = std::clamp((l - log10AnchorLow) / log10Span, 0.0f, 1.0f);
            const float t = kTiltExponentRange * (log10Pivot - l);  // T
            neg[k]        = -t;                                     // -> 10^-T
            pos[k]        = t;                                      // -> 10^+T
            zero[k]       = -(log10TauLow + u * log10Ratio);        // -> 1 / tau_0
        }
        batchPow10(neg, neg, numBins_);
        batchPow10(pos, pos, numBins_);
        batchPow10(zero, zero, numBins_);

        // --- Pass 3: inverse taus, clamped, turned into pole exponents. -------
        // tilt = +1 lengthens the lows, so its INVERSE tau carries 10^-T; tilt =
        // -1 carries 10^+T. Getting that pair the wrong way round inverts the
        // whole tilt control and SC-004 (d) is the criterion that sees it.
        constexpr float kLog10e = 0.43429448190325176f;
        const float     scale =
            -static_cast<float>(hopSize_) / static_cast<float>(sampleRate_) * kLog10e;
        constexpr float kInvTauMin = 1.0f / kMaxSmearSeconds;  // tau = 10 s
        constexpr float kInvTauMax = 1.0f / kMinSmearSeconds;  // tau = 0.02 s
        for (std::size_t k = 0; k < numBins_; ++k) {
            const float invBase = zero[k];
            const float tMinus  = neg[k];  // 10^-T
            const float tPlus   = pos[k];  // 10^+T
            neg[k]  = scale * std::clamp(invBase * tPlus, kInvTauMin, kInvTauMax);
            pos[k]  = scale * std::clamp(invBase * tMinus, kInvTauMin, kInvTauMax);
            zero[k] = scale * std::clamp(invBase, kInvTauMin, kInvTauMax);
        }
        batchPow10(neg, neg, numBins_);
        batchPow10(pos, pos, numBins_);
        batchPow10(zero, zero, numBins_);

        // --- Pass 4: FR-023's defensive backstop, the one poleForTau applies. --
        for (std::size_t k = 0; k < numBins_; ++k) {
            neg[k]  = std::clamp(neg[k], 0.0f, kMaxPole);
            zero[k] = std::clamp(zero[k], 0.0f, kMaxPole);
            pos[k]  = std::clamp(pos[k], 0.0f, kMaxPole);
        }

        tiltScratchDirty_ = true;
    }

    /// Resolve the three tilt tables down to the single per-frame pole table
    /// both channels read (FR-034, D-7; plan S4.3).
    ///
    /// THE INTERPOLATED FORM *IS* THE SPECIFIED LAW, not an approximation of
    /// some other law a test could find a discrepancy against. Negative tilt
    /// blends poleNeg_ -> poleZero_ over t in [-1, 0]; positive tilt blends
    /// poleZero_ -> polePos_ over t in [0, +1]. The result is monotone in `t`
    /// bin-by-bin (a linear blend of two ordered endpoints) and stays inside
    /// [0, kMaxPole] by construction, so no re-clamp is needed here.
    ///
    /// Two multiplies and an add per bin: THERE IS NO TRANSCENDENTAL IN THE
    /// PER-FRAME PATH. The only transcendentals inside processBlock are the
    /// deferred rebuild's, and only on a block where an endpoint setter fired.
    ///
    /// The `t != lastResolvedTilt_` guard is behaviour-identical, not an
    /// approximation: the resolve is a pure function of `t` and the three
    /// tables, rebuildPoleTables() is their ONLY writer, and it sets
    /// tiltScratchDirty_. Exact float equality is the right test here precisely
    /// because a settled smoother snaps (smoother.h:197-201) - a held control
    /// therefore costs one compare per frame instead of a full table pass.
    void resolveTiltScratch(float tilt) noexcept {
        if (!tiltScratchDirty_ && tilt == lastResolvedTilt_) return;
        if (numBins_ == 0 || poleScratch_.size() != numBins_) return;  // unprepared: inert

        if (tilt <= 0.0f) {
            const float blend = tilt + 1.0f;  // -1 -> poleNeg_, 0 -> poleZero_
            for (std::size_t k = 0; k < numBins_; ++k) {
                poleScratch_[k] = poleNeg_[k] + (poleZero_[k] - poleNeg_[k]) * blend;
            }
        } else {
            for (std::size_t k = 0; k < numBins_; ++k) {  // 0 -> poleZero_, +1 -> polePos_
                poleScratch_[k] = poleZero_[k] + (polePos_[k] - poleZero_[k]) * tilt;
            }
        }

        lastResolvedTilt_ = tilt;
        tiltScratchDirty_ = false;
    }

    // -------------------------------------------------------------------------
    // The render path (FR-013, FR-014, FR-016, FR-046, FR-061; plan S5, S7.3)
    // -------------------------------------------------------------------------

    /// Push one chunk of at most kProcessChunkSamples, drain every frame that
    /// became available, then pop the same number of samples back out.
    ///
    /// THREE INVARIANTS RIDE ON THE ORDER BELOW AND EACH FAILS SILENTLY IF
    /// DISTURBED (the three systems/atmosphere_engine.h:2266-2302 records,
    /// transcribed to this component):
    ///
    /// (a) FRAME-MAJOR, CHANNEL-MINOR. The three smoothers advance EXACTLY ONCE
    ///     PER HOP of audio, outside the channel loop, and both channels share
    ///     the one control value the advance produced. Moving the advance inside
    ///     the channel loop doubles the advance rate - halving the 50 ms
    ///     constant to ~25 ms - and hands L and R values one hop apart inside
    ///     the same frame. NO CRITERION SWEEPING SETTLED VALUES CAN SEE THAT: it
    ///     is still click-free and still costs the same. FR-053's applied reads
    ///     exist to make it observable and SC-017
    ///     (SpectralSmear_ControlCadence) IS THE ONLY CRITERION THAT READS THEM.
    ///
    /// (b) THE PULL IS INSIDE THE DRAIN LOOP AND IS ALWAYS EXACTLY hopSize_,
    ///     NEVER `n`. OverlapAdd::synthesize() accumulates at offset 0
    ///     UNCONDITIONALLY (primitives/stft.h:300-308); the per-frame hop offset
    ///     comes only from pullSamples shifting the buffer left (:340-357). Two
    ///     synthesises without an intervening pull of exactly hopSize stack both
    ///     frames at the same offset and destroy COLA - and it presents as a
    ///     windowing bug. A wrong pull SIZE is worse still: pullSamples returns
    ///     SILENTLY on an oversized request (:339-342) without zeroing the
    ///     destination, i.e. a stale-buffer read rather than a detectable
    ///     failure.
    ///
    /// (c) L IS ALWAYS PROCESSED BEFORE R. Part of the determinism contract even
    ///     though FR-044 gives the channels independent streams: it fixes the
    ///     order of any future shared state. WITH TWO INDEPENDENT STREAMS AND NO
    ///     SHARED PER-CHANNEL STATE, SWAPPING THE LOOP ORDER IS BIT-IDENTICAL
    ///     TODAY, so no criterion can change verdict on it and it is discharged
    ///     BY INSPECTION at the compliance pass (plan S17 C-8). The implementer
    ///     must not go hunting for a test that cannot exist.
    ///
    /// `while`, not `if`: analyze() consumes only hopSize_ (stft.h:171), so a
    /// chunk that crosses two hop boundaries must drain both.
    void pumpChunk(float* l, float* r, std::size_t n) noexcept {
        stft_[0].pushSamples(l, n);
        stft_[1].pushSamples(r, n);

        while (stft_[0].canAnalyze()) {
            assert(stft_[1].canAnalyze());  // identical push counts => lockstep

            // (a) ONCE per frame-pair, OUTSIDE the channel loop.
            const float a = std::clamp(smearSm_.process(), 0.0f, 1.0f);
            const float d = std::clamp(decohereSm_.process(), 0.0f, 1.0f);
            const float t = std::clamp(tiltSm_.process(), -1.0f, 1.0f);

            resolveTiltScratch(t);               // shared by both channels
            const float g = coherenceMakeup(d);  // FR-042, one value per frame

            for (std::size_t ch = 0; ch < 2u; ++ch) {  // (c) L strictly before R
                stft_[ch].analyze(spectrum_[ch]);

                // FR-045: the magnitude pass runs FIRST, so the magnitude memory
                // always integrates the ANALYSED magnitudes and never a value
                // this component perturbed. Since the phase pass writes only
                // phase, the two orderings are BIT-IDENTICAL TODAY and no render
                // can distinguish them - the ordering is stated here so a future
                // magnitude-domain addition cannot silently create a feedback
                // path. Discharged by inspection, like (c).
                const bool poisoned = smearMagnitudes(ch, a);

                // FR-043, ONE rule governing BOTH skips: decohere() is called on
                // EVERY frame. Passing 0.0f takes its burn-only branch, so the
                // draw count per channel per frame is numBins - 2 whether the
                // identity gate engaged, the poison path fired, or the full path
                // ran. Calling it conditionally would leave rng_[ch] numBins - 2
                // draws behind a clean render FOREVER after the first poisoned
                // frame, and FR-062's deliberate no-latch design makes that
                // reachable in normal operation, not only at a terminal state.
                decohere(ch, poisoned ? 0.0f : d);

                ola_[ch].synthesize(spectrum_[ch]);
                ola_[ch].pullSamples(hopScratch_[ch].data(), hopSize_);  // (b) ALWAYS hopSize_
                writeHopToFifo(ch, g);
            }

            // The cursors are SHARED by the two channels (one ring geometry, two
            // data arrays), so they advance ONCE per frame, after both writes.
            fifoWrite_ = (fifoWrite_ + hopSize_) & fifoMask_;
            fifoCount_ += hopSize_;

            // prevMakeup_ is updated once per frame AFTER BOTH CHANNELS HAVE
            // CONSUMED IT - a per-channel update would give R a different ramp
            // from L (FR-046).
            prevMakeup_ = g;
        }

        popFifo(l, r, n);
    }

    /// Apply FR-046's per-sample make-up ramp and FR-061's clamp, then write one
    /// hop into this channel's FIFO (plan S7.3).
    ///
    /// WHY A RAMP AND NOT AetherReverb's PER-HOP CONSTANT
    /// (effects/aether_reverb.h:4097-4102, `dstL[i] *= g;`) - a DELIBERATE
    /// deviation: `decoherence` is a modulation target here (FR-033) and
    /// FR-035's 50 ms smoothing runs on the FRAME clock (93.75 Hz at the
    /// reference geometry), so a 0 -> 1 step moves `d` to 1 - 0.34424 = 0.656
    /// after ONE frame and `g` from 1.000 to ~1.55 at a single sample boundary.
    /// That is a ~55 % instantaneous amplitude step, an order of magnitude above
    /// the peak inter-sample delta of a 1 kHz tone at 48 kHz
    /// (A * 2*pi*1000/48000 = 0.131*A), i.e. a textbook 5-sigma derivative
    /// outlier for ClickDetector. SC-012 (c) is the enforcing measurement.
    ///
    /// The weight is (i + 1) / hopSize, so the ramp ends EXACTLY at `g` and the
    /// next frame's ramp starts from the value this one finished on.
    ///
    /// THE CLAMP IS AN ORDERED COMPARISON (std::clamp), not a bit test, so
    /// -ffast-math cannot fold it. Note that std::clamp does NOT reject NaN
    /// (`v < lo` and `hi < v` are both false for a NaN), which is why the
    /// finiteness backstop is FR-062's frame accumulator, UPSTREAM, and
    /// explicitly not this clamp.
    void writeHopToFifo(std::size_t ch, float g) noexcept {
        const float* src = hopScratch_[ch].data();
        const float  inv = 1.0f / static_cast<float>(hopSize_);
        for (std::size_t i = 0; i < hopSize_; ++i) {
            const float w       = static_cast<float>(i + 1u) * inv;  // ends exactly at g
            const float gain    = prevMakeup_ + (g - prevMakeup_) * w;
            const float v       = src[i] * gain;
            const float clamped = std::clamp(v, -kOutputClamp, kOutputClamp);
            if (clamped != v) {
                ++clampEngagements_;  // once per engaging SAMPLE (FR-054)
            }
            fifo_[ch][(fifoWrite_ + i) & fifoMask_] = clamped;
        }
    }

    /// Emit `n` output samples (FR-014).
    ///
    /// THE WARM-UP COUNTER IS THE RULE, not an emptiness test. "Emit zeros
    /// whenever the FIFO happens to be empty" makes the offset a function of the
    /// caller's block size - (ceil(fftSize/chunk) - 1) * chunk, measured at
    /// 960 / 1020 / 1022 samples for chunks 64 / 30 / 7 at fftSize 1024
    /// (effects/aether_reverb.h:643-660). Never fftSize, and never the same
    /// twice, which would make getLatencySamples() a lie in every host and put
    /// SC-011 (SpectralSmear_PartitionInvariance) out of reach.
    ///
    /// The `else` branch is UNREACHABLE in a prepared instance: plan S11's
    /// occupancy proof gives occupancy in (0, hop] for every n after the
    /// warm-up. It exists so that a future cadence change degrades to silence
    /// rather than to unwritten memory.
    void popFifo(float* l, float* r, std::size_t n) noexcept {
        for (std::size_t i = 0; i < n; ++i) {
            if (warmupRemaining_ > 0u) {
                --warmupRemaining_;
                l[i] = 0.0f;
                r[i] = 0.0f;
            } else if (fifoCount_ > 0u) {
                l[i]      = fifo_[0][fifoRead_];
                r[i]      = fifo_[1][fifoRead_];
                fifoRead_ = (fifoRead_ + 1u) & fifoMask_;
                --fifoCount_;
            } else {
                l[i] = 0.0f;
                r[i] = 0.0f;
            }
        }
    }

    /// The magnitude pass (FR-020 - FR-025, FR-062; plan S6).
    /// @returns true if the frame was poisoned (FR-062) and the caller must burn
    ///          the phase draws instead of perturbing.
    ///
    /// ONE PASS OVER k IN [0, numBins_), AND DC (BIN 0) AND NYQUIST
    /// (numBins_ - 1) ARE INCLUDED (FR-025). Unlike phase, their magnitude is a
    /// free real quantity, and excluding them would leave two unsmeared spikes
    /// in the fog. DO NOT copy FR-040's `for (k = 1; k + 1 < numBins_; ++k)`
    /// bounds from decohere() into this loop - the two loops sit adjacent and
    /// that is the single most likely mistake in this component.
    /// SpectralSmear_DcNyquistSmear is the criterion that catches it.
    ///
    /// THE INTEGRATOR (FR-020) is state[k] = (1 - p)*mag[k] + p*state[k],
    /// written in the fused form m + p*(state - m) (one FMA). NORMALISATION BY
    /// (1 - p) IS A REQUIREMENT, NOT A DETAIL: it makes the steady-state gain
    /// exactly unity at every p, which is what lets FR-021's identity claim and
    /// SC-003's null test coexist with a smear knob that never changes the
    /// long-term average spectrum (a declared Non-Goal). The discretisation is
    /// EXACT, not an approximation: the continuous one-pole dx/dt = (m - x)/tau
    /// sampled at the frame period hopSize/sampleRate has the closed-form
    /// solution x[n+1] = m + (x[n] - m) * exp(-hop/(sampleRate*tau)), which is
    /// precisely poleForTau(). There is no bilinear or backward-Euler step
    /// anywhere here, so `tau` means exactly what SC-004 (c) measures.
    ///
    /// THE BLEND (FR-021, D-12). `amount` blends the analysed magnitude against
    /// the integrator state; it is NOT a scale on the pole. Under the rejected
    /// form p(k) = amount * poleTable(k), at the reference geometry with the
    /// shipped defaults poleTable(100 Hz) = 0.99366, so
    /// amount in {0, .25, .5, .75, 1} maps to effective time constants
    /// 0, 7.6 ms, 15 ms, 36 ms, 1.68 s - four "no smearing" points and an
    /// endpoint, which would make a roadmap-declared modulation target do
    /// nothing over 99 % of a modulator's sweep.
    ///
    /// COST. SpectralBuffer keeps a lazy dual representation with dirty flags
    /// (primitives/spectral_buffer.h:7-12, :179-198), so writing BOTH magnitude
    /// and phase costs the same two bulk SIMD conversions as writing phase
    /// alone: the magnitude half is close to free relative to the round trip it
    /// shares.
    [[nodiscard]] bool smearMagnitudes(std::size_t ch, float amount) noexcept {
        SpectralBuffer& spectrum = spectrum_[ch];
        float*          state    = magState_[ch].data();
        const float*    pole     = poleScratch_.data();

        // FR-062: ONE finiteness test per frame per channel, on an accumulator
        // of the ANALYSED magnitudes - never per bin (the
        // systems/atmosphere_engine.h:2250-2260 cost rule). The accumulator is
        // sound because the analysed magnitude is the only entry point for
        // poison: a non-finite input sample gives mag = sqrt(NaN...) = NaN and
        // the sum inherits it, while the integrator state can only have become
        // non-finite through a previous frame's magnitude - and that frame's
        // clear already zeroed it.
        float accum = 0.0f;

        if (primeNext_[ch]) {
            // FR-022 priming (Clarifications Q1). The memory takes this frame
            // verbatim and integration starts on the NEXT one. No magnitude is
            // written: `amount` blends state against mag and state == mag here,
            // so the write would be the identity. SC-018 asserts exactly this.
            for (std::size_t k = 0; k < numBins_; ++k) {
                const float m = spectrum.getMagnitude(k);
                accum += m;
                state[k] = m;
            }
            primeNext_[ch] = false;
        } else if (amount == 0.0f) {
            // FR-021's EXACT-identity gate. The integrator STILL runs, so the
            // memory is never stale when the knob comes back up, but NO
            // magnitude is written at all: leaving the analysed spectrum
            // untouched is more exactly transparent than writing mag[k] back,
            // which would force a polar round trip for no change.
            for (std::size_t k = 0; k < numBins_; ++k) {
                const float m = spectrum.getMagnitude(k);
                accum += m;
                const float st = m + pole[k] * (state[k] - m);
                // FR-024 denormal flush. THE FLUSH EXISTS FOR HOSTS THAT HAVE
                // NOT SET THE MXCSR FTZ/DAZ BITS: this repo's own test binaries
                // enable FTZ/DAZ process-wide
                // (tests/test_helpers/enable_ftz_daz.h:27-32, called from
                // dsp/tests/dsp_test_main.cpp), so NO RENDER CAN OBSERVE ITS
                // ABSENCE - both the numerical result and the timing are
                // identical with and without it. FR-024 is therefore discharged
                // BY INSPECTION at the compliance pass, not by a criterion, and
                // no white-box helper is added for it. It is an ORDERED
                // comparison, not a bit test, so -ffast-math cannot fold it -
                // and NaN fails an ordered <, so a poisoned state passes
                // through to FR-062's accumulator instead of being silently
                // zeroed here. Magnitudes are sqrt(re^2 + im^2) >= 0, so the
                // one-sided test is complete.
                state[k] = (st < kDenormalFloor) ? 0.0f : st;
            }
        } else {
            for (std::size_t k = 0; k < numBins_; ++k) {
                const float m = spectrum.getMagnitude(k);
                accum += m;
                const float st = m + pole[k] * (state[k] - m);  // FR-020
                state[k]       = (st < kDenormalFloor) ? 0.0f : st;  // FR-024, see above
                spectrum.setMagnitude(k, m + amount * (state[k] - m));  // FR-021
            }
        }

        if (!detail::isFinite(accum)) {
            return handlePoison(ch);
        }
        return false;
    }

    /// FR-062's poison clear: zero the memory, RE-ARM the priming flag,
    /// synthesise silence for this frame, count it, and CONTINUE (plan S8).
    /// @returns always true - "this frame was poisoned", which is what makes the
    ///          caller pass 0.0f to decohere() and burn the draws instead of
    ///          perturbing a spectrum that is now identically zero.
    ///
    /// (a) THE CLEAR RE-ARMS THE PRIME, it does not merely zero. Zeroing alone
    ///     would leave the integrator rebuilding the magnitude envelope through
    ///     (1 - p) per frame - p is ~0.99 across the band at the shipped
    ///     endpoints - so a full recovery would take several time constants
    ///     (seconds at the low anchor) rather than one frame. Re-arming makes the
    ///     NEXT clean frame write state[k] = mag[k] verbatim, so recovery is
    ///     complete one frame after the last poisoned one.
    ///     SpectralSmear_MagnitudePrimingAfterPoison (SC-018 arm (b)) is the
    ///     direct assertion on the re-arm; SC-016 (iii) measures the recovery.
    ///
    /// (b) BOTH MAGNITUDE AND PHASE ARE ZEROED, AND THAT IS NOT BELT-AND-BRACES
    ///     (plan S17 C-3). computePolarBulk produces phase = atan2(imag, real),
    ///     which is NaN for a non-finite input, and reconstructCartesianBulk then
    ///     computes mag * cos(phase) - and 0.0f * NaN is NaN. Zeroing the
    ///     magnitudes alone would therefore still synthesise NaN into the
    ///     overlap-add. Zeroing the phase too makes the reconstruction exactly
    ///     0 * cos(0) = 0. The loop covers k in [0, numBins_), DC and Nyquist
    ///     INCLUDED - a partial clear is a partial poison.
    ///
    /// (c) IT DOES NOT LATCH, AND THAT IS A DELIBERATE, DOCUMENTED DEVIATION
    ///     FROM AtmosphereEngine (atmosphere_engine.h:2244-2260, "no
    ///     auto-resume: reset() ... is the one documented recovery"). That stage
    ///     sits inside a voice, where a dead voice is one silent note; this one
    ///     sits on the GLOBAL bus post-voice-sum, where latching would convert a
    ///     single poisoned frame - one denormal-ridden host buffer, one
    ///     upstream plugin glitch - into a dead instrument that only reset()
    ///     could revive. The magnitude memory is the ONLY state that can carry
    ///     poison forward, and (a) clears and re-primes it, so continuing is
    ///     safe rather than optimistic.
    ///
    /// The caller does NOT skip the phase pass on this path: it calls
    /// decohere(ch, 0.0f), whose burn-only branch advances rng_[ch] by the same
    /// numBins_ - 2 draws every other frame costs (FR-043). The zeroed spectrum
    /// stays zeroed - the burn branch performs no setPhase.
    [[nodiscard]] bool handlePoison(std::size_t ch) noexcept {
        std::fill(magState_[ch].begin(), magState_[ch].end(), 0.0f);  // (a) clear
        primeNext_[ch] = true;                                        // (a) RE-ARM

        SpectralBuffer& spectrum = spectrum_[ch];
        for (std::size_t k = 0; k < numBins_; ++k) {  // (b) synthesise silence
            spectrum.setMagnitude(k, 0.0f);
            spectrum.setPhase(k, 0.0f);
        }

        ++poisonEngagements_;  // (c) FR-054's counter, zeroed by prepare() and reset()
        return true;           // (d) CONTINUE - no latch
    }

    /// The phase pass (FR-040 - FR-045; plan S7.1).
    ///
    /// ONE PASS OVER k IN [1, numBins_ - 1): DC (BIN 0) AND NYQUIST
    /// (numBins_ - 1) ARE EXCLUDED (FR-040), because their phase is not free in
    /// a real spectrum (systems/atmosphere_engine.h:2325-2328). They also
    /// consume NO draw - that is a determinism decision, not an optimisation,
    /// and it fixes the draw count at numBins_ - 2 per channel per frame (1023
    /// at the reference geometry). DO NOT copy smearMagnitudes()'s
    /// `for (k = 0; k < numBins_; ++k)` bounds into this loop - the two loops
    /// sit adjacent and that is the single most likely mistake in this
    /// component; SpectralSmear_DcNyquistSmear is the criterion that catches
    /// the inverse error.
    ///
    /// MAGNITUDE IS NEVER WRITTEN - only phase moves, so the stage is a
    /// decoherer and not a filter. Xorshift32::nextFloat() is BIPOLAR on
    /// [-1, +1] (core/random.h:59-63), so the perturbation is uniform on
    /// +/-amount*pi: 0 is the identity and 1 is full decoherence.
    ///
    /// THE GATE IS AN EXACT-VALUE GATE ON A SETTLED FLOAT, never a threshold:
    /// any non-zero `amount`, however small, takes the full path. It is
    /// reachable and stable because OnePoleSmoother::process() snaps to the
    /// target on completion (primitives/smoother.h:197-201), and 0.0f is the
    /// shipped default. Skipping the write is MORE exactly transparent than
    /// adding zero, which would still force a polar round trip and re-round the
    /// spectrum.
    ///
    /// FR-043's BURN. The zero branch still draws numBins_ - 2 values and
    /// discards them, so the stream position is a function of ELAPSED FRAMES
    /// ALONE - never of how long the knob sat at zero, and never of whether the
    /// frame was poisoned (pumpChunk passes 0.0f rather than skipping the call,
    /// so ONE rule governs both skips). Without the burn, rng_[ch] would fall
    /// numBins_ - 2 draws behind a clean render FOREVER after the first gated
    /// or poisoned frame. SC-009 (c) measures the consequence over a FIXED
    /// ABSOLUTE window.
    void decohere(std::size_t ch, float amount) noexcept {
        Xorshift32& rng = rng_[ch];

        if (amount == 0.0f) {
            for (std::size_t k = 1; k + 1 < numBins_; ++k) {
                (void) rng.nextFloat();  // FR-043: BURN, do not perturb
            }
            return;
        }

        SpectralBuffer& spectrum = spectrum_[ch];
        for (std::size_t k = 1; k + 1 < numBins_; ++k) {
            spectrum.setPhase(k, spectrum.getPhase(k) + amount * kPi * rng.nextFloat());
        }
    }

    // -------------------------------------------------------------------------
    // State (declaration order is the plan S1.5 order)
    // -------------------------------------------------------------------------

    // Geometry - valid whenever prepared_, even with enabled_ == false (FR-018/Q6)
    double      sampleRate_ = 0.0;
    std::size_t fftSize_    = 0;
    std::size_t hopSize_    = 0;
    std::size_t numBins_    = 0;
    float       frameRate_  = 0.0f;  // sampleRate_ / hopSize_, the smoother clock
    bool        prepared_   = false;
    bool        enabled_    = false;

    // Sub-objects, one pair per channel (prepared only when enabled_)
    std::array<STFT, 2>           stft_{};
    std::array<OverlapAdd, 2>     ola_{};
    std::array<SpectralBuffer, 2> spectrum_{};

    // Owned heap - the whole of getAllocatedBytes() (FR-017)
    std::vector<float> poleNeg_;                    // numBins_, tilt = -1
    std::vector<float> poleZero_;                   // numBins_, tilt =  0
    std::vector<float> polePos_;                    // numBins_, tilt = +1
    std::vector<float> poleScratch_;                // numBins_, tilt-resolved once per frame
    std::array<std::vector<float>, 2> magState_;    // numBins_ each (FR-022)
    std::array<std::vector<float>, 2> fifo_;        // fifoCapacity_ each (FR-016)
    std::array<std::vector<float>, 2> hopScratch_;  // hopSize_ each

    // FIFO cursors - SHARED by the two channels (one ring geometry, two arrays)
    std::size_t fifoCapacity_    = 0;
    std::size_t fifoMask_        = 0;
    std::size_t fifoRead_        = 0;
    std::size_t fifoWrite_       = 0;
    std::size_t fifoCount_       = 0;
    std::size_t warmupRemaining_ = 0;  // FR-014 counter, NOT an emptiness test

    // Control
    OnePoleSmoother smearSm_;
    OnePoleSmoother decohereSm_;
    OnePoleSmoother tiltSm_;
    float smearTarget_    = kDefaultSmearAmount;
    float decohereTarget_ = kDefaultDecoherence;
    float tiltTarget_     = kDefaultSmearTilt;
    float tauLow_         = kDefaultSmearTimeLow;
    float tauHigh_        = kDefaultSmearTimeHigh;

    // Per-frame derived state
    float lastResolvedTilt_ = 0.0f;   // the tilt poleScratch_ was built for
    bool  tiltScratchDirty_ = true;   // set by prepare(), and by every table rebuild
    bool  poleTablesDirty_  = false;  // set by setSmearTimeLow/High, consumed in processBlock
    float prevMakeup_       = 1.0f;   // FR-046's ramp origin

    // Per-channel flags / streams
    std::array<bool, 2>       primeNext_{true, true};  // FR-022
    std::array<Xorshift32, 2> rng_{Xorshift32{1u}, Xorshift32{1u}};
    std::uint32_t             seed_ = kDefaultSeed;

    // Counters (FR-054)
    std::size_t clampEngagements_  = 0;
    std::size_t poisonEngagements_ = 0;
    std::size_t allocatedBytes_    = 0;
};

} // namespace Krate::DSP
