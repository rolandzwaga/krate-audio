# Quickstart: BitcrusherProcessor

**Feature**: 064-bitcrusher-processor | **Date**: 2026-01-14

## Overview

BitcrusherProcessor is a Layer 2 DSP processor that combines bit depth reduction and sample rate decimation for lo-fi effects. It composes existing Layer 1 primitives (BitCrusher, SampleRateReducer, DCBlocker, OnePoleSmoother) with gain staging and dither gating.

## Basic Usage

```cpp
#include <krate/dsp/processors/bitcrusher_processor.h>

using namespace Krate::DSP;

// Create processor
BitcrusherProcessor processor;

// Prepare for 44.1kHz, max 512 samples per block
processor.prepare(44100.0, 512);

// Configure for classic 8-bit sound
processor.setBitDepth(8.0f);          // 8-bit quantization
processor.setReductionFactor(2.0f);   // Half sample rate
processor.setDitherAmount(0.5f);      // 50% dither
processor.setMix(1.0f);               // 100% wet

// Process audio in-place
processor.process(buffer, numSamples);
```

## Gain Staging

Use pre-gain (drive) to push audio harder into the bitcrusher for more aggressive artifacts:

```cpp
// Aggressive crushing with makeup gain
processor.setPreGain(12.0f);   // +12dB drive
processor.setBitDepth(4.0f);   // Extreme 4-bit
processor.setPostGain(-6.0f);  // Compensate for added level
```

## Processing Order

Control whether bit crushing or sample rate reduction happens first:

```cpp
// Default: bit crush first (quantization noise gets aliased)
processor.setProcessingOrder(ProcessingOrder::BitCrushFirst);

// Alternative: sample reduce first (stairstep gets quantized)
processor.setProcessingOrder(ProcessingOrder::SampleReduceFirst);
```

## Dither Control

TPDF dither smooths quantization artifacts. Dither is automatically gated when the signal drops below -60dB to prevent noise during silence:

```cpp
// Full dither for smoothest quantization
processor.setDitherAmount(1.0f);

// No dither for harsh digital character
processor.setDitherAmount(0.0f);

// Disable automatic gating (always apply dither)
processor.setDitherGateEnabled(false);
```

## Mix Control

Blend processed signal with dry input:

```cpp
// Full effect
processor.setMix(1.0f);

// Parallel processing (50/50 blend)
processor.setMix(0.5f);

// Bypass (efficient - skips processing)
processor.setMix(0.0f);
```

## Parameter Behavior

| Parameter | Smoothing | Behavior |
|-----------|-----------|----------|
| Pre-gain | 5ms | Smooth transition |
| Post-gain | 5ms | Smooth transition |
| Mix | 5ms | Smooth transition |
| Bit depth | None | Immediate (per spec) |
| Reduction factor | None | Immediate (per spec) |
| Processing order | None | Immediate (per spec) |

## Reset and Lifecycle

```cpp
// Reset state (e.g., on transport stop)
processor.reset();

// Re-prepare if sample rate changes
processor.prepare(newSampleRate, maxBlockSize);
```

## Performance Notes

- CPU budget: < 0.1% per mono channel at 44.1kHz (per SC-005)
- DC blocking: Automatic 10Hz highpass after processing
- Bypass optimization: mix=0 skips all wet processing
- No internal oversampling (compose externally if needed)

## File Location

```
dsp/include/krate/dsp/processors/bitcrusher_processor.h
```

## Dependencies

```cpp
#include <krate/dsp/core/db_utils.h>           // Layer 0
#include <krate/dsp/primitives/bit_crusher.h>   // Layer 1
#include <krate/dsp/primitives/sample_rate_reducer.h>  // Layer 1
#include <krate/dsp/primitives/dc_blocker.h>    // Layer 1
#include <krate/dsp/primitives/smoother.h>      // Layer 1
#include <krate/dsp/processors/envelope_follower.h>  // Layer 2
```
