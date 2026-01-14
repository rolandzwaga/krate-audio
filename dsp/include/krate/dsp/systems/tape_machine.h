// ==============================================================================
// Layer 3: System Component - Tape Machine
// ==============================================================================
// Complete tape machine emulation composing TapeSaturator, NoiseGenerator,
// LFO, Biquad, and OnePoleSmoother for authentic analog tape character.
//
// Features:
// - Machine models (Studer/Ampex) with preset defaults
// - Tape formulations (Type456/Type900/TypeGP9) affecting saturation
// - Tape speeds (7.5/15/30 ips) affecting frequency response
// - Head bump (low-frequency enhancement)
// - HF rolloff (high-frequency attenuation)
// - Wow and flutter (pitch modulation)
// - Tape hiss (pink noise with HF emphasis)
// - 5ms parameter smoothing for click-free operation
//
// Signal Flow (FR-033):
// Input Gain -> Saturation -> Head Bump -> HF Rolloff -> Wow/Flutter -> Hiss -> Output Gain
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, constexpr, [[nodiscard]])
// - Principle IX: Layer 3 (depends on Layers 0, 1, 2)
// - Principle X: DSP Constraints (smoothing for parameter changes)
// - Principle XII: Test-First Development
//
// Reference: specs/066-tape-machine/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/lfo.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/processors/noise_generator.h>
#include <krate/dsp/processors/tape_saturator.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Krate {
namespace DSP {

// =============================================================================
// Enumerations (FR-031, FR-004, FR-005)
// =============================================================================

/// @brief Machine model selection affecting preset defaults.
enum class MachineModel : uint8_t {
    Studer = 0,  ///< Studer-style: 80Hz head bump at 7.5ips, 50Hz at 15ips, 35Hz at 30ips
    Ampex = 1    ///< Ampex-style: 100Hz head bump at 7.5ips, 60Hz at 15ips, 40Hz at 30ips
};

/// @brief Tape speed selection affecting frequency characteristics.
enum class TapeSpeed : uint8_t {
    IPS_7_5 = 0,  ///< 7.5 inches per second (lo-fi, pronounced head bump, HF rolloff ~10kHz)
    IPS_15 = 1,   ///< 15 inches per second (standard, moderate characteristics, HF rolloff ~15kHz)
    IPS_30 = 2    ///< 30 inches per second (hi-fi, subtle head bump, HF rolloff ~20kHz)
};

/// @brief Tape formulation selection affecting saturation behavior (FR-034).
enum class TapeType : uint8_t {
    Type456 = 0,  ///< Classic warm: -3dB drive offset, 1.2x saturation, +0.1 bias
    Type900 = 1,  ///< Hot punchy: +2dB drive offset, 1.0x saturation, 0.0 bias
    TypeGP9 = 2   ///< Modern clean: +4dB drive offset, 0.8x saturation, -0.05 bias
};

// =============================================================================
// TapeMachine Class
// =============================================================================

/// @brief Layer 3 tape machine system composing saturation, filtering, and modulation.
///
/// Provides comprehensive tape machine emulation by composing:
/// - TapeSaturator for core tape saturation character
/// - Biquad filters for head bump and HF rolloff
/// - LFOs for wow and flutter modulation
/// - NoiseGenerator for tape hiss
/// - OnePoleSmoothers for click-free parameter changes
///
/// @par Signal Flow (FR-033)
/// Input Gain -> Saturation -> Head Bump -> HF Rolloff -> Wow/Flutter -> Hiss -> Output Gain
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept processing, no allocations)
/// - Principle III: Modern C++ (C++20)
/// - Principle IX: Layer 3 (depends only on Layers 0, 1, 2)
/// - Principle XII: Test-First Development
///
/// @see specs/066-tape-machine/spec.md
class TapeMachine {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kDefaultSmoothingMs = 5.0f;
    static constexpr float kMinInputOutputDb = -24.0f;
    static constexpr float kMaxInputOutputDb = +24.0f;
    static constexpr float kMinHeadBumpFreq = 30.0f;
    static constexpr float kMaxHeadBumpFreq = 120.0f;
    static constexpr float kMinHfRolloffFreq = 5000.0f;
    static constexpr float kMaxHfRolloffFreq = 22000.0f;
    static constexpr float kMinWowRate = 0.1f;
    static constexpr float kMaxWowRate = 2.0f;
    static constexpr float kMinFlutterRate = 2.0f;
    static constexpr float kMaxFlutterRate = 15.0f;
    static constexpr float kMaxWowDepthCents = 15.0f;
    static constexpr float kMaxFlutterDepthCents = 6.0f;
    static constexpr float kMaxHissLevel = -20.0f;  // SC-004: Maximum hiss level in dB RMS

    // Head bump frequency defaults by machine model and tape speed
    static constexpr float kStuderHeadBump_7_5 = 80.0f;
    static constexpr float kStuderHeadBump_15 = 50.0f;
    static constexpr float kStuderHeadBump_30 = 35.0f;
    static constexpr float kAmpexHeadBump_7_5 = 100.0f;
    static constexpr float kAmpexHeadBump_15 = 60.0f;
    static constexpr float kAmpexHeadBump_30 = 40.0f;

    // HF rolloff frequency defaults by tape speed
    static constexpr float kHfRolloff_7_5 = 10000.0f;
    static constexpr float kHfRolloff_15 = 15000.0f;
    static constexpr float kHfRolloff_30 = 20000.0f;

    // Wow/Flutter depth defaults by machine model
    static constexpr float kStuderWowDepth = 6.0f;
    static constexpr float kStuderFlutterDepth = 3.0f;
    static constexpr float kAmpexWowDepth = 9.0f;
    static constexpr float kAmpexFlutterDepth = 2.4f;

    // Filter Q values
    static constexpr float kButterworthQ = 0.707f;
    static constexpr float kHeadBumpQ = 1.5f;
    static constexpr float kHeadBumpMaxGain = 6.0f;  // Maximum head bump gain in dB

    // =========================================================================
    // Lifecycle (FR-002, FR-003)
    // =========================================================================

    /// @brief Default constructor with safe defaults.
    TapeMachine() noexcept
        : machineModel_(MachineModel::Studer)
        , tapeSpeed_(TapeSpeed::IPS_15)
        , tapeType_(TapeType::Type456)
        , inputLevelDb_(0.0f)
        , outputLevelDb_(0.0f)
        , bias_(0.0f)
        , saturation_(0.5f)
        , headBumpAmount_(0.5f)
        , headBumpFrequency_(kStuderHeadBump_15)
        , headBumpFrequencyManual_(false)
        , hfRolloffAmount_(0.5f)
        , hfRolloffFrequency_(kHfRolloff_15)
        , hfRolloffFrequencyManual_(false)
        , hissAmount_(0.0f)
        , wowAmount_(0.0f)
        , flutterAmount_(0.0f)
        , wowRate_(0.5f)
        , flutterRate_(6.0f)
        , wowDepthCents_(kStuderWowDepth)
        , flutterDepthCents_(kStuderFlutterDepth)
        , wowDepthManual_(false)
        , flutterDepthManual_(false)
        , driveOffset_(0.0f)
        , saturationMultiplier_(1.0f)
        , biasOffset_(0.0f)
        , sampleRate_(0.0)
        , maxBlockSize_(0)
        , prepared_(false) {
    }

    /// @brief Destructor.
    ~TapeMachine() = default;

    // Move-only (LFO and NoiseGenerator are non-copyable)
    TapeMachine(const TapeMachine&) = delete;
    TapeMachine& operator=(const TapeMachine&) = delete;
    TapeMachine(TapeMachine&&) noexcept = default;
    TapeMachine& operator=(TapeMachine&&) noexcept = default;

    /// @brief Prepare the tape machine for processing (FR-002).
    /// @param sampleRate Sample rate in Hz (44100-192000)
    /// @param maxBlockSize Maximum expected block size
    void prepare(double sampleRate, size_t maxBlockSize) noexcept {
        sampleRate_ = sampleRate;
        maxBlockSize_ = maxBlockSize;

        // Prepare TapeSaturator (core processing)
        saturator_.prepare(sampleRate, maxBlockSize);
        saturator_.setMix(1.0f);  // 100% wet for tape character

        // Prepare NoiseGenerator for hiss (FR-020)
        noiseGen_.prepare(static_cast<float>(sampleRate), maxBlockSize);
        noiseGen_.setNoiseEnabled(NoiseType::TapeHiss, true);
        noiseGen_.setNoiseLevel(NoiseType::TapeHiss, kMaxHissLevel);
        // Configure TapeHiss to have 0dB floor so it outputs at configured level
        // regardless of input signal (tape machine hiss is a constant floor)
        noiseGen_.setTapeHissParams(0.0f, 0.0f);  // 0dB floor, no sensitivity

        // Prepare LFOs for wow and flutter (FR-030: Triangle waveform)
        wowLfo_.prepare(sampleRate);
        wowLfo_.setWaveform(Waveform::Triangle);
        wowLfo_.setFrequency(wowRate_);

        flutterLfo_.prepare(sampleRate);
        flutterLfo_.setWaveform(Waveform::Triangle);
        flutterLfo_.setFrequency(flutterRate_);

        // Configure head bump filter (Biquad Peak)
        headBumpFilter_.configure(FilterType::Peak, headBumpFrequency_,
                                   1.5f, 6.0f, static_cast<float>(sampleRate));

        // Configure HF rolloff filter (Biquad Lowpass)
        hfRolloffFilter_.configure(FilterType::Lowpass, hfRolloffFrequency_,
                                    kButterworthQ, 0.0f, static_cast<float>(sampleRate));

        // Configure smoothers (5ms for click-free operation)
        const float sr = static_cast<float>(sampleRate);
        inputGainSmoother_.configure(kDefaultSmoothingMs, sr);
        outputGainSmoother_.configure(kDefaultSmoothingMs, sr);
        headBumpAmountSmoother_.configure(kDefaultSmoothingMs, sr);
        headBumpFreqSmoother_.configure(kDefaultSmoothingMs, sr);
        hfRolloffAmountSmoother_.configure(kDefaultSmoothingMs, sr);
        hfRolloffFreqSmoother_.configure(kDefaultSmoothingMs, sr);
        hissAmountSmoother_.configure(kDefaultSmoothingMs, sr);
        wowAmountSmoother_.configure(kDefaultSmoothingMs, sr);
        flutterAmountSmoother_.configure(kDefaultSmoothingMs, sr);

        // Snap smoothers to current values
        inputGainSmoother_.snapTo(dbToGain(inputLevelDb_));
        outputGainSmoother_.snapTo(dbToGain(outputLevelDb_));
        headBumpAmountSmoother_.snapTo(headBumpAmount_);
        headBumpFreqSmoother_.snapTo(headBumpFrequency_);
        hfRolloffAmountSmoother_.snapTo(hfRolloffAmount_);
        hfRolloffFreqSmoother_.snapTo(hfRolloffFrequency_);
        hissAmountSmoother_.snapTo(hissAmount_);
        wowAmountSmoother_.snapTo(wowAmount_);
        flutterAmountSmoother_.snapTo(flutterAmount_);

        // Allocate wow/flutter delay buffer (~50ms for pitch modulation)
        size_t wowFlutterBufferSize = static_cast<size_t>(sampleRate * 0.05) + 1;
        wowFlutterBuffer_.resize(wowFlutterBufferSize, 0.0f);
        wowFlutterWriteIndex_ = 0;

        // Apply tape type to saturator
        applyTapeTypeToSaturator();

        prepared_ = true;
    }

    /// @brief Clear all internal state (FR-003).
    void reset() noexcept {
        saturator_.reset();
        noiseGen_.reset();
        wowLfo_.reset();
        flutterLfo_.reset();
        headBumpFilter_.reset();
        hfRolloffFilter_.reset();

        // Snap smoothers to current values
        inputGainSmoother_.snapTo(dbToGain(inputLevelDb_));
        outputGainSmoother_.snapTo(dbToGain(outputLevelDb_));
        headBumpAmountSmoother_.snapTo(headBumpAmount_);
        headBumpFreqSmoother_.snapTo(headBumpFrequency_);
        hfRolloffAmountSmoother_.snapTo(hfRolloffAmount_);
        hfRolloffFreqSmoother_.snapTo(hfRolloffFrequency_);
        hissAmountSmoother_.snapTo(hissAmount_);
        wowAmountSmoother_.snapTo(wowAmount_);
        flutterAmountSmoother_.snapTo(flutterAmount_);

        // Clear wow/flutter buffer
        std::fill(wowFlutterBuffer_.begin(), wowFlutterBuffer_.end(), 0.0f);
        wowFlutterWriteIndex_ = 0;

        // Reset filter state tracking to force reconfiguration
        lastHeadBumpFreq_ = -1.0f;
        lastHfRolloffFreq_ = -1.0f;
    }

    // =========================================================================
    // Machine/Speed/Type Selection (FR-031, FR-004, FR-005)
    // =========================================================================

    /// @brief Set the machine model (FR-031).
    /// @param model Studer or Ampex - sets preset defaults for head bump and wow/flutter
    /// @note Call BEFORE setTapeSpeed() to ensure correct frequency defaults
    void setMachineModel(MachineModel model) noexcept {
        machineModel_ = model;

        // Update wow/flutter depth defaults if not manually overridden
        if (!wowDepthManual_) {
            wowDepthCents_ = (model == MachineModel::Studer) ? kStuderWowDepth : kAmpexWowDepth;
        }
        if (!flutterDepthManual_) {
            flutterDepthCents_ = (model == MachineModel::Studer) ? kStuderFlutterDepth : kAmpexFlutterDepth;
        }

        // Update head bump frequency default if not manually overridden
        if (!headBumpFrequencyManual_) {
            updateHeadBumpFrequencyDefault();
        }
    }

    /// @brief Set the tape speed (FR-004).
    /// @param speed 7.5, 15, or 30 ips - affects head bump and HF rolloff defaults
    void setTapeSpeed(TapeSpeed speed) noexcept {
        tapeSpeed_ = speed;

        // Update head bump frequency default if not manually overridden
        if (!headBumpFrequencyManual_) {
            updateHeadBumpFrequencyDefault();
        }

        // Update HF rolloff frequency default if not manually overridden
        if (!hfRolloffFrequencyManual_) {
            switch (speed) {
                case TapeSpeed::IPS_7_5:
                    hfRolloffFrequency_ = kHfRolloff_7_5;
                    break;
                case TapeSpeed::IPS_15:
                    hfRolloffFrequency_ = kHfRolloff_15;
                    break;
                case TapeSpeed::IPS_30:
                    hfRolloffFrequency_ = kHfRolloff_30;
                    break;
            }
            if (prepared_) {
                hfRolloffFreqSmoother_.setTarget(hfRolloffFrequency_);
            }
        }
    }

    /// @brief Set the tape type/formulation (FR-005).
    /// @param type Type456, Type900, or TypeGP9 - affects saturation character
    void setTapeType(TapeType type) noexcept {
        tapeType_ = type;

        // Calculate tape type modifiers (FR-034)
        switch (type) {
            case TapeType::Type456:
                driveOffset_ = -3.0f;
                saturationMultiplier_ = 1.2f;
                biasOffset_ = 0.1f;
                break;
            case TapeType::Type900:
                driveOffset_ = 2.0f;
                saturationMultiplier_ = 1.0f;
                biasOffset_ = 0.0f;
                break;
            case TapeType::TypeGP9:
                driveOffset_ = 4.0f;
                saturationMultiplier_ = 0.8f;
                biasOffset_ = -0.05f;
                break;
        }

        applyTapeTypeToSaturator();
    }

    // =========================================================================
    // Gain Staging (FR-006, FR-007)
    // =========================================================================

    /// @brief Set input level in dB (FR-006).
    /// @param dB Input level [-24, +24]
    void setInputLevel(float dB) noexcept {
        inputLevelDb_ = std::clamp(dB, kMinInputOutputDb, kMaxInputOutputDb);
        if (prepared_) {
            inputGainSmoother_.setTarget(dbToGain(inputLevelDb_));
        }
    }

    /// @brief Set output level in dB (FR-007).
    /// @param dB Output level [-24, +24]
    void setOutputLevel(float dB) noexcept {
        outputLevelDb_ = std::clamp(dB, kMinInputOutputDb, kMaxInputOutputDb);
        if (prepared_) {
            outputGainSmoother_.setTarget(dbToGain(outputLevelDb_));
        }
    }

    // =========================================================================
    // Saturation Control (FR-008, FR-009, FR-010)
    // =========================================================================

    /// @brief Set tape bias/asymmetry (FR-008).
    /// @param bias Bias value [-1, +1], 0 = symmetric
    void setBias(float bias) noexcept {
        bias_ = std::clamp(bias, -1.0f, 1.0f);
        applyTapeTypeToSaturator();
    }

    /// @brief Set saturation amount (FR-009).
    /// @param amount Saturation [0, 1], 0 = linear, 1 = full saturation
    void setSaturation(float amount) noexcept {
        saturation_ = std::clamp(amount, 0.0f, 1.0f);
        applyTapeTypeToSaturator();
    }

    /// @brief Set hysteresis model/solver (FR-010).
    /// @param solver Solver type for hysteresis model
    void setHysteresisModel(HysteresisSolver solver) noexcept {
        saturator_.setSolver(solver);
    }

    // =========================================================================
    // Head Bump Control (FR-011, FR-012)
    // =========================================================================

    /// @brief Set head bump amount (FR-011).
    /// @param amount Head bump intensity [0, 1], 0 = disabled, 1 = maximum boost
    void setHeadBumpAmount(float amount) noexcept {
        headBumpAmount_ = std::clamp(amount, 0.0f, 1.0f);
        if (prepared_) {
            headBumpAmountSmoother_.setTarget(headBumpAmount_);
        }
    }

    /// @brief Set head bump center frequency (FR-012).
    /// @param hz Center frequency [30, 120] Hz
    /// @note Setting this manually overrides machine model/speed defaults
    void setHeadBumpFrequency(float hz) noexcept {
        headBumpFrequency_ = std::clamp(hz, kMinHeadBumpFreq, kMaxHeadBumpFreq);
        headBumpFrequencyManual_ = true;
        if (prepared_) {
            headBumpFreqSmoother_.setTarget(headBumpFrequency_);
        }
    }

    // =========================================================================
    // HF Rolloff Control (FR-035, FR-036)
    // =========================================================================

    /// @brief Set HF rolloff amount (FR-035).
    /// @param amount Rolloff intensity [0, 1], 0 = disabled, 1 = maximum attenuation
    void setHighFreqRolloffAmount(float amount) noexcept {
        hfRolloffAmount_ = std::clamp(amount, 0.0f, 1.0f);
        if (prepared_) {
            hfRolloffAmountSmoother_.setTarget(hfRolloffAmount_);
        }
    }

    /// @brief Set HF rolloff cutoff frequency (FR-036).
    /// @param hz Cutoff frequency [5000, 22000] Hz
    /// @note Setting this manually overrides tape speed defaults
    void setHighFreqRolloffFrequency(float hz) noexcept {
        hfRolloffFrequency_ = std::clamp(hz, kMinHfRolloffFreq, kMaxHfRolloffFreq);
        hfRolloffFrequencyManual_ = true;
        if (prepared_) {
            hfRolloffFreqSmoother_.setTarget(hfRolloffFrequency_);
        }
    }

    // =========================================================================
    // Hiss Control (FR-013)
    // =========================================================================

    /// @brief Set tape hiss amount (FR-013).
    /// @param amount Hiss intensity [0, 1], 0 = disabled, 1 = maximum (SC-004: -20dB RMS max)
    void setHiss(float amount) noexcept {
        hissAmount_ = std::clamp(amount, 0.0f, 1.0f);
        if (prepared_) {
            hissAmountSmoother_.setTarget(hissAmount_);
        }
    }

    // =========================================================================
    // Wow/Flutter Control (FR-014, FR-015, FR-016, FR-037, FR-038)
    // =========================================================================

    /// @brief Set combined wow and flutter amount (FR-014).
    /// @param amount Combined modulation depth [0, 1]
    /// @note Convenience method - sets both wow and flutter equally
    void setWowFlutter(float amount) noexcept {
        setWow(amount);
        setFlutter(amount);
    }

    /// @brief Set wow amount independently (FR-015).
    /// @param amount Wow intensity [0, 1]
    void setWow(float amount) noexcept {
        wowAmount_ = std::clamp(amount, 0.0f, 1.0f);
        if (prepared_) {
            wowAmountSmoother_.setTarget(wowAmount_);
        }
    }

    /// @brief Set flutter amount independently (FR-015).
    /// @param amount Flutter intensity [0, 1]
    void setFlutter(float amount) noexcept {
        flutterAmount_ = std::clamp(amount, 0.0f, 1.0f);
        if (prepared_) {
            flutterAmountSmoother_.setTarget(flutterAmount_);
        }
    }

    /// @brief Set wow LFO rate (FR-016, FR-028).
    /// @param hz Rate [0.1, 2.0] Hz
    void setWowRate(float hz) noexcept {
        wowRate_ = std::clamp(hz, kMinWowRate, kMaxWowRate);
        wowLfo_.setFrequency(wowRate_);
    }

    /// @brief Set flutter LFO rate (FR-016, FR-029).
    /// @param hz Rate [2.0, 15.0] Hz
    void setFlutterRate(float hz) noexcept {
        flutterRate_ = std::clamp(hz, kMinFlutterRate, kMaxFlutterRate);
        flutterLfo_.setFrequency(flutterRate_);
    }

    /// @brief Set wow depth in cents (FR-037).
    /// @param cents Maximum pitch deviation [0, 15] cents
    /// @note Setting this manually overrides machine model defaults
    void setWowDepth(float cents) noexcept {
        wowDepthCents_ = std::clamp(cents, 0.0f, kMaxWowDepthCents);
        wowDepthManual_ = true;
    }

    /// @brief Set flutter depth in cents (FR-038).
    /// @param cents Maximum pitch deviation [0, 6] cents
    /// @note Setting this manually overrides machine model defaults
    void setFlutterDepth(float cents) noexcept {
        flutterDepthCents_ = std::clamp(cents, 0.0f, kMaxFlutterDepthCents);
        flutterDepthManual_ = true;
    }

    // =========================================================================
    // Processing (FR-017)
    // =========================================================================

    /// @brief Process audio buffer in-place (FR-017).
    /// @param buffer Audio buffer to process
    /// @param numSamples Number of samples in buffer
    /// @note Signal flow (FR-033): Input Gain -> Saturation -> Head Bump -> HF Rolloff -> Wow/Flutter -> Hiss -> Output Gain
    void process(float* buffer, size_t numSamples) noexcept {
        // Handle zero-sample blocks (SC-008)
        if (numSamples == 0 || !prepared_) {
            return;
        }

        for (size_t i = 0; i < numSamples; ++i) {
            // Get smoothed parameters
            const float inputGain = inputGainSmoother_.process();
            const float outputGain = outputGainSmoother_.process();
            const float headBumpAmt = headBumpAmountSmoother_.process();
            const float headBumpFreq = headBumpFreqSmoother_.process();
            const float hfRolloffAmt = hfRolloffAmountSmoother_.process();
            const float hfRolloffFreq = hfRolloffFreqSmoother_.process();
            const float hissAmt = hissAmountSmoother_.process();
            const float wowAmt = wowAmountSmoother_.process();
            const float flutterAmt = flutterAmountSmoother_.process();

            float sample = buffer[i];

            // 1. Input Gain
            sample *= inputGain;

            // Store dry sample for hiss processing
            const float preSaturation = sample;

            // 2. Saturation (via TapeSaturator) - process single sample
            float satBuffer[1] = {sample};
            saturator_.process(satBuffer, 1);
            sample = satBuffer[0];

            // 3. Head Bump (Biquad Peak filter) - SC-002: 3-6dB boost at 100%
            if (headBumpAmt > 0.0f) {
                // Update filter if frequency changed significantly
                if (std::abs(headBumpFreq - lastHeadBumpFreq_) > 0.1f) {
                    // Configure peak filter with full boost - blending will scale effect
                    headBumpFilter_.configure(FilterType::Peak, headBumpFreq,
                                              kHeadBumpQ, kHeadBumpMaxGain,
                                              static_cast<float>(sampleRate_));
                    lastHeadBumpFreq_ = headBumpFreq;
                }
                // Process through filter (adds boost at center frequency)
                float filtered = headBumpFilter_.process(sample);
                // Blend: at 0% = dry, at 100% = full filter effect
                sample = sample * (1.0f - headBumpAmt) + filtered * headBumpAmt;
            } else {
                // Still run through filter to keep state consistent, but output dry
                (void)headBumpFilter_.process(sample);
            }

            // 4. HF Rolloff (Biquad Lowpass filter) - SC-003: 6dB/octave slope
            if (hfRolloffAmt > 0.0f) {
                // Update filter if frequency changed significantly
                if (std::abs(hfRolloffFreq - lastHfRolloffFreq_) > 0.1f) {
                    hfRolloffFilter_.configure(FilterType::Lowpass, hfRolloffFreq,
                                               kButterworthQ, 0.0f,
                                               static_cast<float>(sampleRate_));
                    lastHfRolloffFreq_ = hfRolloffFreq;
                }
                // Process through lowpass
                float filtered = hfRolloffFilter_.process(sample);
                // Blend: at 0% = dry (no rolloff), at 100% = full lowpass
                sample = sample * (1.0f - hfRolloffAmt) + filtered * hfRolloffAmt;
            } else {
                // Still run through filter to keep state consistent
                (void)hfRolloffFilter_.process(sample);
            }

            // 5. Wow/Flutter (variable delay modulation) - FR-030: Triangle waveform
            if ((wowAmt > 0.0f || flutterAmt > 0.0f) && !wowFlutterBuffer_.empty()) {
                // Get LFO values (Triangle, -1 to +1)
                float wowMod = wowLfo_.process();
                float flutterMod = flutterLfo_.process();

                // Convert cents to delay samples for pitch modulation effect
                // Formula: delay_samples = depth_cents * sampleRate / (1200 * rate)
                // Simplified for subtle effect
                float wowDelaySamples = wowAmt * wowMod * wowDepthCents_ *
                                        static_cast<float>(sampleRate_) / 120000.0f;
                float flutterDelaySamples = flutterAmt * flutterMod * flutterDepthCents_ *
                                            static_cast<float>(sampleRate_) / 120000.0f;

                float totalDelay = wowDelaySamples + flutterDelaySamples;

                // Write to circular buffer
                wowFlutterBuffer_[wowFlutterWriteIndex_] = sample;

                // Calculate read position with interpolation
                float readPos = static_cast<float>(wowFlutterWriteIndex_) -
                                std::abs(totalDelay) - 1.0f;
                if (readPos < 0.0f) {
                    readPos += static_cast<float>(wowFlutterBuffer_.size());
                }

                // Linear interpolation for smooth playback
                size_t readIndex0 = static_cast<size_t>(readPos) % wowFlutterBuffer_.size();
                size_t readIndex1 = (readIndex0 + 1) % wowFlutterBuffer_.size();
                float frac = readPos - std::floor(readPos);

                sample = wowFlutterBuffer_[readIndex0] * (1.0f - frac) +
                         wowFlutterBuffer_[readIndex1] * frac;

                // Advance write index
                wowFlutterWriteIndex_ = (wowFlutterWriteIndex_ + 1) % wowFlutterBuffer_.size();
            }

            // 6. Hiss (additive tape noise) - SC-004: max -20dB RMS
            if (hissAmt > 0.0f) {
                // Generate noise sample
                float noiseBuffer[1] = {0.0f};
                float inputForNoise[1] = {preSaturation};
                noiseGen_.process(inputForNoise, noiseBuffer, 1);

                // Scale hiss by amount (maps 0-1 to silence to max level)
                float hissLevel = hissAmt * noiseBuffer[0];
                sample += hissLevel;
            }

            // 7. Output Gain
            sample *= outputGain;

            buffer[i] = sample;
        }
    }

    // =========================================================================
    // Getters
    // =========================================================================

    [[nodiscard]] MachineModel getMachineModel() const noexcept { return machineModel_; }
    [[nodiscard]] TapeSpeed getTapeSpeed() const noexcept { return tapeSpeed_; }
    [[nodiscard]] TapeType getTapeType() const noexcept { return tapeType_; }
    [[nodiscard]] float getInputLevel() const noexcept { return inputLevelDb_; }
    [[nodiscard]] float getOutputLevel() const noexcept { return outputLevelDb_; }
    [[nodiscard]] float getBias() const noexcept { return bias_; }
    [[nodiscard]] float getSaturation() const noexcept { return saturation_; }
    [[nodiscard]] float getHeadBumpAmount() const noexcept { return headBumpAmount_; }
    [[nodiscard]] float getHeadBumpFrequency() const noexcept { return headBumpFrequency_; }
    [[nodiscard]] float getHighFreqRolloffAmount() const noexcept { return hfRolloffAmount_; }
    [[nodiscard]] float getHighFreqRolloffFrequency() const noexcept { return hfRolloffFrequency_; }
    [[nodiscard]] float getHiss() const noexcept { return hissAmount_; }
    [[nodiscard]] float getWow() const noexcept { return wowAmount_; }
    [[nodiscard]] float getFlutter() const noexcept { return flutterAmount_; }
    [[nodiscard]] float getWowRate() const noexcept { return wowRate_; }
    [[nodiscard]] float getFlutterRate() const noexcept { return flutterRate_; }
    [[nodiscard]] float getWowDepth() const noexcept { return wowDepthCents_; }
    [[nodiscard]] float getFlutterDepth() const noexcept { return flutterDepthCents_; }

private:
    // =========================================================================
    // Internal Helpers
    // =========================================================================

    /// @brief Update head bump frequency based on machine model and tape speed.
    void updateHeadBumpFrequencyDefault() noexcept {
        float freq = kStuderHeadBump_15;  // Default

        if (machineModel_ == MachineModel::Studer) {
            switch (tapeSpeed_) {
                case TapeSpeed::IPS_7_5: freq = kStuderHeadBump_7_5; break;
                case TapeSpeed::IPS_15: freq = kStuderHeadBump_15; break;
                case TapeSpeed::IPS_30: freq = kStuderHeadBump_30; break;
            }
        } else {
            switch (tapeSpeed_) {
                case TapeSpeed::IPS_7_5: freq = kAmpexHeadBump_7_5; break;
                case TapeSpeed::IPS_15: freq = kAmpexHeadBump_15; break;
                case TapeSpeed::IPS_30: freq = kAmpexHeadBump_30; break;
            }
        }

        headBumpFrequency_ = freq;
        if (prepared_) {
            headBumpFreqSmoother_.setTarget(headBumpFrequency_);
        }
    }

    /// @brief Apply tape type modifiers to the saturator.
    void applyTapeTypeToSaturator() noexcept {
        // Apply drive offset and tape type modifiers
        saturator_.setDrive(inputLevelDb_ + driveOffset_);
        saturator_.setSaturation(saturation_ * saturationMultiplier_);
        saturator_.setBias(bias_ + biasOffset_);
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Machine/Speed/Type selection
    MachineModel machineModel_;
    TapeSpeed tapeSpeed_;
    TapeType tapeType_;

    // Gain staging
    float inputLevelDb_;
    float outputLevelDb_;

    // Saturation parameters
    float bias_;
    float saturation_;

    // Head bump parameters
    float headBumpAmount_;
    float headBumpFrequency_;
    bool headBumpFrequencyManual_;

    // HF rolloff parameters
    float hfRolloffAmount_;
    float hfRolloffFrequency_;
    bool hfRolloffFrequencyManual_;

    // Hiss parameter
    float hissAmount_;

    // Wow/flutter parameters
    float wowAmount_;
    float flutterAmount_;
    float wowRate_;
    float flutterRate_;
    float wowDepthCents_;
    float flutterDepthCents_;
    bool wowDepthManual_;
    bool flutterDepthManual_;

    // Tape type modifiers
    float driveOffset_;
    float saturationMultiplier_;
    float biasOffset_;

    // Runtime state
    double sampleRate_;
    size_t maxBlockSize_;
    bool prepared_;

    // Filter state tracking (for coefficient update optimization)
    float lastHeadBumpFreq_ = -1.0f;
    float lastHfRolloffFreq_ = -1.0f;

    // Components
    TapeSaturator saturator_;
    NoiseGenerator noiseGen_;
    LFO wowLfo_;
    LFO flutterLfo_;
    Biquad headBumpFilter_;
    Biquad hfRolloffFilter_;

    // Smoothers (9 total as per spec)
    OnePoleSmoother inputGainSmoother_;
    OnePoleSmoother outputGainSmoother_;
    OnePoleSmoother headBumpAmountSmoother_;
    OnePoleSmoother headBumpFreqSmoother_;
    OnePoleSmoother hfRolloffAmountSmoother_;
    OnePoleSmoother hfRolloffFreqSmoother_;
    OnePoleSmoother hissAmountSmoother_;
    OnePoleSmoother wowAmountSmoother_;
    OnePoleSmoother flutterAmountSmoother_;

    // Wow/flutter delay buffer
    std::vector<float> wowFlutterBuffer_;
    size_t wowFlutterWriteIndex_ = 0;
};

} // namespace DSP
} // namespace Krate
