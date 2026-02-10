// =============================================================================
// Mock Plugin IDs for Control Testbench
// =============================================================================
// Provides standalone-compatible definitions for VST-specific types used by
// custom controls. This allows controls to be tested without the full VST3 SDK.
// =============================================================================

#pragma once

#include <cstdint>

// Mock Steinberg namespace for standalone compilation
namespace Steinberg {
namespace Vst {
    using ParamID = uint32_t;
}
}

// =============================================================================
// Mock Parameter IDs
// =============================================================================
// These match the real plugin_ids.h values for TapPatternEditor
// In the testbench, these are used for logging/debugging only

// MultiTap Custom Pattern Parameters
// Time ratios for each of the 8 custom taps
constexpr uint32_t kMultiTapCustomTime0Id = 3500;
constexpr uint32_t kMultiTapCustomTime1Id = 3501;
constexpr uint32_t kMultiTapCustomTime2Id = 3502;
constexpr uint32_t kMultiTapCustomTime3Id = 3503;
constexpr uint32_t kMultiTapCustomTime4Id = 3504;
constexpr uint32_t kMultiTapCustomTime5Id = 3505;
constexpr uint32_t kMultiTapCustomTime6Id = 3506;
constexpr uint32_t kMultiTapCustomTime7Id = 3507;

// Level values for each of the 8 custom taps
constexpr uint32_t kMultiTapCustomLevel0Id = 3510;
constexpr uint32_t kMultiTapCustomLevel1Id = 3511;
constexpr uint32_t kMultiTapCustomLevel2Id = 3512;
constexpr uint32_t kMultiTapCustomLevel3Id = 3513;
constexpr uint32_t kMultiTapCustomLevel4Id = 3514;
constexpr uint32_t kMultiTapCustomLevel5Id = 3515;
constexpr uint32_t kMultiTapCustomLevel6Id = 3516;
constexpr uint32_t kMultiTapCustomLevel7Id = 3517;

// =============================================================================
// StepPatternEditor Parameters (TranceGate step levels)
// =============================================================================
// Step levels 0-31 are contiguous: kTranceGateStepLevel0Id + i
constexpr uint32_t kTranceGateStepLevel0Id = 668;

// =============================================================================
// ADSRDisplay Parameters (Amp Envelope)
// =============================================================================
constexpr uint32_t kAmpEnvAttackId = 700;
constexpr uint32_t kAmpEnvDecayId = 701;
constexpr uint32_t kAmpEnvSustainId = 702;
constexpr uint32_t kAmpEnvReleaseId = 703;
constexpr uint32_t kAmpEnvAttackCurveId = 704;
constexpr uint32_t kAmpEnvDecayCurveId = 705;
constexpr uint32_t kAmpEnvReleaseCurveId = 706;
constexpr uint32_t kAmpEnvBezierEnabledId = 707;
constexpr uint32_t kAmpEnvBezierAttackCp1xId = 710;

// =============================================================================
// ADSRDisplay Parameters (Filter Envelope)
// =============================================================================
constexpr uint32_t kFilterEnvAttackId = 800;
constexpr uint32_t kFilterEnvDecayId = 801;
constexpr uint32_t kFilterEnvSustainId = 802;
constexpr uint32_t kFilterEnvReleaseId = 803;
constexpr uint32_t kFilterEnvAttackCurveId = 804;
constexpr uint32_t kFilterEnvDecayCurveId = 805;
constexpr uint32_t kFilterEnvReleaseCurveId = 806;
constexpr uint32_t kFilterEnvBezierEnabledId = 807;
constexpr uint32_t kFilterEnvBezierAttackCp1xId = 810;

// =============================================================================
// ADSRDisplay Parameters (Mod Envelope)
// =============================================================================
constexpr uint32_t kModEnvAttackId = 900;
constexpr uint32_t kModEnvDecayId = 901;
constexpr uint32_t kModEnvSustainId = 902;
constexpr uint32_t kModEnvReleaseId = 903;
constexpr uint32_t kModEnvAttackCurveId = 904;
constexpr uint32_t kModEnvDecayCurveId = 905;
constexpr uint32_t kModEnvReleaseCurveId = 906;
constexpr uint32_t kModEnvBezierEnabledId = 907;
constexpr uint32_t kModEnvBezierAttackCp1xId = 910;

// =============================================================================
// Mod Matrix Parameters (Global Routes, 8 slots)
// =============================================================================
// Base params: Source/Dest/Amount per slot (3 per slot, IDs 1300-1323)
constexpr uint32_t kModMatrixBaseId = 1300;
constexpr uint32_t kModSlot0SourceId = 1300;
constexpr uint32_t kModSlot0DestId = 1301;
constexpr uint32_t kModSlot0AmountId = 1302;
// Detail params: Curve/Smooth/Scale/Bypass per slot (4 per slot, IDs 1324-1355)
constexpr uint32_t kModSlot0CurveId = 1324;
constexpr uint32_t kModSlot0SmoothId = 1325;
constexpr uint32_t kModSlot0ScaleId = 1326;
constexpr uint32_t kModSlot0BypassId = 1327;
