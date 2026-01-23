# Layer 2: DSP Processors

This folder contains mid-level processors that compose Layer 1 primitives into more complex DSP algorithms. **Layer 2 depends on Layers 0-1** (core and primitives).

Processors handle specific audio processing tasks like filtering, saturation, pitch shifting, and spectral manipulation.

## Files

### Filters

| File | Purpose |
|------|---------|
| `multimode_filter.h` | Complete filter module with 8 types (LP/HP/BP/Notch/Allpass/Shelf/Peak), selectable slopes (12-48 dB/oct), smoothing, and optional pre-filter drive with oversampling. |
| `crossover_filter.h` | Multi-band crossover filter for splitting audio into frequency bands (used in multiband processing). |
| `formant_filter.h` | Formant filter bank for vocal-like resonances and vowel sounds. |
| `envelope_filter.h` | Auto-wah/envelope filter that modulates cutoff based on input amplitude. |
| `phaser.h` | Classic phaser effect using cascaded allpass stages with LFO modulation. |

### Saturation & Distortion

| File | Purpose |
|------|---------|
| `saturation_processor.h` | Unified saturation processor with multiple character modes (tube, tape, transistor). |
| `tube_stage.h` | Vacuum tube saturation emulation with triode and pentode characteristics. |
| `tape_saturator.h` | Tape machine saturation with hysteresis and frequency-dependent behavior. |
| `diode_clipper.h` | Diode clipping circuit emulation for aggressive distortion. |
| `wavefolder_processor.h` | Wavefolding processor with gain staging and multiple folding algorithms. |
| `fuzz_processor.h` | Fuzz effect processor emulating classic fuzz pedal circuits. |
| `bitcrusher_processor.h` | Combined bit depth and sample rate reduction with aliasing character. |

### Dynamics

| File | Purpose |
|------|---------|
| `dynamics_processor.h` | General dynamics processing: compression, limiting, expansion, gating. |
| `ducking_processor.h` | Sidechain ducking/pumping effect for rhythmic volume modulation. |
| `envelope_follower.h` | Amplitude envelope extraction for control signals and analysis. |

### Spatial & Stereo

| File | Purpose |
|------|---------|
| `diffusion_network.h` | Allpass diffusion network for reverb and delay smearing effects. |
| `midside_processor.h` | Mid/side encoding, processing, and decoding for stereo width manipulation. |

### Spectral

| File | Purpose |
|------|---------|
| `spectral_gate.h` | Frequency-selective gating that removes spectral content below a threshold. |
| `spectral_tilt.h` | Spectral tilt filter for adjusting the overall brightness/darkness balance. |
| `spectral_morph_filter.h` | Morphing between spectral characteristics of different filter types. |

### Pitch & Time

| File | Purpose |
|------|---------|
| `pitch_shift_processor.h` | Real-time pitch shifting with formant preservation options. |
| `grain_processor.h` | Single grain processor for granular synthesis engines. |
| `grain_scheduler.h` | Grain timing and density management for granular effects. |
| `pattern_scheduler.h` | Pattern-based event scheduling for rhythmic effects. |

### Synthesis

| File | Purpose |
|------|---------|
| `noise_generator.h` | Multi-type noise generation: white, pink, brown, tape hiss, vinyl crackle. |
| `resonator_bank.h` | Bank of tuned resonators for body resonance and modal synthesis. |
| `karplus_strong.h` | Karplus-Strong string synthesis algorithm. |
| `waveguide_resonator.h` | Digital waveguide resonator for physical modeling synthesis. |
| `modal_resonator.h` | Modal synthesis using 32 decaying sinusoidal oscillators for bells, bars, and plates. |

### Feedback

| File | Purpose |
|------|---------|
| `reverse_feedback_processor.h` | Feedback processor that reverses audio in the feedback path. |

## Usage

Include files using the `<krate/dsp/processors/...>` path:

```cpp
#include <krate/dsp/processors/multimode_filter.h>
#include <krate/dsp/processors/saturation_processor.h>
#include <krate/dsp/processors/pitch_shift_processor.h>
```

## Design Principles

- **Composition over inheritance** - processors compose primitives
- **Parameter smoothing** built-in for click-free modulation
- **Stereo-aware** - most processors support mono and stereo
- **prepare()/reset()/process()** lifecycle pattern
- **Noexcept processing** for real-time guarantees
