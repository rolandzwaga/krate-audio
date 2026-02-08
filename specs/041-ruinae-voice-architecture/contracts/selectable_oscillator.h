// ==============================================================================
// API Contract: SelectableOscillator
// ==============================================================================
// Layer 3: System Component
// Location: dsp/include/krate/dsp/systems/selectable_oscillator.h
//
// This contract defines the public interface. Implementation details are
// in the plan and data model.
// ==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace Krate::DSP {

// Forward declarations for oscillator types (from spec)
enum class OscType : uint8_t;
enum class PhaseMode : uint8_t;

class SelectableOscillator {
public:
    SelectableOscillator() noexcept = default;
    ~SelectableOscillator() noexcept = default;

    // Non-copyable, movable
    SelectableOscillator(const SelectableOscillator&) = delete;
    SelectableOscillator& operator=(const SelectableOscillator&) = delete;
    SelectableOscillator(SelectableOscillator&&) noexcept = default;
    SelectableOscillator& operator=(SelectableOscillator&&) noexcept = default;

    // Lifecycle
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;

    // Type selection (FR-002, FR-003, FR-004)
    void setType(OscType type) noexcept;
    [[nodiscard]] OscType getActiveType() const noexcept;

    // Phase mode (FR-005)
    void setPhaseMode(PhaseMode mode) noexcept;

    // Frequency control
    void setFrequency(float hz) noexcept;

    // Processing (FR-004)
    void processBlock(float* output, size_t numSamples) noexcept;
};

} // namespace Krate::DSP
