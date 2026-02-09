# Data Model: Ruinae Engine Composition

**Feature Branch**: `044-engine-composition`
**Date**: 2026-02-09

## Entities

### E-001: RuinaeEngine

**Location**: `dsp/include/krate/dsp/systems/ruinae_engine.h`
**Layer**: 3 (Systems)
**Namespace**: `Krate::DSP`

**Description**: Top-level DSP system composing all Ruinae synthesizer components.

**Constants**:
| Name | Type | Value | Description |
|------|------|-------|-------------|
| `kMaxPolyphony` | `size_t` | 16 | Maximum simultaneous voices |
| `kMinMasterGain` | `float` | 0.0f | Minimum master gain |
| `kMaxMasterGain` | `float` | 2.0f | Maximum master gain |

**Composed Sub-Components (Member Variables)**:
| Member | Type | Count | Description |
|--------|------|-------|-------------|
| `voices_` | `std::array<RuinaeVoice, kMaxPolyphony>` | 16 | Pre-allocated voice pool |
| `allocator_` | `VoiceAllocator` | 1 | Polyphonic voice management |
| `monoHandler_` | `MonoHandler` | 1 | Monophonic mode with legato/portamento |
| `noteProcessor_` | `NoteProcessor` | 1 | Pitch bend smoothing, velocity curves |
| `globalModEngine_` | `ModulationEngine` | 1 | Global modulation (LFOs, Chaos, Rungler, Macros) |
| `globalFilterL_` | `SVF` | 1 | Global stereo filter (left channel) |
| `globalFilterR_` | `SVF` | 1 | Global stereo filter (right channel) |
| `effectsChain_` | `RuinaeEffectsChain` | 1 | Stereo effects chain (freeze, delay, reverb) |

**Scratch Buffers (Pre-allocated at prepare())**:
| Member | Type | Description |
|--------|------|-------------|
| `voiceScratchBuffer_` | `std::vector<float>` | Per-voice mono scratch (maxBlockSize) |
| `mixBufferL_` | `std::vector<float>` | Stereo mix accumulator (left) |
| `mixBufferR_` | `std::vector<float>` | Stereo mix accumulator (right) |
| `previousOutputL_` | `std::vector<float>` | Previous block output for modulation audio input (left) |
| `previousOutputR_` | `std::vector<float>` | Previous block output for modulation audio input (right) |

**State Variables**:
| Member | Type | Default | Description |
|--------|------|---------|-------------|
| `mode_` | `VoiceMode` | `VoiceMode::Poly` | Current voice mode |
| `polyphonyCount_` | `size_t` | 8 | Active polyphony count |
| `masterGain_` | `float` | 1.0f | Master gain [0, 2] |
| `gainCompensation_` | `float` | 1/sqrt(8) | Auto-calculated: 1/sqrt(polyphonyCount) |
| `softLimitEnabled_` | `bool` | true | Soft limiter toggle |
| `globalFilterEnabled_` | `bool` | false | Global filter toggle |
| `stereoSpread_` | `float` | 0.0f | Voice stereo spread [0, 1] |
| `stereoWidth_` | `float` | 1.0f | Stereo width [0, 2] |
| `sampleRate_` | `double` | 0.0 | Current sample rate |
| `prepared_` | `bool` | false | Whether prepare() has been called |
| `timestampCounter_` | `uint64_t` | 0 | Monotonic noteOn timestamp counter |
| `noteOnTimestamps_` | `std::array<uint64_t, kMaxPolyphony>` | all 0 | Per-voice noteOn timestamps |
| `voicePanPositions_` | `std::array<float, kMaxPolyphony>` | all 0.5f | Pre-computed pan positions per voice |
| `monoVoiceNote_` | `int8_t` | -1 | MIDI note of current mono voice |
| `blockContext_` | `BlockContext` | defaults | Current block context for tempo |
| `globalFilterCutoffHz_` | `float` | 1000.0f | Base global filter cutoff (before modulation) |
| `globalFilterResonance_` | `float` | 0.707f | Base global filter resonance (before modulation) |

**Lifecycle Methods**:
| Method | Real-Time Safe | Description |
|--------|---------------|-------------|
| `prepare(double sampleRate, size_t maxBlockSize)` | No | Initialize all components, allocate buffers |
| `reset()` | Yes | Clear all state, produce silence |

**Note Dispatch Methods**:
| Method | Mode | Description |
|--------|------|-------------|
| `noteOn(uint8_t note, uint8_t velocity)` | Both | Route through VoiceAllocator (poly) or MonoHandler (mono) |
| `noteOff(uint8_t note)` | Both | Route release through appropriate handler |

**Configuration Methods** (all noexcept, real-time safe):
| Category | Methods |
|----------|---------|
| Polyphony | `setPolyphony(size_t)`, `setMode(VoiceMode)` |
| Stereo | `setStereoSpread(float)`, `setStereoWidth(float)` |
| Global Filter | `setGlobalFilterEnabled(bool)`, `setGlobalFilterCutoff(float)`, `setGlobalFilterResonance(float)`, `setGlobalFilterType(SVFMode)` |
| Global Modulation | `setGlobalModRoute(int, ModSource, RuinaeModDest, float)`, `clearGlobalModRoute(int)` |
| Global Mod Sources | `setGlobalLFO1Rate(float)`, `setGlobalLFO1Waveform(Waveform)`, `setGlobalLFO2Rate(float)`, `setGlobalLFO2Waveform(Waveform)`, `setChaosSpeed(float)`, `setMacroValue(size_t, float)` |
| Performance Controllers | `setPitchBend(float)`, `setAftertouch(float)`, `setModWheel(float)` |
| Master Output | `setMasterGain(float)`, `setSoftLimitEnabled(bool)` |
| Effects Chain | `setDelayType(RuinaeDelayType)`, `setDelayTime(float)`, `setDelayFeedback(float)`, `setDelayMix(float)`, `setReverbParams(ReverbParams)`, `setFreezeEnabled(bool)`, `setFreeze(bool)`, etc. |
| Voice Parameters | All RuinaeVoice setters forwarded to all 16 voices |
| Mono Config | `setMonoPriority(MonoMode)`, `setLegato(bool)`, `setPortamentoTime(float)`, `setPortamentoMode(PortaMode)` |
| VoiceAllocator Config | `setAllocationMode(AllocationMode)`, `setStealMode(StealMode)` |
| NoteProcessor Config | `setPitchBendRange(float)`, `setTuningReference(float)`, `setVelocityCurve(VelocityCurve)` |
| Tempo | `setTempo(double)`, `setBlockContext(BlockContext)` |

**Query Methods** (all const noexcept [[nodiscard]]):
| Method | Return | Description |
|--------|--------|-------------|
| `getActiveVoiceCount()` | `uint32_t` | Number of currently active voices |
| `getMode()` | `VoiceMode` | Current voice mode |
| `getLatencySamples()` | `size_t` | Total processing latency from effects |

**Processing Method**:
| Method | Signature | Description |
|--------|-----------|-------------|
| `processBlock` | `void processBlock(float* left, float* right, size_t numSamples) noexcept` | Process one stereo block |

### E-002: RuinaeModDest

**Location**: `dsp/include/krate/dsp/systems/ruinae_engine.h` (inline enum)
**Layer**: 3 (Systems)
**Namespace**: `Krate::DSP`

**Description**: Internal enum for global modulation destinations within the RuinaeEngine.

| Value | uint32_t | Description |
|-------|----------|-------------|
| GlobalFilterCutoff | 64 | Global filter cutoff frequency offset |
| GlobalFilterResonance | 65 | Global filter resonance offset |
| MasterVolume | 66 | Master volume offset |
| EffectMix | 67 | Effect chain mix offset (currently: delay mix) |
| AllVoiceFilterCutoff | 68 | Offset forwarded to all voices' filter cutoff |
| AllVoiceMorphPosition | 69 | Offset forwarded to all voices' morph position |
| AllVoiceTranceGateRate | 70 | Offset forwarded to all voices' trance gate rate |

## Relationships

```
RuinaeEngine (Layer 3)
    |-- owns 16x RuinaeVoice (Layer 3)
    |       |-- owns 2x SelectableOscillator (Layer 3)
    |       |-- owns SVF, LadderFilter, FormantFilter, FeedbackComb (Layer 1/2)
    |       |-- owns ChaosWaveshaper, SpectralDistortion, etc. (Layer 1/2)
    |       |-- owns TranceGate (Layer 2)
    |       |-- owns 3x ADSREnvelope (Layer 1)
    |       |-- owns LFO (Layer 1)
    |       |-- owns VoiceModRouter (Layer 3)
    |
    |-- owns 1x VoiceAllocator (Layer 3)
    |-- owns 1x MonoHandler (Layer 2)
    |-- owns 1x NoteProcessor (Layer 2)
    |-- owns 1x ModulationEngine (Layer 3)
    |       |-- owns 2x LFO (Layer 1)
    |       |-- owns EnvelopeFollower, Random, Chaos, S&H, Pitch, Transient (Layer 2)
    |
    |-- owns 2x SVF (Layer 1) -- global stereo filter
    |-- owns 1x RuinaeEffectsChain (Layer 3)
    |       |-- owns FreezeMode (Layer 4)
    |       |-- owns 5x Delay types (Layer 4)
    |       |-- owns Reverb (Layer 4)
    |
    |-- uses Sigmoid::tanh() (Layer 0) -- soft limiter
    |-- uses detail::isNaN/isInf (Layer 0) -- parameter validation
```

## State Transitions

### Voice Lifecycle
```
                noteOn()
    Idle ────────────────► Active
      ▲                      │
      │  voiceFinished()     │ noteOff()
      │  (deferred)          │
      │                      ▼
    Idle ◄──────────────  Releasing
```

### Engine Mode
```
    Poly ◄────────────► Mono
         setMode(Mono)     setMode(Poly)

    Poly->Mono: Most recent voice survives at index 0
    Mono->Poly: Voice 0 continues, MonoHandler reset
```

## Processing Flow (per block)

```
1.  Clear mixBufferL_, mixBufferR_
2.  Build BlockContext, set tempo on modulation sources and effects chain
3.  Process globalModEngine_ with previousOutputL_/R_ as audio input
4.  Read global modulation offsets for engine-level params
5.  Apply global modulation to globalFilterCutoff, masterVolume, etc.
6.  Process pitch bend smoother (noteProcessor_.processPitchBend())
7.  For each active voice [i]:
    a.  Poly: setFrequency(noteProcessor_.getFrequency(voiceNote))
    b.  Mono: per-sample portamento -> setFrequency()
    c.  Forward "AllVoice" modulation offsets
    d.  processBlock into voiceScratchBuffer_ (mono)
    e.  Pan and sum: mixBufferL_[s] += sample * leftGain[i]
                     mixBufferR_[s] += sample * rightGain[i]
8.  Apply stereo width (Mid/Side) to mixBufferL_, mixBufferR_
9.  Apply global filter (if enabled) to mixBufferL_, mixBufferR_
10. Process effects chain in-place on mixBufferL_, mixBufferR_
11. Apply master gain * gainCompensation to both channels
12. Apply soft limiter (if enabled) per sample
13. Flush NaN/Inf to 0.0
14. Write to output left[], right[]
15. Copy output to previousOutputL_, previousOutputR_
16. Deferred voiceFinished() notifications
```
