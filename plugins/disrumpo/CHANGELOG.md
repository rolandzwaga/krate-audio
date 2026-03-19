# Changelog

All notable changes to Disrumpo will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.9.7] - 2026-03-19

### Fixed

- **Preset browser loading broken for modulation and morph data** — `loadComponentStateWithNotify()` was missing v5+ modulation and v6+ morph sections, causing all presets to appear identical in the browser. Deduplicated ~430 lines of state parsing into a single `parseComponentState<ParamSetter>` template method.
- **Incorrect mod source indices in factory presets** — Preset generator had wrong ModSource enum ordering (Chaos at 5 instead of 9, Macros at 9-12 instead of 5-8). Fixed and regenerated all 119 factory presets.
- **Input gain, output gain, and mix not affecting audio** — These three global parameters were registered through UI and controller but never applied to the actual audio signal. Now properly applied (input gain before crossover, output gain and dry/wet mix after band summation).
- **Spectrum analyzer data overflow in IMessage fallback hosts** — Throttled spectrum data sends to ~30Hz and increased DataExchange queue from 4 to 8 blocks.

### Changed

- **Standardized LFO note value dropdowns** — Replaced the custom 15-entry note value list (1/1 to 1/16T) with the standard DSP dropdown matching all other plugins. Bumped preset version to 10 with automatic migration of old note indices.
- **Extended tempo sync note values** — Added 2/1, 3/1, and 4/1 note values (with triplet and dotted variants) to all LFO and sweep note value dropdowns, expanding to 30 entries for longer sync periods.

## [0.9.6] - 2026-03-18

### Changed

- **Refactored controller.cpp into focused modules** — Split the 5626-line controller.cpp into 6 files: visibility_controllers.h/.cpp (9 IDependent observer classes), parameter_registration.cpp (5 registerXxxParams methods), format_helpers.h (string formatting utilities), and custom_buttons.h (preset browser/save buttons). Main controller.cpp reduced to ~1940 lines.
- **Migrated spectrum analyzer to VST3 DataExchange API** — Replaced the IMessage-based FIFO pointer sharing with the standard VST3 DataExchange API for transferring audio samples from Processor to Controller, with automatic IMessage fallback for older hosts.

### Added

- **DataExchange integration test** — Verifies the full Processor→Controller spectrum data pipeline via DataExchange blocks.

## [0.9.5] - 2026-03-16

### Fixed

- ** Fix ALL distortion type UIs ** - A large number of the UI controls were not properly wired to their underlying processor. These were all properly wired.
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
