# Quickstart: Filter Feedback Matrix

**Feature**: 096-filter-feedback-matrix
**Date**: 2026-01-24

## Overview

FilterFeedbackMatrix creates complex resonant textures by connecting multiple SVF filters with configurable cross-feedback routing. Think of it as a Feedback Delay Network (FDN) where the delay lines also have filtering.

## Basic Usage

### Minimal Example: Two-Filter Resonator

```cpp
#include <krate/dsp/systems/filter_feedback_matrix.h>

using namespace Krate::DSP;

// Create a 2-filter matrix
FilterFeedbackMatrix<2> matrix;

// Prepare for 44.1kHz
matrix.prepare(44100.0);

// Configure filters
matrix.setFilterCutoff(0, 500.0f);   // Filter 0: 500Hz
matrix.setFilterCutoff(1, 1500.0f);  // Filter 1: 1500Hz

// Set cross-feedback (0 -> 1 and 1 -> 0)
matrix.setFeedbackAmount(0, 1, 0.5f);  // 50% from filter 0 to 1
matrix.setFeedbackAmount(1, 0, 0.5f);  // 50% from filter 1 to 0

// Process audio
float output = matrix.process(inputSample);
```

### Stereo Processing

```cpp
FilterFeedbackMatrix<4> matrix;
matrix.prepare(48000.0);

// Configure filters...

// Stereo processing (dual-mono, independent per channel)
float left = inputLeft;
float right = inputRight;
matrix.processStereo(left, right);
```

## Common Configurations

### Parallel Filters (No Feedback)

```cpp
FilterFeedbackMatrix<4> matrix;
matrix.prepare(44100.0);

// Set global feedback to 0 = parallel filter bank
matrix.setGlobalFeedback(0.0f);

// All filters receive input
matrix.setInputGains({1.0f, 1.0f, 1.0f, 1.0f});

// Equal mix of all outputs
matrix.setOutputGains({0.25f, 0.25f, 0.25f, 0.25f});

// Set different cutoffs for formant-like effect
matrix.setFilterCutoff(0, 250.0f);
matrix.setFilterCutoff(1, 700.0f);
matrix.setFilterCutoff(2, 2000.0f);
matrix.setFilterCutoff(3, 4000.0f);
```

### Serial Chain (Filter 0 -> 1 -> 2 -> 3)

```cpp
FilterFeedbackMatrix<4> matrix;
matrix.prepare(44100.0);

// Only filter 0 gets input
matrix.setInputGains({1.0f, 0.0f, 0.0f, 0.0f});

// Only filter 3 goes to output
matrix.setOutputGains({0.0f, 0.0f, 0.0f, 1.0f});

// Serial routing
matrix.setFeedbackAmount(0, 1, 1.0f);  // 0 -> 1
matrix.setFeedbackAmount(1, 2, 1.0f);  // 1 -> 2
matrix.setFeedbackAmount(2, 3, 1.0f);  // 2 -> 3
```

### Self-Oscillating Resonators

```cpp
FilterFeedbackMatrix<4> matrix;
matrix.prepare(44100.0);

// High self-feedback on each filter (diagonal of matrix)
for (size_t i = 0; i < 4; ++i) {
    matrix.setFeedbackAmount(i, i, 0.95f);  // Self-feedback
    matrix.setFilterResonance(i, 10.0f);     // High Q
}

// Per-filter tanh (FR-011) bounds output before feedback routing, preventing runaway
```

### Time-Based Resonance (With Delays)

```cpp
FilterFeedbackMatrix<4> matrix;
matrix.prepare(44100.0);

// Add delays to feedback paths for rhythmic/pitch effects
matrix.setFeedbackDelay(0, 1, 10.0f);   // 10ms delay 0->1
matrix.setFeedbackDelay(1, 0, 10.0f);   // 10ms delay 1->0
matrix.setFeedbackDelay(2, 3, 20.0f);   // 20ms delay 2->3
matrix.setFeedbackDelay(3, 2, 20.0f);   // 20ms delay 3->2

// Cross-feedback creates complex pitched resonances
matrix.setFeedbackAmount(0, 1, 0.7f);
matrix.setFeedbackAmount(1, 0, 0.7f);
matrix.setFeedbackAmount(2, 3, 0.7f);
matrix.setFeedbackAmount(3, 2, 0.7f);
```

## CPU Optimization

### Use Fewer Active Filters

```cpp
FilterFeedbackMatrix<4> matrix;  // Capacity for 4
matrix.prepare(44100.0);

// Only process 2 filters (saves CPU)
matrix.setActiveFilters(2);

// Only filters 0 and 1 are processed
// Filters 2 and 3 are skipped
```

## Edge Cases

### Zero-Length Input Buffer

```cpp
// Safe to call with zero samples - returns immediately
matrix.process(0.0f);  // OK, returns 0
```

### NaN/Inf Input

```cpp
float badInput = std::numeric_limits<float>::quiet_NaN();
float output = matrix.process(badInput);
// output = 0.0f, and internal state is reset
```

### High Feedback

```cpp
// Total feedback > 100% is safe due to per-filter tanh limiting
matrix.setFeedbackAmount(0, 0, 0.9f);
matrix.setFeedbackAmount(1, 0, 0.9f);
matrix.setFeedbackAmount(2, 0, 0.9f);
matrix.setFeedbackAmount(3, 0, 0.9f);
// Filter 0 receives 360% feedback -> tanh prevents blowup
```

## API Quick Reference

### Lifecycle
```cpp
void prepare(double sampleRate) noexcept;
void reset() noexcept;
bool isPrepared() const noexcept;
```

### Filter Configuration
```cpp
void setActiveFilters(size_t count) noexcept;
void setFilterMode(size_t index, SVFMode mode) noexcept;
void setFilterCutoff(size_t index, float hz) noexcept;
void setFilterResonance(size_t index, float q) noexcept;
```

### Feedback Matrix
```cpp
void setFeedbackAmount(size_t from, size_t to, float amount) noexcept;
void setFeedbackMatrix(const std::array<std::array<float, N>, N>& matrix) noexcept;
void setFeedbackDelay(size_t from, size_t to, float ms) noexcept;
void setGlobalFeedback(float amount) noexcept;
```

### Input/Output Routing
```cpp
void setInputGain(size_t index, float gain) noexcept;
void setOutputGain(size_t index, float gain) noexcept;
void setInputGains(const std::array<float, N>& gains) noexcept;
void setOutputGains(const std::array<float, N>& gains) noexcept;
```

### Processing
```cpp
float process(float input) noexcept;
void processStereo(float& left, float& right) noexcept;
```
