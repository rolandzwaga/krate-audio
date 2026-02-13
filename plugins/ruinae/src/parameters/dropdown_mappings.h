#pragma once

// ==============================================================================
// Ruinae Dropdown Mappings
// ==============================================================================
// Provides enum-to-string mappings for all Ruinae-specific dropdown parameters.
// Used by parameter registration and display formatting functions.
// ==============================================================================

#include <krate/dsp/systems/ruinae_types.h>
#include <krate/dsp/core/modulation_types.h>
#include <krate/dsp/primitives/lfo.h>
#include <krate/dsp/primitives/svf.h>
#include <krate/dsp/processors/mono_handler.h>
#include <krate/dsp/systems/poly_synth_engine.h>
#include "engine/ruinae_engine.h"  // For RuinaeModDest

#include "pluginterfaces/base/ustring.h"

namespace Ruinae {

// =============================================================================
// OscType dropdown (10 types, stepCount = 9)
// =============================================================================

inline constexpr int kOscTypeCount = static_cast<int>(Krate::DSP::OscType::NumTypes);

inline const Steinberg::Vst::TChar* const kOscTypeStrings[] = {
    STR16("PolyBLEP"),
    STR16("Wavetable"),
    STR16("Phase Dist"),
    STR16("Sync"),
    STR16("Additive"),
    STR16("Chaos"),
    STR16("Particle"),
    STR16("Formant"),
    STR16("Spectral Freeze"),
    STR16("Noise"),
};

// =============================================================================
// RuinaeFilterType dropdown (7 types, stepCount = 6)
// =============================================================================

inline constexpr int kFilterTypeCount = static_cast<int>(Krate::DSP::RuinaeFilterType::NumTypes);

inline const Steinberg::Vst::TChar* const kFilterTypeStrings[] = {
    STR16("SVF LP"),
    STR16("SVF HP"),
    STR16("SVF BP"),
    STR16("SVF Notch"),
    STR16("Ladder"),
    STR16("Formant"),
    STR16("Comb"),
};

// =============================================================================
// RuinaeDistortionType dropdown (6 types, stepCount = 5)
// =============================================================================

inline constexpr int kDistortionTypeCount = static_cast<int>(Krate::DSP::RuinaeDistortionType::NumTypes);

inline const Steinberg::Vst::TChar* const kDistortionTypeStrings[] = {
    STR16("Clean"),
    STR16("Chaos Waveshaper"),
    STR16("Spectral"),
    STR16("Granular"),
    STR16("Wavefolder"),
    STR16("Tape Saturator"),
};

// =============================================================================
// MixMode dropdown (2 modes, stepCount = 1)
// =============================================================================

inline constexpr int kMixModeCount = 2;

inline const Steinberg::Vst::TChar* const kMixModeStrings[] = {
    STR16("Crossfade"),
    STR16("Spectral Morph"),
};

// =============================================================================
// RuinaeDelayType dropdown (5 types, stepCount = 4)
// =============================================================================

inline constexpr int kDelayTypeCount = static_cast<int>(Krate::DSP::RuinaeDelayType::NumTypes);

inline const Steinberg::Vst::TChar* const kDelayTypeStrings[] = {
    STR16("Digital"),
    STR16("Tape"),
    STR16("Ping Pong"),
    STR16("Granular"),
    STR16("Spectral"),
};

// =============================================================================
// Waveform dropdown for LFO (6 shapes, stepCount = 5)
// =============================================================================

inline constexpr int kWaveformCount = 6;

inline const Steinberg::Vst::TChar* const kWaveformStrings[] = {
    STR16("Sine"),
    STR16("Triangle"),
    STR16("Sawtooth"),
    STR16("Square"),
    STR16("Sample & Hold"),
    STR16("Smooth Random"),
};

// =============================================================================
// VoiceMode dropdown (2 modes, stepCount = 1)
// =============================================================================

inline constexpr int kVoiceModeCount = 2;

inline const Steinberg::Vst::TChar* const kVoiceModeStrings[] = {
    STR16("Poly"),
    STR16("Mono"),
};

// =============================================================================
// MonoMode (3 modes, stepCount = 2)
// =============================================================================

inline constexpr int kMonoModeCount = 3;

inline const Steinberg::Vst::TChar* const kMonoModeStrings[] = {
    STR16("Last Note"),
    STR16("Low Note"),
    STR16("High Note"),
};

// =============================================================================
// PortaMode (2 modes, stepCount = 1)
// =============================================================================

inline constexpr int kPortaModeCount = 2;

inline const Steinberg::Vst::TChar* const kPortaModeStrings[] = {
    STR16("Always"),
    STR16("Legato Only"),
};

// =============================================================================
// SVFMode for Global Filter (4 modes exposed, stepCount = 3)
// =============================================================================

inline constexpr int kGlobalFilterTypeCount = 4;

inline const Steinberg::Vst::TChar* const kGlobalFilterTypeStrings[] = {
    STR16("Lowpass"),
    STR16("Highpass"),
    STR16("Bandpass"),
    STR16("Notch"),
};

// =============================================================================
// ModSource dropdown (13 sources, stepCount = 12)
// =============================================================================

inline constexpr int kModSourceCount = static_cast<int>(Krate::DSP::kModSourceCount);

inline const Steinberg::Vst::TChar* const kModSourceStrings[] = {
    STR16("None"),
    STR16("LFO 1"),
    STR16("LFO 2"),
    STR16("Env Follower"),
    STR16("Random"),
    STR16("Macro 1"),
    STR16("Macro 2"),
    STR16("Macro 3"),
    STR16("Macro 4"),
    STR16("Chaos"),
    STR16("Sample & Hold"),
    STR16("Pitch Follower"),
    STR16("Transient"),
};

// =============================================================================
// RuinaeModDest dropdown (8 destinations, stepCount = 7)
// Values start at 64 in the enum, so we map 0-7 to 64-71.
// =============================================================================

inline constexpr int kModDestCount = 8;

inline const Steinberg::Vst::TChar* const kModDestStrings[] = {
    STR16("Global Flt Cutoff"),
    STR16("Global Flt Reso"),
    STR16("Master Volume"),
    STR16("Effect Mix"),
    STR16("All Voice Flt Cutoff"),
    STR16("All Voice Morph Pos"),
    STR16("All Voice Gate Rate"),
    STR16("All Voice Spectral Tilt"),
};

// Helper: map dropdown index (0-7) to RuinaeModDest enum value (64-71)
inline Krate::DSP::RuinaeModDest modDestFromIndex(int index) {
    return static_cast<Krate::DSP::RuinaeModDest>(
        static_cast<uint32_t>(Krate::DSP::RuinaeModDest::GlobalFilterCutoff) + index);
}

// =============================================================================
// ChaosType dropdown (2 types, stepCount = 1)
// =============================================================================

inline constexpr int kChaosTypeCount = 2;

inline const Steinberg::Vst::TChar* const kChaosTypeStrings[] = {
    STR16("Lorenz"),
    STR16("Rossler"),
};

// =============================================================================
// NumSteps dropdown for Trance Gate (3 options: 8, 16, 32 -- stepCount = 2)
// =============================================================================

inline constexpr int kNumStepsCount = 3;

inline const Steinberg::Vst::TChar* const kNumStepsStrings[] = {
    STR16("8"),
    STR16("16"),
    STR16("32"),
};

// Helper: map dropdown index to step count value
inline int numStepsFromIndex(int index) {
    constexpr int kStepValues[] = {8, 16, 32};
    if (index < 0 || index >= kNumStepsCount) return 16;
    return kStepValues[index];
}

// Helper: map step count value to dropdown index
inline int numStepsToIndex(int steps) {
    if (steps <= 8) return 0;
    if (steps <= 16) return 1;
    return 2;
}

} // namespace Ruinae
