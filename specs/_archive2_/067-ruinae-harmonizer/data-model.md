# Data Model: Ruinae Harmonizer Integration

**Feature**: 067-ruinae-harmonizer
**Date**: 2026-02-19

## Entities

### E-001: RuinaeHarmonizerParams

**Location**: `plugins/ruinae/src/parameters/harmonizer_params.h`
**Pattern**: Follows `RuinaePhaserParams` in `phaser_params.h`

```cpp
struct RuinaeHarmonizerParams {
    // Global parameters
    std::atomic<int>   harmonyMode{0};       // 0=Chromatic, 1=Scalic
    std::atomic<int>   key{0};               // 0=C, 1=C#, ..., 11=B
    std::atomic<int>   scale{0};             // ScaleType enum (0-8)
    std::atomic<int>   pitchShiftMode{0};    // PitchMode enum (0-3)
    std::atomic<bool>  formantPreserve{false};
    std::atomic<int>   numVoices{0};         // 0-4 (default 0 = silent)
    std::atomic<float> dryLevelDb{0.0f};     // -60 to +6 dB (default 0 dB)
    std::atomic<float> wetLevelDb{-6.0f};    // -60 to +6 dB (default -6 dB)

    // Per-voice parameters (4 voices)
    std::atomic<int>   voiceInterval[4]{{0}, {0}, {0}, {0}};       // -24 to +24 steps
    std::atomic<float> voiceLevelDb[4]{{0.0f}, {0.0f}, {0.0f}, {0.0f}};  // -60 to +6 dB
    std::atomic<float> voicePan[4]{{0.0f}, {0.0f}, {0.0f}, {0.0f}};      // -1 to +1
    std::atomic<float> voiceDelayMs[4]{{0.0f}, {0.0f}, {0.0f}, {0.0f}};  // 0 to 50 ms
    std::atomic<float> voiceDetuneCents[4]{{0.0f}, {0.0f}, {0.0f}, {0.0f}}; // -50 to +50 cents
};
```

**Validation rules**:
- harmonyMode: clamped to [0, 1]
- key: clamped to [0, 11]
- scale: clamped to [0, 8] (kNumScaleTypes - 1)
- pitchShiftMode: clamped to [0, 3]
- numVoices: clamped to [0, 4]
- dryLevelDb/wetLevelDb: clamped to [-60.0, +6.0]
- voiceInterval: clamped to [-24, +24]
- voiceLevelDb: clamped to [-60.0, +6.0]
- voicePan: clamped to [-1.0, +1.0]
- voiceDelayMs: clamped to [0.0, 50.0]
- voiceDetuneCents: clamped to [-50.0, +50.0]

---

### E-002: Parameter ID Map

**Location**: `plugins/ruinae/src/plugin_ids.h`

| Parameter | ID | Type | Normalized Default | Plain Default | Unit |
|---|---|---|---|---|---|
| Harmonizer Enabled | 1503 | Toggle | 0.0 | off | -- |
| Harmony Mode | 2800 | Dropdown (2) | 0.0 | Chromatic | -- |
| Key | 2801 | Dropdown (12) | 0.0 | C | -- |
| Scale | 2802 | Dropdown (9) | 0.0 | Major | -- |
| Pitch Shift Mode | 2803 | Dropdown (4) | 0.0 | Simple | -- |
| Formant Preserve | 2804 | Toggle | 0.0 | off | -- |
| Num Voices | 2805 | Dropdown (5) | 0.0 | 0 | -- |
| Dry Level | 2806 | Continuous | 0.909 | 0 | dB |
| Wet Level | 2807 | Continuous | 0.818 | -6 | dB |
| Voice 1 Interval | 2810 | Discrete (49) | 0.5 | 0 | steps |
| Voice 1 Level | 2811 | Continuous | 0.909 | 0 | dB |
| Voice 1 Pan | 2812 | Continuous | 0.5 | 0 | -- |
| Voice 1 Delay | 2813 | Continuous | 0.0 | 0 | ms |
| Voice 1 Detune | 2814 | Continuous | 0.5 | 0 | cents |
| Voice 2 Interval | 2820 | Discrete (49) | 0.5 | 0 | steps |
| Voice 2 Level | 2821 | Continuous | 0.909 | 0 | dB |
| Voice 2 Pan | 2822 | Continuous | 0.5 | 0 | -- |
| Voice 2 Delay | 2823 | Continuous | 0.0 | 0 | ms |
| Voice 2 Detune | 2824 | Continuous | 0.5 | 0 | cents |
| Voice 3 Interval | 2830 | Discrete (49) | 0.5 | 0 | steps |
| Voice 3 Level | 2831 | Continuous | 0.909 | 0 | dB |
| Voice 3 Pan | 2832 | Continuous | 0.5 | 0 | -- |
| Voice 3 Delay | 2833 | Continuous | 0.0 | 0 | ms |
| Voice 3 Detune | 2834 | Continuous | 0.5 | 0 | cents |
| Voice 4 Interval | 2840 | Discrete (49) | 0.5 | 0 | steps |
| Voice 4 Level | 2841 | Continuous | 0.909 | 0 | dB |
| Voice 4 Pan | 2842 | Continuous | 0.5 | 0 | -- |
| Voice 4 Delay | 2843 | Continuous | 0.0 | 0 | ms |
| Voice 4 Detune | 2844 | Continuous | 0.5 | 0 | cents |

---

### E-003: Normalization Formulas

All parameters at VST boundary are normalized 0.0 to 1.0. Conversion formulas:

| Parameter | Norm -> Plain | Plain -> Norm |
|---|---|---|
| Dropdown (N entries) | `round(norm * (N-1))` | `plain / (N-1)` |
| Toggle | `norm >= 0.5 ? true : false` | `plain ? 1.0 : 0.0` |
| dB Level [-60, +6] | `norm * 66.0 - 60.0` | `(plain + 60.0) / 66.0` |
| Interval [-24, +24] | `round(norm * 48) - 24` | `(plain + 24) / 48.0` |
| Pan [-1, +1] | `norm * 2.0 - 1.0` | `(plain + 1.0) / 2.0` |
| Delay [0, 50] ms | `norm * 50.0` | `plain / 50.0` |
| Detune [-50, +50] cents | `norm * 100.0 - 50.0` | `(plain + 50.0) / 100.0` |

---

### E-004: State Serialization (v16)

Added after v15 mod source params:

```
[int32]  harmonyMode       (std::atomic<int>: writeInt32/readInt32)
[int32]  key               (std::atomic<int>: writeInt32/readInt32)
[int32]  scale             (std::atomic<int>: writeInt32/readInt32)
[int32]  pitchShiftMode    (std::atomic<int>: writeInt32/readInt32)
[int32]  formantPreserve   (std::atomic<bool> stored as int32: 0 or 1)
[int32]  numVoices         (std::atomic<int>: writeInt32/readInt32)
[float]  dryLevelDb        (std::atomic<float>: writeFloat/readFloat)
[float]  wetLevelDb        (std::atomic<float>: writeFloat/readFloat)
[int32]  voice1Interval    (std::atomic<int>: writeInt32/readInt32)
[float]  voice1LevelDb
[float]  voice1Pan
[float]  voice1DelayMs
[float]  voice1DetuneCents
[int32]  voice2Interval
[float]  voice2LevelDb
[float]  voice2Pan
[float]  voice2DelayMs
[float]  voice2DetuneCents
[int32]  voice3Interval
[float]  voice3LevelDb
[float]  voice3Pan
[float]  voice3DelayMs
[float]  voice3DetuneCents
[int32]  voice4Interval
[float]  voice4LevelDb
[float]  voice4Pan
[float]  voice4DelayMs
[float]  voice4DetuneCents
[int8]   harmonizerEnabled (standalone enable flag: writeInt8/readInt8)
```

**Type rule**: `std::atomic<int>` and `std::atomic<bool>` fields use `writeInt32`/`readInt32`; `std::atomic<float>` fields use `writeFloat`/`readFloat`. This matches the codebase-wide pattern established in `phaser_params.h` and `delay_params.h`. The `harmonizerEnabled` flag uses `writeInt8`/`readInt8` to match the existing FX enable serialization pattern.

Total: 6 x int32 (global ints) + 2 x float (global floats) + 4 x (int32 + 4 x float) (per-voice) + 1 x int8 (enable) = 6×4 + 2×4 + 4×(4 + 4×4) + 1 = 24 + 8 + 80 + 1 = **113 bytes** added to state.

---

### E-005: Signal Flow Diagram

```
Voice Sum (stereo)
    |
    v
[Phaser] (stereo in-place, if enabled)
    |
    v
[Delay] (5 types, crossfade, compensation)
    |
    v
[Harmonizer] (if enabled)
    |  stereo-to-mono: (L+R)*0.5
    |  -> HarmonizerEngine::process(mono_in, L_out, R_out)
    |  <- stereo output replaces signal
    v
[Reverb] (stereo in-place, if enabled)
    |
    v
Output (stereo)
```

---

### E-006: Latency Model

```
targetLatencySamples_ = spectralDelay.getLatencySamples()
                      + harmonizer(PhaseVocoder).getLatencySamples()

At 44.1kHz, default FFT sizes:
  Spectral delay:  4096 samples
  Harmonizer PV:   4096 + 1024 = 5120 samples
  Combined:        9216 samples (~209 ms)
```

This value is computed once in `prepare()` and held constant for the session.
