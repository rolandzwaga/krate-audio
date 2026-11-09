# Changelog

All notable changes to Disrumpo will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- **Level meters** — The top bar shows input and output level meters (peak and RMS for left and right), each band's title shows that band's level, and a red light marks any sample above 0 dBFS until you click it.

### Fixed

- **Centre pan was 3 dB below unity** — Each band applied a mono pan law to its stereo signal, so with every pan centred the output sat 3 dB below the input. Pan is now a stereo balance: centre is exact unity, and hard left or right keeps the near side at full level and silences the far side.
- **Oversampling, Oversample Max and band Bypass did nothing** — The audio path never reached the bands' oversamplers or their bypass switch, so every band ran at the host rate and a bypassed band stayed distorted. Bands now oversample at 2x or 4x as their distortion types ask, capped by Oversample Max, and a bypassed band passes its crossover signal through unchanged.
- **Tone, DC blocking and morph smoothing ran at the wrong rate** — Inside each band these were set up for 8x the rate they ran at, so a Tone of 8 kHz acted near 1 kHz and 10 ms of morph smoothing took about 80 ms. Each now runs at the rate it is clocked.
- **No latency was reported to the host** — Oversampling and the Spectral type delay a band, but the host was told 0 samples and the bands and the dry signal arrived at different times. Disrumpo now reports its latency, delays the faster bands and the dry path so everything lines up, and reports an endless tail while a Granular or Spectral node, or a Feedback or Allpass Resonant node with feedback above 0, is active.
- **Bands at different oversampling and the dry path now line up** — Bands were delayed by whole samples only, while the oversampling filters delay by a fraction (3.0 samples at 2x, 6.5 at 4x), so next to a band at another factor, or against the dry signal at in-between Mix settings, the sum dipped: -0.4 dB around 5 kHz and -1.3 dB around 10 kHz with a 2x band at 48 kHz. Each oversampled band is now delayed by the fraction that keeps those sums flattest from 20 Hz to 20 kHz (chosen per sample rate, and for a 2x band by whether a 4x band is in the patch). From 88.2 kHz up, Mix 50 % and every crossover sum stay within 0.02 dB of flat. At 44.1 and 48 kHz a 2x band without a 4x band stays within 2.0 dB (44.1 kHz) and 0.9 dB (48 kHz), the worst being at 20 kHz; in a patch with a 4x band the filters' own phase still leaves notches above 17 kHz (down to -17 dB at 48 kHz and -30 dB at 44.1 kHz), which no delay removes. The reported latency is unchanged.
- **Left and right channels leaked into each other** — Both channels ran through one shared distortion and morph engine, so its filters and delay lines saw the two channels interleaved; a signal on the left alone came out of the right at up to -7 dBFS. Each channel now has its own engine.
- **Global Mix between 0 and 100 % notched frequencies** — The dry signal did not have the phase response of the band sum, so in-between mix settings cancelled around the crossover frequencies (down to a complete null with 2 bands). The dry path now carries the same phase response, and the mix stays flat within 0.02 dB.
- **Sweep changed the band's level instead of its drive** — Sweep Intensity multiplied the band's input, so it acted as a volume control, and it measured distance to fixed band centres. It now scales the drive of the bands near the sweep (up to double at 200 % on the centre), using the centres of the actual crossover bands.
- **Sweep LFO and envelope moved once per block** — The sweep LFO stepped once per audio block, the envelope follower heard only the first sample of each block, and a synced sweep LFO ignored the host tempo. Both now run every sample, and the synced LFO follows the host tempo.
- **Morph X/Y Link to the sweep worked only with the editor open** — The link was driven by the editor, followed only the base sweep frequency and was not saved. It now runs in the audio engine, follows the moving sweep and is saved with the preset; projects from earlier versions load with the links off.
- **One bad sample froze the sweep** — A single non-finite input sample stuck in the sweep's envelope follower until the plugin was reset, pinning the sweep centre at 20 Hz. The follower now recovers on the next sample.
- **MIDI learn did not work** — The plugin had no MIDI input, so a learned CC never reached its parameter, and mappings saved with a preset were lost. MIDI learn from a control's context menu now works end to end, and both global and per-preset mappings are saved with the project.
- **Hosts restored presets with the wrong type settings** — Loading a preset or project restored each node's shape knobs but not the distortion settings derived from them (threshold, curve, sag, tape model, feedback, vowel and so on), so a node kept the defaults or the previous preset's values. 115 of the 121 factory presets loaded wrong in a running instance; every preset now loads as saved.
- **A sample-rate or block-size change reset the patch** — Each change, and the usual host load order, rebuilt every band's morph nodes as a single SoftClip node and reset the crossover frequencies while the editor kept showing the loaded values. The patch now survives, and each band's morph mode and smoothing are saved.
- **Modulation routings and macros were cleared on activation** — Every time the host activated the plugin, including right after loading a project, the modulation matrix was emptied, and the next save stored it empty. Routings and macros now survive activation.
- **Sweep Width and Intensity reverted after loading a project** — The loaded values were overwritten by the defaults on the first audio block, and the next save made the loss permanent. Both now load as saved.
- **Presets saved from the plugin were stored wrong** — The preset writer saved the later modulation sources one entry too low, 1 or 2 active nodes as one more, the crossover frequencies on the wrong scale and a fixed custom sweep curve. Saved presets now hold what is set.
- **Old projects moved Sample & Hold, Pitch and Transient routings** — Projects saved before the Rungler source was added loaded these three routings onto the neighbouring source. They now load onto the right one.
- **Loading a preset during playback wrote into the running audio** — Preset and project loads changed the audio engine from the host's thread while it was processing. The new state is now handed to the audio thread and applied at the start of a block.
- **Morphing between distortion types jumped** — Morphing between different families stepped in level at 40 % and 60 %, two different types of one family switched abruptly at the midpoint, and in Linear 1D mode nodes C and D could never be reached on their own. Cross-type morphs now blend continuously, and Linear 1D visits A, B, C and D in turn.
- **Spectral morphs went silent, and shared counts drifted** — Morphing between Spectral nodes reallocated memory on every sample and silenced the effect, and two nodes that shared an FFT size, stage count or iteration count morphed to one below it (2048 became 2047). The FFT size now switches without allocating, and shared values stay put.
- **Granular drove grains five times harder than its Drive knob** — Every grain was driven 5x on top of the node Drive, and when a grain ended a neighbouring grain could be skipped or processed twice. Drive now matches the knob, and every grain plays through in order.
- **Stochastic made sound from silence** — Silent input came out as a DC offset, up to full scale with Jitter and Drift at maximum. Silence now stays silent, and Tube, Stochastic and the Temporal Diode and Tube curves now block DC.
- **Quantize turned silence into DC** — The level grid had no step at zero for many Levels settings, and Offset shifted the whole grid. The grid is now centred on zero, Offset skews only the decision points, and silence stays silent.
- **Fuzz could not be opened up with Tone** — Fuzz and Asymmetric Fuzz kept an extra low-pass near 1.8 kHz under the node's own Tone. The node Tone is now the only tone control.
- **Temporal and Stochastic nodes heard a Drive change late** — In a band with two or more nodes, a Drive change on a Temporal or Stochastic node took effect only with the next change to that band's nodes, so the last move of the Drive knob could be lost (a Temporal node stayed 3.5 dB quieter than its Drive setting). Every Drive change now reaches them at once.
- **Fuzz Bias below centre all sounded the same** — Every Bias setting at or below the centre clamped to the most gated sound. Bias now covers Fuzz's whole range.
- **Tube cascade stages shared one filter state** — With more than one stage, every stage ran through the same tube stage, so its filters saw the stages interleaved. Each stage now has its own.
- **The bottom fifth of Bitcrush Bits did nothing** — The knob ran from 1 to 16 bits while the crusher stops at 4. Bits now spans 4 to 16.
- **Feedback type: Curve did nothing, Lim off still limited, Threshold was reversed** — All three now work as labelled.
- **Ring Saturation jumped at Mod Depth 0** — At exactly 0 the output stage was skipped, so the level jumped at the end stop. Depth 0 now joins the rest of the range smoothly.
- **Fresh fold nodes were quiet and dark** — New Sine, Triangle and Serge Fold nodes started darkened by their Smooth setting, and Triangle Fold came out more than 20 dB quieter than the other types. Fresh fold nodes now start open, and the folders get makeup gain.
- **Clicks on common moves** — Changing Band Count, changing a node's type, moving Drive away from 0 and automating Input Gain, Output Gain or Mix stepped the output on one sample. Band Count changes now dip for 10 ms, type changes fade over 5 ms with the new type's processor cleared, Drive blends in from 0, and the gains and Mix glide.
- **Modulation left a band stuck at its last value** — When every modulation offset on a cross-type morph band returned to 0, the band kept its last modulated drive until the next edit. It now returns to its base settings.
- **Low crossovers drifted at high sample rates** — The crossover filters lost precision at low frequencies (the default 112 Hz split was 0.11 dB off at 192 kHz). They now compute in double precision.
- **Seven sweep controls and the node Bias had no knob in the editor** — Sweep Intensity, Falloff, Morph Link, LFO Waveform, LFO Sync, LFO Note Value and Envelope Sensitivity, and the selected node's Bias, were reachable only from the host's parameter list. The sweep row has a second line for them, and Bias sits next to Drive and Mix in the expanded band panel.
- **Node edits landed on the wrong node** — Switching a node's type kept the old type's shape values on the node, every node shared node A's per-type memory, and node, MorphPad and node-selector displays did not follow automation or preset loads. Each node now keeps its own values, and the displays follow the parameters.
- **Host parameter reset and display were wrong** — Resetting Band Count or Oversample Max in the host set 1 band and 1x, Sweep Intensity showed 0-100 % for a 0-200 % range, and many modulation parameters showed raw numbers (Env Attack 10 ms read 0.091 ms). Defaults and displays now match what the plugin plays.
- **Fresh Temporal nodes started in Derivative mode** — A new Temporal node now starts in Envelope Follow.
- **Type menus listed numbers before their names** — Several shape-slot menus showed nine numeric entries before the type's own options, and picking an entry chose the wrong one. The menus now show only their options.
- **The expanded band panel ignored Band Count** — An expanded panel stayed visible after its band was removed, or never appeared when the band came back. It now follows Band Count.
- **The spectrum analyzer showed spliced audio** — Its analysis window was pieced together from fragments of about half a second of audio. It now receives the audio contiguously at every block size.
- **Crossover dividers got out of order when dragged** — Dragging a divider between close neighbours could place it past one of them. Dividers now stay in order.
- **The editor took keys from text fields** — Arrow keys, Tab and Space were caught by the editor even while a text field or preset overlay had focus, so Space could toggle a band's bypass while typing. Those keys now go to the focused field.
- **The editor could crash on close** — Closing the editor after switching a band from its Shape page back to Main could touch freed memory. Fixed.
- **Modulation rings and the spectrum stopped after reopening the editor** — After the editor was closed and reopened, the modulation range rings on the sliders and the shared-memory spectrum stayed empty, and a host that connected the editor after activation got no spectrum. Both now work after any reopen or late connect.
- **The Morph Smooth knob sat under the Nodes buttons** — The knob is moved down so it is fully visible and clickable.
- **Preset loads re-applied each band's nodes once per setting** — A preset chosen in the preset browser sends 272 node settings in one audio block, and each one re-applied all four nodes of its band and re-routed their distortion settings: 272 times in that block. A band's nodes are now applied once per block, however many of its settings changed.
- **A project could hang on its second load** — A saved custom sweep curve with a middle point at the very start or end of the curve loaded once, but loading that state again into the same instance looped forever on the host's thread. Loading now always finishes, and curve points that are not finite numbers are skipped.
- **Crossovers could run closer than half an octave** — Only a divider drag kept adjacent crossovers half an octave apart; automation, the host's parameter list or a loaded project could set them closer, or out of order. The audio engine now runs each crossover at least half an octave above the one below it and returns it to its own setting when the one below moves away. Near the top of the range, where the upper limit (20 kHz, or 45 % of the sample rate if lower) leaves no room, the highest crossover takes the limit and the ones below it step down by half an octave each. The factory presets are not affected.
- **Chaos Speed showed 0 to 1 while the chaos source ran 0.05x to 20x** — The host's parameter list and automation lanes showed Chaos Speed as a bare 0 to 1 number, and its default read 0.048. It now shows the speed the chaos source runs, 0.05x to 20x, with its default at 1x. Saved projects keep their sound.
- **Morphing between two types of one oversampling factor dipped the band** — A rounding error in the morph weights pushed the band to the next oversampling factor at some cursor positions, so a morph between two 2x types (Soft Clip to Tube) switched to 4x and back 12 times in a 2 s sweep, each switch a 10 ms dip in the band's level. A morph between types of one factor now stays at that factor.
- **Solo and Mute clicked** — Soloing, unsoloing or muting a band added it to or removed it from the band sum on one sample, so the output stepped (unsoloing a band stepped up to 2.5 times the largest step of the steady signal). Bands now fade in and out of the sum over 15 ms.

### Changed

- **Oversampling tops out at 4x** — Oversample Max "8x" now runs 4x. The entry stays so saved projects keep their setting.
- **Output Gain applies after the Mix** — Output Gain now scales the whole output, dry and wet, so it also works at Mix 0.
- **Digital types no longer use Drive as a volume knob** — Aliasing, Sample Reduce, Bitwise Mangler, Granular, Chaos and Spectral divide their output by the drive above 1, so Drive changes the character without raising the level.
- **Fuzz, Asymmetric Fuzz and the rectifiers are quieter at high drive** — Their level rose with Drive all the way up (a -6 dBFS sine came out at +9.4 dBFS from Fuzz and +10.3 dBFS from Half Rectify at Drive 10). Up to Drive 5 nothing changes; above it the output is scaled by 5 / Drive (3.5 dB quieter at Drive 7.5, 6 dB at Drive 10), so the level stays near its Drive 5 value. Full Rectify's shape does not depend on Drive, so above Drive 5 its Drive no longer changes the sound. Nine factory presets come out 0.01 to 0.40 dB quieter (Fuzz_Face the most); none needed new levels.
- **Less aliasing on Half Rectify and Triangle Fold** — Both run their shaper through first-order antiderivative anti-aliasing at their oversampled rate, which removes most of the aliasing of the rectifier's step and the fold's corners: a 1 kHz tone at 48 kHz aliases at -72 dB instead of -42 dB (Half Rectify, Drive 2.5) and -73 dB instead of -43 dB (Triangle Fold, Drive 10). The sound is otherwise the same: the level within 0.02 dB and the harmonics of a 100 Hz tone within 0.02 dB. At Oversample Max 1x both run as before.
- **Fresh instances start with no morph smoothing** — A new instance runs morph smoothing at its displayed default of 0 ms instead of 10 ms.
- **Fractal Multiband bands sum flat** — Fractal's Multiband mode now phase-aligns its four internal bands, so they add back without the -0.8 dB dip around 320 Hz they had; its crossovers also start at their frequencies instead of sweeping in from 1 kHz over the first few milliseconds. Fractal Multiband sounds come out about 0.2 dB louder, mostly a phase change between 60 Hz and 1 kHz.
- **Crossover phase compensation in double precision** — The band crossovers' phase-compensation filters now run in double precision with an exact Butterworth Q and follow the crossover frequency while it glides. The dry signal at Mix below 100 % lines up with the bands to -140 dB or better at every sample rate (was -65 dB at 192 kHz), and moving a crossover no longer leaves a brief phase mismatch. The change to the sound is far below audibility.
- **Lower CPU use** — The oversampling filters, the morph engines and the distortion nodes do the same work with less overhead, and the sound is unchanged sample for sample. Measured on one core at 44.1 kHz with 512-sample blocks: four bands of HardClip at 4x went from 17.7 % to 4.0 %, four bands of four morphing nodes from 21.7 % to 11.4 % (four Formant nodes), from 38.5 % to 14.9 % (HardClip, Sine Fold, Full Rectify, Ring Saturation at 4x) and from 47.4 % to 27.0 % (Formant, Chaos, Granular, Fractal).
- **Factory presets re-levelled; 120 presets** — With centre pan at unity and the other level fixes, many presets got louder; 66 presets have a lower Output Gain (by 0.1 to 9.8 dB) so none peaks above 0 dBFS on a -12 dBFS test signal, and none was raised. Init/Init_5_Bands, a 5-band preset the 4-band plugin cannot play, is removed, and Experimental/Formant_Shift is back.

## [0.9.11] - 2026-11-02

### Added

- **Version in the editor** — The plugin version is shown in the top bar, next to the Check for Updates button.

### Fixed

- **The plugin bundle reported version 0.8.0** — The bundle's `moduleinfo.json` (and, on macOS, the version Finder shows for the bundle) carried the shared build's version 0.8.0 instead of the plugin's own. It now matches the version shown in the editor. Hosts never used this value for anything audible; it matters for tools that list installed plugins.
- **The AUv3 version ran only on Apple Silicon** — In the macOS installer, the AUv3 app (`Disrumpo AUv3.app`) and the extension inside it were built for Apple Silicon only, while the VST3 and AU beside them were universal, so on an Intel Mac the AUv3 version did not load. Both are now universal (Apple Silicon and Intel), like the rest of the package.
- **The update check never worked on Linux** — The check could not fetch over https, so the update banner never appeared on Linux. It now fetches through the system's libcurl (`libcurl.so.4`, or `libcurl-gnutls.so.4`), loaded when the check runs; on a system without libcurl the check stays silent, as before.

## [0.9.10] - 2026-10-09

### Fixed

- **The spectrum analyzer stayed empty on macOS in some hosts** — In the AU version, and in VST3 hosts that leave the processor-to-editor data transfer to the plugin, the editor never received a spectrum block: the transfer blocks were requested at a size macOS's aligned allocator rejects, so every block came back empty and every update was dropped without a message. The block size is now rounded up to the required alignment. Hosts that provide their own transfer handler, and Windows and Linux, were never affected; the sound was never affected anywhere.

## [0.9.9] - 2026-03-20

### Added

- **SharedDisplayBridge visualizer fallback** — Spectrum analyzer now works in DAWs that don't implement `IConnectionPoint::connect()` (e.g. Ableton, Reason). A three-tier automatic fallback detects when DataExchange is unavailable and reads display data directly from processor-owned shared memory via a global registry. No user configuration needed.
- **Diagnostic logging for display pipeline** — `[KrateBridge]` messages in DebugView/stderr trace the full lifecycle: initialize, connect, setActive, DataExchange receipt, and fallback activation. Filterable by `KrateBridge` prefix.

## [0.9.8] - 2026-03-19

### Added

- **Band Tone and Band Bias modulation targets** — Tone and Bias are now available as per-band modulation routing destinations, grouped alongside each band's existing targets (Morph X/Y, Drive, Mix, Gain, Pan) in the routing dropdown.
- **Modulation overlay on Tone, Gain, and Pan sliders** — Upgraded these controls to ModSlider so they display the blue modulation range indicator when targeted by a modulation source.

### Changed

- **Removed Bias from main band tab** — Bias is type-specific (only affects tube, tape, fuzz, wavefolder, and ring mod types) and is already accessible through each distortion type's shape slot controls. Removing it from the main tab avoids confusion when it has no effect.
- **Preset version bumped to 12** — Existing presets are automatically migrated; modulation routing destination indices are remapped from the old 6-per-band to the new 8-per-band layout.

### Fixed

- **Modulation routing slots shifted on reload** — The controller's component state parser was missing the Rungler data block (10 bytes), causing all routing and macro data to be read from wrong stream offsets. Routing slot assignments would silently shift down by one position on every save/reload cycle.

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
