#pragma once

// ==============================================================================
// Profundum - Catch2-free preset state decode (Phase 13 T006; plan 4.7 first block)
// ==============================================================================
// Shared by the preset host tool and the preset tests: no Catch2, no fixture.
// Decodes a v3 component state (610 bytes since Phase 14, plugin_ids.h:30-75) field by field:
//   0-3     version i32
//   4-19    global block: gain f32, voice mode i32, polyphony i32, ceiling f32 (global_params.h:182-187)
//   20-35   macros 4 x f32 (macro_params.h:144-147)
//   36-67   note block (note_params.h:360-369)
//   68-83   morph control: position f32, ramp f32, anchor mode i32, edit target i32 (morph_params.h:301-306)
//   84-87   seed u32
//   88/289  endpoints A / B, 201 bytes each, through deserializeMorphState (morph_state.h:574)
//   490     runtime blob, kRt*Offset fields (plugin_ids.h:70-77)
//   510     two name blocks, u8 length + bytes (plugin_ids.h:55-57)
// All multi-byte words are little-endian (processor.cpp:600, :665 use kLittleEndian).
// ==============================================================================

#include "parameters/sound_param_row.h"  // leafBitsOf, lawOf, kNumLeaves (field order of the leaves)
#include "plugin_ids.h"

#include "pluginterfaces/base/ftypes.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/systems/morph_state.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>

namespace ProfundumPluginTest {

struct DecodedPresetState {
    bool ok = false;
    std::int32_t version = 0;
    std::size_t bytes = 0;
    float outputGain = 0.0f, outputCeilingDb = 0.0f;
    std::int32_t voiceMode = 0, polyphony = 0;
    std::array<float, 4> macros{};
    struct Note {
        std::int32_t priority = 0, legato = 0;
        float portamentoMs = 0.0f;
        std::int32_t portamentoMode = 0, glideMode = 0, memory = 0, retrigger = 0;
        float bendSt = 0.0f;
    } note{};
    struct Morph {
        float position = 0.0f, rampMs = 0.0f;
        std::int32_t anchorMode = 0, editTarget = 0;
    } morph{};
    std::uint32_t seed = 0;
    Krate::DSP::MorphState a{}, b{};               // deserializeMorphState (morph_state.h:574)
    struct Runtime {
        std::uint8_t version = 0;
        float target = 0.0f, position = 0.0f, anchorPosition = 0.0f;
        std::uint8_t mode = 0;
        float rampMs = 0.0f;
        std::uint8_t mainMask = 0, anchorSelection = 0;
    } runtime{};                                   // offsets: plugin_ids.h:70-77
    std::array<std::string, 2> names;              // length byte + bytes (plugin_ids.h:55-57)
    /// Phase 14 (C-10, plan 4.12): the 36-byte Morph Modulation block at kStateMorphModOffset, as stored (Sync Rate
    /// is the kNoteValueDropdownMapping index). present: the stream held the whole block.
    struct MorphMod {
        std::int32_t source = 0;
        float amount = 0, attackMs = 10, decayMs = 300, sustain = 0;
        std::int32_t cycleShape = 0;
        float cycleRateHz = 1;
        std::int32_t cycleSync = 0, cycleSyncRate = 13;
        bool present = false;
    } morphMod{};
};

namespace preset_decode_detail {

inline constexpr std::size_t kWord = sizeof(std::uint32_t);
inline constexpr std::size_t kGlobalOffset = sizeof(Steinberg::int32);          // after the version word
inline constexpr std::size_t kGlobalBytes = 4 * kWord;                         // gain, mode, polyphony, ceiling
inline constexpr std::size_t kMacroOffset = kGlobalOffset + kGlobalBytes;
inline constexpr std::size_t kNumMacros = 4;
inline constexpr std::size_t kNoteOffset = Profundum::kStateV1Bytes;
inline constexpr std::size_t kMorphControlOffset = kNoteOffset + Profundum::kStateNoteBlockBytes;
inline constexpr std::size_t kSeedOffset = kMorphControlOffset + Profundum::kStateMorphControlBytes;
static_assert(kMacroOffset + kNumMacros * kWord == Profundum::kStateV1Bytes, "plan 3.4: macros end the v1 body");
static_assert(kSeedOffset + Profundum::kStateSeedBytes == Profundum::kStateEndpointAOffset,
              "plan 3.2: the seed precedes endpoint A");

/// A little-endian u32 at @p at; 0 when the word does not fit.
[[nodiscard]] inline std::uint32_t u32At(std::span<const std::uint8_t> s, std::size_t at) noexcept {
    if (at + kWord > s.size()) {
        return 0u;
    }
    return static_cast<std::uint32_t>(s[at]) | (static_cast<std::uint32_t>(s[at + 1]) << 8) |
           (static_cast<std::uint32_t>(s[at + 2]) << 16) | (static_cast<std::uint32_t>(s[at + 3]) << 24);
}
[[nodiscard]] inline std::int32_t i32At(std::span<const std::uint8_t> s, std::size_t at) noexcept {
    return static_cast<std::int32_t>(u32At(s, at));
}
[[nodiscard]] inline float f32At(std::span<const std::uint8_t> s, std::size_t at) noexcept {
    return std::bit_cast<float>(u32At(s, at));
}
[[nodiscard]] inline std::uint8_t u8At(std::span<const std::uint8_t> s, std::size_t at) noexcept {
    return at < s.size() ? s[at] : std::uint8_t{0};
}

[[nodiscard]] inline std::string fmt(double v) {
    char text[48];
    std::snprintf(text, sizeof(text), "%.9g", v);
    return text;
}

[[nodiscard]] inline bool floatClose(double x, double y, double tol) noexcept {
    return std::abs(x - y) <= tol * std::max(std::abs(y), 1.0);   // NaN on either side compares false
}

}  // namespace preset_decode_detail

/// Decodes a component-state blob. ok is true only for a complete v3 stream (version == kCurrentStateVersion,
/// bytes == kStateV3Bytes) whose two endpoints both deserialise; fields that fit are decoded either way.
[[nodiscard]] inline DecodedPresetState decodePresetState(std::span<const std::uint8_t> comp) {
    namespace d = preset_decode_detail;
    namespace pf = Profundum;
    DecodedPresetState out{};
    out.bytes = comp.size();
    if (comp.size() < sizeof(Steinberg::int32)) {
        return out;
    }
    out.version = d::i32At(comp, 0);

    out.outputGain = d::f32At(comp, d::kGlobalOffset);
    out.voiceMode = d::i32At(comp, d::kGlobalOffset + d::kWord);
    out.polyphony = d::i32At(comp, d::kGlobalOffset + 2 * d::kWord);
    out.outputCeilingDb = d::f32At(comp, d::kGlobalOffset + 3 * d::kWord);
    for (std::size_t i = 0; i < d::kNumMacros; ++i) {
        out.macros[i] = d::f32At(comp, d::kMacroOffset + i * d::kWord);
    }

    out.note.priority = d::i32At(comp, d::kNoteOffset);
    out.note.legato = d::i32At(comp, d::kNoteOffset + d::kWord);
    out.note.portamentoMs = d::f32At(comp, d::kNoteOffset + 2 * d::kWord);
    out.note.portamentoMode = d::i32At(comp, d::kNoteOffset + 3 * d::kWord);
    out.note.glideMode = d::i32At(comp, d::kNoteOffset + 4 * d::kWord);
    out.note.memory = d::i32At(comp, d::kNoteOffset + 5 * d::kWord);
    out.note.retrigger = d::i32At(comp, d::kNoteOffset + 6 * d::kWord);
    out.note.bendSt = d::f32At(comp, d::kNoteOffset + 7 * d::kWord);

    out.morph.position = d::f32At(comp, d::kMorphControlOffset);
    out.morph.rampMs = d::f32At(comp, d::kMorphControlOffset + d::kWord);
    out.morph.anchorMode = d::i32At(comp, d::kMorphControlOffset + 2 * d::kWord);
    out.morph.editTarget = d::i32At(comp, d::kMorphControlOffset + 3 * d::kWord);

    out.seed = d::u32At(comp, d::kSeedOffset);

    bool endpointsOk = true;
    const std::array<std::size_t, 2> endpointAt{pf::kStateEndpointAOffset, pf::kStateEndpointBOffset};
    const std::array<Krate::DSP::MorphState*, 2> endpointOut{&out.a, &out.b};
    for (std::size_t e = 0; e < endpointAt.size(); ++e) {
        const std::size_t at = endpointAt[e];
        if (at + Krate::DSP::kMorphStateBytes > comp.size()) {
            endpointsOk = false;
            continue;
        }
        const auto* src = reinterpret_cast<const std::byte*>(comp.data() + at);
        endpointsOk = Krate::DSP::deserializeMorphState(src, Krate::DSP::kMorphStateBytes, *endpointOut[e]) &&
                      endpointsOk;
    }

    constexpr std::size_t rt = pf::kStateRuntimeOffset;
    out.runtime.version = d::u8At(comp, rt + pf::kRtVersionOffset);
    out.runtime.target = d::f32At(comp, rt + pf::kRtTargetOffset);
    out.runtime.position = d::f32At(comp, rt + pf::kRtPositionOffset);
    out.runtime.anchorPosition = d::f32At(comp, rt + pf::kRtAnchorPositionOffset);
    out.runtime.mode = d::u8At(comp, rt + pf::kRtModeOffset);
    out.runtime.rampMs = d::f32At(comp, rt + pf::kRtRampOffset);
    out.runtime.mainMask = d::u8At(comp, rt + pf::kRtMainMaskOffset);
    out.runtime.anchorSelection = d::u8At(comp, rt + pf::kRtAnchorSelectionOffset);

    for (std::size_t e = 0; e < out.names.size(); ++e) {
        const std::size_t at = pf::kStateNamesOffset + e * pf::kEndpointNameBlockBytes;
        if (at + pf::kEndpointNameBlockBytes > comp.size()) {
            continue;
        }
        const std::size_t len = std::min<std::size_t>(comp[at], pf::kEndpointNameMaxBytes);
        out.names[e].assign(reinterpret_cast<const char*>(comp.data() + at + 1), len);
    }

    // Phase 14 (C-10): the Morph Modulation block, in its table order (morph_mod_params.h saveMorphModParams).
    if (comp.size() >= pf::kStateV3Bytes) {
        constexpr std::size_t mm = pf::kStateMorphModOffset;
        out.morphMod.source = d::i32At(comp, mm);
        out.morphMod.amount = d::f32At(comp, mm + d::kWord);
        out.morphMod.attackMs = d::f32At(comp, mm + 2 * d::kWord);
        out.morphMod.decayMs = d::f32At(comp, mm + 3 * d::kWord);
        out.morphMod.sustain = d::f32At(comp, mm + 4 * d::kWord);
        out.morphMod.cycleShape = d::i32At(comp, mm + 5 * d::kWord);
        out.morphMod.cycleRateHz = d::f32At(comp, mm + 6 * d::kWord);
        out.morphMod.cycleSync = d::i32At(comp, mm + 7 * d::kWord);
        out.morphMod.cycleSyncRate = d::i32At(comp, mm + 8 * d::kWord);
        out.morphMod.present = true;
    }

    out.ok = out.version == pf::kCurrentStateVersion && out.bytes == pf::kStateV3Bytes && endpointsOk;
    return out;
}

/// Field-by-field compare. Ints, enums and names: exact. Floats: |x - y| <= tol * max(|y|, 1). MorphState: by leafBitsOf
/// leaf (float leaves with tol, the rest exact). The first difference is written to `where`.
[[nodiscard]] inline bool decodedEqual(const DecodedPresetState& x, const DecodedPresetState& y, double tol,
                                       std::string& where) {
    namespace d = preset_decode_detail;
    where.clear();
    const auto intField = [&where](const char* name, long long xv, long long yv) {
        if (xv == yv) {
            return true;
        }
        where = std::string(name) + ": " + std::to_string(xv) + " vs " + std::to_string(yv);
        return false;
    };
    const auto floatField = [&where, tol](const std::string& name, float xv, float yv) {
        if (d::floatClose(static_cast<double>(xv), static_cast<double>(yv), tol)) {
            return true;
        }
        where = name + ": " + d::fmt(static_cast<double>(xv)) + " vs " + d::fmt(static_cast<double>(yv));
        return false;
    };
    const auto morphField = [&where, tol](const char* endpoint, const Krate::DSP::MorphState& xs,
                                          const Krate::DSP::MorphState& ys) {
        const Profundum::LeafBits xb = Profundum::leafBitsOf(xs);
        const Profundum::LeafBits yb = Profundum::leafBitsOf(ys);
        for (std::size_t i = 0; i < Profundum::kNumLeaves; ++i) {
            const auto& info = Profundum::lawOf(static_cast<std::uint8_t>(i));
            const std::string name = std::string(endpoint) + "." + info.name;
            if (info.kind == Krate::DSP::MorphState::Kind::F32) {
                const auto xv = static_cast<double>(std::bit_cast<float>(xb[i]));
                const auto yv = static_cast<double>(std::bit_cast<float>(yb[i]));
                if (!d::floatClose(xv, yv, tol)) {
                    where = name + ": " + d::fmt(xv) + " vs " + d::fmt(yv);
                    return false;
                }
            } else if (xb[i] != yb[i]) {
                where = name + ": " + std::to_string(xb[i]) + " vs " + std::to_string(yb[i]);
                return false;
            }
        }
        return true;
    };

    if (!intField("ok", x.ok ? 1 : 0, y.ok ? 1 : 0) || !intField("version", x.version, y.version) ||
        !intField("bytes", static_cast<long long>(x.bytes), static_cast<long long>(y.bytes)) ||
        !floatField("outputGain", x.outputGain, y.outputGain) ||
        !intField("voiceMode", x.voiceMode, y.voiceMode) || !intField("polyphony", x.polyphony, y.polyphony) ||
        !floatField("outputCeilingDb", x.outputCeilingDb, y.outputCeilingDb)) {
        return false;
    }
    for (std::size_t i = 0; i < x.macros.size(); ++i) {
        if (!floatField("macros[" + std::to_string(i) + "]", x.macros[i], y.macros[i])) {
            return false;
        }
    }
    if (!intField("note.priority", x.note.priority, y.note.priority) ||
        !intField("note.legato", x.note.legato, y.note.legato) ||
        !floatField("note.portamentoMs", x.note.portamentoMs, y.note.portamentoMs) ||
        !intField("note.portamentoMode", x.note.portamentoMode, y.note.portamentoMode) ||
        !intField("note.glideMode", x.note.glideMode, y.note.glideMode) ||
        !intField("note.memory", x.note.memory, y.note.memory) ||
        !intField("note.retrigger", x.note.retrigger, y.note.retrigger) ||
        !floatField("note.bendSt", x.note.bendSt, y.note.bendSt) ||
        !floatField("morph.position", x.morph.position, y.morph.position) ||
        !floatField("morph.rampMs", x.morph.rampMs, y.morph.rampMs) ||
        !intField("morph.anchorMode", x.morph.anchorMode, y.morph.anchorMode) ||
        !intField("morph.editTarget", x.morph.editTarget, y.morph.editTarget) ||
        !intField("seed", x.seed, y.seed) || !morphField("a", x.a, y.a) || !morphField("b", x.b, y.b) ||
        !intField("runtime.version", x.runtime.version, y.runtime.version) ||
        !floatField("runtime.target", x.runtime.target, y.runtime.target) ||
        !floatField("runtime.position", x.runtime.position, y.runtime.position) ||
        !floatField("runtime.anchorPosition", x.runtime.anchorPosition, y.runtime.anchorPosition) ||
        !intField("runtime.mode", x.runtime.mode, y.runtime.mode) ||
        !floatField("runtime.rampMs", x.runtime.rampMs, y.runtime.rampMs) ||
        !intField("runtime.mainMask", x.runtime.mainMask, y.runtime.mainMask) ||
        !intField("runtime.anchorSelection", x.runtime.anchorSelection, y.runtime.anchorSelection)) {
        return false;
    }
    for (std::size_t e = 0; e < x.names.size(); ++e) {
        if (x.names[e] != y.names[e]) {
            where = "names[" + std::to_string(e) + "]: \"" + x.names[e] + "\" vs \"" + y.names[e] + "\"";
            return false;
        }
    }
    return true;
}

/// Exponent test on the bit pattern: immune to fast-math (never std::isfinite).
[[nodiscard]] inline bool presetFloatIsFinite(float x) noexcept {
    return ::Krate::DSP::detail::isFinite(x);   // barrier read: a plain bit_cast mask is folded under fast-math
}

/// C-9 rule 5 (plan 6): an active endpoint (plan 5.6: A alone at p = 0, B alone at p = 1, both for 0 < p < 1) stores
/// Rectify, Crush or Fracture at Amount >= 0.9 with Anchor Level < 0.25.
/// One predicate, shared by Profundum_PresetDefsRules and the [.listen] case.
[[nodiscard]] inline bool fundamentalLossFlagged(const DecodedPresetState& d) noexcept {
    using Alg = Krate::DSP::BassCharacterStage::Algorithm;
    const auto lossy = [](const Krate::DSP::MorphState& s) noexcept {
        const bool alg = s.characterAlgorithm == Alg::Rectify || s.characterAlgorithm == Alg::Crush ||
                         s.characterAlgorithm == Alg::Fracture;
        return alg && s.characterAmount >= 0.9f && s.anchor.level < 0.25f;
    };
    const float p = d.morph.position;
    const bool aActive = !(p >= 1.0f);   // p < 1, or a non-finite p (treated as a blend: both active)
    const bool bActive = !(p <= 0.0f);   // p > 0, or a non-finite p
    return (aActive && lossy(d.a)) || (bActive && lossy(d.b));
}

/// C-6 (plan 5.5): block 512, note-off at 3 s, then the reported tail.
struct SweepTimeline {
    double fs;
    std::size_t block;
    std::size_t noteOff;
    std::size_t total;
};

[[nodiscard]] inline SweepTimeline makeTimeline(double fs, Steinberg::uint32 tailSamples) {
    const auto noteOff = static_cast<std::size_t>(std::llround(3.0 * fs));
    return SweepTimeline{fs, 512, noteOff, noteOff + static_cast<std::size_t>(tailSamples)};
}

}  // namespace ProfundumPluginTest
