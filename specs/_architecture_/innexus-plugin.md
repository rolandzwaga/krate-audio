# Innexus Plugin Architecture

[<- Back to Architecture Index](README.md)

**Plugin**: Innexus (Harmonic Analysis/Synthesis) | **Location**: `plugins/innexus/`

---

## Overview

Innexus is a harmonic analysis/resynthesis plugin that analyzes audio samples (or live sidechain input) into harmonic partial data and resynthesizes them via an oscillator bank and residual noise synthesizer. MIDI input controls which pitches are played using the analyzed timbral data.

---

## VST3 Components

| Component | Path | Purpose |
|-----------|------|---------|
| Processor | `plugins/innexus/src/processor/` | Audio processing (real-time) |
| Controller | `plugins/innexus/src/controller/` | UI state management |
| Entry | `plugins/innexus/src/entry.cpp` | Factory registration |
| IDs | `plugins/innexus/src/plugin_ids.h` | Parameter and component IDs |

---

## Audio Bus Configuration

| Bus | Type | Channels | Purpose |
|-----|------|----------|---------|
| Audio Output | `kMain` | Stereo | Synthesized audio output |
| MIDI Input | Event | N/A | Note on/off, pitch bend |
| Sidechain Input | `kAux` | Stereo | Live audio input for real-time analysis (bus index 0). Hosts route sidechain audio to this bus. The plugin downmixes stereo to mono internally before feeding the analysis pipeline. Registered in `Processor::initialize()` via `addAudioInput(STR16("Sidechain"), SpeakerArr::kStereo, BusTypes::kAux, BusInfo::kDefaultActive)`. |

---

## Plugin-Local DSP Components

### LiveAnalysisPipeline
**Path:** [live_analysis_pipeline.h](../../plugins/innexus/src/dsp/live_analysis_pipeline.h) / [live_analysis_pipeline.cpp](../../plugins/innexus/src/dsp/live_analysis_pipeline.cpp) | **Since:** 0.12.0

Real-time analysis pipeline for sidechain audio. Orchestrates the full analysis chain: PreProcessingPipeline, YinPitchDetector, STFT (short and optionally long window), PartialTracker, HarmonicModelBuilder, and SpectralCoringEstimator. Processes live audio incrementally into HarmonicFrame + ResidualFrame data that drives the existing oscillator bank and residual synthesizer.

```cpp
namespace Innexus {

class LiveAnalysisPipeline {
    // Lifecycle
    void prepare(double sampleRate, LatencyMode mode);
    void reset();
    void setLatencyMode(LatencyMode mode);
    void setResidualEnabled(bool enabled) noexcept;
    void setResponsiveness(float value) noexcept;  // M4: forwards to HarmonicModelBuilder

    // Audio feeding (real-time safe, called from audio thread)
    void pushSamples(const float* data, size_t count);

    // Frame retrieval
    [[nodiscard]] bool hasNewFrame() const noexcept;
    [[nodiscard]] const Krate::DSP::HarmonicFrame& consumeFrame() noexcept;
    [[nodiscard]] const Krate::DSP::ResidualFrame& consumeResidualFrame() noexcept;

    // Query
    [[nodiscard]] bool isPrepared() const noexcept;
};

} // namespace Innexus
```

**When to use:**
- Live sidechain mode: real-time audio-to-HarmonicFrame conversion from external audio input
- Any scenario requiring incremental (streaming) harmonic analysis on the audio thread
- Phase 21 (Multi-Source Blending): one instance per live source

**Instantiability note:** Designed for multiple simultaneous instances. No singletons, no static state. Each instance owns its own pipeline components and buffers. Safe for Phase 21 multi-source blending where multiple live analysis pipelines run in parallel.

**Latency modes:**
- `LowLatency`: Short STFT window only (<= 25ms analysis-to-synthesis latency)
- `HighPrecision`: Dual STFT windows (short + long) for better low-frequency resolution (~50-100ms latency)

**Key constraint:** Real-time safe. All buffers (YIN circular buffer, pre-processing buffer, spectral buffers) are pre-allocated during `prepare()`. No memory allocations, locks, exceptions, or I/O in `pushSamples()` or `runAnalysis()`.

**Dependencies:** Plugin-local (PreProcessingPipeline, dual_stft_config.h, plugin_ids.h), Shared DSP Library (YinPitchDetector, STFT, SpectralBuffer, PartialTracker, HarmonicModelBuilder, SpectralCoringEstimator)

---

### SampleAnalyzer
**Path:** [sample_analyzer.h](../../plugins/innexus/src/dsp/sample_analyzer.h) | **Since:** 0.11.0

Offline analysis of loaded audio samples into HarmonicFrame sequences. Runs on a background thread (not audio thread). Produces the same HarmonicFrame format as LiveAnalysisPipeline but processes entire samples at once rather than incrementally.

---

### PreProcessingPipeline
**Path:** [pre_processing_pipeline.h](../../plugins/innexus/src/dsp/pre_processing_pipeline.h) | **Since:** 0.11.0

Audio conditioning pipeline (DC blocking, normalization, pre-emphasis) shared by both SampleAnalyzer and LiveAnalysisPipeline. Processes audio in-place.

---

## M4 Musical Control Layer (Freeze, Morph, Harmonic Filter, Responsiveness)
**Since:** M4 (118-musical-control-layer)

Processor extension that inserts a creative control pipeline between the analysis output (HarmonicFrame + ResidualFrame) and the oscillator bank / residual synthesizer. Provides four user-facing parameters: Freeze, Morph Position, Harmonic Filter Type, and Responsiveness.

### Signal Chain Position

```
Analysis Output (HarmonicFrame + ResidualFrame)
    |
    v
[Manual Freeze Gate] --- captures State A (frozen snapshot) on engage
    |                     10ms crossfade on disengage
    v
[Confidence-Gated Auto-Freeze] (existing M1 mechanism, bypassed during manual freeze)
    |
    v
[Morph Interpolation] --- lerpHarmonicFrame/lerpResidualFrame between
    |                      frozen State A and live State B
    v
[Harmonic Filter] --- applyHarmonicMask with pre-computed filter mask
    |                  residual passes through unmodified
    v
Oscillator Bank + Residual Synthesizer (existing)
```

### Parameters (IDs 300-303)

| Parameter | ID | Type | Range | Default |
|-----------|-----|------|-------|---------|
| Freeze | `kFreezeId` (300) | Toggle (stepCount=1) | 0/1 | 0 (off) |
| Morph Position | `kMorphPositionId` (301) | RangeParameter | 0.0 - 1.0 | 0.0 |
| Harmonic Filter | `kHarmonicFilterTypeId` (302) | StringListParameter | 5 presets | AllPass |
| Responsiveness | `kResponsivenessId` (303) | RangeParameter | 0.0 - 1.0 | 0.5 |

### Manual Freeze State (Separate from Auto-Freeze)

Manual freeze uses its own member variables (`manualFreezeActive_`, `manualFrozenFrame_`, `manualFrozenResidualFrame_`), entirely independent of the confidence-gated auto-freeze mechanism (`isFrozen_`, `lastGoodFrame_`). When manual freeze is active, it takes priority over auto-freeze. This separation prevents interference between the two mechanisms (manual = creative intent, auto = stability fallback).

On freeze disengage, a 10ms crossfade smooths the transition from frozen to live frames, preventing audible clicks.

### Morph Interpolation Pipeline

When manual freeze is active, the morph position (smoothed via `OnePoleSmoother` at ~5-10ms time constant) controls linear interpolation between the frozen State A and the current live State B using `lerpHarmonicFrame()` and `lerpResidualFrame()` from `harmonic_frame_utils.h` (shared DSP Layer 2). When freeze is off, morph has no effect (live frame passes through unmodified).

### Harmonic Filter Mask Pre-Computation

The filter mask (`std::array<float, kMaxPartials>`) is recomputed via `computeHarmonicMask()` only when the filter type parameter changes, not every frame. The pre-computed mask is applied to the morphed frame via `applyHarmonicMask()` each frame. The residual component passes through unmodified (harmonic filter only affects partials). The oscillator bank's existing ~2ms per-partial amplitude smoothing handles transitions when the filter type changes.

### Responsiveness Forwarding

The Responsiveness parameter is forwarded to `LiveAnalysisPipeline::setResponsiveness()`, which in turn calls `HarmonicModelBuilder::setResponsiveness()`. This controls the dual-timescale blend between fast-tracking and slow-stable partial estimation. Value 0.0 = slow/stable, 1.0 = fast/responsive, 0.5 = default (matches M1/M3 behavior). Only affects live sidechain mode; has no effect in sample playback mode.

### State Persistence (Version 4)

State version bumped from 3 (M3) to 4. Four new fields appended after M3 data in byte order: `int8 freeze`, `float morphPosition`, `int32 filterType`, `float responsiveness`. Loading a v3 state applies M4 defaults (freeze=off, morph=0.0, filter=AllPass, responsiveness=0.5). Backward compatible.

### Key Design Patterns

- **Pre-allocated storage**: All frozen frames, morph intermediates, and filter masks are member variables. Zero heap allocations on the audio thread.
- **Atomic parameter exchange**: All four parameters use `std::atomic<float>` for thread-safe communication from `processParameterChanges()` to `process()`.
- **Frame-rate processing**: Freeze/morph/filter operate per analysis frame (~86 Hz at 44.1kHz/512 hop), not per audio sample. Combined overhead is negligible (<0.1% CPU).
- **Smooth transitions**: Morph position smoothed via OnePoleSmoother; filter changes handled by oscillator bank's amplitude smoothing; freeze disengage uses explicit 10ms crossfade.

---

## M5 Harmonic Memory (Snapshot Capture & Recall)
**Since:** M5 (119-harmonic-memory)

Adds a Harmonic Memory system: 8 pre-allocated memory slots for storing and recalling normalized timbral snapshots. Capture extracts L2-normalized harmonic data from the current analysis/freeze/morph state. Recall loads a stored snapshot into the existing manual freeze infrastructure for MIDI playback. State persistence extended to version 5. JSON export/import via IMessage.

### Parameters (IDs 304-306)

| Parameter | ID | Type | Range | Default |
|-----------|-----|------|-------|---------|
| Memory Slot | `kMemorySlotId` (304) | StringListParameter | 8 entries ("Slot 1"-"Slot 8") | Slot 1 |
| Memory Capture | `kMemoryCaptureId` (305) | Momentary trigger | 0/1 | 0 |
| Memory Recall | `kMemoryRecallId` (306) | Momentary trigger | 0/1 | 0 |

### Trigger Detection Idiom

Capture and Recall use a 0-to-1 transition detection pattern:

```cpp
const float current = memoryCapture_.load(std::memory_order_relaxed);
if (current > 0.5f && previousCaptureTrigger_ <= 0.5f) {
    // Fire capture logic
}
previousCaptureTrigger_ = current;
// Auto-reset: memoryCapture_.store(0.0f); + notify host via outputParameterChanges
```

The auto-reset ensures each button press fires exactly once, and the host UI reflects the reset via `outputParameterChanges`.

### Capture Source Selection Logic

Capture selects the source frame according to priority:

1. **Manual freeze active + morph > 1e-6**: Post-morph blended frame (`morphedFrame_`/`morphedResidualFrame_`)
2. **Manual freeze active + morph == 0**: Frozen frame (`manualFrozenFrame_`/`manualFrozenResidualFrame_`)
3. **No freeze + sidechain mode**: Current live analysis frame (pre-filter)
4. **No freeze + sample mode**: Current sample playback analysis frame
5. **No analysis active**: Empty/default-constructed frame

In all cases, capture reads pre-filter data (harmonic filter mask is NOT baked into stored amplitudes).

### Recall Integration with Freeze/Morph/Filter

Recall loads a stored snapshot into the existing manual freeze infrastructure:

```
Recall -> recallSnapshotToFrame() -> manualFrozenFrame_ + manualFrozenResidualFrame_
                                  -> manualFreezeActive_ = true
                                  -> existing morph/filter/crossfade pipeline applies
```

- Slot-to-slot recall triggers a 10ms crossfade (reuses existing `freezeRecoveryCrossfadeActive_`)
- Recalled snapshot becomes State A for morph (morph 0.0 = fully recalled, 1.0 = fully live)
- Harmonic filter applies post-morph, as in M4
- Empty slot recall is silently ignored (no state change)

### Memory Slot Storage

```cpp
std::array<Krate::DSP::MemorySlot, 8> memorySlots_{};  // ~6.8 KB total, pre-allocated
```

Each `MemorySlot` has an `occupied` flag and a `HarmonicSnapshot`. Slots are independent -- capture into one slot does not affect any other slot. Capture into a currently-recalled slot does NOT update `manualFrozenFrame_` (the live freeze frame is a separate copy).

### State Persistence (Version 5)

State version bumped from 4 (M4) to 5. Memory slot data appended after M4 payload:

```
[V4 data] [selectedSlotIndex: int32] [slot0: int8 occupied, HarmonicSnapshot if occupied] ... [slot7: ...]
```

- Loading a v4 state initializes all 8 slots to empty and selected slot to 0. Backward compatible.
- Freeze/recall state is NOT persisted (freeze defaults to off on load, matching M4 behavior).
- Selected slot index IS persisted.

### JSON Export/Import via IMessage (notify() Pattern)

JSON export/import runs on the controller/UI thread. The controller packages imported snapshot binary and target slot index into an `IMessage` ("HarmonicSnapshotImport"), which the processor receives in `notify()` and writes via a fixed-size `memcpy` (no allocation, no locks).

```cpp
// Controller -> Processor via IMessage
message.setMessageID("HarmonicSnapshotImport");
attrs->setInt("slotIndex", targetSlot);
attrs->setBinary("snapshotData", &snapshot, sizeof(HarmonicSnapshot));
```

### Key Design Patterns

- **Pre-allocated storage**: All 8 memory slots are member variables. Zero heap allocations on the audio thread for capture/recall.
- **Atomic parameter exchange**: Memory Slot, Capture, and Recall use `std::atomic<float>` for thread-safe communication.
- **Transition detection**: Capture and Recall detect 0-to-1 transitions with `previousCaptureTrigger_`/`previousRecallTrigger_` tracking.
- **Auto-reset triggers**: After firing, trigger parameters are reset to 0.0 and host is notified via `outputParameterChanges`.
- **Copy semantics**: Recall copies snapshot data into `manualFrozenFrame_`; subsequent captures into the same slot do not alter the live freeze frame.

---

## M6 Creative Extensions (Evolution, Modulators, Blending, Cross-Synthesis, Stereo Spread)
**Since:** M6 (120-creative-extensions)

Adds five creative extension features: (1) cross-synthesis timbral blend, (2) stereo partial spread and detune in the oscillator bank, (3) autonomous evolution engine for timbral drift, (4) two independent harmonic LFO modulators, and (5) multi-source blending from weighted memory slots. Adds 31 new parameters (IDs 600-649) and extends state persistence to version 6.

### Signal Chain Position (FR-049)

```
Source Selection (sample/live/recalled snapshot)
    |
    v
[Multi-Source Blend] --- if blendEnabled: weighted sum of up to 8 slots + 1 live
    |                     (overrides evolution and normal recall path, FR-052)
    v
[Evolution Engine] --- if evolutionEnabled && !blendEnabled: autonomous drift
    |                   through occupied memory slot waypoints
    v
[Cross-Synthesis Timbral Blend] --- lerp between pure harmonic reference and
    |                                 source model (blend=0: pure 1/n, blend=1: source)
    v
[Harmonic Filter] (M4, unchanged)
    |
    v
[Harmonic Modulators] --- 2 independent LFOs modulate per-partial amp/freq/pan
    |
    v
Oscillator Bank (processStereo with spread + detune) + Residual Synthesizer
```

### Parameters (IDs 600-649)

| Parameter | ID | Type | Range | Default |
|-----------|-----|------|-------|---------|
| Timbral Blend | `kTimbralBlendId` (600) | RangeParameter | 0.0 - 1.0 | 1.0 |
| Stereo Spread | `kStereoSpreadId` (601) | RangeParameter | 0.0 - 1.0 | 0.0 |
| Evolution Enable | `kEvolutionEnableId` (602) | Toggle | 0/1 | 0 (off) |
| Evolution Speed | `kEvolutionSpeedId` (603) | RangeParameter | 0.01 - 10.0 Hz | 0.1 |
| Evolution Depth | `kEvolutionDepthId` (604) | RangeParameter | 0.0 - 1.0 | 0.5 |
| Evolution Mode | `kEvolutionModeId` (605) | StringListParameter | Cycle/PingPong/Random Walk | Cycle |
| Mod 1 Enable | `kMod1EnableId` (610) | Toggle | 0/1 | 0 (off) |
| Mod 1 Waveform | `kMod1WaveformId` (611) | StringListParameter | Sine/Triangle/Square/Saw/Random S&H | Sine |
| Mod 1 Rate | `kMod1RateId` (612) | RangeParameter | 0.01 - 20.0 Hz | 1.0 |
| Mod 1 Depth | `kMod1DepthId` (613) | RangeParameter | 0.0 - 1.0 | 0.0 |
| Mod 1 Range Start | `kMod1RangeStartId` (614) | RangeParameter | 1 - 48 | 1 |
| Mod 1 Range End | `kMod1RangeEndId` (615) | RangeParameter | 1 - 48 | 48 |
| Mod 1 Target | `kMod1TargetId` (616) | StringListParameter | Amplitude/Frequency/Pan | Amplitude |
| Mod 2 Enable | `kMod2EnableId` (620) | Toggle | 0/1 | 0 (off) |
| Mod 2 Waveform | `kMod2WaveformId` (621) | StringListParameter | (same as Mod 1) | Sine |
| Mod 2 Rate | `kMod2RateId` (622) | RangeParameter | 0.01 - 20.0 Hz | 1.0 |
| Mod 2 Depth | `kMod2DepthId` (623) | RangeParameter | 0.0 - 1.0 | 0.0 |
| Mod 2 Range Start | `kMod2RangeStartId` (624) | RangeParameter | 1 - 48 | 1 |
| Mod 2 Range End | `kMod2RangeEndId` (625) | RangeParameter | 1 - 48 | 48 |
| Mod 2 Target | `kMod2TargetId` (626) | StringListParameter | Amplitude/Frequency/Pan | Amplitude |
| Detune Spread | `kDetuneSpreadId` (630) | RangeParameter | 0.0 - 1.0 | 0.0 |
| Blend Enable | `kBlendEnableId` (640) | Toggle | 0/1 | 0 (off) |
| Blend Slot Weight 1-8 | `kBlendSlotWeight1Id`-`kBlendSlotWeight8Id` (641-648) | RangeParameter | 0.0 - 1.0 | 0.0 |
| Blend Live Weight | `kBlendLiveWeightId` (649) | RangeParameter | 0.0 - 1.0 | 0.0 |

### EvolutionEngine
**Path:** [evolution_engine.h](../../plugins/innexus/src/dsp/evolution_engine.h) | **Since:** M6

Autonomous timbral drift engine that drives a morph position through occupied memory slot waypoints. The phase is global (not per-note, FR-020) and free-running. Supports three traversal modes: Cycle (wrap), PingPong (bounce at endpoints), and RandomWalk (random drift within depth range using Xorshift32 RNG).

```cpp
namespace Innexus {

enum class EvolutionMode : int { Cycle, PingPong, RandomWalk };

class EvolutionEngine {
    // Lifecycle
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Configuration
    void updateWaypoints(const std::array<Krate::DSP::MemorySlot, 8>& slots) noexcept;
    void setMode(EvolutionMode mode) noexcept;
    void setSpeed(float speedHz) noexcept;      // [0.01, 10.0] Hz
    void setDepth(float depth) noexcept;        // [0.0, 1.0]
    void setManualOffset(float offset) noexcept; // Coexists with manual morph (FR-021)

    // Per-sample advance (audio thread)
    void advance() noexcept;

    // Frame interpolation (per analysis frame)
    [[nodiscard]] bool getInterpolatedFrame(
        const std::array<Krate::DSP::MemorySlot, 8>& slots,
        Krate::DSP::HarmonicFrame& frame,
        Krate::DSP::ResidualFrame& residual) const noexcept;

    // Query
    [[nodiscard]] float getPosition() const noexcept;
    [[nodiscard]] int getNumWaypoints() const noexcept;
};

} // namespace Innexus
```

**When to use:**
- Autonomous timbral animation between stored memory slot snapshots
- "Evolving pad" effects where the timbre drifts continuously without user interaction
- Creative performance: speed and depth are automatable, mode selectable at runtime

**Pipeline position:** After source selection, before cross-synthesis timbral blend. Active only when `evolutionEnabled && !blendEnabled` (FR-022, FR-052). Uses `lerpHarmonicFrame()` and `lerpResidualFrame()` for waypoint interpolation (FR-019). Requires >= 2 occupied memory slots to produce output; returns false with < 2 waypoints.

**Key constraints:** All methods `noexcept`, no heap allocations. Fixed-size `std::array<int, 8>` for waypoint indices. Phase does not reset on MIDI note events (FR-020). Manual offset coexistence: `effectivePos = clamp(phase * depth + manualOffset, 0, 1)` (FR-021).

---

### HarmonicModulator
**Path:** [harmonic_modulator.h](../../plugins/innexus/src/dsp/harmonic_modulator.h) | **Since:** M6

LFO-driven per-partial animation with configurable waveform, rate, depth, target partial range, and modulation target. Two independent instances are used in the processor (Modulator 1 and Modulator 2). The LFO is free-running (phase initialized to 0.0 in `prepare()`, never resets on MIDI note events per FR-029, FR-051).

```cpp
namespace Innexus {

enum class ModulatorWaveform : int { Sine, Triangle, Square, Saw, RandomSH };
enum class ModulatorTarget : int { Amplitude, Frequency, Pan };

class HarmonicModulator {
    static constexpr float kModMaxCents = 50.0f;  // Max frequency modulation range
    static constexpr float kModMaxPan = 0.5f;     // Max pan modulation range

    // Lifecycle
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Configuration
    void setWaveform(ModulatorWaveform waveform) noexcept;
    void setRate(float rateHz) noexcept;           // [0.01, 20.0] Hz
    void setDepth(float depth) noexcept;           // [0.0, 1.0]
    void setRange(int start, int end) noexcept;    // 1-based partial range [1, 48]
    void setTarget(ModulatorTarget target) noexcept;

    // Per-sample advance (audio thread)
    void advance() noexcept;

    // Modulation application (per analysis frame)
    void applyAmplitudeModulation(Krate::DSP::HarmonicFrame& frame) const noexcept;
    void getFrequencyMultipliers(std::array<float, kMaxPartials>& multipliers) const noexcept;
    void getPanOffsets(std::array<float, kMaxPartials>& offsets) const noexcept;

    // Query
    [[nodiscard]] float getCurrentValue() const noexcept;      // Bipolar [-1, +1]
    [[nodiscard]] float getCurrentValueUnipolar() const noexcept; // [0, 1]
    [[nodiscard]] float getPhase() const noexcept;             // [0, 1)
};

} // namespace Innexus
```

**When to use:**
- Per-partial amplitude animation (tremolo/shimmer effects on specific harmonic ranges)
- Per-partial frequency animation (vibrato/detuning effects on partial subsets)
- Per-partial pan animation (spatial movement of harmonic content)
- Two modulators can overlap ranges: amplitude effects multiply, frequency/pan effects add (FR-028)

**Pipeline position:** Applied after harmonic filter, before oscillator bank (FR-049 step 4). Amplitude modulation modifies `HarmonicFrame` amplitudes directly. Frequency multipliers are applied via `oscillatorBank_.applyExternalFrequencyMultipliers()`. Pan offsets are applied via `oscillatorBank_.applyPanOffsets()`.

**Modulation formulas:**
- Amplitude (FR-025): `effectiveAmp = modelAmp * (1 - depth + depth * lfoUnipolar)`
- Frequency (FR-026): `multiplier = pow(2, depth * lfoBipolar * 50 / 1200)`
- Pan (FR-027): `offset = depth * lfoBipolar * 0.5`

**LFO waveforms (formula-based, no wavetable, no heap):**
- Sine: `sin(2*pi*phase)`
- Triangle: `4*|phase - 0.5| - 1` (phase=0: +1, phase=0.5: -1)
- Square: `phase < 0.5 ? +1 : -1`
- Saw: `2*phase - 1`
- Random S&H: held random value (Xorshift32), updated on phase wrap

---

### HarmonicBlender
**Path:** [harmonic_blender.h](../../plugins/innexus/src/dsp/harmonic_blender.h) | **Since:** M6

Multi-source spectral blending from up to 8 stored memory slot snapshots plus 1 optional live analysis frame. Each source has an independent weight that is normalized internally before blending (FR-035). Empty slots contribute zero regardless of weight. When enabled, overrides both evolution and normal recall/freeze path (FR-052).

```cpp
namespace Innexus {

class HarmonicBlender {
    static constexpr int kNumSlots = 8;

    // Weight configuration
    void setSlotWeight(int slotIndex, float weight) noexcept;  // [0, 7], [0.0, 1.0]
    void setLiveWeight(float weight) noexcept;                 // [0.0, 1.0]

    // Blending (per analysis frame)
    [[nodiscard]] bool blend(
        const std::array<Krate::DSP::MemorySlot, 8>& slots,
        const Krate::DSP::HarmonicFrame& liveFrame,
        const Krate::DSP::ResidualFrame& liveResidual,
        bool hasLiveSource,
        Krate::DSP::HarmonicFrame& frame,
        Krate::DSP::ResidualFrame& residual) const noexcept;

    // Query (after blend() call)
    [[nodiscard]] float getEffectiveSlotWeight(int slotIndex) const noexcept;
    [[nodiscard]] float getEffectiveLiveWeight() const noexcept;
};

} // namespace Innexus
```

**When to use:**
- Combining timbral characteristics from multiple stored snapshots into a single output
- Live source mixing: blend real-time sidechain analysis with stored snapshots
- Creative layering: weight different timbral snapshots to create hybrid timbres

**Pipeline position:** First in the M6 pipeline (FR-049 step 1). When `blendEnabled`, the blended output replaces the normal source selection (freeze/recall/morph) and skips evolution (FR-052). The blended frame then flows through cross-synthesis, harmonic filter, modulators, and oscillator bank as normal.

**Blending formula (FR-037):**
- `blendedAmp_n = sum(effectiveWeight_i * sourceAmp_n_i)`
- `blendedRelFreq_n = sum(effectiveWeight_i * sourceRelFreq_n_i)`
- `blendedResidualBands_k = sum(effectiveWeight_i * sourceBands_k_i)`
- Weight normalization (R-006): `effectiveWeight_i = weight_i / totalWeight`

**Key constraints:** All methods `noexcept`, no heap allocations. Fixed-size arrays only. Missing partials (beyond a source's numPartials) contribute zero amplitude (FR-038). All-zero weights produce silence (FR-039, returns false). Single-source blend at weight=1.0 produces output identical to direct Memory Recall (SC-011).

---

### State Persistence (Version 6)

State version bumped from 5 (M5) to 6. All 31 M6 parameter values appended after M5 data in normalized float format. Loading a v5 state initializes all M6 parameters to defaults (timbralBlend=1.0, stereoSpread=0.0, all others=0.0/off). Backward compatible.

### Key Design Patterns

- **Pre-allocated storage**: All M6 DSP classes (EvolutionEngine, HarmonicModulator, HarmonicBlender) use fixed-size arrays. Zero heap allocations on the audio thread.
- **Atomic parameter exchange**: All 31 M6 parameters use `std::atomic<float>` for thread-safe communication from `processParameterChanges()` to `process()`.
- **Parameter smoothing**: OnePoleSmoother (~5-10ms) on all continuous parameters to prevent clicks during automation (SC-007).
- **Pipeline priority**: blendEnabled overrides evolutionEnabled (FR-052). Both override normal recall/freeze path.
- **Frame-rate processing**: Evolution, modulators, and blending operate per analysis frame (~86 Hz at 44.1kHz/512 hop). Only evolution phase advance and modulator LFO advance are per-sample.
- **Stereo output in KrateDSP**: `processStereo()` added to `HarmonicOscillatorBank` (Layer 2, shared DSP library) while plugin-local DSP (evolution, modulators, blender) stays in `plugins/innexus/src/dsp/` per FR-046.

---

## M7 Plugin UI (Display Data Pipeline, Custom Views, Modulator Sub-Controller)
**Since:** M7 (121-plugin-ui)

Full VSTGUI interface for Innexus: 800x600 fixed editor with 48 parameter controls, 5 custom `CView` subclasses for real-time display, a reusable modulator template with `DelegationController`-based sub-controller for tag remapping, and an `IMessage`-based processor-to-controller data pipeline with 30ms `CVSTGUITimer` polling.

### Display Data Pipeline (Canonical Real-Time Display Update Strategy)

The Innexus display data pipeline uses `IMessage` for processor-to-controller communication combined with a 30ms `CVSTGUITimer` for UI refresh. This is the canonical pattern for transferring real-time analysis data to custom views:

1. **Processor** populates a `DisplayData` struct at the end of each `process()` call and sends it via `allocateMessage()`/`sendMessage()` with message ID `"InnexusDisplayData"`.
2. **Controller** receives the message in `notify()`, deserializes the binary payload via `memcpy` into `cachedDisplayData_`.
3. **Timer** (`CVSTGUITimer`, 30ms interval, started in `didOpen()`, stopped in `willClose()`) fires `onDisplayTimerFired()`, which distributes the cached data to all custom views via their `updateData()` methods and triggers `setDirty(true)` for VSTGUI redraw.
4. **Frame counter** (`DisplayData::frameCounter`) prevents redundant view updates when no new analysis frames have arrived.

### DisplayData
**Path:** [display_data.h](../../plugins/innexus/src/controller/display_data.h) | **Since:** M7

Flat POD struct transferred from Processor to Controller via IMessage binary payload. Contains all data needed by the 5 custom views.

```cpp
namespace Innexus {

struct DisplayData
{
    float partialAmplitudes[48]{};    // Linear amplitudes [0.0, ~1.0]
    uint8_t partialActive[48]{};      // 1 = active, 0 = filtered/attenuated
    float f0 = 0.0f;                  // Fundamental frequency (Hz)
    float f0Confidence = 0.0f;        // [0.0, 1.0]
    uint8_t slotOccupied[8]{};        // 1 = memory slot occupied
    float evolutionPosition = 0.0f;   // Combined morph position [0.0, 1.0]
    float manualMorphPosition = 0.0f; // Manual knob value [0.0, 1.0]
    float mod1Phase = 0.0f;           // LFO phase [0.0, 1.0]
    float mod2Phase = 0.0f;           // LFO phase [0.0, 1.0]
    bool mod1Active = false;          // Modulator 1 enabled & depth > 0
    bool mod2Active = false;          // Modulator 2 enabled & depth > 0
    uint32_t frameCounter = 0;        // Monotonic, incremented per new frame
};

} // namespace Innexus
```

**When to use:**
- Any new custom view that needs real-time data from the processor should add fields to this struct and consume them in `onDisplayTimerFired()`.
- The struct is `memcpy`-safe (POD). No pointers, no strings, no virtual functions.

---

### HarmonicDisplayView
**Path:** [harmonic_display_view.h](../../plugins/innexus/src/controller/views/harmonic_display_view.h) / [harmonic_display_view.cpp](../../plugins/innexus/src/controller/views/harmonic_display_view.cpp) | **Since:** M7

Custom `CView` subclass that renders 48 vertical bars representing harmonic partial amplitudes. Uses dB scaling via `amplitudeToBarHeight()` for perceptually uniform display. Active partials are drawn in the accent color; filtered/attenuated partials are dimmed.

```cpp
namespace Innexus {

class HarmonicDisplayView : public VSTGUI::CView
{
    // Data injection (called from timer)
    void updateData(const DisplayData& data);

    // Rendering
    void draw(VSTGUI::CDrawContext* context) override;

    // Utility (public for testing)
    static float amplitudeToBarHeight(float amp, float viewHeight);

    // Test accessors
    bool hasData() const;
    float getAmplitude(int index) const;
    bool isActive(int index) const;
};

} // namespace Innexus
```

**When to use:** Primary spectral visualization in the Innexus editor. Consumes `DisplayData::partialAmplitudes` and `DisplayData::partialActive`.

---

### ConfidenceIndicatorView
**Path:** [confidence_indicator_view.h](../../plugins/innexus/src/controller/views/confidence_indicator_view.h) / [confidence_indicator_view.cpp](../../plugins/innexus/src/controller/views/confidence_indicator_view.cpp) | **Since:** M7

Custom `CView` subclass that displays the fundamental frequency (F0) detection confidence as a color-coded horizontal bar with a note name label. Color transitions from red (low confidence) through yellow to green (high confidence) via `getConfidenceColor()`.

```cpp
namespace Innexus {

class ConfidenceIndicatorView : public VSTGUI::CView
{
    // Data injection (called from timer)
    void updateData(const DisplayData& data);

    // Rendering
    void draw(VSTGUI::CDrawContext* context) override;

    // Utility (public for testing)
    static VSTGUI::CColor getConfidenceColor(float confidence);
    static std::string freqToNoteName(float freq);

    // Test accessors
    float getConfidence() const;
    float getF0() const;
};

} // namespace Innexus
```

**When to use:** Displays pitch detection quality alongside the spectral display. Consumes `DisplayData::f0Confidence` and `DisplayData::f0`.

---

### MemorySlotStatusView
**Path:** [memory_slot_status_view.h](../../plugins/innexus/src/controller/views/memory_slot_status_view.h) / [memory_slot_status_view.cpp](../../plugins/innexus/src/controller/views/memory_slot_status_view.cpp) | **Since:** M7

Custom `CView` subclass that renders 8 circles indicating which memory slots are occupied. Filled circles = occupied, outlined circles = empty.

```cpp
namespace Innexus {

class MemorySlotStatusView : public VSTGUI::CView
{
    // Data injection (called from timer)
    void updateData(const DisplayData& data);

    // Rendering
    void draw(VSTGUI::CDrawContext* context) override;

    // Test accessor
    bool isSlotOccupied(int index) const;
};

} // namespace Innexus
```

**When to use:** Visual feedback for memory slot state in the Memory section. Consumes `DisplayData::slotOccupied`.

---

### EvolutionPositionView
**Path:** [evolution_position_view.h](../../plugins/innexus/src/controller/views/evolution_position_view.h) / [evolution_position_view.cpp](../../plugins/innexus/src/controller/views/evolution_position_view.cpp) | **Since:** M7

Custom `CView` subclass that renders a horizontal track with a playhead indicator showing the evolution engine position, plus an optional ghost indicator for the manual morph position.

```cpp
namespace Innexus {

class EvolutionPositionView : public VSTGUI::CView
{
    // Data injection (called from timer)
    void updateData(const DisplayData& data, bool evolutionActive);

    // Rendering
    void draw(VSTGUI::CDrawContext* context) override;

    // Test accessors
    float getPosition() const;
    float getManualPosition() const;
    bool getShowGhost() const;
};

} // namespace Innexus
```

**When to use:** Visual feedback for evolution engine traversal in the Evolution section. Consumes `DisplayData::evolutionPosition` and `DisplayData::manualMorphPosition`. The `evolutionActive` flag controls ghost indicator visibility.

---

### ModulatorActivityView
**Path:** [modulator_activity_view.h](../../plugins/innexus/src/controller/views/modulator_activity_view.h) / [modulator_activity_view.cpp](../../plugins/innexus/src/controller/views/modulator_activity_view.cpp) | **Since:** M7

Custom `CView` subclass that renders a pulsing circular indicator showing LFO activity for a single modulator. The pulse intensity follows the LFO phase. When inactive (disabled or depth=0), the indicator is dimmed.

```cpp
namespace Innexus {

class ModulatorActivityView : public VSTGUI::CView
{
    // Configuration
    void setModIndex(int index);

    // Data injection (called from timer)
    void updateData(float phase, bool active);

    // Rendering
    void draw(VSTGUI::CDrawContext* context) override;

    // Test accessors
    int getModIndex() const;
    float getPhase() const;
    bool isActive() const;
};

} // namespace Innexus
```

**When to use:** Visual feedback for modulator LFO state inside the modulator template. Two instances are used (one per modulator). Consumes `DisplayData::mod1Phase`/`mod2Phase` and `DisplayData::mod1Active`/`mod2Active`.

---

### ModulatorSubController
**Path:** [modulator_sub_controller.h](../../plugins/innexus/src/controller/modulator_sub_controller.h) / [modulator_sub_controller.cpp](../../plugins/innexus/src/controller/modulator_sub_controller.cpp) | **Since:** M7

`DelegationController` subclass that enables a single modulator template in `editor.uidesc` to be instantiated twice (for Modulator 1 and Modulator 2) with automatic parameter tag remapping. Created by `Controller::createSubController()` when the template name matches `"ModulatorController"`.

```cpp
namespace Innexus {

class ModulatorSubController : public VSTGUI::DelegationController
{
    ModulatorSubController(int modIndex, VSTGUI::IController* parent);

    // Tag remapping: resolves generic names (e.g., "Mod.Enable") to
    // concrete parameter IDs based on modIndex (0 -> kMod1*, 1 -> kMod2*)
    int32_t getTagForName(VSTGUI::UTF8StringPtr name,
                          int32_t registeredTag) const override;

    // Wires ModulatorActivityView with correct mod index
    VSTGUI::CView* verifyView(VSTGUI::CView* view,
                               const VSTGUI::UIAttributes& attrs,
                               const VSTGUI::IUIDescription* desc) override;

    int getModIndex() const;
};

} // namespace Innexus
```

**Tag remapping table:**

| Template Tag Name | Mod 1 (modIndex=0) | Mod 2 (modIndex=1) |
|---|---|---|
| `Mod.Enable` | `kMod1EnableId` (610) | `kMod2EnableId` (620) |
| `Mod.Waveform` | `kMod1WaveformId` (611) | `kMod2WaveformId` (621) |
| `Mod.Rate` | `kMod1RateId` (612) | `kMod2RateId` (622) |
| `Mod.Depth` | `kMod1DepthId` (613) | `kMod2DepthId` (623) |
| `Mod.RangeStart` | `kMod1RangeStartId` (614) | `kMod2RangeStartId` (624) |
| `Mod.RangeEnd` | `kMod1RangeEndId` (615) | `kMod2RangeEndId` (625) |
| `Mod.Target` | `kMod1TargetId` (616) | `kMod2TargetId` (626) |

**When to use:** Automatically created by the controller. The template definition in `editor.uidesc` uses generic `Mod.*` tag names that are resolved at runtime by this sub-controller. This pattern avoids duplicating the entire modulator UI layout for each modulator instance.

### Controller UI Infrastructure
**Path:** [controller.h](../../plugins/innexus/src/controller/controller.h) / [controller.cpp](../../plugins/innexus/src/controller/controller.cpp) | **Since:** M7

The Controller extends `EditControllerEx1` and `VST3EditorDelegate` to provide:

- `createView()`: Returns `VST3Editor` with `editor.uidesc`
- `createCustomView()`: Instantiates the 5 custom views by `custom-view-name` attribute
- `createSubController()`: Instantiates `ModulatorSubController` for `"ModulatorController"` sub-controller name
- `didOpen()`: Starts 30ms `CVSTGUITimer` for display data polling
- `willClose()`: Stops timer, nulls all custom view observation pointers
- `notify()`: Receives `"InnexusDisplayData"` messages from Processor, deserializes into `cachedDisplayData_`
- `onDisplayTimerFired()`: Distributes cached display data to all custom views

**Observation pointer pattern:** The controller holds raw pointers to custom views (VSTGUI owns the view lifetime). These are set in `createCustomView()` and nulled in `willClose()` to prevent dangling pointer access.

### Key Design Patterns

- **IMessage + 30ms timer**: Canonical real-time display data update strategy for Innexus. Processor sends binary `DisplayData` via IMessage; controller caches it and distributes to views at ~33fps via timer callback. This avoids any audio thread blocking.
- **Frame counter deduplication**: `DisplayData::frameCounter` is checked in `onDisplayTimerFired()` to skip redundant view updates when no new analysis frames have arrived since the last timer tick.
- **Template + sub-controller reuse**: Single modulator template in `editor.uidesc` instantiated twice with `ModulatorSubController` handling parameter tag remapping. Eliminates UI layout duplication.
- **Vector-drawn shared components**: All standard controls use `Krate::Plugins::ArcKnob`, `ToggleButton`, `ActionButton`, `BipolarSlider`, and `FieldsetContainer` from `plugins/shared/src/ui/`. No bitmap assets required.
- **Fixed 800x600 layout**: Innexus editor size is fixed at 800x600 pixels, defined in `editor.uidesc`.

---

## Spec A Harmonic Physics (Warmth, Coupling, Dynamics)
**Since:** Spec A (122-harmonic-physics)

Adds a physics-based harmonic processing system with three sub-processors that operate as HarmonicFrame transforms, modifying only partial amplitudes. The chain runs between the existing frame source pipeline (morph/filter/modulators) and `oscillatorBank_.loadFrame()`. All four parameters default to 0.0 for bit-exact bypass.

### Signal Chain Position

```
[Harmonic Modulators] (M6, amplitude modulation applied)
    |
    v
[Harmonic Physics: Coupling -> Warmth -> Dynamics]
    |
    v
oscillatorBank_.loadFrame()
```

The `applyHarmonicPhysics()` method is called immediately before every `oscillatorBank_.loadFrame()` call site in the processor (7 sites total). This ensures physics transforms apply regardless of which code path produces the frame (sample playback, live analysis, freeze/recall, morph, evolution, blend).

### Parameters (IDs 700-703)

| Parameter | ID | Type | Range | Default |
|-----------|-----|------|-------|---------|
| Warmth | `kWarmthId` (700) | RangeParameter | 0.0 - 1.0 | 0.0 |
| Coupling | `kCouplingId` (701) | RangeParameter | 0.0 - 1.0 | 0.0 |
| Stability | `kStabilityId` (702) | RangeParameter | 0.0 - 1.0 | 0.0 |
| Entropy | `kEntropyId` (703) | RangeParameter | 0.0 - 1.0 | 0.0 |

### HarmonicPhysics
**Path:** [harmonic_physics.h](../../plugins/innexus/src/dsp/harmonic_physics.h) | **Since:** Spec A

Header-only physics-based harmonic processing system. Processes `HarmonicFrame` in-place, modifying only partial amplitudes. Follows the same plugin-local DSP pattern as `HarmonicModulator` and `EvolutionEngine`: header-only, `prepare()`/`reset()`/`processFrame()` interface.

```cpp
namespace Innexus {

struct AgentState {
    std::array<float, kMaxPartials> amplitude{};    // Per-partial tracked amplitude
    std::array<float, kMaxPartials> velocity{};     // Per-partial rate of change
    std::array<float, kMaxPartials> persistence{};  // Per-partial stability score [0, 1]
    std::array<float, kMaxPartials> energyShare{};  // Per-partial energy allocation
};

class HarmonicPhysics {
    // Lifecycle
    void prepare(double sampleRate, int hopSize) noexcept;
    void reset() noexcept;

    // Per-frame processing (audio thread, real-time safe)
    void processFrame(Krate::DSP::HarmonicFrame& frame) noexcept;

    // Parameter setters
    void setWarmth(float value) noexcept;     // [0.0, 1.0]
    void setCoupling(float value) noexcept;   // [0.0, 1.0]
    void setStability(float value) noexcept;  // [0.0, 1.0]
    void setEntropy(float value) noexcept;    // [0.0, 1.0]
};

} // namespace Innexus
```

**Processing chain order** (inside `processFrame()`):
1. **Coupling** (`applyCoupling`): Nearest-neighbor energy sharing between adjacent partials. Reads amplitudes into a temporary buffer, blends neighbors with coupling weight, normalizes by sum-of-squares to exactly conserve energy. Boundary partials handled safely.
2. **Warmth** (`applyWarmth`): Tanh-based soft saturation of harmonic amplitudes using `amp_out[i] = tanh(drive * amp[i]) / tanh(drive)` with `drive = exp(warmth * ln(8))`. Compresses dominant partials and relatively boosts quiet ones. Output RMS never exceeds input RMS.
3. **Dynamics** (`applyDynamics`): Per-partial stateful agent system with inertia (Stability) and decay (Entropy). Each partial has tracked amplitude, velocity, persistence, and energy share. Stability resists sudden amplitude changes weighted by persistence. Entropy causes unreinforced harmonics to fade. Energy budget normalization prevents total energy from exceeding input global amplitude.

**When to use:**
- Adding physics-based timbral shaping to the resynthesis output
- Warmth for natural compression of harmonic spectra (taming dominant partials)
- Coupling for spectral smoothing and energy redistribution between neighbors
- Stability for inertia effects where partials resist sudden changes
- Entropy for decay effects where unstable partials fade away

**Key constraints:** All methods `noexcept`, no heap allocations. Fixed-size `std::array<float, kMaxPartials>` for agent state (SoA layout). Each sub-processor has an early-out bypass when its parameter is 0.0 (bit-exact). Frame-rate processing (~94 Hz at 48kHz/512 hop). Combined CPU overhead < 0.5% of a single core with 48 partials.

**Dependencies:** `HarmonicFrame` and `Partial` from `dsp/include/krate/dsp/processors/harmonic_types.h`, `kMaxPartials` constant. No KrateDSP library dependencies beyond types.

### State Persistence (Version 7)

State version bumped from 6 (M6) to 7. Four new float values appended after M6 data: warmth, coupling, stability, entropy. Loading a v6 state initializes all 4 physics parameters to 0.0 (bit-exact bypass). Backward compatible.

---

## Spec B Analysis Feedback Loop (Self-Evolving Timbral System)
**Since:** Spec B (123-analysis-feedback-loop)

Feeds the synth's previous block output back into its own analysis pipeline input, creating self-reinforcing timbral resonances. At low feedback: subtle harmonic self-reinforcement. At high feedback: harmonics crystallize into attractor states with emergent tonal behavior. Two new parameters (FeedbackAmount, FeedbackDecay) and a pre-allocated feedback buffer. The feedback mixing occurs between the sidechain stereo-to-mono downmix and the `pushSamples()` call.

### Signal Flow (Sidechain Mode with Feedback)

```
Sidechain In (stereo)
    |
    v
Stereo-to-Mono Downmix -> sidechainBuffer_
    |
    v
[Feedback Mixing] (Spec B, FR-001/FR-003)
|  Per-sample soft-limited mixing:
|    fbSample = tanh(feedbackBuffer_[s] * fbAmount * 2.0) * 0.5
|    mixedInput[s] = sidechain[s] * (1 - fbAmount) + fbSample
|  Bypassed when: fbAmount == 0, freeze active, or sample mode
    |
    v
pushSamples(mixedInput, numSamples)
    |
    v
LiveAnalysisPipeline (STFT, f0 detection, etc.)
    |
    v
[Morph -> Filter -> Modulators -> Physics -> Oscillator Bank]
    |
    v
Stereo Output
    |
    v
[Feedback Capture] (FR-002/FR-006)
|  feedbackBuffer_[s] = (outL[s] + outR[s]) * 0.5
    |
    v
[Feedback Decay] (FR-013)
|  decayCoeff = exp(-decayAmount * blockSize / sampleRate)
|  feedbackBuffer_[s] *= decayCoeff
    |
    v
feedbackBuffer_ ready for next block
```

### Parameters (IDs 710-711)

| Parameter | ID | Type | Range | Default |
|-----------|-----|------|-------|---------|
| Feedback Amount | `kAnalysisFeedbackId` (710) | RangeParameter | 0.0 - 1.0 | 0.0 |
| Feedback Decay | `kAnalysisFeedbackDecayId` (711) | RangeParameter | 0.0 - 1.0 | 0.2 |

### Feedback Buffer Pattern

The feedback buffer follows the same pre-allocation pattern as `sidechainBuffer_`:

```cpp
// Pre-allocated in processor.h (same size as sidechainBuffer_)
std::array<float, 8192> feedbackBuffer_{};

// Cleared in setActive() -- no allocation on audio thread (FR-005, FR-018)
feedbackBuffer_.fill(0.0f);
```

The buffer stores a mono representation of the synth output (left+right averaged if stereo). It introduces exactly one block of latency: output from block N feeds into analysis input of block N+1. The buffer is transient runtime state -- never persisted in plugin state.

### Soft Limiter Formula (FR-009)

Per-sample tanh-based soft limiting bounds the feedback signal before mixing:

```cpp
fbSample = std::tanh(feedbackBuffer_[s] * fbAmount * 2.0f) * 0.5f;
```

The `* 2.0` amplifies the signal into tanh's nonlinear region; the `* 0.5` scales the output to [-0.5, +0.5]. This ensures the feedback contribution is always bounded regardless of the synth output level.

### 5-Layer Safety Stack

Five independent safety mechanisms prevent feedback divergence:

| Layer | Mechanism | Location | Bounds |
|-------|-----------|----------|--------|
| 1 | **Soft limiter** (NEW) | Feedback mixing loop in `process()` | `tanh(x*2)*0.5` bounds feedback to [-0.5, +0.5] |
| 2 | **Energy budget normalization** (existing) | `HarmonicPhysics::applyDynamics` | Total harmonic energy cannot exceed frame's global amplitude |
| 3 | **Hard output clamp** (existing) | `HarmonicOscillatorBank::kOutputClamp = 2.0f` | Final output clamped to [-2.0, +2.0] |
| 4 | **Confidence gate** (existing) | Auto-freeze in `processor.cpp` | Garbage analysis triggers auto-freeze, preventing garbage harmonics |
| 5 | **Feedback decay** (NEW) | Per-block exponential decay in `process()` | `exp(-decay * blockSize / sampleRate)` leaks energy each block |

The layers are independent and composable. Simultaneous engagement produces more aggressively bounded output. With FeedbackAmount=0.0, the feedback path is completely bypassed (early-out), producing bit-identical output to the pre-feedback implementation.

### Freeze Interaction Contract (FR-015, FR-016)

- **Freeze engaged**: Feedback mixing is automatically bypassed. The frozen harmonic frame remains unmodified. The `manualFrozen` flag (from `freeze_` atomic) gates the feedback mixing block.
- **Freeze disengaged**: The feedback buffer is cleared to all zeros (`feedbackBuffer_.fill(0.0f)`) at the existing freeze-disengage transition (`!currentFreezeState && previousFreezeState_`). This prevents stale audio from contaminating the re-engaged analysis pipeline.
- **FeedbackAmount changes during freeze**: Have no effect on the frozen frame. Changes take effect only after freeze is disengaged.

Freeze state tracking for the feedback buffer uses `previousFreezeForFeedback_` (separate from the existing `previousFreezeState_` used for crossfade logic).

### Mode Restriction (FR-014)

Feedback is only active in sidechain mode (`InputSource::Sidechain`). Both the feedback mixing block and feedback capture block check `inputSource_ > 0.5f` (sidechain mode) before executing. In sample mode, the feedback path is completely bypassed regardless of FeedbackAmount.

### State Persistence (Version 8)

State version bumped from 7 (Spec A) to 8. Two new float values appended after Spec A data: FeedbackAmount, FeedbackDecay. Loading a v7 state initializes FeedbackAmount=0.0 and FeedbackDecay=0.2 (matching parameter defaults, preserving identical behavior for presets saved before this spec). Both values clamped to [0.0, 1.0] on read.

### Key Design Patterns

- **Pre-allocated storage**: Feedback buffer is a `std::array<float, 8192>` member, same pattern as `sidechainBuffer_`. Zero heap allocations on the audio thread.
- **Atomic parameter exchange**: Both parameters use `std::atomic<float>` for thread-safe communication from `processParameterChanges()` to `process()`.
- **Early-out bypass**: When `feedbackAmount == 0.0f`, the entire mixing loop is skipped. No overhead when feedback is disabled.
- **Block-rate parameter reading**: Both parameters are read once per `process()` call. No per-sample smoothing needed (no zipper artifacts at block-rate transitions).
- **In-place mixing**: Feedback is mixed directly into `sidechainBuffer_` before `pushSamples()`. If `sidechainMono` points to raw bus data (mono input case), it is first copied to `sidechainBuffer_` to avoid modifying the host's buffer.

---

## Spec 124 ADSR Envelope Detection (Amplitude Envelope Shaping)
**Since:** Spec 124 (124-adsr-envelope-detection)

Adds automatic ADSR envelope detection from analyzed sample amplitude contours, 9 user-editable envelope parameters, per-sample amplitude shaping in the audio output path, per-slot ADSR storage with geometric mean interpolation for morph/evolution, and an ADSRDisplay visualization. When Envelope Amount is 0.0, the ADSR path is completely bypassed (bit-exact).

### Signal Chain Position

```
Oscillator Bank + Residual Synthesizer (existing output)
    |
    v
[ADSR Envelope Shaping] (Spec 124)
|  gain = lerp(1.0, adsr_.process(), smoothedAmount)
|  output[s] *= gain
|  Bypassed when: Amount == 0.0 (skip ALL ADSR processing including envelope tick)
    |
    v
Final Stereo Output
```

The ADSR envelope is applied per-sample after the oscillator bank output. The `adsr_` instance is a monophonic `Krate::DSP::ADSREnvelope` member of the Processor. Gate is controlled by MIDI note-on (gate true) and note-off (gate false) with hard retrigger mode (new note-on during held note resets envelope to attack stage immediately).

### Parameters (IDs 720-728)

| Parameter | ID | Type | Range | Default |
|-----------|-----|------|-------|---------|
| Attack | `kAdsrAttackId` (720) | RangeParameter (log) | 1 - 5000 ms | 10 ms |
| Decay | `kAdsrDecayId` (721) | RangeParameter (log) | 1 - 5000 ms | 100 ms |
| Sustain | `kAdsrSustainId` (722) | RangeParameter | 0.0 - 1.0 | 1.0 |
| Release | `kAdsrReleaseId` (723) | RangeParameter (log) | 1 - 5000 ms | 100 ms |
| Amount | `kAdsrAmountId` (724) | RangeParameter | 0.0 - 1.0 | 0.0 |
| Time Scale | `kAdsrTimeScaleId` (725) | RangeParameter | 0.25 - 4.0 | 1.0 |
| Attack Curve | `kAdsrAttackCurveId` (726) | RangeParameter | -1.0 - +1.0 | 0.0 |
| Decay Curve | `kAdsrDecayCurveId` (727) | RangeParameter | -1.0 - +1.0 | 0.0 |
| Release Curve | `kAdsrReleaseCurveId` (728) | RangeParameter | -1.0 - +1.0 | 0.0 |

**Note:** `kReleaseTimeId` (200) is the existing oscillator release fade parameter. The ADSR release is `kAdsrReleaseId` (723) -- a separate parameter.

### EnvelopeDetector
**Path:** [envelope_detector.h](../../plugins/innexus/src/dsp/envelope_detector.h) | **Since:** Spec 124

Plugin-local DSP component that analyzes the amplitude contour of harmonic analysis frames and fits ADSR parameters using O(1) rolling least-squares steady-state detection. Called from `SampleAnalyzer::analyzeOnThread()` after frame analysis completes. Not called for sidechain input (sidechain mode bypasses detection).

```cpp
namespace Innexus {

struct DetectedADSR {
    float attackMs = 10.0f;      // Detected attack time (ms), clamped [1, 5000]
    float decayMs = 100.0f;      // Detected decay time (ms), clamped [1, 5000]
    float sustainLevel = 1.0f;   // Detected sustain level [0, 1] relative to peak
    float releaseMs = 100.0f;    // Detected release time (ms), clamped [1, 5000]
};

class EnvelopeDetector {
public:
    // Static analysis function -- not real-time, runs on analysis thread
    [[nodiscard]] static DetectedADSR detect(
        const std::vector<Krate::DSP::HarmonicFrame>& frames,
        float hopTimeSec) noexcept;

private:
    static constexpr int kWindowSize = 12;           // Rolling window size [8-20 valid range]
    static constexpr float kSlopeThreshold = 0.0005f; // |slope| < threshold for steady state
    static constexpr float kVarianceThreshold = 0.002f; // variance < threshold for steady state
    static constexpr int kMinFrames = 4;              // Minimum frames for valid detection
};

} // namespace Innexus
```

**Detection algorithm:**
1. Extract `globalAmplitude` per frame into amplitude contour vector
2. Find peak index (attack endpoint)
3. Compute Attack = peakIndex * hopTimeSec * 1000 ms
4. Scan post-peak frames with O(1) rolling least-squares (fixed window of `kWindowSize=12` frames):
   - Maintain running sums: n, sum_x, sum_y, sum_xy, sum_x2
   - Welford online variance: mean, M2
   - Steady-state condition: |slope| < 0.0005/frame AND variance < 0.002
5. Compute Decay = (steadyStateStart - peakIndex) * hopTimeSec * 1000 ms
6. Compute Sustain = mean amplitude in steady-state region / peak amplitude
7. Compute Release = (totalFrames - 1 - steadyStateEnd) * hopTimeSec * 1000 ms
8. Clamp all outputs: times to [1, 5000] ms, sustain to [0, 1]

**When to use:**
- Automatic ADSR parameter fitting from loaded audio sample analysis
- Initial envelope detection that populates user-editable ADSR knobs
- NOT for sidechain/live analysis (detection is suppressed in sidechain mode)

**Key constraints:** Static method, not real-time. Allocates `std::vector` internally (runs on analysis thread, not audio thread). Returns sensible defaults for edge cases (empty frames, constant amplitude, very short contours).

### ADSR Amplitude Shaping in Processor

The processor applies per-sample ADSR envelope shaping after the oscillator bank output:

```cpp
// Per-sample gain computation
float envGain = adsr_.process();
float smoothedAmount = adsrAmountSmoother_.process(adsrAmount);
float gain = 1.0f * (1.0f - smoothedAmount) + envGain * smoothedAmount;
output[s] *= gain;
```

- **Amount=0.0**: `gain = 1.0` (bit-exact bypass, no envelope tick)
- **Amount=1.0**: `gain = envGain` (full ADSR shaping)
- **Intermediate Amount**: Linear blend, output is never fully silent (intentional)

Effective envelope times are scaled: `effectiveTime = paramTime * timeScale`, clamped to [1, 5000] ms per segment independently.

### Per-Slot ADSR Storage and Interpolation

Each of the 8 `MemorySlot` instances stores 9 ADSR fields alongside the `HarmonicSnapshot` (see Layer 2 documentation for field details). Capture writes current processor ADSR atomics into the slot. Recall restores all 9 values to processor atomics and sends `IMessage` to update controller knobs.

**Geometric mean interpolation pattern for morph/evolution:**

Time parameters (Attack, Decay, Release) use geometric mean interpolation to preserve perceptual proportionality across logarithmic ranges:

```cpp
// Geometric mean for time parameters (morph factor t in [0, 1])
float result = std::exp((1.0f - t) * std::log(a) + t * std::log(b));

// Example: morph t=0.5 between Attack=10ms and Attack=500ms
// result = exp(0.5 * log(10) + 0.5 * log(500)) = sqrt(10 * 500) = ~70.7ms
```

Linear parameters (Sustain, Amount, TimeScale, AttackCurve, DecayCurve, ReleaseCurve) use standard linear interpolation: `a * (1-t) + b * t`.

This pattern is shared by both `HarmonicBlender::getBlendedAdsr()` (host-driven multi-source morph) and `EvolutionEngine::interpolateAdsr()` (autonomous evolution drift). Both use the same formula to ensure consistent interpolation behavior.

### ADSRDisplay Visualization
**Path:** [adsr_display.h](../../plugins/shared/src/ui/adsr_display.h) | **Since:** Spec 124

Shared UI component (`Krate::Plugins::ADSRDisplay`, extends `CView`) that renders the ADSR envelope curve with draggable control points and an animated playback dot. Wired in `Controller::createCustomView()` with `custom-view-name="ADSRDisplay"`.

- Base ADSR param IDs: 720 (A=720, D=721, S=722, R=723, consecutive)
- Base curve param IDs: 726 (AC=726, DC=727, RC=728, consecutive)
- Playback dot driven by processor atomics: `adsrEnvelopeOutput_` (float), `adsrStage_` (int), `adsrActive_` (bool)

### State Persistence (Version 9)

State version bumped from 8 (Spec B) to 9. Nine global ADSR float values appended after Spec B data (in parameter ID order: 720-728), followed by per-slot ADSR data for all 8 slots (9 floats per slot = 72 floats total). Loading a v1-v8 state initializes Envelope Amount=0.0 (bypass), all curve amounts=0.0, Attack=10ms, Decay=100ms, Sustain=1.0, Release=100ms, TimeScale=1.0. Backward compatible.

### Key Design Patterns

- **Bit-exact bypass**: When Amount=0.0, ALL ADSR processing is skipped -- no envelope tick, no multiply. Output is identical to pre-feature behavior.
- **Monophonic envelope**: Single `ADSREnvelope` instance in Processor, not per-voice. Hard retrigger mode resets to attack on new note-on.
- **Pre-allocated storage**: `ADSREnvelope` and `OnePoleSmoother` are member variables. Zero heap allocations on the audio thread.
- **Atomic parameter exchange**: All 9 parameters use `std::atomic<float>` for thread-safe communication.
- **Amount smoothing**: `OnePoleSmoother` (~5-10ms) on the Amount parameter prevents clicks during automation transitions.
- **Geometric mean interpolation**: Time parameters use log-space weighted interpolation for perceptually correct morphing between slots. Shared formula between morph engine and evolution engine.
