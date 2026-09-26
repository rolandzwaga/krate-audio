#pragma once

// ==============================================================================
// Vorago Phase 12 - checked-in expected parameter table (SC-018, SC-011 column)
// ==============================================================================
// T033 (specs/vorago-phase12-parameters/tasks.md). One row per registered ID (108),
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

inline constexpr std::size_t kNumExpectedParams = 108;

// clang-format off
inline constexpr std::array<ExpectedParamRow, kNumExpectedParams> kExpectedParams = {{
    // id, title, units, stepCount, flags, n0, taper, min, max, eps, mid (n = 0.5), SC-011
    {0, "Master Gain", "dB", 0, detail_expected::kAuto, 0.5, ::Vorago::Taper::Linear, 0.0, 2.0, 0.0, 1.0, Sc011::A},
    {1, "Polyphony", "", 5, detail_expected::kList, 0.6, ::Vorago::Taper::Discrete, 0.0, 5.0, 0.0, 3.0, Sc011::C},
    {2, "Seed", "", 15, detail_expected::kList, 0.0, ::Vorago::Taper::Discrete, 0.0, 15.0, 0.0, 8.0, Sc011::C},
    {3, "Output Saturation", "%", 0, detail_expected::kAuto, 0.12, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {4, "Sustain Pedal", "", 0, detail_expected::kHidden, 0.0, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::C},
    {5, "Channel Pressure", "%", 0, detail_expected::kHidden, 0.0, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {100, "Darkness", "%", 0, detail_expected::kAuto, 0.0, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {101, "Age", "%", 0, detail_expected::kAuto, 0.0, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {102, "Density", "%", 0, detail_expected::kAuto, 0.0, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {103, "Movement", "%", 0, detail_expected::kAuto, 0.0, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {104, "Gravity", "%", 0, detail_expected::kAuto, 0.5, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {105, "Entropy", "%", 0, detail_expected::kAuto, 0.0, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {106, "Pressure", "%", 0, detail_expected::kAuto, 0.0, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {107, "Weight", "%", 0, detail_expected::kAuto, 0.0, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {108, "Fog", "%", 0, detail_expected::kAuto, 0.0, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {109, "Life", "%", 0, detail_expected::kAuto, 0.0, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {110, "Depth", "%", 0, detail_expected::kAuto, 0.0, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {111, "Mass", "%", 0, detail_expected::kAuto, 0.0, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {200, "Cloud Richness", "%", 0, detail_expected::kAuto, 0.7, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {201, "Cloud Tilt", "dB/oct", 0, detail_expected::kAuto, 0.3333333333333333, ::Vorago::Taper::Linear, -12.0, 12.0, 0.0, 0.0, Sc011::A},
    {202, "Cloud Mutation", "%", 0, detail_expected::kAuto, 0.15, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {203, "Cloud Inharmonicity", "", 0, detail_expected::kAuto, 0.15, ::Vorago::Taper::Linear, 0.0, 0.1, 0.0, 0.05, Sc011::A},
    {204, "Cloud Drift Depth", "ct", 0, detail_expected::kAuto, 0.16, ::Vorago::Taper::Linear, 0.0, 50.0, 0.0, 25.0, Sc011::A},
    {205, "Cloud Stereo Spread", "%", 0, detail_expected::kAuto, 0.45, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {206, "Cloud Spectral Gravity", "", 0, detail_expected::kAuto, 0.55, ::Vorago::Taper::Linear, -1.0, 1.0, 0.0, 0.0, Sc011::A},
    {300, "Noise Level", "dB", 0, detail_expected::kAuto, 0.7222222222222222, ::Vorago::Taper::Linear, -96.0, 12.0, 0.0, -42.0, Sc011::A},
    {301, "Noise Wake", "%", 0, detail_expected::kAuto, 0.35, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {302, "Noise Wander Rate", "Hz", 0, detail_expected::kAuto, 0.11928031367991558, ::Vorago::Taper::Log, 0.01, 100.0, 0.0, 1.0, Sc011::A},
    {310, "Noise Slot 1 Model", "", 3, detail_expected::kList, 0.3333333333333333, ::Vorago::Taper::Discrete, 0.0, 3.0, 0.0, 2.0, Sc011::B},
    {311, "Noise Slot 2 Model", "", 3, detail_expected::kList, 0.6666666666666666, ::Vorago::Taper::Discrete, 0.0, 3.0, 0.0, 2.0, Sc011::B},
    {312, "Noise Slot 3 Model", "", 3, detail_expected::kList, 0.0, ::Vorago::Taper::Discrete, 0.0, 3.0, 0.0, 2.0, Sc011::B},
    {313, "Noise Slot 4 Model", "", 3, detail_expected::kList, 1.0, ::Vorago::Taper::Discrete, 0.0, 3.0, 0.0, 2.0, Sc011::B},
    {320, "Noise Slot 1 Type", "", 11, detail_expected::kList, 0.45454545454545453, ::Vorago::Taper::Discrete, 0.0, 11.0, 0.0, 6.0, Sc011::B},
    {321, "Noise Slot 2 Type", "", 11, detail_expected::kList, 0.45454545454545453, ::Vorago::Taper::Discrete, 0.0, 11.0, 0.0, 6.0, Sc011::B},
    {322, "Noise Slot 3 Type", "", 11, detail_expected::kList, 0.45454545454545453, ::Vorago::Taper::Discrete, 0.0, 11.0, 0.0, 6.0, Sc011::B},
    {323, "Noise Slot 4 Type", "", 11, detail_expected::kList, 0.45454545454545453, ::Vorago::Taper::Discrete, 0.0, 11.0, 0.0, 6.0, Sc011::B},
    {330, "Noise Slot 1 Comb Freq", "Hz", 0, detail_expected::kAuto, 0.15921974703872435, ::Vorago::Taper::Log, 20.0, 19845.0, 0.0, 630.0, Sc011::A},
    {331, "Noise Slot 2 Comb Freq", "Hz", 0, detail_expected::kAuto, 0.15921974703872435, ::Vorago::Taper::Log, 20.0, 19845.0, 0.0, 630.0, Sc011::A},
    {332, "Noise Slot 3 Comb Freq", "Hz", 0, detail_expected::kAuto, 0.15921974703872435, ::Vorago::Taper::Log, 20.0, 19845.0, 0.0, 630.0, Sc011::A},
    {333, "Noise Slot 4 Comb Freq", "Hz", 0, detail_expected::kAuto, 0.15921974703872435, ::Vorago::Taper::Log, 20.0, 19845.0, 0.0, 630.0, Sc011::A},
    {340, "Noise Slot 1 Comb Spread", "%", 0, detail_expected::kAuto, 0.35, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {341, "Noise Slot 2 Comb Spread", "%", 0, detail_expected::kAuto, 0.35, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {342, "Noise Slot 3 Comb Spread", "%", 0, detail_expected::kAuto, 0.35, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {343, "Noise Slot 4 Comb Spread", "%", 0, detail_expected::kAuto, 0.35, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {350, "Noise Slot 1 Comb Feedback", "%", 0, detail_expected::kAuto, 0.6111111111111112, ::Vorago::Taper::Linear, 0.0, 0.9, 0.0, 0.45, Sc011::A},
    {351, "Noise Slot 2 Comb Feedback", "%", 0, detail_expected::kAuto, 0.6111111111111112, ::Vorago::Taper::Linear, 0.0, 0.9, 0.0, 0.45, Sc011::A},
    {352, "Noise Slot 3 Comb Feedback", "%", 0, detail_expected::kAuto, 0.6111111111111112, ::Vorago::Taper::Linear, 0.0, 0.9, 0.0, 0.45, Sc011::A},
    {353, "Noise Slot 4 Comb Feedback", "%", 0, detail_expected::kAuto, 0.8333333333333333, ::Vorago::Taper::Linear, 0.0, 0.9, 0.0, 0.45, Sc011::A},
    {400, "Resonance Gravity", "", 0, detail_expected::kAuto, 0.5, ::Vorago::Taper::Linear, -1.0, 1.0, 0.0, 0.0, Sc011::A},
    {401, "Resonance Mix", "%", 0, detail_expected::kAuto, 0.45, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {402, "Resonance Wander Rate", "Hz", 0, detail_expected::kAuto, 0.43575558719297985, ::Vorago::Taper::Log, 0.002, 1.0, 0.0, 0.044721359549995794, Sc011::A},
    {403, "Resonance Anchor", "", 2, detail_expected::kList, 1.0, ::Vorago::Taper::Discrete, 0.0, 2.0, 0.0, 1.0, Sc011::B},
    {500, "Ecology Mix", "%", 0, detail_expected::kAuto, 0.15, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {501, "Ecology Loop Gain", "%", 0, detail_expected::kAuto, 0.7999999999999999, ::Vorago::Taper::Linear, 0.0, 0.9, 0.0, 0.45, Sc011::A},
    {510, "Ecology Loop 1 Filter", "", 2, detail_expected::kList, 0.0, ::Vorago::Taper::Discrete, 0.0, 2.0, 0.0, 1.0, Sc011::C},
    {511, "Ecology Loop 2 Filter", "", 2, detail_expected::kList, 0.0, ::Vorago::Taper::Discrete, 0.0, 2.0, 0.0, 1.0, Sc011::C},
    {512, "Ecology Loop 3 Filter", "", 2, detail_expected::kList, 0.0, ::Vorago::Taper::Discrete, 0.0, 2.0, 0.0, 1.0, Sc011::C},
    {513, "Ecology Loop 4 Filter", "", 2, detail_expected::kList, 0.0, ::Vorago::Taper::Discrete, 0.0, 2.0, 0.0, 1.0, Sc011::C},
    {514, "Ecology Loop 5 Filter", "", 2, detail_expected::kList, 0.0, ::Vorago::Taper::Discrete, 0.0, 2.0, 0.0, 1.0, Sc011::C},
    {515, "Ecology Loop 6 Filter", "", 2, detail_expected::kList, 0.0, ::Vorago::Taper::Discrete, 0.0, 2.0, 0.0, 1.0, Sc011::C},
    {600, "Sub Level Offset", "dB", 0, detail_expected::kAuto, 0.5, ::Vorago::Taper::Linear, -24.0, 24.0, 0.0, 0.0, Sc011::A},
    {601, "Sub Tracking", "%", 0, detail_expected::kAuto, 1.0, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {610, "Sub Div2 Level", "dB", 0, detail_expected::kAuto, 0.6363636363636364, ::Vorago::Taper::Linear, -60.0, 6.0, 0.0, -27.0, Sc011::A},
    {611, "Sub Div4 Level", "dB", 0, detail_expected::kAuto, 0.5454545454545454, ::Vorago::Taper::Linear, -60.0, 6.0, 0.0, -27.0, Sc011::A},
    {612, "Sub Fifth-Below Level", "dB", 0, detail_expected::kAuto, 0.45454545454545453, ::Vorago::Taper::Linear, -60.0, 6.0, 0.0, -27.0, Sc011::A},
    {700, "Smear Amount", "%", 0, detail_expected::kAuto, 0.2, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {701, "Smear Decoherence", "%", 0, detail_expected::kAuto, 0.2, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {702, "Smear Tilt", "", 0, detail_expected::kAuto, 0.5, ::Vorago::Taper::Linear, -1.0, 1.0, 0.0, 0.0, Sc011::A},
    {800, "Event Rate", "x", 0, detail_expected::kAuto, 0.5, ::Vorago::Taper::Log, 0.1, 10.0, 0.0, 1.0, Sc011::A},
    {900, "Ecosystem Depth", "%", 0, detail_expected::kAuto, 0.85, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {1000, "Body Blend", "%", 0, detail_expected::kAuto, 0.35, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {1001, "Body Damping", "%", 0, detail_expected::kAuto, 0.25, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {1002, "Body Resonance", "%", 0, detail_expected::kAuto, 0.7, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {1003, "Body Mix", "%", 0, detail_expected::kAuto, 1.0, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {1004, "Body Material A", "", 10, detail_expected::kList, 0.5, ::Vorago::Taper::Discrete, 0.0, 10.0, 0.0, 5.0, Sc011::B},
    {1005, "Body Material B", "", 10, detail_expected::kList, 0.6, ::Vorago::Taper::Discrete, 0.0, 10.0, 0.0, 5.0, Sc011::B},
    {1100, "Space Size", "%", 0, detail_expected::kAuto, 0.5, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {1101, "Space Darkness", "%", 0, detail_expected::kAuto, 0.8, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {1102, "Space Decay", "s", 0, detail_expected::kAuto, 0.7705244525331133, ::Vorago::Taper::Log, 0.5, 60.0, 0.0, 5.477225575051661, Sc011::A},
    {1103, "Space Fog", "%", 0, detail_expected::kAuto, 0.3, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {1104, "Space Damper Depth", "%", 0, detail_expected::kAuto, 0.35, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {1105, "Space Mix", "%", 0, detail_expected::kAuto, 1.0, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {1106, "Space Width", "%", 0, detail_expected::kAuto, 1.0, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {1107, "Space Density", "%", 0, detail_expected::kAuto, 0.75, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {1108, "Space Dimensionality", "%", 0, detail_expected::kAuto, 0.5, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {1109, "Space Breath", "%", 0, detail_expected::kAuto, 0.5, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {1110, "Space Early Size", "ms", 0, detail_expected::kAuto, 0.7653462773366764, ::Vorago::Taper::Log, 80.0, 300.0, 0.0, 154.91933384829667, Sc011::A},
    {1111, "Space Early Level", "%", 0, detail_expected::kAuto, 0.8, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {1112, "Space Early Absorption", "%", 0, detail_expected::kAuto, 0.6, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {1113, "Space Early Send", "%", 0, detail_expected::kAuto, 0.7, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {1114, "Space Damper Rate", "", 0, detail_expected::kAuto, 0.6007619328947518, ::Vorago::Taper::OffsetLog, 0.0, 1.0, 0.01, 0.09049875621120891, Sc011::A},
    {1115, "Space Freeze", "", 1, detail_expected::kList, 0.0, ::Vorago::Taper::Discrete, 0.0, 1.0, 0.0, 1.0, Sc011::B},
    {1200, "Envelope Mode", "", 1, detail_expected::kList, 0.0, ::Vorago::Taper::Discrete, 0.0, 1.0, 0.0, 1.0, Sc011::C},
    {1201, "Envelope Stage 1 Time", "ms", 0, detail_expected::kAuto, 0.8092844131590221, ::Vorago::Taper::OffsetLog, 0.0, 120000.0, 10.0, 1085.4907576059234, Sc011::A},
    {1202, "Envelope Stage 2 Time", "ms", 0, detail_expected::kAuto, 0.8524345784937186, ::Vorago::Taper::OffsetLog, 0.0, 120000.0, 10.0, 1085.4907576059234, Sc011::A},
    {1203, "Envelope Stage 3 Time", "ms", 0, detail_expected::kAuto, 0.8955906544535204, ::Vorago::Taper::OffsetLog, 0.0, 120000.0, 10.0, 1085.4907576059234, Sc011::A},
    {1204, "Envelope Stage 4 Time", "ms", 0, detail_expected::kAuto, 0.9262128548621035, ::Vorago::Taper::OffsetLog, 0.0, 120000.0, 10.0, 1085.4907576059234, Sc011::A},
    {1205, "Envelope Release", "ms", 0, detail_expected::kAuto, 0.8955906544535204, ::Vorago::Taper::OffsetLog, 0.0, 120000.0, 10.0, 1085.4907576059234, Sc011::A},
    {1206, "Envelope Growth Duration", "s", 0, detail_expected::kAuto, 1.0, ::Vorago::Taper::Log, 1.0, 120.0, 0.0, 10.954451150103322, Sc011::A},
    {1300, "Bloom Depth", "%", 0, detail_expected::kAuto, 0.6, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {1301, "Bloom Spawn Rate", "Hz", 0, detail_expected::kAuto, 0.6037728487568957, ::Vorago::Taper::OffsetLog, 0.0, 0.05, 0.0001, 0.0021383029285599394, Sc011::A},
    {1400, "Ghost Peak Level", "%", 0, detail_expected::kAuto, 0.6, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {1401, "Ghost Blur", "%", 0, detail_expected::kAuto, 0.85, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {1402, "Ghost Reverse Probability", "%", 0, detail_expected::kAuto, 0.0, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {1403, "Ghost Event Triggers", "", 1, detail_expected::kList, 0.0, ::Vorago::Taper::Discrete, 0.0, 1.0, 0.0, 1.0, Sc011::C},
    {1500, "Breathing Depth", "%", 0, detail_expected::kAuto, 0.3, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {1501, "Breathing Irregularity", "%", 0, detail_expected::kAuto, 0.3, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
    {1502, "Tidal Depth", "%", 0, detail_expected::kAuto, 0.4, ::Vorago::Taper::Linear, 0.0, 1.0, 0.0, 0.5, Sc011::A},
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
    {0, "Master Gain", "dB", 0, kAuto, 0.5},
    {1, "Polyphony", "", 5, kList, 0.6},
    {100, "Darkness", "%", 0, kAuto, 0.0},
    {101, "Age", "%", 0, kAuto, 0.0},
    {102, "Density", "%", 0, kAuto, 0.0},
    {103, "Movement", "%", 0, kAuto, 0.0},
    {104, "Gravity", "%", 0, kAuto, 0.5},
    {105, "Entropy", "%", 0, kAuto, 0.0},
    {106, "Pressure", "%", 0, kAuto, 0.0},
    {107, "Weight", "%", 0, kAuto, 0.0},
    {108, "Fog", "%", 0, kAuto, 0.0},
    {109, "Life", "%", 0, kAuto, 0.0},
    {110, "Depth", "%", 0, kAuto, 0.0},
    {111, "Mass", "%", 0, kAuto, 0.0},
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

static_assert(kExpectedParams.size() == 108, "plan section 3.2: 108 registered IDs");
static_assert(detail_expected::idsStrictlyAscending(), "rows are in ascending (band) order");
static_assert(detail_expected::countClass(Sc011::A) == 85, "plan section 6.4: 85 group-A IDs");
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
