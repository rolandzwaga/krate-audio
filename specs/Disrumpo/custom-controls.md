# Disrumpo - Custom VSTGUI Controls

**Related Documents:**
- [spec.md](spec.md) - Core requirements specification
- [plan.md](plan.md) - System architecture and development roadmap
- [tasks.md](tasks.md) - Task breakdown and milestones
- [ui-mockups.md](ui-mockups.md) - UI layout specifications
- [dsp-details.md](dsp-details.md) - DSP implementation details

---

## Overview

Disrumpo requires several custom VSTGUI controls that are not available in the standard VSTGUI library. This document provides detailed specifications for implementing these controls.

| Control | Purpose | Complexity |
|---------|---------|------------|
| SpectrumDisplay | Real-time FFT spectrum with band visualization | High |
| MorphPad | 2D XY pad for morph position control | Medium |
| BandStrip | Per-band control group with expand/collapse | Medium |
| SweepIndicator | Sweep position overlay on spectrum | Low |

### VSTGUI API Version

This specification targets **VSTGUI 4.11+** event-based API.

**Key event properties:**
- `event.mousePosition` - Mouse position (CPoint)
- `event.buttonState` - Which buttons pressed (`.isLeft()`, `.isRight()`)
- `event.modifiers` - Modifier keys (`.has(ModifierKey::Shift)`, `.has(ModifierKey::Alt)`)
- `event.consumed = true` - Mark event as handled
- `event.clickCount` - For double-click detection

**Existing VSTGUI controls to reference:**
- `CXYPad` (`vstgui/lib/controls/cxypad.h`) - XY pad with value packing
- `CVuMeter` (`vstgui/lib/controls/cvumeter.h`) - Level metering

---

## 1. SpectrumDisplay Control

### 1.1 Purpose

A real-time spectrum analyzer that visualizes the audio signal's frequency content while also serving as an interactive control for:
- Adjusting crossover frequencies between bands
- Selecting bands for editing
- Displaying the sweep position overlay

### 1.2 Visual Design

```
┌─────────────────────────────────────────────────────────────────────────┐
│                                                                         │
│  0dB ─┬─────────────────────────────────────────────────────────────────│
│       │    ████                                                         │
│       │   █████                         ███                             │
│       │  ███████        ███            █████                            │
│ -24dB │ █████████      █████          ███████         ██                │
│       │██████████     ███████        █████████       ████               │
│ -48dB │███████████   █████████      ███████████     ██████              │
│       │████████████ ███████████    █████████████   ████████             │
│ -72dB │█████████████████████████  ███████████████ ██████████            │
│       └──────┬──────────┬───────────────┬──────────────┬────────────────│
│              │          │               │              │                │
│          [Divider 1] [Divider 2]    [Divider 3]   [Freq Scale]         │
│              │          │               │                               │
│  ┌───────────┴──────────┴───────────────┴──────────────────────────────┐│
│  │   20Hz      200Hz        2kHz          8kHz           20kHz         ││
│  │        BAND 1      BAND 2       BAND 3         BAND 4               ││
│  │       <200Hz     200-2kHz     2k-8kHz         >8kHz                 ││
│  └──────────────────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────────────┘
```

### 1.3 Components

#### 1.3.1 Spectrum Analyzer Layer

| Property | Specification |
|----------|---------------|
| FFT Size | 2048 samples (configurable: 512, 1024, 2048, 4096) |
| Scope Size | 512 display points (decimated from FFT bins) |
| Window Function | Hann (default), Blackman-Harris optional |
| Update Rate | ~30 fps recommended (16-33ms timer interval) |
| Frequency Scale | Logarithmic (20 Hz - 20 kHz) |
| Amplitude Scale | dB scale (-96 dB to 0 dB, configurable range) |
| Smoothing | Peak hold with decay, configurable attack/release |
| Display Mode | Filled bars, line, or filled area |

**FFT Processing Pipeline:**

The spectrum analyzer uses a standard FFT pipeline:
1. **Audio Thread**: Pushes samples to FIFO ring buffer
2. **UI Thread**: When enough samples available, copies to FFT buffer
3. **Windowing**: Apply Hann window to reduce spectral leakage
4. **FFT**: Compute forward FFT (real-to-complex)
5. **Magnitude**: Calculate magnitude from complex bins
6. **Decimation**: Reduce FFT bins to scope size for display
7. **Smoothing**: Apply attack/release smoothing
8. **Rendering**: Draw the spectrum

```cpp
struct SpectrumConfig {
    // FFT configuration
    static constexpr int kFFTOrder = 11;                    // 2^11 = 2048
    static constexpr int kFFTSize = 1 << kFFTOrder;         // 2048 samples
    static constexpr int kScopeSize = 512;                  // Display resolution

    // Smoothing (0-1, higher = faster response)
    float smoothingAttack = 0.9f;   // Fast attack for transients
    float smoothingRelease = 0.7f;  // Slower release for visual appeal

    // Peak hold
    float peakHoldTime = 1.0f;      // Seconds before peak starts falling
    float peakFallRate = 12.0f;     // dB per second
    bool showPeakHold = true;

    // Display
    bool fillSpectrum = true;       // Filled vs line display
    float minDb = -96.0f;           // Floor level
    float maxDb = 0.0f;             // Ceiling level
};
```

**FIFO Buffer Pattern:**
```cpp
// Audio thread pushes samples into FIFO
// UI thread reads when enough samples accumulated

class SpectrumFIFO {
public:
    void pushSample(float sample) {
        if (fifoIndex_ < kFFTSize) {
            fifo_[fifoIndex_++] = sample;
        }
        if (fifoIndex_ == kFFTSize) {
            // Signal that FFT data is ready
            nextFFTBlockReady_.store(true, std::memory_order_release);
        }
    }

    bool isNextBlockReady() const {
        return nextFFTBlockReady_.load(std::memory_order_acquire);
    }

    void copyToFFTBuffer(float* dest) {
        std::copy(fifo_.begin(), fifo_.end(), dest);
        nextFFTBlockReady_.store(false, std::memory_order_release);
        fifoIndex_ = 0;
    }

private:
    std::array<float, kFFTSize> fifo_{};
    int fifoIndex_ = 0;
    std::atomic<bool> nextFFTBlockReady_{false};
};
```

**Window Function (Hann):**
```cpp
// Apply Hann window before FFT to reduce spectral leakage
// This is essential for clean spectrum visualization
void applyHannWindow(float* data, int size) {
    for (int i = 0; i < size; ++i) {
        float multiplier = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (size - 1)));
        data[i] *= multiplier;
    }
}

// Pre-compute window coefficients for efficiency
class HannWindow {
public:
    explicit HannWindow(int size) : coefficients_(size) {
        for (int i = 0; i < size; ++i) {
            coefficients_[i] = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (size - 1)));
        }
    }

    void apply(float* data) const {
        for (size_t i = 0; i < coefficients_.size(); ++i) {
            data[i] *= coefficients_[i];
        }
    }

private:
    std::vector<float> coefficients_;
};
```

**Magnitude Calculation and dB Normalization:**
```cpp
// After FFT, convert complex bins to dB magnitude
// Uses Krate::DSP::Complex from <krate/dsp/primitives/fft.h>
void calculateMagnitudes(const Krate::DSP::Complex* fftData,
                         float* scopeData, int numBins, int scopeSize) {
    for (int i = 0; i < scopeSize; ++i) {
        // Map scope index to FFT bin with logarithmic skew
        float skewedProportion = 1.0f - std::exp(
            std::log(1.0f - static_cast<float>(i) / scopeSize) * 0.2f);
        int fftBinIndex = static_cast<int>(skewedProportion * numBins);
        fftBinIndex = std::clamp(fftBinIndex, 0, numBins - 1);

        // Get magnitude using Krate::DSP::Complex::magnitude()
        float magnitude = fftData[fftBinIndex].magnitude();

        // Convert to normalized dB (0.0 to 1.0)
        float level = magnitude > 0.0f
            ? std::clamp((20.0f * std::log10(magnitude) - minDb_) / (maxDb_ - minDb_), 0.0f, 1.0f)
            : 0.0f;

        // Apply smoothing (attack for rising, release for falling)
        scopeData[i] = level > scopeData[i]
            ? level * smoothingAttack_ + scopeData[i] * (1.0f - smoothingAttack_)
            : level * (1.0f - smoothingRelease_) + scopeData[i] * smoothingRelease_;
    }
}
```

**Frequency-to-Pixel Mapping:**

Two approaches are commonly used for logarithmic frequency display:

**Approach 1: Standard Log10 Mapping** (used for crossover dividers, frequency labels)
```cpp
// Standard logarithmic mapping - maps frequency to pixel position
float freqToX(float freq, float width) const {
    const float minFreq = 20.0f;
    const float maxFreq = 20000.0f;
    float logMin = std::log10(minFreq);
    float logMax = std::log10(maxFreq);
    float logFreq = std::log10(std::clamp(freq, minFreq, maxFreq));
    return width * (logFreq - logMin) / (logMax - logMin);
}

float xToFreq(float x, float width) const {
    const float minFreq = 20.0f;
    const float maxFreq = 20000.0f;
    float logMin = std::log10(minFreq);
    float logMax = std::log10(maxFreq);
    float logFreq = logMin + (x / width) * (logMax - logMin);
    return std::pow(10.0f, logFreq);
}
```

**Approach 2: Exponential Skew** (used for FFT bin-to-scope decimation)
```cpp
// Skews the linear index to emphasize lower frequencies
// The 0.2f factor controls skew amount (lower = more bass emphasis)
float skewedProportion(int scopeIndex, int scopeSize) {
    return 1.0f - std::exp(
        std::log(1.0f - static_cast<float>(scopeIndex) / scopeSize) * 0.2f);
}
```

Both approaches produce perceptually similar results. The standard log10 mapping is clearer for interactive elements (crossovers, hit testing), while the exponential skew is computationally efficient for batch FFT bin mapping.

#### 1.3.2 Band Region Layer

Each frequency band is rendered as a colored region behind the spectrum.

| Property | Specification |
|----------|---------------|
| Colors | Band 1-4: #FF6B35, #4ECDC4, #95E86B, #C792EA (from color scheme) |
| | Band 5-8: #FFCB6B, #FF5370, #89DDFF, #F78C6C |
| Opacity | 20% normal, 40% when band selected, 60% when band hovered |
| Border | 1px solid at band boundaries (crossover positions) |
| Label | Band number centered in region, frequency range below |

**Band State:**
```cpp
struct BandRegion {
    int bandIndex;              // 0-7
    float lowFreqHz;            // Lower crossover frequency
    float highFreqHz;           // Upper crossover frequency
    bool selected = false;      // Currently editing this band
    bool hovered = false;       // Mouse over this band
    bool solo = false;          // Band is soloed
    bool mute = false;          // Band is muted
    bool bypass = false;        // Band is bypassed
};
```

**Visual States:**
- **Normal**: Background color at 20% opacity
- **Hovered**: Background color at 40% opacity, cursor changes to pointer
- **Selected**: Background color at 60% opacity, white border highlight
- **Muted**: Grayed out (desaturated), strikethrough on label
- **Soloed**: Full saturation, other bands dimmed
- **Bypassed**: Dashed border

#### 1.3.3 Crossover Divider Layer

Draggable vertical lines at crossover frequencies.

| Property | Specification |
|----------|---------------|
| Visual | 2px vertical line, color matches adjacent band's accent |
| Hit Area | 10px wide invisible hit zone for easier grabbing |
| Cursor | `ew-resize` (horizontal resize) when hovering |
| Constraint | Minimum 0.5 octave spacing between adjacent dividers |
| Feedback | Frequency tooltip while dragging |

**Divider Interaction:**
```cpp
struct CrossoverDivider {
    int index;                  // 0 to (numBands - 2)
    float frequencyHz;          // Current crossover frequency
    bool dragging = false;
    bool hovered = false;

    // Constraints
    float minFreqHz;            // Cannot go below this (previous divider + 0.5 octave)
    float maxFreqHz;            // Cannot go above this (next divider - 0.5 octave)
};

// Constraint calculation
float getMinFrequency(int dividerIndex, const std::vector<float>& crossovers) {
    if (dividerIndex == 0) {
        return 20.0f * std::pow(2.0f, 0.5f);  // 20Hz + 0.5 octave
    }
    return crossovers[dividerIndex - 1] * std::pow(2.0f, 0.5f);
}

float getMaxFrequency(int dividerIndex, const std::vector<float>& crossovers) {
    if (dividerIndex == crossovers.size() - 1) {
        return 20000.0f / std::pow(2.0f, 0.5f);  // 20kHz - 0.5 octave
    }
    return crossovers[dividerIndex + 1] / std::pow(2.0f, 0.5f);
}
```

#### 1.3.4 Sweep Indicator Layer (Audio-Synchronized Overlay)

When sweep is enabled, shows the sweep position and width in real-time, synchronized with the actual audio processing.

| Property | Specification |
|----------|---------------|
| Visual | Semi-transparent Gaussian bell curve overlay |
| Color | Primary: White at 40% opacity, Edge glow: Accent color at 20% |
| Center Line | 2px solid vertical line at sweep center frequency |
| Width Indicator | Gaussian falloff showing sweep width (σ = width/2) |
| Intensity Indicator | Vertical height of curve proportional to sweep intensity |
| Animation | Smooth 60fps updates synchronized to audio position |

**Visual Design:**
```
                    Sweep Width (2σ)
               ◄────────────────────►
                       ╱╲ ← Peak = Intensity
                      ╱  ╲
                     ╱    ╲
                    ╱      ╲
                   ╱        ╲
──────────────────╱──────────╲────────────────────
                      │
                 Center Freq
                 (e.g. 2.4kHz)

With intensity bands affected visualization:
┌────────────────────────────────────────────────────┐
│  ░░░░░░░░░░░░░░░░████████░░░░░░░░░░░░░░░░░░░░░░░░ │
│  ░░░░░░░░░░░░░░████████████░░░░░░░░░░░░░░░░░░░░░░ │
│  ░░░░░░░░░░░░████████████████░░░░░░░░░░░░░░░░░░░░ │
│ Band 1 │ Band 2 │▓▓BAND 3▓▓│ Band 4 │   Band 5   │
│  (dim) │ (dim)  │(affected)│ (dim)  │   (dim)    │
└────────────────────────────────────────────────────┘
   ░ = dimmed bands    ▓ = sweep-affected region
```

##### Audio-Visual Synchronization

**The sweep visualization MUST be sample-accurate to the audio processing.**

| Aspect | Implementation |
|--------|----------------|
| Data Source | Processor calculates actual sweep position per audio block |
| Update Rate | Every audio buffer (typically 64-512 samples) |
| Communication | Lock-free SPSC queue from audio thread to UI |
| Latency Compensation | UI subtracts audio output latency for visual alignment |
| Interpolation | UI interpolates between updates for smooth 60fps display |

**Data Flow:**
```
┌─────────────────────────────────────────────────────────────────────┐
│                        AUDIO THREAD                                   │
│                                                                       │
│  ┌─────────────┐    ┌──────────────────┐    ┌────────────────────┐  │
│  │ Modulation  │───►│ SweepProcessor   │───►│ Sweep Position     │  │
│  │ Sources     │    │ calculates       │    │ Ring Buffer        │  │
│  │ (LFO, Env)  │    │ center freq      │    │ (per-block data)   │  │
│  └─────────────┘    └──────────────────┘    └────────────────────┘  │
│                                                       │              │
└───────────────────────────────────────────────────────┼──────────────┘
                                                        │
                    ┌───────────────────────────────────┘
                    │  Lock-free read
                    ▼
┌─────────────────────────────────────────────────────────────────────┐
│                         UI THREAD                                     │
│                                                                       │
│  ┌──────────────────┐    ┌─────────────────┐    ┌────────────────┐  │
│  │ Read latest      │───►│ Interpolate for │───►│ Render sweep   │  │
│  │ sweep position   │    │ 60fps display   │    │ indicator      │  │
│  │ from buffer      │    │ (lerp or spline)│    │ overlay        │  │
│  └──────────────────┘    └─────────────────┘    └────────────────┘  │
│                                                                       │
└─────────────────────────────────────────────────────────────────────┘
```

**Sweep Position Data Structure:**
```cpp
struct SweepPositionData {
    float centerFreqHz;      // Current sweep center frequency
    float widthOctaves;      // Sweep width in octaves
    float intensity;         // 0.0-1.0, how much the sweep affects bands
    uint64_t samplePosition; // Sample count for timing sync
    bool enabled;            // Sweep on/off state
};

// Ring buffer for audio → UI communication
// Sized to hold ~100ms of position data at 44.1kHz
// = ~4 entries at 1024 sample blocks
static constexpr int kSweepBufferSize = 8;

class SweepPositionBuffer {
public:
    // Called from audio thread after each block
    void push(const SweepPositionData& data);

    // Called from UI thread
    bool pop(SweepPositionData& data);

    // Get interpolated position for current display time
    SweepPositionData getInterpolatedPosition(uint64_t targetSample) const;

private:
    std::array<SweepPositionData, kSweepBufferSize> buffer_;
    std::atomic<int> writeIndex_{0};
    std::atomic<int> readIndex_{0};
};
```

**Latency Compensation:**
```cpp
// In SpectrumDisplay::onTimer()
void SpectrumDisplay::updateSweepPosition() {
    // Get current playback position from host
    uint64_t currentPlaybackSample = getHostPlaybackPosition();

    // Subtract output latency to align visual with what user hears
    uint64_t visualTargetSample = currentPlaybackSample - outputLatencySamples_;

    // Get interpolated sweep position for this target time
    SweepPositionData pos = sweepBuffer_.getInterpolatedPosition(visualTargetSample);

    // Update visual state
    sweepCenterFreq_ = pos.centerFreqHz;
    sweepWidth_ = pos.widthOctaves;
    sweepIntensity_ = pos.intensity;

    invalid();  // Request redraw
}
```

##### Sweep Animation Modes

The visual behavior changes based on sweep modulation source:

| Mode | Visual Behavior |
|------|-----------------|
| **Manual** | Sweep follows knob position, immediate response |
| **LFO-Driven** | Smooth oscillating motion, shows full LFO cycle preview |
| **Envelope-Driven** | Reactive motion following input dynamics |
| **Automation** | Follows DAW automation lane precisely |

**LFO Preview Mode (Optional Enhancement):**
When sweep is LFO-driven, show a faint "ghost" trail indicating the upcoming sweep path:
```
┌────────────────────────────────────────────────────┐
│                                                    │
│         ┄┄┄○────────●═══════════○┄┄┄              │
│        past    current      future                 │
│              ◄─────────────────►                   │
│                  LFO cycle                         │
└────────────────────────────────────────────────────┘
  ○ = ghost positions   ● = current position
  ┄ = faint trail       ═ = upcoming path
```

##### Band Intensity Visualization

When sweep is active, bands within the sweep's influence should visually respond:

| Distance from Sweep | Visual Effect |
|--------------------|---------------|
| Within 0.5σ (center) | Full intensity highlight, slight glow |
| Within 1σ | 75% highlight |
| Within 2σ | 50% highlight |
| Beyond 2σ | Normal/dimmed appearance |

**Gaussian Intensity Calculation:**
```cpp
float calculateBandIntensity(float bandCenterFreq, float sweepCenterFreq,
                              float sweepWidthOctaves, float sweepIntensity) {
    // Convert to log space for perceptually uniform distances
    float bandLog = std::log2(bandCenterFreq);
    float sweepLog = std::log2(sweepCenterFreq);

    // Distance in octaves
    float distanceOctaves = std::abs(bandLog - sweepLog);

    // Gaussian falloff (σ = width/2)
    float sigma = sweepWidthOctaves / 2.0f;
    float gaussian = std::exp(-(distanceOctaves * distanceOctaves) /
                              (2.0f * sigma * sigma));

    return gaussian * sweepIntensity;
}
```

#### 1.3.5 Frequency Scale Layer

Bottom axis showing frequency labels.

| Property | Specification |
|----------|---------------|
| Labels | 20, 50, 100, 200, 500, 1k, 2k, 5k, 10k, 20k Hz |
| Font | Secondary text color (#8888AA), 10px |
| Grid Lines | Optional faint vertical lines at label positions |

### 1.4 Interaction Behavior

#### 1.4.1 Mouse Events

| Action | Behavior |
|--------|----------|
| **Hover over band** | Highlight band region, show band info tooltip |
| **Click on band** | Select band for editing, notify controller |
| **Hover over divider** | Change cursor to resize, highlight divider |
| **Drag divider** | Move crossover frequency, show frequency tooltip |
| **Double-click divider** | Reset to default frequency distribution |
| **Right-click** | Context menu (solo/mute/bypass band, reset crossovers) |
| **Scroll wheel** | Zoom frequency range (future feature) |
| **Shift+drag divider** | Fine adjustment (10x precision) |

#### 1.4.2 Keyboard Events (when focused)

| Key | Behavior |
|-----|----------|
| **Tab** | Cycle through bands |
| **Left/Right** | Adjust selected divider frequency |
| **Shift+Left/Right** | Fine adjust selected divider |
| **S** | Toggle solo on selected band |
| **M** | Toggle mute on selected band |
| **B** | Toggle bypass on selected band |
| **R** | Reset crossovers to equal distribution |

### 1.5 Data Flow

The SpectrumDisplay receives two types of data from the audio thread:

1. **Spectrum Data** - Audio samples for FFT analysis
2. **Sweep Position Data** - Current sweep position synchronized to audio timing

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                            AUDIO THREAD                                       │
│                                                                               │
│  ┌─────────────┐                              ┌─────────────────────────┐    │
│  │ Audio Input │──────────────────────────────► Spectrum Ring Buffer    │    │
│  └─────────────┘                              │ (2048 samples, SPSC)    │    │
│         │                                      └─────────────────────────┘    │
│         ▼                                                │                    │
│  ┌─────────────────┐    ┌───────────────┐               │                    │
│  │ Sweep Processor │───►│ Sweep Position│               │                    │
│  │ (with mods)     │    │ Ring Buffer   │               │                    │
│  └─────────────────┘    │ (8 entries)   │               │                    │
│                          └───────────────┘               │                    │
│                                  │                       │                    │
└──────────────────────────────────┼───────────────────────┼────────────────────┘
                                   │                       │
                    Lock-free read │                       │ Lock-free read
                                   ▼                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                             UI THREAD                                         │
│                                                                               │
│  ┌──────────────────────────┐         ┌─────────────────────────────────┐   │
│  │ Sweep Position Pipeline  │         │ Spectrum Analysis Pipeline      │   │
│  │                          │         │                                 │   │
│  │ ┌────────────────────┐   │         │ ┌─────────────┐ ┌────────────┐  │   │
│  │ │ Read sweep buffer  │   │         │ │ Read audio  │►│ Apply FFT  │  │   │
│  │ └────────┬───────────┘   │         │ │ samples     │ │ + Window   │  │   │
│  │          ▼               │         │ └─────────────┘ └─────┬──────┘  │   │
│  │ ┌────────────────────┐   │         │                       ▼         │   │
│  │ │ Compensate for     │   │         │              ┌────────────────┐ │   │
│  │ │ output latency     │   │         │              │ Log magnitude  │ │   │
│  │ └────────┬───────────┘   │         │              │ + Smoothing    │ │   │
│  │          ▼               │         │              └───────┬────────┘ │   │
│  │ ┌────────────────────┐   │         │                      │          │   │
│  │ │ Interpolate for    │   │         └──────────────────────┼──────────┘   │
│  │ │ 60fps display      │   │                                │              │
│  │ └────────┬───────────┘   │                                │              │
│  │          │               │                                │              │
│  └──────────┼───────────────┘                                │              │
│             │                                                │              │
│             ▼                                                ▼              │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │                     SpectrumDisplay::draw()                            │  │
│  │  1. Draw band regions (affected by sweep intensity)                   │  │
│  │  2. Draw spectrum bars                                                │  │
│  │  3. Draw sweep indicator overlay (synchronized to audio)              │  │
│  │  4. Draw crossover dividers                                           │  │
│  │  5. Draw frequency scale                                              │  │
│  └───────────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────────┘
```

**Critical Timing Requirement:**
The sweep indicator position MUST be synchronized with the audio the user is hearing. This means:
- Accounting for audio output latency (typically 5-50ms depending on DAW/driver settings)
- Interpolating sweep position between audio buffer updates for smooth animation
- Never displaying a sweep position that is ahead of or significantly behind the audio

### 1.6 VSTGUI Implementation

```cpp
// plugins/Disrumpo/src/controller/views/spectrum_display.h
#pragma once

#include "vstgui/lib/cview.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/events.h"

#include <array>
#include <vector>
#include <atomic>
#include <memory>

namespace Disrumpo {

class SpectrumDisplay : public VSTGUI::CView {
public:
    // View identifier for uidesc
    static constexpr auto kViewIdentifier = "SpectrumDisplay";

    SpectrumDisplay(const VSTGUI::CRect& size);
    ~SpectrumDisplay() override;

    //--- Configuration ---
    void setNumBands(int numBands);
    void setCrossoverFrequency(int dividerIndex, float frequencyHz);
    void setSelectedBand(int bandIndex);
    void setBandState(int bandIndex, bool solo, bool mute, bool bypass);
    void setSweepEnabled(bool enabled);
    void setSweepPosition(float centerFreqHz, float widthOctaves, float intensity);

    //--- Audio-synchronized sweep (preferred method) ---
    void setSweepPositionBuffer(SweepPositionBuffer* buffer);
    void setOutputLatency(uint64_t latencySamples);

    //--- Spectrum Data (called from message handler, not audio thread) ---
    void updateSpectrum(const float* magnitudes, int numBins, float sampleRate);

    //--- CView overrides ---
    void draw(VSTGUI::CDrawContext* context) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& event) override;
    void onMouseMoveEvent(VSTGUI::MouseMoveEvent& event) override;
    void onMouseUpEvent(VSTGUI::MouseUpEvent& event) override;
    void onMouseWheelEvent(VSTGUI::MouseWheelEvent& event) override;
    void onKeyboardEvent(VSTGUI::KeyboardEvent& event) override;

    //--- Idle callback for animation (alternative to timer) ---
    void onIdle() override;
    bool attached(CView* parent) override;
    bool removed(CView* parent) override;

    //--- Listener interface ---
    class Listener {
    public:
        virtual ~Listener() = default;
        virtual void onCrossoverChanged(int dividerIndex, float frequencyHz) = 0;
        virtual void onBandSelected(int bandIndex) = 0;
        virtual void onBandStateChanged(int bandIndex, bool solo, bool mute, bool bypass) = 0;
    };

    void setListener(Listener* listener) { listener_ = listener; }

private:
    //--- Drawing helpers ---
    void drawBandRegions(VSTGUI::CDrawContext* context);
    void drawSpectrum(VSTGUI::CDrawContext* context);
    void drawCrossoverDividers(VSTGUI::CDrawContext* context);
    void drawSweepIndicator(VSTGUI::CDrawContext* context);  // Gaussian overlay
    void drawFrequencyScale(VSTGUI::CDrawContext* context);
    void drawTooltip(VSTGUI::CDrawContext* context);

    //--- Sweep helpers ---
    void updateSweepFromBuffer();  // Called in onTimer(), reads from buffer
    float calculateBandSweepIntensity(int bandIndex) const;  // For band highlighting

    //--- Coordinate conversion ---
    float freqToX(float freqHz) const;
    float xToFreq(float x) const;
    float dbToY(float db) const;

    //--- Hit testing ---
    int hitTestDivider(const VSTGUI::CPoint& where) const;
    int hitTestBand(const VSTGUI::CPoint& where) const;

    //--- State ---
    int numBands_ = 4;
    int selectedBand_ = -1;
    int hoveredBand_ = -1;
    int hoveredDivider_ = -1;
    int draggingDivider_ = -1;

    std::vector<float> crossoverFrequencies_;  // N-1 crossovers for N bands
    std::vector<bool> bandSolo_;
    std::vector<bool> bandMute_;
    std::vector<bool> bandBypass_;

    //--- Sweep state (synchronized to audio) ---
    bool sweepEnabled_ = false;
    float sweepCenterFreq_ = 1000.0f;      // Current display position (interpolated)
    float sweepWidth_ = 1.5f;
    float sweepIntensity_ = 0.5f;

    //--- Sweep synchronization ---
    SweepPositionBuffer* sweepPositionBuffer_ = nullptr;  // Shared with processor
    uint64_t outputLatencySamples_ = 0;                   // From host for compensation
    SweepPositionData lastSweepData_;                     // For interpolation
    SweepPositionData targetSweepData_;                   // Next position to interpolate to
    float sweepInterpolationProgress_ = 1.0f;             // 0.0 = at last, 1.0 = at target

    //--- Spectrum data ---
    std::vector<float> smoothedMagnitudes_;
    std::vector<float> peakMagnitudes_;
    std::vector<float> peakHoldTimers_;
    float sampleRate_ = 44100.0f;
    int fftSize_ = 2048;

    //--- Display config ---
    float minDb_ = -72.0f;
    float maxDb_ = 0.0f;
    float smoothingAttack_ = 0.9f;
    float smoothingRelease_ = 0.7f;
    float peakHoldTime_ = 1.0f;
    float peakFallRate_ = 12.0f;

    //--- Colors (from color scheme) ---
    std::array<VSTGUI::CColor, 8> bandColors_ = {{
        {0xFF, 0x6B, 0x35, 0xFF},  // Band 1 - Orange
        {0x4E, 0xCD, 0xC4, 0xFF},  // Band 2 - Teal
        {0x95, 0xE8, 0x6B, 0xFF},  // Band 3 - Green
        {0xC7, 0x92, 0xEA, 0xFF},  // Band 4 - Purple
        {0xFF, 0xCB, 0x6B, 0xFF},  // Band 5 - Yellow
        {0xFF, 0x53, 0x70, 0xFF},  // Band 6 - Red
        {0x89, 0xDD, 0xFF, 0xFF},  // Band 7 - Light Blue
        {0xF7, 0x8C, 0x6C, 0xFF},  // Band 8 - Coral
    }};

    //--- Tooltip ---
    bool showTooltip_ = false;
    VSTGUI::CPoint tooltipPosition_;
    std::string tooltipText_;

    //--- Listener ---
    Listener* listener_ = nullptr;
};

} // namespace Disrumpo
```

**Timer/Animation Approach:**

There are two options for animation in VSTGUI:

1. **CVSTGUITimer with callback** (recommended for precise timing):
```cpp
// In constructor or attached():
animationTimer_ = makeOwned<CVSTGUITimer>(
    [this](CVSTGUITimer*) { onTimerCallback(); },
    16,   // 16ms = ~60fps
    true  // auto-start
);

// In destructor or removed():
if (animationTimer_) {
    animationTimer_->stop();
    animationTimer_ = nullptr;
}
```

2. **CView::onIdle** (simpler, uses global idle rate):
```cpp
// In attached():
setWantsIdle(true);

// In removed():
setWantsIdle(false);

// Override:
void onIdle() override {
    updateAnimation();
    invalid();  // Request redraw
}
```

The spec uses `onIdle()` for simplicity, but `CVSTGUITimer` allows precise control over update rate.

### 1.7 Performance Considerations

| Aspect | Requirement | Implementation |
|--------|-------------|----------------|
| Frame Rate | ~30 fps recommended | Use `CVSTGUITimer` at 33ms (30Hz) |
| FFT Computation | < 1ms per frame | Use optimized FFT (PFFFT or similar) |
| Memory | Zero allocations after init | Pre-allocate all buffers in constructor |
| Thread Safety | Lock-free audio→UI | Use SPSC ring buffer with atomic flag |
| GPU Acceleration | Optional | Use COffscreenContext for caching static elements |

**Why 30Hz is Recommended:**
- Higher rates (60Hz) provide diminishing visual benefit for spectrum display
- 30Hz matches typical video frame rates and feels smooth
- Reduces CPU overhead significantly (half the draw calls)
- FFT data updates at audio buffer rate (~86Hz at 512 samples/44.1kHz) anyway

**Conditional Repainting:**
```cpp
void SpectrumDisplay::onTimerCallback() {
    // Only repaint if we have new data
    if (spectrumFIFO_.isNextBlockReady()) {
        processFFTData();
        invalid();  // Request redraw
    }

    // Always update sweep position (if enabled) for smooth animation
    if (sweepEnabled_) {
        updateSweepFromBuffer();
        invalid();
    }
}
```

**Pre-allocation Checklist:**
- [ ] FFT input buffer (fftSize floats)
- [ ] FFT output buffer (fftSize/2 + 1 complex values)
- [ ] Scope data array (scopeSize floats)
- [ ] Smoothed magnitudes (scopeSize floats)
- [ ] Peak magnitudes (scopeSize floats)
- [ ] Hann window coefficients (fftSize floats)
- [ ] Path objects for drawing (reuse, don't recreate)

**Using KrateDSP Library:**

The project already has a complete FFT implementation in the shared DSP library. No third-party library is needed.

```cpp
#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/core/window_functions.h>

class SpectrumProcessor {
public:
    void prepare(size_t fftSize) {
        fft_.prepare(fftSize);
        windowCoeffs_ = Krate::DSP::Window::generate(
            Krate::DSP::WindowType::Hann, fftSize);
        windowedInput_.resize(fftSize);
        fftOutput_.resize(fft_.numBins());
    }

    void process(const float* samples) {
        const size_t size = windowCoeffs_.size();

        // Apply Hann window
        for (size_t i = 0; i < size; ++i) {
            windowedInput_[i] = samples[i] * windowCoeffs_[i];
        }

        // Forward FFT: real → complex
        fft_.forward(windowedInput_.data(), fftOutput_.data());
    }

    float getMagnitude(size_t bin) const {
        return fftOutput_[bin].magnitude();
    }

    size_t numBins() const { return fft_.numBins(); }

private:
    Krate::DSP::FFT fft_;
    std::vector<float> windowCoeffs_;
    std::vector<float> windowedInput_;
    std::vector<Krate::DSP::Complex> fftOutput_;
};
```

**Available in KrateDSP:**
- `Krate::DSP::FFT` - Radix-2 DIT FFT (256-8192 samples)
- `Krate::DSP::Window::generateHann()` - Hann window with COLA support
- `Krate::DSP::Complex::magnitude()` - Bin magnitude calculation

**Note:** The Hilbert transform (`krate/dsp/primitives/hilbert_transform.h`) is **not** suitable for spectrum visualization. Hilbert creates an analytic signal for frequency shifting (SSB modulation), not frequency-domain decomposition. FFT is the correct choice for spectrum display.

### 1.8 Accessibility

| Feature | Implementation |
|---------|----------------|
| Keyboard Navigation | Tab through bands/dividers, arrow keys to adjust |
| Screen Reader | Announce band name, frequency range, state on focus |
| High Contrast | Ensure sufficient contrast ratios for all colors |
| Reduced Motion | Option to disable animation, show static spectrum |

---

## 2. MorphPad Control

### 2.1 Purpose

A 2D XY pad that controls the morph position within a band's morph space. Shows the positions of morph nodes and allows the user to drag the morph cursor.

### 2.2 Visual Design

```
┌─────────────────────────────────────────┐
│                                         │
│      ● Fuzz (A)          ● Chaos (D)    │
│                                         │
│              ╲              ╱           │
│               ╲            ╱            │
│                ╲          ╱             │
│                 ○ ← cursor              │
│                ╱          ╲             │
│               ╱            ╲            │
│              ╱              ╲           │
│                                         │
│      ● Tube (B)          ● Fold (C)     │
│                                         │
│  X: 0.72  Y: 0.31                       │
└─────────────────────────────────────────┘
```

### 2.3 Components

#### 2.3.1 Node Display

| Property | Specification |
|----------|---------------|
| Shape | Filled circle, 12px diameter |
| Color | Matches distortion type category color |
| Label | Node letter (A, B, C, D) + type name |
| Position | Fixed at corners or user-definable (Alt+drag) |
| Active Indicator | Ring/glow when node has significant weight |

**Node Colors by Category:**
| Category | Color |
|----------|-------|
| Saturation (D01-D06) | #FF6B35 (Orange) |
| Wavefold (D07-D09) | #4ECDC4 (Teal) |
| Digital (D12-D14, D18-D19) | #95E86B (Green) |
| Rectify (D10-D11) | #C792EA (Purple) |
| Dynamic (D15) | #FFCB6B (Yellow) |
| Hybrid (D16-D17, D26) | #FF5370 (Red) |
| Experimental (D20-D25) | #89DDFF (Light Blue) |

#### 2.3.2 Morph Cursor

| Property | Specification |
|----------|---------------|
| Shape | Open circle, 16px diameter, 2px stroke |
| Color | White |
| Shadow | Subtle drop shadow for visibility |
| Animation | Smooth interpolation when values change via automation |

#### 2.3.3 Connection Lines

| Property | Specification |
|----------|---------------|
| Style | Dashed or gradient lines from cursor to active nodes |
| Opacity | Proportional to node weight |
| Color | White at low opacity |

#### 2.3.4 Position Display

| Property | Specification |
|----------|---------------|
| Location | Bottom-left corner of pad |
| Format | "X: 0.00  Y: 0.00" |
| Update | Real-time as cursor moves |

### 2.4 Interaction Behavior

| Action | Behavior |
|--------|----------|
| **Click** | Move cursor to click position |
| **Drag** | Move cursor with mouse |
| **Shift+Drag** | Fine adjustment (10x precision) |
| **Double-click** | Reset cursor to center (0.5, 0.5) |
| **Alt+Drag on node** | Reposition node in morph space |
| **Right-click on node** | Context menu (change type, remove node) |
| **Scroll wheel** | Adjust X (vertical scroll) or Y (horizontal scroll) |

### 2.5 Morph Modes

The pad behavior changes based on the selected morph mode:

#### 2D Planar Mode (Default)
- Full XY control
- Cursor can be positioned anywhere in the pad
- All 4 corners available for nodes

#### 1D Linear Mode
- Only X axis active
- Cursor constrained to horizontal center line
- Nodes arranged along X axis

#### 2D Radial Mode
- Cursor shows angle and distance from center
- Visual: radial grid overlay
- Distance = morph amount, Angle = position between nodes

### 2.6 VSTGUI Implementation

```cpp
// plugins/Disrumpo/src/controller/views/morph_pad.h
// Reference: CXYPad (vstgui/lib/controls/cxypad.h) for basic XY pad patterns
#pragma once

#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/events.h"
#include <array>
#include <functional>

namespace Disrumpo {

class MorphPad : public VSTGUI::CControl {
public:
    static constexpr auto kViewIdentifier = "MorphPad";

    MorphPad(const VSTGUI::CRect& size);

    //--- Configuration ---
    void setMorphMode(int mode);  // 0=Linear1D, 1=Planar2D, 2=Radial2D
    void setNumNodes(int count);  // 2-4
    void setNodePosition(int nodeIndex, float x, float y);
    void setNodeType(int nodeIndex, int distortionType);
    void setNodeLabel(int nodeIndex, const std::string& label);

    //--- Position ---
    void setMorphPosition(float x, float y);
    float getMorphX() const { return morphX_; }
    float getMorphY() const { return morphY_; }

    //--- CControl overrides ---
    void draw(VSTGUI::CDrawContext* context) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& event) override;
    void onMouseMoveEvent(VSTGUI::MouseMoveEvent& event) override;
    void onMouseUpEvent(VSTGUI::MouseUpEvent& event) override;
    void onMouseCancelEvent(VSTGUI::MouseCancelEvent& event) override;
    void onMouseWheelEvent(VSTGUI::MouseWheelEvent& event) override;
    void onKeyboardEvent(VSTGUI::KeyboardEvent& event) override;

    //--- Listener for node changes ---
    class Listener {
    public:
        virtual ~Listener() = default;
        virtual void onMorphPositionChanged(float x, float y) = 0;
        virtual void onNodePositionChanged(int nodeIndex, float x, float y) = 0;
        virtual void onNodeRightClicked(int nodeIndex) = 0;
    };

    void setListener(Listener* listener) { listener_ = listener; }

private:
    void drawBackground(VSTGUI::CDrawContext* context);
    void drawGrid(VSTGUI::CDrawContext* context);
    void drawConnectionLines(VSTGUI::CDrawContext* context);
    void drawNodes(VSTGUI::CDrawContext* context);
    void drawCursor(VSTGUI::CDrawContext* context);
    void drawPositionLabel(VSTGUI::CDrawContext* context);

    VSTGUI::CPoint positionToPixel(float x, float y) const;
    std::pair<float, float> pixelToPosition(const VSTGUI::CPoint& pixel) const;
    int hitTestNode(const VSTGUI::CPoint& where) const;

    //--- Handle mouse movement (shared by down/move events) ---
    void onMouseMove(VSTGUI::MouseDownUpMoveEvent& event);

    //--- State ---
    int morphMode_ = 1;  // Planar2D
    float morphX_ = 0.5f;
    float morphY_ = 0.5f;

    struct Node {
        float x = 0.0f;
        float y = 0.0f;
        int type = 0;
        std::string label;
        bool active = true;
    };

    std::array<Node, 4> nodes_;
    int numActiveNodes_ = 2;

    int draggingNode_ = -1;
    bool draggingCursor_ = false;
    float mouseStartValue_ = 0.0f;  // For CControl value tracking
    VSTGUI::CPoint mouseChangeStartPoint_;

    Listener* listener_ = nullptr;
};

} // namespace Disrumpo
```

---

## 3. BandStrip Control

### 3.1 Purpose

A compound control representing a single frequency band's controls in a collapsible strip format.

### 3.2 Visual Design

**Collapsed State:**
```
┌─────────────────────────────────┐
│ BAND 2  [Fuzz▼] (●) [S][B][M]  │
│ 200Hz - 2kHz                    │
└─────────────────────────────────┘
```

**Expanded State:**
```
┌─────────────────────────────────┐
│ BAND 2  [Fuzz▼]     [S][B][M]  │
│ 200Hz - 2kHz        [▲ Collapse]│
├─────────────────────────────────┤
│                                 │
│  DRIVE        MIX        TONE   │
│   (●)         (●)        (●)    │
│   5.0        80%       4.0kHz   │
│                                 │
│  ┌─ TYPE-SPECIFIC ────────────┐│
│  │  BIAS         SUSTAIN      ││
│  │   (●)          (●)         ││
│  │  +0.3         0.7          ││
│  └────────────────────────────┘│
│                                 │
│  GAIN        PAN                │
│   (●)         (●)               │
│  +3dB        -20%               │
└─────────────────────────────────┘
```

### 3.3 Components

| Component | Type | Description |
|-----------|------|-------------|
| Header | Container | Band name, type selector, quick controls |
| Type Selector | COptionMenu | Dropdown for distortion type |
| Solo/Bypass/Mute | COnOffButton × 3 | Quick toggles |
| Common Params | CKnob × 3 | Drive, Mix, Tone |
| Type-Specific | Dynamic | Changes based on selected type |
| Output Params | CKnob × 2 | Gain, Pan |
| Expand/Collapse | COnOffButton | Toggle expanded view |

### 3.4 Implementation Notes

This control is primarily a **container** that manages the layout and visibility of child controls. It can be implemented as:
- A custom `CViewContainer` subclass
- A VSTGUI template defined in `editor.uidesc` with visibility controllers

The type-specific parameter zone should use a **view switcher** pattern:
```xml
<view class="CViewSwitchContainer" switch-control="band-type-param">
  <view type-index="0"><!-- Soft Clip params --></view>
  <view type-index="1"><!-- Hard Clip params --></view>
  <!-- ... etc for all 26 types ... -->
</view>
```

---

## 4. Implementation Checklist

### 4.1 SpectrumDisplay

- [ ] Create header file `spectrum_display.h`
- [ ] Create implementation `spectrum_display.cpp`
- [ ] Implement FFT processing (integrate PFFFT or similar)
- [ ] Implement lock-free ring buffer for audio→UI data transfer
- [ ] Implement logarithmic frequency mapping
- [ ] Implement band region rendering
- [ ] Implement crossover divider interaction
- [ ] Implement sweep overlay
- [ ] Implement smooth animation/peak hold
- [ ] Register as custom view in VSTGUI
- [ ] Add to `editor.uidesc`
- [ ] Write unit tests for coordinate conversion
- [ ] Write unit tests for hit testing

### 4.2 MorphPad

- [ ] Create header file `morph_pad.h`
- [ ] Create implementation `morph_pad.cpp`
- [ ] Implement node rendering
- [ ] Implement cursor rendering and interaction
- [ ] Implement connection line rendering
- [ ] Implement all three morph modes
- [ ] Implement node repositioning (Alt+drag)
- [ ] Register as custom view in VSTGUI
- [ ] Add to `editor.uidesc`
- [ ] Write unit tests

### 4.3 BandStrip

- [ ] Design uidesc template for collapsed state
- [ ] Design uidesc template for expanded state
- [ ] Implement expand/collapse animation
- [ ] Implement type-specific view switching
- [ ] Test with all 26 distortion types

---

## 5. Testing Requirements

### 5.1 Visual Testing

| Test | Verification |
|------|--------------|
| Color accuracy | All band colors match spec |
| Responsive layout | Controls resize correctly at all window sizes |
| Animation smoothness | 60fps spectrum animation without jitter |
| Font rendering | All text legible at minimum window size |

### 5.2 Interaction Testing

| Test | Verification |
|------|--------------|
| Divider constraints | Cannot drag dividers closer than 0.5 octave |
| Fine adjustment | Shift+drag provides 10x precision |
| Keyboard navigation | Tab cycles through all focusable elements |
| Context menus | Right-click shows appropriate options |

### 5.3 Performance Testing

| Metric | Target |
|--------|--------|
| Draw time | < 2ms per frame |
| FFT computation | < 1ms per frame |
| Memory allocation | Zero allocations during animation |
| CPU usage | < 5% UI thread for all custom controls |
