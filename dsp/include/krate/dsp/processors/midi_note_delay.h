// ==============================================================================
// MidiNoteDelay - MIDI echo post-processor for arpeggiator (Layer 2 Processor)
// ==============================================================================
// Spec: Gradus MIDI Delay Lane
//
// Post-processes ArpEvent output from ArpeggiatorCore, generating delayed
// echo copies of NoteOn events with per-step configurable delay time,
// feedback count, velocity decay, pitch shift, and gate scaling.
//
// Constitution Compliance:
// - Principle II:  Real-Time Safety (noexcept, zero allocation, fixed buffers)
// - Principle III: Modern C++ (C++20, std::array, std::span)
// - Principle IX:  Layer 2 (depends on Layer 0 core utilities + Layer 1 ArpEvent)
// ==============================================================================

#pragma once

#include <krate/dsp/core/block_context.h>
#include <krate/dsp/core/note_value.h>
#include <krate/dsp/processors/arpeggiator_core.h> // ArpEvent

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Krate::DSP {

// =============================================================================
// MidiDelayStepConfig
// =============================================================================

/// @brief Per-step delay configuration for MIDI echo generation.
struct MidiDelayStepConfig {
    bool active{false};            ///< Per-step enable (false = no echoes, skip entirely)
    TimeMode timeMode{TimeMode::Synced};
    float delayTimeMs{250.0f};     ///< Used when timeMode == Free (10-2000ms)
    int noteValueIndex{10};        ///< Used when timeMode == Synced (0-29, default 1/8)
    int feedbackCount{3};          ///< Number of echo repeats (0 = off, max 16)
    float velocityDecay{0.5f};     ///< Velocity multiplier decay per echo (0-1)
    int pitchShiftPerRepeat{0};    ///< Semitones added per echo (-24..+24)
    float gateScaling{1.0f};       ///< Gate duration multiplier per echo (0.1-2.0)
};

// =============================================================================
// MidiNoteDelay (Layer 2 Processor)
// =============================================================================

/// @brief MIDI echo post-processor for arpeggiator output.
///
/// Receives ArpEvents from ArpeggiatorCore and generates delayed echo copies
/// based on per-step delay configuration. Echoes that extend beyond the current
/// process block are queued in a fixed-size buffer for future blocks.
///
/// Each pending echo tracks two countdowns:
/// - noteOnRemaining: samples until the NoteOn event fires
/// - noteOffRemaining: samples until the NoteOff event fires (noteOn + gate)
///
/// Both countdowns are decremented by blockSize each process call. Events are
/// emitted when their countdown falls within [0, blockSize).
///
/// @par Real-Time Safety
/// All methods are noexcept. Zero heap allocation. Fixed-size pending buffer
/// with oldest-stealing overflow policy.
class MidiNoteDelay {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr size_t kMaxPendingEchoes = 256;
    static constexpr size_t kMaxSteps = 32;
    static constexpr int kMaxFeedback = 16;
    static constexpr float kMinDelayTimeMs = 10.0f;
    static constexpr float kMaxDelayTimeMs = 2000.0f;
    static constexpr float kMinGateScaling = 0.1f;
    static constexpr float kMaxGateScaling = 2.0f;

    // =========================================================================
    // Configuration
    // =========================================================================

    /// @brief Set delay configuration for a specific step.
    void setStepConfig(size_t step, const MidiDelayStepConfig& config) noexcept
    {
        if (step < kMaxSteps) {
            stepConfigs_[step] = config;
        }
    }

    /// @brief Get delay configuration for a specific step (read-only).
    [[nodiscard]] const MidiDelayStepConfig& getStepConfig(size_t step) const noexcept
    {
        return stepConfigs_[std::min(step, kMaxSteps - 1)];
    }

    /// @brief Clear all pending echoes and reset state.
    void reset() noexcept
    {
        pendingCount_ = 0;
        emergencyNoteOffCount_ = 0;
    }

    /// @brief Get current number of pending echoes (for diagnostics/testing).
    [[nodiscard]] size_t pendingCount() const noexcept { return pendingCount_; }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process a block: pass through input events, schedule new echoes,
    ///        emit due echoes, advance time.
    ///
    /// @param ctx              Block context (tempo, sample rate, block size)
    /// @param inputEvents      ArpEvent span from ArpeggiatorCore output
    /// @param inputCount       Number of valid events in inputEvents
    /// @param outputEvents     Output span for combined events (pass-through + echoes)
    /// @param currentDelayStep Which delay lane step is active for new NoteOn echoes
    /// @return Number of events written to outputEvents
    size_t process(
        const BlockContext& ctx,
        std::span<const ArpEvent> inputEvents,
        size_t inputCount,
        std::span<ArpEvent> outputEvents,
        size_t currentDelayStep) noexcept
    {
        const size_t maxOutput = outputEvents.size();
        size_t outCount = 0;

        // --- 1. Pass through all input events and schedule echoes for NoteOns ---
        for (size_t i = 0; i < inputCount && outCount < maxOutput; ++i) {
            outputEvents[outCount++] = inputEvents[i];

            if (inputEvents[i].type == ArpEvent::Type::NoteOn) {
                scheduleEchoes(ctx, inputEvents[i], currentDelayStep);
            }
        }

        // --- 2. Emit emergency NoteOffs from overflow stealing ---
        emitEmergencyNoteOffs(outputEvents, outCount, maxOutput);

        // --- 3. Emit due pending echoes (NoteOns and NoteOffs) ---
        emitDueEchoes(ctx, outputEvents, outCount, maxOutput);

        // --- 4. Advance time and compact ---
        advanceAndCompact(ctx.blockSize);

        return outCount;
    }

private:
    // =========================================================================
    // Internal Types
    // =========================================================================

    struct PendingEcho {
        uint8_t note{0};
        uint8_t velocity{0};
        int64_t noteOnRemaining{0};    ///< Samples until NoteOn (from block start)
        int64_t noteOffRemaining{0};   ///< Samples until NoteOff (from block start)
        bool noteOnEmitted{false};
        bool noteOffEmitted{false};
    };

    // =========================================================================
    // Echo Scheduling
    // =========================================================================

    void scheduleEchoes(
        const BlockContext& ctx,
        const ArpEvent& sourceEvent,
        size_t delayStep) noexcept
    {
        const auto& config = stepConfigs_[std::min(delayStep, kMaxSteps - 1)];
        if (!config.active || config.feedbackCount <= 0) return;

        const float delayMs = calculateDelayMs(config, ctx.tempoBPM);
        const int64_t delaySamples = static_cast<int64_t>(
            static_cast<double>(delayMs) * ctx.sampleRate / 1000.0);
        if (delaySamples <= 0) return;

        // Base gate: 80% of delay time (so echoes don't overlap by default)
        const int64_t baseGateSamples = std::max(
            int64_t{1},
            static_cast<int64_t>(static_cast<double>(delaySamples) * 0.8));

        const int feedbackCount = std::clamp(config.feedbackCount, 0, kMaxFeedback);
        const float velocityRetain = 1.0f - std::clamp(config.velocityDecay, 0.0f, 1.0f);
        const int pitchShift = std::clamp(config.pitchShiftPerRepeat, -24, 24);
        const float gateScale = std::clamp(config.gateScaling, kMinGateScaling, kMaxGateScaling);

        // Iterative multiplication avoids std::pow on audio thread
        float decayAccum = velocityRetain;
        float gateScaleAccum = gateScale;

        for (int echo = 1; echo <= feedbackCount; ++echo) {
            // Velocity with exponential decay (iterative: multiply each iteration)
            const int echoVelocity = static_cast<int>(
                std::round(static_cast<float>(sourceEvent.velocity) * decayAccum));
            if (echoVelocity < 1) break;

            // Pitch (clamped to MIDI 0-127)
            const int echoPitch = std::clamp(
                static_cast<int>(sourceEvent.note) + echo * pitchShift, 0, 127);

            // Gate duration with geometric scaling (iterative)
            const int64_t echoGate = std::max(
                int64_t{1},
                static_cast<int64_t>(static_cast<double>(baseGateSamples) *
                                     static_cast<double>(gateScaleAccum)));

            // Timing: offset from current block start
            const int64_t noteOnTime = static_cast<int64_t>(sourceEvent.sampleOffset)
                                     + static_cast<int64_t>(echo) * delaySamples;
            const int64_t noteOffTime = noteOnTime + echoGate;

            addPendingEcho(
                static_cast<uint8_t>(echoPitch),
                static_cast<uint8_t>(echoVelocity),
                noteOnTime,
                noteOffTime);

            // Advance accumulators for next echo
            decayAccum *= velocityRetain;
            gateScaleAccum *= gateScale;
        }
    }

    [[nodiscard]] static float calculateDelayMs(
        const MidiDelayStepConfig& config,
        double tempoBPM) noexcept
    {
        if (config.timeMode == TimeMode::Free) {
            return std::clamp(config.delayTimeMs, kMinDelayTimeMs, kMaxDelayTimeMs);
        }
        return dropdownToDelayMs(config.noteValueIndex, tempoBPM);
    }

    // =========================================================================
    // Echo Emission
    // =========================================================================

    void emitDueEchoes(
        const BlockContext& ctx,
        std::span<ArpEvent> outputEvents,
        size_t& outCount,
        size_t maxOutput) noexcept
    {
        const auto blockSize = static_cast<int64_t>(ctx.blockSize);

        for (size_t i = 0; i < pendingCount_; ++i) {
            auto& echo = pendingEchoes_[i];

            // Emit NoteOn if due in this block
            if (!echo.noteOnEmitted && echo.noteOnRemaining < blockSize && outCount < maxOutput) {
                const int32_t offset = static_cast<int32_t>(
                    std::clamp(echo.noteOnRemaining, int64_t{0}, blockSize - 1));

                outputEvents[outCount++] = ArpEvent{
                    ArpEvent::Type::NoteOn,
                    echo.note,
                    echo.velocity,
                    offset,
                    false};
                echo.noteOnEmitted = true;
            }

            // Emit NoteOff if due in this block (and NoteOn already emitted)
            if (echo.noteOnEmitted && !echo.noteOffEmitted
                && echo.noteOffRemaining < blockSize && outCount < maxOutput) {
                const int32_t offset = static_cast<int32_t>(
                    std::clamp(echo.noteOffRemaining, int64_t{0}, blockSize - 1));

                outputEvents[outCount++] = ArpEvent{
                    ArpEvent::Type::NoteOff,
                    echo.note,
                    0,
                    offset,
                    false};
                echo.noteOffEmitted = true;
            }
        }
    }

    // =========================================================================
    // Time Advancement & Compaction
    // =========================================================================

    void advanceAndCompact(size_t blockSize) noexcept
    {
        const auto bs = static_cast<int64_t>(blockSize);
        size_t writeIdx = 0;

        for (size_t i = 0; i < pendingCount_; ++i) {
            auto& echo = pendingEchoes_[i];

            echo.noteOnRemaining -= bs;
            echo.noteOffRemaining -= bs;

            // Remove fully completed echoes (both events emitted)
            if (echo.noteOnEmitted && echo.noteOffEmitted) {
                continue;
            }
            // Safety: remove echoes whose NoteOff is past and somehow missed
            if (echo.noteOffRemaining < -bs) {
                continue;
            }

            if (writeIdx != i) {
                pendingEchoes_[writeIdx] = pendingEchoes_[i];
            }
            ++writeIdx;
        }
        pendingCount_ = writeIdx;
    }

    // =========================================================================
    // Pending Echo Management
    // =========================================================================

    void addPendingEcho(
        uint8_t note, uint8_t velocity,
        int64_t noteOnRemaining, int64_t noteOffRemaining) noexcept
    {
        if (pendingCount_ >= kMaxPendingEchoes) {
            // Steal oldest (first slot). If its NoteOn was already emitted,
            // queue an emergency NoteOff to prevent stuck notes.
            const auto& evicted = pendingEchoes_[0];
            if (evicted.noteOnEmitted && !evicted.noteOffEmitted
                && emergencyNoteOffCount_ < kMaxEmergencyNoteOffs) {
                emergencyNoteOffs_[emergencyNoteOffCount_++] = evicted.note;
            }
            for (size_t i = 1; i < pendingCount_; ++i) {
                pendingEchoes_[i - 1] = pendingEchoes_[i];
            }
            --pendingCount_;
        }

        pendingEchoes_[pendingCount_] = PendingEcho{
            note, velocity, noteOnRemaining, noteOffRemaining, false, false};
        ++pendingCount_;
    }

    /// @brief Emit emergency NoteOffs at sampleOffset 0 for stolen echoes.
    void emitEmergencyNoteOffs(
        std::span<ArpEvent> outputEvents,
        size_t& outCount,
        size_t maxOutput) noexcept
    {
        for (size_t i = 0; i < emergencyNoteOffCount_ && outCount < maxOutput; ++i) {
            outputEvents[outCount++] = ArpEvent{
                ArpEvent::Type::NoteOff,
                emergencyNoteOffs_[i],
                0,
                0,
                false};
        }
        emergencyNoteOffCount_ = 0;
    }

    // =========================================================================
    // State
    // =========================================================================

    static constexpr size_t kMaxEmergencyNoteOffs = 16;

    std::array<MidiDelayStepConfig, kMaxSteps> stepConfigs_{};
    std::array<PendingEcho, kMaxPendingEchoes> pendingEchoes_{};
    size_t pendingCount_{0};
    std::array<uint8_t, kMaxEmergencyNoteOffs> emergencyNoteOffs_{};
    size_t emergencyNoteOffCount_{0};
};

} // namespace Krate::DSP
