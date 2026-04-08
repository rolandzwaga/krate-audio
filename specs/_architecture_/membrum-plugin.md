# Membrum Plugin Architecture

[<- Back to Architecture Index](README.md)

**Plugin**: Membrum (Synthesized Drum Machine) | **Location**: `plugins/membrum/`

---

## Overview

Membrum is a synthesized drum machine plugin that generates percussive sounds via physical modelling. Phase 1 delivers a single drum voice triggered by MIDI note 36, using an impact exciter driving a 16-mode circular membrane resonator with an amplitude envelope. No custom UI -- host-generic editor only.

---

## VST3 Components

| Component | Path | Purpose |
|-----------|------|---------|
| Processor | `plugins/membrum/src/processor/` | Audio processing (real-time), MIDI handling, voice management |
| Controller | `plugins/membrum/src/controller/` | Parameter registration, state sync |
| Entry | `plugins/membrum/src/entry.cpp` | Factory registration |
| IDs | `plugins/membrum/src/plugin_ids.h` | FUIDs, subcategories, parameter IDs |

---

## Audio Bus Configuration

| Bus | Type | Channels | Purpose |
|-----|------|----------|---------|
| Audio Output | `kMain` | Stereo | Synthesized audio output (mono voice duplicated to stereo) |
| MIDI Input | Event | N/A | Note on/off for triggering drum voice |

---

## Parameters (Phase 1)

| ID | Name | Range | Default | Description |
|----|------|-------|---------|-------------|
| kMaterialId (100) | Material | 0.0-1.0 | 0.5 | Controls brightness/damping: 0 = woody (max HF damping), 1 = metallic (no HF damping). Maps directly to ModalResonatorBank brightness parameter. |
| kSizeId (101) | Size | 0.0-1.0 | 0.5 | Drum size. Maps to fundamental frequency: 0 = 500 Hz (small), 1 = 60 Hz (large). |
| kDecayId (102) | Decay | 0.0-1.0 | 0.5 | Resonator decay time. Maps to 0.05-2.0 seconds. |
| kStrikePosId (103) | Strike Position | 0.0-1.0 | 0.5 | Affects mode amplitude distribution via Bessel function evaluation at the strike radius. |
| kLevelId (104) | Level | 0.0-1.0 | 0.8 | Output level multiplier. |

---

## Plugin-Local DSP Components

### DrumVoice
**Path:** [drum_voice.h](../../plugins/membrum/src/dsp/drum_voice.h) | **Since:** 0.1.0

Single drum voice that wires three existing KrateDSP components into a percussive physical model: ImpactExciter (Layer 2) produces the strike impulse, ModalResonatorBank (Layer 2, 16 modes) provides the resonant body using circular membrane Bessel ratios, and ADSREnvelope (Layer 1) shapes the amplitude.

```cpp
namespace Membrum {

class DrumVoice {
    // Lifecycle
    DrumVoice() noexcept = default;
    void prepare(double sampleRate) noexcept;

    // Note control
    void noteOn(uint8_t velocity) noexcept;
    void noteOff() noexcept;

    // Audio processing (real-time safe, per-sample)
    [[nodiscard]] float process() noexcept;
    [[nodiscard]] bool isActive() const noexcept;

    // Parameter setters (safe to call from audio thread)
    void setMaterial(float value) noexcept;   // 0-1, maps to brightness
    void setSize(float value) noexcept;       // 0-1, maps to fundamental freq
    void setDecay(float value) noexcept;      // 0-1, maps to decay time
    void setStrikePosition(float value) noexcept; // 0-1, maps to Bessel eval radius
    void setLevel(float value) noexcept;      // 0-1, output gain
};

} // namespace Membrum
```

**When to use:**
- Phase 1: Single-voice membrane drum synthesis triggered by MIDI note 36
- Phase 2+: One DrumVoice per pad in multi-pad voice allocation

**Key design decisions:**
- Uses direct composition (not IResonator interface) since Phase 1 has only one body model
- Parameter mapping is internal to DrumVoice (Material -> brightness, Size -> f0, etc.)
- `noteOn()` calls `setModes()` (resets filter states); parameter changes call `updateModes()` (preserves states)
- Early-out optimization: `process()` returns 0.0f when `!isActive()`

**Dependencies:** ImpactExciter (`<krate/dsp/processors/impact_exciter.h>`), ModalResonatorBank (`<krate/dsp/processors/modal_resonator_bank.h>`), ADSREnvelope (`<krate/dsp/primitives/adsr_envelope.h>`)

---

### membrane_modes.h
**Path:** [membrane_modes.h](../../plugins/membrum/src/dsp/membrane_modes.h) | **Since:** 0.1.0

Compile-time constants and a Bessel function evaluator for the 16-mode circular membrane model. Contains the pre-computed Bessel zero ratios (f_mn / f_01) that define the inharmonic frequency relationships of an ideal circular drum membrane, plus `evaluateBesselJ()` for computing mode amplitudes at a given strike position.

```cpp
namespace Membrum {

// 16-mode frequency ratios relative to fundamental (Bessel zeros)
// j_01=1.000, j_11=1.594, j_21=2.136, j_02=2.296, j_31=2.653, ...
constexpr std::array<float, 16> kMembraneRatios = { ... };

// Mode amplitude weights (derived from Bessel function values)
constexpr std::array<float, 16> kModeAmplitudes = { ... };

// Bessel function evaluator for strike-position-dependent amplitude modulation
[[nodiscard]] constexpr float evaluateBesselJ(int order, float x) noexcept;

} // namespace Membrum
```

**When to use:**
- DrumVoice uses these constants to configure ModalResonatorBank with physically-correct membrane mode frequencies
- `evaluateBesselJ()` computes position-dependent mode amplitudes when Strike Position parameter changes
- Phase 2: May become one of several body model configurations (membrane, plate, bar, shell, etc.)

**Key properties:**
- All data is `constexpr` -- zero runtime cost for the ratio tables
- 16 modes covers the perceptually significant partials of a circular membrane
- Ratios are inharmonic (not integer multiples of fundamental), giving the characteristic drum timbre
