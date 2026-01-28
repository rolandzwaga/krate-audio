# Disrumpo - Task Breakdown

**Related Documents:**
- [specs-overview.md](specs-overview.md) - Core requirements specification
- [plans-overview.md](plans-overview.md) - System architecture and development roadmap
- [ui-mockups.md](ui-mockups.md) - Detailed UI specifications
- [dsp-details.md](dsp-details.md) - DSP implementation details
- [custom-controls.md](custom-controls.md) - Custom VSTGUI control specifications

---

## Phase 1: Foundation (Weeks 1-4)

### Week 1: Project Setup

- [ ] **T1.1** Create CMake project structure for Disrumpo plugin
  - Setup `plugins/Disrumpo/CMakeLists.txt`
  - Configure VST3 SDK and VSTGUI dependencies
  - Add to monorepo build

- [ ] **T1.2** Implement parameter ID encoding system
  - Create `plugin_ids.h` with `ParameterID` enum
  - Implement `makeBandParamId()` helper
  - Implement `makeNodeParamId()` helper
  - Add `BandParamType` and `NodeParamType` enums

- [ ] **T1.3** Create basic Processor skeleton
  - Implement `Processor` class inheriting from `AudioEffect`
  - Setup audio bus configuration (stereo in/out)
  - Implement empty `process()` method
  - Add state serialization (`getState`/`setState`)

- [ ] **T1.4** Create basic Controller skeleton
  - Implement `Controller` class inheriting from `EditControllerEx1`
  - Register global parameters
  - Implement `createView()` (placeholder)

- [ ] **T1.5** Verify plugin loads in DAW
  - Test in Reaper
  - Test in Ableton Live
  - Verify parameter automation works

**Milestone M1: Plugin loads in DAW** ✓

### Week 2: Band Management

- [ ] **T2.1** Integrate `CrossoverLR4` from KrateDSP
  - Create `CrossoverNetwork` wrapper class
  - Support dynamic band count (1-8)
  - Implement cascaded band splitting

- [ ] **T2.2** Implement per-band routing
  - Create `BandState` structure
  - Implement band gain/pan processing
  - Add solo/bypass/mute logic

- [ ] **T2.3** Implement band summation
  - Verify phase-coherent recombination
  - Test flat frequency response (±0.1 dB)

- [ ] **T2.4** Add band count parameter
  - Dynamic crossover frequency redistribution
  - Preserve crossover positions on band count change when possible

### Week 3: Distortion Integration

- [ ] **T3.1** Create `DistortionAdapter` class
  - Unified interface for all 26 distortion types
  - Map distortion type enum to KrateDSP processors
  - Implement `setType()` and `setParams()`

- [ ] **T3.2** Integrate basic saturation types (D01-D06)
  - Soft Clip via `SaturationProcessor::Tape`
  - Hard Clip via `SaturationProcessor::Digital`
  - Tube via `TubeStage`
  - Tape via `TapeSaturator`
  - Fuzz via `FuzzProcessor`

- [ ] **T3.3** Integrate wavefold types (D07-D09)
  - Sine Fold via `WavefolderProcessor::Serge`
  - Triangle Fold via `WavefolderProcessor::Simple`
  - Serge Fold via `WavefolderProcessor::Lockhart`

- [ ] **T3.4** Integrate digital types (D12-D14, D18-D19)
  - Bitcrush via `BitcrusherProcessor`
  - Sample Reduce via `SampleRateReducer`
  - Aliasing via `AliasingEffect`
  - Bitwise Mangler via `BitwiseMangler`

- [ ] **T3.5** Integrate oversampling
  - Add `Oversampler` per band
  - Implement intelligent factor selection based on distortion type

- [ ] **T3.6** Wire distortion to bands
  - Each band processes through its distortion adapter
  - Common parameters (Drive, Mix, Tone) functional

**Milestone M2: Working multiband distortion** ✓

### Week 4: Basic UI

- [ ] **T4.1** Setup VSTGUI framework
  - Create `editor.uidesc` base structure
  - Define color scheme per spec
  - Setup font styles

- [ ] **T4.2** Implement spectrum display (placeholder)
  - Create custom view class
  - Draw band regions with dividers
  - Make dividers draggable (crossover adjustment)

- [ ] **T4.3** Create band strip component
  - Distortion type dropdown
  - Drive knob
  - Mix knob
  - Solo/Bypass/Mute toggles

- [ ] **T4.4** Implement global controls section
  - Input/Output gain knobs
  - Global mix knob
  - Band count selector

- [ ] **T4.5** Implement preset selector (basic)
  - Dropdown with preset names
  - Previous/Next buttons
  - Save button

**Milestone M3: Level 1 UI functional** ✓

---

## Phase 2: Core Features (Weeks 5-10)

### Week 5-6: Morph System

- [ ] **T5.1** Implement `MorphNode` data structure
  - Node ID, type, parameters
  - Position in morph space (X, Y)

- [ ] **T5.2** Implement `MorphEngine` - weight computation
  - Inverse distance weighting algorithm
  - Support 2-4 active nodes
  - Handle edge cases (node at cursor position)

- [ ] **T5.3** Implement 1D Linear morph mode
  - Single axis interpolation A↔B↔C↔D
  - Smooth parameter transitions

- [ ] **T5.4** Implement 2D Planar morph mode
  - XY position in node space
  - Bilinear interpolation between 4 nodes

- [ ] **T5.5** Implement parameter interpolation
  - Same-family types: interpolate params directly
  - Cross-family types: parallel processing + crossfade

- [ ] **T5.6** Implement morph smoothing
  - Configurable smoothing time (0-500ms)
  - Artifact-free transitions at all speeds

- [ ] **T5.7** Add morph position parameters
  - Morph X per band
  - Morph Y per band
  - Morph mode per band

**Milestone M4: 2D morph system** ✓

### Week 7: Morph UI

- [ ] **T7.1** Create XY morph pad control
  - Custom VSTGUI view
  - Draw node positions
  - Cursor position indicator
  - Drag to move cursor

- [ ] **T7.2** Implement node visualization
  - Color-coded by distortion type
  - Size indicates weight/influence
  - Alt+drag to reposition nodes

- [ ] **T7.3** Create node editor panel
  - Type selector dropdown
  - Type-specific parameters
  - Add/remove node buttons

- [ ] **T7.4** Add morph mode selector
  - 1D Linear / 2D Planar / 2D Radial

### Week 8: Sweep System

- [ ] **T8.1** Implement `SweepProcessor` class
  - Center frequency parameter
  - Width parameter (octaves)
  - Intensity parameter

- [ ] **T8.2** Implement Gaussian intensity distribution
  - Calculate intensity multiplier per band
  - Smooth falloff at band boundaries

- [ ] **T8.3** Implement sweep-morph linking
  - None (independent)
  - Linear (low freq → morph 0)
  - Inverse (low freq → morph 1)
  - Custom curve (future)

- [ ] **T8.4** Add sweep UI controls
  - Enable toggle
  - Frequency knob
  - Width knob
  - Intensity knob
  - Link mode selector

**Milestone M5: Sweep with morph link** ✓

### Week 9-10: Modulation

- [ ] **T9.1** Implement LFO sources (2x)
  - Rate (free/sync)
  - Shape (6 waveforms)
  - Phase offset
  - Unipolar option

- [ ] **T9.2** Integrate `EnvelopeFollower` from KrateDSP
  - Attack/Release parameters
  - Sensitivity control

- [ ] **T9.3** Implement Random source
  - Rate parameter
  - Smoothness (S&H vs smooth random)

- [ ] **T9.4** Implement Macro parameters (4x)
  - Min/Max range mapping
  - Curve shape

- [ ] **T9.5** Implement modulation routing
  - `ModRouting` structure
  - Route any source to any destination
  - Amount with curves (Linear, Exp, S-Curve, Stepped)

- [ ] **T9.6** Create modulation UI panel
  - Source configuration
  - Routing matrix display
  - Add/remove routing controls

**Milestone M6: Full modulation** ✓

---

## Phase 3: Advanced (Weeks 11-14)

### Week 11: Intelligent Oversampling

- [ ] **T11.1** Define per-type oversampling profiles
  - Map all 26 types to recommended factors
  - Document rationale per type

- [ ] **T11.2** Implement weighted calculation
  - Consider morph weights in calculation
  - Round up to power of 2

- [ ] **T11.3** Implement dynamic switching
  - Smooth transitions when factor changes
  - Avoid audible artifacts during switch

- [ ] **T11.4** CPU optimization pass
  - Profile hot paths
  - Optimize morph weight computation
  - Consider SIMD for distortion processing

### Week 12: Preset System

- [ ] **T12.1** Implement VST3 preset serialization
  - Version field for future compatibility
  - Serialize all parameters
  - Handle older versions gracefully

- [ ] **T12.2** Create preset browser UI
  - Category folders
  - Search/filter
  - Preview (future)

- [ ] **T12.3** Implement save dialog
  - Name input
  - Category selection
  - Overwrite confirmation

- [ ] **T12.4** Create factory presets (120)
  - Init presets (5)
  - Sweep presets (15)
  - Morph presets (15)
  - Category-specific (Bass, Leads, Pads, Drums, etc.)

**Milestone M7: Preset system** ✓

### Week 13: Undo/Redo & Polish

- [ ] **T13.1** Implement undo stack
  - Parameter change tracking
  - Edit grouping (drag = single undo)
  - 50+ levels deep

- [ ] **T13.2** Implement real spectrum analyzer
  - FFT-based visualization
  - Per-band coloring
  - Input/output overlay

- [ ] **T13.3** Implement metering
  - Input/Output peak+RMS
  - Per-band level indicators
  - Clipping detection

### Week 14: Progressive Disclosure

- [ ] **T14.1** Implement expand/collapse panels
  - Band detail expansion
  - Modulation panel toggle
  - Smooth animations

- [ ] **T14.2** Add keyboard shortcuts
  - Tab to cycle bands
  - Space to toggle bypass
  - Arrow keys for fine adjustment

- [ ] **T14.3** Implement tooltips
  - Parameter descriptions
  - Current value display
  - Keyboard shortcut hints

- [ ] **T14.4** Implement window resize
  - Minimum 800x500
  - Maximum 1400x900
  - Maintain aspect ratio

**Milestone M8: Complete UI** ✓

---

## Phase 4: Polish (Weeks 15-16)

### Week 15: Testing

- [ ] **T15.1** Cross-platform testing
  - Windows build verification
  - macOS build verification
  - Linux build verification

- [ ] **T15.2** DAW compatibility testing
  - Ableton Live
  - Bitwig Studio
  - Cubase
  - FL Studio
  - Reaper
  - Studio One
  - Logic Pro (macOS)

- [ ] **T15.3** Performance profiling
  - Verify CPU targets met
  - Memory usage analysis
  - Latency verification

- [ ] **T15.4** Bug fixing
  - Address all P1/P2 issues
  - Regression testing

**Milestone M9: Beta** ✓

### Week 16: Release

- [ ] **T16.1** Create user manual
  - Quick start guide
  - Feature documentation
  - Preset creation guide

- [ ] **T16.2** Finalize factory presets
  - Quality review
  - Category organization
  - Naming conventions

- [ ] **T16.3** Create installers
  - Windows (Inno Setup or WiX)
  - macOS (PKG/DMG)
  - Linux (DEB/RPM)

- [ ] **T16.4** Code signing
  - Windows EV certificate
  - macOS Developer ID
  - Notarization

**Milestone M10: Release 1.0** ✓

---

## Integration with Experimental Types (D20-D26)

These tasks can be done in parallel or after the core implementation:

- [ ] **TX.1** Integrate Chaos distortion (D20)
  - `ChaosWaveshaper` from KrateDSP
  - Attractor type selection UI
  - Speed and coupling parameters

- [ ] **TX.2** Integrate Formant distortion (D21)
  - `FormantDistortion` from KrateDSP
  - Vowel selector UI
  - Formant shift parameter

- [ ] **TX.3** Integrate Granular distortion (D22)
  - `GranularDistortion` from KrateDSP
  - Grain size/density UI
  - Freeze toggle

- [ ] **TX.4** Integrate Spectral distortion (D23)
  - `SpectralDistortion` from KrateDSP
  - FFT size selector
  - Mode selector UI

- [ ] **TX.5** Integrate Fractal distortion (D24)
  - `FractalDistortion` from KrateDSP
  - Iteration count UI
  - Mode selector

- [ ] **TX.6** Integrate Stochastic distortion (D25)
  - `StochasticShaper` from KrateDSP
  - Jitter parameters UI
  - Reseed button

- [ ] **TX.7** Integrate Allpass Resonant distortion (D26)
  - `AllpassSaturator` from KrateDSP
  - Topology selector UI
  - Frequency/decay parameters

---

## Advanced Morph Drivers (Future)

- [ ] **TF.1** Implement Chaos morph driver
  - Lorenz/Rossler attractor for morph path
  - Speed and coupling controls

- [ ] **TF.2** Implement Envelope morph driver
  - Input loudness → morph position
  - Sensitivity curve

- [ ] **TF.3** Implement Pitch morph driver
  - Detected pitch → morph position
  - Min/max frequency mapping

- [ ] **TF.4** Implement Transient morph driver
  - Attack detection → morph jumps
  - Sensitivity and decay
