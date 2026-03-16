# Changelog

All notable changes to Disrumpo will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.9.5] - 2026-03-16

### Fixed

- **Node B/C/D distortion controls not affecting sound** — Band popup UI controls (Drive, Mix, Tone, Bias, and all type-specific Shape parameters) were hardwired to node A. Turning knobs on nodes B, C, or D had no audible effect. Introduced proxy display parameters with bidirectional sync so all node controls correctly modify the selected node's actual parameters.
- **Shape shadow save/restore broken for non-node-A selections** — Type-switching shadow storage (which remembers per-type knob positions) was also hardcoded to node A, causing shape values to be lost when switching distortion types on other nodes.

## [0.9.4] - 2026-03-14

### Added

- **Single distortion mode (activeNodeCount=1)** — Default is now 1 node per band, bypassing morph overhead. MorphPad renders in greyscale with mouse interaction disabled when count=1.
- **Band-relative expanded panel positioning** — Expanded band popups now appear anchored to the triggering band instead of always at the top-left.
- **Band-colored popup borders** — Each expanded panel has a 2px border matching its band color (orange, teal, green, purple).
- **Drop shadow on expanded panels** — Semi-transparent shadow offset behind expanded band popups for visual depth.

## [0.9.3] - 2026-03-05

### Added

- **In-plugin update checker** — Automatically checks for new versions on editor open (24h cooldown) and shows a non-intrusive banner with download link when an update is available
- **"Check for Updates" button** — Manual version check in the plugin header
- Version display and dismiss functionality for update notifications

## [0.9.2] - 2026-03-02

### Fixed

- **VSTGUI crash during rapid preset switching**: Added bulk parameter load guard (FrameInvalidationGuard + bulkParamLoad flag) to suppress thousands of individual invalidRect() calls during setComponentState() and loadComponentStateWithNotify(). The guard hides the CFrame during bulk loads to short-circuit the IDependent notification chain, then triggers a single full-frame repaint on completion. Prevents STACK_BUFFER_OVERRUN crash from CInvalidRectList iterator invalidation.

## [0.9.1] - 2026-02-03

### Added

- Frequency labels on crossover band handles: abbreviated at rest (e.g., "200", "2k"), precise on hover/drag (e.g., "2.14 kHz")
- Collision avoidance for adjacent crossover labels when handles are close together
- Spectrum visualizer mode button (Wet/Dry/Both)

## [0.9.0] - 2026-02-01

### Added

- Initial release of Disrumpo multiband morphing distortion plugin
- 4-band crossover network with adjustable frequencies
- Morph system for smooth transitions between distortion types
- Sweep system with LFO and envelope modes
- Intelligent oversampling for alias-free distortion
- MorphPad for 2D morph control
- 120 factory presets across 11 categories
- MIDI CC learn for parameter automation
- Keyboard shortcuts for workflow efficiency

## [0.1.0] - 2026-01-26

- Initial plugin skeleton
