# API Contract: Global Modulation Composition and Forwarding

**Feature**: 042-ext-modulation-system | **Layer**: 3 (Systems)

## Overview

This contract defines how the existing ModulationEngine is composed into a test scaffold to provide global modulation. The test scaffold validates the patterns that will be used by the Phase 6 RuinaeEngine.

## Global Destination ID Constants

```cpp
// Test scaffold constants (defined in ext_modulation_test.cpp)
constexpr uint32_t kGlobalFilterCutoffDestId = 0;
constexpr uint32_t kGlobalFilterResonanceDestId = 1;
constexpr uint32_t kMasterVolumeDestId = 2;
constexpr uint32_t kEffectMixDestId = 3;
constexpr uint32_t kAllVoiceFilterCutoffDestId = 4;
constexpr uint32_t kAllVoiceMorphPositionDestId = 5;
constexpr uint32_t kTranceGateRateDestId = 6;
```

## Source Registration Pattern (FR-012)

```cpp
ModulationEngine engine;
engine.prepare(44100.0, 512);

// Built-in sources (owned by engine):
// - LFO 1 (ModSource::LFO1) -- configured via setLFO1Rate(), setLFO1Waveform()
// - LFO 2 (ModSource::LFO2)
// - Chaos Mod (ModSource::Chaos) -- configured via setChaosModel(), setChaosSpeed()
// - Env Follower (ModSource::EnvFollower)
// - Random (ModSource::Random)
// - S&H (ModSource::SampleHold)
// - Pitch Follower (ModSource::PitchFollower)
// - Transient (ModSource::Transient)

// External sources injected via macros:
// - Pitch Bend -> Macro1: engine.setMacroValue(0, normalizedPitchBend)
// - Mod Wheel -> Macro2: engine.setMacroValue(1, normalizedModWheel)
// - Rungler -> Macro3: engine.setMacroValue(2, rungler.getCurrentValue())
// - User Macro -> Macro4: engine.setMacroValue(3, userValue)
```

## Routing Configuration Pattern (FR-013)

```cpp
// Example: LFO1 -> Global Filter Cutoff
ModRouting routing;
routing.source = ModSource::LFO1;
routing.destParamId = kGlobalFilterCutoffDestId;
routing.amount = 0.5f;
routing.curve = ModCurve::Linear;
routing.active = true;
engine.setRouting(0, routing);

// Example: Chaos -> Master Volume
routing.source = ModSource::Chaos;
routing.destParamId = kMasterVolumeDestId;
routing.amount = 0.3f;
routing.active = true;
engine.setRouting(1, routing);
```

## Processing Order (FR-014)

```
1. Update external source values (pitch bend, mod wheel, rungler)
   engine.setMacroValue(0, pitchBendNormalized);
   engine.setMacroValue(1, modWheelNormalized);
   engine.setMacroValue(2, rungler.getCurrentValue());

2. Process global modulation engine
   engine.process(blockCtx, inputL, inputR, numSamples);

3. Read global offsets
   float globalFilterOffset = engine.getModulationOffset(kGlobalFilterCutoffDestId);
   float allVoiceCutoffOffset = engine.getModulationOffset(kAllVoiceFilterCutoffDestId);
   float allVoiceMorphOffset = engine.getModulationOffset(kAllVoiceMorphPositionDestId);
   float tranceGateRateOffset = engine.getModulationOffset(kTranceGateRateDestId);

4. Forward "All Voice" offsets to each active voice
   for (auto& voice : voices) {
       if (!voice.isActive()) continue;
       // Apply via two-stage clamping (FR-021)
   }

5. Process each voice
   for (auto& voice : voices) {
       voice.processBlock(output, numSamples);
   }
```

## Global-to-Voice Forwarding (FR-018, FR-019, FR-020, FR-021)

### Two-Stage Clamping Formula

```cpp
// FR-021: Global offset applied AFTER per-voice clamping
float finalValue = std::clamp(
    std::clamp(baseValue + perVoiceOffset, min, max) + globalOffset,
    min, max
);
```

### All Voice Filter Cutoff (FR-018)
```cpp
// Global offset is in normalized range [-1, +1]
// Scale to semitones: offset * 48 semitones (4 octaves, matching standard filter
// cutoff modulation range in professional synthesizers like Surge XT and Vital)
float globalCutoffSemitones = allVoiceCutoffOffset * 48.0f;
// Apply to each voice's filter cutoff computation
// Two-stage clamping range: [-96.0, +96.0] semitones (8 octaves total)
```

### All Voice Morph Position (FR-019)
```cpp
// Global offset applied directly in normalized [0, 1] space (no scaling needed)
float globalMorphOffset = allVoiceMorphOffset;
// For each voice: finalMix = clamp(clamp(baseMix + perVoiceOffset, 0.0f, 1.0f) + globalMorphOffset, 0.0f, 1.0f)
```

### Trance Gate Rate (FR-020)
```cpp
// Global offset scaled to Hz range
float globalRateOffsetHz = tranceGateRateOffset * 19.9f;  // scale [-1,+1] to ~[-20,+20] Hz
// For each voice:
float baseRate = voice.tranceGateParams.rateHz;
float effectiveRate = std::clamp(baseRate + globalRateOffsetHz, 0.1f, 20.0f);
```

## Pitch Bend Normalization (FR-015)

```cpp
// MIDI 14-bit pitch bend: [0x0000, 0x3FFF] with center at 0x2000
float normalizePitchBend(uint16_t rawValue) noexcept {
    // Center = 8192, range = [-8192, +8191]
    return static_cast<float>(static_cast<int>(rawValue) - 8192) / 8192.0f;
}
// Result: -1.0 at 0x0000, 0.0 at 0x2000, ~+1.0 at 0x3FFF

// Map to macro (macros are unipolar [0, 1]):
// NOTE: This conversion LOSES the bipolar representation. The full [-1,+1] range
// is recovered by using bipolar routing amounts. For example:
//   pitchBend = -1.0 -> macroValue = 0.0 -> with routing amount +2.0 -> offset = 0.0
//   pitchBend = +1.0 -> macroValue = 1.0 -> with routing amount +2.0 -> offset = 2.0
// Phase 6 (Ruinae Engine) may add a dedicated PitchBend ModSource enum to avoid this.
float pitchBendAsMacro = (normalizePitchBend(rawValue) + 1.0f) * 0.5f;
engine.setMacroValue(0, pitchBendAsMacro);
```

## Mod Wheel Normalization (FR-016)

```cpp
// MIDI CC#1: [0, 127]
float normalizeModWheel(uint8_t ccValue) noexcept {
    return static_cast<float>(ccValue) / 127.0f;
}
// Result: 0.0 at CC=0, ~0.5 at CC=64, 1.0 at CC=127

engine.setMacroValue(1, normalizeModWheel(ccValue));
```

## Amount Smoothing (FR-023)

The ModulationEngine already applies OnePoleSmoother (20ms time constant) to each routing's amount. This satisfies FR-023 for global routes. No additional smoothing needed.

Per-voice route amounts are NOT smoothed (spec clarification), applied instantly.

## Test Verification Matrix

| Test Case | Setup | Expected |
|-----------|-------|----------|
| LFO1 -> GlobalFilterCutoff | LFO1 output = +1.0, amount = 0.5 | GlobalFilterCutoff offset = +0.5 |
| Chaos -> MasterVolume | Chaos varies over blocks | MasterVolume offset varies smoothly |
| No global routings | Engine processes with no routings | All offsets = 0.0 |
| ModWheel -> EffectMix | CC#1 = 64, route via Macro2, amount = 1.0 | EffectMix offset ~ 0.5 |
| PitchBend -> AllVoiceFilterCutoff | Bend = 0x3FFF (max), amount = 0.5 | AllVoiceFilterCutoff offset ~ +0.5 |
| AllVoiceFilterCutoff forwarding | 3 voices, LFO2 = +0.5, amount = 0.8 | Each voice cutoff shifted by 0.4 |
| AllVoiceMorphPosition forwarding | Macro1 = 0.7, amount = 1.0, 2 voices | Each voice morph shifted by 0.7 |
| TranceGateRate forwarding | Source varies, amount = 1.0 | All voices' gate rates update |
| Two-stage clamping | Per-voice offset = +0.9, global offset = +0.5, range [0,1] | perVoice clamped to 1.0, then 1.0 + 0.5 clamped to 1.0 |
