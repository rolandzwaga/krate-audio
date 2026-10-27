#pragma once

// ==============================================================================
// Profundum - Factory Preset Definitions (Phase 13, FR-010 / FR-011)
// ==============================================================================
// Spec:  specs/profundum-phase13-presets-release/spec.md (C-2.1 columns,
//        C-2.2 showcase rules, FR-010, FR-011, FR-022)
// Plan:  specs/profundum-phase13-presets-release/plan.md section 4.3
// Tasks: specs/profundum-phase13-presets-release/tasks.md T005 (this file),
//        T023 (rows 51-53), T025 (rows 1-50); FR-022: no definition before
//        the probe table.
//
// THIS HEADER IS DATA ONLY (the Vorago rules, tools/vorago_preset_defs.h:12-21):
//   - every function is `inline`;
//   - every table is a function-local `static const`;
//   - the only project include is `plugin_ids.h`, plus std and vsttypes.h;
//   - no state layout: the generator writes each `Comp` chunk through the
//     SHIPPED `Profundum::Processor::getState()` (spec C-4).
//
// Values are NORMALIZED 0..1, exactly as a host delivers them. Untouched IDs
// keep their registered defaults.
//
// Phase 14 T023 (specs/profundum-phase14-morph-modulation, plan 4.13, 5.7 step 5; FR-040 - FR-042, FR-046, FR-063):
//   - CapabilityGroup::MorphMod and the four columns M-ENV, M-CYCLE, M-VEL, M-WHEEL appended after R-SINE;
//   - `controls` may carry the Morph Modulation block 1003-1011;
//   - the conversions of evidence/conversions.md (rule (ii), kMorphModConversionRule) and the two presets added
//     for the columns no existing preset could carry (definitions 59-60; morphModProbeBase names the probe's base).
//
// ParamSetting, Capability and CapabilityGroup also exist in
// Vorago::PresetDefs / Seraphis::PresetDefs: different namespaces and different
// targets; no TU includes two defs headers.
// ==============================================================================

#include "plugin_ids.h"  // ${CMAKE_SOURCE_DIR}/plugins/profundum/src is on the include path.

#include "pluginterfaces/vst/vsttypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Profundum::PresetDefs {

/// A registered ID and its NORMALIZED value, as a host delivers it.
struct ParamSetting {
    Steinberg::Vst::ParamID id;
    double normalized;
};

/// The C-2.1 groups (Phase 9 shards), plus the reference columns.
enum class CapabilityGroup : std::uint8_t {
    Core,
    Character,
    Anchor,
    Pitch,
    Envelope,
    Mass,
    Field,
    Motion,
    Morph,
    NoteMemory,
    Macros,
    Reference,
    MorphMod   // Phase 14 C-14: the four Morph Modulation source columns
};

/// FR-010: the 50 AT columns in AT order, then the three reference columns. Phase 14 (FR-040) appends the four
/// Morph Modulation source columns, so every Phase 13 column keeps its index.
enum class Capability : std::uint8_t {
    AT01, AT02, AT03, AT04, AT05, AT06, AT07, AT08, AT09, AT10, AT11, AT12, AT13, AT14, AT15, AT16, AT17, AT18, AT19,
    AT20, AT21, AT22, AT23, AT24, AT25, AT26, AT27, AT28, AT29, AT30, AT31, AT32, AT33, AT34, AT35, AT36, AT37, AT38,
    AT39, AT40, AT41, AT42, AT43, AT44, AT45, AT46, AT47, AT48, AT49, AT50, RSolid, RLock, RSine,
    MEnv, MCycle, MVel, MWheel, Count
};
inline constexpr std::size_t kNumCapabilities = static_cast<std::size_t>(Capability::Count);
static_assert(kNumCapabilities == 57,
              "C-2.1: 50 AT columns + R-SOLID, R-LOCK, R-SINE; Phase 14 C-14: + M-ENV, M-CYCLE, M-VEL, M-WHEEL");

struct CapabilityInfo {
    Capability column;
    CapabilityGroup group;
    std::uint8_t atId;       // 1..50; 0 for a reference or MorphMod column
    std::string_view label;  // "AT-04 Character Tube", printed in coverage-matrix.csv
};

/// One row per Capability, in enumerator order (row i has column == Capability(i)).
/// Labels: "AT-nn <Group> <contrast words>" from the spec C-2.1 table.
[[nodiscard]] inline const std::array<CapabilityInfo, kNumCapabilities>& capabilityTable() {
    using C = Capability;
    using G = CapabilityGroup;
    static const std::array<CapabilityInfo, kNumCapabilities> kTable{{
        {C::AT01, G::Core, 1, "AT-01 Core Excitation Mix"},
        {C::AT02, G::Core, 2, "AT-02 Core Shape"},
        {C::AT03, G::Core, 3, "AT-03 Core Harmonic Fold"},
        {C::AT04, G::Character, 4, "AT-04 Character Tube"},
        {C::AT05, G::Character, 5, "AT-05 Character Wavefold"},
        {C::AT06, G::Character, 6, "AT-06 Character Rectify"},
        {C::AT07, G::Character, 7, "AT-07 Character Bite"},
        {C::AT08, G::Character, 8, "AT-08 Character Crush"},
        {C::AT09, G::Character, 9, "AT-09 Character Fracture"},
        {C::AT10, G::Anchor, 10, "AT-10 Anchor Level"},
        {C::AT11, G::Anchor, 11, "AT-11 Anchor Lock to Punch"},
        {C::AT12, G::Anchor, 12, "AT-12 Anchor Lock to Ghost"},
        {C::AT13, G::Anchor, 13, "AT-13 Anchor Lock to Follow"},
        {C::AT14, G::Anchor, 14, "AT-14 Anchor Sine to Triangle"},
        {C::AT15, G::Anchor, 15, "AT-15 Anchor Sine to Rounded Sine"},
        {C::AT16, G::Anchor, 16, "AT-16 Anchor Sine to Asymmetric Sine"},
        {C::AT17, G::Anchor, 17, "AT-17 Anchor Sine to Octave Down"},
        {C::AT18, G::Anchor, 18, "AT-18 Anchor Fold Feed"},
        {C::AT19, G::Pitch, 19, "AT-19 Pitch Drop Amount"},
        {C::AT20, G::Pitch, 20, "AT-20 Pitch Drop Time"},
        {C::AT21, G::Pitch, 21, "AT-21 Pitch Drop Shape"},
        {C::AT22, G::Pitch, 22, "AT-22 Pitch Drop Rebound"},
        {C::AT23, G::Pitch, 23, "AT-23 Pitch Glide"},
        {C::AT24, G::Envelope, 24, "AT-24 Envelope Amp Attack"},
        {C::AT25, G::Envelope, 25, "AT-25 Envelope Amp Release"},
        {C::AT26, G::Mass, 26, "AT-26 Mass Amount"},
        {C::AT27, G::Mass, 27, "AT-27 Mass Dynamics Gain"},
        {C::AT28, G::Mass, 28, "AT-28 Mass Transient Attack"},
        {C::AT29, G::Field, 29, "AT-29 Field Solid to Spread"},
        {C::AT30, G::Field, 30, "AT-30 Field Solid to Wide"},
        {C::AT31, G::Field, 31, "AT-31 Field Spread"},
        {C::AT32, G::Field, 32, "AT-32 Field Gravity"},
        {C::AT33, G::Field, 33, "AT-33 Field Motion"},
        {C::AT34, G::Motion, 34, "AT-34 Motion Cycle to Mass"},
        {C::AT35, G::Motion, 35, "AT-35 Motion Cycle to Depth"},
        {C::AT36, G::Motion, 36, "AT-36 Motion Cycle to Body"},
        {C::AT37, G::Motion, 37, "AT-37 Motion Cycle to Edge"},
        {C::AT38, G::Motion, 38, "AT-38 Motion Cycle to Shift"},
        {C::AT39, G::Motion, 39, "AT-39 Motion Env to Mass"},
        {C::AT40, G::Motion, 40, "AT-40 Motion Drift to Mass"},
        {C::AT41, G::Morph, 41, "AT-41 Morph Position Full"},
        {C::AT42, G::Morph, 42, "AT-42 Morph Position Half"},
        {C::AT43, G::NoteMemory, 43, "AT-43 NoteMemory AlwaysRetrigger"},
        {C::AT44, G::NoteMemory, 44, "AT-44 NoteMemory Legato"},
        {C::AT45, G::NoteMemory, 45, "AT-45 NoteMemory EnvelopeRetriggerOnly"},
        {C::AT46, G::NoteMemory, 46, "AT-46 NoteMemory OscillatorContinuous"},
        {C::AT47, G::Macros, 47, "AT-47 Macros Weight"},
        {C::AT48, G::Macros, 48, "AT-48 Macros Dark Bright"},
        {C::AT49, G::Macros, 49, "AT-49 Macros Tight Loose"},
        {C::AT50, G::Macros, 50, "AT-50 Macros Clean Violent"},
        {C::RSolid, G::Reference, 0, "R-SOLID Reference Solid"},
        {C::RLock, G::Reference, 0, "R-LOCK Reference Lock"},
        {C::RSine, G::Reference, 0, "R-SINE Reference Sine"},
        {C::MEnv, G::MorphMod, 0, "M-ENV Morph Mod Envelope"},
        {C::MCycle, G::MorphMod, 0, "M-CYCLE Morph Mod Cycle"},
        {C::MVel, G::MorphMod, 0, "M-VEL Morph Mod Velocity"},
        {C::MWheel, G::MorphMod, 0, "M-WHEEL Morph Mod Wheel"},
    }};
    return kTable;
}

/// Phase 14 plan 5.7 step 4: the two conversion rules the probe selection can apply. PerColumn = rule (i), one
/// conversion per column (the eligible carrier with the largest margin); EveryCarrier = rule (ii), every eligible
/// carrier converts for the column of its largest margin. A preset converts at most once under either rule.
enum class MorphModConversionRule : std::uint8_t { PerColumn, EveryCarrier };

/// R4 (evidence/plan-rulings.md, user 2026-10-26; FR-046): rule (ii). evidence/probe/select.js applies the same
/// rule; Profundum_PresetDefsRules re-derives the converted set from evidence/probe-morphmod.csv with this constant.
inline constexpr MorphModConversionRule kMorphModConversionRule = MorphModConversionRule::EveryCarrier;

/// == ProfundumEngine::kDefaultSeed (static_assert in the host).
inline constexpr std::uint32_t kFactoryDefaultSeed = 0xB455u;

struct ProfundumPresetDef {
    std::string_view name;                     // file stem and Info Name; unique; passes isValidPresetName
    std::string_view category;                 // one of kCategories
    std::string_view description;              // Info Comment; names the primary in words; no " & < > (written unescaped)
    Capability primary;
    std::vector<Capability> secondaries;
    std::vector<ParamSetting> endpointA;       // Sound-route IDs only (the 60 E-parameters)
    std::vector<ParamSetting> endpointB;       // Sound-route IDs only; EMPTY = endpoint B is a copy of endpoint A
    std::vector<ParamSetting> controls;        // 1, 100-103, 105, 410-416, 1000, 1002 (P-6 forbids 0, 2, 3, 417; also 4-7 and 1001)
                                               // and, from Phase 14, the Morph Modulation block 1003-1011
    std::string_view nameA;                    // <= 31 UTF-8 bytes, already sanitised, never "A"
    std::string_view nameB;                    // never "B"
    std::uint32_t seed = kFactoryDefaultSeed;  // C-4 step 6
    bool seedDependent = false;                // C-7; verified in both directions by FR-044
    // Phase 14 FR-046: a preset added for a MorphMod column no existing preset could carry names the probe preset
    // whose evidence/probe-morphmod.csv rows prove its source and settings (the NEW PRESET NEEDED row's base); empty
    // for every other definition (a converted preset is proven by its own rows).
    std::string_view morphModProbeBase{};
};

inline constexpr std::array<std::string_view, 6> kCategories{"Sub", "Resonant", "Harmonic", "Driven", "Plucked", "Animated"};

/// Definition order == file order. Rows are authored only after the probe table is recorded (FR-022,
/// evidence/probe-table.csv, T017). T023: plan 7 rows 51-53 (the reference columns); T025 adds rows 1-50 before them.
///
/// Normalized values, with the plain value each realises (laws: morph_state.h kFieldTable). Envelope times are
/// logarithmic over [0.1, 10000] ms: n = log10(ms / 0.1) / 5. Stepped lists are index / maxEnum: Character
/// {Tube, Fold, Rectify, Bite, Crush, Fracture} / 5, Image Mode {Solid, Spread, Wide} / 2, Anchor Waveform
/// {Sine, Triangle, RoundedSine, AsymmetricSine, OctaveDownSine} / 4, Anchor Mode {Lock, Follow, Punch, Ghost} / 3.
[[nodiscard]] inline const std::vector<ProfundumPresetDef>& allPresets() {
    using C = Capability;
    static const std::vector<ProfundumPresetDef> kPresets{
        // ---- 1: AT-01 excitation (plan 7 row 1; secondary AT-03) ----
        ProfundumPresetDef{
            .name = "Edge Ignition",
            .category = "Harmonic",
            .description = "Core Excitation Mix: a sine recipe pushed to edge 1 with the excitation mix and drive at full, "
                           "igniting with a fast 7 semitone drop; Harmonic Fold 50 adds folded overtones over a Triangle "
                           "Anchor. Morph to B burns it: Rectify, a lopsided drive and a wide image.",
            .primary = C::AT01,
            .secondaries = {C::AT03},
            // Whole-instrument pilot (2026-10-26): both claims measure nonlinearity against a linear twin, so the core
            // stays a sine and the variety sits outside that path: a Triangle Anchor, spread 0.5 and a 7 st, 40 ms
            // drop. A fuller recipe (depth 0.85, body 0.25, Shift +0.2, SHAPE 0.3) read AT-01 25.5 -> 4.1 dB and AT-03
            // 1.9 -> -0.6 dB, with or without Fracture at 0.25.
            .endpointA = {{kCoreDepthId, 1.0},            // depth 1
                          {kCoreBodyId, 0.0},             // body 0
                          {kCoreEdgeId, 1.0},             // edge 1
                          {kCoreExcitationMixId, 1.0},    // excitation mix 1
                          {kFoldAmountId, 0.5},           // Harmonic Fold 50
                          {kFoldShapeId, 0.0},            // SHAPE 0
                          {kCharacterId, 0.0},            // Tube
                          {kCharacterAmountId, 0.0},      // Amount 0
                          {kCharacterToneId, 0.5},        // Tone 0.5
                          {kAmpAttackId, 0.260206},       // 2 ms
                          {kAmpDecayId, 0.720412},        // 400 ms
                          {kAmpSustainId, 0.8},           // sustain 0.8
                          {kAmpReleaseId, 0.695424},      // 300 ms
                          {kFieldImageModeId, 0.5},       // SPREAD
                          {kFieldSpreadId, 0.5},          // spread 0.5
                          {kMassId, 0.55},                // Mass 55
                          {kAnchorWaveformId, 0.25},      // Triangle
                          {kAnchorModeId, 0.0},           // LOCK
                          {kAnchorLevelId, 0.8},          // level 0.8
                          {kAnchorFoldFeedId, 0.0},       // Anchor Fold Feed 0
                          {kCoreExcitationDriveId, 1.0},  // drive 1
                          {kDropAmountId, 0.145833},      // drop 7 st
                          {kDropTimeId, 0.444762}},       // drop time 40 ms
            // Morph pilot (2026-10-26): B is a designed destination, not a nudge of A: Rectify at Amount 0.4 over a
            // lopsided drive, Fold 60 at HOLLOW, a fuller body, an Asymmetric Sine Anchor at level 1 and the WIDE image
            // (Rectify 0.7 with Fold 80 read 3.7 - 4.2 dB under A at C3).
            .endpointB = {{kCoreDepthId, 0.8},                 // depth 0.8
                          {kCoreBodyId, 0.3},                  // body 0.3
                          {kCoreEdgeId, 1.0},                  // edge 1
                          {kCoreExcitationMixId, 1.0},         // excitation mix 1
                          {kFoldAmountId, 0.6},                // Harmonic Fold 60
                          {kFoldShapeId, 0.5},                 // SHAPE 0.5 (HOLLOW)
                          {kCharacterId, 0.4},                 // Rectify
                          {kCharacterAmountId, 0.4},           // Amount 0.4
                          {kCharacterToneId, 0.7},             // Tone 0.7
                          {kAmpAttackId, 0.260206},            // 2 ms
                          {kAmpDecayId, 0.720412},             // 400 ms
                          {kAmpSustainId, 0.95},               // sustain 0.95
                          {kAmpReleaseId, 0.695424},           // 300 ms
                          {kFieldImageModeId, 1.0},            // WIDE
                          {kFieldSpreadId, 0.8},               // spread 0.8
                          {kMassId, 0.8},                      // Mass 80
                          {kAnchorWaveformId, 0.75},           // Asymmetric Sine
                          {kAnchorModeId, 0.0},                // LOCK
                          {kAnchorLevelId, 1.0},               // level 1
                          {kAnchorFoldFeedId, 0.0},            // Anchor Fold Feed 0
                          {kCoreExcitationDriveId, 1.0},       // drive 1
                          {kCoreExcitationAsymmetryId, 0.8}},  // asymmetry 0.8
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Ignition",
            .nameB = "Rectified Burn",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 2: AT-02 SHAPE (plan 7 row 2) ----
        // T027 FR-061 level: sustain 0.7 -> 1, depth 0 -> 0.3 (-16.64 -> -13.31 dBFS).
        ProfundumPresetDef{
            .name = "Biting Reed",
            .category = "Harmonic",
            .description = "Core Shape: a saw-like recipe at edge 0.5 with SHAPE at 1, the BITE end, snapping wide; "
                           "the reed snarls on every note. Morph to B warms it into a mono Tube reed an octave deep.",
            .primary = C::AT02,
            .secondaries = {},
            // Designed-library pass (2026-10-26): AT-02 reads brightness at the Sum tap (margin 1.48), so the recipe
            // and Anchor stay; A gains a WIDE image, a 2 st snap and a harder transient.
            .endpointA = {{kCoreDepthId, 0.3},                      // depth 0.3
                          {kCoreBodyId, 0.0},                       // body 0
                          {kCoreEdgeId, 0.5},                       // edge 0.5
                          {kCoreExcitationMixId, 0.0},              // excitation mix 0
                          {kFoldAmountId, 0.0},                     // Harmonic Fold 0
                          {kFoldShapeId, 1.0},                      // SHAPE 1
                          {kCharacterId, 0.0},                      // Tube
                          {kCharacterAmountId, 0.0},                // Amount 0
                          {kCharacterToneId, 0.5},                  // Tone 0.5
                          {kAmpAttackId, 0.295424},                 // 3 ms
                          {kAmpDecayId, 0.695424},                  // 300 ms
                          {kAmpSustainId, 1.0},                     // sustain 1
                          {kAmpReleaseId, 0.679588},                // 250 ms
                          {kFieldImageModeId, 1.0},                 // WIDE
                          {kFieldSpreadId, 0.6},                    // spread 0.6
                          {kMassId, 0.5},                           // Mass 50
                          {kAnchorWaveformId, 0.0},                 // Sine
                          {kAnchorModeId, 0.0},                     // LOCK
                          {kAnchorLevelId, 0.6},                    // level 0.6
                          {kAnchorFoldFeedId, 0.0},                 // Anchor Fold Feed 0
                          {kCoreShiftId, 0.5},                      // shift 0
                          {kDropAmountId, 0.041667},                // drop 2 st
                          {kDropTimeId, 0.361191},                  // drop time 20 ms
                          {kDynamicsTransientAttackId, 0.666667}},  // transient attack +4 dB
            // Designed-library pass (2026-10-26): B warms the reed: a body, excitation, a ROUND-leaning fold and Tube
            // 0.5 over a mono Octave Down Anchor.
            .endpointB = {{kCoreDepthId, 0.6},          // depth 0.6
                          {kCoreBodyId, 0.5},           // body 0.5
                          {kCoreEdgeId, 0.3},           // edge 0.3
                          {kCoreExcitationMixId, 0.4},  // excitation mix 0.4
                          {kFoldAmountId, 0.4},         // Harmonic Fold 40
                          {kFoldShapeId, 0.3},          // SHAPE 0.3
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.5},    // Amount 0.5
                          {kCharacterToneId, 0.4},      // Tone 0.4
                          {kAmpAttackId, 0.295424},     // 3 ms
                          {kAmpDecayId, 0.695424},      // 300 ms
                          {kAmpSustainId, 1.0},         // sustain 1
                          {kAmpReleaseId, 0.679588},    // 250 ms
                          {kFieldImageModeId, 0.0},     // SOLID
                          {kFieldSpreadId, 0.0},        // spread 0
                          {kMassId, 0.6},               // Mass 60
                          {kAnchorWaveformId, 1.0},     // OctaveDownSine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.8},        // level 0.8
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kCoreShiftId, 0.5}},         // shift 0
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Full Bite",
            .nameB = "Warm Reed",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 3: AT-03 Fold (plan 7 row 3) ----
        ProfundumPresetDef{
            .name = "Folded Mass",
            .category = "Driven",
            .description = "Core Harmonic Fold: Harmonic Fold at 100 over a deep core with some body, sharpened by Bite and "
                           "spread wide, with an Asymmetric Anchor at 0.6 holding the floor. Morph to B crushes it: Crush, "
                           "the BITE shape and an octave-down Anchor.",
            .primary = C::AT03,
            .secondaries = {},
            // Whole-instrument pilot (2026-10-26): A keeps its AT-03 claim (Fold 100) and uses the rest: body and a
            // little excitation, Shift down, Bite at Amount 0.4, an Asymmetric Sine Anchor and the WIDE image. The
            // fuller recipe narrows the AT-03 margin (12.8 -> 1.2 dB at E1); Bite 0.4 vs 0.25 read 1.19 vs 1.17 dB.
            .endpointA = {{kCoreDepthId, 0.7},          // depth 0.7
                          {kCoreBodyId, 0.4},           // body 0.4
                          {kCoreEdgeId, 0.3},           // edge 0.3
                          {kCoreExcitationMixId, 0.2},  // excitation mix 0.2
                          {kFoldAmountId, 1.0},         // Harmonic Fold 100
                          {kFoldShapeId, 0.2},          // SHAPE 0.2
                          {kCharacterId, 0.6},          // Bite
                          {kCharacterAmountId, 0.4},    // Amount 0.4
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.260206},     // 2 ms
                          {kAmpDecayId, 0.739794},      // 500 ms
                          {kAmpSustainId, 0.8},         // sustain 0.8
                          {kAmpReleaseId, 0.695424},    // 300 ms
                          {kFieldImageModeId, 1.0},     // WIDE
                          {kFieldSpreadId, 0.5},        // spread 0.5
                          {kMassId, 0.7},               // Mass 70
                          {kAnchorWaveformId, 0.75},    // Asymmetric Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.6},        // level 0.6
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kCoreShiftId, 0.4}},         // shift -0.2
            // Morph pilot (2026-10-26): B crushes the fold: Crush at Amount 0.45, Fold SHAPE at BITE, a full body with
            // some excitation, Mass 90, an Octave Down Anchor at level 1, full sustain and the SPREAD image (Crush 0.6
            // at Mass 85 read 6.5 dB under A at E1).
            .endpointB = {{kCoreDepthId, 0.7},          // depth 0.7
                          {kCoreBodyId, 0.7},           // body 0.7
                          {kCoreEdgeId, 0.5},           // edge 0.5
                          {kCoreExcitationMixId, 0.4},  // excitation mix 0.4
                          {kFoldAmountId, 1.0},         // Harmonic Fold 100
                          {kFoldShapeId, 1.0},          // SHAPE 1 (BITE)
                          {kCharacterId, 0.8},          // Crush
                          {kCharacterAmountId, 0.45},   // Amount 0.45
                          {kCharacterToneId, 0.4},      // Tone 0.4
                          {kAmpAttackId, 0.260206},     // 2 ms
                          {kAmpDecayId, 0.739794},      // 500 ms
                          {kAmpSustainId, 1.0},         // sustain 1
                          {kAmpReleaseId, 0.695424},    // 300 ms
                          {kFieldImageModeId, 0.5},     // SPREAD
                          {kFieldSpreadId, 0.5},        // spread 0.5
                          {kMassId, 0.9},               // Mass 90
                          {kAnchorWaveformId, 1.0},     // Octave Down
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 1.0},        // level 1
                          {kAnchorFoldFeedId, 0.0}},    // Anchor Fold Feed 0
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Folded Floor",
            .nameB = "Crushed Body",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 4: AT-04 Tube (plan 7 row 4) ----
        // T027: the AT-26 secondary is withdrawn: Tube at Amount 1 holds the sub, Mass 0 -> 90 read 0.34 / 0.12 dB.
        ProfundumPresetDef{
            .name = "Valve Pressure",
            .category = "Driven",
            .description = "Character Tube: the Tube algorithm at Amount 1 for full valve pressure, with Mass 90 over "
                           "an octave-down Anchor at 0.5, wide. Morph to B folds it: a body, excitation and a BITE "
                           "fold under a brighter Tube, mono.",
            .primary = C::AT04,
            .secondaries = {},
            // Designed-library pass (2026-10-26): AT-04 had 0.82 dB of margin, so the core and Character stay; A gains
            // an Octave Down Anchor and a WIDE image (with a 3 st drop as well, AT-04 read 0.22 dB).
            .endpointA = {{kCoreDepthId, 0.7},          // depth 0.7
                          {kCoreBodyId, 0.35},          // body 0.35
                          {kCoreEdgeId, 0.3},           // edge 0.3
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 1.0},    // Amount 1
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.295424},     // 3 ms
                          {kAmpDecayId, 0.720412},      // 400 ms
                          {kAmpSustainId, 0.85},        // sustain 0.85
                          {kAmpReleaseId, 0.695424},    // 300 ms
                          {kFieldImageModeId, 1.0},     // WIDE
                          {kFieldSpreadId, 0.5},        // spread 0.5
                          {kMassId, 0.9},               // Mass 90
                          {kAnchorWaveformId, 1.0},     // OctaveDownSine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.5},        // level 0.5
                          {kAnchorFoldFeedId, 0.0}},    // Anchor Fold Feed 0
            // Designed-library pass (2026-10-26): B folds a body under a brighter Tube 0.5, mono, over a Triangle
            // Anchor (Tube on both: AT-04).
            .endpointB = {{kCoreDepthId, 0.45},         // depth 0.45
                          {kCoreBodyId, 0.6},           // body 0.6
                          {kCoreEdgeId, 0.7},           // edge 0.7
                          {kCoreExcitationMixId, 0.4},  // excitation mix 0.4
                          {kFoldAmountId, 0.55},        // Harmonic Fold 55
                          {kFoldShapeId, 0.8},          // SHAPE 0.8
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.5},    // Amount 0.5
                          {kCharacterToneId, 0.8},      // Tone 0.8
                          {kAmpAttackId, 0.295424},     // 3 ms
                          {kAmpDecayId, 0.720412},      // 400 ms
                          {kAmpSustainId, 0.85},        // sustain 0.85
                          {kAmpReleaseId, 0.695424},    // 300 ms
                          {kFieldImageModeId, 0.0},     // SOLID
                          {kFieldSpreadId, 0.0},        // spread 0
                          {kMassId, 0.75},              // Mass 75
                          {kAnchorWaveformId, 0.25},    // Triangle
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.7},        // level 0.7
                          {kAnchorFoldFeedId, 0.0}},    // Anchor Fold Feed 0
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Hot Valve",
            .nameB = "Folded Valve",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 5: AT-05 Wavefold (plan 7 row 5) ----
        ProfundumPresetDef{
            .name = "Folding Wire",
            .category = "Driven",
            .description = "Character Wavefold: the Fold algorithm at Amount 1, a wire-bright wavefold over a mid core "
                           "and a Triangle Anchor, snapping wide. Morph to B softens it into a mono, lightly folded "
                           "body an octave deep.",
            .primary = C::AT05,
            .secondaries = {},
            // Designed-library pass (2026-10-26): AT-05 had 5.03 of margin; A gains a Triangle Anchor, a WIDE image and
            // a 4 st snap (SHAPE 0.3 ahead of the Character stage read AT-05 at -0.81).
            .endpointA = {{kCoreDepthId, 0.6},          // depth 0.6
                          {kCoreBodyId, 0.3},           // body 0.3
                          {kCoreEdgeId, 0.4},           // edge 0.4
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.2},          // Fold
                          {kCharacterAmountId, 1.0},    // Amount 1
                          {kCharacterToneId, 0.6},      // Tone 0.6
                          {kAmpAttackId, 0.2},          // 1 ms
                          {kAmpDecayId, 0.679588},      // 250 ms
                          {kAmpSustainId, 0.7},         // sustain 0.7
                          {kAmpReleaseId, 0.660206},    // 200 ms
                          {kFieldImageModeId, 1.0},     // WIDE
                          {kFieldSpreadId, 0.7},        // spread 0.7
                          {kMassId, 0.5},               // Mass 50
                          {kAnchorWaveformId, 0.25},    // Triangle
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.6},        // level 0.6
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kDropAmountId, 0.083333},    // drop 4 st
                          {kDropTimeId, 0.410077}},     // drop time 30 ms
            // Designed-library pass (2026-10-26): B softens the wire: Fold at 0.35 over a fuller body with excitation,
            // a light fold and Shift down, mono, over an Octave Down Anchor (Fold on both: AT-05).
            .endpointB = {{kCoreDepthId, 0.85},         // depth 0.85
                          {kCoreBodyId, 0.7},           // body 0.7
                          {kCoreEdgeId, 0.2},           // edge 0.2
                          {kCoreExcitationMixId, 0.4},  // excitation mix 0.4
                          {kFoldAmountId, 0.4},         // Harmonic Fold 40
                          {kFoldShapeId, 0.2},          // SHAPE 0.2
                          {kCharacterId, 0.2},          // Fold
                          {kCharacterAmountId, 0.35},   // Amount 0.35
                          {kCharacterToneId, 0.2},      // Tone 0.2
                          {kAmpAttackId, 0.2},          // 1 ms
                          {kAmpDecayId, 0.679588},      // 250 ms
                          {kAmpSustainId, 0.7},         // sustain 0.7
                          {kAmpReleaseId, 0.75563},     // 600 ms
                          {kFieldImageModeId, 0.0},     // SOLID
                          {kFieldSpreadId, 0.0},        // spread 0
                          {kMassId, 0.7},               // Mass 70
                          {kAnchorWaveformId, 1.0},     // OctaveDownSine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.6},        // level 0.6
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kCoreShiftId, 0.25}},        // shift -0.5
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Live Wire",
            .nameB = "Soft Wire",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 6: AT-06 Rectify (plan 7 row 6) ----
        // Phase 14 T023: converted to M-ENV (AT-06 stays with Poly Rectifier).
        ProfundumPresetDef{
            .name = "Rectified Floor",
            .category = "Driven",
            .description = "Morph Mod Envelope: every note leaps 60 percent of the way from the Rectify floor toward "
                           "its folded B, then settles near a third (Amount 0.6, an instant attack, Sustain 0.6). A "
                           "is Rectify at Amount 1 with Tone 0.4 over a floor-heavy, lightly folded core and an "
                           "Asymmetric Anchor at 0.6; B folds a fuller body under a lighter Rectify, wide.",
            .primary = C::MEnv,
            .secondaries = {},
            // Designed-library pass (2026-10-26): AT-06 had 21.9 of margin; A gains a body, excitation, a light fold,
            // an Asymmetric Anchor and a spread top.
            .endpointA = {{kCoreDepthId, 0.9},          // depth 0.9
                          {kCoreBodyId, 0.3},           // body 0.3
                          {kCoreEdgeId, 0.3},           // edge 0.3
                          {kCoreExcitationMixId, 0.2},  // excitation mix 0.2
                          {kFoldAmountId, 0.2},         // Harmonic Fold 20
                          {kFoldShapeId, 0.3},          // SHAPE 0.3
                          {kCharacterId, 0.4},          // Rectify
                          {kCharacterAmountId, 1.0},    // Amount 1
                          {kCharacterToneId, 0.4},      // Tone 0.4
                          {kAmpAttackId, 0.260206},     // 2 ms
                          {kAmpDecayId, 0.695424},      // 300 ms
                          {kAmpSustainId, 0.9},         // sustain 0.9
                          {kAmpReleaseId, 0.679588},    // 250 ms
                          {kFieldImageModeId, 0.5},     // SPREAD
                          {kFieldSpreadId, 0.4},        // spread 0.4
                          {kMassId, 0.55},              // Mass 55
                          {kAnchorWaveformId, 0.75},    // AsymmetricSine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.6},        // level 0.6
                          {kAnchorFoldFeedId, 0.0}},    // Anchor Fold Feed 0
            // Designed-library pass (2026-10-26): B folds a fuller body under Rectify 0.45, wide (Rectify on both:
            // AT-06).
            .endpointB = {{kCoreDepthId, 0.5},          // depth 0.5
                          {kCoreBodyId, 0.5},           // body 0.5
                          {kCoreEdgeId, 0.6},           // edge 0.6
                          {kCoreExcitationMixId, 0.5},  // excitation mix 0.5
                          {kFoldAmountId, 0.5},         // Harmonic Fold 50
                          {kFoldShapeId, 0.8},          // SHAPE 0.8
                          {kCharacterId, 0.4},          // Rectify
                          {kCharacterAmountId, 0.45},   // Amount 0.45
                          {kCharacterToneId, 0.55},     // Tone 0.55
                          {kAmpAttackId, 0.260206},     // 2 ms
                          {kAmpDecayId, 0.695424},      // 300 ms
                          {kAmpSustainId, 1.0},         // sustain 1
                          {kAmpReleaseId, 0.679588},    // 250 ms
                          {kFieldImageModeId, 1.0},     // WIDE
                          {kFieldSpreadId, 0.7},        // spread 0.7
                          {kMassId, 0.7},               // Mass 70
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.8},        // level 0.8
                          {kAnchorFoldFeedId, 0.0}},    // Anchor Fold Feed 0
            // Phase 14 T023 (FR-046, plan 5.7 step 5): the winning M-ENV candidate of evidence/conversions.md (margin
            // 3.565 dB, rule (ii)), its nine fields through the 1003-1011 laws (morph_mod_params.h).
            .controls = {{kMorphPositionId, 0.0},                       // p = 0
                         {kMorphModSourceId, 0.25},                     // Source Envelope (1 / 4)
                         {kMorphModAmountId, 0.8},                      // Amount +0.6
                         {kMorphModAttackId, 0.0},                      // attack 0.1 ms
                         {kMorphModDecayId, 0.75563024976044124},       // decay 600 ms
                         {kMorphModSustainId, 0.6},                     // sustain 0.6
                         {kMorphModCycleShapeId, 0.0},                  // Cycle Shape Sine (default)
                         {kMorphModCycleRateId, 0.60587150266824885},   // Cycle Rate 1 Hz (default)
                         {kMorphModCycleSyncId, 0.0},                   // Sync Off (default)
                         {kMorphModCycleSyncRateId, 13.0 / 29.0}},      // Sync Rate 1/4 (index 13, default)
            .nameA = "Rectified",
            .nameB = "Folded Rectifier",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 7: AT-07 Bite (plan 7 row 7; secondary AT-28) ----
        ProfundumPresetDef{
            .name = "Bite Down",
            .category = "Driven",
            .description = "Character Bite: Bite at Amount 1 with the Transient Attack at +12 dB over a Rounded "
                           "Anchor, so every note bites down, wide. Morph to B folds it into a mono body an octave "
                           "deep with half the Bite and no transient boost.",
            .primary = C::AT07,
            .secondaries = {C::AT28},
            // Designed-library pass (2026-10-26): AT-07 had 9.47 of margin, AT-28 7.84; A gains a Rounded Anchor, a
            // WIDE image and a 3 st snap (a light fold read AT-07 at -0.10).
            .endpointA = {{kCoreDepthId, 0.7},                // depth 0.7
                          {kCoreBodyId, 0.3},                 // body 0.3
                          {kCoreEdgeId, 0.3},                 // edge 0.3
                          {kCoreExcitationMixId, 0.0},        // excitation mix 0
                          {kFoldAmountId, 0.0},               // Harmonic Fold 0
                          {kFoldShapeId, 0.0},                // SHAPE 0
                          {kCharacterId, 0.6},                // Bite
                          {kCharacterAmountId, 1.0},          // Amount 1
                          {kCharacterToneId, 0.55},           // Tone 0.55
                          {kAmpAttackId, 0.2},                // 1 ms
                          {kAmpDecayId, 0.708814},            // 350 ms
                          {kAmpSustainId, 0.6},               // sustain 0.6
                          {kAmpReleaseId, 0.660206},          // 200 ms
                          {kFieldImageModeId, 1.0},           // WIDE
                          {kFieldSpreadId, 0.6},              // spread 0.6
                          {kMassId, 0.5},                     // Mass 50
                          {kAnchorWaveformId, 0.5},           // RoundedSine
                          {kAnchorModeId, 0.0},               // LOCK
                          {kAnchorLevelId, 0.6},              // level 0.6
                          {kAnchorFoldFeedId, 0.0},           // Anchor Fold Feed 0
                          {kDynamicsTransientAttackId, 1.0},  // transient attack +12 dB
                          {kDropAmountId, 0.0625},            // drop 3 st
                          {kDropTimeId, 0.388095}},           // drop time 25 ms
            // Designed-library pass (2026-10-26): B is a folded, mono body an octave deep with Bite 0.5, Shift up and
            // no transient boost (Bite on both: AT-07).
            .endpointB = {{kCoreDepthId, 0.85},               // depth 0.85
                          {kCoreBodyId, 0.5},                 // body 0.5
                          {kCoreEdgeId, 0.2},                 // edge 0.2
                          {kCoreExcitationMixId, 0.5},        // excitation mix 0.5
                          {kFoldAmountId, 0.45},              // Harmonic Fold 45
                          {kFoldShapeId, 0.6},                // SHAPE 0.6
                          {kCharacterId, 0.6},                // Bite
                          {kCharacterAmountId, 0.5},          // Amount 0.5
                          {kCharacterToneId, 0.4},            // Tone 0.4
                          {kAmpAttackId, 0.2},                // 1 ms
                          {kAmpDecayId, 0.75563},             // 600 ms
                          {kAmpSustainId, 0.8},               // sustain 0.8
                          {kAmpReleaseId, 0.660206},          // 200 ms
                          {kFieldImageModeId, 0.0},           // SOLID
                          {kFieldSpreadId, 0.0},              // spread 0
                          {kMassId, 0.75},                    // Mass 75
                          {kAnchorWaveformId, 1.0},           // OctaveDownSine
                          {kAnchorModeId, 0.0},               // LOCK
                          {kAnchorLevelId, 0.6},              // level 0.6
                          {kAnchorFoldFeedId, 0.0},           // Anchor Fold Feed 0
                          {kDynamicsTransientAttackId, 0.5},  // transient attack 0 dB
                          {kCoreShiftId, 0.75}},              // shift 0.5
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Hard Bite",
            .nameB = "Soft Bite",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 8: AT-08 Crush (plan 7 row 8) ----
        ProfundumPresetDef{
            .name = "Crushed Basalt",
            .category = "Driven",
            .description = "Character Crush: Crush at Amount 1 grinds a lightly folded core while a Triangle Anchor at "
                           "0.5 keeps the fundamental. Morph to B turns it lopsided under a lighter Crush, an octave "
                           "deep and wide.",
            .primary = C::AT08,
            .secondaries = {},
            // Designed-library pass (2026-10-26): AT-08 had 13.4 of margin; A gains a body, excitation, a light fold, a
            // Triangle Anchor and a spread top.
            .endpointA = {{kCoreDepthId, 0.8},          // depth 0.8
                          {kCoreBodyId, 0.4},           // body 0.4
                          {kCoreEdgeId, 0.3},           // edge 0.3
                          {kCoreExcitationMixId, 0.2},  // excitation mix 0.2
                          {kFoldAmountId, 0.2},         // Harmonic Fold 20
                          {kFoldShapeId, 0.3},          // SHAPE 0.3
                          {kCharacterId, 0.8},          // Crush
                          {kCharacterAmountId, 1.0},    // Amount 1
                          {kCharacterToneId, 0.45},     // Tone 0.45
                          {kAmpAttackId, 0.260206},     // 2 ms
                          {kAmpDecayId, 0.720412},      // 400 ms
                          {kAmpSustainId, 0.8},         // sustain 0.8
                          {kAmpReleaseId, 0.695424},    // 300 ms
                          {kFieldImageModeId, 0.5},     // SPREAD
                          {kFieldSpreadId, 0.4},        // spread 0.4
                          {kMassId, 0.6},               // Mass 60
                          {kAnchorWaveformId, 0.25},    // Triangle
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.5},        // level 0.5
                          {kAnchorFoldFeedId, 0.0}},    // Anchor Fold Feed 0
            // Designed-library pass (2026-10-26): B is a lopsided basalt under Crush 0.55 over an Octave Down Anchor,
            // wide (Crush on both: AT-08).
            .endpointB = {{kCoreDepthId, 0.5},                 // depth 0.5
                          {kCoreBodyId, 0.3},                  // body 0.3
                          {kCoreEdgeId, 0.7},                  // edge 0.7
                          {kCoreExcitationMixId, 0.5},         // excitation mix 0.5
                          {kFoldAmountId, 0.5},                // Harmonic Fold 50
                          {kFoldShapeId, 0.6},                 // SHAPE 0.6
                          {kCharacterId, 0.8},                 // Crush
                          {kCharacterAmountId, 0.55},          // Amount 0.55
                          {kCharacterToneId, 0.6},             // Tone 0.6
                          {kAmpAttackId, 0.260206},            // 2 ms
                          {kAmpDecayId, 0.720412},             // 400 ms
                          {kAmpSustainId, 0.95},               // sustain 0.95
                          {kAmpReleaseId, 0.695424},           // 300 ms
                          {kFieldImageModeId, 1.0},            // WIDE
                          {kFieldSpreadId, 0.6},               // spread 0.6
                          {kMassId, 0.7},                      // Mass 70
                          {kAnchorWaveformId, 1.0},            // OctaveDownSine
                          {kAnchorModeId, 0.0},                // LOCK
                          {kAnchorLevelId, 0.5},               // level 0.5
                          {kAnchorFoldFeedId, 0.0},            // Anchor Fold Feed 0
                          {kCoreExcitationDriveId, 0.8},       // drive 0.8
                          {kCoreExcitationAsymmetryId, 0.8}},  // asymmetry 0.8
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Crushed",
            .nameB = "Lopsided Basalt",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 9: AT-09 Fracture (plan 7 row 9; secondary AT-13, the Fracture variant (P-2)) ----
        // T027 FR-061 level: Anchor 0.5 -> 1, Mass 50 -> 100 (-22.18 -> -13.73 dBFS).
        // Endpoint A is the probe's AT-09 base (B_s + Fracture at Amount 1) with the Anchor in FOLLOW, so the
        // AT-13 Fracture-variant claim reads P as stored (P-2: Amount 1 on endpoint A, p = 0).
        // G22 re-author (FR-048 / SC-028): Fold SHAPE 1 on both endpoints, not 0.5. At SHAPE 0.5 a load under a held E1
        // read step ratio 2.53 against the 2x bar (the un-fractured core edge during the Character selection transition);
        // at SHAPE 1 it reads 1.12, with AT-09 at 26.8 / 29.1 dB (bar 3) and AT-13 passing at E1 and C3.
        ProfundumPresetDef{
            .name = "Fault Line",
            .category = "Driven",
            .description = "Character Fracture: Fracture at Amount 1 on a harmonic-rich core; the Anchor in FOLLOW "
                           "tracks the fractured pitch. Morph to B softens it: a fuller, narrower body under a lighter "
                           "Fracture.",
            .primary = C::AT09,
            .secondaries = {C::AT13},
            .endpointA = {{kCoreDepthId, 0.5},                // depth 0.5
                          {kCoreBodyId, 0.3},                 // body 0.3
                          {kCoreEdgeId, 1.0},                 // edge 1
                          {kCoreShiftId, 0.0},                // shift -1
                          {kCoreExcitationMixId, 1.0},        // excitation mix 1
                          {kCoreExcitationDriveId, 1.0},      // drive 1
                          {kCoreExcitationAsymmetryId, 0.5},  // asymmetry 0.5
                          {kFoldAmountId, 0.8},               // Harmonic Fold 80
                          {kFoldShapeId, 1.0},                // SHAPE 1 (FR-048 held load)
                          {kCharacterId, 1.0},                // Fracture
                          {kCharacterAmountId, 1.0},          // Amount 1
                          {kCharacterToneId, 0.7},            // Tone 0.7
                          {kAmpAttackId, 0.2},                // 1 ms
                          {kAmpDecayId, 0.835218},            // 1500 ms
                          {kAmpSustainId, 1.0},               // sustain 1
                          {kAmpReleaseId, 0.75563},           // 600 ms
                          {kFieldImageModeId, 1.0},           // WIDE
                          {kFieldSpreadId, 1.0},              // spread 1
                          {kMassId, 1.0},                     // Mass 100
                          {kAnchorWaveformId, 0.0},           // Sine
                          {kAnchorModeId, 1.0 / 3.0},         // FOLLOW
                          {kAnchorLevelId, 1.0},              // level 1
                          {kAnchorFoldFeedId, 0.0}},          // Anchor Fold Feed 0
            // Designed-library pass (2026-10-26): A is unchanged (the FR-048 held-load author); B is a fuller, narrower
            // body under Fracture 0.6 on the same SHAPE 1 (Fracture on both: AT-09).
            .endpointB = {{kCoreDepthId, 0.85},               // depth 0.85
                          {kCoreBodyId, 0.5},                 // body 0.5
                          {kCoreEdgeId, 0.3},                 // edge 0.3
                          {kCoreShiftId, 0.5},                // shift 0
                          {kCoreExcitationMixId, 0.2},        // excitation mix 0.2
                          {kCoreExcitationDriveId, 1.0},      // drive 1
                          {kCoreExcitationAsymmetryId, 0.5},  // asymmetry 0.5
                          {kFoldAmountId, 0.2},               // Harmonic Fold 20
                          {kFoldShapeId, 1.0},                // SHAPE 1 (FR-048 held load)
                          {kCharacterId, 1.0},                // Fracture
                          {kCharacterAmountId, 0.6},          // Amount 0.6
                          {kCharacterToneId, 0.4},            // Tone 0.4
                          {kAmpAttackId, 0.2},                // 1 ms
                          {kAmpDecayId, 0.835218},            // 1500 ms
                          {kAmpSustainId, 0.6},               // sustain 0.6
                          {kAmpReleaseId, 0.75563},           // 600 ms
                          {kFieldImageModeId, 0.5},           // SPREAD
                          {kFieldSpreadId, 0.5},              // spread 0.5
                          {kMassId, 0.8},                     // Mass 80
                          {kAnchorWaveformId, 0.0},           // Sine
                          {kAnchorModeId, 1.0 / 3.0},         // FOLLOW
                          {kAnchorLevelId, 1.0},              // level 1
                          {kAnchorFoldFeedId, 0.0}},          // Anchor Fold Feed 0
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Fractured",
            .nameB = "Soft Fault",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 10: AT-10 Anchor level (plan 7 row 10) ----
        // T027 FR-061 level: sustain 1 -> 0.5 (-5.10 -> -11.12 dBFS).
        ProfundumPresetDef{
            .name = "Bedrock",
            .category = "Sub",
            .description = "Anchor Level: the Sine Anchor in LOCK at level 1 under a dark core grained by the Fold Character, "
                           "with Mass 70 for a heavy floor and a faint spread on top. Morph to B warms it into a driven "
                           "Tube bass with a body.",
            .primary = C::AT10,
            .secondaries = {},
            // Whole-instrument pilot (2026-10-26): A keeps its AT-10 claim (Sine, LOCK) and uses the rest: a little
            // body and excitation, a light ROUND fold, Shift down, the Fold Character at Amount 0.3 and a SPREAD top.
            .endpointA = {{kCoreDepthId, 0.85},               // depth 0.85
                          {kCoreBodyId, 0.2},                 // body 0.2
                          {kCoreEdgeId, 0.1},                 // edge 0.1
                          {kCoreExcitationMixId, 0.15},       // excitation mix 0.15
                          {kFoldAmountId, 0.15},              // Harmonic Fold 15
                          {kFoldShapeId, 0.0},                // SHAPE 0
                          {kCharacterId, 0.2},                // Fold
                          {kCharacterAmountId, 0.3},          // Amount 0.3
                          {kCharacterToneId, 0.3},            // Tone 0.3
                          {kAmpAttackId, 0.295424},           // 3 ms
                          {kAmpDecayId, 0.660206},            // 200 ms
                          {kAmpSustainId, 0.5},               // sustain 0.5
                          {kAmpReleaseId, 0.679588},          // 250 ms
                          {kFieldImageModeId, 0.5},           // SPREAD
                          {kFieldSpreadId, 0.25},             // spread 0.25
                          {kMassId, 0.7},                     // Mass 70
                          {kAnchorWaveformId, 0.0},           // Sine
                          {kAnchorModeId, 0.0},               // LOCK
                          {kAnchorLevelId, 1.0},              // level 1
                          {kAnchorFoldFeedId, 0.0},           // Anchor Fold Feed 0
                          {kDynamicsTransientAttackId, 0.5},  // transient attack 0 dB
                          {kCoreShiftId, 0.4}},               // shift -0.2
            // Morph pilot (2026-10-26): B is a warm driven bass on the same floor: Tube at Amount 0.8, a body and some
            // edge, half the excitation, a light fold, Mass 85 and a harder transient. The Anchor stays Sine on both
            // endpoints: the AT-10 primary's precondition (library rule 8).
            .endpointB = {{kCoreDepthId, 0.8},                // depth 0.8
                          {kCoreBodyId, 0.45},                // body 0.45
                          {kCoreEdgeId, 0.3},                 // edge 0.3
                          {kCoreExcitationMixId, 0.5},        // excitation mix 0.5
                          {kFoldAmountId, 0.2},               // Harmonic Fold 20
                          {kFoldShapeId, 0.0},                // SHAPE 0
                          {kCharacterId, 0.0},                // Tube
                          {kCharacterAmountId, 0.8},          // Amount 0.8
                          {kCharacterToneId, 0.6},            // Tone 0.6
                          {kAmpAttackId, 0.295424},           // 3 ms
                          {kAmpDecayId, 0.660206},            // 200 ms
                          {kAmpSustainId, 0.5},               // sustain 0.5
                          {kAmpReleaseId, 0.679588},          // 250 ms
                          {kFieldImageModeId, 0.0},           // SOLID
                          {kFieldSpreadId, 0.0},              // spread 0
                          {kMassId, 0.85},                    // Mass 85
                          {kAnchorWaveformId, 0.0},           // Sine
                          {kAnchorModeId, 0.0},               // LOCK
                          {kAnchorLevelId, 1.0},              // level 1
                          {kAnchorFoldFeedId, 0.0},           // Anchor Fold Feed 0
                          {kDynamicsTransientAttackId, 0.7}}, // transient attack up
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Bare Floor",
            .nameB = "Tube Floor",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 11: AT-11 PUNCH (plan 7 row 11) ----
        ProfundumPresetDef{
            .name = "Punch Anchor",
            .category = "Plucked",
            .description = "Anchor Lock to Punch: the Anchor in PUNCH, a 9 dB boost over 40 ms, under a 300 ms pluck that "
                           "drops 5 semitones into a Crush-grained body with a Rounded Anchor. Morph to B turns the pluck "
                           "into a longer, biting note.",
            .primary = C::AT11,
            .secondaries = {},
            // Whole-instrument pilot (2026-10-26): A keeps its AT-11 claim (PUNCH, p 0) and uses the rest: more body
            // and edge with a little excitation and fold toward BITE, Crush at Amount 0.3, a Rounded Sine Anchor and
            // a 5 st, 80 ms pitch drop. The boost rises 6 -> 9 dB: the AT-11 margin read 0.17 dB at 6 dB.
            .endpointA = {{kCoreDepthId, 0.55},            // depth 0.55
                          {kCoreBodyId, 0.4},              // body 0.4
                          {kCoreEdgeId, 0.35},             // edge 0.35
                          {kCoreExcitationMixId, 0.2},     // excitation mix 0.2
                          {kFoldAmountId, 0.2},            // Harmonic Fold 20
                          {kFoldShapeId, 0.7},             // SHAPE 0.7
                          {kCharacterId, 0.8},             // Crush
                          {kCharacterAmountId, 0.3},       // Amount 0.3
                          {kCharacterToneId, 0.6},         // Tone 0.6
                          {kAmpAttackId, 0.2},             // 1 ms
                          {kAmpDecayId, 0.695424},         // 300 ms
                          {kAmpSustainId, 0.5},            // sustain 0.5
                          {kAmpReleaseId, 0.635218},       // 150 ms
                          {kFieldImageModeId, 0.0},        // SOLID
                          {kFieldSpreadId, 0.0},           // spread 0
                          {kMassId, 0.5},                  // Mass 50
                          {kAnchorWaveformId, 0.5},        // Rounded Sine
                          {kAnchorModeId, 2.0 / 3.0},      // PUNCH
                          {kAnchorLevelId, 1.0},           // level 1
                          {kAnchorFoldFeedId, 0.0},        // Anchor Fold Feed 0
                          {kAnchorPunchBoostId, 0.75},     // punch boost 9 dB
                          {kAnchorPunchTimeId, 0.531551},  // punch time 40 ms
                          {kDropAmountId, 0.104167},       // drop 5 st
                          {kDropTimeId, 0.528334}},        // drop time 80 ms
            // Morph pilot (2026-10-26): B is a longer, biting note: Bite at Amount 0.6, Fold SHAPE at BITE with a light
            // fold, more edge and body, a 600 ms decay to sustain 0.6, a 9 dB punch and the SPREAD image.
            .endpointB = {{kCoreDepthId, 0.5},             // depth 0.5
                          {kCoreBodyId, 0.5},              // body 0.5
                          {kCoreEdgeId, 0.6},              // edge 0.6
                          {kCoreExcitationMixId, 0.3},     // excitation mix 0.3
                          {kFoldAmountId, 0.3},            // Harmonic Fold 30
                          {kFoldShapeId, 1.0},             // SHAPE 1 (BITE)
                          {kCharacterId, 0.6},             // Bite
                          {kCharacterAmountId, 0.6},       // Amount 0.6
                          {kCharacterToneId, 0.7},         // Tone 0.7
                          {kAmpAttackId, 0.2},             // 1 ms
                          {kAmpDecayId, 0.755630},         // 600 ms
                          {kAmpSustainId, 0.6},            // sustain 0.6
                          {kAmpReleaseId, 0.635218},       // 150 ms
                          {kFieldImageModeId, 0.5},        // SPREAD
                          {kFieldSpreadId, 0.4},           // spread 0.4
                          {kMassId, 0.6},                  // Mass 60
                          {kAnchorWaveformId, 0.0},        // Sine
                          {kAnchorModeId, 2.0 / 3.0},      // PUNCH
                          {kAnchorLevelId, 1.0},           // level 1
                          {kAnchorFoldFeedId, 0.0},        // Anchor Fold Feed 0
                          {kAnchorPunchBoostId, 0.75},     // punch boost 9 dB
                          {kAnchorPunchTimeId, 0.531551}}, // punch time 40 ms
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Punched",
            .nameB = "Long Bite",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 12: AT-12 GHOST (plan 7 row 12) ----
        // T027 FR-061 level: sustain 0.2 -> 0.7 (-21.72 -> -10.84 dBFS).
        ProfundumPresetDef{
            .name = "Ghost Kick",
            .category = "Plucked",
            .description = "Anchor Lock to Ghost: the Anchor in GHOST for 100 ms under a short decay of a Bite-edged, "
                           "folded body, a kick-like thump. Morph to B crushes it into a wide tap.",
            .primary = C::AT12,
            .secondaries = {},
            // Designed-library pass (2026-10-26): AT-12 reads the sub onset (margin 0.83 dB), so depth stays; A adds
            // excitation, a BITE-leaning fold and Bite 0.4 above the sub, a short 3 st drop and a spread top.
            .endpointA = {{kCoreDepthId, 0.5},          // depth 0.5
                          {kCoreBodyId, 0.2},           // body 0.2
                          {kCoreEdgeId, 0.4},           // edge 0.4
                          {kCoreExcitationMixId, 0.3},  // excitation mix 0.3
                          {kFoldAmountId, 0.25},        // Harmonic Fold 25
                          {kFoldShapeId, 0.6},          // SHAPE 0.6
                          {kCharacterId, 0.6},          // Bite
                          {kCharacterAmountId, 0.4},    // Amount 0.4
                          {kCharacterToneId, 0.6},      // Tone 0.6
                          {kAmpAttackId, 0.2},          // 1 ms
                          {kAmpDecayId, 0.651055},      // 180 ms
                          {kAmpSustainId, 0.7},         // sustain 0.7
                          {kAmpReleaseId, 0.615836},    // 120 ms
                          {kFieldImageModeId, 0.5},     // SPREAD
                          {kFieldSpreadId, 0.3},        // spread 0.3
                          {kMassId, 0.6},               // Mass 60
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 1.0},         // GHOST
                          {kAnchorLevelId, 1.0},        // level 1
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kCoreShiftId, 0.6},          // shift 0.2
                          {kAnchorGhostTimeId, 1.0},    // ghost time 100 ms
                          {kDropAmountId, 0.0625},      // drop 3 st
                          {kDropTimeId, 0.388095}},     // drop time 25 ms
            // Designed-library pass (2026-10-26): B crushes the tap: more edge and body, a BITE fold, Crush 0.45, wide.
            .endpointB = {{kCoreDepthId, 0.35},             // depth 0.35
                          {kCoreBodyId, 0.5},               // body 0.5
                          {kCoreEdgeId, 0.6},               // edge 0.6
                          {kCoreExcitationMixId, 0.5},      // excitation mix 0.5
                          {kFoldAmountId, 0.5},             // Harmonic Fold 50
                          {kFoldShapeId, 0.9},              // SHAPE 0.9
                          {kCharacterId, 0.8},              // Crush
                          {kCharacterAmountId, 0.45},       // Amount 0.45
                          {kCharacterToneId, 0.6},          // Tone 0.6
                          {kAmpAttackId, 0.2},              // 1 ms
                          {kAmpDecayId, 0.695424},          // 300 ms
                          {kAmpSustainId, 0.85},            // sustain 0.85
                          {kAmpReleaseId, 0.615836},        // 120 ms
                          {kFieldImageModeId, 1.0},         // WIDE
                          {kFieldSpreadId, 0.6},            // spread 0.6
                          {kMassId, 0.7},                   // Mass 70
                          {kAnchorWaveformId, 0.0},         // Sine
                          {kAnchorModeId, 1.0},             // GHOST
                          {kAnchorLevelId, 1.0},            // level 1
                          {kAnchorFoldFeedId, 0.0},         // Anchor Fold Feed 0
                          {kCoreShiftId, 0.6},              // shift 0.2
                          {kAnchorGhostTimeId, 0.889076}},  // ghost time 60 ms
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Ghost Thump",
            .nameB = "Crushed Tap",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 13: AT-13 FOLLOW (plan 7 row 13; secondary AT-06; the Rectify variant (P-2)) ----
        ProfundumPresetDef{
            .name = "Rectified Follow",
            .category = "Driven",
            .description = "Anchor Lock to Follow: Rectify at Amount 1 doubles the core and the Anchor in FOLLOW at "
                           "level 1 goes with it, wide. Morph to B grinds it into a mono Crush body over a locked "
                           "Triangle Anchor.",
            .primary = C::AT13,
            .secondaries = {C::AT06},
            // Designed-library pass (2026-10-26): AT-13 reads the Anchor pitch, so the core, the Sine Anchor and the
            // pitch stay; A gains Harmonic Fold 20 (AT-06 had 31 of margin), a WIDE image, a longer release and a
            // harder transient.
            .endpointA = {{kCoreDepthId, 1.0},                   // depth 1
                          {kCoreBodyId, 0.0},                    // body 0
                          {kCoreEdgeId, 0.0},                    // edge 0
                          {kCoreExcitationMixId, 0.0},           // excitation mix 0
                          {kFoldAmountId, 0.2},                  // Harmonic Fold 20
                          {kFoldShapeId, 0.0},                   // SHAPE 0
                          {kCharacterId, 0.4},                   // Rectify
                          {kCharacterAmountId, 1.0},             // Amount 1
                          {kCharacterToneId, 0.5},               // Tone 0.5
                          {kAmpAttackId, 0.260206},              // 2 ms
                          {kAmpDecayId, 0.695424},               // 300 ms
                          {kAmpSustainId, 0.9},                  // sustain 0.9
                          {kAmpReleaseId, 0.720412},             // 400 ms
                          {kFieldImageModeId, 1.0},              // WIDE
                          {kFieldSpreadId, 0.6},                 // spread 0.6
                          {kMassId, 0.5},                        // Mass 50
                          {kAnchorWaveformId, 0.0},              // Sine
                          {kAnchorModeId, 1.0 / 3.0},            // FOLLOW
                          {kAnchorLevelId, 1.0},                 // level 1
                          {kAnchorFoldFeedId, 0.0},              // Anchor Fold Feed 0
                          {kDynamicsTransientAttackId, 0.625}},  // transient attack +3 dB
            // Designed-library pass (2026-10-26): B is a mono, Crush-grained body over a Triangle Anchor in LOCK.
            .endpointB = {{kCoreDepthId, 0.5},          // depth 0.5
                          {kCoreBodyId, 0.5},           // body 0.5
                          {kCoreEdgeId, 0.5},           // edge 0.5
                          {kCoreExcitationMixId, 0.3},  // excitation mix 0.3
                          {kFoldAmountId, 0.4},         // Harmonic Fold 40
                          {kFoldShapeId, 0.5},          // SHAPE 0.5
                          {kCharacterId, 0.8},          // Crush
                          {kCharacterAmountId, 0.5},    // Amount 0.5
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.260206},     // 2 ms
                          {kAmpDecayId, 0.695424},      // 300 ms
                          {kAmpSustainId, 0.9},         // sustain 0.9
                          {kAmpReleaseId, 0.720412},    // 400 ms
                          {kFieldImageModeId, 0.0},     // SOLID
                          {kFieldSpreadId, 0.0},        // spread 0
                          {kMassId, 0.65},              // Mass 65
                          {kAnchorWaveformId, 0.25},    // Triangle
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 1.0},        // level 1
                          {kAnchorFoldFeedId, 0.0}},    // Anchor Fold Feed 0
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Following",
            .nameB = "Crushed Follow",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 14: AT-14 Triangle (plan 7 row 14) ----
        // T027 FR-061 level: sustain 0.9 -> 0.5 (-6.77 -> -11.88 dBFS).
        // T027: attack 20 ms parts it from Locked Floor (d 0.165 -> 2.96 at 4 ms vs 20 ms); AT-14 unchanged.
        ProfundumPresetDef{
            .name = "Triangle Floor",
            .category = "Sub",
            .description = "Anchor Sine to Triangle: a Triangle Anchor at level 1 under a sine core with a short 3 "
                           "semitone thump and a spread top, a hollow-edged floor. Morph to B crushes it: a body, fold "
                           "and Crush over a wide image.",
            .primary = C::AT14,
            .secondaries = {},
            // Designed-library pass (2026-10-26): AT-14 reads D_cx at the Sum tap against a sine core (margin 0.84 dB),
            // so the core stays a sine; A gains a spread top, a 3 st thump and a harder transient, all outside that
            // path.
            .endpointA = {{kCoreDepthId, 1.0},                     // depth 1
                          {kCoreBodyId, 0.0},                      // body 0
                          {kCoreEdgeId, 0.0},                      // edge 0
                          {kCoreExcitationMixId, 0.0},             // excitation mix 0
                          {kFoldAmountId, 0.0},                    // Harmonic Fold 0
                          {kFoldShapeId, 0.0},                     // SHAPE 0
                          {kCharacterId, 0.0},                     // Tube
                          {kCharacterAmountId, 0.0},               // Amount 0
                          {kCharacterToneId, 0.5},                 // Tone 0.5
                          {kAmpAttackId, 0.460206},                // 20 ms
                          {kAmpDecayId, 0.695424},                 // 300 ms
                          {kAmpSustainId, 0.5},                    // sustain 0.5
                          {kAmpReleaseId, 0.75563},                // 600 ms
                          {kFieldImageModeId, 0.5},                // SPREAD
                          {kFieldSpreadId, 0.35},                  // spread 0.35
                          {kMassId, 0.6},                          // Mass 60
                          {kAnchorWaveformId, 0.25},               // Triangle
                          {kAnchorModeId, 0.0},                    // LOCK
                          {kAnchorLevelId, 1.0},                   // level 1
                          {kAnchorFoldFeedId, 0.0},                // Anchor Fold Feed 0
                          {kDynamicsTransientAttackId, 0.708333},  // transient attack +5 dB
                          {kDropAmountId, 0.0625},                 // drop 3 st
                          {kDropTimeId, 0.428663}},                // drop time 35 ms
            // Designed-library pass (2026-10-26): B crushes the floor: a body, a fold toward BITE, Crush 0.35 and a
            // WIDE image (Triangle on both: AT-14).
            .endpointB = {{kCoreDepthId, 0.6},                   // depth 0.6
                          {kCoreBodyId, 0.5},                    // body 0.5
                          {kCoreEdgeId, 0.4},                    // edge 0.4
                          {kCoreExcitationMixId, 0.3},           // excitation mix 0.3
                          {kFoldAmountId, 0.35},                 // Harmonic Fold 35
                          {kFoldShapeId, 0.6},                   // SHAPE 0.6
                          {kCharacterId, 0.8},                   // Crush
                          {kCharacterAmountId, 0.35},            // Amount 0.35
                          {kCharacterToneId, 0.5},               // Tone 0.5
                          {kAmpAttackId, 0.460206},              // 20 ms
                          {kAmpDecayId, 0.695424},               // 300 ms
                          {kAmpSustainId, 0.7},                  // sustain 0.7
                          {kAmpReleaseId, 0.75563},              // 600 ms
                          {kFieldImageModeId, 1.0},              // WIDE
                          {kFieldSpreadId, 0.7},                 // spread 0.7
                          {kMassId, 0.75},                       // Mass 75
                          {kAnchorWaveformId, 0.25},             // Triangle
                          {kAnchorModeId, 0.0},                  // LOCK
                          {kAnchorLevelId, 0.85},                // level 0.85
                          {kAnchorFoldFeedId, 0.0},              // Anchor Fold Feed 0
                          {kDynamicsTransientAttackId, 0.625}},  // transient attack +3 dB
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Triangle Under",
            .nameB = "Crushed Floor",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 15: AT-15 Rounded Sine (plan 7 row 15) ----
        // T027 FR-061 level: sustain 0.85 -> 0.55 (-6.53 -> -10.31 dBFS).
        ProfundumPresetDef{
            .name = "Rounded Weight",
            .category = "Sub",
            .description = "Anchor Sine to Rounded Sine: a Rounded Sine Anchor at level 1 under a sine core warmed by "
                           "a little Tube, with Mass 70, soft but weighty. Morph to B bites: a body, a BITE-shaped "
                           "fold and Bite over a wide image.",
            .primary = C::AT15,
            .secondaries = {},
            // Designed-library pass (2026-10-26): AT-15 had 5.4 dB of D_cx margin; A takes a little body, Tube 0.25 and
            // a 2 st drop.
            .endpointA = {{kCoreDepthId, 1.0},          // depth 1
                          {kCoreBodyId, 0.15},          // body 0.15
                          {kCoreEdgeId, 0.0},           // edge 0
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.25},   // Amount 0.25
                          {kCharacterToneId, 0.35},     // Tone 0.35
                          {kAmpAttackId, 0.35563},      // 6 ms
                          {kAmpDecayId, 0.720412},      // 400 ms
                          {kAmpSustainId, 0.55},        // sustain 0.55
                          {kAmpReleaseId, 0.720412},    // 400 ms
                          {kFieldImageModeId, 0.5},     // SPREAD
                          {kFieldSpreadId, 0.3},        // spread 0.3
                          {kMassId, 0.7},               // Mass 70
                          {kAnchorWaveformId, 0.5},     // RoundedSine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 1.0},        // level 1
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kDropAmountId, 0.041667},    // drop 2 st
                          {kDropTimeId, 0.471666}},     // drop time 50 ms
            // Designed-library pass (2026-10-26): B bites: a body, a BITE-shaped fold, Bite 0.5 and a WIDE image
            // (Rounded Sine on both: AT-15).
            .endpointB = {{kCoreDepthId, 0.55},          // depth 0.55
                          {kCoreBodyId, 0.5},            // body 0.5
                          {kCoreEdgeId, 0.45},           // edge 0.45
                          {kCoreExcitationMixId, 0.25},  // excitation mix 0.25
                          {kFoldAmountId, 0.4},          // Harmonic Fold 40
                          {kFoldShapeId, 0.85},          // SHAPE 0.85
                          {kCharacterId, 0.6},           // Bite
                          {kCharacterAmountId, 0.5},     // Amount 0.5
                          {kCharacterToneId, 0.6},       // Tone 0.6
                          {kAmpAttackId, 0.35563},       // 6 ms
                          {kAmpDecayId, 0.720412},       // 400 ms
                          {kAmpSustainId, 0.68},         // sustain 0.68
                          {kAmpReleaseId, 0.720412},     // 400 ms
                          {kFieldImageModeId, 1.0},      // WIDE
                          {kFieldSpreadId, 0.6},         // spread 0.6
                          {kMassId, 0.85},               // Mass 85
                          {kAnchorWaveformId, 0.5},      // RoundedSine
                          {kAnchorModeId, 0.0},          // LOCK
                          {kAnchorLevelId, 1.0},         // level 1
                          {kAnchorFoldFeedId, 0.0}},     // Anchor Fold Feed 0
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Rounded",
            .nameB = "Rounded Bite",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 16: AT-16 Asymmetric (plan 7 row 16) ----
        ProfundumPresetDef{
            .name = "Lopsided Sub",
            .category = "Sub",
            .description = "Anchor Sine to Asymmetric Sine: an Asymmetric Sine Anchor gives the sub a lopsided, "
                           "even-harmonic push, echoed by a lopsided drive, a light fold and a touch of Fracture. "
                           "Morph to B crushes it wide.",
            .primary = C::AT16,
            .secondaries = {},
            // Designed-library pass (2026-10-26): AT-16 had 11.2 dB of D_cx margin; A echoes the lopsided Anchor with a
            // lopsided drive, a light fold and Fracture 0.2.
            .endpointA = {{kCoreDepthId, 1.0},                     // depth 1
                          {kCoreBodyId, 0.25},                     // body 0.25
                          {kCoreEdgeId, 0.15},                     // edge 0.15
                          {kCoreExcitationMixId, 0.25},            // excitation mix 0.25
                          {kFoldAmountId, 0.2},                    // Harmonic Fold 20
                          {kFoldShapeId, 0.3},                     // SHAPE 0.3
                          {kCharacterId, 1.0},                     // Fracture
                          {kCharacterAmountId, 0.2},               // Amount 0.2
                          {kCharacterToneId, 0.4},                 // Tone 0.4
                          {kAmpAttackId, 0.260206},                // 2 ms
                          {kAmpDecayId, 0.679588},                 // 250 ms
                          {kAmpSustainId, 0.8},                    // sustain 0.8
                          {kAmpReleaseId, 0.695424},               // 300 ms
                          {kFieldImageModeId, 0.5},                // SPREAD
                          {kFieldSpreadId, 0.3},                   // spread 0.3
                          {kMassId, 0.45},                         // Mass 45
                          {kAnchorWaveformId, 0.75},               // AsymmetricSine
                          {kAnchorModeId, 0.0},                    // LOCK
                          {kAnchorLevelId, 1.0},                   // level 1
                          {kAnchorFoldFeedId, 0.0},                // Anchor Fold Feed 0
                          {kDynamicsTransientAttackId, 0.833333},  // transient attack +8 dB
                          {kCoreExcitationDriveId, 0.6},           // drive 0.6
                          {kCoreExcitationAsymmetryId, 0.75}},     // asymmetry 0.75
            // Designed-library pass (2026-10-26): B crushes it wide: a lopsided drive, a fold and Crush 0.5 with Shift
            // up (Asymmetric Sine on both: AT-16).
            .endpointB = {{kCoreDepthId, 0.7},                     // depth 0.7
                          {kCoreBodyId, 0.45},                     // body 0.45
                          {kCoreEdgeId, 0.5},                      // edge 0.5
                          {kCoreExcitationMixId, 0.5},             // excitation mix 0.5
                          {kFoldAmountId, 0.35},                   // Harmonic Fold 35
                          {kFoldShapeId, 0.45},                    // SHAPE 0.45
                          {kCharacterId, 0.8},                     // Crush
                          {kCharacterAmountId, 0.5},               // Amount 0.5
                          {kCharacterToneId, 0.6},                 // Tone 0.6
                          {kAmpAttackId, 0.260206},                // 2 ms
                          {kAmpDecayId, 0.679588},                 // 250 ms
                          {kAmpSustainId, 0.9},                    // sustain 0.9
                          {kAmpReleaseId, 0.739794},               // 500 ms
                          {kFieldImageModeId, 1.0},                // WIDE
                          {kFieldSpreadId, 0.8},                   // spread 0.8
                          {kMassId, 0.45},                         // Mass 45
                          {kAnchorWaveformId, 0.75},               // AsymmetricSine
                          {kAnchorModeId, 0.0},                    // LOCK
                          {kAnchorLevelId, 1.0},                   // level 1
                          {kAnchorFoldFeedId, 0.0},                // Anchor Fold Feed 0
                          {kDynamicsTransientAttackId, 0.833333},  // transient attack +8 dB
                          {kCoreExcitationDriveId, 0.8},           // drive 0.8
                          {kCoreExcitationAsymmetryId, 0.9},       // asymmetry 0.9
                          {kCoreShiftId, 0.75}},                   // shift 0.5
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Lopsided",
            .nameB = "Lopsided Crush",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 17: AT-17 Octave Down (plan 7 row 17) ----
        ProfundumPresetDef{
            .name = "Under Octave",
            .category = "Sub",
            .description = "Anchor Sine to Octave Down: the Octave Down Anchor puts a sub an octave under a "
                           "Crush-grained core with a light fold. Morph to B bites: more body, a BITE-shaped fold, "
                           "Bite and an Asymmetric Anchor, wide.",
            .primary = C::AT17,
            .secondaries = {},
            // Designed-library pass (2026-10-26): AT-17 (P_half) had 50 dB of margin; A grains the core with Crush 0.3
            // and a light fold.
            .endpointA = {{kCoreDepthId, 0.8},           // depth 0.8
                          {kCoreBodyId, 0.35},           // body 0.35
                          {kCoreEdgeId, 0.25},           // edge 0.25
                          {kCoreExcitationMixId, 0.25},  // excitation mix 0.25
                          {kFoldAmountId, 0.25},         // Harmonic Fold 25
                          {kFoldShapeId, 0.3},           // SHAPE 0.3
                          {kCharacterId, 0.8},           // Crush
                          {kCharacterAmountId, 0.3},     // Amount 0.3
                          {kCharacterToneId, 0.4},       // Tone 0.4
                          {kAmpAttackId, 0.339794},      // 5 ms
                          {kAmpDecayId, 0.739794},       // 500 ms
                          {kAmpSustainId, 0.9},          // sustain 0.9
                          {kAmpReleaseId, 0.730643},     // 450 ms
                          {kFieldImageModeId, 0.5},      // SPREAD
                          {kFieldSpreadId, 0.4},         // spread 0.4
                          {kMassId, 0.6},                // Mass 60
                          {kAnchorWaveformId, 1.0},      // OctaveDownSine
                          {kAnchorModeId, 0.0},          // LOCK
                          {kAnchorLevelId, 0.9},         // level 0.9
                          {kAnchorFoldFeedId, 0.0},      // Anchor Fold Feed 0
                          {kCoreShiftId, 0.4}},          // shift -0.2
            // Designed-library pass (2026-10-26): B bites: more body, a BITE-shaped fold, Bite 0.45 and an Asymmetric
            // Sine Anchor, wide.
            .endpointB = {{kCoreDepthId, 0.5},          // depth 0.5
                          {kCoreBodyId, 0.6},           // body 0.6
                          {kCoreEdgeId, 0.5},           // edge 0.5
                          {kCoreExcitationMixId, 0.4},  // excitation mix 0.4
                          {kFoldAmountId, 0.6},         // Harmonic Fold 60
                          {kFoldShapeId, 0.75},         // SHAPE 0.75
                          {kCharacterId, 0.6},          // Bite
                          {kCharacterAmountId, 0.45},   // Amount 0.45
                          {kCharacterToneId, 0.6},      // Tone 0.6
                          {kAmpAttackId, 0.339794},     // 5 ms
                          {kAmpDecayId, 0.739794},      // 500 ms
                          {kAmpSustainId, 0.9},         // sustain 0.9
                          {kAmpReleaseId, 0.730643},    // 450 ms
                          {kFieldImageModeId, 1.0},     // WIDE
                          {kFieldSpreadId, 0.7},        // spread 0.7
                          {kMassId, 0.8},               // Mass 80
                          {kAnchorWaveformId, 0.75},    // AsymmetricSine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.8},        // level 0.8
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kCoreShiftId, 0.4}},         // shift -0.2
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Octave Under",
            .nameB = "Bitten Body",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 18: AT-18 Fold Feed (plan 7 row 18) ----
        // Tube at Amount 0 (C-9: never Rectify + Tone 1 + Fold 100 + WIDE); the probe base A_s + Fold 100 +
        // Anchor level 1, with a SPREAD image.
        ProfundumPresetDef{
            .name = "Fed Fold",
            .category = "Driven",
            .description = "Anchor Fold Feed: Harmonic Fold 100 with the Anchor Fold Feed at 1, the Anchor driven into "
                           "the folder, snapping wide. Morph to B bites: a body, a BITE fold and Bite with less feed, "
                           "mono.",
            .primary = C::AT18,
            .secondaries = {},
            // Designed-library pass (2026-10-26): AT-18 reads the fed Anchor through the folder (margin 2.49), so the
            // Anchor stays; A gains a WIDE image, a 3 st snap and a harder transient.
            .endpointA = {{kCoreDepthId, 1.0},                   // depth 1
                          {kCoreBodyId, 0.0},                    // body 0
                          {kCoreEdgeId, 0.0},                    // edge 0
                          {kCoreExcitationMixId, 0.0},           // excitation mix 0
                          {kFoldAmountId, 1.0},                  // Harmonic Fold 100
                          {kFoldShapeId, 0.0},                   // SHAPE 0
                          {kCharacterId, 0.0},                   // Tube
                          {kCharacterAmountId, 0.0},             // Amount 0
                          {kCharacterToneId, 0.5},               // Tone 0.5
                          {kAmpAttackId, 0.260206},              // 2 ms
                          {kAmpDecayId, 0.708814},               // 350 ms
                          {kAmpSustainId, 0.8},                  // sustain 0.8
                          {kAmpReleaseId, 0.695424},             // 300 ms
                          {kFieldImageModeId, 1.0},              // WIDE
                          {kFieldSpreadId, 0.5},                 // spread 0.5
                          {kMassId, 0.5},                        // Mass 50
                          {kAnchorWaveformId, 0.0},              // Sine
                          {kAnchorModeId, 0.0},                  // LOCK
                          {kAnchorLevelId, 1.0},                 // level 1
                          {kAnchorFoldFeedId, 1.0},              // Anchor Fold Feed 1
                          {kDropAmountId, 0.0625},               // drop 3 st
                          {kDropTimeId, 0.410077},               // drop time 30 ms
                          {kDynamicsTransientAttackId, 0.625}},  // transient attack +3 dB
            // Designed-library pass (2026-10-26): B bites: a body, a BITE fold, Bite 0.5, less feed, over a mono
            // Triangle Anchor.
            .endpointB = {{kCoreDepthId, 0.6},          // depth 0.6
                          {kCoreBodyId, 0.4},           // body 0.4
                          {kCoreEdgeId, 0.4},           // edge 0.4
                          {kCoreExcitationMixId, 0.3},  // excitation mix 0.3
                          {kFoldAmountId, 0.7},         // Harmonic Fold 70
                          {kFoldShapeId, 0.7},          // SHAPE 0.7
                          {kCharacterId, 0.6},          // Bite
                          {kCharacterAmountId, 0.5},    // Amount 0.5
                          {kCharacterToneId, 0.6},      // Tone 0.6
                          {kAmpAttackId, 0.260206},     // 2 ms
                          {kAmpDecayId, 0.708814},      // 350 ms
                          {kAmpSustainId, 0.8},         // sustain 0.8
                          {kAmpReleaseId, 0.695424},    // 300 ms
                          {kFieldImageModeId, 0.0},     // SOLID
                          {kFieldSpreadId, 0.0},        // spread 0
                          {kMassId, 0.7},               // Mass 70
                          {kAnchorWaveformId, 0.25},    // Triangle
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 1.0},        // level 1
                          {kAnchorFoldFeedId, 0.3}},    // Anchor Fold Feed 0.3
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Fed Anchor",
            .nameB = "Bitten Fold",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 19: AT-19 Drop amount (plan 7 row 19) ----
        ProfundumPresetDef{
            .name = "Dropping Stone",
            .category = "Plucked",
            .description = "Pitch Drop Amount: a 24 st drop over 200 ms at every note, a Tube-warmed, folded stone "
                           "falling into the floor. Morph to B halves the drop and fractures it wide.",
            .primary = C::AT19,
            .secondaries = {},
            // Designed-library pass (2026-10-26): AT-19 had 18 of margin; A drops a Tube-warmed, folded body over a
            // Rounded Anchor.
            .endpointA = {{kCoreDepthId, 0.9},           // depth 0.9
                          {kCoreBodyId, 0.3},            // body 0.3
                          {kCoreEdgeId, 0.3},            // edge 0.3
                          {kCoreExcitationMixId, 0.25},  // excitation mix 0.25
                          {kFoldAmountId, 0.3},          // Harmonic Fold 30
                          {kFoldShapeId, 0.4},           // SHAPE 0.4
                          {kCharacterId, 0.0},           // Tube
                          {kCharacterAmountId, 0.35},    // Amount 0.35
                          {kCharacterToneId, 0.5},       // Tone 0.5
                          {kAmpAttackId, 0.260206},      // 2 ms
                          {kAmpDecayId, 0.720412},       // 400 ms
                          {kAmpSustainId, 0.5},          // sustain 0.5
                          {kAmpReleaseId, 0.660206},     // 200 ms
                          {kFieldImageModeId, 0.5},      // SPREAD
                          {kFieldSpreadId, 0.3},         // spread 0.3
                          {kMassId, 0.55},               // Mass 55
                          {kAnchorWaveformId, 0.5},      // RoundedSine
                          {kAnchorModeId, 0.0},          // LOCK
                          {kAnchorLevelId, 1.0},         // level 1
                          {kAnchorFoldFeedId, 0.0},      // Anchor Fold Feed 0
                          {kCoreShiftId, 0.4},           // shift -0.2
                          {kDropAmountId, 0.5},          // drop 24 st
                          {kDropTimeId, 0.638809},       // drop time 200 ms
                          {kDropShapeId, 0.0},           // drop shape 0
                          {kDropReboundId, 0.0}},        // rebound 0
            // Designed-library pass (2026-10-26): B halves the drop and fractures the stone, wide, over a Triangle
            // Anchor.
            .endpointB = {{kCoreDepthId, 0.5},           // depth 0.5
                          {kCoreBodyId, 0.5},            // body 0.5
                          {kCoreEdgeId, 0.6},            // edge 0.6
                          {kCoreExcitationMixId, 0.25},  // excitation mix 0.25
                          {kFoldAmountId, 0.55},         // Harmonic Fold 55
                          {kFoldShapeId, 0.7},           // SHAPE 0.7
                          {kCharacterId, 1.0},           // Fracture
                          {kCharacterAmountId, 0.45},    // Amount 0.45
                          {kCharacterToneId, 0.6},       // Tone 0.6
                          {kAmpAttackId, 0.260206},      // 2 ms
                          {kAmpDecayId, 0.720412},       // 400 ms
                          {kAmpSustainId, 0.85},         // sustain 0.85
                          {kAmpReleaseId, 0.660206},     // 200 ms
                          {kFieldImageModeId, 1.0},      // WIDE
                          {kFieldSpreadId, 0.7},         // spread 0.7
                          {kMassId, 0.7},                // Mass 70
                          {kAnchorWaveformId, 0.25},     // Triangle
                          {kAnchorModeId, 0.0},          // LOCK
                          {kAnchorLevelId, 1.0},         // level 1
                          {kAnchorFoldFeedId, 0.0},      // Anchor Fold Feed 0
                          {kCoreShiftId, 0.4},           // shift -0.2
                          {kDropAmountId, 0.25},         // drop 12 st
                          {kDropTimeId, 0.670442},       // drop time 260 ms
                          {kDropShapeId, 0.0},           // drop shape 0
                          {kDropReboundId, 0.0}},        // rebound 0
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Stone Drop",
            .nameB = "Fractured Stone",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 20: AT-20 Drop time (plan 7 row 20) ----
        ProfundumPresetDef{
            .name = "Slow Fall",
            .category = "Plucked",
            .description = "Pitch Drop Time: a 24 st drop stretched to 500 ms, a slow fall onto the note through a "
                           "hollow fold over an Asymmetric Anchor, wide. Morph to B turns it into a mono Rectify sub "
                           "with half the drop.",
            .primary = C::AT20,
            .secondaries = {},
            // Designed-library pass (2026-10-26): AT-20 had 5.95 of margin; A falls through a HOLLOW-leaning fold with
            // the Fold Character over an Asymmetric Anchor, wide.
            .endpointA = {{kCoreDepthId, 0.7},          // depth 0.7
                          {kCoreBodyId, 0.5},           // body 0.5
                          {kCoreEdgeId, 0.3},           // edge 0.3
                          {kCoreExcitationMixId, 0.2},  // excitation mix 0.2
                          {kFoldAmountId, 0.2},         // Harmonic Fold 20
                          {kFoldShapeId, 0.5},          // SHAPE 0.5
                          {kCharacterId, 0.2},          // Fold
                          {kCharacterAmountId, 0.3},    // Amount 0.3
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.2},          // 1 ms
                          {kAmpDecayId, 0.76902},       // 700 ms
                          {kAmpSustainId, 0.6},         // sustain 0.6
                          {kAmpReleaseId, 0.695424},    // 300 ms
                          {kFieldImageModeId, 1.0},     // WIDE
                          {kFieldSpreadId, 0.5},        // spread 0.5
                          {kMassId, 0.5},               // Mass 50
                          {kAnchorWaveformId, 0.75},    // AsymmetricSine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 1.0},        // level 1
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.7},  // body curvature 0.7
                          {kDropAmountId, 0.5},         // drop 24 st
                          {kDropTimeId, 0.749285},      // drop time 500 ms
                          {kDropShapeId, 0.0},          // drop shape 0
                          {kDropReboundId, 0.0}},       // rebound 0
            // Designed-library pass (2026-10-26): B is a mono, folded Rectify sub (Amount 0.7) over an Octave Down
            // Anchor with half the drop, a longer decay and Shift down.
            .endpointB = {{kCoreDepthId, 0.9},          // depth 0.9
                          {kCoreBodyId, 0.6},           // body 0.6
                          {kCoreEdgeId, 0.2},           // edge 0.2
                          {kCoreExcitationMixId, 0.5},  // excitation mix 0.5
                          {kFoldAmountId, 0.3},         // Harmonic Fold 30
                          {kFoldShapeId, 0.6},          // SHAPE 0.6
                          {kCharacterId, 0.4},          // Rectify
                          {kCharacterAmountId, 0.7},    // Amount 0.7
                          {kCharacterToneId, 0.8},      // Tone 0.8
                          {kAmpAttackId, 0.2},          // 1 ms
                          {kAmpDecayId, 0.835218},      // 1.5 s
                          {kAmpSustainId, 0.75},        // sustain 0.75
                          {kAmpReleaseId, 0.75563},     // 600 ms
                          {kFieldImageModeId, 0.0},     // SOLID
                          {kFieldSpreadId, 0.0},        // spread 0
                          {kMassId, 0.6},               // Mass 60
                          {kAnchorWaveformId, 1.0},     // OctaveDownSine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 1.0},        // level 1
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.7},  // body curvature 0.7
                          {kDropAmountId, 0.25},        // drop 12 st
                          {kDropTimeId, 0.687696},      // drop time 300 ms
                          {kDropShapeId, 0.0},          // drop shape 0
                          {kDropReboundId, 0.0},        // rebound 0
                          {kCoreShiftId, 0.25}},        // shift -0.5
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Long Fall",
            .nameB = "Rectified Fall",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 21: AT-21 Drop shape (plan 7 row 21) ----
        ProfundumPresetDef{
            .name = "Curved Drop",
            .category = "Plucked",
            .description = "Pitch Drop Shape: a 24 st drop with shape 1, the curve bending late into the pitch over a "
                           "Triangle Anchor with a spread top. Morph to B bites it, wide, with a gentler curve.",
            .primary = C::AT21,
            .secondaries = {},
            // Designed-library pass (2026-10-26): AT-21 reads the pitch curve (margin 0.60), so the core stays; A gains
            // a spread top, a Triangle Anchor and a longer release.
            .endpointA = {{kCoreDepthId, 0.5},                   // depth 0.5
                          {kCoreBodyId, 0.2},                    // body 0.2
                          {kCoreEdgeId, 0.6},                    // edge 0.6
                          {kCoreExcitationMixId, 0.0},           // excitation mix 0
                          {kFoldAmountId, 0.0},                  // Harmonic Fold 0
                          {kFoldShapeId, 0.0},                   // SHAPE 0
                          {kCharacterId, 0.0},                   // Tube
                          {kCharacterAmountId, 0.0},             // Amount 0
                          {kCharacterToneId, 0.5},               // Tone 0.5
                          {kAmpAttackId, 0.295424},              // 3 ms
                          {kAmpDecayId, 0.730643},               // 450 ms
                          {kAmpSustainId, 0.45},                 // sustain 0.45
                          {kAmpReleaseId, 0.708814},             // 350 ms
                          {kFieldImageModeId, 0.5},              // SPREAD
                          {kFieldSpreadId, 0.4},                 // spread 0.4
                          {kMassId, 0.5},                        // Mass 50
                          {kAnchorWaveformId, 0.25},             // Triangle
                          {kAnchorModeId, 0.0},                  // LOCK
                          {kAnchorLevelId, 1.0},                 // level 1
                          {kAnchorFoldFeedId, 0.0},              // Anchor Fold Feed 0
                          {kCoreShiftId, 0.55},                  // shift 0.1
                          {kDropAmountId, 0.5},                  // drop 24 st
                          {kDropTimeId, 0.604124},               // drop time 150 ms
                          {kDropShapeId, 1.0},                   // drop shape 1
                          {kDropReboundId, 0.0},                 // rebound 0
                          {kDynamicsTransientAttackId, 0.875}},  // transient attack +9 dB
            // Designed-library pass (2026-10-26): B bites: a body, excitation, a BITE fold and Bite 0.5, wide, with a
            // gentler curve.
            .endpointB = {{kCoreDepthId, 0.4},                   // depth 0.4
                          {kCoreBodyId, 0.5},                    // body 0.5
                          {kCoreEdgeId, 0.7},                    // edge 0.7
                          {kCoreExcitationMixId, 0.5},           // excitation mix 0.5
                          {kFoldAmountId, 0.45},                 // Harmonic Fold 45
                          {kFoldShapeId, 0.8},                   // SHAPE 0.8
                          {kCharacterId, 0.6},                   // Bite
                          {kCharacterAmountId, 0.5},             // Amount 0.5
                          {kCharacterToneId, 0.7},               // Tone 0.7
                          {kAmpAttackId, 0.295424},              // 3 ms
                          {kAmpDecayId, 0.730643},               // 450 ms
                          {kAmpSustainId, 0.45},                 // sustain 0.45
                          {kAmpReleaseId, 0.708814},             // 350 ms
                          {kFieldImageModeId, 1.0},              // WIDE
                          {kFieldSpreadId, 0.6},                 // spread 0.6
                          {kMassId, 0.65},                       // Mass 65
                          {kAnchorWaveformId, 0.25},             // Triangle
                          {kAnchorModeId, 0.0},                  // LOCK
                          {kAnchorLevelId, 1.0},                 // level 1
                          {kAnchorFoldFeedId, 0.0},              // Anchor Fold Feed 0
                          {kCoreShiftId, 0.55},                  // shift 0.1
                          {kDropAmountId, 0.5},                  // drop 24 st
                          {kDropTimeId, 0.604124},               // drop time 150 ms
                          {kDropShapeId, 0.3},                   // drop shape 0.3
                          {kDropReboundId, 0.0},                 // rebound 0
                          {kDynamicsTransientAttackId, 0.875}},  // transient attack +9 dB
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Curved",
            .nameB = "Bitten Curve",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 22: AT-22 Drop rebound (plan 7 row 22) ----
        ProfundumPresetDef{
            .name = "Rebound",
            .category = "Plucked",
            .description = "Pitch Drop Rebound: a 24 st drop with rebound 1 that undershoots and bounces back up to "
                           "the note, a Crush-grained, folded body over a Rounded Anchor. Morph to B warms it into a "
                           "wide Tube bounce.",
            .primary = C::AT22,
            .secondaries = {},
            // Designed-library pass (2026-10-26): AT-22 had 5.22 of margin; A bounces a Crush-grained, folded body over
            // a Rounded Anchor.
            .endpointA = {{kCoreDepthId, 0.7},           // depth 0.7
                          {kCoreBodyId, 0.4},            // body 0.4
                          {kCoreEdgeId, 0.3},            // edge 0.3
                          {kCoreExcitationMixId, 0.25},  // excitation mix 0.25
                          {kFoldAmountId, 0.25},         // Harmonic Fold 25
                          {kFoldShapeId, 0.35},          // SHAPE 0.35
                          {kCharacterId, 0.8},           // Crush
                          {kCharacterAmountId, 0.3},     // Amount 0.3
                          {kCharacterToneId, 0.5},       // Tone 0.5
                          {kAmpAttackId, 0.320412},      // 4 ms
                          {kAmpDecayId, 0.739794},       // 500 ms
                          {kAmpSustainId, 0.5},          // sustain 0.5
                          {kAmpReleaseId, 0.679588},     // 250 ms
                          {kFieldImageModeId, 0.5},      // SPREAD
                          {kFieldSpreadId, 0.5},         // spread 0.5
                          {kMassId, 0.6},                // Mass 60
                          {kAnchorWaveformId, 0.5},      // RoundedSine
                          {kAnchorModeId, 0.0},          // LOCK
                          {kAnchorLevelId, 1.0},         // level 1
                          {kAnchorFoldFeedId, 0.0},      // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.3},   // body curvature 0.3
                          {kDropAmountId, 0.5},          // drop 24 st
                          {kDropTimeId, 0.57722},        // drop time 120 ms
                          {kDropShapeId, 0.0},           // drop shape 0
                          {kDropReboundId, 1.0}},        // rebound 1
            // Designed-library pass (2026-10-26): B warms it into a wide Tube bounce with less rebound.
            .endpointB = {{kCoreDepthId, 0.5},           // depth 0.5
                          {kCoreBodyId, 0.6},            // body 0.6
                          {kCoreEdgeId, 0.55},           // edge 0.55
                          {kCoreExcitationMixId, 0.25},  // excitation mix 0.25
                          {kFoldAmountId, 0.5},          // Harmonic Fold 50
                          {kFoldShapeId, 0.7},           // SHAPE 0.7
                          {kCharacterId, 0.0},           // Tube
                          {kCharacterAmountId, 0.55},    // Amount 0.55
                          {kCharacterToneId, 0.4},       // Tone 0.4
                          {kAmpAttackId, 0.320412},      // 4 ms
                          {kAmpDecayId, 0.739794},       // 500 ms
                          {kAmpSustainId, 0.65},         // sustain 0.65
                          {kAmpReleaseId, 0.679588},     // 250 ms
                          {kFieldImageModeId, 1.0},      // WIDE
                          {kFieldSpreadId, 0.7},         // spread 0.7
                          {kMassId, 0.7},                // Mass 70
                          {kAnchorWaveformId, 0.0},      // Sine
                          {kAnchorModeId, 0.0},          // LOCK
                          {kAnchorLevelId, 1.0},         // level 1
                          {kAnchorFoldFeedId, 0.0},      // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.3},   // body curvature 0.3
                          {kDropAmountId, 0.5},          // drop 24 st
                          {kDropTimeId, 0.57722},        // drop time 120 ms
                          {kDropShapeId, 0.0},           // drop shape 0
                          {kDropReboundId, 0.3}},        // rebound 0.3
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Bounce",
            .nameB = "Tube Bounce",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 23: AT-23 Glide (plan 7 row 23; secondary AT-44) ----
        ProfundumPresetDef{
            .name = "Glide Path",
            .category = "Resonant",
            .description = "Pitch Glide: MONO legato with a 200 ms portamento sliding a hollow, Fracture-edged body, "
                           "wide; NoteMemory Legato keeps the phase across each slide. Morph to B drives the slide "
                           "with Tube.",
            .primary = C::AT23,
            .secondaries = {C::AT44},
            // Designed-library pass (2026-10-26): AT-23 had 285 of margin, AT-44 65; the sliding body gains excitation,
            // a HOLLOW fold, Fracture 0.25, an Asymmetric Anchor at level 1 and a WIDE image, at full sustain (Fracture
            // cost level: -16.3 dBFS at Anchor 0.8, sustain 0.9).
            .endpointA = {{kCoreDepthId, 0.6},           // depth 0.6
                          {kCoreBodyId, 0.6},            // body 0.6
                          {kCoreEdgeId, 0.2},            // edge 0.2
                          {kCoreExcitationMixId, 0.25},  // excitation mix 0.25
                          {kFoldAmountId, 0.3},          // Harmonic Fold 30
                          {kFoldShapeId, 0.5},           // SHAPE 0.5
                          {kCharacterId, 1.0},           // Fracture
                          {kCharacterAmountId, 0.25},    // Amount 0.25
                          {kCharacterToneId, 0.45},      // Tone 0.45
                          {kAmpAttackId, 0.339794},      // 5 ms
                          {kAmpDecayId, 0.75563},        // 600 ms
                          {kAmpSustainId, 1.0},          // sustain 1
                          {kAmpReleaseId, 0.720412},     // 400 ms
                          {kFieldImageModeId, 1.0},      // WIDE
                          {kFieldSpreadId, 0.6},         // spread 0.6
                          {kMassId, 0.5},                // Mass 50
                          {kAnchorWaveformId, 0.75},     // AsymmetricSine
                          {kAnchorModeId, 0.0},          // LOCK
                          {kAnchorLevelId, 1.0},         // level 1
                          {kAnchorFoldFeedId, 0.0},      // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.8}},  // body curvature 0.8
            // Designed-library pass (2026-10-26): B drives the slide: a fuller body, a fold toward BITE, Tube 0.55 over
            // a Triangle Anchor.
            .endpointB = {{kCoreDepthId, 0.4},           // depth 0.4
                          {kCoreBodyId, 0.8},            // body 0.8
                          {kCoreEdgeId, 0.5},            // edge 0.5
                          {kCoreExcitationMixId, 0.5},   // excitation mix 0.5
                          {kFoldAmountId, 0.55},         // Harmonic Fold 55
                          {kFoldShapeId, 0.75},          // SHAPE 0.75
                          {kCharacterId, 0.0},           // Tube
                          {kCharacterAmountId, 0.55},    // Amount 0.55
                          {kCharacterToneId, 0.5},       // Tone 0.5
                          {kAmpAttackId, 0.339794},      // 5 ms
                          {kAmpDecayId, 0.75563},        // 600 ms
                          {kAmpSustainId, 0.9},          // sustain 0.9
                          {kAmpReleaseId, 0.720412},     // 400 ms
                          {kFieldImageModeId, 0.5},      // SPREAD
                          {kFieldSpreadId, 0.5},         // spread 0.5
                          {kMassId, 0.65},               // Mass 65
                          {kAnchorWaveformId, 0.25},     // Triangle
                          {kAnchorModeId, 0.0},          // LOCK
                          {kAnchorLevelId, 0.8},         // level 0.8
                          {kAnchorFoldFeedId, 0.0},      // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.8}},  // body curvature 0.8
            .controls = {{kNoteLegatoId, 1.0},           // legato On
                         {kNotePortamentoId, 0.575793},  // portamento 200 ms
                         {kNoteMemoryId, 1.0 / 3.0},     // Note Memory Legato
                         {kNoteRetriggerId, 0.0},        // Retrigger Reset
                         {kMorphPositionId, 0.0}},       // p = 0
            .nameA = "Gliding",
            .nameB = "Driven Glide",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 24: AT-24 attack (plan 7 row 24) ----
        ProfundumPresetDef{
            .name = "Slow Swell",
            .category = "Resonant",
            .description = "Envelope Amp Attack: a 500 ms attack swells a resonant body up to full sustain, a light Rectify "
                           "adding a glassy octave over an Asymmetric Anchor. Morph to B opens it into a hollow, wide, "
                           "slower glow.",
            .primary = C::AT24,
            .secondaries = {},
            // Whole-instrument pilot (2026-10-26): A keeps its AT-24 claim (the 500 ms attack) and uses the rest: a
            // little excitation, a light fold, Rectify at Amount 0.2 and an Asymmetric Sine Anchor. Level: SHAPE 0.4,
            // Rectify 0.3, Anchor 0.6, sustain 0.8 read -19.5 dBFS, 8.4 dB under the library median (bar 4 dB);
            // SHAPE 0.15, Rectify 0.2, Anchor 0.7 read -16.1 dBFS; sustain 1 lifts the rest.
            .endpointA = {{kCoreDepthId, 0.5},          // depth 0.5
                          {kCoreBodyId, 0.7},           // body 0.7
                          {kCoreEdgeId, 0.3},           // edge 0.3
                          {kCoreExcitationMixId, 0.1},  // excitation mix 0.1
                          {kFoldAmountId, 0.2},         // Harmonic Fold 20
                          {kFoldShapeId, 0.15},         // SHAPE 0.15
                          {kCharacterId, 0.4},          // Rectify
                          {kCharacterAmountId, 0.2},    // Amount 0.2
                          {kCharacterToneId, 0.4},      // Tone 0.4
                          {kAmpAttackId, 0.739794},     // 500 ms
                          {kAmpDecayId, 0.780618},      // 800 ms
                          {kAmpSustainId, 1.0},         // sustain 1
                          {kAmpReleaseId, 0.790849},    // 900 ms
                          {kFieldImageModeId, 0.5},     // SPREAD
                          {kFieldSpreadId, 0.6},        // spread 0.6
                          {kMassId, 0.5},               // Mass 50
                          {kAnchorWaveformId, 0.75},    // Asymmetric Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.7},        // level 0.7
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.7},  // body curvature 0.7
                          {kCoreBodyEmphasisId, 0.7}},  // body emphasis -0.3
            // Morph pilot (2026-10-26): B is a hollow, wide glow: Fold SHAPE at HOLLOW with Fold 40 and the Fold
            // Character, Shift up to +0.7, more edge and less body, a 900 ms attack (library rule 1 caps it at 1 s)
            // and 1.5 s release, a Triangle Anchor at 0.7 and the WIDE image, at full sustain and depth 0.45 (it read
            // 3.8 dB under A at C3 at depth 0.3, Anchor 0.5).
            .endpointB = {{kCoreDepthId, 0.45},         // depth 0.45
                          {kCoreBodyId, 0.3},           // body 0.3
                          {kCoreEdgeId, 0.6},           // edge 0.6
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.4},         // Harmonic Fold 40
                          {kFoldShapeId, 0.5},          // SHAPE 0.5 (HOLLOW)
                          {kCharacterId, 0.2},          // Fold
                          {kCharacterAmountId, 0.4},    // Amount 0.4
                          {kCharacterToneId, 0.6},      // Tone 0.6
                          {kAmpAttackId, 0.790849},     // 900 ms
                          {kAmpDecayId, 0.780618},      // 800 ms
                          {kAmpSustainId, 1.0},         // sustain 1
                          {kAmpReleaseId, 0.835218},    // 1500 ms
                          {kFieldImageModeId, 1.0},     // WIDE
                          {kFieldSpreadId, 0.9},        // spread 0.9
                          {kMassId, 0.45},              // Mass 45
                          {kAnchorWaveformId, 0.25},    // Triangle
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.7},        // level 0.7
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.7},  // body curvature 0.7
                          {kCoreBodyEmphasisId, 0.7},   // body emphasis -0.3
                          {kCoreShiftId, 0.85}},        // shift +0.7
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Swelling",
            .nameB = "Hollow Glow",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 25: AT-25 release (plan 7 row 25) ----
        // Phase 14 T023: converted to M-WHEEL (AT-25 stays with Poly Pluck). Plan 10: M-ENV, M-CYCLE and M-VEL failed
        // Profundum_PresetLoudnessMatch (evidence/gate-exclusions.csv), so it carries the Mod Wheel, which rests at A.
        ProfundumPresetDef{
            .name = "Long Tail",
            .category = "Sub",
            .description = "Morph Mod Wheel: the Mod Wheel carries the sound from A to B (Amount 1; wheel down plays A), "
                           "and while it drives the morph it no longer adds Mass. A lets a folded, wide body ring on "
                           "over a Triangle Anchor with a 2000 ms release; wheel up darkens it into a mono Tube sub "
                           "with a 3.5 s tail.",
            .primary = C::MWheel,
            .secondaries = {},
            // Designed-library pass (2026-10-26): AT-25 had 36.8 of margin; A rings a folded, wide body over a Triangle
            // Anchor.
            .endpointA = {{kCoreDepthId, 0.75},         // depth 0.75
                          {kCoreBodyId, 0.4},           // body 0.4
                          {kCoreEdgeId, 0.3},           // edge 0.3
                          {kCoreExcitationMixId, 0.2},  // excitation mix 0.2
                          {kFoldAmountId, 0.2},         // Harmonic Fold 20
                          {kFoldShapeId, 0.45},         // SHAPE 0.45
                          {kCharacterId, 0.2},          // Fold
                          {kCharacterAmountId, 0.3},    // Amount 0.3
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.4},          // 10 ms
                          {kAmpDecayId, 0.720412},      // 400 ms
                          {kAmpSustainId, 0.8},         // sustain 0.8
                          {kAmpReleaseId, 0.860206},    // 2000 ms
                          {kFieldImageModeId, 1.0},     // WIDE
                          {kFieldSpreadId, 0.6},        // spread 0.6
                          {kMassId, 0.55},              // Mass 55
                          {kAnchorWaveformId, 0.25},    // Triangle
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.9},        // level 0.9
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kCoreShiftId, 0.65}},        // shift 0.3
            // Designed-library pass (2026-10-26): B darkens into a mono Tube sub with a 3.5 s tail (library rule 1 caps
            // release at 4 s).
            .endpointB = {{kCoreDepthId, 0.9},          // depth 0.9
                          {kCoreBodyId, 0.1},           // body 0.1
                          {kCoreEdgeId, 0.1},           // edge 0.1
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.5},    // Amount 0.5
                          {kCharacterToneId, 0.3},      // Tone 0.3
                          {kAmpAttackId, 0.4},          // 10 ms
                          {kAmpDecayId, 0.720412},      // 400 ms
                          {kAmpSustainId, 0.9},         // sustain 0.9
                          {kAmpReleaseId, 0.908814},    // 3.5 s
                          {kFieldImageModeId, 0.0},     // SOLID
                          {kFieldSpreadId, 0.0},        // spread 0
                          {kMassId, 0.65},              // Mass 65
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.9},        // level 0.9
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kCoreShiftId, 0.65}},        // shift 0.3
            // Phase 14 T023 (FR-046, plan 5.7 step 5): the winning M-ENV candidate of evidence/conversions.md (margin
            // 7.704 dB, rule (ii)), its nine fields through the 1003-1011 laws (morph_mod_params.h).
            .controls = {{kMorphPositionId, 0.0},                       // p = 0
                         {kMorphModSourceId, 1.0},                      // Source Mod Wheel (4 / 4)
                         {kMorphModAmountId, 1.0},                      // Amount +1
                         {kMorphModAttackId, 0.39999999922342100},      // attack 10 ms (default)
                         {kMorphModDecayId, 0.69542425054972046},       // decay 300 ms (default)
                         {kMorphModSustainId, 0.0},                     // sustain 0 (default)
                         {kMorphModCycleShapeId, 0.0},                  // Cycle Shape Sine (default)
                         {kMorphModCycleRateId, 0.60587150266824885},   // Cycle Rate 1 Hz (default)
                         {kMorphModCycleSyncId, 0.0},                   // Sync Off (default)
                         {kMorphModCycleSyncRateId, 13.0 / 29.0}},      // Sync Rate 1/4 (index 13, default)
            .nameA = "Long Ring",
            .nameB = "Dark Tail",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 26: AT-26 Mass (plan 7 row 26) ----
        // T027 FR-061 level: sustain 1 -> 0.3 (-4.28 -> -10.77 dBFS; AT-26 unchanged).
        // Anchor level 0.5: the Phase 9 AT-26 per-row setting (G11-2), so Mass can raise the floor. T027: the sine
        // recipe (the probe base): depth 0.9 / body 0.2 / edge 0.15 read 2.59 dB at E1; the AT-27 secondary is withdrawn
        // (a sine core leaves the dynamics stage 0.12 - 2.05 dB, never 3).
        ProfundumPresetDef{
            .name = "Heavy Mass",
            .category = "Sub",
            .description = "Mass Amount: Mass at 100 with a Rounded Anchor at 0.5 and a 4 semitone drop, the heaviest "
                           "held weight. Morph to B drives it: a body, fold and Tube over an octave-down Anchor.",
            .primary = C::AT26,
            .secondaries = {},
            // Designed-library pass (2026-10-26): AT-26 reads E_sub at the Sum tap on a sine recipe (margin 2.25 dB); A
            // keeps the recipe and gains a Rounded Anchor, a 4 st drop, a harder transient and a spread top.
            .endpointA = {{kCoreDepthId, 1.0},                      // depth 1
                          {kCoreBodyId, 0.0},                       // body 0
                          {kCoreEdgeId, 0.0},                       // edge 0
                          {kCoreExcitationMixId, 0.0},              // excitation mix 0
                          {kFoldAmountId, 0.0},                     // Harmonic Fold 0
                          {kFoldShapeId, 0.0},                      // SHAPE 0
                          {kCharacterId, 0.0},                      // Tube
                          {kCharacterAmountId, 0.0},                // Amount 0
                          {kCharacterToneId, 0.5},                  // Tone 0.5
                          {kAmpAttackId, 0.260206},                 // 2 ms
                          {kAmpDecayId, 0.695424},                  // 300 ms
                          {kAmpSustainId, 0.3},                     // sustain 0.3
                          {kAmpReleaseId, 0.708814},                // 350 ms
                          {kFieldImageModeId, 0.5},                 // SPREAD
                          {kFieldSpreadId, 0.3},                    // spread 0.3
                          {kMassId, 1.0},                           // Mass 100
                          {kAnchorWaveformId, 0.5},                 // RoundedSine
                          {kAnchorModeId, 0.0},                     // LOCK
                          {kAnchorLevelId, 0.5},                    // level 0.5
                          {kAnchorFoldFeedId, 0.0},                 // Anchor Fold Feed 0
                          {kDropAmountId, 0.083333},                // drop 4 st
                          {kDropTimeId, 0.493648},                  // drop time 60 ms
                          {kDynamicsTransientAttackId, 0.666667}},  // transient attack +4 dB
            // Designed-library pass (2026-10-26): B drives it: a body, fold and Tube 0.6 over an Octave Down Anchor,
            // wide.
            .endpointB = {{kCoreDepthId, 0.55},         // depth 0.55
                          {kCoreBodyId, 0.5},           // body 0.5
                          {kCoreEdgeId, 0.35},          // edge 0.35
                          {kCoreExcitationMixId, 0.4},  // excitation mix 0.4
                          {kFoldAmountId, 0.45},        // Harmonic Fold 45
                          {kFoldShapeId, 0.5},          // SHAPE 0.5
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.6},    // Amount 0.6
                          {kCharacterToneId, 0.45},     // Tone 0.45
                          {kAmpAttackId, 0.260206},     // 2 ms
                          {kAmpDecayId, 0.695424},      // 300 ms
                          {kAmpSustainId, 0.4},         // sustain 0.4
                          {kAmpReleaseId, 0.708814},    // 350 ms
                          {kFieldImageModeId, 1.0},     // WIDE
                          {kFieldSpreadId, 0.5},        // spread 0.5
                          {kMassId, 1.0},               // Mass 100
                          {kAnchorWaveformId, 1.0},     // OctaveDownSine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.6},        // level 0.6
                          {kAnchorFoldFeedId, 0.0}},    // Anchor Fold Feed 0
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Full Mass",
            .nameB = "Heavy Drive",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 27: AT-27 dynamics gain (plan 7 row 27) ----
        // The probe's AT-27 base B at Mass 100 with sustain 1 (B10-1; T027: sustain 0.8 read 2.92 dB at C3); decay 1990 ms keeps C-6 (attack + decay
        // <= 2000 ms).
        ProfundumPresetDef{
            .name = "Pressure Front",
            .category = "Harmonic",
            .description = "Mass Dynamics Gain: Mass 100 on a heavy harmonic body, the dynamics gain pressing the "
                           "front of each note. Morph to B turns it into a dark, mono, Crush-grained sub with a softer "
                           "front.",
            .primary = C::AT27,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 0.5},                // depth 0.5
                          {kCoreBodyId, 0.3},                 // body 0.3
                          {kCoreEdgeId, 1.0},                 // edge 1
                          {kCoreShiftId, 0.0},                // shift -1
                          {kCoreExcitationMixId, 1.0},        // excitation mix 1
                          {kCoreExcitationDriveId, 1.0},      // drive 1
                          {kCoreExcitationAsymmetryId, 0.5},  // asymmetry 0.5
                          {kFoldAmountId, 0.8},               // Harmonic Fold 80
                          {kFoldShapeId, 0.5},                // SHAPE 0.5
                          {kCharacterId, 0.2},                // Fold
                          {kCharacterAmountId, 0.5},          // Amount 0.5
                          {kCharacterToneId, 0.8},            // Tone 0.8
                          {kAmpAttackId, 0.2},                // 1 ms
                          {kAmpDecayId, 0.859771},            // 1990 ms
                          {kAmpSustainId, 1.0},               // sustain 1
                          {kAmpReleaseId, 0.780618},          // 800 ms
                          {kFieldImageModeId, 1.0},           // WIDE
                          {kFieldSpreadId, 1.0},              // spread 1
                          {kMassId, 1.0},                     // Mass 100
                          {kAnchorWaveformId, 0.0},           // Sine
                          {kAnchorModeId, 0.0},               // LOCK
                          {kAnchorLevelId, 0.5},              // level 0.5
                          {kAnchorFoldFeedId, 0.0}},          // Anchor Fold Feed 0
            // Designed-library pass (2026-10-26): A is unchanged (AT-27 margin 1.01); B is its dark opposite: a clean,
            // sub-heavy body under Crush 0.5 over a mono Triangle Anchor, a 30 ms attack, a 600 ms decay, Mass 70.
            .endpointB = {{kCoreDepthId, 0.9},                // depth 0.9
                          {kCoreBodyId, 0.1},                 // body 0.1
                          {kCoreEdgeId, 0.1},                 // edge 0.1
                          {kCoreShiftId, 0.75},               // shift 0.5
                          {kCoreExcitationMixId, 0.0},        // excitation mix 0
                          {kCoreExcitationDriveId, 1.0},      // drive 1
                          {kCoreExcitationAsymmetryId, 0.5},  // asymmetry 0.5
                          {kFoldAmountId, 0.0},               // Harmonic Fold 0
                          {kFoldShapeId, 0.9},                // SHAPE 0.9
                          {kCharacterId, 0.8},                // Crush
                          {kCharacterAmountId, 0.5},          // Amount 0.5
                          {kCharacterToneId, 0.3},            // Tone 0.3
                          {kAmpAttackId, 0.495424},           // 30 ms
                          {kAmpDecayId, 0.75563},             // 600 ms
                          {kAmpSustainId, 0.5},               // sustain 0.5
                          {kAmpReleaseId, 0.720412},          // 400 ms
                          {kFieldImageModeId, 0.0},           // SOLID
                          {kFieldSpreadId, 0.0},              // spread 0
                          {kMassId, 0.7},                     // Mass 70
                          {kAnchorWaveformId, 0.25},          // Triangle
                          {kAnchorModeId, 0.0},               // LOCK
                          {kAnchorLevelId, 0.45},             // level 0.45
                          {kAnchorFoldFeedId, 0.0}},          // Anchor Fold Feed 0
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Pressing",
            .nameB = "Crushed Front",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 28: AT-28 transient (plan 7 row 28) ----
        // T027 FR-061 level: sustain 0.4 -> 0.85 (-17.92 -> -11.38 dBFS).
        ProfundumPresetDef{
            .name = "Hard Attack",
            .category = "Plucked",
            .description = "Mass Transient Attack: the Transient Attack at +12 dB snaps the front of a folded harmonic "
                           "pluck with a 5 semitone snap. Morph to B rounds it into a soft mono Tube note an octave "
                           "deep.",
            .primary = C::AT28,
            .secondaries = {},
            // Designed-library pass (2026-10-26): AT-28 had 6.23 of margin; the harmonic pluck gains a Triangle Anchor
            // and a 5 st snap.
            .endpointA = {{kCoreDepthId, 0.5},                // depth 0.5
                          {kCoreBodyId, 0.3},                 // body 0.3
                          {kCoreEdgeId, 0.8},                 // edge 0.8
                          {kCoreShiftId, 0.2},                // shift -0.6
                          {kCoreExcitationMixId, 0.8},        // excitation mix 0.8
                          {kCoreExcitationDriveId, 0.8},      // drive 0.8
                          {kCoreExcitationAsymmetryId, 0.3},  // asymmetry 0.3
                          {kFoldAmountId, 0.4},               // Harmonic Fold 40
                          {kFoldShapeId, 0.3},                // SHAPE 0.3
                          {kCharacterId, 0.2},                // Fold
                          {kCharacterAmountId, 0.4},          // Amount 0.4
                          {kCharacterToneId, 0.7},            // Tone 0.7
                          {kAmpAttackId, 0.2},                // 1 ms
                          {kAmpDecayId, 0.730643},            // 450 ms
                          {kAmpSustainId, 0.85},              // sustain 0.85
                          {kAmpReleaseId, 0.679588},          // 250 ms
                          {kFieldImageModeId, 0.5},           // SPREAD
                          {kFieldSpreadId, 0.7},              // spread 0.7
                          {kMassId, 0.55},                    // Mass 55
                          {kAnchorWaveformId, 0.25},          // Triangle
                          {kAnchorModeId, 0.0},               // LOCK
                          {kAnchorLevelId, 0.5},              // level 0.5
                          {kAnchorFoldFeedId, 0.0},           // Anchor Fold Feed 0
                          {kDynamicsTransientAttackId, 1.0},  // transient attack +12 dB
                          {kDropAmountId, 0.104167},          // drop 5 st
                          {kDropTimeId, 0.388095}},           // drop time 25 ms
            // Designed-library pass (2026-10-26): B is the opposite: a round, mono Tube note over an Octave Down Anchor
            // with no transient boost.
            .endpointB = {{kCoreDepthId, 0.8},                 // depth 0.8
                          {kCoreBodyId, 0.5},                  // body 0.5
                          {kCoreEdgeId, 0.2},                  // edge 0.2
                          {kCoreShiftId, 0.2},                 // shift -0.6
                          {kCoreExcitationMixId, 0.2},         // excitation mix 0.2
                          {kCoreExcitationDriveId, 0.8},       // drive 0.8
                          {kCoreExcitationAsymmetryId, 0.3},   // asymmetry 0.3
                          {kFoldAmountId, 0.1},                // Harmonic Fold 10
                          {kFoldShapeId, 0.0},                 // SHAPE 0
                          {kCharacterId, 0.0},                 // Tube
                          {kCharacterAmountId, 0.5},           // Amount 0.5
                          {kCharacterToneId, 0.3},             // Tone 0.3
                          {kAmpAttackId, 0.2},                 // 1 ms
                          {kAmpDecayId, 0.76902},              // 700 ms
                          {kAmpSustainId, 0.85},               // sustain 0.85
                          {kAmpReleaseId, 0.679588},           // 250 ms
                          {kFieldImageModeId, 0.0},            // SOLID
                          {kFieldSpreadId, 0.0},               // spread 0
                          {kMassId, 0.7},                      // Mass 70
                          {kAnchorWaveformId, 1.0},            // OctaveDownSine
                          {kAnchorModeId, 0.0},                // LOCK
                          {kAnchorLevelId, 0.8},               // level 0.8
                          {kAnchorFoldFeedId, 0.0},            // Anchor Fold Feed 0
                          {kDynamicsTransientAttackId, 0.5}},  // transient attack 0 dB
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Snapping",
            .nameB = "Soft Round",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 29: AT-29 SPREAD (plan 7 row 29; secondary AT-31) ----
        ProfundumPresetDef{
            .name = "Wide Harmonics",
            .category = "Harmonic",
            .description = "Field Solid to Spread: the SPREAD image at spread 1 opens a folded, Bite-edged harmonic "
                           "body while the sub stays centred. Morph to B darkens it into a Tube-warmed spread an "
                           "octave deep.",
            .primary = C::AT29,
            .secondaries = {C::AT31},
            // Designed-library pass (2026-10-26): AT-29 / AT-31 had 113 of margin; the open body gains a fold, Bite
            // 0.35 and an Asymmetric Anchor.
            .endpointA = {{kCoreDepthId, 0.5},             // depth 0.5
                          {kCoreBodyId, 0.6},              // body 0.6
                          {kCoreEdgeId, 0.5},              // edge 0.5
                          {kCoreExcitationMixId, 0.4},     // excitation mix 0.4
                          {kFoldAmountId, 0.3},            // Harmonic Fold 30
                          {kFoldShapeId, 0.4},             // SHAPE 0.4
                          {kCharacterId, 0.6},             // Bite
                          {kCharacterAmountId, 0.35},      // Amount 0.35
                          {kCharacterToneId, 0.6},         // Tone 0.6
                          {kAmpAttackId, 0.295424},        // 3 ms
                          {kAmpDecayId, 0.75563},          // 600 ms
                          {kAmpSustainId, 0.85},           // sustain 0.85
                          {kAmpReleaseId, 0.739794},       // 500 ms
                          {kFieldImageModeId, 0.5},        // SPREAD
                          {kFieldSpreadId, 1.0},           // spread 1
                          {kMassId, 0.5},                  // Mass 50
                          {kAnchorWaveformId, 0.75},       // AsymmetricSine
                          {kAnchorModeId, 0.0},            // LOCK
                          {kAnchorLevelId, 0.6},           // level 0.6
                          {kAnchorFoldFeedId, 0.0},        // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.6},     // body curvature 0.6
                          {kCoreShiftId, 0.6},             // shift 0.2
                          {kCoreExcitationDriveId, 0.6}},  // drive 0.6
            // Designed-library pass (2026-10-26): B is a dark, Tube-warmed spread over an Octave Down Anchor (SPREAD at
            // spread 1 on both: AT-29).
            .endpointB = {{kCoreDepthId, 0.8},             // depth 0.8
                          {kCoreBodyId, 0.2},              // body 0.2
                          {kCoreEdgeId, 0.2},              // edge 0.2
                          {kCoreExcitationMixId, 0.0},     // excitation mix 0
                          {kFoldAmountId, 0.0},            // Harmonic Fold 0
                          {kFoldShapeId, 0.0},             // SHAPE 0
                          {kCharacterId, 0.0},             // Tube
                          {kCharacterAmountId, 0.6},       // Amount 0.6
                          {kCharacterToneId, 0.3},         // Tone 0.3
                          {kAmpAttackId, 0.295424},        // 3 ms
                          {kAmpDecayId, 0.75563},          // 600 ms
                          {kAmpSustainId, 0.5},            // sustain 0.5
                          {kAmpReleaseId, 0.739794},       // 500 ms
                          {kFieldImageModeId, 0.5},        // SPREAD
                          {kFieldSpreadId, 1.0},           // spread 1
                          {kMassId, 0.65},                 // Mass 65
                          {kAnchorWaveformId, 1.0},        // OctaveDownSine
                          {kAnchorModeId, 0.0},            // LOCK
                          {kAnchorLevelId, 0.5},           // level 0.5
                          {kAnchorFoldFeedId, 0.0},        // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.6},     // body curvature 0.6
                          {kCoreShiftId, 0.25},            // shift -0.5
                          {kCoreExcitationDriveId, 0.6}},  // drive 0.6
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Spread Open",
            .nameB = "Spread Dark",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 30: AT-30 WIDE (plan 7 row 30) ----
        // The probe base A_s + Rectify at Amount 0.5 + WIDE, spread 1; Tone 0.5 and Fold 0 (C-9 rule 4).
        ProfundumPresetDef{
            .name = "Wide Rectifier",
            .category = "Driven",
            .description = "Field Solid to Wide: Rectify at 0.5 with Tone 0.5 over a lightly folded body and a Rounded "
                           "Anchor under the WIDE image at spread 1. Morph to B bites, wide, an octave deep.",
            .primary = C::AT30,
            .secondaries = {},
            // Designed-library pass (2026-10-26): AT-30 had 96.7 of margin; A gains a body, excitation, a light fold
            // and a Rounded Anchor (Tone 0.5: rule 4).
            .endpointA = {{kCoreDepthId, 1.0},          // depth 1
                          {kCoreBodyId, 0.3},           // body 0.3
                          {kCoreEdgeId, 0.3},           // edge 0.3
                          {kCoreExcitationMixId, 0.2},  // excitation mix 0.2
                          {kFoldAmountId, 0.2},         // Harmonic Fold 20
                          {kFoldShapeId, 0.3},          // SHAPE 0.3
                          {kCharacterId, 0.4},          // Rectify
                          {kCharacterAmountId, 0.5},    // Amount 0.5
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.260206},     // 2 ms
                          {kAmpDecayId, 0.708814},      // 350 ms
                          {kAmpSustainId, 0.9},         // sustain 0.9
                          {kAmpReleaseId, 0.695424},    // 300 ms
                          {kFieldImageModeId, 1.0},     // WIDE
                          {kFieldSpreadId, 1.0},        // spread 1
                          {kMassId, 0.5},               // Mass 50
                          {kAnchorWaveformId, 0.5},     // RoundedSine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.8},        // level 0.8
                          {kAnchorFoldFeedId, 0.0}},    // Anchor Fold Feed 0
            // Designed-library pass (2026-10-26): B bites wide over an Octave Down Anchor (WIDE at spread 1 on both:
            // AT-30).
            .endpointB = {{kCoreDepthId, 0.8},          // depth 0.8
                          {kCoreBodyId, 0.5},           // body 0.5
                          {kCoreEdgeId, 0.5},           // edge 0.5
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.4},         // Harmonic Fold 40
                          {kFoldShapeId, 0.7},          // SHAPE 0.7
                          {kCharacterId, 0.6},          // Bite
                          {kCharacterAmountId, 0.5},    // Amount 0.5
                          {kCharacterToneId, 0.6},      // Tone 0.6
                          {kAmpAttackId, 0.260206},     // 2 ms
                          {kAmpDecayId, 0.708814},      // 350 ms
                          {kAmpSustainId, 1.0},         // sustain 1
                          {kAmpReleaseId, 0.695424},    // 300 ms
                          {kFieldImageModeId, 1.0},     // WIDE
                          {kFieldSpreadId, 1.0},        // spread 1
                          {kMassId, 0.65},              // Mass 65
                          {kAnchorWaveformId, 1.0},     // OctaveDownSine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 1.0},        // level 1
                          {kAnchorFoldFeedId, 0.0}},    // Anchor Fold Feed 0
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Wide Rectified",
            .nameB = "Wide Bite",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 31: AT-31 Spread (plan 7 row 31) ----
        ProfundumPresetDef{
            .name = "Spread Choir",
            .category = "Harmonic",
            .description = "Field Spread: SPREAD at spread 0.9 over a high, folded body touched by Fracture, a choir "
                           "of partials around a centred floor. Morph to B turns it into a Rectify-edged choir.",
            .primary = C::AT31,
            .secondaries = {},
            // Designed-library pass (2026-10-26): AT-31 had 108 of margin; the choir gains excitation, a fold, Fracture
            // 0.15 and a Triangle Anchor at level 1, at full sustain and depth 0.7 (it read -20.8 dBFS before).
            .endpointA = {{kCoreDepthId, 0.7},           // depth 0.7
                          {kCoreBodyId, 0.85},           // body 0.85
                          {kCoreEdgeId, 0.35},           // edge 0.35
                          {kCoreExcitationMixId, 0.3},   // excitation mix 0.3
                          {kFoldAmountId, 0.25},         // Harmonic Fold 25
                          {kFoldShapeId, 0.5},           // SHAPE 0.5
                          {kCharacterId, 1.0},           // Fracture
                          {kCharacterAmountId, 0.15},    // Amount 0.15
                          {kCharacterToneId, 0.55},      // Tone 0.55
                          {kAmpAttackId, 0.460206},      // 20 ms
                          {kAmpDecayId, 0.76902},        // 700 ms
                          {kAmpSustainId, 1.0},          // sustain 1
                          {kAmpReleaseId, 0.76902},      // 700 ms
                          {kFieldImageModeId, 0.5},      // SPREAD
                          {kFieldSpreadId, 0.9},         // spread 0.9
                          {kMassId, 0.45},               // Mass 45
                          {kAnchorWaveformId, 0.25},     // Triangle
                          {kAnchorModeId, 0.0},          // LOCK
                          {kAnchorLevelId, 1.0},         // level 1
                          {kAnchorFoldFeedId, 0.0},      // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.75},  // body curvature 0.75
                          {kCoreShiftId, 0.55},          // shift 0.1
                          {kFieldGravityId, 0.4}},       // gravity 0.4
            // Designed-library pass (2026-10-26): B is a lopsided, Rectify-edged choir over a Rounded Anchor (SPREAD on
            // both: AT-31).
            .endpointB = {{kCoreDepthId, 0.4},             // depth 0.4
                          {kCoreBodyId, 0.3},              // body 0.3
                          {kCoreEdgeId, 0.7},              // edge 0.7
                          {kCoreExcitationMixId, 0.5},     // excitation mix 0.5
                          {kFoldAmountId, 0.5},            // Harmonic Fold 50
                          {kFoldShapeId, 0.9},             // SHAPE 0.9
                          {kCharacterId, 0.4},             // Rectify
                          {kCharacterAmountId, 0.4},       // Amount 0.4
                          {kCharacterToneId, 0.7},         // Tone 0.7
                          {kAmpAttackId, 0.460206},        // 20 ms
                          {kAmpDecayId, 0.76902},          // 700 ms
                          {kAmpSustainId, 0.45},           // sustain 0.45
                          {kAmpReleaseId, 0.76902},        // 700 ms
                          {kFieldImageModeId, 0.5},        // SPREAD
                          {kFieldSpreadId, 0.9},           // spread 0.9
                          {kMassId, 0.6},                  // Mass 60
                          {kAnchorWaveformId, 0.5},        // RoundedSine
                          {kAnchorModeId, 0.0},            // LOCK
                          {kAnchorLevelId, 0.55},          // level 0.55
                          {kAnchorFoldFeedId, 0.0},        // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.75},    // body curvature 0.75
                          {kCoreShiftId, 0.75},            // shift 0.5
                          {kFieldGravityId, 0.4},          // gravity 0.4
                          {kCoreExcitationDriveId, 0.8}},  // drive 0.8
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Choir",
            .nameB = "Rectified Choir",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 32: AT-32 Gravity (plan 7 row 32) ----
        ProfundumPresetDef{
            .name = "Gravity Well",
            .category = "Resonant",
            .description = "Field Gravity: Gravity at 1 pulls the low band of a folded SPREAD image at spread 1 back "
                           "to the centre, over an octave-down Anchor. Morph to B darkens it into a Crush-grained well "
                           "with half the pull.",
            .primary = C::AT32,
            .secondaries = {},
            // Designed-library pass (2026-10-26): AT-32 had 10.1 of margin; the well gains an Octave Down Anchor and
            // Mass 60.
            .endpointA = {{kCoreDepthId, 0.5},                // depth 0.5
                          {kCoreBodyId, 0.4},                 // body 0.4
                          {kCoreEdgeId, 0.7},                 // edge 0.7
                          {kCoreShiftId, 0.3},                // shift -0.4
                          {kCoreExcitationMixId, 0.6},        // excitation mix 0.6
                          {kCoreExcitationDriveId, 0.6},      // drive 0.6
                          {kCoreExcitationAsymmetryId, 0.5},  // asymmetry 0.5
                          {kFoldAmountId, 0.4},               // Harmonic Fold 40
                          {kFoldShapeId, 0.5},                // SHAPE 0.5
                          {kCharacterId, 0.2},                // Fold
                          {kCharacterAmountId, 0.3},          // Amount 0.3
                          {kCharacterToneId, 0.6},            // Tone 0.6
                          {kAmpAttackId, 0.2},                // 1 ms
                          {kAmpDecayId, 0.835218},            // 1500 ms
                          {kAmpSustainId, 1.0},               // sustain 1
                          {kAmpReleaseId, 0.76902},           // 700 ms
                          {kFieldImageModeId, 0.5},           // SPREAD
                          {kFieldSpreadId, 1.0},              // spread 1
                          {kMassId, 0.6},                     // Mass 60
                          {kAnchorWaveformId, 1.0},           // OctaveDownSine
                          {kAnchorModeId, 0.0},               // LOCK
                          {kAnchorLevelId, 0.5},              // level 0.5
                          {kAnchorFoldFeedId, 0.0},           // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.7},        // body curvature 0.7
                          {kFieldGravityId, 1.0}},            // gravity 1
            // Designed-library pass (2026-10-26): B is a dark, Crush-grained well with half the gravity (SPREAD at
            // spread 1 on both: AT-32).
            .endpointB = {{kCoreDepthId, 0.8},                // depth 0.8
                          {kCoreBodyId, 0.2},                 // body 0.2
                          {kCoreEdgeId, 0.2},                 // edge 0.2
                          {kCoreShiftId, 0.3},                // shift -0.4
                          {kCoreExcitationMixId, 0.1},        // excitation mix 0.1
                          {kCoreExcitationDriveId, 0.6},      // drive 0.6
                          {kCoreExcitationAsymmetryId, 0.5},  // asymmetry 0.5
                          {kFoldAmountId, 0.1},               // Harmonic Fold 10
                          {kFoldShapeId, 0.0},                // SHAPE 0
                          {kCharacterId, 0.8},                // Crush
                          {kCharacterAmountId, 0.5},          // Amount 0.5
                          {kCharacterToneId, 0.4},            // Tone 0.4
                          {kAmpAttackId, 0.2},                // 1 ms
                          {kAmpDecayId, 0.780618},            // 800 ms
                          {kAmpSustainId, 0.5},               // sustain 0.5
                          {kAmpReleaseId, 0.76902},           // 700 ms
                          {kFieldImageModeId, 0.5},           // SPREAD
                          {kFieldSpreadId, 1.0},              // spread 1
                          {kMassId, 0.7},                     // Mass 70
                          {kAnchorWaveformId, 0.0},           // Sine
                          {kAnchorModeId, 0.0},               // LOCK
                          {kAnchorLevelId, 0.5},              // level 0.5
                          {kAnchorFoldFeedId, 0.0},           // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.7},        // body curvature 0.7
                          {kFieldGravityId, 0.5}},            // gravity 0.5
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Deep Well",
            .nameB = "Crushed Well",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 33: AT-33 Field Motion (plan 7 row 33) ----
        // P-4: SPREAD, spread 1 and Field Motion depth 1 on endpoint A. Field Motion source DRIFT (the probe
        // base's) at depth 1, so seedDependent (C-7, plan 6 rule 9).
        ProfundumPresetDef{
            .name = "Orbiting Field",
            .category = "Animated",
            .description = "Field Motion: Field Motion at depth 1 orbits a SPREAD image at spread 1 around a folded, "
                           "Bite-edged core over an Asymmetric Anchor. Morph to B darkens it into a wide Tube orbit an "
                           "octave deep.",
            .primary = C::AT33,
            .secondaries = {},
            // Designed-library pass (2026-10-26): AT-33 had 110 of margin; the orbit gains a fold, Bite 0.35 and an
            // Asymmetric Anchor.
            .endpointA = {{kCoreDepthId, 0.55},           // depth 0.55
                          {kCoreBodyId, 0.4},             // body 0.4
                          {kCoreEdgeId, 0.6},             // edge 0.6
                          {kCoreExcitationMixId, 0.5},    // excitation mix 0.5
                          {kFoldAmountId, 0.3},           // Harmonic Fold 30
                          {kFoldShapeId, 0.4},            // SHAPE 0.4
                          {kCharacterId, 0.6},            // Bite
                          {kCharacterAmountId, 0.35},     // Amount 0.35
                          {kCharacterToneId, 0.55},       // Tone 0.55
                          {kAmpAttackId, 0.260206},       // 2 ms
                          {kAmpDecayId, 0.835218},        // 1500 ms
                          {kAmpSustainId, 1.0},           // sustain 1
                          {kAmpReleaseId, 0.75563},       // 600 ms
                          {kFieldImageModeId, 0.5},       // SPREAD
                          {kFieldSpreadId, 1.0},          // spread 1
                          {kMassId, 0.5},                 // Mass 50
                          {kAnchorWaveformId, 0.75},      // AsymmetricSine
                          {kAnchorModeId, 0.0},           // LOCK
                          {kAnchorLevelId, 0.5},          // level 0.5
                          {kAnchorFoldFeedId, 0.0},       // Anchor Fold Feed 0
                          {kCoreShiftId, 0.35},           // shift -0.3
                          {kCoreExcitationDriveId, 0.7},  // drive 0.7
                          {kFieldMotionDepthId, 1.0},     // Field Motion depth 1
                          {kFieldMotionRateId, 0.5},      // Field Motion rate 0.5
                          {kFieldMotionSourceId, 0.0}},   // Field Motion DRIFT
            // Designed-library pass (2026-10-26): B is a dark Tube orbit over an Octave Down Anchor, WIDE.
            .endpointB = {{kCoreDepthId, 0.85},           // depth 0.85
                          {kCoreBodyId, 0.3},             // body 0.3
                          {kCoreEdgeId, 0.2},             // edge 0.2
                          {kCoreExcitationMixId, 0.1},    // excitation mix 0.1
                          {kFoldAmountId, 0.0},           // Harmonic Fold 0
                          {kFoldShapeId, 0.0},            // SHAPE 0
                          {kCharacterId, 0.0},            // Tube
                          {kCharacterAmountId, 0.55},     // Amount 0.55
                          {kCharacterToneId, 0.35},       // Tone 0.35
                          {kAmpAttackId, 0.260206},       // 2 ms
                          {kAmpDecayId, 0.835218},        // 1500 ms
                          {kAmpSustainId, 1.0},           // sustain 1
                          {kAmpReleaseId, 0.75563},       // 600 ms
                          {kFieldImageModeId, 1.0},       // WIDE
                          {kFieldSpreadId, 0.8},          // spread 0.8
                          {kMassId, 0.65},                // Mass 65
                          {kAnchorWaveformId, 1.0},       // OctaveDownSine
                          {kAnchorModeId, 0.0},           // LOCK
                          {kAnchorLevelId, 0.5},          // level 0.5
                          {kAnchorFoldFeedId, 0.0},       // Anchor Fold Feed 0
                          {kCoreShiftId, 0.35},           // shift -0.3
                          {kCoreExcitationDriveId, 0.7},  // drive 0.7
                          {kFieldMotionDepthId, 1.0},     // Field Motion depth 1
                          {kFieldMotionRateId, 0.35},     // Field Motion rate 0.35
                          {kFieldMotionSourceId, 0.0}},   // Field Motion DRIFT
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Orbiting",
            .nameB = "Dark Orbit",
            .seed = kFactoryDefaultSeed,
            .seedDependent = true,
        },
        // ---- 34: AT-34 CYCLE to Mass (plan 7 row 34) ----
        // T027 FR-061 level: sustain 1 -> 0.5 (-6.55 -> -11.18 dBFS).
        ProfundumPresetDef{
            .name = "Mass Tide",
            .category = "Animated",
            .description = "Motion Cycle to Mass: a slow CYCLE swells and drains the Mass of a warm Tube-driven body under an "
                           "Anchor at 0.5. Morph to B whips it into a fractured, wide storm at 1 Hz.",
            .primary = C::AT34,
            .secondaries = {},
            // Whole-instrument pilot (2026-10-26): A keeps its AT-34 claim (the CYCLE on Mass) and uses the rest: body
            // and edge with a little excitation and fold, Shift down, Tube at Amount 0.45.
            .endpointA = {{kCoreDepthId, 0.85},            // depth 0.85
                          {kCoreBodyId, 0.35},             // body 0.35
                          {kCoreEdgeId, 0.25},             // edge 0.25
                          {kCoreExcitationMixId, 0.2},     // excitation mix 0.2
                          {kFoldAmountId, 0.3},            // Harmonic Fold 30
                          {kFoldShapeId, 0.2},             // SHAPE 0.2
                          {kCharacterId, 0.0},             // Tube
                          {kCharacterAmountId, 0.45},      // Amount 0.45
                          {kCharacterToneId, 0.45},        // Tone 0.45
                          {kAmpAttackId, 0.2},             // 1 ms
                          {kAmpDecayId, 0.695424},         // 300 ms
                          {kAmpSustainId, 0.5},            // sustain 0.5
                          {kAmpReleaseId, 0.720412},       // 400 ms
                          {kFieldImageModeId, 0.5},        // SPREAD
                          {kFieldSpreadId, 0.3},           // spread 0.3
                          {kMassId, 0.5},                  // Mass 50
                          {kAnchorWaveformId, 0.0},        // Sine
                          {kAnchorModeId, 0.0},            // LOCK
                          {kAnchorLevelId, 0.5},           // level 0.5
                          {kAnchorFoldFeedId, 0.0},        // Anchor Fold Feed 0
                          {kMotionSourceId, 2.0 / 3.0},    // CYCLE
                          {kMotionTargetId, 0.0},          // -> Mass
                          {kMotionAmountId, 1.0},          // amount 1
                          {kMotionDepthId, 1.0},           // Motion depth 1
                          {kMotionCycleRateId, 0.423486},  // 0.25 Hz
                          {kMotionCycleShapeId, 0.0},      // shape Sine
                          {kMotionPolarityId, 0.0},        // Bipolar
                          {kCoreShiftId, 0.4}},            // shift -0.2
            // Morph pilot (2026-10-26): B is a storm: the same CYCLE on Mass at 1 Hz over a body and edge with
            // excitation, Fold SHAPE at HOLLOW, Fracture at Amount 0.5 and the WIDE image.
            .endpointB = {{kCoreDepthId, 0.6},             // depth 0.6
                          {kCoreBodyId, 0.5},              // body 0.5
                          {kCoreEdgeId, 0.5},              // edge 0.5
                          {kCoreExcitationMixId, 0.4},     // excitation mix 0.4
                          {kFoldAmountId, 0.5},            // Harmonic Fold 50
                          {kFoldShapeId, 0.5},             // SHAPE 0.5 (HOLLOW)
                          {kCharacterId, 1.0},             // Fracture
                          {kCharacterAmountId, 0.5},       // Amount 0.5
                          {kCharacterToneId, 0.6},         // Tone 0.6
                          {kAmpAttackId, 0.2},             // 1 ms
                          {kAmpDecayId, 0.695424},         // 300 ms
                          {kAmpSustainId, 0.9},            // sustain 0.9 (B at 0.5 read 5.1 dB under A)
                          {kAmpReleaseId, 0.720412},       // 400 ms
                          {kFieldImageModeId, 1.0},        // WIDE
                          {kFieldSpreadId, 0.8},           // spread 0.8
                          {kMassId, 0.6},                  // Mass 60
                          {kAnchorWaveformId, 0.0},        // Sine
                          {kAnchorModeId, 0.0},            // LOCK
                          {kAnchorLevelId, 0.5},           // level 0.5
                          {kAnchorFoldFeedId, 0.0},        // Anchor Fold Feed 0
                          {kMotionSourceId, 2.0 / 3.0},    // CYCLE
                          {kMotionTargetId, 0.0},          // -> Mass
                          {kMotionAmountId, 1.0},          // amount 1
                          {kMotionDepthId, 1.0},           // Motion depth 1
                          {kMotionCycleRateId, 0.605875},  // 1 Hz
                          {kMotionCycleShapeId, 0.0},      // shape Sine
                          {kMotionPolarityId, 0.0}},       // Bipolar
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Tide",
            .nameB = "Storm Tide",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 35: AT-35 to Depth (plan 7 row 35) ----
        ProfundumPresetDef{
            .name = "Breathing Depth",
            .category = "Animated",
            .description = "Motion Cycle to Depth: a CYCLE breathes the core Depth of a heavy, Crush-grained harmonic "
                           "body in and out over a Triangle Anchor, wide. Morph to B breathes a warm, mono Tube body.",
            .primary = C::AT35,
            .secondaries = {},
            // Designed-library pass (2026-10-26): AT-35 had 2.06 of margin; the shared heavy body trades the Fold
            // Character for Crush 0.4, over a Triangle Anchor, WIDE.
            .endpointA = {{kCoreDepthId, 0.5},                // depth 0.5
                          {kCoreBodyId, 0.3},                 // body 0.3
                          {kCoreEdgeId, 1.0},                 // edge 1
                          {kCoreShiftId, 0.0},                // shift -1
                          {kCoreExcitationMixId, 1.0},        // excitation mix 1
                          {kCoreExcitationDriveId, 1.0},      // drive 1
                          {kCoreExcitationAsymmetryId, 0.5},  // asymmetry 0.5
                          {kFoldAmountId, 0.6},               // Harmonic Fold 60
                          {kFoldShapeId, 0.5},                // SHAPE 0.5
                          {kCharacterId, 0.8},                // Crush
                          {kCharacterAmountId, 0.4},          // Amount 0.4
                          {kCharacterToneId, 0.55},           // Tone 0.55
                          {kAmpAttackId, 0.2},                // 1 ms
                          {kAmpDecayId, 0.835218},            // 1500 ms
                          {kAmpSustainId, 1.0},               // sustain 1
                          {kAmpReleaseId, 0.780618},          // 800 ms
                          {kFieldImageModeId, 1.0},           // WIDE
                          {kFieldSpreadId, 0.7},              // spread 0.7
                          {kMassId, 0.5},                     // Mass 50
                          {kAnchorWaveformId, 0.25},          // Triangle
                          {kAnchorModeId, 0.0},               // LOCK
                          {kAnchorLevelId, 0.5},              // level 0.5
                          {kAnchorFoldFeedId, 0.0},           // Anchor Fold Feed 0
                          {kMotionSourceId, 2.0 / 3.0},       // CYCLE
                          {kMotionTargetId, 0.25},            // -> Depth
                          {kMotionAmountId, 1.0},             // amount 1
                          {kMotionDepthId, 1.0},              // Motion depth 1
                          {kMotionCycleRateId, 0.485321},     // 0.4 Hz
                          {kMotionCycleShapeId, 0.0},         // shape Sine
                          {kMotionPolarityId, 0.0}},          // Bipolar
            // Designed-library pass (2026-10-26): B breathes a warm, mono Tube body.
            .endpointB = {{kCoreDepthId, 0.85},               // depth 0.85
                          {kCoreBodyId, 0.4},                 // body 0.4
                          {kCoreEdgeId, 0.3},                 // edge 0.3
                          {kCoreShiftId, 0.5},                // shift 0
                          {kCoreExcitationMixId, 0.2},        // excitation mix 0.2
                          {kCoreExcitationDriveId, 1.0},      // drive 1
                          {kCoreExcitationAsymmetryId, 0.5},  // asymmetry 0.5
                          {kFoldAmountId, 0.2},               // Harmonic Fold 20
                          {kFoldShapeId, 0.2},                // SHAPE 0.2
                          {kCharacterId, 0.0},                // Tube
                          {kCharacterAmountId, 0.5},          // Amount 0.5
                          {kCharacterToneId, 0.4},            // Tone 0.4
                          {kAmpAttackId, 0.2},                // 1 ms
                          {kAmpDecayId, 0.835218},            // 1500 ms
                          {kAmpSustainId, 1.0},               // sustain 1
                          {kAmpReleaseId, 0.780618},          // 800 ms
                          {kFieldImageModeId, 0.0},           // SOLID
                          {kFieldSpreadId, 0.0},              // spread 0
                          {kMassId, 0.6},                     // Mass 60
                          {kAnchorWaveformId, 0.0},           // Sine
                          {kAnchorModeId, 0.0},               // LOCK
                          {kAnchorLevelId, 0.5},              // level 0.5
                          {kAnchorFoldFeedId, 0.0},           // Anchor Fold Feed 0
                          {kMotionSourceId, 2.0 / 3.0},       // CYCLE
                          {kMotionTargetId, 0.25},            // -> Depth
                          {kMotionAmountId, 1.0},             // amount 1
                          {kMotionDepthId, 1.0},              // Motion depth 1
                          {kMotionCycleRateId, 0.423486},     // 0.25 Hz
                          {kMotionCycleShapeId, 0.0},         // shape Sine
                          {kMotionPolarityId, 0.0}},          // Bipolar
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Breathing",
            .nameB = "Warm Breath",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 36: AT-36 to Body (plan 7 row 36) ----
        ProfundumPresetDef{
            .name = "Body Wave",
            .category = "Animated",
            .description = "Motion Cycle to Body: a CYCLE sweeps the Body of a Rectify-edged harmonic core over a "
                           "Rounded Anchor, a slow wave through the mids. Morph to B waves a Bite-edged body, wide.",
            .primary = C::AT36,
            .secondaries = {},
            // Designed-library pass (2026-10-26): AT-36 had 2.81 of margin; the shared heavy body trades the Fold
            // Character for Rectify 0.4, over a Rounded Anchor.
            .endpointA = {{kCoreDepthId, 0.5},                // depth 0.5
                          {kCoreBodyId, 0.3},                 // body 0.3
                          {kCoreEdgeId, 1.0},                 // edge 1
                          {kCoreShiftId, 0.0},                // shift -1
                          {kCoreExcitationMixId, 1.0},        // excitation mix 1
                          {kCoreExcitationDriveId, 1.0},      // drive 1
                          {kCoreExcitationAsymmetryId, 0.5},  // asymmetry 0.5
                          {kFoldAmountId, 0.8},               // Harmonic Fold 80
                          {kFoldShapeId, 0.5},                // SHAPE 0.5
                          {kCharacterId, 0.4},                // Rectify
                          {kCharacterAmountId, 0.4},          // Amount 0.4
                          {kCharacterToneId, 0.5},            // Tone 0.5
                          {kAmpAttackId, 0.2},                // 1 ms
                          {kAmpDecayId, 0.835218},            // 1500 ms
                          {kAmpSustainId, 1.0},               // sustain 1
                          {kAmpReleaseId, 0.780618},          // 800 ms
                          {kFieldImageModeId, 0.5},           // SPREAD
                          {kFieldSpreadId, 0.8},              // spread 0.8
                          {kMassId, 0.5},                     // Mass 50
                          {kAnchorWaveformId, 0.5},           // RoundedSine
                          {kAnchorModeId, 0.0},               // LOCK
                          {kAnchorLevelId, 0.5},              // level 0.5
                          {kAnchorFoldFeedId, 0.0},           // Anchor Fold Feed 0
                          {kMotionSourceId, 2.0 / 3.0},       // CYCLE
                          {kMotionTargetId, 0.5},             // -> Body
                          {kMotionAmountId, 1.0},             // amount 1
                          {kMotionDepthId, 1.0},              // Motion depth 1
                          {kMotionCycleRateId, 0.514679},     // 0.5 Hz
                          {kMotionCycleShapeId, 0.2},         // shape Triangle
                          {kMotionPolarityId, 0.0}},          // Bipolar
            // Designed-library pass (2026-10-26): B waves a Bite-edged body over a Triangle Anchor, WIDE.
            .endpointB = {{kCoreDepthId, 0.8},                // depth 0.8
                          {kCoreBodyId, 0.5},                 // body 0.5
                          {kCoreEdgeId, 0.3},                 // edge 0.3
                          {kCoreShiftId, 0.35},               // shift -0.3
                          {kCoreExcitationMixId, 0.3},        // excitation mix 0.3
                          {kCoreExcitationDriveId, 1.0},      // drive 1
                          {kCoreExcitationAsymmetryId, 0.5},  // asymmetry 0.5
                          {kFoldAmountId, 0.3},               // Harmonic Fold 30
                          {kFoldShapeId, 0.1},                // SHAPE 0.1
                          {kCharacterId, 0.6},                // Bite
                          {kCharacterAmountId, 0.45},         // Amount 0.45
                          {kCharacterToneId, 0.5},            // Tone 0.5
                          {kAmpAttackId, 0.2},                // 1 ms
                          {kAmpDecayId, 0.835218},            // 1500 ms
                          {kAmpSustainId, 1.0},               // sustain 1
                          {kAmpReleaseId, 0.780618},          // 800 ms
                          {kFieldImageModeId, 1.0},           // WIDE
                          {kFieldSpreadId, 0.6},              // spread 0.6
                          {kMassId, 0.6},                     // Mass 60
                          {kAnchorWaveformId, 0.25},          // Triangle
                          {kAnchorModeId, 0.0},               // LOCK
                          {kAnchorLevelId, 0.5},              // level 0.5
                          {kAnchorFoldFeedId, 0.0},           // Anchor Fold Feed 0
                          {kMotionSourceId, 2.0 / 3.0},       // CYCLE
                          {kMotionTargetId, 0.5},             // -> Body
                          {kMotionAmountId, 1.0},             // amount 1
                          {kMotionDepthId, 1.0},              // Motion depth 1
                          {kMotionCycleRateId, 0.447473},     // 0.3 Hz
                          {kMotionCycleShapeId, 0.2},         // shape Triangle
                          {kMotionPolarityId, 0.0}},          // Bipolar
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Body Wave",
            .nameB = "Bitten Wave",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 37: AT-37 to Edge (plan 7 row 37) ----
        // T027 FR-061 level: sustain 0.9 -> 1, depth 0 -> 0.1 (-15.55 -> -12.67 dBFS).
        // The probe's AT-37 base: A_s with recipe kSawAnchor at edge 0.5 (the per-row setting).
        ProfundumPresetDef{
            .name = "Edge Cycle",
            .category = "Animated",
            .description = "Motion Cycle to Edge: a saw-like recipe whose Edge a CYCLE opens and closes, snapping "
                           "wide. Morph to B sweeps a folded, Fracture-grained body, mono, an octave deep.",
            .primary = C::AT37,
            .secondaries = {},
            // Designed-library pass (2026-10-26): AT-37 reads brightness swing at the Sum tap (margin 1.19), so the
            // recipe and Anchor stay; A gains a WIDE image and a 2 st snap.
            .endpointA = {{kCoreDepthId, 0.1},             // depth 0.1
                          {kCoreBodyId, 0.0},              // body 0
                          {kCoreEdgeId, 0.5},              // edge 0.5
                          {kCoreExcitationMixId, 0.0},     // excitation mix 0
                          {kFoldAmountId, 0.0},            // Harmonic Fold 0
                          {kFoldShapeId, 0.0},             // SHAPE 0
                          {kCharacterId, 0.0},             // Tube
                          {kCharacterAmountId, 0.0},       // Amount 0
                          {kCharacterToneId, 0.5},         // Tone 0.5
                          {kAmpAttackId, 0.260206},        // 2 ms
                          {kAmpDecayId, 0.720412},         // 400 ms
                          {kAmpSustainId, 1.0},            // sustain 1
                          {kAmpReleaseId, 0.695424},       // 300 ms
                          {kFieldImageModeId, 1.0},        // WIDE
                          {kFieldSpreadId, 0.6},           // spread 0.6
                          {kMassId, 0.5},                  // Mass 50
                          {kAnchorWaveformId, 0.0},        // Sine
                          {kAnchorModeId, 0.0},            // LOCK
                          {kAnchorLevelId, 1.0},           // level 1
                          {kAnchorFoldFeedId, 0.0},        // Anchor Fold Feed 0
                          {kCoreShiftId, 0.5},             // shift 0
                          {kMotionSourceId, 2.0 / 3.0},    // CYCLE
                          {kMotionTargetId, 0.75},         // -> Edge
                          {kMotionAmountId, 1.0},          // amount 1
                          {kMotionDepthId, 1.0},           // Motion depth 1
                          {kMotionCycleRateId, 0.576514},  // 0.8 Hz
                          {kMotionCycleShapeId, 0.0},      // shape Sine
                          {kMotionPolarityId, 0.0},        // Bipolar
                          {kDropAmountId, 0.041667},       // drop 2 st
                          {kDropTimeId, 0.361191}},        // drop time 20 ms
            // Designed-library pass (2026-10-26): B sweeps a folded, Fracture-grained body over a mono Octave Down
            // Anchor.
            .endpointB = {{kCoreDepthId, 0.75},            // depth 0.75
                          {kCoreBodyId, 0.5},              // body 0.5
                          {kCoreEdgeId, 0.3},              // edge 0.3
                          {kCoreExcitationMixId, 0.4},     // excitation mix 0.4
                          {kFoldAmountId, 0.4},            // Harmonic Fold 40
                          {kFoldShapeId, 0.5},             // SHAPE 0.5
                          {kCharacterId, 1.0},             // Fracture
                          {kCharacterAmountId, 0.45},      // Amount 0.45
                          {kCharacterToneId, 0.55},        // Tone 0.55
                          {kAmpAttackId, 0.260206},        // 2 ms
                          {kAmpDecayId, 0.720412},         // 400 ms
                          {kAmpSustainId, 1.0},            // sustain 1
                          {kAmpReleaseId, 0.695424},       // 300 ms
                          {kFieldImageModeId, 0.0},        // SOLID
                          {kFieldSpreadId, 0.0},           // spread 0
                          {kMassId, 0.65},                 // Mass 65
                          {kAnchorWaveformId, 1.0},        // OctaveDownSine
                          {kAnchorModeId, 0.0},            // LOCK
                          {kAnchorLevelId, 1.0},           // level 1
                          {kAnchorFoldFeedId, 0.0},        // Anchor Fold Feed 0
                          {kCoreShiftId, 0.5},             // shift 0
                          {kMotionSourceId, 2.0 / 3.0},    // CYCLE
                          {kMotionTargetId, 0.75},         // -> Edge
                          {kMotionAmountId, 1.0},          // amount 1
                          {kMotionDepthId, 1.0},           // Motion depth 1
                          {kMotionCycleRateId, 0.514679},  // 0.5 Hz
                          {kMotionCycleShapeId, 0.0},      // shape Sine
                          {kMotionPolarityId, 0.0}},       // Bipolar
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Edge Sweep",
            .nameB = "Fractured Sweep",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 38: AT-38 to Shift (plan 7 row 38) ----
        // The probe's AT-38 base B_s and rate (0.30 / 0.30 oct against 0.25 oct at 0.1 Hz), kept as it is.
        ProfundumPresetDef{
            .name = "Shifting Spectrum",
            .category = "Animated",
            .description = "Motion Cycle to Shift: a slow CYCLE drifts the spectral Shift of a heavy harmonic body up "
                           "and down. Morph to B shifts a warm Tube body over a Triangle Anchor.",
            .primary = C::AT38,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 0.5},                // depth 0.5
                          {kCoreBodyId, 0.3},                 // body 0.3
                          {kCoreEdgeId, 1.0},                 // edge 1
                          {kCoreShiftId, 0.0},                // shift -1
                          {kCoreExcitationMixId, 1.0},        // excitation mix 1
                          {kCoreExcitationDriveId, 1.0},      // drive 1
                          {kCoreExcitationAsymmetryId, 0.5},  // asymmetry 0.5
                          {kFoldAmountId, 0.8},               // Harmonic Fold 80
                          {kFoldShapeId, 0.5},                // SHAPE 0.5
                          {kCharacterId, 0.2},                // Fold
                          {kCharacterAmountId, 0.5},          // Amount 0.5
                          {kCharacterToneId, 0.8},            // Tone 0.8
                          {kAmpAttackId, 0.2},                // 1 ms
                          {kAmpDecayId, 0.835218},            // 1500 ms
                          {kAmpSustainId, 1.0},               // sustain 1
                          {kAmpReleaseId, 0.780618},          // 800 ms
                          {kFieldImageModeId, 1.0},           // WIDE
                          {kFieldSpreadId, 1.0},              // spread 1
                          {kMassId, 0.5},                     // Mass 50
                          {kAnchorWaveformId, 0.0},           // Sine
                          {kAnchorModeId, 0.0},               // LOCK
                          {kAnchorLevelId, 0.5},              // level 0.5
                          {kAnchorFoldFeedId, 0.0},           // Anchor Fold Feed 0
                          {kMotionSourceId, 2.0 / 3.0},       // CYCLE
                          {kMotionTargetId, 1.0},             // -> Shift
                          {kMotionAmountId, 1.0},             // amount 1
                          {kMotionDepthId, 1.0},              // Motion depth 1
                          {kMotionCycleRateId, 0.302936},     // 0.1 Hz
                          {kMotionCycleShapeId, 0.0},         // shape Sine
                          {kMotionPolarityId, 0.0}},          // Bipolar
            // Designed-library pass (2026-10-26): A is unchanged (AT-38 margin 0.05); B shifts a warm Tube body over a
            // Triangle Anchor.
            .endpointB = {{kCoreDepthId, 0.85},               // depth 0.85
                          {kCoreBodyId, 0.5},                 // body 0.5
                          {kCoreEdgeId, 0.3},                 // edge 0.3
                          {kCoreShiftId, 0.5},                // shift 0
                          {kCoreExcitationMixId, 0.2},        // excitation mix 0.2
                          {kCoreExcitationDriveId, 0.5},      // drive 0.5
                          {kCoreExcitationAsymmetryId, 0.5},  // asymmetry 0.5
                          {kFoldAmountId, 0.2},               // Harmonic Fold 20
                          {kFoldShapeId, 0.1},                // SHAPE 0.1
                          {kCharacterId, 0.0},                // Tube
                          {kCharacterAmountId, 0.5},          // Amount 0.5
                          {kCharacterToneId, 0.4},            // Tone 0.4
                          {kAmpAttackId, 0.2},                // 1 ms
                          {kAmpDecayId, 0.835218},            // 1500 ms
                          {kAmpSustainId, 0.6},               // sustain 0.6
                          {kAmpReleaseId, 0.780618},          // 800 ms
                          {kFieldImageModeId, 0.5},           // SPREAD
                          {kFieldSpreadId, 0.5},              // spread 0.5
                          {kMassId, 0.6},                     // Mass 60
                          {kAnchorWaveformId, 0.25},          // Triangle
                          {kAnchorModeId, 0.0},               // LOCK
                          {kAnchorLevelId, 0.5},              // level 0.5
                          {kAnchorFoldFeedId, 0.0},           // Anchor Fold Feed 0
                          {kMotionSourceId, 2.0 / 3.0},       // CYCLE
                          {kMotionTargetId, 1.0},             // -> Shift
                          {kMotionAmountId, 1.0},             // amount 1
                          {kMotionDepthId, 1.0},              // Motion depth 1
                          {kMotionCycleRateId, 0.35628},      // 0.15 Hz
                          {kMotionCycleShapeId, 0.0},         // shape Sine
                          {kMotionPolarityId, 0.0}},          // Bipolar
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Shifting",
            .nameB = "Warm Shift",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 39: AT-39 ENV to Mass (plan 7 row 39) ----
        // C-9: ENV is Unipolar, so the swing is 0 -> +50 Mass. T027: from Mass 0 (the steepest E_sub span; Mass 25 read
        // 1.37 dB at E1), ENV 1000 ms up / 1000 ms down so the slewed Mass reaches its peak and returns inside the 2 s
        // retrigger, SOLID and the A_s amp envelope (1 ms / 150 ms): 3.26 / 3.18 dB.
        ProfundumPresetDef{
            .name = "Envelope Mass",
            .category = "Animated",
            .description = "Motion Env to Mass: a Unipolar ENV, 1000 ms up and 1000 ms down, swells the Mass from 0 at "
                           "every note and lets it settle. Morph to B swells a Crush-grained, folded body, wide.",
            .primary = C::AT39,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 1.0},           // depth 1
                          {kCoreBodyId, 0.0},            // body 0
                          {kCoreEdgeId, 0.0},            // edge 0
                          {kCoreExcitationMixId, 0.0},   // excitation mix 0
                          {kFoldAmountId, 0.0},          // Harmonic Fold 0
                          {kFoldShapeId, 0.0},           // SHAPE 0
                          {kCharacterId, 0.0},           // Tube
                          {kCharacterAmountId, 0.0},     // Amount 0
                          {kCharacterToneId, 0.5},       // Tone 0.5
                          {kAmpAttackId, 0.2},           // 1 ms
                          {kAmpDecayId, 0.635218},       // 150 ms
                          {kAmpSustainId, 1.0},          // sustain 1
                          {kAmpReleaseId, 0.720412},     // 400 ms
                          {kFieldImageModeId, 0.0},      // SOLID
                          {kFieldSpreadId, 0.0},         // spread 0
                          {kMassId, 0.0},                // Mass 0
                          {kAnchorWaveformId, 0.0},      // Sine
                          {kAnchorModeId, 0.0},          // LOCK
                          {kAnchorLevelId, 0.5},         // level 0.5
                          {kAnchorFoldFeedId, 0.0},      // Anchor Fold Feed 0
                          {kMotionSourceId, 1.0 / 3.0},  // ENV
                          {kMotionTargetId, 0.0},        // -> Mass
                          {kMotionAmountId, 1.0},        // amount 1
                          {kMotionDepthId, 1.0},         // Motion depth 1
                          {kMotionPolarityId, 1.0},      // Unipolar
                          {kMotionEnvAttackId, 0.8},     // ENV attack 1000 ms
                          {kMotionEnvDecayId, 0.8}},     // ENV decay 1000 ms
            // Designed-library pass (2026-10-26): A is unchanged (AT-39 margin 0.18); B swells a Crush-grained, folded
            // body over a Rounded Anchor, WIDE.
            .endpointB = {{kCoreDepthId, 0.6},            // depth 0.6
                          {kCoreBodyId, 0.5},             // body 0.5
                          {kCoreEdgeId, 0.5},             // edge 0.5
                          {kCoreExcitationMixId, 0.5},    // excitation mix 0.5
                          {kFoldAmountId, 0.5},           // Harmonic Fold 50
                          {kFoldShapeId, 0.6},            // SHAPE 0.6
                          {kCharacterId, 0.8},            // Crush
                          {kCharacterAmountId, 0.45},     // Amount 0.45
                          {kCharacterToneId, 0.55},       // Tone 0.55
                          {kAmpAttackId, 0.2},            // 1 ms
                          {kAmpDecayId, 0.635218},        // 150 ms
                          {kAmpSustainId, 1.0},           // sustain 1
                          {kAmpReleaseId, 0.720412},      // 400 ms
                          {kFieldImageModeId, 1.0},       // WIDE
                          {kFieldSpreadId, 0.6},          // spread 0.6
                          {kMassId, 0.0},                 // Mass 0
                          {kAnchorWaveformId, 0.5},       // RoundedSine
                          {kAnchorModeId, 0.0},           // LOCK
                          {kAnchorLevelId, 0.7},          // level 0.7
                          {kAnchorFoldFeedId, 0.0},       // Anchor Fold Feed 0
                          {kMotionSourceId, 1.0 / 3.0},   // ENV
                          {kMotionTargetId, 0.0},         // -> Mass
                          {kMotionAmountId, 1.0},         // amount 1
                          {kMotionDepthId, 1.0},          // Motion depth 1
                          {kMotionPolarityId, 1.0},       // Unipolar
                          {kMotionEnvAttackId, 0.8},      // ENV attack 1000 ms
                          {kMotionEnvDecayId, 0.76902}},  // ENV decay 700 ms
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Mass Kick",
            .nameB = "Crushed Kick",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 40: AT-40 DRIFT to Mass (plan 7 row 40) ----
        ProfundumPresetDef{
            .name = "Drifting Weight",
            .category = "Animated",
            .description = "Motion Drift to Mass: a DRIFT source at smoothness 0.5 wanders the Mass, different under "
                           "every seed. Morph to B drifts a folded, Fracture-grained body.",
            .primary = C::AT40,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 1.0},              // depth 1
                          {kCoreBodyId, 0.0},               // body 0
                          {kCoreEdgeId, 0.0},               // edge 0
                          {kCoreExcitationMixId, 0.0},      // excitation mix 0
                          {kFoldAmountId, 0.0},             // Harmonic Fold 0
                          {kFoldShapeId, 0.0},              // SHAPE 0
                          {kCharacterId, 0.0},              // Tube
                          {kCharacterAmountId, 0.0},        // Amount 0
                          {kCharacterToneId, 0.5},          // Tone 0.5
                          {kAmpAttackId, 0.415836},         // 12 ms
                          {kAmpDecayId, 0.720412},          // 400 ms
                          {kAmpSustainId, 1.0},             // sustain 1
                          {kAmpReleaseId, 0.739794},        // 500 ms
                          {kFieldImageModeId, 1.0},         // WIDE
                          {kFieldSpreadId, 0.6},            // spread 0.6
                          {kMassId, 0.5},                   // Mass 50
                          {kAnchorWaveformId, 0.0},         // Sine
                          {kAnchorModeId, 0.0},             // LOCK
                          {kAnchorLevelId, 0.5},            // level 0.5
                          {kAnchorFoldFeedId, 0.0},         // Anchor Fold Feed 0
                          {kMotionSourceId, 1.0},           // DRIFT
                          {kMotionTargetId, 0.0},           // -> Mass
                          {kMotionAmountId, 1.0},           // amount 1
                          {kMotionDepthId, 1.0},            // Motion depth 1
                          {kMotionDriftSmoothnessId, 0.5},  // smoothness 0.5
                          {kMotionPolarityId, 0.0}},        // Bipolar
            // Designed-library pass (2026-10-26): A is unchanged (AT-40 margin 0.38); B drifts a folded,
            // Fracture-grained body over an Asymmetric Anchor.
            .endpointB = {{kCoreDepthId, 0.85},             // depth 0.85
                          {kCoreBodyId, 0.5},               // body 0.5
                          {kCoreEdgeId, 0.45},              // edge 0.45
                          {kCoreExcitationMixId, 0.4},      // excitation mix 0.4
                          {kFoldAmountId, 0.3},             // Harmonic Fold 30
                          {kFoldShapeId, 0.4},              // SHAPE 0.4
                          {kCharacterId, 1.0},              // Fracture
                          {kCharacterAmountId, 0.4},        // Amount 0.4
                          {kCharacterToneId, 0.5},          // Tone 0.5
                          {kAmpAttackId, 0.415836},         // 12 ms
                          {kAmpDecayId, 0.720412},          // 400 ms
                          {kAmpSustainId, 1.0},             // sustain 1
                          {kAmpReleaseId, 0.739794},        // 500 ms
                          {kFieldImageModeId, 0.5},         // SPREAD
                          {kFieldSpreadId, 0.5},            // spread 0.5
                          {kMassId, 0.5},                   // Mass 50
                          {kAnchorWaveformId, 0.75},        // AsymmetricSine
                          {kAnchorModeId, 0.0},             // LOCK
                          {kAnchorLevelId, 0.8},            // level 0.8
                          {kAnchorFoldFeedId, 0.0},         // Anchor Fold Feed 0
                          {kMotionSourceId, 1.0},           // DRIFT
                          {kMotionTargetId, 0.0},           // -> Mass
                          {kMotionAmountId, 1.0},           // amount 1
                          {kMotionDepthId, 1.0},            // Motion depth 1
                          {kMotionDriftSmoothnessId, 0.7},  // smoothness 0.7
                          {kMotionPolarityId, 0.0}},        // Bipolar
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Drifting",
            .nameB = "Fractured Drift",
            .seed = kFactoryDefaultSeed,
            .seedDependent = true,
        },
        // ---- 41: AT-41 Morph 0 to 1 (plan 7 row 41; secondary AT-42) ----
        // T027 FR-061 level: endpoint A sustain 1 -> 0.6 (-6.46 -> -10.90 dBFS at p = 0).
        // A distinct endpoint B (plan 7): the Phase 8 showcase pair, A_s against B (decay 1990 ms, C-6).
        ProfundumPresetDef{
            .name = "Morph Span",
            .category = "Harmonic",
            .description = "Morph Position Full: endpoint A is a clean sub, endpoint B a driven harmonic growl; Morph from 0 to "
                           "1 crosses the whole span.",
            .primary = C::AT41,
            .secondaries = {C::AT42},
            // Designed-library pass (2026-10-26): AT-41 had 14.4 of margin, AT-42 6.55; the clean sub gains Harmonic
            // Fold 15.
            .endpointA = {{kCoreDepthId, 0.85},         // depth 0.85
                          {kCoreBodyId, 0.2},           // body 0.2
                          {kCoreEdgeId, 0.1},           // edge 0.1
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.15},        // Harmonic Fold 15
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.0},    // Amount 0
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.2},          // 1 ms
                          {kAmpDecayId, 0.635218},      // 150 ms
                          {kAmpSustainId, 0.6},         // sustain 0.6
                          {kAmpReleaseId, 0.695424},    // 300 ms
                          {kFieldImageModeId, 0.5},     // SPREAD
                          {kFieldSpreadId, 0.3},        // spread 0.3
                          {kMassId, 0.5},               // Mass 50
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.9},        // level 0.9
                          {kAnchorFoldFeedId, 0.0}},    // Anchor Fold Feed 0
            .endpointB = {{kCoreDepthId, 0.5},                // depth 0.5
                          {kCoreBodyId, 0.3},                 // body 0.3
                          {kCoreEdgeId, 1.0},                 // edge 1
                          {kCoreShiftId, 0.0},                // shift -1
                          {kCoreExcitationMixId, 1.0},        // excitation mix 1
                          {kCoreExcitationDriveId, 1.0},      // drive 1
                          {kCoreExcitationAsymmetryId, 0.5},  // asymmetry 0.5
                          {kFoldAmountId, 0.8},               // Harmonic Fold 80
                          {kFoldShapeId, 0.5},                // SHAPE 0.5
                          {kCharacterId, 0.2},                // Fold
                          {kCharacterAmountId, 0.5},          // Amount 0.5
                          {kCharacterToneId, 0.8},            // Tone 0.8
                          {kAmpAttackId, 0.2},                // 1 ms
                          {kAmpDecayId, 0.859771},            // 1990 ms
                          {kAmpSustainId, 0.8},               // sustain 0.8
                          {kAmpReleaseId, 0.780618},          // 800 ms
                          {kFieldImageModeId, 1.0},           // WIDE
                          {kFieldSpreadId, 1.0},              // spread 1
                          {kMassId, 0.5},                     // Mass 50
                          {kAnchorWaveformId, 0.0},           // Sine
                          {kAnchorModeId, 0.0},               // LOCK
                          {kAnchorLevelId, 0.5},              // level 0.5
                          {kAnchorFoldFeedId, 0.0}},          // Anchor Fold Feed 0
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Clean Sub",
            .nameB = "Driven Growl",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 42: AT-42 Morph 0 to 0.5 (plan 7 row 42) ----
        // T027 FR-061 level: sustain 0.9 -> 1, endpoint A depth 0.2 -> 0.6 (-17.49 -> -10.83 dBFS; AT-42 margin 4.38).
        // A distinct endpoint B (plan 7): recipe kBodyRound on A, kBodyHollow driven by excitation and Bite on B;
        // stored Morph Position 0.5. T027: Mass 20 on A, 90 on B (55 on both read D_nl 2.26 dB at E1): 4.73 / 5.90 dB.
        ProfundumPresetDef{
            .name = "Halfway Morph",
            .category = "Resonant",
            .description = "Morph Position Half: a round endpoint A against a hollow, bright, driven endpoint B, stored halfway "
                           "at 0.5.",
            .primary = C::AT42,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 0.6},          // depth 0.6
                          {kCoreBodyId, 0.0},           // body 0
                          {kCoreEdgeId, 0.1},           // edge 0.1
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.0},    // Amount 0
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.295424},     // 3 ms
                          {kAmpDecayId, 0.739794},      // 500 ms
                          {kAmpSustainId, 1.0},         // sustain 1
                          {kAmpReleaseId, 0.739794},    // 500 ms
                          {kFieldImageModeId, 0.0},     // SOLID
                          {kFieldSpreadId, 0.0},        // spread 0
                          {kMassId, 0.2},               // Mass 20
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.8},        // level 0.8
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.1},  // body curvature 0.1
                          {kCoreBodyEmphasisId, 0.2},   // body emphasis -0.8
                          {kCoreShiftId, 0.3}},         // shift -0.4
            .endpointB = {{kCoreDepthId, 0.0},            // depth 0
                          {kCoreBodyId, 0.25},            // body 0.25
                          {kCoreEdgeId, 0.9},             // edge 0.9
                          {kCoreExcitationMixId, 0.8},    // excitation mix 0.8
                          {kFoldAmountId, 0.0},           // Harmonic Fold 0
                          {kFoldShapeId, 0.0},            // SHAPE 0
                          {kCharacterId, 0.6},            // Bite
                          {kCharacterAmountId, 0.7},      // Amount 0.7
                          {kCharacterToneId, 0.7},        // Tone 0.7
                          {kAmpAttackId, 0.295424},       // 3 ms
                          {kAmpDecayId, 0.739794},        // 500 ms
                          {kAmpSustainId, 1.0},           // sustain 1
                          {kAmpReleaseId, 0.739794},      // 500 ms
                          {kFieldImageModeId, 0.5},       // SPREAD
                          {kFieldSpreadId, 0.8},          // spread 0.8
                          {kMassId, 0.9},                 // Mass 90
                          {kAnchorWaveformId, 0.0},       // Sine
                          {kAnchorModeId, 0.0},           // LOCK
                          {kAnchorLevelId, 0.5},          // level 0.5
                          {kAnchorFoldFeedId, 0.0},       // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.6},    // body curvature 0.6
                          {kCoreBodyEmphasisId, 0.0},     // body emphasis -1
                          {kCoreShiftId, 0.925},          // shift 0.85
                          {kCoreExcitationDriveId, 0.8}}, // drive 0.8
            .controls = {{kMorphPositionId, 0.5}}, // p = 0.5
            .nameA = "Round",
            .nameB = "Hollow Bright",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 43: AT-43 AlwaysRetrigger (plan 7 row 43) ----
        ProfundumPresetDef{
            .name = "Restrike",
            .category = "Resonant",
            .description = "NoteMemory AlwaysRetrigger: MONO with Retrigger Reset, every note restrikes a "
                           "Fracture-grained, folded body from the start, wide. Morph to B restrikes a lopsided, "
                           "Rectify-edged edge, mono, an octave deep.",
            .primary = C::AT43,
            .secondaries = {},
            // Designed-library pass (2026-10-26): AT-43 had 137 degrees of margin; the restruck body gains excitation,
            // a fold, Fracture 0.2, a Rounded Anchor at level 1 and a WIDE image, at full sustain and depth 0.6
            // (Fracture 0.3 read -17.1 dBFS).
            .endpointA = {{kCoreDepthId, 0.6},          // depth 0.6
                          {kCoreBodyId, 0.8},           // body 0.8
                          {kCoreEdgeId, 0.4},           // edge 0.4
                          {kCoreExcitationMixId, 0.3},  // excitation mix 0.3
                          {kFoldAmountId, 0.25},        // Harmonic Fold 25
                          {kFoldShapeId, 0.45},         // SHAPE 0.45
                          {kCharacterId, 1.0},          // Fracture
                          {kCharacterAmountId, 0.2},    // Amount 0.2
                          {kCharacterToneId, 0.55},     // Tone 0.55
                          {kAmpAttackId, 0.2},          // 1 ms
                          {kAmpDecayId, 0.720412},      // 400 ms
                          {kAmpSustainId, 1.0},         // sustain 1
                          {kAmpReleaseId, 0.695424},    // 300 ms
                          {kFieldImageModeId, 1.0},     // WIDE
                          {kFieldSpreadId, 0.5},        // spread 0.5
                          {kMassId, 0.5},               // Mass 50
                          {kAnchorWaveformId, 0.5},     // RoundedSine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 1.0},        // level 1
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.9},  // body curvature 0.9
                          {kCoreBodyEmphasisId, 0.5},   // body emphasis -0.5
                          {kCoreShiftId, 0.6}},         // shift 0.2
            // Designed-library pass (2026-10-26): B restrikes a lopsided, Rectify-edged edge, mono, over an Octave Down
            // Anchor at sustain 0.35 (it read 4 dB over A at 0.45).
            .endpointB = {{kCoreDepthId, 0.7},                 // depth 0.7
                          {kCoreBodyId, 0.3},                  // body 0.3
                          {kCoreEdgeId, 0.6},                  // edge 0.6
                          {kCoreExcitationMixId, 0.5},         // excitation mix 0.5
                          {kFoldAmountId, 0.5},                // Harmonic Fold 50
                          {kFoldShapeId, 0.9},                 // SHAPE 0.9
                          {kCharacterId, 0.4},                 // Rectify
                          {kCharacterAmountId, 0.45},          // Amount 0.45
                          {kCharacterToneId, 0.6},             // Tone 0.6
                          {kAmpAttackId, 0.2},                 // 1 ms
                          {kAmpDecayId, 0.720412},             // 400 ms
                          {kAmpSustainId, 0.35},               // sustain 0.35
                          {kAmpReleaseId, 0.695424},           // 300 ms
                          {kFieldImageModeId, 0.0},            // SOLID
                          {kFieldSpreadId, 0.0},               // spread 0
                          {kMassId, 0.65},                     // Mass 65
                          {kAnchorWaveformId, 1.0},            // OctaveDownSine
                          {kAnchorModeId, 0.0},                // LOCK
                          {kAnchorLevelId, 0.8},               // level 0.8
                          {kAnchorFoldFeedId, 0.0},            // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.9},         // body curvature 0.9
                          {kCoreBodyEmphasisId, 0.5},          // body emphasis -0.5
                          {kCoreShiftId, 0.6},                 // shift 0.2
                          {kCoreExcitationDriveId, 0.8},       // drive 0.8
                          {kCoreExcitationAsymmetryId, 0.8}},  // asymmetry 0.8
            .controls = {{kNoteMemoryId, 0.0},     // Note Memory AlwaysRetrigger
                         {kNoteRetriggerId, 0.0},  // Retrigger Reset
                         {kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Restruck",
            .nameB = "Rectified Strike",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 44: AT-44 Legato (plan 7 row 44) ----
        ProfundumPresetDef{
            .name = "Legato Line",
            .category = "Resonant",
            .description = "NoteMemory Legato: MONO with Retrigger Reset; overlapping notes carry the phase on, a "
                           "joined, Tube-warmed line over a Triangle Anchor. Morph to B fractures the line, wide.",
            .primary = C::AT44,
            .secondaries = {},
            // Designed-library pass (2026-10-26): AT-44 had 137 degrees of margin; the line gains excitation, a light
            // fold, Tube 0.4, a Triangle Anchor and a spread top.
            .endpointA = {{kCoreDepthId, 0.7},          // depth 0.7
                          {kCoreBodyId, 0.45},          // body 0.45
                          {kCoreEdgeId, 0.2},           // edge 0.2
                          {kCoreExcitationMixId, 0.2},  // excitation mix 0.2
                          {kFoldAmountId, 0.25},        // Harmonic Fold 25
                          {kFoldShapeId, 0.3},          // SHAPE 0.3
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.4},    // Amount 0.4
                          {kCharacterToneId, 0.45},     // Tone 0.45
                          {kAmpAttackId, 0.380618},     // 8 ms
                          {kAmpDecayId, 0.75563},       // 600 ms
                          {kAmpSustainId, 0.9},         // sustain 0.9
                          {kAmpReleaseId, 0.730643},    // 450 ms
                          {kFieldImageModeId, 0.5},     // SPREAD
                          {kFieldSpreadId, 0.4},        // spread 0.4
                          {kMassId, 0.55},              // Mass 55
                          {kAnchorWaveformId, 0.25},    // Triangle
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.7},        // level 0.7
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kCoreBodyEmphasisId, 0.6},   // body emphasis -0.4
                          {kCoreShiftId, 0.4}},         // shift -0.2
            // Designed-library pass (2026-10-26): B fractures the line: more edge, a BITE fold, Fracture 0.4 over an
            // Asymmetric Anchor, wide.
            .endpointB = {{kCoreDepthId, 0.6},          // depth 0.6
                          {kCoreBodyId, 0.3},           // body 0.3
                          {kCoreEdgeId, 0.7},           // edge 0.7
                          {kCoreExcitationMixId, 0.5},  // excitation mix 0.5
                          {kFoldAmountId, 0.5},         // Harmonic Fold 50
                          {kFoldShapeId, 0.8},          // SHAPE 0.8
                          {kCharacterId, 1.0},          // Fracture
                          {kCharacterAmountId, 0.4},    // Amount 0.4
                          {kCharacterToneId, 0.6},      // Tone 0.6
                          {kAmpAttackId, 0.380618},     // 8 ms
                          {kAmpDecayId, 0.75563},       // 600 ms
                          {kAmpSustainId, 1.0},         // sustain 1
                          {kAmpReleaseId, 0.730643},    // 450 ms
                          {kFieldImageModeId, 1.0},     // WIDE
                          {kFieldSpreadId, 0.7},        // spread 0.7
                          {kMassId, 0.65},              // Mass 65
                          {kAnchorWaveformId, 0.75},    // AsymmetricSine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.9},        // level 0.9
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kCoreBodyEmphasisId, 0.6},   // body emphasis -0.4
                          {kCoreShiftId, 0.4}},         // shift -0.2
            .controls = {{kNoteMemoryId, 1.0 / 3.0},  // Note Memory Legato
                         {kNoteRetriggerId, 0.0},     // Retrigger Reset
                         {kMorphPositionId, 0.0}},    // p = 0
            .nameA = "Joined",
            .nameB = "Fractured Line",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 45: AT-45 EnvRetriggerOnly (plan 7 row 45) ----
        // T027 FR-061 level: sustain 0.7 -> 1, depth 0.2 -> 0.5 (-23.93 -> -11.59 dBFS).
        ProfundumPresetDef{
            .name = "Envelope Restart",
            .category = "Harmonic",
            .description = "NoteMemory EnvelopeRetriggerOnly: MONO with Retrigger Reset; each note restarts the "
                           "envelope but not the oscillator, a Crush-grained, folded edge, wide. Morph to B rounds it "
                           "into a mono Tube body.",
            .primary = C::AT45,
            .secondaries = {},
            // Designed-library pass (2026-10-26): AT-45 had 137 degrees of margin; the restart gains a BITE-leaning
            // fold, Crush 0.35, a Triangle Anchor and a WIDE image.
            .endpointA = {{kCoreDepthId, 0.5},             // depth 0.5
                          {kCoreBodyId, 0.3},              // body 0.3
                          {kCoreEdgeId, 0.8},              // edge 0.8
                          {kCoreExcitationMixId, 0.6},     // excitation mix 0.6
                          {kFoldAmountId, 0.35},           // Harmonic Fold 35
                          {kFoldShapeId, 0.6},             // SHAPE 0.6
                          {kCharacterId, 0.8},             // Crush
                          {kCharacterAmountId, 0.35},      // Amount 0.35
                          {kCharacterToneId, 0.6},         // Tone 0.6
                          {kAmpAttackId, 0.2},             // 1 ms
                          {kAmpDecayId, 0.739794},         // 500 ms
                          {kAmpSustainId, 1.0},            // sustain 1
                          {kAmpReleaseId, 0.720412},       // 400 ms
                          {kFieldImageModeId, 1.0},        // WIDE
                          {kFieldSpreadId, 0.5},           // spread 0.5
                          {kMassId, 0.5},                  // Mass 50
                          {kAnchorWaveformId, 0.25},       // Triangle
                          {kAnchorModeId, 0.0},            // LOCK
                          {kAnchorLevelId, 0.6},           // level 0.6
                          {kAnchorFoldFeedId, 0.0},        // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.2},     // body curvature 0.2
                          {kCoreShiftId, 0.75},            // shift 0.5
                          {kCoreExcitationDriveId, 0.7}},  // drive 0.7
            // Designed-library pass (2026-10-26): B is a round, mono Tube body over an Octave Down Anchor.
            .endpointB = {{kCoreDepthId, 0.85},            // depth 0.85
                          {kCoreBodyId, 0.5},              // body 0.5
                          {kCoreEdgeId, 0.2},              // edge 0.2
                          {kCoreExcitationMixId, 0.1},     // excitation mix 0.1
                          {kFoldAmountId, 0.1},            // Harmonic Fold 10
                          {kFoldShapeId, 0.1},             // SHAPE 0.1
                          {kCharacterId, 0.0},             // Tube
                          {kCharacterAmountId, 0.5},       // Amount 0.5
                          {kCharacterToneId, 0.3},         // Tone 0.3
                          {kAmpAttackId, 0.2},             // 1 ms
                          {kAmpDecayId, 0.739794},         // 500 ms
                          {kAmpSustainId, 1.0},            // sustain 1
                          {kAmpReleaseId, 0.720412},       // 400 ms
                          {kFieldImageModeId, 0.0},        // SOLID
                          {kFieldSpreadId, 0.0},           // spread 0
                          {kMassId, 0.6},                  // Mass 60
                          {kAnchorWaveformId, 1.0},        // OctaveDownSine
                          {kAnchorModeId, 0.0},            // LOCK
                          {kAnchorLevelId, 0.6},           // level 0.6
                          {kAnchorFoldFeedId, 0.0},        // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.2},     // body curvature 0.2
                          {kCoreShiftId, 0.75},            // shift 0.5
                          {kCoreExcitationDriveId, 0.7}},  // drive 0.7
            .controls = {{kNoteMemoryId, 2.0 / 3.0},  // Note Memory EnvelopeRetriggerOnly
                         {kNoteRetriggerId, 0.0},     // Retrigger Reset
                         {kMorphPositionId, 0.0}},    // p = 0
            .nameA = "Restarting",
            .nameB = "Round Restart",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 46: AT-46 OscContinuous (plan 7 row 46) ----
        ProfundumPresetDef{
            .name = "Continuous Phase",
            .category = "Sub",
            .description = "NoteMemory OscillatorContinuous: MONO with Retrigger Reset; the oscillator runs on through "
                           "every note, a Fracture-edged fold over a Triangle Anchor. Morph to B crushes it wide.",
            .primary = C::AT46,
            .secondaries = {},
            // Designed-library pass (2026-10-26): AT-46 had 137 degrees of margin; A runs a Fracture-edged fold over a
            // Triangle Anchor.
            .endpointA = {{kCoreDepthId, 0.6},             // depth 0.6
                          {kCoreBodyId, 0.5},              // body 0.5
                          {kCoreEdgeId, 0.3},              // edge 0.3
                          {kCoreExcitationMixId, 0.3},     // excitation mix 0.3
                          {kFoldAmountId, 0.35},           // Harmonic Fold 35
                          {kFoldShapeId, 0.25},            // SHAPE 0.25
                          {kCharacterId, 1.0},             // Fracture
                          {kCharacterAmountId, 0.3},       // Amount 0.3
                          {kCharacterToneId, 0.5},         // Tone 0.5
                          {kAmpAttackId, 0.339794},        // 5 ms
                          {kAmpDecayId, 0.695424},         // 300 ms
                          {kAmpSustainId, 0.75},           // sustain 0.75
                          {kAmpReleaseId, 0.708814},       // 350 ms
                          {kFieldImageModeId, 0.5},        // SPREAD
                          {kFieldSpreadId, 0.4},           // spread 0.4
                          {kMassId, 0.65},                 // Mass 65
                          {kAnchorWaveformId, 0.25},       // Triangle
                          {kAnchorModeId, 0.0},            // LOCK
                          {kAnchorLevelId, 1.0},           // level 1
                          {kAnchorFoldFeedId, 0.0},        // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.3},     // body curvature 0.3
                          {kCoreShiftId, 0.35},            // shift -0.3
                          {kCoreExcitationDriveId, 0.5}},  // drive 0.5
            // Designed-library pass (2026-10-26): B crushes it wide.
            .endpointB = {{kCoreDepthId, 0.45},         // depth 0.45
                          {kCoreBodyId, 0.6},           // body 0.6
                          {kCoreEdgeId, 0.55},          // edge 0.55
                          {kCoreExcitationMixId, 0.4},  // excitation mix 0.4
                          {kFoldAmountId, 0.55},        // Harmonic Fold 55
                          {kFoldShapeId, 0.8},          // SHAPE 0.8
                          {kCharacterId, 0.8},          // Crush
                          {kCharacterAmountId, 0.5},    // Amount 0.5
                          {kCharacterToneId, 0.6},      // Tone 0.6
                          {kAmpAttackId, 0.339794},     // 5 ms
                          {kAmpDecayId, 0.695424},      // 300 ms
                          {kAmpSustainId, 1.0},         // sustain 1
                          {kAmpReleaseId, 0.708814},    // 350 ms
                          {kFieldImageModeId, 1.0},     // WIDE
                          {kFieldSpreadId, 0.7},        // spread 0.7
                          {kMassId, 0.75},              // Mass 75
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 1.0},        // level 1
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.3},  // body curvature 0.3
                          {kCoreShiftId, 0.35}},        // shift -0.3
            .controls = {{kNoteMemoryId, 1.0},     // Note Memory OscillatorContinuous
                         {kNoteRetriggerId, 0.0},  // Retrigger Reset
                         {kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Running",
            .nameB = "Running Crush",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 47: AT-47 Weight (plan 7 row 47) ----
        // T027 FR-061 level: sustain 1 -> 0.6 (-6.22 -> -10.66 dBFS).
        // The probe's AT-47 base A_s (3.21 / 3.21 dB against 3 dB): the core is kept; the field (after the Sum
        // tap), attack and release are this preset's own.
        ProfundumPresetDef{
            .name = "Weight Lever",
            .category = "Sub",
            .description = "Macros Weight: a clean sine sub with a soft 20 ms attack under a WIDE image, the Weight "
                           "macro stored at 0.6. Morph to B bites: a body, a BITE-shaped fold and Bite over a Triangle "
                           "Anchor.",
            .primary = C::AT47,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 1.0},          // depth 1
                          {kCoreBodyId, 0.0},           // body 0
                          {kCoreEdgeId, 0.0},           // edge 0
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.0},    // Amount 0
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.460206},     // 20 ms
                          {kAmpDecayId, 0.635218},      // 150 ms
                          {kAmpSustainId, 0.6},         // sustain 0.6
                          {kAmpReleaseId, 0.615836},    // 120 ms
                          {kFieldImageModeId, 1.0},     // WIDE
                          {kFieldSpreadId, 0.4},        // spread 0.4
                          {kMassId, 0.5},               // Mass 50
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 1.0},        // level 1
                          {kAnchorFoldFeedId, 0.0}},    // Anchor Fold Feed 0
            // Designed-library pass (2026-10-26): A is unchanged (AT-47 margin 0.23 dB); B bites: a body, a BITE-shaped
            // fold and Bite 0.4 over a Triangle Anchor.
            .endpointB = {{kCoreDepthId, 0.55},         // depth 0.55
                          {kCoreBodyId, 0.5},           // body 0.5
                          {kCoreEdgeId, 0.4},           // edge 0.4
                          {kCoreExcitationMixId, 0.3},  // excitation mix 0.3
                          {kFoldAmountId, 0.4},         // Harmonic Fold 40
                          {kFoldShapeId, 0.7},          // SHAPE 0.7
                          {kCharacterId, 0.6},          // Bite
                          {kCharacterAmountId, 0.4},    // Amount 0.4
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.460206},     // 20 ms
                          {kAmpDecayId, 0.635218},      // 150 ms
                          {kAmpSustainId, 0.8},         // sustain 0.8
                          {kAmpReleaseId, 0.615836},    // 120 ms
                          {kFieldImageModeId, 0.5},     // SPREAD
                          {kFieldSpreadId, 0.6},        // spread 0.6
                          {kMassId, 0.7},               // Mass 70
                          {kAnchorWaveformId, 0.25},    // Triangle
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.85},       // level 0.85
                          {kAnchorFoldFeedId, 0.0}},    // Anchor Fold Feed 0
            .controls = {{kMacroWeightId, 0.6},    // Weight 0.6
                         {kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Weighted",
            .nameB = "Weighted Bite",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 48: AT-48 Dark Bright (plan 7 row 48) ----
        // A distinct endpoint B (plan 7). Endpoint A is the probe's AT-48 base B_s.
        ProfundumPresetDef{
            .name = "Dark To Bright",
            .category = "Harmonic",
            .description = "Macros Dark Bright: a heavy harmonic body with the Dark Bright macro at 0.5; endpoint B is "
                           "a darker, rounder, wider twin an octave deep.",
            .primary = C::AT48,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 0.5},                // depth 0.5
                          {kCoreBodyId, 0.3},                 // body 0.3
                          {kCoreEdgeId, 1.0},                 // edge 1
                          {kCoreShiftId, 0.0},                // shift -1
                          {kCoreExcitationMixId, 1.0},        // excitation mix 1
                          {kCoreExcitationDriveId, 1.0},      // drive 1
                          {kCoreExcitationAsymmetryId, 0.5},  // asymmetry 0.5
                          {kFoldAmountId, 0.8},               // Harmonic Fold 80
                          {kFoldShapeId, 0.5},                // SHAPE 0.5
                          {kCharacterId, 0.2},                // Fold
                          {kCharacterAmountId, 0.5},          // Amount 0.5
                          {kCharacterToneId, 0.8},            // Tone 0.8
                          {kAmpAttackId, 0.2},                // 1 ms
                          {kAmpDecayId, 0.835218},            // 1500 ms
                          {kAmpSustainId, 1.0},               // sustain 1
                          {kAmpReleaseId, 0.780618},          // 800 ms
                          {kFieldImageModeId, 0.0},           // SOLID
                          {kFieldSpreadId, 0.0},              // spread 0
                          {kMassId, 0.5},                     // Mass 50
                          {kAnchorWaveformId, 0.0},           // Sine
                          {kAnchorModeId, 0.0},               // LOCK
                          {kAnchorLevelId, 0.5},              // level 0.5
                          {kAnchorFoldFeedId, 0.0}},          // Anchor Fold Feed 0
            // Designed-library pass (2026-10-26): A is unchanged (AT-48 margin 0.41, the probe base); B, the darker
            // twin, folds wider over an Octave Down Anchor at sustain 0.7 (it read 3.6 dB over A at sustain 1).
            .endpointB = {{kCoreDepthId, 0.5},                // depth 0.5
                          {kCoreBodyId, 0.5},                 // body 0.5
                          {kCoreEdgeId, 0.4},                 // edge 0.4
                          {kCoreShiftId, 0.25},               // shift -0.5
                          {kCoreExcitationMixId, 1.0},        // excitation mix 1
                          {kCoreExcitationDriveId, 1.0},      // drive 1
                          {kCoreExcitationAsymmetryId, 0.5},  // asymmetry 0.5
                          {kFoldAmountId, 0.5},               // Harmonic Fold 50
                          {kFoldShapeId, 0.2},                // SHAPE 0.2
                          {kCharacterId, 0.0},                // Tube
                          {kCharacterAmountId, 0.4},          // Amount 0.4
                          {kCharacterToneId, 0.4},            // Tone 0.4
                          {kAmpAttackId, 0.2},                // 1 ms
                          {kAmpDecayId, 0.835218},            // 1500 ms
                          {kAmpSustainId, 0.85},              // sustain 0.85
                          {kAmpReleaseId, 0.780618},          // 800 ms
                          {kFieldImageModeId, 1.0},           // WIDE
                          {kFieldSpreadId, 0.8},              // spread 0.8
                          {kMassId, 0.5},                     // Mass 50
                          {kAnchorWaveformId, 1.0},           // OctaveDownSine
                          {kAnchorModeId, 0.0},               // LOCK
                          {kAnchorLevelId, 0.6},              // level 0.6
                          {kAnchorFoldFeedId, 0.0}},          // Anchor Fold Feed 0
            .controls = {{kMacroDarkBrightId, 0.5},  // Dark Bright 0.5
                         {kMorphPositionId, 0.0}},   // p = 0
            .nameA = "Bright Body",
            .nameB = "Dark Body",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 49: AT-49 Tight Loose (plan 7 row 49) ----
        // T027 FR-061 level: sustain 1 -> 0.5 (-6.03 -> -12.05 dBFS; AT-49 margin 0.13 -> 0.40).
        // The probe's AT-49 base A_s (ratio 2.13 against 2): core and envelope kept; the field is this preset's.
        ProfundumPresetDef{
            .name = "Loose Ends",
            .category = "Resonant",
            .description = "Macros Tight Loose: a half-sustain body with the Tight Loose macro at 0.5, loose ends "
                           "around a SPREAD image. Morph to B bites: a body, a fold and Bite over a Triangle Anchor, "
                           "wide.",
            .primary = C::AT49,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 1.0},          // depth 1
                          {kCoreBodyId, 0.0},           // body 0
                          {kCoreEdgeId, 0.0},           // edge 0
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.0},    // Amount 0
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.2},          // 1 ms
                          {kAmpDecayId, 0.635218},      // 150 ms
                          {kAmpSustainId, 0.5},         // sustain 0.5
                          {kAmpReleaseId, 0.539794},    // 50 ms
                          {kFieldImageModeId, 0.5},     // SPREAD
                          {kFieldSpreadId, 0.9},        // spread 0.9
                          {kMassId, 0.5},               // Mass 50
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 1.0},        // level 1
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kFieldGravityId, 0.2}},      // gravity 0.2
            // Designed-library pass (2026-10-26): A is unchanged (AT-49 margin 0.40, core and envelope from the probe);
            // B bites: a body, a fold, Bite 0.45 over a Triangle Anchor, wide.
            .endpointB = {{kCoreDepthId, 0.5},          // depth 0.5
                          {kCoreBodyId, 0.5},           // body 0.5
                          {kCoreEdgeId, 0.5},           // edge 0.5
                          {kCoreExcitationMixId, 0.4},  // excitation mix 0.4
                          {kFoldAmountId, 0.45},        // Harmonic Fold 45
                          {kFoldShapeId, 0.6},          // SHAPE 0.6
                          {kCharacterId, 0.6},          // Bite
                          {kCharacterAmountId, 0.45},   // Amount 0.45
                          {kCharacterToneId, 0.55},     // Tone 0.55
                          {kAmpAttackId, 0.2},          // 1 ms
                          {kAmpDecayId, 0.635218},      // 150 ms
                          {kAmpSustainId, 0.8},         // sustain 0.8
                          {kAmpReleaseId, 0.660206},    // 200 ms
                          {kFieldImageModeId, 1.0},     // WIDE
                          {kFieldSpreadId, 0.8},        // spread 0.8
                          {kMassId, 0.5},               // Mass 50
                          {kAnchorWaveformId, 0.25},    // Triangle
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.8},        // level 0.8
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kFieldGravityId, 0.2}},      // gravity 0.2
            .controls = {{kMacroTightLooseId, 0.5},  // Tight Loose 0.5
                         {kMorphPositionId, 0.0}},   // p = 0
            .nameA = "Loose",
            .nameB = "Bitten Ends",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 50: AT-50 Clean Violent (plan 7 row 50) ----
        ProfundumPresetDef{
            .name = "Clean To Violent",
            .category = "Driven",
            .description = "Macros Clean Violent: a lightly driven core over a Triangle Anchor with the Clean Violent "
                           "macro stored at 0.4, wide, with room to turn toward violent. Morph to B strains it with a "
                           "fold and Fracture.",
            .primary = C::AT50,
            .secondaries = {},
            // Designed-library pass (2026-10-26): AT-50 had 8.3 of margin; A gains Harmonic Fold 15, a Triangle Anchor,
            // a WIDE image and a 2 st drop.
            .endpointA = {{kCoreDepthId, 0.8},            // depth 0.8
                          {kCoreBodyId, 0.2},             // body 0.2
                          {kCoreEdgeId, 0.3},             // edge 0.3
                          {kCoreExcitationMixId, 0.3},    // excitation mix 0.3
                          {kFoldAmountId, 0.15},          // Harmonic Fold 15
                          {kFoldShapeId, 0.0},            // SHAPE 0
                          {kCharacterId, 0.0},            // Tube
                          {kCharacterAmountId, 0.2},      // Amount 0.2
                          {kCharacterToneId, 0.5},        // Tone 0.5
                          {kAmpAttackId, 0.260206},       // 2 ms
                          {kAmpDecayId, 0.695424},        // 300 ms
                          {kAmpSustainId, 0.9},           // sustain 0.9
                          {kAmpReleaseId, 0.679588},      // 250 ms
                          {kFieldImageModeId, 1.0},       // WIDE
                          {kFieldSpreadId, 0.5},          // spread 0.5
                          {kMassId, 0.55},                // Mass 55
                          {kAnchorWaveformId, 0.25},      // Triangle
                          {kAnchorModeId, 0.0},           // LOCK
                          {kAnchorLevelId, 0.8},          // level 0.8
                          {kAnchorFoldFeedId, 0.0},       // Anchor Fold Feed 0
                          {kCoreExcitationDriveId, 0.5},  // drive 0.5
                          {kDropAmountId, 0.041667},      // drop 2 st
                          {kDropTimeId, 0.410077}},       // drop time 30 ms
            // Designed-library pass (2026-10-26): B strains: a body, excitation, a fold and Fracture 0.3, at full
            // sustain over a full Anchor.
            .endpointB = {{kCoreDepthId, 0.7},             // depth 0.7
                          {kCoreBodyId, 0.5},              // body 0.5
                          {kCoreEdgeId, 0.6},              // edge 0.6
                          {kCoreExcitationMixId, 0.6},     // excitation mix 0.6
                          {kFoldAmountId, 0.45},           // Harmonic Fold 45
                          {kFoldShapeId, 0.6},             // SHAPE 0.6
                          {kCharacterId, 1.0},             // Fracture
                          {kCharacterAmountId, 0.3},       // Amount 0.3
                          {kCharacterToneId, 0.55},        // Tone 0.55
                          {kAmpAttackId, 0.260206},        // 2 ms
                          {kAmpDecayId, 0.695424},         // 300 ms
                          {kAmpSustainId, 1.0},            // sustain 1
                          {kAmpReleaseId, 0.679588},       // 250 ms
                          {kFieldImageModeId, 0.5},        // SPREAD
                          {kFieldSpreadId, 0.5},           // spread 0.5
                          {kMassId, 0.7},                  // Mass 70
                          {kAnchorWaveformId, 0.0},        // Sine
                          {kAnchorModeId, 0.0},            // LOCK
                          {kAnchorLevelId, 1.0},           // level 1
                          {kAnchorFoldFeedId, 0.0},        // Anchor Fold Feed 0
                          {kCoreExcitationDriveId, 0.5}},  // drive 0.5
            .controls = {{kMacroCleanViolentId, 0.4},  // Clean Violent 0.4
                         {kMorphPositionId, 0.0}},     // p = 0
            .nameA = "Restrained",
            .nameB = "Fractured Strain",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 51: R-SOLID (plan 7 row 51) ----
        ProfundumPresetDef{
            .name = "Mono Bedrock",
            .category = "Sub",
            .description = "Reference Solid: a SOLID image on both endpoints over a warm Tube 0.4 sub with a light "
                           "fold and an octave-down Anchor, the mono floor that every widened preset is heard against. "
                           "Morph to B fractures it, still mono.",
            .primary = C::RSolid,
            .secondaries = {},
            // Designed-library pass (2026-10-26): R-SOLID had 108 of margin; A adds excitation, a light fold, an Octave
            // Down Anchor and a 3 st drop.
            .endpointA = {{kCoreDepthId, 0.7},           // recipe depth 0.7
                          {kCoreBodyId, 0.35},           // body 0.35
                          {kCoreEdgeId, 0.3},            // edge 0.3
                          {kCharacterId, 0.0},           // Tube
                          {kCharacterAmountId, 0.4},     // Amount 0.4
                          {kCharacterToneId, 0.4},       // Tone 0.4
                          {kFieldImageModeId, 0.0},      // SOLID
                          {kAnchorWaveformId, 1.0},      // OctaveDownSine
                          {kAnchorModeId, 0.0},          // LOCK
                          {kAnchorLevelId, 0.8},         // level 0.8
                          {kMassId, 0.6},                // Mass 60
                          {kAmpAttackId, 0.339794},      // 5 ms
                          {kAmpDecayId, 0.695424},       // 300 ms
                          {kAmpSustainId, 0.9},          // sustain 0.9
                          {kAmpReleaseId, 0.720412},     // 400 ms
                          {kCoreExcitationMixId, 0.25},  // excitation mix 0.25
                          {kFoldAmountId, 0.3},          // Harmonic Fold 30
                          {kFoldShapeId, 0.2},           // SHAPE 0.2
                          {kDropAmountId, 0.0625},       // drop 3 st
                          {kDropTimeId, 0.444762}},      // drop time 40 ms
            // Designed-library pass (2026-10-26): B fractures it, still mono (SOLID on both: R-SOLID).
            .endpointB = {{kCoreDepthId, 0.45},          // depth 0.45
                          {kCoreBodyId, 0.5},            // body 0.5
                          {kCoreEdgeId, 0.5},            // edge 0.5
                          {kCharacterId, 1.0},           // Fracture
                          {kCharacterAmountId, 0.5},     // Amount 0.5
                          {kCharacterToneId, 0.7},       // Tone 0.7
                          {kFieldImageModeId, 0.0},      // SOLID (R-SOLID: both endpoints)
                          {kAnchorWaveformId, 0.0},      // Sine
                          {kAnchorModeId, 0.0},
                          {kAnchorLevelId, 0.8},
                          {kMassId, 0.75},               // Mass 75
                          {kAmpAttackId, 0.339794},
                          {kAmpDecayId, 0.695424},
                          {kAmpSustainId, 0.9},
                          {kAmpReleaseId, 0.76902},      // 700 ms
                          {kFoldAmountId, 0.6},          // Harmonic Fold 60
                          {kFoldShapeId, 0.85},          // SHAPE 0.85
                          {kCoreExcitationMixId, 0.5}},  // excitation mix 0.5
            .controls = {{kMorphPositionId, 0.0}},
            .nameA = "Solid Ground",
            .nameB = "Solid Fracture",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 52: R-LOCK (plan 7 row 52; secondary AT-14) ----
        // T027 FR-061 level: sustain 1 -> 0.6 (-6.06 -> -10.49 dBFS; R-LOCK margin unchanged).
        // Endpoint A keeps the Phase 9 AT-11 base core (showcasePair().first + sustain 1: sine recipe, excitation
        // mix 0, attack 1 ms, decay 150 ms, Anchor level 1): the probe read that reverse arm at 2.09 / 2.04 dB
        // against the 2 dB bar (evidence/probe-table.csv), so the core is not thickened here.
        ProfundumPresetDef{
            .name = "Locked Floor",
            .category = "Sub",
            .description = "Reference Lock: the Anchor held in LOCK under a Triangle waveform with a SPREAD image, a "
                           "steady floor with no punch. Morph to B drives it: a body, fold and Tube over a wide image.",
            .primary = C::RLock,
            .secondaries = {C::AT14},
            .endpointA = {{kCoreDepthId, 1.0},          // sine recipe: depth 1
                          {kCoreBodyId, 0.0},           // body 0
                          {kCoreEdgeId, 0.0},           // edge 0
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFieldImageModeId, 0.5},     // SPREAD
                          {kFieldSpreadId, 0.6},        // spread 0.6
                          {kAnchorWaveformId, 0.25},    // Triangle
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 1.0},        // level 1
                          {kAmpAttackId, 0.2},          // 1 ms
                          {kAmpDecayId, 0.635218},      // 150 ms
                          {kAmpSustainId, 0.6},         // sustain 0.6
                          {kAmpReleaseId, 0.695424}},   // 300 ms
            // Designed-library pass (2026-10-26): A is unchanged (R-LOCK margin 0.04 dB); B drives it: a body, fold and
            // Tube 0.45 over a WIDE image (Triangle and LOCK on both: AT-14, R-LOCK).
            .endpointB = {{kCoreDepthId, 0.5},           // depth 0.5
                          {kCoreBodyId, 0.45},           // body 0.45
                          {kCoreEdgeId, 0.55},           // edge 0.55
                          {kCoreExcitationMixId, 0.35},  // excitation mix 0.35
                          {kFieldImageModeId, 1.0},      // WIDE
                          {kFieldSpreadId, 1.0},         // spread 1
                          {kAnchorWaveformId, 0.25},     // Triangle (AT-14 claim: both endpoints)
                          {kAnchorModeId, 0.0},          // LOCK (R-LOCK: both endpoints)
                          {kAnchorLevelId, 1.0},
                          {kAmpAttackId, 0.2},
                          {kAmpDecayId, 0.635218},
                          {kAmpSustainId, 0.7},          // sustain 0.7
                          {kAmpReleaseId, 0.695424},
                          {kFoldAmountId, 0.5},          // Harmonic Fold 50
                          {kFoldShapeId, 0.5},           // SHAPE 0.5
                          {kCharacterId, 0.0},           // Tube
                          {kCharacterAmountId, 0.6},     // Amount 0.6
                          {kCharacterToneId, 0.5},       // Tone 0.5
                          {kMassId, 0.7}},               // Mass 70
            .controls = {{kMorphPositionId, 0.0}},      // p = 0 (P-3: the R-LOCK arm reads A(P))
            .nameA = "Triangle Lock",
            .nameB = "Triangle Driven",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 53: R-SINE (plan 7 row 53; the pilot preset, plan 5.8) ----
        // Endpoint A keeps the Phase 9 AT-14 base core (sine recipe, Anchor level 1): the D_cx reverse arm reads the
        // Sum tap's harmonics n >= 2 against h1, and a harmonic body on the active endpoint raises that baseline
        // (probe: 5.61 / 6.13 dB against the 3 dB bar). The harmonic body is endpoint B, reached by Morph.
        ProfundumPresetDef{
            .name = "Bedrock Sine",
            .category = "Sub",
            .description = "Reference Sine: a pure Sine Anchor on both endpoints under a WIDE image with a 5 semitone "
                           "drop; endpoint B adds a heavy, crushed harmonic body to morph into.",
            .primary = C::RSine,
            .secondaries = {},
            // Designed-library pass (2026-10-26): R-SINE reads D_cx on a sine core (margin 1.68 dB); A gains a 5 st
            // drop and a harder transient.
            .endpointA = {{kCoreDepthId, 1.0},                   // sine recipe: depth 1
                          {kCoreBodyId, 0.0},                    // body 0
                          {kCoreEdgeId, 0.0},                    // edge 0
                          {kCoreExcitationMixId, 0.0},           // excitation mix 0
                          {kFieldImageModeId, 1.0},              // WIDE
                          {kFieldSpreadId, 1.0},                 // spread 1
                          {kAnchorWaveformId, 0.0},              // Sine
                          {kAnchorModeId, 0.0},                  // LOCK
                          {kAnchorLevelId, 1.0},                 // level 1
                          {kAmpAttackId, 0.380618},              // 8 ms
                          {kAmpDecayId, 0.720412},               // 400 ms
                          {kAmpSustainId, 0.85},                 // sustain 0.85
                          {kAmpReleaseId, 0.739794},             // 500 ms
                          {kDropAmountId, 0.104167},             // drop 5 st
                          {kDropTimeId, 0.512234},               // drop time 70 ms
                          {kDynamicsTransientAttackId, 0.625}},  // transient attack +3 dB
            // Designed-library pass (2026-10-26): B's heavy body gains a fold and Crush 0.3.
            .endpointB = {{kCoreDepthId, 0.5},            // kHeavy recipe: depth 0.5
                          {kCoreBodyId, 0.3},             // body 0.3
                          {kCoreEdgeId, 0.6},             // edge 0.6
                          {kCoreShiftId, 0.0},            // shift -1
                          {kCoreExcitationMixId, 1.0},    // excitation mix 1
                          {kCoreExcitationDriveId, 0.6},  // drive 0.6
                          {kFieldImageModeId, 1.0},       // WIDE
                          {kFieldSpreadId, 1.0},          // spread 1
                          {kAnchorWaveformId, 0.0},       // Sine (R-SINE: both endpoints)
                          {kAnchorModeId, 0.0},           // LOCK
                          {kAnchorLevelId, 0.7},          // level 0.7
                          {kAmpAttackId, 0.380618},
                          {kAmpDecayId, 0.720412},
                          {kAmpSustainId, 0.85},
                          {kAmpReleaseId, 0.739794},
                          {kFoldAmountId, 0.3},           // Harmonic Fold 30
                          {kFoldShapeId, 0.4},            // SHAPE 0.4
                          {kCharacterId, 0.8},            // Crush
                          {kCharacterAmountId, 0.3},      // Amount 0.3
                          {kCharacterToneId, 0.5}},       // Tone 0.5
            .controls = {{kMorphPositionId, 0.0}},
            .nameA = "Pure Sine",
            .nameB = "Heavy Body",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 54: AT-31 in Poly (Phase 13b, FR-066; endpoint A from Spread Choir with the TuneP_Poly edits) ----
        // Phase 14 T023: converted to M-ENV (AT-31 stays with Spread Choir).
        ProfundumPresetDef{
            .name = "Poly Spread",
            .category = "Harmonic",
            .description = "Morph Mod Envelope: every chord's envelope carries the morph from A to B and holds it "
                           "there (Amount 1, an instant attack, Sustain 1). A is a hollow, Tube-coloured, folded body "
                           "spread wide in SPREAD over a Rounded Anchor, played as chords in Poly; B spreads darker "
                           "Tube chords.",
            .primary = C::MEnv,
            .secondaries = {},
            // Designed-library pass (2026-10-26): AT-31 had 112 of margin; the chord spread gains excitation, a fold,
            // Tube 0.35 and a Rounded Anchor. SHAPE stays 0: at 0.5 a load under a held MONO note read the FR-048 step
            // ratio at 4.5 - 5.5 (bar 2), with or without the fold; at 0 it reads 1.34 (Crush 0.35 is gone too).
            .endpointA = {{kCoreDepthId, 0.4},
                          {kCoreBodyId, 0.5},
                          {kCoreEdgeId, 0.6},
                          {kCoreExcitationMixId, 0.25},  // excitation mix 0.25
                          {kFoldAmountId, 0.3},          // Harmonic Fold 30
                          {kFoldShapeId, 0.0},           // SHAPE 0
                          {kCharacterId, 0.0},           // Tube
                          {kCharacterAmountId, 0.35},    // Amount 0.35
                          {kCharacterToneId, 0.6},       // Tone 0.6
                          {kAmpAttackId, 0.3},
                          {kAmpDecayId, 0.76902},
                          {kAmpSustainId, 0.8},
                          {kAmpReleaseId, 0.76902},
                          {kFieldImageModeId, 0.5},
                          {kFieldSpreadId, 0.9},
                          {kMassId, 0.5},
                          {kAnchorWaveformId, 0.5},      // RoundedSine
                          {kAnchorModeId, 0.0},
                          {kAnchorLevelId, 0.55},
                          {kAnchorFoldFeedId, 0.0},
                          {kCoreBodyCurvatureId, 0.75},
                          {kCoreShiftId, 0.35},
                          {kFieldGravityId, 0.4}},
            // Designed-library pass (2026-10-26): B spreads dark Tube chords with a softer attack (SPREAD on both:
            // AT-31).
            .endpointB = {{kCoreDepthId, 0.7},           // depth 0.7
                          {kCoreBodyId, 0.2},            // body 0.2
                          {kCoreEdgeId, 0.3},            // edge 0.3
                          {kCoreExcitationMixId, 0.0},   // excitation mix 0
                          {kFoldAmountId, 0.0},          // Harmonic Fold 0
                          {kFoldShapeId, 0.0},           // SHAPE 0
                          {kCharacterId, 0.0},           // Tube
                          {kCharacterAmountId, 0.6},     // Amount 0.6
                          {kCharacterToneId, 0.3},       // Tone 0.3
                          {kAmpAttackId, 0.495424},      // 30 ms
                          {kAmpDecayId, 0.76902},
                          {kAmpSustainId, 0.5},          // sustain 0.5
                          {kAmpReleaseId, 0.815836},     // 1.2 s
                          {kFieldImageModeId, 0.5},
                          {kFieldSpreadId, 0.9},
                          {kMassId, 0.6},                // Mass 60
                          {kAnchorWaveformId, 0.0},      // Sine
                          {kAnchorModeId, 0.0},
                          {kAnchorLevelId, 0.45},        // level 0.45
                          {kAnchorFoldFeedId, 0.0},
                          {kCoreBodyCurvatureId, 0.75},
                          {kCoreShiftId, 0.3},           // shift -0.4
                          {kFieldGravityId, 0.4}},
            // Phase 14 T023 (FR-046, plan 5.7 step 5): the winning M-ENV candidate of evidence/conversions.md (margin
            // 3.764 dB, rule (ii)), its nine fields through the 1003-1011 laws (morph_mod_params.h).
            .controls = {{kMorphPositionId, 0.0},                       // p = 0
                         {kVoiceModeId, 1.0},                           // Poly (Polyphony 4, P-6)
                         {kMorphModSourceId, 0.25},                     // Source Envelope (1 / 4)
                         {kMorphModAmountId, 1.0},                      // Amount +1
                         {kMorphModAttackId, 0.0},                      // attack 0.1 ms
                         {kMorphModDecayId, 0.75563024976044124},       // decay 600 ms
                         {kMorphModSustainId, 1.0},                     // sustain 1
                         {kMorphModCycleShapeId, 0.0},                  // Cycle Shape Sine (default)
                         {kMorphModCycleRateId, 0.60587150266824885},   // Cycle Rate 1 Hz (default)
                         {kMorphModCycleSyncId, 0.0},                   // Sync Off (default)
                         {kMorphModCycleSyncRateId, 13.0 / 29.0}},      // Sync Rate 1/4 (index 13, default)
            .nameA = "Spread Chord",
            .nameB = "Dark Chord Spread",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 55: AT-24 in Poly (Phase 13b, FR-066; endpoint A from Slow Swell with the TuneP_Poly edits) ----
        // Phase 14 T023: converted to M-ENV (AT-24 stays with Slow Swell).
        ProfundumPresetDef{
            .name = "Poly Swell",
            .category = "Resonant",
            .description = "Morph Mod Envelope: every chord's envelope carries the morph from A to B and holds it "
                           "there (Amount 1, an instant attack, Sustain 1). A swells a folded, shifted body under "
                           "every held chord in Poly, wide over a Rounded Anchor; B swells warmer and slower with "
                           "Tube.",
            .primary = C::MEnv,
            .secondaries = {},
            // Designed-library pass (2026-10-26): AT-24 had 17.2 of margin; the swelling chords gain excitation, a
            // fold, a Rounded Anchor and a WIDE image. SHAPE stays 0: at 0.4 a load under a held MONO note read the
            // FR-048 step ratio at 5.5 - 7.3 (bar 2); at 0 it reads 1.44.
            .endpointA = {{kCoreDepthId, 0.5},
                          {kCoreBodyId, 0.35},
                          {kCoreEdgeId, 0.5},
                          {kCoreExcitationMixId, 0.15},  // excitation mix 0.15
                          {kFoldAmountId, 0.25},         // Harmonic Fold 25
                          {kFoldShapeId, 0.0},           // SHAPE 0
                          {kCharacterId, 0.2},
                          {kCharacterAmountId, 0.3},
                          {kCharacterToneId, 0.5},
                          {kAmpAttackId, 0.739794},
                          {kAmpDecayId, 0.780618},
                          {kAmpSustainId, 0.8},
                          {kAmpReleaseId, 0.790849},
                          {kFieldImageModeId, 1.0},      // WIDE
                          {kFieldSpreadId, 0.7},         // spread 0.7
                          {kMassId, 0.5},
                          {kAnchorWaveformId, 0.5},      // RoundedSine
                          {kAnchorModeId, 0.0},
                          {kAnchorLevelId, 0.6},
                          {kAnchorFoldFeedId, 0.0},
                          {kCoreBodyCurvatureId, 0.7},
                          {kCoreBodyEmphasisId, 0.7},
                          {kCoreShiftId, 0.7}},
            // Designed-library pass (2026-10-26): B swells darker and slower: a fuller body under Tube 0.8, Shift -1,
            // edge 0.1, a 1 s attack (library rule 1 caps it there) over a Triangle Anchor (Tube 0.5 at Shift -0.3 read
            // A-B d 9.99 against 10).
            .endpointB = {{kCoreDepthId, 0.3},          // depth 0.3
                          {kCoreBodyId, 0.6},           // body 0.6
                          {kCoreEdgeId, 0.1},           // edge 0.1
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.45},        // Harmonic Fold 45
                          {kFoldShapeId, 0.15},         // SHAPE 0.15
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.8},    // Amount 0.8
                          {kCharacterToneId, 0.4},      // Tone 0.4
                          {kAmpAttackId, 0.8},          // 1 s
                          {kAmpDecayId, 0.780618},
                          {kAmpSustainId, 0.8},
                          {kAmpReleaseId, 0.835218},    // 1.5 s
                          {kFieldImageModeId, 0.5},     // SPREAD
                          {kFieldSpreadId, 0.5},        // spread 0.5
                          {kMassId, 0.6},               // Mass 60
                          {kAnchorWaveformId, 0.25},    // Triangle
                          {kAnchorModeId, 0.0},
                          {kAnchorLevelId, 0.6},
                          {kAnchorFoldFeedId, 0.0},
                          {kCoreBodyCurvatureId, 0.7},
                          {kCoreBodyEmphasisId, 0.7},
                          {kCoreShiftId, 0.0}},         // shift -1
            // Phase 14 T023 (FR-046, plan 5.7 step 5): the winning M-ENV candidate of evidence/conversions.md (margin
            // 2.586 dB, rule (ii)), its nine fields through the 1003-1011 laws (morph_mod_params.h).
            .controls = {{kMorphPositionId, 0.0},                       // p = 0
                         {kVoiceModeId, 1.0},                           // Poly (Polyphony 4, P-6)
                         {kMorphModSourceId, 0.25},                     // Source Envelope (1 / 4)
                         {kMorphModAmountId, 1.0},                      // Amount +1
                         {kMorphModAttackId, 0.0},                      // attack 0.1 ms
                         {kMorphModDecayId, 0.75563024976044124},       // decay 600 ms
                         {kMorphModSustainId, 1.0},                     // sustain 1
                         {kMorphModCycleShapeId, 0.0},                  // Cycle Shape Sine (default)
                         {kMorphModCycleRateId, 0.60587150266824885},   // Cycle Rate 1 Hz (default)
                         {kMorphModCycleSyncId, 0.0},                   // Sync Off (default)
                         {kMorphModCycleSyncRateId, 13.0 / 29.0}},      // Sync Rate 1/4 (index 13, default)
            .nameA = "Swelling Chord",
            .nameB = "Warm Chord Swell",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 56: AT-01 in Poly (Phase 13b, FR-066; endpoint A from Edge Ignition with the TuneP_Poly edits) ----
        // Phase 14 T023: converted to M-ENV (AT-01 stays with Edge Ignition).
        ProfundumPresetDef{
            .name = "Poly Ignition",
            .category = "Driven",
            .description = "Morph Mod Envelope: every chord's envelope carries the morph from A to B and holds it "
                           "there (Amount 1, an instant attack, Sustain 1). A is full edge excitation through Bite on "
                           "a WIDE image over a Rounded Anchor, a driven chord bass in Poly; B warms the chords with "
                           "Tube, an octave deep.",
            .primary = C::MEnv,
            .secondaries = {},
            // Designed-library pass (2026-10-26): AT-01 had 7.01 of margin; the chords gain a Rounded Anchor.
            .endpointA = {{kCoreDepthId, 1.0},
                          {kCoreBodyId, 0.5},
                          {kCoreEdgeId, 0.6},
                          {kCoreExcitationMixId, 1.0},
                          {kFoldAmountId, 0.2},
                          {kFoldShapeId, 0.0},
                          {kCharacterId, 0.6},
                          {kCharacterAmountId, 0.6},
                          {kCharacterToneId, 0.7},
                          {kAmpAttackId, 0.260206},
                          {kAmpDecayId, 0.720412},
                          {kAmpSustainId, 0.8},
                          {kAmpReleaseId, 0.695424},
                          {kFieldImageModeId, 1.0},
                          {kFieldSpreadId, 0.8},
                          {kMassId, 0.55},
                          {kAnchorWaveformId, 0.5},       // RoundedSine
                          {kAnchorModeId, 0.0},
                          {kAnchorLevelId, 0.8},
                          {kAnchorFoldFeedId, 0.0},
                          {kCoreExcitationDriveId, 1.0},
                          {kCoreShiftId, 0.75}},
            // Designed-library pass (2026-10-26): B warms the chords: Tube 0.6 over an Octave Down Anchor, no fold,
            // SPREAD.
            .endpointB = {{kCoreDepthId, 0.8},            // depth 0.8
                          {kCoreBodyId, 0.3},             // body 0.3
                          {kCoreEdgeId, 0.3},             // edge 0.3
                          {kCoreExcitationMixId, 0.3},    // excitation mix 0.3
                          {kFoldAmountId, 0.0},           // Harmonic Fold 0
                          {kFoldShapeId, 0.0},            // SHAPE 0
                          {kCharacterId, 0.0},            // Tube
                          {kCharacterAmountId, 0.6},      // Amount 0.6
                          {kCharacterToneId, 0.35},       // Tone 0.35
                          {kAmpAttackId, 0.260206},
                          {kAmpDecayId, 0.720412},
                          {kAmpSustainId, 0.8},
                          {kAmpReleaseId, 0.695424},
                          {kFieldImageModeId, 0.5},       // SPREAD
                          {kFieldSpreadId, 0.5},          // spread 0.5
                          {kMassId, 0.6},                 // Mass 60
                          {kAnchorWaveformId, 1.0},       // OctaveDownSine
                          {kAnchorModeId, 0.0},
                          {kAnchorLevelId, 0.8},
                          {kAnchorFoldFeedId, 0.0},
                          {kCoreExcitationDriveId, 1.0},
                          {kCoreShiftId, 0.5}},           // shift 0
            // Phase 14 T023 (FR-046, plan 5.7 step 5): the winning M-ENV candidate of evidence/conversions.md (margin
            // 31.839 dB, rule (ii)), its nine fields through the 1003-1011 laws (morph_mod_params.h).
            .controls = {{kMorphPositionId, 0.0},                       // p = 0
                         {kVoiceModeId, 1.0},                           // Poly (Polyphony 4, P-6)
                         {kMorphModSourceId, 0.25},                     // Source Envelope (1 / 4)
                         {kMorphModAmountId, 1.0},                      // Amount +1
                         {kMorphModAttackId, 0.0},                      // attack 0.1 ms
                         {kMorphModDecayId, 0.75563024976044124},       // decay 600 ms
                         {kMorphModSustainId, 1.0},                     // sustain 1
                         {kMorphModCycleShapeId, 0.0},                  // Cycle Shape Sine (default)
                         {kMorphModCycleRateId, 0.60587150266824885},   // Cycle Rate 1 Hz (default)
                         {kMorphModCycleSyncId, 0.0},                   // Sync Off (default)
                         {kMorphModCycleSyncRateId, 13.0 / 29.0}},      // Sync Rate 1/4 (index 13, default)
            .nameA = "Ignited Chord",
            .nameB = "Warm Ignited Chord",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 57: AT-06 in Poly (Phase 13b, FR-066; endpoint A from Rectified Floor with the TuneP_Poly edits) ----
        ProfundumPresetDef{
            .name = "Poly Rectifier",
            .category = "Driven",
            .description = "Character Rectify: full Rectify on a brighter body over a Triangle Anchor, wide, rectified "
                           "chords in Poly. Morph to B darkens them into mono chords under a lighter Rectify.",
            .primary = C::AT06,
            .secondaries = {},
            // Designed-library pass (2026-10-26): AT-06 had 7.45 of margin; the chords gain a Triangle Anchor and a
            // WIDE image (SHAPE 0.3 read AT-06 at -0.21).
            .endpointA = {{kCoreDepthId, 0.6},
                          {kCoreBodyId, 0.4},
                          {kCoreEdgeId, 0.4},
                          {kCoreExcitationMixId, 0.0},
                          {kFoldAmountId, 0.0},
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.4},
                          {kCharacterAmountId, 1.0},
                          {kCharacterToneId, 0.6},
                          {kAmpAttackId, 0.35563},
                          {kAmpDecayId, 0.695424},
                          {kAmpSustainId, 0.9},
                          {kAmpReleaseId, 0.679588},
                          {kFieldImageModeId, 1.0},     // WIDE
                          {kFieldSpreadId, 0.6},        // spread 0.6
                          {kMassId, 0.55},
                          {kAnchorWaveformId, 0.25},    // Triangle
                          {kAnchorModeId, 0.0},
                          {kAnchorLevelId, 0.6},
                          {kAnchorFoldFeedId, 0.0}},
            // Designed-library pass (2026-10-26): B darkens the chords under Rectify 0.6, mono, over an Asymmetric
            // Anchor (Rectify on both: AT-06).
            .endpointB = {{kCoreDepthId, 0.85},         // depth 0.85
                          {kCoreBodyId, 0.2},           // body 0.2
                          {kCoreEdgeId, 0.2},           // edge 0.2
                          {kCoreExcitationMixId, 0.0},
                          {kFoldAmountId, 0.0},
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.4},          // Rectify
                          {kCharacterAmountId, 0.6},    // Amount 0.6
                          {kCharacterToneId, 0.4},      // Tone 0.4
                          {kAmpAttackId, 0.35563},
                          {kAmpDecayId, 0.695424},
                          {kAmpSustainId, 0.5},         // sustain 0.5
                          {kAmpReleaseId, 0.679588},
                          {kFieldImageModeId, 0.0},     // SOLID
                          {kFieldSpreadId, 0.0},        // spread 0
                          {kMassId, 0.65},              // Mass 65
                          {kAnchorWaveformId, 0.75},    // AsymmetricSine
                          {kAnchorModeId, 0.0},
                          {kAnchorLevelId, 0.6},
                          {kAnchorFoldFeedId, 0.0}},
            .controls = {{kMorphPositionId, 0.0},   // p = 0
                         {kVoiceModeId, 1.0}},      // Poly (Polyphony 4, P-6)
            .nameA = "Rectified Chord",
            .nameB = "Dark Rectified Chord",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 58: AT-25 in Poly (Phase 13b, FR-066; endpoint A from Long Tail with the TuneP_Poly edits) ----
        ProfundumPresetDef{
            .name = "Poly Pluck",
            .category = "Plucked",
            .description = "Envelope Amp Release: a short decay to a lower sustain with a long release, folded chords "
                           "over a Triangle Anchor that ring out wide in Poly. Morph to B bites them.",
            .primary = C::AT25,
            .secondaries = {},
            // Designed-library pass (2026-10-26): AT-25 had 38 of margin; the chord pluck gains excitation, a fold, the
            // Fold Character, a Triangle Anchor and a WIDE image. SHAPE stays 0: at 0.5 a load under a held MONO note
            // read the FR-048 step ratio at 2.20 (bar 2); at 0 it reads 1.17.
            .endpointA = {{kCoreDepthId, 0.75},
                          {kCoreBodyId, 0.5},
                          {kCoreEdgeId, 0.5},
                          {kCoreExcitationMixId, 0.25},  // excitation mix 0.25
                          {kFoldAmountId, 0.3},          // Harmonic Fold 30
                          {kFoldShapeId, 0.0},           // SHAPE 0
                          {kCharacterId, 0.2},           // Fold
                          {kCharacterAmountId, 0.35},    // Amount 0.35
                          {kCharacterToneId, 0.55},      // Tone 0.55
                          {kAmpAttackId, 0.4},
                          {kAmpDecayId, 0.65},
                          {kAmpSustainId, 0.6},
                          {kAmpReleaseId, 0.860206},
                          {kFieldImageModeId, 1.0},      // WIDE
                          {kFieldSpreadId, 0.5},         // spread 0.5
                          {kMassId, 0.55},
                          {kAnchorWaveformId, 0.25},     // Triangle
                          {kAnchorModeId, 0.0},
                          {kAnchorLevelId, 0.9},
                          {kAnchorFoldFeedId, 0.0},
                          {kCoreShiftId, 0.65}},
            // Designed-library pass (2026-10-26): B bites: more edge, a BITE fold and Bite 0.45 with a shorter ring.
            .endpointB = {{kCoreDepthId, 0.5},          // depth 0.5
                          {kCoreBodyId, 0.3},           // body 0.3
                          {kCoreEdgeId, 0.7},           // edge 0.7
                          {kCoreExcitationMixId, 0.5},  // excitation mix 0.5
                          {kFoldAmountId, 0.5},         // Harmonic Fold 50
                          {kFoldShapeId, 0.85},         // SHAPE 0.85
                          {kCharacterId, 0.6},          // Bite
                          {kCharacterAmountId, 0.45},   // Amount 0.45
                          {kCharacterToneId, 0.65},     // Tone 0.65
                          {kAmpAttackId, 0.4},
                          {kAmpDecayId, 0.720412},      // 400 ms
                          {kAmpSustainId, 0.5},         // sustain 0.5
                          {kAmpReleaseId, 0.860206},
                          {kFieldImageModeId, 0.5},     // SPREAD
                          {kFieldSpreadId, 0.7},        // spread 0.7
                          {kMassId, 0.6},               // Mass 60
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},
                          {kAnchorLevelId, 0.9},
                          {kAnchorFoldFeedId, 0.0},
                          {kCoreShiftId, 0.65}},
            .controls = {{kMorphPositionId, 0.0},   // p = 0
                         {kVoiceModeId, 1.0}},      // Poly (Polyphony 4, P-6)
            .nameA = "Plucked Chord",
            .nameB = "Bitten Chord",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 59: M-CYCLE (Phase 14 T023, FR-046): added, no existing preset could carry M-CYCLE ----
        // evidence/conversions.md NEW PRESET NEEDED: the probe's best row, base Fed Fold (definition 18), margin
        // 42.832 dB.
        ProfundumPresetDef{
            .name = "Tidal Fold",
            .category = "Animated",
            .description = "Morph Mod Cycle: a slow, free-running Square Cycle (0.05 Hz, Amount 1) swings the whole sound "
                           "between A and B, holding each for 10 s. A is a dark, round sub with a hollow body and a little Tube "
                           "over a Rounded Anchor; B is the tide coming in: a bright, Rectify-edged fold, spread wide.",
            .primary = C::MCycle,
            .secondaries = {},
            // Endpoints: designed for this preset (R9, user 2026-10-27; the probe measured the column on Fed Fold's); the
            // nine Morph Mod fields are the probe row's candidate through the 1003-1011 laws (morph_mod_params.h).
            .endpointA = {{kCoreDepthId, 0.8},                  // depth 0.8
                          {kCoreBodyId, 0.6},                   // body 0.6
                          {kCoreEdgeId, 0.1},                   // edge 0.1
                          {kCoreShiftId, 0.35},                 // shift -0.3
                          {kCoreExcitationMixId, 0.0},            // excitation mix 0
                          {kFoldAmountId, 0.1},                   // Harmonic Fold 10
                          {kFoldShapeId, 0.2},                  // SHAPE 0.2 (ROUND side)
                          {kCharacterId, 0.0},                    // Tube
                          {kCharacterAmountId, 0.3},            // Amount 0.3
                          {kCharacterToneId, 0.4},              // Tone 0.4
                          {kAmpAttackId, 0.260206},             // 2 ms
                          {kAmpDecayId, 0.708814},              // 350 ms
                          {kAmpSustainId, 0.45},                // sustain 0.45
                          {kAmpReleaseId, 0.695424},            // 300 ms
                          {kFieldImageModeId, 0.5},             // SPREAD
                          {kFieldSpreadId, 0.4},                // spread 0.4
                          {kMassId, 0.6},                       // Mass 60
                          {kAnchorWaveformId, 0.5},             // Rounded Sine
                          {kAnchorModeId, 0.0},                   // LOCK
                          {kAnchorLevelId, 0.8},                // level 0.8
                          {kAnchorFoldFeedId, 0.0}},              // Anchor Fold Feed 0
            .endpointB = {{kCoreDepthId, 0.3},                  // depth 0.3
                          {kCoreBodyId, 0.5},                   // body 0.5
                          {kCoreEdgeId, 0.8},                   // edge 0.8
                          {kCoreShiftId, 0.65},                 // shift +0.3
                          {kCoreExcitationMixId, 0.5},          // excitation mix 0.5
                          {kFoldAmountId, 0.8},                 // Harmonic Fold 80
                          {kFoldShapeId, 0.6},                  // SHAPE 0.6
                          {kCharacterId, 0.4},                  // Rectify
                          {kCharacterAmountId, 0.6},            // Amount 0.6
                          {kCharacterToneId, 0.6},              // Tone 0.6
                          {kAmpAttackId, 0.260206},             // 2 ms
                          {kAmpDecayId, 0.708814},              // 350 ms
                          {kAmpSustainId, 1.0},                 // sustain 1
                          {kAmpReleaseId, 0.695424},            // 300 ms
                          {kFieldImageModeId, 0.5},               // SPREAD
                          {kFieldSpreadId, 0.8},                // spread 0.8
                          {kMassId, 0.8},                       // Mass 80
                          {kAnchorWaveformId, 0.25},            // Triangle
                          {kAnchorModeId, 0.0},                   // LOCK
                          {kAnchorLevelId, 1.0},                // level 1
                          {kAnchorFoldFeedId, 0.0}},              // Anchor Fold Feed 0
            .controls = {{kMorphPositionId, 0.0},                       // p = 0
                         {kMorphModSourceId, 0.5},                      // Source Cycle (2 / 4)
                         {kMorphModAmountId, 1.0},                      // Amount +1
                         {kMorphModAttackId, 0.39999999922342100},      // attack 10 ms (default)
                         {kMorphModDecayId, 0.69542425054972046},       // decay 300 ms (default)
                         {kMorphModSustainId, 0.0},                     // sustain 0 (default)
                         {kMorphModCycleShapeId, 0.6},                  // Cycle Shape Square (3 / 5)
                         {kMorphModCycleRateId, 0.21174300729694398},   // Cycle Rate 0.05 Hz
                         {kMorphModCycleSyncId, 0.0},                   // Sync Off (default)
                         {kMorphModCycleSyncRateId, 13.0 / 29.0}},      // Sync Rate 1/4 (index 13, default)
            .nameA = "Low Tide",
            .nameB = "High Tide",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
            .morphModProbeBase = "Fed Fold",
        },
        // ---- 60: M-VEL (Phase 14 T023, FR-046): added, no existing preset could carry M-VEL ----
        // evidence/conversions.md NEW PRESET NEEDED: the probe's best row, base Fed Fold (definition 18), margin
        // 41.765 dB.
        ProfundumPresetDef{
            .name = "Struck Fold",
            .category = "Driven",
            .description = "Morph Mod Velocity: how hard each note is struck sets how far it morphs from A toward B (Amount 1; "
                           "velocity 127 reaches B). A is a warm, lightly folded Tube bass over a lopsided Anchor; struck hard "
                           "it cracks into B, a Fracture-split fold with a 7 semitone snap, wide.",
            .primary = C::MVel,
            .secondaries = {},
            // Endpoints: designed for this preset (R9, user 2026-10-27; the probe measured the column on Fed Fold's); the
            // nine Morph Mod fields are the probe row's candidate through the 1003-1011 laws (morph_mod_params.h).
            .endpointA = {{kCoreDepthId, 0.7},                  // depth 0.7
                          {kCoreBodyId, 0.5},                   // body 0.5
                          {kCoreEdgeId, 0.3},                   // edge 0.3
                          {kCoreExcitationMixId, 0.0},            // excitation mix 0
                          {kFoldAmountId, 0.15},                // Harmonic Fold 15
                          {kFoldShapeId, 0.3},                  // SHAPE 0.3
                          {kCharacterId, 0.0},                    // Tube
                          {kCharacterAmountId, 0.5},            // Amount 0.5
                          {kCharacterToneId, 0.5},              // Tone 0.5
                          {kAmpAttackId, 0.260206},             // 2 ms
                          {kAmpDecayId, 0.708814},              // 350 ms
                          {kAmpSustainId, 0.65},                // sustain 0.65
                          {kAmpReleaseId, 0.695424},            // 300 ms
                          {kFieldImageModeId, 0.5},             // SPREAD
                          {kFieldSpreadId, 0.3},                // spread 0.3
                          {kMassId, 0.55},                      // Mass 55
                          {kAnchorWaveformId, 0.75},            // Asymmetric Sine
                          {kAnchorModeId, 0.0},                   // LOCK
                          {kAnchorLevelId, 0.8},                // level 0.8
                          {kAnchorFoldFeedId, 0.0},               // Anchor Fold Feed 0
                          {kDropAmountId, 0.0}},                  // drop 0 st
            .endpointB = {{kCoreDepthId, 0.4},                  // depth 0.4
                          {kCoreBodyId, 0.4},                   // body 0.4
                          {kCoreEdgeId, 0.7},                   // edge 0.7
                          {kCoreExcitationMixId, 0.6},          // excitation mix 0.6
                          {kFoldAmountId, 0.9},                 // Harmonic Fold 90
                          {kFoldShapeId, 0.8},                  // SHAPE 0.8
                          {kCharacterId, 1.0},                    // Fracture
                          {kCharacterAmountId, 0.7},            // Amount 0.7
                          {kCharacterToneId, 0.6},              // Tone 0.6
                          {kAmpAttackId, 0.260206},             // 2 ms
                          {kAmpDecayId, 0.708814},              // 350 ms
                          {kAmpSustainId, 1.0},                 // sustain 1
                          {kAmpReleaseId, 0.695424},            // 300 ms
                          {kFieldImageModeId, 1.0},               // WIDE
                          {kFieldSpreadId, 0.7},                // spread 0.7
                          {kMassId, 0.7},                       // Mass 70
                          {kAnchorWaveformId, 0.75},            // Asymmetric Sine
                          {kAnchorModeId, 0.0},                   // LOCK
                          {kAnchorLevelId, 1.0},                // level 1
                          {kAnchorFoldFeedId, 0.0},               // Anchor Fold Feed 0
                          {kDropAmountId, 0.145833},            // drop 7 st
                          {kDropTimeId, 0.410077}},             // drop time 30 ms
            .controls = {{kMorphPositionId, 0.0},                       // p = 0
                         {kMorphModSourceId, 0.75},                     // Source Velocity (3 / 4)
                         {kMorphModAmountId, 1.0},                      // Amount +1
                         {kMorphModAttackId, 0.39999999922342100},      // attack 10 ms (default)
                         {kMorphModDecayId, 0.69542425054972046},       // decay 300 ms (default)
                         {kMorphModSustainId, 0.0},                     // sustain 0 (default)
                         {kMorphModCycleShapeId, 0.0},                  // Cycle Shape Sine (default)
                         {kMorphModCycleRateId, 0.60587150266824885},   // Cycle Rate 1 Hz (default)
                         {kMorphModCycleSyncId, 0.0},                   // Sync Off (default)
                         {kMorphModCycleSyncRateId, 13.0 / 29.0}},      // Sync Rate 1/4 (index 13, default)
            .nameA = "Soft Strike",
            .nameB = "Hard Strike",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
            .morphModProbeBase = "Fed Fold",
        },
    };
    return kPresets;
}

// ==============================================================================
// Info chunk
// ==============================================================================

/// The bytes PresetManager::savePreset writes, in the shape of
/// tools/vorago_preset_defs.h:1731-1755, with PlugInName "Profundum",
/// PlugInCategory "Synth" and MusicalCategory == MusicalInstrument == category.
/// Exactly seven attributes: the Comment line is always written (SC-002).
/// Explicit `\n`, pure ASCII.
[[nodiscard]] inline std::string buildProfundumInfoXml(std::string_view name, std::string_view category,
                                                       std::string_view description) {
    std::string xml;
    xml += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml += "<MetaInfo>\n";
    xml += "  <Attr id=\"MediaType\" value=\"VstPreset\" type=\"string\"/>\n";
    xml += "  <Attr id=\"PlugInName\" value=\"Profundum\" type=\"string\"/>\n";
    xml += "  <Attr id=\"PlugInCategory\" value=\"Synth\" type=\"string\"/>\n";
    xml += "  <Attr id=\"Name\" value=\"";
    xml += name;
    xml += "\" type=\"string\"/>\n";
    xml += "  <Attr id=\"MusicalCategory\" value=\"";
    xml += category;
    xml += "\" type=\"string\"/>\n";
    xml += "  <Attr id=\"MusicalInstrument\" value=\"";
    xml += category;
    xml += "\" type=\"string\"/>\n";
    xml += "  <Attr id=\"Comment\" value=\"";
    xml += description;
    xml += "\" type=\"string\"/>\n";
    xml += "</MetaInfo>\n";
    return xml;
}

}  // namespace Profundum::PresetDefs
