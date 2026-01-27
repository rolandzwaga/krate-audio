# Quickstart Guide: TapeMachine System

**Feature**: 066-tape-machine | **Date**: 2026-01-14

This guide provides a quick reference for using the TapeMachine system.

---

## Basic Usage

### Minimal Setup

```cpp
#include <krate/dsp/systems/tape_machine.h>

using namespace Krate::DSP;

// Create and prepare
TapeMachine tape;
tape.prepare(44100.0, 512);

// Process audio
float buffer[512];
tape.process(buffer, 512);
```

### Typical Configuration

```cpp
TapeMachine tape;
tape.prepare(sampleRate, maxBlockSize);

// Configure machine character
tape.setMachineModel(MachineModel::Ampex);  // Warm American sound
tape.setTapeSpeed(TapeSpeed::IPS_15);       // Standard studio speed
tape.setTapeType(TapeType::Type456);        // Classic warm formulation

// Set saturation
tape.setSaturation(0.6f);   // 60% saturation
tape.setBias(0.1f);         // Slight asymmetry

// Add character
tape.setHeadBumpAmount(0.7f);     // Moderate head bump
tape.setHighFreqRolloffAmount(0.5f);  // Subtle HF smoothing

// Add modulation
tape.setWow(0.3f);
tape.setFlutter(0.2f);

// Add tape hiss
tape.setHiss(0.2f);  // Subtle hiss

// Process
tape.process(buffer, numSamples);
```

---

## Machine Models

### Studer (Default)

Swiss precision - tighter, more transparent.

```cpp
tape.setMachineModel(MachineModel::Studer);
// Defaults: Head bump 80/50/35 Hz, Wow 6 cents, Flutter 3 cents
```

### Ampex

American warmth - fuller lows, more colored.

```cpp
tape.setMachineModel(MachineModel::Ampex);
// Defaults: Head bump 100/60/40 Hz, Wow 9 cents, Flutter 2.4 cents
```

---

## Tape Speeds

### 7.5 IPS (Lo-Fi)

Pronounced character, thick sound.

```cpp
tape.setTapeSpeed(TapeSpeed::IPS_7_5);
// Head bump ~80-100Hz, HF rolloff ~10kHz
```

### 15 IPS (Balanced)

Most common studio speed.

```cpp
tape.setTapeSpeed(TapeSpeed::IPS_15);
// Head bump ~50-60Hz, HF rolloff ~15kHz
```

### 30 IPS (Hi-Fi)

Clean, extended response.

```cpp
tape.setTapeSpeed(TapeSpeed::IPS_30);
// Head bump ~35-40Hz, HF rolloff ~20kHz
```

---

## Tape Formulations

### Type456 (Classic Warm)

Earlier saturation, more harmonics.

```cpp
tape.setTapeType(TapeType::Type456);
// -3dB drive offset, 1.2x saturation, +0.1 bias
```

### Type900 (Hot Punchy)

Higher headroom, tight transients.

```cpp
tape.setTapeType(TapeType::Type900);
// +2dB drive offset, 1.0x saturation, 0.0 bias
```

### TypeGP9 (Modern Clean)

Highest headroom, subtle coloration.

```cpp
tape.setTapeType(TapeType::TypeGP9);
// +4dB drive offset, 0.8x saturation, -0.05 bias
```

---

## Sound Design Examples

### Vintage Tape Character

```cpp
tape.setMachineModel(MachineModel::Ampex);
tape.setTapeSpeed(TapeSpeed::IPS_7_5);
tape.setTapeType(TapeType::Type456);
tape.setSaturation(0.8f);
tape.setHeadBumpAmount(0.9f);
tape.setHighFreqRolloffAmount(0.8f);
tape.setWow(0.5f);
tape.setFlutter(0.4f);
tape.setHiss(0.4f);
```

### Clean Tape Polish

```cpp
tape.setMachineModel(MachineModel::Studer);
tape.setTapeSpeed(TapeSpeed::IPS_30);
tape.setTapeType(TapeType::TypeGP9);
tape.setSaturation(0.3f);
tape.setHeadBumpAmount(0.3f);
tape.setHighFreqRolloffAmount(0.2f);
tape.setWow(0.0f);
tape.setFlutter(0.0f);
tape.setHiss(0.0f);
```

### Lo-Fi Tape Wobble

```cpp
tape.setMachineModel(MachineModel::Ampex);
tape.setTapeSpeed(TapeSpeed::IPS_7_5);
tape.setTapeType(TapeType::Type456);
tape.setSaturation(1.0f);
tape.setHeadBumpAmount(1.0f);
tape.setHighFreqRolloffAmount(1.0f);
tape.setWow(1.0f);
tape.setWowDepth(12.0f);  // Override default for extreme wobble
tape.setFlutter(0.8f);
tape.setFlutterDepth(4.0f);
tape.setHiss(0.6f);
```

---

## Full Manual Control

Override all machine model defaults:

```cpp
// Manual frequency control
tape.setHeadBumpFrequency(75.0f);      // Custom head bump frequency
tape.setHighFreqRolloffFrequency(12000.0f);  // Custom rolloff

// Manual modulation control
tape.setWowRate(0.8f);      // Custom wow rate
tape.setWowDepth(8.0f);     // Custom wow depth
tape.setFlutterRate(10.0f); // Custom flutter rate
tape.setFlutterDepth(2.0f); // Custom flutter depth
```

---

## Parameter Ranges Reference

| Parameter | Min | Max | Unit |
|-----------|-----|-----|------|
| inputLevel | -24 | +24 | dB |
| outputLevel | -24 | +24 | dB |
| bias | -1 | +1 | normalized |
| saturation | 0 | 1 | normalized |
| headBumpAmount | 0 | 1 | normalized |
| headBumpFrequency | 30 | 120 | Hz |
| hfRolloffAmount | 0 | 1 | normalized |
| hfRolloffFrequency | 5000 | 22000 | Hz |
| hiss | 0 | 1 | normalized |
| wow | 0 | 1 | normalized |
| flutter | 0 | 1 | normalized |
| wowRate | 0.1 | 2.0 | Hz |
| flutterRate | 2.0 | 15.0 | Hz |
| wowDepth | 0 | 15 | cents |
| flutterDepth | 0 | 6 | cents |

---

## Integration Notes

### Sample Rate Support (SC-009)

```cpp
// Supports 44.1kHz to 192kHz
tape.prepare(192000.0, 512);  // High sample rate
tape.prepare(44100.0, 512);   // Standard rate
```

### Zero-Sample Blocks (SC-008)

```cpp
// Safe to call with zero samples
tape.process(buffer, 0);  // No-op, no error
```

### State Reset

```cpp
// Clear all internal state (filters, LFOs, etc.)
tape.reset();

// Required when:
// - Sample rate changes
// - Starting new audio file
// - User requests preset change with clean slate
```

### Thread Safety

```cpp
// process() is real-time safe
// - No allocations
// - No locks
// - noexcept guaranteed

// Parameter setters can be called from any thread
// - Smoothing prevents clicks
// - ~5ms transition time
```
