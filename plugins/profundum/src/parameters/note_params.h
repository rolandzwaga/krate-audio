#pragma once

// ==============================================================================
// Profundum - Note pack (ID 400-499: drop / note behaviour / amp envelope)
// Phase 11 T011 (FR-002, FR-003, FR-005, FR-006, FR-007, FR-008, FR-014,
// FR-044, FR-054; plan 4.2)
// ==============================================================================
// Two halves:
//
//  * E rows (morphed leaves, stored in the endpoint slots, persisted in the
//    endpoint blobs):
//      400->14 Drop Amount, 401->15 Drop Time, 402->16 Drop Shape,
//      403->17 Drop Rebound; 420->18 Attack, 421->19 Decay, 422->20 Sustain,
//      423->21 Release, 424->22 Attack Curve, 425->23 Decay Curve,
//      426->24 Release Curve.
//    Ranges, laws and defaults come from lawOf(leaf) only (FR-002).
//    Entry points: handleNoteParamChange(EndpointParams&, ...) (bool = handled),
//    registerNoteSoundRows(ParameterContainer&), formatNoteParam(id, v, string).
//
//  * NoteParams (the non-morphed IDs 410-417, persisted in state bytes 36-67):
//      410 Mono Priority  list 3   411 Legato         Off / On
//      412 Portamento     [0, 10000] ms, log on (t + 1 ms)
//      413 Portamento Mode list 2  414 Glide Mode     list 3
//      415 Note Memory    list 4   416 Note Retrigger list 2
//      417 Pitch Bend Range [0, 24] st, 24 steps, default 2
//    Entry points: handleNoteParamChange(NoteParams&, ...), registerNoteParams
//    (410-417 ONLY; the E rows register through registerNoteSoundRows),
//    formatNoteParam(id, v, glideModeIndex, string), saveNoteParams,
//    loadNoteParams, loadNoteParamsToController, resetNoteParams, pushNoteParams.
//
// FR-054: the registered IDs in 400-499 are exactly {400-403, 410-417, 420-426}.
// ==============================================================================

#include "parameters/sound_param_row.h"
#include "plugin_ids.h"

#include "ui/parameter_helpers.h"  // plugins/shared/src/ui/parameter_helpers.h:118

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/processors/mono_handler.h>
#include <krate/dsp/processors/note_memory.h>
#include <krate/dsp/systems/profundum_engine.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>

namespace Profundum {

// ==============================================================================
// E rows (plan 4.2 table: note_params.h, leaves {14..24})
// ==============================================================================

inline constexpr std::array<SoundParamRow, 11> kNoteRows{{
    {kDropAmountId, 14, kNoLeaf, STR16("Drop Amount"), STR16("st"), nullptr, 0, DisplayUnit::Semitone},
    {kDropTimeId, 15, kNoLeaf, STR16("Drop Time"), STR16("ms"), nullptr, 0, DisplayUnit::Time},
    {kDropShapeId, 16, kNoLeaf, STR16("Drop Shape"), STR16("%"), nullptr, 0, DisplayUnit::Percent},
    {kDropReboundId, 17, kNoLeaf, STR16("Drop Rebound"), STR16("%"), nullptr, 0, DisplayUnit::Percent},
    {kAmpAttackId, 18, kNoLeaf, STR16("Attack"), STR16("ms"), nullptr, 0, DisplayUnit::Time},
    {kAmpDecayId, 19, kNoLeaf, STR16("Decay"), STR16("ms"), nullptr, 0, DisplayUnit::Time},
    {kAmpSustainId, 20, kNoLeaf, STR16("Sustain"), STR16("%"), nullptr, 0, DisplayUnit::Percent},
    {kAmpReleaseId, 21, kNoLeaf, STR16("Release"), STR16("ms"), nullptr, 0, DisplayUnit::Time},
    {kAmpAttackCurveId, 22, kNoLeaf, STR16("Attack Curve"), STR16("%"), nullptr, 0, DisplayUnit::BipolarPercent},
    {kAmpDecayCurveId, 23, kNoLeaf, STR16("Decay Curve"), STR16("%"), nullptr, 0, DisplayUnit::BipolarPercent},
    {kAmpReleaseCurveId, 24, kNoLeaf, STR16("Release Curve"), STR16("%"), nullptr, 0, DisplayUnit::BipolarPercent},
}};

namespace note_params_detail {

inline constexpr std::uint8_t kFirstLeaf = 14;
inline constexpr std::uint8_t kLastLeaf = 24;

/// (a) The pack's leaf / leaf2 values are exactly {14..24}: each once, nothing else.
[[nodiscard]] constexpr bool rowsCoverLeavesExactly() noexcept {
    std::array<int, kNumLeaves> hits{};
    for (const SoundParamRow& r : kNoteRows) {
        if (r.leaf >= kNumLeaves) {
            return false;
        }
        ++hits[r.leaf];
        if (r.leaf2 != kNoLeaf) {
            if (r.leaf2 >= kNumLeaves) {
                return false;
            }
            ++hits[r.leaf2];
        }
    }
    for (std::size_t i = 0; i < kNumLeaves; ++i) {
        const bool inPack = i >= kFirstLeaf && i <= kLastLeaf;
        if (hits[i] != (inPack ? 1 : 0)) {
            return false;
        }
    }
    return true;
}

/// (b) Every list row has maxEnum + 1 labels (a Sync Rate pair: the 30-entry product); continuous rows have none
/// and name an F32 leaf. (d) Every Bool row has exactly 2 labels.
[[nodiscard]] constexpr bool rowsLabelCountsMatchLaw() noexcept {
    for (const SoundParamRow& r : kNoteRows) {
        const FieldInfo& f = lawOf(r.leaf);
        if (f.kind == MorphState::Kind::F32) {
            if (r.labels != nullptr || r.labelCount != 0 || r.leaf2 != kNoLeaf) {
                return false;
            }
            continue;
        }
        if (r.labels == nullptr) {
            return false;
        }
        if (r.leaf2 != kNoLeaf) {
            const int product = (static_cast<int>(f.maxEnum) + 1) * (static_cast<int>(lawOf(r.leaf2).maxEnum) + 1);
            if (product != Krate::DSP::kNoteValueDropdownCount || r.labelCount != product) {
                return false;
            }
            continue;
        }
        if (static_cast<int>(r.labelCount) != static_cast<int>(f.maxEnum) + 1) {
            return false;
        }
        if (f.kind == MorphState::Kind::Bool && r.labelCount != 2) {
            return false;
        }
    }
    return true;
}

/// (c) Every row ID lies in the note band [400, 500).
[[nodiscard]] constexpr bool rowsInBand() noexcept {
    for (const SoundParamRow& r : kNoteRows) {
        if (r.id < static_cast<Steinberg::Vst::ParamID>(kDropAmountId) ||
            r.id >= static_cast<Steinberg::Vst::ParamID>(kNoteParamRangeEnd)) {
            return false;
        }
    }
    return true;
}

/// FR-054: no E row sits on a note-behaviour ID (410-417); together with registerNoteParams the band holds
/// exactly {400-403, 410-417, 420-426}.
[[nodiscard]] constexpr bool rowsAvoidNoteBehaviourIds() noexcept {
    for (const SoundParamRow& r : kNoteRows) {
        if (r.id >= static_cast<Steinberg::Vst::ParamID>(kNoteMonoPriorityId) &&
            r.id <= static_cast<Steinberg::Vst::ParamID>(kNotePitchBendRangeId)) {
            return false;
        }
    }
    return true;
}

}  // namespace note_params_detail

static_assert(note_params_detail::rowsCoverLeavesExactly(), "note pack rows must cover leaves {14..24} exactly");
static_assert(note_params_detail::rowsLabelCountsMatchLaw(), "note pack list rows: labelCount == maxEnum + 1");
static_assert(note_params_detail::rowsInBand(), "note pack rows must lie in [400, 500)");
static_assert(note_params_detail::rowsAvoidNoteBehaviourIds(), "FR-054: E rows never use 410-417");

/// The E row of an ID, or nullptr.
[[nodiscard]] constexpr const SoundParamRow* noteRowOf(Steinberg::Vst::ParamID id) noexcept {
    for (const SoundParamRow& r : kNoteRows) {
        if (r.id == id) {
            return &r;
        }
    }
    return nullptr;
}

/// FR-011: an E row writes its leaf into the Edit-Target endpoint. Returns false (and writes nothing) for any
/// other ID. v is already finite and in [0, 1]. Real-time safe.
inline bool handleNoteParamChange(EndpointParams& target, Steinberg::Vst::ParamID id, double v) noexcept {
    const SoundParamRow* row = noteRowOf(id);
    if (row == nullptr) {
        return false;
    }
    writeSoundRow(target, *row, v);
    return true;
}

/// FR-001 / FR-003: the 11 E rows through the generic row path (defaults from MorphState{}).
inline void registerNoteSoundRows(Steinberg::Vst::ParameterContainer& parameters) {
    for (const SoundParamRow& r : kNoteRows) {
        registerSoundRow(parameters, r);
    }
}

/// FR-006 for the E rows. kResultFalse for every other ID (the note-behaviour IDs use the 4-argument overload).
inline Steinberg::tresult formatNoteParam(Steinberg::Vst::ParamID id, double v, Steinberg::Vst::String128 string) {
    const SoundParamRow* row = noteRowOf(id);
    if (row == nullptr) {
        return Steinberg::kResultFalse;
    }
    return formatSoundRow(*row, v, string);
}

// ==============================================================================
// Note behaviour (non-morphed, IDs 410-417)
// ==============================================================================

using NoteMonoMode = Krate::DSP::MonoMode;
using NotePortaMode = Krate::DSP::PortaMode;
using NoteGlideMode = Krate::DSP::GlideMode;
using NoteMemoryMode = Krate::DSP::NoteMemory::Mode;
using NoteRetriggerMode = Krate::DSP::NoteMemory::Retrigger;

// Largest valid index of each list, derived from the engine enums (mono_handler.h:53, :64, :74; note_memory.h:24-32).
inline constexpr int kMaxMonoPriorityIndex = static_cast<int>(NoteMonoMode::HighNote);                  // 2
inline constexpr int kMaxLegatoIndex = 1;
inline constexpr int kMaxPortamentoModeIndex = static_cast<int>(NotePortaMode::LegatoOnly);            // 1
inline constexpr int kMaxGlideModeIndex = static_cast<int>(NoteGlideMode::Quantized);                  // 2
inline constexpr int kMaxNoteMemoryIndex = static_cast<int>(NoteMemoryMode::OscillatorContinuous);     // 3
inline constexpr int kMaxNoteRetriggerIndex = static_cast<int>(NoteRetriggerMode::Continue);           // 1
static_assert(kMaxMonoPriorityIndex == 2 && kMaxPortamentoModeIndex == 1 && kMaxGlideModeIndex == 2 &&
                  kMaxNoteMemoryIndex == 3 && kMaxNoteRetriggerIndex == 1,
              "the list labels below assume these enum extents");

/// 412: [0, 10000] ms, offset-log law on (t + 1 ms) (spec ID table; MonoHandler clamp, mono_handler.h:111-112).
inline constexpr PlainLaw kPortamentoLaw{static_cast<double>(Krate::DSP::MonoHandler::kMinPortamentoTimeMs),
                                         static_cast<double>(Krate::DSP::MonoHandler::kMaxPortamentoTimeMs), 1.0,
                                         true};
static_assert(kPortamentoLaw.lo == 0.0 && kPortamentoLaw.hi == 10000.0 && kPortamentoLaw.logOffset == 1.0 &&
                  kPortamentoLaw.logarithmic,
              "412 law is PlainLaw{0, 10000, 1, true}");
// plainFromNormalized(PlainLaw, v) snaps v <= 0 to lo explicitly (sound_param_row.h:129-132), so 0 -> lo == 0 ms
// exactly; lo + logOffset > 0 keeps the log branch defined.
static_assert(kPortamentoLaw.lo == 0.0 && kPortamentoLaw.lo + kPortamentoLaw.logOffset > 0.0,
              "the 412 law maps normalised 0 to exactly 0 ms");

/// 417: [0, kMaxBendRangeSt] st in 1-semitone steps (FR-008).
inline constexpr int kBendRangeSteps = static_cast<int>(Krate::DSP::ProfundumEngine::kMaxBendRangeSt);  // 24
static_assert(static_cast<float>(kBendRangeSteps) == Krate::DSP::ProfundumEngine::kMaxBendRangeSt &&
                  kBendRangeSteps == 24,
              "FR-008: 24 one-semitone steps");
inline constexpr double kDefaultBendRangeNormalized =
    static_cast<double>(Krate::DSP::ProfundumEngine::kDefaultBendRangeSt) / static_cast<double>(kBendRangeSteps);
static_assert(kDefaultBendRangeNormalized == 2.0 / 24.0, "the default normalised of 417 is 2 / 24");

/// The engine's private Settings defaults, re-stated (plan 1.3; SC-007 pins them through the public getters).
struct NoteParams {
    std::atomic<int> monoPriority{static_cast<int>(NoteMonoMode::LastNote)};
    std::atomic<int> legato{0};
    std::atomic<float> portamentoMs{0.0f};
    std::atomic<int> portamentoMode{static_cast<int>(NotePortaMode::Always)};
    std::atomic<int> glideMode{static_cast<int>(NoteGlideMode::ConstantTime)};
    std::atomic<int> memoryMode{static_cast<int>(NoteMemoryMode::AlwaysRetrigger)};
    std::atomic<int> retrigger{static_cast<int>(NoteRetriggerMode::Reset)};
    std::atomic<float> bendRangeSt{Krate::DSP::ProfundumEngine::kDefaultBendRangeSt};  // profundum_engine.h:112
};

/// Plan 4.2. v is already finite and in [0, 1] (processParameterChanges). IDs outside 410-417 store nothing.
inline void handleNoteParamChange(NoteParams& params, Steinberg::Vst::ParamID id,
                                  Steinberg::Vst::ParamValue v) noexcept {
    switch (id) {
        case kNoteMonoPriorityId:
            params.monoPriority.store(indexFromNormalized(v, kMaxMonoPriorityIndex), std::memory_order_relaxed);
            break;
        case kNoteLegatoId:
            params.legato.store(indexFromNormalized(v, kMaxLegatoIndex), std::memory_order_relaxed);
            break;
        case kNotePortamentoId:
            params.portamentoMs.store(static_cast<float>(plainFromNormalized(kPortamentoLaw, v)),
                                      std::memory_order_relaxed);
            break;
        case kNotePortamentoModeId:
            params.portamentoMode.store(indexFromNormalized(v, kMaxPortamentoModeIndex), std::memory_order_relaxed);
            break;
        case kNoteGlideModeId:
            params.glideMode.store(indexFromNormalized(v, kMaxGlideModeIndex), std::memory_order_relaxed);
            break;
        case kNoteMemoryId:
            params.memoryMode.store(indexFromNormalized(v, kMaxNoteMemoryIndex), std::memory_order_relaxed);
            break;
        case kNoteRetriggerId:
            params.retrigger.store(indexFromNormalized(v, kMaxNoteRetriggerIndex), std::memory_order_relaxed);
            break;
        case kNotePitchBendRangeId:
            // round(24 v), v clamped (FR-008)
            params.bendRangeSt.store(static_cast<float>(indexFromNormalized(v, kBendRangeSteps)),
                                     std::memory_order_relaxed);
            break;
        default:
            break;
    }
}

/// FR-001 / FR-003 / FR-004 (plan 4.2): IDs 410-417 only. The dropdown helper sets only the CURRENT value
/// (parameter_helpers.h:118-131), so every list's registered default is pinned by hand.
inline void registerNoteParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;

    auto addList = [&parameters](const TChar* title, ParamID id, std::initializer_list<const TChar*> labels) {
        auto* list = Krate::Plugins::createDropdownParameterWithDefault(title, id, 0, labels);
        list->getInfo().defaultNormalizedValue = 0.0;  // every note-behaviour list defaults to index 0
        parameters.addParameter(list);
    };

    addList(STR16("Mono Priority"), kNoteMonoPriorityId, {STR16("Last"), STR16("Low"), STR16("High")});
    addList(STR16("Legato"), kNoteLegatoId, {STR16("Off"), STR16("On")});

    parameters.addParameter(STR16("Portamento"), STR16("ms"), 0, normalizedFromPlain(kPortamentoLaw, 0.0),
                            ParameterInfo::kCanAutomate, static_cast<Steinberg::int32>(kNotePortamentoId));

    addList(STR16("Portamento Mode"), kNotePortamentoModeId, {STR16("Always"), STR16("Legato Only")});
    addList(STR16("Glide Mode"), kNoteGlideModeId,
            {STR16("Constant Time"), STR16("Constant Rate"), STR16("Quantized")});
    addList(STR16("Note Memory"), kNoteMemoryId,
            {STR16("Always Retrigger"), STR16("Legato"), STR16("Envelope Retrigger"),
             STR16("Oscillator Continuous")});
    addList(STR16("Note Retrigger"), kNoteRetriggerId, {STR16("Reset"), STR16("Continue")});

    auto* bendRange = new RangeParameter(
        STR16("Pitch Bend Range"), kNotePitchBendRangeId, STR16("st"), 0.0, static_cast<ParamValue>(kBendRangeSteps),
        static_cast<ParamValue>(Krate::DSP::ProfundumEngine::kDefaultBendRangeSt), kBendRangeSteps,
        ParameterInfo::kCanAutomate);
    bendRange->getInfo().defaultNormalizedValue = kDefaultBendRangeNormalized;
    parameters.addParameter(bendRange);
}

/// FR-006 / FR-007 (plan 4.3): the E rows, plus Portamento (412) with the Time rule and a suffix that follows
/// the CURRENT Glide Mode index (0 ConstantTime "ms", 1 ConstantRate "ms/oct", 2 Quantized "ms"; under
/// ConstantRate a value of 1000 or more prints "%.2f s/oct"). The list IDs and 417 format themselves
/// (kResultFalse). ASCII only.
inline Steinberg::tresult formatNoteParam(Steinberg::Vst::ParamID id, double v, int glideModeIndex,
                                          Steinberg::Vst::String128 string) {
    using namespace Steinberg;

    if (id != kNotePortamentoId) {
        return formatNoteParam(id, v, string);
    }

    const double x = static_cast<double>(static_cast<float>(plainFromNormalized(kPortamentoLaw, v)));
    const bool perOctave = glideModeIndex == static_cast<int>(NoteGlideMode::ConstantRate);
    const double r = std::round(10.0 * x) / 10.0;
    char8 text[64];
    if (r < 1000.0) {
        if (perOctave) {
            std::snprintf(text, sizeof(text), "%.1f ms/oct", x);
        } else {
            std::snprintf(text, sizeof(text), "%.1f ms", x);
        }
    } else if (perOctave) {
        std::snprintf(text, sizeof(text), "%.2f s/oct", x / 1000.0);
    } else {
        std::snprintf(text, sizeof(text), "%.2f s", x / 1000.0);
    }
    UString(string, 128).fromAscii(text);
    return kResultOk;
}

/// Spec state bytes 36-67: priority i32, legato i32, portamento ms f32, portamento mode i32, glide mode i32,
/// Note Memory mode i32, retrigger i32, bend range f32 (32 bytes).
inline void saveNoteParams(const NoteParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeInt32(static_cast<Steinberg::int32>(params.monoPriority.load(std::memory_order_relaxed)));
    streamer.writeInt32(static_cast<Steinberg::int32>(params.legato.load(std::memory_order_relaxed)));
    streamer.writeFloat(params.portamentoMs.load(std::memory_order_relaxed));
    streamer.writeInt32(static_cast<Steinberg::int32>(params.portamentoMode.load(std::memory_order_relaxed)));
    streamer.writeInt32(static_cast<Steinberg::int32>(params.glideMode.load(std::memory_order_relaxed)));
    streamer.writeInt32(static_cast<Steinberg::int32>(params.memoryMode.load(std::memory_order_relaxed)));
    streamer.writeInt32(static_cast<Steinberg::int32>(params.retrigger.load(std::memory_order_relaxed)));
    streamer.writeFloat(params.bendRangeSt.load(std::memory_order_relaxed));
}
static_assert(kStateNoteBlockBytes == std::size_t{7 * 4 + 4}, "saveNoteParams writes 8 four-byte fields");

namespace note_params_detail {

[[nodiscard]] inline bool indexInRange(Steinberg::int32 v, int maxIndex) noexcept {
    return v >= 0 && v <= static_cast<Steinberg::int32>(maxIndex);
}

[[nodiscard]] inline float clampPortamentoMs(float ms) noexcept {
    return std::clamp(ms, Krate::DSP::MonoHandler::kMinPortamentoTimeMs, Krate::DSP::MonoHandler::kMaxPortamentoTimeMs);
}

[[nodiscard]] inline float clampBendRangeSt(float st) noexcept {
    return std::clamp(st, 0.0f, Krate::DSP::ProfundumEngine::kMaxBendRangeSt);
}

}  // namespace note_params_detail

/// EOF-safe; false at the first failed read, leaving every later field unchanged (FR-044). Sanitising follows
/// global_params.h:189-226: a non-finite float keeps its atomic, a finite float is clamped to its range, an enum
/// index outside [0, maxIndex] is not stored.
inline bool loadNoteParams(NoteParams& params, Steinberg::IBStreamer& streamer) {
    using note_params_detail::indexInRange;

    auto readIndex = [&streamer](std::atomic<int>& field, int maxIndex) -> bool {
        Steinberg::int32 v = 0;
        if (!streamer.readInt32(v)) {
            return false;
        }
        if (indexInRange(v, maxIndex)) {
            field.store(static_cast<int>(v), std::memory_order_relaxed);
        }
        return true;
    };

    if (!readIndex(params.monoPriority, kMaxMonoPriorityIndex)) {
        return false;
    }
    if (!readIndex(params.legato, kMaxLegatoIndex)) {
        return false;
    }

    float portaMs = 0.0f;
    if (!streamer.readFloat(portaMs)) {
        return false;
    }
    if (Krate::DSP::detail::isFinite(portaMs)) {
        params.portamentoMs.store(note_params_detail::clampPortamentoMs(portaMs), std::memory_order_relaxed);
    }

    if (!readIndex(params.portamentoMode, kMaxPortamentoModeIndex)) {
        return false;
    }
    if (!readIndex(params.glideMode, kMaxGlideModeIndex)) {
        return false;
    }
    if (!readIndex(params.memoryMode, kMaxNoteMemoryIndex)) {
        return false;
    }
    if (!readIndex(params.retrigger, kMaxNoteRetriggerIndex)) {
        return false;
    }

    float bendSt = 0.0f;
    if (!streamer.readFloat(bendSt)) {
        return false;
    }
    if (Krate::DSP::detail::isFinite(bendSt)) {
        params.bendRangeSt.store(note_params_detail::clampBendRangeSt(bendSt), std::memory_order_relaxed);
    }
    return true;
}

/// FR-045 / FR-046: the loadNoteParams rules applied BEFORE normalising, then setParam(id, normalized).
/// EOF-safe; returns at the first failed read. A non-finite float or an out-of-range index makes NO call.
template <typename SetParamFunc>
inline void loadNoteParamsToController(Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    using note_params_detail::indexInRange;

    auto readIndex = [&streamer, &setParam](Steinberg::Vst::ParamID id, int maxIndex) -> bool {
        Steinberg::int32 v = 0;
        if (!streamer.readInt32(v)) {
            return false;
        }
        if (indexInRange(v, maxIndex)) {
            setParam(id, static_cast<double>(v) / static_cast<double>(maxIndex));
        }
        return true;
    };

    if (!readIndex(kNoteMonoPriorityId, kMaxMonoPriorityIndex)) {
        return;
    }
    if (!readIndex(kNoteLegatoId, kMaxLegatoIndex)) {
        return;
    }

    float portaMs = 0.0f;
    if (!streamer.readFloat(portaMs)) {
        return;
    }
    if (Krate::DSP::detail::isFinite(portaMs)) {
        setParam(kNotePortamentoId,
                 normalizedFromPlain(kPortamentoLaw,
                                     static_cast<double>(note_params_detail::clampPortamentoMs(portaMs))));
    }

    if (!readIndex(kNotePortamentoModeId, kMaxPortamentoModeIndex)) {
        return;
    }
    if (!readIndex(kNoteGlideModeId, kMaxGlideModeIndex)) {
        return;
    }
    if (!readIndex(kNoteMemoryId, kMaxNoteMemoryIndex)) {
        return;
    }
    if (!readIndex(kNoteRetriggerId, kMaxNoteRetriggerIndex)) {
        return;
    }

    float bendSt = 0.0f;
    if (!streamer.readFloat(bendSt)) {
        return;
    }
    if (Krate::DSP::detail::isFinite(bendSt)) {
        setParam(kNotePitchBendRangeId, static_cast<double>(note_params_detail::clampBendRangeSt(bendSt)) /
                                            static_cast<double>(kBendRangeSteps));
    }
}

/// FR-044 (v1 load) / plan 4.2: the engine's Settings defaults.
inline void resetNoteParams(NoteParams& params) noexcept {
    params.monoPriority.store(static_cast<int>(NoteMonoMode::LastNote), std::memory_order_relaxed);
    params.legato.store(0, std::memory_order_relaxed);
    params.portamentoMs.store(0.0f, std::memory_order_relaxed);
    params.portamentoMode.store(static_cast<int>(NotePortaMode::Always), std::memory_order_relaxed);
    params.glideMode.store(static_cast<int>(NoteGlideMode::ConstantTime), std::memory_order_relaxed);
    params.memoryMode.store(static_cast<int>(NoteMemoryMode::AlwaysRetrigger), std::memory_order_relaxed);
    params.retrigger.store(static_cast<int>(NoteRetriggerMode::Reset), std::memory_order_relaxed);
    params.bendRangeSt.store(Krate::DSP::ProfundumEngine::kDefaultBendRangeSt, std::memory_order_relaxed);
}

/// FR-014: the eight change-only setters (profundum_engine.h:331-362). Each enum is range-checked before its
/// static_cast; an out-of-range index makes no call. Audio thread; real-time safe.
inline void pushNoteParams(const NoteParams& params, Krate::DSP::ProfundumEngine& engine) noexcept {
    using note_params_detail::indexInRange;

    const int priority = params.monoPriority.load(std::memory_order_relaxed);
    if (indexInRange(priority, kMaxMonoPriorityIndex)) {
        engine.setMonoPriority(static_cast<NoteMonoMode>(priority));
    }
    engine.setLegato(params.legato.load(std::memory_order_relaxed) != 0);
    engine.setPortamentoTimeMs(params.portamentoMs.load(std::memory_order_relaxed));
    const int portaMode = params.portamentoMode.load(std::memory_order_relaxed);
    if (indexInRange(portaMode, kMaxPortamentoModeIndex)) {
        engine.setPortamentoMode(static_cast<NotePortaMode>(portaMode));
    }
    const int glide = params.glideMode.load(std::memory_order_relaxed);
    if (indexInRange(glide, kMaxGlideModeIndex)) {
        engine.setGlideMode(static_cast<NoteGlideMode>(glide));
    }
    const int memory = params.memoryMode.load(std::memory_order_relaxed);
    if (indexInRange(memory, kMaxNoteMemoryIndex)) {
        engine.setNoteMemoryMode(static_cast<NoteMemoryMode>(memory));
    }
    const int retrigger = params.retrigger.load(std::memory_order_relaxed);
    if (indexInRange(retrigger, kMaxNoteRetriggerIndex)) {
        engine.setNoteRetrigger(static_cast<NoteRetriggerMode>(retrigger));
    }
    engine.setPitchBendRange(params.bendRangeSt.load(std::memory_order_relaxed));
}

}  // namespace Profundum
