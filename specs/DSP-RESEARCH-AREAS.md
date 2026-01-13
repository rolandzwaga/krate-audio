# Krate Audio Library - DSP Categories Roadmap

## Completed Research

- ✅ **Delay** - Delay lines, feedback, modulated delays
- ✅ **Distortion** - Waveshaping, saturation, clipping algorithms
- ✅ **Filters** - IIR/FIR, biquads, SVF, ladder, crossovers, formants

---

## High Priority (Core Building Blocks)

### 1. Oscillators & Waveform Generation
- Anti-aliased oscillators (PolyBLEP, BLIT, minBLEP, DPW)
- Wavetable synthesis and interpolation
- FM/PM synthesis
- Noise generators (white, pink, brown, velvet)
- Sub-oscillators and unison/detune

### 2. Dynamics Processing
- Compressors (feedforward, feedback, VCA, opto, FET modeling)
- Limiters (brickwall, soft-knee, lookahead)
- Gates and expanders
- Transient shapers
- Envelope followers and sidechain techniques

### 3. Modulation Effects
- Chorus (including ensemble/dimension-style)
- Phaser (analog pole/zero modeling)
- Tremolo and auto-pan
- Ring modulation and AM
- Vibrato

### 4. Reverb & Spatial
- Algorithmic reverbs (Schroeder, Freeverb, Dattorro, plate, hall)
- Early reflections modeling
- Convolution fundamentals
- Stereo widening (M/S, Haas, correlation-based)
- 3D/binaural panning

---

## Medium Priority (Extended Functionality)

### 5. Envelope Generators & LFOs
- ADSR with various curve types (linear, exponential, analog RC)
- Multi-stage envelopes
- LFO shapes and sync modes
- Smoothing and slew limiting

### 6. Pitch & Time
- Pitch shifting (granular, phase vocoder)
- Time stretching
- Pitch detection (autocorrelation, YIN, MPM)
- Formant preservation

### 7. Spectral Processing
- FFT/IFFT fundamentals
- Spectral freeze, smear, blur
- Vocoder
- Spectral filtering and morphing

---

## Lower Priority (Utilities & Analysis)

### 8. Sample Rate Conversion
- Interpolation methods (linear, cubic, sinc)
- Oversampling/downsampling
- Fractional delay lines

### 9. Metering & Analysis
- Peak, RMS, LUFS measurement
- Spectrum analysis
- Phase correlation

### 10. Utility DSP
- Dithering algorithms
- Gain staging
- Crossfading strategies
- Buffer management

---

## Suggested Research Order

1. **Oscillators** - Foundation for synthesizer work
2. **Dynamics** - Essential for mixing/mastering tools
3. **Modulation Effects** - Builds on delay and filter knowledge
4. **Reverb & Spatial** - Complex but highly valuable
5. **Envelopes & LFOs** - Control signal generation
6. **Pitch & Time** - More advanced DSP territory
7. **Spectral** - FFT-based processing
8. **Utilities** - Fill in as needed

---
