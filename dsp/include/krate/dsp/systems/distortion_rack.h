// ==============================================================================
// Layer 3: System Component - DistortionRack
// ==============================================================================
// Multi-stage distortion chain with 4 configurable slots, per-slot enable/mix/gain
// controls with 5ms smoothing, per-slot DC blocking, and global oversampling.
//
// Feature: 068-distortion-rack
// Layer: 3 (Systems)
// Dependencies:
//   - Layer 0: core/db_utils.h (dbToGain)
//   - Layer 1: primitives/waveshaper.h, primitives/dc_blocker.h,
//              primitives/oversampler.h, primitives/smoother.h
//   - Layer 2: processors/tube_stage.h, processors/diode_clipper.h,
//              processors/wavefolder_processor.h, processors/tape_saturator.h,
//              processors/fuzz_processor.h, processors/bitcrusher_processor.h
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, std::variant, compile-time dispatch)
// - Principle IX: Layer 3 (depends on Layer 0, 1, 2)
// - Principle X: DSP Constraints (DC blocking, oversampling for aliasing)
// - Principle XII: Test-First Development
//
// Reference: specs/068-distortion-rack/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/oversampler.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/primitives/waveshaper.h>
#include <krate/dsp/processors/bitcrusher_processor.h>
#include <krate/dsp/processors/diode_clipper.h>
#include <krate/dsp/processors/fuzz_processor.h>
#include <krate/dsp/processors/tape_saturator.h>
#include <krate/dsp/processors/tube_stage.h>
#include <krate/dsp/processors/wavefolder_processor.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>

namespace Krate {
namespace DSP {

// =============================================================================
// Forward Declarations
// =============================================================================

class DistortionRack;

// =============================================================================
// Internal Visitors for std::variant dispatch
// =============================================================================

namespace detail {

/// @brief Visitor for resetting processors through std::variant.
struct ResetProcessorVisitor {
    void operator()(std::monostate&) const noexcept {}
    void operator()(Waveshaper&) const noexcept {} // Stateless

    template<typename T>
    void operator()(T& proc) const noexcept {
        proc.reset();
    }
};

/// @brief Visitor for preparing processors through std::variant.
struct PrepareProcessorVisitor {
    double sampleRate;
    size_t maxBlockSize;

    void operator()(std::monostate&) const noexcept {}
    void operator()(Waveshaper&) const noexcept {} // Stateless, no prepare needed

    template<typename T>
    void operator()(T& proc) const noexcept {
        proc.prepare(sampleRate, maxBlockSize);
    }
};

} // namespace detail

// =============================================================================
// SlotType Enumeration (FR-002)
// =============================================================================

/// @brief Available processor types for DistortionRack slots.
///
/// Each slot can be configured with one of these processor types:
/// - Empty: Pass-through (no processing)
/// - Waveshaper: Layer 1 generic waveshaping primitive
/// - TubeStage through Bitcrusher: Layer 2 distortion processors
enum class SlotType : uint8_t {
    Empty = 0,       ///< No processor (bypass)
    Waveshaper,      ///< Layer 1: Generic waveshaping
    TubeStage,       ///< Layer 2: Tube saturation
    DiodeClipper,    ///< Layer 2: Diode clipping
    Wavefolder,      ///< Layer 2: Wavefolding
    TapeSaturator,   ///< Layer 2: Tape saturation
    Fuzz,            ///< Layer 2: Fuzz distortion
    Bitcrusher       ///< Layer 2: Bit crushing
};

// =============================================================================
// ProcessorVariant Type Alias (FR-019a)
// =============================================================================

/// @brief Type-erased processor storage using std::variant.
///
/// Enables compile-time polymorphism without virtual dispatch overhead.
/// std::monostate represents the Empty slot type.
using ProcessorVariant = std::variant<
    std::monostate,
    Waveshaper,
    TubeStage,
    DiodeClipper,
    WavefolderProcessor,
    TapeSaturator,
    FuzzProcessor,
    BitcrusherProcessor
>;

// =============================================================================
// DistortionRack Class
// =============================================================================

/// @brief Multi-stage distortion rack with 4 configurable slots.
///
/// Provides a chainable 4-slot distortion processor rack. Each slot can be
/// configured with different distortion types, enable/bypass, dry/wet mix,
/// and per-slot gain. Global oversampling (1x/2x/4x) is applied once around
/// the entire chain for efficiency.
///
/// @par Signal Chain
/// Input -> [Oversample Up] -> Slot 0 (process -> mix -> gain -> DC block) ->
/// Slot 1 -> Slot 2 -> Slot 3 -> [Oversample Down] -> Output
///
/// @par Features
/// - 4 configurable slots with 8 processor types
/// - Per-slot enable with 5ms smoothing (click-free toggling)
/// - Per-slot dry/wet mix with 5ms smoothing
/// - Per-slot gain [-24, +24] dB with 5ms smoothing
/// - Per-slot DC blocking (10 Hz cutoff, active only when slot enabled)
/// - Global oversampling (1x/2x/4x) with zero-latency mode
/// - Type-safe processor access via getProcessor<T>()
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle III: Modern C++ (C++20, std::variant, compile-time dispatch)
/// - Principle IX: Layer 3 (depends on Layer 0, 1, 2)
/// - Principle X: DSP Constraints (DC blocking, oversampling)
///
/// @par Usage Example
/// @code
/// DistortionRack rack;
/// rack.prepare(44100.0, 512);
///
/// // Configure slots
/// rack.setSlotType(0, SlotType::TubeStage);
/// rack.setSlotType(1, SlotType::Wavefolder);
/// rack.setSlotEnabled(0, true);
/// rack.setSlotEnabled(1, true);
/// rack.setSlotMix(0, 0.75f);
/// rack.setOversamplingFactor(4);
///
/// // Process stereo audio
/// rack.process(leftBuffer, rightBuffer, numSamples);
///
/// // Access processor for fine control
/// if (auto* tube = rack.getProcessor<TubeStage>(0)) {
///     tube->setBias(0.3f);
/// }
/// @endcode
///
/// @see specs/068-distortion-rack/spec.md
class DistortionRack {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// Number of slots in the rack (FR-001)
    static constexpr size_t kNumSlots = 4;

    /// Default smoothing time in milliseconds (FR-009, FR-015, FR-046)
    static constexpr float kDefaultSmoothingMs = 5.0f;

    /// DC blocker cutoff frequency in Hz (FR-049)
    static constexpr float kDCBlockerCutoffHz = 10.0f;

    /// Minimum slot gain in dB (FR-044)
    static constexpr float kMinGainDb = -24.0f;

    /// Maximum slot gain in dB (FR-044)
    static constexpr float kMaxGainDb = +24.0f;

    // =========================================================================
    // Lifecycle (FR-033 to FR-037)
    // =========================================================================

    /// @brief Default constructor.
    ///
    /// Initializes all slots to Empty with default parameters.
    DistortionRack() noexcept = default;

    /// @brief Configure the rack for the given sample rate and block size.
    ///
    /// Prepares all internal components (oversamplers, DC blockers, smoothers,
    /// slot processors) for processing. Must be called before process().
    ///
    /// @param sampleRate Sample rate in Hz (e.g., 44100.0)
    /// @param maxBlockSize Maximum block size in samples
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;

    /// @brief Clear all internal state without reallocation.
    ///
    /// Resets all slot processors, DC blockers, and oversamplers. Snaps all
    /// smoothers to their current targets. Call when starting a new audio
    /// stream or after a discontinuity.
    void reset() noexcept;

    /// @brief Process stereo audio through the rack (FR-038).
    ///
    /// Applies the configured distortion chain to stereo audio in-place.
    /// When all slots are disabled or empty, output equals input.
    ///
    /// @param left Left channel buffer (input/output)
    /// @param right Right channel buffer (input/output)
    /// @param numSamples Number of samples per channel
    void process(float* left, float* right, size_t numSamples) noexcept;

    // =========================================================================
    // Slot Type Configuration (FR-002 to FR-005)
    // =========================================================================

    /// @brief Set the processor type for a slot (FR-003).
    ///
    /// Changes the processor type for the specified slot. The new processor
    /// is allocated and prepared immediately. Out-of-range slot indices are
    /// ignored (FR-004).
    ///
    /// @param slot Slot index [0, 3]
    /// @param type Processor type to use
    void setSlotType(size_t slot, SlotType type) noexcept;

    /// @brief Get the processor type for a slot.
    ///
    /// @param slot Slot index [0, 3]
    /// @return Slot type (SlotType::Empty for out-of-range indices)
    [[nodiscard]] SlotType getSlotType(size_t slot) const noexcept;

    // =========================================================================
    // Slot Enable/Bypass (FR-006 to FR-009)
    // =========================================================================

    /// @brief Enable or disable a slot (FR-006).
    ///
    /// Disabled slots pass audio through unchanged. Enable/disable transitions
    /// are smoothed over 5ms to prevent clicks (FR-009).
    ///
    /// @param slot Slot index [0, 3]
    /// @param enabled True to enable, false to disable
    void setSlotEnabled(size_t slot, bool enabled) noexcept;

    /// @brief Check if a slot is enabled.
    ///
    /// @param slot Slot index [0, 3]
    /// @return True if enabled (false for out-of-range indices)
    [[nodiscard]] bool getSlotEnabled(size_t slot) const noexcept;

    // =========================================================================
    // Slot Mix (FR-010 to FR-015)
    // =========================================================================

    /// @brief Set the dry/wet mix for a slot (FR-010).
    ///
    /// Controls the blend between dry input and processed output.
    /// - 0.0 = dry only (FR-012)
    /// - 1.0 = wet only (FR-013)
    /// Mix changes are smoothed over 5ms (FR-015).
    ///
    /// @param slot Slot index [0, 3]
    /// @param mix Mix amount [0.0, 1.0], clamped (FR-011)
    void setSlotMix(size_t slot, float mix) noexcept;

    /// @brief Get the mix amount for a slot.
    ///
    /// @param slot Slot index [0, 3]
    /// @return Mix amount [0.0, 1.0] (1.0 for out-of-range indices)
    [[nodiscard]] float getSlotMix(size_t slot) const noexcept;

    // =========================================================================
    // Slot Gain (FR-043 to FR-047)
    // =========================================================================

    /// @brief Set the gain for a slot in dB (FR-043).
    ///
    /// Applied after the slot's processor output and before the next slot.
    /// Gain changes are smoothed over 5ms (FR-046).
    ///
    /// @param slot Slot index [0, 3]
    /// @param dB Gain in dB [-24, +24], clamped (FR-044)
    void setSlotGain(size_t slot, float dB) noexcept;

    /// @brief Get the gain for a slot in dB.
    ///
    /// @param slot Slot index [0, 3]
    /// @return Gain in dB (0.0 for out-of-range indices)
    [[nodiscard]] float getSlotGain(size_t slot) const noexcept;

    // =========================================================================
    // Processor Access (FR-016 to FR-019a)
    // =========================================================================

    /// @brief Get a typed pointer to a slot's processor (FR-016).
    ///
    /// Returns nullptr if:
    /// - Slot index is out of range
    /// - Slot type does not match requested type T
    /// - Slot type is Empty
    ///
    /// @tparam T Processor type to retrieve
    /// @param slot Slot index [0, 3]
    /// @param channel Channel index [0, 1] for stereo (default: 0 = left)
    /// @return Pointer to processor, or nullptr if type mismatch/empty/out-of-range
    template<typename T>
    [[nodiscard]] T* getProcessor(size_t slot, size_t channel = 0) noexcept;

    /// @brief Get a const typed pointer to a slot's processor.
    ///
    /// @tparam T Processor type to retrieve
    /// @param slot Slot index [0, 3]
    /// @param channel Channel index [0, 1] for stereo (default: 0 = left)
    /// @return Const pointer to processor, or nullptr
    template<typename T>
    [[nodiscard]] const T* getProcessor(size_t slot, size_t channel = 0) const noexcept;

    // =========================================================================
    // Global Oversampling (FR-020 to FR-027)
    // =========================================================================

    /// @brief Set the global oversampling factor (FR-021).
    ///
    /// Oversampling is applied once around the entire chain for efficiency.
    /// Valid factors: 1 (off), 2, 4. Invalid values are ignored (FR-025).
    ///
    /// @param factor Oversampling factor (1, 2, or 4)
    void setOversamplingFactor(int factor) noexcept;

    /// @brief Get the current oversampling factor.
    ///
    /// @return Oversampling factor (1, 2, or 4)
    [[nodiscard]] int getOversamplingFactor() const noexcept;

    /// @brief Get the latency introduced by oversampling.
    ///
    /// @return Latency in samples (0 for factor=1 with zero-latency mode)
    [[nodiscard]] size_t getLatency() const noexcept;

    // =========================================================================
    // Global Output Gain (FR-028 to FR-032)
    // =========================================================================

    /// @brief Set the global output gain in dB (FR-028).
    ///
    /// Applied after the entire processing chain (FR-029).
    /// Gain changes are smoothed over 5ms (FR-032).
    ///
    /// @param dB Gain in dB [-24, +24], clamped (FR-030)
    void setOutputGain(float dB) noexcept;

    /// @brief Get the global output gain in dB.
    ///
    /// @return Gain in dB (FR-031: default 0.0)
    [[nodiscard]] float getOutputGain() const noexcept;

    // =========================================================================
    // DC Blocking (FR-048 to FR-052)
    // =========================================================================

    /// @brief Enable or disable global DC blocking (FR-051).
    ///
    /// When enabled, DC blockers are active after each enabled slot.
    /// When disabled, no DC blocking is applied.
    ///
    /// @param enabled True to enable, false to disable
    void setDCBlockingEnabled(bool enabled) noexcept;

    /// @brief Check if DC blocking is enabled.
    ///
    /// @return True if DC blocking is enabled (FR-052: default true)
    [[nodiscard]] bool getDCBlockingEnabled() const noexcept;

private:
    // =========================================================================
    // Internal Slot Structure
    // =========================================================================

    struct Slot {
        // Processors (stereo = 2 mono instances)
        ProcessorVariant processorL;
        ProcessorVariant processorR;

        // Per-slot DC blocking
        DCBlocker dcBlockerL;
        DCBlocker dcBlockerR;

        // Parameter smoothers (5ms smoothing time)
        OnePoleSmoother enableSmoother;  // 0.0 = disabled, 1.0 = enabled
        OnePoleSmoother mixSmoother;     // 0.0 = dry, 1.0 = wet
        OnePoleSmoother gainSmoother;    // linear gain (from dB)

        // Current parameter values (targets for smoothers)
        bool enabled = false;
        float mix = 1.0f;        // [0.0, 1.0]
        float gainDb = 0.0f;     // [-24.0, +24.0] dB
        SlotType type = SlotType::Empty;
    };

    // =========================================================================
    // Internal Processing Methods
    // =========================================================================

    /// @brief Process the chain without oversampling.
    void processChain(float* left, float* right, size_t numSamples) noexcept;

    /// @brief Process a single slot.
    void processSlot(size_t slotIndex, float* left, float* right, size_t numSamples) noexcept;

    /// @brief Prepare a slot's processor after type change.
    void prepareSlotProcessor(Slot& slot) noexcept;

    /// @brief Create a processor variant for a slot type.
    [[nodiscard]] ProcessorVariant createProcessor(SlotType type) const noexcept;

    // =========================================================================
    // ProcessVisitor for std::variant dispatch (FR-019a)
    // =========================================================================

    /// @brief Visitor for processing through std::variant.
    struct ProcessVisitor {
        float* buffer;
        size_t numSamples;

        /// @brief No-op for empty slots (std::monostate).
        void operator()(std::monostate&) const noexcept {
            // Pass-through: buffer unchanged
        }

        /// @brief Process through Waveshaper (Layer 1 - stateless).
        void operator()(Waveshaper& proc) const noexcept {
            proc.processBlock(buffer, numSamples);
        }

        /// @brief Process through any Layer 2 processor.
        template<typename T>
        void operator()(T& proc) const noexcept {
            proc.process(buffer, numSamples);
        }
    };

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Slots
    std::array<Slot, kNumSlots> slots_;

    // Oversamplers (both instantiated; one used based on factor)
    Oversampler<2, 2> oversampler2x_;
    Oversampler<4, 2> oversampler4x_;
    int oversamplingFactor_ = 1;

    // DC blocking global flag
    bool dcBlockingEnabled_ = true;

    // Global output gain
    float outputGainDb_ = 0.0f;
    OnePoleSmoother outputGainSmoother_;

    // Cached configuration
    double sampleRate_ = 44100.0;
    size_t maxBlockSize_ = 512;
    bool prepared_ = false;
};

// =============================================================================
// Template Method Implementations
// =============================================================================

template<typename T>
T* DistortionRack::getProcessor(size_t slot, size_t channel) noexcept {
    if (slot >= kNumSlots || channel > 1) {
        return nullptr;
    }

    ProcessorVariant& variant = (channel == 0) ? slots_[slot].processorL : slots_[slot].processorR;
    return std::get_if<T>(&variant);
}

template<typename T>
const T* DistortionRack::getProcessor(size_t slot, size_t channel) const noexcept {
    if (slot >= kNumSlots || channel > 1) {
        return nullptr;
    }

    const ProcessorVariant& variant = (channel == 0) ? slots_[slot].processorL : slots_[slot].processorR;
    return std::get_if<T>(&variant);
}

// =============================================================================
// Inline Method Implementations
// =============================================================================

inline void DistortionRack::prepare(double sampleRate, size_t maxBlockSize) noexcept {
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;

    const float sr = static_cast<float>(sampleRate);

    // Configure smoothers for all slots
    for (auto& slot : slots_) {
        slot.enableSmoother.configure(kDefaultSmoothingMs, sr);
        slot.mixSmoother.configure(kDefaultSmoothingMs, sr);
        slot.gainSmoother.configure(kDefaultSmoothingMs, sr);

        // Set initial targets
        slot.enableSmoother.setTarget(slot.enabled ? 1.0f : 0.0f);
        slot.mixSmoother.setTarget(slot.mix);
        slot.gainSmoother.setTarget(dbToGain(slot.gainDb));

        // Snap to initial values
        slot.enableSmoother.snapToTarget();
        slot.mixSmoother.snapToTarget();
        slot.gainSmoother.snapToTarget();

        // Configure DC blockers
        slot.dcBlockerL.prepare(sampleRate, kDCBlockerCutoffHz);
        slot.dcBlockerR.prepare(sampleRate, kDCBlockerCutoffHz);

        // Prepare slot processors
        prepareSlotProcessor(slot);
    }

    // Prepare oversamplers (use zero-latency mode by default)
    oversampler2x_.prepare(sampleRate, maxBlockSize, OversamplingQuality::Economy, OversamplingMode::ZeroLatency);
    oversampler4x_.prepare(sampleRate, maxBlockSize, OversamplingQuality::Economy, OversamplingMode::ZeroLatency);

    // Configure global output gain smoother (FR-032: 5ms smoothing)
    outputGainSmoother_.configure(kDefaultSmoothingMs, sr);
    outputGainSmoother_.setTarget(dbToGain(outputGainDb_));
    outputGainSmoother_.snapToTarget();

    prepared_ = true;
}

inline void DistortionRack::reset() noexcept {
    for (auto& slot : slots_) {
        // Snap smoothers to current targets
        slot.enableSmoother.snapToTarget();
        slot.mixSmoother.snapToTarget();
        slot.gainSmoother.snapToTarget();

        // Reset DC blockers
        slot.dcBlockerL.reset();
        slot.dcBlockerR.reset();

        // Reset processors using visitor pattern
        std::visit(detail::ResetProcessorVisitor{}, slot.processorL);
        std::visit(detail::ResetProcessorVisitor{}, slot.processorR);
    }

    // Reset oversamplers
    oversampler2x_.reset();
    oversampler4x_.reset();

    // Snap output gain smoother
    outputGainSmoother_.snapToTarget();
}

inline void DistortionRack::process(float* left, float* right, size_t numSamples) noexcept {
    // FR-040: n=0 returns immediately
    if (numSamples == 0 || left == nullptr || right == nullptr) {
        return;
    }

    // FR-037: Before prepare(), return input unchanged
    if (!prepared_) {
        return;
    }

    // Dispatch based on oversampling factor
    if (oversamplingFactor_ == 1) {
        // No oversampling - process directly
        processChain(left, right, numSamples);
    } else if (oversamplingFactor_ == 2) {
        // 2x oversampling
        oversampler2x_.process(left, right, numSamples,
            [this](float* osLeft, float* osRight, size_t osNumSamples) {
                processChain(osLeft, osRight, osNumSamples);
            });
    } else { // factor == 4
        // 4x oversampling
        oversampler4x_.process(left, right, numSamples,
            [this](float* osLeft, float* osRight, size_t osNumSamples) {
                processChain(osLeft, osRight, osNumSamples);
            });
    }

    // FR-029: Apply global output gain after entire processing chain
    for (size_t i = 0; i < numSamples; ++i) {
        const float gain = outputGainSmoother_.process();
        left[i] *= gain;
        right[i] *= gain;
    }
}

inline void DistortionRack::processChain(float* left, float* right, size_t numSamples) noexcept {
    // FR-039: Process slots in order 0 -> 1 -> 2 -> 3
    for (size_t i = 0; i < kNumSlots; ++i) {
        processSlot(i, left, right, numSamples);
    }
}

inline void DistortionRack::processSlot(size_t slotIndex, float* left, float* right, size_t numSamples) noexcept {
    Slot& slot = slots_[slotIndex];

    // Process sample-by-sample for parameter smoothing
    for (size_t i = 0; i < numSamples; ++i) {
        // Advance smoothers
        const float enableAmt = slot.enableSmoother.process();
        const float mixAmt = slot.mixSmoother.process();
        const float gain = slot.gainSmoother.process();

        // FR-007: Disabled slots pass through (enableAmt near 0)
        if (enableAmt < 0.0001f) {
            continue; // Skip processing, keep dry signal
        }

        // Store dry samples
        const float dryL = left[i];
        const float dryR = right[i];

        // Process through slot processor
        float wetL = dryL;
        float wetR = dryR;

        // Use visitor to dispatch to the correct processor
        ProcessVisitor visitorL{&wetL, 1};
        ProcessVisitor visitorR{&wetR, 1};
        std::visit(visitorL, slot.processorL);
        std::visit(visitorR, slot.processorR);

        // FR-048, FR-050: Apply DC blocking only when slot is enabled
        if (dcBlockingEnabled_) {
            wetL = slot.dcBlockerL.process(wetL);
            wetR = slot.dcBlockerR.process(wetR);
        }

        // Apply dry/wet mix
        // FR-012: mix=0 produces dry only
        // FR-013: mix=1 produces wet only
        wetL = dryL * (1.0f - mixAmt) + wetL * mixAmt;
        wetR = dryR * (1.0f - mixAmt) + wetR * mixAmt;

        // FR-047: Apply gain after slot processing
        wetL *= gain;
        wetR *= gain;

        // Apply enable smoothing (crossfade between dry and processed)
        left[i] = dryL * (1.0f - enableAmt) + wetL * enableAmt;
        right[i] = dryR * (1.0f - enableAmt) + wetR * enableAmt;
    }
}

inline void DistortionRack::setSlotType(size_t slot, SlotType type) noexcept {
    // FR-004: Out-of-range slot indices are ignored
    if (slot >= kNumSlots) {
        return;
    }

    slots_[slot].type = type;

    // FR-003a: Immediately allocate and construct the new processor
    slots_[slot].processorL = createProcessor(type);
    slots_[slot].processorR = createProcessor(type);

    // Prepare the new processors if we're already prepared
    if (prepared_) {
        prepareSlotProcessor(slots_[slot]);
    }
}

inline SlotType DistortionRack::getSlotType(size_t slot) const noexcept {
    if (slot >= kNumSlots) {
        return SlotType::Empty;
    }
    return slots_[slot].type;
}

inline void DistortionRack::setSlotEnabled(size_t slot, bool enabled) noexcept {
    if (slot >= kNumSlots) {
        return;
    }
    slots_[slot].enabled = enabled;
    slots_[slot].enableSmoother.setTarget(enabled ? 1.0f : 0.0f);
}

inline bool DistortionRack::getSlotEnabled(size_t slot) const noexcept {
    if (slot >= kNumSlots) {
        return false;
    }
    return slots_[slot].enabled;
}

inline void DistortionRack::setSlotMix(size_t slot, float mix) noexcept {
    if (slot >= kNumSlots) {
        return;
    }
    // FR-011: Clamp mix to [0.0, 1.0]
    slots_[slot].mix = std::clamp(mix, 0.0f, 1.0f);
    slots_[slot].mixSmoother.setTarget(slots_[slot].mix);
}

inline float DistortionRack::getSlotMix(size_t slot) const noexcept {
    if (slot >= kNumSlots) {
        return 1.0f; // Default
    }
    return slots_[slot].mix;
}

inline void DistortionRack::setSlotGain(size_t slot, float dB) noexcept {
    if (slot >= kNumSlots) {
        return;
    }
    // FR-044: Clamp gain to [-24, +24] dB
    slots_[slot].gainDb = std::clamp(dB, kMinGainDb, kMaxGainDb);
    slots_[slot].gainSmoother.setTarget(dbToGain(slots_[slot].gainDb));
}

inline float DistortionRack::getSlotGain(size_t slot) const noexcept {
    if (slot >= kNumSlots) {
        return 0.0f; // Default
    }
    return slots_[slot].gainDb;
}

inline void DistortionRack::setOversamplingFactor(int factor) noexcept {
    // FR-025: Only valid factors 1, 2, 4 are accepted
    if (factor == 1 || factor == 2 || factor == 4) {
        oversamplingFactor_ = factor;
    }
    // Invalid factors are ignored (no change)
}

inline int DistortionRack::getOversamplingFactor() const noexcept {
    return oversamplingFactor_;
}

inline size_t DistortionRack::getLatency() const noexcept {
    if (oversamplingFactor_ == 1) {
        return 0;
    } else if (oversamplingFactor_ == 2) {
        return oversampler2x_.getLatency();
    } else {
        return oversampler4x_.getLatency();
    }
}

inline void DistortionRack::setOutputGain(float dB) noexcept {
    // FR-030: Clamp to [-24, +24] dB range
    outputGainDb_ = std::clamp(dB, kMinGainDb, kMaxGainDb);
    outputGainSmoother_.setTarget(dbToGain(outputGainDb_));
}

inline float DistortionRack::getOutputGain() const noexcept {
    return outputGainDb_;
}

inline void DistortionRack::setDCBlockingEnabled(bool enabled) noexcept {
    dcBlockingEnabled_ = enabled;
}

inline bool DistortionRack::getDCBlockingEnabled() const noexcept {
    return dcBlockingEnabled_;
}

inline ProcessorVariant DistortionRack::createProcessor(SlotType type) const noexcept {
    switch (type) {
        case SlotType::Empty:
            return std::monostate{};
        case SlotType::Waveshaper:
            return Waveshaper{};
        case SlotType::TubeStage:
            return TubeStage{};
        case SlotType::DiodeClipper:
            return DiodeClipper{};
        case SlotType::Wavefolder:
            return WavefolderProcessor{};
        case SlotType::TapeSaturator:
            return TapeSaturator{};
        case SlotType::Fuzz:
            return FuzzProcessor{};
        case SlotType::Bitcrusher:
            return BitcrusherProcessor{};
        default:
            return std::monostate{};
    }
}

inline void DistortionRack::prepareSlotProcessor(Slot& slot) noexcept {
    // Prepare the processor using visitor pattern
    detail::PrepareProcessorVisitor visitor{sampleRate_, maxBlockSize_};
    std::visit(visitor, slot.processorL);
    std::visit(visitor, slot.processorR);
}

} // namespace DSP
} // namespace Krate
