# Disrumpo - DSP Implementation Details

**Related Documents:**
- [spec.md](spec.md) - Core requirements specification
- [plan.md](plan.md) - System architecture and development roadmap
- [tasks.md](tasks.md) - Task breakdown and milestones
- [ui-mockups.md](ui-mockups.md) - Detailed UI specifications
- [custom-controls.md](custom-controls.md) - Custom VSTGUI control specifications

---

## 1. Data Structures

### Parameter ID Encoding

Following the Iterum plugin pattern from `plugins/iterum/src/plugin_ids.h`:

```cpp
// Disrumpo/src/plugin_ids.h
#pragma once

#include "pluginterfaces/vst/vsttypes.h"

namespace Disrumpo {

// Parameter IDs follow kNameId convention per CLAUDE.md
// Encoding: band index (0-7) in bits 8-11, node index (0-3) in bits 12-15

enum ParameterID : Steinberg::Vst::ParamID {
    // Global Parameters
    kInputGainId        = 0x0F00,
    kOutputGainId       = 0x0F01,
    kGlobalMixId        = 0x0F02,
    kBandCountId        = 0x0F03,
    kOversampleMaxId    = 0x0F04,

    // Sweep Parameters (band = 0xE)
    kSweepEnableId      = 0x0E00,
    kSweepFrequencyId   = 0x0E01,
    kSweepWidthId       = 0x0E02,
    kSweepIntensityId   = 0x0E03,
    kSweepMorphLinkId   = 0x0E04,

    // Per-band parameters use helper: makeBandParamId(bandIndex, paramType)
    // Per-node parameters use helper: makeNodeParamId(bandIndex, nodeIndex, paramType)
};

// Band-level parameter types (node = 0xF)
enum BandParamType : uint8_t {
    kBandGain       = 0x00,
    kBandPan        = 0x01,
    kBandSolo       = 0x02,
    kBandBypass     = 0x03,
    kBandMute       = 0x04,
    kBandMorphX     = 0x08,
    kBandMorphY     = 0x09,
    kBandMorphMode  = 0x0A,
};

// Node-level parameter types
enum NodeParamType : uint8_t {
    kNodeType       = 0x00,
    kNodeDrive      = 0x01,
    kNodeMix        = 0x02,
    kNodeTone       = 0x03,
    kNodeBias       = 0x04,
    kNodeFolds      = 0x05,
    kNodeBitDepth   = 0x06,
};

constexpr Steinberg::Vst::ParamID makeBandParamId(uint8_t band, BandParamType param) {
    return static_cast<Steinberg::Vst::ParamID>((0xF << 12) | (band << 8) | param);
}

constexpr Steinberg::Vst::ParamID makeNodeParamId(uint8_t band, uint8_t node, NodeParamType param) {
    return static_cast<Steinberg::Vst::ParamID>((node << 12) | (band << 8) | param);
}

} // namespace Disrumpo
```

### Core Structures

Following KrateDSP conventions (`Krate::DSP` namespace, `kConstantName` constants):

```cpp
// dsp/include/krate/dsp/systems/Disrumpo/distortion_types.h
#pragma once

#include <cstdint>

namespace Krate::DSP {

/// @brief Distortion algorithm types for Disrumpo morph engine.
/// Maps to existing KrateDSP processors where available.
enum class DistortionType : uint8_t {
    // Saturation (use SaturationProcessor, TubeStage, TapeSaturator, FuzzProcessor)
    SoftClip = 0,       ///< Waveshaper::Tanh
    HardClip,           ///< Sigmoid::hardClip
    Tube,               ///< TubeStage processor
    Tape,               ///< TapeSaturator processor
    Fuzz,               ///< FuzzProcessor::Germanium
    AsymmetricFuzz,     ///< FuzzProcessor with bias

    // Wavefold (use WavefolderProcessor)
    SineFold,           ///< WavefolderProcessor::Serge
    TriangleFold,       ///< WavefolderProcessor::Simple
    SergeFold,          ///< WavefolderProcessor::Lockhart

    // Rectify (composable from core math)
    FullRectify,        ///< std::abs(x)
    HalfRectify,        ///< std::max(0.0f, x)

    // Digital (use BitcrusherProcessor, SampleRateReducer, AliasingEffect, BitwiseMangler)
    Bitcrush,           ///< BitcrusherProcessor
    SampleReduce,       ///< SampleRateReducer
    Quantize,           ///< BitCrusher quantization
    Aliasing,           ///< AliasingEffect (intentional aliasing without AA)
    BitwiseMangler,     ///< BitwiseMangler (XOR, bit rotation, overflow wrap)

    // Dynamic (envelope-reactive distortion)
    Temporal,           ///< TemporalDistortion (envelope-following drive)

    // Hybrid (complex routing)
    RingSaturation,     ///< RingSaturation (signal × saturate(signal))
    FeedbackDist,       ///< FeedbackDistortion (delay + saturation + feedback)

    // Experimental (sound design)
    Chaos,              ///< ChaosWaveshaper (Lorenz/Rossler/Chua attractors)
    Formant,            ///< FormantDistortion (vowel shaping + saturation)
    Granular,           ///< GranularDistortion (per-grain variable distortion)
    Spectral,           ///< SpectralDistortion (per-bin FFT domain saturation)
    Fractal,            ///< FractalDistortion (recursive multi-scale distortion)
    Stochastic,         ///< StochasticShaper (randomized transfer function)

    // Hybrid (complex routing) - additional
    AllpassResonant,    ///< AllpassSaturator (resonant distortion networks)
};

/// @brief Morph interpolation mode.
enum class MorphMode : uint8_t {
    Linear1D = 0,   ///< Single axis A↔B↔C↔D
    Planar2D,       ///< XY position in node space
    Radial2D,       ///< Angle + distance from center
};

/// @brief Sweep-to-morph linking mode.
enum class SweepMorphLink : uint8_t {
    None = 0,       ///< Independent control
    Linear,         ///< Low freq = morph 0, high freq = morph 1
    Inverse,        ///< Low freq = morph 1, high freq = morph 0
    Custom,         ///< User-defined curve
};

// Constants following kPascalCase convention
inline constexpr float kMinDrive = 0.0f;
inline constexpr float kMaxDrive = 10.0f;
inline constexpr float kDefaultDrive = 1.0f;

inline constexpr float kMinToneHz = 200.0f;
inline constexpr float kMaxToneHz = 8000.0f;
inline constexpr float kDefaultToneHz = 4000.0f;

inline constexpr float kMinBandGainDb = -24.0f;
inline constexpr float kMaxBandGainDb = +24.0f;

inline constexpr int kMinBands = 1;
inline constexpr int kMaxBands = 8;
inline constexpr int kDefaultBands = 4;

inline constexpr float kMinCrossoverHz = 20.0f;
inline constexpr float kMaxCrossoverHz = 20000.0f;
inline constexpr float kMinCrossoverSpacingOctaves = 0.5f;

} // namespace Krate::DSP
```

```cpp
// dsp/include/krate/dsp/systems/Disrumpo/morph_node.h
#pragma once

#include "distortion_types.h"

namespace Krate::DSP {

/// @brief Parameters for a single distortion algorithm.
/// Not all fields apply to all types - use the appropriate subset.
struct DistortionParams {
    // Common parameters
    float drive = kDefaultDrive;    ///< Drive amount [0, 10]
    float mix = 1.0f;               ///< Wet/dry mix [0, 1]
    float tone = kDefaultToneHz;    ///< Tone filter frequency [200, 8000] Hz

    // Saturation-specific
    float bias = 0.0f;              ///< Asymmetry bias [-1, 1]
    float sag = 0.0f;               ///< Sag/compression [0, 1]

    // Wavefold-specific
    float folds = 1.0f;             ///< Number of folds [1, 8]
    float shape = 0.0f;             ///< Fold shape [0, 1]
    float symmetry = 0.5f;          ///< Fold symmetry [0, 1]

    // Digital-specific
    float bitDepth = 16.0f;         ///< Bit depth [1, 16]
    float srRatio = 1.0f;           ///< Sample rate reduction [1, 32]
    float smoothness = 0.0f;        ///< Anti-aliasing smoothness [0, 1]

    // Dynamic-specific (Temporal)
    float sensitivity = 0.5f;       ///< Envelope sensitivity [0, 1]
    float attackMs = 10.0f;         ///< Envelope attack [1, 100] ms
    float releaseMs = 100.0f;       ///< Envelope release [10, 500] ms

    // Hybrid-specific (RingSaturation, FeedbackDist)
    float modDepth = 0.5f;          ///< Modulation depth [0, 1]
    float stages = 1.0f;            ///< Processing stages [1, 4]
    float feedback = 0.5f;          ///< Feedback amount [0, 1.5]
    float delayMs = 10.0f;          ///< Delay time [1, 100] ms

    // Aliasing-specific
    float freqShift = 0.0f;         ///< Frequency shift before downsample [-1000, 1000] Hz

    // Bitwise-specific
    float rotateAmount = 0.0f;      ///< Bit rotation amount [-16, 16]

    // Experimental-specific (Chaos, Formant, Granular)
    float chaosAmount = 0.5f;       ///< Chaos blend [0, 1]
    float attractorSpeed = 1.0f;    ///< Attractor evolution speed [0.1, 10]
    float grainSizeMs = 50.0f;      ///< Grain size [5, 100] ms
    float formantShift = 0.0f;      ///< Formant shift [-12, +12] semitones
};

/// @brief A morph node containing distortion type and position.
struct MorphNode {
    int id = 0;                     ///< Unique node identifier (0-3)
    DistortionType type = DistortionType::SoftClip;
    DistortionParams params;
    float posX = 0.0f;              ///< X position in morph space [0, 1]
    float posY = 0.0f;              ///< Y position in morph space [0, 1]
};

/// @brief State for a single frequency band.
struct BandState {
    float lowFreqHz = kMinCrossoverHz;
    float highFreqHz = kMaxCrossoverHz;

    std::array<MorphNode, 4> nodes; ///< Up to 4 morph nodes (fixed-size for RT safety)
    int activeNodeCount = 2;        ///< How many nodes are active (2-4)

    MorphMode morphMode = MorphMode::Linear1D;
    float morphX = 0.0f;            ///< Current morph X position [0, 1]
    float morphY = 0.0f;            ///< Current morph Y position [0, 1]

    float gainDb = 0.0f;            ///< Band output gain [-24, +24] dB
    float pan = 0.0f;               ///< Band pan [-1, +1]

    bool solo = false;
    bool bypass = false;
    bool mute = false;
};

} // namespace Krate::DSP
```

```cpp
// dsp/include/krate/dsp/systems/Disrumpo/modulation_types.h
#pragma once

#include <cstdint>

namespace Krate::DSP {

/// @brief Modulation source type.
enum class ModSource : uint8_t {
    None = 0,
    LFO1,
    LFO2,
    EnvFollower,
    Random,
    Macro1,
    Macro2,
    Macro3,
    Macro4,
    Chaos,          ///< Lorenz/Rossler/Chua attractor output
    SampleHold,     ///< Sample & hold (stepped modulation)
    PitchFollower,  ///< Detected input pitch
    Transient,      ///< Transient detector output
};

/// @brief Modulation curve shape.
enum class ModCurve : uint8_t {
    Linear = 0,
    Exponential,
    SCurve,
    Stepped,
};

/// @brief A single modulation routing.
struct ModRouting {
    ModSource source = ModSource::None;
    uint32_t destParamId = 0;       ///< Target parameter ID
    float amount = 0.0f;            ///< Modulation depth [-1, +1]
    ModCurve curve = ModCurve::Linear;
};

inline constexpr int kMaxModRoutings = 32;

} // namespace Krate::DSP
```

---

## 2. Preset System

### Standard VST3 Preset Format

Disrumpo uses the **standard VST3 preset format** (`.vstpreset` files). This ensures:
- Full host integration (preset browsers, save/load dialogs)
- Cross-DAW compatibility
- No custom file I/O code required

### Implementation

The Processor implements `getState()`/`setState()` to serialize all parameters:

```cpp
// processor.cpp
tresult PLUGIN_API Processor::getState(IBStream* state) {
    IBStreamer streamer(state, kLittleEndian);

    // Version for future compatibility
    streamer.writeInt32(kPresetVersion);

    // Global parameters
    streamer.writeFloat(inputGain_);
    streamer.writeFloat(outputGain_);
    streamer.writeFloat(globalMix_);
    streamer.writeInt32(bandCount_);
    streamer.writeInt32(maxOversample_);

    // Sweep parameters
    streamer.writeBool(sweepEnabled_);
    streamer.writeFloat(sweepFrequency_);
    streamer.writeFloat(sweepWidth_);
    streamer.writeFloat(sweepIntensity_);
    streamer.writeInt32(static_cast<int32>(sweepMorphLink_));

    // Per-band state (fixed 8 bands for format stability)
    for (int b = 0; b < kMaxBands; ++b) {
        const auto& band = bands_[b];
        streamer.writeFloat(band.lowFreqHz);
        streamer.writeFloat(band.highFreqHz);
        streamer.writeInt32(static_cast<int32>(band.morphMode));
        streamer.writeFloat(band.morphX);
        streamer.writeFloat(band.morphY);
        streamer.writeInt32(band.activeNodeCount);
        streamer.writeFloat(band.gainDb);
        streamer.writeFloat(band.pan);
        streamer.writeBool(band.solo);
        streamer.writeBool(band.bypass);
        streamer.writeBool(band.mute);

        // Per-node state (fixed 4 nodes)
        for (int n = 0; n < 4; ++n) {
            const auto& node = band.nodes[n];
            streamer.writeInt32(static_cast<int32>(node.type));
            streamer.writeFloat(node.params.drive);
            streamer.writeFloat(node.params.mix);
            streamer.writeFloat(node.params.tone);
            streamer.writeFloat(node.params.bias);
            streamer.writeFloat(node.params.folds);
            streamer.writeFloat(node.params.bitDepth);
            streamer.writeFloat(node.posX);
            streamer.writeFloat(node.posY);
        }
    }

    // Modulation routings
    streamer.writeInt32(activeRoutingCount_);
    for (int r = 0; r < kMaxModRoutings; ++r) {
        const auto& routing = modRoutings_[r];
        streamer.writeInt32(static_cast<int32>(routing.source));
        streamer.writeInt32(routing.destParamId);
        streamer.writeFloat(routing.amount);
        streamer.writeInt32(static_cast<int32>(routing.curve));
    }

    return kResultOk;
}

tresult PLUGIN_API Processor::setState(IBStream* state) {
    IBStreamer streamer(state, kLittleEndian);

    int32 version;
    if (!streamer.readInt32(version)) return kResultFalse;
    if (version > kPresetVersion) return kResultFalse;  // Future version

    // Read all parameters (mirror of getState)...
    return kResultOk;
}
```

### Factory Presets

Factory presets are `.vstpreset` files installed to the standard location:
- **Windows**: `C:\Program Files\Common Files\VST3\Presets\Krate Audio\Disrumpo\`
- **macOS**: `/Library/Audio/Presets/Krate Audio/Disrumpo/`
- **Linux**: `~/.vst3/presets/Krate Audio/Disrumpo/`

| Category | Count |
|----------|-------|
| Init | 5 |
| Sweep | 15 |
| Morph | 15 |
| Bass | 10 |
| Leads | 10 |
| Pads | 10 |
| Drums | 10 |
| Experimental | 15 |
| Chaos | 10 |
| Dynamic | 10 |
| Lo-Fi | 10 |
| **Total** | **120** |

### Preset Versioning

The preset format includes a version number to handle future additions:
- Version 1: Initial release
- Future versions add parameters at the end
- `setState()` gracefully handles older versions by using defaults for missing data

---

## 3. New Distortion Types (from DST-ROADMAP)

This section documents the additional distortion types added from the DST-ROADMAP, extending the original 14 types to 26.

### D15: Temporal Distortion
**Category:** Dynamic | **Status:** Implemented (spec 108)

Envelope-following drive modulation. The distortion intensity varies based on the input signal's amplitude.

| Mode | Behavior |
|------|----------|
| EnvelopeFollow | Louder = more distortion |
| InverseEnvelope | Quiet = more distortion (expansion) |
| Derivative | Transients get different curve |
| Hysteresis | Deep memory of recent samples |

**Use Cases:** Expressive dynamics, breathing textures, transient shaping

### D16: Ring Saturation
**Category:** Hybrid | **Status:** Implemented (spec 109)

Self-modulation: `output = input * saturate(input * drive)`. Creates inharmonic sidebands like ring mod, but signal-coherent.

**Character:** Metallic, bell-like, aggressive at high settings

### D17: Feedback Distortion
**Category:** Hybrid | **Status:** Implemented (spec 110)

Delay + saturator + feedback loop with limiting. Creates sustained, singing distortion that can approach self-oscillation.

**Use Cases:** Screaming leads, feedback effects, infinite sustain

### D18: Aliasing Effect
**Category:** Digital | **Status:** Implemented (spec 112)

Intentional aliasing via downsampling without anti-aliasing filter. Optional frequency shifting before downsample creates unique spectral folding patterns.

**Character:** Digital grunge, lo-fi, metallic artifacts

### D19: Bitwise Mangler
**Category:** Digital | **Status:** Implemented (spec 111)

Operations on the bit representation of samples: XOR, bit rotation, bit shuffle, overflow wrap.

| Operation | Character |
|-----------|-----------|
| XorPattern | Harmonically complex noise |
| XorPrevious | Sample-correlated artifacts |
| BitRotate | Extreme tonal shifts |
| OverflowWrap | Hard digital clipping with wraparound |

### D20: Chaos Distortion
**Category:** Experimental | **Status:** Implemented

Chaotic attractor waveshaping using Lorenz, Rossler, Chua, or Henon systems. The transfer function evolves over time, creating living, breathing distortion.

**Character:** Organic, unpredictable, evolving textures

### D21: Formant Distortion
**Category:** Experimental | **Status:** Implemented

Distortion through vocal-tract-like resonances. Combines FormantFilter with waveshaping for "talking distortion" effects.

**Character:** Vowel-shaped, vocal, alien textures

### D22: Granular Distortion
**Category:** Experimental | **Status:** Implemented

Per-grain variable distortion. Each micro-grain (5-100ms) gets different drive, algorithm, or parameters, creating evolving textured destruction.

**Character:** Glitchy, textured, pointillist

### D23: Spectral Distortion
**Category:** Experimental | **Status:** Implemented

Apply distortion algorithms per-frequency-bin in the spectral domain via FFT. Can saturate magnitudes while preserving phase perfectly—impossible in time domain.

| Mode | Behavior |
|------|----------|
| PerBinSaturate | Apply saturation to each bin's magnitude |
| MagnitudeOnly | Saturate magnitudes, preserve phase exactly |
| BinSelective | Different distortion per frequency range |
| SpectralBitcrush | Quantize magnitudes per bin |

**Character:** Impossible frequency-selective distortion, phase-coherent saturation

### D24: Fractal Distortion
**Category:** Experimental | **Status:** Implemented

Recursive multi-scale distortion creating harmonic structure that reveals new detail at every "zoom level."

| Mode | Behavior |
|------|----------|
| Residual | Distort progressively smaller residuals |
| Multiband | Split into octave bands, recurse each with depth scaling |
| Harmonic | Separate odd/even harmonics, different curves per level |
| Cascade | Different waveshaper at each iteration level |
| Feedback | Cross-feed between iteration levels (chaotic) |

**Character:** Self-similar harmonic stacking, "zoom into detail" effect

### D25: Stochastic Distortion
**Category:** Experimental | **Status:** Implemented

Randomized transfer function simulating analog component tolerance variation. Each sample gets slightly different curve.

**Use Cases:** Analog warmth, component drift simulation, organic variation

### D26: Allpass Resonant Distortion
**Category:** Hybrid | **Status:** Implemented

Place saturation inside allpass filter feedback loops for resonant, pitched distortion.

| Topology | Behavior |
|----------|----------|
| SingleAllpass | One allpass with saturation in feedback |
| AllpassChain | Series of allpass filters with saturation |
| KarplusStrong | Delay + saturator + feedback (plucked string) |
| FeedbackMatrix | 4x4 matrix of cross-fed saturators |

**Character:** Pitched/resonant distortion that can self-oscillate

---

## 4. Oversampling Recommendations

| Type | Factor | Rationale |
|------|--------|-----------|
| Soft Clip | 2x | Mild harmonics |
| Hard Clip, Fuzz | 4x | Strong harmonics |
| Wavefolders | 4x | Many harmonics from folding |
| Rectifiers | 4x | Frequency doubling |
| Digital (D12-D14) | 1x | Aliasing intentional |
| Temporal | 2x | Moderate harmonics |
| Ring Saturation | 4x | Inharmonic sidebands |
| Feedback | 2x | Controlled by limiter |
| Aliasing (D18) | 1x | Aliasing is the effect |
| Bitwise | 1x | Artifacts are the effect |
| Chaos | 2x | Moderate, unpredictable |
| Formant | 2x | Resonances focus energy |
| Granular | 2x | Per-grain varies |
| Spectral | 1x | FFT domain, no aliasing |
| Fractal | 2x | Varies by iteration depth |
| Stochastic | 2x | Randomized curves |
| Allpass Resonant | 4x | Self-oscillation potential |

---

## 5. File Structure

Following the Krate Audio monorepo pattern:

```
krate-audio/
├── dsp/                                    # Shared KrateDSP library (PLUGIN-AGNOSTIC)
│   ├── include/krate/dsp/
│   │   ├── systems/                       # Layer 3: Generic reusable systems
│   │   │   ├── crossover_network.h        # Linkwitz-Riley multiband crossover (NEW)
│   │   │   └── ... (existing systems)
│   │   ├── processors/                    # Layer 2: Existing processors (used as-is)
│   │   │   ├── saturation_processor.h     # → D01-D02
│   │   │   ├── tube_stage.h               # → D03
│   │   │   ├── tape_saturator.h           # → D04
│   │   │   ├── fuzz_processor.h           # → D05-D06
│   │   │   ├── wavefolder_processor.h     # → D07-D09
│   │   │   ├── bitcrusher_processor.h     # → D12-D14
│   │   │   └── envelope_follower.h        # Modulation source
│   │   └── primitives/                    # Layer 1: Existing primitives (used as-is)
│   │       ├── biquad.h                   # Filter building blocks
│   │       ├── oversampler.h              # Anti-aliasing
│   │       ├── lfo.h                      # Modulation source
│   │       ├── smoother.h                 # Parameter smoothing
│   │       └── sample_rate_reducer.h      # → D13
│   └── tests/
│       └── unit/systems/
│           └── crossover_network_test.cpp # Tests for new generic component
│
├── plugins/
│   └── Disrumpo/                           # Disrumpo VST3 plugin
│       ├── CMakeLists.txt
│       ├── src/
│       │   ├── entry.cpp                  # VST3 entry point
│       │   ├── plugin_ids.h               # Parameter ID definitions
│       │   ├── version.h                  # Plugin version info
│       │   ├── dsp/                       # PLUGIN-SPECIFIC DSP composition
│       │   │   ├── distortion_types.h     # Disrumpo distortion enum
│       │   │   ├── distortion_adapter.h   # Unified interface to KrateDSP processors
│       │   │   ├── morph_engine.h         # Weight computation, interpolation
│       │   │   ├── band_processor.h       # Per-band morph + distortion
│       │   │   ├── sweep_processor.h      # Frequency sweep with intensity
│       │   │   └── modulation_engine.h    # LFO/Env routing
│       │   ├── processor/
│       │   │   ├── processor.h
│       │   │   └── processor.cpp          # Audio processing (composes plugin DSP)
│       │   └── controller/
│       │       ├── controller.h
│       │       └── controller.cpp         # UI & parameter management
│       ├── tests/
│       │   ├── unit/                      # Plugin-specific unit tests
│       │   │   ├── distortion_adapter_test.cpp
│       │   │   ├── morph_engine_test.cpp
│       │   │   └── band_processor_test.cpp
│       │   ├── integration/               # Full signal path tests
│       │   └── approval/                  # Regression tests
│       └── resources/
│           ├── editor.uidesc              # VSTGUI UI definition
│           ├── presets/                   # Factory presets (.vstpreset)
│           └── installers/                # Platform installers
│
└── specs/
    └── Disrumpo/                           # Feature specifications
        ├── spec.md                        # Requirements
        ├── plan.md                        # Implementation plan
        ├── tasks.md                       # Task breakdown
        ├── ui-mockups.md                  # UI specifications
        └── dsp-details.md                 # DSP implementation (this file)
```

### Key Architecture Notes

1. **DSP library is plugin-agnostic**: KrateDSP contains only generic, reusable building blocks. No plugin-specific code lives there.

2. **Plugin-specific DSP in plugin folder**: Disrumpo-specific composition (morph engine, distortion adapter, band processor) lives in `plugins/Disrumpo/src/dsp/`. This follows the same pattern as Iterum.

3. **Compose existing processors**: The `DistortionAdapter` in the plugin composes existing KrateDSP processors (`SaturationProcessor`, `TubeStage`, `FuzzProcessor`, `WavefolderProcessor`, `BitcrusherProcessor`).

4. **Generic components may be promoted**: If `CrossoverNetwork` proves useful to multiple plugins, it can be added to `dsp/include/krate/dsp/systems/`. Start in the plugin, promote when reuse is proven.

5. **Test hierarchy**:
   - `dsp/tests/`: Tests for generic KrateDSP components
   - `plugins/Disrumpo/tests/`: Tests for Disrumpo-specific composition

---

## 6. Code Examples

### Distortion Adapter Pattern

```cpp
// plugins/Disrumpo/src/dsp/distortion_adapter.h
#pragma once

#include <krate/dsp/processors/saturation_processor.h>
#include <krate/dsp/processors/tube_stage.h>
#include <krate/dsp/processors/tape_saturator.h>
#include <krate/dsp/processors/fuzz_processor.h>
#include <krate/dsp/processors/wavefolder_processor.h>
#include <krate/dsp/processors/bitcrusher_processor.h>
#include <krate/dsp/processors/temporal_distortion.h>
#include <krate/dsp/processors/feedback_distortion.h>
#include <krate/dsp/processors/aliasing_effect.h>
#include <krate/dsp/processors/spectral_distortion.h>
#include <krate/dsp/processors/fractal_distortion.h>
#include <krate/dsp/processors/formant_distortion.h>
#include <krate/dsp/processors/granular_distortion.h>
#include <krate/dsp/processors/allpass_saturator.h>
#include <krate/dsp/primitives/sample_rate_reducer.h>
#include <krate/dsp/primitives/ring_saturation.h>
#include <krate/dsp/primitives/bitwise_mangler.h>
#include <krate/dsp/primitives/chaos_waveshaper.h>
#include <krate/dsp/primitives/stochastic_shaper.h>
#include "distortion_types.h"
#include "morph_node.h"

#include <variant>
#include <cmath>

namespace Krate::DSP {

/// @brief Unified interface for all distortion types.
/// Real-time safe: no allocations after prepare().
class DistortionAdapter {
public:
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;

        // Prepare all processor variants
        saturation_.prepare(sampleRate);
        tube_.prepare(sampleRate);
        tape_.prepare(sampleRate);
        fuzz_.prepare(sampleRate);
        wavefolder_.prepare(sampleRate);
        bitcrusher_.prepare(sampleRate);
        srReducer_.prepare(sampleRate);
        // New processors from DST-ROADMAP
        temporal_.prepare(sampleRate, 512);
        ringSaturation_.prepare(sampleRate);
        feedbackDist_.prepare(sampleRate, 512);
        aliasing_.prepare(sampleRate, 512);
        bitwiseMangler_.prepare(sampleRate);
        // All experimental types now implemented
        chaos_.prepare(sampleRate);
        formant_.prepare(sampleRate, 512);
        granular_.prepare(sampleRate, 512);
        spectral_.prepare(sampleRate, 2048);
        fractal_.prepare(sampleRate, 512);
        stochastic_.prepare(sampleRate);
        allpassSaturator_.prepare(sampleRate, 512);
    }

    void reset() noexcept {
        saturation_.reset();
        tube_.reset();
        tape_.reset();
        fuzz_.reset();
        wavefolder_.reset();
        bitcrusher_.reset();
        srReducer_.reset();
        temporal_.reset();
        ringSaturation_.reset();
        feedbackDist_.reset();
        aliasing_.reset();
        bitwiseMangler_.reset();
        chaos_.reset();
        formant_.reset();
        granular_.reset();
        spectral_.reset();
        fractal_.reset();
        stochastic_.reset();
        allpassSaturator_.reset();
    }

    void setType(DistortionType type) noexcept {
        currentType_ = type;

        // Configure the appropriate processor
        switch (type) {
            case DistortionType::SoftClip:
                saturation_.setType(SaturationProcessor::Type::Tape);
                break;
            case DistortionType::HardClip:
                saturation_.setType(SaturationProcessor::Type::Digital);
                break;
            case DistortionType::Tube:
                // TubeStage is ready to use
                break;
            case DistortionType::Tape:
                tape_.setModel(TapeSaturator::Model::Hysteresis);
                break;
            case DistortionType::Fuzz:
                fuzz_.setType(FuzzProcessor::Type::Germanium);
                break;
            case DistortionType::AsymmetricFuzz:
                fuzz_.setType(FuzzProcessor::Type::Silicon);
                break;
            case DistortionType::SineFold:
                wavefolder_.setModel(WavefolderProcessor::Model::Serge);
                break;
            case DistortionType::TriangleFold:
                wavefolder_.setModel(WavefolderProcessor::Model::Simple);
                break;
            case DistortionType::SergeFold:
                wavefolder_.setModel(WavefolderProcessor::Model::Lockhart);
                break;
            case DistortionType::Bitcrush:
            case DistortionType::Quantize:
                // BitcrusherProcessor handles both
                break;
            case DistortionType::SampleReduce:
                // SampleRateReducer is ready
                break;
            case DistortionType::Temporal:
                temporal_.setMode(TemporalDistortion::Mode::EnvelopeFollow);
                break;
            case DistortionType::RingSaturation:
                // RingSaturation is ready
                break;
            case DistortionType::FeedbackDist:
                // FeedbackDistortion is ready
                break;
            case DistortionType::Aliasing:
                // AliasingEffect is ready
                break;
            case DistortionType::BitwiseMangler:
                bitwiseMangler_.setOperation(BitwiseMangler::Operation::XorPattern);
                break;
            default:
                break;
        }
    }

    [[nodiscard]] float process(float input) noexcept {
        switch (currentType_) {
            case DistortionType::SoftClip:
            case DistortionType::HardClip:
                return saturation_.process(input);
            case DistortionType::Tube:
                return tube_.process(input);
            case DistortionType::Tape:
                return tape_.process(input);
            case DistortionType::Fuzz:
            case DistortionType::AsymmetricFuzz:
                return fuzz_.process(input);
            case DistortionType::SineFold:
            case DistortionType::TriangleFold:
            case DistortionType::SergeFold:
                return wavefolder_.process(input);
            case DistortionType::Bitcrush:
            case DistortionType::Quantize:
                return bitcrusher_.process(input);
            case DistortionType::SampleReduce:
                return srReducer_.process(input);
            case DistortionType::FullRectify:
                return std::abs(input);
            case DistortionType::HalfRectify:
                return std::max(0.0f, input);
            case DistortionType::Temporal:
                return temporal_.process(input);
            case DistortionType::RingSaturation:
                return ringSaturation_.process(input);
            case DistortionType::FeedbackDist:
                return feedbackDist_.process(input);
            case DistortionType::Aliasing:
                return aliasing_.process(input);
            case DistortionType::BitwiseMangler:
                return bitwiseMangler_.process(input);
            case DistortionType::Chaos:
                return chaos_.process(input);
            case DistortionType::Formant:
                return formant_.process(input);
            case DistortionType::Granular:
                return granular_.process(input);
            case DistortionType::Spectral:
                return spectral_.process(input);
            case DistortionType::Fractal:
                return fractal_.process(input);
            case DistortionType::Stochastic:
                return stochastic_.process(input);
            case DistortionType::AllpassResonant:
                return allpassSaturator_.process(input);
            default:
                return input;
        }
    }

private:
    double sampleRate_ = 44100.0;
    DistortionType currentType_ = DistortionType::SoftClip;

    // Original processor instances
    SaturationProcessor saturation_;
    TubeStage tube_;
    TapeSaturator tape_;
    FuzzProcessor fuzz_;
    WavefolderProcessor wavefolder_;
    BitcrusherProcessor bitcrusher_;
    SampleRateReducer srReducer_;

    // New processors from DST-ROADMAP
    TemporalDistortion temporal_;
    RingSaturation ringSaturation_;
    FeedbackDistortion feedbackDist_;
    AliasingEffect aliasing_;
    BitwiseMangler bitwiseMangler_;

    // Experimental processors (all implemented)
    ChaosWaveshaper chaos_;
    FormantDistortion formant_;
    GranularDistortion granular_;
    SpectralDistortion spectral_;
    FractalDistortion fractal_;
    StochasticShaper stochastic_;
    AllpassSaturator allpassSaturator_;
};

} // namespace Krate::DSP
```

### Crossover Network (Linkwitz-Riley 4th Order)

> **Note:** The crossover network uses the existing `CrossoverLR4` from FLT-ROADMAP (spec 079).
> Location: `dsp/include/krate/dsp/processors/crossover_filter.h`

```cpp
// plugins/Disrumpo/src/dsp/crossover_network.h
// Wraps existing CrossoverLR4 for multi-band use
#pragma once

#include <krate/dsp/processors/crossover_filter.h>
#include <array>
#include <vector>

namespace Krate::DSP {

/// @brief Multi-band crossover network for 1-8 bands.
/// Uses existing CrossoverLR4 from KrateDSP (FLT-ROADMAP spec 079).
class CrossoverNetwork {
public:
    void prepare(double sampleRate, int numBands) noexcept {
        sampleRate_ = sampleRate;
        numBands_ = std::clamp(numBands, kMinBands, kMaxBands);

        // N bands requires N-1 crossovers
        crossovers_.resize(numBands_ - 1);
        for (auto& xover : crossovers_) {
            xover.prepare(sampleRate);
        }
    }

    void setCrossoverFrequencies(const std::vector<float>& frequencies) noexcept {
        for (size_t i = 0; i < crossovers_.size() && i < frequencies.size(); ++i) {
            crossovers_[i].setCrossoverFrequency(frequencies[i]);
        }
    }

    /// @brief Process input and output to band buffers.
    void process(float input, std::array<float, kMaxBands>& bands) noexcept {
        if (numBands_ == 1) {
            bands[0] = input;
            return;
        }

        // Cascaded splitting: split input, then split each output
        float remaining = input;
        for (int i = 0; i < numBands_ - 1; ++i) {
            auto outputs = crossovers_[i].process(remaining);
            bands[i] = outputs.low;
            remaining = outputs.high;
        }
        bands[numBands_ - 1] = remaining;
    }

private:
    double sampleRate_ = 44100.0;
    int numBands_ = kDefaultBands;
    std::vector<CrossoverLR4> crossovers_;  // Uses existing KrateDSP component
};

} // namespace Krate::DSP
```

### Using Existing LFO for Modulation

```cpp
// Example: Using existing LFO in modulation engine
#include <krate/dsp/primitives/lfo.h>
#include <krate/dsp/processors/envelope_follower.h>

namespace Krate::DSP {

class DisrumpoModulationEngine {
public:
    void prepare(double sampleRate) noexcept {
        lfo1_.prepare(sampleRate);
        lfo2_.prepare(sampleRate);
        envFollower_.prepare(sampleRate);

        // Configure LFO defaults per spec
        lfo1_.setFrequency(1.0f);  // 1 Hz default
        lfo1_.setWaveform(Waveform::Sine);

        lfo2_.setFrequency(0.5f);
        lfo2_.setWaveform(Waveform::Triangle);
        lfo2_.setTempoSync(true);
        lfo2_.setNoteValue(NoteValue::Quarter);
    }

    void setTempo(float bpm) noexcept {
        lfo1_.setTempo(bpm);
        lfo2_.setTempo(bpm);
    }

    /// @brief Process modulation sources and return values.
    void process(float audioInput,
                 float& lfo1Out, float& lfo2Out, float& envOut) noexcept {
        lfo1Out = lfo1_.process();
        lfo2Out = lfo2_.process();
        envOut = envFollower_.process(audioInput);
    }

private:
    LFO lfo1_;
    LFO lfo2_;
    EnvelopeFollower envFollower_;
};

} // namespace Krate::DSP
```

### Intelligent Oversampling Selection

```cpp
// Example: Per-band oversampling based on distortion type
#include <krate/dsp/primitives/oversampler.h>

namespace Krate::DSP {

/// @brief Get recommended oversampling factor for a distortion type.
constexpr int getRecommendedOversample(DistortionType type) noexcept {
    switch (type) {
        case DistortionType::SoftClip:
            return 2;  // Mild harmonics
        case DistortionType::HardClip:
        case DistortionType::Fuzz:
        case DistortionType::AsymmetricFuzz:
            return 4;  // Strong harmonics
        case DistortionType::SineFold:
        case DistortionType::TriangleFold:
        case DistortionType::SergeFold:
            return 4;  // Many harmonics from folding
        case DistortionType::FullRectify:
        case DistortionType::HalfRectify:
            return 4;  // Frequency doubling
        case DistortionType::Bitcrush:
        case DistortionType::SampleReduce:
        case DistortionType::Quantize:
            return 1;  // Aliasing is intentional
        case DistortionType::Temporal:
            return 2;  // Moderate harmonics
        case DistortionType::RingSaturation:
            return 4;  // Inharmonic sidebands
        case DistortionType::FeedbackDist:
            return 2;  // Controlled by limiter
        case DistortionType::Aliasing:
        case DistortionType::BitwiseMangler:
            return 1;  // Artifacts ARE the effect
        case DistortionType::Chaos:
        case DistortionType::Formant:
        case DistortionType::Granular:
        case DistortionType::Fractal:
        case DistortionType::Stochastic:
            return 2;  // Moderate, varies by settings
        case DistortionType::Spectral:
            return 1;  // FFT domain, no aliasing
        case DistortionType::AllpassResonant:
            return 4;  // Self-oscillation potential
        default:
            return 2;
    }
}

/// @brief Calculate weighted oversampling for morph position.
int calculateMorphOversample(const std::array<MorphNode, 4>& nodes,
                              int activeCount,
                              const std::array<float, 4>& weights,
                              int maxOversample) noexcept {
    float weightedSum = 0.0f;
    for (int i = 0; i < activeCount; ++i) {
        int recommended = getRecommendedOversample(nodes[i].type);
        weightedSum += weights[i] * static_cast<float>(recommended);
    }

    int result = static_cast<int>(std::ceil(weightedSum));

    // Round up to power of 2 and clamp to max
    if (result <= 1) return 1;
    if (result <= 2) return std::min(2, maxOversample);
    if (result <= 4) return std::min(4, maxOversample);
    return maxOversample;
}

} // namespace Krate::DSP
```

---

## 7. Morph Algorithm Specification

This section defines the exact behavior of the morph system when blending between distortion types.

### 7.1 Morph Weight Calculation (Inverse Distance Weighting)

When the morph cursor is positioned in 2D space with multiple nodes, weights are calculated using **inverse distance weighting with exponent p=2**:

```cpp
/// @brief Calculate morph weights using inverse distance weighting.
/// @param cursorX Morph cursor X position [0, 1]
/// @param cursorY Morph cursor Y position [0, 1]
/// @param nodes Array of morph nodes with positions
/// @param activeCount Number of active nodes (2-4)
/// @param weights Output array of normalized weights
void calculateMorphWeights(float cursorX, float cursorY,
                           const std::array<MorphNode, 4>& nodes,
                           int activeCount,
                           std::array<float, 4>& weights) noexcept {
    constexpr float kExponent = 2.0f;
    constexpr float kMinDistance = 0.001f;  // Prevent division by zero

    float weightSum = 0.0f;

    for (int i = 0; i < activeCount; ++i) {
        float dx = cursorX - nodes[i].posX;
        float dy = cursorY - nodes[i].posY;
        float distance = std::sqrt(dx * dx + dy * dy);

        // If cursor is exactly on a node, that node gets 100% weight
        if (distance < kMinDistance) {
            std::fill(weights.begin(), weights.end(), 0.0f);
            weights[i] = 1.0f;
            return;
        }

        weights[i] = 1.0f / std::pow(distance, kExponent);
        weightSum += weights[i];
    }

    // Normalize weights to sum to 1.0
    for (int i = 0; i < activeCount; ++i) {
        weights[i] /= weightSum;
    }

    // Zero out inactive nodes
    for (int i = activeCount; i < 4; ++i) {
        weights[i] = 0.0f;
    }
}
```

### 7.2 Cross-Family Morph Behavior

When morphing between distortion types from **different families** (e.g., Saturation → Wavefold), a hybrid approach is used:

#### Transition Zone Model

| Morph Position | Behavior |
|----------------|----------|
| 0% - 40% | **Dominant algorithm only** (algorithm A runs, B is off) |
| 40% - 60% | **Transition zone** (both algorithms run, output blended) |
| 60% - 100% | **Dominant algorithm only** (algorithm B runs, A is off) |

#### Equal-Power Crossfade

Within the transition zone, the output is blended using an **equal-power crossfade** to maintain perceived loudness:

```cpp
/// @brief Calculate equal-power crossfade gains for transition zone.
/// @param morphPosition Overall morph position [0, 1]
/// @param gainA Output gain for algorithm A
/// @param gainB Output gain for algorithm B
/// @return true if in transition zone (both algorithms needed)
bool calculateCrossfadeGains(float morphPosition,
                              float& gainA, float& gainB) noexcept {
    constexpr float kZoneStart = 0.4f;
    constexpr float kZoneEnd = 0.6f;

    if (morphPosition < kZoneStart) {
        // Algorithm A only
        gainA = 1.0f;
        gainB = 0.0f;
        return false;
    }

    if (morphPosition > kZoneEnd) {
        // Algorithm B only
        gainA = 0.0f;
        gainB = 1.0f;
        return false;
    }

    // Transition zone: equal-power crossfade
    float zonePosition = (morphPosition - kZoneStart) / (kZoneEnd - kZoneStart);
    float angle = zonePosition * kPi * 0.5f;

    gainA = std::cos(angle);
    gainB = std::sin(angle);
    return true;
}
```

#### Fade-In on Zone Entry

When entering the transition zone, the secondary algorithm fades in over **5-10ms** to prevent clicks from cold filter states:

```cpp
/// @brief Manages fade-in when secondary algorithm activates.
class TransitionFader {
public:
    void prepare(double sampleRate) noexcept {
        fadeIncrement_ = 1.0f / static_cast<float>(sampleRate * 0.007);  // 7ms
    }

    /// @brief Call when entering transition zone.
    void startFadeIn() noexcept {
        fadeProgress_ = 0.0f;
        fading_ = true;
    }

    /// @brief Apply fade to secondary algorithm's contribution.
    float applyFade(float secondaryGain) noexcept {
        if (!fading_) return secondaryGain;

        fadeProgress_ += fadeIncrement_;
        if (fadeProgress_ >= 1.0f) {
            fadeProgress_ = 1.0f;
            fading_ = false;
        }
        return secondaryGain * fadeProgress_;
    }

private:
    float fadeProgress_ = 1.0f;
    float fadeIncrement_ = 0.001f;
    bool fading_ = false;
};
```

#### Parameter Handling

- **Common parameters** (Drive, Mix, Tone): Interpolated between node values based on weights
- **Type-specific parameters**: Each algorithm uses its own stored values; no cross-mapping
- **Output** is blended; parameters are not

### 7.3 Same-Family Morph Behavior

When morphing within the **same family** (e.g., Tube → Tape, both Saturation):

- Single algorithm instance with interpolated parameters
- Transfer function coefficients interpolated directly
- No parallel processing needed
- CPU cost remains constant

---

## 8. Sweep-Morph Linking Curves

The sweep-morph linking system maps sweep frequency position to morph position using preset curves.

### 8.1 Available Curves

| Curve | Formula | Musical Intent |
|-------|---------|----------------|
| **Linear** | `y = x` | Baseline, predictable |
| **Ease In** | `y = x²` | Slow start → fast end ("save destruction for the end") |
| **Ease Out** | `y = 1 - (1-x)²` | Fast start → gentle landing (transient emphasis) |
| **Ease In-Out** | `y = x² * (3 - 2x)` | Smooth, natural, "musical default" |
| **Hold → Rise** | See below | Flat until ~60%, then fast ramp (performance sweeps) |
| **Stepped** | `y = floor(x * 4) / 3` | Glitch/digital character (4 discrete steps) |

### 8.2 Implementation

```cpp
/// @brief Sweep-morph linking curve types.
enum class SweepMorphCurve : uint8_t {
    None = 0,       // Independent control
    Linear,         // y = x
    Inverse,        // y = 1 - x
    EaseIn,         // y = x²
    EaseOut,        // y = 1 - (1-x)²
    EaseInOut,      // y = smoothstep(x)
    HoldRise,       // Flat then ramp
    Stepped,        // 4 discrete levels
};

/// @brief Apply sweep-morph curve to normalized frequency position.
/// @param x Normalized sweep position [0, 1] (0 = low freq, 1 = high freq)
/// @param curve Selected curve type
/// @return Morph position [0, 1]
float applySweepMorphCurve(float x, SweepMorphCurve curve) noexcept {
    switch (curve) {
        case SweepMorphCurve::None:
            return 0.5f;  // No linking, return center

        case SweepMorphCurve::Linear:
            return x;

        case SweepMorphCurve::Inverse:
            return 1.0f - x;

        case SweepMorphCurve::EaseIn:
            return x * x;

        case SweepMorphCurve::EaseOut:
            return 1.0f - (1.0f - x) * (1.0f - x);

        case SweepMorphCurve::EaseInOut:
            return x * x * (3.0f - 2.0f * x);  // smoothstep

        case SweepMorphCurve::HoldRise: {
            // Flat at 0 until 60%, then linear ramp to 1
            constexpr float kHoldPoint = 0.6f;
            if (x < kHoldPoint) return 0.0f;
            return (x - kHoldPoint) / (1.0f - kHoldPoint);
        }

        case SweepMorphCurve::Stepped:
            return std::floor(x * 4.0f) / 3.0f;

        default:
            return x;
    }
}
```

---

## 9. Modulation Curve Formulas

Modulation routing uses the same curve formulas as sweep-morph linking for consistency.

### 9.1 Available Curves

| Curve | Formula | Use Case |
|-------|---------|----------|
| **Linear** | `y = x` | Direct control |
| **Exponential In** | `y = x²` | Slow start, fast end (filter sweeps) |
| **Exponential Out** | `y = 1 - (1-x)²` | Fast start, gentle end (amp/drive) |
| **S-Curve** | `y = x² * (3 - 2x)` | Smooth, natural response |
| **Stepped** | `y = floor(x * 4) / 3` | Quantized, digital character |

### 9.2 Bipolar Modulation Handling

**Critical rule:** The curve is always applied to the **absolute value** of the modulation, then the sign is applied at the end. This ensures symmetrical behavior for positive and negative modulation amounts.

```cpp
/// @brief Modulation curve types.
enum class ModCurve : uint8_t {
    Linear = 0,
    ExponentialIn,
    ExponentialOut,
    SCurve,
    Stepped,
};

/// @brief Apply modulation curve with correct bipolar handling.
/// @param modInput Raw modulation value [0, 1] from source
/// @param amount Modulation amount [-1, +1]
/// @param curve Curve type
/// @return Final modulation value
float applyModulationCurve(float modInput, float amount, ModCurve curve) noexcept {
    // Clamp input to valid range
    float x = std::clamp(modInput, 0.0f, 1.0f);

    // Apply curve (always in positive domain)
    float shaped;
    switch (curve) {
        case ModCurve::Linear:
            shaped = x;
            break;
        case ModCurve::ExponentialIn:
            shaped = x * x;
            break;
        case ModCurve::ExponentialOut:
            shaped = 1.0f - (1.0f - x) * (1.0f - x);
            break;
        case ModCurve::SCurve:
            shaped = x * x * (3.0f - 2.0f * x);
            break;
        case ModCurve::Stepped:
            shaped = std::floor(x * 4.0f) / 3.0f;
            break;
        default:
            shaped = x;
    }

    // Apply amount (including sign) at the end
    return shaped * amount;
}
```

### 9.3 Multiple Modulation Sources

When multiple modulation routings target the same parameter, their contributions are **summed and clamped**:

```cpp
float finalMod = std::clamp(mod1 + mod2 + mod3, -1.0f, 1.0f);
```

---

## 10. Advanced Modulation Sources

This section specifies the behavior of the four advanced modulation sources.

### 10.1 Chaos Modulation Source

The Chaos source outputs the X-axis of a chaotic attractor for organic, evolving modulation.

| Aspect | Specification |
|--------|---------------|
| **Output** | X-axis of selected attractor |
| **Models** | Lorenz, Rössler, Chua, Hénon |
| **Speed** | Integration time multiplier (0.05 - 20.0) |
| **Coupling** | Audio amplitude → state perturbation |
| **Normalization** | Fixed per-model scaling to [-1, +1] |

#### Implementation

```cpp
/// @brief Chaos attractor modulation source.
class ChaosModSource {
public:
    enum class Model { Lorenz, Rossler, Chua, Henon };

    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
        baseDt_ = 1.0 / sampleRate_;
        envFollower_.prepare(sampleRate, 10.0f);  // 10ms envelope
    }

    void setModel(Model model) noexcept {
        model_ = model;
        updateScaling();
    }

    void setSpeed(float speed) noexcept {
        speed_ = std::clamp(speed, 0.05f, 20.0f);
    }

    void setCoupling(float coupling) noexcept {
        coupling_ = std::clamp(coupling, 0.0f, 1.0f);
    }

    float process(float audioInput) noexcept {
        // Get audio envelope for coupling
        float env = envFollower_.process(std::abs(audioInput));

        // Apply coupling as state perturbation
        float perturbation = coupling_ * env * 0.05f;
        x_ += perturbation * (randomBipolar() * 0.5f);
        y_ += perturbation * (randomBipolar() * 0.5f);

        // Integrate attractor with speed multiplier
        float dt = baseDt_ * speed_ * 1000.0f;  // Scale for musical rates
        integrateAttractor(dt);

        // Normalize output to [-1, +1]
        return std::clamp(x_ / scale_, -1.0f, 1.0f);
    }

private:
    void integrateAttractor(float dt) noexcept {
        switch (model_) {
            case Model::Lorenz: {
                constexpr float sigma = 10.0f, rho = 28.0f, beta = 8.0f / 3.0f;
                float dx = sigma * (y_ - x_);
                float dy = x_ * (rho - z_) - y_;
                float dz = x_ * y_ - beta * z_;
                x_ += dx * dt;
                y_ += dy * dt;
                z_ += dz * dt;
                break;
            }
            case Model::Rossler: {
                constexpr float a = 0.2f, b = 0.2f, c = 5.7f;
                float dx = -y_ - z_;
                float dy = x_ + a * y_;
                float dz = b + z_ * (x_ - c);
                x_ += dx * dt;
                y_ += dy * dt;
                z_ += dz * dt;
                break;
            }
            // Chua and Henon implementations similar...
            default:
                break;
        }
    }

    void updateScaling() noexcept {
        // Fixed scaling per model based on known attractor bounds
        switch (model_) {
            case Model::Lorenz:  scale_ = 20.0f; break;
            case Model::Rossler: scale_ = 10.0f; break;
            case Model::Chua:    scale_ = 2.0f;  break;
            case Model::Henon:   scale_ = 1.5f;  break;
        }
    }

    Model model_ = Model::Lorenz;
    double sampleRate_ = 44100.0;
    double baseDt_ = 1.0 / 44100.0;
    float speed_ = 1.0f;
    float coupling_ = 0.0f;
    float scale_ = 20.0f;

    // Attractor state
    float x_ = 0.1f, y_ = 0.0f, z_ = 0.0f;

    EnvelopeFollower envFollower_;
};
```

### 10.2 Sample & Hold Source

Sample & Hold periodically samples a source signal and holds the value until the next sampling event.

| Aspect | Specification |
|--------|---------------|
| **Sources** | Random (white noise), LFO (selected LFO output), External (audio amplitude) |
| **Rate** | Sampling frequency: 0.1 - 50 Hz (free) or tempo-synced |
| **Slew** | Output smoothing time constant: 0 - 500 ms |
| **Output** | [-1, +1] for Random/LFO, [0, +1] for External |

#### Implementation

```cpp
/// @brief Sample & Hold modulation source.
class SampleHoldSource {
public:
    enum class Source { Random, LFO, External };

    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
        envFollower_.prepare(sampleRate, 7.0f);  // 7ms for external
        slewFilter_.prepare(sampleRate);
    }

    void setSource(Source source) noexcept { source_ = source; }

    void setRate(float hz) noexcept {
        rateHz_ = std::clamp(hz, 0.1f, 50.0f);
        samplesPerTrigger_ = static_cast<int>(sampleRate_ / rateHz_);
    }

    void setSlewMs(float ms) noexcept {
        slewMs_ = std::clamp(ms, 0.0f, 500.0f);
        slewFilter_.setTimeConstant(slewMs_);
    }

    void setLFOSource(LFO* lfo) noexcept { lfoSource_ = lfo; }

    float process(float audioInput) noexcept {
        // Check for sample trigger
        sampleCounter_++;
        if (sampleCounter_ >= samplesPerTrigger_) {
            sampleCounter_ = 0;

            // Sample the source
            switch (source_) {
                case Source::Random:
                    heldValue_ = randomBipolar();  // [-1, +1]
                    break;
                case Source::LFO:
                    if (lfoSource_) {
                        heldValue_ = lfoSource_->getCurrentValue();
                    }
                    break;
                case Source::External:
                    heldValue_ = envFollower_.process(std::abs(audioInput));
                    break;
            }
        }

        // Apply slew (smoothing)
        return slewFilter_.process(heldValue_);
    }

private:
    Source source_ = Source::Random;
    double sampleRate_ = 44100.0;
    float rateHz_ = 4.0f;
    float slewMs_ = 0.0f;
    int samplesPerTrigger_ = 11025;
    int sampleCounter_ = 0;
    float heldValue_ = 0.0f;

    LFO* lfoSource_ = nullptr;
    EnvelopeFollower envFollower_;
    OnePoleSmoother slewFilter_;
};
```

### 10.3 Pitch Follower Source

The Pitch Follower converts detected fundamental frequency into a normalized modulation signal.

| Aspect | Specification |
|--------|---------------|
| **Mapping** | Logarithmic (semitone-based) |
| **Min/Max Hz** | Detection range AND mapping range |
| **Confidence Failure** | Hold last valid value |
| **Tracking Speed** | Output smoothing only (10 - 300 ms) |
| **Polyphonic Input** | Track dominant pitch; otherwise undefined |

#### Pitch-to-Modulation Formula

```cpp
// Convert frequency to MIDI note (float)
float freqToMidi(float freq) {
    return 69.0f + 12.0f * std::log2(freq / 440.0f);
}

// Map MIDI note to modulation value
float midiToMod(float midi, float minHz, float maxHz) {
    float minMidi = freqToMidi(minHz);
    float maxMidi = freqToMidi(maxHz);
    return std::clamp((midi - minMidi) / (maxMidi - minMidi), 0.0f, 1.0f);
}
```

#### Implementation

```cpp
/// @brief Pitch follower modulation source.
class PitchFollowerSource {
public:
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
        pitchDetector_.prepare(sampleRate);
        smoother_.prepare(sampleRate);
    }

    void setMinHz(float hz) noexcept { minHz_ = std::max(20.0f, hz); }
    void setMaxHz(float hz) noexcept { maxHz_ = std::min(5000.0f, hz); }
    void setConfidenceThreshold(float thresh) noexcept {
        confidenceThreshold_ = std::clamp(thresh, 0.0f, 1.0f);
    }
    void setTrackingSpeedMs(float ms) noexcept {
        smoother_.setTimeConstant(std::clamp(ms, 10.0f, 300.0f));
    }

    float process(float audioInput) noexcept {
        auto [freq, confidence] = pitchDetector_.process(audioInput);

        // Check validity
        bool valid = confidence >= confidenceThreshold_
                  && freq >= minHz_
                  && freq <= maxHz_;

        if (valid) {
            // Convert to modulation value
            float midi = 69.0f + 12.0f * std::log2(freq / 440.0f);
            float minMidi = 69.0f + 12.0f * std::log2(minHz_ / 440.0f);
            float maxMidi = 69.0f + 12.0f * std::log2(maxHz_ / 440.0f);

            lastValidValue_ = (midi - minMidi) / (maxMidi - minMidi);
            lastValidValue_ = std::clamp(lastValidValue_, 0.0f, 1.0f);
        }
        // If not valid, lastValidValue_ is held (no update)

        // Apply tracking smoothing
        return smoother_.process(lastValidValue_);
    }

private:
    double sampleRate_ = 44100.0;
    float minHz_ = 80.0f;
    float maxHz_ = 2000.0f;
    float confidenceThreshold_ = 0.5f;
    float lastValidValue_ = 0.5f;

    PitchDetector pitchDetector_;
    OnePoleSmoother smoother_;
};
```

### 10.4 Transient Detector Source

The Transient Detector generates an attack-decay envelope in response to rapid amplitude rises.

| Aspect | Specification |
|--------|---------------|
| **Algorithm** | Envelope derivative (Δenv > threshold) |
| **Output Shape** | Attack-Decay envelope |
| **Sensitivity** | Controls amplitude AND rate-of-change thresholds |
| **Attack** | Rise time to peak (0.5 - 10 ms) |
| **Decay** | Fall time to zero (20 - 200 ms) |
| **Retrigger** | From current envelope level |

#### Implementation

```cpp
/// @brief Transient detector modulation source.
class TransientDetector {
public:
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
        envFollower_.prepare(sampleRate, 5.0f);  // 5ms envelope

        // Pre-calculate coefficients
        updateCoefficients();
    }

    void setSensitivity(float sens) noexcept {
        sensitivity_ = std::clamp(sens, 0.0f, 1.0f);
        updateThresholds();
    }

    void setAttackMs(float ms) noexcept {
        attackMs_ = std::clamp(ms, 0.5f, 10.0f);
        updateCoefficients();
    }

    void setDecayMs(float ms) noexcept {
        decayMs_ = std::clamp(ms, 20.0f, 200.0f);
        updateCoefficients();
    }

    float process(float audioInput) noexcept {
        // Get envelope and its derivative
        float env = envFollower_.process(std::abs(audioInput));
        float delta = env - prevEnv_;
        prevEnv_ = env;

        // Detect transient: amplitude AND rate-of-change must exceed thresholds
        bool transientDetected = (env > amplitudeThreshold_)
                              && (delta > rateThreshold_);

        // Update envelope state
        if (transientDetected) {
            // Retrigger from current level (not zero)
            envelopeState_ = std::max(envelopeState_, 0.01f);
            attacking_ = true;
        }

        if (attacking_) {
            // Attack phase: rise toward 1.0
            envelopeState_ += attackIncrement_;
            if (envelopeState_ >= 1.0f) {
                envelopeState_ = 1.0f;
                attacking_ = false;
            }
        } else {
            // Decay phase: fall toward 0.0
            envelopeState_ *= decayCoeff_;
            if (envelopeState_ < 0.001f) {
                envelopeState_ = 0.0f;
            }
        }

        return envelopeState_;
    }

private:
    void updateThresholds() noexcept {
        // Higher sensitivity = lower thresholds
        amplitudeThreshold_ = 0.5f * (1.0f - sensitivity_);
        rateThreshold_ = 0.1f * (1.0f - sensitivity_);
    }

    void updateCoefficients() noexcept {
        // Attack: linear increment to reach 1.0 in attackMs_
        float attackSamples = (attackMs_ / 1000.0f) * static_cast<float>(sampleRate_);
        attackIncrement_ = 1.0f / std::max(1.0f, attackSamples);

        // Decay: exponential coefficient
        float decaySamples = (decayMs_ / 1000.0f) * static_cast<float>(sampleRate_);
        decayCoeff_ = std::exp(-1.0f / decaySamples);
    }

    double sampleRate_ = 44100.0;
    float sensitivity_ = 0.5f;
    float attackMs_ = 2.0f;
    float decayMs_ = 100.0f;

    float amplitudeThreshold_ = 0.25f;
    float rateThreshold_ = 0.05f;
    float attackIncrement_ = 0.01f;
    float decayCoeff_ = 0.9999f;

    float prevEnv_ = 0.0f;
    float envelopeState_ = 0.0f;
    bool attacking_ = false;

    EnvelopeFollower envFollower_;
};
```

---

## 11. Modulation Source Summary

| Source | Output Range | Key Parameters | Musical Use |
|--------|--------------|----------------|-------------|
| **Chaos** | [-1, +1] | Model, Speed, Coupling | Organic drift, evolving textures |
| **Sample & Hold** | [-1, +1] or [0, +1] | Source, Rate, Slew | Stepped modulation, random gates |
| **Pitch Follower** | [0, +1] | Min/Max Hz, Confidence, Tracking | Pitch-reactive effects |
| **Transient Detector** | [0, +1] | Sensitivity, Attack, Decay | Rhythm-following, ducking |

---

## 12. MIDI CC Integration

Disrumpo supports comprehensive MIDI CC mapping for hardware control integration.

### 12.1 MIDI-Learnable Parameters

**All exposed parameters** are MIDI-learnable. This includes:
- Global parameters (input/output gain, mix)
- Per-band parameters (gain, pan, morph X/Y)
- Per-node parameters (drive, mix, tone)
- Sweep parameters (frequency, width, intensity)
- Modulation routing amounts

### 12.2 CC Mapping Persistence

MIDI CC mappings use a **hybrid persistence model**:

| Level | Scope | Use Case |
|-------|-------|----------|
| **Global** | All presets | Consistent controller layout across sessions |
| **Per-Preset** | Single preset | Preset-specific performance mappings |

**Resolution order:** Per-preset mappings override global mappings for the same parameter.

### 12.3 14-bit CC Support

For high-resolution control, Disrumpo supports **14-bit MIDI CC** using standard CC pairs:

| MSB CC | LSB CC | Resolution | Use Case |
|--------|--------|------------|----------|
| 0-31 | 32-63 | 16384 steps | Critical continuous parameters |

14-bit mode is automatic when both MSB and LSB CCs are mapped to the same parameter.

### 12.4 MIDI Learn Workflow

```
1. Right-click any control → "MIDI Learn"
2. Control highlights (waiting for CC)
3. Move hardware controller
4. Mapping created and saved
5. Right-click → "Clear MIDI Learn" to remove
```

### 12.5 Implementation

```cpp
/// @brief MIDI CC mapping entry.
struct MidiCCMapping {
    uint8_t ccNumber = 0;           ///< CC number (0-127)
    uint32_t paramId = 0;           ///< Target parameter ID
    bool is14Bit = false;           ///< True if using CC pair for 14-bit
    bool isPerPreset = false;       ///< True if per-preset override
};

/// @brief MIDI CC mapping manager.
class MidiCCManager {
public:
    void setGlobalMapping(uint8_t cc, uint32_t paramId) noexcept;
    void setPresetMapping(uint8_t cc, uint32_t paramId) noexcept;
    void clearMapping(uint8_t cc) noexcept;

    /// @brief Process incoming CC and return parameter update.
    std::optional<std::pair<uint32_t, float>> processCC(uint8_t cc, uint8_t value) noexcept;

    /// @brief Handle 14-bit CC pair.
    void processCC14Bit(uint8_t ccMSB, uint8_t valueMSB,
                        uint8_t ccLSB, uint8_t valueLSB) noexcept;

private:
    std::array<MidiCCMapping, 128> globalMappings_;
    std::array<MidiCCMapping, 128> presetMappings_;
    std::array<uint8_t, 32> lsbBuffer_;  // For 14-bit assembly
};
```

---

## 13. Safety Limits & Error Handling

### 13.1 Feedback Limiting (D17 Feedback Distortion)

D17 uses **soft saturation** on the feedback path to prevent runaway:

```cpp
/// @brief Apply soft saturation to feedback amount.
/// Allows musical use while preventing self-oscillation.
float softLimitFeedback(float feedback, float drive) noexcept {
    // Tanh-based soft limiting
    // At feedback=1.0, effective feedback is ~0.76 (safe)
    // At feedback=1.5, effective feedback is ~0.91 (risky but musical)
    float x = feedback * drive;
    return std::tanh(x * 0.8f);
}
```

**Behavior:**
- Feedback 0.0 - 0.8: Near-linear, full control
- Feedback 0.8 - 1.0: Gentle compression begins
- Feedback 1.0+: Asymptotically approaches 1.0, never exceeds

### 13.2 Experimental Safety Toggle

A global **"Experimental Safety"** toggle protects users from extreme settings:

| State | Behavior |
|-------|----------|
| **ON (default)** | Limits extreme values, adds output protection |
| **OFF** | Full experimental range, user accepts risk |

**Protected parameters when Safety ON:**

| Parameter | Safe Limit | Reason |
|-----------|------------|--------|
| D17 Feedback | 0.95 max | Prevent self-oscillation |
| D20 Chaos Speed | 10.0 max | Prevent attractor divergence |
| D26 Allpass Resonance | 0.9 max | Prevent resonance explosion |
| All outputs | -12dBFS limiter | Protect monitoring chain |

### 13.3 NaN/Inf Protection

**Input sanitization** prevents NaN/Inf at source:

```cpp
/// @brief Sanitize input to prevent NaN/Inf propagation.
float sanitizeInput(float x) noexcept {
    // Fast NaN/Inf check using comparison
    // NaN fails all comparisons, Inf fails magnitude check
    if (!(x >= -1e10f && x <= 1e10f)) {
        return 0.0f;
    }
    return x;
}

/// @brief Apply to morph weights before processing.
void sanitizeMorphWeights(std::array<float, 4>& weights) noexcept {
    float sum = 0.0f;
    for (auto& w : weights) {
        w = sanitizeInput(w);
        w = std::max(0.0f, w);  // Weights must be non-negative
        sum += w;
    }
    // Renormalize if sum is valid
    if (sum > 0.0001f) {
        for (auto& w : weights) {
            w /= sum;
        }
    } else {
        // Fallback: equal weights
        for (auto& w : weights) {
            w = 0.25f;
        }
    }
}
```

### 13.4 DC Blocking Strategy

DC blocking is handled **per-implementation** based on unit test results:

1. Most distortion types already include DC blocking
2. Unit tests measure DC offset at output
3. Types showing >0.01 DC offset get explicit DC blocker added
4. Global output DC blocker as final safety net

```cpp
/// @brief Simple DC blocker (6dB/octave highpass at ~5Hz).
class DCBlocker {
public:
    void prepare(double sampleRate) noexcept {
        coefficient_ = 1.0f - (kPi * 2.0f * 5.0f / static_cast<float>(sampleRate));
    }

    float process(float input) noexcept {
        float output = input - lastInput_ + coefficient_ * lastOutput_;
        lastInput_ = input;
        lastOutput_ = output;
        return output;
    }

private:
    float coefficient_ = 0.9997f;
    float lastInput_ = 0.0f;
    float lastOutput_ = 0.0f;
};
```

---

## 14. Preset Design Guidelines

### 14.1 Naming Convention

Factory presets use **descriptive, evocative names** that convey the sonic character:

**Good examples:**
- "Warm Tape Crunch"
- "Aggressive Digital Bite"
- "Subtle Harmonic Glow"
- "Screaming Lead"
- "Dusty Vinyl"
- "Chaos Engine"

**Avoid:**
- Category prefixes: "SAT - Warm Tape" ❌
- Numbered prefixes: "001 Tape Crunch" ❌
- Technical jargon only: "Tanh 4x OS" ❌

### 14.2 Init Preset Specification

The Init preset provides a **bypass-equivalent** starting point:

| Parameter | Init Value | Rationale |
|-----------|------------|-----------|
| Band Count | 1 | Simplest configuration |
| Band 1 Type | SoftClip | Most transparent |
| Drive | 0.0 | No distortion |
| Mix | 100% | Full wet (but no effect) |
| Tone | 4000 Hz | Neutral |
| Input Gain | 0 dB | Unity |
| Output Gain | 0 dB | Unity |
| Morph X/Y | 0.5, 0.5 | Center |
| Sweep | Off | Disabled |
| Modulation | None | No routings |

### 14.3 Preset Metadata

Each factory preset includes full metadata:

```cpp
/// @brief Preset metadata structure.
struct PresetMetadata {
    std::string name;           ///< Display name (required)
    std::string author;         ///< Creator name (required)
    std::vector<std::string> tags;  ///< Category tags (required)
    std::string description;    ///< Detailed description (optional)
    uint64_t createTimestamp;   ///< Unix timestamp (automatic)
    uint32_t pluginVersion;     ///< Version that created it (automatic)
};
```

**Required tags (at least one):**
- `Warm`, `Aggressive`, `Subtle`, `Experimental`
- `Bass`, `Lead`, `Pad`, `Drums`, `Vocal`, `Master`
- `Clean`, `Dirty`, `Vintage`, `Modern`, `Lo-Fi`

---

## 15. Accessibility

### 15.1 Screen Reader Support

Disrumpo defers to **VSTGUI's accessibility layer** for screen reader support:
- Standard VSTGUI controls provide accessible names automatically
- Custom controls implement `IAccessible` interface via VSTGUI patterns
- No custom announcement format required

### 15.2 High Contrast Mode

The plugin **respects OS high-contrast settings**:

```cpp
/// @brief Check if high contrast mode is active.
bool isHighContrastEnabled() noexcept {
#if defined(_WIN32)
    HIGHCONTRAST hc = { sizeof(HIGHCONTRAST) };
    SystemParametersInfo(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0);
    return (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
#elif defined(__APPLE__)
    // macOS accessibility API
    return [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldIncreaseContrast];
#else
    // Linux: Check GTK/Qt settings if available
    return false;
#endif
}
```

When high contrast is detected:
- Switch to high-contrast color scheme
- Increase control border widths
- Use solid fills instead of gradients

### 15.3 Reduced Motion Mode

The plugin **respects OS reduced motion preferences**:

```cpp
/// @brief Check if reduced motion is preferred.
bool isReducedMotionPreferred() noexcept {
#if defined(_WIN32)
    BOOL animationsEnabled = TRUE;
    SystemParametersInfo(SPI_GETCLIENTAREAANIMATION, 0, &animationsEnabled, 0);
    return !animationsEnabled;
#elif defined(__APPLE__)
    return [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceMotion];
#else
    return false;
#endif
}
```

When reduced motion is detected:
- Disable morph pad animation trails
- Disable sweep visualization animation
- Disable metering smoothing animation
- Use instant transitions instead of fades

---

## 16. Automation Smoothing

### 16.1 Smoothing Behavior

**All parameters** use smoothing to prevent zipper noise and artifacts:

| Parameter Type | Smoothing Time | Rationale |
|----------------|----------------|-----------|
| Continuous (gain, mix, drive) | 5-10 ms | Prevent zipper noise |
| Discrete (type, mode) | 5 ms crossfade | Prevent clicks |
| Position (morph X/Y) | 7 ms | Match transition zone fade |

### 16.2 Implementation

```cpp
/// @brief Parameter smoother with configurable time constant.
class ParameterSmoother {
public:
    void prepare(double sampleRate, float timeMs = 7.0f) noexcept {
        coefficient_ = std::exp(-1.0f / (static_cast<float>(sampleRate) * timeMs * 0.001f));
    }

    void setTarget(float target) noexcept {
        target_ = target;
    }

    float process() noexcept {
        current_ = current_ * coefficient_ + target_ * (1.0f - coefficient_);
        return current_;
    }

    bool isSettled() const noexcept {
        return std::abs(current_ - target_) < 0.0001f;
    }

private:
    float target_ = 0.0f;
    float current_ = 0.0f;
    float coefficient_ = 0.999f;
};
```

### 16.3 VST3 Automation Handling

VST3 delivers automation at **per-sample resolution** via `IParameterChanges`. The plugin:
1. Receives per-sample parameter changes from host
2. Applies smoothing to each change
3. Uses smoothed values for processing

No additional sample-accuracy logic is needed beyond the standard VST3 pattern.

---

## 17. State Version Migration

### 17.1 Version Strategy

Preset format includes a version number for forward compatibility:

```cpp
constexpr int32 kPresetVersion = 1;  // Increment when format changes

// In setState():
int32 version;
streamer.readInt32(version);

if (version > kPresetVersion) {
    // Future version: load what we understand
    // Unknown parameters will be skipped
}

if (version < kPresetVersion) {
    // Older version: apply defaults for missing parameters
    applyVersionDefaults(version);
}
```

### 17.2 Default Value Strategy

When loading older presets, new parameters receive **sensible defaults**:

```cpp
void applyVersionDefaults(int32 loadedVersion) noexcept {
    // Parameters added in version 2
    if (loadedVersion < 2) {
        experimentalSafetyEnabled_ = true;  // Safe default
        // ... other v2 parameters
    }

    // Parameters added in version 3
    if (loadedVersion < 3) {
        // ... v3 parameters with defaults that preserve original sound
    }
}
```

**Default selection criteria:**
1. Preserve original preset sound as closely as possible
2. Choose the safest option when sound-neutral
3. Document each default in changelog

### 17.3 Forward Compatibility

When loading presets from **newer plugin versions**:

1. Read all parameters up to our known format
2. Ignore unknown parameters silently (no error)
3. Preset loads with known parameters intact
4. No warning shown to user

This allows presets to be shared between users with different plugin versions without friction.
