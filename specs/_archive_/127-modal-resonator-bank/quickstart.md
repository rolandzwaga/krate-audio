# Quickstart: Modal Resonator Bank (Spec 127)

**Branch**: `127-modal-resonator-bank`
**Plugin**: Innexus

## What This Feature Does

Adds a modal resonator bank that transforms the analyzed residual signal into physically resonant textures. The residual noise is passed through a bank of tuned resonators derived from the analyzed harmonic content, producing ringing metallic/wooden/glass-like textures.

## Architecture at a Glance

```
Per Voice (inside sample loop):
  oscillatorBank.processStereo() -> harmonic signal (unchanged)
  residualSynth.process()        -> residual signal
                                      |
                                      v
                                 transient emphasis
                                      |
                                      v
                              modalResonator.processSample(excitation)
                                      |
                                      v
                                   softClip
                                      |
                                      v
                              PhysicalModelMixer::process(
                                  harmonic, residual, physical, mix)
                                      |
                                      v
                                  voice output
```

## New Files

| File | Layer | Purpose |
|------|-------|---------|
| `dsp/include/krate/dsp/processors/modal_resonator_bank.h` | L2 | Core resonator bank (header-only) |
| `dsp/include/krate/dsp/processors/modal_resonator_bank_simd.cpp` | L2 | SIMD kernel (Phase 2, optional) |
| `plugins/innexus/src/dsp/physical_model_mixer.h` | Plugin | Stateless mix utility |
| `dsp/tests/unit/processors/test_modal_resonator_bank.cpp` | Test | Unit tests |
| `plugins/innexus/tests/unit/processor/test_physical_model.cpp` | Test | Integration tests |

## Modified Files

| File | Change |
|------|--------|
| `plugins/innexus/src/plugin_ids.h` | Add 5 parameter IDs (800-804) |
| `plugins/innexus/src/processor/innexus_voice.h` | Add `modalResonator` field |
| `plugins/innexus/src/processor/processor.h` | Add atomic fields + smoothers for 5 params |
| `plugins/innexus/src/processor/processor.cpp` | Voice render loop: add modal processing |
| `plugins/innexus/src/processor/processor_params.cpp` | Handle new param changes |
| `plugins/innexus/src/processor/processor_state.cpp` | Save/load 5 new params |
| `plugins/innexus/src/controller/controller.cpp` | Register 5 new params |
| `dsp/CMakeLists.txt` | Add SIMD source (Phase 2 only) |
| `plugins/innexus/tests/CMakeLists.txt` | Add test files |
| `dsp/tests/CMakeLists.txt` | Add test file |

## Key Constants

```cpp
// Inside ModalResonatorBank (voicing parameters):
static constexpr float kTransientEmphasisGain = 4.0f;  // excitation transient boost
static constexpr float kMaxB3 = 4.0e-5f;               // Chaigne-Lambourg max damping
static constexpr float kSilenceThreshold = 1e-12f;      // denormal protection
```

## Parameter IDs

```cpp
kPhysModelMixId       = 800  // 0.0-1.0, default 0.0
kResonanceDecayId     = 801  // 0.01-5.0s, log, default 0.5
kResonanceBrightnessId = 802  // 0.0-1.0, default 0.5
kResonanceStretchId   = 803  // 0.0-1.0, default 0.0
kResonanceScatterId   = 804  // 0.0-1.0, default 0.0
```

## Core Algorithm (Coupled-Form Resonator)

Per mode, per sample:
```cpp
float s = sinState_[k];
float c = cosState_[k];
float eps = epsilon_[k];      // 2 * sin(pi * f / sampleRate)
float R = radius_[k];          // exp(-decayRate / sampleRate)
float gain = inputGain_[k];    // amplitude * (1 - R)

float s_new = R * (s + eps * c) + gain * excitation;
float c_new = R * (c - eps * s_new);

sinState_[k] = s_new;
cosState_[k] = c_new;
output += s_new;  // sum all modes
```

## Damping Model (Chaigne-Lambourg)

```cpp
float b1 = 1.0f / decayTime;                        // base decay rate
float b3 = (1.0f - brightness) * kMaxB3;            // HF damping coefficient
float decayRate_k = b1 + b3 * warped_freq_k * warped_freq_k;
float R_k = std::exp(-decayRate_k / sampleRate);
```

## Inharmonic Warping

```cpp
// Stretch (stiff-string model)
float B = stretch * stretch * 0.001f;
float f_warped = f_k * std::sqrt(1.0f + B * k * k);

// Scatter (deterministic sinusoidal displacement)
constexpr float D = pi * (std::sqrt(5.0f) - 1.0f) / 2.0f;  // golden ratio * pi
float C = scatter * 0.02f;
f_warped *= (1.0f + C * std::sin(k * D));
```

## Build & Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Build
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests
"$CMAKE" --build build/windows-x64-release --config Release --target innexus_tests

# Test
build/windows-x64-release/bin/Release/dsp_tests.exe "ModalResonatorBank*"
build/windows-x64-release/bin/Release/innexus_tests.exe "PhysicalModel*"

# Pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"
```

## Backwards Compatibility

- Physical Model Mix defaults to 0.0 (disabled)
- At mix=0: `output = harmonic + residual` (bit-exact current behavior)
- Old presets that lack the new parameters will use defaults
- No changes to existing signal path when mix=0
