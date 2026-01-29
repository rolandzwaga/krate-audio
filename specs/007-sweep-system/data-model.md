# Data Model: Sweep System

**Feature**: 007-sweep-system
**Date**: 2026-01-29

---

## Entities

### 1. SweepProcessor

**Purpose**: Core DSP class that calculates per-band intensity multipliers based on sweep parameters.

**Location**: `plugins/Disrumpo/src/dsp/sweep_processor.h`

**Layer**: Plugin DSP (composes Layer 1/2 primitives)

| Field | Type | Range | Default | Description |
|-------|------|-------|---------|-------------|
| enabled_ | bool | - | false | Sweep on/off state |
| centerFreqHz_ | float | [20, 20000] | 1000.0 | Sweep center frequency in Hz |
| widthOctaves_ | float | [0.5, 4.0] | 1.5 | Sweep width in octaves |
| intensity_ | float | [0.0, 2.0] | 0.5 | Intensity multiplier (0-200%) |
| falloffMode_ | SweepFalloff | Sharp/Smooth | Smooth | Falloff shape |
| morphLinkMode_ | MorphLinkMode | 8 modes | None | Sweep-to-morph linking curve |

**Internal State**:

| Field | Type | Description |
|-------|------|-------------|
| frequencySmoother_ | OnePoleSmoother | Smooths frequency changes (10-50ms) |
| smoothedFreqHz_ | float | Current smoothed frequency |
| bandIntensities_ | std::array<float, 8> | Cached per-band intensity values |
| sampleRate_ | double | Current sample rate |
| prepared_ | bool | True after prepare() called |

**Methods**:

| Method | Signature | Description |
|--------|-----------|-------------|
| prepare | `void prepare(double sampleRate, int maxBlockSize)` | Initialize for given sample rate |
| reset | `void reset()` | Reset internal state |
| setEnabled | `void setEnabled(bool enabled)` | Enable/disable sweep |
| setCenterFrequency | `void setCenterFrequency(float hz)` | Set target frequency |
| setWidth | `void setWidth(float octaves)` | Set sweep width |
| setIntensity | `void setIntensity(float value)` | Set intensity (0-2) |
| setFalloffMode | `void setFalloffMode(SweepFalloff mode)` | Set Sharp/Smooth |
| setMorphLinkMode | `void setMorphLinkMode(MorphLinkMode mode)` | Set morph link curve |
| setSmoothingTime | `void setSmoothingTime(float ms)` | Set frequency smoothing |
| process | `void process()` | Update smoothed frequency |
| calculateBandIntensity | `float calculateBandIntensity(float bandCenterHz) const` | Get intensity for band |
| getMorphPosition | `float getMorphPosition() const` | Get linked morph position |
| getSmoothedFrequency | `float getSmoothedFrequency() const` | Get current smoothed freq |
| getPositionData | `SweepPositionData getPositionData(uint64_t samplePos) const` | Get data for UI sync |

---

### 2. SweepFalloff (Enum)

**Purpose**: Defines the intensity falloff shape.

**Location**: `plugins/Disrumpo/src/dsp/sweep_types.h`

```cpp
enum class SweepFalloff : uint8_t {
    Sharp = 0,   // Linear falloff, exactly 0 at edge
    Smooth = 1   // Gaussian falloff
};
```

---

### 3. MorphLinkMode (Enum - Extended)

**Purpose**: Defines how sweep position maps to morph position.

**Location**: `plugins/Disrumpo/src/plugin_ids.h` (update existing)

```cpp
enum class MorphLinkMode : uint8_t {
    None = 0,       // Manual control only
    SweepFreq,      // Linear: y = x
    InverseSweep,   // Inverse: y = 1 - x
    EaseIn,         // Quadratic: y = x^2
    EaseOut,        // Inverse quadratic: y = 1 - (1-x)^2
    HoldRise,       // Hold at 0 until 60%, then rise
    Stepped,        // Quantize to 4 levels
    Custom,         // User-defined breakpoints (NEW)
    COUNT           // Sentinel (8 modes)
};
```

---

### 4. SweepPositionData (Struct)

**Purpose**: Data structure for audio-to-UI communication.

**Location**: `dsp/include/krate/dsp/primitives/sweep_position_buffer.h`

```cpp
struct SweepPositionData {
    float centerFreqHz = 1000.0f;   // Current sweep center frequency
    float widthOctaves = 1.5f;      // Sweep width in octaves
    float intensity = 0.5f;         // 0.0-2.0 intensity
    uint64_t samplePosition = 0;    // Sample count for timing sync
    bool enabled = false;           // Sweep on/off state
    SweepFalloff falloff = SweepFalloff::Smooth;  // Falloff mode for rendering
};
```

---

### 5. SweepPositionBuffer (Class)

**Purpose**: Lock-free SPSC ring buffer for audio-UI synchronization.

**Location**: `dsp/include/krate/dsp/primitives/sweep_position_buffer.h`

**Layer**: Layer 1 (primitives)

| Field | Type | Description |
|-------|------|-------------|
| buffer_ | std::array<SweepPositionData, 8> | Ring buffer storage |
| writeIndex_ | std::atomic<int> | Producer write position |
| readIndex_ | std::atomic<int> | Consumer read position |
| elementCount_ | std::atomic<int> | Number of available elements |

**Methods**:

| Method | Signature | Thread | Description |
|--------|-----------|--------|-------------|
| push | `bool push(const SweepPositionData& data)` | Audio | Add new position data |
| pop | `bool pop(SweepPositionData& data)` | UI | Read oldest position data |
| getLatest | `bool getLatest(SweepPositionData& data)` | UI | Read newest without removing |
| clear | `void clear()` | Any (safe point) | Clear all entries |

---

### 6. CustomCurve (Class)

**Purpose**: User-defined breakpoint curve for Custom morph link mode.

**Location**: `plugins/Disrumpo/src/dsp/custom_curve.h`

| Field | Type | Description |
|-------|------|-------------|
| breakpoints_ | std::vector<Breakpoint> | Sorted list of (x,y) points |

**Breakpoint Struct**:
```cpp
struct Breakpoint {
    float x;  // Normalized position [0, 1]
    float y;  // Output value [0, 1]
};
```

**Constraints**:
- Minimum 2 breakpoints
- Maximum 8 breakpoints
- First breakpoint.x must be 0.0
- Last breakpoint.x must be 1.0
- Breakpoints sorted by x ascending

**Methods**:

| Method | Signature | Description |
|--------|-----------|-------------|
| evaluate | `float evaluate(float x) const` | Interpolate curve at position |
| addBreakpoint | `bool addBreakpoint(float x, float y)` | Add point (fails if at limit) |
| removeBreakpoint | `bool removeBreakpoint(int index)` | Remove point (fails if at minimum) |
| setBreakpoint | `void setBreakpoint(int index, float x, float y)` | Move existing point |
| getBreakpointCount | `int getBreakpointCount() const` | Number of breakpoints |
| getBreakpoint | `Breakpoint getBreakpoint(int index) const` | Get point by index |
| reset | `void reset()` | Reset to default (0,0) to (1,1) |
| saveState | `void saveState(IBStream* stream) const` | Serialize to stream |
| loadState | `void loadState(IBStream* stream)` | Deserialize from stream |

---

### 7. SweepLFO (Class)

**Purpose**: LFO wrapper for sweep frequency modulation.

**Location**: `plugins/Disrumpo/src/dsp/sweep_lfo.h`

| Field | Type | Range | Default | Description |
|-------|------|-------|---------|-------------|
| enabled_ | bool | - | false | LFO on/off |
| rate_ | float | [0.01, 20.0] Hz | 1.0 | LFO rate in Hz |
| depth_ | float | [0.0, 1.0] | 1.0 | Modulation depth |
| waveform_ | Waveform | 6 shapes | Sine | LFO shape |
| tempoSync_ | bool | - | false | Tempo sync mode |
| noteValue_ | NoteValue | - | Quarter | Sync note value |

**Internal**:

| Field | Type | Description |
|-------|------|-------------|
| lfo_ | Krate::DSP::LFO | Underlying LFO instance |
| minFreqHz_ | float | 20.0 (minimum output) |
| maxFreqHz_ | float | 20000.0 (maximum output) |

**Methods**:

| Method | Signature | Description |
|--------|-----------|-------------|
| prepare | `void prepare(double sampleRate)` | Initialize LFO |
| reset | `void reset()` | Reset phase |
| setEnabled | `void setEnabled(bool enabled)` | Enable/disable |
| setRate | `void setRate(float hz)` | Set free-running rate |
| setDepth | `void setDepth(float depth)` | Set modulation amount |
| setWaveform | `void setWaveform(Waveform shape)` | Set LFO shape |
| setTempoSync | `void setTempoSync(bool enabled, float bpm, NoteValue value)` | Configure sync |
| process | `float process()` | Get frequency offset in Hz |

---

### 8. SweepEnvelopeFollower (Class)

**Purpose**: Envelope follower wrapper for input-driven sweep modulation.

**Location**: `plugins/Disrumpo/src/dsp/sweep_envelope.h`

| Field | Type | Range | Default | Description |
|-------|------|-------|---------|-------------|
| enabled_ | bool | - | false | Envelope on/off |
| attackMs_ | float | [1, 100] | 10.0 | Attack time in ms |
| releaseMs_ | float | [10, 500] | 100.0 | Release time in ms |
| sensitivity_ | float | [0.0, 1.0] | 0.5 | Sensitivity (0-100%) |

**Internal**:

| Field | Type | Description |
|-------|------|-------------|
| env_ | Krate::DSP::EnvelopeFollower | Underlying envelope follower |
| minFreqHz_ | float | 20.0 |
| maxFreqHz_ | float | 20000.0 |

**Methods**:

| Method | Signature | Description |
|--------|-----------|-------------|
| prepare | `void prepare(double sampleRate)` | Initialize |
| reset | `void reset()` | Reset state |
| setEnabled | `void setEnabled(bool enabled)` | Enable/disable |
| setAttack | `void setAttack(float ms)` | Set attack time |
| setRelease | `void setRelease(float ms)` | Set release time |
| setSensitivity | `void setSensitivity(float value)` | Set sensitivity |
| process | `float process(float inputSample)` | Get frequency offset |
| getCurrentLevel | `float getCurrentLevel() const` | Get current envelope |

---

### 9. SweepIndicator (UI Class)

**Purpose**: VSTGUI control for rendering sweep overlay on SpectrumDisplay.

**Location**: `plugins/Disrumpo/src/controller/sweep_indicator.h`

| Field | Type | Description |
|-------|------|-------------|
| centerFreqHz_ | float | Current center frequency |
| widthOctaves_ | float | Current width |
| intensity_ | float | Current intensity |
| enabled_ | bool | Visibility state |
| falloffMode_ | SweepFalloff | Sharp or Smooth |

**Methods**:

| Method | Signature | Description |
|--------|-----------|-------------|
| draw | `void draw(CDrawContext* context) override` | Render overlay |
| updateFromPositionData | `void updateFromPositionData(const SweepPositionData& data)` | Update state |

---

## Relationships

```
┌──────────────────────────────────────────────────────────────────┐
│                        Processor (Audio Thread)                    │
│                                                                    │
│  ┌────────────┐    ┌──────────────┐    ┌─────────────────────┐   │
│  │ SweepLFO   │───►│              │    │                     │   │
│  └────────────┘    │   Sweep      │    │  SweepPosition      │   │
│                    │   Processor  │───►│  Buffer             │   │
│  ┌────────────┐    │              │    │  (Lock-free SPSC)   │   │
│  │ SweepEnv   │───►│              │    │                     │   │
│  │ Follower   │    └──────────────┘    └─────────────────────┘   │
│  └────────────┘           │                     │                 │
│                           │                     │                 │
│                           ▼                     │                 │
│                  ┌────────────────┐             │                 │
│                  │  MorphEngine   │             │                 │
│                  │ (position      │             │                 │
│                  │  updates)      │             │                 │
│                  └────────────────┘             │                 │
└────────────────────────────────────────────────┼─────────────────┘
                                                  │
                    ┌─────────────────────────────┘
                    │ Lock-free read
                    ▼
┌──────────────────────────────────────────────────────────────────┐
│                        Controller (UI Thread)                      │
│                                                                    │
│  ┌────────────────┐    ┌─────────────────────────────────────┐   │
│  │ Sweep Panel    │    │           SpectrumDisplay           │   │
│  │ (Parameters)   │    │  ┌─────────────────────────────┐    │   │
│  └────────────────┘    │  │      SweepIndicator         │    │   │
│                        │  │      (Overlay Layer)        │    │   │
│  ┌────────────────┐    │  └─────────────────────────────┘    │   │
│  │ CustomCurve    │    └─────────────────────────────────────┘   │
│  │ Editor         │                                               │
│  └────────────────┘                                               │
└──────────────────────────────────────────────────────────────────┘
```

---

## State Transitions

### SweepProcessor States

```
┌─────────┐    prepare()    ┌──────────┐
│ Created │────────────────►│ Prepared │
└─────────┘                 └──────────┘
                                 │
                                 │ setEnabled(true)
                                 ▼
                            ┌──────────┐
                            │ Active   │◄───┐
                            └──────────┘    │
                                 │          │
                    setEnabled(false)       │ setEnabled(true)
                                 │          │
                                 ▼          │
                            ┌──────────┐    │
                            │ Bypassed │────┘
                            └──────────┘
```

### CustomCurve Validation

```
┌─────────┐
│ Input   │
│ x, y    │
└────┬────┘
     │
     ▼
┌─────────────┐    No     ┌──────────────┐
│ x in [0,1]? │──────────►│ Reject/Clamp │
└──────┬──────┘           └──────────────┘
       │ Yes
       ▼
┌─────────────┐    No     ┌──────────────┐
│ y in [0,1]? │──────────►│ Reject/Clamp │
└──────┬──────┘           └──────────────┘
       │ Yes
       ▼
┌──────────────┐   No     ┌──────────────┐
│ Not at limit?│─────────►│ Reject       │
└──────┬───────┘          └──────────────┘
       │ Yes
       ▼
┌─────────────┐
│ Insert      │
│ sorted      │
└─────────────┘
```

---

## Parameter Mappings

### Normalized to Real Value Conversions

| Parameter | Normalized [0,1] | Real Value | Formula |
|-----------|------------------|------------|---------|
| Frequency | 0.0 - 1.0 | 20 - 20000 Hz | `20 * pow(1000, normalized)` |
| Width | 0.0 - 1.0 | 0.5 - 4.0 oct | `0.5 + normalized * 3.5` |
| Intensity | 0.0 - 1.0 | 0% - 200% | `normalized * 2.0` |

**Note on Frequency Mapping**: The log-scale formula `20 * pow(1000, normalized)` maps:
- normalized=0.0 → 20 Hz
- normalized=0.5 → ~632 Hz
- normalized=1.0 → 20,000 Hz

To convert 1kHz default to normalized: `log10(1000/20) / log10(1000) ≈ 0.566`. The default in the SweepProcessor table (1000.0 Hz) is the Hz value; the corresponding normalized value for UI is ~0.566.
| LFO Rate | 0.0 - 1.0 | 0.01 - 20 Hz | `0.01 * pow(2000, normalized)` |
| Attack | 0.0 - 1.0 | 1 - 100 ms | `1 + normalized * 99` |
| Release | 0.0 - 1.0 | 10 - 500 ms | `10 + normalized * 490` |
| Sensitivity | 0.0 - 1.0 | 0% - 100% | `normalized` |

---

## Serialization Format

### Preset State Structure

```
Version (int32)              = 3 (kPresetVersion)
... existing state ...

// Sweep State (added in v3)
SweepEnabled (bool)
SweepFrequency (float)       // Normalized [0,1]
SweepWidth (float)           // Normalized [0,1]
SweepIntensity (float)       // Normalized [0,1]
SweepFalloff (int32)         // 0=Sharp, 1=Smooth
SweepMorphLink (int32)       // 0-7 (MorphLinkMode)
SweepSmoothingTime (float)   // ms

// LFO State
SweepLFOEnabled (bool)
SweepLFORate (float)         // Normalized [0,1]
SweepLFOWaveform (int32)     // 0-5 (Waveform enum)
SweepLFODepth (float)        // [0,1]
SweepLFOTempoSync (bool)
SweepLFONoteValue (int32)    // NoteValue enum

// Envelope State
SweepEnvEnabled (bool)
SweepEnvAttack (float)       // Normalized [0,1]
SweepEnvRelease (float)      // Normalized [0,1]
SweepEnvSensitivity (float)  // [0,1]

// Custom Curve (if MorphLink == Custom)
CustomCurvePointCount (int32)
For each point:
  PointX (float)             // [0,1]
  PointY (float)             // [0,1]
```
