# Data Model: Ruinae Flanger Effect

**Spec**: [spec.md](spec.md) | **Date**: 2026-03-12

## Entities

### 1. Flanger (DSP Class)

**Location**: `dsp/include/krate/dsp/processors/flanger.h`
**Namespace**: `Krate::DSP`
**Layer**: 2 (Processors)

| Field | Type | Default | Constraints | Description |
|-------|------|---------|-------------|-------------|
| `delayL_` | `DelayLine` | -- | Prepared with 10ms max | Left channel delay line |
| `delayR_` | `DelayLine` | -- | Prepared with 10ms max | Right channel delay line |
| `lfoL_` | `LFO` | -- | -- | Left channel LFO |
| `lfoR_` | `LFO` | -- | -- | Right channel LFO (phase-offset) |
| `rateSmoother_` | `OnePoleSmoother` | -- | 5ms smoothing | Rate parameter smoother |
| `depthSmoother_` | `OnePoleSmoother` | -- | 5ms smoothing | Depth parameter smoother |
| `feedbackSmoother_` | `OnePoleSmoother` | -- | 5ms smoothing | Feedback parameter smoother |
| `mixSmoother_` | `OnePoleSmoother` | -- | 5ms smoothing | Mix parameter smoother |
| `feedbackStateL_` | `float` | `0.0f` | -- | Left channel feedback state |
| `feedbackStateR_` | `float` | `0.0f` | -- | Right channel feedback state |
| `sampleRate_` | `double` | `44100.0` | > 0 | Current sample rate |
| `rate_` | `float` | `0.5f` | [0.05, 5.0] Hz | LFO rate target |
| `depth_` | `float` | `0.5f` | [0.0, 1.0] | Depth target |
| `feedback_` | `float` | `0.0f` | [-1.0, +1.0] | Feedback target |
| `mix_` | `float` | `0.5f` | [0.0, 1.0] | Mix target |
| `stereoSpread_` | `float` | `90.0f` | [0, 360] degrees | LFO phase offset between L/R |
| `waveform_` | `LFOWaveform` | `Triangle` | Sine or Triangle | LFO waveform |
| `tempoSync_` | `bool` | `false` | -- | Tempo sync enable |
| `noteValue_` | `NoteValue` | `Quarter` | -- | Tempo sync note value |
| `noteModifier_` | `NoteModifier` | `Plain` | -- | Note modifier (plain/dotted/triplet) |
| `tempo_` | `double` | `120.0` | [20, 300] BPM | Host tempo |
| `prepared_` | `bool` | `false` | -- | Whether prepare() was called |

**Constants**:

| Constant | Value | Description |
|----------|-------|-------------|
| `kMinRate` | `0.05f` | Minimum LFO rate (Hz) |
| `kMaxRate` | `5.0f` | Maximum LFO rate (Hz) |
| `kDefaultRate` | `0.5f` | Default LFO rate (Hz) |
| `kMinDelayMs` | `0.3f` | Minimum delay / sweep floor (ms) |
| `kMaxDelayMs` | `4.0f` | Maximum delay / sweep ceiling (ms) |
| `kMaxDelayBufferMs` | `10.0f` | Delay line buffer size (ms) |
| `kDefaultDepth` | `0.5f` | Default depth |
| `kMinFeedback` | `-1.0f` | Minimum feedback |
| `kMaxFeedback` | `1.0f` | Maximum feedback |
| `kDefaultFeedback` | `0.0f` | Default feedback |
| `kFeedbackClamp` | `0.98f` | Internal feedback safety clamp |
| `kDefaultMix` | `0.5f` | Default mix |
| `kDefaultStereoSpread` | `90.0f` | Default stereo spread (degrees) |
| `kSmoothingTimeMs` | `5.0f` | Parameter smoothing time constant |

**Methods**:

| Method | Signature | Description |
|--------|-----------|-------------|
| `prepare` | `void prepare(double sampleRate) noexcept` | Allocate delay lines, configure LFOs and smoothers |
| `reset` | `void reset() noexcept` | Reset delay lines, LFOs, smoothers, feedback state |
| `processStereo` | `void processStereo(float* left, float* right, size_t numSamples) noexcept` | Process stereo audio in-place |
| `setRate` | `void setRate(float rateHz) noexcept` | Set LFO rate (updates smoother target + LFO) |
| `setDepth` | `void setDepth(float depth) noexcept` | Set sweep depth |
| `setFeedback` | `void setFeedback(float feedback) noexcept` | Set feedback amount (clamped to +/-0.98) |
| `setMix` | `void setMix(float mix) noexcept` | Set dry/wet mix |
| `setStereoSpread` | `void setStereoSpread(float degrees) noexcept` | Set LFO phase offset |
| `setWaveform` | `void setWaveform(LFOWaveform wf) noexcept` | Set LFO waveform |
| `setTempoSync` | `void setTempoSync(bool enabled) noexcept` | Enable/disable tempo sync |
| `setNoteValue` | `void setNoteValue(NoteValue nv, NoteModifier nm) noexcept` | Set tempo sync note value |
| `setTempo` | `void setTempo(double bpm) noexcept` | Set host tempo |

**Processing Topology**:
```
Input ----+---> dry
          |
          +-- feedbackState * feedback (tanh soft-clipped) --->+
          |                                                     |
          v                                                     |
      [DelayLine write] --> [readLinear(lfoModulatedDelay)] --> wet ---> feedbackState
                                                                         (for next sample)

[True Crossfade Mix: (1-mix)*dry + mix*wet] ---> output
```

### 2. ModulationType (Enum)

**Location**: `plugins/ruinae/src/engine/ruinae_effects_chain.h` (or a shared header)

| Value | Integer | Description |
|-------|---------|-------------|
| `None` | 0 | No modulation effect active |
| `Phaser` | 1 | Phaser active |
| `Flanger` | 2 | Flanger active |

### 3. RuinaeFlangerParams (Plugin Parameter Struct)

**Location**: `plugins/ruinae/src/parameters/flanger_params.h`
**Namespace**: `Ruinae`

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `rateHz` | `std::atomic<float>` | `0.5f` | Rate in Hz (0.05-5.0) |
| `depth` | `std::atomic<float>` | `0.5f` | Depth (0.0-1.0) |
| `feedback` | `std::atomic<float>` | `0.0f` | Feedback (-1.0 to +1.0) |
| `mix` | `std::atomic<float>` | `0.5f` | Mix (0.0-1.0) |
| `stereoSpread` | `std::atomic<float>` | `90.0f` | Stereo spread (0-360 degrees) |
| `waveform` | `std::atomic<int>` | `1` | Waveform index (0=Sine, 1=Triangle) |
| `sync` | `std::atomic<bool>` | `false` | Tempo sync enable |
| `noteValue` | `std::atomic<int>` | `kNoteValueDefaultIndex` | Note value dropdown index |

### 4. Parameter IDs (plugin_ids.h additions)

| ID | Name | Range |
|----|------|-------|
| 1910 | `kFlangerRateId` | Flanger base ID |
| 1911 | `kFlangerDepthId` | |
| 1912 | `kFlangerFeedbackId` | |
| 1913 | `kFlangerMixId` | |
| 1914 | `kFlangerStereoSpreadId` | |
| 1915 | `kFlangerWaveformId` | |
| 1916 | `kFlangerSyncId` | |
| 1917 | `kFlangerNoteValueId` | |
| 1918 | `kModulationTypeId` | Modulation type selector (replaces phaserEnabled functionality) |
| 1919 | `kFlangerEndId` | End marker |

## Relationships

```
RuinaeEffectsChain
├── phaser_ : Phaser          (existing, Layer 2)
├── flanger_ : Flanger        (NEW, Layer 2)
├── activeModType_ : ModulationType
├── incomingModType_ : ModulationType
└── modCrossfade state (alpha, increment, crossfading flag)

Processor
├── flangerParams_ : RuinaeFlangerParams   (NEW)
├── modulationType_ : std::atomic<int>     (NEW, replaces phaserEnabled_)
└── phaserEnabled_ : std::atomic<bool>     (DEPRECATED, kept for migration)

Flanger (Layer 2)
├── delayL_, delayR_ : DelayLine           (Layer 1)
├── lfoL_, lfoR_ : LFO                    (Layer 1)
├── *Smoother_ : OnePoleSmoother           (Layer 1)
└── uses: flushDenormal(), isNaN(), isInf() (Layer 0)
```

## State Transitions

### Modulation Slot State Machine

```
         setModulationType(Phaser)
None ──────────────────────────────> Phaser
  ^                                    |
  |    setModulationType(None)         |  setModulationType(Flanger)
  |<───────────────────────────────    |
  |                                    v
  |<──────────────────────────────  Flanger
         setModulationType(None)
```

All transitions use a 30ms linear-ramp crossfade:
1. Set `incomingModType_` to new type
2. Prepare incoming effect if not already prepared
3. Start crossfade (`modCrossfadeAlpha_ = 0.0f`, compute increment)
4. During crossfade: both effects process, outputs blended
5. On crossfade complete: `activeModType_ = incomingModType_`, stop crossfading

### Preset Migration State

```
Old Preset Load:
  Stream contains: [...phaserParams...][phaserEnabled:int8][...rest...]
  Read phaserEnabled -> map to modulationType:
    0 -> ModulationType::None
    1 -> ModulationType::Phaser
  flangerParams_ -> defaults (no flanger data in old preset)

New Preset Load:
  Stream contains: [...phaserParams...][modulationType:int32][...flangerParams...][...rest...]
  Read modulationType directly
  Read flangerParams
```

## Validation Rules

| Parameter | Validation | Applied Where |
|-----------|-----------|---------------|
| Rate | Clamp to [0.05, 5.0] Hz | `Flanger::setRate()`, `handleFlangerParamChange()` |
| Depth | Clamp to [0.0, 1.0] | `Flanger::setDepth()`, `handleFlangerParamChange()` |
| Feedback | Clamp to [-1.0, +1.0]; internal clamp to [-0.98, +0.98] | `handleFlangerParamChange()` stores full range; `Flanger` clamps internally in process loop |
| Mix | Clamp to [0.0, 1.0] | `Flanger::setMix()`, `handleFlangerParamChange()` |
| Stereo Spread | Clamp to [0.0, 360.0] degrees | `Flanger::setStereoSpread()`, `handleFlangerParamChange()` |
| Waveform | Clamp to [0, 1] (Sine=0, Triangle=1) | `handleFlangerParamChange()` |
| ModulationType | Clamp to [0, 2] (None=0, Phaser=1, Flanger=2) | Processor param change handler |
| Sample rate | Must be > 0; default 44100 | `Flanger::prepare()` |
