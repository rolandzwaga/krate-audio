# Quickstart: Sweep System Implementation

**Feature**: 007-sweep-system
**Date**: 2026-01-29

---

## Overview

This guide provides a step-by-step implementation path for the Sweep System. Follow tasks in order, completing tests before implementation per Constitution Principle XII.

---

## Prerequisites

Before starting:
1. Verify 005-morph-system is complete (MorphEngine exists)
2. Verify 006-morph-ui is complete (MorphPad, X/Y Link dropdowns)
3. Verify sweep parameter IDs in `plugin_ids.h` (0x0E00-0x0E05)

---

## Implementation Order

### Phase 1: Core DSP (T8.1-T8.6)

#### Step 1.1: Create sweep_types.h

**File**: `plugins/Disrumpo/src/dsp/sweep_types.h`

```cpp
#pragma once

#include <cstdint>

namespace Disrumpo {

/// @brief Sweep falloff mode.
enum class SweepFalloff : uint8_t {
    Sharp = 0,   // Linear falloff, exactly 0 at edge
    Smooth = 1   // Gaussian falloff
};

} // namespace Disrumpo
```

#### Step 1.2: Write Intensity Calculation Tests

**File**: `plugins/Disrumpo/tests/dsp/sweep_intensity_tests.cpp`

Test cases:
1. Gaussian intensity at center = intensity param (within 0.01)
2. Gaussian intensity at 1 sigma = 0.606 * intensity (within 0.02)
3. Gaussian intensity at 2 sigma = 0.135 * intensity (within 0.02)
4. Linear intensity at center = intensity param (within 0.01)
5. Linear intensity at edge = exactly 0.0 (within 0.01)
6. Linear intensity beyond edge = 0.0

```cpp
TEST_CASE("Gaussian intensity at center equals intensity param", "[sweep][gaussian]") {
    float result = calculateGaussianIntensity(1000.0f, 1000.0f, 2.0f, 1.0f);
    REQUIRE_THAT(result, Catch::Matchers::WithinAbs(1.0f, 0.01f));
}
```

#### Step 1.3: Implement Intensity Calculations

**File**: `plugins/Disrumpo/src/dsp/sweep_processor.cpp`

Implement `calculateGaussianIntensity()` and `calculateLinearFalloff()` per formulas in contracts/sweep_morph_link.h.

#### Step 1.4: Write SweepProcessor Tests

**File**: `plugins/Disrumpo/tests/dsp/sweep_processor_tests.cpp`

Test cases:
1. Prepare initializes smoothers
2. SetCenterFrequency targets smoother
3. Process advances smoother
4. GetSmoothedFrequency returns smoothed value
5. Enable/disable state
6. Width and intensity parameters

#### Step 1.5: Implement SweepProcessor Class

**File**: `plugins/Disrumpo/src/dsp/sweep_processor.h` and `.cpp`

Follow the API contract in `contracts/sweep_processor.h`.

---

### Phase 2: Morph Linking (T8.8-T8.13)

#### Step 2.1: Update MorphLinkMode Enum

**File**: `plugins/Disrumpo/src/plugin_ids.h`

Add `Custom = 7` to the enum. Update `COUNT` to 8.

#### Step 2.2: Write Morph Link Curve Tests

**File**: `plugins/Disrumpo/tests/dsp/sweep_morph_link_tests.cpp`

Test each curve at key positions (0, 0.25, 0.5, 0.75, 1.0):

```cpp
TEST_CASE("Linear curve: y = x", "[sweep][morph-link]") {
    REQUIRE(applyMorphLinkCurve(MorphLinkMode::SweepFreq, 0.0f) == 0.0f);
    REQUIRE(applyMorphLinkCurve(MorphLinkMode::SweepFreq, 0.5f) == 0.5f);
    REQUIRE(applyMorphLinkCurve(MorphLinkMode::SweepFreq, 1.0f) == 1.0f);
}

TEST_CASE("EaseIn curve: y = x^2", "[sweep][morph-link]") {
    REQUIRE_THAT(applyMorphLinkCurve(MorphLinkMode::EaseIn, 0.5f),
                 Catch::Matchers::WithinAbs(0.25f, 0.001f));
}

TEST_CASE("HoldRise curve", "[sweep][morph-link]") {
    REQUIRE(applyMorphLinkCurve(MorphLinkMode::HoldRise, 0.3f) == 0.0f);
    REQUIRE(applyMorphLinkCurve(MorphLinkMode::HoldRise, 0.6f) == 0.0f);
    REQUIRE_THAT(applyMorphLinkCurve(MorphLinkMode::HoldRise, 0.8f),
                 Catch::Matchers::WithinAbs(0.5f, 0.001f));
}

TEST_CASE("Stepped curve: 4 discrete levels", "[sweep][morph-link]") {
    REQUIRE(applyMorphLinkCurve(MorphLinkMode::Stepped, 0.0f) == 0.0f);
    REQUIRE_THAT(applyMorphLinkCurve(MorphLinkMode::Stepped, 0.3f),
                 Catch::Matchers::WithinAbs(0.333f, 0.01f));
    REQUIRE_THAT(applyMorphLinkCurve(MorphLinkMode::Stepped, 0.6f),
                 Catch::Matchers::WithinAbs(0.667f, 0.01f));
    REQUIRE(applyMorphLinkCurve(MorphLinkMode::Stepped, 0.99f) == 1.0f);
}
```

#### Step 2.3: Implement Morph Link Curves

Use inline functions in `contracts/sweep_morph_link.h` (already provided).

#### Step 2.4: Write CustomCurve Tests

**File**: `plugins/Disrumpo/tests/dsp/custom_curve_tests.cpp`

Test cases:
1. Default curve is linear (2 points: 0,0 and 1,1)
2. Evaluate at breakpoints returns exact values
3. Evaluate between breakpoints interpolates linearly
4. Cannot remove below 2 breakpoints
5. Cannot add above 8 breakpoints
6. Breakpoints stay sorted by x

#### Step 2.5: Implement CustomCurve Class

**File**: `plugins/Disrumpo/src/dsp/custom_curve.h` and `.cpp`

```cpp
class CustomCurve {
public:
    static constexpr int kMinBreakpoints = 2;
    static constexpr int kMaxBreakpoints = 8;

    CustomCurve() {
        // Initialize to linear: (0,0) and (1,1)
        breakpoints_.push_back({0.0f, 0.0f});
        breakpoints_.push_back({1.0f, 1.0f});
    }

    float evaluate(float x) const {
        x = std::clamp(x, 0.0f, 1.0f);

        // Find bracketing breakpoints
        for (size_t i = 0; i < breakpoints_.size() - 1; ++i) {
            if (x >= breakpoints_[i].x && x <= breakpoints_[i + 1].x) {
                float t = (x - breakpoints_[i].x) /
                          (breakpoints_[i + 1].x - breakpoints_[i].x);
                return breakpoints_[i].y + t * (breakpoints_[i + 1].y - breakpoints_[i].y);
            }
        }
        return breakpoints_.back().y;
    }

    // ... other methods
};
```

#### Step 2.6: Integrate Morph Linking into SweepProcessor

Add `getMorphPosition()` method that:
1. Normalizes current frequency to [0, 1]
2. Applies selected morph link curve
3. Returns morph position

---

### Phase 3: Sweep Automation (T8.7, FR-023-029)

#### Step 3.1: Write SweepLFO Tests

**File**: `plugins/Disrumpo/tests/dsp/sweep_lfo_tests.cpp`

Test cases:
1. LFO produces correct waveforms
2. Rate matches specified frequency
3. Tempo sync produces correct period
4. Depth scales output appropriately

#### Step 3.2: Implement SweepLFO

**File**: `plugins/Disrumpo/src/dsp/sweep_lfo.h`

Wrapper around `Krate::DSP::LFO` with sweep-specific range mapping.

#### Step 3.3: Write SweepEnvelopeFollower Tests

**File**: `plugins/Disrumpo/tests/dsp/sweep_envelope_tests.cpp`

Test cases:
1. Responds to input level changes
2. Attack time affects rise rate
3. Release time affects fall rate
4. Sensitivity scales output

#### Step 3.4: Implement SweepEnvelopeFollower

**File**: `plugins/Disrumpo/src/dsp/sweep_envelope.h`

Wrapper around `Krate::DSP::EnvelopeFollower` with sweep-specific mapping.

---

### Phase 4: Audio-UI Sync (T8.16)

#### Step 4.1: Write SweepPositionBuffer Tests

**File**: `dsp/tests/primitives/sweep_position_buffer_tests.cpp`

Test cases:
1. Push and pop in FIFO order
2. Buffer full behavior
3. GetLatest returns newest
4. DrainToLatest gets most recent
5. Interpolation between samples
6. Clear resets state

#### Step 4.2: Implement SweepPositionBuffer

**File**: `dsp/include/krate/dsp/primitives/sweep_position_buffer.h`

Use the contract header as the implementation (inline methods).

---

### Phase 5: UI Controls (T8.14-T8.15)

#### Step 5.1: Add Sweep Parameters to Controller

**File**: `plugins/Disrumpo/src/controller/controller.cpp`

Register sweep parameters in `initialize()`:
- SweepEnable (bool)
- SweepFrequency (log scale 20-20000 Hz)
- SweepWidth (linear 0.5-4.0 oct)
- SweepIntensity (linear 0-200%)
- SweepFalloff (list: Sharp, Smooth)
- SweepMorphLink (list: 8 modes)

#### Step 5.2: Create Sweep Panel in editor.uidesc

Add VSTGUI controls:
- COnOffButton for Enable
- CKnob for Frequency (with log scale)
- CKnob for Width
- CKnob for Intensity
- CSegmentButton for Falloff
- COptionMenu for MorphLink

#### Step 5.3: Implement SweepIndicator Control

**File**: `plugins/Disrumpo/src/controller/sweep_indicator.h` and `.cpp`

```cpp
class SweepIndicator : public CView {
public:
    void draw(CDrawContext* context) override {
        if (!enabled_) return;

        // Calculate center position
        float centerX = freqToX(centerFreqHz_);

        // Draw curve based on falloff mode
        if (falloff_ == SweepFalloff::Smooth) {
            drawGaussianCurve(context, centerX);
        } else {
            drawTriangularCurve(context, centerX);
        }

        // Draw center line
        context->setLineWidth(2.0);
        context->drawLine(CPoint(centerX, 0), CPoint(centerX, getHeight()));
    }

    void updateFromPositionData(const SweepPositionData& data) {
        centerFreqHz_ = data.centerFreqHz;
        widthOctaves_ = data.widthOctaves;
        intensity_ = data.intensity;
        enabled_ = data.enabled;
        falloff_ = static_cast<SweepFalloff>(data.falloff);
        invalid();  // Request redraw
    }

private:
    float freqToX(float freq) const {
        // Log scale mapping
        float logMin = std::log10(20.0f);
        float logMax = std::log10(20000.0f);
        float logFreq = std::log10(freq);
        return getWidth() * (logFreq - logMin) / (logMax - logMin);
    }
};
```

#### Step 5.4: Integrate SweepIndicator into SpectrumDisplay

Add SweepIndicator as an overlay layer in SpectrumDisplay.

---

### Phase 6: Processor Integration

#### Step 6.1: Add SweepProcessor to Disrumpo Processor

**File**: `plugins/Disrumpo/src/processor/processor.h`

```cpp
class Processor : public Steinberg::Vst::AudioEffect {
private:
    // Add sweep components
    SweepProcessor sweepProcessor_;
    SweepLFO sweepLFO_;
    SweepEnvelopeFollower sweepEnvelope_;
    SweepPositionBuffer sweepPositionBuffer_;
};
```

#### Step 6.2: Handle Sweep Parameters in processParameterChanges

Process sweep parameter updates and forward to SweepProcessor.

#### Step 6.3: Integrate Sweep into Audio Processing

In `process()`:
1. Process sweep modulation sources (LFO, envelope)
2. Update SweepProcessor frequency with modulation
3. Calculate band intensities
4. Apply intensities to band processing
5. Push position data to buffer

#### Step 6.4: Apply Morph Linking

When sweep-morph link is not None:
1. Get morph position from SweepProcessor
2. Update MorphEngine position(s) based on link mode

---

### Phase 7: State Serialization

#### Step 7.1: Add Sweep State to Preset Save/Load

**File**: `plugins/Disrumpo/src/processor/processor.cpp`

In `setState()` and `getState()`:
- Sweep enable, frequency, width, intensity, falloff, morph link
- LFO state (enable, rate, waveform, depth, tempo sync)
- Envelope state (enable, attack, release, sensitivity)
- Custom curve breakpoints

---

## Testing Checklist

After implementation, verify:

- [ ] All unit tests pass
- [ ] Gaussian intensity SC-001, SC-002, SC-003 pass
- [ ] Linear intensity SC-004, SC-005 pass
- [ ] All 8 morph link curves produce correct output (SC-007)
- [ ] SweepIndicator renders at correct position (SC-008, SC-009)
- [ ] UI controls respond within 16ms (SC-010)
- [ ] Visualization updates at 30fps minimum (SC-011)
- [ ] Presets save/load correctly (SC-012)
- [ ] CPU overhead < 0.1% per band (SC-013)
- [ ] Internal LFO rate accuracy within 0.1% (SC-015)
- [ ] Envelope follower timing within 10% (SC-016)
- [ ] Run pluginval at strictness level 5

---

## Common Pitfalls

1. **Forgetting to prepare()**: Always call `prepare()` before processing
2. **Thread safety**: Use lock-free buffer for audio-UI communication
3. **Parameter smoothing**: Apply smoothing to frequency, not derived values
4. **Log scale**: Use log2 for octave calculations, log10 for display
5. **Custom curve sorting**: Keep breakpoints sorted by x after edits
6. **Modulation combination**: Sum LFO and envelope, then clamp to range
