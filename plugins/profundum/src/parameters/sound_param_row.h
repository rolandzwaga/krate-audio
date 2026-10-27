#pragma once

// ==============================================================================
// Profundum - Sound parameter row type, leaf words and mapping laws
// Phase 11 T008 (FR-002, FR-003, FR-006, FR-011, FR-012; plan 4.1, 4.3)
// ==============================================================================
// One E-parameter (a morphed sound control) is one SoundParamRow that names its
// leaf in the MorphState field table (BassMorphEngine::kLawTable,
// bass_morph_engine.h:74). Every range, law, log offset and enumerator count is
// DERIVED from that table (FR-002): no morphed range literal lives here or in
// any pack.
//
// Each leaf is stored as one 32-bit word: float -> its bit pattern, enum -> its
// underlying uint8, bool -> 0 / 1. The leaf order is the DSP visitor's
// (morph_state.h:334), which leavesMatchTable() already asserts against the
// table (morph_state.h:440), so kDefaultLeafBits cannot drift from MorphState{}.
//
// The generic row path (writeSoundRow / registerSoundRow / formatSoundRow) is
// applied by the nine section packs' thin wrappers (plan 4.2). Header-only.
// ==============================================================================

#include "ui/parameter_helpers.h"  // plugins/shared/src/ui/parameter_helpers.h:80, :85, :118

#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <krate/dsp/core/note_value.h>
#include <krate/dsp/systems/bass_morph_engine.h>
#include <krate/dsp/systems/morph_state.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>

namespace Profundum {

using MorphState = Krate::DSP::MorphState;
using FieldInfo = Krate::DSP::MorphState::FieldInfo;

inline constexpr std::size_t kNumLeaves = Krate::DSP::kMorphLeafFieldCount;  // 64 (morph_state.h; Phase 15: + 2)
inline constexpr std::uint8_t kNoLeaf = 0xFF;
using LeafBits = std::array<std::uint32_t, kNumLeaves>;

enum class DisplayUnit : std::uint8_t { List, Percent, BipolarPercent, Time, Hz, Db, Semitone, Mass100, Degrees };

/// One E-parameter. A Sync Rate row (809, 907) carries two leaves: note in `leaf`, modifier in `leaf2` (FR-012).
struct SoundParamRow {
    Steinberg::Vst::ParamID id;
    std::uint8_t leaf;                           // kLawTable index
    std::uint8_t leaf2;                          // the modifier leaf of a Sync Rate pair, else kNoLeaf
    const Steinberg::Vst::TChar* title;
    const Steinberg::Vst::TChar* units;          // ParameterInfo::units
    const Steinberg::Vst::TChar* const* labels;  // list rows; nullptr for continuous
    std::uint8_t labelCount;                     // 0 for continuous
    DisplayUnit display;
};

/// The table row of a leaf (precondition: leaf < kNumLeaves).
[[nodiscard]] constexpr const FieldInfo& lawOf(std::uint8_t leaf) noexcept {
    return Krate::DSP::BassMorphEngine::kLawTable[leaf];  // bass_morph_engine.h:74
}

// ------------------------------------------------------------------------------
// Leaf words
// ------------------------------------------------------------------------------

/// One 32-bit word per leaf: float -> bit pattern, enum -> underlying uint8, bool -> 0/1.
[[nodiscard]] constexpr LeafBits leafBitsOf(const MorphState& s) noexcept {
    LeafBits bits{};
    Krate::DSP::detail::morph_state_io::forEachMorphLeaf(
        [&bits]([[maybe_unused]] auto leaf, const auto& v) {
            using T = std::remove_cvref_t<decltype(v)>;
            constexpr std::size_t i = decltype(leaf)::value;
            if constexpr (std::is_same_v<T, float>) {
                bits[i] = std::bit_cast<std::uint32_t>(v);
            } else if constexpr (std::is_same_v<T, bool>) {
                bits[i] = v ? 1u : 0u;
            } else {
                bits[i] = static_cast<std::uint32_t>(static_cast<std::underlying_type_t<T>>(v));
            }
        },
        s);
    return bits;
}

/// The inverse; the caller guarantees each word is a valid encoding for its leaf.
[[nodiscard]] constexpr MorphState morphStateFromLeafBits(const LeafBits& b) noexcept {
    MorphState s{};
    Krate::DSP::detail::morph_state_io::forEachMorphLeaf(
        [&b]([[maybe_unused]] auto leaf, auto& v) {
            using T = std::remove_cvref_t<decltype(v)>;
            constexpr std::size_t i = decltype(leaf)::value;
            if constexpr (std::is_same_v<T, float>) {
                v = std::bit_cast<float>(b[i]);
            } else if constexpr (std::is_same_v<T, bool>) {
                v = b[i] != 0u;
            } else {
                v = static_cast<T>(static_cast<std::underlying_type_t<T>>(b[i]));
            }
        },
        s);
    return s;
}

/// FR-003: the engine's defaults, as leaf words (compile-time).
inline constexpr LeafBits kDefaultLeafBits = leafBitsOf(MorphState{});

// ------------------------------------------------------------------------------
// Mapping laws (FR-002; plan 4.1 table). All in DOUBLE: SC-006's 1e-9 is
// unreachable through a float.
// ------------------------------------------------------------------------------

/// Non-leaf continuous laws (Portamento, Morph Ramp Time, the performance parameters).
struct PlainLaw {
    double lo;
    double hi;
    double logOffset;
    bool logarithmic;
};

/// Explicit endpoint snaps: v <= 0 (including -0 and NaN) -> lo, v >= 1 -> hi, independent of pow rounding.
[[nodiscard]] inline double plainFromNormalized(const PlainLaw& l, double v) noexcept {
    if (!(v > 0.0)) {
        return l.lo;
    }
    if (v >= 1.0) {
        return l.hi;
    }
    if (!l.logarithmic) {
        return l.lo + v * (l.hi - l.lo);
    }
    if (l.logOffset == 0.0) {
        return Krate::Plugins::logMapFromNormalized(v, l.lo, l.hi);  // parameter_helpers.h:80
    }
    const double o = l.logOffset;
    return (l.lo + o) * std::pow((l.hi + o) / (l.lo + o), v) - o;
}

/// Inverse of plainFromNormalized; lo maps to exactly 0 and hi to exactly 1.
[[nodiscard]] inline double normalizedFromPlain(const PlainLaw& l, double plain) noexcept {
    if (!(plain > l.lo)) {
        return 0.0;
    }
    if (plain >= l.hi) {
        return 1.0;
    }
    if (!l.logarithmic) {
        return std::clamp((plain - l.lo) / (l.hi - l.lo), 0.0, 1.0);
    }
    if (l.logOffset == 0.0) {
        return Krate::Plugins::logMapToNormalized(plain, l.lo, l.hi);  // parameter_helpers.h:85
    }
    const double o = l.logOffset;
    return std::clamp(std::log((plain + o) / (l.lo + o)) / std::log((l.hi + o) / (l.lo + o)), 0.0, 1.0);
}

/// min(maxIndex, int(v * maxIndex + 0.5)) with v clamped to [0, 1] (NaN -> 0).
[[nodiscard]] constexpr int indexFromNormalized(double v, int maxIndex) noexcept {
    const double c = v > 0.0 ? (v < 1.0 ? v : 1.0) : 0.0;
    const int idx = static_cast<int>(c * static_cast<double>(maxIndex) + 0.5);
    return idx < maxIndex ? idx : maxIndex;
}

/// The continuous law of an F32 leaf (Linear and EqualPower knobs are linear, FR-002).
[[nodiscard]] constexpr PlainLaw plainLawOf(const FieldInfo& f) noexcept {
    return PlainLaw{static_cast<double>(f.lo), static_cast<double>(f.hi), static_cast<double>(f.logOffset),
                    f.law == MorphState::Law::Logarithmic};
}

/// F32 rows: normalised -> plain. Stepped rows: the rounded index.
[[nodiscard]] inline double plainFromNormalized(const FieldInfo& f, double v) noexcept {
    if (f.law == MorphState::Law::Stepped) {
        return static_cast<double>(indexFromNormalized(v, static_cast<int>(f.maxEnum)));
    }
    return plainFromNormalized(plainLawOf(f), v);
}

/// F32 rows: plain -> normalised. Stepped rows: index / maxEnum.
[[nodiscard]] inline double normalizedFromPlain(const FieldInfo& f, double plain) noexcept {
    if (f.law == MorphState::Law::Stepped) {
        const double maxEnum = static_cast<double>(f.maxEnum);
        return std::clamp(std::round(plain), 0.0, maxEnum) / maxEnum;
    }
    return normalizedFromPlain(plainLawOf(f), plain);
}

/// Any leaf: normalised -> stored word. F32: the float cast, with an open hi pulled to the float just below it
/// (motion.cyclePhaseDeg, leaf 45, only). Enum8 / Bool: the rounded index.
[[nodiscard]] inline std::uint32_t leafWordFromNormalized(std::uint8_t leaf, double v) noexcept {
    const FieldInfo& f = lawOf(leaf);
    if (f.kind != MorphState::Kind::F32) {
        return static_cast<std::uint32_t>(indexFromNormalized(v, static_cast<int>(f.maxEnum)));
    }
    float x = static_cast<float>(plainFromNormalized(f, v));
    if (f.hiOpen && !(x < f.hi)) {
        x = std::nextafter(f.hi, f.lo);
    }
    return std::bit_cast<std::uint32_t>(x);
}

/// Any leaf: stored word -> normalised.
[[nodiscard]] inline double normalizedFromLeafWord(std::uint8_t leaf, std::uint32_t word) noexcept {
    const FieldInfo& f = lawOf(leaf);
    if (f.kind != MorphState::Kind::F32) {
        const std::uint32_t idx = std::min(word, static_cast<std::uint32_t>(f.maxEnum));
        return static_cast<double>(idx) / static_cast<double>(f.maxEnum);
    }
    return normalizedFromPlain(f, static_cast<double>(std::bit_cast<float>(word)));
}

/// (note, modifier) words -> kNoteValueDropdownMapping index (note_value.h:136; a bijection over the 30 entries).
/// -1 if the pair is in no entry (never for words the plugin writes).
[[nodiscard]] constexpr int syncRateIndexOf(std::uint32_t noteWord, std::uint32_t modWord) noexcept {
    for (int i = 0; i < Krate::DSP::kNoteValueDropdownCount; ++i) {
        const auto& m = Krate::DSP::kNoteValueDropdownMapping[static_cast<std::size_t>(i)];
        if (static_cast<std::uint32_t>(m.note) == noteWord && static_cast<std::uint32_t>(m.modifier) == modWord) {
            return i;
        }
    }
    return -1;
}

/// FR-003 / Risk R2: the registered default of an E-leaf. normalizedFromLeafWord of the default word, then a
/// bounded std::nextafter nudge (at most 8 steps each way) until it denormalises to the default word bit-for-bit.
/// SC-006 DefaultLeafBitsRoundTrip pins the equality for all 62 leaves.
[[nodiscard]] inline double defaultNormalizedOf(std::uint8_t leaf) noexcept {
    const std::uint32_t want = kDefaultLeafBits[leaf];
    const double n = normalizedFromLeafWord(leaf, want);
    if (leafWordFromNormalized(leaf, n) == want) {
        return n;
    }
    double up = n;
    double down = n;
    for (int step = 0; step < 8; ++step) {
        up = std::min(std::nextafter(up, 2.0), 1.0);
        if (leafWordFromNormalized(leaf, up) == want) {
            return up;
        }
        down = std::max(std::nextafter(down, -1.0), 0.0);
        if (leafWordFromNormalized(leaf, down) == want) {
            return down;
        }
    }
    return n;  // unreachable for the shipped table; the SC-006 section fails loudly if it ever is
}

// ------------------------------------------------------------------------------
// Endpoint slots (plan 4.3). The processor holds two (A, B); the audio thread
// assembles a MorphState from a dirty one (FR-013), the sender reports a
// syncPending one to the controller (FR-025).
// ------------------------------------------------------------------------------

struct EndpointParams {
    std::array<std::atomic<std::uint32_t>, kNumLeaves> leaf;
    std::atomic<bool> dirty{true};         // audio: assemble + setEndpoint
    std::atomic<bool> syncPending{false};  // sender: report to the controller (FR-025)

    EndpointParams() noexcept { store(kDefaultLeafBits); }

    /// 62 relaxed loads.
    [[nodiscard]] LeafBits load() const noexcept {
        LeafBits b{};
        for (std::size_t i = 0; i < kNumLeaves; ++i) {
            b[i] = leaf[i].load(std::memory_order_relaxed);
        }
        return b;
    }

    /// 62 relaxed stores.
    void store(const LeafBits& b) noexcept {
        for (std::size_t i = 0; i < kNumLeaves; ++i) {
            leaf[i].store(b[i], std::memory_order_relaxed);
        }
    }
};

// ------------------------------------------------------------------------------
// Generic row path (called only through the nine pack wrappers, plan 4.2)
// ------------------------------------------------------------------------------

/// FR-011 / FR-012: writes 1 leaf word (2 for a Sync Rate pair), then sets dirty + syncPending.
/// v is already finite and in [0, 1] (processParameterChanges). Real-time safe.
inline void writeSoundRow(EndpointParams& target, const SoundParamRow& row, double v) noexcept {
    if (row.leaf2 != kNoLeaf) {
        const int idx = indexFromNormalized(v, Krate::DSP::kNoteValueDropdownCount - 1);
        const auto& m = Krate::DSP::kNoteValueDropdownMapping[static_cast<std::size_t>(idx)];
        target.leaf[row.leaf].store(static_cast<std::uint32_t>(static_cast<std::uint8_t>(m.note)),
                                    std::memory_order_relaxed);
        target.leaf[row.leaf2].store(static_cast<std::uint32_t>(static_cast<std::uint8_t>(m.modifier)),
                                     std::memory_order_relaxed);
    } else {
        target.leaf[row.leaf].store(leafWordFromNormalized(row.leaf, v), std::memory_order_relaxed);
    }
    target.dirty.store(true, std::memory_order_release);
    target.syncPending.store(true, std::memory_order_release);
}

/// FR-001 / FR-003 / FR-004. List rows through the label array with defaultNormalizedValue set explicitly
/// (createDropdownParameterWithDefault sets only the current value, parameter_helpers.h:118-131); continuous rows
/// as a Parameter with stepCount 0, kCanAutomate, the row's units and defaultNormalizedOf(leaf).
inline void registerSoundRow(Steinberg::Vst::ParameterContainer& parameters, const SoundParamRow& row) {
    using namespace Steinberg::Vst;

    if (row.labels != nullptr && row.labelCount > 0) {
        int defaultIndex = 0;
        double defaultNorm = 0.0;
        if (row.leaf2 != kNoLeaf) {
            defaultIndex = syncRateIndexOf(kDefaultLeafBits[row.leaf], kDefaultLeafBits[row.leaf2]);
            defaultIndex = std::max(defaultIndex, 0);
            defaultNorm = static_cast<double>(defaultIndex) / static_cast<double>(row.labelCount - 1);
        } else {
            defaultIndex = static_cast<int>(kDefaultLeafBits[row.leaf]);
            defaultNorm = defaultNormalizedOf(row.leaf);
        }
        auto* list = Krate::Plugins::createDropdownParameterWithDefault(
            row.title, row.id, static_cast<std::int32_t>(defaultIndex), row.labels, static_cast<int>(row.labelCount));
        list->getInfo().defaultNormalizedValue = defaultNorm;
        parameters.addParameter(list);
        return;
    }

    parameters.addParameter(row.title, row.units, 0, defaultNormalizedOf(row.leaf), ParameterInfo::kCanAutomate,
                            static_cast<Steinberg::int32>(row.id));
}

/// FR-006 display table (plan 4.3). ASCII only. List rows return kResultFalse (the StringListParameter formats
/// itself). The displayed value is the stored float (the word leafWordFromNormalized writes).
inline Steinberg::tresult formatSoundRow(const SoundParamRow& row, double v, Steinberg::Vst::String128 string) {
    using namespace Steinberg;

    if (row.display == DisplayUnit::List || row.leaf2 != kNoLeaf || lawOf(row.leaf).kind != MorphState::Kind::F32) {
        return kResultFalse;
    }

    const double x = static_cast<double>(std::bit_cast<float>(leafWordFromNormalized(row.leaf, v)));
    char8 text[64];
    switch (row.display) {
        case DisplayUnit::Time: {
            const double r = std::round(10.0 * x) / 10.0;
            if (r < 1000.0) {
                std::snprintf(text, sizeof(text), "%.1f ms", x);
            } else {
                std::snprintf(text, sizeof(text), "%.2f s", x / 1000.0);
            }
            break;
        }
        case DisplayUnit::Hz: {
            const double r = std::round(100.0 * x) / 100.0;
            if (r < 1.0) {
                std::snprintf(text, sizeof(text), "%.2f Hz", x);
            } else {
                std::snprintf(text, sizeof(text), "%.1f Hz", x);
            }
            break;
        }
        case DisplayUnit::Db:
            std::snprintf(text, sizeof(text), "%+.1f dB", x + 0.0);
            break;
        case DisplayUnit::Semitone:
            std::snprintf(text, sizeof(text), "%.1f st", x);
            break;
        case DisplayUnit::Percent:
            std::snprintf(text, sizeof(text), "%.0f%%", 100.0 * x);
            break;
        case DisplayUnit::BipolarPercent:
            std::snprintf(text, sizeof(text), "%+.0f%%", 100.0 * x);
            break;
        case DisplayUnit::Mass100:
            std::snprintf(text, sizeof(text), "%.0f", x);
            break;
        case DisplayUnit::Degrees:
            std::snprintf(text, sizeof(text), "%.0f deg", x);
            break;
        case DisplayUnit::List:
        default:
            return kResultFalse;
    }
    UString(string, 128).fromAscii(text);
    return kResultOk;
}

}  // namespace Profundum
