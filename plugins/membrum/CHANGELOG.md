# Changelog

All notable changes to Membrum will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0] - 2026-04-08

### Added

- **Plugin scaffold** -- CMake target, entry point, processor/controller skeletons, AU configuration, Windows resources, CI integration
- **Single drum voice** -- ImpactExciter + ModalResonatorBank (16 Bessel membrane modes) + ADSREnvelope signal path
- **5 parameters** -- Material (woody/metallic), Size (small/large), Decay (short/long), Strike Position (center/edge), Level (volume)
- **MIDI note 36 trigger** -- Single voice responds to C1 note-on/off with velocity-sensitive excitation
- **Velocity response** -- Soft hits produce dark/muted tones, hard hits produce bright/punchy tones with wider bandwidth excitation
- **State save/load** -- Binary state format with version field for forward compatibility
- **Host-generic editor** -- No custom UI; parameters visible in DAW's built-in parameter editor
- **Cross-platform** -- VST3 (Windows, macOS, Linux) + Audio Unit (macOS)
