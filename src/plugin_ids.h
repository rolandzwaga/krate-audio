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
// ID Range Allocation (100-ID gaps for future expansion):
//   0-99:      Global parameters
//   100-199:   Granular Delay (spec 034)
//   200-299:   Spectral Delay (spec 033)
//   300-399:   Shimmer Delay (spec 029)
//   400-499:   Tape Delay (spec 024)
//   500-599:   BBD Delay (spec 025)
//   600-699:   Digital Delay (spec 026)
//   700-799:   PingPong Delay (spec 027)
//   800-899:   Reverse Delay (spec 030)
//   900-999:   MultiTap Delay (spec 028)
//   1000-1099: Freeze Mode (spec 031)
//   1100-1199: Ducking Delay (spec 032)
// ==============================================================================

enum ParameterIDs : Steinberg::Vst::ParamID {
    // ==========================================================================
    // Global Parameters (0-99)
    // ==========================================================================
    kBypassId = 0,
    kGainId = 1,

    // ==========================================================================
    // Granular Delay Parameters (100-199) - spec 034
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
    kGranularEndId = 199,

    // ==========================================================================
    // Spectral Delay Parameters (200-299) - spec 033
    // ==========================================================================
    kSpectralBaseId = 200,
    // Parameters to be added during integration
    kSpectralEndId = 299,

    // ==========================================================================
    // Shimmer Delay Parameters (300-399) - spec 029
    // ==========================================================================
    kShimmerBaseId = 300,
    // Parameters to be added during integration
    kShimmerEndId = 399,

    // ==========================================================================
    // Tape Delay Parameters (400-499) - spec 024
    // ==========================================================================
    kTapeBaseId = 400,
    // Parameters to be added during integration
    kTapeEndId = 499,

    // ==========================================================================
    // BBD Delay Parameters (500-599) - spec 025
    // ==========================================================================
    kBBDBaseId = 500,
    // Parameters to be added during integration
    kBBDEndId = 599,

    // ==========================================================================
    // Digital Delay Parameters (600-699) - spec 026
    // ==========================================================================
    kDigitalBaseId = 600,
    // Parameters to be added during integration
    kDigitalEndId = 699,

    // ==========================================================================
    // PingPong Delay Parameters (700-799) - spec 027
    // ==========================================================================
    kPingPongBaseId = 700,
    // Parameters to be added during integration
    kPingPongEndId = 799,

    // ==========================================================================
    // Reverse Delay Parameters (800-899) - spec 030
    // ==========================================================================
    kReverseBaseId = 800,
    // Parameters to be added during integration
    kReverseEndId = 899,

    // ==========================================================================
    // MultiTap Delay Parameters (900-999) - spec 028
    // ==========================================================================
    kMultiTapBaseId = 900,
    // Parameters to be added during integration
    kMultiTapEndId = 999,

    // ==========================================================================
    // Freeze Mode Parameters (1000-1099) - spec 031
    // ==========================================================================
    kFreezeBaseId = 1000,
    // Parameters to be added during integration
    kFreezeEndId = 1099,

    // ==========================================================================
    // Ducking Delay Parameters (1100-1199) - spec 032
    // ==========================================================================
    kDuckingBaseId = 1100,
    // Parameters to be added during integration
    kDuckingEndId = 1199,

    // ==========================================================================
    kNumParameters = 1200
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
