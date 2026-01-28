# BandProcessor API Contract

**Component**: BandProcessor
**Location**: `plugins/disrumpo/src/dsp/band_processor.h`
**Layer**: Plugin-specific DSP

## Class Interface

```cpp
namespace Disrumpo {

class BandProcessor {
public:
    // Constants
    static constexpr float kDefaultSmoothingMs = 10.0f;

    // Lifecycle
    BandProcessor() noexcept;
    ~BandProcessor() noexcept;

    // Initialization
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Parameter setters (thread-safe)
    void setGainDb(float db) noexcept;
    void setPan(float pan) noexcept;
    void setMute(bool muted) noexcept;

    // Processing
    void process(float& left, float& right) noexcept;

    // Queries
    [[nodiscard]] bool isSmoothing() const noexcept;
};

} // namespace Disrumpo
```

## Method Contracts

### prepare()

**Signature**: `void prepare(double sampleRate) noexcept`

**Preconditions**:
- `sampleRate > 0`

**Postconditions**:
- All smoothers configured for sample rate
- Smoothers snapped to default values (gain=0dB, pan=0, mute=0)

**Thread Safety**: NOT thread-safe. Call before audio processing starts.

### reset()

**Signature**: `void reset() noexcept`

**Preconditions**: None

**Postconditions**:
- All smoothers reset to zero
- No processing state retained

**Thread Safety**: NOT thread-safe. Call from audio thread between blocks only.

### setGainDb()

**Signature**: `void setGainDb(float db) noexcept`

**Preconditions**:
- Valid float (NaN/Inf handled by smoother)

**Postconditions**:
- Target gain set to linear conversion of clamped dB value
- Gain change smoothed over ~10ms

**Value Handling**:
- `db` clamped to [-24, +24]
- Converted to linear: `gain = 10^(db/20)`

**Thread Safety**: Thread-safe (atomic target set).

### setPan()

**Signature**: `void setPan(float pan) noexcept`

**Preconditions**:
- Valid float (NaN/Inf handled by smoother)

**Postconditions**:
- Target pan set to clamped value
- Pan change smoothed over ~10ms

**Value Handling**:
- `pan` clamped to [-1, +1]
- -1 = full left, 0 = center, +1 = full right

**Thread Safety**: Thread-safe (atomic target set).

### setMute()

**Signature**: `void setMute(bool muted) noexcept`

**Preconditions**: None

**Postconditions**:
- Target mute set (0.0 or 1.0)
- Mute transition smoothed over ~10ms (click-free)

**Thread Safety**: Thread-safe (atomic target set).

### process()

**Signature**: `void process(float& left, float& right) noexcept`

**Preconditions**:
- `prepare()` must have been called

**Postconditions**:
- `left` and `right` modified in-place with:
  - Gain scaling
  - Equal-power pan law
  - Mute multiplier

**Processing Formula**:
```cpp
// Per FR-022: Equal-power pan law
float smoothedGain = gainSmoother_.process();
float smoothedPan = panSmoother_.process();
float smoothedMute = muteSmoother_.process();  // 0.0 = unmuted, 1.0 = muted

// Calculate pan coefficients
float leftCoeff = std::cos(smoothedPan * kPi / 4.0f + kPi / 4.0f);
float rightCoeff = std::sin(smoothedPan * kPi / 4.0f + kPi / 4.0f);

// Apply all in one pass
float muteMultiplier = 1.0f - smoothedMute;
left = left * smoothedGain * leftCoeff * muteMultiplier;
right = right * smoothedGain * rightCoeff * muteMultiplier;
```

**Thread Safety**: NOT thread-safe. Call from audio thread only.

**Real-Time Safety**: Guaranteed:
- No allocations
- No locks
- No exceptions
- O(1) complexity

### isSmoothing()

**Signature**: `[[nodiscard]] bool isSmoothing() const noexcept`

**Postconditions**:
- Returns true if any smoother has not reached target
- Returns false when all transitions complete

**Thread Safety**: Thread-safe (atomic reads).

## Error Handling

| Condition | Behavior |
|-----------|----------|
| NaN/Inf input | Smoother sanitizes to 0 or clamps |
| Out of range dB | Clamped to [-24, +24] |
| Out of range pan | Clamped to [-1, +1] |
| Not prepared | Undefined behavior |

## Pan Law Verification

Per FR-022, equal-power pan law:

| Pan Value | Left Gain | Right Gain | Total Power |
|-----------|-----------|------------|-------------|
| -1.0 (left) | 1.000 | 0.000 | 1.0 |
| -0.5 | 0.924 | 0.383 | 1.0 |
| 0.0 (center) | 0.707 | 0.707 | 1.0 |
| +0.5 | 0.383 | 0.924 | 1.0 |
| +1.0 (right) | 0.000 | 1.000 | 1.0 |

## Usage Example

```cpp
BandProcessor bands[8];

// Initialize all
for (auto& band : bands) {
    band.prepare(44100.0);
}

// Set parameters (thread-safe, can be called from UI)
bands[0].setGainDb(6.0f);   // +6 dB
bands[1].setPan(-0.5f);     // Slightly left
bands[2].setMute(true);     // Muted

// In audio callback
for (int i = 0; i < numSamples; ++i) {
    for (int b = 0; b < numBands; ++b) {
        bands[b].process(bandsL[b][i], bandsR[b][i]);
    }
}
```

## Dependencies

- `Krate::DSP::OnePoleSmoother` (Layer 1)
- `Krate::DSP::dbToGain` (Layer 0, optional - can inline formula)
