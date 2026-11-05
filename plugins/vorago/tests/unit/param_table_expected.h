#pragma once

// ==============================================================================
// Vorago Phase 12 - checked-in expected parameter table (SC-018, SC-011 column)
// ==============================================================================
// T033 (specs/vorago-phase12-parameters/tasks.md). One row per registered ID (110;
// Phase 14 FR-074 / T017 added the ecosystem roster rows 901 and 902),
// in registration (band) order, transcribed from plan section 3.2 (titles, units,
// ranges, defaults, tapers), 3.3.1 (the n = 0.5 midpoints) and 6.4 (SC-011 class).
//
// The numbers were computed once, in double, from the plan's ranges and defaults by
// a one-off Node command (n0 = inverse map of the plain default; mid = forward map
// at n = 0.5: linear mn + (mx - mn) / 2, log sqrt(mn * mx), offset-log
// sqrt((mn + eps)(mx + eps)) - eps; discrete: the index indexFromNormalized(0.5)
// selects). They are NOT read from the pack headers: this table is the independent
// reference Vorago_ParameterInfoTable compares the controller and the packs against.
//
// Discrete rows (Taper::Discrete) carry min 0 / max (count - 1) in INDEX units, and
// `mid` is that index. Sustain Pedal (4) is a continuous [0, 1] value (>= 0.5 = down)
// stored by a linear handler, so it is tabulated as Linear.
// ==============================================================================

#include "parameters/param_mapping.h"  // Vorago::Taper

#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/vsttypes.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace VoragoTest {

/// SC-011 scope class (plan section 6.4).
enum class Sc011 : std::uint8_t {
    A,  ///< continuous sweep, clauses 1-4
    B,  ///< discrete step sweep, clauses 1-4
    C,  ///< clause 4 only
};

struct ExpectedParamRow {
    Steinberg::Vst::ParamID id;
    const char* title;
    const char* units;
    Steinberg::int32 stepCount;
    Steinberg::int32 flags;
    double defaultNormalized;  ///< n0, compared within 1e-9
    ::Vorago::Taper taper;
    double minPlain;  ///< plain min (index 0 for discrete rows)
    double maxPlain;  ///< plain max (count - 1 for discrete rows)
    double eps;       ///< offset-log epsilon; 0 otherwise
    double mid;       ///< plain value at n = 0.5 (the index for discrete rows)
    Sc011 sc011;
};

namespace detail_expected {
inline constexpr Steinberg::int32 kAuto = Steinberg::Vst::ParameterInfo::kCanAutomate;
inline constexpr Steinberg::int32 kList =
    Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList;
inline constexpr Steinberg::int32 kHidden =
    Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsHidden;
}  // namespace detail_expected

inline constexpr std::size_t kNumExpectedParams = 110;

// clang-format off
inline constexpr std::array<ExpectedParamRow, kNumExpectedParams> kExpectedParams = {{
    // id, title, units, stepCount, flags, n0, taper, min, max, eps, mid (n = 0.5), SC-011
    {.id = 0, .title = "Master Gain", .units = "dB", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.5, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 2.0, .eps = 0.0, .mid = 1.0, .sc011 = Sc011::A},
    {.id = 1, .title = "Polyphony", .units = "", .stepCount = 5, .flags = detail_expected::kList, .defaultNormalized = 0.6, .taper = ::Vorago::Taper::Discrete, .minPlain = 0.0, .maxPlain = 5.0, .eps = 0.0, .mid = 3.0, .sc011 = Sc011::C},
    {.id = 2, .title = "Seed", .units = "", .stepCount = 15, .flags = detail_expected::kList, .defaultNormalized = 0.0, .taper = ::Vorago::Taper::Discrete, .minPlain = 0.0, .maxPlain = 15.0, .eps = 0.0, .mid = 8.0, .sc011 = Sc011::C},
    {.id = 3, .title = "Output Saturation", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.12, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 4, .title = "Sustain Pedal", .units = "", .stepCount = 0, .flags = detail_expected::kHidden, .defaultNormalized = 0.0, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::C},
    {.id = 5, .title = "Channel Pressure", .units = "%", .stepCount = 0, .flags = detail_expected::kHidden, .defaultNormalized = 0.0, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 100, .title = "Darkness", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.0, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 101, .title = "Age", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.0, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 102, .title = "Density", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.0, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 103, .title = "Movement", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.0, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 104, .title = "Gravity", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.5, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 105, .title = "Entropy", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.0, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 106, .title = "Pressure", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.0, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 107, .title = "Weight", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.0, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 108, .title = "Fog", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.0, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 109, .title = "Life", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.0, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 110, .title = "Depth", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.0, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 111, .title = "Mass", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.0, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 200, .title = "Cloud Richness", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.7, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 201, .title = "Cloud Tilt", .units = "dB/oct", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.3333333333333333, .taper = ::Vorago::Taper::Linear, .minPlain = -12.0, .maxPlain = 12.0, .eps = 0.0, .mid = 0.0, .sc011 = Sc011::A},
    {.id = 202, .title = "Cloud Mutation", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.15, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 203, .title = "Cloud Inharmonicity", .units = "", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.15, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 0.1, .eps = 0.0, .mid = 0.05, .sc011 = Sc011::A},
    {.id = 204, .title = "Cloud Drift Depth", .units = "ct", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.16, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 50.0, .eps = 0.0, .mid = 25.0, .sc011 = Sc011::A},
    {.id = 205, .title = "Cloud Stereo Spread", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.45, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 206, .title = "Cloud Spectral Gravity", .units = "", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.55, .taper = ::Vorago::Taper::Linear, .minPlain = -1.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.0, .sc011 = Sc011::A},
    {.id = 300, .title = "Noise Level", .units = "dB", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.7222222222222222, .taper = ::Vorago::Taper::Linear, .minPlain = -96.0, .maxPlain = 12.0, .eps = 0.0, .mid = -42.0, .sc011 = Sc011::A},
    {.id = 301, .title = "Noise Wake", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.35, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 302, .title = "Noise Wander Rate", .units = "Hz", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.11928031367991558, .taper = ::Vorago::Taper::Log, .minPlain = 0.01, .maxPlain = 100.0, .eps = 0.0, .mid = 1.0, .sc011 = Sc011::A},
    {.id = 310, .title = "Noise Slot 1 Model", .units = "", .stepCount = 3, .flags = detail_expected::kList, .defaultNormalized = 0.3333333333333333, .taper = ::Vorago::Taper::Discrete, .minPlain = 0.0, .maxPlain = 3.0, .eps = 0.0, .mid = 2.0, .sc011 = Sc011::B},
    {.id = 311, .title = "Noise Slot 2 Model", .units = "", .stepCount = 3, .flags = detail_expected::kList, .defaultNormalized = 0.6666666666666666, .taper = ::Vorago::Taper::Discrete, .minPlain = 0.0, .maxPlain = 3.0, .eps = 0.0, .mid = 2.0, .sc011 = Sc011::B},
    {.id = 312, .title = "Noise Slot 3 Model", .units = "", .stepCount = 3, .flags = detail_expected::kList, .defaultNormalized = 0.0, .taper = ::Vorago::Taper::Discrete, .minPlain = 0.0, .maxPlain = 3.0, .eps = 0.0, .mid = 2.0, .sc011 = Sc011::B},
    {.id = 313, .title = "Noise Slot 4 Model", .units = "", .stepCount = 3, .flags = detail_expected::kList, .defaultNormalized = 1.0, .taper = ::Vorago::Taper::Discrete, .minPlain = 0.0, .maxPlain = 3.0, .eps = 0.0, .mid = 2.0, .sc011 = Sc011::B},
    {.id = 320, .title = "Noise Slot 1 Type", .units = "", .stepCount = 11, .flags = detail_expected::kList, .defaultNormalized = 0.45454545454545453, .taper = ::Vorago::Taper::Discrete, .minPlain = 0.0, .maxPlain = 11.0, .eps = 0.0, .mid = 6.0, .sc011 = Sc011::B},
    {.id = 321, .title = "Noise Slot 2 Type", .units = "", .stepCount = 11, .flags = detail_expected::kList, .defaultNormalized = 0.45454545454545453, .taper = ::Vorago::Taper::Discrete, .minPlain = 0.0, .maxPlain = 11.0, .eps = 0.0, .mid = 6.0, .sc011 = Sc011::B},
    {.id = 322, .title = "Noise Slot 3 Type", .units = "", .stepCount = 11, .flags = detail_expected::kList, .defaultNormalized = 0.45454545454545453, .taper = ::Vorago::Taper::Discrete, .minPlain = 0.0, .maxPlain = 11.0, .eps = 0.0, .mid = 6.0, .sc011 = Sc011::B},
    {.id = 323, .title = "Noise Slot 4 Type", .units = "", .stepCount = 11, .flags = detail_expected::kList, .defaultNormalized = 0.45454545454545453, .taper = ::Vorago::Taper::Discrete, .minPlain = 0.0, .maxPlain = 11.0, .eps = 0.0, .mid = 6.0, .sc011 = Sc011::B},
    {.id = 330, .title = "Noise Slot 1 Comb Freq", .units = "Hz", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.15921974703872435, .taper = ::Vorago::Taper::Log, .minPlain = 20.0, .maxPlain = 19845.0, .eps = 0.0, .mid = 630.0, .sc011 = Sc011::A},
    {.id = 331, .title = "Noise Slot 2 Comb Freq", .units = "Hz", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.15921974703872435, .taper = ::Vorago::Taper::Log, .minPlain = 20.0, .maxPlain = 19845.0, .eps = 0.0, .mid = 630.0, .sc011 = Sc011::A},
    {.id = 332, .title = "Noise Slot 3 Comb Freq", .units = "Hz", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.15921974703872435, .taper = ::Vorago::Taper::Log, .minPlain = 20.0, .maxPlain = 19845.0, .eps = 0.0, .mid = 630.0, .sc011 = Sc011::A},
    {.id = 333, .title = "Noise Slot 4 Comb Freq", .units = "Hz", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.15921974703872435, .taper = ::Vorago::Taper::Log, .minPlain = 20.0, .maxPlain = 19845.0, .eps = 0.0, .mid = 630.0, .sc011 = Sc011::A},
    {.id = 340, .title = "Noise Slot 1 Comb Spread", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.35, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 341, .title = "Noise Slot 2 Comb Spread", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.35, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 342, .title = "Noise Slot 3 Comb Spread", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.35, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 343, .title = "Noise Slot 4 Comb Spread", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.35, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 350, .title = "Noise Slot 1 Comb Feedback", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.6111111111111112, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 0.9, .eps = 0.0, .mid = 0.45, .sc011 = Sc011::A},
    {.id = 351, .title = "Noise Slot 2 Comb Feedback", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.6111111111111112, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 0.9, .eps = 0.0, .mid = 0.45, .sc011 = Sc011::A},
    {.id = 352, .title = "Noise Slot 3 Comb Feedback", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.6111111111111112, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 0.9, .eps = 0.0, .mid = 0.45, .sc011 = Sc011::A},
    {.id = 353, .title = "Noise Slot 4 Comb Feedback", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.8333333333333333, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 0.9, .eps = 0.0, .mid = 0.45, .sc011 = Sc011::A},
    {.id = 400, .title = "Resonance Gravity", .units = "", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.5, .taper = ::Vorago::Taper::Linear, .minPlain = -1.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.0, .sc011 = Sc011::A},
    {.id = 401, .title = "Resonance Mix", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.45, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 402, .title = "Resonance Wander Rate", .units = "Hz", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.43575558719297985, .taper = ::Vorago::Taper::Log, .minPlain = 0.002, .maxPlain = 1.0, .eps = 0.0, .mid = 0.044721359549995794, .sc011 = Sc011::A},
    {.id = 403, .title = "Resonance Anchor", .units = "", .stepCount = 2, .flags = detail_expected::kList, .defaultNormalized = 1.0, .taper = ::Vorago::Taper::Discrete, .minPlain = 0.0, .maxPlain = 2.0, .eps = 0.0, .mid = 1.0, .sc011 = Sc011::B},
    {.id = 500, .title = "Ecology Mix", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.15, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 501, .title = "Ecology Loop Gain", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.7999999999999999, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 0.9, .eps = 0.0, .mid = 0.45, .sc011 = Sc011::A},
    {.id = 510, .title = "Ecology Loop 1 Filter", .units = "", .stepCount = 2, .flags = detail_expected::kList, .defaultNormalized = 0.0, .taper = ::Vorago::Taper::Discrete, .minPlain = 0.0, .maxPlain = 2.0, .eps = 0.0, .mid = 1.0, .sc011 = Sc011::C},
    {.id = 511, .title = "Ecology Loop 2 Filter", .units = "", .stepCount = 2, .flags = detail_expected::kList, .defaultNormalized = 0.0, .taper = ::Vorago::Taper::Discrete, .minPlain = 0.0, .maxPlain = 2.0, .eps = 0.0, .mid = 1.0, .sc011 = Sc011::C},
    {.id = 512, .title = "Ecology Loop 3 Filter", .units = "", .stepCount = 2, .flags = detail_expected::kList, .defaultNormalized = 0.0, .taper = ::Vorago::Taper::Discrete, .minPlain = 0.0, .maxPlain = 2.0, .eps = 0.0, .mid = 1.0, .sc011 = Sc011::C},
    {.id = 513, .title = "Ecology Loop 4 Filter", .units = "", .stepCount = 2, .flags = detail_expected::kList, .defaultNormalized = 0.0, .taper = ::Vorago::Taper::Discrete, .minPlain = 0.0, .maxPlain = 2.0, .eps = 0.0, .mid = 1.0, .sc011 = Sc011::C},
    {.id = 514, .title = "Ecology Loop 5 Filter", .units = "", .stepCount = 2, .flags = detail_expected::kList, .defaultNormalized = 0.0, .taper = ::Vorago::Taper::Discrete, .minPlain = 0.0, .maxPlain = 2.0, .eps = 0.0, .mid = 1.0, .sc011 = Sc011::C},
    {.id = 515, .title = "Ecology Loop 6 Filter", .units = "", .stepCount = 2, .flags = detail_expected::kList, .defaultNormalized = 0.0, .taper = ::Vorago::Taper::Discrete, .minPlain = 0.0, .maxPlain = 2.0, .eps = 0.0, .mid = 1.0, .sc011 = Sc011::C},
    {.id = 600, .title = "Sub Level Offset", .units = "dB", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.5, .taper = ::Vorago::Taper::Linear, .minPlain = -24.0, .maxPlain = 24.0, .eps = 0.0, .mid = 0.0, .sc011 = Sc011::A},
    {.id = 601, .title = "Sub Tracking", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 1.0, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 610, .title = "Sub Div2 Level", .units = "dB", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.6363636363636364, .taper = ::Vorago::Taper::Linear, .minPlain = -60.0, .maxPlain = 6.0, .eps = 0.0, .mid = -27.0, .sc011 = Sc011::A},
    {.id = 611, .title = "Sub Div4 Level", .units = "dB", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.5454545454545454, .taper = ::Vorago::Taper::Linear, .minPlain = -60.0, .maxPlain = 6.0, .eps = 0.0, .mid = -27.0, .sc011 = Sc011::A},
    {.id = 612, .title = "Sub Fifth-Below Level", .units = "dB", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.45454545454545453, .taper = ::Vorago::Taper::Linear, .minPlain = -60.0, .maxPlain = 6.0, .eps = 0.0, .mid = -27.0, .sc011 = Sc011::A},
    {.id = 700, .title = "Smear Amount", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.2, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 701, .title = "Smear Decoherence", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.2, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 702, .title = "Smear Tilt", .units = "", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.5, .taper = ::Vorago::Taper::Linear, .minPlain = -1.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.0, .sc011 = Sc011::A},
    {.id = 800, .title = "Event Rate", .units = "x", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.5, .taper = ::Vorago::Taper::Log, .minPlain = 0.1, .maxPlain = 10.0, .eps = 0.0, .mid = 1.0, .sc011 = Sc011::A},
    {.id = 900, .title = "Ecosystem Depth", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.85, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 901, .title = "Ecosystem Sync", .units = "", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.0, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 0.5, .eps = 0.0, .mid = 0.25, .sc011 = Sc011::A},
    {.id = 902, .title = "Ecosystem Self Affinity", .units = "", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.25, .taper = ::Vorago::Taper::Linear, .minPlain = -2.0, .maxPlain = 2.0, .eps = 0.0, .mid = 0.0, .sc011 = Sc011::A},
    {.id = 1000, .title = "Body Blend", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.35, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 1001, .title = "Body Damping", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.25, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 1002, .title = "Body Resonance", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.7, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 1003, .title = "Body Mix", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 1.0, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 1004, .title = "Body Material A", .units = "", .stepCount = 10, .flags = detail_expected::kList, .defaultNormalized = 0.5, .taper = ::Vorago::Taper::Discrete, .minPlain = 0.0, .maxPlain = 10.0, .eps = 0.0, .mid = 5.0, .sc011 = Sc011::B},
    {.id = 1005, .title = "Body Material B", .units = "", .stepCount = 10, .flags = detail_expected::kList, .defaultNormalized = 0.6, .taper = ::Vorago::Taper::Discrete, .minPlain = 0.0, .maxPlain = 10.0, .eps = 0.0, .mid = 5.0, .sc011 = Sc011::B},
    {.id = 1100, .title = "Space Size", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.5, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 1101, .title = "Space Darkness", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.8, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 1102, .title = "Space Decay", .units = "s", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.7705244525331133, .taper = ::Vorago::Taper::Log, .minPlain = 0.5, .maxPlain = 60.0, .eps = 0.0, .mid = 5.477225575051661, .sc011 = Sc011::A},
    {.id = 1103, .title = "Space Fog", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.3, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 1104, .title = "Space Damper Depth", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.35, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 1105, .title = "Space Mix", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 1.0, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 1106, .title = "Space Width", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 1.0, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 1107, .title = "Space Density", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.75, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 1108, .title = "Space Dimensionality", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.5, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 1109, .title = "Space Breath", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.5, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 1110, .title = "Space Early Size", .units = "ms", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.7653462773366764, .taper = ::Vorago::Taper::Log, .minPlain = 80.0, .maxPlain = 300.0, .eps = 0.0, .mid = 154.91933384829667, .sc011 = Sc011::A},
    {.id = 1111, .title = "Space Early Level", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.8, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 1112, .title = "Space Early Absorption", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.6, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 1113, .title = "Space Early Send", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.7, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 1114, .title = "Space Damper Rate", .units = "", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.6007619328947518, .taper = ::Vorago::Taper::OffsetLog, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.01, .mid = 0.09049875621120891, .sc011 = Sc011::A},
    {.id = 1115, .title = "Space Freeze", .units = "", .stepCount = 1, .flags = detail_expected::kList, .defaultNormalized = 0.0, .taper = ::Vorago::Taper::Discrete, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 1.0, .sc011 = Sc011::B},
    {.id = 1200, .title = "Envelope Mode", .units = "", .stepCount = 1, .flags = detail_expected::kList, .defaultNormalized = 0.0, .taper = ::Vorago::Taper::Discrete, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 1.0, .sc011 = Sc011::C},
    {.id = 1201, .title = "Envelope Stage 1 Time", .units = "ms", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.8092844131590221, .taper = ::Vorago::Taper::OffsetLog, .minPlain = 0.0, .maxPlain = 120000.0, .eps = 10.0, .mid = 1085.4907576059234, .sc011 = Sc011::A},
    {.id = 1202, .title = "Envelope Stage 2 Time", .units = "ms", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.8524345784937186, .taper = ::Vorago::Taper::OffsetLog, .minPlain = 0.0, .maxPlain = 120000.0, .eps = 10.0, .mid = 1085.4907576059234, .sc011 = Sc011::A},
    {.id = 1203, .title = "Envelope Stage 3 Time", .units = "ms", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.8955906544535204, .taper = ::Vorago::Taper::OffsetLog, .minPlain = 0.0, .maxPlain = 120000.0, .eps = 10.0, .mid = 1085.4907576059234, .sc011 = Sc011::A},
    {.id = 1204, .title = "Envelope Stage 4 Time", .units = "ms", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.9262128548621035, .taper = ::Vorago::Taper::OffsetLog, .minPlain = 0.0, .maxPlain = 120000.0, .eps = 10.0, .mid = 1085.4907576059234, .sc011 = Sc011::A},
    {.id = 1205, .title = "Envelope Release", .units = "ms", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.8955906544535204, .taper = ::Vorago::Taper::OffsetLog, .minPlain = 0.0, .maxPlain = 120000.0, .eps = 10.0, .mid = 1085.4907576059234, .sc011 = Sc011::A},
    {.id = 1206, .title = "Envelope Growth Duration", .units = "s", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 1.0, .taper = ::Vorago::Taper::Log, .minPlain = 1.0, .maxPlain = 120.0, .eps = 0.0, .mid = 10.954451150103322, .sc011 = Sc011::A},
    {.id = 1300, .title = "Bloom Depth", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.6, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 1301, .title = "Bloom Spawn Rate", .units = "Hz", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.6037728487568957, .taper = ::Vorago::Taper::OffsetLog, .minPlain = 0.0, .maxPlain = 0.05, .eps = 0.0001, .mid = 0.0021383029285599394, .sc011 = Sc011::A},
    {.id = 1400, .title = "Ghost Peak Level", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.0 /* 13c B-14: was 0.6 */, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 1401, .title = "Ghost Blur", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.85, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 1402, .title = "Ghost Reverse Probability", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.0, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 1403, .title = "Ghost Event Triggers", .units = "", .stepCount = 1, .flags = detail_expected::kList, .defaultNormalized = 0.0, .taper = ::Vorago::Taper::Discrete, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 1.0, .sc011 = Sc011::C},
    {.id = 1500, .title = "Breathing Depth", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.3, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 1501, .title = "Breathing Irregularity", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.3, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
    {.id = 1502, .title = "Tidal Depth", .units = "%", .stepCount = 0, .flags = detail_expected::kAuto, .defaultNormalized = 0.4, .taper = ::Vorago::Taper::Linear, .minPlain = 0.0, .maxPlain = 1.0, .eps = 0.0, .mid = 0.5, .sc011 = Sc011::A},
}};
// clang-format on

// ==============================================================================
// Compile-time checks on the table itself
// ==============================================================================

namespace detail_expected {

[[nodiscard]] constexpr bool strEq(const char* a, const char* b) noexcept {
    while (*a != '\0' && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

[[nodiscard]] constexpr const ExpectedParamRow* findRow(Steinberg::Vst::ParamID id) noexcept {
    for (const auto& r : kExpectedParams) {
        if (r.id == id) { return &r; }
    }
    return nullptr;
}

[[nodiscard]] constexpr std::size_t countClass(Sc011 c) noexcept {
    std::size_t n = 0;
    for (const auto& r : kExpectedParams) { n += (r.sc011 == c) ? 1u : 0u; }
    return n;
}

[[nodiscard]] constexpr bool idsStrictlyAscending() noexcept {
    for (std::size_t i = 1; i < kExpectedParams.size(); ++i) {
        if (kExpectedParams[i - 1].id >= kExpectedParams[i].id) { return false; }
    }
    return true;
}

[[nodiscard]] constexpr bool inRange(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamID lo,
                                     Steinberg::Vst::ParamID hi) noexcept {
    return id >= lo && id <= hi;
}

/// Plan section 6.4 groups B and C written out as ID ranges, independently of the
/// SC-011 column above (everything else is group A).
[[nodiscard]] constexpr Sc011 sc011ClassFor(Steinberg::Vst::ParamID id) noexcept {
    if (inRange(id, 310, 313) || inRange(id, 320, 323) || id == 403 || inRange(id, 1004, 1005) ||
        id == 1115) {
        return Sc011::B;
    }
    if (id == 1 || id == 2 || id == 4 || inRange(id, 510, 515) || id == 1200 || id == 1403) {
        return Sc011::C;
    }
    return Sc011::A;
}

[[nodiscard]] constexpr bool classesMatchPlan() noexcept {
    for (const auto& r : kExpectedParams) {
        if (r.sc011 != sc011ClassFor(r.id)) { return false; }
    }
    return true;
}

/// Discrete rows are exactly the list rows (kIsList, stepCount == count - 1 > 0);
/// continuous rows have stepCount 0.
[[nodiscard]] constexpr bool discreteRowsAreLists() noexcept {
    for (const auto& r : kExpectedParams) {
        const bool discrete = r.taper == ::Vorago::Taper::Discrete;
        const bool list = (r.flags & Steinberg::Vst::ParameterInfo::kIsList) != 0;
        if (discrete != list) { return false; }
        if (discrete != (r.stepCount > 0)) { return false; }
        if (discrete && r.maxPlain != static_cast<double>(r.stepCount)) { return false; }
    }
    return true;
}

/// The 14 Phase 11 rows keep their Phase 11 registration (FR-048: types frozen):
/// registerGlobalParams (Master Gain "dB" default 0.5; Polyphony list, 5 steps,
/// default 0.6) and registerMacroParams (twelve "%" parameters, Gravity 0.5).
struct Phase11Row {
    Steinberg::Vst::ParamID id;
    const char* title;
    const char* units;
    Steinberg::int32 stepCount;
    Steinberg::int32 flags;
    double defaultNormalized;
};

inline constexpr std::array<Phase11Row, 14> kPhase11Rows = {{
    {.id = 0, .title = "Master Gain", .units = "dB", .stepCount = 0, .flags = kAuto, .defaultNormalized = 0.5},
    {.id = 1, .title = "Polyphony", .units = "", .stepCount = 5, .flags = kList, .defaultNormalized = 0.6},
    {.id = 100, .title = "Darkness", .units = "%", .stepCount = 0, .flags = kAuto, .defaultNormalized = 0.0},
    {.id = 101, .title = "Age", .units = "%", .stepCount = 0, .flags = kAuto, .defaultNormalized = 0.0},
    {.id = 102, .title = "Density", .units = "%", .stepCount = 0, .flags = kAuto, .defaultNormalized = 0.0},
    {.id = 103, .title = "Movement", .units = "%", .stepCount = 0, .flags = kAuto, .defaultNormalized = 0.0},
    {.id = 104, .title = "Gravity", .units = "%", .stepCount = 0, .flags = kAuto, .defaultNormalized = 0.5},
    {.id = 105, .title = "Entropy", .units = "%", .stepCount = 0, .flags = kAuto, .defaultNormalized = 0.0},
    {.id = 106, .title = "Pressure", .units = "%", .stepCount = 0, .flags = kAuto, .defaultNormalized = 0.0},
    {.id = 107, .title = "Weight", .units = "%", .stepCount = 0, .flags = kAuto, .defaultNormalized = 0.0},
    {.id = 108, .title = "Fog", .units = "%", .stepCount = 0, .flags = kAuto, .defaultNormalized = 0.0},
    {.id = 109, .title = "Life", .units = "%", .stepCount = 0, .flags = kAuto, .defaultNormalized = 0.0},
    {.id = 110, .title = "Depth", .units = "%", .stepCount = 0, .flags = kAuto, .defaultNormalized = 0.0},
    {.id = 111, .title = "Mass", .units = "%", .stepCount = 0, .flags = kAuto, .defaultNormalized = 0.0},
}};

[[nodiscard]] constexpr bool phase11RowsUnchanged() noexcept {
    for (const auto& p : kPhase11Rows) {
        const ExpectedParamRow* r = findRow(p.id);
        if (r == nullptr) { return false; }
        if (!strEq(r->title, p.title) || !strEq(r->units, p.units)) { return false; }
        if (r->stepCount != p.stepCount || r->flags != p.flags) { return false; }
        if (r->defaultNormalized != p.defaultNormalized) { return false; }
    }
    return true;
}

}  // namespace detail_expected

static_assert(kExpectedParams.size() == 110,
              "plan section 3.2: 108 registered IDs + Phase 14 FR-074: 901, 902");
static_assert(detail_expected::idsStrictlyAscending(), "rows are in ascending (band) order");
static_assert(detail_expected::countClass(Sc011::A) == 87,
              "plan section 6.4: 85 group-A IDs + Phase 14 continuous 901, 902");
static_assert(detail_expected::countClass(Sc011::B) == 12, "plan section 6.4: 12 group-B IDs");
static_assert(detail_expected::countClass(Sc011::C) == 11, "plan section 6.4: 11 group-C IDs");
static_assert(detail_expected::countClass(Sc011::A) + detail_expected::countClass(Sc011::B) +
                      detail_expected::countClass(Sc011::C) ==
                  kExpectedParams.size(),
              "every row carries exactly one SC-011 class");
static_assert(detail_expected::classesMatchPlan(), "SC-011 column == plan section 6.4 ranges");
static_assert(detail_expected::discreteRowsAreLists(), "discrete rows == list rows");
static_assert(detail_expected::phase11RowsUnchanged(),
              "the 14 Phase 11 rows keep their Phase 11 title/units/stepCount/flags/default");

}  // namespace VoragoTest
