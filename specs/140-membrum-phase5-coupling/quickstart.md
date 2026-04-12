# Quickstart: Membrum Phase 5 -- Cross-Pad Coupling

## What this phase adds

Phase 5 adds **sympathetic resonance between pads** in the Membrum drum synth. When one drum is struck, other drums in the kit resonate sympathetically -- the kick makes the snare wires buzz, toms resonate when neighboring toms are hit. This is the acoustic behavior that makes a physical drum kit "breathe."

## Architecture overview

```
                    +-----------+
  MIDI note-on ---->| VoicePool |--- noteOn(voiceId, partials) --->+
                    | 16 voices |                                   |
                    | 32 pads   |--- noteOff(voiceId) ------------>|
                    +-----+-----+                                   |
                          |                                         v
                    outL, outR                            SympatheticResonance
                          |                               (64 resonators, SIMD)
                          v                                         |
                    mono = (L+R)/2                                  |
                          |                                         |
                    DelayLine (0.5-2ms)                              |
                          |                                         |
                          +-----> coupling = engine.process() <-----+
                                        |
                                  energy limiter
                                        |
                                  outL += coupling
                                  outR += coupling
```

## Key files to modify

| File | Change |
|------|--------|
| `dsp/include/krate/dsp/processors/modal_resonator_bank.h` | Add `getModeFrequency()` and `getNumModes()` accessors |
| `plugins/membrum/src/dsp/pad_config.h` | Add `kPadCouplingAmount = 36`, `couplingAmount` field |
| `plugins/membrum/src/dsp/pad_category.h` | **New file**: `PadCategory` enum + `classifyPad()` |
| `plugins/membrum/src/dsp/coupling_matrix.h` | **New file**: Two-layer coupling coefficient resolver |
| `plugins/membrum/src/plugin_ids.h` | Add parameter IDs 270-273, update version to 5 |
| `plugins/membrum/src/processor/processor.h` | Add coupling engine, delay, matrix, atomics |
| `plugins/membrum/src/processor/processor.cpp` | Signal chain integration, state v5, parameter handling |
| `plugins/membrum/src/voice_pool/voice_pool.h` | Add coupling engine pointer, noteOn/noteOff hooks |
| `plugins/membrum/src/voice_pool/voice_pool.cpp` | Implement coupling hooks |
| `plugins/membrum/src/dsp/drum_voice.h` | Add `getPartialInfo()` method |
| `plugins/membrum/src/controller/controller.cpp` | Register Phase 5 parameters |

## Key new files

| File | Purpose |
|------|---------|
| `plugins/membrum/src/dsp/pad_category.h` | Pad classification (Kick/Snare/Tom/HatCymbal/Perc) |
| `plugins/membrum/src/dsp/coupling_matrix.h` | 32x32 coupling coefficient two-layer resolver |

## Test files

| File | Coverage |
|------|----------|
| `plugins/membrum/tests/unit/dsp/test_pad_category.cpp` | Category classification rules |
| `plugins/membrum/tests/unit/dsp/test_coupling_matrix.cpp` | Matrix resolver, Tier 1/2 logic |
| `plugins/membrum/tests/unit/processor/test_coupling_integration.cpp` | Signal chain, noteOn/noteOff hooks |
| `plugins/membrum/tests/unit/processor/test_coupling_state.cpp` | State v5 round-trip, v4 migration |
| `plugins/membrum/tests/unit/processor/test_coupling_energy.cpp` | Energy limiter, bypass behavior |
| `dsp/tests/unit/processors/test_modal_bank_frequency.cpp` | getModeFrequency accessor |

## Build and test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Build
"$CMAKE" --build build/windows-x64-release --config Release --target membrum_tests

# Run tests
build/windows-x64-release/bin/Release/membrum_tests.exe

# Run specific coupling tests
build/windows-x64-release/bin/Release/membrum_tests.exe "[coupling]"

# Pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3"
```

## Parameter ID reference

| ID | Name | Range | Default |
|----|------|-------|---------|
| 270 | Global Coupling | 0.0 - 1.0 | 0.0 |
| 271 | Snare Buzz | 0.0 - 1.0 | 0.0 |
| 272 | Tom Resonance | 0.0 - 1.0 | 0.0 |
| 273 | Coupling Delay | 0.5 - 2.0 ms | 1.0 ms |
| padId + 36 | Per-Pad Coupling Amount | 0.0 - 1.0 | 0.5 |

## Effective gain formula (FR-014)

```
perSampleGain = globalCoupling
              * effectiveGain[src][dst]     // from CouplingMatrix resolver
              * padCouplingAmount[src]
              * padCouplingAmount[dst]
```

Where `effectiveGain` is resolved from the two-layer matrix:
- Tier 1 (computed): Kick->Snare uses `snareBuzz * 0.05`, Tom->Tom uses `tomResonance * 0.05`
- Tier 2 (override): Direct per-pair coefficient set programmatically
