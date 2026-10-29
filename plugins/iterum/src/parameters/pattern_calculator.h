// ==============================================================================
// Pattern Calculator
// ==============================================================================
// What the 19 preset timing patterns play, computed in the controller exactly as MultiTapDelay computes it: in
// milliseconds at a given tempo, with every clamp the DSP applies, in the order it applies them
// (specs/iterum-tap-editor/design.md 2.6). Used by Copy to Custom and by the tap editor's read-only display, so
// what the display shows, what Copy writes and what plays are the same numbers.
//
// The DSP lines mirrored: the tempo clamp of MultiTapDelay::setTempo and noteToDelayMs ([20, 300] BPM); the
// rhythmic base clamp of applyTimingPattern / calculatePatternTimes ([1, 5000] ms) and TapManager's per-tap
// min(.., 5000) of loadPatternWithBaseTime; the mathematical base clamp of process() ([1, 5000] ms, before any
// ratio); Golden's chain over the clamped previous tap; Fibonacci's fib(i+1) x B / 4; Exponential's B x 2^i;
// Primes' B x primeRatios[i]; Linear's spread clamped at its end (maxT = min(B x n, 5000)); -3 dB per tap; then
// the spatial pattern's level rule (Decaying -3 dB per tap, Flat 0 dB, the pan-only patterns leave the level).
// ==============================================================================

#pragma once

#include "parameters/multitap_params.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/note_value.h>
#include <krate/dsp/effects/multi_tap_delay.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace Iterum {

/// One tap of a preset timing pattern.
struct PatternTap {
    float ms;       ///< what plays, with every DSP clamp
    float beats;    ///< the unclamped musical value, in quarter-note beats
    float levelDb;  ///< the timing pattern's own level (-3 dB per tap)
};

namespace PatternCalculatorDetail {

inline constexpr double kMinMs = 1.0;
inline constexpr double kMaxMs = 5000.0;
inline constexpr int kLinearPattern = 18;
inline constexpr int kLastPresetPattern = 18;

[[nodiscard]] inline double clampTempo(double bpm) noexcept {
    return std::clamp(bpm, Krate::DSP::kMinTempoSyncBPM, Krate::DSP::kMaxTempoSyncBPM);
}

/// Beats of the base of the rhythmic patterns 0-13 (the multipliers of MultiTapDelay::applyTimingPattern).
[[nodiscard]] inline double rhythmicBeats(int timingPattern) noexcept {
    static constexpr double kBeats[14] = {4.0,       2.0,       1.0,       0.5,       0.25,      0.125, 3.0,
                                          1.5,       0.75,      0.375,     4.0 / 3.0, 2.0 / 3.0, 1.0 / 3.0,
                                          1.0 / 6.0};
    return kBeats[std::clamp(timingPattern, 0, 13)];
}

/// MultiTapDelay::applyPrimeNumbersPattern's ratios.
inline constexpr double kPrimeRatios[16] = {0.5,  0.75, 1.25, 1.75,  2.75,  3.25,  4.25,  4.75,
                                            5.75, 7.25, 7.75, 9.25, 10.25, 10.75, 11.75, 13.25};

/// TapManager::fibonacci: fib(1) = fib(2) = 1.
[[nodiscard]] inline double fibonacci(size_t n) noexcept {
    if (n <= 2) return 1.0;
    double prev2 = 1.0;
    double prev1 = 1.0;
    for (size_t k = 3; k <= n; ++k) {
        const double current = prev1 + prev2;
        prev2 = prev1;
        prev1 = current;
    }
    return prev1;
}

} // namespace PatternCalculatorDetail

/// Tap @p i of @p n of timing pattern @p timingPattern (0-18) at @p bpm. Note Value / Modifier are the 911 / 912
/// dropdown indices, used by the mathematical patterns 14-18 only.
[[nodiscard]] inline PatternTap patternTap(int timingPattern, int noteValueIdx, int noteModifierIdx, size_t i,
                                           size_t n, double bpm) noexcept {
    namespace D = PatternCalculatorDetail;
    const double q = 60000.0 / D::clampTempo(bpm);  // ms per beat
    const float levelDb = -3.0f * static_cast<float>(i);
    const double tap = static_cast<double>(i + 1);

    if (timingPattern < 0 || timingPattern > D::kLastPresetPattern || n == 0) {
        return {static_cast<float>(D::kMinMs), 0.0f, levelDb};
    }

    if (timingPattern <= 13) {
        const double baseBeats = D::rhythmicBeats(timingPattern);
        const double baseMs = std::clamp(baseBeats * q, D::kMinMs, D::kMaxMs);
        return {static_cast<float>(std::min(tap * baseMs, D::kMaxMs)), static_cast<float>(tap * baseBeats), levelDb};
    }

    const auto mapping = multiTapNoteValueFromDropdown(noteValueIdx, noteModifierIdx);
    const double baseBeats = static_cast<double>(Krate::DSP::getBeatsForNote(mapping.note, mapping.modifier));
    const double baseMs = std::clamp(baseBeats * q, D::kMinMs, D::kMaxMs);  // applied before any ratio

    double ms = baseMs;
    double beats = baseBeats;
    switch (timingPattern) {
        case 14: {  // Golden Ratio: a chain over the clamped previous tap
            const double phi = static_cast<double>(Krate::DSP::kGoldenRatio);
            for (size_t k = 1; k <= i; ++k) {
                ms = std::min(ms * phi, D::kMaxMs);
                beats *= phi;
            }
            break;
        }
        case 15: {  // Fibonacci
            const double f = D::fibonacci(i + 1);
            ms = std::min(f * baseMs * 0.25, D::kMaxMs);
            beats = f * baseBeats * 0.25;
            break;
        }
        case 16: {  // Exponential
            const double m = std::pow(2.0, static_cast<double>(i));
            ms = std::min(baseMs * m, D::kMaxMs);
            beats = baseBeats * m;
            break;
        }
        case 17: {  // Primes
            const double r = D::kPrimeRatios[std::min(i, size_t{15})];
            ms = std::min(baseMs * r, D::kMaxMs);
            beats = baseBeats * r;
            break;
        }
        case D::kLinearPattern:
        default: {  // Linear: the spread is clamped at its end, not per tap
            const double count = static_cast<double>(n);
            const double maxMs = std::min(baseMs * count, D::kMaxMs);
            const double step = n > 1 ? (maxMs - baseMs) / (count - 1.0) : 0.0;
            ms = baseMs + step * static_cast<double>(i);
            beats = baseBeats * tap;
            break;
        }
    }
    return {static_cast<float>(ms), static_cast<float>(beats), levelDb};
}

/// True when any of the @p n taps plays at a time other than its musical value at @p bpm, i.e. when any of the
/// DSP's clamps changed what plays.
[[nodiscard]] inline bool patternClampEngaged(int timingPattern, int noteValueIdx, int noteModifierIdx, size_t n,
                                              double bpm) noexcept {
    const double q = 60000.0 / PatternCalculatorDetail::clampTempo(bpm);
    for (size_t i = 0; i < n; ++i) {
        const PatternTap tap = patternTap(timingPattern, noteValueIdx, noteModifierIdx, i, n, bpm);
        if (std::abs(static_cast<double>(tap.ms) - static_cast<double>(tap.beats) * q) > 0.01) {
            return true;
        }
    }
    return false;
}

/// The level tap @p i plays at: the processor applies the spatial pattern after the timing pattern in every block,
/// so Decaying (4) gives -3 dB per tap, Flat (5) 0 dB, and the pan-only patterns leave the timing pattern's level.
[[nodiscard]] inline float playedLevelDb(float patternLevelDb, int spatialPatternIdx, size_t i) noexcept {
    switch (spatialPatternIdx) {
        case 4: return -3.0f * static_cast<float>(i);
        case 5: return 0.0f;
        default: return patternLevelDb;
    }
}

/// A preset pattern as the Custom pattern that plays it (design 2.6 steps 2-4): the times as ratios of the chosen
/// Length, the levels as linear gain.
struct CustomPatternCopy {
    std::array<float, kCustomPatternMaxTaps> ratios{};
    std::array<float, kCustomPatternMaxTaps> levels{};
    size_t count = 0;
    Krate::DSP::CustomLength length = Krate::DSP::CustomLength::Free;
};

/// The smallest synced Length that holds every tap when no clamp is engaged and every tap lies within 8 beats
/// (ratio = beats / span beats: the copy keeps following the tempo); Free at the played times otherwise
/// (ratio = ms / 5000: exact at this tempo, including the taps the source stacks at 5 s).
[[nodiscard]] inline CustomPatternCopy customPatternFromPreset(int timingPattern, int noteValueIdx,
                                                               int noteModifierIdx, int spatialPatternIdx, size_t n,
                                                               double bpm) noexcept {
    using Krate::DSP::CustomLength;
    CustomPatternCopy copy;
    copy.count = std::clamp(n, size_t{1}, kCustomPatternMaxTaps);

    std::array<PatternTap, kCustomPatternMaxTaps> taps{};
    float maxBeats = 0.0f;
    for (size_t i = 0; i < copy.count; ++i) {
        taps[i] = patternTap(timingPattern, noteValueIdx, noteModifierIdx, i, copy.count, bpm);
        maxBeats = std::max(maxBeats, taps[i].beats);
    }
    const bool clamped = patternClampEngaged(timingPattern, noteValueIdx, noteModifierIdx, copy.count, bpm);

    constexpr float kEpsilon = 1e-6f;
    const float longest = Krate::DSP::kCustomLengthBeats[Krate::DSP::kCustomLengthCount - 1];
    if (!clamped && maxBeats <= longest + kEpsilon) {
        size_t index = 1;
        while (index + 1 < Krate::DSP::kCustomLengthCount &&
               Krate::DSP::kCustomLengthBeats[index] < maxBeats - kEpsilon) {
            ++index;
        }
        copy.length = static_cast<CustomLength>(index);
        const float spanBeats = Krate::DSP::kCustomLengthBeats[index];
        for (size_t i = 0; i < copy.count; ++i) {
            copy.ratios[i] = std::clamp(taps[i].beats / spanBeats, 0.0f, 1.0f);
        }
    } else {
        copy.length = CustomLength::Free;
        for (size_t i = 0; i < copy.count; ++i) {
            copy.ratios[i] = std::clamp(taps[i].ms / Krate::DSP::kCustomFreeLengthMs, 0.0f, 1.0f);
        }
    }

    for (size_t i = 0; i < copy.count; ++i) {
        const float dB = playedLevelDb(taps[i].levelDb, spatialPatternIdx, i);
        copy.levels[i] = std::clamp(Krate::DSP::dbToGain(dB), 0.0f, 1.0f);
    }
    return copy;
}

} // namespace Iterum
