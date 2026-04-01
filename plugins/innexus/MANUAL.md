# Innexus — User Manual

## Introduction

Innexus is a harmonic analysis and resynthesis instrument that deconstructs audio into its fundamental components — pitch, harmonics, and noise — then rebuilds it as a fully playable synthesizer voice. Load a sample or feed live audio through the sidechain, and Innexus extracts up to 96 partials in real time using spectral analysis and pitch tracking. The resulting harmonic model can be frozen, morphed, filtered, modulated, blended across multiple sources, and physically modelled, turning any sound into a rich, evolving instrument.

At its core, Innexus uses a bank of amplitude-stable Gordon-Smith oscillators (48-96 partials) driven by real-time spectral analysis. The analysis pipeline detects fundamental frequencies, tracks individual harmonics, separates tonal content from noise residual, and builds a harmonic model that can be manipulated in ways impossible with conventional sampling or synthesis.

The interface is organized into a single scrollable window with clearly labeled sections:

- **Header** — Input source, sample loading, latency mode, and master gain
- **Analysis Display** — Real-time spectral visualization and pitch detection
- **Musical Control** — Freeze, morph, response, and harmonic filtering
- **Oscillator / Residual** — Release time, partial count, inharmonicity, levels, and residual shaping
- **Memory** — 8 harmonic snapshot slots with capture and recall
- **Stereo / Detune** — Stereo spread and per-partial detuning
- **Evolution Engine** — Autonomous timbral morphing through memory slots
- **Presets** — Preset management
- **Modulators** — Two independent LFOs for per-partial modulation
- **Multi-Source Blend** — Weighted mix of memory slots and live input
- **Exciter** — Residual, impact, and bow excitation models
- **Resonator Bank** — Modal resonator, waveguide string, and body resonance
- **Sympathetic Resonance** — Cross-voice sympathetic string resonance
- **Harmonic Physics** — Warmth, coupling, stability, and entropy
- **Analysis Feedback** — Self-evolving feedback loop
- **ADSR Envelope** — Auto-detected envelope shaping
- **Voice Mode** — Mono, 4-voice, and 8-voice polyphony with MPE

### Quick Start

1. Load a sample by clicking the **Load** button in the header (or drag a .wav/.aiff file onto the window)
2. Play MIDI notes — Innexus synthesizes the sample's harmonic content at the played pitch
3. Adjust **Master Gain** and **Harmonic Level** to set the output volume
4. Explore the **Harmonic Filter** to sculpt the spectrum (try Odd Only or Even Only)
5. Use **Freeze** to capture a moment, then sweep **Morph** to blend between frozen and live states
6. Capture harmonic snapshots into **Memory** slots and enable the **Evolution Engine** for autonomous timbral drifting

---

## Signal Flow

Understanding the signal path helps you predict how changes in one section affect the overall sound.

```
Input Source (Sample file or Sidechain audio)
        |
        v
  Pre-Processing (DC removal, 30 Hz high-pass, noise gate)
        |
        v
  Analysis Pipeline (YIN pitch tracking, dual-window STFT, partial tracking,
                     DFT amplitude normalization, harmonic model building)
        |
        v
  Harmonic Model -----> Memory Slots (capture/recall)
        |                     |
        v                     v
  Musical Control         Evolution Engine
  (Freeze, Morph,         (Cycle, PingPong,
   Harmonic Filter)        Random Walk)
        |                     |
        +----------+----------+
                   |
                   v
          Multi-Source Blend (8 slot weights + live weight)
                   |
                   v
          Harmonic Physics (warmth, coupling, stability, entropy)
                   |
                   v
          Harmonic Modulators (LFO 1 & 2: amplitude, frequency, pan)
                   |
                   v
          Oscillator Bank (48-96 Gordon-Smith oscillators)
                   |
        +----------+----------+
        |                     |
        v                     v
  Harmonic Output         Exciter
  (Harmonic Level)        (Residual / Impact / Bow)
        |                     |
        |                     v
        |              Resonator Bank
        |              (Modal / Waveguide)
        |                     |
        |                     v
        |              Body Resonance
        |              (Size, Material, Mix)
        |                     |
        |                     v
        |              Residual Output
        |              (Residual Level, Brightness,
        |               Transient Emphasis,
        |               Physical Model Mix)
        |                     |
        +----------+----------+
                   |
                   v
          Per-Voice Gain (velocity, expression, ADSR envelope)
                   |
                   v
          Stereo Spread + Detune Spread
                   |
                   v
          Polyphonic Voice Sum (1/sqrt(N) gain compensation)
                   |
                   v
          Sympathetic Resonance (post-voice, pre-master)
                   |
                   v
          Master Gain --> Safety Limiter --> Output
                   |
                   +---> Analysis Feedback Loop (sidechain mode only)
                              |
                              +---> back to Analysis Pipeline
```

MIDI input (notes, velocity, pitch bend, MPE expression) controls the oscillator bank pitch and triggering. The analysis provides the *timbre* (harmonic amplitudes, frequencies, and noise character), while MIDI provides the *musical control* (which notes to play, dynamics, articulation). In polyphonic mode, each voice independently tracks its own pitch and envelope.

---

## Header

The header is always visible at the top of the plugin window.

| Control | Description |
|---------|-------------|
| **INNEXUS** | Plugin title (left side) |
| **Source** | Input source selector: **Sample** (analyze a loaded audio file) or **Sidechain** (analyze live audio from the DAW's sidechain input) |
| **Load** | Click to load a .wav or .aiff file for analysis. You can also drag and drop files directly onto the plugin window. |
| **Filename** | Displays the name of the currently loaded sample |
| **Latency** | Analysis precision mode: **Low Latency** (11.6 ms processing delay, faster response) or **High Precision** (longer analysis windows, detects fundamentals down to 40 Hz, better frequency resolution) |
| **Gain** | Master output level (0.0-1.0, default 0.8) — controls the final output volume |

---

## Analysis Display

The analysis display section provides real-time visual feedback of what Innexus is hearing and synthesizing.

### Harmonic Display

The left panel shows a bar graph of the amplitude of each detected partial (up to 96 bars depending on the Partial Count setting). Cyan bars indicate active harmonics, and the display updates approximately 30 times per second from the analysis data. This gives you immediate visual feedback of the spectral content being synthesized.

### Pitch Detection

The right panel shows the fundamental frequency detection status:

- **Detected frequency** in Hz and musical note name (e.g., "A3 = 220 Hz")
- **Confidence indicator** — color-coded quality meter (green = high, yellow = medium, red = low)
- **Mode badge** — shows the current analysis mode (MONO, POLY, AUTO>M, or AUTO>P)
- In polyphonic mode, displays up to 8 detected voices with individual confidence bars

---

## Musical Control

The Musical Control section provides real-time manipulation of the harmonic model.

| Control | Range | Description |
|---------|-------|-------------|
| **Freeze** | On/Off | Captures and holds the current harmonic state as a frozen snapshot. The oscillator bank continues playing from the frozen state. Uses a 10 ms crossfade when disengaging to prevent click artifacts. |
| **Morph** | 0.0-1.0 | Blends between the frozen state (0.0) and live analysis (1.0). Only active when Freeze is engaged. Per-partial amplitude and frequency interpolation with smooth 7 ms filtering to prevent zipper noise. |
| **Response** | 0.0-1.0 | Controls how quickly the live analysis updates (sidechain mode only). 0.0 = slowest/most stable, 1.0 = fastest/most responsive. Default 0.5. |
| **Harmonic Filter** | 5 modes | Per-partial amplitude mask applied after morph, before synthesis. Does not affect the residual noise component. |

### Harmonic Filter Modes

| Mode | Description |
|------|-------------|
| **All-Pass** | No filtering — all partials pass through at full amplitude |
| **Odd Only** | Keeps only odd-numbered harmonics (1st, 3rd, 5th, ...) — creates a hollow, clarinet-like character |
| **Even Only** | Keeps only even-numbered harmonics (2nd, 4th, 6th, ...) — creates a thinner, octave-up quality |
| **Low Harm.** | Emphasizes the fundamental and lower partials; upper harmonics are progressively attenuated |
| **High Harm.** | Emphasizes upper partials; the fundamental is attenuated by 18 dB or more |

---

## Oscillator / Residual

This section controls the synthesis engine's core parameters and the balance between tonal and noise components.

### Oscillator Controls

| Control | Range | Description |
|---------|-------|-------------|
| **Release** | 20-5000 ms | Exponential fade-out time on MIDI note-off. Controls how long harmonics ring out after releasing a key. Short values (20-50 ms) for percussive sounds, longer values (500-5000 ms) for pads. |
| **Partials** | 48 / 64 / 80 / 96 | Number of active oscillators in the resynthesis engine. 48 = efficient, covers fundamental + upper harmonics. 96 = captures more spectral detail including subtle sub-harmonics. Higher counts increase CPU usage. |
| **Inharm.** | 0-100% | Inharmonicity amount. At 100%, the oscillators use the source's exact analyzed frequency deviations (natural for bells, metallic instruments). At 0%, frequencies are forced to a perfect harmonic series (1f, 2f, 3f, ...). Middle values blend between pure and source-derived frequencies. Default 100%. |

### Level Controls

| Control | Range | Description |
|---------|-------|-------------|
| **Harmonic Level** | 0.0-2.0 | Output level of the tonal harmonic content. 0.0 = silence, 1.0 = unity, 2.0 = +6 dB boost. Default 1.0. |
| **Residual Level** | 0.0-2.0 | Output level of the noise/residual component (non-harmonic spectral content: breath, fricatives, inharmonic shimmer). 0.0 = pure tones only, 1.0 = balanced mix, 2.0 = emphasis on noisy textures. Default 0.3. |
| **Residual Brightness** | -1.0 to +1.0 | Spectral tilt of the residual noise. Negative = darker (sub-harmonic rumble), 0.0 = neutral (matches analysis), positive = brighter (sizzle, air). |
| **Transient Emphasis** | 0.0-2.0 | Boosts or suppresses detected attack transients. 0.0 = no emphasis, 0.5-1.0 = gentle attack shaping, 2.0 = extreme percussive attack. |

---

## Memory

The Memory section lets you capture, store, and recall up to 8 harmonic snapshots. Each snapshot preserves the complete harmonic state (partial amplitudes, frequencies, and ADSR envelope) at the moment of capture.

| Control | Description |
|---------|-------------|
| **Slot** | Selector for the active memory slot (1-8) |
| **Capture** | Saves the current harmonic state into the selected slot. Captures from whatever source is active — post-morph blend, frozen frame, live sidechain, or sample analysis. Can be triggered at any time during playback. |
| **Recall** | Loads the harmonic snapshot from the selected slot. Automatically engages Freeze and loads the recalled state with a click-free crossfade transition. |
| **Slot Status** | Visual grid showing which slots contain saved data. Occupied slots are highlighted; empty slots are grayed out. |

---

## Stereo / Detune

| Control | Range | Description |
|---------|-------|-------------|
| **Stereo Spread** | 0.0-1.0 | Per-partial stereo panning for decorrelation. At 0.0, all partials are center-panned (mono). At 1.0, odd partials pan left and even partials pan right, with the fundamental reduced to 25%. Values of 0.3-0.7 create a lush, widened stereo image without phasing artifacts. Default 0.0. |
| **Detune Spread** | 0.0-1.0 | Per-partial frequency offset for chorus-like richness. Odd partials detune positive, even partials detune negative. The fundamental is excluded (less than 1 cent deviation). 0.0 = no detune, 0.2-0.5 = subtle thickness, 1.0 = extreme detuning with bell-like ensemble character. Default 0.0. |

---

## Evolution Engine

The Evolution Engine provides autonomous timbral morphing by smoothly interpolating between occupied memory slots over time. It runs continuously (not synced to MIDI notes), creating slow, evolving textures.

| Control | Range | Description |
|---------|-------|-------------|
| **Enable** | On/Off | Activates autonomous morphing between occupied memory slots |
| **Speed** | 0.01-10.0 Hz | Rate of evolution. 0.01 Hz = 100 seconds per cycle, 0.1 Hz = 10 seconds, 1.0 Hz = 1 second, 10.0 Hz = chaotic rapid morphing. |
| **Depth** | 0.0-1.0 | How deeply to explore the morph range between waypoints. 0.0 = nearly static, 0.5 = balanced exploration, 1.0 = maximum morph excursion. Default 0.5. |
| **Mode** | 3 modes | Traversal pattern through occupied memory slots |
| **Position** | (visual) | Indicator dot showing the current interpolation position within the evolution cycle |

### Evolution Modes

| Mode | Description |
|------|-------------|
| **Cycle** | 1 -> 2 -> 3 -> 4 -> 1 -> 2 -> ... Linear forward motion, wraps around. |
| **PingPong** | 1 -> 2 -> 3 -> 4 -> 3 -> 2 -> 1 -> 2 -> ... Bounces back and forth. |
| **Random Walk** | Drifts randomly within the depth range. Unpredictable, organic movement. |

Evolution requires at least 2 occupied memory slots. For best results, capture contrasting timbres — e.g., a bright attack moment and a dark sustain — then let the engine drift between them at a slow speed (0.05-0.2 Hz) for pad textures.

---

## Presets

| Control | Description |
|---------|-------------|
| **Preset Browser** | Opens an overlay with factory presets organized by category. Search and filter by name, load with one click. |
| **Save** | Opens a dialog to save the current state as a user preset, including all parameter values and memory slot contents. |

---

## Modulators

Two independent LFO modulators provide per-partial animation. Each modulator applies its waveform to a selectable range of partials, targeting amplitude, frequency, or pan.

### Per-Modulator Controls

| Control | Range | Description |
|---------|-------|-------------|
| **Enable** | On/Off | Activates this modulator |
| **Waveform** | 5 shapes | Sine, Triangle, Square, Saw, or Random S&H (sample-and-hold noise) |
| **Rate** | 0.01-20.0 Hz | LFO frequency in free-running Hz mode |
| **Rate Sync** | On/Off | When on, the Rate control switches to note values for tempo-synced modulation. When off, Rate is in free Hz. Default on. |
| **Note Value** | 21 values | Tempo-synced rate when Rate Sync is enabled. Ranges from 1/64 triplet through 4 bars dotted. Default 1/8 note. |
| **Depth** | 0.0-1.0 | How much the LFO modulates the target. 0.0 = no modulation, 1.0 = maximum. |
| **Range Start** | 1-96 | First partial affected by this modulator |
| **Range End** | 1-96 | Last partial affected by this modulator. Only partials between Start and End are modulated. |
| **Target** | 3 modes | What the LFO modulates |

### Modulation Targets

| Target | Description |
|--------|-------------|
| **Amplitude** | Multiplicative modulation — partials get quieter and louder rhythmically. Unipolar (0 to 1). |
| **Frequency** | Additive pitch modulation in cents (up to +/-50 cents). Creates vibrato and pitch wobble effects. Bipolar. |
| **Pan** | Stereo position offset (up to +/-0.5). Partials drift left and right. Bipolar. |

When two modulators overlap the same partial range and target, their effects combine (multiply for amplitude, add for frequency/pan).

---

## Multi-Source Blend

The Multi-Source Blend section provides a weighted mix of memory slots and live input, allowing hybrid timbres from multiple captured states simultaneously.

| Control | Range | Description |
|---------|-------|-------------|
| **Enable** | On/Off | Activates blend mode. When on, overrides normal recall/freeze/evolution. |
| **Slot 1-8** | 0.0-1.0 each | Individual weight for each memory slot. Only occupied slots contribute (empty slots add nothing). Weights are automatically normalized so that total output level stays consistent. |
| **Live** | 0.0-1.0 | Weight of the live sidechain/sample analysis input. 0.0 = only recalled memories, 1.0 = only live input, 0.5 = 50/50 blend. |

---

## Exciter

The Exciter section controls how energy is injected into the resonator bank. The exciter type determines the character of the attack and sustain for the residual/physical model path.

| Control | Range | Description |
|---------|-------|-------------|
| **Exciter Type** | 3 modes | Selects the excitation source: **Residual** (original analyzed noise), **Impact** (synthetic mallet strike), or **Bow** (bowed string model) |

### Impact Exciter

Available when Exciter Type is set to **Impact**. Simulates a mallet or hammer striking the resonator.

| Control | Range | Description |
|---------|-------|-------------|
| **Hardness** | 0.0-1.0 | Mallet hardness. 0.0 = soft felt (muted, mellow), 0.5 = medium rubber (balanced), 1.0 = hard metal (bright, percussive). Default 0.5. |
| **Mass** | 0.0-1.0 | Mallet mass. Low values = light, snappy attack. High values = heavy, booming impact with longer contact time. Default 0.3. |
| **Brightness** | -1.0 to +1.0 | Spectral coloration of the impact burst. Negative = darker, 0.0 = neutral, positive = brighter. |
| **Position** | 0.0-1.0 | Strike point along the resonating body. 0.0 = near the bridge (thin, metallic), 0.5 = center (full, fundamental-heavy), 1.0 = near the nut (muted, soft). Default 0.13. |

### Bow Exciter

Available when Exciter Type is set to **Bow**. Simulates a bow continuously exciting the resonator, producing sustained tones.

| Control | Range | Description |
|---------|-------|-------------|
| **Pressure** | 0.0-1.0 | Bow pressure against the string. Low = airy, flautando. High = intense, crunchy. Default 0.3. |
| **Speed** | 0.0-1.0 | Bow velocity. Low = slow, gentle. High = fast, aggressive. Default 0.5. |
| **Position** | 0.0-1.0 | Bow contact point. 0.0 = sul ponticello (bridge, glassy harmonics), 0.5 = normal position, 1.0 = sul tasto (fingerboard, muted). Default 0.13. |
| **Oversampling** | On/Off | Enables 2x oversampling for the bow model. Improves high-frequency accuracy at the cost of additional CPU. |

---

## Resonator Bank

The Resonator Bank processes the exciter output through a physical resonance model. Two resonator algorithms are available, plus a body resonance post-processor.

### Resonance Type

| Control | Range | Description |
|---------|-------|-------------|
| **Resonance Type** | Modal / Waveguide | Selects the resonance algorithm. **Modal** uses a bank of damped bandpass filters tuned to the analyzed partials. **Waveguide** uses a digital waveguide (Karplus-Strong family) for string-like resonance. |

### Modal Resonator Controls

| Control | Range | Description |
|---------|-------|-------------|
| **Decay** | 0.01-5.0 s | How long modal resonances ring. Short (0.01-0.1 s) for percussive sounds, long (1-5 s) for sustained ringing. Default 0.5 s. Exponential mapping. |
| **Brightness** | 0.0-1.0 | Spectral tilt of the resonator modes. 0.0 = dark (upper modes decay faster), 0.5 = neutral, 1.0 = bright (all modes ring equally). Default 0.5. |
| **Stretch** | 0.0-1.0 | Stretches the spacing of resonant modes. 0.0 = harmonic (perfect integer ratios), 1.0 = fully stretched (piano-like inharmonicity). |
| **Scatter** | 0.0-1.0 | Randomizes per-mode decay rates. 0.0 = uniform decay, 1.0 = chaotic, with some modes ringing much longer than others. |

### Waveguide Controls

Available when Resonance Type is set to **Waveguide**.

| Control | Range | Description |
|---------|-------|-------------|
| **Stiffness** | 0.0-1.0 | String stiffness. 0.0 = ideal flexible string. Higher values add inharmonic dispersion (higher partials go slightly sharp), like a real piano or metallic string. |
| **Pick Position** | 0.0-1.0 | Pluck or excitation point on the string. Affects which harmonics are present. 0.0 = bridge (all harmonics), 0.13 = typical guitar position, 0.5 = center (suppresses even harmonics). |

### Body Resonance

Post-processor applied after either resonator type. Simulates the resonant body (soundboard, cabinet, shell) of an acoustic instrument.

| Control | Range | Description |
|---------|-------|-------------|
| **Size** | 0.0-1.0 | Body size. Small values = compact, bright body (ukulele, snare). Large values = spacious, deep body (cello, concert grand). Default 0.5. |
| **Material** | 0.0-1.0 | Body material character. 0.0 = dense, metallic. 0.5 = wood (warm, balanced). 1.0 = thin, resonant membrane. Default 0.5. |
| **Mix** | 0.0-1.0 | Body resonance contribution. 0.0 = no body (dry resonator output), 1.0 = full body processing. Default 0.0 (off). |

### Physical Model Mix

| Control | Range | Description |
|---------|-------|-------------|
| **Phys Model Mix** | 0.0-1.0 | Blends between the standard residual path (0.0) and the physical model resonator path (1.0). At 0.0, the output is identical to pre-physical-model behavior. At 1.0, the residual is entirely replaced by the resonator output. Default 0.0. |

---

## Sympathetic Resonance

The Sympathetic Resonance module adds cross-voice resonance that simulates the way untouched strings vibrate in sympathy with played notes (like an open piano or sitar). It operates post-voice-sum, before the master gain.

| Control | Range | Description |
|---------|-------|-------------|
| **Amount** | 0.0-1.0 | Sympathetic string activation level. 0.0 = off, 0.5 = subtle ringing, 1.0 = prominent resonance. When a MIDI note is played, the resonator pool is populated with modes matching the note's partials. Default 0.0. |
| **Decay** | 0.0-1.0 | How long sympathetic resonances ring after the excitation ends. 0.0 = very short, 0.5 = moderate (natural), 1.0 = very long tail. Default 0.5. |

---

## Harmonic Physics

The Harmonic Physics section makes harmonics behave like a coupled physical system rather than independent sine waves. These four parameters add organic, instrument-like character to the resynthesis. They operate on the harmonic model *before* oscillator synthesis.

| Control | Range | Description |
|---------|-------|-------------|
| **Warmth** | 0.0-1.0 | Soft saturation applied to partial amplitudes. Compresses dominant partials and relatively boosts quiet ones. 0.0 = linear (clean), 0.5 = gentle warmth with 2-3 dB compression on peaks, 1.0 = pronounced saturation with a vintage tube-like character. Output energy is guaranteed not to exceed input. Default 0.0. |
| **Coupling** | 0.0-1.0 | Nearest-neighbor energy sharing between adjacent harmonics. Creates spectral viscosity where partials influence each other. 0.0 = independent partials, 0.5 = moderate smoothing, 1.0 = strong coupling where harmonics blend together and lose individual clarity. Energy is conserved within 0.001%. Default 0.0. |
| **Stability** | 0.0-1.0 | Inertia for partial amplitude changes. 0.0 = responsive (tracks input instantly), 0.5 = moderate damping (smooths out rapid changes), 1.0 = extreme lag where old amplitudes are heavily preserved. Reinforces persistent partials and resists transient noise. Default 0.0. |
| **Entropy** | 0.0-1.0 | Natural decay rate of partials. 0.0 = partials sustain indefinitely, 0.5 = gentle decay over approximately 10 analysis frames, 1.0 = rapid decay where ghost partials vanish quickly. Useful for clearing transient noise that doesn't persist in the input. Default 0.0. |

---

## Analysis Feedback

The Analysis Feedback loop feeds the synthesizer's output back into its own analysis pipeline, creating emergent, self-evolving harmonic behavior. This is available in sidechain mode only.

| Control | Range | Description |
|---------|-------|-------------|
| **Feedback Amount** | 0.0-1.0 | Mix ratio of synth output fed back into the analysis input. 0.0 = normal operation, 0.3-0.5 = gentle self-reinforcement (resonant, rich), 1.0 = full feedback (can create chaotic states). A built-in soft limiter prevents clipping. Default 0.0. |
| **Feedback Decay** | 0.0-1.0 | Exponential entropy leak in the feedback buffer. 0.0 = no decay (feedback persists), 0.2 = moderate decay (stale signal fades over ~10 seconds), 1.0 = rapid decay (only recent feedback survives, ~1 second memory). Default 0.2. |

### Safety

The feedback loop includes a multi-layer safety stack: soft limiter, energy budget normalization, hard clamp, confidence gate, and decay. It is automatically bypassed when Freeze is engaged, and the feedback buffer is cleared when Freeze is disengaged to prevent stale contamination.

---

## ADSR Envelope

Innexus auto-detects the amplitude envelope of loaded samples and allows real-time editing. The envelope shapes the synthesized output, making it respond dynamically to note events rather than producing a flat sustain.

### Envelope Display

The visual graph shows the detected or edited envelope shape with interactive drag points. A playback dot tracks the current envelope stage in real time. Click the **Expand** button for a full-window overlay with detailed envelope editing.

### Envelope Parameters

| Control | Range | Description |
|---------|-------|-------------|
| **Attack** | 1-5000 ms | Time from note-on to peak amplitude. Auto-detected from the sample's leading edge. 1 ms = percussive click, 10 ms = standard, 500 ms = smooth pad entry. Default 10 ms. |
| **Decay** | 1-5000 ms | Time from peak to sustain level. Detected from the sample's post-attack settling. Default 100 ms. |
| **Sustain** | 0.0-1.0 | Level held while the MIDI note is pressed. 1.0 = full amplitude, 0.0 = silence after decay. Default 1.0. |
| **Release** | 1-5000 ms | Time from note-off to silence. Detected from the sample's tail decay. Default 100 ms. |
| **Amount** | 0.0-1.0 | Blend between the detected envelope (1.0) and no envelope shaping (0.0). Automatically set when envelope detection runs on sample load. Default 0.0 (no shaping). |

### Shaping Controls

| Control | Range | Description |
|---------|-------|-------------|
| **Time Scale** | 0.25-4.0x | Multiplies all time parameters uniformly. 0.25x = 4x faster (snappier), 1.0x = original, 4.0x = 4x slower (more spacious). Default 1.0x. |
| **Attack Curve** | -1.0 to +1.0 | Shape of the attack ramp. -1.0 = exponential (slow start, fast finish), 0.0 = linear, +1.0 = logarithmic (fast start, slow finish). |
| **Decay Curve** | -1.0 to +1.0 | Shape of the decay from peak to sustain. Same curve range as attack. |
| **Release Curve** | -1.0 to +1.0 | Shape of the release from sustain to silence. Same curve range as attack. |

---

## Voice Mode

| Control | Range | Description |
|---------|-------|-------------|
| **Voice Mode** | 3 modes | **Mono** = single voice (default). **4 Voices** = 4-voice polyphony. **8 Voices** = 8-voice polyphony. Higher voice counts increase CPU usage proportionally. Automatic 1/sqrt(N) gain compensation prevents clipping when multiple voices sound simultaneously. |

### MPE Support

In polyphonic mode, Innexus supports MIDI Polyphonic Expression (MPE) via VST3 Note Expression:

| Expression | Range | Description |
|------------|-------|-------------|
| **Tuning** | -1.0 to +1.0 | Per-note pitch bend in semitones |
| **Volume** | 0.0-1.0 | Per-note volume (0.25 normalized = unity gain) |
| **Pan** | 0.0-1.0 | Per-note stereo position (0.5 = center) |
| **Brightness** | 0.0-1.0 | Per-note harmonic/residual balance (0.5 = neutral, 0.0 = harmonics boosted, 1.0 = residual boosted) |

---

## Tips & Techniques

### From Sample to Playable Instrument

The most basic Innexus workflow: load a sample, play MIDI. The plugin uses the sample's timbre as the "DNA" for your synthesized sound. Short samples work as well as long ones — Innexus analyzes the harmonic content, not the waveform directly. A single piano chord can become a rich, evolving pad.

### Freeze as a Creative Tool

Freeze isn't just for "pausing" — it's a performance tool. Engage Freeze to lock in a particular harmonic moment, then use Morph to blend between that frozen state and whatever the live input is doing. This creates a tension-and-release dynamic where the timbre alternates between a known reference point and live variation.

### Memory Slot Workflows

- **Performance snapshots**: Capture 4-5 different timbral states before a performance, then recall them on the fly during playback
- **Evolution fuel**: Populate all 8 slots with contrasting timbres (bright attack, dark sustain, noise-heavy, pure tone, etc.), then enable Evolution for endlessly shifting textures
- **Cross-synthesis palette**: Use Multi-Source Blend with weighted slots to create timbres that don't exist in any single source

### Building Physical Instruments

The Exciter + Resonator Bank + Body Resonance chain turns Innexus into a physically modelled instrument:

1. Set **Exciter Type** to Impact for percussion, or Bow for sustained tones
2. Choose **Modal** for bell/marimba-like resonance, or **Waveguide** for string-like character
3. Dial in **Body Resonance** (Size + Material + Mix) to add acoustic body character
4. Use **Physical Model Mix** to blend between the original residual and the resonator output
5. Adjust **Resonance Decay** and **Brightness** to shape the ring-out character

### Feedback for Self-Evolving Textures

The Analysis Feedback loop is unique to Innexus. Unlike a delay or reverb, it creates a self-referential system where the synth's own output influences what it "hears" and therefore what it produces. At low amounts (0.1-0.3), this creates subtle resonant reinforcement. At higher amounts, the system can enter attractor states — stable-but-complex patterns that evolve organically.

### Harmonic Physics for Realism

Real acoustic instruments have nonlinear, coupled vibrating systems — strings influence each other, resonant bodies add warmth, energy dissipates naturally. The Harmonic Physics section approximates these behaviors:

- **Warmth** acts like a resonant body (soft saturation)
- **Coupling** acts like sympathetic string resonance (energy sharing)
- **Stability** acts like physical mass (inertia, resistance to change)
- **Entropy** acts like air damping (natural decay)

### Modulators for Animation

Use the two modulators to add life to static timbres. Some effective setups:

- **Slow amplitude mod on low partials** (Rate 0.05 Hz, Depth 0.3, Range 1-8, Target Amplitude) — subtle breathing effect
- **Medium frequency mod on high partials** (Rate 2 Hz, Depth 0.2, Range 32-96, Target Frequency) — shimmer and sparkle
- **Tempo-synced pan mod** (Rate Sync on, 1/8 note, Depth 0.5, Full range, Target Pan) — rhythmic stereo movement

### CPU Management

If CPU usage is high:
- Reduce **Voice Mode** from 8 Voices to 4 or Mono
- Reduce **Partial Count** from 96 to 48 — the difference is often subtle
- Disable unused **Modulators**
- Turn off **Evolution Engine** when not needed
- Set **Physical Model Mix** to 0 and **Body Mix** to 0 if not using physical modelling
- Disable **Bow Oversampling** unless needed
- Reduce **Feedback Amount** to 0 when not using the feedback loop
- Reduce **Sympathetic Amount** to 0 when not needed
- In sidechain mode, use **Low Latency** mode unless you need bass precision

### Drag and Drop

Drag .wav or .aiff files directly onto the Innexus window. The plugin provides visual feedback during the drag operation and begins analysis immediately after the drop. No file dialogs needed.
