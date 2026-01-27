# Research: Tape Delay Mode

**Feature**: 024-tape-delay
**Date**: 2025-12-25
**Phase**: 0 (Research)

## Executive Summary

This document captures research findings for implementing the Tape Delay Mode (Layer 4 user feature). The implementation will compose existing Layer 3 components rather than building from scratch, significantly reducing complexity and ensuring consistency with the existing DSP architecture.

## Reference Tape Echo Units

### Roland RE-201 Space Echo (1974)

The RE-201 is the most iconic tape echo and primary reference for this implementation.

**Key characteristics:**
- 3 playback heads at fixed positions along the tape loop
- Variable motor speed controls delay time (50ms-600ms per head)
- Head combinations create complex rhythmic patterns
- Spring reverb integrated (not implemented here)
- Self-oscillation capable at high feedback

**Head timing (relative to motor speed):**
| Head | Position | Typical Use |
|------|----------|-------------|
| Head 1 | 1.0x | Slapback, short echoes |
| Head 2 | 1.5x | Medium delay |
| Head 3 | 2.0x | Long delay, rhythmic |

### Echoplex EP-3/EP-4 (1970s)

**Key characteristics:**
- Single playback head with variable tape speed
- Longer maximum delay (~600ms)
- Distinct preamp coloration (tube warmth)
- Sound-on-sound recording capability
- Darker, more degraded character than RE-201

### Watkins Copicat (1958)

**Key characteristics:**
- 3 playback heads like RE-201
- More lo-fi character
- Higher wow/flutter due to mechanical design
- Distinctive "English" sound used on many classic recordings

## Technical Analysis

### Wow and Flutter

**Wow** (slow pitch drift, 0.5-5 Hz):
- Caused by eccentric tape reels, belt stretch
- Rate scales inversely with tape speed (slower tape = slower wow)
- Depth typically 0.5-2% pitch deviation

**Flutter** (fast pitch wobble, 5-50 Hz):
- Caused by capstan irregularities, tape slip
- Rate relatively constant regardless of tape speed
- Depth typically 0.1-0.5% pitch deviation

**Combined effect**: The spec's Wear control maps to both:
```cpp
// From CharacterProcessor
void setTapeWowDepth(float depth);    // 0-1, scales to 2% max
void setTapeFlutterDepth(float depth); // 0-1, scales to 1% max
```

### Motor Inertia

Real tape machines exhibit mechanical inertia when changing speed:

**Acceleration characteristics:**
- Heavy reel assemblies resist instant speed changes
- Transition time: 200-500ms for typical speed changes
- Creates characteristic pitch sweep during transitions
- More pronounced on large reel-to-reel machines

**Implementation approach:**
```cpp
// Use OnePoleSmoother with long smoothing time
OnePoleSmoother delaySmoother_;
// Configure for 300ms smoothing (middle of 200-500ms range)
delaySmoother_.configure(300.0f, sampleRate);
```

### Tape Saturation Characteristics

**Harmonic profile:**
- Primarily even-order harmonics (2nd, 4th)
- Gentle compression at high levels
- No harsh clipping (tape "absorbs" transients)

**Existing implementation in CharacterProcessor:**
```cpp
// SaturationType::Tape provides correct harmonic profile
tapeSaturation_.setType(SaturationType::Tape);
// Saturation amount maps 0-1 to -17dB to +24dB drive
```

### Head Gap Frequency Response

Physical playback heads have finite gap width affecting frequency response:

**High-frequency rolloff:**
- Gap width determines wavelength limit
- Typical rolloff: -3dB at 8-12kHz
- More rolloff at slower tape speeds

**Existing implementation:**
```cpp
// CharacterProcessor tapeRolloff_ filter
tapeRolloff_.setType(FilterType::Lowpass);
tapeRolloff_.setCutoff(12000.0f);  // Default, maps to Age control
```

### Splice Artifacts (Age)

Tape loops have splice points that create periodic artifacts:

**Characteristics:**
- Brief transient/click at splice
- Frequency = motor speed (tape loop length)
- More pronounced on worn/old tape
- Not always audible in well-maintained units

**Implementation consideration:**
The Age control should modulate splice artifact intensity. CharacterProcessor doesn't currently have splice artifacts - this may need to be added or simulated via periodic noise bursts.

## Existing Component Analysis

### TapManager Capabilities

**Suitable for echo heads:**
- Up to 16 taps (we need 3)
- Per-tap: enable, level, pan, filter, feedback
- Supports fixed timing ratios
- Already has parameter smoothing

**Limitations:**
- Currently uses single shared delay line (mono sum)
- Head ratios will be configured manually, not via pattern

**Integration approach:**
```cpp
// Configure 3 heads at RE-201 ratios
taps_.setTapTimeMs(0, motorSpeedMs * 1.0f);
taps_.setTapTimeMs(1, motorSpeedMs * 1.5f);
taps_.setTapTimeMs(2, motorSpeedMs * 2.0f);
```

### CharacterProcessor Tape Mode

**Already implemented:**
- [x] Wow/flutter LFOs with configurable rate/depth
- [x] Tape saturation with harmonic profile
- [x] Tape hiss via NoiseGenerator
- [x] High-frequency rolloff filter
- [x] Crossfade between modes (Clean/Tape/BBD/Digital)

**Not implemented (needed for Age control):**
- [ ] Splice artifacts
- [ ] Dynamic rolloff based on "age"
- [ ] Increased noise with age

**Gap analysis:**
CharacterProcessor provides most tape character. Age control will need to:
1. Reduce rolloff frequency (more HF loss)
2. Increase hiss level
3. Add splice artifacts (may need extension)

### FeedbackNetwork Capabilities

**Suitable for feedback path:**
- Feedback amount 0-120% (self-oscillation)
- Filter in path (LP/HP/BP)
- Saturation in path
- Cross-feedback for stereo
- Freeze mode

**Integration:**
```cpp
// Configure for tape-like feedback darkening
feedback_.setFilterEnabled(true);
feedback_.setFilterType(FilterType::Lowpass);
feedback_.setFilterCutoff(4000.0f);  // Progressive HF loss
feedback_.setSaturationEnabled(true);
feedback_.setSaturationType(SaturationType::Tape);
```

### ModulationMatrix Role

**Considered but not required:**
- CharacterProcessor already has internal LFOs for wow/flutter
- ModulationMatrix would add complexity without benefit
- Decision: Do not compose ModulationMatrix for initial implementation

## Design Decisions

### D1: Wow Rate Scaling

**Problem**: Real tape wow rate depends on tape speed (motor speed).

**Decision**: Scale wow rate inversely with motor speed.
```cpp
// Base wow rate at 500ms delay
constexpr float kBaseWowRate = 0.5f;  // Hz
constexpr float kBaseDelayMs = 500.0f;

float scaledWowRate = kBaseWowRate * (kBaseDelayMs / currentDelayMs);
character_.setTapeWowRate(scaledWowRate);
```

### D2: Signal Flow Order

**Problem**: Which order should signal pass through components?

**Decision**: TapManager → FeedbackNetwork → CharacterProcessor

**Rationale:**
1. TapManager reads from delay line at head positions
2. FeedbackNetwork processes feedback path (filter + saturation)
3. CharacterProcessor adds final tape character (wow/flutter applied to output)

This matches how physical tape machines work: heads read from tape, feedback goes back into record path, playback includes tape transport effects.

### D3: Splice Artifacts

**Problem**: CharacterProcessor doesn't have splice artifacts.

**Decision**: Defer splice artifacts to future enhancement.

**Rationale:**
- Core tape delay sound achievable without splice artifacts
- Adding periodic transients requires careful design
- Age control will work with existing hiss/rolloff
- Can be added as enhancement in future iteration

### D4: Head Filter Usage

**Problem**: TapManager has per-tap filters. Should we use them?

**Decision**: Do not use TapManager filters for echo heads.

**Rationale:**
- CharacterProcessor's tape rolloff is more authentic
- Per-head filtering not typical of real tape echos
- Simplifies parameter mapping
- Filters can be enabled by users who want them

## Clarifications Resolved

### Q1: Maximum Delay Time

**Spec states**: 20-2000ms (FR-002)

**Analysis**: RE-201 max is ~600ms. Echoplex ~600ms. 2000ms exceeds physical tape machines but provides modern flexibility.

**Resolution**: Keep 2000ms max as specified. Document that >600ms is "extended mode" beyond vintage accuracy.

### Q2: Feedback Self-Oscillation

**Spec states**: 0-100%+ feedback (FR-027)

**Analysis**: FeedbackNetwork supports up to 120% (1.2 linear).

**Resolution**: Map user's 100% to 1.0 linear feedback. Above 100% enters self-oscillation territory with saturation limiting.

### Q3: Stereo Handling

**Spec states**: Stereo processing required.

**Analysis**:
- TapManager uses mono delay line with stereo pan per tap
- CharacterProcessor supports stereo via processStereo()
- FeedbackNetwork has cross-feedback for ping-pong

**Resolution**: Use TapManager's stereo pan for head placement. Heads 1/2/3 can be panned L/C/R for width.

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Performance budget exceeded | Low | High | Leverage optimized Layer 3 components |
| Wow rate scaling sounds unnatural | Medium | Medium | Provide user adjustment or fixed "authentic" mode |
| Missing splice artifacts disappoints users | Low | Low | Document as future enhancement |
| Motor inertia too slow/fast | Medium | Medium | Make inertia time configurable |

## References

- [Tape Delay Explained](https://www.soundonsound.com/techniques/tape-delay)
- [RE-201 Service Manual](https://www.synthxl.com/wp-content/uploads/2018/01/Roland_RE-201_Service_Manual.pdf)
- [The Science of Wow and Flutter](https://www.aes.org/e-lib/browse.cfm?elib=3932)
