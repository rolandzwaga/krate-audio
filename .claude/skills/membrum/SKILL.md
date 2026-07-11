---
name: membrum
description: Domain knowledge for the Membrum drum-synth plugin — per-voice signal path, preset/kit invariants, ring/range constraints, and where the deep tooling lives. Use when working on Membrum, drum synthesis, kits, pads, bodies/exciters, or Membrum preset retuning, especially outside the plugins/membrum/ tree where the leaf CLAUDE.md won't auto-load.
allowed-tools: Read, Glob, Grep
---

# Membrum — Drum Synthesizer

Membrum is a physically-modelled drum synth (AU `aumu`) and the current active-development plugin.
When editing files under `plugins/membrum/`, its leaf `CLAUDE.md` auto-loads with the build/test/param
facts — this skill carries the domain lore that also applies when you're reasoning about Membrum from
elsewhere (DSP library, presets, tooling).

## Signal path (per voice)

Exciter (strike/noise) → Body resonator → parallel noise layer → per-voice shaper → voice → voice pool → master.
- `dsp/` splits into `dsp/{bodies,exciters,unnatural}/`; `voice_pool/` handles polyphony; `state/` is the state codec.
- There is a **Fast** and a **Slow** two-block processing path — they must stay behavior-equivalent.
- Per-pad `noiseLayerGain` multiplies the parallel noise layer; snares push it >1 so wire buzz reaches
  near-body level (a snare's identity IS the wire buzz), while hats/toms/kick are unaffected.

## Load-bearing constraints (violating these breaks presets or tests)

- **Kit categories are FIXED:** `Acoustic`, `Electronic`, `Percussive`, `Unnatural`. Never invent new ones;
  filesystem subdir AND XML metadata must both match.
- **Ring/range:** `maxPolyphony` ∈ `[4,16]`; `modeInject > 0` rings undamped (flat plateau);
  Friction sustains without note-off (use NoiseBurst for one-shot swirls). Enforced by factory
  round-trip + infinite-ring tests.
- **Presets** live in `C:\ProgramData\Krate Audio\Membrum\Kits\{category}\`, NOT in the VST3 bundle.
  Install via the build target; never hand-copy a single VST3 file (missing `Resources/editor.uidesc`
  → blank UI + crash on unload).
- **UI data bridge:** piggyback per-block UI data on the `MetersBlock` DataExchange — don't add new
  queues or IMessage loops.

## Deep tooling (read these for real work)

- **Full signal-path audit:** the `membrum-signal-path` workflow — [`.claude/workflows/membrum-signal-path.js`](../../workflows/membrum-signal-path.js).
  Stage-by-stage bug hunt + dynamics/sameness diagnostic + literature correctness check.
- **Offline sample fitting:** [`tools/membrum-fit/AGENT_GUIDE.md`](../../../tools/membrum-fit/AGENT_GUIDE.md)
  (and its `README.md`) — the offline drum-sample fitter that renders + extracts features.
- **Plugin specifics:** [`plugins/membrum/CLAUDE.md`](../../../plugins/membrum/CLAUDE.md) — skeleton,
  param-ID base, `membrum_tests` target, pluginval path.
