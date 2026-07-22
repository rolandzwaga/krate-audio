---
name: gradus
description: Domain knowledge for the Gradus step-arpeggiator plugin — the MIDI/event signal path (note input → ArpeggiatorCore → MidiNoteDelay echoes → event out + audition), load-bearing invariants (arp param IDs shared with Ruinae, unified save prefix + intentionally-local load path, fixed event-buffer caps), and where the deep tooling lives. Use when working on Gradus, arpeggiators, the shared arp engine, or Gradus presets — especially outside plugins/gradus/ where the leaf CLAUDE.md won't auto-load.
allowed-tools: Read, Glob, Grep
---

# Gradus — Standalone Step Arpeggiator

Gradus (AU `aumu`) is a MIDI-in / MIDI-out step arpeggiator with a stereo audition-only output,
extracted from Ruinae's arp section and driving the **shared** `Krate::DSP::ArpeggiatorCore` engine.
When editing under `plugins/gradus/`, its leaf `CLAUDE.md` auto-loads with build/test/param facts — this
skill carries the domain lore that also applies when reasoning about Gradus from elsewhere (the DSP
library engine, `plugins/shared/`, presets, tooling).

## Signal path (MIDI/event flow)

Host MIDI-in → Processor drains `inputEvents` into `arpCore_.noteOn/noteOff` (velocity denormalized ×127) →
per-block `arpCore_.processBlock` generates `ArpEvent`s into `arpEvents_[128]` → `midiDelay_.process`
merges the arp events with scheduled geometric echoes into `combinedEvents_[512]` → routed to
`data.outputEvents` as VST NoteOn/NoteOff (channel 0, noteId −1) **and** fed to the built-in monophonic
`AuditionVoice` for local audible monitoring. The engine itself:

- **Note input** (`held_note_buffer.h`): `HeldNoteBuffer` (cap 32, dedup, insertion + pitch-sorted arrays)
  + `NoteSelector` implementing 12 `ArpMode` traversals (Up/Down/UpDown/…/Markov) with octave expansion.
  Latch (Off/Hold/Add) and Live-vs-Sequencer source mode branch the noteOff handling.
- **Sequencing/lanes:** 10 polymetric `ArpLane` instances (vel/gate/pitch/modifier/ratchet/condition/
  chord/inversion/midiDelay/seqNote), each advancing on its own speed multiplier, swing, and length-jitter,
  with optional baked 256-entry speed-curve tables (double-buffered off-thread, consumed on the audio thread).
- **Emission:** `processBlock` is a jump-ahead loop resolving coincident events by priority
  (BarBoundary > NoteOff > Step > SubStep). `fireStep` runs the full gate/condition/modifier/velocity/
  pitch/range/humanize/ratchet/strum chain; every NoteOn schedules a guaranteed NoteOff.
- **Echo tail (Gradus-only):** `MidiNoteDelay` schedules geometric echo trains per NoteOn (feedback ≤16,
  velocity decay, pitch shift, gate scaling) in a 256-slot buffer with oldest-stealing overflow guards.

## Load-bearing constraints (violating these breaks presets, tests, or hosts)

- **Arp param IDs `3000-3372` are byte-for-byte shared with Ruinae** for preset interchange. Do not
  renumber or repurpose them.
- **Save prefix is unified; the load path is deliberately NOT.** `saveArpParams` calls
  `Krate::Shared::saveArpParamsShared` (the 50-field shared prefix in `arp_params_common.h`) then appends
  the Gradus tail (MIDI-delay / sequencer / Markov / speed-curve lanes). `loadArpParams` stays Gradus-local
  because its clamp ranges diverge — **`mode` clamps 0-11 here vs 0-9 in Ruinae** — so it must NOT be unified.
- **A cross-plugin byte-golden test guards shared-save identity.** Any change to the shared prefix order or
  fields must keep Gradus and Ruinae byte-identical or that test fails.
- **State framing:** `int32 version` (=`kCurrentStateVersion`=3) precedes the payload; `setState` accepts
  only version 2 or 3. Backward compat is EOF-driven (missing trailing lane → keep defaults); save tail
  order must exactly mirror load tail order, field for field.
- **Fixed event-buffer capacities:** `arpEvents_ ≤ 128`, `combinedEvents_ ≤ 512`; `midiDelay_` enforces
  `outCount < maxOutput` internally. The engine is zero-heap / all-`noexcept` — fixed arrays only.
- **`operatingMode` is always forced to `kArpMIDI`** after load and re-asserted every block — the arp is
  never bypassed. Audition params (4000-4003) are session-only (never serialized).
- **Change-gated setters rely on prev-value caches** (`prevArpMode_` etc.) to avoid resetting engine step
  state each block; `sourceMode` toggles additionally `requestPanicNoteOff()`. Do not push these params
  unconditionally, and note the caches are not reset in `setState`.
- **Lanes are addressed in TWO orders that disagree at indices 3/4/5.** Lane-param order
  (`getArpLane`, `getArpLaneStepBaseParamId`, `getArpLaneLengthParamId`) is
  `Vel Gate Pitch Ratchet Modifier Condition Chord Inv Delay`; ring/UI order
  (`subZoneToLaneIndex`, `ringDataBridge_`, `RingRenderer::isBarTypeLane`, `kDepthParamIds`) is
  `Vel Gate Pitch Modifier Condition Ratchet Chord Inv`. Anything handed a lane index **by the ring
  renderer** must resolve it with `getRingLaneStepBaseParamId` / `ringDataBridge_.laneAt`, never the
  `getArpLane` table — mixing them silently edits the wrong lane's parameters.
- **Transport edges belong to Sequencer mode only.** Live free-runs, so the rising play edge must not
  reset the engine (that clears `heldNotes_` and orphans sounding notes without emitting NoteOffs).
- **`requestPanicNoteOff()` discharges unconditionally** at the top of `processBlock`; the older
  `needsDisableNoteOff_` paths only fire when the arp is disabled or the held buffer just emptied.
- **Speed-curve points are capped at `kMaxSpeedCurvePoints` (64) on BOTH save and load.** The writer
  must never emit more than the reader consumes or the whole rest of the stream shifts.

## Deep tooling (read these for real work)

- **Full audit:** the `gradus-audit` workflow — [`.claude/workflows/gradus-audit.js`](../../workflows/gradus-audit.js).
  Adversarially-verified bug/anti-pattern/duplication sweep over `plugins/gradus` + the shared arp engine,
  ending in an ordered fix plan.
- **Plugin specifics:** [`plugins/gradus/CLAUDE.md`](../../../plugins/gradus/CLAUDE.md) — skeleton,
  param-ID scheme, `gradus_tests` target, pluginval path.
- **Engine headers:** `dsp/include/krate/dsp/processors/arpeggiator_core.h` (+ `.cpp` for the real
  `processBlock`/`fireStep` bodies), `dsp/include/krate/dsp/processors/midi_note_delay.h`,
  `dsp/include/krate/dsp/primitives/held_note_buffer.h`, `dsp/include/krate/dsp/primitives/arp_lane.h`.
- **Shared save prefix:** [`plugins/shared/src/parameters/arp_params_common.h`](../../../plugins/shared/src/parameters/arp_params_common.h)
  — the byte-golden-guarded contract with Ruinae. Gradus's tail + local load live in
  `plugins/gradus/src/parameters/arpeggiator_params.h`.
