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
