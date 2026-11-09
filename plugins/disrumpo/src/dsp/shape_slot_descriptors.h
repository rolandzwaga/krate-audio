#pragma once

// ==============================================================================
// Shape-slot descriptors (audit D-090)
// ==============================================================================
// One table per distortion type: what each of the ten normalized shape slots
// means. Processor::mapShapeSlotsToParams applies it to DistortionParams and
// formatShapeSlot (controller/format_helpers.h) shows the same values, so the
// host text and the knob popups cannot drift from the DSP again.
//
// A descriptor names the DistortionParams field a slot drives and how:
//   Norm     field = v (shown as a percent unless `display` says otherwise)
//   Linear   field = min + v * span
//   Bipolar  field = (v * 2 - 1) * span, computed as v * (2 * span) - span
//   Index    field = min + int(v * span + 0.5)      (span = entries - 1)
//   Pow2     field = min * 2^int(v * span + 0.5)
//   Toggle   field = v >= 0.5
// `display` covers the Norm fields the adapter maps further (distortion_adapter.cpp):
// the 9-entry waveshape curve selectors, Temporal's hold time and Ring
// Saturation's carrier, which is Hz or a harmonic ratio depending on slot 5.
// ==============================================================================

#include "distortion_adapter.h"
#include "distortion_types.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace Disrumpo {

inline constexpr int kShapeSlotDescriptorCount = 10;

enum class SlotMap : uint8_t { Unused, Norm, Linear, Bipolar, Index, Pow2, Toggle };

enum class SlotDisplay : uint8_t {
    Plain,           ///< the field value with `unit` (Norm: percent)
    CurveName,       ///< kWaveshapeCurveNames[round(field * 8)]
    TemporalHoldMs,  ///< 1 + field * 499 ms (the Hysteresis hold decay)
    RingCarrier,     ///< 20 * 250^field Hz, or ratio 1 + round(field * 15) in Harmonic mode
};

struct ShapeSlotDescriptor {
    SlotMap map = SlotMap::Unused;
    float DistortionParams::* floatField = nullptr;
    int DistortionParams::* intField = nullptr;
    bool DistortionParams::* boolField = nullptr;
    float min = 0.0f;
    float span = 1.0f;
    const char* unit = "";
    const char* const* labels = nullptr;  ///< Index slots: one name per entry
    SlotDisplay display = SlotDisplay::Plain;
};

/// The 9 WaveshapeType curves the adapter selects with round(field * 8).
inline constexpr std::array<const char*, 9> kWaveshapeCurveNames{
    "Tanh", "Atan", "Cubic", "Quintic", "RSqrt", "Erf", "HardClip", "Diode", "Tube"};

/// Ring Saturation's Freq mode slot, and its entry that turns slot 6 into a harmonic ratio.
inline constexpr int kRingSatFreqModeSlot = 5;
inline constexpr int kRingSatHarmonicFreqMode = 1;

// Entry names of the Index slots, as the editor's option menus list them.
namespace ShapeSlotLabels {
inline constexpr const char* kTubeStage[] = {"12AX7", "EL84", "6L6", "300B"};
inline constexpr const char* kTapeModel[] = {"Simple", "Hysteresis"};
inline constexpr const char* kTransistor[] = {"Germanium", "Silicon"};
inline constexpr const char* kSergeModel[] = {"Serge", "Simple", "Buchla", "Lockhart"};
inline constexpr const char* kBitcrushMode[] = {"Truncate", "Dither"};
inline constexpr const char* kSampleMode[] = {"ZOH", "Linear"};
inline constexpr const char* kTemporalMode[] = {"Follow", "Inverse", "Transient", "Hyst"};
inline constexpr const char* kCarrierType[] = {"Sine", "Triangle", "Square", "Saw"};
inline constexpr const char* kRingFreqMode[] = {"Fixed", "Harmonic", "Track", "Random"};
inline constexpr const char* kFeedbackFilter[] = {"Off", "LP", "BP", "HP"};
inline constexpr const char* kFeedbackStages[] = {"1x", "2x", "3x", "4x"};
inline constexpr const char* kBitwiseOp[] = {"XorPat", "XorPrev", "BitAnd", "Overflow"};
inline constexpr const char* kAttractor[] = {"Lorenz", "Rossler", "Chua", "Henon"};
inline constexpr const char* kVowel[] = {"A", "E", "I", "O", "U"};
inline constexpr const char* kFormantCount[] = {"2", "3", "4", "5"};
inline constexpr const char* kGrainEnvelope[] = {"Hann", "Trap", "Sine", "Blk"};
inline constexpr const char* kSpectralMode[] = {"PerBin", "MagOnly", "Selective", "Bitcrush"};
inline constexpr const char* kSpectralMag[] = {"Linear", "Log", "Bark", "ERB"};
inline constexpr const char* kSpectralPhase[] = {"Preserve", "Random", "Gradient", "Temporal"};
inline constexpr const char* kFractalMode[] = {"Residual", "Multiband", "Harmonic", "Cascade", "Feedback"};
inline constexpr const char* kFractalBlend[] = {"Add", "Multiply", "Max", "Screen"};
inline constexpr const char* kStochasticCurve[] = {"Tanh", "Atan", "Cubic", "Qntc", "RSqrt", "Erf"};
inline constexpr const char* kStochasticCorr[] = {"None", "Sample", "Param", "Both"};
inline constexpr const char* kNoiseColor[] = {"White", "Pink", "Brown", "Blue", "Violet"};
inline constexpr const char* kAllpassTopo[] = {"Single", "Chain", "Karplus", "Matrix"};
inline constexpr const char* kAllpassSat[] = {"Tanh", "Clip", "Asym", "Hyst"};
}  // namespace ShapeSlotLabels

namespace ShapeSlotDetail {

using P = DistortionParams;

constexpr ShapeSlotDescriptor norm(float P::* f, SlotDisplay d = SlotDisplay::Plain) noexcept {
    return {.map = SlotMap::Norm, .floatField = f, .unit = "%", .display = d};
}
constexpr ShapeSlotDescriptor lin(float P::* f, float min, float span, const char* unit = "") noexcept {
    return {.map = SlotMap::Linear, .floatField = f, .min = min, .span = span, .unit = unit};
}
constexpr ShapeSlotDescriptor bip(float P::* f, float span, const char* unit = "") noexcept {
    return {.map = SlotMap::Bipolar, .floatField = f, .span = span, .unit = unit};
}
template <size_t N>
constexpr ShapeSlotDescriptor idx(int P::* f, const char* const (&labels)[N], int first = 0) noexcept {
    return {.map = SlotMap::Index, .intField = f, .min = static_cast<float>(first),
            .span = static_cast<float>(N - 1), .labels = labels};
}
constexpr ShapeSlotDescriptor count(int P::* f, int first, int entries, const char* unit = "") noexcept {
    return {.map = SlotMap::Index, .intField = f, .min = static_cast<float>(first),
            .span = static_cast<float>(entries - 1), .unit = unit};
}
constexpr ShapeSlotDescriptor pow2(int P::* f, int first, int entries) noexcept {
    return {.map = SlotMap::Pow2, .intField = f, .min = static_cast<float>(first),
            .span = static_cast<float>(entries - 1)};
}
constexpr ShapeSlotDescriptor toggle(bool P::* f) noexcept {
    return {.map = SlotMap::Toggle, .boolField = f};
}

using Slots = std::array<ShapeSlotDescriptor, kShapeSlotDescriptorCount>;
namespace L = ShapeSlotLabels;
constexpr auto kCurve = SlotDisplay::CurveName;

constexpr Slots describe(DistortionType type) noexcept {
    switch (type) {
        case DistortionType::SoftClip: return {norm(&P::curve), norm(&P::knee)};
        case DistortionType::HardClip: return {norm(&P::threshold), norm(&P::ceiling)};
        case DistortionType::Tube: return {bip(&P::bias, 1.0f), norm(&P::sag), idx(&P::satStage, L::kTubeStage)};
        case DistortionType::Tape:
            return {bip(&P::bias, 1.0f), norm(&P::sag), norm(&P::speed), idx(&P::tapeModel, L::kTapeModel),
                    norm(&P::hfRoll), norm(&P::flutter)};
        case DistortionType::Fuzz:
            return {bip(&P::bias, 1.0f), idx(&P::transistor, L::kTransistor), norm(&P::octave), norm(&P::sustain)};
        case DistortionType::AsymmetricFuzz:
            return {bip(&P::bias, 1.0f), norm(&P::asymmetry), idx(&P::transistor, L::kTransistor),
                    norm(&P::sustain), norm(&P::body)};
        case DistortionType::SineFold:
            return {lin(&P::folds, 1.0f, 11.0f), bip(&P::symmetry, 1.0f), norm(&P::shape), bip(&P::bias, 1.0f),
                    norm(&P::smoothness)};
        case DistortionType::TriangleFold:
            return {lin(&P::folds, 1.0f, 11.0f), bip(&P::symmetry, 1.0f), norm(&P::angle), bip(&P::bias, 1.0f),
                    norm(&P::smoothness)};
        case DistortionType::SergeFold:
            return {lin(&P::folds, 1.0f, 11.0f), bip(&P::symmetry, 1.0f), idx(&P::foldModel, L::kSergeModel),
                    bip(&P::bias, 1.0f), norm(&P::shape), norm(&P::smoothness)};
        case DistortionType::FullRectify: return {norm(&P::smoothness), toggle(&P::dcBlock)};
        case DistortionType::HalfRectify: return {norm(&P::threshold), norm(&P::smoothness), toggle(&P::dcBlock)};
        case DistortionType::Bitcrush:
            return {lin(&P::bitDepth, 4.0f, 12.0f, " bit"), norm(&P::dither), idx(&P::bitcrushMode, L::kBitcrushMode),
                    norm(&P::jitter)};
        case DistortionType::SampleReduce:
            return {lin(&P::sampleRateRatio, 1.0f, 31.0f, "x"), norm(&P::jitter), idx(&P::sampleMode, L::kSampleMode),
                    norm(&P::smoothness)};
        case DistortionType::Quantize:
            return {norm(&P::quantLevels), norm(&P::dither), norm(&P::smoothness), norm(&P::quantOffset)};
        case DistortionType::Aliasing:
            return {lin(&P::sampleRateRatio, 2.0f, 30.0f, "x"), bip(&P::freqShift, 5000.0f, " Hz")};
        case DistortionType::BitwiseMangler:
            return {idx(&P::bitwiseOp, L::kBitwiseOp), norm(&P::bitwiseIntensity), norm(&P::bitwisePattern)};
        case DistortionType::Temporal:
            return {idx(&P::dynamicMode, L::kTemporalMode), norm(&P::sensitivity), norm(&P::dynamicCurve, kCurve),
                    lin(&P::attackMs, 1.0f, 499.0f, " ms"), lin(&P::releaseMs, 10.0f, 4990.0f, " ms"),
                    norm(&P::dynamicDepth), ShapeSlotDescriptor{}, norm(&P::hold, SlotDisplay::TemporalHoldMs)};
        case DistortionType::RingSaturation:
            return {norm(&P::modDepth), count(&P::stages, 1, 4, "x"), norm(&P::rsCurve, kCurve),
                    idx(&P::carrierType, L::kCarrierType), bip(&P::bias, 1.0f), idx(&P::rsFreqSelect, L::kRingFreqMode),
                    norm(&P::rsCarrierFreq, SlotDisplay::RingCarrier)};
        case DistortionType::FeedbackDist:
            return {lin(&P::feedback, 0.0f, 1.5f), lin(&P::delayMs, 1.0f, 99.0f, " ms"), norm(&P::fbCurve, kCurve),
                    idx(&P::filterType, L::kFeedbackFilter), norm(&P::filterFreq),
                    idx(&P::stages, L::kFeedbackStages, 1), toggle(&P::limiter), norm(&P::limThreshold),
                    norm(&P::fbDecay)};
        case DistortionType::AllpassResonant:
            return {idx(&P::allpassTopo, L::kAllpassTopo), lin(&P::resonantFreq, 20.0f, 1980.0f, " Hz"),
                    lin(&P::allpassFeedback, 0.0f, 0.99f), lin(&P::decayTimeS, 0.01f, 9.99f, " s"),
                    norm(&P::allpassDrive), idx(&P::allpassSatType, L::kAllpassSat), ShapeSlotDescriptor{},
                    norm(&P::allpassDamp)};
        case DistortionType::Chaos:
            return {idx(&P::chaosAttractor, L::kAttractor), lin(&P::attractorSpeed, 0.01f, 99.99f),
                    norm(&P::chaosAmount), norm(&P::chaosCoupling), norm(&P::chaosXDrive), norm(&P::chaosYDrive),
                    norm(&P::smoothness)};
        case DistortionType::Formant:
            return {idx(&P::vowelSelect, L::kVowel), bip(&P::formantShift, 24.0f, " st"), norm(&P::formantCurve),
                    norm(&P::formantReso), norm(&P::formantBW), idx(&P::formantCount, L::kFormantCount),
                    norm(&P::formantGender), norm(&P::formantBlend)};
        case DistortionType::Granular:
            return {lin(&P::grainSizeMs, 5.0f, 95.0f, " ms"), norm(&P::grainDensity), norm(&P::grainPVar),
                    norm(&P::grainDVar), norm(&P::grainPos), norm(&P::grainCurve),
                    idx(&P::grainEnvType, L::kGrainEnvelope)};
        case DistortionType::Spectral:
            return {idx(&P::spectralMode, L::kSpectralMode), pow2(&P::fftSize, 512, 4), norm(&P::spectralCurve, kCurve),
                    norm(&P::spectralTilt), norm(&P::spectralThreshold), idx(&P::spectralMagMode, L::kSpectralMag),
                    norm(&P::spectralFreq), idx(&P::spectralPhase, L::kSpectralPhase)};
        case DistortionType::Fractal:
            return {idx(&P::fractalMode, L::kFractalMode), count(&P::iterations, 1, 8), lin(&P::scaleFactor, 0.3f, 0.6f),
                    norm(&P::fractalCurve, kCurve), norm(&P::frequencyDecay), lin(&P::fractalFB, 0.0f, 0.5f),
                    idx(&P::fractalBlend, L::kFractalBlend), norm(&P::fractalDepth)};
        case DistortionType::Stochastic:
            return {idx(&P::stochasticCurve, L::kStochasticCurve), norm(&P::jitterAmount),
                    lin(&P::jitterRate, 0.1f, 99.9f, " Hz"), norm(&P::coefficientNoise), norm(&P::stochasticDrift),
                    idx(&P::stochasticCorr, L::kStochasticCorr), norm(&P::stochasticSmooth),
                    idx(&P::stochasticNoiseColor, L::kNoiseColor)};
        default: return {};
    }
}

constexpr std::array<Slots, kDistortionTypeCount> buildTable() noexcept {
    std::array<Slots, kDistortionTypeCount> table{};
    for (int t = 0; t < kDistortionTypeCount; ++t) table[static_cast<size_t>(t)] = describe(static_cast<DistortionType>(t));
    return table;
}

}  // namespace ShapeSlotDetail

/// Descriptors of every type's ten shape slots, indexed by DistortionType.
inline constexpr std::array<std::array<ShapeSlotDescriptor, kShapeSlotDescriptorCount>, kDistortionTypeCount>
    kShapeSlotDescriptors = ShapeSlotDetail::buildTable();

[[nodiscard]] constexpr const ShapeSlotDescriptor& shapeSlotDescriptor(DistortionType type, int slot) noexcept {
    return kShapeSlotDescriptors[static_cast<size_t>(type)][static_cast<size_t>(slot)];
}

/// Plain value of a Norm / Linear / Bipolar slot (the float the field receives).
[[nodiscard]] inline float shapeSlotFloat(const ShapeSlotDescriptor& d, float v) noexcept {
    switch (d.map) {
        case SlotMap::Linear: return d.min + v * d.span;
        case SlotMap::Bipolar: return v * (2.0f * d.span) - d.span;
        default: return v;
    }
}

/// Entry index of an Index / Pow2 slot: int(v * (entries - 1) + 0.5).
[[nodiscard]] inline int shapeSlotIndex(const ShapeSlotDescriptor& d, float v) noexcept {
    return static_cast<int>(v * d.span + 0.5f);
}

/// Value an Index / Pow2 slot writes to its int field.
[[nodiscard]] inline int shapeSlotInt(const ShapeSlotDescriptor& d, float v) noexcept {
    const int first = static_cast<int>(d.min);
    return d.map == SlotMap::Pow2 ? first * (1 << shapeSlotIndex(d, v)) : first + shapeSlotIndex(d, v);
}

/// Write slot value @p v to the DistortionParams field the descriptor names.
inline void applyShapeSlot(const ShapeSlotDescriptor& d, float v, DistortionParams& p) noexcept {
    switch (d.map) {
        case SlotMap::Unused: break;
        case SlotMap::Norm:
        case SlotMap::Linear:
        case SlotMap::Bipolar: p.*d.floatField = shapeSlotFloat(d, v); break;
        case SlotMap::Index:
        case SlotMap::Pow2: p.*d.intField = shapeSlotInt(d, v); break;
        case SlotMap::Toggle: p.*d.boolField = v >= 0.5f; break;
    }
}

/// Write every used shape slot of @p type (kShapeSlotDescriptorCount values) to @p p. The
/// processor maps a node's slots with it, and the controller derives the node parameters that
/// mirror a slot (Bias, Folds, Bit Depth: the slot wins, follow-ups 34 and 87).
inline void applyShapeSlots(DistortionType type, const float* slots, DistortionParams& p) noexcept {
    const int t = static_cast<int>(type);
    if (t < 0 || t >= kDistortionTypeCount) {
        return;
    }
    for (int s = 0; s < kShapeSlotDescriptorCount; ++s) {
        const ShapeSlotDescriptor& d = shapeSlotDescriptor(type, s);
        if (d.map != SlotMap::Unused) {
            applyShapeSlot(d, slots[s], p);
        }
    }
}

/// Waveshape curve selector [0, 1] -> WaveshapeType index 0..8 (adapter, CurveName display).
[[nodiscard]] inline int waveshapeCurveIndex(float v) noexcept {
    return std::clamp(static_cast<int>(v * 8.0f + 0.5f), 0, 8);
}

/// Temporal hold [0, 1] -> Hysteresis decay 1..500 ms (adapter, TemporalHoldMs display).
[[nodiscard]] inline float temporalHoldMs(float v) noexcept { return 1.0f + v * 499.0f; }

/// Ring Saturation carrier in Fixed mode: 20 * 250^v Hz (adapter).
[[nodiscard]] inline float ringSatCarrierHz(float v) noexcept { return 20.0f * std::pow(250.0f, v); }

/// Ring Saturation carrier in Harmonic mode: ratio 1 + round(v * 15) (adapter).
[[nodiscard]] inline float ringSatHarmonicRatio(float v) noexcept { return 1.0f + std::round(v * 15.0f); }

}  // namespace Disrumpo
