# Profundum Phase 9: listening sheet (FR-071, SC-035)

One entry per **Listen** row of spec FR-070 (H-1, H-2, H-8, H-10, H-14, H-19, H-28 x2). The renders come from the hidden
case `VoiceEngine_ListeningRenders` (`[.listen]`, `dsp/tests/unit/systems/profundum_engine_probe_test.cpp`), run by
`bash specs/profundum-phase9-voice-engine/evidence/close/listen.sh`.

**Render setup (every row):** the real `ProfundumEngine`, MONO, 48 kHz, host block 64, default config (PerVoice placement,
dynamics oversampling on), output ceiling -1 dBTP. Files are 32-bit float stereo WAVs in `evidence/listen/`. A row is a run
of segments. Each segment uses a fresh engine: note-on at the segment start, the note held for `hold` seconds (with any
retriggers or morph toggles inside the hold), note-off, a 0.75 s tail, then 0.25 s of silence. So each segment lasts hold + 1 s.
The base state of every segment is **A_s**: the showcase A endpoint (`showcasePair().first`: kSineAnchor recipe, excitation 0,
Fold 0, Character Tube at Amount 0, Drop 0, SOLID, Mass 50, Anchor Sine / Lock at level 1, amp attack 1 ms / release 50 ms)
with amp sustain 1 (plan S1 C-8). Each row lists only what it changes. `evidence/listen/manifest.txt` holds the measured
segment start times, peak dBFS and finite check per file.

**Verdicts are the user's (SC-035).** An agent never fills a "Verdict (user)" line. A verdict that asks for a change becomes
a ruling in the owning FR / SC row (FR-071). For H-28 a defect verdict opens a Phase 5 / Phase 3 fix outside this phase (OQ-9).

---

## H-1: excitation split hump (Phase 2, roadmap line 280)

- **Render:** `evidence/listen/h01-split-hump.wav` (16 s)
- **Setting:** A_s with the kSawAnchor recipe. The excitation mix is 0 or 1 (drive 0.5, asymmetry 0, the defaults).

  | Start | Hold | Segment |
  |---|---|---|
  | 0 s | 3 s | E1 saw, excitation mix 0 |
  | 4 s | 3 s | E1 saw, excitation mix 1 |
  | 8 s | 3 s | A1 saw, excitation mix 0 |
  | 12 s | 3 s | A1 saw, excitation mix 1 |

- **Question:** Is the +1.2 ... +1.6 dB hump at 500-550 Hz on the excitation's linear path audible when the excitation
  engages (mix 0 -> 1)? If so, does it read as a colour you would keep, or as a boxy or honky resonance that should be removed?
- **Verdict (user, 2026-10-23):** "colour I would keep"
## H-2: "breathing" at C3 through the Fold (Phase 3, roadmap line 328)

- **Render:** `evidence/listen/h02-fold-breathing-c3.wav` (61 s)
- **Setting:** A_s with the kSawAnchor recipe and Fold 100. C3 is held for 60 s (0 s to 60 s). The soak records the drift (SC-013).
- **Question:** Over the 60 s hold, do you hear a slow movement in the timbre (the core partials' phase walk, about 0.59 deg per
  60 s seen through the Fold)? If so, is it musically welcome (the user's earlier note) or a defect to pin?
- **Verdict (user, 2026-10-23):** "I do not hear a slow movement in the timbre, it just sounds like a continuous note to me"
## H-8: Anchor raw level swing on hard snaps (Phase 4, roadmap line 372)

- **Render:** `evidence/listen/h08-drop-snaps-e0.wav` (18 s)
- **Setting:** A_s with Drop 48 st. The note is E0, retriggered every 1 s (8 snaps per segment).

  | Start | Hold | Segment |
  |---|---|---|
  | 0 s | 8 s | E0, Drop 48 st, T 10 ms (the worst case, -8.6 dB raw Anchor level swing) |
  | 9 s | 8 s | E0, Drop 48 st, T 100 ms (the default time, as a reference) |

- **Question:** On the 10 ms snaps, does the Anchor's level dip or swing during the drop read as a gap, a pump or a click in
  the sub? Or does it sit inside the snap's character? Compare it with the 100 ms reference.
- **Verdict (user, 2026-10-23):** "It sounds like a pump to me, it fits the character, the 100ms version is more musical I think"
## H-10: a note after a Drop against a fresh note (Phase 5 handoff (1), roadmap line 425)

- **Render:** `evidence/listen/h10-after-drop-vs-fresh.wav` (8 s)
- **Setting:** A_s with kSawAnchor, Fold 100 and Character Tube at Amount 1 (stages that depend on the waveform).

  | Start | Hold | Segment |
  |---|---|---|
  | 0 s | 3 s | E1 fresh note, Drop 0 |
  | 4 s | 3 s | E1 with a 24 st / 100 ms Drop (compare the part after the drop has landed) |

- **Question:** Once the drop has landed, does the held note sound different from the fresh one (a different timbre or growl,
  because the partial phases are kept)? If so, does that matter? A "yes, fix it" verdict records a possible Phase 4 amendment.
  It is not made in this phase.
- **Verdict (user, 2026-10-23):** "The held note sounds the same for both"
## H-14: sub-only voice at Mass 100; reference / saw voices (Phase 6 handoff (3))

- **Render:** `evidence/listen/h14-mass100-voices.wav` (15 s)
- **Setting:** A_s with Mass 100. Each segment is E1 with a retrigger at 2 s (two attacks per segment).

  | Start | Hold | Segment |
  |---|---|---|
  | 0 s | 4 s | sub-only voice (kSineAnchor). It sits on the limiter, steady GR about -0.18 dB |
  | 5 s | 4 s | reference voice (kDefaultControls recipe) |
  | 10 s | 4 s | saw voice (kSawAnchor). The limiter takes 5.8-7.9 dB for about 0.5 s at the attack |

- **Question:** At Mass 100, does the sub-only voice sound squashed or "stuck" on the limiter? On the reference and saw voices,
  is the 0.5 s of heavy limiting after each attack audible as pumping or a flattened transient? If so, is it acceptable?
- **Verdict (user, 2026-10-23):** "No pumping, all three sound good"
## H-19: Fold / Rectify at the WIDE offset ceilings (Phase 7 handoff (5))

- **Render:** `evidence/listen/h19-wide-ceiling-fold-rectify.wav` (16 s)
- **Setting:** A_s with kSawAnchor, Character Amount 1, and WIDE at Spread 1 (the offset ceiling: 0.15 for Fold, 0.10 for
  Rectify). The note is E1.

  | Start | Hold | Segment |
  |---|---|---|
  | 0 s | 3 s | Fold, Anchor level 1 |
  | 4 s | 3 s | Fold, Anchor level 0 |
  | 8 s | 3 s | Rectify, Anchor level 1 |
  | 12 s | 3 s | Rectify, Anchor level 0 |

- **Question:** In stereo, and again summed to mono, does the fundamental sound hollow, phasey or thin? With the Anchor at 0,
  the regenerated fundamental is inverted between the channels. Does the Anchor (level 1) bring the sub back to a solid centre?
- **Verdict (user, 2026-10-23):** "Anchor brings back the sub to a solid center, no hollow sounds"
## H-28 (1): Fracture -> Crush / Rectify switch at C3 (Phase 8 handoff 9.11; OQ-9)

- **Render:** `evidence/listen/h28a-fracture-switch-c3.wav` (14 s)
- **Setting:** A_s with kSawAnchor and Character Amount 1. Endpoint A is Fracture and endpoint B is the target. The morph
  position flips A <-> B every 1 s with a 5 ms ramp (switches at +1, +2, +3, +4 and +5 s into each segment). The note is C3.

  | Start | Hold | Segment |
  |---|---|---|
  | 0 s | 6 s | Fracture <-> Crush |
  | 7 s | 6 s | Fracture <-> Rectify |

- **Question:** Does any switch click or thump? This is the component's own switch, which this phase does not re-measure (OQ-9).
  A "defect" verdict opens a Phase 5 fix outside this phase.
- **Verdict (user, 2026-10-23):** "No clicks or thumps"
## H-28 (2): OctaveDownSine -> each other Anchor waveform at E1 (Phase 8 handoff 9.11; OQ-9)

- **Render:** `evidence/listen/h28b-octavedown-switch-e1.wav` (28 s)
- **Setting:** A_s with Anchor level 1. Endpoint A has the Anchor waveform OctaveDownSine and endpoint B has the target. The
  morph position flips every 1 s with a 5 ms ramp. The note is E1.

  | Start | Hold | Segment |
  |---|---|---|
  | 0 s | 6 s | OctaveDownSine <-> Sine |
  | 7 s | 6 s | OctaveDownSine <-> Triangle |
  | 14 s | 6 s | OctaveDownSine <-> RoundedSine |
  | 21 s | 6 s | OctaveDownSine <-> AsymmetricSine |

- **Question:** Does any waveform switch click or thump in the sub? This is the Anchor's own switch, which this phase does not
  re-measure (OQ-9). A "defect" verdict opens a Phase 3 fix outside this phase.
- **Verdict (user, 2026-10-23):** "No clicks or thumps"