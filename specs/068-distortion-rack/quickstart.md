# Quickstart: DistortionRack

**Feature**: 068-distortion-rack | **Date**: 2026-01-15

## Overview

DistortionRack is a Layer 3 system component that provides a 4-slot chainable distortion processor rack. Each slot can hold any supported distortion processor with independent enable, mix, and gain controls.

---

## Basic Usage

```cpp
#include <krate/dsp/systems/distortion_rack.h>

using namespace Krate::DSP;

// Create the rack
DistortionRack rack;

// Prepare for 44.1 kHz, 512 sample blocks
rack.prepare(44100.0, 512);

// Configure slot 0 as a tube stage
rack.setSlotType(0, SlotType::TubeStage);
rack.setSlotEnabled(0, true);
rack.setSlotMix(0, 0.7f);      // 70% wet
rack.setSlotGain(0, 3.0f);     // +3 dB makeup

// Configure slot 1 as a diode clipper
rack.setSlotType(1, SlotType::DiodeClipper);
rack.setSlotEnabled(1, true);
rack.setSlotMix(1, 1.0f);      // 100% wet
rack.setSlotGain(1, 0.0f);     // Unity gain

// Process audio
void processAudio(float* left, float* right, size_t numSamples) {
    rack.process(left, right, numSamples);
}
```

---

## Slot Types

| SlotType | Processor | Character |
|----------|-----------|-----------|
| `Empty` | None (bypass) | Pass-through |
| `Waveshaper` | Waveshaper | Generic waveshaping curves |
| `TubeStage` | TubeStage | Warm tube saturation |
| `DiodeClipper` | DiodeClipper | Analog diode clipping |
| `Wavefolder` | WavefolderProcessor | Complex harmonics |
| `TapeSaturator` | TapeSaturator | Magnetic tape character |
| `Fuzz` | FuzzProcessor | Fuzz Face style |
| `Bitcrusher` | BitcrusherProcessor | Lo-fi bit reduction |

---

## Fine-Grained Processor Control

Access the underlying processor for detailed parameter control:

```cpp
// Set up tube stage with custom parameters
rack.setSlotType(0, SlotType::TubeStage);
if (auto* tube = rack.getProcessor<TubeStage>(0)) {
    tube->setInputGain(6.0f);       // +6 dB input
    tube->setBias(0.3f);            // Asymmetric saturation
    tube->setSaturationAmount(0.8f); // 80% saturation
}

// Configure diode clipper
rack.setSlotType(1, SlotType::DiodeClipper);
if (auto* diode = rack.getProcessor<DiodeClipper>(1)) {
    diode->setDiodeType(DiodeType::Germanium);
    diode->setTopology(ClipperTopology::Asymmetric);
    diode->setDrive(12.0f);         // +12 dB drive
}

// Configure wavefolder
rack.setSlotType(2, SlotType::Wavefolder);
if (auto* folder = rack.getProcessor<WavefolderProcessor>(2)) {
    folder->setModel(WavefolderModel::Buchla259);
    folder->setFoldAmount(3.0f);
    folder->setSymmetry(0.2f);      // Slight even harmonics
}
```

---

## Stereo Processing

The rack processes stereo audio. Each slot contains two independent mono processors (L/R):

```cpp
// Access left channel processor
auto* tubeL = rack.getProcessor<TubeStage>(0, 0);

// Access right channel processor
auto* tubeR = rack.getProcessor<TubeStage>(0, 1);

// By default, both channels have identical settings
// For stereo width, configure them differently:
if (tubeL && tubeR) {
    tubeL->setBias(-0.1f);
    tubeR->setBias(0.1f);  // Different bias for stereo interest
}
```

---

## Oversampling

Enable oversampling for reduced aliasing on aggressive saturation:

```cpp
// Set 4x oversampling before prepare()
rack.setOversamplingFactor(4);
rack.prepare(44100.0, 512);

// Check latency introduced
size_t latency = rack.getLatency();  // Samples of delay
```

| Factor | Effective Rate | Typical Latency | Use Case |
|--------|----------------|-----------------|----------|
| 1 | 44.1 kHz | 0 | Light saturation |
| 2 | 88.2 kHz | ~15 samples | Moderate saturation |
| 4 | 176.4 kHz | ~30 samples | Heavy distortion |

---

## DC Blocking

Per-slot DC blocking is enabled by default to prevent DC offset accumulation:

```cpp
// Check if DC blocking is enabled
bool dcEnabled = rack.getDCBlockingEnabled();  // true by default

// Disable DC blocking (not recommended)
rack.setDCBlockingEnabled(false);
```

---

## Slot Mix and Gain

Each slot has independent mix (dry/wet) and gain controls:

```cpp
// Parallel distortion: 50% dry + 50% wet
rack.setSlotMix(0, 0.5f);

// Gain staging: attenuate hot signals
rack.setSlotGain(0, -6.0f);  // -6 dB output

// Full wet with makeup gain
rack.setSlotMix(1, 1.0f);
rack.setSlotGain(1, 6.0f);   // +6 dB makeup
```

---

## Preset Examples

### Classic Rock Crunch

```cpp
void setupClassicRock(DistortionRack& rack) {
    rack.setOversamplingFactor(2);
    rack.prepare(sampleRate, blockSize);

    // Tube preamp stage
    rack.setSlotType(0, SlotType::TubeStage);
    rack.setSlotEnabled(0, true);
    rack.setSlotMix(0, 1.0f);
    rack.setSlotGain(0, 0.0f);
    if (auto* tube = rack.getProcessor<TubeStage>(0)) {
        tube->setInputGain(12.0f);
        tube->setSaturationAmount(0.6f);
    }
}
```

### Modern High Gain

```cpp
void setupHighGain(DistortionRack& rack) {
    rack.setOversamplingFactor(4);
    rack.prepare(sampleRate, blockSize);

    // Stage 1: Tube preamp
    rack.setSlotType(0, SlotType::TubeStage);
    rack.setSlotEnabled(0, true);
    if (auto* tube = rack.getProcessor<TubeStage>(0)) {
        tube->setInputGain(18.0f);
        tube->setSaturationAmount(0.9f);
    }

    // Stage 2: Diode clipper for tightness
    rack.setSlotType(1, SlotType::DiodeClipper);
    rack.setSlotEnabled(1, true);
    rack.setSlotMix(1, 0.3f);  // Blend with tube
    if (auto* diode = rack.getProcessor<DiodeClipper>(1)) {
        diode->setDiodeType(DiodeType::Silicon);
        diode->setDrive(6.0f);
    }
}
```

### Lo-Fi Effect

```cpp
void setupLoFi(DistortionRack& rack) {
    rack.setOversamplingFactor(1);  // No oversampling for aliasing
    rack.prepare(sampleRate, blockSize);

    // Tape saturation for warmth
    rack.setSlotType(0, SlotType::TapeSaturator);
    rack.setSlotEnabled(0, true);
    if (auto* tape = rack.getProcessor<TapeSaturator>(0)) {
        tape->setModel(TapeModel::Simple);
        tape->setSaturation(0.7f);
    }

    // Bitcrusher for lo-fi character
    rack.setSlotType(1, SlotType::Bitcrusher);
    rack.setSlotEnabled(1, true);
    rack.setSlotMix(1, 0.4f);  // Subtle blend
    if (auto* crush = rack.getProcessor<BitcrusherProcessor>(1)) {
        crush->setBitDepth(12.0f);
        crush->setReductionFactor(2.0f);
    }
}
```

---

## Real-Time Safety

The following methods are real-time safe and can be called from the audio thread:

- `process()`
- `reset()`
- `setSlotEnabled()`
- `setSlotMix()`
- `setSlotGain()`
- `setDCBlockingEnabled()`
- All getter methods

The following methods allocate and should only be called from the control thread:

- `prepare()`
- `setSlotType()`
- `setOversamplingFactor()` (requires re-calling prepare())

---

## Integration with VST3

```cpp
// In Processor::setActive()
tresult PLUGIN_API Processor::setActive(TBool state) {
    if (state) {
        rack_.prepare(processSetup.sampleRate, processSetup.maxSamplesPerBlock);
    } else {
        rack_.reset();
    }
    return kResultOk;
}

// In Processor::process()
tresult PLUGIN_API Processor::process(ProcessData& data) {
    // Handle parameter changes first
    processParameterChanges(data.inputParameterChanges);

    // Process audio
    if (data.numInputs > 0 && data.numOutputs > 0) {
        float* left = data.outputs[0].channelBuffers32[0];
        float* right = data.outputs[0].channelBuffers32[1];

        // Copy input to output if different buffers
        // ...

        rack_.process(left, right, data.numSamples);
    }
    return kResultOk;
}
```
