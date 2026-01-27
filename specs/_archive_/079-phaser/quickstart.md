# Quickstart: Phaser Effect Processor

**Spec**: 079-phaser | **Location**: `dsp/include/krate/dsp/processors/phaser.h`

## Include

```cpp
#include <krate/dsp/processors/phaser.h>
using namespace Krate::DSP;
```

## Basic Usage

### Classic 4-Stage Phaser

```cpp
Phaser phaser;

// Initialize for sample rate
phaser.prepare(44100.0);

// Configure classic settings
phaser.setNumStages(4);           // 4 stages = 2 notches
phaser.setRate(0.5f);             // 0.5 Hz sweep rate
phaser.setDepth(0.5f);            // Medium sweep range
phaser.setCenterFrequency(1000.0f); // 1kHz center
phaser.setFeedback(0.0f);         // No resonance
phaser.setMix(0.5f);              // 50/50 dry/wet

// Process audio (in audio callback)
for (size_t i = 0; i < numSamples; ++i) {
    buffer[i] = phaser.process(buffer[i]);
}
```

### Deep Phaser with Resonance

```cpp
Phaser phaser;
phaser.prepare(44100.0);

// More stages and feedback for intense effect
phaser.setNumStages(8);           // 8 stages = 4 notches
phaser.setRate(0.3f);             // Slower sweep
phaser.setDepth(0.8f);            // Wide sweep range
phaser.setCenterFrequency(800.0f);
phaser.setFeedback(0.7f);         // Strong resonance
phaser.setMix(0.6f);

// Block processing
phaser.processBlock(buffer, numSamples);
```

### Negative Feedback Phaser

```cpp
Phaser phaser;
phaser.prepare(44100.0);

phaser.setNumStages(6);
phaser.setRate(0.8f);
phaser.setDepth(0.6f);
phaser.setCenterFrequency(1200.0f);
phaser.setFeedback(-0.5f);        // Negative feedback shifts notch positions
phaser.setMix(0.5f);
```

## Stereo Processing

### Wide Stereo Phaser

```cpp
Phaser phaser;
phaser.prepare(44100.0);

phaser.setNumStages(6);
phaser.setRate(0.4f);
phaser.setDepth(0.7f);
phaser.setCenterFrequency(1000.0f);
phaser.setFeedback(0.3f);
phaser.setStereoSpread(180.0f);   // L/R completely out of phase
phaser.setMix(0.5f);

// Process stereo (in audio callback)
phaser.processStereo(leftBuffer, rightBuffer, numSamples);
```

### Subtle Stereo Width

```cpp
phaser.setStereoSpread(90.0f);    // 90 degrees = quadrature
// When L channel is at peak, R channel is at midpoint
```

## Tempo Synchronization

### Synced to 1/4 Note

```cpp
Phaser phaser;
phaser.prepare(44100.0);

// Enable tempo sync
phaser.setTempoSync(true);
phaser.setNoteValue(NoteValue::Quarter);  // One sweep per quarter note
phaser.setTempo(120.0f);                  // 120 BPM = 2 Hz rate

phaser.setNumStages(4);
phaser.setDepth(0.6f);
phaser.setCenterFrequency(1000.0f);
phaser.setFeedback(0.4f);
phaser.setMix(0.5f);
```

### Synced to Dotted Eighth

```cpp
phaser.setTempoSync(true);
phaser.setNoteValue(NoteValue::Eighth, NoteModifier::Dotted);
phaser.setTempo(100.0f);
```

### Dynamic Tempo Updates

```cpp
// In audio callback, update tempo from host
void processBlock(float** outputs, size_t numSamples, float hostTempo) {
    phaser.setTempo(hostTempo);  // Smooth tempo changes
    phaser.processStereo(outputs[0], outputs[1], numSamples);
}
```

## Waveform Selection

### Triangle Wave (Ramp-like)

```cpp
phaser.setWaveform(Waveform::Triangle);
// Creates more linear sweep with sharp turnaround at extremes
```

### Square Wave (Stepped)

```cpp
phaser.setWaveform(Waveform::Square);
// Jumps between min and max frequency, no smooth sweep
// Creates unique "sample-and-hold" character
```

### Sawtooth Wave

```cpp
phaser.setWaveform(Waveform::Sawtooth);
// Sweeps up slowly, resets quickly
// Asymmetric modulation character
```

## Parameter Automation

### Real-Time Parameter Changes

All parameter changes are smoothed (5ms) for click-free automation:

```cpp
// Safe to call from any thread
phaser.setRate(newRate);              // Smoothed
phaser.setDepth(newDepth);            // Smoothed
phaser.setFeedback(newFeedback);      // Smoothed
phaser.setMix(newMix);                // Smoothed
phaser.setCenterFrequency(newFreq);   // Smoothed

// Stage count changes are immediate (integer parameter)
phaser.setNumStages(newStages);       // Immediate (may cause small click)
```

### Immediate Value Setting (for preset loading)

```cpp
// When loading presets, snap to values without smoothing
phaser.prepare(44100.0);  // Prepares all smoothers

// Then call prepare again to reset with new sample rate
// All smoothers snap to their target values on prepare()
```

## Processing Patterns

### Mono Input, Stereo Output

```cpp
// If you have mono input but want stereo output
float monoIn = ...;
float left = monoIn;
float right = monoIn;

phaser.setStereoSpread(90.0f);  // Create stereo width
phaser.processStereo(&left, &right, 1);

// left and right now have different phaser modulation
```

### Bypass Handling

```cpp
// Mix = 0.0 means 100% dry (bypass without processing overhead)
if (bypassed) {
    phaser.setMix(0.0f);  // Smoothly fade to dry
} else {
    phaser.setMix(0.5f);  // Return to normal mix
}

// Note: Still call process() to keep LFO running for smooth re-enable
```

## State Management

### Reset for New Audio Stream

```cpp
// Call when starting new audio (e.g., transport start)
phaser.reset();
// Clears all filter states and feedback, resets LFO phase
```

### Query State

```cpp
if (phaser.isPrepared()) {
    // Safe to process
    phaser.processBlock(buffer, numSamples);
}

// Check current configuration
int stages = phaser.getNumStages();
float rate = phaser.getRate();
float depth = phaser.getDepth();
// ... etc.
```

## Complete Plugin Integration Example

```cpp
class MyPhaserPlugin {
public:
    void prepareToPlay(double sampleRate, int maxBlockSize) {
        phaser_.prepare(sampleRate);
        // Configure from saved parameters
        updatePhaserFromParams();
    }

    void processBlock(float** buffer, int numChannels, int numSamples) {
        // Update tempo from host if tempo-synced
        if (tempoSyncParam_) {
            phaser_.setTempo(hostTempo_);
        }

        // Process stereo
        if (numChannels >= 2) {
            phaser_.processStereo(buffer[0], buffer[1], numSamples);
        } else {
            phaser_.processBlock(buffer[0], numSamples);
        }
    }

    void parameterChanged(int paramId, float value) {
        switch (paramId) {
            case kRateParam:
                phaser_.setRate(value);
                break;
            case kDepthParam:
                phaser_.setDepth(value);
                break;
            case kFeedbackParam:
                // Map 0-1 to -1..+1
                phaser_.setFeedback(value * 2.0f - 1.0f);
                break;
            case kMixParam:
                phaser_.setMix(value);
                break;
            case kStagesParam:
                // Map 0-1 to 2,4,6,8,10,12
                int stages = 2 + static_cast<int>(value * 5) * 2;
                phaser_.setNumStages(stages);
                break;
            // ... etc.
        }
    }

private:
    Phaser phaser_;
    bool tempoSyncParam_ = false;
    float hostTempo_ = 120.0f;
};
```

## Performance Considerations

- **12 stages at 44.1kHz**: < 0.5% CPU per channel
- **Block processing**: More efficient than sample-by-sample
- **Stereo processing**: Single call vs two mono calls
- **LFO wavetables**: Pre-computed, no runtime trig functions
- **Smoothers**: Add minimal overhead, essential for click-free automation
