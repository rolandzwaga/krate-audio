// =============================================================================
// TapPatternEditor Pure Logic Functions
// =============================================================================
// Extracted for testability (humble object pattern) - Constitution Principle XIII
// These pure functions can be tested without VSTGUI dependencies.
//
// specs/iterum-tap-editor/design.md: the time axis (2.1), the one grid function every drawing, snap and placement
// path uses (2.2), the default rule (3.1), placement (3.3), the level axis (5.2), the ruler (2.2), overlap labels
// (5.5) and the readout (4.3). Geometry of the 852 x 140 view (5.1): plot x 42..844, 0 dB at y 14, -48 dB at y 106,
// silence band y 106..114, ruler y 114..140.
// =============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/note_value.h>
#include <krate/dsp/effects/multi_tap_delay.h>  // CustomLength, kCustomLengthBeats, customLengthSpanMs

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace Iterum {

/// The custom pattern's span (specs/iterum-tap-editor/design.md 2.1): one definition, shared with the DSP.
using Krate::DSP::CustomLength;

/// Maximum number of taps supported by the pattern editor
static constexpr size_t kMaxPatternTaps = 16;

/// Minimum editor width in pixels (FR-007: handle narrow widths)
static constexpr float kMinEditorWidth = 200.0f;

// =============================================================================
// Geometry (design 5.1, 5.2, 4.1)
// =============================================================================

inline constexpr float kPlotLeft = 42.0f;      ///< x of ratio 0
inline constexpr float kPlotRight = 844.0f;    ///< x of ratio 1
inline constexpr float kPlotWidth = kPlotRight - kPlotLeft;  ///< 802 px
inline constexpr float kPlotTop = 14.0f;       ///< y of 0 dB
inline constexpr float kPlotBottom = 106.0f;   ///< y of -48 dB
inline constexpr float kPlotHeight = kPlotBottom - kPlotTop;  ///< 92 px
inline constexpr float kBandBottom = 114.0f;   ///< the silence band is y 106..114
inline constexpr float kRulerBaseline = 114.0f;
inline constexpr float kRulerTickBottom = 122.0f;
inline constexpr float kRulerLabelTop = 122.0f;
inline constexpr float kRulerLabelBottom = 138.0f;
inline constexpr float kGutterRight = 30.0f;   ///< level labels and tempo are right-aligned here

inline constexpr float kTapBarWidth = 12.0f;   ///< drawn bar
inline constexpr float kTapHitWidth = 16.0f;   ///< hit box
inline constexpr float kTapHandleDrawHeight = 6.0f;
inline constexpr float kHandleAbove = 3.0f;    ///< the handle reaches 3 px above the bar top
inline constexpr float kHandleBelow = 8.0f;    ///< and 8 px below it
inline constexpr float kTallTapMinHeight = 12.0f;  ///< a shorter bar has one grip
inline constexpr float kDeadZonePx = 2.0f;
inline constexpr float kFineDivisor = 10.0f;   ///< Shift: 10:1

/// Level axis floor (design 5.2) and the collision distance of two taps, 8 px of the 802 px plot (design 3.3, 5.5).
inline constexpr float kLevelFloorDb = -48.0f;
inline constexpr float kCollisionRatio = 8.0f / 802.0f;

/// Default level for a tap (100% / full level)
inline constexpr float kDefaultTapLevel = 1.0f;

/// The DSP's silence: a level at or below it plays at -96 dB and reads "-inf dB" (DSP applyCustomTimingPattern).
inline constexpr float kSilentLevel = 0.001f;

// =============================================================================
// Value Clamping Functions (T018.1)
// =============================================================================

/// Clamp a value to valid range [0.0, 1.0]
inline float clampRatio(float value) noexcept {
    return std::clamp(value, 0.0f, 1.0f);
}

// =============================================================================
// Grid Snapping (design 2.2, 2.3)
// =============================================================================

/// Snap division options for grid snapping (parameter 913)
/// Matches note value dropdown order: Off, then 21 note values; index s is note-value dropdown index s - 1.
/// Order: 1/64T, 1/64, 1/64D, 1/32T, 1/32, 1/32D, ... 1/1T, 1/1, 1/1D
enum class SnapDivision {
    Off = 0,
    N64T = 1, N64 = 2, N64D = 3,
    N32T = 4, N32 = 5, N32D = 6,
    N16T = 7, N16 = 8, N16D = 9,
    N8T = 10, N8 = 11, N8D = 12,
    N4T = 13, N4 = 14, N4D = 15,
    N2T = 16, N2 = 17, N2D = 18,
    N1T = 19, N1 = 20, N1D = 21
};

inline constexpr int kSnapDivisionCount = 22;

/// The Snap menu's name of @p snap ("1/8D"); "Off" for Off.
[[nodiscard]] inline const char* snapDivisionName(SnapDivision snap) noexcept {
    static constexpr const char* kNames[kSnapDivisionCount] = {
        "Off",
        "1/64T", "1/64", "1/64D", "1/32T", "1/32", "1/32D", "1/16T", "1/16", "1/16D",
        "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D", "1/2T", "1/2", "1/2D", "1/1T", "1/1", "1/1D"};
    const int index = static_cast<int>(snap);
    return (index >= 0 && index < kSnapDivisionCount) ? kNames[index] : "Off";
}

/// The length of @p snap in quarter-note beats (0 for Off): the note-value dropdown's own note length.
[[nodiscard]] inline double snapUnitBeats(SnapDivision snap) noexcept {
    const int index = static_cast<int>(snap);
    if (index <= 0 || index >= kSnapDivisionCount) return 0.0;
    const auto mapping = Krate::DSP::kNoteValueDropdownMapping[index - 1];
    return static_cast<double>(Krate::DSP::getBeatsForNote(mapping.note, mapping.modifier));
}

// =============================================================================
// Mouse Button Handling Functions (T018.5)
// =============================================================================

/// Check if right button should be ignored
inline bool shouldIgnoreRightClick(bool isRightButton) noexcept {
    return isRightButton;  // Right-click is ignored in v1
}

// =============================================================================
// Editor Size Validation (T031.9)
// =============================================================================

/// Get effective editor width (enforces minimum)
inline float getEffectiveEditorWidth(float actualWidth) noexcept {
    return std::max(actualWidth, kMinEditorWidth);
}

// =============================================================================
// Time axis (design 2.1)
// =============================================================================

/// The span of @p length in milliseconds at @p bpm: 5000 in Free, the Length's beats at the tempo otherwise.
[[nodiscard]] inline float spanMs(CustomLength length, double bpm) noexcept {
    return Krate::DSP::customLengthSpanMs(length, bpm);
}

/// The playable fraction c = min(1, 5000 / span): taps right of it play at 5000 ms.
[[nodiscard]] inline float playableFraction(CustomLength length, double bpm) noexcept {
    const float span = spanMs(length, bpm);
    if (span <= Krate::DSP::kCustomFreeLengthMs) return 1.0f;
    return Krate::DSP::kCustomFreeLengthMs / span;
}

/// The span in quarter-note beats: the Length's beats when synced, 5000 ms at the (clamped) tempo in Free.
[[nodiscard]] inline double spanBeats(CustomLength length, double bpm) noexcept {
    const auto index = static_cast<size_t>(length);
    if (length != CustomLength::Free && index < Krate::DSP::kCustomLengthCount) {
        return static_cast<double>(Krate::DSP::kCustomLengthBeats[index]);
    }
    const double clamped = std::clamp(bpm, Krate::DSP::kMinTempoSyncBPM, Krate::DSP::kMaxTempoSyncBPM);
    return static_cast<double>(Krate::DSP::kCustomFreeLengthMs) * clamped / 60000.0;
}

/// The one default position D(i, n) = (i + 1) / n x c (design 3.1): Reset and the ideal of double-click and
/// placement.
[[nodiscard]] inline float defaultTapTime(size_t tapIndex, size_t tapCount, float playable) noexcept {
    if (tapCount == 0) return 0.0f;
    return static_cast<float>(tapIndex + 1) / static_cast<float>(tapCount) * playable;
}

// =============================================================================
// Axis mapping (design 5.1, 5.2)
// =============================================================================

/// x of a time ratio on the inset plot: 42 + ratio x 802, the edges by branch.
[[nodiscard]] inline float timeRatioToX(float ratio) noexcept {
    if (ratio <= 0.0f) return kPlotLeft;
    if (ratio >= 1.0f) return kPlotRight;
    return kPlotLeft + ratio * kPlotWidth;
}

/// The time ratio of an x, clamped to [0, 1] outside the plot.
[[nodiscard]] inline float xToTimeRatio(float x) noexcept {
    if (x <= kPlotLeft) return 0.0f;
    if (x >= kPlotRight) return 1.0f;
    return (x - kPlotLeft) / kPlotWidth;
}

/// True when @p level lies below the display floor (-48 dB): drawn in the silence band, no bar.
[[nodiscard]] inline bool isSilentLevel(float level) noexcept {
    return level <= 0.0f || Krate::DSP::gainToDb(level) < kLevelFloorDb;
}

/// The middle of the silence band, where a silent tap's handle sits.
inline constexpr float kSilentY = (kPlotBottom + kBandBottom) * 0.5f;

/// y of a linear level on the dB axis: 0 dB at 14, -48 dB at 106; a silent level is in the band.
[[nodiscard]] inline float levelToY(float level) noexcept {
    if (level >= 1.0f) return kPlotTop;
    if (isSilentLevel(level)) return kSilentY;
    const float dB = std::min(Krate::DSP::gainToDb(level), 0.0f);
    return kPlotTop + (-dB / -kLevelFloorDb) * kPlotHeight;
}

/// The level of a y: above the plot 1.0, in the silence band and below it exactly 0.
[[nodiscard]] inline float yToLevel(float y) noexcept {
    if (y <= kPlotTop) return 1.0f;
    if (y > kPlotBottom) return 0.0f;
    const float dB = kLevelFloorDb * (y - kPlotTop) / kPlotHeight;
    return std::clamp(Krate::DSP::dbToGain(dB), 0.0f, 1.0f);
}

/// The dB a level drag reaches from @p startLevel after @p dy px (design 4.2): it starts from the display floor, so
/// a silent tap leaves the band on its first upward pixel; Shift is 10:1.
[[nodiscard]] inline float levelDragDb(float startLevel, float dy, bool fine) noexcept {
    const float startDb = std::max(Krate::DSP::gainToDb(startLevel), kLevelFloorDb);
    const float dbPerPx = -kLevelFloorDb / kPlotHeight / (fine ? kFineDivisor : 1.0f);
    return startDb - dy * dbPerPx;
}

/// The level of a dragged dB: at or above 0 dB is 1.0, below the floor is exactly 0 (design 5.2).
[[nodiscard]] inline float levelFromDragDb(float dB) noexcept {
    if (dB >= 0.0f) return 1.0f;
    if (dB < kLevelFloorDb) return 0.0f;
    return std::clamp(Krate::DSP::dbToGain(dB), 0.0f, 1.0f);
}

// =============================================================================
// Grid lines (design 2.2)
// =============================================================================

/// A vertical line of the plot: a beat, a bar or a snap line, as a ratio of the length (design 2.2).
struct GridLine {
    float ratio = 0.0f;
    enum Kind { Beat, Bar, Snap } kind = Beat;
};

namespace detail {

/// The multiples k x unit strictly inside the span (k >= 1), as ratios; ratio 0 never, ratio 1 not included.
[[nodiscard]] inline std::vector<float> unitMultiples(double unitBeats, double span) {
    std::vector<float> out;
    if (unitBeats <= 0.0 || span <= 0.0) return out;
    for (int k = 1;; ++k) {
        const double ratio = static_cast<double>(k) * unitBeats / span;
        if (ratio >= 1.0 - 1e-5) break;
        out.push_back(static_cast<float>(ratio));
    }
    return out;
}

/// The snap lines of @p unitBeats: its multiples plus ratio 1 always.
[[nodiscard]] inline std::vector<float> snapRatios(double unitBeats, double span) {
    std::vector<float> out = unitMultiples(unitBeats, span);
    out.push_back(1.0f);
    return out;
}

}  // namespace detail

/// The unit the snap lines use: @p snap, or 1/16 when the unit is as long as the span or longer, which would leave
/// fewer than two lines (design 2.2). Off stays Off.
[[nodiscard]] inline SnapDivision effectiveSnapUnit(CustomLength length, SnapDivision snap, double bpm) noexcept {
    if (snap == SnapDivision::Off) return snap;
    const double unit = snapUnitBeats(snap);
    const double span = spanBeats(length, bpm);
    if (unit <= 0.0 || unit / span >= 1.0 - 1e-5) return SnapDivision::N16;
    return snap;
}

/// The snap lines of @p snap over @p length as ratios, ascending: the multiples of the effective unit inside the
/// span plus ratio 1, never ratio 0; empty for Snap Off.
[[nodiscard]] inline std::vector<float> snapLineRatios(CustomLength length, SnapDivision snap, double bpm) {
    if (snap == SnapDivision::Off) return {};
    const SnapDivision unit = effectiveSnapUnit(length, snap, bpm);
    return detail::snapRatios(snapUnitBeats(unit), spanBeats(length, bpm));
}

/// Every grid line of @p length with @p snap at @p bpm (design 2.2): beat lines at every quarter note and bar lines
/// at every whole note from ratio 0 (in Free at the tempo, so the musical grid moves under the millisecond taps),
/// then the snap lines.
[[nodiscard]] inline std::vector<GridLine> gridLines(CustomLength length, SnapDivision snap, double bpm) {
    std::vector<GridLine> lines;
    const double span = spanBeats(length, bpm);
    for (int k = 0;; ++k) {
        const double ratio = static_cast<double>(k) / span;
        if (ratio > 1.0 + 1e-6) break;
        lines.push_back({static_cast<float>(std::min(ratio, 1.0)), (k % 4 == 0) ? GridLine::Bar : GridLine::Beat});
    }
    for (const float r : snapLineRatios(length, snap, bpm)) {
        lines.push_back({r, GridLine::Snap});
    }
    return lines;
}

/// @p ratio snapped to the nearest snap line of @p lines, ties to the later line (design 2.3); clamped to [0, 1]
/// when @p lines holds no snap line.
[[nodiscard]] inline float snapToLines(float ratio, const std::vector<GridLine>& lines) noexcept {
    float best = -1.0f;
    float bestDistance = 0.0f;
    for (const auto& line : lines) {
        if (line.kind != GridLine::Snap) continue;
        const float distance = std::abs(line.ratio - ratio);
        if (best < 0.0f || distance <= bestDistance + 1e-7f) {
            best = line.ratio;
            bestDistance = distance;
        }
    }
    return best < 0.0f ? clampRatio(ratio) : best;
}

// =============================================================================
// Placement (design 3.3)
// =============================================================================

/// The candidate lines of placement (design 3.3 steps 2-3): the snap lines (or 1/16 with Snap Off), then the 1/16
/// and the 1/64 retries, each cut at the playable fraction.
struct PlacementGrids {
    std::vector<float> primary;
    std::vector<float> sixteenth;
    std::vector<float> sixtyFourth;
};

/// The placement candidate grids (design 3.3): only lines in (0, c], so no candidate lies beyond the 5 s ceiling.
[[nodiscard]] inline PlacementGrids placementGrids(CustomLength length, SnapDivision snap, double bpm) {
    const float playable = playableFraction(length, bpm);
    const auto cut = [playable](std::vector<float> lines) {
        lines.erase(std::remove_if(lines.begin(), lines.end(),
                                   [playable](float r) { return r <= 0.0f || r > playable + 1e-6f; }),
                    lines.end());
        return lines;
    };
    PlacementGrids grids;
    grids.sixteenth = cut(snapLineRatios(length, SnapDivision::N16, bpm));
    grids.sixtyFourth = cut(snapLineRatios(length, SnapDivision::N64, bpm));
    grids.primary = (snap == SnapDivision::Off) ? grids.sixteenth : cut(snapLineRatios(length, snap, bpm));
    return grids;
}

/// Where revealed or double-clicked tap @p index goes (design 3.3 steps 1-3): its stored ratio when that is free
/// (inside (0, c] and no other active tap within kCollisionRatio), else the free line nearest @p ideal of the first
/// grid that has one (nearest, ties later, then alternately later and earlier), else @p ideal.
[[nodiscard]] inline float placeTap(const std::array<float, kMaxPatternTaps>& ratios, size_t active, size_t index,
                                    float ideal, const PlacementGrids& grids, float playable) noexcept {
    if (index >= kMaxPatternTaps) return 0.0f;
    const size_t count = std::min(active, kMaxPatternTaps);
    const auto isFree = [&](float t) {
        if (!(t > 0.0f) || t > playable + 1e-6f) return false;
        for (size_t j = 0; j < count; ++j) {
            if (j != index && std::abs(ratios[j] - t) < kCollisionRatio) return false;
        }
        return true;
    };
    if (isFree(ratios[index])) return ratios[index];

    for (const std::vector<float>* grid : {&grids.primary, &grids.sixteenth, &grids.sixtyFourth}) {
        if (grid->empty()) continue;
        const auto size = static_cast<std::ptrdiff_t>(grid->size());
        std::ptrdiff_t nearest = 0;
        float nearestDistance = std::abs((*grid)[0] - ideal);
        for (std::ptrdiff_t k = 1; k < size; ++k) {
            const float distance = std::abs((*grid)[static_cast<size_t>(k)] - ideal);
            if (distance <= nearestDistance + 1e-7f) {
                nearest = k;
                nearestDistance = distance;
            }
        }
        if (isFree((*grid)[static_cast<size_t>(nearest)])) return (*grid)[static_cast<size_t>(nearest)];
        for (std::ptrdiff_t step = 1; step < size; ++step) {
            const std::ptrdiff_t later = nearest + step;
            const std::ptrdiff_t earlier = nearest - step;
            if (later < size && isFree((*grid)[static_cast<size_t>(later)])) return (*grid)[static_cast<size_t>(later)];
            if (earlier >= 0 && isFree((*grid)[static_cast<size_t>(earlier)])) {
                return (*grid)[static_cast<size_t>(earlier)];
            }
        }
    }
    return ideal;
}

// =============================================================================
// Ruler (design 2.2)
// =============================================================================

/// A ruler label and its box in view x (design 2.2).
struct RulerLabel {
    std::string text;
    float left = 0.0f;
    float right = 0.0f;
    bool rightAligned = false;  ///< the label at ratio 1 (or a ceiling label near it) ends 2 px left of its line
};

/// The width of @p text in value-font (11 pt): digits and letters about 6.2 px, '.' and ' ' about 3.1 px.
[[nodiscard]] inline float estimateLabelWidth(const std::string& text) noexcept {
    float width = 0.0f;
    for (const char c : text) width += (c == '.' || c == ' ') ? 3.1f : 6.2f;
    return width;
}

/// The ruler's labels (design 2.2), ascending in x. Synced: beat k of delay at beat line k ("1" .. "4" for 1/1; the
/// two sixteenth lines "0.25", "0.5" for 1/8). Free: every 500 ms "0.5" .. "4.5" and "5.0 s" at ratio 1. A label's
/// left edge sits 2 px right of its line, the one at ratio 1 ends 2 px left of it; the end label and the "5 s"
/// ceiling label are placed first, and a label coming within 4 px of a placed one is skipped.
[[nodiscard]] inline std::vector<RulerLabel> rulerLabels(CustomLength length, double bpm) {
    struct Candidate {
        float ratio;
        std::string text;
    };
    std::vector<Candidate> candidates;  // the end label first
    char buffer[32];
    if (length == CustomLength::Free) {
        candidates.push_back({1.0f, "5.0 s"});
        for (int k = 1; k <= 9; ++k) {
            std::snprintf(buffer, sizeof(buffer), "%.1f", 0.5 * static_cast<double>(k));
            candidates.push_back({static_cast<float>(k) / 10.0f, buffer});
        }
    } else {
        const double span = spanBeats(length, bpm);
        if (span < 1.0 - 1e-6) {
            // Shorter than a beat (1/8): the labels sit at the sixteenth lines, in beats of delay
            const auto sixteenths = detail::snapRatios(0.25, span);
            for (auto it = sixteenths.rbegin(); it != sixteenths.rend(); ++it) {
                std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(*it) * span);
                candidates.push_back({*it, buffer});
            }
        } else {
            const int beats = static_cast<int>(std::lround(span));
            candidates.push_back({1.0f, std::to_string(beats)});
            for (int k = 1; k < beats; ++k) {
                candidates.push_back({static_cast<float>(static_cast<double>(k) / span), std::to_string(k)});
            }
        }
    }

    const auto boxOf = [](float ratio, const std::string& text, bool alignRight) {
        const float x = timeRatioToX(ratio);
        const float width = estimateLabelWidth(text);
        RulerLabel label{text, x + 2.0f, x + 2.0f + width, false};
        if (alignRight || label.right > kPlotRight - 2.0f) {
            label.right = x - 2.0f;
            label.left = label.right - width;
            label.rightAligned = true;
        }
        return label;
    };
    std::vector<RulerLabel> placed;
    const auto tryPlace = [&placed](const RulerLabel& label) {
        for (const auto& other : placed) {
            if (label.left < other.right + 4.0f && label.right > other.left - 4.0f) return;
        }
        placed.push_back(label);
    };

    tryPlace(boxOf(candidates.front().ratio, candidates.front().text, true));
    const float playable = playableFraction(length, bpm);
    if (playable < 1.0f) {
        tryPlace(boxOf(playable, "5 s", false));
    }
    for (size_t i = 1; i < candidates.size(); ++i) {
        tryPlace(boxOf(candidates[i].ratio, candidates[i].text, false));
    }
    std::sort(placed.begin(), placed.end(),
              [](const RulerLabel& a, const RulerLabel& b) { return a.left < b.left; });
    return placed;
}

// =============================================================================
// Overlap (design 5.5)
// =============================================================================

/// Taps whose centres lie within 8 px of each other share one number label (design 5.5): groups ascending in x,
/// each one's indices ascending.
[[nodiscard]] inline std::vector<std::vector<size_t>> labelGroups(const float* ratios, size_t count) {
    std::vector<std::vector<size_t>> groups;
    if (ratios == nullptr || count == 0) return groups;
    count = std::min(count, kMaxPatternTaps);
    std::vector<size_t> order(count);
    for (size_t i = 0; i < count; ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [ratios](size_t a, size_t b) {
        return timeRatioToX(ratios[a]) < timeRatioToX(ratios[b]);
    });
    float previousX = 0.0f;
    for (size_t k = 0; k < count; ++k) {
        const size_t index = order[k];
        const float x = timeRatioToX(ratios[index]);
        if (k == 0 || x - previousX >= kCollisionRatio * kPlotWidth) {
            groups.emplace_back();
        }
        groups.back().push_back(index);
        previousX = x;
    }
    for (auto& group : groups) std::sort(group.begin(), group.end());
    return groups;
}

// =============================================================================
// Readout (design 4.3)
// =============================================================================

/// The core-time format of @p ms: "%.1f ms" below 1000 ms, "%.2f s" from 1000 ms.
[[nodiscard]] inline std::string formatCoreTime(float ms) {
    char buffer[32];
    if (ms < 999.95f) {
        std::snprintf(buffer, sizeof(buffer), "%.1f ms", static_cast<double>(ms));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.2f s", static_cast<double>(ms) / 1000.0);
    }
    return buffer;
}

/// "%.1f dB" of @p level, "-inf dB" at or below the DSP's silence.
[[nodiscard]] inline std::string formatLevelDb(float level) {
    if (level <= kSilentLevel) return "-inf dB";
    const float dB = Krate::DSP::gainToDb(level);
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.1f dB", static_cast<double>(std::abs(dB) < 0.05f ? 0.0f : dB));
    return buffer;
}

/// The tap's delay in snap units, "k × <unit>" (the end line that is not a multiple: "end"), when Snap is on, Shift
/// is not held and the tap lies on a snap line; empty otherwise (design 4.3).
[[nodiscard]] inline std::string gridLabel(float ratio, CustomLength length, SnapDivision snap, double bpm,
                                           bool shift) {
    if (snap == SnapDivision::Off || shift) return {};
    const SnapDivision unit = effectiveSnapUnit(length, snap, bpm);
    const double unitBeats = snapUnitBeats(unit);
    const double span = spanBeats(length, bpm);
    const auto multiples = detail::unitMultiples(unitBeats, span);
    std::string name = snapDivisionName(unit);
    for (size_t k = 0; k < multiples.size(); ++k) {
        if (std::abs(multiples[k] - ratio) < 1e-4f) return std::to_string(k + 1) + " \xC3\x97 " + name;
    }
    if (std::abs(ratio - 1.0f) < 1e-4f) {
        const double count = span / unitBeats;
        const double whole = std::round(count);
        if (std::abs(count - whole) < 1e-4) return std::to_string(static_cast<long long>(whole)) + " \xC3\x97 " + name;
        return "end";
    }
    return {};
}

/// The hover / drag readout "<grid> · <time> · <level>" (design 4.3); the grid field and its separator are omitted
/// when gridLabel is empty. The time is ratio x span at @p bpm, inside the DSP's [1, 5000] ms.
[[nodiscard]] inline std::string formatTapReadout(float ratio, float level, CustomLength length, SnapDivision snap,
                                                  double bpm, bool shift) {
    const std::string dot = " \xC2\xB7 ";
    const float ms = std::clamp(clampRatio(ratio) * spanMs(length, bpm), Krate::DSP::MultiTapDelay::kMinDelayMs,
                                Krate::DSP::kCustomFreeLengthMs);
    std::string text = formatCoreTime(ms) + dot + formatLevelDb(level);
    const std::string grid = gridLabel(ratio, length, snap, bpm, shift);
    return grid.empty() ? text : grid + dot + text;
}

} // namespace Iterum
