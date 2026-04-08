# Data Model: Membrum Phase 1 -- Plugin Scaffold + Single Voice

**Date**: 2026-04-08

## Entities

### Parameter IDs (plugin_ids.h)

```cpp
namespace Membrum {

static const Steinberg::FUID kProcessorUID(0x4D656D62, 0x72756D50, 0x726F6331, 0x00000136);
static const Steinberg::FUID kControllerUID(0x4D656D62, 0x72756D43, 0x74726C31, 0x00000136);

static constexpr auto kSubCategories = "Instrument|Drum";

constexpr Steinberg::int32 kCurrentStateVersion = 1;

enum ParameterIds : Steinberg::Vst::ParamID
{
    kMaterialId       = 100,  // 0.0 woody -- 1.0 metallic
    kSizeId           = 101,  // 0.0 small(500Hz) -- 1.0 large(50Hz)
    kDecayId          = 102,  // 0.0 short -- 1.0 long
    kStrikePositionId = 103,  // 0.0 center -- 1.0 edge
    kLevelId          = 104,  // 0.0 silent -- 1.0 full
};

} // namespace Membrum
```

Parameter ID range 100-199 reserved for Membrum Phase 1. Phase 2+ will use higher ranges for per-pad parameters.

### DrumVoice (plugins/membrum/src/dsp/drum_voice.h)

```
DrumVoice
  |-- ImpactExciter exciter_         (Layer 2, 280 bytes)
  |-- ModalResonatorBank modalBank_  (Layer 2, 3872 bytes, 32-byte aligned)
  |-- ADSREnvelope ampEnvelope_      (Layer 1, 3172 bytes)
  |
  |-- float material_     (cached param: 0.0-1.0)
  |-- float size_         (cached param: 0.0-1.0)
  |-- float decay_        (cached param: 0.0-1.0)
  |-- float strikePos_    (cached param: 0.0-1.0)
  |-- float level_        (cached param: 0.0-1.0)
  |-- bool active_        (true when envelope is active)
```

**Methods**:
- `prepare(double sampleRate)` -- prepare all sub-components
- `noteOn(float velocity)` -- trigger exciter, set modes, gate envelope
- `noteOff()` -- gate(false) on envelope
- `process() -> float` -- one sample: exciter -> modal bank -> envelope * level
- `isActive() -> bool` -- ampEnvelope_.isActive()
- `setMaterial/setSize/setDecay/setStrikePosition/setLevel(float)` -- parameter setters

### Processor (plugins/membrum/src/processor/processor.h)

```
Membrum::Processor : public Steinberg::Vst::AudioEffect
  |-- DrumVoice voice_
  |-- std::atomic<float> material_, size_, decay_, strikePosition_, level_
  |-- double sampleRate_
  |
  |-- initialize()       -> add event input bus, stereo output bus (0 audio inputs)
  |-- setupProcessing()  -> prepare voice at sample rate
  |-- process()          -> handle MIDI, process voice, write stereo output
  |-- getState/setState  -> version + 5 float64 params
```

### Controller (plugins/membrum/src/controller/controller.h)

```
Membrum::Controller : public Steinberg::Vst::EditControllerEx1
  |-- initialize()       -> register 5 RangeParameters (all 0.0-1.0)
  |-- setComponentState() -> sync from processor state
  |-- createView()       -> return nullptr (no custom UI, host-generic only)
```

## State Format (Binary)

```
Offset  Size    Field
0       4       int32: state version (1)
4       8       float64: material (normalized)
12      8       float64: size (normalized)
20      8       float64: decay (normalized)
28      8       float64: strikePosition (normalized)
36      8       float64: level (normalized)
```

Total: 44 bytes.

## Membrane Mode Constants

```cpp
// 16 circular membrane modes -- Bessel zero ratios (j_mn / j_01)
constexpr std::array<float, 16> kMembraneRatios = {
    1.000f, 1.593f, 2.136f, 2.296f, 2.653f, 2.918f, 3.156f, 3.501f,
    3.600f, 3.649f, 4.060f, 4.231f, 4.602f, 4.832f, 4.903f, 5.131f
};

// Bessel order (m) for each mode -- determines strike position response
constexpr std::array<int, 16> kMembraneBesselOrder = {
    0, 1, 2, 0, 3, 1, 4, 2, 0, 5, 3, 1, 6, 4, 2, 0
};

// j_mn values (actual Bessel zeros, for amplitude calculation)
constexpr std::array<float, 16> kMembraneBesselZeros = {
    2.405f, 3.832f, 5.136f, 5.520f, 6.380f, 7.016f, 7.588f, 8.417f,
    8.654f, 8.772f, 9.761f, 10.173f, 11.065f, 11.620f, 11.791f, 12.336f
};
```

## Parameter Mapping Detail

### Material (0.0 woody -- 1.0 metallic)
- `brightness = material` (maps to ModalResonatorBank b3 coefficient)
- `stretch = material * 0.3` (stiffness/inharmonicity)
- `baseDecayTime = lerp(0.15, 0.8, material)` (woody decays faster inherently)

### Size (0.0 small -- 1.0 large)
- `f0 = 500.0 * pow(0.1, size)` -- exponential: 500Hz at 0.0, ~158Hz at 0.5, 50Hz at 1.0
- Mode frequencies: `f[k] = f0 * kMembraneRatios[k]`
- Subtle secondary: `baseDecayTime *= 1.0 + 0.1 * size` (larger bodies ring slightly longer)

### Decay (0.0 short -- 1.0 long)
- `decayScale = exp(lerp(log(0.3), log(3.0), decay))` -- exponential mapping
- `effectiveDecayTime = baseDecayTime * decayScale`
- Preserves material's relative per-mode decay ratios

### Strike Position (0.0 center -- 1.0 edge)
- `r_over_a = strikePosition * 0.9` (never hit exactly at edge)
- `amplitude[k] = abs(J_m(j_mn * r_over_a))` where m = kMembraneBesselOrder[k]
- Center: only m=0 modes (indices 0, 3, 8, 15) are excited
- Edge: all modes excited, higher-order modes dominate

### Level (0.0 silent -- 1.0 full)
- Direct linear gain on post-envelope output
- Applied as: `output * level_`

### Velocity Mapping (automatic, not a parameter)
- velocity is normalized to [0.0, 1.0] (raw MIDI velocity / 127.0f) before being passed to DrumVoice::noteOn()
- Passed to ImpactExciter.trigger():
  - amplitude: `pow(velocity, 0.6)` (built into exciter)
  - hardness: `lerp(0.3, 0.8, velocity)`
  - brightness: `lerp(0.15, 0.4, velocity)`
  - mass: `0.3` (fixed)
  - position: `0.0` (comb filter disabled)
  - f0: `0.0` (comb filter disabled)

## Relationships

```
Processor 1---1 DrumVoice
DrumVoice 1---1 ImpactExciter
DrumVoice 1---1 ModalResonatorBank
DrumVoice 1---1 ADSREnvelope

Controller ---parameters---> Host ---automation---> Processor
Host ---MIDI---> Processor ---trigger---> DrumVoice
```
