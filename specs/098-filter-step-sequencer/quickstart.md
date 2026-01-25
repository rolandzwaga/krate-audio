# Quickstart: FilterStepSequencer

**Feature**: 098-filter-step-sequencer | **Date**: 2026-01-25

## What is FilterStepSequencer?

A 16-step filter parameter sequencer that creates rhythmic filter sweeps synchronized to host tempo. Think classic trance-gate effects, rhythmic filter modulation, or evolving textures.

## Quick Example

```cpp
#include <krate/dsp/systems/filter_step_sequencer.h>

using namespace Krate::DSP;

// Create the sequencer
FilterStepSequencer seq;
seq.prepare(44100.0);

// Configure 4 steps with ascending cutoff
seq.setNumSteps(4);
seq.setStepCutoff(0, 200.0f);   // Step 0: Dark
seq.setStepCutoff(1, 800.0f);   // Step 1: Warm
seq.setStepCutoff(2, 2000.0f);  // Step 2: Present
seq.setStepCutoff(3, 5000.0f);  // Step 3: Bright

// Set tempo and note value
seq.setTempo(120.0f);                     // 120 BPM
seq.setNoteValue(NoteValue::Quarter);     // 1/4 note steps

// Add some character
seq.setGlideTime(50.0f);  // Smooth 50ms transitions

// Process audio
for (size_t i = 0; i < numSamples; ++i) {
    buffer[i] = seq.process(buffer[i]);
}
```

## Common Patterns

### Trance Gate Effect

```cpp
seq.setNumSteps(8);
seq.setNoteValue(NoteValue::Sixteenth);
seq.setGateLength(0.5f);  // 50% gate = pumping effect

// Alternating cutoffs
for (int i = 0; i < 8; ++i) {
    seq.setStepCutoff(i, (i % 2 == 0) ? 2000.0f : 800.0f);
}
```

### Dubstep Wobble

```cpp
seq.setNumSteps(4);
seq.setNoteValue(NoteValue::Eighth);
seq.setGlideTime(100.0f);

// Resonant lowpass wobble
for (int i = 0; i < 4; ++i) {
    seq.setStepCutoff(i, 200.0f + i * 400.0f);
    seq.setStepQ(i, 8.0f);  // High resonance
}
```

### Evolving Texture with Filter Type Changes

```cpp
seq.setNumSteps(8);
seq.setNoteValue(NoteValue::Quarter);
seq.setGlideTime(200.0f);  // Long glide for smooth morphing

// Mix of filter types
seq.setStepType(0, SVFMode::Lowpass);
seq.setStepType(1, SVFMode::Lowpass);
seq.setStepType(2, SVFMode::Bandpass);
seq.setStepType(3, SVFMode::Bandpass);
seq.setStepType(4, SVFMode::Highpass);
seq.setStepType(5, SVFMode::Highpass);
seq.setStepType(6, SVFMode::Notch);
seq.setStepType(7, SVFMode::Notch);
```

### Swing Groove

```cpp
seq.setNumSteps(8);
seq.setNoteValue(NoteValue::Sixteenth);
seq.setSwing(0.5f);  // 3:1 swing ratio for groove
```

### Triplet and Dotted Note Timing

Use `NoteModifier` for triplet or dotted rhythms:

```cpp
// Triplet eighths - 3 notes per beat (shuffle feel)
seq.setNoteValue(NoteValue::Eighth, NoteModifier::Triplet);

// Dotted sixteenths - longer, syncopated feel
seq.setNoteValue(NoteValue::Sixteenth, NoteModifier::Dotted);

// Normal quarter notes (default - no modifier needed)
seq.setNoteValue(NoteValue::Quarter);  // Same as NoteModifier::Normal
```

**Common rhythmic feels:**
- `NoteValue::Eighth + Triplet`: Classic shuffle/swing-8 feel
- `NoteValue::Sixteenth + Triplet`: Fast triplet patterns (12/8 feel)
- `NoteValue::Quarter + Dotted`: Slow, syncopated pulse

### Random Exploration

```cpp
seq.setNumSteps(8);
seq.setDirection(Direction::Random);  // Non-repeating random

// Different filter settings on each step
for (int i = 0; i < 8; ++i) {
    seq.setStepCutoff(i, 200.0f + i * 500.0f);
    seq.setStepType(i, static_cast<SVFMode>(i % 4));
}
```

## Integration with VST3

### In Processor::process()

```cpp
void MyProcessor::process(ProcessData& data) {
    // Get tempo from host
    if (data.processContext) {
        sequencer_.setTempo(static_cast<float>(data.processContext->tempo));
    }

    // Process audio
    float* left = data.outputs[0].channelBuffers32[0];
    float* right = data.outputs[0].channelBuffers32[1];

    // Option 1: Use BlockContext
    BlockContext ctx;
    ctx.tempoBPM = data.processContext->tempo;
    ctx.sampleRate = processSetup.sampleRate;
    sequencer_.processBlock(left, data.numSamples, &ctx);
    sequencer_.processBlock(right, data.numSamples, &ctx);

    // Option 2: Direct processing
    for (int32 i = 0; i < data.numSamples; ++i) {
        left[i] = sequencer_.process(left[i]);
        right[i] = sequencer_.process(right[i]);
    }
}
```

### Transport Sync

```cpp
// In process() when host seeks
if (data.processContext && (data.processContext->state & ProcessContext::kProjectTimeMusicValid)) {
    sequencer_.sync(data.processContext->projectTimeMusicInBeat);
}
```

## Parameter Mapping Reference

| Parameter | VST Range | Internal Range | Notes |
|-----------|-----------|----------------|-------|
| Num Steps | 1-16 (int) | 1-16 | Direct mapping |
| Cutoff (per step) | 0-1 (normalized) | 20-20000 Hz | Log mapping recommended |
| Q (per step) | 0-1 (normalized) | 0.5-20.0 | Log or linear |
| Filter Type (per step) | 0-5 (int) | SVFMode enum | 0=LP, 1=HP, 2=BP, 3=Notch, 4=AP, 5=Peak |
| Gain (per step) | 0-1 (normalized) | -24 to +12 dB | Linear dB mapping |
| Tempo | 20-300 BPM | 20-300 BPM | Direct or from host |
| Note Value | 0-20 (dropdown) | See NoteValue | Use kNoteValueDropdownMapping |
| Swing | 0-100% | 0-1 | Direct / 100 |
| Glide | 0-500 ms | 0-500 ms | Direct |
| Gate Length | 0-100% | 0-1 | Direct / 100 |
| Direction | 0-3 (int) | Direction enum | 0=Fwd, 1=Bwd, 2=PP, 3=Rnd |

## Tips

1. **Start with defaults**: The sequencer works out of the box with sensible defaults.

2. **Glide makes it musical**: Even 10-20ms of glide smooths harsh transitions.

3. **Gate length for rhythm**: 50-75% gate length creates rhythmic pumping.

4. **Swing adds groove**: 25-50% swing works well for most music.

5. **Use PingPong for builds**: Rising then falling filter creates tension.

6. **Random for generative**: Combined with different step settings, creates evolving textures.

## Troubleshooting

**No output change?**
- Check `prepare()` was called
- Verify `numSteps` > 0
- Ensure tempo is reasonable (20-300 BPM)

**Clicking sounds?**
- Increase glide time (even 5ms helps)
- Check gate length isn't changing too rapidly
- For extreme settings, the 5ms crossfade handles transitions

**Steps not syncing to host?**
- Call `setTempo()` with host tempo each buffer
- Use `sync()` when host transport seeks
- Or pass BlockContext to `processBlock()`

**Random mode repeating?**
- This is by design: no immediate repeats, but steps can recur
- For truly different each time, use more steps
