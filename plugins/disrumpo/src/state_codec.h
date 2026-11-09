#pragma once

// ==============================================================================
// Disrumpo state codec: the normalized <-> plain mappings shared by the
// processor (processParameterChanges, getState/setState) and the controller
// (parseComponentState, createComponentStateStream, parameter registration).
// ==============================================================================
// One copy of every range, so the session writer, the preset writer and both
// readers cannot drift apart again (audit D-052, D-086, D-091).
//
// Only constexpr / inline helpers live here: no processor or controller types.
// The processor is built with -ffast-math on macOS, so the range ends are exact
// by branch (never by arithmetic that has to cancel), log/exp arguments are kept
// inside their domain, and non-finite input is classified with
// Krate::DSP::detail::isFinite.
// ==============================================================================

#include "plugin_ids.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/note_value.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Disrumpo {

static_assert(kNoteValueCount == Krate::DSP::kNoteValueDropdownCount,
              "kNoteValueCount must alias the shared note-value dropdown size");

// ==============================================================================
// Crossover frequency: log10 over [20, 20000] Hz
// ==============================================================================

inline constexpr float kMinCrossoverHz = 20.0f;
inline constexpr float kMaxCrossoverHz = 20000.0f;

[[nodiscard]] inline float crossoverNormToHz(float norm) noexcept {
    if (!Krate::DSP::detail::isFinite(norm) || norm <= 0.0f) return kMinCrossoverHz;
    if (norm >= 1.0f) return kMaxCrossoverHz;
    const float logMin = std::log10(kMinCrossoverHz);
    const float logMax = std::log10(kMaxCrossoverHz);
    return std::pow(10.0f, logMin + norm * (logMax - logMin));
}

[[nodiscard]] inline float crossoverHzToNorm(float hz) noexcept {
    if (!Krate::DSP::detail::isFinite(hz) || hz <= kMinCrossoverHz) return 0.0f;
    if (hz >= kMaxCrossoverHz) return 1.0f;
    const float logMin = std::log10(kMinCrossoverHz);
    const float logMax = std::log10(kMaxCrossoverHz);
    return std::clamp((std::log10(hz) - logMin) / (logMax - logMin), 0.0f, 1.0f);
}

// ==============================================================================
// Sweep center frequency: log2 over [20, 20000] Hz
// ==============================================================================

inline constexpr float kNormMinFreqHz = 20.0f;
inline constexpr float kNormMaxFreqHz = 20000.0f;
inline constexpr float kLog2MinFreq = 4.321928f;    // log2(20)
inline constexpr float kLog2MaxFreq = 14.287712f;   // log2(20000)
inline constexpr float kLog2FreqRange = kLog2MaxFreq - kLog2MinFreq;

[[nodiscard]] inline float sweepFreqNormToHz(float norm) noexcept {
    if (!Krate::DSP::detail::isFinite(norm) || norm <= 0.0f) return kNormMinFreqHz;
    if (norm >= 1.0f) return kNormMaxFreqHz;
    return std::pow(2.0f, kLog2MinFreq + norm * kLog2FreqRange);
}

[[nodiscard]] inline float sweepFreqHzToNorm(float hz) noexcept {
    if (!Krate::DSP::detail::isFinite(hz) || hz <= kNormMinFreqHz) return 0.0f;
    if (hz >= kNormMaxFreqHz) return 1.0f;
    return std::clamp((std::log2(hz) - kLog2MinFreq) / kLog2FreqRange, 0.0f, 1.0f);
}

// ==============================================================================
// LFO rate (sweep LFO, LFO 1, LFO 2): ln over [0.01, 20] Hz
// ==============================================================================

inline constexpr float kMinLfoRateHz = 0.01f;
inline constexpr float kMaxLfoRateHz = 20.0f;

[[nodiscard]] inline float lfoRateNormToHz(float norm) noexcept {
    if (!Krate::DSP::detail::isFinite(norm) || norm <= 0.0f) return kMinLfoRateHz;
    if (norm >= 1.0f) return kMaxLfoRateHz;
    const float logMin = std::log(kMinLfoRateHz);
    const float logMax = std::log(kMaxLfoRateHz);
    return std::exp(logMin + norm * (logMax - logMin));
}

[[nodiscard]] inline float lfoRateHzToNorm(float hz) noexcept {
    if (!Krate::DSP::detail::isFinite(hz) || hz <= kMinLfoRateHz) return 0.0f;
    if (hz >= kMaxLfoRateHz) return 1.0f;
    const float logMin = std::log(kMinLfoRateHz);
    const float logMax = std::log(kMaxLfoRateHz);
    return std::clamp((std::log(hz) - logMin) / (logMax - logMin), 0.0f, 1.0f);
}

// ==============================================================================
// List parameters: index k of `count` entries <-> k / (count - 1)
// ==============================================================================

[[nodiscard]] inline double listIndexToNorm(int index, int count) noexcept {
    if (count <= 1) return 0.0;
    return static_cast<double>(std::clamp(index, 0, count - 1)) / static_cast<double>(count - 1);
}

[[nodiscard]] inline int normToListIndex(double norm, int count) noexcept {
    if (count <= 1 || !Krate::DSP::detail::isFinite(norm)) return 0;
    return std::clamp(static_cast<int>(std::lround(norm * static_cast<double>(count - 1))), 0, count - 1);
}

/// Entry counts of the list parameters the state stream stores as int8 indices.
inline constexpr int kLfoShapeCount = 6;           ///< 6 waveforms (sweep LFO, LFO 1, LFO 2)
inline constexpr int kEnvFollowerSourceCount = 5;  ///< Input L/R/Sum/Mid/Side
inline constexpr int kChaosModelCount = 4;         ///< Lorenz, Rossler, Chua, Henon
inline constexpr int kSampleHoldSourceCount = 4;   ///< Random, LFO1, LFO2, External
inline constexpr int kRunglerBitsCount = 13;       ///< 4..16 bits (stored as the bit count)

// ==============================================================================
// Routing source (D-055): 05a3ddb7 inserted ModSource::Rungler = 10 and moved
// SampleHold / PitchFollower / Transient from 10 / 11 / 12 up by one, without a
// version bump. v8 and older streams all predate it. v9 is ambiguous (v0.9.0 /
// v0.9.1 old numbering, v0.9.4 to v0.9.6 new, same layout) and is read with the
// new numbering; see the version history in plugin_ids.h.
// ==============================================================================

inline constexpr int kFirstRenumberedModSource = 10;  ///< old SampleHold, now Rungler

[[nodiscard]] constexpr int migrateModSourceIndex(int stored, int32_t version) noexcept {
    return (version <= 8 && stored >= kFirstRenumberedModSource) ? stored + 1 : stored;
}

// ==============================================================================
// Linear ranges: plain = min + norm * span
// ==============================================================================

struct LinearRange {
    float min;
    float span;
    [[nodiscard]] constexpr float toPlain(float norm) const noexcept { return min + norm * span; }
    [[nodiscard]] constexpr float toNorm(float plain) const noexcept { return (plain - min) / span; }
};

inline constexpr LinearRange kSweepWidthOctRange{.min = 0.5f, .span = 3.5f};       ///< 0.5..4 octaves
inline constexpr LinearRange kSweepIntensityRange{.min = 0.0f, .span = 2.0f};      ///< 0..200 %
inline constexpr LinearRange kLfoPhaseDegRange{.min = 0.0f, .span = 360.0f};
inline constexpr LinearRange kEnvFollowerAttackMsRange{.min = 1.0f, .span = 99.0f};
inline constexpr LinearRange kEnvFollowerReleaseMsRange{.min = 10.0f, .span = 490.0f};
inline constexpr LinearRange kRandomRateHzRange{.min = 0.1f, .span = 49.9f};
inline constexpr LinearRange kChaosSpeedRange{.min = 0.05f, .span = 19.95f};
inline constexpr LinearRange kSampleHoldRateHzRange{.min = 0.1f, .span = 49.9f};
inline constexpr LinearRange kSampleHoldSlewMsRange{.min = 0.0f, .span = 500.0f};
inline constexpr LinearRange kPitchFollowerMinHzRange{.min = 20.0f, .span = 480.0f};
inline constexpr LinearRange kPitchFollowerMaxHzRange{.min = 200.0f, .span = 4800.0f};
inline constexpr LinearRange kPitchFollowerTrackingMsRange{.min = 10.0f, .span = 290.0f};
inline constexpr LinearRange kTransientAttackMsRange{.min = 0.5f, .span = 9.5f};
inline constexpr LinearRange kTransientDecayMsRange{.min = 20.0f, .span = 180.0f};
inline constexpr LinearRange kRunglerRateHzRange{.min = 0.1f, .span = 49.9f};

}  // namespace Disrumpo
