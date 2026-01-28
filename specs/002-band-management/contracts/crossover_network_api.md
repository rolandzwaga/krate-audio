# CrossoverNetwork API Contract

**Component**: CrossoverNetwork
**Location**: `plugins/disrumpo/src/dsp/crossover_network.h`
**Layer**: Plugin-specific DSP

## Class Interface

```cpp
namespace Disrumpo {

class CrossoverNetwork {
public:
    // Constants
    static constexpr int kMaxBands = 8;
    static constexpr int kMinBands = 1;
    static constexpr int kDefaultBands = 4;
    static constexpr float kDefaultSmoothingMs = 10.0f;

    // Lifecycle
    CrossoverNetwork() noexcept;
    ~CrossoverNetwork() noexcept;

    // Initialization
    void prepare(double sampleRate, int numBands) noexcept;
    void reset() noexcept;

    // Configuration
    void setBandCount(int numBands) noexcept;
    void setCrossoverFrequency(int index, float hz) noexcept;

    // Queries
    [[nodiscard]] int getBandCount() const noexcept;
    [[nodiscard]] float getCrossoverFrequency(int index) const noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;

    // Processing
    void process(float input, std::array<float, kMaxBands>& bands) noexcept;
};

} // namespace Disrumpo
```

## Method Contracts

### prepare()

**Signature**: `void prepare(double sampleRate, int numBands) noexcept`

**Preconditions**:
- `sampleRate > 0`
- `numBands >= kMinBands && numBands <= kMaxBands`

**Postconditions**:
- All internal crossovers initialized for given sample rate
- `isPrepared()` returns true
- Crossover frequencies logarithmically distributed
- All filter states cleared

**Thread Safety**: NOT thread-safe. Call from UI thread or before audio starts.

### reset()

**Signature**: `void reset() noexcept`

**Preconditions**: None

**Postconditions**:
- All filter states cleared to zero
- Crossover frequencies unchanged
- Band count unchanged

**Thread Safety**: Call from audio thread between blocks only.

### setBandCount()

**Signature**: `void setBandCount(int numBands) noexcept`

**Preconditions**:
- `numBands >= kMinBands && numBands <= kMaxBands`
- `isPrepared()` should be true (otherwise no-op)

**Postconditions**:
- `getBandCount()` returns new value
- Crossover frequencies redistributed per FR-011a/FR-011b
- Filter states preserved where possible

**Thread Safety**: Thread-safe (atomic write).

### setCrossoverFrequency()

**Signature**: `void setCrossoverFrequency(int index, float hz) noexcept`

**Preconditions**:
- `index >= 0 && index < getBandCount() - 1`
- `hz >= kMinCrossoverHz && hz <= kMaxCrossoverHz`

**Postconditions**:
- Internal crossover target frequency updated
- Frequency change smoothed over ~5ms (CrossoverLR4 default)

**Thread Safety**: Thread-safe (atomic write to CrossoverLR4).

### process()

**Signature**: `void process(float input, std::array<float, kMaxBands>& bands) noexcept`

**Preconditions**:
- `isPrepared()` must be true (otherwise undefined behavior)
- `bands` array must be valid

**Postconditions**:
- `bands[0..getBandCount()-1]` contain split outputs
- Sum of all bands equals input (phase-coherent)
- `bands[getBandCount()..kMaxBands-1]` unchanged

**Thread Safety**: NOT thread-safe. Call from audio thread only.

**Real-Time Safety**: Guaranteed (FR-001a):
- No allocations
- No locks
- No exceptions
- O(N) complexity where N = getBandCount()

**Processing Model Clarification** (FR-001a):
- The Processor calls `process()` **once per sample** in a loop for block processing
- There is no `processBlock()` method - the caller iterates over samples
- Example: `for (int i = 0; i < numSamples; ++i) { network.process(input[i], bands); }`

## Error Handling

| Condition | Behavior |
|-----------|----------|
| `numBands` out of range | Clamped to [kMinBands, kMaxBands] |
| `hz` out of range | Clamped to [kMinCrossoverHz, sampleRate * 0.45] |
| `index` out of range | Silently ignored |
| Not prepared | `process()` outputs zeros |

## Usage Example

```cpp
CrossoverNetwork networkL, networkR;
std::array<float, 8> bandsL, bandsR;

// Initialize
networkL.prepare(44100.0, 4);
networkR.prepare(44100.0, 4);

// In audio callback
for (int i = 0; i < numSamples; ++i) {
    networkL.process(inputL[i], bandsL);
    networkR.process(inputR[i], bandsR);

    // Process each band...
    for (int b = 0; b < 4; ++b) {
        outputL[i] += bandsL[b];
        outputR[i] += bandsR[b];
    }
}
```

## Dependencies

- `Krate::DSP::CrossoverLR4` (Layer 2)
- `Krate::DSP::OnePoleSmoother` (Layer 1, for frequency smoothing during band count change)
