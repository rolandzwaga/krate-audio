// ==============================================================================
// Vorago lever bounds sentinels, one call per (arm, sample rate)
// ==============================================================================
// VoragoEngine_EcosystemLeverBounded and VoragoEngine_CapabilityLeverBounded
// (60 s renders per arm at 44.1 / 48 / 96 kHz) are defined in
// vorago_ecosystem_lever_test.cpp, the one TU that defines
// detail::VoragoEcosystemLeverProbe. Their twelve TEST_CASEs, two per TU, sit in
// vorago_ecosystem_lever_test.cpp and vorago_lever_bounded_part1..5_test.cpp,
// which dsp/tests/CMakeLists.txt lists at spread positions.
//
// WHY SPREAD: tools/run-gate.js shards with Catch2 --shard-count / --shard-index
// in declaration order, and Catch2 cuts that list into contiguous chunks. As two
// cases (245 s + 217 s measured alone, 2026-11-09) they sat in one chunk and held
// every dsp_systems_tests gate for ~10 min on one shard. Twelve cases in one TU
// would still be one chunk; slots at least ~200 cases apart in the list land in
// different shards whenever a shard holds fewer cases than that (run-gate's
// 28 shards at --share 4 hold 66). NOT [long]: bounds sentinels stay per-push.
// ==============================================================================

#pragma once

namespace VoragoLeverBounded {

enum class EcoArm { Injected, Natural };

/// VoragoEngine_EcosystemLeverBounded at one arm and one sample rate.
void runEcosystemLeverBounded(EcoArm which, double fs);

/// VoragoEngine_CapabilityLeverBounded at one Gravity value (0 or 1) and one sample rate.
void runCapabilityLeverBounded(float gravity, double fs);

} // namespace VoragoLeverBounded
