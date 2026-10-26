# D5: Voice and note behaviour (FR-008, FR-023; plan §3.5)

**Status: RATIFIED by the user, 2026-10-23 (spec Clarifications "Session 2026-10-23 (12a ratification)").** This document is a **cross-reference**. It adds no behaviour
(spec FR-008; plan §3.5). Everything below describes code that already ships: Phase 4 (`NoteMemory`), Phase 9
(`ProfundumEngine`, `ProfundumVoice`), the shared `VoiceAllocator` and `MonoHandler` they compose, and the Phase 11
parameter registrations. The UI adds no voice behaviour (FR-023). Every statement cites the source line it rests on.

Paths are abbreviated as follows:

| Short | File |
|---|---|
| `va` | `dsp/include/krate/dsp/systems/voice_allocator.h` (shared) |
| `nm` | `dsp/include/krate/dsp/processors/note_memory.h` (Phase 4) |
| `eng` | `dsp/include/krate/dsp/systems/profundum_engine.h` (Phase 9) |
| `voice` | `dsp/include/krate/dsp/systems/profundum_voice.h` (Phase 9) |
| `mh` | `dsp/include/krate/dsp/processors/mono_handler.h` (shared) |
| `np` | `plugins/profundum/src/parameters/note_params.h` (Phase 11) |
| `gp` | `plugins/profundum/src/parameters/global_params.h` (Phase 11) |

---

## 1. Stealing and retrigger

### 1.1 Who allocates

- **MONO** goes through `MonoHandler`; **POLY** goes through `VoiceAllocator` in mode Oldest with Hard steal
  (`eng:12–13`). `configureAllocator` sets those two modes (`eng:645–648`).
- Voice Mode (ID 1) is registered with the strings `Mono` / `Poly`, default `Mono` (`gp:139–142`, `gp:50`).
  Polyphony (ID 2) is registered as `1` … `8`, default 4 (`gp:144–149`, `gp:51`; `eng:99–100`).
- The engine never sets a unison count, so the allocator keeps its default of one voice per note (`va:1136`).
- Note events are latched and applied at the next 32-sample grid point. They carry no sample offset (`eng:16–19`).
  A full queue (256 entries, `eng:102`) drops a note-on and counts it (`eng:221–236`). A note-off is never dropped:
  when the queue is full it is recorded in the pending-release flags (`eng:237–249`).

### 1.2 Hard steal (POLY)

- A new note first takes an idle voice (`va:927–935`). Only when no idle voice is left does the allocator steal
  (`va:938`).
- With Hard steal the allocator emits a `Steal` event for the victim (`va:1025–1033`), then assigns the new note to
  that same voice and emits its `NoteOn` (`va:1048–1068`).
- The engine records the `Steal` and classifies the `NoteOn` that follows on that voice as
  `NoteMemory::Transition::VoiceReuse` (`eng:790–799`). The voice resolves that transition through Note Memory
  (`voice:328–331`); see section 3.
- A steal is not a cut. When the resolved actions include a phase reset on an audible voice, the voice arms its
  `RetriggerDeclick` dip and defers the transition to the dip's switch sample (`voice:333–340`; `nm:94–95`). The dip is
  3 ms long (`voice:74`, `voice:226`). The steal fade **is** this dip; it has no separate gain factor (`voice:15–16`,
  `voice:917`).
- The new note's pitch is `midiNoteToFrequency(note)`, not the allocator's own tuning (`eng:800–801`).

### 1.3 Same-note retrigger

- **POLY.** If the pressed note is already sounding on a voice (Active or Releasing), the allocator retriggers that
  voice instead of allocating a new one (`va:237–242`; the search is `va:829–841`). A Releasing voice returns to Active
  (`va:41`). With one voice per note the allocator emits `Steal` then `NoteOn` on that same voice (`va:844–866`). The
  engine therefore sees a `VoiceReuse` transition, exactly as for a steal (`eng:790–799`).
- **MONO.** A note-on is classified by the state of voice 0 before the event (`eng:769–781`):
  - `FromSilence` when voice 0 is not audible;
  - `Overlap` when a key was already held (legato playing);
  - `Tail` when no key was held but the release tail still sounds.
- **MONO, return to a held key.** Releasing the active key while another key is still held resolves as
  `ReturnToHeld` (`eng:824–826`). Releasing the last held key releases voice 0 (`eng:817–821`). Releasing a held key
  that is not the active one changes nothing (`eng:822–823`).

### 1.4 Determinism and lifecycle

- The Note Memory decision is a pure function of mode, Retrigger setting, transition and audibility. It keeps no
  state between calls and does not allocate (`nm:10–11`, `nm:83–106`).
- During a running dip a new transition replaces the pending one: the last call wins (`voice:342–346`).
- A released voice keeps rendering until its release ends. At each POLY grid point the engine hands finished
  voices back to the allocator (`eng:866–872`).
- Lowering Polyphony releases the voices above the new count; they finish their release tails (`eng:701–707`).
- Switching Voice Mode releases every voice (no note hangs, none is cut) and resets both note handlers
  (`eng:755–763`).

## 2. The POLY steal order

When every voice is in use, the victim is chosen in two tiers (`va:631–638`):

1. **The oldest releasing voice.** Releasing voices are preferred over held ones (`va:632–634`).
2. **Otherwise, the oldest active (held) voice** (`va:636–637`).

"Oldest" means the lowest note-on timestamp among voices in that state (`va:671–684`). It applies because the engine
selects `AllocationMode::Oldest` (`eng:646`, dispatched at `va:642–653`). Every note-on advances the timestamp
(`va:236`), and both a new allocation and a retrigger stamp the voice with it (`va:861`, `va:1048–1068`). So a
same-note retrigger makes that voice the newest.

## 3. Note Memory (ID 415) and Note Retrigger (ID 416)

### 3.1 Registered strings

| ID | Name | Registered strings (index order) | Default | Source |
|---|---|---|---|---|
| 415 | Note Memory | `Always Retrigger`, `Legato`, `Envelope Retrigger`, `Oscillator Continuous` | `Always Retrigger` | `np:314–316`, `np:253` |
| 416 | Note Retrigger | `Reset`, `Continue` | `Reset` | `np:317`, `np:254` |

The four indices map to `NoteMemory::Mode` `AlwaysRetrigger`, `Legato`, `EnvelopeRetriggerOnly`,
`OscillatorContinuous` (`nm:24–29`, `np:211`, `np:219`). The two Retrigger indices map to `Reset` and `Continue`
(`nm:32`, `np:212`, `np:220`). The engine hands both settings to every voice (`eng:729–731`).

### 3.2 What each mode restarts

Each cell gives what restarts for that transition: the oscillator, the envelope, both, or nothing (`nm:119–126`).

| Mode (415) | From silence | Overlap | Tail | Return to held | Voice reuse |
|---|---|---|---|---|---|
| Always Retrigger | osc + env | osc + env | osc + env | osc + env | osc + env |
| Legato | osc + env | nothing | osc + env | nothing | osc + env |
| Envelope Retrigger | osc + env | env | env | env | env |
| Oscillator Continuous | env | nothing | env | nothing | env |

Rules applied to every cell (`nm:91–105`):

- An envelope restart also triggers the Drop and re-arms the Anchor (`nm:100–103`).
- Note Retrigger 416 matters only when the oscillator restarts: `Reset` resets the phase, `Continue` keeps it running
  (`nm:94`, `nm:99`).
- A phase reset on an audible voice is declicked (`nm:95`, `nm:104`), using the 3 ms dip of section 1.2.
- The Drop snaps when the voice was silent or the transition is declicked (`nm:102`).

### 3.3 Which transitions occur in each Voice Mode

- **MONO** produces `FromSilence`, `Overlap`, `Tail` (`eng:776–778`) and `ReturnToHeld` (`eng:826`).
- **POLY** produces only `VoiceReuse`, `Tail` and `FromSilence` (`eng:796–798`). Reading the table in 3.2 with those
  three columns, `Legato` and `Always Retrigger` resolve identically in POLY, and `Envelope Retrigger` and
  `Oscillator Continuous` differ only in whether the oscillator restarts from silence. This follows from the cited
  code; it is not new behaviour.
- MONO key priority is Mono Priority 410 (`Last`, `Low`, `High`; `np:305`; `mh:53–57`). Legato 411 and the portamento
  controls 412–414 act on the `MonoHandler` (`eng:709–722`).

## 4. Which voice the spectrum shows

In POLY the spectrum display follows one voice. The rule that picks it is D2's focus rule (plan §4.3.2), which D2
quotes verbatim. D5 does not restate it. How it meets section 1.3: a same-note retrigger keeps the same voice
(`va:237–242`), and a steal moves the focus to the stealing note's voice (plan §4.3.2).
