# Harmonizer Effect: DSP Research & Gap Analysis

> **Date**: 2026-02-15
> **Scope**: How harmonizers work at the DSP level, algorithm trade-offs, and missing components in KrateDSP

---

## Table of Contents

1. [What Is a Harmonizer?](#1-what-is-a-harmonizer)
2. [Core Pitch-Shifting Algorithms](#2-core-pitch-shifting-algorithms) *(incl. per-algorithm SIMD analysis)*
3. [Critical Challenges](#3-critical-challenges) *(incl. formant SIMD analysis)*
4. [Harmonizer-Specific Logic](#4-harmonizer-specific-logic) *(incl. multi-voice SIMD analysis)*
5. [State of the Art](#5-state-of-the-art)
6. [Algorithm Comparison](#6-algorithm-comparison) *(incl. SIMD benefit column)*
7. [Existing KrateDSP Components](#7-existing-kratedsp-components)
8. [Gap Analysis: Missing Components](#8-gap-analysis-missing-components)
9. [Key Academic References](#9-key-academic-references)
10. [SIMD Optimization Priority Matrix](#10-simd-optimization-priority-matrix)

---

## 1. What Is a Harmonizer?

A **harmonizer** generates one or more pitch-shifted copies of an input signal, blending them with the original to create musical harmonies. The term was coined by Eventide Audio (originally a trademark).

At the DSP level, a harmonizer is a **pitch shifter** augmented with **musical intelligence**: the ability to select pitch-shift intervals that are diatonically correct with respect to a key, scale, or harmonic context. A simple pitch shifter applies a fixed chromatic interval (e.g., always +4 semitones); a harmonizer applies a *variable* interval that changes depending on the input note to maintain scale-correctness.

### Harmonizer vs Pitch Shifter vs Chorus

| Effect | Core Mechanism | Interval | Typical Use |
|--------|---------------|----------|-------------|
| **Pitch Shifter** | Shifts pitch by a fixed chromatic interval | Constant (e.g., +5 semitones always) | Octave effects, fixed transposition |
| **Harmonizer** | Shifts pitch by a musically intelligent, variable interval | Variable (e.g., "3rd above" = +3 or +4 semitones depending on scale degree) | Vocal harmonies, guitar harmony leads |
| **Chorus** | Modulates delay time with an LFO, creating slight pitch variation | Very small, oscillating (+/- a few cents) | Thickening, widening |

### Historical Context

- **Eventide H910 (1975)**: First commercial digital pitch changer. Hybrid analog/digital using variable sample-rate conversion (D/A runs at different rate than A/D). 11-bit, +/- 1 octave. Introduced the word "glitch" into audio engineering -- the audible artifact at splice discontinuities.
- **Eventide H949 (1977)**: Introduced "de-glitching" via autocorrelation to find phase-aligned splice points between two read taps, eliminating the worst crossfade artifacts.
- **Eventide H3000 Ultra-Harmonizer (1988)**: Fully programmable, 11 algorithms, true diatonic pitch shifting. Its micropitch detuning quality is widely considered unmatched, attributed to phase-coherent pitch shifting.
- **TC-Helicon VoiceWorks (2000s)**: Advanced vocal harmony with multiple modes -- Scalic (preset key/scale), Chordal (chord recognition), Shift (fixed chromatic), and MIDI Notes (direct MIDI control). Used "Hybrid Shift" combining multiple pitch-shifting technologies.

**Sources**:
- [Valhalla DSP -- Early pitch shifting: The Eventide H910](https://valhalladsp.com/2010/05/07/early-pitch-shifting-the-eventide-h910-harmonizer/)
- [Valhalla DSP -- Pitch Shifting: The H949 and "de-glitching"](https://valhalladsp.com/2010/05/07/pitch-shifting-the-h949-and-de-glitching/)
- [Reverb -- The Tech Behind Eventide H3000](https://reverb.com/news/tech-behind-eventide-h3000-ultra-harmonizer)
- [Sound On Sound -- TC-Helicon Voice Works](https://www.soundonsound.com/reviews/tc-helicon-voice-works)

---

## 2. Core Pitch-Shifting Algorithms

### 2.1 Time-Domain: Delay-Line Crossfade (Simplest)

**Principle**: Write audio into a circular buffer at a fixed rate; read from it at a different rate. If the read pointer moves faster than the write pointer, pitch goes up; slower, pitch goes down.

**The fundamental problem**: The read pointer will eventually collide with or pass the write pointer, causing discontinuities. The solution is **two read pointers** with crossfading:

1. Two read pointers operate 180 degrees out of phase in their offset cycles
2. As one pointer approaches the write pointer (offset approaching zero), its gain fades out
3. Simultaneously, the other pointer resets to maximum offset and fades in
4. A window function (Hann, raised cosine) shapes the crossfade

**Mathematics**: For a pitch shift ratio `r`:
- Read pointer increment per sample = `r` (fractional values require interpolation)
- The delay line buffer acts as a circular buffer of length N
- Crossfade region duration trades off between comb filtering (too long) and glitching (too short)

**Characteristics**:
- Latency: < 10ms
- Quality: Metallic/flanging coloration, "stuttering" at splice points. Best for small shifts (+/- a few semitones)
- CPU: Extremely low -- two interpolated delay reads and a crossfade per sample
- Polyphonic: Yes (no pitch detection required)

This is the technique used in the original Eventide H910 and the basis for many guitar pedal pitch shifters.

**SIMD Optimization Potential: LOW**

A single delay-line read is inherently serial -- each sample reads from a different buffer position, so there is no data parallelism within one channel. The crossfade math (`output = a * env_a + b * env_b`) is trivially vectorizable over a block with `_mm_mul_ps` / `_mm_add_ps` (4 samples at a time), but it is such a small fraction of total work that the gain is negligible. Where SIMD becomes relevant is **multi-tap or multi-voice** scenarios: if 4 delay taps (or 4 harmony voices) each need an interpolated read, their interpolation math can be interleaved in SSE registers for ~3-4x throughput on the arithmetic. However, each tap reads from a *different* buffer position, requiring gather operations -- SSE lacks efficient gather, and even AVX2's `_mm256_i32gather_ps` costs ~12-20 cycles. **Bottom line**: Not worth hand-vectorizing for a single pitch shifter; becomes worthwhile at 4+ parallel voices. [Source: KVR SIMD Delay Discussion](https://www.kvraudio.com/forum/viewtopic.php?t=285500)

**Sources**:
- [katjaas.nl -- Pitch Shifting](https://www.katjaas.nl/pitchshift/pitchshift.html)
- [katjaas.nl -- Low Latency Pitch Shifting](https://www.katjaas.nl/pitchshiftlowlatency/pitchshiftlowlatency.html)
- [MATLAB -- Delay-Based Pitch Shifter](https://www.mathworks.com/help/audio/ug/delay-based-pitch-shifter.html)

### 2.2 Time-Domain: TD-PSOLA (Pitch-Synchronous Overlap-Add)

**Principle**: Uses pitch detection to identify the fundamental frequency, segments the signal into overlapping grains aligned with pitch periods, then repositions grains to change pitch:
- **Pitch up**: Place grains closer together (reduce inter-grain spacing)
- **Pitch down**: Place grains further apart (increase spacing)

**Algorithm**:
1. Pitch detection (YIN or autocorrelation) to find fundamental period T0
2. Pitch mark placement at each period boundary
3. Window each grain with a Hann window spanning 2-4 pitch periods
4. Overlap-add synthesis with modified spacing: output period T1 = T0 / pitchShiftRatio

**Key insight for formant preservation**: The *rate* of grain emission determines the output pitch, while the *playback speed within each grain* determines the formant shift. To shift pitch while preserving formants, change the grain rate but keep each grain's internal playback speed at 1x.

**Characteristics**:
- Latency: 5-20ms (1-4 pitch periods)
- Quality: Excellent for monophonic signals (especially voice)
- CPU: Low
- Polyphonic: No -- requires accurate pitch detection, fails on polyphonic signals

**SIMD Optimization Potential: LOW-MODERATE**

The per-grain windowing (multiply samples by Hann window) and overlap-add accumulation (`output[i] += grain[i]`) are textbook SIMD -- 4x speedup with `_mm_mul_ps` and `_mm_add_ps` over contiguous blocks. Using FMA, both can be fused: `_mm_fmadd_ps(grain, window, output)`. However, the pitch detection step (autocorrelation) dominates CPU cost, and it benefits more from an algorithmic change (FFT-based autocorrelation, O(N log N)) than from SIMD on the naive O(N^2) approach. The grain repositioning logic itself is control flow, not data parallelism, so SIMD doesn't apply there.

**Sources**:
- [Wikipedia -- PSOLA](https://en.wikipedia.org/wiki/PSOLA)
- [KVR Audio Forum -- The intricacies of PSOLA pitch shifting](https://www.kvraudio.com/forum/viewtopic.php?t=511549)

### 2.3 Time-Domain: WSOLA (Waveform Similarity Overlap-Add)

**Principle**: Like OLA but does NOT require pitch detection. Keeps grain size fixed and uses **cross-correlation** to find the optimal overlap position, maximizing waveform similarity at splice points.

**Algorithm**:
1. Extract overlapping grains at regular intervals (fixed grain size, 20-50ms)
2. For each synthesis grain, search within a tolerance window for the position that maximizes cross-correlation with the previous grain's tail
3. Overlap-add at the best-matched position

**Characteristics**:
- Latency: ~100ms (SoundTouch)
- Quality: No pitch detection needed; works on polyphonic material. Downward shifts can lose short attacks; upward shifts can produce audible repetitions
- CPU: Low (~1% overhead per SoundTouch)

**SIMD Optimization Potential: MODERATE**

The cross-correlation search is the most expensive step and benefits significantly from SIMD. A dot product over a search window of L samples: `r[tau] = sum(x[n] * y[n+tau])` maps directly to `_mm_mul_ps` + horizontal addition, giving ~3-4x speedup on the inner loop. SoundTouch's implementation uses integer SIMD (MMX/SSE2) for its correlation search. The overlap-add step is identical to PSOLA and trivially vectorizable.

**Sources**:
- [SoundTouch README](https://www.surina.net/soundtouch/README.html)
- Verhelst & Roelands (1993), "An overlap-add technique based on waveform similarity (WSOLA)", ICASSP

### 2.4 Frequency-Domain: Phase Vocoder (STFT-Based)

The most widely used pitch-shifting algorithm for production-quality applications. Introduced by Flanagan & Golden (1966) for speech analysis-synthesis, adapted for music by Moorer (1978) at Stanford CCRMA.

**Architecture**: Analysis (STFT) --> Modification --> Synthesis (ISTFT with overlap-add)

**Detailed algorithm**:

**Step 1 -- Analysis (STFT)**:
- Window the input signal with a window function (Hann) of size N
- Apply FFT to each windowed frame
- Advance by hop size Ha (analysis hop), typically N/4 for 75% overlap
- Extract magnitude |X[k]| and phase angle(X[k]) for each bin k

**Step 2 -- Instantaneous Frequency Estimation**:
For each bin k, estimate the true frequency from phase differences between successive frames:
```
phase_diff[k] = phase_current[k] - phase_previous[k]
expected_diff = 2 * pi * k * Ha / N
deviation = phase_diff[k] - expected_diff
deviation_wrapped = wrap_to_pi(deviation)
true_freq[k] = (2 * pi * k / N) + (deviation_wrapped / Ha)
```

**Step 3 -- Pitch Shifting via Direct Frequency-Domain Scaling** (Laroche & Dolson method):
For each analysis bin k, compute target bin j = k * pitchShiftRatio:
```
synth_magnitude[j] = analysis_magnitude[k]
synth_frequency[j] = analysis_frequency[k] * pitchShiftRatio
synth_phase[j] = synth_phase_prev[j] + synth_frequency[j] * Hs
```
This enables non-linear frequency modifications like harmonizing (different pitch ratios for different frequency bands).

**Step 4 -- Synthesis (ISTFT)**:
- Construct complex spectrum from magnitude and phase
- Inverse FFT, window, overlap-add

**The Phasiness Problem**: The standard phase vocoder preserves **horizontal phase coherence** (continuity across time for each bin) but destroys **vertical phase coherence** (relationships between adjacent bins within a frame). This produces a reverberant, smeared quality.

**Characteristics**:
- Latency: N samples minimum. N=2048 at 44.1kHz = 46ms; N=4096 = 93ms
- Quality: Good with phase locking; phasey without it
- CPU: Moderate (dominated by FFT/IFFT, O(N log N) per frame)
- Polyphonic: Yes

**SIMD Optimization Potential: VERY HIGH -- This is where SIMD matters most**

The phase vocoder pipeline has ~10 distinct stages. Here is a per-step analysis of SIMD applicability:

| Step | Operation | SIMD Potential | Est. Speedup | % of Total CPU |
|------|-----------|---------------|--------------|----------------|
| FFT/IFFT | Butterfly operations | **Already SIMD** (pffft) | ~4x (done) | 30-40% |
| Windowing | `input[i] * window[i]` | **Trivial** (`_mm_mul_ps`) | ~4x | ~5% |
| Cart-to-Polar | `sqrt(re^2+im^2)`, `atan2(im,re)` | **HIGH** (vectorized transcendentals) | 10-50x | 15-25% |
| Phase diff/unwrap | `delta = curr - prev`, wrap to [-pi,pi] | **Easy** (`_mm_sub_ps` + `_mm_round_ps`) | ~4x | 3-5% |
| Freq estimation | Element-wise FMA | **Trivial** (`_mm_fmadd_ps`) | ~4x | ~2% |
| Bin shifting | Scatter write to different dest bins | **LOW** (scatter-limited) | 1.5-2x | ~5% |
| Phase accumulation | `phase[k] += freq[k] * hop` | **Trivial** (FMA) | ~4x | ~2% |
| Polar-to-Cart | `mag * cos(phase)`, `mag * sin(phase)` | **HIGH** (vectorized sincos) | 3-8x | 10-15% |

**Cartesian-to-Polar (the biggest win after FFT)**: `std::atan2` costs ~50-100 cycles per scalar call. Vectorized alternatives process 4 values simultaneously:
- **Pommier `atan2_ps`** (SSE2): Polynomial approximation, max error 2.38e-7 vs `atan2f`. Used by Rubber Band via bqvec's `VectorOpsComplex.h`. [Source](https://github.com/to-miz/sse_mathfun_extension)
- **Mazzo vectorized atan2**: 50x speedup over scalar, 2.04 cycles per element, 6-term Remez polynomial. [Source](https://mazzo.li/posts/vectorized-atan2.html)
- For magnitude, `_mm_sqrt_ps` (~11-14 cycles for 4 floats) or `_mm_rsqrt_ps` + Newton-Raphson (~7-8 cycles, ~22-bit accuracy).

**Polar-to-Cartesian**: Vectorized `sincos_ps` computes both sin and cos simultaneously for 4 floats at barely more cost than one. Pommier's `sse_mathfun` provides this. [Source](https://github.com/RJVB/sse_mathfun)

**Phase wrapping** (4 instructions for 4 bins with SSE4.1):
```
delta = _mm_sub_ps(current, previous);
wrapped = _mm_sub_ps(delta, _mm_mul_ps(twopi, _mm_round_ps(_mm_mul_ps(delta, inv_twopi), _MM_FROUND_TO_NEAREST_INT)));
```

**Bin shifting** is the one step that resists SIMD: it's essentially a scatter operation where different source bins map to different destination bins. AVX-512 has `_mm512_i32scatter_ps` but it's not widely available. On SSE/AVX, each lane must be extracted and written individually.

**Real-world SIMD usage in production pitch shifters**:
- **Rubber Band**: Uses Intel IPP (`ippsCartToPolar_32f`), Apple vDSP (`vvatan2f`), or Pommier SSE math as fallback for Cart/Polar conversion. [Source](https://github.com/breakfastquay/rubberband)
- **pffft**: Already delivers ~4x for FFT/IFFT via SSE radix-4 butterflies.
- **KFR library**: Provides vectorized `carg` (phase) and `cabs` (magnitude). [Source](https://github.com/kfrlib/kfr)

**Sources**:
- Flanagan & Golden (1966), "Phase Vocoder", Bell System Technical Journal
- Moorer (1978), "The Use of the Phase Vocoder in Computer Music Applications", JAES
- [Bernsee -- Pitch Shifting Using The Fourier Transform](http://blogs.zynaptiq.com/bernsee/pitch-shifting-using-the-ft/)

### 2.5 Phase Locking (Laroche & Dolson, 1999)

The landmark papers "Improved Phase Vocoder Time-Scale Modification of Audio" and "New Phase-Vocoder Techniques for Pitch-Shifting, Harmonizing and Other Exotic Effects" introduced two critical improvements:

**Identity (Rigid) Phase Locking**:
1. **Peak detection**: Find local maxima in the magnitude spectrum
2. **Region of influence**: Assign each non-peak bin to its nearest peak
3. **Phase propagation for peaks only**: Apply standard horizontal phase propagation to peak bins
4. **Phase locking for regions**: All bins in a region maintain their original phase difference relative to the region's peak:
   ```
   phi_out[k] = phi_out[peak] + (phi_in[k] - phi_in[peak])
   ```

This preserves vertical phase coherence within each harmonic's spectral lobe, dramatically reducing phasiness.

**SIMD Optimization Potential: MODERATE**

Peak detection (3-point local maximum comparison) vectorizes well -- compare 4 bins simultaneously using `_mm_cmpgt_ps` and `_mm_and_ps`, then extract the peak mask with `_mm_movemask_ps` (~2-3x speedup). However, the subsequent region assignment and phase propagation are harder to vectorize because each peak's region has a variable width. The phase-locking step itself (`phi_out[k] = phi_out[peak] + (phi_in[k] - phi_in[peak])`) is a per-region broadcast + subtract + add pattern -- vectorizable within each region but regions have different sizes, causing SIMD lane waste at boundaries.

An important insight from Laroche & Dolson: with phase locking, sin/cos calls are only needed for **peak bins** (typically 20-100 peaks), not all N/2+1 bins. This dramatically reduces the total transcendental function cost, somewhat diminishing the relative SIMD benefit for the polar-to-cartesian step compared to a basic phase vocoder.

**Sources**:
- Laroche & Dolson (1999), "New phase-vocoder techniques for pitch-shifting", IEEE WASPAA
- Laroche & Dolson (1999), "Improved phase vocoder time-scale modification", IEEE Trans. Speech Audio

### 2.6 PVSOLA (Phase Vocoder with Synchronized Overlap-Add)

A hybrid combining spectral analysis with time-domain overlap-add synthesis:
1. Perform STFT analysis
2. For synthesis, use cross-correlation in the time domain to find optimal overlap positions
3. Reset phase at these positions, preserving original vertical phase structure

**Key advantage**: Transients are preserved naturally because cross-correlation-based overlap positioning tends to align with transient boundaries.

**Sources**:
- Kraft et al. (2011), "PVSOLA: A Phase Vocoder with Synchronized OverLap-Add", DAFx-11

### 2.7 Phase Vocoder Done Right (Prusa & Holighaus, 2022)

A novel phase correction method based on phase gradient estimation and integration:
- Does NOT require explicit peak picking or tracking
- Does NOT require transient detection or special treatment
- Eliminates typical phase vocoder artifacts even for extreme stretching
- Greatly simplifies the processing pipeline vs Laroche-Dolson

**Source**: [Prusa & Holighaus (2022), arXiv:2202.07382](https://arxiv.org/abs/2202.07382)

---

## 3. Critical Challenges

### 3.1 Formant Preservation

**The problem**: When pitch is shifted up uniformly, the spectral envelope (formant structure) shifts proportionally with the fundamental frequency. Since formants are determined by vocal tract geometry (not pitch), this produces the "chipmunk" effect on voices shifted up.

**The solution -- spectral envelope separation**:

Separate the signal into:
1. **Fine structure** (harmonic content, pitch information)
2. **Spectral envelope** (formant positions, timbral identity)

Apply pitch shifting only to the fine structure; restore the original spectral envelope.

**Methods for spectral envelope estimation**:

| Method | Approach | Pros | Cons |
|--------|----------|------|------|
| **Cepstral** | IFFT of log-magnitude, low-pass in quefrency domain, FFT back | Computationally efficient | Underestimates peaks, struggles with high-frequency formants |
| **True Envelope** (Roebel) | Iterative cepstral lifting | Better peak tracking | Higher CPU |
| **LPC** | All-pole filter model | Very effective for speech | Struggles with non-speech |
| **Peak-picking** | Interpolate between magnitude peaks | Simple | Resolution-dependent |

**Formant-corrected pitch shifting in frequency domain**:
```
target_magnitude[j] = spectral_envelope[j] * (analysis_magnitude[k] / spectral_envelope[k])
target_frequency[j] = analysis_frequency[k] * pitchShiftRatio
```

**Practical limitation**: No pitch shifter achieves perfectly artifact-free formant preservation for large shifts (beyond +/- 5-7 semitones). Harmonic spacing changes create gaps that envelope correction cannot fully mask.

**SIMD Optimization Potential: HIGH -- The entire cepstral pipeline is SIMD-friendly**

The cepstral method pipeline is: `log(|X[k]|)` --> IFFT --> liftering window --> FFT --> `exp()` --> spectral envelope. Every step is either an FFT (already SIMD via pffft) or an element-wise operation:

| Step | SIMD Approach | Speedup |
|------|--------------|---------|
| `log(magnitude[k])` | Pommier `log_ps` (SSE) or IEEE 754 bit trick: `log2(x) ~ (float_as_int(x) - 0x3F800000) / (1<<23)` | 4-40x |
| IFFT | pffft (already SIMD) | ~4x (done) |
| Liftering (multiply by window) | `_mm_mul_ps` | ~4x |
| FFT | pffft (already SIMD) | ~4x (done) |
| `exp(smoothed[k])` | Pommier `exp_ps` or IEEE 754 inverse bit trick | 4-20x |
| Spectral divide/multiply | `_mm_div_ps`, `_mm_mul_ps` | ~4x |

The `log` and `exp` calls are the dominant non-FFT cost. Pommier's `sse_mathfun` provides `log_ps` and `exp_ps` with accuracy comparable to `logf`/`expf`. For audio applications where ~3% error is acceptable, the IEEE 754 bit manipulation approach (`_mm_castps_si128` + integer arithmetic) achieves ~40x throughput improvement over scalar `logf`. [Source](http://gallium.inria.fr/blog/fast-vectorizable-math-approx/)

**Sources**:
- [Bernsee -- On the Importance of Formants in Pitch Shifting](http://blogs.zynaptiq.com/bernsee/formants-pitch-shifting/)
- Roebel & Rodet (2005), "Efficient Spectral Envelope Estimation and its application to pitch shifting"

### 3.2 Transient Preservation

**The problem**: Phase vocoders inherently smear transients. The windowed STFT spreads transient energy across multiple frames, producing "pre-echo" or "smearing."

**Solutions**:

1. **Phase reset at transients** (Duxbury et al., 2002; Roebel, 2003): Detect transient events via spectral flux or group delay analysis. At detected transients, reset synthesis phase to match analysis phase directly rather than propagating.

2. **Roebel's contribution** (IRCAM, 2003): Uses group delay of spectral peaks to detect transients. No constraint on stretch factor during transient segments.

3. **PVSOLA**: Transients preserved naturally because cross-correlation positioning tends to align with transient boundaries.

**Sources**:
- Roebel (2003), "A new approach to transient processing in the phase vocoder", DAFx
- Roebel (2003), "Transient detection and preservation in the phase vocoder", ICMC

### 3.3 Latency vs Quality Trade-offs

| Algorithm | Typical Latency | Best For |
|-----------|----------------|----------|
| Delay-line crossfade | < 10ms | Guitar pedals, small shifts, shimmer in feedback |
| TD-PSOLA | 5-20ms | Monophonic voice |
| WSOLA | ~100ms | Speech, simple material |
| Phase Vocoder (1024-pt) | ~23ms | Acceptable frequency resolution |
| Phase Vocoder (2048-pt) | ~46ms | Standard for music |
| Phase Vocoder (4096-pt) | ~93ms | Best frequency resolution |

**Fundamental constraint**: Identifying a 50Hz component requires at least one full period (20ms). Any algorithm that processes low-frequency content accurately cannot achieve latency below this.

---

## 4. Harmonizer-Specific Logic

### 4.1 Intelligent Pitch Shifting (Diatonic Harmonization)

This is what distinguishes a harmonizer from a simple pitch shifter. The algorithm:

1. **Pitch detection**: Determine the fundamental frequency (MIDI note / pitch class) of the input
2. **Note quantization**: Map detected frequency to nearest chromatic pitch class
3. **Diatonic interval lookup**: Given input note, key, scale, and desired harmony interval:
   - Find the input note's scale degree
   - Apply the diatonic interval (e.g., "3rd above")
   - Calculate the required pitch shift in semitones (this varies per input note)

**Example -- Key of C Major, Harmony = "3rd above"**:

| Input | Scale Degree | 3rd Above | Shift (semitones) |
|-------|-------------|-----------|-------------------|
| C | 1 | E | +4 (major 3rd) |
| D | 2 | F | +3 (minor 3rd) |
| E | 3 | G | +3 (minor 3rd) |
| F | 4 | A | +4 (major 3rd) |
| G | 5 | B | +4 (major 3rd) |
| A | 6 | C | +3 (minor 3rd) |
| B | 7 | D | +3 (minor 3rd) |

For non-scale input notes, harmonizers typically use the interval for the nearest scale note.

### 4.2 Key and Scale Tracking Modes

Commercial harmonizers provide several modes:

| Mode | How It Works | Use Case |
|------|-------------|----------|
| **Manual key/scale** | User specifies key + scale type | Live performance, known key |
| **Chordal** | Internal chord recognition adjusts harmony in real-time | Dynamic harmonic context |
| **MIDI input** | External MIDI keyboard specifies exact target notes | Studio, precise control |
| **Guitar input** | Analyzes chords from guitar input | Singer-guitarist performance |
| **Auto key detection** | Chromatic analysis infers key | Hands-free operation |

### 4.3 Multiple Voice Generation

A multi-voice harmonizer generates N independent pitch-shifted copies. Each voice typically has:
- Independent pitch-shift interval
- Independent delay (natural onset timing differences)
- Independent level and pan
- Optional independent feedback path

**Implementation considerations**:
- Each voice requires a full pitch-shifting pipeline (expensive for phase-vocoder approaches)
- Time-domain approaches can share analysis (pitch detection) but need separate synthesis
- Micro-pitch detuning between voices (a few cents) creates natural "ensemble" width

**SIMD Optimization Potential: VERY HIGH -- The ideal use case for voice-parallel SIMD**

Multiple independent voices is the textbook scenario for SIMD: N voices performing identical operations on different data. By interleaving voice data in SoA (Structure-of-Arrays) layout, 4 voices (SSE) or 8 voices (AVX) process simultaneously:

```
// 4 harmony voices, all at different pitch ratios, processed in one SSE register
__m128 pitchRatios = {voice0.ratio, voice1.ratio, voice2.ratio, voice3.ratio};
__m128 phases      = {voice0.phase, voice1.phase, voice2.phase, voice3.phase};
__m128 magnitudes  = {voice0.mag[k], voice1.mag[k], voice2.mag[k], voice3.mag[k]};
// All phase/magnitude manipulations vectorized across voices...
```

**For delay-line pitch shifters**: All per-voice arithmetic (crossfade envelope, interpolation weights) runs in parallel. The limiting factor is the gather from each voice's delay buffer (different read positions).

**For phase vocoders**: Each voice's entire per-bin spectral processing (phase diff, unwrap, frequency estimation, accumulation, polar conversion) can be interleaved across voices. The FFT/IFFT cannot be interleaved (each voice needs its own transform) but the per-bin processing between FFTs can.

**Real-world validation**: [Fathom Synth](https://www.fathomsynth.com/parallel) rewrote their oscillator engine in AVX to process 8 detune voices simultaneously, reporting that "the CPU load required for any number of detune voices up to eight is the same as two detune voices." [Madrona Labs (Sumu)](https://madronalabs.com/news/191) reports ~10% total CPU savings from vertical (4-voice parallel) filter processing.

**Data layout requirement**: Voice-parallel SIMD requires SoA layout (one array per field across all voices) rather than AoS (one struct per voice). Per our project experience (MEMORY.md), SoA provides marginal benefit when data fits in L1 cache (~32KB). For a 4-voice harmonizer with phase-vocoder mode, each voice has a 4096-bin spectral buffer (~16KB per voice, ~64KB total), which exceeds L1 and makes SoA more impactful.

**Estimated speedup**: 3-4x (SSE, 4 voices). A 4-voice harmonizer with phase vocoder mode runs at roughly the cost of 1.5 voices instead of 4.

### 4.4 Shimmer Effect (Pitch Shifting in Feedback)

A related application: placing a pitch shifter inside a delay/reverb feedback loop:

```
Input --> [Delay/Reverb] --> [Pitch Shifter (+12 semitones)] --> [Feedback < 1.0] --> back to input
               |
               v
             Output
```

Each repetition accumulates the pitch shift, creating cascading harmonics. The feedback gain must be < 1.0 for stability. This is the classic Brian Eno / Daniel Lanois "shimmer" sound.

**Key insight from Sean Costello (Valhalla DSP)**: "Pitch shifting is a useful way of avoiding oscillation [in feedback loops], as it pushes the feedback energy into regions that are above or below the original energy in frequency."

**Sources**:
- [Valhalla DSP -- Eno/Lanois Shimmer Sound](https://valhalladsp.com/2010/05/11/enolanois-shimmer-sound-how-it-is-made/)
- [CCRMA -- Shimmer Audio Effect: A Harmonic Reverberator](https://ccrma.stanford.edu/~jingjiez/portfolio/echoing-harmonics/pdfs/Shimmer%20Audio%20Effect%20-%20A%20Harmonic%20Reverberator.pdf)

---

## 5. State of the Art

### 5.1 Production-Quality Libraries

| Library | Approach | License | Notes |
|---------|----------|---------|-------|
| **Rubber Band** | Phase vocoder + phase lamination + transient detection | BSD/Commercial | R2 (faster) and R3 (higher quality, 3x CPU). Pitch via time-stretch + resample |
| **zplane elastique** | Proprietary | Commercial | Formant-preserving, used in Cubase/Ableton |
| **Signalsmith Stretch** | STFT + iterative phase prediction + non-linear frequency mapping | MIT | Single C++11 header, handles wide pitch range |
| **Bungee** | Adaptive phase vocoder, per-grain resampling | MPL 2.0 | Supports zero/negative playback speeds |
| **SoundTouch** | WSOLA | LGPL | Simple, low CPU, ~100ms latency |

### 5.2 Signalsmith Stretch (Notable Open-Source Approach)

Uses a novel phase prediction:
- Measures relative phase at nearby time-frequency points
- Iteratively constructs output phase: horizontal predictions first, then downward (lower frequency) informed by horizontal pass
- Multi-channel: picks loudest channel for phase prediction, copies inter-channel phase differences
- Non-linear frequency mapping: identifies spectral peaks, creates 1:1 frequency scaling around strong harmonics, stretches only weaker components

**Source**: [Signalsmith Stretch Design](https://signalsmith-audio.co.uk/writing/2023/stretch-design/)

---

## 6. Algorithm Comparison

| Algorithm | Polyphonic | Formant Pres. | Transients | Latency | CPU | SIMD Benefit | Best For |
|-----------|-----------|--------------|------------|---------|-----|-------------|----------|
| Delay-line crossfade | Yes | No | Good | <10ms | Very Low | Low (scatter reads) | Shimmer, small shifts |
| TD-PSOLA | No | Natural | Excellent | 5-20ms | Low | Low-Mod (OLA only) | Monophonic voice |
| WSOLA | Partial | No | Moderate | ~100ms | Low | Mod (cross-corr) | Speech |
| Phase Vocoder (basic) | Yes | No | Poor | 23-93ms | Moderate | **Very High** (see 2.4) | General purpose |
| PV + phase locking | Yes | No | Moderate | 23-93ms | Mod-High | **Very High** | Higher quality |
| PV + formant correction | Yes | Yes | Moderate | 23-93ms | High | **Very High** (log/exp) | Vocal processing |
| PVSOLA | Yes | No | Good | 23-93ms | Moderate | High (PV + corr) | Polyphonic |

### Recommendation for Iterum

Given that Iterum is a **delay plugin** (not a dedicated vocal processor), two approaches are most relevant:

1. **Shimmer/feedback harmonizer**: Delay-line crossfade pitch shifter is sufficient and has lowest latency/CPU. Metallic artifacts become part of the character and are masked by delay/reverb context. **We already have this in `ShimmerDelay`.**

2. **Clean harmonizer voice**: Phase vocoder with identity phase locking (Laroche-Dolson) provides the best quality-to-complexity ratio. Add cepstral formant correction for vocal processing. **We already have `PitchShiftProcessor` with `PhaseVocoder` mode and `FormantPreserver`.**

3. **Diatonic harmony**: Add pitch detection (YIN or autocorrelation) feeding a scale-degree lookup table, then dynamically set the pitch-shift ratio. **We already have `PitchDetector` and `pitch_utils.h` with scale quantization.**

---

## 7. Existing KrateDSP Components

### Layer 0 -- Core

| Component | File | Relevance |
|-----------|------|-----------|
| `semitonesToRatio()`, `ratioToSemitones()`, `quantizePitch()` | `core/pitch_utils.h` | Pitch conversion and scale quantization |
| `linearInterpolate()`, `cubicHermiteInterpolate()` | `core/interpolation.h` | Sample-domain resampling |
| `Window::generate()` (Hann, Hamming, Blackman, Kaiser) | `core/window_functions.h` | STFT windowing, COLA-compatible |
| `wrapPhase()`, phase accumulation | `core/phase_utils.h` | Phase vocoder phase handling |
| `GrainEnvelope` | `core/grain_envelope.h` | Grain windowing for granular pitch shift |
| Math constants, dB utils, fast math | `core/` | General DSP utilities |

### Layer 1 -- Primitives

| Component | File | Relevance |
|-----------|------|-----------|
| `FFT` (pffft-backed, SIMD) | `primitives/fft.h` | Phase vocoder FFT/IFFT |
| `STFT` + `OverlapAdd` | `primitives/stft.h` | Complete STFT analysis/synthesis chain |
| `SpectralBuffer` (dual Cartesian/polar) | `primitives/spectral_buffer.h` | Spectral magnitude/phase manipulation |
| Spectral utilities (bin-freq conversion, phase diff, centroid) | `primitives/spectral_utils.h` | Phase vocoder instantaneous frequency estimation |
| `DelayLine` (circular, interpolated) | `primitives/delay_line.h` | Grain processing, delay-based pitch shift |
| `PitchDetector` (autocorrelation) | `primitives/pitch_detector.h` | Input pitch detection for diatonic harmony |
| `GrainPool` (up to 64 grains, voice stealing) | `primitives/grain_pool.h` | Granular pitch-shift voice management |
| `OnePoleSmoother`, `LinearRamp`, `SlewLimiter` | `primitives/smoother.h` | Click-free parameter automation |
| `Biquad`, `SmoothedBiquad`, `BiquadCascade` | `primitives/biquad.h` | Formant filtering, tone shaping |
| `HilbertTransform` | `primitives/hilbert_transform.h` | Analytic signal for frequency shifting |
| `PolyBlepOscillator`, `WavetableOscillator` | `primitives/` | Voice generation |

### Layer 2 -- Processors

| Component | File | Relevance |
|-----------|------|-----------|
| **`PitchShiftProcessor`** | `processors/pitch_shift_processor.h` | **Complete pitch shifter** with 4 modes: Simple (delay-line), Granular (dual crossfade), PitchSync (adaptive grain), PhaseVocoder (STFT) |
| **`FormantPreserver`** | `processors/formant_preserver.h` | **Cepstral formant correction** for vocal naturalness |
| `GrainProcessor` | `processors/grain_processor.h` | Per-grain processing (pitch, pan, envelope) |
| `FrequencyShifter` | `processors/frequency_shifter.h` | SSB modulation (Hz shift, not semitones) |
| `ResonatorBank` | `processors/resonator_bank.h` | Up to 16 tuned resonators |
| `SubOscillator` | `processors/sub_oscillator.h` | Octave-down frequency division |
| `AdditiveOscillator` | `processors/additive_oscillator.h` | Up to 128 partials, IFFT synthesis |

### Layer 3 -- Systems

| Component | File | Relevance |
|-----------|------|-----------|
| **`UnisonEngine`** | `systems/unison_engine.h` | Up to 16 detuned voices, stereo panning, gain compensation |
| `GranularEngine` | `systems/granular_engine.h` | Complete granular system with scheduling |
| `VoiceAllocator` | `systems/voice_allocator.h` | Polyphonic voice allocation (LRU, oldest, round-robin) |
| `SynthVoice` | `systems/synth_voice.h` | Single voice framework (osc + env + filter + mod) |
| `ModulationEngine` / `ModulationMatrix` | `systems/` | LFO routing, parameter automation |
| `DelayEngine` | `systems/delay_engine.h` | Multi-tap delay with feedback routing |
| `StereoField` | `systems/stereo_field.h` | Stereo width, M/S processing |
| `CharacterProcessor` | `systems/character_processor.h` | Analog character modeling |

### Layer 4 -- Effects

| Component | File | Relevance |
|-----------|------|-----------|
| `ShimmerDelay` | `effects/shimmer_delay.h` | Already uses pitch shifting in feedback loop |
| `GranularDelay` | `effects/granular_delay.h` | Granular processing with delay |
| `SpectralDelay` | `effects/spectral_delay.h` | Spectral domain delay effects |
| `Reverb` | `effects/reverb.h` | Reference for large system architecture |

---

## 8. Gap Analysis: Missing Components

### What We Already Have (Verified Against Code)

| Capability | Component(s) | Status | Verification Notes |
|-----------|-------------|--------|-------------------|
| Pitch shifting (4 modes) | `PitchShiftProcessor` | Complete | 4 modes verified: Simple (delay-line, L308-483), Granular (Hann crossfade, L508-654), PitchSync (adaptive grain, L688-893), PhaseVocoder (STFT, L916-1191). All in [pitch_shift_processor.h](dsp/include/krate/dsp/processors/pitch_shift_processor.h) |
| Formant preservation | `FormantPreserver` | Complete | Cepstral method verified: log-mag → IFFT → Hann lifter → FFT → pow(10,x). Configurable quefrency (0.5-5ms). In [formant_preserver.h](dsp/include/krate/dsp/processors/formant_preserver.h). Uses scalar `std::log10`/`std::pow` (SIMD opportunity) |
| FFT / STFT / Overlap-Add | `FFT`, `STFT`, `OverlapAdd` | Complete | pffft-backed SIMD FFT. STFT/OLA in [stft.h](dsp/include/krate/dsp/primitives/stft.h) |
| Spectral manipulation | `SpectralBuffer`, `spectral_utils.h` | Complete | Dual Cartesian/polar with lazy conversion. Phase utilities including `wrapPhase()` (scalar, while-loop based -- could benefit from SSE4.1 `_mm_round_ps`). In [spectral_utils.h](dsp/include/krate/dsp/primitives/spectral_utils.h) |
| Pitch detection | `PitchDetector` | Complete | Autocorrelation-based, 256-sample window (~5.8ms). Used by `PitchSyncGranularShifter`. In [pitch_detector.h](dsp/include/krate/dsp/primitives/pitch_detector.h) |
| Pitch quantization | `quantizePitch()` | Partial | Quantizes to nearest scale degree (Off/Semitones/Octaves/Fifths/Scale modes). **Only supports major scale, hardcoded root=0 (C)**. Has `frequencyToNoteClass()` and `frequencyToCentsDeviation()`. Does NOT compute diatonic intervals. In [pitch_utils.h](dsp/include/krate/dsp/core/pitch_utils.h) |
| Semitone/ratio conversion | `semitonesToRatio()`, `ratioToSemitones()` | Complete | Standard 2^(st/12) formula. In [pitch_utils.h](dsp/include/krate/dsp/core/pitch_utils.h) |
| Window functions | `Window` | Complete | Hann, Hamming, Blackman, Kaiser. In [window_functions.h](dsp/include/krate/dsp/core/window_functions.h) |
| Grain pool management | `GrainPool` | Complete | Up to 64 grains, voice stealing. In [grain_pool.h](dsp/include/krate/dsp/primitives/grain_pool.h) |
| Delay lines | `DelayLine` | Complete | Circular, linear/allpass interpolation. In [delay_line.h](dsp/include/krate/dsp/primitives/delay_line.h) |
| Parameter smoothing | `OnePoleSmoother` et al. | Complete | Exponential, linear ramp, slew limiter. In [smoother.h](dsp/include/krate/dsp/primitives/smoother.h) |
| Stereo field | `StereoField` | Complete | Width, M/S processing. In [stereo_field.h](dsp/include/krate/dsp/systems/stereo_field.h) |
| Shimmer | `ShimmerDelay` | Complete | Pitch shift in feedback loop. In [shimmer_delay.h](dsp/include/krate/dsp/effects/shimmer_delay.h) |

### What's Missing

#### 1. Diatonic Interval Calculator (MISSING -- Layer 0)

**What**: A class that, given a key (C-B), scale type (major, minor, dorian, etc.), input note, and desired diatonic interval (2nd through octave), returns the correct number of semitones to shift.

**Why**: `quantizePitch()` in [pitch_utils.h](dsp/include/krate/dsp/core/pitch_utils.h) can quantize to scale degrees, but it does NOT compute diatonic intervals. Verified limitations: only supports major scale (hardcoded), root is always 0 (no key selection), and the function snaps a semitone value to the nearest scale degree -- it does not compute "given this input note, what is a 3rd above in this key?" A harmonizer needs that interval lookup: "Input note is D, key is C major, interval is 3rd above --> output is F, shift = +3 semitones."

**Proposed API**:
```cpp
struct DiatonicInterval {
    int semitones;      // actual semitone shift (e.g., +3 or +4)
    int scaleDegree;    // target scale degree (1-7)
    int octaveOffset;   // if interval wraps around
};

class ScaleHarmonizer {
public:
    void setKey(int rootNote);           // 0=C, 1=C#, ..., 11=B
    void setScale(ScaleType type);       // Major, Minor, Dorian, etc.
    void setInterval(int diatonicSteps); // 1=unison, 2=2nd, 3=3rd, etc.

    DiatonicInterval calculate(int inputMidiNote) const;
    float getSemitoneShift(float inputFrequency) const;
};
```

**Complexity**: Low. This is a lookup table indexed by scale degree.

#### 2. Multi-Voice Harmonizer System (MISSING -- Layer 3)

**What**: A system that manages N independent pitch-shifted voices, each with its own interval, level, pan, and delay. Coordinates pitch detection (shared), interval calculation (per voice), and pitch shifting (per voice).

**Why**: `UnisonEngine` provides multi-voice detuning but with a *fixed frequency* per voice (set externally). A harmonizer needs each voice to independently track the input pitch, calculate its own diatonic interval, and apply pitch shifting with formant preservation.

**Proposed API**:
```cpp
class HarmonizerEngine {
public:
    void prepare(double sampleRate, int maxBlockSize);
    void process(const float* input, float* output, int numSamples);

    // Voice configuration
    void setNumVoices(int count);           // 1-4 harmony voices
    void setVoiceInterval(int voice, int diatonicSteps); // e.g., 3rd, 5th
    void setVoiceLevel(int voice, float dB);
    void setVoicePan(int voice, float pan);  // -1 to +1
    void setVoiceDelay(int voice, float ms); // onset offset

    // Global configuration
    void setKey(int rootNote);
    void setScale(ScaleType type);
    void setPitchShiftMode(PitchShiftMode mode); // Simple, Granular, PhaseVocoder
    void setFormantPreserve(bool enabled);
    void setDryLevel(float dB);
};
```

**Internal architecture**:
- 1x shared `PitchDetector` (analyze input once)
- 1x `ScaleHarmonizer` (compute intervals)
- Nx `PitchShiftProcessor` (one per voice -- most expensive part)
- Nx `FormantPreserver` (optional, one per voice if phase-vocoder mode)
- Per-voice `OnePoleSmoother` for level/pan automation
- Per-voice `DelayLine` for onset offset

**Complexity**: Moderate. The main challenge is CPU cost -- N phase-vocoder instances are expensive. The granular or simple modes would be cheaper per voice.

#### 3. Phase Locking for Phase Vocoder (CONFIRMED MISSING -- Layer 2)

**What**: Identity phase locking (Laroche & Dolson 1999) for the existing `PhaseVocoderPitchShifter`.

**Why**: Without phase locking, the phase vocoder produces "phasey" output. Adding identity phase locking would significantly improve quality.

**Verification** (code reviewed: [pitch_shift_processor.h:1069-1157](dsp/include/krate/dsp/processors/pitch_shift_processor.h#L1069-L1157)):

The `PhaseVocoderPitchShifter::processFrame()` uses **basic phase accumulation only** -- no phase locking is implemented. Specifically:

1. **No peak detection**: There is no code scanning the magnitude spectrum for local maxima. The loop at line 1073 iterates all bins identically.
2. **No region-of-influence assignment**: There is no assignment of non-peak bins to their nearest peak.
3. **Phase propagation is per-bin, not peak-based**: Phase is accumulated independently for every bin via `synthPhase_[k] += freq` (line 1124). This is the classic basic phase vocoder approach that destroys vertical phase coherence.
4. **Sin/cos called for every bin**: Lines 1128-1130 call scalar `std::cos` and `std::sin` for all 2049 bins per frame (4096-pt FFT). With identity phase locking, these transcendentals would only be needed for peak bins (typically 20-100), dramatically reducing cost.

**What's needed**: Add identity phase locking per Laroche & Dolson (1999):
- Peak detection in magnitude spectrum (3-point local maximum)
- Region assignment (each non-peak bin assigned to nearest peak)
- Phase propagation for peaks only, then lock non-peak phases: `phi_out[k] = phi_out[peak] + (phi_in[k] - phi_in[peak])`
- This would both improve quality (reduce phasiness) and reduce CPU (fewer sin/cos calls)

#### 4. Transient Detection / Phase Reset (CONFIRMED MISSING -- Layer 1)

**What**: A transient detector that identifies percussive onsets in the spectral domain (via spectral flux, energy ratio, or group delay analysis) and triggers phase resets in the phase vocoder.

**Why**: Without transient handling, the phase vocoder smears drum hits, consonant onsets, and other transient events. This is one of the most audible quality differences between basic and production-quality pitch shifters.

**Verification** (code reviewed: [pitch_shift_processor.h:1069-1157](dsp/include/krate/dsp/processors/pitch_shift_processor.h#L1069-L1157)):

The `PhaseVocoderPitchShifter::processFrame()` has **no transient detection or phase reset logic**. Every frame is processed identically:

1. **No spectral flux calculation**: There is no comparison of magnitude between successive frames to detect energy changes.
2. **No group delay analysis**: No group delay computation for transient detection per Roebel (2003).
3. **No phase reset**: The synthesis phase (`synthPhase_[k]`) is always accumulated (`synthPhase_[k] += freq`, line 1124). There is no conditional branch to reset it to the analysis phase at transient boundaries.
4. **No transient flag or detection state**: No member variables related to transient detection.

**What's needed**: Add a transient detector (spectral flux is simplest):
- Compute `spectralFlux = sum(max(0, |X_curr[k]| - |X_prev[k]|))` per frame
- When flux exceeds a threshold (relative to running average): set transient flag
- At transient frames: `synthPhase_[k] = analysisPhase[k]` (phase reset) instead of accumulation
- This preserves sharp attack transients instead of smearing them across frames

#### 5. Harmony Mode Controller (MISSING -- Layer 3 or Plugin Level)

**What**: Higher-level logic for managing harmony modes beyond simple diatonic intervals:
- **Chromatic mode**: Fixed semitone shift (no scale awareness)
- **Scalic mode**: Diatonic interval in a fixed key/scale
- **Chordal mode**: Interval adapts to detected chords
- **MIDI mode**: Target notes from external MIDI input

**Why**: Commercial harmonizers (TC-Helicon, Eventide) offer multiple harmony intelligence modes. At minimum, chromatic and scalic modes are expected.

**Complexity**: Chromatic and scalic modes are straightforward (use `ScaleHarmonizer`). Chordal and MIDI modes are significantly more complex and could be deferred.

#### 6. Pitch Smoothing / Hysteresis (MISSING -- Layer 1 or 2)

**What**: A pitch tracking post-processor that:
- Applies median filtering or hysteresis to prevent rapid note-to-note switching from pitch detection jitter
- Handles the transition between notes (portamento vs immediate switch)
- Manages the case where the input is between two notes or unvoiced

**Why**: Raw pitch detector output is noisy and can oscillate between adjacent notes, causing the harmony voice to "warble" or produce audible pitch jumps. Commercial harmonizers apply significant smoothing and confidence thresholds.

**Complexity**: Low-moderate. A median filter + confidence gate + hysteresis threshold.

### Summary: Priority Order for Implementation

| Priority | Component | Layer | Effort | Impact | Status |
|----------|-----------|-------|--------|--------|--------|
| 1 | Diatonic Interval Calculator (`ScaleHarmonizer`) | 0 | Low | Required -- core logic | **CONFIRMED MISSING** (pitch_utils.h only has major scale quantization, no interval calc) |
| 2 | Multi-Voice Harmonizer System (`HarmonizerEngine`) | 3 | Moderate | Required -- orchestration | **MISSING** |
| 3 | Identity Phase Locking (Laroche-Dolson) | 2 | Low-Moderate | High quality improvement | **CONFIRMED MISSING** (processFrame uses basic per-bin phase accumulation only) |
| 4 | Pitch Smoothing / Hysteresis | 1-2 | Low | Practical usability | **MISSING** |
| 5 | Transient Detection / Phase Reset | 1-2 | Moderate | Quality improvement | **CONFIRMED MISSING** (no spectral flux, no phase reset logic) |
| 6 | Harmony Mode Controller | 3/plugin | Low-Moderate | Feature completeness | **MISSING** |

---

## 9. Key Academic References

| Year | Authors | Paper | Contribution |
|------|---------|-------|-------------|
| 1966 | Flanagan & Golden | "Phase Vocoder" (Bell Sys. Tech. Journal) | Introduced the phase vocoder |
| 1978 | Moorer | "The Use of the Phase Vocoder in Computer Music Applications" (JAES) | Adapted phase vocoder for music |
| 1993 | Verhelst & Roelands | "WSOLA" (ICASSP) | Waveform-similarity overlap-add |
| 1999 | Laroche & Dolson | "Improved phase vocoder time-scale modification" (IEEE Trans.) | Phase locking (loose and identity) |
| 1999 | Laroche & Dolson | "New phase-vocoder techniques for pitch-shifting, harmonizing" (IEEE WASPAA) | Direct frequency-domain pitch shifting, harmonizing |
| 2002 | de Cheveigne & Kawahara | "YIN" (JASA) | YIN pitch detection, 3x fewer errors |
| 2003 | Roebel | "A new approach to transient processing in the phase vocoder" (DAFx) | Group-delay transient detection |
| 2005 | Roebel & Rodet | "Efficient Spectral Envelope Estimation" | True envelope for formant-correct pitch shifting |
| 2011 | Kraft et al. | "PVSOLA" (DAFx-11) | Hybrid phase vocoder + WSOLA |
| 2022 | Prusa & Holighaus | "Phase Vocoder Done Right" (arXiv) | Phase gradient estimation, no peak picking needed |

---

## 10. SIMD Optimization Priority Matrix

Summary of all SIMD opportunities ranked by impact (combination of speedup magnitude and fraction of total CPU time in a harmonizer pipeline):

### Tier 1: High Impact, Already Done

| Operation | Status | Notes |
|-----------|--------|-------|
| FFT/IFFT (pffft) | Done | ~4x via SSE radix-4 butterflies. 30-40% of total CPU |

### Tier 2: High Impact, Low-Medium Implementation Effort

| Priority | Operation | Speedup | Effort | Key Intrinsics / Libraries |
|----------|-----------|---------|--------|---------------------------|
| 1 | Cartesian-to-Polar (`atan2`, `sqrt`) | 10-50x | Medium | Pommier `atan2_ps`, `_mm_sqrt_ps` or `_mm_rsqrt_ps` + Newton-Raphson |
| 2 | Polar-to-Cartesian (`sin`, `cos`) | 3-8x | Medium | Pommier `sincos_ps` from sse_mathfun |
| 3 | `log` / `exp` (formant cepstrum) | 4-40x | Low | Pommier `log_ps`/`exp_ps`, or IEEE 754 bit tricks |
| 4 | Multi-voice parallel processing | 3-4x | Medium-High | SoA layout, all per-bin ops across 4 voices in `__m128` |

### Tier 3: Moderate Impact, Low Effort (May Auto-Vectorize)

| Priority | Operation | Speedup | Effort | Key Intrinsics |
|----------|-----------|---------|--------|----------------|
| 5 | Windowing (input * window) | ~4x | Very Low | `_mm_mul_ps` (compilers often auto-vectorize) |
| 6 | Phase diff + unwrapping | ~4x | Low | `_mm_sub_ps`, `_mm_round_ps` (SSE4.1) |
| 7 | Overlap-add accumulation | ~4x | Very Low | `_mm_add_ps` or `_mm_fmadd_ps` |
| 8 | Phase accumulation, freq estimation | ~4x | Very Low | `_mm_fmadd_ps` |
| 9 | Spectral multiply/divide (formant) | ~4x | Very Low | `_mm_mul_ps`, `_mm_div_ps` |

### Tier 4: Low Impact or Diminishing Returns

| Priority | Operation | Speedup | Why Low Impact |
|----------|-----------|---------|---------------|
| 10 | Peak detection (phase locking) | 2-3x | Small fraction of CPU; region propagation is irregular |
| 11 | Bin shifting / scatter | 1.5-2x | Scatter writes resist SIMD; no efficient SSE/AVX scatter |
| 12 | Cross-correlation (WSOLA) | 3-4x | Only applies if using WSOLA mode |
| 13 | Delay-line reads (single tap) | ~1x | Sequential reads, no parallelism |
| 14 | YIN normalization (prefix sum) | ~1x | Sequential dependency |

### Recommended Implementation Strategy

1. **Add a vectorized math header** (`simd_math.h` or similar) wrapping Pommier's sse_mathfun with `atan2_ps`, `sincos_ps`, `log_ps`, `exp_ps`. pffft already demonstrates this library is compatible with our build. This single addition unlocks Tier 2 items 1-3.

2. **Add batch `cartesianToPolar()` / `polarToCartesian()`** to `SpectralBuffer` using the vectorized math. Our current `Complex::magnitude()` and `Complex::phase()` use scalar `std::sqrt` and `std::atan2` -- replacing these with batched SIMD versions is the single highest-impact optimization for the phase vocoder.

3. **Design the multi-voice `HarmonizerEngine` with SoA layout from the start**. For 4 voices with 4096-bin spectral buffers (~64KB total), data exceeds L1 cache and SoA becomes more impactful than the marginal gains we measured for L1-fitting particle oscillator data (see MEMORY.md).

4. **Don't hand-vectorize** Tier 3 operations initially -- verify whether `-O2` auto-vectorization handles them. Only add explicit intrinsics if profiling shows they're still bottlenecks.

### Alignment Requirements

| ISA | Alignment | Register Width | Floats/Op | Notes |
|-----|-----------|----------------|-----------|-------|
| SSE | 16 bytes | 128-bit | 4 | Minimum target; `_mm_loadu_ps` nearly free on Haswell+ |
| AVX | 32 bytes | 256-bit | 8 | Use `alignas(32)` for stack buffers |
| AVX-512 | 64 bytes | 512-bit | 16 | Not widely available; don't target yet |
| NEON | 16 bytes | 128-bit | 4 | ARM equivalent of SSE; pffft supports it |

Use `pffft_aligned_malloc` (already in codebase) or `alignas()` for aligned allocations.

### SIMD Sources

- [Pommier sse_mathfun -- SSE sin/cos/log/exp](https://github.com/RJVB/sse_mathfun)
- [sse_mathfun_extension -- SSE atan2](https://github.com/to-miz/sse_mathfun_extension)
- [Mazzo -- Vectorized atan2 (50x speedup)](https://mazzo.li/posts/vectorized-atan2.html)
- [FABE -- SIMD sin/cos/sincos](https://github.com/farukalpay/FABE)
- [Fathom Synth -- AVX parallel voice processing](https://www.fathomsynth.com/parallel)
- [Madrona Labs Sumu -- SIMD filter optimization](https://madronalabs.com/news/191)
- [Gallium -- Fast vectorizable math approximations](http://gallium.inria.fr/blog/fast-vectorizable-math-approx/)
- [Rubber Band VectorOpsComplex.h -- Production SIMD Cart/Polar](https://github.com/breakfastquay/rubberband)
- [KVR -- SIMD delay line discussion](https://www.kvraudio.com/forum/viewtopic.php?t=285500)
- [KVR -- Modern synth architecture SIMD](https://www.kvraudio.com/forum/viewtopic.php?t=586455)
- [SLEEF -- Vectorized math library](https://sleef.org/)
- [dspGuru -- Magnitude estimator tricks](https://dspguru.com/dsp/tricks/magnitude-estimator/)

### Additional Sources

- [Bernsee -- Pitch Shifting Using The Fourier Transform](http://blogs.zynaptiq.com/bernsee/pitch-shifting-using-the-ft/)
- [Bernsee -- On the Importance of Formants](http://blogs.zynaptiq.com/bernsee/formants-pitch-shifting/)
- [katjaas.nl -- Pitch Shifting](https://www.katjaas.nl/pitchshift/pitchshift.html)
- [Signalsmith Stretch Design](https://signalsmith-audio.co.uk/writing/2023/stretch-design/)
- [Valhalla DSP -- Shimmer Sound](https://valhalladsp.com/2010/05/11/enolanois-shimmer-sound-how-it-is-made/)
- [Eventide H90 Harmonizer Documentation](https://cdn.eventideaudio.com/manuals/h90/1.10.7/content/algorithms/harmonizer.html)
- [Rubber Band Technical Documentation](https://www.breakfastquay.com/rubberband/technical.html)
- DAFX: Digital Audio Effects (Udo Zolzer, ed.) -- Chapters on pitch shifting and time-scale modification
- [JOS -- Spectral Audio Signal Processing](https://ccrma.stanford.edu/~jos/sasp/) (Julius O. Smith III)
