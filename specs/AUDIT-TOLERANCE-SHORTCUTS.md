# Test Tolerance Audit

**Created**: 2025-12-25
**Purpose**: Track potential test shortcuts where tolerances may have been relaxed instead of fixing implementations.

## Resolved Issues

### 1. Pitch Shifter (spec 016) - RESOLVED: FALSE POSITIVE

**Location**: [tests/unit/processors/pitch_shift_processor_test.cpp](../tests/unit/processors/pitch_shift_processor_test.cpp)

**Spec Requirement** (SC-001):
- Simple mode: ±10 cents accuracy
- Granular/PhaseVocoder: ±5 cents accuracy
- ±5 cents = ~0.29% frequency tolerance

**Original Test Issue**:
- Tests used 2% tolerance (35 cents) - appeared to be a shortcut
- Actually a measurement methodology problem, not implementation issue

**Investigation Findings**:
- FFT-based frequency detection was fooled by crossfade amplitude modulation (AM) artifacts
- Crossfade at ~22Hz creates AM sidebands that shift apparent FFT peak
- Autocorrelation method correctly measures fundamental frequency
- Diagnostic test showed: FFT=894Hz (wrong), Autocorrelation=882Hz (correct for 880Hz target)

**Resolution**:
- Updated tests to use `estimateFrequencyAutocorr()` instead of `estimateFrequencyFFT()`
- Tests now pass with spec-compliant ±5 cents (0.289%) tolerance
- Pitch shifter implementations were actually correct all along

**Status**: ✅ RESOLVED (2025-12-25)

---

## Critical Issues

(None currently)

---

### 2. Oversampler (spec 006) - RESOLVED: IMPLEMENTATION COMPLIANT

**Spec Requirement** (SC-003):
> Passband frequency response is flat within 0.1dB up to 20kHz

**Investigation Findings**:
- Original test used 3dB tolerance (overly generous)
- Created comprehensive SC-003 compliance test at multiple frequencies
- Actual measurements show excellent passband flatness:
  - Standard quality: +0.05dB to -0.03dB (well within ±0.1dB)
  - High quality: -0.03dB to -0.04dB (well within ±0.1dB)

**Resolution**:
- Added "[SC-003]" tagged test with proper 0.1dB tolerance at 1kHz, 5kHz, 10kHz, 15kHz
- Updated original passband test comments to reference comprehensive test
- Implementation was already compliant; test was just unnecessarily loose

**Status**: ✅ RESOLVED (2025-12-25)

---

### 3. Multimode Filter - RESOLVED: IMPLEMENTATION EXCELLENT

**Location**: [tests/unit/processors/multimode_filter_test.cpp](../tests/unit/processors/multimode_filter_test.cpp)

**Original Concern**:
- Line 376-378: `margin(1.0f)` for allpass 0dB response
- Line 447-448: `margin(2.0f)` for peak filter passband

**Investigation Findings**:
- Allpass filter actual measurements: -0.016dB to +0.007dB (well within ±0.02dB!)
- Peak filter passband measurements: +0.020dB at 200Hz, +0.017dB at 5kHz (within ±0.02dB!)
- Margins are generous for robustness, not to hide issues
- Implementation quality is excellent

**Status**: ✅ VERIFIED - NOT A SHORTCUT (2025-12-25)

---

### 4. Biquad Filter - RESOLVED: IMPLEMENTATION PERFECT

**Location**: [tests/unit/primitives/biquad_test.cpp](../tests/unit/primitives/biquad_test.cpp)

**Original Concern**:
- Line 807: `CHECK(maxPowerSum == Approx(1.0f).margin(0.15f));`
- 15% tolerance seemed high for Linkwitz-Riley power sum

**Investigation Findings**:
- Actual measured power sum: exactly 1.0
- Linkwitz-Riley implementation is perfect (LP^2 + HP^2 = 1.0)
- Margin is generous for robustness, not to hide issues

**Status**: ✅ VERIFIED - NOT A SHORTCUT (2025-12-25)

---

## Needs Investigation

(None currently)

---

## Verified Legitimate Tolerances

These tolerances match their spec requirements - no action needed:

| Component | Tolerance | Spec Reference |
|-----------|-----------|----------------|
| Noise Generator spectral slopes | ±1dB/octave | SC-009/010/011 |
| Feedback Network decay | ±0.5dB | SC-001 |
| Dynamics Processor GR | ±0.5dB | SC-001 |
| dB conversion accuracy | ±0.0001dB | SC-002 |
| Smoother timing | ±5% | SC-005 |

---

## Audit Methodology

### Search Patterns Used

```bash
# Wide margins in tests
grep -r "margin\([0-9]+\.[0-9]*f?\)" tests/

# Comments about tolerance
grep -r "(relaxed|adjusted|tolerance|allow|wider)" tests/

# Cross-reference spec vs test
# For each SC-xxx in spec.md, verify test matches tolerance
```

### Red Flags

1. Comments containing "relaxed", "adjusted", "initial implementation"
2. Margins > 1.0f for dB measurements expecting 0dB
3. Percentage tolerances > 5% for accuracy tests
4. Test thresholds that differ from spec SC-xxx requirements

---

## Resolution Tracking

| Issue | Priority | Status | Resolution |
|-------|----------|--------|------------|
| Pitch Shifter SC-001 | High | ✅ Resolved | False positive - FFT fooled by AM artifacts; autocorrelation correct |
| Oversampler SC-003 | Medium | ✅ Resolved | Implementation compliant; added proper 0.1dB test |
| Multimode filter margins | Medium | ✅ Verified | Implementation excellent (<0.02dB); margins conservative |
| Biquad power sum | Low | ✅ Verified | Implementation perfect (=1.0); margin conservative |

---

## Notes

- Character Processor THD (SC-005) was fixed in this session by adjusting the implementation's drive range, not the test thresholds
- THD measurement methodology documented in [TESTING-GUIDE.md](TESTING-GUIDE.md#thd-total-harmonic-distortion-measurement)
