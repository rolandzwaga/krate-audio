# Data Model: Ruinae Plugin Shell

**Feature**: 045-plugin-shell
**Date**: 2026-02-09

## Entity Overview

The Ruinae plugin shell's data model is composed of 19 parameter pack structs (one per synthesizer section), plus the state versioning metadata. All parameter packs follow the same Iterum-established pattern.

## E-001: State Version Metadata

```
StateVersion
  stateVersion: int32 (monotonically increasing, starts at 1)
  -- First field in IBStreamer state, read before any parameter data
  -- Current version: kCurrentStateVersion = 1
```

## E-002: Global Parameters (ID 0-99)

```
GlobalParams
  masterGain: atomic<float> = 1.0f          // 0.0-2.0, ID 0
  voiceMode: atomic<int> = 0                // 0=Poly, 1=Mono, ID 1
  polyphony: atomic<int> = 8               // 1-16, ID 2
  softLimit: atomic<bool> = true           // on/off, ID 3
```

**Validation**: masterGain clamped to [0.0, 2.0]; polyphony clamped to [1, 16].
**Engine mapping**: setMasterGain(float), setMode(VoiceMode), setPolyphony(size_t), setSoftLimitEnabled(bool).

## E-003: OSC A Parameters (ID 100-199)

```
OscAParams
  type: atomic<int> = 0                     // OscType enum (0-9), default=PolyBLEP (index 0), ID 100
  tuneSemitones: atomic<float> = 0.0f       // -24 to +24 st, ID 101
  fineCents: atomic<float> = 0.0f           // -100 to +100 ct, ID 102
  level: atomic<float> = 1.0f              // 0.0-1.0, ID 103
  phase: atomic<int> = 0                    // PhaseMode (0=Reset, 1=Continuous), ID 104
```

**Note (A12)**: OscType index 0 = PolyBLEP (verified from ruinae_types.h), which is the default "saw" oscillator.

**Engine mapping**: setOscAType(OscType), setOscAPhaseMode(PhaseMode), setOscATuneSemitones(float), setOscAFineCents(float), setOscALevel(float). All setters now present per A1.

## E-004: OSC B Parameters (ID 200-299)

```
OscBParams
  type: atomic<int> = 0                     // OscType enum (0-9), default=PolyBLEP (index 0), ID 200
  tuneSemitones: atomic<float> = 0.0f       // -24 to +24 st, ID 201
  fineCents: atomic<float> = 0.0f           // -100 to +100 ct, ID 202
  level: atomic<float> = 1.0f              // 0.0-1.0, ID 203
  phase: atomic<int> = 0                    // PhaseMode (0=Reset, 1=Continuous), ID 204
```

**Note (A12)**: OscType index 0 = PolyBLEP (verified from ruinae_types.h), which is the default "saw" oscillator.

**Engine mapping**: setOscBType(OscType), setOscBPhaseMode(PhaseMode), setOscBTuneSemitones(float), setOscBFineCents(float), setOscBLevel(float). All setters now present per A1.

## E-005: Mixer Parameters (ID 300-399)

```
MixerParams
  mode: atomic<int> = 0                     // 0=Crossfade, 1=SpectralMorph, ID 300
  position: atomic<float> = 0.5f           // 0.0-1.0 (0=A, 1=B), ID 301
```

**Engine mapping**: setMixMode(MixMode), setMixPosition(float).

## E-006: Filter Parameters (ID 400-499)

```
RuinaeFilterParams
  type: atomic<int> = 0                     // RuinaeFilterType enum (0-6), ID 400
  cutoffHz: atomic<float> = 20000.0f       // 20-20000 Hz, ID 401 (default: fully open)
  resonance: atomic<float> = 0.707f        // 0.1-30.0, ID 402
  envAmount: atomic<float> = 0.0f          // -48 to +48 semitones, ID 403
  keyTrack: atomic<float> = 0.0f           // 0.0-1.0, ID 404
```

**Engine mapping**: setFilterType(RuinaeFilterType), setFilterCutoff(float), setFilterResonance(float), setFilterEnvAmount(float), setFilterKeyTrack(float).

## E-007: Distortion Parameters (ID 500-599)

```
RuinaeDistortionParams
  type: atomic<int> = 0                     // RuinaeDistortionType enum (0-5), default=Clean (index 0), ID 500
  drive: atomic<float> = 0.0f              // 0.0-1.0, ID 501
  character: atomic<float> = 0.5f          // 0.0-1.0, ID 502
  mix: atomic<float> = 1.0f               // 0.0-1.0, ID 503
```

**Note (A2)**: Distortion default is now Clean (index 0) per code changes. Previous conflicting comment removed.

**Engine mapping**: setDistortionType(RuinaeDistortionType), setDistortionDrive(float), setDistortionCharacter(float), setDistortionMix(float) [added per A1].

## E-008: Trance Gate Parameters (ID 600-699)

```
RuinaeTranceGateParams
  enabled: atomic<bool> = false             // on/off, ID 600
  numSteps: atomic<int> = 16               // 8/16/32, ID 601
  rateHz: atomic<float> = 4.0f            // 0.1-100 Hz, ID 602
  depth: atomic<float> = 1.0f             // 0.0-1.0, ID 603
  attackMs: atomic<float> = 2.0f          // 1-20 ms, ID 604
  releaseMs: atomic<float> = 10.0f        // 1-50 ms, ID 605
  tempoSync: atomic<bool> = true          // on/off, ID 606
  noteValue: atomic<int> = 10             // Note value dropdown index, ID 607
```

**Engine mapping**: setTranceGateEnabled(bool), setTranceGateParams(Krate::DSP::TranceGateParams). The plugin constructs a DSP TranceGateParams struct from the atomic values.

## E-009: Amp Envelope Parameters (ID 700-799)

```
AmpEnvParams
  attackMs: atomic<float> = 10.0f          // 0-10000 ms, ID 700
  decayMs: atomic<float> = 50.0f          // 0-10000 ms, ID 701
  sustain: atomic<float> = 1.0f           // 0.0-1.0, ID 702 (default: full sustain for audible sound)
  releaseMs: atomic<float> = 100.0f       // 0-10000 ms, ID 703
```

**Engine mapping**: setAmpAttack(float ms), setAmpDecay(float ms), setAmpSustain(float), setAmpRelease(float ms).

## E-010: Filter Envelope Parameters (ID 800-899)

```
FilterEnvParams
  attackMs: atomic<float> = 10.0f          // 0-10000 ms, ID 800
  decayMs: atomic<float> = 200.0f         // 0-10000 ms, ID 801
  sustain: atomic<float> = 0.0f           // 0.0-1.0, ID 802
  releaseMs: atomic<float> = 100.0f       // 0-10000 ms, ID 803
```

**Engine mapping**: setFilterAttack(float ms), setFilterDecay(float ms), setFilterSustain(float), setFilterRelease(float ms).

## E-011: Mod Envelope Parameters (ID 900-999)

```
ModEnvParams
  attackMs: atomic<float> = 10.0f          // 0-10000 ms, ID 900
  decayMs: atomic<float> = 200.0f         // 0-10000 ms, ID 901
  sustain: atomic<float> = 0.0f           // 0.0-1.0, ID 902
  releaseMs: atomic<float> = 100.0f       // 0-10000 ms, ID 903
```

**Engine mapping**: setModAttack(float ms), setModDecay(float ms), setModSustain(float), setModRelease(float ms).

## E-012: LFO 1 Parameters (ID 1000-1099)

```
LFO1Params
  rateHz: atomic<float> = 1.0f            // 0.01-50 Hz, ID 1000
  shape: atomic<int> = 0                   // Waveform enum (0-5), ID 1001
  depth: atomic<float> = 0.0f             // 0.0-1.0, ID 1002
  sync: atomic<bool> = false               // on/off, ID 1003
```

**Engine mapping**: setGlobalLFO1Rate(float hz), setGlobalLFO1Waveform(Waveform).

## E-013: LFO 2 Parameters (ID 1100-1199)

```
LFO2Params
  rateHz: atomic<float> = 1.0f            // 0.01-50 Hz, ID 1100
  shape: atomic<int> = 0                   // Waveform enum (0-5), ID 1101
  depth: atomic<float> = 0.0f             // 0.0-1.0, ID 1102
  sync: atomic<bool> = false               // on/off, ID 1103
```

**Engine mapping**: setGlobalLFO2Rate(float hz), setGlobalLFO2Waveform(Waveform).

## E-014: Chaos Mod Parameters (ID 1200-1299)

```
ChaosModParams
  rateHz: atomic<float> = 1.0f            // 0.01-10 Hz, ID 1200
  type: atomic<int> = 0                    // 0=Lorenz, 1=Rossler, ID 1201
  depth: atomic<float> = 0.0f             // 0.0-1.0, ID 1202
```

**Engine mapping**: setChaosSpeed(float). Type and depth are stored for mod matrix configuration.

## E-015: Mod Matrix Parameters (ID 1300-1399)

```
ModMatrixParams
  slots: array of 8x {
    source: atomic<int> = 0                // ModSource enum (0-12), ID 1300+i*3
    dest: atomic<int> = 0                  // RuinaeModDest (64-70), ID 1301+i*3
    amount: atomic<float> = 0.0f           // -1.0 to +1.0 (bipolar), ID 1302+i*3
  }
```

**Note (A26)**: Mod matrix UI numbering - code uses 0-based (Slot 0-7), UI should display 1-based (Slot 1-8).

**Engine mapping**: setGlobalModRoute(slot, ModSource, RuinaeModDest, amount). Called when any slot parameter changes. Amount is denormalized from [0,1] to [-1,+1].

## E-016: Global Filter Parameters (ID 1400-1499)

```
GlobalFilterParams
  enabled: atomic<bool> = false             // on/off, ID 1400
  type: atomic<int> = 0                    // SVFMode subset (0-3 = LP/HP/BP/Notch), ID 1401
  cutoffHz: atomic<float> = 1000.0f       // 20-20000 Hz, ID 1402
  resonance: atomic<float> = 0.707f       // 0.1-30.0, ID 1403
```

**Engine mapping**: setGlobalFilterEnabled(bool), setGlobalFilterType(SVFMode), setGlobalFilterCutoff(float), setGlobalFilterResonance(float).

## E-017: Freeze Parameters (ID 1500-1599)

```
RuinaeFreezeParams
  enabled: atomic<bool> = false             // on/off, ID 1500
  freeze: atomic<bool> = false              // freeze capture toggle, ID 1501
```

**Naming clarification (A19)**: Freeze effect (IDs 1500-1599) = Spectral Freeze (freezes audio spectrum). Reverb freeze (ID 1706) = Reverb tail hold (infinite decay). Different features.

**Engine mapping**: setFreezeEnabled(bool), setFreeze(bool).

## E-018: Delay Parameters (ID 1600-1699)

```
RuinaeDelayParams
  type: atomic<int> = 0                    // RuinaeDelayType enum (0-4), ID 1600
  timeMs: atomic<float> = 500.0f          // 1-5000 ms, ID 1601
  feedback: atomic<float> = 0.4f          // 0.0-1.2, ID 1602
  mix: atomic<float> = 0.0f              // 0.0-1.0, ID 1603 (default: dry)
  sync: atomic<bool> = false               // on/off, ID 1604
  noteValue: atomic<int> = 10             // Note value dropdown index, ID 1605
```

**Engine mapping**: setDelayType(RuinaeDelayType), setDelayTime(float ms), setDelayFeedback(float), setDelayMix(float).

## E-019: Reverb Parameters (ID 1700-1799)

```
RuinaeReverbParams
  size: atomic<float> = 0.5f              // 0.0-1.0, ID 1700
  damping: atomic<float> = 0.5f           // 0.0-1.0, ID 1701
  width: atomic<float> = 1.0f             // 0.0-1.0, ID 1702
  mix: atomic<float> = 0.3f              // 0.0-1.0, ID 1703
  preDelayMs: atomic<float> = 0.0f       // 0-100 ms, ID 1704
  diffusion: atomic<float> = 0.7f        // 0.0-1.0, ID 1705
  freeze: atomic<bool> = false             // on/off, ID 1706
  modRateHz: atomic<float> = 0.5f        // 0.0-2.0 Hz, ID 1707
  modDepth: atomic<float> = 0.0f         // 0.0-1.0, ID 1708
```

**Engine mapping**: Constructs `Krate::DSP::ReverbParams` struct from atomics, calls `engine.setReverbParams(params)`.

## E-020: Mono Mode Parameters (ID 1800-1899)

```
MonoModeParams
  priority: atomic<int> = 0               // MonoMode (0=Last, 1=Low, 2=High), ID 1800
  legato: atomic<bool> = false             // on/off, ID 1801
  portamentoTimeMs: atomic<float> = 0.0f  // 0-5000 ms, ID 1802
  portaMode: atomic<int> = 0              // 0=Always, 1=LegatoOnly, ID 1803
```

**Engine mapping**: setMonoPriority(MonoMode), setLegato(bool), setPortamentoTime(float ms), setPortamentoMode(PortaMode).

## State Serialization Order

The state is serialized in this exact order (version prefixed):

```
1. int32 stateVersion
2. GlobalParams (4 fields: float, int32, int32, int32)
3. OscAParams (5 fields: int32, float, float, float, int32)
4. OscBParams (5 fields: int32, float, float, float, int32)
5. MixerParams (2 fields: int32, float)
6. RuinaeFilterParams (5 fields: int32, float, float, float, float)
7. RuinaeDistortionParams (4 fields: int32, float, float, float)
8. RuinaeTranceGateParams (8 fields: int32, int32, float, float, float, float, int32, int32)
9. AmpEnvParams (4 fields: float, float, float, float)
10. FilterEnvParams (4 fields: float, float, float, float)
11. ModEnvParams (4 fields: float, float, float, float)
12. LFO1Params (4 fields: float, int32, float, int32)
13. LFO2Params (4 fields: float, int32, float, int32)
14. ChaosModParams (3 fields: float, int32, float)
15. ModMatrixParams (24 fields: 8x [int32, int32, float])
16. GlobalFilterParams (4 fields: int32, int32, float, float)
17. RuinaeFreezeParams (2 fields: int32, int32)
18. RuinaeDelayParams (6 fields: int32, float, float, float, int32, int32)
19. RuinaeReverbParams (9 fields: float, float, float, float, float, float, int32, float, float)
20. MonoModeParams (4 fields: int32, int32, float, int32)

Total fields: ~97 serialized values
```

## Relationships

```
Processor 1--1 RuinaeEngine         (owns, creates in constructor or setupProcessing)
Processor 1--* ParameterPack        (owns all 19 parameter pack structs)
Controller 1--* Parameter           (registers ~80-100 VST3 parameters)
Controller 1--1 PresetManager       (already initialized)
ParameterPack -->|applied to| RuinaeEngine  (via setter calls in applyParamsToEngine())
```
