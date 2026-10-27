# Phase 14 plan rulings (user, 2026-10-26)

Written by the main loop after grilling the user on plan §11 Q1–Q4, the R5 amendments and tasks Deviation 1.
Each decision below is the user's answer, verbatim in substance.

| Ruling | Decision | Matches the plan's reading? |
|---|---|---|
| **R1 = P-1 / §11 Q4** | **Store and forward.** `setMorphPosition` stores the sanitised base `b` AND forwards it; `prepare` / `reset` re-hand `b` immediately before the morph snap. | Yes |
| **R2 = P-3 / §11 Q3** | **Always reset.** Every successful `restoreMorphRuntime` (preset or state load) resets the modulator, also mid-stream under a held note (the envelope restarts, the Cycle re-phases). | Yes |
| **R3 = P-5 / §11 Q1** | **Accept the frame cadence.** The live indicator updates when a spectrum frame is staged (each completed window while a note sounds, every 0.25 s in silence, `spectrum_producer.h:47`); no morph-only republish; the manual says so. | Yes |
| **R4 = P-9 / §11 Q2** | **Rule (ii): convert EVERY eligible carrier** the probe finds (not one per column). `kMorphModConversionRule` = rule (ii). Every converted preset must pass every Phase 13 gate (claims, loudness, distinctness, breadth caps, MorphEndpoints, MorphSweep, held load, render guard) and every pre-existing coverage column stays covered. | Rule (ii) is one of the two built options (ruling table: "both are built; the ruled one is the constant in T023"), so no re-cut |
| **R5 = P-2, P-4, P-6, P-7, P-8** | **Accept as written.** P-2 engine byte patch of blob bytes 1–4; P-4 + tasks Deviation 7 hard-coded count sites move (80→89, 288→320, 574→610, 84→93); P-6 confound recorded in the FR-031 1003 row with both arms' `smoothedMass` logged; P-7 three appended event kinds (`MorphModulation`, `MorphModWheel`, `MorphModBypass`); P-8 dsp/ fence names the existing test files appended to. | Yes |
| **R6 = Deviation 1** | **Accept.** The FR-031 / §5.6 read window is the D_nl reader's frame `[1.0 s, 1.0 s + W)`, W = 262 144 samples; the precondition window likewise; bar unchanged (3 dB). | Yes |

Earlier stage decisions (spec Clarifications, Session 2026-10-26) stand unchanged: Q1–Q8, C1 (dsp/ scope confirmed), C10 (state v3, 610 bytes).
