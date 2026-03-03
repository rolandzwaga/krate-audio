// ==============================================================================
// API Contract: Multi-Stage Envelope Generator
// ==============================================================================
// Layer 2 (Processor) - depends on Layer 0 (core) and Layer 1 (primitives)
// Namespace: Krate::DSP
// Header: dsp/include/krate/dsp/processors/multi_stage_envelope.h
//
// This file documents the public API surface. Implementation details omitted.
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/envelope_utils.h>  // EnvCurve, RetriggerMode, etc.
#include <array>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// EnvStageConfig (FR-002)
// =============================================================================

struct EnvStageConfig {
    float targetLevel = 0.0f;            // [0.0, 1.0] - target output level
    float timeMs = 100.0f;               // [0.0, 10000.0] - transition time in ms
    EnvCurve curve = EnvCurve::Exponential;  // Curve shape (FR-020 default)
};

// =============================================================================
// MultiStageEnvState (FR-004)
// =============================================================================

enum class MultiStageEnvState : uint8_t {
    Idle = 0,
    Running,
    Sustaining,
    Releasing
};

// =============================================================================
// MultiStageEnvelope (FR-001 through FR-037)
// =============================================================================

class MultiStageEnvelope {
public:
    static constexpr int kMinStages = 4;            // FR-001
    static constexpr int kMaxStages = 8;            // FR-001
    static constexpr float kMaxStageTimeMs = 10000.0f;  // FR-002

    MultiStageEnvelope() noexcept = default;

    // =========================================================================
    // Lifecycle (FR-010)
    // =========================================================================

    void prepare(float sampleRate) noexcept;
    void reset() noexcept;

    // =========================================================================
    // Gate Control (FR-005)
    // =========================================================================

    void gate(bool on) noexcept;

    // =========================================================================
    // Stage Configuration (FR-001, FR-002, FR-016, FR-020)
    // =========================================================================

    void setNumStages(int count) noexcept;              // [kMinStages, kMaxStages]
    void setStageLevel(int stage, float level) noexcept; // [0.0, 1.0]
    void setStageTime(int stage, float ms) noexcept;    // [0.0, kMaxStageTimeMs]
    void setStageCurve(int stage, EnvCurve curve) noexcept;

    // Convenience: set all stage parameters at once
    void setStage(int stage, float level, float ms, EnvCurve curve) noexcept;

    // =========================================================================
    // Sustain Point (FR-012, FR-015)
    // =========================================================================

    void setSustainPoint(int stage) noexcept;  // [0, numStages-1]

    // =========================================================================
    // Loop Control (FR-022, FR-023, FR-025)
    // =========================================================================

    void setLoopEnabled(bool enabled) noexcept;
    void setLoopStart(int stage) noexcept;     // [0, numStages-1]
    void setLoopEnd(int stage) noexcept;       // [loopStart, numStages-1]

    // =========================================================================
    // Release (FR-006)
    // =========================================================================

    void setReleaseTime(float ms) noexcept;    // [0.0, kMaxStageTimeMs]

    // =========================================================================
    // Retrigger Mode (FR-028, FR-029)
    // =========================================================================

    void setRetriggerMode(RetriggerMode mode) noexcept;

    // =========================================================================
    // Processing (FR-008, FR-033, FR-034)
    // =========================================================================

    [[nodiscard]] float process() noexcept;
    void processBlock(float* output, size_t numSamples) noexcept;

    // =========================================================================
    // State Queries (FR-004, FR-009)
    // =========================================================================

    [[nodiscard]] MultiStageEnvState getState() const noexcept;
    [[nodiscard]] bool isActive() const noexcept;      // state != Idle
    [[nodiscard]] bool isReleasing() const noexcept;   // state == Releasing
    [[nodiscard]] float getOutput() const noexcept;    // current output level
    [[nodiscard]] int getCurrentStage() const noexcept;

    // =========================================================================
    // Configuration Queries
    // =========================================================================

    [[nodiscard]] int getNumStages() const noexcept;
    [[nodiscard]] int getSustainPoint() const noexcept;
    [[nodiscard]] bool getLoopEnabled() const noexcept;
    [[nodiscard]] int getLoopStart() const noexcept;
    [[nodiscard]] int getLoopEnd() const noexcept;
};

} // namespace DSP
} // namespace Krate
