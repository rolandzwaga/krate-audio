# Membrum Plugin Architecture

[<- Back to Architecture Index](README.md)

**Plugin**: Membrum (Synthesized Drum Machine) | **Location**: `plugins/membrum/`

---

## Overview

Membrum is a synthesized drum machine plugin that generates percussive sounds via physical modelling. It features 32 pads (MIDI notes 36-67, GM drum map) with independent per-pad sound configuration, polyphonic voice management with voice stealing, multi-bus output routing, and kit/pad preset infrastructure. Each pad drives an exciter into a resonant body model with tone shaping, material morphing, and unnatural-zone spectral manipulation.

---

## VST3 Components

| Component | Path | Purpose |
|-----------|------|---------|
| Processor | `plugins/membrum/src/processor/` | Audio processing (real-time), MIDI handling, voice management, multi-bus output |
| Controller | `plugins/membrum/src/controller/` | Parameter registration, state sync, selected-pad proxy logic, kit/pad preset providers |
| Entry | `plugins/membrum/src/entry.cpp` | Factory registration |
| IDs | `plugins/membrum/src/plugin_ids.h` | FUIDs, subcategories, parameter IDs, per-pad ID scheme |

---

## Audio Bus Configuration

| Bus | Type | Channels | Purpose |
|-----|------|----------|---------|
| Audio Output 0 | `kMain` | Stereo | Main mix output (all voices route here by default) |
| Audio Output 1-15 | `kAux` | Stereo | Per-pad auxiliary outputs (host-activated, pad routes via `outputBus` field) |
| MIDI Input | Event | N/A | Note on/off for triggering drum voices (MIDI 36-67) |

**Multi-bus routing** (Phase 4): Each pad's `PadConfig::outputBus` field (0-15) determines which output bus receives that pad's audio. Bus 0 (main) always receives all audio. Buses 1-15 are auxiliary and only active when the host activates them via `activateBus()`. The processor tracks activation state in `busActive_[16]` and the extended `VoicePool::processBlock()` overload accumulates voice audio to both the main bus and the pad's assigned aux bus (if active and > 0).

---

## Parameters

### Global Parameters (Phase 1-3)

| ID | Name | Range | Default | Description |
|----|------|-------|---------|-------------|
| kMaterialId (100) | Material | 0.0-1.0 | 0.5 | Proxy: forwards to selected pad's material (offset 2) |
| kSizeId (101) | Size | 0.0-1.0 | 0.5 | Proxy: forwards to selected pad's size (offset 3) |
| kDecayId (102) | Decay | 0.0-1.0 | 0.5 | Proxy: forwards to selected pad's decay (offset 4) |
| kStrikePositionId (103) | Strike Position | 0.0-1.0 | 0.5 | Proxy: forwards to selected pad's strikePosition (offset 5) |
| kLevelId (104) | Level | 0.0-1.0 | 0.8 | Proxy: forwards to selected pad's level (offset 6) |
| kExciterTypeId (200) | Exciter Type | 0-5 | 0 | Proxy: forwards to selected pad's exciterType (offset 0) |
| kBodyModelId (201) | Body Model | 0-5 | 0 | Proxy: forwards to selected pad's bodyModel (offset 1) |
| kExciterFMRatioId (202) | FM Ratio | 0.0-1.0 | 0.5 | Proxy: forwards to selected pad's fmRatio (offset 32) |
| kExciterFeedbackAmountId (203) | Feedback Amount | 0.0-1.0 | 0.0 | Proxy: forwards to selected pad's feedbackAmount (offset 33) |
| kExciterNoiseBurstDurationId (204) | Noise Burst Duration | 0.0-1.0 | 0.5 | Proxy: forwards to selected pad's noiseBurstDuration (offset 34) |
| kExciterFrictionPressureId (205) | Friction Pressure | 0.0-1.0 | 0.0 | Proxy: forwards to selected pad's frictionPressure (offset 35) |
| kToneShaperFilterTypeId (210) | TS Filter Type | 0-2 | 0 | Proxy: LP/HP/BP (offset 7) |
| kToneShaperFilterCutoffId (211) | TS Filter Cutoff | 0.0-1.0 | 1.0 | Proxy: offset 8 |
| kToneShaperFilterResonanceId (212) | TS Filter Resonance | 0.0-1.0 | 0.0 | Proxy: offset 9 |
| kToneShaperFilterEnvAmountId (213) | TS Filter Env Amount | 0.0-1.0 | 0.5 | Proxy: offset 10 |
| kToneShaperDriveAmountId (214) | TS Drive Amount | 0.0-1.0 | 0.0 | Proxy: offset 11 |
| kToneShaperFoldAmountId (215) | TS Fold Amount | 0.0-1.0 | 0.0 | Proxy: offset 12 |
| kToneShaperPitchEnvStartId (216) | TS Pitch Env Start | 0.0-1.0 | 0.0 | Proxy: offset 13 |
| kToneShaperPitchEnvEndId (217) | TS Pitch Env End | 0.0-1.0 | 0.0 | Proxy: offset 14 |
| kToneShaperPitchEnvTimeId (218) | TS Pitch Env Time | 0.0-1.0 | 0.0 | Proxy: offset 15 |
| kToneShaperPitchEnvCurveId (219) | TS Pitch Env Curve | 0-1 | 0 | Proxy: Exp/Lin (offset 16) |
| kToneShaperFilterEnvAttackId (220) | TS Filter Env Attack | 0.0-1.0 | 0.0 | Proxy: offset 17 |
| kToneShaperFilterEnvDecayId (221) | TS Filter Env Decay | 0.0-1.0 | 0.1 | Proxy: offset 18 |
| kToneShaperFilterEnvSustainId (222) | TS Filter Env Sustain | 0.0-1.0 | 0.0 | Proxy: offset 19 |
| kToneShaperFilterEnvReleaseId (223) | TS Filter Env Release | 0.0-1.0 | 0.1 | Proxy: offset 20 |
| kUnnaturalModeStretchId (230) | Mode Stretch | 0.0-1.0 | 0.333 | Proxy: offset 21 |
| kUnnaturalDecaySkewId (231) | Decay Skew | 0.0-1.0 | 0.5 | Proxy: offset 22 |
| kUnnaturalModeInjectAmountId (232) | Mode Inject Amount | 0.0-1.0 | 0.0 | Proxy: offset 23 |
| kUnnaturalNonlinearCouplingId (233) | Nonlinear Coupling | 0.0-1.0 | 0.0 | Proxy: offset 24 |
| kMorphEnabledId (240) | Morph Enabled | 0-1 | 0 | Proxy: offset 25 |
| kMorphStartId (241) | Morph Start | 0.0-1.0 | 1.0 | Proxy: offset 26 |
| kMorphEndId (242) | Morph End | 0.0-1.0 | 0.0 | Proxy: offset 27 |
| kMorphDurationMsId (243) | Morph Duration | 0.0-1.0 | 0.095 | Proxy: offset 28 |
| kMorphCurveId (244) | Morph Curve | 0-1 | 0 | Proxy: Lin/Exp (offset 29) |
| kMaxPolyphonyId (250) | Max Polyphony | 4-16 | 8 | Global: voice pool polyphony limit |
| kVoiceStealingId (251) | Voice Stealing | 0-2 | 0 | Global: Oldest/Quietest/Priority |
| kChokeGroupId (252) | Choke Group | 0-8 | 0 | Proxy: forwards to selected pad's chokeGroup (offset 30) |
| kSelectedPadId (260) | Selected Pad | 0-31 | 0 | Controller-only: selects which pad the global proxy IDs control |

### Per-Pad Parameter ID Scheme (Phase 4)

Per-pad parameters use a computed ID formula:

```
Parameter ID = kPadBaseId + padIndex * kPadParamStride + paramOffset

kPadBaseId      = 1000
kPadParamStride = 64
padIndex        = [0, 31]   (MIDI notes 36-67)
paramOffset     = [0, 35]   (active); [36, 63] reserved for Phase 5+
```

**ID ranges per pad:**

| Pad | MIDI | IDs (active) | IDs (reserved) |
|-----|------|-------------|----------------|
| 0 | 36 | 1000-1035 | 1036-1063 |
| 1 | 37 | 1064-1099 | 1100-1127 |
| ... | ... | ... | ... |
| 31 | 67 | 2984-3019 | 3020-3047 |

**Helper functions** (`dsp/pad_config.h`):
- `padParamId(padIndex, offset)` -- compute the VST3 parameter ID
- `padIndexFromParamId(paramId)` -- extract pad index (-1 if not a pad param)
- `padOffsetFromParamId(paramId)` -- extract offset (-1 if invalid/reserved)

**Per-pad parameter offsets** (`PadParamOffset` enum):

| Offset | Name | PadConfig Field |
|--------|------|-----------------|
| 0 | kPadExciterType | exciterType |
| 1 | kPadBodyModel | bodyModel |
| 2 | kPadMaterial | material |
| 3 | kPadSize | size |
| 4 | kPadDecay | decay |
| 5 | kPadStrikePosition | strikePosition |
| 6 | kPadLevel | level |
| 7-20 | kPadTSFilter*/Drive/Fold/PitchEnv* | Tone Shaper params |
| 21-24 | kPadModeStretch/DecaySkew/ModeInject/Coupling | Unnatural Zone |
| 25-29 | kPadMorphEnabled/Start/End/Duration/Curve | Material Morph |
| 30 | kPadChokeGroup | chokeGroup |
| 31 | kPadOutputBus | outputBus |
| 32-35 | kPadFMRatio/FeedbackAmount/NoiseBurstDuration/FrictionPressure | Exciter secondary |

### Selected-Pad Proxy Controller Pattern (Phase 4)

The controller implements a **proxy pattern** where global parameter IDs (100-252) act as aliases for the currently selected pad's per-pad parameters. This allows host-generic editors and automation to control whichever pad is selected without knowing about the 32-pad ID scheme.

**How it works:**
1. `kSelectedPadId` (260) is a stepped parameter [0, 31] that selects the active pad
2. When the user changes a global proxy parameter (e.g., `kMaterialId` = 100), the controller's `setParamNormalized()` override forwards the value to the selected pad's corresponding per-pad parameter (e.g., pad 5's `kPadMaterial` at ID 1322)
3. When `kSelectedPadId` changes, `syncGlobalProxyFromPad()` reads the new pad's PadConfig values and updates all global proxy parameters to reflect the new pad's state
4. A `suppressProxyForward_` flag prevents re-entrancy during pad-switch sync

**Controller methods:**
- `setParamNormalized()` -- overridden to intercept global proxy writes and forward to per-pad IDs
- `syncGlobalProxyFromPad(padIndex)` -- reads pad's config, updates all global proxies
- `forwardGlobalToPad(globalId, value)` -- maps a global ID to the selected pad's per-pad ID and sets it

**Processor side:** Global proxy IDs (100-252) are no-ops in `processParameterChanges()`. Only per-pad IDs (1000+) reach the processor and modify `VoicePool::padConfigs_[]`.

---

## Plugin-Local DSP Components

### PadConfig
**Path:** [pad_config.h](../../plugins/membrum/src/dsp/pad_config.h) | **Since:** 0.4.0 (Phase 4)

Pre-allocated struct holding one pad's complete configuration. 32 instances stored in `VoicePool::padConfigs_[]`. No dynamic memory. All float values are normalized [0.0, 1.0] -- denormalization happens at point of use.

```cpp
namespace Membrum {

constexpr int kNumPads        = 32;       // GM drum map MIDI 36-67
constexpr int kPadBaseId      = 1000;     // First per-pad parameter ID
constexpr int kPadParamStride = 64;       // IDs per pad (36 active, 28 reserved)
constexpr int kMaxOutputBuses = 16;       // 1 main + 15 aux

struct PadConfig {
    ExciterType   exciterType;       // offset 0
    BodyModelType bodyModel;         // offset 1
    float material, size, decay, strikePosition, level;  // offsets 2-6
    // ... Tone Shaper (offsets 7-20), Unnatural Zone (21-24),
    //     Material Morph (25-29), chokeGroup (30), outputBus (31),
    //     Exciter secondary (32-35)
};

// ID computation helpers
constexpr int padParamId(int padIndex, int offset) noexcept;
constexpr int padIndexFromParamId(int paramId) noexcept;
constexpr int padOffsetFromParamId(int paramId) noexcept;

} // namespace Membrum
```

**When to use:**
- VoicePool reads PadConfig at noteOn to configure the allocated DrumVoice slot
- Processor dispatches per-pad parameter changes via `setPadConfigField()` / `setPadConfigSelector()`
- State serialization reads/writes all 32 PadConfigs

### DefaultKit
**Path:** [default_kit.h](../../plugins/membrum/src/dsp/default_kit.h) | **Since:** 0.4.0 (Phase 4)

GM-inspired default kit templates applied on first load. Maps all 32 pads (MIDI 36-67) to six archetypes: Kick, Snare, Tom, Hat, Cymbal, Perc.

```cpp
namespace Membrum {

enum class DrumTemplate { Kick, Snare, Tom, Hat, Cymbal, Perc };

namespace DefaultKit {
    void applyTemplate(PadConfig& cfg, DrumTemplate tmpl, float sizeOverride = -1.0f);
    void apply(std::array<PadConfig, kNumPads>& pads);
} // namespace DefaultKit

} // namespace Membrum
```

**Key design decisions:**
- `apply()` initializes all 32 pads with GM-standard assignments (e.g., pad 0 = Bass Drum, pad 2 = Snare, etc.)
- Tom pads (MIDI 41, 43, 45, 47, 48, 50) have progressively decreasing size values (0.8 down to 0.4)
- Hat pads (MIDI 42, 44, 46) are assigned to choke group 1
- Kick template includes pitch envelope (160 Hz -> 50 Hz, 20 ms)
- Called during processor `initialize()` and as fallback for legacy state loading

### PadCategory + classifyPad()
**Path:** [pad_category.h](../../plugins/membrum/src/dsp/pad_category.h) | **Since:** 0.5.0 (Phase 5)

Enum classification of a pad into one of five drum categories, derived from runtime `PadConfig` via a priority-ordered rule chain (FR-033). Used by `CouplingMatrix` Tier 1 to map the two global coupling knobs (Snare Buzz, Tom Resonance) onto concrete source/destination pad pairs.

```cpp
namespace Membrum {

enum class PadCategory : int {
    Kick,       // Membrane body + pitch envelope active
    Snare,      // Membrane body + NoiseBurst exciter
    Tom,        // Membrane body (no pitch env, no noise exciter)
    HatCymbal,  // NoiseBody
    Perc,       // Any other configuration
    kCount
};

[[nodiscard]] PadCategory classifyPad(const PadConfig& cfg) noexcept;

} // namespace Membrum
```

**Classification rules (first match wins):**
1. Membrane body + `tsPitchEnvTime > 0` -> Kick
2. Membrane body + `NoiseBurst` exciter -> Snare
3. Membrane body (fallthrough) -> Tom
4. NoiseBody -> HatCymbal
5. Everything else -> Perc

Called by the processor whenever a pad's body model, exciter type, or pitch-envelope time changes, before invoking `CouplingMatrix::recomputeFromTier1()`.

### CouplingMatrix
**Path:** [coupling_matrix.h](../../plugins/membrum/src/dsp/coupling_matrix.h) | **Since:** 0.5.0 (Phase 5)

Two-layer resolver for 32x32 cross-pad coupling coefficients, clamped to `[0.0, 0.05]` (FR-031). Each pair stores `{computedGain, overrideGain, hasOverride}` and resolves into a flat `effectiveGain[src][dst]` array consumed by the audio thread. No dynamic allocation.

```cpp
namespace Membrum {

class CouplingMatrix {
    static constexpr float kMaxCoefficient = 0.05f;
    static constexpr int   kSize = kNumPads; // 32

    // Tier 1: global knobs + categories (+ optional per-pad amounts for Phase 6)
    void recomputeFromTier1(float snareBuzz, float tomResonance,
                            const PadCategory* categories) noexcept;
    void recomputeFromTier1(float snareBuzz, float tomResonance,
                            const PadCategory* categories,
                            const float* padCouplingAmounts) noexcept;

    // Tier 2: per-pair overrides
    void setOverride(int src, int dst, float coeff) noexcept;
    void clearOverride(int src, int dst) noexcept;
    [[nodiscard]] bool  hasOverrideAt(int src, int dst) const noexcept;
    [[nodiscard]] float getOverrideGain(int src, int dst) const noexcept;

    // Resolved access (audio thread)
    [[nodiscard]] float getEffectiveGain(int src, int dst) const noexcept;
    [[nodiscard]] const float (&effectiveGainArray() const noexcept)[kSize][kSize];

    // Serialization
    [[nodiscard]] int getOverrideCount() const noexcept;
    template <typename Fn> void forEachOverride(Fn&& fn) const noexcept;
    void clearAll() noexcept;
};

} // namespace Membrum
```

**Tier 1 rules:**
- `Kick -> Snare`: `gain = snareBuzz * 0.05`
- `Tom -> Tom`: `gain = tomResonance * 0.05`
- All other pairs: `gain = 0`
- When per-pad amounts are supplied (Phase 6 / FR-014, FR-023), each pair is additionally scaled by `amounts[src] * amounts[dst]`, so a pad with amount = 0 is excluded as both source and receiver.

**Tier 2 (overrides):** User/programmatic per-pair coefficients. When `hasOverride[src][dst]` is true, `effectiveGain[src][dst] = overrideGain`; otherwise it falls back to `computedGain` (FR-030). Overrides are enumerated via `forEachOverride()` for state serialization.

**When recomputation fires:**
- `kGlobalSnareBuzzId` / `kGlobalTomResonanceId` parameter change
- Any per-pad body-model, exciter-type, or pitch-envelope-time change (category may shift)
- Per-pad coupling-amount change (Phase 6)
- State load (after all PadConfigs are restored)

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
- VoicePool allocates DrumVoice slots from a fixed 16-slot pool
- At noteOn, PadConfig values are applied to the allocated voice via `applyPadConfigToSlot()`
- Early-out optimization: `process()` returns 0.0f when `!isActive()`

**Key design decisions:**
- Uses direct composition (not IResonator interface) since Phase 1 has only one body model
- Parameter mapping is internal to DrumVoice (Material -> brightness, Size -> f0, etc.)
- `noteOn()` calls `setModes()` (resets filter states); parameter changes call `updateModes()` (preserves states)

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

---

## VoicePool

**Path:** [voice_pool.h](../../plugins/membrum/src/voice_pool/voice_pool.h) / [voice_pool.cpp](../../plugins/membrum/src/voice_pool/voice_pool.cpp) | **Since:** 0.3.0 (Phase 3), extended in 0.4.0 (Phase 4)

Fixed 16-slot polyphonic voice pool driven by `Krate::DSP::VoiceAllocator`. Manages per-pad configuration, voice stealing with click-free fast-release crossfade, choke groups, and multi-bus output routing.

```cpp
namespace Membrum {

class VoicePool {
    void prepare(double sampleRate, int maxBlockSize) noexcept;
    void noteOn(uint8_t midiNote, float velocity) noexcept;
    void noteOff(uint8_t midiNote) noexcept;

    // Legacy mono-output processBlock (Phase 3 compat)
    void processBlock(float* outL, float* outR, int numSamples) noexcept;

    // Multi-bus processBlock (Phase 4)
    void processBlock(float* outL, float* outR,
                      float** auxL, float** auxR,
                      const bool* busActive, int numOutputBuses,
                      int numSamples) noexcept;

    // Per-pad configuration (Phase 4)
    void setPadConfigField(int padIndex, int offset, float normalizedValue) noexcept;
    void setPadConfigSelector(int padIndex, int offset, int discreteValue) noexcept;
    const PadConfig& padConfig(int padIndex) const noexcept;
    PadConfig& padConfigMut(int padIndex) noexcept;
    std::array<PadConfig, kNumPads>& padConfigsArray() noexcept;

    void setMaxPolyphony(int n) noexcept;
    void setVoiceStealingPolicy(VoiceStealingPolicy p) noexcept;
    void setPadChokeGroup(int padIndex, uint8_t group) noexcept;
};

} // namespace Membrum
```

**Key design decisions:**
- 16 main voices + 16 shadow voices for two-slot crossfade fast-release (5 ms exponential decay)
- DrumVoice arrays are heap-allocated via `unique_ptr` (each ~218 KiB, total ~6.84 MiB)
- `padConfigs_[32]` stores all per-pad configurations; `applyPadConfigToSlot()` copies config to voice at noteOn
- Multi-bus processBlock accumulates each voice to main bus (always) plus assigned aux bus (if active)

---

## Kit and Pad Preset Infrastructure

**Path:** [membrum_preset_config.h](../../plugins/membrum/src/preset/membrum_preset_config.h) | **Since:** 0.4.0 (Phase 4)

Two separate `PresetManagerConfig` instances for kit-level and pad-level presets, using the shared `Krate::Plugins::PresetManager` infrastructure.

```cpp
namespace Membrum {

// Kit presets: save/load all 32 pad configurations (9036 bytes)
PresetManagerConfig kitPresetConfig();
// Subcategories: Electronic, Acoustic, Experimental, Cinematic

// Pad presets: save/load a single pad's sound (284 bytes)
PresetManagerConfig padPresetConfig();
// Subcategories: Kick, Snare, Tom, Hat, Cymbal, Perc, Tonal, 808, FX

} // namespace Membrum
```

**Controller preset providers:**
- `kitPresetStateProvider()` -- produces a 9036-byte kit preset blob (v4 format without selectedPadIndex)
- `kitPresetLoadProvider(stream)` -- loads a kit preset blob and syncs all controller params
- `padPresetStateProvider()` -- produces a 284-byte pad preset blob for the currently selected pad
- `padPresetLoadProvider(stream)` -- loads a pad preset blob and applies to the currently selected pad only

---

## State Serialization

### State v4 Binary Format (Phase 4)

```
Offset  Size     Field
------  ----     -----
0       4        int32: version = 4
4       4        int32: maxPolyphony [4, 16]
8       4        int32: voiceStealingPolicy [0, 2]

-- Per-pad block (282 bytes each, 32 pads):
+0      4        int32: exciterType
+4      4        int32: bodyModel
+8      272      34 x float64: sound params (offsets 2-35, normalized)
+280    1        uint8: chokeGroup [0, 8]
+281    1        uint8: outputBus [0, 15]

Pad 0 starts at byte 12
Pad 1 starts at byte 294
...
Pad 31 ends at byte 9036

9036    4        int32: selectedPadIndex [0, 31]

Total: 9040 bytes
```

**Kit preset format:** Same as state v4 but without the final `selectedPadIndex` field (9036 bytes).

**Per-pad preset format:**

```
Offset  Size     Field
------  ----     -----
0       4        int32: version = 1
4       4        int32: exciterType
8       4        int32: bodyModel
12      272      34 x float64: sound params (offsets 2-35)

Total: 284 bytes
```

### Legacy State Migration (v1/v2/v3)

The processor's `setState()` accepts state versions 1-3. Legacy parameters are loaded into pad 0's PadConfig; pads 1-31 retain their DefaultKit defaults. Phase 3 polyphony/stealing/choke data is read from the v3 tail if present.

---

## Multi-Bus Output Routing (Phase 4)

**Processor:**
- `activateBus()` override tracks which output buses the host has activated in `busActive_[16]`
- Bus 0 (main) is always active; buses 1-15 require host activation
- During `process()`, the extended `VoicePool::processBlock()` overload is called with aux bus buffer pointers and the activation state array

**VoicePool:**
- After rendering each voice to a scratch buffer, audio is accumulated to main output (always)
- If the voice's pad has `outputBus > 0` and that bus is active, audio is also accumulated to the corresponding aux bus buffers

**PadConfig:**
- `outputBus` field (offset 31, uint8 [0, 15]) determines the routing
- `outputBus = 0` means main-only (no aux copy)
- Output bus is NOT proxied through a global parameter ID (kit-level concern, not a sound parameter)

---

## Cross-Pad Coupling Signal Chain (Phase 5)

**Since:** 0.5.0 | **Spec:** [140-membrum-phase5-coupling](../140-membrum-phase5-coupling/spec.md)

Sympathetic resonance between pads is rendered by a global post-voice stage in the processor. The audio path sums the stereo voice mix to mono, pushes it through a short delay line, excites a shared `SympatheticResonance` engine with the delayed sample, applies an energy limiter, and mixes the result back into the main bus only.

**Per-sample flow (simplified):**
```
mono    = 0.5f * (mainL[s] + mainR[s])
delayed = couplingDelay_.read()
couplingDelay_.write(mono)
if (!couplingEngine_.isBypassed()) {
    float wet = couplingEngine_.process(delayed);
    wet      = energyLimiter_.process(wet);
    mainL[s] += wet;
    mainR[s] += wet;
}
```

**Components (in the processor):**
- `Krate::DSP::SympatheticResonance couplingEngine_` -- Layer 3 resonator pool; `setAmount(globalCoupling)` drives true zero-cost bypass when `globalCoupling == 0`
- `Krate::DSP::DelayLine couplingDelay_` -- short pre-coupling delay (decorrelates source from resonators, avoids direct feedback)
- `CouplingMatrix couplingMatrix_` -- 32x32 effective-gain table; the voice pool reads `effectiveGainArray()` at note-on to scale each source pad's excitation when seeding coupling voices for the destination pads
- Energy limiter -- soft clip on the coupling sum to keep total output within headroom (FR-019)

**Bypass rule:** When `globalCoupling_ == 0.0f`, `couplingEngine_.setAmount(0)` puts the engine in bypass mode; the per-sample path skips `process()` entirely (`if (!couplingEngine_.isBypassed())`), yielding zero CPU cost. The processor also uses `couplingEngine_.isBypassed()` alongside `voicePool_.isAnyVoiceActive()` to decide whether `process()` can early-out.

**Aux bus policy:** Coupling wet signal is mixed **into the main bus only**. Auxiliary buses 1-15 receive pristine per-pad audio with no coupling contribution -- this preserves stems for external routing/processing (FR-018). Only the host-visible main L/R pair carries the sympathetic resonance.

**Parameter plumbing:**
- `kGlobalCouplingId` -> `globalCoupling_` atomic + `couplingEngine_.setAmount()`
- `kGlobalSnareBuzzId` / `kGlobalTomResonanceId` -> `recomputeCouplingMatrix()` (Tier 1)
- Per-pair overrides (Phase 6) -> `couplingMatrix_.setOverride()` (Tier 2)
- Any pad-config change that shifts `PadCategory` -> `recomputeCouplingMatrix()` so the Tier 1 rules re-map against the new category set

**Voice-pool integration:** `voicePool_.setCouplingEngine(&couplingEngine_)` is wired in `setupProcessing()`. On note-on for pad `src`, the voice pool walks `couplingMatrix_.effectiveGainArray()[src][*]`, probes the source pad's `ModalResonatorBank` via `getModeFrequency(k)` to build a `SympatheticPartialInfo`, and calls `couplingEngine_.noteOn(voiceId, partials)` scaled by the resolved coefficient for each non-zero destination.

---

## Phase 6 Components (Custom UI + Macros + Visualization)

**Since:** 0.6.0 | **Spec:** [141-membrum-phase6-ui](../141-membrum-phase6-ui/spec.md)

Phase 6 introduces a full custom VSTGUI editor with five per-pad macros, a 4x8 pad grid, a 32x32 coupling matrix editor, and promoted pitch-envelope control. Three new processor/DSP components provide the glue between the audio thread and the UI.

### MacroMapper
**Path:** [processor/macro_mapper.h](../../plugins/membrum/src/processor/macro_mapper.h) | **Contract:** [macro_mapper.h](../141-membrum-phase6-ui/contracts/macro_mapper.h) | **Since:** 0.6.0 (Phase 6)

Control-rate forward driver that maps the five per-pad macros (Tightness, Brightness, Body Size, Punch, Complexity) onto documented target parameters using delta-from-registered-default arithmetic. Plugin-local to Membrum.

```cpp
namespace Membrum {

struct RegisteredDefaultsTable {
    std::array<float, 64> byOffset{};   // indexed by PadParamOffset
};

class MacroMapper {
    void prepare(const RegisteredDefaultsTable& defaults) noexcept;   // UI thread, once
    void apply(int padIndex, PadConfig& padConfig) noexcept;          // audio thread
    void reapplyAll(std::array<PadConfig, 32>& pads) noexcept;        // kit-preset load
    [[nodiscard]] bool isDirty(int padIndex) const noexcept;
    void invalidateCache() noexcept;
};

} // namespace Membrum
```

**Delta semantics:** For each target `T` of macro `M`, `effective(T) = clamp(registeredDefault(T) + delta_M(macro), 0, 1)`. Macro value `0.5` yields delta 0 (target sits at its registered default); `0.0` and `1.0` define negative/positive excursions. Per-macro span constants live in `namespace MacroCurves` (e.g., `kTightnessMaterialSpan = 0.30f`, `kBrightnessCutoffSpan = 0.40f` in log-Hz, `kPunchPitchEnvDepthSpan = 0.50f`, etc.).

**Threading:** `prepare()` runs once on the component thread during `Processor::initialize()`. `apply()` / `reapplyAll()` are audio-thread only, called from `processParameterChanges()`. No allocations, locks, or exceptions. A 32-entry `PadCache` early-outs when cached macros equal live values.

**When to use:**
- Processor calls `apply(padIndex, padConfig)` whenever a macro parameter for that pad changes
- Kit preset load triggers `reapplyAll()` to recompute all underlying parameters from restored macro state

### PadGlowPublisher
**Path:** [dsp/pad_glow_publisher.h](../../plugins/membrum/src/dsp/pad_glow_publisher.h) | **Contract:** [pad_glow_publisher.h](../141-membrum-phase6-ui/contracts/pad_glow_publisher.h) | **Since:** 0.6.0 (Phase 6)

Lock-free pre-allocated 1024-bit (32 x `std::atomic<uint32_t>`) amplitude publisher. Drives pad-glow intensity in `PadGridView` from the audio thread without locks. Header-only.

```cpp
namespace Membrum {

class PadGlowPublisher {
    static constexpr int kNumPads = 32;
    static constexpr int kAmplitudeBuckets = 32;     // 5 bits per pad

    void publish(int padIndex, float amplitude) noexcept;              // audio thread
    void snapshot(std::array<std::uint8_t, 32>& out) const noexcept;   // UI thread
    [[nodiscard]] std::uint8_t readPadBucket(int padIndex) const noexcept;
    void reset() noexcept;
};

} // namespace Membrum
```

**Encoding:** Each pad occupies one `std::atomic<uint32_t>` word. Amplitude is quantised to 5 bits (32 buckets) and encoded one-hot — the UI recovers the bucket via `31 - std::countl_zero(word)`. `static_assert(std::atomic<std::uint32_t>::is_always_lock_free)` at construction.

**Threading:** Audio-thread `publish()` uses `memory_order_relaxed`; UI-thread `snapshot()` / `readPadBucket()` use `memory_order_acquire`. Worst-case visual error is one UI frame (~33 ms) of lag, well inside the SC-005 50 ms budget. Cache-line aligned (`alignas(64)`) to avoid false sharing.

**When to use:**
- Voice pool calls `publish(padIndex, voiceAmplitude)` at most once per pad per audio block
- `PadGridView` timer callback at <=30 Hz calls `snapshot()` to drive glow rendering

### MatrixActivityPublisher
**Path:** [dsp/matrix_activity_publisher.h](../../plugins/membrum/src/dsp/matrix_activity_publisher.h) | **Contract:** [matrix_activity_publisher.h](../141-membrum-phase6-ui/contracts/matrix_activity_publisher.h) | **Since:** 0.6.0 (Phase 6)

Same lock-free pattern as `PadGlowPublisher`, but each of the 32 source words stores a 32-bit destination bitmask. Bit `D` of word `S` means "source `S` is currently driving coupling energy above threshold into destination `D`". Consumed by `CouplingMatrixView` to outline active cells during playback.

```cpp
namespace Membrum {

class MatrixActivityPublisher {
    void publishSourceActivity(int src, std::uint32_t dstMask) noexcept;       // audio
    [[nodiscard]] std::uint32_t readSourceActivity(int src) const noexcept;    // UI
    void snapshot(std::array<std::uint32_t, 32>& out) const noexcept;          // UI
    void reset() noexcept;
};

} // namespace Membrum
```

**Threading:** Identical to `PadGlowPublisher` — audio thread uses `relaxed`, UI thread uses `acquire`; cache-line aligned storage; always lock-free.

**When to use:**
- Coupling post-voice stage calls `publishSourceActivity(src, mask)` per active source per block
- `CouplingMatrixView` timer callback calls `snapshot()` or `readSourceActivity()` per frame
