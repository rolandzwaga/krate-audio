# Unified Exciter Interface Contract

## Interface Signature (FR-015)

All exciter types MUST implement:

```cpp
float process(float feedbackVelocity) noexcept;
```

## Exciter Type Behavior

| Exciter | feedbackVelocity Usage | trigger() Sets |
|---------|----------------------|----------------|
| ResidualSynthesizer | Ignored (buffer playback) | N/A (frame loading) |
| ImpactExciter | Ignored (transient pulse) | velocity, hardness, mass, brightness, position, f0 |
| BowExciter | Used (friction computation) | velocity (maps to maxVelocity, targetEnergy) |

## Voice Engine Call Pattern (FR-017)

```cpp
// Unified call pattern -- NO exciter-type branching for process()
float feedbackVelocity = resonator->getFeedbackVelocity();
float excitation = 0.0f;

switch (exciterType) {
    case ExciterType::Residual:
        excitation = voice.residualSynth.process(feedbackVelocity);
        break;
    case ExciterType::Impact:
        excitation = voice.impactExciter.process(feedbackVelocity);
        break;
    case ExciterType::Bow:
        voice.bowExciter.setEnvelopeValue(adsrValue);
        voice.bowExciter.setResonatorEnergy(resonator->getControlEnergy());
        excitation = voice.bowExciter.process(feedbackVelocity);
        break;
}

float output = resonator->process(excitation);
```

Note: While the switch exists for exciter-specific pre-processing (ADSR routing for bow), the actual `process(feedbackVelocity)` call is uniform. The spec's "no exciter-type branching" (FR-017) refers to the process call signature being identical, not that zero branching exists in the voice engine.

## Refactoring Changes

### ImpactExciter (impact_exciter.h)

```cpp
// BEFORE:
float process() noexcept { ... }
void processBlock(float* output, int numSamples) noexcept {
    for (int i = 0; i < numSamples; ++i)
        output[i] = process();
}

// AFTER:
float process(float /*feedbackVelocity*/) noexcept { ... } // body unchanged
void processBlock(float* output, int numSamples) noexcept {
    for (int i = 0; i < numSamples; ++i)
        output[i] = process(0.0f);
}
```

### ResidualSynthesizer (residual_synthesizer.h)

```cpp
// BEFORE:
[[nodiscard]] float process() noexcept { ... }

// AFTER:
[[nodiscard]] float process(float /*feedbackVelocity*/) noexcept { ... } // body unchanged
```

### Call Site Updates

| File | Line | Before | After |
|------|------|--------|-------|
| processor.cpp | 1631 | `v.impactExciter.process()` | `v.impactExciter.process(feedbackVelocity)` |
| processor.cpp | 1635 | `v.residualSynth.process()` | `v.residualSynth.process(feedbackVelocity)` |
| impact_exciter_test.cpp | ~13 sites | `.process()` | `.process(0.0f)` |
| residual_synthesizer_tests.cpp | multiple | `.process()` | `.process(0.0f)` |
