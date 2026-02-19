#pragma once

// ==============================================================================
// Ruinae Dropdown Mappings
// ==============================================================================
// Provides enum-to-string mappings for all Ruinae-specific dropdown parameters.
// Used by parameter registration and display formatting functions.
// ==============================================================================

#include "ruinae_types.h"
#include "ui/mod_matrix_types.h"
#include <krate/dsp/systems/oscillator_types.h>
#include <krate/dsp/core/modulation_types.h>
#include <krate/dsp/primitives/lfo.h>
#include <krate/dsp/primitives/svf.h>
#include <krate/dsp/processors/mono_handler.h>
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"
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
// RuinaeFilterType dropdown (13 types, stepCount = 12)
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
    STR16("SVF Allpass"),
    STR16("SVF Peak"),
    STR16("SVF Lo Shelf"),
    STR16("SVF Hi Shelf"),
    STR16("Env Filter"),
    STR16("Self-Osc"),
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
// ModSource / ModDest — derived from central registry in mod_matrix_types.h
// =============================================================================
// Names and counts are defined once in kGlobalSourceNames / kGlobalDestNames.
// These aliases and helpers exist for backward compatibility with parameter
// registration and value denormalization code.

/// Total source parameter count: kNumGlobalSources + 1 for "None" at index 0
inline constexpr int kModSourceCount = Krate::Plugins::kNumGlobalSources + 1;

/// Total dest parameter count: matches kNumGlobalDestinations
inline constexpr int kModDestCount = Krate::Plugins::kNumGlobalDestinations;

/// Populate a StringListParameter with source names from the central registry.
/// Prepends "None" at index 0, then appends kGlobalSourceNames[0..12].
inline void appendSourceStrings(Steinberg::Vst::StringListParameter* param) {
    Steinberg::Vst::String128 buf;
    Steinberg::UString(buf, 128).fromAscii("None");
    param->appendString(buf);
    for (const auto& src : Krate::Plugins::kGlobalSourceNames) {
        Steinberg::UString(buf, 128).fromAscii(src.fullName);
        param->appendString(buf);
    }
}

/// Populate a StringListParameter with destination names from the central registry.
/// Uses hostName (shorter) for VST host parameter display.
inline void appendDestStrings(Steinberg::Vst::StringListParameter* param) {
    Steinberg::Vst::String128 buf;
    for (const auto& dest : Krate::Plugins::kGlobalDestNames) {
        Steinberg::UString(buf, 128).fromAscii(dest.hostName);
        param->appendString(buf);
    }
}

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

// =============================================================================
// Digital Delay: Era dropdown (3 presets, stepCount = 2)
// =============================================================================

inline constexpr int kDigitalEraCount = 3;

inline const Steinberg::Vst::TChar* const kDigitalEraStrings[] = {
    STR16("Pristine"),
    STR16("80s Digital"),
    STR16("Lo-Fi"),
};

// =============================================================================
// Digital Delay: LimiterCharacter dropdown (3 modes, stepCount = 2)
// =============================================================================

inline constexpr int kLimiterCharacterCount = 3;

inline const Steinberg::Vst::TChar* const kLimiterCharacterStrings[] = {
    STR16("Soft"),
    STR16("Medium"),
    STR16("Hard"),
};

// =============================================================================
// Digital Delay: WavefolderModel dropdown (4 models, stepCount = 3)
// =============================================================================

inline constexpr int kWavefolderModelCount = 4;

inline const Steinberg::Vst::TChar* const kWavefolderModelStrings[] = {
    STR16("Simple"),
    STR16("Serge"),
    STR16("Buchla 259"),
    STR16("Lockhart"),
};

// =============================================================================
// PingPong Delay: LRRatio dropdown (7 ratios, stepCount = 6)
// =============================================================================

inline constexpr int kLRRatioCount = 7;

inline const Steinberg::Vst::TChar* const kLRRatioStrings[] = {
    STR16("1:1"),
    STR16("2:1"),
    STR16("3:2"),
    STR16("4:3"),
    STR16("1:2"),
    STR16("2:3"),
    STR16("3:4"),
};

// =============================================================================
// Granular Delay: PitchQuantMode dropdown (5 modes, stepCount = 4)
// =============================================================================

inline constexpr int kPitchQuantModeCount = 5;

inline const Steinberg::Vst::TChar* const kPitchQuantModeStrings[] = {
    STR16("Off"),
    STR16("Semitones"),
    STR16("Octaves"),
    STR16("Fifths"),
    STR16("Scale"),
};

// =============================================================================
// Granular Delay: GrainEnvelopeType dropdown (6 types, stepCount = 5)
// =============================================================================

inline constexpr int kGrainEnvelopeCount = 6;

inline const Steinberg::Vst::TChar* const kGrainEnvelopeStrings[] = {
    STR16("Hann"),
    STR16("Trapezoid"),
    STR16("Sine"),
    STR16("Blackman"),
    STR16("Linear"),
    STR16("Exponential"),
};

// =============================================================================
// Spectral Delay: FFTSize dropdown (4 sizes, stepCount = 3)
// =============================================================================

inline constexpr int kFFTSizeCount = 4;

inline const Steinberg::Vst::TChar* const kFFTSizeStrings[] = {
    STR16("512"),
    STR16("1024"),
    STR16("2048"),
    STR16("4096"),
};

// Helper: map dropdown index to actual FFT size
inline size_t fftSizeFromIndex(int index) {
    constexpr size_t kFFTSizes[] = {512, 1024, 2048, 4096};
    if (index < 0 || index >= kFFTSizeCount) return 1024;
    return kFFTSizes[index];
}

// Helper: map actual FFT size to dropdown index
inline int fftSizeToIndex(size_t fftSize) {
    if (fftSize <= 512) return 0;
    if (fftSize <= 1024) return 1;
    if (fftSize <= 2048) return 2;
    return 3;
}

// =============================================================================
// Spectral Delay: SpreadDirection dropdown (3 directions, stepCount = 2)
// =============================================================================

inline constexpr int kSpreadDirectionCount = 3;

inline const Steinberg::Vst::TChar* const kSpreadDirectionStrings[] = {
    STR16("Low > High"),
    STR16("High > Low"),
    STR16("Center Out"),
};

// =============================================================================
// Spectral Delay: SpreadCurve dropdown (2 curves, stepCount = 1)
// =============================================================================

inline constexpr int kSpreadCurveCount = 2;

inline const Steinberg::Vst::TChar* const kSpreadCurveStrings[] = {
    STR16("Linear"),
    STR16("Logarithmic"),
};

// =============================================================================
// Phaser: Stages dropdown (6 options, stepCount = 5)
// =============================================================================

inline constexpr int kPhaserStagesCount = 6;

inline const Steinberg::Vst::TChar* const kPhaserStagesStrings[] = {
    STR16("2"),
    STR16("4"),
    STR16("6"),
    STR16("8"),
    STR16("10"),
    STR16("12"),
};

// Helper: map dropdown index to actual stage count
inline int phaserStagesFromIndex(int index) {
    constexpr int kStages[] = {2, 4, 6, 8, 10, 12};
    if (index < 0 || index >= kPhaserStagesCount) return 4;
    return kStages[index];
}

// Helper: map stage count to dropdown index
inline int phaserStagesToIndex(int stages) {
    return std::clamp((stages - 2) / 2, 0, kPhaserStagesCount - 1);
}

// =============================================================================
// Phaser: Waveform dropdown (4 shapes, stepCount = 3)
// =============================================================================

inline constexpr int kPhaserWaveformCount = 4;

inline const Steinberg::Vst::TChar* const kPhaserWaveformStrings[] = {
    STR16("Sine"),
    STR16("Triangle"),
    STR16("Sawtooth"),
    STR16("Square"),
};

// =============================================================================
// Harmonizer: HarmonyMode dropdown (2 modes, stepCount = 1)
// =============================================================================

inline constexpr int kHarmonyModeCount = 2;

inline const Steinberg::Vst::TChar* const kHarmonyModeStrings[] = {
    STR16("Chromatic"),
    STR16("Scalic"),
};

// =============================================================================
// Harmonizer: Key dropdown (12 keys, stepCount = 11)
// =============================================================================

inline constexpr int kHarmonizerKeyCount = 12;

inline const Steinberg::Vst::TChar* const kHarmonizerKeyStrings[] = {
    STR16("C"), STR16("C#"), STR16("D"), STR16("Eb"),
    STR16("E"), STR16("F"), STR16("F#"), STR16("G"),
    STR16("Ab"), STR16("A"), STR16("Bb"), STR16("B"),
};

// =============================================================================
// Harmonizer: Scale dropdown (9 types, stepCount = 8)
// =============================================================================

inline constexpr int kHarmonizerScaleCount = 9;

inline const Steinberg::Vst::TChar* const kHarmonizerScaleStrings[] = {
    STR16("Major"), STR16("Natural Minor"), STR16("Harmonic Minor"),
    STR16("Melodic Minor"), STR16("Dorian"), STR16("Mixolydian"),
    STR16("Phrygian"), STR16("Lydian"), STR16("Chromatic"),
};

// =============================================================================
// Harmonizer: PitchShiftMode dropdown (4 modes, stepCount = 3)
// =============================================================================

inline constexpr int kHarmonizerPitchModeCount = 4;

inline const Steinberg::Vst::TChar* const kHarmonizerPitchModeStrings[] = {
    STR16("Simple"), STR16("Granular"),
    STR16("Phase Vocoder"), STR16("Pitch Sync"),
};

// =============================================================================
// Harmonizer: NumVoices dropdown (4 options: 1-4, stepCount = 3)
// =============================================================================

inline constexpr int kHarmonizerNumVoicesCount = 4;

inline const Steinberg::Vst::TChar* const kHarmonizerNumVoicesStrings[] = {
    STR16("1"), STR16("2"), STR16("3"), STR16("4"),
};

// =============================================================================
// Harmonizer: Interval helpers (49 options: -24 to +24, stepCount = 48)
// =============================================================================

inline constexpr int kHarmonizerIntervalCount = 49;

// Helper: convert dropdown index (0..48) to diatonic step value (-24..+24)
inline int harmonizerIntervalFromIndex(int index) {
    return std::clamp(index - 24, -24, 24);
}

// Helper: convert diatonic step value to dropdown index
inline int harmonizerIntervalToIndex(int interval) {
    return std::clamp(interval + 24, 0, 48);
}

} // namespace Ruinae
