# Ultimate Delay Plugin: Layered Feature Roadmap

## Executive Summary

This document defines a **layered, component-based architecture** for building the most comprehensive delay plugin ever constructed. Rather than implementing features as isolated chunks of work, we build **foundational DSP primitives** that compose into **system components**, which then combine to create **user-facing features**.

This approach maximizes code reuse, ensures consistent behavior across features, and creates a maintainable codebase where improvements to foundational components automatically benefit all features that depend on them.

---

## Architecture Philosophy

### Core Principles

1. **Composition over Implementation**: Every feature is built from smaller, reusable components
2. **Single Responsibility**: Each primitive does one thing extremely well
3. **Real-time Safety First**: No memory allocation, no locks, no blocking in the audio thread
4. **Type Safety**: Leverage TypeScript's type system to prevent bugs at compile time
5. **Testability**: Each layer can be unit tested in isolation
6. **Sample-Accurate**: All timing-critical operations work at sample granularity

### Layer Hierarchy

```
┌─────────────────────────────────────────────────────────────┐
│                    LAYER 4: USER FEATURES                    │
│  (Tape Mode, BBD Mode, Multi-Tap, Ping-Pong, Shimmer, etc.) │
├─────────────────────────────────────────────────────────────┤
│                  LAYER 3: SYSTEM COMPONENTS                  │
│    (Delay Engine, Modulation Matrix, Feedback Network,      │
│     Character Processor, Stereo Field, Dynamics Section)    │
├─────────────────────────────────────────────────────────────┤
│                   LAYER 2: DSP PROCESSORS                    │
│  (Filters, Saturation, Pitch Shifter, Diffuser, Envelope,   │
│   Noise Generator, Compressor, Ducking, Mid/Side)           │
├─────────────────────────────────────────────────────────────┤
│                  LAYER 1: DSP PRIMITIVES                     │
│    (Delay Line, Interpolators, LFO, Smoothers, Ring Buffer, │
│     Biquad, Parameter, Oversampling, FFT)                   │
├─────────────────────────────────────────────────────────────┤
│                    LAYER 0: CORE UTILITIES                   │
│      (Memory Pool, Lock-free Queue, Math Utils, SIMD,       │
│       Sample Rate Handling, Block Processing)                │
└─────────────────────────────────────────────────────────────┘
```

---

## LAYER 0: Core Utilities

**Purpose**: Foundation for real-time safe operations. Every component above depends on these.

### 0.1 Memory Management

| Component | Description | Key Considerations |
|-----------|-------------|-------------------|
| `MemoryPool` | Pre-allocated memory pool for real-time safe allocation | Fixed-size blocks, no fragmentation, thread-safe claiming |
| `StaticBuffer<T, N>` | Compile-time sized buffer | Zero runtime allocation, cache-friendly alignment |
| `ArenaAllocator` | Fast bump allocator for per-block temporary allocations | Reset at block boundary, no individual frees |

**Anti-patterns to avoid**:
- Never use `new`/`delete` in audio thread
- Never use STL containers that can reallocate (vector, string)
- Never use smart pointers that might deallocate

### 0.2 Thread Communication

| Component | Description | Key Considerations |
|-----------|-------------|-------------------|
| `LockFreeQueue<T>` | SPSC queue for parameter/event passing | Atomic operations only, fixed capacity |
| `AtomicParameter<T>` | Atomic wrapper for primitive parameter values | Naturally aligned, no tearing on 32/64-bit values |
| `DoubleBuffer<T>` | Swap-buffer pattern for complex state updates | Message thread writes, audio thread reads |

### 0.3 Math Utilities

| Component | Description | Key Considerations |
|-----------|-------------|-------------------|
| `FastMath` | Optimized transcendental approximations | `fastSin`, `fastTanh`, `fastExp`, `fastLog` with configurable precision |
| `Interpolation` | Static interpolation functions | Linear, cubic Hermite, Catmull-Rom, Lagrange |
| `SIMDOps` | SIMD-accelerated vector operations | SSE/AVX/NEON abstractions for cross-platform |
| `dBConversion` | Decibel/linear conversion | `dBToGain`, `gainToDb` with proper handling of silence |

### 0.4 Sample Rate & Block Processing

| Component | Description | Key Considerations |
|-----------|-------------|-------------------|
| `SampleRateConverter` | Handle sample rate changes | Coefficient recalculation triggers, nyquist tracking |
| `BlockContext` | Per-block processing context | Sample rate, block size, tempo, transport state |
| `ProcessorBase` | Abstract base for all processors | `prepare()`, `process()`, `reset()` interface |

**Spec Dependencies**: None (foundational layer)

---

## LAYER 1: DSP Primitives

**Purpose**: Fundamental DSP building blocks that all higher-level processors use.

### 1.1 Delay Line (`DelayLine`)

The most critical primitive. All delay-based effects depend on this.

| Sub-component | Description | Research-backed approach |
|---------------|-------------|-------------------------|
| `CircularBuffer` | Core ring buffer storage | Power-of-2 size for fast masking, or variable size with conditional wrap |
| `WriteHead` | Sample input management | Simple increment with wrap |
| `ReadHead` | Fractional delay reading | Supports multiple read positions for multi-tap |
| `InterpolationStrategy` | Pluggable interpolation | Linear (cheap), Cubic (better), Allpass (feedback loops), Sinc (highest quality) |

**Critical implementation details**:
- Use `readPos = (writePos - delaySamples) & mask` for power-of-2 buffers
- For non-power-of-2: conditional branch is faster than modulo for cache coherence
- Maximum delay time determines buffer size (e.g., 10 seconds at 192kHz = 1,920,000 samples)
- Allpass interpolation is NOT suitable for modulated delays (use for fixed delays in feedback loops)
- Linear interpolation causes high-frequency rolloff proportional to fractional position

**Spec requirements**:
- Maximum delay time: configurable, default 10 seconds
- Sample rates: 44.1kHz to 192kHz
- Interpolation modes: Linear, Cubic Hermite, Lagrange (3rd, 5th order), Thiran allpass
- Multi-tap support: up to 16 independent read heads

### 1.2 Interpolators

| Type | Use Case | CPU Cost | Quality |
|------|----------|----------|---------|
| `LinearInterpolator` | LFO modulated delays, chorusing | Very Low | Good for oversampled signals |
| `CubicHermiteInterpolator` | General purpose, smooth modulation | Low | Good balance |
| `CatmullRomInterpolator` | Upsampling without pre-filtering | Low | Better than Hermite |
| `LagrangeInterpolator` | High-quality pitch shifting | Medium | Excellent |
| `SincInterpolator` | Highest quality resampling | High | Reference quality |
| `AllpassInterpolator` | Fixed fractional delays in feedback | Low | No amplitude distortion |

### 1.3 LFO (Low Frequency Oscillator)

| Feature | Description |
|---------|-------------|
| Waveforms | Sine, Triangle, Saw, Square, Sample & Hold, Random (smoothed) |
| Sync | Free-running Hz or tempo-synced (note values with dotted/triplet) |
| Phase | Adjustable phase offset (0-360°) |
| Retrigger | On/off for each note or free-running |
| Wavetable | Custom drawable waveform support |
| Frequency | 0.01 Hz to 20 Hz (with audio-rate option for FM effects) |

**Implementation approach**:
- Wavetable-based for efficiency (2048 samples is sufficient for LFO)
- Phase accumulator with increment = frequency / sampleRate
- Tempo sync: increment = (bpm / 60) * noteMultiplier / sampleRate

### 1.4 Parameter Smoother

Prevents zipper noise from parameter changes.

| Type | Behavior | Use Case |
|------|----------|----------|
| `OnePoleSmoother` | Exponential approach | Most parameters, simple and effective |
| `LinearRamp` | Constant rate change | Delay time (tape-like pitch effect) |
| `LogarithmicSmoother` | Perceptually linear for dB values | Volume, gain parameters |
| `SlewLimiter` | Maximum rate limit | Prevent sudden jumps |

**Critical formula** (one-pole):
```
smoothingCoeff = exp(-2.0 * PI * smoothingFreqHz / sampleRate)
smoothedValue = targetValue + smoothingCoeff * (smoothedValue - targetValue)
```

### 1.5 Biquad Filter

Foundation for all filtering operations.

| Implementation | Pros | Cons | Use Case |
|----------------|------|------|----------|
| Direct Form I | Stable with fixed-point, good for high Q | More state variables | General use |
| Direct Form II | Fewer state variables | Numerical issues at low freq | Simple filters |
| Transposed Direct Form II | Best for floating-point | - | **Recommended default** |
| State Variable Filter (SVF) | Excellent for modulation, multiple outputs | Needs oversampling above fs/6 | Synth-style filters |

**Filter types needed**:
- Lowpass (6, 12, 18, 24 dB/oct)
- Highpass (6, 12, 18, 24 dB/oct)
- Bandpass
- Notch
- Allpass
- Low/High Shelf
- Peak/Parametric EQ

### 1.6 Oversampler

Required for nonlinear processing (saturation, distortion).

| Factor | Use Case | Filter Order |
|--------|----------|--------------|
| 2x | Light saturation, sufficient for most cases | 12-16 taps |
| 4x | Heavy distortion | 24-32 taps |
| 8x | Extreme nonlinearities | 48-64 taps |

**Process**:
1. Upsample: Zero-stuff then lowpass at original Nyquist
2. Process: Apply nonlinear function at higher rate
3. Downsample: Lowpass at original Nyquist then decimate

**Research finding**: 2x oversampling is the practical transparency limit for real-time. Higher factors add phase smear and transient loss that can be more audible than the aliasing they prevent.

### 1.7 FFT Processor

For spectral processing features.

| Component | Description |
|-----------|-------------|
| `FFT` | Forward/Inverse FFT (power-of-2 sizes) |
| `STFT` | Short-time Fourier transform with windowing |
| `OverlapAdd` | OLA reconstruction with configurable hop size |
| `SpectralBuffer` | Complex spectrum storage and manipulation |

**Key parameters**:
- FFT sizes: 256, 512, 1024, 2048, 4096, 8192
- Window types: Hann, Hamming, Blackman, Kaiser
- Overlap: 50% (2x), 75% (4x) typical for COLA

---

## LAYER 2: DSP Processors

**Purpose**: Higher-level processors composed from Layer 1 primitives.

### 2.1 Multi-Mode Filter (`FilterProcessor`)

**Composition**: Biquad + Parameter Smoother + SVF (optional)

| Feature | Implementation |
|---------|----------------|
| Filter Types | LP/HP/BP/Notch/Allpass/Shelf/Peak |
| Slopes | 6/12/18/24 dB per octave (cascaded biquads) |
| Resonance | Q control with self-oscillation capability |
| Drive | Pre-filter saturation for analog character |
| Modulation Inputs | Cutoff, Resonance, Drive |

### 2.2 Saturation Processor (`SaturatorProcessor`)

**Composition**: Oversampler + Waveshaper + DC Blocker

| Saturation Type | Algorithm | Character |
|-----------------|-----------|-----------|
| Tape | `tanh(x)` with asymmetry, frequency-dependent | Warm, compressive |
| Tube | Polynomial + asymmetric clipping | Even harmonics, rich |
| Transistor | Hard knee soft clip | Aggressive, bright |
| Digital Clip | Hard clip | Harsh, intentional |
| Diode | `x / (1 + |x|)` variants | Subtle warmth |

**Implementation notes**:
- Always oversample before nonlinear processing
- Consider ADAA (Antiderivative Anti-Aliasing) for CPU savings
- DC blocker essential after asymmetric saturation

### 2.3 Pitch Shifter (`PitchShiftProcessor`)

**Composition**: Delay Line + STFT (for quality modes) + Interpolator

| Mode | Latency | Quality | Use Case |
|------|---------|---------|----------|
| Simple (delay modulation) | Zero | Audible artifacts | Quick shifts |
| Granular | Low | Good | General use |
| Phase Vocoder | High | Excellent | Quality-critical |

### 2.4 Diffusion Network (`DiffuserProcessor`)

**Composition**: Multiple Allpass Delays + Mixing Matrix

| Parameter | Range | Effect |
|-----------|-------|--------|
| Size | 1-100% | Allpass delay times |
| Density | 1-100% | Number of active stages |
| Modulation | 0-100% | LFO on delay times |

**Implementation**: Cascade of 4-8 allpass filters with prime-related delay times.

### 2.5 Envelope Follower (`EnvelopeFollower`)

**Composition**: Rectifier + One-pole Filters (attack/release)

| Output | Use Case |
|--------|----------|
| Amplitude envelope | Ducking, dynamics control |
| RMS level | Smoother response |
| Peak level | Fast transient detection |

### 2.6 Noise Generator (`NoiseGenerator`)

| Type | Character | Use Case |
|------|-----------|----------|
| White | Flat spectrum | Hiss, general noise |
| Pink | -3dB/octave | More natural hiss |
| Tape Hiss | Filtered, dynamic | Tape emulation |
| Vinyl Crackle | Impulsive | Lo-fi character |
| Asperity | Tape head noise, varies with level | Authentic tape |

### 2.7 Compressor/Limiter (`DynamicsProcessor`)

**Composition**: Envelope Follower + Gain Computer + Parameter Smoother

| Feature | Purpose |
|---------|---------|
| Threshold | Level where processing begins |
| Ratio | Compression amount |
| Attack/Release | Envelope timing |
| Knee | Hard/soft transition |
| Makeup Gain | Output level compensation |
| Sidechain Input | External trigger source |

### 2.8 Ducking Processor (`DuckingProcessor`)

**Composition**: Envelope Follower + Gain Reducer

| Mode | Behavior |
|------|----------|
| Output Duck | Reduce wet signal when dry is present |
| Feedback Duck | Reduce feedback when input is present |
| Combined | Both simultaneously |

### 2.9 Mid/Side Processor (`MidSideProcessor`)

**Composition**: Matrix encoding/decoding + independent processing

| Operation | Formula |
|-----------|---------|
| Encode | `mid = (L + R) / 2`, `side = (L - R) / 2` |
| Decode | `L = mid + side`, `R = mid - side` |

---

## LAYER 3: System Components

**Purpose**: Major functional blocks that combine Layer 2 processors.

### 3.1 Delay Engine (`DelayEngine`)

The core delay processing unit.

**Composition**:
- DelayLine (Layer 1)
- Interpolator (Layer 1, selectable)
- Parameter Smoothers (Layer 1)
- Tempo Sync logic

| Feature | Implementation |
|---------|----------------|
| Delay Time | 0ms to 10,000ms (or tempo-synced) |
| Time Modes | Free (ms), Synced (note values), Tap tempo |
| Crossfade | Smooth transitions when time changes dramatically |
| Kill Dry | Option for 100% wet operation |

### 3.2 Feedback Network (`FeedbackNetwork`)

**Composition**:
- Delay Engine
- Filter Processor (in feedback path)
- Saturator Processor (in feedback path)
- Pitch Shifter (optional, in feedback path)
- Compressor (feedback limiting)

| Topology | Description |
|----------|-------------|
| Simple | Direct output-to-input |
| Cross-feedback | L↔R interaction |
| Nested | Feedback within feedback |
| Matrix | Multi-tap cross-feeding |

| Parameter | Range | Behavior |
|-----------|-------|----------|
| Feedback Amount | 0-100% (or 120% for self-oscillation) |
| Feedback Filter | HP/LP/BP in feedback path |
| Feedback Saturation | Drive amount |
| Freeze | 100% feedback with input muted |

### 3.3 Modulation Matrix (`ModulationMatrix`)

**Composition**:
- Multiple LFOs (Layer 1)
- Envelope Followers (Layer 2)
- Envelope Generators (ADSR)
- Step Sequencer
- Random/Chaos generators

| Source Type | Examples |
|-------------|----------|
| LFO | LFO1, LFO2, LFO3 (with different shapes/rates) |
| Envelope | Input envelope, ADSR envelopes |
| Sequencer | 8-32 step sequencers |
| Random | S&H, smoothed random |
| MIDI | Velocity, aftertouch, mod wheel, expression |
| Macro | User-defined macro controls |

| Destination | Examples |
|-------------|----------|
| Delay Time | Primary delay, tap times |
| Feedback | Amount, filter cutoff |
| Mix | Dry/wet balance |
| Filter | Cutoff, resonance |
| Pan | Stereo position |
| Pitch | Shift amount |
| Any Parameter | Full matrix routing |

### 3.4 Character Processor (`CharacterProcessor`)

Applies analog character/coloration.

**Composition**:
- Saturation Processor
- Noise Generator
- Filter Processor (coloration EQ)
- Wow/Flutter LFO + Delay modulation

| Character Mode | Components Active |
|----------------|-------------------|
| Tape | Saturation (tape), Wow/Flutter, Hiss, EQ rolloff |
| BBD | Saturation (soft), Clock noise, Bandwidth limit |
| Digital Vintage | Bit reduction, Sample rate reduction |
| Clean | Bypass (or minimal coloration) |

### 3.5 Stereo Field (`StereoField`)

**Composition**:
- Mid/Side Processor
- Pan controls
- Width processing
- Haas delay

| Mode | Behavior |
|------|----------|
| Mono | Summed output |
| Stereo | Independent L/R processing |
| Ping-Pong | Alternating L/R delays |
| Dual Mono | Same delay, panned |
| Mid/Side | Independent M/S delays |

| Parameter | Effect |
|-----------|--------|
| Width | Stereo spread 0-200% |
| Pan | Output position |
| L/R Offset | Time difference between channels |
| Ratio | L/R delay time ratio for polyrhythm |

### 3.6 Tap Manager (`TapManager`)

**Composition**:
- Multiple Delay Engines
- Per-tap controls
- Mixing matrix

| Per-Tap Parameter | Range |
|-------------------|-------|
| Time | Independent timing |
| Level | -inf to +6dB |
| Pan | L/R position |
| Filter | Per-tap filtering |
| Pitch | Per-tap pitch shift |
| Feedback | Per-tap to master feedback |

### 3.7 Flexible Feedback Network (`FlexibleFeedbackNetwork`)

**Purpose**: Extensible feedback network that allows injection of arbitrary processors into the feedback path. Enables shimmer, freeze, and experimental effects that need pitch shifting, granular processing, or other transformations in the feedback loop.

**Composition**:
- Delay Line (Layer 1) × 2 for stereo
- Parameter Smoothers (Layer 1)
- Injectable processor slot (IFeedbackProcessor interface)
- Filter Processor (Layer 2, in feedback path)
- Dynamics Processor (Layer 2, feedback limiting)

**Interface**:
```cpp
/// Interface for processors that can be injected into feedback path
class IFeedbackProcessor {
public:
    virtual ~IFeedbackProcessor() = default;
    virtual void prepare(double sampleRate, size_t maxBlockSize) noexcept = 0;
    virtual void process(float* left, float* right, size_t numSamples) noexcept = 0;
    virtual void reset() noexcept = 0;
    [[nodiscard]] virtual size_t getLatencySamples() const noexcept = 0;
};
```

**Signal Flow**:
```
Input ──┬──────────────────────────────────────────┬──► Mix ──► Output
        │                                          │
        ▼                                          │
   ┌─────────┐                                     │
   │  Delay  │◄────────────────────────────────────┤
   │  Line   │                                     │
   └────┬────┘                                     │
        │ (feedback path)                          │
        ▼                                          │
   ┌─────────────────┐                             │
   │ Injectable Proc │ ◄── PitchShifter, Granular, │
   │ (IFeedbackProc) │     Bitcrusher, etc.        │
   └────────┬────────┘                             │
            ▼                                      │
   ┌─────────────────┐                             │
   │     Filter      │                             │
   └────────┬────────┘                             │
            ▼                                      │
   ┌─────────────────┐                             │
   │    Limiter      │                             │
   └────────┬────────┘                             │
            │                                      │
            └──────────────────────────────────────┘
```

| Feature | Description |
|---------|-------------|
| Processor Injection | Set any IFeedbackProcessor implementation |
| Hot-Swap | Change processor without glitches (crossfade) |
| Bypass | Process with no injected processor (like FeedbackNetwork) |
| Freeze Mode | 100% feedback + input mute built-in |
| Latency Reporting | Aggregates latency from injected processor |

| Parameter | Range | Behavior |
|-----------|-------|----------|
| Feedback Amount | 0-120% | Self-oscillation at >100% |
| Delay Time | 0ms-10000ms | Smoothed transitions |
| Filter Enabled | on/off | LP/HP/BP in feedback |
| Filter Cutoff | 20Hz-20kHz | Smoothed |
| Freeze | on/off | Infinite sustain mode |
| Processor Mix | 0-100% | Blend processed/unprocessed in feedback |

**Consumers**:
- ShimmerDelay (Layer 4) - inject PitchShifter + DiffusionNetwork
- FreezeMode (Layer 4) - inject PitchShifter, enable freeze
- Future experimental modes - inject granular, spectral, bitcrusher, etc.

---

## LAYER 4: User Features

**Purpose**: Complete, user-facing delay modes composed from Layer 3 components.

### 4.1 Tape Delay Mode

**Components Used**:
- Delay Engine
- Feedback Network
- Character Processor (Tape mode)
- Modulation Matrix (wow/flutter)

| Control | Maps To |
|---------|---------|
| Motor Speed | Delay time |
| Wear | Wow/flutter depth, hiss level |
| Saturation | Tape drive |
| Age | EQ, noise, splice artifacts |
| Echo Heads | Tap pattern (3 playback heads like RE-201) |

**Unique behaviors**:
- Wow rate/depth scales with motor speed (delay time)
- Splice artifacts at tape loop point
- Motor inertia when changing speed
- Head gap simulation for frequency response

### 4.2 BBD (Bucket Brigade) Mode

**Components Used**:
- Delay Engine
- Feedback Network
- Character Processor (BBD mode)

| Control | Maps To |
|---------|---------|
| Time | Delay length (with bandwidth tracking) |
| Feedback | Loop gain |
| Modulation | Triangle LFO depth |
| Age | Clock noise, bandwidth reduction |
| Era | Different BBD chip models |

**Unique behaviors**:
- Bandwidth inversely proportional to delay time
- Compander artifacts (pumping)
- Clock noise proportional to delay time
- Limited frequency response (dark character)

### 4.3 Digital Delay Mode

**Components Used**:
- Delay Engine
- Feedback Network
- Character Processor (Clean or Vintage Digital)

| Control | Maps To |
|---------|---------|
| Time | Delay length |
| Feedback | Loop gain |
| Modulation | Optional LFO |
| Era | Clean, 80s digital, lo-fi |
| Limiter | Feedback limiter character |

**Unique behaviors**:
- Program-dependent limiter in feedback
- Optional bit reduction for vintage digital
- Pristine mode for transparent delays

### 4.4 Ping-Pong Mode

**Components Used**:
- Delay Engine (x2)
- Stereo Field (Ping-Pong mode)
- Feedback Network (cross-fed)

| Control | Maps To |
|---------|---------|
| Time | Both L/R delays (or ratio mode) |
| Feedback | Cross-feedback amount |
| Width | Stereo spread |
| Offset | L/R time offset |

### 4.5 Multi-Tap Mode

**Components Used**:
- Tap Manager
- Feedback Network
- Modulation Matrix

| Control | Maps To |
|---------|---------|
| Tap Count | 2-16 active taps |
| Pattern | Preset or user-defined tap timing |
| Feedback | Tap-to-feedback routing |
| Spread | Stereo distribution of taps |

### 4.6 Shimmer Mode

**Components Used**:
- Delay Engine
- Feedback Network
- Pitch Shifter (in feedback)
- Diffusion Network

| Control | Maps To |
|---------|---------|
| Shift | Pitch shift amount (+/- 24 semitones) |
| Mix | Pitched vs unpitched balance |
| Diffusion | Smear amount |
| Feedback | Shimmer intensity |

### 4.7 Reverse Delay Mode

**Components Used**:
- Delay Engine (with reverse buffer)
- Feedback Network

| Control | Maps To |
|---------|---------|
| Time | Buffer/grain length |
| Mode | Full reverse, alternating, random |
| Crossfade | Grain overlap |
| Feedback | Reverse iterations |

### 4.8 Granular Delay Mode

**Components Used**:
- FFT Processor (or time-domain granular)
- Delay Engine (as source)
- Modulation Matrix

| Control | Maps To |
|---------|---------|
| Grain Size | 10-500ms |
| Density | Grains per second |
| Pitch | Per-grain pitch randomization |
| Position | Randomization of playback position |
| Freeze | Buffer hold |

### 4.9 Spectral Delay Mode

**Components Used**:
- FFT Processor
- Multiple Delay Engines (per-band)

| Control | Maps To |
|---------|---------|
| Band Delays | Different delay per frequency band |
| Diffusion | Spectral smearing |
| Freeze | Spectral freeze |
| Mask | Frequency-dependent processing |

### 4.10 Ducking Delay Mode

**Components Used**:
- Any delay mode
- Ducking Processor

| Control | Maps To |
|---------|---------|
| Duck Amount | Reduction depth |
| Threshold | Input level trigger |
| Attack/Release | Ducking envelope |
| Target | Output, feedback, or both |

### 4.11 Freeze Mode

**Components Used**:
- Flexible Feedback Network (with pitch shifter injection)
- Diffusion Network (optional)
- Modulation Matrix (optional, for evolving textures)

| Control | Maps To |
|---------|---------|
| Freeze | Toggle infinite hold (100% feedback, input muted) |
| Pitch | Pitch shift in frozen feedback path (shimmer-style) |
| Shimmer Mix | Blend pitched/unpitched frozen content |
| Decay | Fade rate (0 = infinite sustain, 100 = fast decay) |
| Diffusion | Smear frozen content for pad-like texture |
| Filter | LP/HP in feedback for tonal shaping |

**Unique behaviors**:
- Instant capture of current delay buffer contents
- Smooth fade-in/out of freeze state (no clicks)
- Optional pitch shifting creates evolving frozen textures
- Decay control allows frozen content to naturally fade
- Can be combined with any delay mode as a modifier

**Signal Flow**:
```
Input ──┬──► [Muted when frozen] ──► Delay ──┬──► Output
        │                                    │
        │    ┌────────────────────────────────┘
        │    │
        │    ▼
        │  [Flexible Feedback Network]
        │    │
        │    ├──► Pitch Shifter (optional)
        │    ├──► Shimmer Mix
        │    ├──► Diffusion (optional)
        │    ├──► Filter
        │    └──► Limiter
        │         │
        └─────────┴──► Feedback (100% when frozen)
```

---

## Implementation Phases

### Phase 1: Foundation (Weeks 1-4)

**Goal**: Complete Layer 0 and Layer 1

| Week | Deliverables |
|------|--------------|
| 1 | Memory utilities, math functions, block processing base |
| 2 | Delay Line with all interpolation modes |
| 3 | LFO, Parameter Smoother, Biquad Filter |
| 4 | Oversampler, integration testing |

**Exit Criteria**:
- All primitives pass unit tests
- Delay line achieves < 1% CPU for 10-second stereo delay
- No memory allocation in process() path

### Phase 2: Processors (Weeks 5-8)

**Goal**: Complete Layer 2

| Week | Deliverables |
|------|--------------|
| 5 | Multi-mode filter, saturation processor |
| 6 | Pitch shifter (basic mode), diffuser |
| 7 | Envelope follower, dynamics, ducking |
| 8 | Noise generators, mid/side, integration |

**Exit Criteria**:
- All processors compose correctly
- Saturation produces < -90dB aliasing at 2x oversample
- Filter modulation is click-free

### Phase 3: Systems (Weeks 9-12)

**Goal**: Complete Layer 3

| Week | Deliverables |
|------|--------------|
| 9 | Delay Engine, Feedback Network |
| 10 | Modulation Matrix with full routing |
| 11 | Character Processor (all modes) |
| 12 | Stereo Field, Tap Manager |

**Exit Criteria**:
- Modulation matrix supports 8 sources → any destination
- Feedback can self-oscillate stably
- Character modes are perceptually distinct

### Phase 4: Features - Core Modes (Weeks 13-16)

**Goal**: Tape, BBD, Digital, Ping-Pong modes

| Week | Deliverables |
|------|--------------|
| 13 | Tape Delay mode complete |
| 14 | BBD mode complete |
| 15 | Digital mode complete |
| 16 | Ping-Pong mode, integration |

### Phase 5: Features - Advanced Modes (Weeks 17-20)

**Goal**: Multi-tap, Shimmer, Reverse, Granular

| Week | Deliverables |
|------|--------------|
| 17 | Multi-tap mode (up to 16 taps) |
| 18 | Shimmer mode |
| 19 | Reverse delay mode |
| 20 | Granular delay mode |

### Phase 6: Features - Specialty & Polish (Weeks 21-24)

**Goal**: Spectral, Ducking, Presets, UI polish

| Week | Deliverables |
|------|--------------|
| 21 | Spectral delay mode |
| 22 | Ducking integration, auto-clear |
| 23 | Preset system, factory presets |
| 24 | Final polish, documentation |

---

## Technical Specifications

### Audio Requirements

| Specification | Value |
|---------------|-------|
| Sample Rates | 44.1, 48, 88.2, 96, 176.4, 192 kHz |
| Bit Depth | 32-bit float internal, 64-bit optional |
| Block Sizes | 1 to 8192 samples |
| Latency | 0 samples (base), configurable for lookahead |
| CPU Target | < 5% single core for full feature set |

### Plugin Formats

| Format | Priority |
|--------|----------|
| VST3 | Primary |
| AU | Mac support |
| AAX | Pro Tools |
| CLAP | Future-proof |

### UI Requirements

| Feature | Description |
|---------|-------------|
| Scalable | 100% to 200% UI scaling |
| Visualizations | Delay timing grid, modulation display, level meters |
| A/B Compare | Quick preset switching |
| Undo/Redo | Full edit history |
| MIDI Learn | One-click parameter mapping |

---

## Speckit Integration Notes

This roadmap is structured to generate speckit specifications at each layer:

### Constitution Principles

```
- Real-time safety: No allocation, locks, or blocking in audio thread
- Composition: Features built from reusable components
- Type safety: TypeScript types prevent runtime errors
- Testability: Each layer independently testable
- Sample accuracy: All timing at sample granularity
```

### Specification Generation

For each component, generate a speckit specification with:

1. **Functional Requirements**: What it does
2. **Interface Definition**: TypeScript types for inputs/outputs
3. **Dependencies**: Which lower-layer components it uses
4. **Test Criteria**: How to verify correctness
5. **Performance Budget**: CPU/memory constraints

### Example Spec Structure

```
/specify Implement DelayLine primitive with:
- Circular buffer with configurable max delay (up to 10 seconds)
- Multiple read heads for multi-tap support
- Pluggable interpolation (Linear, Cubic, Lagrange, Allpass)
- Zero-copy design with no audio-thread allocation
- Sample-accurate delay time with smooth transitions
```

---

## References & Research Sources

### Core DSP Theory
- Julius O. Smith III - Physical Audio Signal Processing (Stanford CCRMA)
- Udo Zölzer - DAFX: Digital Audio Effects
- Richard G. Lyons - Understanding Digital Signal Processing

### Implementation Best Practices
- Ross Bencina - Real-time Audio Programming 101
- Timur Doumler - Thread Synchronization in Real-time Audio
- Sean Costello (Valhalla DSP) - Delay line implementation discussions

### Specific Techniques
- Fractional Delay Filtering (CCRMA) - Interpolation methods
- Antiderivative Anti-Aliasing (ADAA) - Jatin Chowdhury
- State Variable Filter - Andrew Simper (Cytomic)

---

*Document Version: 1.0*
*Last Updated: December 2025*
*For use with speckit specification-driven development*
