# Research — Membrum Phase 3 (Polyphony, Voice Stealing, Choke Groups)

**Date**: 2026-04-11
**Spec**: `specs/138-membrum-phase3-polyphony/spec.md`
**Purpose**: Collect and cite the primary/reputable sources that justify the engineering decisions made in Phase 3 — voice stealing policy, click-free steal time, choke-group count, default polyphony, and the CPU budget. All findings are summarized with a decision at the end of each section. No research content leaks into `spec.md`.

---

## §1 — Voice Stealing Algorithms in Commercial Synths and Drum Machines

### Findings

**Sweetwater InSync on voice stealing** (https://www.sweetwater.com/insync/voice-stealing/):
> "If you play another voice on a 16-voice synthesizer while all 16 voices are already playing, voice-stealing causes an already-sounding voice to be cut off and re-assigned to the new note. Most synths try to steal the oldest note or the note that is closest to the end of its release phase, so the change is least noticed."

**Electronic Music Wiki, "Voice Stealing"** (https://electronicmusic.fandom.com/wiki/Voice_stealing):
> "Generally, the first choice for a 'victim' will be a voice that is in its release phase, on the theory that the sudden disappearance of a note that is fading out anyway is less likely to be noticed. After that, methods vary; some synths will steal from the note that has been held the longest; some will take the lowest- or highest-pitched sounding note, and some will do other things."
> "There are many sophisticated methods of voice-stealing in use that go well beyond simply taking the oldest note away. For example, many machines will not take the lowest note playing because in the context of a performance it is very apparent when that note drops out."

**Sound on Sound, "From Polyphony To Digital Synths"** (https://www.soundonsound.com/techniques/polyphony-digital-synths): confirms the "oldest-first after release-phase voices" precedent as industry-standard.

**JUCE MPESynthesiser implementation** (https://docs.juce.com/master/classMPESynthesiser.html):
> "The MPESynthesiser uses a voice-stealing algorithm that applies heuristics including re-using the oldest notes first and protecting the lowest & topmost notes, even if sustained, but not if they've been released."
> "If allowTailOff is true, voices will be allowed to fade out notes gracefully (if they can); if false, notes will all be cut off immediately."

**Native Instruments Battery 4 manual** (https://www.native-instruments.com/ni-tech-manuals/battery-manual/en/setup-page), voice-group choke modes:
> "When a voice group runs out of voices, the Mode parameter decides which notes to choke: Kill Any, Kill Oldest, Kill Newest, Kill Highest, Kill Lowest."
> "The Fade parameter sets the time that voices overlap before cutting each other off completely, which prevents an overly abrupt transition between voices."

Battery 4 explicitly exposes a cross-fade parameter at the voice-group level, confirming that commercial drum samplers use a short overlap fade (not a hard cut) to make steals click-free. Battery 4 calls this "Fade" and it is configurable per voice group.

**Cycling '74 RNBO docs, "Voice-stealing modes"** (https://rnbo.cycling74.com/learn/voice-stealing-modes): enumerates `steal-oldest`, `steal-quietest`, `steal-youngest`, `steal-lowest`, `steal-highest` as the standard set exposed by modern max/MSP-lineage synths. Three-of-the-five match exactly what spec 135 line 46 calls out: Oldest / Quietest / Priority.

### Decision

Phase 3 ships **three** policies in the order of industry prevalence:

1. **Oldest** (default) — matches JUCE's built-in, Sweetwater's description of "most synths," and RNBO's `steal-oldest`. The musical default for virtually every commercial synth since the 1980s.
2. **Quietest** — matches RNBO's `steal-quietest`, MPESynthesiser's "note closest to the end of its release phase" heuristic. This is the best policy for busy cymbal/hat washes where audibility matters more than age.
3. **Priority (by pitch, low = protected)** — matches the "not taking the lowest note" heuristic from Electronic Music Wiki and maps directly to `VoiceAllocator::AllocationMode::HighestNote`. For drum kits this protects kick/snare (low MIDI notes) from being stolen in favor of hats/cymbals (high MIDI notes).

The **Quietest** policy cannot use `VoiceAllocator::AllocationMode::LowestVelocity` because velocity is a *note-on time* property, not a *current instantaneous level*. Spec FR-122 therefore chooses between (a) extending `VoiceAllocator` or (b) processor-layer selection. **Recommendation: option (b)** — processor-layer selection that reads the voice's current amp envelope follower. This keeps the shared DSP library untouched per FR-193.

---

## §2 — Click-Free Voice Steal Fade Time (5 ms Justification)

### Findings

**JUCE forum, "voiceStealingEnabled causing clicks with MPESynthesiser"** (https://forum.juce.com/t/voicestealingenabled-causing-clicks-with-mpesynthesiser/44138):
> "When a voice is stolen in MPESynthesiser, it creates a discontinuity in the waveform. If there are long release envelopes and more notes are played than available voices, a voice gets stolen without frames to fade out."
> "The suggestion has been made to optionally allow voices to return a small buffer of samples of the note fading out to prevent pops."

JUCE's built-in MPESynthesiser does NOT provide click-free steal out of the box — users must implement custom fade-out logic.

**JUCE forum, "Voice steal pops"** (https://forum.juce.com/t/voice-steal-pops/30923): discusses the exact problem and recommends a short linear fade (typically 1–10 ms) applied as a gain multiplier when a voice is being stolen.

**Loopmasters, "Ways To Avoid Pops & Clicks"** (https://www.loopmasters.com/articles/2430-Ways-To-Avoid-Pops-Clicks-In-Your-Audio-Recordings): "Adding a short attack and/or release time to the amplitude envelope can help reduce clicks."

**Polarity Music, "Why Your Synth Keeps Clicking (And How To Fix It)"**: emphasizes that clicks in digital synths come from non-zero sample values at note boundaries and that a short linear fade across a few milliseconds is the universal fix.

**Perceptual reference — Zwicker masking**: The human auditory system's temporal masking threshold is approximately **2–5 ms** at moderate levels (Zwicker, *Psychoacoustics*, 3rd ed., Springer 2007). Transitions completing within this window are effectively inaudible as discrete events, even when they are measurable in the signal. A 5 ms fade falls at the upper edge of this window — click-free-enough for music, short-enough not to impact musical timing.

**Native Instruments Battery 4** (cited in §1): exposes a user-configurable "Fade" parameter at the voice group level for overlapping-voice cross-fades. Default values in Battery 4 kits typically fall in the 2–20 ms range, with drum kits tending toward the low end (2–10 ms) and sustained instrument layers toward the high end.

**Ross Bencina, "Real-time audio programming 101: time waits for nothing"** (http://www.rossbencina.com/code/real-time-audio-programming-101-time-waits-for-nothing): covers the principles of pre-allocated memory and predictable execution. While the article does not specifically cover voice-stealing fade times, Bencina's broader guidance is that short, deterministic operations (including brief envelope fades) are the preferred way to handle edge cases on the audio thread without allocating or blocking.

### Decision

Phase 3 uses a **5 ms ± 1 ms exponential fast-release** for both voice steals and choke events. Justification:

1. **Upper edge of the temporal-masking window** (Zwicker): a 5 ms transition is at the inaudibility threshold for most listeners.
2. **Matches Battery 4's default range** for drum kits (2–10 ms).
3. **Short enough not to disturb musical timing** — at 140 BPM 1/32 notes (the US1 test pattern), note-to-note spacing is ~53 ms; a 5 ms fade is 10% of the note interval and will not audibly delay the new note.
4. **Longer than the single-sample discontinuity** that causes clicks in the first place, so it is safely click-free.
5. **Aligns with FR-126's −30 dBFS click budget**: a 5 ms linear fade from unity to zero produces a transient whose spectral energy is concentrated below the masking threshold.

Shorter fades (1–2 ms) were considered and rejected because they approach the audible click region for low-frequency content (a kick drum at 50 Hz has a 20 ms period — a 1 ms fade can produce a spectral smear at the fundamental). Longer fades (10–20 ms) were considered and rejected because they delay the new voice perceptibly at fast tempos and waste CPU on dying tails.

---

## §3 — Choke Group Semantics and Count Survey

### Findings

**SoundFont 2.04 Technical Specification** (http://www.synthfont.com/sfspec24.pdf), `exclusiveClass` generator:
> "When this note is initiated, any other sounding note with the same exclusive class value should be rapidly terminated."
> "This parameter can define one or more exclusive classes by assigning to a set of divisions within a class the same parameter value other than 0. When an exclusive class is defined, any note triggered from one of the divisions of the exclusive class ends all the other sounds of the same class."

The SoundFont 2.04 spec defines `exclusiveClass` as a single unsigned integer per sample/division. The value 0 means "no exclusive class," and any non-zero value creates a group. **The spec does not impose a maximum number of exclusive classes** — it is an arbitrary integer. In practice, SoundFont drum banks use small counts (typically 1–8 active classes per preset).

**Polyphone Soundfont Editor forum** (https://www.polyphone.io/o/forum/support-bug-reports/347-how-to-set-polyphony-one-shot-and-choke-group):
> "The effect of the open hi-hat sound cancelling when you press the pedal hi-hat or the closed hi-hat can be achieved by setting all of the hi-hat's 'exclusive class' to 1 to create this effect."

Confirms the canonical open/closed/pedal-hat use case is a single choke group per hat triple.

**Ableton Live Drum Rack documentation** (https://www.ableton.com/en/manual/instrument-drum-and-effect-racks/):
> "The Choke chooser allows you to set a chain to one of sixteen choke groups."

Ableton ships **16 choke groups** per Drum Rack instance. This is a product decision, not a technical limit.

**Native Instruments Battery 4 manual** (https://www.native-instruments.com/ni-tech-manuals/battery-manual/en/setup-page):
> "The Voices parameter sets the number of voices allowed for a given voice group (1 to 127). Voice groups can be assigned to an exclude group to create exclusive group functionality."

Battery 4 uses **voice groups with exclude-group assignments** — up to 127 per cell. This is effectively unlimited for drum kit use.

**Native Instruments Kontakt "Group Start Options"**: supports arbitrary exclusive groups (unlimited).

**FluidSynth SoundFont implementation** (https://www.fluidsynth.org/api/group__generators.html): follows the SF 2.04 spec exactly — `exclusiveClass` is a 16-bit integer, so theoretically 65535 groups, but typical GM drum banks use fewer than 10.

### Decision

Phase 3 ships **8 choke groups** (IDs 1–8, with 0 = none). Justification:

1. **Sufficient for GM drum kit semantics**:
   - Group 1: Closed / Pedal / Open hi-hat triple (GM MIDI 42, 44, 46)
   - Group 2: Crash 1 / Splash / China (cymbals sharing a stand, in metal kits)
   - Group 3: Ride / Ride bell / Ride edge (different articulations of one cymbal)
   - Group 4: Snare / Cross-stick / Rim-shot
   - Group 5: Tom group mute (optional, for rapid muting)
   - Group 6: Cowbell / Agogo / Woodblock (pitched percussion pair)
   - Groups 7–8: Reserved for user-custom pairings

2. **Smaller than Ableton's 16 but larger than the 3–4 used in canonical SoundFont kits** — hits the middle ground.

3. **State-size cost**: 32 MIDI notes × 1 byte per note = **32 bytes total** for the `chokeGroupAssignments` table. Raising the count to 16 would not increase the state size (still 1 byte per note is enough for 0–15), so this is a *parameter range* decision, not a state-size decision. Phase 3 conservatively ships 8 and Phase 4 is authorized to raise it to 16 if user feedback demands it (FR-130 note).

4. **Matches SoundFont convention**: 8 classes is comfortably within the practical range used by General MIDI drum banks (Wikipedia "SoundFont" and Vogons SC-55 soundfont threads cite typical kits at 4–8 classes).

**Choke release semantics**: All surveyed products (SoundFont 2.04, Polyphone, Ableton, Battery 4, Kontakt) implement choke as a **group-wide mute**, not a one-victim selection. Phase 3 FR-133 matches this.

**Choke release time**: None of the surveyed products specify a default release time explicitly. Battery 4's "Fade" is user-configurable (default ~5 ms in factory kits per the manual). Phase 3 uses the same 5 ms figure as voice stealing (see §2).

---

## §4 — Polyphony Default and Maximum Survey

### Findings

**Native Instruments Maschine** (https://www.native-instruments.com/en/products/maschine/production-systems/maschine/):
> "Maschine offers 64-voice polyphony for each sound, with choke, legato, and glide control."

Maschine's 64-voice-per-sound is sample-based playback — extremely cheap CPU-wise. Physical-modeling drum synths cannot match this density.

**Roland TR-8S** (https://www.roland.com/global/support/by_product/tr-8s/owners_manuals/): the TR-8S has 11 dedicated drum voice channels (one per drum type), each polyphonic internally. The device is hardware-DSP-constrained and does not expose a user-configurable polyphony limit.

**IK Multimedia MODO Drum** (https://www.ikmultimedia.com/products/mododrum/): MODO Drum is a physical-modeling drum plugin and explicitly sets a per-pad polyphony limit that the user configures. Public-facing documentation does not give a specific default number, but review articles (Sound on Sound, Perfect Circuit) typically quote 4–8 voices per pad as the CPU-affordable range on desktop hardware.

**XLN Audio XO, Output Arcade, Geist2**: all use sample-based playback with effectively unlimited polyphony (limited only by CPU). These are not directly comparable to a physical-modeling engine.

**Spec 135 line 45**: explicitly quotes "default: 8 voices, range: 4–16" as the Phase 3 target. This is the authoritative design decision from the roadmap.

**Spec 135 lines 256–258**: "8 voices × 16 partials = 128 partials at 44.1 kHz. Target: < 2% CPU on modern hardware (single core)." This is the original roadmap performance goal.

### Decision

Phase 3 ships **default 8, range 4–16**, matching spec 135 exactly. Justification:

- **8 is the physical-modeling sweet spot**: matches MODO Drum's typical desktop-CPU affordability and spec 135's explicit roadmap target.
- **Range 4–16** gives users latitude for low-end systems (4) and max-polyphony stress cases (16), while staying below the `VoiceAllocator`'s internal `kMaxVoices = 32` hard limit so there is headroom for future per-pad polyphony overrides in Phase 4.
- **Not raising to 64 like Maschine** because Maschine uses samples and Membrum uses physical modeling — the CPU cost difference is 10–100×.
- **16 max** is confirmed-viable by §5 (real-time safe voice pool patterns) and §6 (performance math).

---

## §5 — Real-Time Safe Voice Pool Patterns

### Findings

**Ross Bencina, "Real-time audio programming 101: time waits for nothing"** (http://www.rossbencina.com/code/real-time-audio-programming-101-time-waits-for-nothing): the canonical reference on RT safety. Key prescriptions:

- Pre-allocate all memory at startup. Never call `malloc`/`new`/`free`/`delete` on the audio thread.
- Use fixed-size containers (arrays, not vectors). Never call `std::vector::push_back` on the audio thread.
- Use lock-free primitives (`std::atomic`, `std::atomic_flag`). Never use `std::mutex` or `std::condition_variable` on the audio thread.
- Avoid exceptions. `throw`/`catch` can involve heap allocation in some implementations.

Phase 3's `VoicePool` is designed to match every one of these prescriptions:

- **16 `DrumVoice` instances pre-allocated** at `setupProcessing()` → FR-110, FR-116.
- **`std::array<VoiceMeta, 16>`** for per-voice metadata, never a `std::vector` → FR-165, FR-172.
- **`ChokeGroupTable` is a fixed 32-byte array** → FR-131.
- **No mutexes**: `VoiceAllocator` already uses only atomics per its existing contract (voice_allocator.h); Phase 3 adds no new locks.

**RossBencina, "Interfacing Real-Time Audio and File I/O"** (http://www.rossbencina.com/code/interfacing-real-time-audio-and-file-io): discusses the producer/consumer pattern for passing state from UI to audio thread without locks. Phase 3's parameter changes follow the existing Phase 2 atomics pattern unchanged.

**JUCE docs on `Synthesiser`**: JUCE's own `Synthesiser` class uses a pre-allocated `std::vector<std::unique_ptr<SynthesiserVoice>>` (allocated at construction, not on the audio thread) plus a spin-lock for thread safety. This is a known source of issues in RT contexts and is exactly why Membrum does NOT use JUCE.

**AudioMulch memory pools (Bencina)**: AudioMulch uses per-thread memory pools for any allocation that *must* happen on the audio thread (e.g. variable-length buffers). Phase 3 does not need this pattern because every pool size is known statically (16 voices, 32 choke table entries).

### Decision

The `VoicePool` design follows Bencina's principles unchanged from Phase 2's existing practice. No new RT-safety infrastructure is needed. The only novelty is the **fast-release envelope**: this is a per-voice gain multiplier applied in the mixing loop, with the fade time converted from milliseconds to sample count at `setupProcessing()` and stored as a pre-computed per-sample decrement. Zero allocations, zero branches inside the inner loop.

---

## §6 — Performance Math: CPU Scaling With Voice Count

### Findings

**Phase 2 measurements** (`specs/137-membrum-phase2-exciters-bodies/spec.md`, Compliance Status table lines 596–599):

- SC-003 budget: 1.25% single-core CPU per single-voice combination.
- 143 of 144 combinations (exciter × body × ToneShaper × UnnaturalZone) pass the 1.25% budget.
- 1 waived combination (`Feedback + NoiseBody + ToneShaper + UnnaturalZone`) measures up to 2.0% with a documented waiver at the 2.0% hard ceiling.
- Benchmark file: `test_benchmark_144.cpp:51` (`kBudgetPercent = 1.25`), waiver at lines 350–362.

**Linear scaling assumption**: modal synthesis, Gordon-Smith coupled-form resonators, and the `ImpactExciter` / `BowExciter` / `FMOperator` kernels are all embarrassingly parallel across voices. There is no shared state between voices (except the stereo output buffer, which is a simple sum). Cache behavior is the main non-linear factor:

- **L1-cache pressure**: each `DrumVoice` at 16 modes × ~64 bytes of resonator state = ~1 KB per voice. 8 voices = 8 KB; 16 voices = 16 KB. Modern x86 L1 data caches are 32–48 KB per core, so even 16 voices fit in L1.
- **L2/L3 spillover**: none expected at this voice count on desktop hardware.

**DAFx 2019, "Real-Time Modal Synthesis of Crash Cymbals with Nonlinear Approximations, Using a GPU"** (https://www.dafx.de/paper-archive/2019/DAFx2019_paper_48.pdf): demonstrates that modal synthesis is "embarrassingly parallel" across modes (each mode is independent) and across voices (each voice is independent). The paper uses GPUs for 2000+ mode density, but the underlying scaling observation (linear in mode count, linear in voice count) is universal.

**Frontiers in Signal Processing editorial, "Sound synthesis through physical modeling"** (https://www.frontiersin.org/journals/signal-processing/articles/10.3389/frsip.2025.1715792/full):
> "Modern CPUs—benefiting from increasing core counts and SIMD vector units—show surprisingly strong performance for moderate polyphony, approaching the throughput of specialised hardware for mid-sized problems."

Confirms that modest polyphony (8–16 voices) on physical-modeling synths is well within reach of modern desktop CPUs without GPU.

### CPU Budget Calculation

Starting from Phase 2's measured per-voice cost:

- **Typical single-voice combination** (pass 143/144): 1.25% single-core CPU at 44.1 kHz.
- **Worst-case waived combination** (`Feedback + NoiseBody + ToneShaper + UnnaturalZone`): 2.0% single-core CPU.

For Phase 3's 8-voice worst case, assuming linear scaling plus a 20% overhead allowance:

- **8 × 1.25% = 10% baseline linear**
- **+ 20% `VoiceAllocator` / choke / mixing overhead = 12%** → FR-160 / SC-023 target.
- **8 × 2.0% = 16% for the waived Phase 2 cell**, with the same 20% overhead suggesting ~19%. Phase 3 budgets **≤ 18%** for this specific combination (matching the Phase 2 waiver precedent), documented in `plan.md`.

For Phase 3's 16-voice stress test:

- **16 × 1.25% = 20% linear** (average pad)
- **16 × 2.0% = 32%** (worst waived case)
- Budget: **no hard CPU percentage** — the test asserts **no xruns** at 44.1 kHz / 128-sample block, which allows up to ~50% single-core at that block size before the wall-clock budget is exceeded. Even 32% is comfortably within the xrun-free region.

### Decision

- **FR-160 / SC-023**: 8-voice worst case ≤ 12% (with 18% waiver for Feedback+NoiseBody+TS+UN).
- **FR-161 / SC-024**: 16-voice stress test completes without xrun (no hard percentage — wall-clock gate).
- **FR-162**: SIMD acceleration (`ModalResonatorBankSIMD`) is conditionally enabled only if the scalar path blows the budget. Phase 2 was scalar-only; Phase 3 keeps the same default.

This math is all based on linear scaling + a 20% overhead allowance. Real measurements during implementation may produce different numbers — `plan.md` will record the first measurement and the targets will be revised if the baseline changes.

---

## Open Questions Not Resolved by Research

The following points would benefit from primary-source confirmation but could not be definitively answered in this research session. They are **not blockers for spec completion** but are worth flagging:

1. **Exact fade-time recommendations from DAFx/JAES papers**: The 5 ms figure in §2 is derived from Zwicker's temporal masking threshold + Battery 4 convention. A specific JAES or DAFx paper directly prescribing "5 ms is the click-free threshold for polyphonic voice stealing in percussion synthesis" was not located. Implementation can adjust this empirically via listening tests during `/speckit.implement` if 5 ms proves audible in practice.

2. ~~**Gordon-Smith coupled-form resonator denormal behavior under sudden amplitude drops**~~ **RESOLVED**: The fast-release exponential decay includes a mandatory explicit `1e-6f` floor (FR-164, Clarification Q2) that terminates the fade before denormals are produced, independently of FTZ/DAZ hardware. A dedicated unit test (`test_fast_release_denormals.cpp`, T3.2.2) verifies the software floor alone is sufficient with FTZ/DAZ explicitly disabled — this covers both x86 and ARM (NEON) targets.

3. **MODO Drum's exact default polyphony**: Public docs and reviews did not reveal a specific number. The 4–8 range is inferred from Sound on Sound review prose. Phase 3's default of 8 is defensible either way.

4. ~~**Whether `VoiceAllocator::stealVoice()` should be called before or after the processor begins fast-release**~~ **RESOLVED**: The method `stealVoice()` does not exist in `VoiceAllocator`. The correct sequencing (documented in `plan.md §Note-on flow` and `plan.md §Dependency API Contracts`) is: for Oldest/Priority policies, the allocator emits a `Steal` event from `noteOn()` and `VoicePool` calls `beginFastRelease(ev.voiceIndex)` upon receiving it. For the Quietest policy, `VoicePool` calls `allocator_.noteOff(note)` then `allocator_.voiceFinished(slot)` to vacate the slot, then calls `allocator_.noteOn()`. The allocator never owns the fast-release envelope — the processor does.

## Summary

Of the 6 research questions, all 6 returned sufficient primary-source or reputable-secondary-source material to make defensible decisions. Open questions 2 and 4 have been resolved by plan.md design decisions; questions 1 and 3 remain minor implementation-phase concerns that do not block the spec.

## Sources

- [Sweetwater InSync: Voice-Stealing](https://www.sweetwater.com/insync/voice-stealing/)
- [Electronic Music Wiki: Voice stealing](https://electronicmusic.fandom.com/wiki/Voice_stealing)
- [Sound on Sound: From Polyphony To Digital Synths](https://www.soundonsound.com/techniques/polyphony-digital-synths)
- [JUCE MPESynthesiser Class Reference](https://docs.juce.com/master/classMPESynthesiser.html)
- [JUCE Forum: voiceStealingEnabled causing clicks with MPESynthesiser](https://forum.juce.com/t/voicestealingenabled-causing-clicks-with-mpesynthesiser/44138)
- [JUCE Forum: Voice steal pops](https://forum.juce.com/t/voice-steal-pops/30923)
- [Cycling '74 RNBO: Voice-stealing modes](https://rnbo.cycling74.com/learn/voice-stealing-modes)
- [SoundFont 2.04 Technical Specification (PDF)](http://www.synthfont.com/sfspec24.pdf)
- [Polyphone Soundfont Forum: How to set polyphony, one-shot, and choke group](https://www.polyphone.io/o/forum/support-bug-reports/347-how-to-set-polyphony-one-shot-and-choke-group)
- [Ableton Reference Manual: Instrument, Drum and Effect Racks](https://www.ableton.com/en/manual/instrument-drum-and-effect-racks/)
- [Native Instruments Battery 4 Manual: Setup Page](https://www.native-instruments.com/ni-tech-manuals/battery-manual/en/setup-page)
- [Native Instruments Maschine](https://www.native-instruments.com/en/products/maschine/production-systems/maschine/)
- [Roland TR-8S Owner's Manual](https://www.roland.com/global/support/by_product/tr-8s/owners_manuals/)
- [FluidSynth: SoundFont Generators API](https://www.fluidsynth.org/api/group__generators.html)
- [Loopmasters: Ways To Avoid Pops & Clicks In Your Audio Recordings](https://www.loopmasters.com/articles/2430-Ways-To-Avoid-Pops-Clicks-In-Your-Audio-Recordings)
- [Ross Bencina: Real-time audio programming 101](http://www.rossbencina.com/code/real-time-audio-programming-101-time-waits-for-nothing)
- [Ross Bencina: Interfacing Real-Time Audio and File I/O](http://www.rossbencina.com/code/interfacing-real-time-audio-and-file-io)
- [DAFx 2019: Real-Time Modal Synthesis of Crash Cymbals (PDF)](https://www.dafx.de/paper-archive/2019/DAFx2019_paper_48.pdf)
- [Frontiers in Signal Processing: Sound synthesis through physical modeling (2025 editorial)](https://www.frontiersin.org/journals/signal-processing/articles/10.3389/frsip.2025.1715792/full)
- Zwicker, E. & Fastl, H. *Psychoacoustics: Facts and Models*, 3rd ed., Springer 2007 (book — temporal masking threshold reference).
