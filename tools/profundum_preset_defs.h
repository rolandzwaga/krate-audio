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
    Reference
};

/// FR-010: the 50 AT columns in AT order, then the three reference columns.
enum class Capability : std::uint8_t {
    AT01, AT02, AT03, AT04, AT05, AT06, AT07, AT08, AT09, AT10, AT11, AT12, AT13, AT14, AT15, AT16, AT17, AT18, AT19,
    AT20, AT21, AT22, AT23, AT24, AT25, AT26, AT27, AT28, AT29, AT30, AT31, AT32, AT33, AT34, AT35, AT36, AT37, AT38,
    AT39, AT40, AT41, AT42, AT43, AT44, AT45, AT46, AT47, AT48, AT49, AT50, RSolid, RLock, RSine, Count
};
inline constexpr std::size_t kNumCapabilities = static_cast<std::size_t>(Capability::Count);
static_assert(kNumCapabilities == 53, "C-2.1: 50 AT columns + R-SOLID, R-LOCK, R-SINE");

struct CapabilityInfo {
    Capability column;
    CapabilityGroup group;
    std::uint8_t atId;       // 1..50; 0 for a reference column
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
    }};
    return kTable;
}

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
    std::string_view nameA;                    // <= 31 UTF-8 bytes, already sanitised, never "A"
    std::string_view nameB;                    // never "B"
    std::uint32_t seed = kFactoryDefaultSeed;  // C-4 step 6
    bool seedDependent = false;                // C-7; verified in both directions by FR-044
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
            .description = "Core Excitation Mix: a sine recipe pushed to edge 1 with the excitation mix and drive at full; "
                           "Harmonic Fold 50 adds folded overtones.",
            .primary = C::AT01,
            .secondaries = {C::AT03},
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
                          {kFieldSpreadId, 0.4},          // spread 0.4
                          {kMassId, 0.55},                // Mass 55
                          {kAnchorWaveformId, 0.0},       // Sine
                          {kAnchorModeId, 0.0},           // LOCK
                          {kAnchorLevelId, 0.8},          // level 0.8
                          {kAnchorFoldFeedId, 0.0},       // Anchor Fold Feed 0
                          {kCoreExcitationDriveId, 1.0}}, // drive 1
            .endpointB = {{kCoreDepthId, 1.0},                // depth 1
                          {kCoreBodyId, 0.0},                 // body 0
                          {kCoreEdgeId, 0.8},                 // edge 0.8
                          {kCoreExcitationMixId, 1.0},        // excitation mix 1
                          {kFoldAmountId, 0.5},               // Harmonic Fold 50
                          {kFoldShapeId, 0.0},                // SHAPE 0
                          {kCharacterId, 0.0},                // Tube
                          {kCharacterAmountId, 0.0},          // Amount 0
                          {kCharacterToneId, 0.5},            // Tone 0.5
                          {kAmpAttackId, 0.260206},           // 2 ms
                          {kAmpDecayId, 0.720412},            // 400 ms
                          {kAmpSustainId, 0.8},               // sustain 0.8
                          {kAmpReleaseId, 0.695424},          // 300 ms
                          {kFieldImageModeId, 0.5},           // SPREAD
                          {kFieldSpreadId, 0.4},              // spread 0.4
                          {kMassId, 0.55},                    // Mass 55
                          {kAnchorWaveformId, 0.0},           // Sine
                          {kAnchorModeId, 0.0},               // LOCK
                          {kAnchorLevelId, 0.8},              // level 0.8
                          {kAnchorFoldFeedId, 0.0},           // Anchor Fold Feed 0
                          {kCoreExcitationDriveId, 1.0},      // drive 1
                          {kCoreExcitationAsymmetryId, 0.4}}, // asymmetry 0.4
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Ignition",
            .nameB = "Asymmetric Burn",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 2: AT-02 SHAPE (plan 7 row 2) ----
        // T027 FR-061 level: sustain 0.7 -> 1, depth 0 -> 0.3 (-16.64 -> -13.31 dBFS).
        ProfundumPresetDef{
            .name = "Biting Reed",
            .category = "Harmonic",
            .description = "Core Shape: a saw-like recipe at edge 0.5 with SHAPE at 1, the BITE end; the reed snarls on every "
                           "note.",
            .primary = C::AT02,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 0.3},          // depth 0.3
                          {kCoreBodyId, 0.0},           // body 0
                          {kCoreEdgeId, 0.5},           // edge 0.5
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 1.0},          // SHAPE 1
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.0},    // Amount 0
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.295424},     // 3 ms
                          {kAmpDecayId, 0.695424},      // 300 ms
                          {kAmpSustainId, 1.0},         // sustain 1
                          {kAmpReleaseId, 0.679588},    // 250 ms
                          {kFieldImageModeId, 0.5},     // SPREAD
                          {kFieldSpreadId, 0.5},        // spread 0.5
                          {kMassId, 0.5},               // Mass 50
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.6},        // level 0.6
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kCoreShiftId, 0.5}},         // shift 0
            .endpointB = {{kCoreDepthId, 0.3},          // depth 0.3
                          {kCoreBodyId, 0.0},           // body 0
                          {kCoreEdgeId, 0.6},           // edge 0.6
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.7},          // SHAPE 0.7
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.0},    // Amount 0
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.295424},     // 3 ms
                          {kAmpDecayId, 0.695424},      // 300 ms
                          {kAmpSustainId, 1.0},         // sustain 1
                          {kAmpReleaseId, 0.679588},    // 250 ms
                          {kFieldImageModeId, 0.5},     // SPREAD
                          {kFieldSpreadId, 0.5},        // spread 0.5
                          {kMassId, 0.5},               // Mass 50
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.6},        // level 0.6
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kCoreShiftId, 0.5}},         // shift 0
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Full Bite",
            .nameB = "Softer Reed",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 3: AT-03 Fold (plan 7 row 3) ----
        ProfundumPresetDef{
            .name = "Folded Mass",
            .category = "Driven",
            .description = "Core Harmonic Fold: Harmonic Fold at 100 over a deep core, with the Anchor at 0.6 holding the floor.",
            .primary = C::AT03,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 0.8},          // depth 0.8
                          {kCoreBodyId, 0.2},           // body 0.2
                          {kCoreEdgeId, 0.2},           // edge 0.2
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 1.0},         // Harmonic Fold 100
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.0},    // Amount 0
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.260206},     // 2 ms
                          {kAmpDecayId, 0.739794},      // 500 ms
                          {kAmpSustainId, 0.8},         // sustain 0.8
                          {kAmpReleaseId, 0.695424},    // 300 ms
                          {kFieldImageModeId, 0.0},     // SOLID
                          {kFieldSpreadId, 0.0},        // spread 0
                          {kMassId, 0.65},              // Mass 65
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.6},        // level 0.6
                          {kAnchorFoldFeedId, 0.0}},    // Anchor Fold Feed 0
            .endpointB = {{kCoreDepthId, 0.8},          // depth 0.8
                          {kCoreBodyId, 0.4},           // body 0.4
                          {kCoreEdgeId, 0.2},           // edge 0.2
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 1.0},         // Harmonic Fold 100
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.0},    // Amount 0
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.260206},     // 2 ms
                          {kAmpDecayId, 0.739794},      // 500 ms
                          {kAmpSustainId, 0.8},         // sustain 0.8
                          {kAmpReleaseId, 0.695424},    // 300 ms
                          {kFieldImageModeId, 0.0},     // SOLID
                          {kFieldSpreadId, 0.0},        // spread 0
                          {kMassId, 0.75},              // Mass 75
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.6},        // level 0.6
                          {kAnchorFoldFeedId, 0.0}},    // Anchor Fold Feed 0
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Folded Floor",
            .nameB = "Folded Body",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 4: AT-04 Tube (plan 7 row 4) ----
        // T027: the AT-26 secondary is withdrawn: Tube at Amount 1 holds the sub, Mass 0 -> 90 read 0.34 / 0.12 dB.
        ProfundumPresetDef{
            .name = "Valve Pressure",
            .category = "Driven",
            .description = "Character Tube: the Tube algorithm at Amount 1 for full valve pressure, with Mass 90 under an Anchor "
                           "at 0.5.",
            .primary = C::AT04,
            .secondaries = {},
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
                          {kFieldImageModeId, 0.5},     // SPREAD
                          {kFieldSpreadId, 0.3},        // spread 0.3
                          {kMassId, 0.9},               // Mass 90
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.5},        // level 0.5
                          {kAnchorFoldFeedId, 0.0}},    // Anchor Fold Feed 0
            .endpointB = {{kCoreDepthId, 0.7},          // depth 0.7
                          {kCoreBodyId, 0.35},          // body 0.35
                          {kCoreEdgeId, 0.4},           // edge 0.4
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 1.0},    // Amount 1
                          {kCharacterToneId, 0.65},     // Tone 0.65
                          {kAmpAttackId, 0.295424},     // 3 ms
                          {kAmpDecayId, 0.720412},      // 400 ms
                          {kAmpSustainId, 0.85},        // sustain 0.85
                          {kAmpReleaseId, 0.695424},    // 300 ms
                          {kFieldImageModeId, 0.5},     // SPREAD
                          {kFieldSpreadId, 0.3},        // spread 0.3
                          {kMassId, 0.9},               // Mass 90
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.5},        // level 0.5
                          {kAnchorFoldFeedId, 0.0}},    // Anchor Fold Feed 0
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Hot Valve",
            .nameB = "Bright Valve",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 5: AT-05 Wavefold (plan 7 row 5) ----
        ProfundumPresetDef{
            .name = "Folding Wire",
            .category = "Driven",
            .description = "Character Wavefold: the Fold algorithm at Amount 1, a wire-bright wavefold over a mid core.",
            .primary = C::AT05,
            .secondaries = {},
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
                          {kFieldImageModeId, 0.5},     // SPREAD
                          {kFieldSpreadId, 0.6},        // spread 0.6
                          {kMassId, 0.5},               // Mass 50
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.6},        // level 0.6
                          {kAnchorFoldFeedId, 0.0}},    // Anchor Fold Feed 0
            .endpointB = {{kCoreDepthId, 0.6},          // depth 0.6
                          {kCoreBodyId, 0.5},           // body 0.5
                          {kCoreEdgeId, 0.4},           // edge 0.4
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.2},          // Fold
                          {kCharacterAmountId, 1.0},    // Amount 1
                          {kCharacterToneId, 0.45},     // Tone 0.45
                          {kAmpAttackId, 0.2},          // 1 ms
                          {kAmpDecayId, 0.679588},      // 250 ms
                          {kAmpSustainId, 0.7},         // sustain 0.7
                          {kAmpReleaseId, 0.660206},    // 200 ms
                          {kFieldImageModeId, 0.5},     // SPREAD
                          {kFieldSpreadId, 0.6},        // spread 0.6
                          {kMassId, 0.5},               // Mass 50
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.6},        // level 0.6
                          {kAnchorFoldFeedId, 0.0}},    // Anchor Fold Feed 0
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Live Wire",
            .nameB = "Bent Wire",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 6: AT-06 Rectify (plan 7 row 6) ----
        ProfundumPresetDef{
            .name = "Rectified Floor",
            .category = "Driven",
            .description = "Character Rectify: Rectify at Amount 1 with Tone 0.4 over a floor-heavy core, the Anchor at 0.6.",
            .primary = C::AT06,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 0.9},          // depth 0.9
                          {kCoreBodyId, 0.0},           // body 0
                          {kCoreEdgeId, 0.1},           // edge 0.1
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.4},          // Rectify
                          {kCharacterAmountId, 1.0},    // Amount 1
                          {kCharacterToneId, 0.4},      // Tone 0.4
                          {kAmpAttackId, 0.260206},     // 2 ms
                          {kAmpDecayId, 0.695424},      // 300 ms
                          {kAmpSustainId, 0.9},         // sustain 0.9
                          {kAmpReleaseId, 0.679588},    // 250 ms
                          {kFieldImageModeId, 0.0},     // SOLID
                          {kFieldSpreadId, 0.0},        // spread 0
                          {kMassId, 0.55},              // Mass 55
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.6},        // level 0.6
                          {kAnchorFoldFeedId, 0.0}},    // Anchor Fold Feed 0
            .endpointB = {{kCoreDepthId, 0.9},          // depth 0.9
                          {kCoreBodyId, 0.0},           // body 0
                          {kCoreEdgeId, 0.1},           // edge 0.1
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.4},          // Rectify
                          {kCharacterAmountId, 1.0},    // Amount 1
                          {kCharacterToneId, 0.3},      // Tone 0.3
                          {kAmpAttackId, 0.260206},     // 2 ms
                          {kAmpDecayId, 0.695424},      // 300 ms
                          {kAmpSustainId, 0.9},         // sustain 0.9
                          {kAmpReleaseId, 0.679588},    // 250 ms
                          {kFieldImageModeId, 0.0},     // SOLID
                          {kFieldSpreadId, 0.0},        // spread 0
                          {kMassId, 0.65},              // Mass 65
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.6},        // level 0.6
                          {kAnchorFoldFeedId, 0.0}},    // Anchor Fold Feed 0
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Rectified",
            .nameB = "Rectified Dark",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 7: AT-07 Bite (plan 7 row 7; secondary AT-28) ----
        ProfundumPresetDef{
            .name = "Bite Down",
            .category = "Driven",
            .description = "Character Bite: Bite at Amount 1 with the Transient Attack at +12 dB, so every note bites down.",
            .primary = C::AT07,
            .secondaries = {C::AT28},
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
                          {kFieldImageModeId, 0.5},           // SPREAD
                          {kFieldSpreadId, 0.4},              // spread 0.4
                          {kMassId, 0.5},                     // Mass 50
                          {kAnchorWaveformId, 0.0},           // Sine
                          {kAnchorModeId, 0.0},               // LOCK
                          {kAnchorLevelId, 0.6},              // level 0.6
                          {kAnchorFoldFeedId, 0.0},           // Anchor Fold Feed 0
                          {kDynamicsTransientAttackId, 1.0}}, // transient attack +12 dB
            .endpointB = {{kCoreDepthId, 0.7},                  // depth 0.7
                          {kCoreBodyId, 0.3},                   // body 0.3
                          {kCoreEdgeId, 0.3},                   // edge 0.3
                          {kCoreExcitationMixId, 0.0},          // excitation mix 0
                          {kFoldAmountId, 0.0},                 // Harmonic Fold 0
                          {kFoldShapeId, 0.0},                  // SHAPE 0
                          {kCharacterId, 0.6},                  // Bite
                          {kCharacterAmountId, 1.0},            // Amount 1
                          {kCharacterToneId, 0.7},              // Tone 0.7
                          {kAmpAttackId, 0.2},                  // 1 ms
                          {kAmpDecayId, 0.708814},              // 350 ms
                          {kAmpSustainId, 0.6},                 // sustain 0.6
                          {kAmpReleaseId, 0.660206},            // 200 ms
                          {kFieldImageModeId, 0.5},             // SPREAD
                          {kFieldSpreadId, 0.4},                // spread 0.4
                          {kMassId, 0.5},                       // Mass 50
                          {kAnchorWaveformId, 0.0},             // Sine
                          {kAnchorModeId, 0.0},                 // LOCK
                          {kAnchorLevelId, 0.6},                // level 0.6
                          {kAnchorFoldFeedId, 0.0},             // Anchor Fold Feed 0
                          {kDynamicsTransientAttackId, 0.875}}, // transient attack +9 dB
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Hard Bite",
            .nameB = "Bright Bite",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 8: AT-08 Crush (plan 7 row 8) ----
        ProfundumPresetDef{
            .name = "Crushed Basalt",
            .category = "Driven",
            .description = "Character Crush: Crush at Amount 1 grinds the core while the Anchor at 0.5 keeps the fundamental.",
            .primary = C::AT08,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 0.8},          // depth 0.8
                          {kCoreBodyId, 0.25},          // body 0.25
                          {kCoreEdgeId, 0.2},           // edge 0.2
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.8},          // Crush
                          {kCharacterAmountId, 1.0},    // Amount 1
                          {kCharacterToneId, 0.45},     // Tone 0.45
                          {kAmpAttackId, 0.260206},     // 2 ms
                          {kAmpDecayId, 0.720412},      // 400 ms
                          {kAmpSustainId, 0.8},         // sustain 0.8
                          {kAmpReleaseId, 0.695424},    // 300 ms
                          {kFieldImageModeId, 0.0},     // SOLID
                          {kFieldSpreadId, 0.0},        // spread 0
                          {kMassId, 0.6},               // Mass 60
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.5},        // level 0.5
                          {kAnchorFoldFeedId, 0.0}},    // Anchor Fold Feed 0
            .endpointB = {{kCoreDepthId, 0.8},          // depth 0.8
                          {kCoreBodyId, 0.25},          // body 0.25
                          {kCoreEdgeId, 0.2},           // edge 0.2
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.8},          // Crush
                          {kCharacterAmountId, 1.0},    // Amount 1
                          {kCharacterToneId, 0.6},      // Tone 0.6
                          {kAmpAttackId, 0.260206},     // 2 ms
                          {kAmpDecayId, 0.720412},      // 400 ms
                          {kAmpSustainId, 0.8},         // sustain 0.8
                          {kAmpReleaseId, 0.695424},    // 300 ms
                          {kFieldImageModeId, 0.0},     // SOLID
                          {kFieldSpreadId, 0.0},        // spread 0
                          {kMassId, 0.6},               // Mass 60
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.5},        // level 0.5
                          {kAnchorFoldFeedId, 0.0}},    // Anchor Fold Feed 0
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Crushed",
            .nameB = "Crushed Bright",
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
            .description = "Character Fracture: Fracture at Amount 1 on a harmonic-rich core; the Anchor in FOLLOW tracks the "
                           "fractured pitch.",
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
            .endpointB = {{kCoreDepthId, 0.5},                // depth 0.5
                          {kCoreBodyId, 0.3},                 // body 0.3
                          {kCoreEdgeId, 1.0},                 // edge 1
                          {kCoreShiftId, 0.0},                // shift -1
                          {kCoreExcitationMixId, 1.0},        // excitation mix 1
                          {kCoreExcitationDriveId, 1.0},      // drive 1
                          {kCoreExcitationAsymmetryId, 0.5},  // asymmetry 0.5
                          {kFoldAmountId, 0.6},               // Harmonic Fold 60
                          {kFoldShapeId, 1.0},                // SHAPE 1 (FR-048 held load)
                          {kCharacterId, 1.0},                // Fracture
                          {kCharacterAmountId, 1.0},          // Amount 1
                          {kCharacterToneId, 0.55},           // Tone 0.55
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
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Fractured",
            .nameB = "Fractured Dark",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 10: AT-10 Anchor level (plan 7 row 10) ----
        // T027 FR-061 level: sustain 1 -> 0.5 (-5.10 -> -11.12 dBFS).
        ProfundumPresetDef{
            .name = "Bedrock",
            .category = "Sub",
            .description = "Anchor Level: a thin core over the Sine Anchor in LOCK at level 1, with Mass 70 for a plain heavy "
                           "floor.",
            .primary = C::AT10,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 1.0},                // depth 1
                          {kCoreBodyId, 0.0},                 // body 0
                          {kCoreEdgeId, 0.0},                 // edge 0
                          {kCoreExcitationMixId, 0.0},        // excitation mix 0
                          {kFoldAmountId, 0.0},               // Harmonic Fold 0
                          {kFoldShapeId, 0.0},                // SHAPE 0
                          {kCharacterId, 0.0},                // Tube
                          {kCharacterAmountId, 0.0},          // Amount 0
                          {kCharacterToneId, 0.5},            // Tone 0.5
                          {kAmpAttackId, 0.295424},           // 3 ms
                          {kAmpDecayId, 0.660206},            // 200 ms
                          {kAmpSustainId, 0.5},               // sustain 0.5
                          {kAmpReleaseId, 0.679588},          // 250 ms
                          {kFieldImageModeId, 0.0},           // SOLID
                          {kFieldSpreadId, 0.0},              // spread 0
                          {kMassId, 0.7},                     // Mass 70
                          {kAnchorWaveformId, 0.0},           // Sine
                          {kAnchorModeId, 0.0},               // LOCK
                          {kAnchorLevelId, 1.0},              // level 1
                          {kAnchorFoldFeedId, 0.0},           // Anchor Fold Feed 0
                          {kDynamicsTransientAttackId, 0.5}}, // transient attack 0 dB
            .endpointB = {{kCoreDepthId, 1.0},                // depth 1
                          {kCoreBodyId, 0.0},                 // body 0
                          {kCoreEdgeId, 0.1},                 // edge 0.1
                          {kCoreExcitationMixId, 0.0},        // excitation mix 0
                          {kFoldAmountId, 0.0},               // Harmonic Fold 0
                          {kFoldShapeId, 0.0},                // SHAPE 0
                          {kCharacterId, 0.0},                // Tube
                          {kCharacterAmountId, 0.0},          // Amount 0
                          {kCharacterToneId, 0.5},            // Tone 0.5
                          {kAmpAttackId, 0.295424},           // 3 ms
                          {kAmpDecayId, 0.660206},            // 200 ms
                          {kAmpSustainId, 0.5},               // sustain 0.5
                          {kAmpReleaseId, 0.679588},          // 250 ms
                          {kFieldImageModeId, 0.0},           // SOLID
                          {kFieldSpreadId, 0.0},              // spread 0
                          {kMassId, 0.7},                     // Mass 70
                          {kAnchorWaveformId, 0.0},           // Sine
                          {kAnchorModeId, 0.0},               // LOCK
                          {kAnchorLevelId, 0.85},             // level 0.85
                          {kAnchorFoldFeedId, 0.0},           // Anchor Fold Feed 0
                          {kDynamicsTransientAttackId, 0.5}}, // transient attack 0 dB
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Bare Floor",
            .nameB = "Floor Edge",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 11: AT-11 PUNCH (plan 7 row 11) ----
        ProfundumPresetDef{
            .name = "Punch Anchor",
            .category = "Plucked",
            .description = "Anchor Lock to Punch: the Anchor in PUNCH, a 6 dB boost over 40 ms, under a 300 ms pluck of a light "
                           "body.",
            .primary = C::AT11,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 0.6},             // depth 0.6
                          {kCoreBodyId, 0.3},              // body 0.3
                          {kCoreEdgeId, 0.2},              // edge 0.2
                          {kCoreExcitationMixId, 0.0},     // excitation mix 0
                          {kFoldAmountId, 0.0},            // Harmonic Fold 0
                          {kFoldShapeId, 0.0},             // SHAPE 0
                          {kCharacterId, 0.0},             // Tube
                          {kCharacterAmountId, 0.0},       // Amount 0
                          {kCharacterToneId, 0.5},         // Tone 0.5
                          {kAmpAttackId, 0.2},             // 1 ms
                          {kAmpDecayId, 0.695424},         // 300 ms
                          {kAmpSustainId, 0.5},            // sustain 0.5
                          {kAmpReleaseId, 0.635218},       // 150 ms
                          {kFieldImageModeId, 0.0},        // SOLID
                          {kFieldSpreadId, 0.0},           // spread 0
                          {kMassId, 0.55},                 // Mass 55
                          {kAnchorWaveformId, 0.0},        // Sine
                          {kAnchorModeId, 2.0 / 3.0},      // PUNCH
                          {kAnchorLevelId, 1.0},           // level 1
                          {kAnchorFoldFeedId, 0.0},        // Anchor Fold Feed 0
                          {kAnchorPunchBoostId, 0.5},      // punch boost 6 dB
                          {kAnchorPunchTimeId, 0.531551}}, // punch time 40 ms
            .endpointB = {{kCoreDepthId, 0.6},             // depth 0.6
                          {kCoreBodyId, 0.3},              // body 0.3
                          {kCoreEdgeId, 0.2},              // edge 0.2
                          {kCoreExcitationMixId, 0.0},     // excitation mix 0
                          {kFoldAmountId, 0.0},            // Harmonic Fold 0
                          {kFoldShapeId, 0.0},             // SHAPE 0
                          {kCharacterId, 0.0},             // Tube
                          {kCharacterAmountId, 0.0},       // Amount 0
                          {kCharacterToneId, 0.5},         // Tone 0.5
                          {kAmpAttackId, 0.2},             // 1 ms
                          {kAmpDecayId, 0.695424},         // 300 ms
                          {kAmpSustainId, 0.4},            // sustain 0.4
                          {kAmpReleaseId, 0.635218},       // 150 ms
                          {kFieldImageModeId, 0.0},        // SOLID
                          {kFieldSpreadId, 0.0},           // spread 0
                          {kMassId, 0.55},                 // Mass 55
                          {kAnchorWaveformId, 0.0},        // Sine
                          {kAnchorModeId, 2.0 / 3.0},      // PUNCH
                          {kAnchorLevelId, 1.0},           // level 1
                          {kAnchorFoldFeedId, 0.0},        // Anchor Fold Feed 0
                          {kAnchorPunchBoostId, 0.75},     // punch boost 9 dB
                          {kAnchorPunchTimeId, 0.531551}}, // punch time 40 ms
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Punched",
            .nameB = "Punched Harder",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 12: AT-12 GHOST (plan 7 row 12) ----
        // T027 FR-061 level: sustain 0.2 -> 0.7 (-21.72 -> -10.84 dBFS).
        ProfundumPresetDef{
            .name = "Ghost Kick",
            .category = "Plucked",
            .description = "Anchor Lock to Ghost: the Anchor in GHOST for 100 ms under a short decay, a kick-like thump.",
            .primary = C::AT12,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 0.5},          // depth 0.5
                          {kCoreBodyId, 0.2},           // body 0.2
                          {kCoreEdgeId, 0.4},           // edge 0.4
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.0},    // Amount 0
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.2},          // 1 ms
                          {kAmpDecayId, 0.651055},      // 180 ms
                          {kAmpSustainId, 0.7},         // sustain 0.7
                          {kAmpReleaseId, 0.615836},    // 120 ms
                          {kFieldImageModeId, 0.0},     // SOLID
                          {kFieldSpreadId, 0.0},        // spread 0
                          {kMassId, 0.6},               // Mass 60
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 1.0},         // GHOST
                          {kAnchorLevelId, 1.0},        // level 1
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kCoreShiftId, 0.6},          // shift 0.2
                          {kAnchorGhostTimeId, 1.0}},   // ghost time 100 ms
            .endpointB = {{kCoreDepthId, 0.5},             // depth 0.5
                          {kCoreBodyId, 0.2},              // body 0.2
                          {kCoreEdgeId, 0.4},              // edge 0.4
                          {kCoreExcitationMixId, 0.0},     // excitation mix 0
                          {kFoldAmountId, 0.0},            // Harmonic Fold 0
                          {kFoldShapeId, 0.0},             // SHAPE 0
                          {kCharacterId, 0.0},             // Tube
                          {kCharacterAmountId, 0.0},       // Amount 0
                          {kCharacterToneId, 0.5},         // Tone 0.5
                          {kAmpAttackId, 0.2},             // 1 ms
                          {kAmpDecayId, 0.679588},         // 250 ms
                          {kAmpSustainId, 0.7},            // sustain 0.7
                          {kAmpReleaseId, 0.615836},       // 120 ms
                          {kFieldImageModeId, 0.0},        // SOLID
                          {kFieldSpreadId, 0.0},           // spread 0
                          {kMassId, 0.6},                  // Mass 60
                          {kAnchorWaveformId, 0.0},        // Sine
                          {kAnchorModeId, 1.0},            // GHOST
                          {kAnchorLevelId, 1.0},           // level 1
                          {kAnchorFoldFeedId, 0.0},        // Anchor Fold Feed 0
                          {kCoreShiftId, 0.6},             // shift 0.2
                          {kAnchorGhostTimeId, 0.889076}}, // ghost time 60 ms
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Ghost Thump",
            .nameB = "Ghost Tap",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 13: AT-13 FOLLOW (plan 7 row 13; secondary AT-06; the Rectify variant (P-2)) ----
        ProfundumPresetDef{
            .name = "Rectified Follow",
            .category = "Driven",
            .description = "Anchor Lock to Follow: Rectify at Amount 1 doubles the core and the Anchor in FOLLOW at level 1 goes "
                           "with it.",
            .primary = C::AT13,
            .secondaries = {C::AT06},
            .endpointA = {{kCoreDepthId, 1.0},          // depth 1
                          {kCoreBodyId, 0.0},           // body 0
                          {kCoreEdgeId, 0.0},           // edge 0
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.4},          // Rectify
                          {kCharacterAmountId, 1.0},    // Amount 1
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.260206},     // 2 ms
                          {kAmpDecayId, 0.695424},      // 300 ms
                          {kAmpSustainId, 0.9},         // sustain 0.9
                          {kAmpReleaseId, 0.679588},    // 250 ms
                          {kFieldImageModeId, 0.5},     // SPREAD
                          {kFieldSpreadId, 0.3},        // spread 0.3
                          {kMassId, 0.5},               // Mass 50
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 1.0 / 3.0},   // FOLLOW
                          {kAnchorLevelId, 1.0},        // level 1
                          {kAnchorFoldFeedId, 0.0}},    // Anchor Fold Feed 0
            .endpointB = {{kCoreDepthId, 1.0},          // depth 1
                          {kCoreBodyId, 0.0},           // body 0
                          {kCoreEdgeId, 0.0},           // edge 0
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.4},          // Rectify
                          {kCharacterAmountId, 1.0},    // Amount 1
                          {kCharacterToneId, 0.4},      // Tone 0.4
                          {kAmpAttackId, 0.260206},     // 2 ms
                          {kAmpDecayId, 0.695424},      // 300 ms
                          {kAmpSustainId, 0.9},         // sustain 0.9
                          {kAmpReleaseId, 0.679588},    // 250 ms
                          {kFieldImageModeId, 0.5},     // SPREAD
                          {kFieldSpreadId, 0.3},        // spread 0.3
                          {kMassId, 0.5},               // Mass 50
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 1.0 / 3.0},   // FOLLOW
                          {kAnchorLevelId, 1.0},        // level 1
                          {kAnchorFoldFeedId, 0.0}},    // Anchor Fold Feed 0
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Following",
            .nameB = "Following Dark",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 14: AT-14 Triangle (plan 7 row 14) ----
        // T027 FR-061 level: sustain 0.9 -> 0.5 (-6.77 -> -11.88 dBFS).
        // T027: attack 20 ms parts it from Locked Floor (d 0.165 -> 2.96 at 4 ms vs 20 ms); AT-14 unchanged.
        ProfundumPresetDef{
            .name = "Triangle Floor",
            .category = "Sub",
            .description = "Anchor Sine to Triangle: a Triangle Anchor at level 1 under a thin core, a hollow-edged floor.",
            .primary = C::AT14,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 1.0},                  // depth 1
                          {kCoreBodyId, 0.0},                   // body 0
                          {kCoreEdgeId, 0.0},                   // edge 0
                          {kCoreExcitationMixId, 0.0},          // excitation mix 0
                          {kFoldAmountId, 0.0},                 // Harmonic Fold 0
                          {kFoldShapeId, 0.0},                  // SHAPE 0
                          {kCharacterId, 0.0},                  // Tube
                          {kCharacterAmountId, 0.0},            // Amount 0
                          {kCharacterToneId, 0.5},              // Tone 0.5
                          {kAmpAttackId, 0.460206},             // 20 ms
                          {kAmpDecayId, 0.695424},              // 300 ms
                          {kAmpSustainId, 0.5},                 // sustain 0.5
                          {kAmpReleaseId, 0.708814},            // 350 ms
                          {kFieldImageModeId, 0.0},             // SOLID
                          {kFieldSpreadId, 0.0},                // spread 0
                          {kMassId, 0.6},                       // Mass 60
                          {kAnchorWaveformId, 0.25},            // Triangle
                          {kAnchorModeId, 0.0},                 // LOCK
                          {kAnchorLevelId, 1.0},                // level 1
                          {kAnchorFoldFeedId, 0.0},             // Anchor Fold Feed 0
                          {kDynamicsTransientAttackId, 0.625}}, // transient attack +3 dB
            .endpointB = {{kCoreDepthId, 1.0},                  // depth 1
                          {kCoreBodyId, 0.0},                   // body 0
                          {kCoreEdgeId, 0.1},                   // edge 0.1
                          {kCoreExcitationMixId, 0.0},          // excitation mix 0
                          {kFoldAmountId, 0.0},                 // Harmonic Fold 0
                          {kFoldShapeId, 0.0},                  // SHAPE 0
                          {kCharacterId, 0.0},                  // Tube
                          {kCharacterAmountId, 0.0},            // Amount 0
                          {kCharacterToneId, 0.5},              // Tone 0.5
                          {kAmpAttackId, 0.460206},             // 20 ms
                          {kAmpDecayId, 0.695424},              // 300 ms
                          {kAmpSustainId, 0.5},                 // sustain 0.5
                          {kAmpReleaseId, 0.708814},            // 350 ms
                          {kFieldImageModeId, 0.0},             // SOLID
                          {kFieldSpreadId, 0.0},                // spread 0
                          {kMassId, 0.6},                       // Mass 60
                          {kAnchorWaveformId, 0.25},            // Triangle
                          {kAnchorModeId, 0.0},                 // LOCK
                          {kAnchorLevelId, 0.85},               // level 0.85
                          {kAnchorFoldFeedId, 0.0},             // Anchor Fold Feed 0
                          {kDynamicsTransientAttackId, 0.625}}, // transient attack +3 dB
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Triangle Under",
            .nameB = "Triangle Lifted",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 15: AT-15 Rounded Sine (plan 7 row 15) ----
        // T027 FR-061 level: sustain 0.85 -> 0.55 (-6.53 -> -10.31 dBFS).
        ProfundumPresetDef{
            .name = "Rounded Weight",
            .category = "Sub",
            .description = "Anchor Sine to Rounded Sine: a Rounded Sine Anchor at level 1 with Mass 70, soft but weighty.",
            .primary = C::AT15,
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
                          {kAmpAttackId, 0.35563},      // 6 ms
                          {kAmpDecayId, 0.720412},      // 400 ms
                          {kAmpSustainId, 0.55},        // sustain 0.55
                          {kAmpReleaseId, 0.720412},    // 400 ms
                          {kFieldImageModeId, 0.5},     // SPREAD
                          {kFieldSpreadId, 0.25},       // spread 0.25
                          {kMassId, 0.7},               // Mass 70
                          {kAnchorWaveformId, 0.5},     // RoundedSine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 1.0},        // level 1
                          {kAnchorFoldFeedId, 0.0}},    // Anchor Fold Feed 0
            .endpointB = {{kCoreDepthId, 1.0},          // depth 1
                          {kCoreBodyId, 0.0},           // body 0
                          {kCoreEdgeId, 0.1},           // edge 0.1
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.0},    // Amount 0
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.35563},      // 6 ms
                          {kAmpDecayId, 0.720412},      // 400 ms
                          {kAmpSustainId, 0.55},        // sustain 0.55
                          {kAmpReleaseId, 0.720412},    // 400 ms
                          {kFieldImageModeId, 0.5},     // SPREAD
                          {kFieldSpreadId, 0.25},       // spread 0.25
                          {kMassId, 0.8},               // Mass 80
                          {kAnchorWaveformId, 0.5},     // RoundedSine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 1.0},        // level 1
                          {kAnchorFoldFeedId, 0.0}},    // Anchor Fold Feed 0
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Rounded",
            .nameB = "Rounded Heavy",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 16: AT-16 Asymmetric (plan 7 row 16) ----
        ProfundumPresetDef{
            .name = "Lopsided Sub",
            .category = "Sub",
            .description = "Anchor Sine to Asymmetric Sine: an Asymmetric Sine Anchor gives the sub a lopsided, even-harmonic "
                           "push.",
            .primary = C::AT16,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 1.0},                     // depth 1
                          {kCoreBodyId, 0.0},                      // body 0
                          {kCoreEdgeId, 0.0},                      // edge 0
                          {kCoreExcitationMixId, 0.0},             // excitation mix 0
                          {kFoldAmountId, 0.0},                    // Harmonic Fold 0
                          {kFoldShapeId, 0.0},                     // SHAPE 0
                          {kCharacterId, 0.0},                     // Tube
                          {kCharacterAmountId, 0.0},               // Amount 0
                          {kCharacterToneId, 0.5},                 // Tone 0.5
                          {kAmpAttackId, 0.260206},                // 2 ms
                          {kAmpDecayId, 0.679588},                 // 250 ms
                          {kAmpSustainId, 0.8},                    // sustain 0.8
                          {kAmpReleaseId, 0.695424},               // 300 ms
                          {kFieldImageModeId, 0.0},                // SOLID
                          {kFieldSpreadId, 0.0},                   // spread 0
                          {kMassId, 0.45},                         // Mass 45
                          {kAnchorWaveformId, 0.75},               // AsymmetricSine
                          {kAnchorModeId, 0.0},                    // LOCK
                          {kAnchorLevelId, 1.0},                   // level 1
                          {kAnchorFoldFeedId, 0.0},                // Anchor Fold Feed 0
                          {kDynamicsTransientAttackId, 0.833333}}, // transient attack +8 dB
            .endpointB = {{kCoreDepthId, 1.0},                     // depth 1
                          {kCoreBodyId, 0.0},                      // body 0
                          {kCoreEdgeId, 0.0},                      // edge 0
                          {kCoreExcitationMixId, 0.0},             // excitation mix 0
                          {kFoldAmountId, 0.0},                    // Harmonic Fold 0
                          {kFoldShapeId, 0.0},                     // SHAPE 0
                          {kCharacterId, 0.0},                     // Tube
                          {kCharacterAmountId, 0.0},               // Amount 0
                          {kCharacterToneId, 0.5},                 // Tone 0.5
                          {kAmpAttackId, 0.260206},                // 2 ms
                          {kAmpDecayId, 0.679588},                 // 250 ms
                          {kAmpSustainId, 0.8},                    // sustain 0.8
                          {kAmpReleaseId, 0.695424},               // 300 ms
                          {kFieldImageModeId, 0.0},                // SOLID
                          {kFieldSpreadId, 0.0},                   // spread 0
                          {kMassId, 0.45},                         // Mass 45
                          {kAnchorWaveformId, 0.75},               // AsymmetricSine
                          {kAnchorModeId, 0.0},                    // LOCK
                          {kAnchorLevelId, 0.8},                   // level 0.8
                          {kAnchorFoldFeedId, 0.0},                // Anchor Fold Feed 0
                          {kDynamicsTransientAttackId, 0.833333}}, // transient attack +8 dB
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Lopsided",
            .nameB = "Lopsided Low",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 17: AT-17 Octave Down (plan 7 row 17) ----
        ProfundumPresetDef{
            .name = "Under Octave",
            .category = "Sub",
            .description = "Anchor Sine to Octave Down: the Octave Down Anchor puts a sub an octave under the core.",
            .primary = C::AT17,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 0.8},          // depth 0.8
                          {kCoreBodyId, 0.2},           // body 0.2
                          {kCoreEdgeId, 0.15},          // edge 0.15
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.0},    // Amount 0
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.339794},     // 5 ms
                          {kAmpDecayId, 0.739794},      // 500 ms
                          {kAmpSustainId, 0.9},         // sustain 0.9
                          {kAmpReleaseId, 0.730643},    // 450 ms
                          {kFieldImageModeId, 0.0},     // SOLID
                          {kFieldSpreadId, 0.0},        // spread 0
                          {kMassId, 0.55},              // Mass 55
                          {kAnchorWaveformId, 1.0},     // OctaveDownSine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.9},        // level 0.9
                          {kAnchorFoldFeedId, 0.0}},    // Anchor Fold Feed 0
            .endpointB = {{kCoreDepthId, 0.8},          // depth 0.8
                          {kCoreBodyId, 0.3},           // body 0.3
                          {kCoreEdgeId, 0.3},           // edge 0.3
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.0},    // Amount 0
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.339794},     // 5 ms
                          {kAmpDecayId, 0.739794},      // 500 ms
                          {kAmpSustainId, 0.9},         // sustain 0.9
                          {kAmpReleaseId, 0.730643},    // 450 ms
                          {kFieldImageModeId, 0.0},     // SOLID
                          {kFieldSpreadId, 0.0},        // spread 0
                          {kMassId, 0.55},              // Mass 55
                          {kAnchorWaveformId, 1.0},     // OctaveDownSine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.9},        // level 0.9
                          {kAnchorFoldFeedId, 0.0}},    // Anchor Fold Feed 0
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Octave Under",
            .nameB = "Octave Under Body",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 18: AT-18 Fold Feed (plan 7 row 18) ----
        // Tube at Amount 0 (C-9: never Rectify + Tone 1 + Fold 100 + WIDE); the probe base A_s + Fold 100 +
        // Anchor level 1, with a SPREAD image.
        ProfundumPresetDef{
            .name = "Fed Fold",
            .category = "Driven",
            .description = "Anchor Fold Feed: Harmonic Fold 100 with the Anchor Fold Feed at 1, the Anchor driven into the "
                           "folder.",
            .primary = C::AT18,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 1.0},          // depth 1
                          {kCoreBodyId, 0.0},           // body 0
                          {kCoreEdgeId, 0.0},           // edge 0
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 1.0},         // Harmonic Fold 100
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.0},    // Amount 0
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.260206},     // 2 ms
                          {kAmpDecayId, 0.708814},      // 350 ms
                          {kAmpSustainId, 0.8},         // sustain 0.8
                          {kAmpReleaseId, 0.695424},    // 300 ms
                          {kFieldImageModeId, 0.5},     // SPREAD
                          {kFieldSpreadId, 0.5},        // spread 0.5
                          {kMassId, 0.5},               // Mass 50
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 1.0},        // level 1
                          {kAnchorFoldFeedId, 1.0}},    // Anchor Fold Feed 1
            .endpointB = {{kCoreDepthId, 1.0},          // depth 1
                          {kCoreBodyId, 0.0},           // body 0
                          {kCoreEdgeId, 0.0},           // edge 0
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 1.0},         // Harmonic Fold 100
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.0},    // Amount 0
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.260206},     // 2 ms
                          {kAmpDecayId, 0.708814},      // 350 ms
                          {kAmpSustainId, 0.8},         // sustain 0.8
                          {kAmpReleaseId, 0.695424},    // 300 ms
                          {kFieldImageModeId, 0.5},     // SPREAD
                          {kFieldSpreadId, 0.5},        // spread 0.5
                          {kMassId, 0.5},               // Mass 50
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 1.0},        // level 1
                          {kAnchorFoldFeedId, 0.7}},    // Anchor Fold Feed 0.7
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Fed Anchor",
            .nameB = "Half Fed",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 19: AT-19 Drop amount (plan 7 row 19) ----
        ProfundumPresetDef{
            .name = "Dropping Stone",
            .category = "Plucked",
            .description = "Pitch Drop Amount: a 24 st drop over 200 ms at every note, a stone falling into the floor.",
            .primary = C::AT19,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 0.9},          // depth 0.9
                          {kCoreBodyId, 0.1},           // body 0.1
                          {kCoreEdgeId, 0.3},           // edge 0.3
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.0},    // Amount 0
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.260206},     // 2 ms
                          {kAmpDecayId, 0.720412},      // 400 ms
                          {kAmpSustainId, 0.5},         // sustain 0.5
                          {kAmpReleaseId, 0.660206},    // 200 ms
                          {kFieldImageModeId, 0.0},     // SOLID
                          {kFieldSpreadId, 0.0},        // spread 0
                          {kMassId, 0.55},              // Mass 55
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 1.0},        // level 1
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kCoreShiftId, 0.4},          // shift -0.2
                          {kDropAmountId, 0.5},         // drop 24 st
                          {kDropTimeId, 0.638809},      // drop time 200 ms
                          {kDropShapeId, 0.0},          // drop shape 0
                          {kDropReboundId, 0.0}},       // rebound 0
            .endpointB = {{kCoreDepthId, 0.9},          // depth 0.9
                          {kCoreBodyId, 0.1},           // body 0.1
                          {kCoreEdgeId, 0.3},           // edge 0.3
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.0},    // Amount 0
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.260206},     // 2 ms
                          {kAmpDecayId, 0.720412},      // 400 ms
                          {kAmpSustainId, 0.5},         // sustain 0.5
                          {kAmpReleaseId, 0.660206},    // 200 ms
                          {kFieldImageModeId, 0.0},     // SOLID
                          {kFieldSpreadId, 0.0},        // spread 0
                          {kMassId, 0.55},              // Mass 55
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 1.0},        // level 1
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kCoreShiftId, 0.4},          // shift -0.2
                          {kDropAmountId, 0.5},         // drop 24 st
                          {kDropTimeId, 0.670442},      // drop time 260 ms
                          {kDropShapeId, 0.0},          // drop shape 0
                          {kDropReboundId, 0.0}},       // rebound 0
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Stone Drop",
            .nameB = "Slower Stone",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 20: AT-20 Drop time (plan 7 row 20) ----
        ProfundumPresetDef{
            .name = "Slow Fall",
            .category = "Plucked",
            .description = "Pitch Drop Time: a 24 st drop stretched to 500 ms, a slow fall onto the note.",
            .primary = C::AT20,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 0.7},          // depth 0.7
                          {kCoreBodyId, 0.5},           // body 0.5
                          {kCoreEdgeId, 0.15},          // edge 0.15
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.0},    // Amount 0
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.2},          // 1 ms
                          {kAmpDecayId, 0.76902},       // 700 ms
                          {kAmpSustainId, 0.6},         // sustain 0.6
                          {kAmpReleaseId, 0.695424},    // 300 ms
                          {kFieldImageModeId, 0.5},     // SPREAD
                          {kFieldSpreadId, 0.3},        // spread 0.3
                          {kMassId, 0.5},               // Mass 50
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 1.0},        // level 1
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.7},  // body curvature 0.7
                          {kDropAmountId, 0.5},         // drop 24 st
                          {kDropTimeId, 0.749285},      // drop time 500 ms
                          {kDropShapeId, 0.0},          // drop shape 0
                          {kDropReboundId, 0.0}},       // rebound 0
            .endpointB = {{kCoreDepthId, 0.7},          // depth 0.7
                          {kCoreBodyId, 0.5},           // body 0.5
                          {kCoreEdgeId, 0.15},          // edge 0.15
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.0},    // Amount 0
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.2},          // 1 ms
                          {kAmpDecayId, 0.76902},       // 700 ms
                          {kAmpSustainId, 0.6},         // sustain 0.6
                          {kAmpReleaseId, 0.695424},    // 300 ms
                          {kFieldImageModeId, 0.5},     // SPREAD
                          {kFieldSpreadId, 0.3},        // spread 0.3
                          {kMassId, 0.5},               // Mass 50
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 1.0},        // level 1
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.7},  // body curvature 0.7
                          {kDropAmountId, 0.416667},    // drop 20 st
                          {kDropTimeId, 0.749285},      // drop time 500 ms
                          {kDropShapeId, 0.0},          // drop shape 0
                          {kDropReboundId, 0.0}},       // rebound 0
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Long Fall",
            .nameB = "Shorter Fall",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 21: AT-21 Drop shape (plan 7 row 21) ----
        ProfundumPresetDef{
            .name = "Curved Drop",
            .category = "Plucked",
            .description = "Pitch Drop Shape: a 24 st drop with shape 1, the curve bending late into the pitch.",
            .primary = C::AT21,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 0.5},                  // depth 0.5
                          {kCoreBodyId, 0.2},                   // body 0.2
                          {kCoreEdgeId, 0.6},                   // edge 0.6
                          {kCoreExcitationMixId, 0.0},          // excitation mix 0
                          {kFoldAmountId, 0.0},                 // Harmonic Fold 0
                          {kFoldShapeId, 0.0},                  // SHAPE 0
                          {kCharacterId, 0.0},                  // Tube
                          {kCharacterAmountId, 0.0},            // Amount 0
                          {kCharacterToneId, 0.5},              // Tone 0.5
                          {kAmpAttackId, 0.295424},             // 3 ms
                          {kAmpDecayId, 0.730643},              // 450 ms
                          {kAmpSustainId, 0.45},                // sustain 0.45
                          {kAmpReleaseId, 0.660206},            // 200 ms
                          {kFieldImageModeId, 0.0},             // SOLID
                          {kFieldSpreadId, 0.0},                // spread 0
                          {kMassId, 0.5},                       // Mass 50
                          {kAnchorWaveformId, 0.0},             // Sine
                          {kAnchorModeId, 0.0},                 // LOCK
                          {kAnchorLevelId, 1.0},                // level 1
                          {kAnchorFoldFeedId, 0.0},             // Anchor Fold Feed 0
                          {kCoreShiftId, 0.55},                 // shift 0.1
                          {kDropAmountId, 0.5},                 // drop 24 st
                          {kDropTimeId, 0.604124},              // drop time 150 ms
                          {kDropShapeId, 1.0},                  // drop shape 1
                          {kDropReboundId, 0.0},                // rebound 0
                          {kDynamicsTransientAttackId, 0.875}}, // transient attack +9 dB
            .endpointB = {{kCoreDepthId, 0.5},                  // depth 0.5
                          {kCoreBodyId, 0.2},                   // body 0.2
                          {kCoreEdgeId, 0.6},                   // edge 0.6
                          {kCoreExcitationMixId, 0.0},          // excitation mix 0
                          {kFoldAmountId, 0.0},                 // Harmonic Fold 0
                          {kFoldShapeId, 0.0},                  // SHAPE 0
                          {kCharacterId, 0.0},                  // Tube
                          {kCharacterAmountId, 0.0},            // Amount 0
                          {kCharacterToneId, 0.5},              // Tone 0.5
                          {kAmpAttackId, 0.295424},             // 3 ms
                          {kAmpDecayId, 0.730643},              // 450 ms
                          {kAmpSustainId, 0.45},                // sustain 0.45
                          {kAmpReleaseId, 0.660206},            // 200 ms
                          {kFieldImageModeId, 0.0},             // SOLID
                          {kFieldSpreadId, 0.0},                // spread 0
                          {kMassId, 0.5},                       // Mass 50
                          {kAnchorWaveformId, 0.0},             // Sine
                          {kAnchorModeId, 0.0},                 // LOCK
                          {kAnchorLevelId, 1.0},                // level 1
                          {kAnchorFoldFeedId, 0.0},             // Anchor Fold Feed 0
                          {kCoreShiftId, 0.55},                 // shift 0.1
                          {kDropAmountId, 0.5},                 // drop 24 st
                          {kDropTimeId, 0.604124},              // drop time 150 ms
                          {kDropShapeId, 0.7},                  // drop shape 0.7
                          {kDropReboundId, 0.0},                // rebound 0
                          {kDynamicsTransientAttackId, 0.875}}, // transient attack +9 dB
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Curved",
            .nameB = "Gentler Curve",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 22: AT-22 Drop rebound (plan 7 row 22) ----
        ProfundumPresetDef{
            .name = "Rebound",
            .category = "Plucked",
            .description = "Pitch Drop Rebound: a 24 st drop with rebound 1 that undershoots and bounces back up to the note.",
            .primary = C::AT22,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 0.7},          // depth 0.7
                          {kCoreBodyId, 0.4},           // body 0.4
                          {kCoreEdgeId, 0.1},           // edge 0.1
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.0},    // Amount 0
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.320412},     // 4 ms
                          {kAmpDecayId, 0.739794},      // 500 ms
                          {kAmpSustainId, 0.5},         // sustain 0.5
                          {kAmpReleaseId, 0.679588},    // 250 ms
                          {kFieldImageModeId, 0.5},     // SPREAD
                          {kFieldSpreadId, 0.5},        // spread 0.5
                          {kMassId, 0.6},               // Mass 60
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 1.0},        // level 1
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.3},  // body curvature 0.3
                          {kDropAmountId, 0.5},         // drop 24 st
                          {kDropTimeId, 0.57722},       // drop time 120 ms
                          {kDropShapeId, 0.0},          // drop shape 0
                          {kDropReboundId, 1.0}},       // rebound 1
            .endpointB = {{kCoreDepthId, 0.7},          // depth 0.7
                          {kCoreBodyId, 0.4},           // body 0.4
                          {kCoreEdgeId, 0.1},           // edge 0.1
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.0},    // Amount 0
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.320412},     // 4 ms
                          {kAmpDecayId, 0.739794},      // 500 ms
                          {kAmpSustainId, 0.5},         // sustain 0.5
                          {kAmpReleaseId, 0.679588},    // 250 ms
                          {kFieldImageModeId, 0.5},     // SPREAD
                          {kFieldSpreadId, 0.5},        // spread 0.5
                          {kMassId, 0.6},               // Mass 60
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 1.0},        // level 1
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.3},  // body curvature 0.3
                          {kDropAmountId, 0.5},         // drop 24 st
                          {kDropTimeId, 0.57722},       // drop time 120 ms
                          {kDropShapeId, 0.0},          // drop shape 0
                          {kDropReboundId, 0.6}},       // rebound 0.6
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Bounce",
            .nameB = "Small Bounce",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 23: AT-23 Glide (plan 7 row 23; secondary AT-44) ----
        ProfundumPresetDef{
            .name = "Glide Path",
            .category = "Resonant",
            .description = "Pitch Glide: MONO legato with a 200 ms portamento sliding a resonant body; NoteMemory Legato keeps "
                           "the phase across each slide.",
            .primary = C::AT23,
            .secondaries = {C::AT44},
            .endpointA = {{kCoreDepthId, 0.6},          // depth 0.6
                          {kCoreBodyId, 0.6},           // body 0.6
                          {kCoreEdgeId, 0.2},           // edge 0.2
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.0},    // Amount 0
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.339794},     // 5 ms
                          {kAmpDecayId, 0.75563},       // 600 ms
                          {kAmpSustainId, 0.9},         // sustain 0.9
                          {kAmpReleaseId, 0.720412},    // 400 ms
                          {kFieldImageModeId, 0.5},     // SPREAD
                          {kFieldSpreadId, 0.5},        // spread 0.5
                          {kMassId, 0.5},               // Mass 50
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.8},        // level 0.8
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.8}}, // body curvature 0.8
            .endpointB = {{kCoreDepthId, 0.6},          // depth 0.6
                          {kCoreBodyId, 0.75},          // body 0.75
                          {kCoreEdgeId, 0.2},           // edge 0.2
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.0},    // Amount 0
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.339794},     // 5 ms
                          {kAmpDecayId, 0.75563},       // 600 ms
                          {kAmpSustainId, 0.9},         // sustain 0.9
                          {kAmpReleaseId, 0.720412},    // 400 ms
                          {kFieldImageModeId, 0.5},     // SPREAD
                          {kFieldSpreadId, 0.5},        // spread 0.5
                          {kMassId, 0.5},               // Mass 50
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.8},        // level 0.8
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.8}}, // body curvature 0.8
            .controls = {{kNoteLegatoId, 1.0},           // legato On
                         {kNotePortamentoId, 0.575793},  // portamento 200 ms
                         {kNoteMemoryId, 1.0 / 3.0},     // Note Memory Legato
                         {kNoteRetriggerId, 0.0},        // Retrigger Reset
                         {kMorphPositionId, 0.0}},       // p = 0
            .nameA = "Gliding",
            .nameB = "Gliding Body",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 24: AT-24 attack (plan 7 row 24) ----
        ProfundumPresetDef{
            .name = "Slow Swell",
            .category = "Resonant",
            .description = "Envelope Amp Attack: a 500 ms attack swells a resonant body up to sustain 0.8.",
            .primary = C::AT24,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 0.5},          // depth 0.5
                          {kCoreBodyId, 0.7},           // body 0.7
                          {kCoreEdgeId, 0.25},          // edge 0.25
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.0},    // Amount 0
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.739794},     // 500 ms
                          {kAmpDecayId, 0.780618},      // 800 ms
                          {kAmpSustainId, 0.8},         // sustain 0.8
                          {kAmpReleaseId, 0.790849},    // 900 ms
                          {kFieldImageModeId, 0.5},     // SPREAD
                          {kFieldSpreadId, 0.6},        // spread 0.6
                          {kMassId, 0.5},               // Mass 50
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.6},        // level 0.6
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.7},  // body curvature 0.7
                          {kCoreBodyEmphasisId, 0.7}},  // body emphasis -0.3
            .endpointB = {{kCoreDepthId, 0.5},          // depth 0.5
                          {kCoreBodyId, 0.55},          // body 0.55
                          {kCoreEdgeId, 0.25},          // edge 0.25
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.0},    // Amount 0
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.708814},     // 350 ms
                          {kAmpDecayId, 0.780618},      // 800 ms
                          {kAmpSustainId, 0.8},         // sustain 0.8
                          {kAmpReleaseId, 0.790849},    // 900 ms
                          {kFieldImageModeId, 0.5},     // SPREAD
                          {kFieldSpreadId, 0.6},        // spread 0.6
                          {kMassId, 0.5},               // Mass 50
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.6},        // level 0.6
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.7},  // body curvature 0.7
                          {kCoreBodyEmphasisId, 0.7}},  // body emphasis -0.3
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Swelling",
            .nameB = "Quicker Swell",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 25: AT-25 release (plan 7 row 25) ----
        ProfundumPresetDef{
            .name = "Long Tail",
            .category = "Sub",
            .description = "Envelope Amp Release: a 2000 ms release lets the sub ring on long after the key lifts.",
            .primary = C::AT25,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 0.75},         // depth 0.75
                          {kCoreBodyId, 0.1},           // body 0.1
                          {kCoreEdgeId, 0.35},          // edge 0.35
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.0},    // Amount 0
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.4},          // 10 ms
                          {kAmpDecayId, 0.720412},      // 400 ms
                          {kAmpSustainId, 0.8},         // sustain 0.8
                          {kAmpReleaseId, 0.860206},    // 2000 ms
                          {kFieldImageModeId, 0.0},     // SOLID
                          {kFieldSpreadId, 0.0},        // spread 0
                          {kMassId, 0.55},              // Mass 55
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.9},        // level 0.9
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kCoreShiftId, 0.65}},        // shift 0.3
            .endpointB = {{kCoreDepthId, 0.75},         // depth 0.75
                          {kCoreBodyId, 0.1},           // body 0.1
                          {kCoreEdgeId, 0.35},          // edge 0.35
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.0},    // Amount 0
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.4},          // 10 ms
                          {kAmpDecayId, 0.720412},      // 400 ms
                          {kAmpSustainId, 0.8},         // sustain 0.8
                          {kAmpReleaseId, 0.835218},    // 1500 ms
                          {kFieldImageModeId, 0.0},     // SOLID
                          {kFieldSpreadId, 0.0},        // spread 0
                          {kMassId, 0.55},              // Mass 55
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.9},        // level 0.9
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kCoreShiftId, 0.65}},        // shift 0.3
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Long Ring",
            .nameB = "Shorter Ring",
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
            .description = "Mass Amount: Mass at 100 with the Anchor at 0.5, the heaviest held weight.",
            .primary = C::AT26,
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
                          {kAmpAttackId, 0.260206},     // 2 ms
                          {kAmpDecayId, 0.695424},      // 300 ms
                          {kAmpSustainId, 0.3},         // sustain 0.3
                          {kAmpReleaseId, 0.708814},    // 350 ms
                          {kFieldImageModeId, 0.0},     // SOLID
                          {kFieldSpreadId, 0.0},        // spread 0
                          {kMassId, 1.0},               // Mass 100
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.5},        // level 0.5
                          {kAnchorFoldFeedId, 0.0}},    // Anchor Fold Feed 0
            .endpointB = {{kCoreDepthId, 1.0},          // depth 1
                          {kCoreBodyId, 0.0},           // body 0
                          {kCoreEdgeId, 0.0},           // edge 0
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.0},    // Amount 0
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.260206},     // 2 ms
                          {kAmpDecayId, 0.695424},      // 300 ms
                          {kAmpSustainId, 0.3},         // sustain 0.3
                          {kAmpReleaseId, 0.708814},    // 350 ms
                          {kFieldImageModeId, 0.0},     // SOLID
                          {kFieldSpreadId, 0.0},        // spread 0
                          {kMassId, 0.85},              // Mass 85
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.5},        // level 0.5
                          {kAnchorFoldFeedId, 0.0}},    // Anchor Fold Feed 0
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Full Mass",
            .nameB = "Less Mass",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 27: AT-27 dynamics gain (plan 7 row 27) ----
        // The probe's AT-27 base B at Mass 100 with sustain 1 (B10-1; T027: sustain 0.8 read 2.92 dB at C3); decay 1990 ms keeps C-6 (attack + decay
        // <= 2000 ms).
        ProfundumPresetDef{
            .name = "Pressure Front",
            .category = "Harmonic",
            .description = "Mass Dynamics Gain: Mass 100 on a heavy harmonic body, the dynamics gain pressing the front of each "
                           "note.",
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
                          {kCharacterToneId, 0.65},           // Tone 0.65
                          {kAmpAttackId, 0.2},                // 1 ms
                          {kAmpDecayId, 0.859771},            // 1990 ms
                          {kAmpSustainId, 1.0},               // sustain 1
                          {kAmpReleaseId, 0.780618},          // 800 ms
                          {kFieldImageModeId, 0.5},           // SPREAD
                          {kFieldSpreadId, 1.0},              // spread 1
                          {kMassId, 1.0},                     // Mass 100
                          {kAnchorWaveformId, 0.0},           // Sine
                          {kAnchorModeId, 0.0},               // LOCK
                          {kAnchorLevelId, 0.5},              // level 0.5
                          {kAnchorFoldFeedId, 0.0}},          // Anchor Fold Feed 0
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Pressing",
            .nameB = "Pressing Narrow",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 28: AT-28 transient (plan 7 row 28) ----
        // T027 FR-061 level: sustain 0.4 -> 0.85 (-17.92 -> -11.38 dBFS).
        ProfundumPresetDef{
            .name = "Hard Attack",
            .category = "Plucked",
            .description = "Mass Transient Attack: the Transient Attack at +12 dB snaps the front of a harmonic pluck.",
            .primary = C::AT28,
            .secondaries = {},
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
                          {kAnchorWaveformId, 0.0},           // Sine
                          {kAnchorModeId, 0.0},               // LOCK
                          {kAnchorLevelId, 0.5},              // level 0.5
                          {kAnchorFoldFeedId, 0.0},           // Anchor Fold Feed 0
                          {kDynamicsTransientAttackId, 1.0}}, // transient attack +12 dB
            .endpointB = {{kCoreDepthId, 0.5},                  // depth 0.5
                          {kCoreBodyId, 0.3},                   // body 0.3
                          {kCoreEdgeId, 0.6},                   // edge 0.6
                          {kCoreShiftId, 0.2},                  // shift -0.6
                          {kCoreExcitationMixId, 0.8},          // excitation mix 0.8
                          {kCoreExcitationDriveId, 0.8},        // drive 0.8
                          {kCoreExcitationAsymmetryId, 0.3},    // asymmetry 0.3
                          {kFoldAmountId, 0.4},                 // Harmonic Fold 40
                          {kFoldShapeId, 0.3},                  // SHAPE 0.3
                          {kCharacterId, 0.2},                  // Fold
                          {kCharacterAmountId, 0.4},            // Amount 0.4
                          {kCharacterToneId, 0.7},              // Tone 0.7
                          {kAmpAttackId, 0.2},                  // 1 ms
                          {kAmpDecayId, 0.730643},              // 450 ms
                          {kAmpSustainId, 0.85},                // sustain 0.85
                          {kAmpReleaseId, 0.679588},            // 250 ms
                          {kFieldImageModeId, 0.5},             // SPREAD
                          {kFieldSpreadId, 0.7},                // spread 0.7
                          {kMassId, 0.55},                      // Mass 55
                          {kAnchorWaveformId, 0.0},             // Sine
                          {kAnchorModeId, 0.0},                 // LOCK
                          {kAnchorLevelId, 0.5},                // level 0.5
                          {kAnchorFoldFeedId, 0.0},             // Anchor Fold Feed 0
                          {kDynamicsTransientAttackId, 0.875}}, // transient attack +9 dB
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Snapping",
            .nameB = "Softer Snap",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 29: AT-29 SPREAD (plan 7 row 29; secondary AT-31) ----
        ProfundumPresetDef{
            .name = "Wide Harmonics",
            .category = "Harmonic",
            .description = "Field Solid to Spread: the SPREAD image at spread 1 opens a harmonic body while the sub stays "
                           "centred.",
            .primary = C::AT29,
            .secondaries = {C::AT31},
            .endpointA = {{kCoreDepthId, 0.5},            // depth 0.5
                          {kCoreBodyId, 0.6},             // body 0.6
                          {kCoreEdgeId, 0.5},             // edge 0.5
                          {kCoreExcitationMixId, 0.4},    // excitation mix 0.4
                          {kFoldAmountId, 0.0},           // Harmonic Fold 0
                          {kFoldShapeId, 0.0},            // SHAPE 0
                          {kCharacterId, 0.0},            // Tube
                          {kCharacterAmountId, 0.0},      // Amount 0
                          {kCharacterToneId, 0.5},        // Tone 0.5
                          {kAmpAttackId, 0.295424},       // 3 ms
                          {kAmpDecayId, 0.75563},         // 600 ms
                          {kAmpSustainId, 0.85},          // sustain 0.85
                          {kAmpReleaseId, 0.739794},      // 500 ms
                          {kFieldImageModeId, 0.5},       // SPREAD
                          {kFieldSpreadId, 1.0},          // spread 1
                          {kMassId, 0.5},                 // Mass 50
                          {kAnchorWaveformId, 0.0},       // Sine
                          {kAnchorModeId, 0.0},           // LOCK
                          {kAnchorLevelId, 0.6},          // level 0.6
                          {kAnchorFoldFeedId, 0.0},       // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.6},    // body curvature 0.6
                          {kCoreShiftId, 0.6},            // shift 0.2
                          {kCoreExcitationDriveId, 0.6}}, // drive 0.6
            .endpointB = {{kCoreDepthId, 0.5},            // depth 0.5
                          {kCoreBodyId, 0.6},             // body 0.6
                          {kCoreEdgeId, 0.65},            // edge 0.65
                          {kCoreExcitationMixId, 0.4},    // excitation mix 0.4
                          {kFoldAmountId, 0.0},           // Harmonic Fold 0
                          {kFoldShapeId, 0.0},            // SHAPE 0
                          {kCharacterId, 0.0},            // Tube
                          {kCharacterAmountId, 0.0},      // Amount 0
                          {kCharacterToneId, 0.5},        // Tone 0.5
                          {kAmpAttackId, 0.295424},       // 3 ms
                          {kAmpDecayId, 0.75563},         // 600 ms
                          {kAmpSustainId, 0.85},          // sustain 0.85
                          {kAmpReleaseId, 0.739794},      // 500 ms
                          {kFieldImageModeId, 0.5},       // SPREAD
                          {kFieldSpreadId, 1.0},          // spread 1
                          {kMassId, 0.5},                 // Mass 50
                          {kAnchorWaveformId, 0.0},       // Sine
                          {kAnchorModeId, 0.0},           // LOCK
                          {kAnchorLevelId, 0.6},          // level 0.6
                          {kAnchorFoldFeedId, 0.0},       // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.6},    // body curvature 0.6
                          {kCoreShiftId, 0.6},            // shift 0.2
                          {kCoreExcitationDriveId, 0.6}}, // drive 0.6
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Spread Open",
            .nameB = "Spread Bright",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 30: AT-30 WIDE (plan 7 row 30) ----
        // The probe base A_s + Rectify at Amount 0.5 + WIDE, spread 1; Tone 0.5 and Fold 0 (C-9 rule 4).
        ProfundumPresetDef{
            .name = "Wide Rectifier",
            .category = "Driven",
            .description = "Field Solid to Wide: Rectify at 0.5 with Tone 0.5 under the WIDE image at spread 1.",
            .primary = C::AT30,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 1.0},          // depth 1
                          {kCoreBodyId, 0.0},           // body 0
                          {kCoreEdgeId, 0.0},           // edge 0
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
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
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.8},        // level 0.8
                          {kAnchorFoldFeedId, 0.0}},    // Anchor Fold Feed 0
            .endpointB = {{kCoreDepthId, 1.0},          // depth 1
                          {kCoreBodyId, 0.0},           // body 0
                          {kCoreEdgeId, 0.0},           // edge 0
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.4},          // Rectify
                          {kCharacterAmountId, 0.5},    // Amount 0.5
                          {kCharacterToneId, 0.4},      // Tone 0.4
                          {kAmpAttackId, 0.260206},     // 2 ms
                          {kAmpDecayId, 0.708814},      // 350 ms
                          {kAmpSustainId, 0.9},         // sustain 0.9
                          {kAmpReleaseId, 0.695424},    // 300 ms
                          {kFieldImageModeId, 1.0},     // WIDE
                          {kFieldSpreadId, 1.0},        // spread 1
                          {kMassId, 0.5},               // Mass 50
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.8},        // level 0.8
                          {kAnchorFoldFeedId, 0.0}},    // Anchor Fold Feed 0
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Wide Rectified",
            .nameB = "Wide Dark",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 31: AT-31 Spread (plan 7 row 31) ----
        ProfundumPresetDef{
            .name = "Spread Choir",
            .category = "Harmonic",
            .description = "Field Spread: SPREAD at spread 0.9 over a high body, a choir of partials around a centred floor.",
            .primary = C::AT31,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 0.4},           // depth 0.4
                          {kCoreBodyId, 0.85},           // body 0.85
                          {kCoreEdgeId, 0.35},           // edge 0.35
                          {kCoreExcitationMixId, 0.0},   // excitation mix 0
                          {kFoldAmountId, 0.0},          // Harmonic Fold 0
                          {kFoldShapeId, 0.0},           // SHAPE 0
                          {kCharacterId, 0.0},           // Tube
                          {kCharacterAmountId, 0.0},     // Amount 0
                          {kCharacterToneId, 0.5},       // Tone 0.5
                          {kAmpAttackId, 0.460206},      // 20 ms
                          {kAmpDecayId, 0.76902},        // 700 ms
                          {kAmpSustainId, 0.8},          // sustain 0.8
                          {kAmpReleaseId, 0.76902},      // 700 ms
                          {kFieldImageModeId, 0.5},      // SPREAD
                          {kFieldSpreadId, 0.9},         // spread 0.9
                          {kMassId, 0.45},               // Mass 45
                          {kAnchorWaveformId, 0.0},      // Sine
                          {kAnchorModeId, 0.0},          // LOCK
                          {kAnchorLevelId, 0.55},        // level 0.55
                          {kAnchorFoldFeedId, 0.0},      // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.75},  // body curvature 0.75
                          {kCoreShiftId, 0.55},          // shift 0.1
                          {kFieldGravityId, 0.4}},       // gravity 0.4
            .endpointB = {{kCoreDepthId, 0.4},           // depth 0.4
                          {kCoreBodyId, 0.7},            // body 0.7
                          {kCoreEdgeId, 0.35},           // edge 0.35
                          {kCoreExcitationMixId, 0.0},   // excitation mix 0
                          {kFoldAmountId, 0.0},          // Harmonic Fold 0
                          {kFoldShapeId, 0.0},           // SHAPE 0
                          {kCharacterId, 0.0},           // Tube
                          {kCharacterAmountId, 0.0},     // Amount 0
                          {kCharacterToneId, 0.5},       // Tone 0.5
                          {kAmpAttackId, 0.460206},      // 20 ms
                          {kAmpDecayId, 0.76902},        // 700 ms
                          {kAmpSustainId, 0.8},          // sustain 0.8
                          {kAmpReleaseId, 0.76902},      // 700 ms
                          {kFieldImageModeId, 0.5},      // SPREAD
                          {kFieldSpreadId, 0.9},         // spread 0.9
                          {kMassId, 0.45},               // Mass 45
                          {kAnchorWaveformId, 0.0},      // Sine
                          {kAnchorModeId, 0.0},          // LOCK
                          {kAnchorLevelId, 0.55},        // level 0.55
                          {kAnchorFoldFeedId, 0.0},      // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.75},  // body curvature 0.75
                          {kCoreShiftId, 0.55},          // shift 0.1
                          {kFieldGravityId, 0.4}},       // gravity 0.4
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Choir",
            .nameB = "Choir Low",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 32: AT-32 Gravity (plan 7 row 32) ----
        ProfundumPresetDef{
            .name = "Gravity Well",
            .category = "Resonant",
            .description = "Field Gravity: Gravity at 1 pulls the low band of a SPREAD image at spread 1 back to the centre.",
            .primary = C::AT32,
            .secondaries = {},
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
                          {kMassId, 0.5},                     // Mass 50
                          {kAnchorWaveformId, 0.0},           // Sine
                          {kAnchorModeId, 0.0},               // LOCK
                          {kAnchorLevelId, 0.5},              // level 0.5
                          {kAnchorFoldFeedId, 0.0},           // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.7},        // body curvature 0.7
                          {kFieldGravityId, 1.0}},            // gravity 1
            .endpointB = {{kCoreDepthId, 0.5},                // depth 0.5
                          {kCoreBodyId, 0.4},                 // body 0.4
                          {kCoreEdgeId, 0.5},                 // edge 0.5
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
                          {kMassId, 0.5},                     // Mass 50
                          {kAnchorWaveformId, 0.0},           // Sine
                          {kAnchorModeId, 0.0},               // LOCK
                          {kAnchorLevelId, 0.5},              // level 0.5
                          {kAnchorFoldFeedId, 0.0},           // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.7},        // body curvature 0.7
                          {kFieldGravityId, 1.0}},            // gravity 1
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Deep Well",
            .nameB = "Darker Well",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 33: AT-33 Field Motion (plan 7 row 33) ----
        // P-4: SPREAD, spread 1 and Field Motion depth 1 on endpoint A. Field Motion source DRIFT (the probe
        // base's) at depth 1, so seedDependent (C-7, plan 6 rule 9).
        ProfundumPresetDef{
            .name = "Orbiting Field",
            .category = "Animated",
            .description = "Field Motion: Field Motion at depth 1 orbits a SPREAD image at spread 1 around a harmonic core.",
            .primary = C::AT33,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 0.55},           // depth 0.55
                          {kCoreBodyId, 0.4},             // body 0.4
                          {kCoreEdgeId, 0.6},             // edge 0.6
                          {kCoreExcitationMixId, 0.5},    // excitation mix 0.5
                          {kFoldAmountId, 0.0},           // Harmonic Fold 0
                          {kFoldShapeId, 0.0},            // SHAPE 0
                          {kCharacterId, 0.0},            // Tube
                          {kCharacterAmountId, 0.0},      // Amount 0
                          {kCharacterToneId, 0.5},        // Tone 0.5
                          {kAmpAttackId, 0.260206},       // 2 ms
                          {kAmpDecayId, 0.835218},        // 1500 ms
                          {kAmpSustainId, 1.0},           // sustain 1
                          {kAmpReleaseId, 0.75563},       // 600 ms
                          {kFieldImageModeId, 0.5},       // SPREAD
                          {kFieldSpreadId, 1.0},          // spread 1
                          {kMassId, 0.5},                 // Mass 50
                          {kAnchorWaveformId, 0.0},       // Sine
                          {kAnchorModeId, 0.0},           // LOCK
                          {kAnchorLevelId, 0.5},          // level 0.5
                          {kAnchorFoldFeedId, 0.0},       // Anchor Fold Feed 0
                          {kCoreShiftId, 0.35},           // shift -0.3
                          {kCoreExcitationDriveId, 0.7},  // drive 0.7
                          {kFieldMotionDepthId, 1.0},     // Field Motion depth 1
                          {kFieldMotionRateId, 0.5},      // Field Motion rate 0.5
                          {kFieldMotionSourceId, 0.0}},   // Field Motion DRIFT
            .endpointB = {{kCoreDepthId, 0.55},           // depth 0.55
                          {kCoreBodyId, 0.4},             // body 0.4
                          {kCoreEdgeId, 0.6},             // edge 0.6
                          {kCoreExcitationMixId, 0.5},    // excitation mix 0.5
                          {kFoldAmountId, 0.0},           // Harmonic Fold 0
                          {kFoldShapeId, 0.0},            // SHAPE 0
                          {kCharacterId, 0.0},            // Tube
                          {kCharacterAmountId, 0.0},      // Amount 0
                          {kCharacterToneId, 0.5},        // Tone 0.5
                          {kAmpAttackId, 0.260206},       // 2 ms
                          {kAmpDecayId, 0.835218},        // 1500 ms
                          {kAmpSustainId, 1.0},           // sustain 1
                          {kAmpReleaseId, 0.75563},       // 600 ms
                          {kFieldImageModeId, 0.5},       // SPREAD
                          {kFieldSpreadId, 1.0},          // spread 1
                          {kMassId, 0.5},                 // Mass 50
                          {kAnchorWaveformId, 0.0},       // Sine
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
            .nameB = "Slower Orbit",
            .seed = kFactoryDefaultSeed,
            .seedDependent = true,
        },
        // ---- 34: AT-34 CYCLE to Mass (plan 7 row 34) ----
        // T027 FR-061 level: sustain 1 -> 0.5 (-6.55 -> -11.18 dBFS).
        ProfundumPresetDef{
            .name = "Mass Tide",
            .category = "Animated",
            .description = "Motion Cycle to Mass: a slow CYCLE swells and drains the Mass under an Anchor at 0.5.",
            .primary = C::AT34,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 1.0},             // depth 1
                          {kCoreBodyId, 0.0},              // body 0
                          {kCoreEdgeId, 0.0},              // edge 0
                          {kCoreExcitationMixId, 0.0},     // excitation mix 0
                          {kFoldAmountId, 0.0},            // Harmonic Fold 0
                          {kFoldShapeId, 0.0},             // SHAPE 0
                          {kCharacterId, 0.0},             // Tube
                          {kCharacterAmountId, 0.0},       // Amount 0
                          {kCharacterToneId, 0.5},         // Tone 0.5
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
                          {kMotionPolarityId, 0.0}},       // Bipolar
            .endpointB = {{kCoreDepthId, 1.0},             // depth 1
                          {kCoreBodyId, 0.0},              // body 0
                          {kCoreEdgeId, 0.0},              // edge 0
                          {kCoreExcitationMixId, 0.0},     // excitation mix 0
                          {kFoldAmountId, 0.0},            // Harmonic Fold 0
                          {kFoldShapeId, 0.0},             // SHAPE 0
                          {kCharacterId, 0.0},             // Tube
                          {kCharacterAmountId, 0.0},       // Amount 0
                          {kCharacterToneId, 0.5},         // Tone 0.5
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
                          {kMotionCycleRateId, 0.485321},  // 0.4 Hz
                          {kMotionCycleShapeId, 0.0},      // shape Sine
                          {kMotionPolarityId, 0.0}},       // Bipolar
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Tide",
            .nameB = "Faster Tide",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 35: AT-35 to Depth (plan 7 row 35) ----
        ProfundumPresetDef{
            .name = "Breathing Depth",
            .category = "Animated",
            .description = "Motion Cycle to Depth: a CYCLE breathes the core Depth of a heavy harmonic body in and out.",
            .primary = C::AT35,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 0.5},                // depth 0.5
                          {kCoreBodyId, 0.3},                 // body 0.3
                          {kCoreEdgeId, 1.0},                 // edge 1
                          {kCoreShiftId, 0.0},                // shift -1
                          {kCoreExcitationMixId, 1.0},        // excitation mix 1
                          {kCoreExcitationDriveId, 1.0},      // drive 1
                          {kCoreExcitationAsymmetryId, 0.5},  // asymmetry 0.5
                          {kFoldAmountId, 0.6},               // Harmonic Fold 60
                          {kFoldShapeId, 0.5},                // SHAPE 0.5
                          {kCharacterId, 0.2},                // Fold
                          {kCharacterAmountId, 0.5},          // Amount 0.5
                          {kCharacterToneId, 0.6},            // Tone 0.6
                          {kAmpAttackId, 0.2},                // 1 ms
                          {kAmpDecayId, 0.835218},            // 1500 ms
                          {kAmpSustainId, 1.0},               // sustain 1
                          {kAmpReleaseId, 0.780618},          // 800 ms
                          {kFieldImageModeId, 0.5},           // SPREAD
                          {kFieldSpreadId, 0.7},              // spread 0.7
                          {kMassId, 0.5},                     // Mass 50
                          {kAnchorWaveformId, 0.0},           // Sine
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
            .endpointB = {{kCoreDepthId, 0.5},                // depth 0.5
                          {kCoreBodyId, 0.3},                 // body 0.3
                          {kCoreEdgeId, 1.0},                 // edge 1
                          {kCoreShiftId, 0.0},                // shift -1
                          {kCoreExcitationMixId, 1.0},        // excitation mix 1
                          {kCoreExcitationDriveId, 1.0},      // drive 1
                          {kCoreExcitationAsymmetryId, 0.5},  // asymmetry 0.5
                          {kFoldAmountId, 0.6},               // Harmonic Fold 60
                          {kFoldShapeId, 0.5},                // SHAPE 0.5
                          {kCharacterId, 0.2},                // Fold
                          {kCharacterAmountId, 0.5},          // Amount 0.5
                          {kCharacterToneId, 0.6},            // Tone 0.6
                          {kAmpAttackId, 0.2},                // 1 ms
                          {kAmpDecayId, 0.835218},            // 1500 ms
                          {kAmpSustainId, 1.0},               // sustain 1
                          {kAmpReleaseId, 0.780618},          // 800 ms
                          {kFieldImageModeId, 0.5},           // SPREAD
                          {kFieldSpreadId, 0.7},              // spread 0.7
                          {kMassId, 0.5},                     // Mass 50
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
            .nameB = "Slow Breath",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 36: AT-36 to Body (plan 7 row 36) ----
        ProfundumPresetDef{
            .name = "Body Wave",
            .category = "Animated",
            .description = "Motion Cycle to Body: a CYCLE sweeps the Body of a harmonic core, a slow wave through the mids.",
            .primary = C::AT36,
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
                          {kFieldImageModeId, 0.5},           // SPREAD
                          {kFieldSpreadId, 0.8},              // spread 0.8
                          {kMassId, 0.5},                     // Mass 50
                          {kAnchorWaveformId, 0.0},           // Sine
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
                          {kAmpDecayId, 0.835218},            // 1500 ms
                          {kAmpSustainId, 1.0},               // sustain 1
                          {kAmpReleaseId, 0.780618},          // 800 ms
                          {kFieldImageModeId, 0.5},           // SPREAD
                          {kFieldSpreadId, 0.8},              // spread 0.8
                          {kMassId, 0.5},                     // Mass 50
                          {kAnchorWaveformId, 0.0},           // Sine
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
            .nameB = "Slow Body Wave",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 37: AT-37 to Edge (plan 7 row 37) ----
        // T027 FR-061 level: sustain 0.9 -> 1, depth 0 -> 0.1 (-15.55 -> -12.67 dBFS).
        // The probe's AT-37 base: A_s with recipe kSawAnchor at edge 0.5 (the per-row setting).
        ProfundumPresetDef{
            .name = "Edge Cycle",
            .category = "Animated",
            .description = "Motion Cycle to Edge: a saw-like recipe whose Edge a CYCLE opens and closes.",
            .primary = C::AT37,
            .secondaries = {},
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
                          {kFieldImageModeId, 0.5},        // SPREAD
                          {kFieldSpreadId, 0.5},           // spread 0.5
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
                          {kMotionPolarityId, 0.0}},       // Bipolar
            .endpointB = {{kCoreDepthId, 0.1},             // depth 0.1
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
                          {kFieldImageModeId, 0.5},        // SPREAD
                          {kFieldSpreadId, 0.5},           // spread 0.5
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
                          {kMotionCycleRateId, 0.514679},  // 0.5 Hz
                          {kMotionCycleShapeId, 0.0},      // shape Sine
                          {kMotionPolarityId, 0.0}},       // Bipolar
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Edge Sweep",
            .nameB = "Slow Edge Sweep",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 38: AT-38 to Shift (plan 7 row 38) ----
        // The probe's AT-38 base B_s and rate (0.30 / 0.30 oct against 0.25 oct at 0.1 Hz), kept as it is.
        ProfundumPresetDef{
            .name = "Shifting Spectrum",
            .category = "Animated",
            .description = "Motion Cycle to Shift: a slow CYCLE drifts the spectral Shift of a heavy harmonic body up and down.",
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
                          {kMotionCycleRateId, 0.35628},      // 0.15 Hz
                          {kMotionCycleShapeId, 0.0},         // shape Sine
                          {kMotionPolarityId, 0.0}},          // Bipolar
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Shifting",
            .nameB = "Shifting Faster",
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
            .description = "Motion Env to Mass: a Unipolar ENV, 1000 ms up and 1000 ms down, swells the Mass from 0 at every note "
                           "and lets it settle.",
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
                          {kAmpDecayId, 0.635218},      // 150 ms
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
            .endpointB = {{kCoreDepthId, 1.0},           // depth 1
                          {kCoreBodyId, 0.0},            // body 0
                          {kCoreEdgeId, 0.0},            // edge 0
                          {kCoreExcitationMixId, 0.0},   // excitation mix 0
                          {kFoldAmountId, 0.0},          // Harmonic Fold 0
                          {kFoldShapeId, 0.0},           // SHAPE 0
                          {kCharacterId, 0.0},           // Tube
                          {kCharacterAmountId, 0.0},     // Amount 0
                          {kCharacterToneId, 0.5},       // Tone 0.5
                          {kAmpAttackId, 0.2},           // 1 ms
                          {kAmpDecayId, 0.635218},      // 150 ms
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
                          {kMotionEnvDecayId, 0.76902}}, // ENV decay 700 ms
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Mass Kick",
            .nameB = "Shorter Kick",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 40: AT-40 DRIFT to Mass (plan 7 row 40) ----
        ProfundumPresetDef{
            .name = "Drifting Weight",
            .category = "Animated",
            .description = "Motion Drift to Mass: a DRIFT source at smoothness 0.5 wanders the Mass, different under every seed.",
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
            .endpointB = {{kCoreDepthId, 1.0},              // depth 1
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
                          {kMotionDriftSmoothnessId, 0.7},  // smoothness 0.7
                          {kMotionPolarityId, 0.0}},        // Bipolar
            .controls = {{kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Drifting",
            .nameB = "Smoother Drift",
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
            .endpointA = {{kCoreDepthId, 0.85},         // depth 0.85
                          {kCoreBodyId, 0.2},           // body 0.2
                          {kCoreEdgeId, 0.1},           // edge 0.1
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
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
            .description = "NoteMemory AlwaysRetrigger: MONO with Retrigger Reset, every note restrikes a resonant body from the "
                           "start.",
            .primary = C::AT43,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 0.3},          // depth 0.3
                          {kCoreBodyId, 0.8},           // body 0.8
                          {kCoreEdgeId, 0.4},           // edge 0.4
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.0},    // Amount 0
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.2},          // 1 ms
                          {kAmpDecayId, 0.720412},      // 400 ms
                          {kAmpSustainId, 0.8},         // sustain 0.8
                          {kAmpReleaseId, 0.695424},    // 300 ms
                          {kFieldImageModeId, 0.5},     // SPREAD
                          {kFieldSpreadId, 0.4},        // spread 0.4
                          {kMassId, 0.5},               // Mass 50
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.8},        // level 0.8
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.9},  // body curvature 0.9
                          {kCoreBodyEmphasisId, 0.5},   // body emphasis -0.5
                          {kCoreShiftId, 0.6}},         // shift 0.2
            .endpointB = {{kCoreDepthId, 0.3},          // depth 0.3
                          {kCoreBodyId, 0.65},          // body 0.65
                          {kCoreEdgeId, 0.4},           // edge 0.4
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.0},    // Amount 0
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.2},          // 1 ms
                          {kAmpDecayId, 0.720412},      // 400 ms
                          {kAmpSustainId, 0.8},         // sustain 0.8
                          {kAmpReleaseId, 0.695424},    // 300 ms
                          {kFieldImageModeId, 0.5},     // SPREAD
                          {kFieldSpreadId, 0.4},        // spread 0.4
                          {kMassId, 0.5},               // Mass 50
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.8},        // level 0.8
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.9},  // body curvature 0.9
                          {kCoreBodyEmphasisId, 0.5},   // body emphasis -0.5
                          {kCoreShiftId, 0.6}},         // shift 0.2
            .controls = {{kNoteMemoryId, 0.0},     // Note Memory AlwaysRetrigger
                         {kNoteRetriggerId, 0.0},  // Retrigger Reset
                         {kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Restruck",
            .nameB = "Restruck Body",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 44: AT-44 Legato (plan 7 row 44) ----
        ProfundumPresetDef{
            .name = "Legato Line",
            .category = "Resonant",
            .description = "NoteMemory Legato: MONO with Retrigger Reset; overlapping notes carry the phase on, a joined line.",
            .primary = C::AT44,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 0.7},          // depth 0.7
                          {kCoreBodyId, 0.45},          // body 0.45
                          {kCoreEdgeId, 0.2},           // edge 0.2
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.0},    // Amount 0
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.380618},     // 8 ms
                          {kAmpDecayId, 0.75563},       // 600 ms
                          {kAmpSustainId, 0.9},         // sustain 0.9
                          {kAmpReleaseId, 0.730643},    // 450 ms
                          {kFieldImageModeId, 0.0},     // SOLID
                          {kFieldSpreadId, 0.0},        // spread 0
                          {kMassId, 0.55},              // Mass 55
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.7},        // level 0.7
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kCoreBodyEmphasisId, 0.6},   // body emphasis -0.4
                          {kCoreShiftId, 0.4}},         // shift -0.2
            .endpointB = {{kCoreDepthId, 0.7},          // depth 0.7
                          {kCoreBodyId, 0.45},          // body 0.45
                          {kCoreEdgeId, 0.35},          // edge 0.35
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.0},    // Amount 0
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.380618},     // 8 ms
                          {kAmpDecayId, 0.75563},       // 600 ms
                          {kAmpSustainId, 0.9},         // sustain 0.9
                          {kAmpReleaseId, 0.730643},    // 450 ms
                          {kFieldImageModeId, 0.0},     // SOLID
                          {kFieldSpreadId, 0.0},        // spread 0
                          {kMassId, 0.55},              // Mass 55
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.7},        // level 0.7
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kCoreBodyEmphasisId, 0.6},   // body emphasis -0.4
                          {kCoreShiftId, 0.4}},         // shift -0.2
            .controls = {{kNoteMemoryId, 1.0 / 3.0},  // Note Memory Legato
                         {kNoteRetriggerId, 0.0},     // Retrigger Reset
                         {kMorphPositionId, 0.0}},    // p = 0
            .nameA = "Joined",
            .nameB = "Joined Edge",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 45: AT-45 EnvRetriggerOnly (plan 7 row 45) ----
        // T027 FR-061 level: sustain 0.7 -> 1, depth 0.2 -> 0.5 (-23.93 -> -11.59 dBFS).
        ProfundumPresetDef{
            .name = "Envelope Restart",
            .category = "Harmonic",
            .description = "NoteMemory EnvelopeRetriggerOnly: MONO with Retrigger Reset; each note restarts the envelope but not "
                           "the oscillator.",
            .primary = C::AT45,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 0.5},            // depth 0.5
                          {kCoreBodyId, 0.3},             // body 0.3
                          {kCoreEdgeId, 0.8},             // edge 0.8
                          {kCoreExcitationMixId, 0.6},    // excitation mix 0.6
                          {kFoldAmountId, 0.0},           // Harmonic Fold 0
                          {kFoldShapeId, 0.0},            // SHAPE 0
                          {kCharacterId, 0.0},            // Tube
                          {kCharacterAmountId, 0.0},      // Amount 0
                          {kCharacterToneId, 0.5},        // Tone 0.5
                          {kAmpAttackId, 0.2},            // 1 ms
                          {kAmpDecayId, 0.739794},        // 500 ms
                          {kAmpSustainId, 1.0},           // sustain 1
                          {kAmpReleaseId, 0.720412},      // 400 ms
                          {kFieldImageModeId, 0.0},       // SOLID
                          {kFieldSpreadId, 0.0},          // spread 0
                          {kMassId, 0.5},                 // Mass 50
                          {kAnchorWaveformId, 0.0},       // Sine
                          {kAnchorModeId, 0.0},           // LOCK
                          {kAnchorLevelId, 0.6},          // level 0.6
                          {kAnchorFoldFeedId, 0.0},       // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.2},    // body curvature 0.2
                          {kCoreShiftId, 0.75},           // shift 0.5
                          {kCoreExcitationDriveId, 0.7}}, // drive 0.7
            .endpointB = {{kCoreDepthId, 0.5},            // depth 0.5
                          {kCoreBodyId, 0.3},             // body 0.3
                          {kCoreEdgeId, 0.65},            // edge 0.65
                          {kCoreExcitationMixId, 0.6},    // excitation mix 0.6
                          {kFoldAmountId, 0.0},           // Harmonic Fold 0
                          {kFoldShapeId, 0.0},            // SHAPE 0
                          {kCharacterId, 0.0},            // Tube
                          {kCharacterAmountId, 0.0},      // Amount 0
                          {kCharacterToneId, 0.5},        // Tone 0.5
                          {kAmpAttackId, 0.2},            // 1 ms
                          {kAmpDecayId, 0.739794},        // 500 ms
                          {kAmpSustainId, 1.0},           // sustain 1
                          {kAmpReleaseId, 0.720412},      // 400 ms
                          {kFieldImageModeId, 0.0},       // SOLID
                          {kFieldSpreadId, 0.0},          // spread 0
                          {kMassId, 0.5},                 // Mass 50
                          {kAnchorWaveformId, 0.0},       // Sine
                          {kAnchorModeId, 0.0},           // LOCK
                          {kAnchorLevelId, 0.6},          // level 0.6
                          {kAnchorFoldFeedId, 0.0},       // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.2},    // body curvature 0.2
                          {kCoreShiftId, 0.75},           // shift 0.5
                          {kCoreExcitationDriveId, 0.7}}, // drive 0.7
            .controls = {{kNoteMemoryId, 2.0 / 3.0},  // Note Memory EnvelopeRetriggerOnly
                         {kNoteRetriggerId, 0.0},     // Retrigger Reset
                         {kMorphPositionId, 0.0}},    // p = 0
            .nameA = "Restarting",
            .nameB = "Restarting Bright",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 46: AT-46 OscContinuous (plan 7 row 46) ----
        ProfundumPresetDef{
            .name = "Continuous Phase",
            .category = "Sub",
            .description = "NoteMemory OscillatorContinuous: MONO with Retrigger Reset; the oscillator runs on through every "
                           "note.",
            .primary = C::AT46,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 0.6},          // depth 0.6
                          {kCoreBodyId, 0.5},           // body 0.5
                          {kCoreEdgeId, 0.1},           // edge 0.1
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.0},    // Amount 0
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.339794},     // 5 ms
                          {kAmpDecayId, 0.695424},      // 300 ms
                          {kAmpSustainId, 1.0},         // sustain 1
                          {kAmpReleaseId, 0.708814},    // 350 ms
                          {kFieldImageModeId, 0.0},     // SOLID
                          {kFieldSpreadId, 0.0},        // spread 0
                          {kMassId, 0.6},               // Mass 60
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 1.0},        // level 1
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kCoreBodyCurvatureId, 0.3},  // body curvature 0.3
                          {kCoreShiftId, 0.35}},        // shift -0.3
            .endpointB = {{kCoreDepthId, 0.6},          // depth 0.6
                          {kCoreBodyId, 0.5},           // body 0.5
                          {kCoreEdgeId, 0.1},           // edge 0.1
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFoldAmountId, 0.0},         // Harmonic Fold 0
                          {kFoldShapeId, 0.0},          // SHAPE 0
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.0},    // Amount 0
                          {kCharacterToneId, 0.5},      // Tone 0.5
                          {kAmpAttackId, 0.339794},     // 5 ms
                          {kAmpDecayId, 0.695424},      // 300 ms
                          {kAmpSustainId, 1.0},         // sustain 1
                          {kAmpReleaseId, 0.708814},    // 350 ms
                          {kFieldImageModeId, 0.0},     // SOLID
                          {kFieldSpreadId, 0.0},        // spread 0
                          {kMassId, 0.7},               // Mass 70
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
            .nameB = "Running Heavy",
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
            .description = "Macros Weight: a clean sine sub with a soft 20 ms attack under a WIDE image, the Weight macro stored "
                           "at 0.6.",
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
            .endpointB = {{kCoreDepthId, 1.0},          // depth 1
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
                          {kAnchorLevelId, 0.9},        // level 0.9
                          {kAnchorFoldFeedId, 0.0}},    // Anchor Fold Feed 0
            .controls = {{kMacroWeightId, 0.6},    // Weight 0.6
                         {kMorphPositionId, 0.0}}, // p = 0
            .nameA = "Weighted",
            .nameB = "Weighted Light",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 48: AT-48 Dark Bright (plan 7 row 48) ----
        // A distinct endpoint B (plan 7). Endpoint A is the probe's AT-48 base B_s.
        ProfundumPresetDef{
            .name = "Dark To Bright",
            .category = "Harmonic",
            .description = "Macros Dark Bright: a heavy harmonic body with the Dark Bright macro at 0.5; endpoint B is a darker, "
                           "rounder twin.",
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
            .endpointB = {{kCoreDepthId, 0.5},                // depth 0.5
                          {kCoreBodyId, 0.5},                 // body 0.5
                          {kCoreEdgeId, 0.4},                 // edge 0.4
                          {kCoreShiftId, 0.0},                // shift -1
                          {kCoreExcitationMixId, 1.0},        // excitation mix 1
                          {kCoreExcitationDriveId, 1.0},      // drive 1
                          {kCoreExcitationAsymmetryId, 0.5},  // asymmetry 0.5
                          {kFoldAmountId, 0.8},               // Harmonic Fold 80
                          {kFoldShapeId, 0.5},                // SHAPE 0.5
                          {kCharacterId, 0.0},                // Tube
                          {kCharacterAmountId, 0.4},          // Amount 0.4
                          {kCharacterToneId, 0.4},            // Tone 0.4
                          {kAmpAttackId, 0.2},                // 1 ms
                          {kAmpDecayId, 0.835218},            // 1500 ms
                          {kAmpSustainId, 1.0},               // sustain 1
                          {kAmpReleaseId, 0.780618},          // 800 ms
                          {kFieldImageModeId, 0.5},           // SPREAD
                          {kFieldSpreadId, 0.6},              // spread 0.6
                          {kMassId, 0.5},                     // Mass 50
                          {kAnchorWaveformId, 0.0},           // Sine
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
            .description = "Macros Tight Loose: a half-sustain body with the Tight Loose macro at 0.5, loose ends around a SPREAD "
                           "image.",
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
            .endpointB = {{kCoreDepthId, 1.0},          // depth 1
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
                          {kFieldSpreadId, 0.6},        // spread 0.6
                          {kMassId, 0.5},               // Mass 50
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.8},        // level 0.8
                          {kAnchorFoldFeedId, 0.0},     // Anchor Fold Feed 0
                          {kFieldGravityId, 0.2}},      // gravity 0.2
            .controls = {{kMacroTightLooseId, 0.5},  // Tight Loose 0.5
                         {kMorphPositionId, 0.0}},   // p = 0
            .nameA = "Loose",
            .nameB = "Loose Narrow",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 50: AT-50 Clean Violent (plan 7 row 50) ----
        ProfundumPresetDef{
            .name = "Clean To Violent",
            .category = "Driven",
            .description = "Macros Clean Violent: a lightly driven core with the Clean Violent macro stored at 0.4, with room "
                           "to turn toward violent.",
            .primary = C::AT50,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 0.8},            // depth 0.8
                          {kCoreBodyId, 0.2},             // body 0.2
                          {kCoreEdgeId, 0.3},             // edge 0.3
                          {kCoreExcitationMixId, 0.3},    // excitation mix 0.3
                          {kFoldAmountId, 0.0},           // Harmonic Fold 0
                          {kFoldShapeId, 0.0},            // SHAPE 0
                          {kCharacterId, 0.0},            // Tube
                          {kCharacterAmountId, 0.2},      // Amount 0.2
                          {kCharacterToneId, 0.5},        // Tone 0.5
                          {kAmpAttackId, 0.260206},       // 2 ms
                          {kAmpDecayId, 0.695424},        // 300 ms
                          {kAmpSustainId, 0.9},           // sustain 0.9
                          {kAmpReleaseId, 0.679588},      // 250 ms
                          {kFieldImageModeId, 0.5},       // SPREAD
                          {kFieldSpreadId, 0.5},          // spread 0.5
                          {kMassId, 0.55},                // Mass 55
                          {kAnchorWaveformId, 0.0},       // Sine
                          {kAnchorModeId, 0.0},           // LOCK
                          {kAnchorLevelId, 0.8},          // level 0.8
                          {kAnchorFoldFeedId, 0.0},       // Anchor Fold Feed 0
                          {kCoreExcitationDriveId, 0.5}}, // drive 0.5
            .endpointB = {{kCoreDepthId, 0.8},            // depth 0.8
                          {kCoreBodyId, 0.2},             // body 0.2
                          {kCoreEdgeId, 0.3},             // edge 0.3
                          {kCoreExcitationMixId, 0.3},    // excitation mix 0.3
                          {kFoldAmountId, 0.0},           // Harmonic Fold 0
                          {kFoldShapeId, 0.0},            // SHAPE 0
                          {kCharacterId, 0.0},            // Tube
                          {kCharacterAmountId, 0.35},     // Amount 0.35
                          {kCharacterToneId, 0.5},        // Tone 0.5
                          {kAmpAttackId, 0.260206},       // 2 ms
                          {kAmpDecayId, 0.695424},        // 300 ms
                          {kAmpSustainId, 0.9},           // sustain 0.9
                          {kAmpReleaseId, 0.679588},      // 250 ms
                          {kFieldImageModeId, 0.5},       // SPREAD
                          {kFieldSpreadId, 0.5},          // spread 0.5
                          {kMassId, 0.55},                // Mass 55
                          {kAnchorWaveformId, 0.0},       // Sine
                          {kAnchorModeId, 0.0},           // LOCK
                          {kAnchorLevelId, 0.8},          // level 0.8
                          {kAnchorFoldFeedId, 0.0},       // Anchor Fold Feed 0
                          {kCoreExcitationDriveId, 0.5}}, // drive 0.5
            .controls = {{kMacroCleanViolentId, 0.4},  // Clean Violent 0.4
                         {kMorphPositionId, 0.0}},     // p = 0
            .nameA = "Restrained",
            .nameB = "Straining",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 51: R-SOLID (plan 7 row 51) ----
        ProfundumPresetDef{
            .name = "Mono Bedrock",
            .category = "Sub",
            .description = "Reference Solid: a SOLID image on both endpoints over a warm Tube 0.4 sub, the mono "
                           "floor that every widened preset is heard against.",
            .primary = C::RSolid,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 0.7},          // recipe depth 0.7
                          {kCoreBodyId, 0.35},          // body 0.35
                          {kCoreEdgeId, 0.2},           // edge 0.2
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.4},    // Amount 0.4
                          {kCharacterToneId, 0.4},      // Tone 0.4
                          {kFieldImageModeId, 0.0},     // SOLID
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.8},        // level 0.8
                          {kMassId, 0.6},               // Mass 60
                          {kAmpAttackId, 0.339794},     // 5 ms
                          {kAmpDecayId, 0.695424},      // 300 ms
                          {kAmpSustainId, 0.9},         // sustain 0.9
                          {kAmpReleaseId, 0.720412}},   // 400 ms
            .endpointB = {{kCoreDepthId, 0.7},
                          {kCoreBodyId, 0.35},
                          {kCoreEdgeId, 0.35},          // edge 0.35
                          {kCharacterId, 0.0},          // Tube
                          {kCharacterAmountId, 0.6},    // Amount 0.6: B leans into the valve
                          {kCharacterToneId, 0.4},
                          {kFieldImageModeId, 0.0},     // SOLID (R-SOLID: both endpoints)
                          {kAnchorWaveformId, 0.0},
                          {kAnchorModeId, 0.0},
                          {kAnchorLevelId, 0.8},
                          {kMassId, 0.6},
                          {kAmpAttackId, 0.339794},
                          {kAmpDecayId, 0.695424},
                          {kAmpSustainId, 0.9},
                          {kAmpReleaseId, 0.720412}},
            .controls = {{kMorphPositionId, 0.0}},
            .nameA = "Solid Ground",
            .nameB = "Solid Heat",
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
            .description = "Reference Lock: the Anchor held in LOCK under a Triangle waveform with a SPREAD image, "
                           "a steady floor with no punch.",
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
            .endpointB = {{kCoreDepthId, 1.0},
                          {kCoreBodyId, 0.0},
                          {kCoreEdgeId, 0.15},          // edge 0.15
                          {kCoreExcitationMixId, 0.0},
                          {kFieldImageModeId, 0.5},     // SPREAD
                          {kFieldSpreadId, 1.0},        // spread 1
                          {kAnchorWaveformId, 0.25},    // Triangle (AT-14 claim: both endpoints)
                          {kAnchorModeId, 0.0},         // LOCK (R-LOCK: both endpoints)
                          {kAnchorLevelId, 1.0},
                          {kAmpAttackId, 0.2},
                          {kAmpDecayId, 0.635218},
                          {kAmpSustainId, 0.6},
                          {kAmpReleaseId, 0.695424}},
            .controls = {{kMorphPositionId, 0.0}},      // p = 0 (P-3: the R-LOCK arm reads A(P))
            .nameA = "Triangle Lock",
            .nameB = "Triangle Open",
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
            .description = "Reference Sine: a pure Sine Anchor on both endpoints under a WIDE image; endpoint B "
                           "adds a heavy harmonic body to morph into.",
            .primary = C::RSine,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 1.0},          // sine recipe: depth 1
                          {kCoreBodyId, 0.0},           // body 0
                          {kCoreEdgeId, 0.0},           // edge 0
                          {kCoreExcitationMixId, 0.0},  // excitation mix 0
                          {kFieldImageModeId, 1.0},     // WIDE
                          {kFieldSpreadId, 1.0},        // spread 1
                          {kAnchorWaveformId, 0.0},     // Sine
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 1.0},        // level 1
                          {kAmpAttackId, 0.380618},     // 8 ms
                          {kAmpDecayId, 0.720412},      // 400 ms
                          {kAmpSustainId, 0.85},        // sustain 0.85
                          {kAmpReleaseId, 0.739794}},   // 500 ms
            .endpointB = {{kCoreDepthId, 0.5},          // kHeavy recipe: depth 0.5
                          {kCoreBodyId, 0.3},           // body 0.3
                          {kCoreEdgeId, 0.6},           // edge 0.6
                          {kCoreShiftId, 0.0},          // shift -1
                          {kCoreExcitationMixId, 1.0},  // excitation mix 1
                          {kCoreExcitationDriveId, 0.6},  // drive 0.6
                          {kFieldImageModeId, 1.0},     // WIDE
                          {kFieldSpreadId, 1.0},        // spread 1
                          {kAnchorWaveformId, 0.0},     // Sine (R-SINE: both endpoints)
                          {kAnchorModeId, 0.0},         // LOCK
                          {kAnchorLevelId, 0.7},        // level 0.7
                          {kAmpAttackId, 0.380618},
                          {kAmpDecayId, 0.720412},
                          {kAmpSustainId, 0.85},
                          {kAmpReleaseId, 0.739794}},
            .controls = {{kMorphPositionId, 0.0}},
            .nameA = "Pure Sine",
            .nameB = "Heavy Body",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 54: AT-31 in Poly (Phase 13b, FR-066; endpoint A from Spread Choir with the TuneP_Poly edits) ----
        ProfundumPresetDef{
            .name = "Poly Spread",
            .category = "Harmonic",
            .description = "Field Spread: a hollow tube-coloured body spread wide in SPREAD, played as chords in Poly.",
            .primary = C::AT31,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 0.4},
                          {kCoreBodyId, 0.5},
                          {kCoreEdgeId, 0.6},
                          {kCoreExcitationMixId, 0.0},
                          {kFoldAmountId, 0.0},
                          {kFoldShapeId, 0.0},
                          {kCharacterId, 0.0},
                          {kCharacterAmountId, 0.4},
                          {kCharacterToneId, 0.6},
                          {kAmpAttackId, 0.3},
                          {kAmpDecayId, 0.76902},
                          {kAmpSustainId, 0.8},
                          {kAmpReleaseId, 0.76902},
                          {kFieldImageModeId, 0.5},
                          {kFieldSpreadId, 0.9},
                          {kMassId, 0.5},
                          {kAnchorWaveformId, 0.0},
                          {kAnchorModeId, 0.0},
                          {kAnchorLevelId, 0.55},
                          {kAnchorFoldFeedId, 0.0},
                          {kCoreBodyCurvatureId, 0.75},
                          {kCoreShiftId, 0.35},
                          {kFieldGravityId, 0.4}},
            .endpointB = {{kCoreDepthId, 0.4},
                          {kCoreBodyId, 0.5},
                          {kCoreEdgeId, 0.6},
                          {kCoreExcitationMixId, 0.0},
                          {kFoldAmountId, 0.0},
                          {kFoldShapeId, 0.0},
                          {kCharacterId, 0.0},
                          {kCharacterAmountId, 0.4},
                          {kCharacterToneId, 0.6},
                          {kAmpAttackId, 0.3},
                          {kAmpDecayId, 0.76902},
                          {kAmpSustainId, 0.8},
                          {kAmpReleaseId, 0.76902},
                          {kFieldImageModeId, 0.5},
                          {kFieldSpreadId, 0.9},
                          {kMassId, 0.5},
                          {kAnchorWaveformId, 0.0},
                          {kAnchorModeId, 0.0},
                          {kAnchorLevelId, 0.55},
                          {kAnchorFoldFeedId, 0.0},
                          {kCoreBodyCurvatureId, 0.75},
                          {kCoreShiftId, 0.35},
                          {kFieldGravityId, 0.4}},
            .controls = {{kMorphPositionId, 0.0},   // p = 0
                         {kVoiceModeId, 1.0}},      // Poly (Polyphony 4, P-6)
            .nameA = "Spread Chord",
            .nameB = "Spread Chord Held",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 55: AT-24 in Poly (Phase 13b, FR-066; endpoint A from Slow Swell with the TuneP_Poly edits) ----
        ProfundumPresetDef{
            .name = "Poly Swell",
            .category = "Resonant",
            .description = "Envelope Amp Attack: a slow attack swells a folded, shifted body under every held chord in Poly.",
            .primary = C::AT24,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 0.5},
                          {kCoreBodyId, 0.35},
                          {kCoreEdgeId, 0.5},
                          {kCoreExcitationMixId, 0.0},
                          {kFoldAmountId, 0.0},
                          {kFoldShapeId, 0.0},
                          {kCharacterId, 0.2},
                          {kCharacterAmountId, 0.3},
                          {kCharacterToneId, 0.5},
                          {kAmpAttackId, 0.739794},
                          {kAmpDecayId, 0.780618},
                          {kAmpSustainId, 0.8},
                          {kAmpReleaseId, 0.790849},
                          {kFieldImageModeId, 0.5},
                          {kFieldSpreadId, 0.8},
                          {kMassId, 0.5},
                          {kAnchorWaveformId, 0.0},
                          {kAnchorModeId, 0.0},
                          {kAnchorLevelId, 0.6},
                          {kAnchorFoldFeedId, 0.0},
                          {kCoreBodyCurvatureId, 0.7},
                          {kCoreBodyEmphasisId, 0.7},
                          {kCoreShiftId, 0.7}},
            .endpointB = {{kCoreDepthId, 0.5},
                          {kCoreBodyId, 0.35},
                          {kCoreEdgeId, 0.5},
                          {kCoreExcitationMixId, 0.0},
                          {kFoldAmountId, 0.0},
                          {kFoldShapeId, 0.0},
                          {kCharacterId, 0.2},
                          {kCharacterAmountId, 0.3},
                          {kCharacterToneId, 0.5},
                          {kAmpAttackId, 0.739794},
                          {kAmpDecayId, 0.780618},
                          {kAmpSustainId, 0.8},
                          {kAmpReleaseId, 0.790849},
                          {kFieldImageModeId, 0.5},
                          {kFieldSpreadId, 0.8},
                          {kMassId, 0.5},
                          {kAnchorWaveformId, 0.0},
                          {kAnchorModeId, 0.0},
                          {kAnchorLevelId, 0.6},
                          {kAnchorFoldFeedId, 0.0},
                          {kCoreBodyCurvatureId, 0.7},
                          {kCoreBodyEmphasisId, 0.7},
                          {kCoreShiftId, 0.7}},
            .controls = {{kMorphPositionId, 0.0},   // p = 0
                         {kVoiceModeId, 1.0}},      // Poly (Polyphony 4, P-6)
            .nameA = "Swelling Chord",
            .nameB = "Swelling Chord Held",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 56: AT-01 in Poly (Phase 13b, FR-066; endpoint A from Edge Ignition with the TuneP_Poly edits) ----
        ProfundumPresetDef{
            .name = "Poly Ignition",
            .category = "Driven",
            .description = "Core Excitation Mix: full edge excitation through Bite on a WIDE image, a driven chord bass in Poly.",
            .primary = C::AT01,
            .secondaries = {},
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
                          {kAnchorWaveformId, 0.0},
                          {kAnchorModeId, 0.0},
                          {kAnchorLevelId, 0.8},
                          {kAnchorFoldFeedId, 0.0},
                          {kCoreExcitationDriveId, 1.0},
                          {kCoreShiftId, 0.75}},
            .endpointB = {{kCoreDepthId, 1.0},
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
                          {kAnchorWaveformId, 0.0},
                          {kAnchorModeId, 0.0},
                          {kAnchorLevelId, 0.8},
                          {kAnchorFoldFeedId, 0.0},
                          {kCoreExcitationDriveId, 1.0},
                          {kCoreShiftId, 0.75}},
            .controls = {{kMorphPositionId, 0.0},   // p = 0
                         {kVoiceModeId, 1.0}},      // Poly (Polyphony 4, P-6)
            .nameA = "Ignited Chord",
            .nameB = "Ignited Chord Held",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 57: AT-06 in Poly (Phase 13b, FR-066; endpoint A from Rectified Floor with the TuneP_Poly edits) ----
        ProfundumPresetDef{
            .name = "Poly Rectifier",
            .category = "Driven",
            .description = "Character Rectify: full Rectify on a brighter body in SPREAD, rectified chords in Poly.",
            .primary = C::AT06,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 0.6},
                          {kCoreBodyId, 0.4},
                          {kCoreEdgeId, 0.4},
                          {kCoreExcitationMixId, 0.0},
                          {kFoldAmountId, 0.0},
                          {kFoldShapeId, 0.0},
                          {kCharacterId, 0.4},
                          {kCharacterAmountId, 1.0},
                          {kCharacterToneId, 0.6},
                          {kAmpAttackId, 0.35563},
                          {kAmpDecayId, 0.695424},
                          {kAmpSustainId, 0.9},
                          {kAmpReleaseId, 0.679588},
                          {kFieldImageModeId, 0.5},
                          {kFieldSpreadId, 0.6},
                          {kMassId, 0.55},
                          {kAnchorWaveformId, 0.0},
                          {kAnchorModeId, 0.0},
                          {kAnchorLevelId, 0.6},
                          {kAnchorFoldFeedId, 0.0}},
            .endpointB = {{kCoreDepthId, 0.6},
                          {kCoreBodyId, 0.4},
                          {kCoreEdgeId, 0.4},
                          {kCoreExcitationMixId, 0.0},
                          {kFoldAmountId, 0.0},
                          {kFoldShapeId, 0.0},
                          {kCharacterId, 0.4},
                          {kCharacterAmountId, 1.0},
                          {kCharacterToneId, 0.6},
                          {kAmpAttackId, 0.35563},
                          {kAmpDecayId, 0.695424},
                          {kAmpSustainId, 0.9},
                          {kAmpReleaseId, 0.679588},
                          {kFieldImageModeId, 0.5},
                          {kFieldSpreadId, 0.6},
                          {kMassId, 0.55},
                          {kAnchorWaveformId, 0.0},
                          {kAnchorModeId, 0.0},
                          {kAnchorLevelId, 0.6},
                          {kAnchorFoldFeedId, 0.0}},
            .controls = {{kMorphPositionId, 0.0},   // p = 0
                         {kVoiceModeId, 1.0}},      // Poly (Polyphony 4, P-6)
            .nameA = "Rectified Chord",
            .nameB = "Rectified Chord Held",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
        },
        // ---- 58: AT-25 in Poly (Phase 13b, FR-066; endpoint A from Long Tail with the TuneP_Poly edits) ----
        ProfundumPresetDef{
            .name = "Poly Pluck",
            .category = "Plucked",
            .description = "Envelope Amp Release: a short decay to a lower sustain with a long release, chords that ring out in Poly.",
            .primary = C::AT25,
            .secondaries = {},
            .endpointA = {{kCoreDepthId, 0.75},
                          {kCoreBodyId, 0.5},
                          {kCoreEdgeId, 0.5},
                          {kCoreExcitationMixId, 0.0},
                          {kFoldAmountId, 0.0},
                          {kFoldShapeId, 0.0},
                          {kCharacterId, 0.0},
                          {kCharacterAmountId, 0.3},
                          {kCharacterToneId, 0.5},
                          {kAmpAttackId, 0.4},
                          {kAmpDecayId, 0.65},
                          {kAmpSustainId, 0.6},
                          {kAmpReleaseId, 0.860206},
                          {kFieldImageModeId, 0.5},
                          {kFieldSpreadId, 0.5},
                          {kMassId, 0.55},
                          {kAnchorWaveformId, 0.0},
                          {kAnchorModeId, 0.0},
                          {kAnchorLevelId, 0.9},
                          {kAnchorFoldFeedId, 0.0},
                          {kCoreShiftId, 0.65}},
            .endpointB = {{kCoreDepthId, 0.75},
                          {kCoreBodyId, 0.5},
                          {kCoreEdgeId, 0.5},
                          {kCoreExcitationMixId, 0.0},
                          {kFoldAmountId, 0.0},
                          {kFoldShapeId, 0.0},
                          {kCharacterId, 0.0},
                          {kCharacterAmountId, 0.3},
                          {kCharacterToneId, 0.5},
                          {kAmpAttackId, 0.4},
                          {kAmpDecayId, 0.65},
                          {kAmpSustainId, 0.6},
                          {kAmpReleaseId, 0.860206},
                          {kFieldImageModeId, 0.5},
                          {kFieldSpreadId, 0.5},
                          {kMassId, 0.55},
                          {kAnchorWaveformId, 0.0},
                          {kAnchorModeId, 0.0},
                          {kAnchorLevelId, 0.9},
                          {kAnchorFoldFeedId, 0.0},
                          {kCoreShiftId, 0.65}},
            .controls = {{kMorphPositionId, 0.0},   // p = 0
                         {kVoiceModeId, 1.0}},      // Poly (Polyphony 4, P-6)
            .nameA = "Plucked Chord",
            .nameB = "Plucked Chord Held",
            .seed = kFactoryDefaultSeed,
            .seedDependent = false,
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
