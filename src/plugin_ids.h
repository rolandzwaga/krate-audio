#pragma once

// ==============================================================================
// Plugin Identifiers
// ==============================================================================
// These GUIDs uniquely identify the plugin components.
// Generate new GUIDs for your plugin at: https://www.guidgenerator.com/
//
// IMPORTANT: Once published, NEVER change these IDs or hosts will not
// recognize saved projects using your plugin.
// ==============================================================================

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Iterum {

// Processor Component ID
// The audio processing component (runs on audio thread)
static const Steinberg::FUID kProcessorUID(0x12345678, 0x12345678, 0x12345678, 0x12345678);

// Controller Component ID
// The edit controller component (runs on UI thread)
static const Steinberg::FUID kControllerUID(0x87654321, 0x87654321, 0x87654321, 0x87654321);

// ==============================================================================
// Parameter IDs
// ==============================================================================
// Define all automatable parameters here.
// Constitution Principle V: All parameter values MUST be normalized (0.0 to 1.0)
//
// ID Range Allocation:
//   0-99:    Global parameters
//   100-119: Granular Delay (spec 034)
//   120-139: Spectral Delay (spec 033)
//   140-159: Shimmer Delay (spec 029)
//   160-179: Tape Delay (spec 024)
//   180-199: BBD Delay (spec 025)
//   200-219: Digital Delay (spec 026)
//   220-239: PingPong Delay (spec 027)
//   240-259: Reverse Delay (spec 030)
//   260-299: MultiTap Delay (spec 028) - extended range for per-tap params
//   300-319: Freeze Mode (spec 031)
//   320-339: Ducking Delay (spec 032)
// ==============================================================================

enum ParameterIDs : Steinberg::Vst::ParamID {
    // ==========================================================================
    // Global Parameters (0-99)
    // ==========================================================================
    kBypassId = 0,
    kGainId = 1,

    // ==========================================================================
    // Granular Delay Parameters (100-119) - spec 034
    // ==========================================================================
    kGranularBaseId = 100,
    kGranularGrainSizeId = 100,      // 10-500ms
    kGranularDensityId = 101,        // 1-100 grains/sec
    kGranularDelayTimeId = 102,      // 0-2000ms
    kGranularPitchId = 103,          // -24 to +24 semitones
    kGranularPitchSprayId = 104,     // 0-1
    kGranularPositionSprayId = 105,  // 0-1
    kGranularPanSprayId = 106,       // 0-1
    kGranularReverseProbId = 107,    // 0-1
    kGranularFreezeId = 108,         // on/off
    kGranularFeedbackId = 109,       // 0-1.2
    kGranularDryWetId = 110,         // 0-1
    kGranularOutputGainId = 111,     // -96 to +6 dB
    kGranularEnvelopeTypeId = 112,   // 0-3 (Hann, Trapezoid, Sine, Blackman)
    kGranularEndId = 119,

    // ==========================================================================
    // Spectral Delay Parameters (120-139) - spec 033
    // ==========================================================================
    kSpectralBaseId = 120,
    // Parameters to be added during integration
    kSpectralEndId = 139,

    // ==========================================================================
    // Shimmer Delay Parameters (140-159) - spec 029
    // ==========================================================================
    kShimmerBaseId = 140,
    // Parameters to be added during integration
    kShimmerEndId = 159,

    // ==========================================================================
    // Tape Delay Parameters (160-179) - spec 024
    // ==========================================================================
    kTapeBaseId = 160,
    // Parameters to be added during integration
    kTapeEndId = 179,

    // ==========================================================================
    // BBD Delay Parameters (180-199) - spec 025
    // ==========================================================================
    kBBDBaseId = 180,
    // Parameters to be added during integration
    kBBDEndId = 199,

    // ==========================================================================
    // Digital Delay Parameters (200-219) - spec 026
    // ==========================================================================
    kDigitalBaseId = 200,
    // Parameters to be added during integration
    kDigitalEndId = 219,

    // ==========================================================================
    // PingPong Delay Parameters (220-239) - spec 027
    // ==========================================================================
    kPingPongBaseId = 220,
    // Parameters to be added during integration
    kPingPongEndId = 239,

    // ==========================================================================
    // Reverse Delay Parameters (240-259) - spec 030
    // ==========================================================================
    kReverseBaseId = 240,
    // Parameters to be added during integration
    kReverseEndId = 259,

    // ==========================================================================
    // MultiTap Delay Parameters (260-299) - spec 028
    // Extended range for per-tap parameters (up to 16 taps)
    // ==========================================================================
    kMultiTapBaseId = 260,
    // Parameters to be added during integration
    kMultiTapEndId = 299,

    // ==========================================================================
    // Freeze Mode Parameters (300-319) - spec 031
    // ==========================================================================
    kFreezeBaseId = 300,
    // Parameters to be added during integration
    kFreezeEndId = 319,

    // ==========================================================================
    // Ducking Delay Parameters (320-339) - spec 032
    // ==========================================================================
    kDuckingBaseId = 320,
    // Parameters to be added during integration
    kDuckingEndId = 339,

    // ==========================================================================
    kNumParameters = 340
};

// ==============================================================================
// Plugin Metadata
// ==============================================================================
// Note: Vendor info (company name, URL, email, copyright) is defined in
// version.h.in which CMake uses to generate version.h
// ==============================================================================

// VST3 Sub-categories (see VST3 SDK documentation for full list)
// Examples: "Fx", "Instrument", "Analyzer", "Delay", "Reverb", etc.
constexpr const char* kSubCategories = "Delay";

} // namespace Iterum
