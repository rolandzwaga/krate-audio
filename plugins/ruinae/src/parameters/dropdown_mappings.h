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


#include <algorithm>
#include <array>
#include <iterator>

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
    STR16("Ring Mod"),
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
static_assert(std::size(kWaveformStrings) == static_cast<std::size_t>(kWaveformCount));

// The digital delay's Mod Waveform: the same six shapes and kWaveformCount, with
// short labels for its narrow menu.
inline const Steinberg::Vst::TChar* const kDelayModWaveformStrings[] = {
    STR16("Sine"),
    STR16("Triangle"),
    STR16("Sawtooth"),
    STR16("Square"),
    STR16("S&H"),
    STR16("Smooth Rnd"),
};
static_assert(std::size(kDelayModWaveformStrings) == static_cast<std::size_t>(kWaveformCount));

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
/// Prepends "None" at index 0, then appends kGlobalSourceNames[0..13].
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

// Helper: map dropdown index (0-14) to RuinaeModDest enum value (64-78)
inline Krate::DSP::RuinaeModDest modDestFromIndex(int index) {
    return static_cast<Krate::DSP::RuinaeModDest>(
        static_cast<uint32_t>(Krate::DSP::RuinaeModDest::GlobalFilterCutoff) +
        static_cast<uint32_t>(std::clamp(index, 0, 14)));
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
// Legacy Trance Gate NumSteps index (3 options: 8, 16, 32)
// =============================================================================
// Gate Steps is a 2-32 RangeParameter now; old states stored this index.

inline constexpr int kNumStepsCount = 3;

// Helper: map a legacy dropdown index to its step count value
inline int numStepsFromIndex(int index) {
    constexpr int kStepValues[] = {8, 16, 32};
    if (index < 0 || index >= kNumStepsCount) return 16;
    return kStepValues[index];
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
// Index -> size: Krate::DSP::fftSizeFromIndex (ruinae_types.h), which the
// effects chain uses too.
static_assert(std::size(kFFTSizeStrings) == static_cast<std::size_t>(kFFTSizeCount));
static_assert(std::size(Krate::DSP::kSpectralDelayFFTSizes) == static_cast<std::size_t>(kFFTSizeCount));

// =============================================================================
// Spectral Delay: SpreadDirection dropdown (5 directions, stepCount = 4; Krate::DSP::
// SpreadDirection order, saved as the index, so new directions go last)
// =============================================================================

inline constexpr int kSpreadDirectionCount = 5;

inline const Steinberg::Vst::TChar* const kSpreadDirectionStrings[] = {
    STR16("Low > High"),
    STR16("High > Low"),
    STR16("Center Out"),
    STR16("Edges In"),
    STR16("Cross"),
};

// =============================================================================
// Spectral Delay: SpreadCurve dropdown (6 curves, stepCount = 5; Krate::DSP::SpreadCurve
// order, saved as the index, so new curves go last)
// =============================================================================

inline constexpr int kSpreadCurveCount = 6;

inline const Steinberg::Vst::TChar* const kSpreadCurveStrings[] = {
    STR16("Linear"),
    STR16("Logarithmic"),
    STR16("Exponential"),
    STR16("Steps"),
    STR16("Scatter"),
    STR16("Random"),
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
static_assert(std::size(kPhaserStagesStrings) == static_cast<std::size_t>(kPhaserStagesCount));

// Helper: map dropdown index to actual stage count
inline int phaserStagesFromIndex(int index) {
    constexpr int kStages[] = {2, 4, 6, 8, 10, 12};
    if (index < 0 || index >= kPhaserStagesCount) return 4;
    return kStages[index];
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
static_assert(std::size(kPhaserWaveformStrings) == static_cast<std::size_t>(kPhaserWaveformCount));

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
static_assert(std::size(kHarmonizerKeyStrings) == static_cast<std::size_t>(kHarmonizerKeyCount));

// =============================================================================
// Harmonizer: Scale dropdown (16 types, stepCount = 15)
// =============================================================================

inline constexpr int kHarmonizerScaleCount = 16;

inline const Steinberg::Vst::TChar* const kHarmonizerScaleStrings[] = {
    STR16("Major"), STR16("Natural Minor"), STR16("Harmonic Minor"),
    STR16("Melodic Minor"), STR16("Dorian"), STR16("Mixolydian"),
    STR16("Phrygian"), STR16("Lydian"), STR16("Chromatic"),
    STR16("Locrian"), STR16("Major Pentatonic"), STR16("Minor Pentatonic"),
    STR16("Blues"), STR16("Whole Tone"),
    STR16("Diminished (W-H)"), STR16("Diminished (H-W)"),
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

// =============================================================================
// Oscillator type-specific dropdowns (068-osc-type-params)
// =============================================================================

// OscWaveform dropdown (5 waveforms, stepCount = 4)
// Shared by PolyBLEP Waveform and Sync Slave Waveform
inline constexpr int kOscWaveformCount = 5;

inline const Steinberg::Vst::TChar* const kOscWaveformStrings[] = {
    STR16("Sine"),
    STR16("Sawtooth"),
    STR16("Square"),
    STR16("Pulse"),
    STR16("Triangle"),
};

// PD Waveform dropdown (8 waveforms, stepCount = 7)
inline constexpr int kPDWaveformCount = 8;

inline const Steinberg::Vst::TChar* const kPDWaveformStrings[] = {
    STR16("Saw"),
    STR16("Square"),
    STR16("Pulse"),
    STR16("DoubleSine"),
    STR16("HalfSine"),
    STR16("ResSaw"),
    STR16("ResTri"),
    STR16("ResTrap"),
};

// Sync Mode dropdown (3 modes, stepCount = 2)
inline constexpr int kSyncModeCount = 3;

inline const Steinberg::Vst::TChar* const kSyncModeStrings[] = {
    STR16("Hard"),
    STR16("Reverse"),
    STR16("Phase Advance"),
};

// Chaos Attractor dropdown (5 attractors, stepCount = 4)
inline constexpr int kChaosAttractorCount = 5;

inline const Steinberg::Vst::TChar* const kChaosAttractorStrings[] = {
    STR16("Lorenz"),
    STR16("Rossler"),
    STR16("Chua"),
    STR16("Duffing"),
    STR16("Van der Pol"),
};

// Chaos Output dropdown (3 axes, stepCount = 2)
inline constexpr int kChaosOutputCount = 3;

inline const Steinberg::Vst::TChar* const kChaosOutputStrings[] = {
    STR16("X"),
    STR16("Y"),
    STR16("Z"),
};

// Particle Spawn Mode dropdown (3 modes, stepCount = 2)
inline constexpr int kParticleSpawnModeCount = 3;

inline const Steinberg::Vst::TChar* const kParticleSpawnModeStrings[] = {
    STR16("Regular"),
    STR16("Random"),
    STR16("Burst"),
};

// Particle Envelope Type dropdown (6 types, stepCount = 5)
inline constexpr int kParticleEnvTypeCount = 6;

inline const Steinberg::Vst::TChar* const kParticleEnvTypeStrings[] = {
    STR16("Hann"),
    STR16("Trap"),
    STR16("Sine"),
    STR16("Blackman"),
    STR16("Linear"),
    STR16("Exp"),
};

// Formant Vowel dropdown (5 vowels, stepCount = 4)
inline constexpr int kFormantVowelCount = 5;

inline const Steinberg::Vst::TChar* const kFormantVowelStrings[] = {
    STR16("A"),
    STR16("E"),
    STR16("I"),
    STR16("O"),
    STR16("U"),
};

// Noise Color dropdown (6 colors, stepCount = 5)
// Exposes 6 of 8 NoiseColor enum values (Velvet and RadioStatic omitted per R-006)
inline constexpr int kNoiseColorCount = 6;

inline const Steinberg::Vst::TChar* const kNoiseColorStrings[] = {
    STR16("White"),
    STR16("Pink"),
    STR16("Brown"),
    STR16("Blue"),
    STR16("Violet"),
    STR16("Grey"),
};

// =============================================================================
// Arp Scale Type: display order mapping (084-arp-scale-mode)
// =============================================================================
// The Arp Scale Type dropdown puts Chromatic first (UI index 0),
// whereas the ScaleType enum has Chromatic at index 8.
// These arrays map between UI order and enum order.

inline constexpr int kArpScaleTypeCount = 16;
inline constexpr int kArpRootNoteCount = 12;

// Maps UI dropdown index to ScaleType enum value
inline constexpr std::array<int, 16> kArpScaleDisplayOrder = {
    8,   // 0: Chromatic
    0,   // 1: Major
    1,   // 2: Natural Minor
    2,   // 3: Harmonic Minor
    3,   // 4: Melodic Minor
    4,   // 5: Dorian
    6,   // 6: Phrygian
    7,   // 7: Lydian
    5,   // 8: Mixolydian
    9,   // 9: Locrian
    10,  // 10: Major Pentatonic
    11,  // 11: Minor Pentatonic
    12,  // 12: Blues
    13,  // 13: Whole Tone
    14,  // 14: Diminished (W-H)
    15,  // 15: Diminished (H-W)
};

// Maps ScaleType enum value to UI dropdown index (inverse of above)
inline constexpr std::array<int, 16> kArpScaleEnumToDisplay = {
    1,   // Major(0) -> UI 1
    2,   // NaturalMinor(1) -> UI 2
    3,   // HarmonicMinor(2) -> UI 3
    4,   // MelodicMinor(3) -> UI 4
    5,   // Dorian(4) -> UI 5
    8,   // Mixolydian(5) -> UI 8
    6,   // Phrygian(6) -> UI 6
    7,   // Lydian(7) -> UI 7
    0,   // Chromatic(8) -> UI 0
    9,   // Locrian(9) -> UI 9
    10,  // MajorPentatonic(10) -> UI 10
    11,  // MinorPentatonic(11) -> UI 11
    12,  // Blues(12) -> UI 12
    13,  // WholeTone(13) -> UI 13
    14,  // DiminishedWH(14) -> UI 14
    15,  // DiminishedHW(15) -> UI 15
};

} // namespace Ruinae
