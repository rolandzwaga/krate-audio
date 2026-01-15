# Data Model: DistortionRack System

**Feature**: 068-distortion-rack | **Date**: 2026-01-15

## Entity Definitions

### SlotType Enum

Identifies the type of processor in a slot.

```cpp
enum class SlotType : uint8_t {
    Empty = 0,       // No processor (bypass)
    Waveshaper,      // Layer 1: Generic waveshaping
    TubeStage,       // Layer 2: Tube saturation
    DiodeClipper,    // Layer 2: Diode clipping
    Wavefolder,      // Layer 2: Wavefolding
    TapeSaturator,   // Layer 2: Tape saturation
    Fuzz,            // Layer 2: Fuzz distortion
    Bitcrusher       // Layer 2: Bit crushing
};
```

| Value | Enum Name | Processor Class | Layer |
|-------|-----------|-----------------|-------|
| 0 | Empty | std::monostate | N/A |
| 1 | Waveshaper | Waveshaper | 1 |
| 2 | TubeStage | TubeStage | 2 |
| 3 | DiodeClipper | DiodeClipper | 2 |
| 4 | Wavefolder | WavefolderProcessor | 2 |
| 5 | TapeSaturator | TapeSaturator | 2 |
| 6 | Fuzz | FuzzProcessor | 2 |
| 7 | Bitcrusher | BitcrusherProcessor | 2 |

---

### ProcessorVariant Type

Internal type alias for the std::variant holding all processor types.

```cpp
using ProcessorVariant = std::variant<
    std::monostate,
    Waveshaper,
    TubeStage,
    DiodeClipper,
    WavefolderProcessor,
    TapeSaturator,
    FuzzProcessor,
    BitcrusherProcessor
>;
```

**Size**: Approximately sizeof(largest processor) + 1 byte for index.
- Largest processor is likely BitcrusherProcessor or TapeSaturator (~200-400 bytes).
- Total variant size: ~400-500 bytes per slot.

---

### Slot (Internal Struct)

Represents a single slot in the rack with its processor and controls.

```cpp
struct Slot {
    // Processors (stereo = 2 mono instances)
    ProcessorVariant processorL;
    ProcessorVariant processorR;

    // Per-slot DC blocking
    DCBlocker dcBlockerL;
    DCBlocker dcBlockerR;

    // Parameter smoothers (5ms smoothing time)
    OnePoleSmoother enableSmoother;  // 0.0 = disabled, 1.0 = enabled
    OnePoleSmoother mixSmoother;     // 0.0 = dry, 1.0 = wet
    OnePoleSmoother gainSmoother;    // linear gain (from dB)

    // Current parameter values (targets for smoothers)
    bool enabled = false;
    float mix = 1.0f;        // [0.0, 1.0]
    float gainDb = 0.0f;     // [-24.0, +24.0] dB
    SlotType type = SlotType::Empty;
};
```

| Field | Type | Default | Range | Description |
|-------|------|---------|-------|-------------|
| processorL | ProcessorVariant | monostate | N/A | Left channel processor |
| processorR | ProcessorVariant | monostate | N/A | Right channel processor |
| dcBlockerL | DCBlocker | unprepared | N/A | Left channel DC blocker |
| dcBlockerR | DCBlocker | unprepared | N/A | Right channel DC blocker |
| enableSmoother | OnePoleSmoother | 0.0 | [0.0, 1.0] | Smoothed enable state |
| mixSmoother | OnePoleSmoother | 1.0 | [0.0, 1.0] | Smoothed mix value |
| gainSmoother | OnePoleSmoother | 1.0 | [0.0, ~15.85] | Smoothed linear gain |
| enabled | bool | false | true/false | Enable state target |
| mix | float | 1.0 | [0.0, 1.0] | Mix value target |
| gainDb | float | 0.0 | [-24.0, +24.0] | Gain in dB target |
| type | SlotType | Empty | enum | Current processor type |

---

### DistortionRack Class

Main class providing the 4-slot distortion rack.

```cpp
class DistortionRack {
public:
    static constexpr size_t kNumSlots = 4;
    static constexpr float kDefaultSmoothingMs = 5.0f;
    static constexpr float kDCBlockerCutoffHz = 10.0f;
    static constexpr float kMinGainDb = -24.0f;
    static constexpr float kMaxGainDb = +24.0f;

    // Lifecycle
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;
    void process(float* left, float* right, size_t numSamples) noexcept;

    // Slot configuration
    void setSlotType(size_t slot, SlotType type) noexcept;
    void setSlotEnabled(size_t slot, bool enabled) noexcept;
    void setSlotMix(size_t slot, float mix) noexcept;
    void setSlotGain(size_t slot, float dB) noexcept;

    // Slot queries
    [[nodiscard]] SlotType getSlotType(size_t slot) const noexcept;
    [[nodiscard]] bool getSlotEnabled(size_t slot) const noexcept;
    [[nodiscard]] float getSlotMix(size_t slot) const noexcept;
    [[nodiscard]] float getSlotGain(size_t slot) const noexcept;

    // Per-slot processor access (for fine-grained control)
    template<typename T>
    [[nodiscard]] T* getProcessor(size_t slot, size_t channel = 0) noexcept;

    template<typename T>
    [[nodiscard]] const T* getProcessor(size_t slot, size_t channel = 0) const noexcept;

    // Global oversampling
    void setOversamplingFactor(int factor) noexcept;  // 1, 2, or 4
    [[nodiscard]] int getOversamplingFactor() const noexcept;
    [[nodiscard]] size_t getLatency() const noexcept;

    // DC blocking control
    void setDCBlockingEnabled(bool enabled) noexcept;
    [[nodiscard]] bool getDCBlockingEnabled() const noexcept;

private:
    // Slots
    std::array<Slot, kNumSlots> slots_;

    // Oversamplers (both instantiated; one used based on factor)
    Oversampler<2, 2> oversampler2x_;
    Oversampler<4, 2> oversampler4x_;
    int oversamplingFactor_ = 1;

    // DC blocking global flag
    bool dcBlockingEnabled_ = true;

    // Cached configuration
    double sampleRate_ = 44100.0;
    double preparedSampleRate_ = 44100.0;  // Rate at which processors were prepared
    size_t maxBlockSize_ = 512;
    bool prepared_ = false;

    // Internal processing
    void processChain(float* left, float* right, size_t numSamples) noexcept;
    void processSlot(size_t slotIndex, float* left, float* right, size_t numSamples) noexcept;
    void prepareSlotProcessor(Slot& slot) noexcept;
};
```

---

## State Transitions

### Slot Lifecycle

```
                    setSlotType(Empty)
         +--------------------------------+
         |                                |
         v                                |
    +--------+    setSlotType(X)    +-----------+
    | Empty  |-------------------->| Configured |
    +--------+                      +-----------+
         ^                                |
         |   setSlotEnabled(false)        | setSlotEnabled(true)
         +--------------------------------+
                                          |
                                          v
                                    +-----------+
                                    |  Active   |
                                    +-----------+
```

### Enable Smoothing State Machine

```
                  setSlotEnabled(true)
    +---------+  ----------------------->  +---------+
    | Off     |                            | Ramping |
    | (0.0)   |                            | to On   |
    +---------+  <-----------------------  +---------+
                  setSlotEnabled(false)        |
                                               | smoother reaches 1.0
                                               v
                                          +---------+
                                          | On      |
                                          | (1.0)   |
                                          +---------+
```

---

## Memory Layout

### Per-Slot Memory

| Component | Approximate Size | Count | Total |
|-----------|------------------|-------|-------|
| ProcessorVariant | ~400 bytes | 2 | 800 bytes |
| DCBlocker | 32 bytes | 2 | 64 bytes |
| OnePoleSmoother | 24 bytes | 3 | 72 bytes |
| State fields | 16 bytes | 1 | 16 bytes |
| **Per-Slot Total** | | | ~952 bytes |

### Class Memory

| Component | Approximate Size |
|-----------|------------------|
| 4 Slots | ~3,800 bytes |
| Oversampler<2,2> | ~4,000 bytes |
| Oversampler<4,2> | ~8,000 bytes |
| Config fields | 64 bytes |
| **Total** | ~16 KB |

---

## Validation Rules

### Slot Index

- Must be < `kNumSlots` (4)
- Out-of-range indices result in no-op (silent failure)

### Slot Type

- Must be valid `SlotType` enum value
- Setting to `Empty` clears the processor variant

### Slot Mix

- Clamped to [0.0, 1.0]
- 0.0 = full dry (processor bypassed)
- 1.0 = full wet (processor output only)

### Slot Gain

- Clamped to [-24.0, +24.0] dB
- Converted to linear gain internally via `dbToGain()`

### Oversampling Factor

- Must be 1, 2, or 4
- Other values are ignored (no change)
- Factor change requires `prepare()` to take full effect

---

## Relationships

```
DistortionRack
├── Slot[0]
│   ├── ProcessorVariant (L)
│   ├── ProcessorVariant (R)
│   ├── DCBlocker (L)
│   ├── DCBlocker (R)
│   └── OnePoleSmoother (enable, mix, gain)
├── Slot[1]
│   └── ...
├── Slot[2]
│   └── ...
├── Slot[3]
│   └── ...
├── Oversampler<2, 2>
└── Oversampler<4, 2>
```
