# API Contract: Phaser

**Class**: `Krate::DSP::Phaser`
**Header**: `<krate/dsp/processors/phaser.h>`
**Layer**: 2 (Processors)

## Class Synopsis

```cpp
namespace Krate {
namespace DSP {

class Phaser {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr int kMaxStages = 12;
    static constexpr int kDefaultStages = 4;
    static constexpr float kDefaultRate = 0.5f;
    static constexpr float kDefaultDepth = 0.5f;
    static constexpr float kDefaultFeedback = 0.0f;
    static constexpr float kDefaultMix = 0.5f;
    static constexpr float kDefaultCenterFreq = 1000.0f;
    static constexpr float kDefaultStereoSpread = 0.0f;
    static constexpr float kSmoothingTimeMs = 5.0f;
    static constexpr float kMinSweepFreq = 20.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // =========================================================================
    // Processing
    // =========================================================================

    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;
    void processStereo(float* left, float* right, size_t numSamples) noexcept;

    // =========================================================================
    // Stage Configuration
    // =========================================================================

    void setNumStages(int stages) noexcept;
    [[nodiscard]] int getNumStages() const noexcept;

    // =========================================================================
    // LFO Parameters
    // =========================================================================

    void setRate(float hz) noexcept;
    [[nodiscard]] float getRate() const noexcept;

    void setDepth(float amount) noexcept;
    [[nodiscard]] float getDepth() const noexcept;

    void setWaveform(Waveform waveform) noexcept;
    [[nodiscard]] Waveform getWaveform() const noexcept;

    // =========================================================================
    // Frequency Range
    // =========================================================================

    void setCenterFrequency(float hz) noexcept;
    [[nodiscard]] float getCenterFrequency() const noexcept;

    // =========================================================================
    // Feedback
    // =========================================================================

    void setFeedback(float amount) noexcept;
    [[nodiscard]] float getFeedback() const noexcept;

    // =========================================================================
    // Stereo
    // =========================================================================

    void setStereoSpread(float degrees) noexcept;
    [[nodiscard]] float getStereoSpread() const noexcept;

    // =========================================================================
    // Mix
    // =========================================================================

    void setMix(float dryWet) noexcept;
    [[nodiscard]] float getMix() const noexcept;

    // =========================================================================
    // Tempo Sync
    // =========================================================================

    void setTempoSync(bool enabled) noexcept;
    [[nodiscard]] bool isTempoSyncEnabled() const noexcept;

    void setNoteValue(NoteValue value, NoteModifier modifier = NoteModifier::None) noexcept;
    void setTempo(float bpm) noexcept;

    // =========================================================================
    // State
    // =========================================================================

    [[nodiscard]] bool isPrepared() const noexcept;
};

} // namespace DSP
} // namespace Krate
```

## Method Details

### Lifecycle

#### `prepare(double sampleRate)`

Initialize the phaser for a given sample rate.

- **Parameters**: `sampleRate` - Sample rate in Hz (must be > 0)
- **Preconditions**: None
- **Postconditions**:
  - Processor is ready for `process()` calls
  - All filter stages prepared
  - LFOs prepared
  - Smoothers configured
  - `isPrepared()` returns `true`
- **Real-time safe**: No (allocates LFO wavetables)
- **Thread safety**: Not thread-safe, call from main thread

#### `reset()`

Reset all internal state without changing parameters.

- **Parameters**: None
- **Preconditions**: `prepare()` has been called
- **Postconditions**:
  - All filter states cleared
  - Feedback state cleared
  - LFO phase reset to 0
- **Real-time safe**: Yes
- **Thread safety**: Call from audio thread only

### Processing

#### `process(float input) -> float`

Process a single mono sample.

- **Parameters**: `input` - Input sample
- **Returns**: Processed output sample
- **Preconditions**: `prepare()` has been called
- **Real-time safe**: Yes
- **Thread safety**: Audio thread only
- **Notes**:
  - Returns input unchanged if not prepared
  - Uses left channel processing path

#### `processBlock(float* buffer, size_t numSamples)`

Process a block of mono samples in-place.

- **Parameters**:
  - `buffer` - Audio buffer (modified in-place)
  - `numSamples` - Number of samples to process
- **Preconditions**: `prepare()` has been called
- **Real-time safe**: Yes
- **Thread safety**: Audio thread only

#### `processStereo(float* left, float* right, size_t numSamples)`

Process stereo audio with independent L/R modulation.

- **Parameters**:
  - `left` - Left channel buffer (modified in-place)
  - `right` - Right channel buffer (modified in-place)
  - `numSamples` - Number of samples to process
- **Preconditions**: `prepare()` has been called
- **Real-time safe**: Yes
- **Thread safety**: Audio thread only
- **Notes**: Right channel LFO is offset by `stereoSpread` degrees

### Stage Configuration

#### `setNumStages(int stages)`

Set the number of allpass stages.

- **Parameters**: `stages` - Number of stages (2, 4, 6, 8, 10, or 12)
- **Validation**:
  - Clamped to [2, 12]
  - Rounded down to nearest even number
- **Real-time safe**: Yes
- **Thread safety**: Any thread
- **Notes**:
  - N stages produces N/2 notches
  - Change is immediate (not smoothed)

### LFO Parameters

#### `setRate(float hz)`

Set the LFO modulation rate.

- **Parameters**: `hz` - Rate in Hz [0.01, 20.0]
- **Validation**: Clamped to valid range
- **Real-time safe**: Yes
- **Thread safety**: Any thread
- **Notes**:
  - Ignored when tempo sync is enabled
  - Change is smoothed over 5ms

#### `setDepth(float amount)`

Set the modulation depth (sweep range).

- **Parameters**: `amount` - Depth [0.0, 1.0]
- **Validation**: Clamped to valid range
- **Real-time safe**: Yes
- **Thread safety**: Any thread
- **Notes**:
  - 0.0 = no sweep (stationary notches)
  - 1.0 = full range sweep (center +/- 100%)
  - Change is smoothed over 5ms

#### `setWaveform(Waveform waveform)`

Set the LFO waveform.

- **Parameters**: `waveform` - One of: `Sine`, `Triangle`, `Square`, `Sawtooth`
- **Validation**: Invalid waveforms default to Sine
- **Real-time safe**: Yes
- **Thread safety**: Any thread
- **Notes**: LFO crossfades between waveforms over 10ms

### Frequency Range

#### `setCenterFrequency(float hz)`

Set the center frequency of the sweep range.

- **Parameters**: `hz` - Center frequency [100, 10000] Hz
- **Validation**: Clamped to valid range
- **Real-time safe**: Yes
- **Thread safety**: Any thread
- **Notes**:
  - Sweep range = center * (1 +/- depth)
  - Change is smoothed over 5ms

### Feedback

#### `setFeedback(float amount)`

Set the feedback amount for resonance.

- **Parameters**: `amount` - Feedback [-1.0, +1.0]
- **Validation**: Clamped to valid range
- **Real-time safe**: Yes
- **Thread safety**: Any thread
- **Notes**:
  - Positive feedback emphasizes notches
  - Negative feedback shifts notch positions
  - tanh soft-limiting prevents oscillation
  - Change is smoothed over 5ms

### Stereo

#### `setStereoSpread(float degrees)`

Set the LFO phase offset for stereo width.

- **Parameters**: `degrees` - Phase offset [0, 360] degrees
- **Validation**: Wrapped to valid range
- **Real-time safe**: Yes
- **Thread safety**: Any thread
- **Notes**:
  - 0 = mono (L/R identical)
  - 90 = quadrature (L at peak when R at midpoint)
  - 180 = inverted (L/R opposite)

### Mix

#### `setMix(float dryWet)`

Set the dry/wet mix.

- **Parameters**: `dryWet` - Mix [0.0, 1.0]
- **Validation**: Clamped to valid range
- **Real-time safe**: Yes
- **Thread safety**: Any thread
- **Notes**:
  - 0.0 = 100% dry (bypass)
  - 0.5 = 50/50 mix (classic phaser)
  - 1.0 = 100% wet
  - Change is smoothed over 5ms

### Tempo Sync

#### `setTempoSync(bool enabled)`

Enable or disable tempo synchronization.

- **Parameters**: `enabled` - True to sync LFO rate to tempo
- **Real-time safe**: Yes
- **Thread safety**: Any thread
- **Notes**: When enabled, `setRate()` is ignored

#### `setNoteValue(NoteValue value, NoteModifier modifier)`

Set the note value for tempo sync.

- **Parameters**:
  - `value` - Note value (Quarter, Eighth, etc.)
  - `modifier` - Optional modifier (None, Dotted, Triplet)
- **Real-time safe**: Yes
- **Thread safety**: Any thread

#### `setTempo(float bpm)`

Set the current tempo.

- **Parameters**: `bpm` - Tempo in BPM [20, 300]
- **Validation**: Clamped to valid range
- **Real-time safe**: Yes
- **Thread safety**: Any thread
- **Notes**: Only affects processing when tempo sync is enabled

## Error Handling

- **NaN/Inf input**: Filter states reset, outputs silence
- **Unprepared state**: `process()` returns input unchanged
- **Invalid parameters**: Clamped to valid range (no errors thrown)

## Performance Characteristics

| Metric | Value |
|--------|-------|
| Per-sample CPU (12 stages) | ~50ns at 44.1kHz |
| Per-block overhead | Minimal (loop setup only) |
| Memory per instance | ~1.7KB |
| Latency | 0 samples |

## Dependencies

| Dependency | Layer | Purpose |
|------------|-------|---------|
| Allpass1Pole | 1 | Cascaded phase shifting |
| LFO | 1 | Modulation source |
| OnePoleSmoother | 1 | Parameter smoothing |
| NoteValue/NoteModifier | 0 | Tempo sync |
| db_utils | 0 | denormal flushing, NaN/Inf detection |
