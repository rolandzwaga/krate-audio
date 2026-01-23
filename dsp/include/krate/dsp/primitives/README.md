# Layer 1: DSP Primitives

This folder contains single-purpose DSP building blocks that form the foundation for higher-level processors. **Layer 1 depends only on Layer 0 (core)** and the C++ standard library.

All primitives are designed for real-time safety with `noexcept` processing methods and no allocations after `prepare()`.

## Files

### Delay & Time-Domain

| File | Purpose |
|------|---------|
| `crossfading_delay_line.h` | Delay line with crossfade support for smooth delay time changes without pitch artifacts. |
| `reverse_buffer.h` | Circular buffer optimized for reverse playback in reverse delay effects. |
| `rolling_capture_buffer.h` | Continuously capturing buffer for freeze and looping effects. |
| `comb_filter.h` | Comb filter for flanging, resonance, and Karplus-Strong synthesis. |

### Filters

| File | Purpose |
|------|---------|
| `biquad.h` | Transposed Direct Form II biquad filter supporting LP/HP/BP/Notch/Allpass/Shelf/Peak types with cascading for steeper slopes. |
| `one_pole.h` | Simple one-pole lowpass/highpass filter for gentle filtering and DC blocking. |
| `svf.h` | State Variable Filter with simultaneous LP/HP/BP/Notch outputs and stable modulation. |
| `allpass_1pole.h` | One-pole allpass filter for phase shifting in phasers and diffusion networks. |
| `ladder_filter.h` | Moog-style ladder filter with resonance and self-oscillation capability. |
| `dc_blocker.h` | High-pass filter tuned to remove DC offset without affecting audible frequencies. |

### Modulation

| File | Purpose |
|------|---------|
| `lfo.h` | Wavetable-based Low Frequency Oscillator with sine, triangle, saw, square, S&H, and smooth random waveforms. Supports tempo sync. |
| `smoother.h` | Parameter smoothing primitives: `OnePoleSmoother` (exponential), `LinearRamp`, and `SlewLimiter` for click-free automation. |

### Spectral Processing

| File | Purpose |
|------|---------|
| `fft.h` | Fast Fourier Transform implementation for spectral analysis and processing. |
| `stft.h` | Short-Time Fourier Transform with overlap-add for time-frequency processing. |
| `spectral_buffer.h` | Circular buffer for managing spectral frames in frequency-domain effects. |

### Saturation & Waveshaping

| File | Purpose |
|------|---------|
| `waveshaper.h` | General-purpose waveshaper with configurable transfer functions. |
| `wavefolder.h` | Wavefolding primitive for creating rich harmonic content from simple waveforms. |
| `hard_clip_adaa.h` | Hard clipper with Anti-Derivative Anti-Aliasing (ADAA) for clean digital clipping. |
| `tanh_adaa.h` | Soft saturation using tanh with ADAA for warm, analog-style overdrive. |
| `chebyshev_shaper.h` | Chebyshev polynomial waveshaper for adding specific harmonics. |

### Sample Rate & Bit Depth

| File | Purpose |
|------|---------|
| `oversampler.h` | Polyphase oversampling for alias-free nonlinear processing with configurable quality. |
| `sample_rate_reducer.h` | Sample-and-hold style rate reduction for lo-fi and vintage digital effects. |
| `sample_rate_converter.h` | High-quality sample rate conversion for format compatibility. |
| `bit_crusher.h` | Bit depth reduction with optional dithering for lo-fi character. |

### Granular

| File | Purpose |
|------|---------|
| `grain_pool.h` | Object pool managing grain instances for granular synthesis, avoiding real-time allocation. |
| `slice_pool.h` | Pool for managing audio slices in slice-based granular processing. |

### Analysis

| File | Purpose |
|------|---------|
| `pitch_detector.h` | Pitch detection using autocorrelation or other algorithms for pitch-tracking effects. |

### Interfaces

| File | Purpose |
|------|---------|
| `i_feedback_processor.h` | Interface for processors that can be inserted into feedback paths (used by feedback networks). |

## Usage

Include files using the `<krate/dsp/primitives/...>` path:

```cpp
#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/lfo.h>
#include <krate/dsp/primitives/smoother.h>
```

## Design Principles

- **Single responsibility** - each primitive does one thing well
- **Composable** - designed to be combined in Layer 2 processors
- **prepare()/reset()/process()** lifecycle pattern
- **No allocations** in process methods after prepare()
- **Noexcept processing** for real-time guarantees
