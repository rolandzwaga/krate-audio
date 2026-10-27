#pragma once

// ==============================================================================
// Profundum - Morph Modulation pack (IDs 1003-1011, state v3 bytes 574-609)
// Phase 14 T011 (FR-020 - FR-025; C-7, C-10, C-12; plan 4.5)
// ==============================================================================
//   1003 Morph Mod Source       list 5 "Off, Envelope, Cycle, Velocity, Mod Wheel"; default Off
//   1004 Morph Mod Amount       [-1, 1] linear, bipolar % display; default 0 (normalised 0.5)
//   1005 Morph Env Attack       [0.1, 10000] ms log; default 10 ms (the 913 law, lawOf(50))
//   1006 Morph Env Decay        [0.1, 10000] ms log; default 300 ms
//   1007 Morph Env Sustain      [0, 1] linear, %; default 0
//   1008 Morph Cycle Shape      list: the six kMotionCycleShapeLabels; default Sine
//   1009 Morph Cycle Rate       [0.01, 20] Hz log; default 1 Hz (the 904 law, lawOf(40))
//   1010 Morph Cycle Sync       list "Off, On"; default Off
//   1011 Morph Cycle Sync Rate  list: the 30 kNoteValueDropdownStrings; default index 13 (1/4)
//
// Stream (C-10, 36 bytes, little-endian, after the two name blocks): source i32, amount f32, attack ms f32,
// decay ms f32, sustain f32, cycle shape i32, cycle rate Hz f32, cycle sync i32, cycle sync rate i32.
//
// None of the nine is an endpoint (sound) parameter (FR-018): they are not kSoundRows rows, they route through
// RouteKind::MorphMod, and Edit Target / Copy A <-> B never touch them. Header-only.
// ==============================================================================

#include "plugin_ids.h"
#include "parameters/motion_params.h"   // kMotionCycleShapeLabels, kMotionCycleSyncLabels (motion_params.h:41-43)
#include "parameters/sound_param_row.h" // PlainLaw, plainFromNormalized, normalizedFromPlain, indexFromNormalized, lawOf
#include "ui/note_value_ui.h"           // plugins/shared/src/ui/note_value_ui.h:21 kNoteValueDropdownStrings
#include "ui/parameter_helpers.h"       // plugins/shared/src/ui/parameter_helpers.h:118

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/note_value.h>
#include <krate/dsp/primitives/envelope_utils.h>
#include <krate/dsp/primitives/lfo.h>
#include <krate/dsp/systems/profundum_morph_modulator.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>

namespace Profundum {

using MorphMod = Krate::DSP::ProfundumMorphModulator;

// ------------------------------------------------------------------------------
// Lists (C-3, C-12)
// ------------------------------------------------------------------------------

inline constexpr int kNumMorphModSources = 5;  // Off, Envelope, Cycle, Velocity, Mod Wheel
inline constexpr std::array<const Steinberg::Vst::TChar*, kNumMorphModSources> kMorphModSourceLabels{
    STR16("Off"), STR16("Envelope"), STR16("Cycle"), STR16("Velocity"), STR16("Mod Wheel")};
inline constexpr int kMorphModSourceModWheel = static_cast<int>(MorphMod::Source::ModWheel);  // 4
inline constexpr int kNumMorphCycleShapes = 6;
inline constexpr int kNumMorphCycleSyncStates = 2;
inline constexpr int kDefaultMorphModSyncRateIndex = 13;  // {Quarter, None} (note_value.h:155)

static_assert(static_cast<int>(MorphMod::Source::Off) == 0 && static_cast<int>(MorphMod::Source::Envelope) == 1 &&
                  static_cast<int>(MorphMod::Source::Cycle) == 2 && static_cast<int>(MorphMod::Source::Velocity) == 3 &&
                  kMorphModSourceModWheel == kNumMorphModSources - 1,
              "C-3: the 1003 list index is the ProfundumMorphModulator::Source enumerator");
static_assert(std::size(kMotionCycleShapeLabels) == static_cast<std::size_t>(kNumMorphCycleShapes) &&
                  static_cast<int>(Krate::DSP::Waveform::SmoothRandom) == kNumMorphCycleShapes - 1,
              "C-12: 1008 lists the six LFO waveforms in enumerator order (lfo.h:47-54)");
static_assert(std::size(kMotionCycleSyncLabels) == static_cast<std::size_t>(kNumMorphCycleSyncStates),
              "C-12: 1010 is Off / On");
static_assert(std::size(Krate::Plugins::Parameters::kNoteValueDropdownStrings) ==
                  static_cast<std::size_t>(Krate::DSP::kNoteValueDropdownCount),
              "C-12: 1011 lists the 30 dropdown note values");
static_assert(Krate::DSP::kNoteValueDropdownMapping[kDefaultMorphModSyncRateIndex].note ==
                      MorphMod::Config{}.cycleNote &&
                  Krate::DSP::kNoteValueDropdownMapping[kDefaultMorphModSyncRateIndex].modifier ==
                      MorphMod::Config{}.cycleModifier,
              "C-12: the 1011 default index 13 is the modulator's default {Quarter, None}");

// ------------------------------------------------------------------------------
// Laws (C-12). The time and rate laws are the 913 / 904 laws, so 1005 / 1009 map exactly as those do.
// ------------------------------------------------------------------------------

inline constexpr PlainLaw kMorphModAmountLaw{-1.0, 1.0, 0.0, false};
inline constexpr PlainLaw kMorphModTimeLaw{static_cast<double>(Krate::DSP::kMinEnvelopeTimeMs),
                                           static_cast<double>(Krate::DSP::kMaxEnvelopeTimeMs), 0.0, true};
inline constexpr PlainLaw kMorphModSustainLaw{0.0, 1.0, 0.0, false};
inline constexpr PlainLaw kMorphModRateLaw{static_cast<double>(Krate::DSP::kMinFrequency),
                                           static_cast<double>(Krate::DSP::kMaxFrequency), 0.0, true};

namespace morph_mod_detail {

[[nodiscard]] constexpr bool sameLaw(const PlainLaw& a, const PlainLaw& b) noexcept {
    return a.lo == b.lo && a.hi == b.hi && a.logOffset == b.logOffset && a.logarithmic == b.logarithmic;
}

}  // namespace morph_mod_detail

static_assert(morph_mod_detail::sameLaw(kMorphModTimeLaw, plainLawOf(lawOf(50))) &&
                  morph_mod_detail::sameLaw(kMorphModTimeLaw, plainLawOf(lawOf(51))),
              "plan 4.5: 1005 / 1006 use the motion.envAttackMs / envDecayMs law (morph_state.h:282-285)");
static_assert(morph_mod_detail::sameLaw(kMorphModRateLaw, plainLawOf(lawOf(40))),
              "plan 4.5: 1009 uses the motion.cycleRateHz law (morph_state.h:257-259)");
static_assert(morph_mod_detail::sameLaw(kMorphModAmountLaw, plainLawOf(lawOf(38))),
              "plan 4.5: 1004 uses the motion.amount law (morph_state.h:253-254)");

// ------------------------------------------------------------------------------
// Defaults (C-12): the modulator's own Config{} defaults
// ------------------------------------------------------------------------------

inline constexpr int kDefaultMorphModSource = 0;  // Off
inline constexpr float kDefaultMorphModAmount = 0.0f;
inline constexpr float kDefaultMorphModAttackMs = 10.0f;
inline constexpr float kDefaultMorphModDecayMs = 300.0f;
inline constexpr float kDefaultMorphModSustain = 0.0f;
inline constexpr int kDefaultMorphModCycleShape = 0;  // Sine
inline constexpr float kDefaultMorphModCycleRateHz = 1.0f;
inline constexpr int kDefaultMorphModCycleSync = 0;  // Off

static_assert(MorphMod::Config{}.source == MorphMod::Source::Off && MorphMod::Config{}.amount == kDefaultMorphModAmount &&
                  MorphMod::Config{}.envAttackMs == kDefaultMorphModAttackMs &&
                  MorphMod::Config{}.envDecayMs == kDefaultMorphModDecayMs &&
                  MorphMod::Config{}.envSustain == kDefaultMorphModSustain &&
                  MorphMod::Config{}.cycleShape == Krate::DSP::Waveform::Sine &&
                  MorphMod::Config{}.cycleRateHz == kDefaultMorphModCycleRateHz && !MorphMod::Config{}.cycleSync,
              "C-12: the registered defaults are the modulator's Config{} defaults");

// ------------------------------------------------------------------------------
// Processor atomics (FR-020)
// ------------------------------------------------------------------------------

struct MorphModParams {
    std::atomic<int> source{kDefaultMorphModSource};
    std::atomic<float> amount{kDefaultMorphModAmount};
    std::atomic<float> attackMs{kDefaultMorphModAttackMs};
    std::atomic<float> decayMs{kDefaultMorphModDecayMs};
    std::atomic<float> sustain{kDefaultMorphModSustain};
    std::atomic<int> cycleShape{kDefaultMorphModCycleShape};
    std::atomic<float> cycleRateHz{kDefaultMorphModCycleRateHz};
    std::atomic<int> cycleSync{kDefaultMorphModCycleSync};
    std::atomic<int> cycleSyncRate{kDefaultMorphModSyncRateIndex};
};

namespace morph_mod_detail {

/// Normalised -> stored float of a continuous law, clamped to the law box.
[[nodiscard]] inline float plainOf(const PlainLaw& law, double v) noexcept {
    return std::clamp(static_cast<float>(plainFromNormalized(law, v)), static_cast<float>(law.lo),
                      static_cast<float>(law.hi));
}

/// A finite float clamps to the law box; a non-finite one is not stored (returns false).
[[nodiscard]] inline bool sanitizeStored(float x, const PlainLaw& law, float& out) noexcept {
    if (!Krate::DSP::detail::isFinite(x)) {
        return false;
    }
    out = std::clamp(x, static_cast<float>(law.lo), static_cast<float>(law.hi));
    return true;
}

/// The registered default of a continuous row: normalizedFromPlain of the plain default, then a bounded
/// std::nextafter nudge (at most 8 steps each way) until it denormalises to the plain default bit-for-bit
/// (the defaultNormalizedOf rule, sound_param_row.h:233-252).
[[nodiscard]] inline double defaultNormalizedFor(const PlainLaw& law, float plain) noexcept {
    const auto want = std::bit_cast<std::uint32_t>(plain);
    const auto hits = [&law, want](double n) noexcept {
        return std::bit_cast<std::uint32_t>(static_cast<float>(plainFromNormalized(law, n))) == want;
    };
    const double n = normalizedFromPlain(law, static_cast<double>(plain));
    if (hits(n)) {
        return n;
    }
    double up = n;
    double down = n;
    for (int step = 0; step < 8; ++step) {
        up = std::min(std::nextafter(up, 2.0), 1.0);
        if (hits(up)) {
            return up;
        }
        down = std::max(std::nextafter(down, -1.0), 0.0);
        if (hits(down)) {
            return down;
        }
    }
    return n;
}

/// Time rule of formatSoundRow (sound_param_row.h:345-353).
inline void formatTime(double x, Steinberg::char8* text, std::size_t size) {
    const double r = std::round(10.0 * x) / 10.0;
    if (r < 1000.0) {
        std::snprintf(text, size, "%.1f ms", x);
    } else {
        std::snprintf(text, size, "%.2f s", x / 1000.0);
    }
}

/// Hz rule of formatSoundRow (sound_param_row.h:354-362).
inline void formatHz(double x, Steinberg::char8* text, std::size_t size) {
    const double r = std::round(100.0 * x) / 100.0;
    if (r < 1.0) {
        std::snprintf(text, size, "%.2f Hz", x);
    } else {
        std::snprintf(text, size, "%.1f Hz", x);
    }
}

}  // namespace morph_mod_detail

// ------------------------------------------------------------------------------
// Entry points
// ------------------------------------------------------------------------------

/// FR-020. value arrives already finite and clamped to [0, 1] (processParameterChanges). Relaxed stores.
/// Returns true if the ID is one of 1003-1011. Real-time safe.
inline bool handleMorphModParamChange(MorphModParams& params, Steinberg::Vst::ParamID id,
                                      Steinberg::Vst::ParamValue value) noexcept {
    using morph_mod_detail::plainOf;
    switch (id) {
        case kMorphModSourceId:
            params.source.store(indexFromNormalized(value, kNumMorphModSources - 1), std::memory_order_relaxed);
            return true;
        case kMorphModAmountId:
            params.amount.store(plainOf(kMorphModAmountLaw, value), std::memory_order_relaxed);
            return true;
        case kMorphModAttackId:
            params.attackMs.store(plainOf(kMorphModTimeLaw, value), std::memory_order_relaxed);
            return true;
        case kMorphModDecayId:
            params.decayMs.store(plainOf(kMorphModTimeLaw, value), std::memory_order_relaxed);
            return true;
        case kMorphModSustainId:
            params.sustain.store(plainOf(kMorphModSustainLaw, value), std::memory_order_relaxed);
            return true;
        case kMorphModCycleShapeId:
            params.cycleShape.store(indexFromNormalized(value, kNumMorphCycleShapes - 1), std::memory_order_relaxed);
            return true;
        case kMorphModCycleRateId:
            params.cycleRateHz.store(plainOf(kMorphModRateLaw, value), std::memory_order_relaxed);
            return true;
        case kMorphModCycleSyncId:
            params.cycleSync.store(indexFromNormalized(value, kNumMorphCycleSyncStates - 1), std::memory_order_relaxed);
            return true;
        case kMorphModCycleSyncRateId:
            params.cycleSyncRate.store(indexFromNormalized(value, Krate::DSP::kNoteValueDropdownCount - 1),
                                       std::memory_order_relaxed);
            return true;
        default:
            return false;
    }
}

/// FR-020 / C-12. Continuous rows kCanAutomate with the bit-exact default; lists kCanAutomate | kIsList with their
/// registered default pinned by hand (createDropdownParameterWithDefault sets only the current value,
/// parameter_helpers.h:118-131; the 1000 pattern, morph_params.h:255-260).
inline void registerMorphModParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    using morph_mod_detail::defaultNormalizedFor;

    const auto addList = [&parameters](const TChar* title, ParamID id, int defaultIndex, const TChar* const* labels,
                                       int count) {
        auto* list = Krate::Plugins::createDropdownParameterWithDefault(title, id, static_cast<std::int32_t>(defaultIndex),
                                                                        labels, count);
        list->getInfo().flags = ParameterInfo::kCanAutomate | ParameterInfo::kIsList;
        list->getInfo().defaultNormalizedValue = static_cast<double>(defaultIndex) / static_cast<double>(count - 1);
        parameters.addParameter(list);
    };
    const auto addContinuous = [&parameters](const TChar* title, const TChar* units, ParamID id, double defaultNorm) {
        parameters.addParameter(title, units, 0, defaultNorm, ParameterInfo::kCanAutomate,
                                static_cast<Steinberg::int32>(id));
    };

    addList(STR16("Morph Mod Source"), kMorphModSourceId, kDefaultMorphModSource, kMorphModSourceLabels.data(),
            kNumMorphModSources);
    addContinuous(STR16("Morph Mod Amount"), STR16("%"), kMorphModAmountId,
                  defaultNormalizedFor(kMorphModAmountLaw, kDefaultMorphModAmount));
    addContinuous(STR16("Morph Env Attack"), STR16("ms"), kMorphModAttackId,
                  defaultNormalizedFor(kMorphModTimeLaw, kDefaultMorphModAttackMs));
    addContinuous(STR16("Morph Env Decay"), STR16("ms"), kMorphModDecayId,
                  defaultNormalizedFor(kMorphModTimeLaw, kDefaultMorphModDecayMs));
    addContinuous(STR16("Morph Env Sustain"), STR16("%"), kMorphModSustainId,
                  defaultNormalizedFor(kMorphModSustainLaw, kDefaultMorphModSustain));
    addList(STR16("Morph Cycle Shape"), kMorphModCycleShapeId, kDefaultMorphModCycleShape, kMotionCycleShapeLabels,
            kNumMorphCycleShapes);
    addContinuous(STR16("Morph Cycle Rate"), STR16("Hz"), kMorphModCycleRateId,
                  defaultNormalizedFor(kMorphModRateLaw, kDefaultMorphModCycleRateHz));
    addList(STR16("Morph Cycle Sync"), kMorphModCycleSyncId, kDefaultMorphModCycleSync, kMotionCycleSyncLabels,
            kNumMorphCycleSyncStates);
    addList(STR16("Morph Cycle Sync Rate"), kMorphModCycleSyncRateId, kDefaultMorphModSyncRateIndex,
            Krate::Plugins::Parameters::kNoteValueDropdownStrings, Krate::DSP::kNoteValueDropdownCount);
}

/// FR-020 display (ASCII). The format strings of formatSoundRow (sound_param_row.h:345-374) on the stored float, so
/// 1004 / 1005 / 1009 print as 902 / 913 / 904 do; the Amount adds + 0.0 so the centre prints "+0%". The four lists
/// format themselves (kResultFalse), as does any other ID.
inline Steinberg::tresult formatMorphModParam(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
                                              Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    using morph_mod_detail::plainOf;

    char8 text[64];
    switch (id) {
        case kMorphModAmountId:
            std::snprintf(text, sizeof(text), "%+.0f%%",
                          100.0 * static_cast<double>(plainOf(kMorphModAmountLaw, value)) + 0.0);
            break;
        case kMorphModAttackId:
        case kMorphModDecayId:
            morph_mod_detail::formatTime(static_cast<double>(plainOf(kMorphModTimeLaw, value)), text, sizeof(text));
            break;
        case kMorphModSustainId:
            std::snprintf(text, sizeof(text), "%.0f%%", 100.0 * static_cast<double>(plainOf(kMorphModSustainLaw, value)));
            break;
        case kMorphModCycleRateId:
            morph_mod_detail::formatHz(static_cast<double>(plainOf(kMorphModRateLaw, value)), text, sizeof(text));
            break;
        default:
            return kResultFalse;
    }
    UString(string, 128).fromAscii(text);
    return kResultOk;
}

/// The engine config of the stored values; the Sync Rate index goes through kNoteValueDropdownMapping
/// (note_value.h:136). Relaxed loads; real-time safe.
[[nodiscard]] inline MorphMod::Config morphModConfigOf(const MorphModParams& params) noexcept {
    MorphMod::Config c{};
    c.source = static_cast<MorphMod::Source>(params.source.load(std::memory_order_relaxed));
    c.amount = params.amount.load(std::memory_order_relaxed);
    c.envAttackMs = params.attackMs.load(std::memory_order_relaxed);
    c.envDecayMs = params.decayMs.load(std::memory_order_relaxed);
    c.envSustain = params.sustain.load(std::memory_order_relaxed);
    c.cycleShape = static_cast<Krate::DSP::Waveform>(params.cycleShape.load(std::memory_order_relaxed));
    c.cycleRateHz = params.cycleRateHz.load(std::memory_order_relaxed);
    c.cycleSync = params.cycleSync.load(std::memory_order_relaxed) != 0;
    const int idx = std::clamp(params.cycleSyncRate.load(std::memory_order_relaxed), 0,
                               Krate::DSP::kNoteValueDropdownCount - 1);
    const auto& m = Krate::DSP::kNoteValueDropdownMapping[static_cast<std::size_t>(idx)];
    c.cycleNote = m.note;
    c.cycleModifier = m.modifier;
    return c;
}

/// C-10: the 36-byte block, in the order of the table.
inline void saveMorphModParams(const MorphModParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeInt32(static_cast<Steinberg::int32>(params.source.load(std::memory_order_relaxed)));
    streamer.writeFloat(params.amount.load(std::memory_order_relaxed));
    streamer.writeFloat(params.attackMs.load(std::memory_order_relaxed));
    streamer.writeFloat(params.decayMs.load(std::memory_order_relaxed));
    streamer.writeFloat(params.sustain.load(std::memory_order_relaxed));
    streamer.writeInt32(static_cast<Steinberg::int32>(params.cycleShape.load(std::memory_order_relaxed)));
    streamer.writeFloat(params.cycleRateHz.load(std::memory_order_relaxed));
    streamer.writeInt32(static_cast<Steinberg::int32>(params.cycleSync.load(std::memory_order_relaxed)));
    streamer.writeInt32(static_cast<Steinberg::int32>(params.cycleSyncRate.load(std::memory_order_relaxed)));
}

/// C-10 load rules (the loadMorphControlParams pattern, morph_params.h:312-346). EOF-safe: false at the first failed
/// read, and every field from the cut on keeps its prior value. A non-finite float is not stored, a finite one clamps
/// to its range; an out-of-range list index is not stored.
inline bool loadMorphModParams(MorphModParams& params, Steinberg::IBStreamer& streamer) {
    using morph_mod_detail::sanitizeStored;
    const auto readInt = [&streamer](std::atomic<int>& dst, int count) {
        Steinberg::int32 v = 0;
        if (!streamer.readInt32(v)) {
            return false;
        }
        if (v >= 0 && v < count) {
            dst.store(static_cast<int>(v), std::memory_order_relaxed);
        }
        return true;
    };
    const auto readFloat = [&streamer](std::atomic<float>& dst, const PlainLaw& law) {
        float v = 0.0f;
        if (!streamer.readFloat(v)) {
            return false;
        }
        float stored = 0.0f;
        if (sanitizeStored(v, law, stored)) {
            dst.store(stored, std::memory_order_relaxed);
        }
        return true;
    };
    return readInt(params.source, kNumMorphModSources) && readFloat(params.amount, kMorphModAmountLaw) &&
           readFloat(params.attackMs, kMorphModTimeLaw) && readFloat(params.decayMs, kMorphModTimeLaw) &&
           readFloat(params.sustain, kMorphModSustainLaw) && readInt(params.cycleShape, kNumMorphCycleShapes) &&
           readFloat(params.cycleRateHz, kMorphModRateLaw) && readInt(params.cycleSync, kNumMorphCycleSyncStates) &&
           readInt(params.cycleSyncRate, Krate::DSP::kNoteValueDropdownCount);
}

/// FR-024: the loadMorphModParams rules applied before normalising, then setParam(id, normalized). EOF-safe; returns
/// at the first failed read. A value the processor would not store makes NO call.
template <typename SetParamFunc>
inline void loadMorphModParamsToController(Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    using morph_mod_detail::sanitizeStored;
    const auto readInt = [&streamer, &setParam](Steinberg::Vst::ParamID id, int count) {
        Steinberg::int32 v = 0;
        if (!streamer.readInt32(v)) {
            return false;
        }
        if (v >= 0 && v < count) {
            setParam(id, static_cast<double>(v) / static_cast<double>(count - 1));
        }
        return true;
    };
    const auto readFloat = [&streamer, &setParam](Steinberg::Vst::ParamID id, const PlainLaw& law) {
        float v = 0.0f;
        if (!streamer.readFloat(v)) {
            return false;
        }
        float stored = 0.0f;
        if (sanitizeStored(v, law, stored)) {
            setParam(id, normalizedFromPlain(law, static_cast<double>(stored)));
        }
        return true;
    };
    static_cast<void>(readInt(kMorphModSourceId, kNumMorphModSources) &&
                      readFloat(kMorphModAmountId, kMorphModAmountLaw) &&
                      readFloat(kMorphModAttackId, kMorphModTimeLaw) && readFloat(kMorphModDecayId, kMorphModTimeLaw) &&
                      readFloat(kMorphModSustainId, kMorphModSustainLaw) &&
                      readInt(kMorphModCycleShapeId, kNumMorphCycleShapes) &&
                      readFloat(kMorphModCycleRateId, kMorphModRateLaw) &&
                      readInt(kMorphModCycleSyncId, kNumMorphCycleSyncStates) &&
                      readInt(kMorphModCycleSyncRateId, Krate::DSP::kNoteValueDropdownCount));
}

/// C-10: a v1 / v2 load resets the block to the defaults.
inline void resetMorphModParams(MorphModParams& params) noexcept {
    params.source.store(kDefaultMorphModSource, std::memory_order_relaxed);
    params.amount.store(kDefaultMorphModAmount, std::memory_order_relaxed);
    params.attackMs.store(kDefaultMorphModAttackMs, std::memory_order_relaxed);
    params.decayMs.store(kDefaultMorphModDecayMs, std::memory_order_relaxed);
    params.sustain.store(kDefaultMorphModSustain, std::memory_order_relaxed);
    params.cycleShape.store(kDefaultMorphModCycleShape, std::memory_order_relaxed);
    params.cycleRateHz.store(kDefaultMorphModCycleRateHz, std::memory_order_relaxed);
    params.cycleSync.store(kDefaultMorphModCycleSync, std::memory_order_relaxed);
    params.cycleSyncRate.store(kDefaultMorphModSyncRateIndex, std::memory_order_relaxed);
}

/// C-7 / FR-022: the wheel drives Morph and stops pushing Mass only when Source = Mod Wheel AND Amount != 0
/// (`!= 0.0f`, so -0 counts as 0).
[[nodiscard]] inline bool morphModHandsWheelToMorph(const MorphModParams& params) noexcept {
    return params.source.load(std::memory_order_relaxed) == kMorphModSourceModWheel &&
           params.amount.load(std::memory_order_relaxed) != 0.0f;
}

}  // namespace Profundum
