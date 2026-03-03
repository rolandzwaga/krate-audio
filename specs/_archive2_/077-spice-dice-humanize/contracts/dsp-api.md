# DSP API Contract: Spice/Dice & Humanize

**Feature**: 077-spice-dice-humanize | **Date**: 2026-02-23

## ArpeggiatorCore Public API Additions

### Spice/Dice Methods

```cpp
/// Set Spice blend amount.
/// @param value Blend ratio in [0.0, 1.0]. 0 = original, 1 = full overlay.
///              Values outside range are clamped.
/// @note Real-time safe. No allocation.
void setSpice(float value) noexcept;

/// Get current Spice blend amount.
/// @return Spice amount in [0.0, 1.0]
[[nodiscard]] float spice() const noexcept;

/// Generate new random overlay values for velocity, gate, ratchet, and condition.
/// Uses spiceDiceRng_ (seed 31337) to generate 128 values (32 per lane x 4 lanes).
/// @note Real-time safe. No allocation, no exceptions, no I/O.
/// @note Each call produces different values (PRNG state advances).
/// @note Order: velocity[0..31], gate[0..31], ratchet[0..31], condition[0..31]
void triggerDice() noexcept;
```

### Humanize Methods

```cpp
/// Set Humanize amount.
/// @param value Magnitude in [0.0, 1.0]. 0 = quantized, 1 = max variation.
///              Values outside range are clamped.
/// @note Real-time safe. No allocation.
void setHumanize(float value) noexcept;

/// Get current Humanize amount.
/// @return Humanize amount in [0.0, 1.0]
[[nodiscard]] float humanize() const noexcept;
```

## Spice Blend Formulas

Applied in fireStep() after lane advances, before Euclidean/Condition/Modifier evaluation:

```
velocity:  effectiveVelScale = velScale + (velocityOverlay_[velStep] - velScale) * spice_
gate:      effectiveGateScale = gateScale + (gateOverlay_[gateStep] - gateScale) * spice_
ratchet:   effectiveRatchet = clamp(round(lerp(ratchetCount, ratchetOverlay_[ratchetStep], spice_)), 1, 4)
condition: effectiveCondition = (spice_ >= 0.5) ? conditionOverlay_[condStep] : condValue
```

Where `lerp(a, b, t) = a + (b - a) * t` and overlay indices are captured BEFORE lane advances.

## Humanize Offset Formulas

Applied in fireStep() after accent/pitch, before note emission:

```
maxTimingSamples = int32_t(sampleRate_ * 0.020f)
timingOffset     = int32_t(humanizeRng_.nextFloat() * maxTimingSamples * humanize_)
velocityOffset   = int(humanizeRng_.nextFloat() * 15.0f * humanize_)
gateOffsetRatio  = humanizeRng_.nextFloat() * 0.10f * humanize_

finalSampleOffset = clamp(sampleOffset + timingOffset, 0, blockSize - 1)
finalVelocity     = clamp(velocity + velocityOffset, 1, 127)
finalGateDuration = max(1, gateDuration + int32_t(gateDuration * gateOffsetRatio))
```

## PRNG Consumption Contract

The humanize PRNG (humanizeRng_) is consumed **exactly 3 times per step** regardless of whether the step fires:

- Fired step: 3 values used for timing/velocity/gate offsets
- Skipped step (Euclidean rest, condition fail, modifier Rest, Tie): 3 values consumed and discarded
- Defensive branch (empty held buffer): 3 values consumed and discarded (after all lane advances)

This ensures the humanize sequence position depends only on total step count, not pattern content.

## Overlay Persistence Contract

| State | reset() | resetLanes() | preset load | triggerDice() |
|---|---|---|---|---|
| velocityOverlay_ | Preserved | Preserved | Reverts to identity | Regenerated |
| gateOverlay_ | Preserved | Preserved | Reverts to identity | Regenerated |
| ratchetOverlay_ | Preserved | Preserved | Reverts to identity | Regenerated |
| conditionOverlay_ | Preserved | Preserved | Reverts to identity | Regenerated |
| spice_ | Preserved | Preserved | Loaded from preset | Unchanged |
| humanize_ | Preserved | Preserved | Loaded from preset | Unchanged |
| spiceDiceRng_ | Preserved | Preserved | Default seed state | Advanced |
| humanizeRng_ | Preserved | Preserved | Default seed state | Unchanged |
