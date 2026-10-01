#pragma once

// ==============================================================================
// Vorago - Factory Preset Definitions (Phase 14, FR-010 / FR-022)
// ==============================================================================
// Spec:  specs/vorago-phase14-presets-release/spec.md (C-2.1 matrix, C-2.2
//        showcase rules, C-2.3 E-ext cells, C-4, FR-010, FR-022, SC-029)
// Plan:  specs/vorago-phase14-presets-release/plan.md section 5.5 (types),
//        6.7 (ablation overrides), 6.12 (required primaries)
// Tasks: specs/vorago-phase14-presets-release/tasks.md T024 (this file, with an
//        EMPTY allPresets()); the library itself is authored later.
//
// THIS HEADER IS DATA ONLY (the rules are Seraphis's, tools/seraphis_preset_defs.h):
//   - every function is `inline`;
//   - every table is a function-local `static const`;
//   - the only project include is `plugin_ids.h`, plus std;
//   - no state layout: the generator writes each `Comp` chunk through the
//     SHIPPED `Vorago::Processor::getState()` (spec C-3).
//
// Values are NORMALIZED 0..1, exactly as a host delivers them (C-4). Untouched
// IDs keep their registered defaults.
//
// ParamSetting also exists as Seraphis::PresetDefs::ParamSetting
// (tools/seraphis_preset_defs.h:59): a different namespace and a different
// target; no TU includes both defs headers.
// ==============================================================================

#include "plugin_ids.h"  // ${CMAKE_SOURCE_DIR}/plugins/vorago/src is on the include path.

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Vorago::PresetDefs {

// ==============================================================================
// Definition types
// ==============================================================================

/// One authored parameter point: a registered `ParamID` and its NORMALIZED value.
struct ParamSetting {
    Steinberg::Vst::ParamID id;
    double normalized;
};

enum class CapabilityGroup : std::uint8_t { S, M, E, D };

/// FR-010: exactly the C-2.1 cells plus the ratified E-ext cells, in this order.
enum class Capability : std::uint8_t {
    S1Noise, S2Resonance, S3Smear, S4Ecology, S5Sub, S6Bloom, S7Ecosystem, S8Cavern, S9Ghost, S10Body,
    M1Darkness, M2Age, M3Density, M4Movement, M5Gravity, M6Entropy,              // VoragoMacro order
    M7Pressure, M8Weight, M9Fog, M10Life, M11Depth, M12Mass,
    E1PartialBloom, E2ResonatorPeaks, E3NoiseWake, E4FeedbackLoopWake, E5GhostBursts,
    E6SyncRateHi, E7SelfAffinityHi,                                               // FR-075
    D1Glass, D1Strings, D1MetalPlate, D1Chamber, D1Ice, D1StoneChamber, D1SteelTank,
    D1WoodenHull, D1CathedralColumn, D1CavernWall, D1GlassSphere,                 // BodyMaterial order
    D2BlendBoth,
    D3Direct, D3FilteredWind, D3GranularDust, D3MetallicHiss,                     // NoiseOrganismModel order
    D4Type1, D4Type2, D4Type3, D4Type4, D4Type5, D4Type6,                         // spec D4.1-D4.12:
    D4Type7, D4Type8, D4Type9, D4Type10, D4Type11, D4Type12,                      // D4.t = kNoiseTypeByIndex[t-1]
    D5Free, D5Keyed, D5Hybrid,
    D6Lowpass, D6Bandpass, D6Highpass,
    D7Div2, D7Div4, D7FifthBelow,
    D8Standard, D8Growth,
    D9FastAttack, D9SlowAttack,
    D10FreezeHolds, D10FreezeOff,
    D11GhostReverse,
    D12TriggersOn, D12TriggersOff,
    D13SlowEvents, D13FastEvents,
    D14Breathing, D14Tidal,
    Count
};

inline constexpr std::size_t kNumCapabilities = static_cast<std::size_t>(Capability::Count);
static_assert(kNumCapabilities == 79, "10 S + 12 M + 7 E + 50 D");

enum class Verification : std::uint8_t {
    Ablation,            // S, M: C-7.4 with `ablation`
    RouteIsolated,       // E1..E5: C-2.1 Group E
    ExtReversion,        // E6.hi, E7.hi: side predicate AND C-7.4 knob reversion
    StateWithS,          // D1..D7, D11, D12.1: state AND the named S cell verified (secondary bar)
    StateWithReversion,  // D13, D14: state AND S7 (D13) AND the reversion / depth ablation
    AttackWindow,        // D8, D9: state; primaries by the C-7.4 attack-window reversion
    FreezeGesture,       // D10.1: the S8 preset's gesture render + dry-residue twin (never primary)
    StateOnly            // D10.2, D12.2 (default-state by construction)
};

struct CellSpec {
    Capability cell;
    CapabilityGroup group;
    std::string_view label;                // "S1 noise organism", printed in the matrix
    Verification verification;
    std::array<ParamSetting, 4> ablation;  // static override (S, M, E-ext, D13, D14); unused {0, -1}
    std::uint8_t ablationCount;
    Capability sConjunct;                  // StateWithS / D13: the S cell required; else Count
};

// Not static: the D1.x primary per-material reversion (plan 6.7) depends on
// which of 1004 / 1005 holds the material, and the D8.2 / D9.1 attack-window
// reversion (plan 6.8) on the decoded envelope, so those cells carry
// ablationCount 0 here and the harness derives their twins from the decode.

namespace detail {

inline constexpr ParamSetting kUnusedSetting{0, -1.0};

[[nodiscard]] inline CellSpec makeCell(Capability cell, CapabilityGroup group, std::string_view label,
                                       Verification verification,
                                       std::initializer_list<ParamSetting> ablation,
                                       Capability sConjunct) {
    CellSpec c{cell,
               group,
               label,
               verification,
               {kUnusedSetting, kUnusedSetting, kUnusedSetting, kUnusedSetting},
               0,
               sConjunct};
    std::size_t i = 0;
    for (const ParamSetting& p : ablation) {
        if (i < c.ablation.size()) {
            c.ablation[i] = p;
            ++i;
        }
    }
    c.ablationCount = static_cast<std::uint8_t>(i);
    return c;
}

}  // namespace detail

/// The C-2.1 roster, indexed by `Capability` (entry i describes cell i).
[[nodiscard]] inline const std::array<CellSpec, kNumCapabilities>& cellSpecs() {
    static const std::array<CellSpec, kNumCapabilities> kSpecs = [] {
        using C = Capability;
        using G = CapabilityGroup;
        using V = Verification;
        std::array<CellSpec, kNumCapabilities> t{};
        const auto put = [&t](C cell, G group, std::string_view label, V verification,
                              std::initializer_list<ParamSetting> ablation, C sConjunct) {
            t[static_cast<std::size_t>(cell)] =
                detail::makeCell(cell, group, label, verification, ablation, sConjunct);
        };

        // ---- Group S: sections (spec C-2.1 table; plan 6.7) ----------------------
        put(C::S1Noise, G::S, "S1 noise organism", V::Ablation, {{kNoiseLevelId, 0.0}}, C::Count);
        put(C::S2Resonance, G::S, "S2 resonance drift", V::Ablation, {{kResonanceMixId, 0.0}}, C::Count);
        put(C::S3Smear, G::S, "S3 spectral smear", V::Ablation,
            {{kSmearAmountId, 0.0}, {kSmearDecoherenceId, 0.0}}, C::Count);
        put(C::S4Ecology, G::S, "S4 feedback ecology", V::Ablation, {{kEcologyMixId, 0.0}}, C::Count);
        put(C::S5Sub, G::S, "S5 subharmonic", V::Ablation,
            {{kSubDiv2LevelId, 0.0},
             {kSubDiv4LevelId, 0.0},
             {kSubFifthBelowLevelId, 0.0},
             {kSubLevelOffsetId, 0.0}},
            C::Count);
        put(C::S6Bloom, G::S, "S6 harmonic bloom", V::Ablation, {{kBloomDepthId, 0.0}}, C::Count);
        put(C::S7Ecosystem, G::S, "S7 ecosystem", V::Ablation, {{kEcosystemDepthId, 0.0}}, C::Count);
        put(C::S8Cavern, G::S, "S8 cavern space", V::Ablation, {{kSpaceMixId, 0.0}}, C::Count);
        put(C::S9Ghost, G::S, "S9 ghost / atmosphere", V::Ablation, {{kGhostPeakLevelId, 0.0}}, C::Count);
        put(C::S10Body, G::S, "S10 acoustic body", V::Ablation, {{kBodyMixId, 0.0}}, C::Count);

        // ---- Group M: macros, reset to the registered default (Gravity 0.5) ------
        put(C::M1Darkness, G::M, "M1 Darkness", V::Ablation, {{kMacroDarknessId, 0.0}}, C::Count);
        put(C::M2Age, G::M, "M2 Age", V::Ablation, {{kMacroAgeId, 0.0}}, C::Count);
        put(C::M3Density, G::M, "M3 Density", V::Ablation, {{kMacroDensityId, 0.0}}, C::Count);
        put(C::M4Movement, G::M, "M4 Movement", V::Ablation, {{kMacroMovementId, 0.0}}, C::Count);
        put(C::M5Gravity, G::M, "M5 Gravity", V::Ablation, {{kMacroGravityId, 0.5}}, C::Count);
        put(C::M6Entropy, G::M, "M6 Entropy", V::Ablation, {{kMacroEntropyId, 0.0}}, C::Count);
        put(C::M7Pressure, G::M, "M7 Pressure", V::Ablation, {{kMacroPressureId, 0.0}}, C::Count);
        put(C::M8Weight, G::M, "M8 Weight", V::Ablation, {{kMacroWeightId, 0.0}}, C::Count);
        put(C::M9Fog, G::M, "M9 Fog", V::Ablation, {{kMacroFogId, 0.0}}, C::Count);
        put(C::M10Life, G::M, "M10 Life", V::Ablation, {{kMacroLifeId, 0.0}}, C::Count);
        put(C::M11Depth, G::M, "M11 Depth", V::Ablation, {{kMacroDepthId, 0.0}}, C::Count);
        put(C::M12Mass, G::M, "M12 Mass", V::Ablation, {{kMacroMassId, 0.0}}, C::Count);

        // ---- Group E: routes (route-isolated, plan 6.9) + ratified E-ext ---------
        put(C::E1PartialBloom, G::E, "E1 partial -> bloom", V::RouteIsolated, {}, C::Count);
        put(C::E2ResonatorPeaks, G::E, "E2 resonator -> resonance peaks", V::RouteIsolated, {}, C::Count);
        put(C::E3NoiseWake, G::E, "E3 noise -> noise wake", V::RouteIsolated, {}, C::Count);
        put(C::E4FeedbackLoopWake, G::E, "E4 feedback -> loop wake", V::RouteIsolated, {}, C::Count);
        put(C::E5GhostBursts, G::E, "E5 ghost -> ghost bursts", V::RouteIsolated, {}, C::Count);
        put(C::E6SyncRateHi, G::E, "E6.hi ecosystem sync rate high", V::ExtReversion,
            {{kEcosystemSyncRateId, 0.0}}, C::Count);
        put(C::E7SelfAffinityHi, G::E, "E7.hi ecosystem self-affinity high", V::ExtReversion,
            {{kEcosystemSelfAffinityId, 0.25}}, C::Count);

        // ---- Group D: shipped enumerations and ranges (plan 6.10) ---------------
        put(C::D1Glass, G::D, "D1.1 material Glass", V::StateWithS, {}, C::S10Body);
        put(C::D1Strings, G::D, "D1.2 material Strings", V::StateWithS, {}, C::S10Body);
        put(C::D1MetalPlate, G::D, "D1.3 material MetalPlate", V::StateWithS, {}, C::S10Body);
        put(C::D1Chamber, G::D, "D1.4 material Chamber", V::StateWithS, {}, C::S10Body);
        put(C::D1Ice, G::D, "D1.5 material Ice", V::StateWithS, {}, C::S10Body);
        put(C::D1StoneChamber, G::D, "D1.6 material StoneChamber", V::StateWithS, {}, C::S10Body);
        put(C::D1SteelTank, G::D, "D1.7 material SteelTank", V::StateWithS, {}, C::S10Body);
        put(C::D1WoodenHull, G::D, "D1.8 material WoodenHull", V::StateWithS, {}, C::S10Body);
        put(C::D1CathedralColumn, G::D, "D1.9 material CathedralColumn", V::StateWithS, {}, C::S10Body);
        put(C::D1CavernWall, G::D, "D1.10 material CavernWall", V::StateWithS, {}, C::S10Body);
        put(C::D1GlassSphere, G::D, "D1.11 material GlassSphere", V::StateWithS, {}, C::S10Body);
        put(C::D2BlendBoth, G::D, "D2 body blend both heard", V::StateWithS, {}, C::S10Body);

        put(C::D3Direct, G::D, "D3.1 noise model Direct", V::StateWithS, {}, C::S1Noise);
        put(C::D3FilteredWind, G::D, "D3.2 noise model FilteredWind", V::StateWithS, {}, C::S1Noise);
        put(C::D3GranularDust, G::D, "D3.3 noise model GranularDust", V::StateWithS, {}, C::S1Noise);
        put(C::D3MetallicHiss, G::D, "D3.4 noise model MetallicHiss", V::StateWithS, {}, C::S1Noise);

        // D4.t = kNoiseTypeByIndex[t - 1] (param_mapping.h), 1-based labels.
        put(C::D4Type1, G::D, "D4.1 noise type White", V::StateWithS, {}, C::S1Noise);
        put(C::D4Type2, G::D, "D4.2 noise type Pink", V::StateWithS, {}, C::S1Noise);
        put(C::D4Type3, G::D, "D4.3 noise type TapeHiss", V::StateWithS, {}, C::S1Noise);
        put(C::D4Type4, G::D, "D4.4 noise type VinylCrackle", V::StateWithS, {}, C::S1Noise);
        put(C::D4Type5, G::D, "D4.5 noise type Asperity", V::StateWithS, {}, C::S1Noise);
        put(C::D4Type6, G::D, "D4.6 noise type Brown", V::StateWithS, {}, C::S1Noise);
        put(C::D4Type7, G::D, "D4.7 noise type Blue", V::StateWithS, {}, C::S1Noise);
        put(C::D4Type8, G::D, "D4.8 noise type Violet", V::StateWithS, {}, C::S1Noise);
        put(C::D4Type9, G::D, "D4.9 noise type Grey", V::StateWithS, {}, C::S1Noise);
        put(C::D4Type10, G::D, "D4.10 noise type Velvet", V::StateWithS, {}, C::S1Noise);
        put(C::D4Type11, G::D, "D4.11 noise type VinylRumble", V::StateWithS, {}, C::S1Noise);
        put(C::D4Type12, G::D, "D4.12 noise type RadioStatic", V::StateWithS, {}, C::S1Noise);

        put(C::D5Free, G::D, "D5.1 anchor Free", V::StateWithS, {}, C::S2Resonance);
        put(C::D5Keyed, G::D, "D5.2 anchor Keyed", V::StateWithS, {}, C::S2Resonance);
        put(C::D5Hybrid, G::D, "D5.3 anchor Hybrid", V::StateWithS, {}, C::S2Resonance);

        put(C::D6Lowpass, G::D, "D6.1 loop filter Lowpass", V::StateWithS, {}, C::S4Ecology);
        put(C::D6Bandpass, G::D, "D6.2 loop filter Bandpass", V::StateWithS, {}, C::S4Ecology);
        put(C::D6Highpass, G::D, "D6.3 loop filter Highpass", V::StateWithS, {}, C::S4Ecology);

        put(C::D7Div2, G::D, "D7.1 loudest sub f/2", V::StateWithS, {}, C::S5Sub);
        put(C::D7Div4, G::D, "D7.2 loudest sub f/4", V::StateWithS, {}, C::S5Sub);
        put(C::D7FifthBelow, G::D, "D7.3 loudest sub fifth below", V::StateWithS, {}, C::S5Sub);

        put(C::D8Standard, G::D, "D8.1 envelope Standard", V::AttackWindow, {}, C::Count);
        put(C::D8Growth, G::D, "D8.2 envelope Growth", V::AttackWindow, {}, C::Count);
        put(C::D9FastAttack, G::D, "D9.1 attack span <= 10 s", V::AttackWindow, {}, C::Count);
        put(C::D9SlowAttack, G::D, "D9.2 attack span >= 90 s", V::AttackWindow, {}, C::Count);

        put(C::D10FreezeHolds, G::D, "D10.1 freeze holds a field", V::FreezeGesture, {}, C::Count);
        put(C::D10FreezeOff, G::D, "D10.2 freeze off", V::StateOnly, {}, C::Count);

        put(C::D11GhostReverse, G::D, "D11 ghost reverse >= 0.5", V::StateWithS, {}, C::S9Ghost);
        put(C::D12TriggersOn, G::D, "D12.1 ghost event triggers on", V::StateWithS, {}, C::S9Ghost);
        put(C::D12TriggersOff, G::D, "D12.2 ghost event triggers off", V::StateOnly, {}, C::Count);

        // D13: reversion 800 -> 0.5 (the 1.0x default, events_params.h:7).
        put(C::D13SlowEvents, G::D, "D13.1 event rate scale <= 0.3", V::StateWithReversion,
            {{kEventsRateScaleId, 0.5}}, C::S7Ecosystem);
        put(C::D13FastEvents, G::D, "D13.2 event rate scale >= 3.0", V::StateWithReversion,
            {{kEventsRateScaleId, 0.5}}, C::S7Ecosystem);
        put(C::D14Breathing, G::D, "D14.1 breathing depth >= 0.7", V::StateWithReversion,
            {{kLifeBreathingDepthId, 0.0}}, C::Count);
        put(C::D14Tidal, G::D, "D14.2 tidal depth >= 0.7", V::StateWithReversion,
            {{kLifeTidalDepthId, 0.0}}, C::Count);
        return t;
    }();
    return kSpecs;
}

// ==============================================================================
// Default-state set and required primaries (plan 6.12, SC-029)
// ==============================================================================

/// The MEASURED default-state set (plan 6.12; T043, artifacts/default_state_vector.log,
/// 2026-09-29): the D cells the default surface itself verifies at the secondary
/// bar. P2-6's prediction also listed D3.1-D3.4, D4.6 and D6.1; they fell out
/// because their conjuncts do not register on the default surface (S1 noise
/// organism ablation d = 0.0110, S4 noise filter d = 0.0009 - the noise bed is
/// inaudible there), so the four noise models need their own showcase presets.
/// User ruling 2026-09-29: N = 42. Re-recording this set in a way that moves
/// |requiredPrimaryCells()| away from 42 is an FR-017 stop.
[[nodiscard]] inline std::span<const Capability> kRecordedDefaultStateCells() {
    static const std::array<Capability, 9> kCells{
        Capability::D1StoneChamber, Capability::D1SteelTank, Capability::D2BlendBoth,
        Capability::D5Hybrid,       Capability::D7Div2,      Capability::D8Standard,
        Capability::D9SlowAttack,   Capability::D10FreezeOff, Capability::D12TriggersOff};
    return {kCells.data(), kCells.size()};
}

[[nodiscard]] inline bool isRecordedDefaultState(Capability c) {
    for (const Capability d : kRecordedDefaultStateCells()) {
        if (d == c) {
            return true;
        }
    }
    return false;
}

/// SC-029's required primaries, DERIVED from kRecordedDefaultStateCells (plan 6.12):
/// S1-S10, M1-M12, E1-E5, plus (D1.* u D3.* u D8.* u D9.*) minus the
/// default-state set, in Capability order. E6.hi / E7.hi are SECONDARIES
/// (gate G2 ruling 2026-09-29: the counted knobs move a preset by 0.15-2.14,
/// under the primary bar F = 4.0 on every measured surface).
[[nodiscard]] inline std::vector<Capability> requiredPrimaryCells() {
    const auto inRange = [](Capability c, Capability first, Capability last) {
        const auto v = static_cast<std::uint8_t>(c);
        return v >= static_cast<std::uint8_t>(first) && v <= static_cast<std::uint8_t>(last);
    };
    std::vector<Capability> out;
    for (std::size_t i = 0; i < kNumCapabilities; ++i) {
        const auto c = static_cast<Capability>(i);
        if (inRange(c, Capability::S1Noise, Capability::E5GhostBursts)) {
            out.push_back(c);  // S, M and E1-E5 cells are never default-state (spec C-2.1)
            continue;
        }
        if (c == Capability::E6SyncRateHi || c == Capability::E7SelfAffinityHi) {
            continue;  // secondaries (G2 ruling 2026-09-29)
        }
        const bool eligibleD = inRange(c, Capability::D1Glass, Capability::D1GlassSphere) ||
                               inRange(c, Capability::D3Direct, Capability::D3MetallicHiss) ||
                               inRange(c, Capability::D8Standard, Capability::D9SlowAttack);
        if (eligibleD && !isRecordedDefaultState(c)) {
            out.push_back(c);
        }
    }
    return out;
}

// ==============================================================================
// Preset definitions
// ==============================================================================

struct VoragoPresetDef {
    std::string_view name;
    std::string_view category;
    std::string_view description;          // P-7: no " & < > (written unescaped, preset_manager.cpp:271-274)
    Capability primary;
    std::vector<Capability> secondaries;
    std::vector<ParamSetting> params;      // normalized; untouched IDs keep registered defaults (C-4)
};

/// MUST equal `makeVoragoPresetConfig().subcategoryNames` element-wise and in
/// order (plugins/vorago/src/preset/vorago_preset_config.h).
inline constexpr std::array<std::string_view, 7> kCategories{
    "Drones", "Abyss", "Caverns", "Organisms", "Machines", "Textures", "Ghosts"};

/// Definition order == file order. Library order is finalised in T044-T047
/// (they insert rows around the six T037 pilot presets, plan 6.16 P1-P6).
///
/// Every row (plan 7 authoring constraints): polyphony left at its registered
/// index 3 (normalized 0.6, 4 voices); A <= 180 s and Rel <= 60 s; Freeze Off;
/// no point for ID 4 or 5; output saturation untouched (its MB base, C-5);
/// >= 8 IDs displaced >= 0.10 from the default and from each other (C-7.1).
/// Tapers: noise level lin [-96, 12] dB; sub levels lin [-60, 6] dB, offset lin
/// [-24, 24] dB; cloud tilt lin [-12, 12] dB; space decay log [0.5, 60] s; early
/// size log [80, 300] ms; envelope times offset-log [0, 120000] ms eps 10; growth
/// duration log [1, 120] s; bloom spawn offset-log [0, 0.05] Hz eps 1e-4;
/// resonance wander log [0.002, 1] Hz; a list's n = index / (count - 1).
[[nodiscard]] inline const std::vector<VoragoPresetDef>& allPresets() {
    using C = Capability;
    static const std::vector<VoragoPresetDef> kPresets{
        // (The two colony-knob showcase rows, "Locked Choir" E6.hi and "Clotting
        // Colony" E7.hi, left the library at gate G2 - ruling 2026-09-29: the
        // knobs move a preset by 0.15-0.85 on colony-forward surfaces and 2.14 at
        // best, under F = 4.0; E6.hi / E7.hi verify as secondaries on Organisms
        // rows that store the knobs at or above the Q7 margin on a Life-high
        // surface. N = 38; the T043 ruling adds the four D3 noise-model rows:
        // N = 42.)
        // ---- 7 row 1: S1 + D3.1, D3.2, D4.1-D4.3 (T044) ----------------------
        // The noise organism leads (+6 dB; the default -18 dB bed measured S1
        // d 0.0110, T043): three Direct slots (White, Pink, TapeHiss) and one
        // FilteredWind slot over a thin, dark cloud, so the S1 ablation removes
        // the texture itself.
        VoragoPresetDef{
            "Wind Through Basalt",
            "Textures",
            "Raw wind and hiss pouring through cracks in black stone over a thin, dark tone.",
            C::S1Noise,
            {C::D3Direct, C::D4Type1, C::D4Type2, C::D4Type3, C::D4Type7},
            {
                {kNoiseLevelId, 102.0 / 108.0},   // 300 -> +6 dB
                {kNoiseSlot0ModelId, 0.0},        // 310 index 0 -> Direct
                {kNoiseSlot0TypeId, 0.0},         // 320 index 0 -> White (D4.1)
                {kNoiseSlot1ModelId, 0.0},        // 311 index 0 -> Direct
                {kNoiseSlot1TypeId, 1.0 / 11.0},  // 321 index 1 -> Pink (D4.2)
                {kNoiseSlot2ModelId, 0.0},        // 312 index 0 -> Direct (its default)
                {kNoiseSlot2TypeId, 2.0 / 11.0},  // 322 index 2 -> TapeHiss (D4.3)
                {kNoiseSlot3ModelId, 0.0},        // 313 index 0 -> Direct (sweep 3: Blue moved here from Swarm Breath, whose S1 conjunct reads 0.65; this bed reads 5.3 with slot 3 Direct)
                {kNoiseSlot3TypeId, 6.0 / 11.0},  // 323 index 6 -> Blue (D4.7)
                {kNoiseWakeId, 0.70},             // 301
                {kNoiseWanderRateId, 0.369280},   // 302 -> 0.3 Hz
                {kCloudRichnessId, 0.30},         // 200 a thin tone under the wind
                {kCloudTiltId, 6.0 / 24.0},       // 201 -> -6 dB
                {kResonanceMixId, 0.15},          // 401 the noise stays raw
                {kMacroMovementId, 0.35},         // 103
                {kSpaceSizeId, 0.70},             // 1100
                {kSpaceMixId, 0.55},              // 1105
                {kSpaceDecayId, 0.519041},        // 1102 -> 6 s
                {kGhostPeakLevelId, 0.15},        // 1400
                {kLifeTidalDepthId, 0.55},        // 1502
                {kSeedId, 2.0 / 15.0},            // 2 index 2 -> "Seed 3"
            }},
        // ---- 7 row 2: S2 + D5.2 (T044) -----------------------------------------
        // Resonance mix near full with the Keyed anchor; the body pulled back so
        // the drifting peaks, not the material, colour the shaft.
        VoragoPresetDef{
            "Resonant Shaft",
            "Caverns",
            "A deep vertical shaft whose walls sing back the played notes in slow, drifting peaks.",
            C::S2Resonance,
            {C::D5Keyed},
            {
                {kResonanceMixId, 0.85},             // 401 (sweep 3: the shaft sat at the limiter once the cavern dropped; probe with mix 0.85 / richness 0.45 / cavern 0.3 / master gain 0.25: S2 4.19, hi -9.2 dB, all arms green)
                {kResonanceAnchorModeId, 0.5},       // 403 index 1 -> Keyed
                {kResonanceGravityId, 0.30},         // 400 -> -0.4
                {kResonanceWanderRateId, 0.258977},  // 402 -> 0.01 Hz
                {kCloudRichnessId, 0.45},            // 200
                {kBodyMixId, 0.40},                  // 1003 the body steps back
                {kNoiseLevelId, 66.0 / 108.0},       // 300 -> -30 dB
                {kSpaceSizeId, 0.85},                // 1100
                {kSpaceMixId, 0.30},                 // 1105
                {kSpaceDecayId, 0.855217},           // 1102 -> 30 s
                {kSpaceDarknessId, 0.55},            // 1101
                {kSpaceEarlySizeId, 0.862061},       // 1110 -> 250 ms
                {kSpaceEarlyLevelId, 0.60},          // 1111
                {kBloomDepthId, 0.30},               // 1300
                {kGhostPeakLevelId, 0.30},           // 1400
                {kMacroDepthId, 0.30},               // 110
                {kSeedId, 3.0 / 15.0},               // 2 index 3 -> "Seed 4"
                {kMasterGainId, 0.25},              // 0 -> gain 0.5: the peaks are the level, trim the whole shaft
            }},
        // ---- 7 row 3: S3 + D8.1, D10.2 (T044) ----------------------------------
        // Smear at full with high decoherence (the default 0.2 / 0.2 measured S3
        // d 0.9987, T043). Envelope Standard and Freeze Off are the registered
        // defaults (D8.1 and D10.2 are default-state cells).
        VoragoPresetDef{
            "Smeared Horizon",
            "Textures",
            "A drone dissolved into a wide, blurred haze where every partial bleeds into the next.",
            C::S3Smear,
            {C::D8Standard, C::D10FreezeOff},
            {
                {kSmearAmountId, 1.0},         // 700
                {kSmearDecoherenceId, 0.85},   // 701
                {kSmearTiltId, 0.70},          // 702 -> +0.4
                {kCloudStereoSpreadId, 0.90},  // 205
                {kCloudDriftDepthId, 0.40},    // 204 -> 20 cents
                {kCloudMutationId, 0.35},      // 202
                {kResonanceMixId, 0.20},       // 401
                {kBodyMixId, 0.55},            // 1003
                {kSpaceMixId, 0.20},           // 1105 (sweep 2 probe: S3 4.30 at 0.20; 3.98 at 0.45 - the cavern smears the smear)
                {kSpaceFogId, 0.60},           // 1103
                {kSpaceBreathId, 0.80},        // 1109
                {kGhostPeakLevelId, 0.25},     // 1400
                {kMacroAgeId, 0.25},           // 101
                {kSeedId, 4.0 / 15.0},         // 2 index 4 -> "Seed 5"
            }},
        // ---- 7 row 4: S4 + D6.1, D6.2, D6.3 (T044) -----------------------------
        // Ecology mix near full at a high loop gain (the default mix 0.15 measured
        // S4 d 0.0009, T043); loops 1-6 hold Lowpass, Bandpass, Highpass twice, so
        // each filter mode runs on at least one loop.
        VoragoPresetDef{
            "Feedback Mire",
            "Machines",
            "Choked feedback loops churning in a sump of rust, each filtered to its own grinding band.",
            C::S4Ecology,
            {C::D6Lowpass, C::D6Bandpass, C::D6Highpass},
            {
                {kEcologyMixId, 0.95},             // 500 (sweep 3: mix 1.0 rendered SILENCE, peak 0.0004 - the loops are wake-gated; the probe's 8.45 was P silent vs a loud twin. Best honest reading S4 1.76 at 0.95 / space mix 0.2)
                {kEcologyLoopGainId, 0.95},        // 501 -> 0.855
                {kEcologyLoop0FilterModeId, 0.0},  // 510 index 0 -> Lowpass (its default)
                {kEcologyLoop1FilterModeId, 0.5},  // 511 index 1 -> Bandpass
                {kEcologyLoop2FilterModeId, 1.0},  // 512 index 2 -> Highpass
                {kEcologyLoop3FilterModeId, 0.5},  // 513 index 1 -> Bandpass
                {kEcologyLoop4FilterModeId, 1.0},  // 514 index 2 -> Highpass
                {kEcologyLoop5FilterModeId, 0.0},  // 515 index 0 -> Lowpass (its default)
                {kCloudRichnessId, 0.45},          // 200
                {kCloudSpectralGravityId, 0.75},   // 206 -> +0.5
                {kResonanceMixId, 0.25},           // 401
                {kNoiseLevelId, 84.0 / 108.0},     // 300 -> -12 dB
                {kBodyDampingId, 0.55},            // 1001
                {kSpaceMixId, 0.20},               // 1105
                {kSpaceDecayId, 0.374258},         // 1102 -> 3 s
                {kGhostPeakLevelId, 0.20},         // 1400
                {kMacroEntropyId, 0.30},           // 105
                {kSeedId, 6.0 / 15.0},             // 2 index 6 -> "Seed 7"
            }},
        // ---- P3 (plan 6.16; 7 row 5): S5 + D7.3 --------------------------------
        // Sub tones: fifth-below -9 dB > f/2 -18 dB (its registered default, not
        // authored) > f/4 -36 dB, so D7.3 holds strictly. Offset +3 dB keeps the
        // pilot's P3' twin (+0.125 normalized = +6 dB) and the sub twin in range.
        VoragoPresetDef{
            "Tectonic Floor",
            "Abyss",
            "A fifth below the floor: slow plates of sub grind under a dark, close cloud.",
            C::S5Sub,
            {C::D7FifthBelow},
            {
                {kSubFifthBelowLevelId, 51.0 / 66.0},  // 612 -> -9 dB, strictly loudest
                {kSubDiv4LevelId, 24.0 / 66.0},        // 611 -> -36 dB
                {kSubLevelOffsetId, 27.0 / 48.0},      // 600 -> +3 dB
                {kMacroDarknessId, 0.70},              // 100
                {kMacroWeightId, 0.35},                // 107 (never Pressure: it lowers the subs)
                {kCloudRichnessId, 0.55},              // 200
                {kCloudTiltId, 4.0 / 24.0},            // 201 -> -8 dB
                {kCloudDriftDepthId, 0.08},            // 204 -> 4 cents
                {kNoiseLevelId, 72.0 / 108.0},         // 300 -> -24 dB
                {kNoiseSlot2TypeId, 10.0 / 11.0},      // 322 index 10 -> VinylRumble (Direct slot)
                {kBodyDampingId, 0.50},                // 1001
                {kSpaceDarknessId, 0.95},              // 1101
                {kSpaceDecayId, 0.663824},             // 1102 -> 12 s
                {kLifeTidalDepthId, 0.60},             // 1502
                {kSeedId, 1.0 / 15.0},                 // 2 index 1 -> "Seed 2"
            }},
        // ---- 7 row 6: S6 + D14.1, D9.2 (T044) ----------------------------------
        // Bloom at full over a sparse cloud (the default 0.6 measured S6 d 0.87,
        // T043) so the budded partials carry the sound; breathing 0.85 (D14.1);
        // stage times 40 + 25 + 25 + 25 = 115 s (D9.2 A >= 90 s, A <= 180 s),
        // release 40 s.
        VoragoPresetDef{
            "Slow Bloom",  // sweep 1: S6 twin d = 0 - at 0.01 Hz no bloom fell inside the window; spawn raised
            "Drones",
            "A sparse drone that takes two minutes to open, budding new partials as it breathes.",
            C::S6Bloom,
            {C::D14Breathing, C::D9SlowAttack},
            {
                {kBloomDepthId, 1.0},                  // 1300
                {kBloomSpawnRateId, 0.918043},         // 1301 -> 0.03 Hz (a bloom every ~33 s: several per minute-window)
                {kCloudRichnessId, 0.70},              // 200 (sweep 2 probe: S6 1.74 at 0.7 - the buds need SOUNDING parents; 0.0000 at 0.35, 0.47 at 0.9; best reading, under F)
                {kLifeBreathingDepthId, 0.85},         // 1500 (D14.1 >= 0.7)
                {kLifeBreathingIrregularityId, 0.10},  // 1501
                {kLifeTidalDepthId, 0.15},             // 1502
                {kEnvelopeStage0TimeId, 0.883054},     // 1201 -> 40 s
                {kEnvelopeStage1TimeId, 0.833031},     // 1202 -> 25 s
                {kEnvelopeStage2TimeId, 0.833031},     // 1203 -> 25 s
                {kEnvelopeStage3TimeId, 0.833031},     // 1204 -> 25 s  (A = 115 s)
                {kEnvelopeReleaseId, 0.883054},        // 1205 -> 40 s
                {kResonanceMixId, 0.25},               // 401
                {kSpaceMixId, 0.70},                   // 1105
                {kSpaceDecayId, 0.710434},             // 1102 -> 15 s
                {kGhostPeakLevelId, 0.35},             // 1400
                {kSeedId, 7.0 / 15.0},                 // 2 index 7 -> "Seed 8"
            }},
        // ---- 7 row 7: S7 + D13.2 (T044) ----------------------------------------
        // Ecosystem depth full, event rate 4.0x (D13.2 >= 3.0), and the colony's
        // destinations (bloom, resonance, noise wake, loops, ghost) audible so the
        // colony has something to steer.
        VoragoPresetDef{
            "Colony Pulse",
            "Organisms",
            "A restless colony of small voices, quickening and feeding on each other in the dark.",
            C::S7Ecosystem,
            {C::D13FastEvents, C::E6SyncRateHi, C::E7SelfAffinityHi},  // sweep 2: the knobs at 1.0 are what lets the colony through
            {
                {kEcosystemDepthId, 1.0},        // 900
                {kEcosystemSelfAffinityId, 1.0}, // 902 (E7.hi)
                {kEcosystemSyncRateId, 1.0},     // 901 (E6.hi)
                {kEventsRateScaleId, 0.801030},  // 800 -> 4.0x
                {kMacroLifeId, 0.45},            // 109
                {kBloomDepthId, 0.80},           // 1300
                {kResonanceMixId, 0.60},         // 401
                {kNoiseLevelId, 86.0 / 108.0},   // 300 -> -10 dB
                {kNoiseWakeId, 0.10},            // 301 (sweep 2 probe: S7 12.02 with wake base 0.1 / sync 1.0 / affinity 1.0 / tilt +8 dB; 2.78 at wake 0.8 - the base masked the ecosystem's wake lane)
                {kEcologyMixId, 0.40},           // 500
                {kGhostPeakLevelId, 0.70},       // 1400
                {kCloudMutationId, 0.45},        // 202
                {kCloudTiltId, 1.0},             // 201 -> +8 dB
                {kSpaceMixId, 0.60},             // 1105
                {kSpaceDecayId, 0.603734},       // 1102 -> 9 s
                {kSeedId, 8.0 / 15.0},           // 2 index 8 -> "Seed 9"
            }},
        // ---- P4 (plan 6.16; 7 row 8): S8 + D10.1 (freeze gesture, R-5) ---------
        // Freeze is stored Off (C-6 arm 4 load-then-play precondition); the
        // Comment carries the gesture. Effective RT60 = 45 s + Depth 0.4 x 25 s.
        VoragoPresetDef{
            "Cathedral Void",
            "Caverns",
            "A vast, slow nave around a wide cloud. Engage Freeze once the drone has bloomed "
            "to hold the space.",
            C::S8Cavern,
            {C::D10FreezeHolds},
            {
                {kSpaceSizeId, 1.0},             // 1100
                {kSpaceDecayId, 0.939910},       // 1102 -> 45 s
                {kSpaceDensityId, 0.90},         // 1107
                {kSpaceDimensionalityId, 0.80},  // 1108
                {kSpaceEarlySizeId, 1.0},        // 1110 -> 300 ms
                {kSpaceEarlyLevelId, 0.50},      // 1111
                {kSpaceDarknessId, 0.60},        // 1101
                {kSpaceBreathId, 0.70},          // 1109
                {kSpaceFreezeId, 0.0},           // 1115 Off (the gesture engages it)
                {kMacroDepthId, 0.40},           // 110
                {kEnvelopeReleaseId, 0.852435},  // 1205 -> 30 s
                {kCloudRichnessId, 0.85},        // 200
                {kCloudStereoSpreadId, 0.80},    // 205
                {kCloudTiltId, 10.0 / 24.0},     // 201 -> -2 dB
                {kResonanceMixId, 0.30},         // 401
                {kBloomDepthId, 0.75},           // 1300
                {kGhostPeakLevelId, 0.45},       // 1400 (less dry residue under the gesture)
                {kSeedId, 5.0 / 15.0},           // 2 index 5 -> "Seed 6"
            }},
        // ---- 7 row 9: S9 + D11, D12.1 (T044) -----------------------------------
        // Ghost peak at full (the default 0.6 measured S9 d 0.0753, T043), reverse
        // 0.75 (D11 >= 0.5), event triggers On (D12.1, additive per FR-061), with
        // the ecosystem and a 2.0x event rate supplying the ghost requests.
        VoragoPresetDef{
            "Choir of Absence",
            "Ghosts",
            "Voices that are not there: reversed fragments of the drone surfacing and sinking away.",
            C::S9Ghost,
            {C::D11GhostReverse, C::D12TriggersOn},
            {
                {kGhostPeakLevelId, 1.0},            // 1400
                {kGhostBlurId, 0.55},                // 1401
                {kGhostReverseProbabilityId, 0.75},  // 1402 (D11 >= 0.5)
                {kGhostEventTriggersId, 1.0},        // 1403 index 1 -> On
                {kEcosystemDepthId, 1.0},            // 900
                {kEventsRateScaleId, 0.650515},      // 800 -> 2.0x
                {kCloudRichnessId, 0.85},            // 200
                {kCloudStereoSpreadId, 0.85},        // 205
                {kCloudSpectralGravityId, 0.30},     // 206 -> -0.4
                {kBodyMixId, 0.60},                  // 1003
                {kSpaceSizeId, 0.80},                // 1100
                {kSpaceDarknessId, 0.65},            // 1101
                {kSpaceMixId, 0.50},                 // 1105
                {kSpaceFogId, 0.50},                 // 1103
                {kLifeBreathingDepthId, 0.50},       // 1500
                {kSeedId, 10.0 / 15.0},              // 2 index 10 -> "Seed 11"
            }},
        // ---- 7 row 10: S10 + D2, D1.6, D1.7 (T044) -----------------------------
        // The registered materials (A StoneChamber, B SteelTank, left untouched) at
        // blend 0.5 (D2 in [0.35, 0.65]; D1.6 needs blend <= 0.65, D1.7 >= 0.35),
        // a resonant, lightly damped body in full over a small, drier space.
        VoragoPresetDef{
            "Hull Resonance",
            "Machines",
            "The inside of a vast steel and stone hull, every plate ringing under the drone.",
            C::S10Body,
            {C::D2BlendBoth, C::D1StoneChamber, C::D1SteelTank},
            {
                {kBodyBlendId, 0.50},             // 1000 both materials heard
                {kBodyDampingId, 0.12},           // 1001
                {kBodyResonanceId, 0.92},         // 1002
                {kBodyMixId, 1.0},                // 1003 the body path in full
                {kResonanceMixId, 0.15},          // 401
                {kCloudRichnessId, 0.55},         // 200
                {kCloudInharmonicityId, 0.60},    // 203 -> 0.06
                {kNoiseLevelId, 66.0 / 108.0},    // 300 -> -30 dB
                {kSpaceSizeId, 0.30},             // 1100
                {kSpaceMixId, 0.30},              // 1105
                {kSpaceDecayId, 0.434349},        // 1102 -> 4 s
                {kSpaceEarlyAbsorptionId, 0.25},  // 1112
                {kGhostPeakLevelId, 0.15},        // 1400
                {kMacroMassId, 0.35},             // 111
                {kSeedId, 11.0 / 15.0},           // 2 index 11 -> "Seed 12"
            }},
        // ---- 7 row 11: M1 + D12.2 (T045) ---------------------------------------
        // Darkness at full over a bright base (tilt +2 dB, a pale cavern, the
        // smear leaning high) so the macro's three darkening routes have room to
        // act. Event triggers stay at their registered Off (D12.2, default state).
        VoragoPresetDef{
            "Lightless",
            "Abyss",
            "A bright cloud pressed down into lightless black, every overtone smothered as it sinks.",
            C::M1Darkness,
            {C::D12TriggersOff},
            {
                {kMacroDarknessId, 1.0},        // 100 (displaced 1.0 >= 0.5)
                {kCloudTiltId, 14.0 / 24.0},    // 201 -> +2 dB, a bright base
                {kCloudRichnessId, 0.90},       // 200
                {kCloudStereoSpreadId, 0.65},   // 205
                {kSpaceDarknessId, 0.40},       // 1101 a pale cavern
                {kSpaceDecayId, 0.625744},      // 1102 -> 10 s
                {kSmearAmountId, 0.55},         // 700
                {kSmearTiltId, 0.80},           // 702 -> +0.6
                {kResonanceMixId, 0.30},        // 401
                {kGhostPeakLevelId, 0.30},      // 1400
                {kLifeBreathingDepthId, 0.15},  // 1500
                {kSeedId, 13.0 / 15.0},         // 2 index 13 -> "Seed 14"
            }},
        // ---- 7 row 12: M2 + D3.3, D4.4-D4.6 (T045) -----------------------------
        // Age at full over a lightly damped body, a bright tilt and a long cavern
        // (Age damps the body, darkens the tilt and shortens the cavern). The
        // noise bed is raised to -6 dB so S1 verifies: three Direct slots
        // (VinylCrackle, Asperity, Brown) and one GranularDust slot.
        VoragoPresetDef{
            "Erosion",
            "Textures",
            "Weathered stone crumbling to dust: crackle and grit wearing a ringing drone down "
            "to a dull husk.",
            C::M2Age,
            {C::D3GranularDust, C::D4Type4, C::D4Type5, C::D4Type6},
            {
                {kMacroAgeId, 1.0},               // 101 (displaced 1.0 >= 0.5)
                {kNoiseLevelId, 90.0 / 108.0},    // 300 -> -6 dB
                {kNoiseSlot0ModelId, 0.0},        // 310 index 0 -> Direct
                {kNoiseSlot0TypeId, 3.0 / 11.0},  // 320 index 3 -> VinylCrackle (D4.4)
                {kNoiseSlot1ModelId, 0.0},        // 311 index 0 -> Direct
                {kNoiseSlot1TypeId, 4.0 / 11.0},  // 321 index 4 -> Asperity (D4.5)
                {kNoiseSlot2ModelId, 0.0},        // 312 index 0 -> Direct (its default)
                {kNoiseSlot2TypeId, 5.0 / 11.0},  // 322 index 5 -> Brown (D4.6, its default)
                {kNoiseSlot3ModelId, 2.0 / 3.0},  // 313 index 2 -> GranularDust (D3.3)
                {kNoiseWakeId, 0.55},             // 301
                {kBodyDampingId, 0.05},           // 1001 Age damps from here
                {kBodyResonanceId, 0.90},         // 1002
                {kCloudTiltId, 0.5},              // 201 -> 0 dB, Age darkens from here
                {kSpaceDecayId, 0.855217},        // 1102 -> 30 s, Age shortens from here
                {kGhostPeakLevelId, 0.20},        // 1400
                {kSeedId, 14.0 / 15.0},           // 2 index 14 -> "Seed 15"
            }},
        // ---- 7 row 13: M3 (T045) -----------------------------------------------
        // Density at full over a thin base: a sparse cloud, a sleeping noise wake
        // and a light bloom, so the macro's richness, wake, noise and bloom routes
        // fill the field.
        VoragoPresetDef{
            "Crowded Dark",
            "Drones",
            "A thin drone that fills to bursting, partials and embers packing the dark shoulder "
            "to shoulder.",
            C::M3Density,
            {},
            {
                {kMacroDensityId, 1.0},          // 102 (displaced 1.0 >= 0.5)
                {kCloudRichnessId, 0.40},        // 200 a sparse base
                {kCloudTiltId, 1.0},             // 201 -> +8 dB (sweep 2 probe: M3 4.17 with tilt +8 / space mix 0.2; 2.77 as first authored)
                {kNoiseWakeId, 0.10},            // 301 a sleeping wake
                {kBloomDepthId, 0.35},           // 1300
                {kBloomSpawnRateId, 0.742386},   // 1301 -> 0.01 Hz
                {kCloudStereoSpreadId, 0.75},    // 205
                {kCloudDriftDepthId, 0.30},      // 204 -> 15 cents
                {kResonanceMixId, 0.60},         // 401
                {kSpaceSizeId, 0.65},            // 1100
                {kSpaceMixId, 0.20},             // 1105
                {kSpaceDimensionalityId, 0.75},  // 1108
                {kGhostPeakLevelId, 0.40},       // 1400
                {kSeedId, 1.0},                  // 2 index 15 -> "Seed 16"
            }},
        // ---- 7 row 14: M4 + D14.2 (T045) ---------------------------------------
        // Movement at full over a still base (no drift, the slowest peak and
        // filter wander, a shallow breath, the dampers barely moving); tidal
        // depth 0.85 (D14.2 >= 0.7).
        VoragoPresetDef{
            "Drifting Strata",
            "Drones",
            "Layers of drone sliding over one another like slow geological strata under a rolling tide.",
            C::M4Movement,
            {C::D14Tidal},
            {
                {kMacroMovementId, 1.0},          // 103 (displaced 1.0 >= 0.5)
                {kLifeTidalDepthId, 0.85},        // 1502 (D14.2 >= 0.7)
                {kCloudDriftDepthId, 0.0},        // 204 -> 0 cents, Movement drifts from here
                {kResonanceMixId, 0.70},          // 401
                {kResonanceWanderRateId, 0.0},    // 402 -> 0.002 Hz
                {kNoiseWanderRateId, 0.0},        // 302 -> 0.01 Hz
                {kLifeBreathingDepthId, 0.10},    // 1500
                {kSpaceDamperDepthId, 0.10},      // 1104
                {kSpaceBreathId, 0.75},           // 1109
                {kCloudRichnessId, 0.55},         // 200
                {kSpaceMixId, 0.20},              // 1105
                {kCloudTiltId, 1.0},              // 201 -> +8 dB (sweep 2 probe: M4 3.97 with tilt +8 / space mix 0.2 - best reading, under F)
                {kCloudSpectralGravityId, 0.35},  // 206 -> -0.3
                {kSeedId, 3.0 / 15.0},            // 2 index 3 -> "Seed 4"
            }},
        // ---- 7 row 15: M5 + D5.3 (T045) ----------------------------------------
        // Gravity at stone (|1.0 - 0.5| = 0.5 >= 0.35): the resonance network
        // loud through the Hybrid anchor (D5.3, its registered default, stored
        // explicitly), where Gravity pulls the peaks down and octave-locks them.
        VoragoPresetDef{
            "Stone Gravity",
            "Abyss",
            "Resonant peaks dragged down and locked to octaves, heavy as stone settling in the deep.",
            C::M5Gravity,
            {C::D5Hybrid},
            {
                {kMacroGravityId, 1.0},              // 104 stone (|g - 0.5| = 0.5 >= 0.35)
                {kResonanceAnchorModeId, 1.0},       // 403 index 2 -> Hybrid (its default)
                {kResonanceMixId, 0.90},             // 401
                {kResonanceWanderRateId, 0.258977},  // 402 -> 0.01 Hz
                {kCloudRichnessId, 0.50},            // 200
                {kCloudSpectralGravityId, 0.80},     // 206 -> +0.6
                {kBodyMixId, 0.50},                  // 1003
                {kMacroDarknessId, 0.40},            // 100
                {kSpaceSizeId, 0.80},                // 1100
                {kSpaceDecayId, 0.855217},           // 1102 -> 30 s
                {kSubLevelOffsetId, 0.375},          // 600 -> -6 dB (sweep 2: 0 dB still sat at the limiter, peak 0.966 / late-sustain +14 dB; probe M5 3.52 - best reading, under F)
                {kGhostPeakLevelId, 0.20},           // 1400
                {kSeedId, 1.0 / 15.0},               // 2 index 1 -> "Seed 2"
            }},
        // ---- 7 row 16: M6 (T045) -----------------------------------------------
        // Entropy at full over a clean base: no mutation, no inharmonicity, no
        // decoherence, no drift and a regular breath, with the smear up so the
        // macro's decoherence route is heard.
        VoragoPresetDef{
            "Entropic Hum",
            "Machines",
            "A clean machine hum slowly coming apart, its partials skewing and fraying into noise.",
            C::M6Entropy,
            {},
            {
                {kMacroEntropyId, 1.0},               // 105 (displaced 1.0 >= 0.5)
                {kCloudMutationId, 0.0},              // 202
                {kCloudInharmonicityId, 0.0},         // 203 -> 0.0
                {kCloudDriftDepthId, 0.0},            // 204 -> 0 cents
                {kSmearAmountId, 0.70},               // 700
                {kSmearDecoherenceId, 0.0},           // 701
                {kLifeBreathingDepthId, 0.60},        // 1500
                {kLifeBreathingIrregularityId, 0.0},  // 1501
                {kCloudRichnessId, 0.85},             // 200
                {kBodyDampingId, 0.40},               // 1001
                {kSpaceMixId, 0.50},                  // 1105
                {kSpaceDecayId, 0.434349},            // 1102 -> 4 s
                {kEcologyMixId, 0.30},                // 500
                {kSeedId, 5.0 / 15.0},                // 2 index 5 -> "Seed 6"
            }},
        // ---- 7 row 17: M7 (T045) -----------------------------------------------
        // Pressure at full over an open base: the ecology nearly dry at a modest
        // loop gain and the f/2 sub loud (-6 dB), so the macro's loop, glue and
        // floor-lowering routes all register. Output saturation stays at its MB
        // base (C-5).
        VoragoPresetDef{
            "Pressure Front",
            "Machines",
            "A compressed wall of pressure: loops churning and the floor pulled tight into a "
            "dense, saturated hum.",
            C::M7Pressure,
            {},
            {
                {kMacroPressureId, 1.0},           // 106 (displaced 1.0 >= 0.5)
                {kEcologyMixId, 0.05},             // 500 Pressure raises it from here
                {kEcologyLoopGainId, 0.50},        // 501 -> 0.45
                {kEcologyLoop1FilterModeId, 0.5},  // 511 index 1 -> Bandpass
                {kSubDiv2LevelId, 54.0 / 66.0},    // 610 -> -6 dB
                {kCloudRichnessId, 0.85},          // 200
                {kCloudTiltId, 14.0 / 24.0},       // 201 -> +2 dB
                {kBodyResonanceId, 0.85},          // 1002
                {kSpaceSizeId, 0.30},              // 1100
                {kSpaceMixId, 0.60},               // 1105
                {kGhostPeakLevelId, 0.25},         // 1400
                {kSeedId, 6.0 / 15.0},             // 2 index 6 -> "Seed 7"
            }},
        // ---- 7 row 18: M8 + D7.2 (T045) ----------------------------------------
        // Weight at full. Sub tones: f/4 -9 dB > f/2 -30 dB > fifth-below -42 dB,
        // so D7.2 holds strictly and S5 is audible; the blend starts at body A so
        // Weight's pull toward the heavier body registers.
        VoragoPresetDef{
            "Weighted Deep",
            "Abyss",
            "Two octaves under the drone a sub swells up, pulling the whole cavern down with it.",
            C::M8Weight,
            {C::D7Div4},
            {
                {kMacroWeightId, 1.0},                 // 107 (displaced 1.0 >= 0.5)
                {kSubDiv4LevelId, 51.0 / 66.0},        // 611 -> -9 dB, strictly loudest
                {kSubDiv2LevelId, 30.0 / 66.0},        // 610 -> -30 dB
                {kSubFifthBelowLevelId, 18.0 / 66.0},  // 612 -> -42 dB
                {kBodyBlendId, 0.0},                   // 1000 body A, Weight blends toward B
                {kBodyDampingId, 0.10},                // 1001
                {kCloudTiltId, 14.0 / 24.0},           // 201 -> +2 dB
                {kCloudRichnessId, 0.80},              // 200 (sweep 2 probe: M8 4.85 with richness 0.8 / space mix 0.2; 2.78 as first authored)
                {kSpaceDarknessId, 0.95},              // 1101
                {kSpaceDecayId, 0.710434},             // 1102 -> 15 s
                {kSpaceMixId, 0.20},                   // 1105
                {kNoiseLevelId, 60.0 / 108.0},         // 300 -> -36 dB
                {kGhostPeakLevelId, 0.25},             // 1400
                {kSeedId, 7.0 / 15.0},                 // 2 index 7 -> "Seed 8"
            }},
        // ---- 7 row 19: M9 + D4.10-D4.12, S1 (T045) -----------------------------
        // Fog at full over a clear base (no smear, a quiet ghost, no cavern fog,
        // a shallow tide). The noise bed at 0 dB (S1 verifies as a secondary)
        // with three Direct slots: Velvet, VinylRumble, RadioStatic.
        VoragoPresetDef{
            "Fogbound",
            "Ghosts",
            "Static and rumble drifting through a thick fog, the drone heard only as a ghost of itself.",
            C::M9Fog,
            {C::D4Type10, C::D4Type11, C::D4Type12, C::D4Type8, C::S1Noise},
            {
                {kMacroFogId, 1.0},                // 108 (displaced 1.0 >= 0.5)
                {kNoiseLevelId, 96.0 / 108.0},     // 300 -> 0 dB
                {kNoiseSlot0ModelId, 0.0},         // 310 index 0 -> Direct
                {kNoiseSlot0TypeId, 9.0 / 11.0},   // 320 index 9 -> Velvet (D4.10)
                {kNoiseSlot1ModelId, 0.0},         // 311 index 0 -> Direct
                {kNoiseSlot1TypeId, 10.0 / 11.0},  // 321 index 10 -> VinylRumble (D4.11)
                {kNoiseSlot2ModelId, 0.0},         // 312 index 0 -> Direct (its default)
                {kNoiseSlot2TypeId, 1.0},          // 322 index 11 -> RadioStatic (D4.12)
                {kNoiseSlot3TypeId, 7.0 / 11.0},   // 323 index 7 -> Violet (D4.8)
                {kNoiseSlot3ModelId, 0.0},         // 313 index 0 -> Direct (sweep 3: Violet moved here from Swarm Breath)
                {kSmearAmountId, 0.0},             // 700 Fog smears from here
                {kGhostPeakLevelId, 0.30},         // 1400 Fog raises it from here
                {kGhostBlurId, 0.50},              // 1401
                {kSpaceFogId, 0.0},                // 1103
                {kLifeTidalDepthId, 0.10},         // 1502
                {kSpaceMixId, 0.90},               // 1105 (sweep 2 probe: M9 1.70 - best reading, under F; 1.41 at 0.80)
                {kCloudRichnessId, 0.60},          // 200
                {kSeedId, 9.0 / 15.0},             // 2 index 9 -> "Seed 10"
            }},
        // ---- 7 row 20: M10 + D13.1 (T045) --------------------------------------
        // Life at 0.7 over a slow colony: event rate 0.2x (D13.1 <= 0.3), the
        // ecosystem in full (S7), a slow bloom spawn, and the colony's
        // destinations (bloom, resonance, wake, loops) audible.
        VoragoPresetDef{
            "Teeming",
            "Organisms",
            "A dense colony teeming in the dark, many small lives waking slowly and feeding on the drone.",
            C::M10Life,
            {C::D13SlowEvents},
            {
                {kMacroLifeId, 0.70},            // 109 (displaced 0.7 >= 0.5)
                {kEventsRateScaleId, 0.150515},  // 800 -> 0.2x (D13.1 <= 0.3)
                {kEcosystemDepthId, 1.0},        // 900
                {kBloomDepthId, 0.85},           // 1300
                {kBloomSpawnRateId, 0.30},       // 1301 a slow base, Life raises it
                {kResonanceMixId, 0.60},         // 401
                {kNoiseWakeId, 0.65},            // 301
                {kEcologyMixId, 0.25},           // 500
                {kCloudMutationId, 0.30},        // 202
                {kSpaceDecayId, 0.519041},       // 1102 -> 6 s
                {kSeedId, 11.0 / 15.0},          // 2 index 11 -> "Seed 12"
            }},
        // ---- 7 row 21: M11 (T045) ----------------------------------------------
        // Depth at full over a small, short space (2 s, size 0.2), so the macro's
        // size and +25 s decay routes open the cavern around the drone.
        VoragoPresetDef{
            "Endless Descent",
            "Caverns",
            "A drone falling into a cavern that keeps opening beneath it, each echo farther "
            "down than the last.",
            C::M11Depth,
            {},
            {
                {kMacroDepthId, 1.0},            // 110 (displaced 1.0 >= 0.5)
                {kSpaceSizeId, 0.20},            // 1100 Depth enlarges from here
                {kSpaceDecayId, 0.289566},       // 1102 -> 2 s, Depth lengthens from here
                {kSpaceEarlySizeId, 0.0},        // 1110 -> 80 ms
                {kSpaceEarlyLevelId, 0.40},      // 1111
                {kSpaceDensityId, 0.50},         // 1107
                {kSpaceDimensionalityId, 0.85},  // 1108
                {kSpaceDarknessId, 0.65},        // 1101
                {kCloudRichnessId, 0.55},        // 200
                {kResonanceMixId, 0.30},         // 401
                {kEnvelopeReleaseId, 0.883054},  // 1205 -> 40 s
                {kBloomDepthId, 0.40},           // 1300
                {kSeedId, 12.0 / 15.0},          // 2 index 12 -> "Seed 13"
            }},
        // ---- 7 row 22: M12 + D7.1 (T045) ---------------------------------------
        // Mass at full. Sub tones: f/2 -6 dB > f/4 -24 dB > fifth-below -30 dB
        // (the latter two registered defaults), so D7.1 holds strictly and S5 is
        // audible; the body starts at a low resonance and the network loud, so
        // Mass's body and network routes register.
        VoragoPresetDef{
            "Monolith",
            "Drones",
            "A single vast mass of sound, body and sub fused into one unmoving block of stone.",
            C::M12Mass,
            {C::D7Div2},
            {
                {kMacroMassId, 1.0},             // 111 (displaced 1.0 >= 0.5)
                {kSubDiv2LevelId, 54.0 / 66.0},  // 610 -> -6 dB, strictly loudest
                {kBodyResonanceId, 0.40},        // 1002 Mass raises it from here
                {kBodyDampingId, 0.35},          // 1001
                {kResonanceMixId, 0.75},         // 401 Mass lowers it from here
                {kCloudRichnessId, 0.80},        // 200
                {kCloudTiltId, 6.0 / 24.0},      // 201 -> -6 dB
                {kSpaceSizeId, 0.60},            // 1100
                {kSpaceMixId, 0.10},                 // 1105 (sweep 2 probe: M12 3.46 - best reading, under F; 2.09 at the default mix)
                {kNoiseLevelId, 66.0 / 108.0},   // 300 -> -30 dB
                {kGhostPeakLevelId, 0.30},       // 1400
                {kLifeBreathingDepthId, 0.15},   // 1500
                {kSeedId, 4.0 / 15.0},           // 2 index 4 -> "Seed 5"
            }},
        // ---- 7 rows 23-27: E1-E5 (T046) -----------------------------------------
        // Each E row (spec C-2.1 Group E, plan 6.9): Ecosystem Depth 1.0, the
        // route's destination section prominent, the other four destinations
        // quiet (bloom 0.10, resonance 0.10, noise -42 dB, ecology 0.03, ghost
        // 0.10) so the route-isolated pair R_k / R_k0 hears route k. No macro is
        // displaced (a macro-carried route fails, C-2.1): Life stays 0, so the
        // 900 -> 0 twin really silences the colony (Life's row adds to 900's base,
        // vorago_macro_matrix.h:630-635). Event rate sits below 1x: the
        // schedulers' lanes are depth-independent and land on the same
        // destinations (vorago_voice.h:2043-2067), so fewer events leave more of
        // each destination's motion to the colony.

        // ---- 7 row 23: E1 (T046) -------------------------------------------------
        // Bloom at a mid base (the colony's Partial lane is ADDED to it, clamped at
        // 1, vorago_voice.h:2115-2116) over a sparse cloud with a 0.01 Hz spawn, so
        // the budded partials swell and fade with the colony. Mutation sits high:
        // the same lane adds to it, and near its clamp that half of the route is
        // small in R_1 and R_0 alike, leaving the bloom to carry the difference.
        VoragoPresetDef{
            "Bloom Colony",
            "Organisms",
            "A sparse drone where a hidden colony swells new partials into bloom and lets them wither.",
            C::E1PartialBloom,
            {},
            {
                {kEcosystemDepthId, 1.0},        // 900
                {kBloomDepthId, 0.50},           // 1300 room above for the colony
                {kBloomSpawnRateId, 0.742386},   // 1301 -> 0.01 Hz
                {kCloudMutationId, 0.85},        // 202 near its clamp
                {kCloudRichnessId, 0.40},        // 200 sparse, so the buds stand out
                {kCloudStereoSpreadId, 0.70},    // 205
                {kCloudTiltId, 10.0 / 24.0},     // 201 -> -2 dB
                {kResonanceMixId, 0.10},         // 401 quiet
                {kNoiseLevelId, 54.0 / 108.0},   // 300 -> -42 dB, quiet
                {kEcologyMixId, 0.03},           // 500 quiet
                {kGhostPeakLevelId, 0.10},       // 1400 quiet
                {kEventsRateScaleId, 0.349485},  // 800 -> 0.5x
                {kLifeBreathingDepthId, 0.50},   // 1500
                {kSpaceMixId, 0.55},             // 1105
                {kSpaceDecayId, 0.710434},       // 1102 -> 15 s
                {kSeedId, 0.0},                  // 2 index 0 -> "Seed 1"
            }},
        // ---- 7 row 24: E2 + D5.1 (T046) ------------------------------------------
        // The resonance network loud through the Free anchor (D5.1), where the
        // colony's Resonator lane wakes the peaks, raises their level (+18 dB span)
        // and widens their wander (vorago_voice.h:2102, :2130-2151); a slow wander base
        // keeps the colony's motion the audible one.
        VoragoPresetDef{
            "Singing Colony",
            "Organisms",
            "Free-floating resonant peaks that a colony wakes into song, one voice rising as another fades.",
            C::E2ResonatorPeaks,
            {C::D5Free},
            {
                {kEcosystemDepthId, 1.0},            // 900
                {kResonanceMixId, 0.90},             // 401
                {kResonanceAnchorModeId, 0.0},       // 403 index 0 -> Free (D5.1)
                {kResonanceGravityId, 0.65},         // 400 -> +0.3
                {kResonanceWanderRateId, 0.147441},  // 402 -> 0.005 Hz
                {kBloomDepthId, 0.10},               // 1300 quiet
                {kNoiseLevelId, 54.0 / 108.0},       // 300 -> -42 dB, quiet
                {kEcologyMixId, 0.03},               // 500 quiet
                {kGhostPeakLevelId, 0.10},           // 1400 quiet
                {kCloudRichnessId, 0.60},            // 200
                {kCloudSpectralGravityId, 0.75},     // 206 -> +0.5
                {kBodyMixId, 0.55},                  // 1003
                {kEventsRateScaleId, 0.389076},      // 800 -> 0.6x
                {kSpaceSizeId, 0.75},                // 1100
                {kSpaceDecayId, 0.817134},           // 1102 -> 25 s
                {kSeedId, 2.0 / 15.0},               // 2 index 2 -> "Seed 3"
            }},
        // ---- 7 row 25: E3 + D3.4, D4.7-D4.9 (T046) --------------------------------
        // The noise bed at -6 dB with a sleeping wake (0.15), so the colony's Noise
        // lane both wakes the slots and lifts their level (+12 dB span,
        // vorago_voice.h:2124-2128): three Direct slots (Blue, Violet, Grey) and
        // the MetallicHiss slot (D3.4, slot 4's registered model, stored).
        VoragoPresetDef{
            "Swarm Breath",
            "Textures",
            "Hiss and glassy air stirred by a swarm, flaring and settling as the colony breathes.",
            C::E3NoiseWake,
            {C::D3MetallicHiss, C::D4Type7, C::D4Type8, C::D4Type9},
            {
                {kEcosystemDepthId, 1.0},         // 900
                {kNoiseLevelId, 90.0 / 108.0},    // 300 -> -6 dB
                {kNoiseWakeId, 0.15},             // 301 a sleeping wake
                {kNoiseSlot0ModelId, 0.0},        // 310 index 0 -> Direct
                {kNoiseSlot0TypeId, 6.0 / 11.0},  // 320 index 6 -> Blue (D4.7)
                {kNoiseSlot1ModelId, 0.0},        // 311 index 0 -> Direct
                {kNoiseSlot1TypeId, 7.0 / 11.0},  // 321 index 7 -> Violet (D4.8)
                {kNoiseSlot2ModelId, 0.0},        // 312 index 0 -> Direct (its default)
                {kNoiseSlot2TypeId, 8.0 / 11.0},  // 322 index 8 -> Grey (D4.9)
                {kNoiseSlot3ModelId, 1.0},        // 313 index 3 -> MetallicHiss (D3.4, its default)
                {kBloomDepthId, 0.10},            // 1300 quiet
                {kResonanceMixId, 0.10},          // 401 quiet
                {kEcologyMixId, 0.03},            // 500 quiet
                {kGhostPeakLevelId, 0.10},        // 1400 quiet
                {kCloudRichnessId, 0.35},         // 200 a thin tone under the swarm
                {kEventsRateScaleId, 0.422549},   // 800 -> 0.7x
                {kSpaceBreathId, 0.85},           // 1109
                {kSpaceDecayId, 0.480958},        // 1102 -> 5 s
                {kSeedId, 5.0 / 15.0},            // 2 index 5 -> "Seed 6"
            }},
        // ---- 7 row 26: E4 (T046) -------------------------------------------------
        // Ecology mix high at a moderate loop gain (0.54), so the colony's Feedback
        // lane has room to wake the loops, raise each loop's gain (+0.18 span) and
        // couple the ring pair (vorago_voice.h:2153-2165) without the base already
        // at its ceiling.
        VoragoPresetDef{
            "Feeding Loops",
            "Machines",
            "Feedback loops that wake and feed on one another, flaring into a grinding chorus "
            "and sinking back.",
            C::E4FeedbackLoopWake,
            {},
            {
                {kEcosystemDepthId, 1.0},          // 900
                {kEcologyMixId, 0.90},             // 500
                {kEcologyLoopGainId, 0.60},        // 501 -> 0.54
                {kEcologyLoop2FilterModeId, 0.5},  // 512 index 1 -> Bandpass
                {kBloomDepthId, 0.10},             // 1300 quiet
                {kResonanceMixId, 0.10},           // 401 quiet
                {kNoiseLevelId, 54.0 / 108.0},     // 300 -> -42 dB, quiet
                {kGhostPeakLevelId, 0.10},         // 1400 quiet
                {kCloudRichnessId, 0.55},          // 200
                {kCloudInharmonicityId, 0.45},     // 203 -> 0.045
                {kBodyDampingId, 0.45},            // 1001
                {kEventsRateScaleId, 0.389076},    // 800 -> 0.6x
                {kSpaceSizeId, 0.35},              // 1100
                {kSpaceMixId, 0.45},               // 1105
                {kSpaceDecayId, 0.434349},         // 1102 -> 4 s
                {kSeedId, 8.0 / 15.0},             // 2 index 8 -> "Seed 9"
            }},
        // ---- 7 row 27: E5 (T046) -------------------------------------------------
        // Ghost peak at full with event triggers left Off (their registered
        // default): the ghost level is ghostPeak x the voices' ghost request, which
        // the colony's Ghost lane drives (vorago_voice.h:2118,
        // vorago_engine.h:1499), so the bursts are the colony's. Reverse stays
        // under 0.5 (D11 is Choir of Absence's).
        VoragoPresetDef{
            "Haunted Colony",
            "Ghosts",
            "A dark room where a colony calls up ghosts of the drone in sudden, blurred bursts.",
            C::E5GhostBursts,
            {},
            {
                {kEcosystemDepthId, 1.0},            // 900
                {kGhostPeakLevelId, 1.0},            // 1400
                {kGhostBlurId, 0.35},                // 1401
                {kGhostReverseProbabilityId, 0.25},  // 1402 (under D11's 0.5)
                {kBloomDepthId, 0.10},               // 1300 quiet
                {kResonanceMixId, 0.10},             // 401 quiet
                {kNoiseLevelId, 54.0 / 108.0},       // 300 -> -42 dB, quiet
                {kEcologyMixId, 0.03},               // 500 quiet
                {kCloudRichnessId, 0.55},            // 200
                {kCloudStereoSpreadId, 0.25},        // 205
                {kCloudTiltId, 6.0 / 24.0},          // 201 -> -6 dB
                {kEventsRateScaleId, 0.349485},      // 800 -> 0.5x
                {kSpaceDarknessId, 0.95},            // 1101
                {kSpaceFogId, 0.65},                 // 1103
                {kSpaceDecayId, 0.551247},           // 1102 -> 7 s
                {kSeedId, 12.0 / 15.0},              // 2 index 12 -> "Seed 13"
            }},
        // ---- P5 (plan 6.16; 7 row 39): D8.2 ------------------------------------
        // Growth mode: A = growth duration = 60 s (stage times are zeroed in
        // Growth, vorago_voice.h:338-342).
        VoragoPresetDef{
            "Growth Ring",
            "Organisms",
            "One slow organic growth, a minute long, that keeps budding new partials as it "
            "breathes.",
            C::D8Growth,
            {},
            // Re-authored at gate G2 (2026-09-29, C-2.2 route). v1 (Life 0.5, bloom
            // 0.85, mutation 0.35) measured d_att 4.43 against d_Sus 8.43; v2 (every
            // trajectory-sensitive section quieted) 5.54 against 6.98. The cause is
            // the attack-window conjunct's construction: each render's Sus window
            // sits at its OWN attack span (plan 6.1, A = growth duration in Growth
            // mode, the stage-time sum in Standard), so with the registered stage
            // times (155 s) the reverted render's Sus was [160, 220] against the
            // Growth render's [65, 125] - two different minutes of the drone's life,
            // and d_Sus measured that offset, not the envelope. v3 stores stage times
            // summing to the growth duration (1 s + 20 s + 19 s + 20 s = 60 s), so the
            // two Sus windows coincide and the swell alone carries the difference: the
            // Standard walk jumps to 1.0 in one second where Growth swells for a
            // minute. (Growth mode ignores the stage times - they only shape P_rev.)
            {
                {kEnvelopeModeId, 1.0},                 // 1200 index 1 -> Growth
                {kEnvelopeGrowthDurationId, 0.855217},  // 1206 -> 60 s
                {kEnvelopeStage0TimeId, 0.491349},   // 1201 -> 1 s  (P_rev: a one-second jump)
                {kEnvelopeStage1TimeId, 0.809284},   // 1202 -> 20 s
                {kEnvelopeStage2TimeId, 0.803826},   // 1203 -> 19 s
                {kEnvelopeStage3TimeId, 0.809284},   // 1204 -> 20 s  (sum 60 s = the growth duration)
                {kBloomDepthId, 0.30},                  // 1300 buds, but few
                {kBloomSpawnRateId, 0.50},              // 1301 slower spawn
                {kMacroLifeId, 0.20},                   // 109 a still colony
                {kEcosystemDepthId, 0.15},              // 900 the colony barely steers
                {kLifeBreathingDepthId, 0.30},          // 1500
                {kLifeBreathingIrregularityId, 0.15},   // 1501 regular breath
                {kLifeTidalDepthId, 0.10},              // 1502
                {kCloudMutationId, 0.10},               // 202
                {kCloudDriftDepthId, 0.20},             // 204 -> 10 cents
                {kCloudSpectralGravityId, 0.40},        // 206 -> -0.2
                {kGhostPeakLevelId, 0.10},              // 1400 the ghost captures little
                {kSmearAmountId, 0.20},                 // 700
                {kSeedId, 9.0 / 15.0},                  // 2 index 9 -> "Seed 10"
            }},
        // ---- P6 (plan 6.16; 7 row 30): D1.Glass --------------------------------
        // Material A = Glass at blend 0.2 (A weight 0.8; credited at blend <= 0.65,
        // spec C-2.1 D1); body mix stays at its registered 1.0 so the body is heard.
        VoragoPresetDef{
            "Glass Well",
            "Caverns",
            "A ringing glass shaft: a bright, barely damped body singing into a pale cavern.",
            C::D1Glass,
            {},
            // Re-authored at gate G2 (2026-09-29, C-2.2 route). v1 (blend 0.2, a 25 s
            // dark cavern, ghost 0.4) measured the Glass-vs-default material twin at
            // d 3.54 against F = 4.0; v2 (body A alone, damping 0.05, a richer cloud,
            // an 8 s paler cavern) 3.53 - the cavern's wet share still carried most
            // of what the descriptor hears; v3 (a quarter wet, body mix explicit) 3.93.
            // v4 takes the cavern to a tenth, the noise bed and the ghost out of the
            // way and rings the glass with a richer cloud, so the dry glass body is
            // what the listener hears.
            {
                {kBodyMaterialAId, 0.0},          // 1004 index 0 -> Glass
                {kBodyBlendId, 0.0},              // 1000 body A alone
                {kBodyMixId, 1.0},                // 1003 the body path in full
                {kBodyResonanceId, 0.98},         // 1002
                {kBodyDampingId, 0.05},           // 1001 barely damped
                {kSpaceMixId, 0.10},              // 1105 a tenth wet: the dry body leads
                {kCloudTiltId, 0.5},              // 201 -> 0 dB
                {kCloudRichnessId, 0.80},         // 200 more partials to ring the glass
                {kCloudInharmonicityId, 0.40},    // 203 -> 0.04
                {kNoiseLevelId, 56.0 / 108.0},    // 300 -> -40 dB
                {kSpaceDarknessId, 0.35},         // 1101 a paler cavern
                {kSpaceSizeId, 0.45},             // 1100 closer
                {kSpaceDecayId, 0.579},           // 1102 -> 8 s
                {kSpaceEarlyAbsorptionId, 0.50},  // 1112
                {kGhostPeakLevelId, 0.0},         // 1400 no ghost
                {kLifeTidalDepthId, 0.20},        // 1502
                {kSeedId, 12.0 / 15.0},           // 2 index 12 -> "Seed 13"
            }},
        // ---- 7 rows 31-38: the D1 material primaries (T047) ------------------
        // Each follows Glass Well v4's recipe (the G2-measured route to the D1
        // per-material reversion, plan 6.7): the material alone on its side
        // (blend 0.0 on A, 1.0 on B; weight 1.0 >= 0.35), body mix at its
        // registered 1.0, the cavern at most a quarter wet, noise and ghost out of
        // the way, so the dry body leads and S10 verifies. The side is chosen so
        // the reversion lands far from the material: the bright and metallic
        // materials sit on A (reverted to StoneChamber), the dark stone and wood
        // materials on B (reverted to SteelTank).

        // ---- 7 row 31: D1.Strings (T047) ---------------------------------------
        VoragoPresetDef{
            "Strung Abyss",
            "Drones",
            "Vast slack strings stretched across a chasm, humming a low, dark, sustained chord.",
            C::D1Strings,
            {},
            {
                {kBodyMaterialAId, 0.1},         // 1004 index 1 -> Strings
                {kBodyBlendId, 0.0},             // 1000 body A alone
                {kBodyMixId, 1.0},               // 1003 the body path in full
                {kBodyResonanceId, 0.99},       // 1002 (sweep 2 probe: D1.2 15.88 with resonance 0.99 / damping 0.02 / tilt +8 dB / space mix 0.05; 2.23 as first authored)
                {kBodyDampingId, 0.02},         // 1001
                {kCloudRichnessId, 0.90},        // 200 many partials to excite the strings
                {kCloudTiltId, 1.0},            // 201 -> +8 dB
                {kCloudInharmonicityId, 0.0},    // 203 -> 0.0, a harmonic source
                {kCloudDriftDepthId, 0.04},      // 204 -> 2 cents
                {kCloudStereoSpreadId, 0.20},    // 205 a narrow, central source
                {kResonanceMixId, 0.15},         // 401
                {kNoiseLevelId, 54.0 / 108.0},   // 300 -> -42 dB
                {kSubDiv2LevelId, 48.0 / 66.0},  // 610 -> -12 dB
                {kSpaceMixId, 0.05},            // 1105 the strings dry
                {kSpaceSizeId, 0.70},            // 1100
                {kSpaceDarknessId, 0.95},        // 1101
                {kSpaceDecayId, 0.663824},       // 1102 -> 12 s
                {kGhostPeakLevelId, 0.05},       // 1400
                {kLifeTidalDepthId, 0.20},       // 1502
                {kEcosystemDepthId, 0.0},        // 900 (E1-E5 skipped: no colony route to claim)
                {kSeedId, 3.0 / 15.0},           // 2 index 3 -> "Seed 4"
            }},
        // ---- 7 row 32: D1.MetalPlate (T047) ------------------------------------
        VoragoPresetDef{
            "Iron Plate",
            "Machines",
            "A great iron plate struck by the drone, ringing with clanging, inharmonic overtones.",
            C::D1MetalPlate,
            {},
            {
                {kBodyMaterialAId, 0.2},          // 1004 index 2 -> MetalPlate
                {kBodyBlendId, 0.0},              // 1000 body A alone
                {kBodyMixId, 1.0},                // 1003 the body path in full
                {kBodyResonanceId, 0.88},         // 1002
                {kBodyDampingId, 0.02},           // 1001 barely damped
                {kCloudRichnessId, 0.45},         // 200
                {kCloudTiltId, 0.5},              // 201 -> 0 dB
                {kCloudInharmonicityId, 0.85},    // 203 -> 0.085
                {kCloudMutationId, 0.0},          // 202
                {kCloudStereoSpreadId, 0.20},     // 205
                {kResonanceMixId, 0.05},          // 401
                {kNoiseLevelId, 48.0 / 108.0},    // 300 -> -48 dB
                {kSpaceMixId, 0.08},              // 1105 nearly dry
                {kSpaceSizeId, 0.30},             // 1100
                {kSpaceDarknessId, 0.50},         // 1101
                {kSpaceDecayId, 0.480959},        // 1102 -> 5 s
                {kSpaceEarlyAbsorptionId, 0.15},  // 1112
                {kGhostPeakLevelId, 0.0},         // 1400 no ghost
                {kLifeBreathingDepthId, 0.05},    // 1500
                {kEcosystemDepthId, 0.0},         // 900 (E1-E5 skipped: no colony route to claim)
                {kSeedId, 10.0 / 15.0},           // 2 index 10 -> "Seed 11"
            }},
        // ---- 7 row 33: D1.Chamber (T047) ---------------------------------------
        VoragoPresetDef{
            "Chamber Drone",
            "Drones",
            "A warm, closed wooden chamber filled by one breathing, slowly blooming drone.",
            C::D1Chamber,
            {},
            {
                {kBodyMaterialBId, 0.3},          // 1005 index 3 -> Chamber
                {kBodyBlendId, 1.0},              // 1000 body B alone
                {kBodyMixId, 1.0},                // 1003 the body path in full
                {kBodyResonanceId, 0.86},         // 1002
                {kBodyDampingId, 0.12},           // 1001
                {kCloudRichnessId, 0.55},         // 200
                {kCloudTiltId, 11.0 / 24.0},      // 201 -> -1 dB
                {kCloudStereoSpreadId, 0.25},     // 205 a close, narrow source
                {kCloudSpectralGravityId, 0.70},  // 206 -> +0.4
                {kResonanceMixId, 0.30},          // 401
                {kBloomDepthId, 0.35},            // 1300
                {kNoiseLevelId, 56.0 / 108.0},    // 300 -> -40 dB
                {kSpaceMixId, 0.18},              // 1105
                {kSpaceSizeId, 0.35},             // 1100
                {kSpaceDensityId, 0.40},          // 1107
                {kSpaceDecayId, 0.551240},        // 1102 -> 7 s
                {kGhostPeakLevelId, 0.10},        // 1400
                {kLifeBreathingDepthId, 0.50},    // 1500
                {kEcosystemDepthId, 0.0},         // 900 (E1-E5 skipped: no colony route to claim)
                {kSeedId, 13.0 / 15.0},           // 2 index 13 -> "Seed 14"
            }},
        // ---- 7 row 34: D1.Ice (T047) -------------------------------------------
        VoragoPresetDef{
            "Ice Shelf",
            "Textures",
            "A glittering shelf of ice, bright shards of tone cracking and shimmering in the cold.",
            C::D1Ice,
            {},
            {
                {kBodyMaterialAId, 0.4},        // 1004 index 4 -> Ice
                {kBodyBlendId, 0.0},            // 1000 body A alone
                {kBodyMixId, 1.0},              // 1003 the body path in full
                {kBodyResonanceId, 0.99},       // 1002 (sweep 2 probe: D1.5 4.37 with resonance 0.99 / damping 0.01 / tilt +8 dB; 3.14 as first authored)
                {kBodyDampingId, 0.01},         // 1001
                {kCloudRichnessId, 0.85},       // 200
                {kCloudTiltId, 1.0},            // 201 -> +8 dB, a bright source
                {kCloudInharmonicityId, 0.30},  // 203 -> 0.03
                {kCloudDriftDepthId, 0.40},     // 204 -> 20 cents
                {kCloudStereoSpreadId, 0.95},   // 205
                {kResonanceMixId, 0.20},        // 401
                {kNoiseLevelId, 52.0 / 108.0},  // 300 -> -44 dB
                {kSpaceMixId, 0.15},            // 1105
                {kSpaceSizeId, 0.55},           // 1100
                {kSpaceDarknessId, 0.25},       // 1101 a pale space
                {kSpaceDecayId, 0.625742},      // 1102 -> 10 s
                {kGhostPeakLevelId, 0.0},       // 1400 no ghost
                {kLifeTidalDepthId, 0.10},      // 1502
                {kEcosystemDepthId, 0.0},       // 900 (E1-E5 skipped: no colony route to claim)
                {kSeedId, 6.0 / 15.0},          // 2 index 6 -> "Seed 7"
            }},
        // ---- 7 row 35: D1.WoodenHull (T047) ------------------------------------
        VoragoPresetDef{
            "Hull Ark",
            "Drones",
            "The creaking wooden hull of an ark rolling on a dark sea, its timbers groaning with the "
            "drone.",
            C::D1WoodenHull,
            {},
            {
                {kBodyMaterialBId, 0.7},          // 1005 index 7 -> WoodenHull
                {kBodyBlendId, 1.0},              // 1000 body B alone
                {kBodyMixId, 1.0},                // 1003 the body path in full
                {kBodyResonanceId, 0.99},         // 1002 (sweep 2 probe: D1.8 6.73 with resonance 0.99 / damping 0.01 / tilt +8 dB / space mix 0.05 / richness 0.8; 0.21 as first authored)
                {kBodyDampingId, 0.01},           // 1001
                {kCloudRichnessId, 0.80},         // 200
                {kCloudTiltId, 1.0},              // 201 -> +8 dB
                {kCloudSpectralGravityId, 0.30},  // 206 -> -0.4
                {kCloudMutationId, 0.30},         // 202
                {kResonanceMixId, 0.30},          // 401
                {kNoiseLevelId, 58.0 / 108.0},    // 300 -> -38 dB
                {kSpaceMixId, 0.05},              // 1105
                {kSpaceSizeId, 0.60},             // 1100
                {kSpaceDecayId, 0.579132},        // 1102 -> 8 s
                {kSpaceBreathId, 0.85},           // 1109 a swell
                {kGhostPeakLevelId, 0.08},        // 1400
                {kLifeTidalDepthId, 0.65},        // 1502 a slow roll
                {kEcosystemDepthId, 0.0},         // 900 (E1-E5 skipped: no colony route to claim)
                {kSeedId, 14.0 / 15.0},           // 2 index 14 -> "Seed 15"
            }},
        // ---- 7 row 36: D1.CathedralColumn (T047) -------------------------------
        VoragoPresetDef{
            "Column Hymn",
            "Caverns",
            "Tall stone columns in a sunken cathedral, each one singing its own grave, sustained hymn.",
            C::D1CathedralColumn,
            {},
            {
                {kBodyMaterialBId, 0.8},         // 1005 index 8 -> CathedralColumn
                {kBodyBlendId, 1.0},             // 1000 body B alone
                {kBodyMixId, 1.0},               // 1003 the body path in full
                {kBodyResonanceId, 0.93},        // 1002
                {kBodyDampingId, 0.14},          // 1001
                {kCloudRichnessId, 0.95},        // 200
                {kCloudTiltId, 10.0 / 24.0},     // 201 -> -2 dB
                {kCloudStereoSpreadId, 0.75},    // 205
                {kResonanceMixId, 0.35},         // 401
                {kBloomDepthId, 0.25},           // 1300
                {kNoiseLevelId, 50.0 / 108.0},   // 300 -> -46 dB
                {kSpaceMixId, 0.25},             // 1105 a quarter wet
                {kSpaceSizeId, 0.90},            // 1100
                {kSpaceDarknessId, 0.65},        // 1101
                {kSpaceDecayId, 0.887416},       // 1102 -> 35 s
                {kSpaceDimensionalityId, 0.85},  // 1108
                {kGhostPeakLevelId, 0.12},       // 1400
                {kLifeBreathingDepthId, 0.15},   // 1500
                {kEcosystemDepthId, 0.0},        // 900 (E1-E5 skipped: no colony route to claim)
                {kSeedId, 0.0},                  // 2 index 0 -> "Seed 1"
            }},
        // ---- 7 row 37: D1.CavernWall (T047) ------------------------------------
        VoragoPresetDef{
            "Cavern Wall",
            "Caverns",
            "Rough, wet rock walls close around a heavy drone, thudding back a dull, massive resonance.",
            C::D1CavernWall,
            {},
            {
                {kBodyMaterialBId, 0.9},         // 1005 index 9 -> CavernWall
                {kBodyBlendId, 1.0},             // 1000 body B alone
                {kBodyMixId, 1.0},               // 1003 the body path in full
                {kBodyResonanceId, 0.99},       // 1002 (sweep 2 probe: D1.10 10.06 with resonance 0.99 / damping 0.01 / tilt +8 dB; 1.36 as first authored)
                {kBodyDampingId, 0.01},         // 1001
                {kCloudRichnessId, 0.50},        // 200
                {kCloudTiltId, 1.0},            // 201 -> +8 dB, the wall lit from above
                {kCloudDriftDepthId, 0.02},      // 204 -> 1 cent
                {kResonanceMixId, 0.15},         // 401
                {kSubDiv2LevelId, 51.0 / 66.0},  // 610 -> -9 dB
                {kNoiseLevelId, 46.0 / 108.0},   // 300 -> -50 dB
                {kSpaceMixId, 0.20},             // 1105
                {kSpaceSizeId, 0.75},            // 1100
                {kSpaceDarknessId, 0.98},        // 1101
                {kSpaceDecayId, 0.663824},       // 1102 -> 12 s
                {kSpaceEarlySizeId, 1.0},        // 1110 -> 300 ms
                {kSpaceEarlyLevelId, 0.95},      // 1111
                {kGhostPeakLevelId, 0.05},       // 1400
                {kEcosystemDepthId, 0.0},        // 900 (E1-E5 skipped: no colony route to claim)
                {kSeedId, 7.0 / 15.0},           // 2 index 7 -> "Seed 8"
            }},
        // ---- 7 row 38: D1.GlassSphere (T047) -----------------------------------
        VoragoPresetDef{
            "Glass Sphere",
            "Ghosts",
            "A hollow sphere of glass drifting in fog, ringing faintly with the ghost of a far-off drone.",
            C::D1GlassSphere,
            {},
            {
                {kBodyMaterialAId, 1.0},             // 1004 index 10 -> GlassSphere
                {kBodyBlendId, 0.0},                 // 1000 body A alone
                {kBodyMixId, 1.0},                   // 1003 the body path in full
                {kBodyResonanceId, 0.995},           // 1002 (sweep 2 probe: D1.11 4.03 with resonance 0.995 / damping 0.005 / tilt +8 dB / space mix 0 / richness 0.5; 2.46 as first authored)
                {kBodyDampingId, 0.005},             // 1001
                {kCloudRichnessId, 0.50},            // 200
                {kCloudTiltId, 1.0},                 // 201 -> +8 dB
                {kCloudInharmonicityId, 0.20},       // 203 -> 0.02
                {kCloudStereoSpreadId, 0.85},        // 205
                {kResonanceMixId, 0.25},             // 401
                {kNoiseLevelId, 48.0 / 108.0},       // 300 -> -48 dB
                {kSpaceMixId, 0.0},                  // 1105 dry
                {kSpaceFogId, 0.60},                 // 1103
                {kSpaceDarknessId, 0.45},            // 1101
                {kSpaceDecayId, 0.710434},           // 1102 -> 15 s
                {kGhostPeakLevelId, 0.20},           // 1400
                {kGhostBlurId, 0.55},                // 1401
                {kGhostReverseProbabilityId, 0.30},  // 1402 (under D11's 0.5)
                {kLifeBreathingDepthId, 0.55},       // 1500
                {kEcosystemDepthId, 0.0},            // 900 (E1-E5 skipped: no colony route to claim)
                {kSeedId, 2.0 / 15.0},               // 2 index 2 -> "Seed 3"
            }},
        // ---- 7 row 40: D9.1 (T047) ---------------------------------------------
        // Stage times 0.5 + 1.5 + 3 + 4 s = 9 s (D9.1 A <= 10 s). The attack-window
        // reversion (plan 6.8) resets 1201-1204 to their registered 155 s, so the
        // two Sus windows sit at different minutes of the drone's life (Growth
        // Ring's G2 lesson): every trajectory-sensitive section (bloom, colony,
        // life modulators, mutation, drift, ghost) is kept quiet so d_Sus stays
        // small and the sudden onset carries d_att.
        VoragoPresetDef{
            "Sudden Chasm",
            "Abyss",
            "The ground gives way at once: a dark, full drone that opens beneath you in seconds.",
            C::D9FastAttack,
            {},
            {
                {kEnvelopeStage0TimeId, 0.418602},     // 1201 -> 0.5 s
                {kEnvelopeStage1TimeId, 0.534165},     // 1202 -> 1.5 s
                {kEnvelopeStage2TimeId, 0.607608},     // 1203 -> 3 s
                {kEnvelopeStage3TimeId, 0.638148},     // 1204 -> 4 s  (A = 9 s)
                {kEnvelopeReleaseId, 0.809284},        // 1205 -> 20 s
                {kMacroDarknessId, 0.40},              // 100 (under M1's 0.5 conjunct)
                {kSubDiv2LevelId, 54.0 / 66.0},        // 610 -> -6 dB
                {kCloudRichnessId, 0.55},              // 200
                {kCloudTiltId, 6.0 / 24.0},            // 201 -> -6 dB
                {kCloudMutationId, 0.03},              // 202
                {kCloudDriftDepthId, 0.04},            // 204 -> 2 cents
                {kBloomDepthId, 0.10},                 // 1300 few buds
                {kEcosystemDepthId, 0.0},              // 900 no colony
                {kLifeBreathingDepthId, 0.08},         // 1500
                {kLifeBreathingIrregularityId, 0.05},  // 1501
                {kLifeTidalDepthId, 0.05},             // 1502
                {kGhostPeakLevelId, 0.15},             // 1400
                {kSpaceSizeId, 0.95},                  // 1100
                {kSpaceDarknessId, 0.95},              // 1101
                {kSeedId, 5.0 / 15.0},                 // 2 index 5 -> "Seed 6"
            }},
        // ---- T043 ruling rows: the D3 noise-model primaries (T047) -------------
        // One showcase preset per noise model (spec Clarifications "T043
        // ruling", N = 42): every slot holds the model and the noise bed sits at
        // or near 0 dB so S1 (the D3 conjunct) verifies. The model slot defaults
        // (slot 1 FilteredWind, 2 GranularDust, 3 Direct, 4 MetallicHiss) mean any
        // preset with audible noise also holds all four models, so each row
        // claims a secondary set no other preset holds together (its FR-011a
        // witness).

        // ---- D3.1 Direct (T047) ------------------------------------------------
        VoragoPresetDef{
            "Dead Air",
            "Ghosts",
            "A dead channel hissing in an empty room, tape hiss and static where a voice should be.",
            C::D3Direct,
            {C::D4Type1, C::D4Type2, C::D4Type3, C::D4Type9},  // sweep 2: Velvet / VinylCrackle / Blue are near-silent as a bed
            {
                {kNoiseLevelId, 1.0},             // 300 -> +12 dB (sweep 2 probe: S1 4.10 at +12 / wake 1.0 with the loud types; 0.12 as first authored)
                {kNoiseSlot0ModelId, 0.0},        // 310 index 0 -> Direct
                {kNoiseSlot0TypeId, 0.0},         // 320 index 0 -> White (D4.1)
                {kNoiseSlot1ModelId, 0.0},        // 311 index 0 -> Direct
                {kNoiseSlot1TypeId, 1.0 / 11.0},  // 321 index 1 -> Pink (D4.2)
                {kNoiseSlot2ModelId, 0.0},        // 312 index 0 -> Direct (its default)
                {kNoiseSlot2TypeId, 2.0 / 11.0},  // 322 index 2 -> TapeHiss (D4.3)
                {kNoiseSlot3ModelId, 0.0},        // 313 index 0 -> Direct
                {kNoiseSlot3TypeId, 8.0 / 11.0},  // 323 index 8 -> Grey (D4.9; sweep 3: moved here from Swarm Breath - Brown stays verified on Erosion)
                {kNoiseWakeId, 1.0},              // 301 (sweep 2 probe: 2.55 at wake 0.6)
                {kCloudRichnessId, 0.20},         // 200 a thin tone under the dead channel (sweep 2 probe: S1 8.81 with richness 0.2 / body mix 0.3; 4.10 without)
                {kBodyMixId, 0.30},               // 1003
                {kNoiseWanderRateId, 0.325257},   // 302 -> 0.2 Hz
                {kCloudTiltId, 4.0 / 24.0},       // 201 -> -8 dB
                {kResonanceMixId, 0.15},          // 401
                {kGhostPeakLevelId, 0.45},        // 1400
                {kGhostBlurId, 0.40},             // 1401
                {kSpaceMixId, 0.60},              // 1105
                {kSpaceFogId, 0.70},              // 1103
                {kEcosystemDepthId, 0.0},         // 900 (E1-E5 skipped: no colony route to claim)
                {kSeedId, 4.0 / 15.0},            // 2 index 4 -> "Seed 5"
            }},
        // ---- D3.2 FilteredWind (T047) ------------------------------------------
        VoragoPresetDef{
            "Abyssal Wind",
            "Abyss",
            "Wind howling up out of a bottomless pit over a deep sub and slowly wandering peaks.",
            C::D3FilteredWind,
            {C::D7Div4, C::D5Free},
            {
                {kNoiseLevelId, 102.0 / 108.0},        // 300 -> +6 dB (sweep 2 probe: S1 4.59 at +6 / wake 1.0; 2.31 at 0 dB / 0.5)
                {kNoiseSlot0ModelId, 1.0 / 3.0},       // 310 index 1 -> FilteredWind (its default)
                {kNoiseSlot1ModelId, 1.0 / 3.0},       // 311 index 1 -> FilteredWind
                {kNoiseSlot2ModelId, 1.0 / 3.0},       // 312 index 1 -> FilteredWind
                {kNoiseSlot3ModelId, 1.0 / 3.0},       // 313 index 1 -> FilteredWind
                {kNoiseWakeId, 1.0},                   // 301
                {kNoiseWanderRateId, 0.174743},        // 302 -> 0.05 Hz
                {kSubDiv4LevelId, 54.0 / 66.0},        // 611 -> -6 dB, strictly loudest (D7.2)
                {kSubDiv2LevelId, 30.0 / 66.0},        // 610 -> -30 dB
                {kSubFifthBelowLevelId, 18.0 / 66.0},  // 612 -> -42 dB
                {kResonanceMixId, 0.70},               // 401 (S2, the D5.1 conjunct)
                {kResonanceAnchorModeId, 0.0},         // 403 index 0 -> Free (D5.1)
                {kMacroDarknessId, 0.40},              // 100 (under M1's 0.5 conjunct)
                {kCloudRichnessId, 0.40},              // 200
                {kCloudTiltId, 2.0 / 24.0},            // 201 -> -10 dB
                {kSpaceDarknessId, 0.95},              // 1101
                {kSpaceDecayId, 0.817134},             // 1102 -> 25 s
                {kGhostPeakLevelId, 0.20},             // 1400
                {kEcosystemDepthId, 0.0},              // 900 (E1-E5 skipped: no colony route to claim)
                {kSeedId, 1.0},                        // 2 index 15 -> "Seed 16"
            }},
        // ---- D3.3 GranularDust (T047) ------------------------------------------
        VoragoPresetDef{
            "Spore Drift",
            "Organisms",
            "Clouds of spores sifting through the dark, their grains drifting backwards into humming "
            "loops.",
            C::D3GranularDust,
            {C::D11GhostReverse, C::D6Bandpass},
            {
                {kNoiseLevelId, 96.0 / 108.0},         // 300 -> 0 dB (sweep 2 probe: S1 4.37 at 0 dB / wake 0.75; the dust drives the limiter above that: +12 dB reads 2.80)
                {kNoiseSlot0ModelId, 2.0 / 3.0},       // 310 index 2 -> GranularDust
                {kNoiseSlot1ModelId, 2.0 / 3.0},       // 311 index 2 -> GranularDust (its default)
                {kNoiseSlot2ModelId, 2.0 / 3.0},       // 312 index 2 -> GranularDust
                {kNoiseSlot3ModelId, 2.0 / 3.0},       // 313 index 2 -> GranularDust
                {kNoiseWakeId, 0.75},                  // 301
                {kGhostPeakLevelId, 0.90},             // 1400 (S9, the D11 conjunct)
                {kGhostReverseProbabilityId, 0.60},    // 1402 (D11 >= 0.5)
                {kEventsRateScaleId, 0.650515},        // 800 -> 2.0x, more ghost requests (the schedulers)
                {kEcologyMixId, 0.60},                 // 500 (S4, the D6.2 conjunct)
                {kEcologyLoopGainId, 0.65},            // 501 -> 0.585
                {kEcologyLoop1FilterModeId, 0.5},      // 511 index 1 -> Bandpass (D6.2)
                {kEcologyLoop3FilterModeId, 0.5},      // 513 index 1 -> Bandpass
                {kBloomDepthId, 0.75},                 // 1300
                {kMacroLifeId, 0.35},                  // 109
                {kCloudMutationId, 0.40},              // 202
                {kCloudRichnessId, 0.45},              // 200
                {kSpaceDecayId, 0.480959},             // 1102 -> 5 s
                {kEcosystemDepthId, 0.0},              // 900 (E1-E5 skipped: no colony route to claim)
                {kSeedId, 9.0 / 15.0},                 // 2 index 9 -> "Seed 10"
            }},
        // ---- D3.4 MetallicHiss (T047) ------------------------------------------
        VoragoPresetDef{
            "Steam Vent",
            "Machines",
            "Metallic steam hissing from rusted vents over a grinding deep sub and whistling loops.",
            C::D3MetallicHiss,
            {C::D6Highpass, C::D7Div4},
            {
                {kNoiseLevelId, 102.0 / 108.0},        // 300 -> +6 dB (sweep 2 probe: S1 5.67 at +6 / wake 1.0; 3.66 as first authored)
                {kNoiseSlot0ModelId, 1.0},             // 310 index 3 -> MetallicHiss
                {kNoiseSlot1ModelId, 1.0},             // 311 index 3 -> MetallicHiss
                {kNoiseSlot2ModelId, 1.0},             // 312 index 3 -> MetallicHiss
                {kNoiseSlot3ModelId, 1.0},             // 313 index 3 -> MetallicHiss (its default)
                {kNoiseWakeId, 1.0},                   // 301
                {kEcologyMixId, 0.70},                 // 500 (S4, the D6.3 conjunct)
                {kEcologyLoopGainId, 0.60},            // 501 -> 0.54
                {kEcologyLoop0FilterModeId, 1.0},      // 510 index 2 -> Highpass (D6.3)
                {kEcologyLoop2FilterModeId, 1.0},      // 512 index 2 -> Highpass
                {kEcologyLoop4FilterModeId, 1.0},      // 514 index 2 -> Highpass
                {kSubDiv4LevelId, 54.0 / 66.0},        // 611 -> -6 dB, strictly loudest (D7.2)
                {kSubDiv2LevelId, 30.0 / 66.0},        // 610 -> -30 dB
                {kSubFifthBelowLevelId, 18.0 / 66.0},  // 612 -> -42 dB
                {kCloudInharmonicityId, 0.70},         // 203 -> 0.07
                {kBodyDampingId, 0.60},                // 1001
                {kSpaceSizeId, 0.25},                  // 1100
                {kSpaceMixId, 0.35},                   // 1105
                {kEcosystemDepthId, 0.0},              // 900 (E1-E5 skipped: no colony route to claim)
                {kSeedId, 11.0 / 15.0},                // 2 index 11 -> "Seed 12"
            }},
    };
    return kPresets;
}

// ==============================================================================
// Info chunk
// ==============================================================================

/// The bytes PresetManager::savePreset writes (plugins/shared/src/preset/
/// preset_manager.cpp:265-277) with PlugInName "Vorago", PlugInCategory "Synth"
/// and MusicalCategory == MusicalInstrument == category; the Comment line is
/// present only for a non-empty description. Explicit `\n`, pure ASCII.
[[nodiscard]] inline std::string buildVoragoInfoXml(std::string_view name, std::string_view category,
                                                    std::string_view description) {
    std::string xml;
    xml += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml += "<MetaInfo>\n";
    xml += "  <Attr id=\"MediaType\" value=\"VstPreset\" type=\"string\"/>\n";
    xml += "  <Attr id=\"PlugInName\" value=\"Vorago\" type=\"string\"/>\n";
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
    if (!description.empty()) {
        xml += "  <Attr id=\"Comment\" value=\"";
        xml += description;
        xml += "\" type=\"string\"/>\n";
    }
    xml += "</MetaInfo>\n";
    return xml;
}

}  // namespace Vorago::PresetDefs
