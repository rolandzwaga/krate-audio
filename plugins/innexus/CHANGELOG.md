# Changelog

All notable changes to Innexus will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.9.3] - 2026-03-07

### Added

- **Preset browser** — Browse and load factory presets via shared `PresetBrowserView` overlay with tabbed categories and search; buttons placed side-by-side in new Presets fieldset container next to Detune
- **Preset save dialog** — Save user presets via shared `SavePresetDialogView` with name input and category selection
- **35 factory presets** — Organized by input signal type across 7 categories: Voice (5), Strings (5), Keys (5), Brass and Winds (5), Drums and Perc (5), Pads and Drones (5), Found Sound (5)
- **Preset generator tool** — `tools/innexus_preset_generator.cpp` generates `.vstpreset` files matching the v8 state format; `generate_innexus_presets` CMake target
- **Full v8 state loading** — `loadComponentStateWithNotify()` parses the complete binary state format (v1–v8), skipping sample path/residual frames/memory slots, and applies all parameters via `beginEdit/performEdit/endEdit` for proper host notification

## [0.9.2] - 2026-03-07

### Fixed

- **Entropy volume drop** — Opening the Entropy knob caused immediate volume loss on steady-state harmonics (~27% at 0.3, ~46% at 0.5); entropy decay was applied to all partial amplitude every frame, even partials actively reinforced by input; now only decays the excess agent amplitude above input, preserving reinforced partials while still decaying ghost partials that lose input backing

## [0.9.1] - 2026-03-07

### Added

- **Drag-and-drop sample loading** — Drag `.wav`, `.aiff`, or `.aif` files directly onto the plugin window to load them; uses VSTGUI's cross-platform `IDropTarget` mechanism with a transparent full-window overlay that passes through all mouse events
- **Drop feedback overlay** — Visual feedback during drag: cyan border + "Drop to load sample" for supported files, red border + "Unsupported format" for invalid files; overlay is invisible when not dragging

## [0.9.0] - 2026-03-06

### Added

- **Analysis Feedback Loop** — Self-evolving timbral system that feeds the synth's output back into its own analysis pipeline, creating emergent harmonic behavior
- **Feedback Amount parameter** (ID 710) — Controls the mix ratio of synth output into the analysis input (0.0–1.0, default 0.0); at 0.0 the signal flow is bit-identical to pre-feedback behavior (SC-001); at higher values, harmonics self-reinforce creating resonant attractor states
- **Feedback Decay parameter** (ID 711) — Per-block exponential entropy leak in the feedback buffer (0.0–1.0, default 0.2); formula `exp(-decay * blockSize / sampleRate)` provides time-consistent decay independent of block size and sample rate
- **Soft limiter** — Per-sample `tanh(fb * amount * 2.0) * 0.5` bounds feedback signal to [-0.5, +0.5] before mixing with sidechain input
- **5-layer safety stack** — Soft limiter (new) + energy budget normalization (existing) + hard output clamp at 2.0f (existing) + confidence gate auto-freeze (existing) + feedback decay (new); guarantees bounded output under all parameter combinations
- **Freeze interaction** — Feedback path automatically bypassed when freeze is engaged (FR-015); feedback buffer cleared to zeros on freeze disengage to prevent stale audio contamination (FR-016)
- **Sample mode bypass** — Feedback is gated to sidechain mode only; in sample mode, FeedbackAmount has no effect (FR-014)
- **State version 8** — Persists both feedback parameters with backward compatibility for v1–v7 presets (defaults to FeedbackAmount=0.0, FeedbackDecay=0.2 on older state load)

### Performance

- Feedback=0.0: bit-identical output, no regression (SC-001)
- Feedback=1.0 with silent input: converges, no interval exceeds t=0 RMS by >3dB over 10s (SC-002)
- Decay=1.0: reaches -60dBFS silence within 10 seconds; decay=0.5: within 60 seconds (SC-003)
- Output never exceeds kOutputClamp (2.0f) under any parameter combination (SC-004)
- Freeze bypass verified within same process() call (SC-005)
- Feedback buffer zeroed on freeze disengage (SC-006)
- Sample mode: bit-identical regardless of FeedbackAmount (SC-007)
- CPU overhead: <1% additional over baseline (SC-008)

## [0.8.0] - 2026-03-06

### Added

- **Harmonic Physics** — Physics-based harmonic processing system making the harmonic model behave like a physical vibrating body rather than independent sine waves
- **Warmth** (FR-001–FR-005) — Soft saturation via `tanh(drive * amp) / tanh(drive)` that compresses dominant partials and relatively boosts quiet ones; drive scales exponentially with parameter; output RMS guaranteed ≤ input RMS via energy normalization; bit-exact bypass at 0.0
- **Harmonic Coupling** (FR-006–FR-011) — Nearest-neighbor energy sharing between harmonics creating spectral viscosity; sum-of-squares energy conservation within 0.001%; safe boundary handling; only amplitudes modified (frequencies/phases untouched); bit-exact bypass at 0.0
- **Harmonic Dynamics Agent System** (FR-012–FR-019) — Per-partial stateful processing with `AgentState` (amplitude, velocity, persistence, energyShare arrays); stability controls inertia weighted by persistence; entropy controls natural decay rate; persistence grows for stable partials and decays for dramatic changes; energy budget normalization prevents runaway; first-frame initialization avoids ramp-from-zero artifacts; bit-exact bypass at stability=0, entropy=0
- **HarmonicPhysics class** — Unified header-only class (`plugins/innexus/src/dsp/harmonic_physics.h`) with processing chain: Coupling → Warmth → Dynamics; called before every `oscillatorBank_.loadFrame()` site (7 call sites)
- **4 new parameters** (IDs 700–703) — Warmth, Coupling, Stability, Entropy; all range [0.0, 1.0], default 0.0, 5ms one-pole smoothing
- **State version 7** — Persists all 4 harmonic physics parameters with backward compatibility for v1–v6 presets (defaults to 0.0 on older state load)

### Performance

- Combined CPU overhead: 0.011% of one core at 48kHz/512-hop with 48 partials (SC-006, budget: <0.5%)
- Bypass bit-exact at all param=0.0 (SC-001)
- Energy conservation within 0.001% for coupling (SC-002)
- Peak-to-average ratio reduction ≥50% for warmth (SC-003)
- Stability inertia <5% change on sudden input shift (SC-004)
- Entropy decay to <1% within 10 frames (SC-005)
- Pluginval passes at strictness level 5 (SC-008)

## [0.7.0] - 2026-03-06

### Added

- **M7: Plugin UI** — Full VSTGUI interface with custom views, display data pipeline, and modulator sub-controllers
- **HarmonicDisplayView** — Real-time harmonic spectrum visualization showing per-partial amplitudes as vertical bars
- **ConfidenceIndicatorView** — F0 confidence meter with color-coded thresholds (green/yellow/red)
- **MemorySlotStatusView** — 8-slot grid showing occupied/empty/selected state for harmonic memory
- **EvolutionPositionView** — Visual indicator of evolution engine's current interpolation position across memory slots
- **ModulatorActivityView** — Per-modulator activity display showing LFO phase and depth
- **ModulatorSubController** — VSTGUI sub-controller enabling per-modulator template instantiation with tag remapping
- **DisplayData pipeline** (FR-048, FR-049) — Timer-driven IMessage updates from processor to controller at ~30 Hz for real-time UI feedback
- **Sample Load button** — Custom CView-based file selector (WAV/AIFF) following cross-platform OutlineButton pattern; replaces broken CTextButton approach
- **Sample filename display** — Shows loaded sample name in the UI, persisted across editor open/close

### Fixed

- **Sample Load button crash** — CTextButton with unregistered control-tag (998) caused heap corruption via VSTGUI's ParameterChangeListener system; replaced with tag-free custom CView
- **Residual synthesizer output level** — Residual noise was ~600,000x louder than harmonic oscillator, drowning out pitched content; two issues fixed:
  - Spectral envelope (band energies) were raw FFT magnitudes used as amplitude multipliers; now normalized to unit RMS for shape-only control
  - `totalEnergy` was at FFT-domain scale; now divided by `fftSize` to convert to time-domain amplitude via Parseval's theorem
- **Residual analyzer harmonic subtraction** — L2-normalized partial amplitudes scaled by `globalAmplitude` (RMS) for better time-domain amplitude matching during harmonic subtraction

### Added (Tests)

- **E2E sample load tests** — 5 end-to-end tests verifying the full WAV file -> SampleAnalyzer -> Processor -> pitched audio pipeline:
  - SampleAnalyzer extracts f0 from sine WAV
  - Resynthesized output matches MIDI note pitch
  - Different MIDI notes produce different pitches (tuning verification)
  - Multi-harmonic WAV preserves harmonic structure
  - Output is tonal, not noise (spectral peak dominance)
- Goertzel frequency detection and minimal WAV file writer test helpers

## [0.6.0] - 2026-03-06

### Added

- **M6: Creative Extensions** — Five new creative features building on the M1–M5 harmonic analysis/synthesis foundation
- **Cross-Synthesis Timbral Blend** (FR-001–FR-005) — Continuous 0.0–1.0 blend between a pure harmonic series (1/n rolloff, L2-normalized) and the analyzed source model; enables carrier-modulator performance with real-time source switching and click-free crossfade
- **Stereo Partial Spread** (FR-006–FR-013) — Per-partial stereo distribution with odd partials panning left, even partials right; constant-power pan law; fundamental at 25% reduced spread; residual center-panned; mono output bus support
- **Detune Spread** (FR-030–FR-032) — Per-partial frequency offset scaling with harmonic number for chorus-like richness; odd partials detune positive, even negative; fundamental excluded (<1 cent deviation)
- **Evolution Engine** (FR-014–FR-023) — Autonomous timbral drift through occupied memory slot waypoints with Cycle, PingPong, and Random Walk modes; global phase (not per-note); manual morph offset coexistence clamped to [0,1]; smoothed speed/depth
- **Harmonic Modulators** (FR-024–FR-029, FR-033) — Two independent LFO-driven per-partial modulators with 5 waveforms (Sine, Triangle, Square, Saw, Random S&H); targets amplitude (multiplicative unipolar), frequency (additive cents), or pan (offset bipolar); configurable partial range; overlapping ranges multiply amplitude / add frequency and pan; free-running phase initialized at prepare(), never reset on MIDI
- **Multi-Source Blending** (FR-034–FR-042) — Normalized weighted sum across up to 8 memory slots plus 1 live source; empty slots contribute zero; all-zero weights produce silence; blend overrides both normal recall/freeze and evolution when enabled
- **`processStereo()` API** (KrateDSP Layer 2) — New stereo output method on `HarmonicOscillatorBank` alongside existing mono `process()`; adds `setStereoSpread()`, `setDetuneSpread()`, `applyPanOffsets()`, `applyExternalFrequencyMultipliers()`
- **EvolutionEngine** (plugin-local DSP) — `plugins/innexus/src/dsp/evolution_engine.h`; uses `lerpHarmonicFrame`/`lerpResidualFrame` for interpolation; Xorshift32 RNG for Random Walk
- **HarmonicModulator** (plugin-local DSP) — `plugins/innexus/src/dsp/harmonic_modulator.h`; formula-based LFO (no wavetable, no heap); Xorshift32 for S&H
- **HarmonicBlender** (plugin-local DSP) — `plugins/innexus/src/dsp/harmonic_blender.h`; weight normalization per R-006
- **31 new parameters** (IDs 600–649) — Timbral Blend, Stereo Spread, Detune Spread, Evolution (enable/speed/depth/mode), Mod 1 & 2 (enable/waveform/rate/depth/range/target), Blend (enable/8 slot weights/live weight)
- **State version 6** — Persists all 31 M6 parameters with backward compatibility for v1–v5 presets (M6 params initialize to spec defaults on older state load)
- **Pipeline order** (FR-049) — Blend/cross-synthesis → Evolution → Harmonic Filter → Modulators → Oscillator bank

### Performance

- Stereo spread=0: bit-identical L/R output (SC-010)
- Stereo spread=1.0: inter-channel decorrelation >0.8 (SC-002)
- Timbral blend=1.0: correlation with source >0.95 (SC-001)
- Evolution: spectral centroid std deviation 214 Hz over 10s (SC-003, threshold >100 Hz)
- Modulator depth: within ±5% of configured value at 2 Hz (SC-004)
- Detune spread: fundamental pitch deviation <1 cent (SC-005)
- Blended centroid: within ±10% of arithmetic mean of sources (SC-006)
- Parameter sweeps: no discontinuities above -80 dBFS (SC-007)
- CPU overhead: <1% additional for all M6 features combined (SC-008)
- State round-trip: 1e-6 tolerance (SC-009)
- Single-source blend identical to direct recall (SC-011)

## [0.5.0] - 2026-03-05

### Added

- **M5: Harmonic Memory** — Snapshot capture & recall system with 8 memory slots for storing and recalling harmonic timbres via MIDI
- **HarmonicSnapshot** (KrateDSP Layer 2) — Core data structure storing L2-normalized amplitudes, relative frequencies, inharmonic deviations, phases, residual band energies, and metadata (f0, spectral centroid, brightness, global amplitude)
- **Memory Slot parameter** — 8-slot selector (Slot 1–8) for targeting capture and recall operations
- **Memory Capture trigger** — One-shot toggle captures current harmonic state into the selected slot with automatic source detection: post-morph blend (freeze+morph>0), frozen frame (freeze active), live sidechain analysis, sample analysis frame, or empty snapshot (no source)
- **Memory Recall trigger** — One-shot toggle recalls the selected slot's snapshot into the oscillator bank with crossfade to frozen state; automatically engages freeze; click-free transition (SC-001: <-60 dB)
- **Recall-to-freeze integration** — Recalled snapshots are loaded as the frozen frame, enabling immediate morph blending between recalled timbre and live analysis (FR-013, FR-014)
- **State version 5** — Binary persistence of all 8 memory slots with per-slot occupied flags and full snapshot data; backward compatible with v1–v4 presets
- **JSON export/import** — Human-readable snapshot serialization with full validation (field presence, range checks, array lengths); import via IMessage controller→processor pipeline (FR-025, FR-026)
- **Edge case robustness** — Recall from empty slot (no-op), capture with no analysis (stores empty snapshot), rapid capture/recall cycling, slot switching during playback

### Performance

- Recall crossfade: max step <-60 dB relative to RMS (SC-001)
- Capture/recall latency: <1 process block (SC-003)
- JSON round-trip: lossless to float precision (SC-006)
- Memory overhead: <64 KB for 8 slots (SC-005)

## [0.4.0] - 2026-03-05

### Added

- **M4: Musical Control Layer** — Four new expressive parameters for real-time harmonic manipulation (Freeze, Morph, Harmonic Filter, Responsiveness)
- **Harmonic Freeze** — Manual toggle captures current HarmonicFrame and ResidualFrame as a frozen timbral snapshot; oscillator bank plays from frozen state until disengaged; ≤10ms linear crossfade on disengage with no audible click (SC-001: <-60 dB); manual freeze overrides auto-freeze (FR-007); frozen state preserved across analysis source switches (FR-008)
- **Morph Position** — Continuous 0.0–1.0 blend between frozen (State A) and live (State B) harmonic states; per-partial amplitude interpolation, relative frequency interpolation, residual band energy interpolation; handles unequal partial counts by fading missing partials to zero (FR-015); 7ms one-pole smoother for click-free automation (FR-017); no effect without freeze engaged (FR-016)
- **Harmonic Filter Type** — Five timbral sculpting presets applied as per-partial amplitude masks after morph, before oscillator bank: All-Pass (identity), Odd Only, Even Only, Low Harmonics (clamp(8/n,0,1) rolloff), High Harmonics (fundamental attenuated ≥18 dB); does not affect residual (FR-027)
- **Responsiveness** — 0.0–1.0 control forwarded to LiveAnalysisPipeline's HarmonicModelBuilder dual-timescale blend; default 0.5 produces identical behavior to M1/M3 default (SC-008); takes effect within one process block (FR-031); no effect in sample mode (FR-032)
- **HarmonicFrameUtils** (KrateDSP Layer 2) — Four inline DSP utilities: `lerpHarmonicFrame`, `lerpResidualFrame`, `computeHarmonicMask`, `applyHarmonicMask`; real-time safe, header-only, handles unequal partial counts
- **State version 4** — Persists all four M4 parameters (freeze, morph position, filter type, responsiveness) with backward compatibility for v1/v2/v3 presets
- **Edge case robustness** — Freeze with no analysis loaded, morph with zero partials, rapid filter toggling during sustained notes, freeze during confidence-gate recovery, morph with very different F0 values (100 Hz vs 1000 Hz)

### Performance

- M4 processing overhead: negligible (<0.1% CPU at 44.1 kHz, SC-007)
- Morph sweep: max frame-to-frame delta 2.07e-06 dB (SC-003)
- Odd/Even filter attenuation: ∞ dB (exact zero mask, exceeds SC-005/SC-006 -60 dB requirement)

## [0.3.1] - 2026-03-05

### Added

- **Update checker backend** — Async version checking against hosted endpoint with 24h cooldown and state persistence (UI integration pending)

## [0.3.0] - 2026-03-04

### Added

- **M3: Live Sidechain Mode** — Real-time continuous analysis from sidechain audio input, enabling performers to route live audio and play MIDI keys that inherit the live source's timbral character
- **Sidechain audio input bus** — Stereo auxiliary input (kAux), automatically downmixed to mono for analysis
- **Input Source parameter** — Switch between Sample and Sidechain modes with 20ms click-free crossfade
- **Latency Mode parameter** — Low Latency (11.6ms, short STFT only) or High Precision (dual-window STFT, detects fundamentals down to 40 Hz)
- **LiveAnalysisPipeline** — Real-time orchestration of PreProcessing, YIN, STFT, PartialTracker, HarmonicModelBuilder, and SpectralCoringEstimator with zero audio-thread allocations
- **SpectralCoringEstimator** (KrateDSP Layer 2) — Lightweight residual estimation by zeroing harmonic bins and measuring inter-harmonic energy, adding zero additional analysis latency
- **Confidence-gated freeze in sidechain mode** — Reuses existing freeze mechanism; activates within 2 STFT hops of silence, recovers with crossfade when signal returns
- **Residual synthesis in sidechain mode** — Existing controls (Harmonic Level, Residual Level, Brightness, Transient Emphasis) work identically in both sample and sidechain modes
- **State version 3** — Persists Input Source and Latency Mode parameters with backward compatibility for v1/v2 presets
- **Early-out optimizations** — Silent sidechain skip (~95% CPU reduction when idle), residual skip when Residual Level is zero (~10% reduction)

### Performance

- Analysis pipeline: < 5% single-core CPU at 44.1 kHz (SC-003)
- Analysis + synthesis: < 8% single-core CPU at 44.1 kHz (SC-004)
- Analysis latency: 11.6ms hop + < 1ms processing overhead (SC-001)
- Source crossfade: < -60 dB discontinuity (SC-005)

## [0.2.0] - 2026-03-04

### Added

- **M1: Core Playable Instrument** — Complete analysis-synthesis pipeline for harmonic resynthesis of audio samples, playable via MIDI
- **Pre-processing pipeline** — DC offset removal (IIR estimator with first-sample init), 4th-order Butterworth HPF at 30 Hz, noise gate (-60 dB default), transient suppression (10:1 ratio)
- **YIN pitch detector** — FFT-accelerated autocorrelation (O(N log N)), CMNDF with parabolic interpolation, confidence gating (0.3 threshold), 2% frequency hysteresis, hold-previous on low confidence
- **Dual-window STFT analysis** — Long window (4096/hop 2048) for frequency resolution, short window (1024/hop 512) for temporal resolution, both BlackmanHarris
- **Partial tracker** — Spectral peak detection with parabolic interpolation, harmonic sieve (sqrt(n) tolerance), frame-to-frame matching by frequency proximity, birth/death with 4-frame grace period, 48-partial hard cap ranked by energy * stability
- **Harmonic model builder** — L2 amplitude normalization, dual-timescale blending (5ms fast / 100ms slow), 5-frame median filter for impulse rejection, spectral centroid/brightness/noisiness descriptors, global amplitude tracking (10ms smoother)
- **48-oscillator harmonic oscillator bank** — Gordon-Smith MCF (Modified Coupled Form), SoA layout with 32-byte alignment, anti-aliasing soft rolloff near Nyquist, phase-continuous frequency updates, 3ms crossfade on pitch jumps > 1 semitone, per-partial amplitude smoothing (~2ms), configurable inharmonicity (0-100%)
- **Sample loading** — WAV/AIFF support via dr_wav, stereo-to-mono downmix, background analysis thread with atomic pointer swap (lock-free, zero audio-thread allocations)
- **MIDI integration** — Note-on/off with monophonic last-note-priority, velocity scaling (global, not per-partial), pitch bend (+/-12 semitones), exponential release envelope with 20ms anti-click minimum, confidence-gated freeze with 7ms recovery crossfade
- **State persistence** — Versioned binary state saves/restores all parameters and sample file path, triggers re-analysis on session reload
- **Plugin parameters** — Release Time (20-5000ms, exponential mapping), Inharmonicity Amount (0-100%)

### Performance

- Oscillator bank: 0.28% CPU at 44.1 kHz stereo, 48 partials (target: < 0.5%)
- Full plugin: < 5% CPU at 44.1 kHz stereo
- Analysis: ~250ms for 10s mono file (target: < 10s)
- YIN pitch error: 0.0% gross error rate (target: < 2%)

## [0.1.0] - 2026-03-03

### Added

- Initial plugin skeleton — VST3 entry point, processor, controller
