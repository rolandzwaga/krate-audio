# Ruinae UI Gaps Assessment

**Date**: 2026-02-14
**Purpose**: Inventory of engine features that have no UI exposure in the Ruinae plugin.

## Entire Missing Sections (no UI at all)

| Feature | Engine Support | Parameter IDs | Notes |
|---------|---------------|---------------|-------|
| **Global Filter** | Full (enable, type, cutoff, resonance) | 1400-1403 | 4 params registered in controller but zero uidesc controls |
| **Mono Mode** | Priority, legato, portamento time, porta mode | 1800-1803 | 4 params registered, zero UI. Can't configure mono behavior |
| **Voice Mode** | Poly/Mono switch | ID 1 | Registered but no selector in uidesc — user can never switch to mono |
| **Macros 1-4** | `setMacroValue(index, value)` | None defined | No parameter IDs, no knobs. Mod wheel hardwired to Macro 0 internally |
| **Rungler** | Full DSP processor, ModulationSource interface | None defined | No parameter IDs for osc1/2 freq, depth, filter, bits, loop mode. Injected via Macro 3 but Macro 3 has no UI either |
| **Stereo Spread** | `setStereoSpread(float)` — voice pan distribution | None defined | No parameter ID exists at all |
| **Stereo Width** | `setStereoWidth(float)` — mid/side matrix | None defined | No parameter ID exists at all |
| **Gain Compensation** | `setGainCompensationEnabled(bool)` | None defined | No parameter ID |
| **Pitch Bend Range** | `setPitchBendRange(float semitones)` | None defined | No parameter ID |
| **Velocity Curve** | `setVelocityCurve(VelocityCurve)` | None defined | No parameter ID |
| **Tuning Reference** | `setTuningReference(float a4Hz)` | None defined | No parameter ID |
| **Voice Allocation Mode** | `setAllocationMode(AllocationMode)` | None defined | No parameter ID (Oldest/Newest/High/Low) |
| **Voice Steal Mode** | `setStealMode(StealMode)` | None defined | No parameter ID |
| **Trance Gate Tempo Sync** | Toggle for tempo-synced gate rate | ID 606 | Registered but no uidesc control — user can toggle note value but not enable sync |

## Global Modulation — Partially Exposed

| Feature | What's Exposed | What's Missing |
|---------|---------------|----------------|
| **LFO 1** | All 11 params | Nothing — fully exposed |
| **LFO 2** | All 11 params | Nothing — fully exposed |
| **Chaos** | Rate, type, depth, sync, note value | Nothing — fully exposed |
| **Mod Matrix** | 8 slots × (source, dest, amount) via ModMatrixGrid | **Curve**, **Smooth**, **Scale**, **Bypass** per slot (IDs 1324-1355) — registered but no UI access |
| **Env Follower** | N/A | No params or UI (engine supports it via ModulationEngine) |
| **Sample & Hold** | N/A | No params or UI |
| **Pitch Follower** | N/A | No params or UI |
| **Transient Detector** | N/A | No params or UI |
| **Random** | N/A | No params or UI |

## Summary by Impact

### High Impact (blocks major functionality)

1. **Voice Mode** — can't switch to mono at all
2. **Mono Mode params** — portamento, legato, priority all inaccessible
3. **Global Filter** — entire post-mix filter section is invisible
4. **Macros 1-4** — no user-facing macro knobs; mod matrix can route to them but users can't control macros directly
5. **Rungler** — completely hidden; no way to configure or even activate it

### Medium Impact (limits expressiveness)

6. **Mod matrix detail params** — can't set curve shapes, smoothing, scaling, or bypass per slot
7. **Stereo Width / Spread** — no stereo field control in the UI
8. **Trance Gate sync toggle** — note value selector exists but the sync enable is missing

### Lower Impact (reasonable defaults work)

9. Voice allocation/steal mode
10. Pitch bend range, velocity curve, tuning reference
11. Gain compensation toggle
12. Env Follower, S&H, Pitch Follower, Transient, Random mod sources (future roadmap items per the signal flow diagram)

## Totals

- ~30+ configurable engine parameters with no UI path
- 5-6 entire mod source types exist in the architecture diagram but have no implementation wiring yet
- 4 parameter groups are registered in the controller but have zero uidesc controls (Global Filter, Mono Mode, Voice Mode, Trance Gate Sync)
