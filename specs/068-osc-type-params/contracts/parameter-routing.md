# Contract: Parameter Routing Chain

**Layer**: Plugin (Ruinae)

## Data Flow

```
VST Host (normalized 0.0-1.0)
    |
    v
processParameterChanges() --> handleOscAParamChange() / handleOscBParamChange()
    |                         (denormalize to DSP domain, store in atomics)
    v
OscAParams / OscBParams (atomic<float> / atomic<int>)
    |
    v
applyParamsToEngine()
    |                         (reads atomics, maps to OscParam via lookup table)
    v
RuinaeEngine::setOscAParam(OscParam, float) / setOscBParam(OscParam, float)
    |                         (iterates all 16 voices)
    v
RuinaeVoice::setOscAParam(OscParam, float)
    |                         (forwards to SelectableOscillator)
    v
SelectableOscillator::setParam(OscParam, float)
    |                         (forwards to active OscillatorSlot*)
    v
OscillatorAdapter<OscT>::setParam(OscParam, float)
    |                         (if constexpr dispatch to OscT setter)
    v
Concrete Oscillator (e.g., PolyBlepOscillator::setWaveform())
```

## Denormalization Rules

| Parameter ID | Normalized Range | DSP Domain | Formula |
|-------------|------------------|------------|---------|
| kOscAWaveformId (110) | 0-1 | int 0-4 | `int(value * 4 + 0.5)` |
| kOscAPulseWidthId (111) | 0-1 | 0.01-0.99 | `0.01 + value * 0.98` |
| kOscAPhaseModId (112) | 0-1 | -1.0 to +1.0 | `value * 2.0 - 1.0` |
| kOscAFreqModId (113) | 0-1 | -1.0 to +1.0 | `value * 2.0 - 1.0` |
| kOscAPDWaveformId (114) | 0-1 | int 0-7 | `int(value * 7 + 0.5)` |
| kOscAPDDistortionId (115) | 0-1 | 0.0-1.0 | `value` (identity) |
| kOscASyncRatioId (116) | 0-1 | 1.0-8.0 | `1.0 + value * 7.0` |
| kOscASyncWaveformId (117) | 0-1 | int 0-4 | `int(value * 4 + 0.5)` |
| kOscASyncModeId (118) | 0-1 | int 0-2 | `int(value * 2 + 0.5)` |
| kOscASyncAmountId (119) | 0-1 | 0.0-1.0 | `value` (identity) |
| kOscASyncPulseWidthId (120) | 0-1 | 0.01-0.99 | `0.01 + value * 0.98` |
| kOscAAdditivePartialsId (121) | 0-1 | int 1-128 | `int(value * 127 + 0.5) + 1` |
| kOscAAdditiveTiltId (122) | 0-1 | -24 to +24 | `value * 48.0 - 24.0` |
| kOscAAdditiveInharmId (123) | 0-1 | 0.0-1.0 | `value` (identity) |
| kOscAChaosAttractorId (124) | 0-1 | int 0-4 | `int(value * 4 + 0.5)` |
| kOscAChaosAmountId (125) | 0-1 | 0.0-1.0 | `value` (identity) |
| kOscAChaosCouplingId (126) | 0-1 | 0.0-1.0 | `value` (identity) |
| kOscAChaosOutputId (127) | 0-1 | int 0-2 | `int(value * 2 + 0.5)` |
| kOscAParticleScatterId (128) | 0-1 | 0.0-12.0 st | `value * 12.0` |
| kOscAParticleDensityId (129) | 0-1 | 1.0-64.0 (float) | `1.0 + value * 63.0` (continuous; `setDensity` accepts float) |
| kOscAParticleLifetimeId (130) | 0-1 | 5.0-2000.0 ms | `5.0 + value * 1995.0` |
| kOscAParticleSpawnModeId (131) | 0-1 | int 0-2 | `int(value * 2 + 0.5)` |
| kOscAParticleEnvTypeId (132) | 0-1 | int 0-5 | `int(value * 5 + 0.5)` |
| kOscAParticleDriftId (133) | 0-1 | 0.0-1.0 | `value` (identity) |
| kOscAFormantVowelId (134) | 0-1 | int 0-4 | `int(value * 4 + 0.5)` |
| kOscAFormantMorphId (135) | 0-1 | 0.0-4.0 | `value * 4.0` |
| kOscASpectralPitchId (136) | 0-1 | -24 to +24 st | `value * 48.0 - 24.0` |
| kOscASpectralTiltId (137) | 0-1 | -12 to +12 dB/oct | `value * 24.0 - 12.0` |
| kOscASpectralFormantId (138) | 0-1 | -12 to +12 st | `value * 24.0 - 12.0` |
| kOscANoiseColorId (139) | 0-1 | int 0-5 | `int(value * 5 + 0.5)` |

OSC B uses identical formulas with IDs offset by +100.

## Dropdown Parameters (registered via StringListParameter)

Dropdown parameters use `StringListParameter` which internally handles denormalization to integer index via `toPlain()`. The `handleOscAParamChange` uses nearest-integer rounding for consistency.

**Step Count convention**: VST3 `StringListParameter` takes `stepCount = numValues - 1` (the number of discrete steps between values, not the number of values). For example, a 5-entry dropdown uses Step Count = 4.

| Parameter | Type | Step Count (numValues - 1) | Num Values | Strings Array |
|-----------|------|---------------------------|------------|---------------|
| Waveform (110/210) | StringListParameter | 4 | 5 | kOscWaveformStrings |
| PD Waveform (114/214) | StringListParameter | 7 | 8 | kPDWaveformStrings |
| Sync Waveform (117/217) | StringListParameter | 4 | 5 | kOscWaveformStrings |
| Sync Mode (118/218) | StringListParameter | 2 | 3 | kSyncModeStrings |
| Chaos Attractor (124/224) | StringListParameter | 4 | 5 | kChaosAttractorStrings |
| Chaos Output (127/227) | StringListParameter | 2 | 3 | kChaosOutputStrings |
| Particle Spawn Mode (131/231) | StringListParameter | 2 | 3 | kParticleSpawnModeStrings |
| Particle Env Type (132/232) | StringListParameter | 5 | 6 | kParticleEnvTypeStrings |
| Formant Vowel (134/234) | StringListParameter | 4 | 5 | kFormantVowelStrings |
| Noise Color (139/239) | StringListParameter | 5 | 6 | kNoiseColorStrings |

## Continuous Parameters (registered via RangeParameter or basic Parameter)

| Parameter | Type | Default | Unit |
|-----------|------|---------|------|
| Pulse Width (111/211) | Parameter | 0.5 | (display: 0.01-0.99) |
| Phase Mod (112/212) | Parameter | 0.5 | (display: -1.0 to +1.0) |
| Freq Mod (113/213) | Parameter | 0.5 | (display: -1.0 to +1.0) |
| PD Distortion (115/215) | Parameter | 0.0 | (display: 0-100%) |
| Sync Ratio (116/216) | Parameter | 0.143 | (display: 1.0-8.0x) |
| Sync Amount (119/219) | Parameter | 1.0 | (display: 0-100%) |
| Sync PW (120/220) | Parameter | 0.5 | (display: 0.01-0.99) |
| Additive Partials (121/221) | Parameter | 0.118 | (display: 1-128) |
| Additive Tilt (122/222) | Parameter | 0.5 | (display: -24 to +24 dB/oct) |
| Additive Inharm (123/223) | Parameter | 0.0 | (display: 0-100%) |
| Chaos Amount (125/225) | Parameter | 0.5 | (display: 0-100%) |
| Chaos Coupling (126/226) | Parameter | 0.0 | (display: 0-100%) |
| Particle Scatter (128/228) | Parameter | 0.25 | (display: 0-12 st) |
| Particle Density (129/229) | Parameter | 0.238 | (display: 1-64) |
| Particle Lifetime (130/230) | Parameter | 0.0977 | (display: 5-2000 ms) |
| Particle Drift (133/233) | Parameter | 0.0 | (display: 0-100%) |
| Formant Morph (135/235) | Parameter | 0.0 | (display: 0.0-4.0) |
| Spectral Pitch (136/236) | Parameter | 0.5 | (display: -24 to +24 st) |
| Spectral Tilt (137/237) | Parameter | 0.5 | (display: -12 to +12 dB/oct) |
| Spectral Formant (138/238) | Parameter | 0.5 | (display: -12 to +12 st) |
