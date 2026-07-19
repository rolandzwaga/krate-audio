# Ruinae + KrateDSP Remediation — Mechanical Implementation Plan

This plan turns the 42 verified findings into ordered, copy-executable tasks. Execute phases **strictly in order (1 → 6)**. Within a phase, execute tasks **top to bottom** — earlier tasks are ordered so their diffs are not invalidated by later ones.

Two finding pairs describe the same defect and are merged: **F006+F010** (reverb crossfade double-process) and **F013+F014** (OSC A/B param duplication). Task IDs below reference the originating finding IDs.

---

## 0. Global rules (read once, apply to EVERY task)

**0.1 TDD is mandatory.** For every task, write the test FIRST, build, run it, and confirm it FAILS for the stated reason, THEN apply the fix and confirm it PASSES. Do not write the fix before the failing test exists. (Project rule: "Write failing test FIRST before fixing.")

**0.2 Read before edit.** Before editing any file, `Read` the exact region named in the task and confirm the line numbers still match. Line numbers here come from the finding snapshot and may drift by a few lines after earlier tasks in the same phase. Never edit from memory.

**0.3 Reusable command blocks** (Git Bash / Bash tool; use these verbatim):

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# --- RUINAE build + test ---
"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests
build/windows-x64-release/bin/Release/ruinae_tests.exe 2>&1 | tail -5
# One case only:  build/windows-x64-release/bin/Release/ruinae_tests.exe "CaseName*" 2>&1 | tail -5

# --- DSP build + test (build only the layer you touched, then run it) ---
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_systems_tests dsp_effects_tests
for t in dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_systems_tests dsp_effects_tests; do build/windows-x64-release/bin/Release/$t.exe 2>&1 | tail -3; done

# --- Cross-plugin regression (run when a shared dsp/ header changed) ---
"$CMAKE" --build build/windows-x64-release --config Release --target plugin_tests disrumpo_tests ruinae_tests innexus_tests gradus_tests membrum_tests shared_tests
for t in plugin_tests disrumpo_tests ruinae_tests innexus_tests gradus_tests membrum_tests shared_tests; do build/windows-x64-release/bin/Release/$t.exe 2>&1 | tail -3; done
```

**0.4 New test file registration:**
- DSP tests: add the new `.cpp` to the matching target in `dsp/tests/CMakeLists.txt` (sources are listed explicitly, not globbed) or it silently drops.
- Ruinae tests: add under `plugins/ruinae/tests/unit/<section>/` and to `plugins/ruinae/tests/CMakeLists.txt` if not globbed.

**0.5 ⚠ SHARED-HEADER WARNING template.** Any task marked **[SHARED KrateDSP]** edits a header under `dsp/include/krate/dsp/` consumed by Iterum, Disrumpo, Innexus, Gradus, and Membrum. For those tasks you MUST run the **Cross-plugin regression** block (0.3) in addition to the touched DSP layer, and confirm all targets green before proceeding.

**0.6 Zero warnings.** Every build must be warning-free (C4244 → add `f`; C4267 → explicit cast; C4100 → `[[maybe_unused]]`). Fix warnings before running tests.

**0.7 Do NOT** `git commit --amend`, do NOT push, and do NOT relax any test threshold to make a test pass.

---

## 1. Summary table

| Task | Finding(s) | Severity | Category | Primary file(s) | Size |
|------|-----------|----------|----------|-----------------|------|
| **T1.1** | F001 | critical | bug (UAF) | `plugins/ruinae/src/controller/controller.cpp` | S |
| **T2.1** | F002 | high | bug (UAF) | `controller_view_sync.cpp`, mod-source display headers | M |
| **T2.2** | F003 | high | bug (UAF) | `controller/controller_settings.cpp` | M |
| **T2.3** | F004 | high | bug | `dsp/include/krate/dsp/systems/voice_allocator.h` **[SHARED]** | M |
| **T2.4** | F019 | medium | rt-safety | `processor/processor_messaging.cpp`, `processor.cpp`, `processor_state.cpp` | M |
| **T2.5** | F042 | low | rt-safety | `processor/processor.cpp`, `processor_messaging.cpp` | M |
| **T2.6** | F006, F010 | medium | bug | `engine/ruinae_effects_chain.h` | S |
| **T2.7** | F011 | medium | wrong-impl | `engine/ruinae_effects_chain.h` | M |
| **T2.8** | F007 | medium | bug | `engine/ruinae_engine.h` | S |
| **T2.9** | F008 | medium | wrong-impl | `engine/ruinae_voice.h` | M |
| **T2.10** | F015 | medium | bug (preset restore) | `processor/processor_params.cpp` | M |
| **T2.11** | F005 | medium | bug | `engine/ruinae_voice.h` | S |
| **T2.12** | F025 | low | bug | `dsp/.../systems/poly_synth_engine.h` **[SHARED]** | S |
| **T2.13** | F027 | low | bug | `dsp/.../processors/ring_modulator.h` **[SHARED]** | S |
| **T2.14** | F028 | low | bug | `dsp/.../primitives/lfo.h` **[SHARED]** | S |
| **T2.15** | F029 | low | wrong-impl | `dsp/.../primitives/chaos_waveshaper.h` **[SHARED]** | S |
| **T2.16** | F022 | low | wrong-impl (doc) | `plugins/shared/src/midi/midi_event_dispatcher.h` | S |
| **T2.17** | F024 | low | wrong-impl (dead) | `plugin_ids.h`, `parameters/arpeggiator_params.h` | S |
| **T2.18** | F009 | medium | bug (dup ID) | `plugins/ruinae/src/plugin_ids.h` | S |
| **T2.19** | F021 | low | bug (sentinel) | `plugins/ruinae/src/plugin_ids.h` | S |
| **T3.1** | F013, F014 | medium | duplication | `parameters/osc_a_params.h`, `osc_b_params.h`, new `osc_params.h` | L |
| **T3.2** | F030 | low | duplication | `parameters/dropdown_mappings.h`, osc param headers | M |
| **T3.3** | F035 | low | duplication | `processor/processor_params.cpp` | M |
| **T3.4** | F031 | low | duplication | `parameters/chorus_params.h`, `flanger_params.h`, new module | L |
| **T3.5** | F032 | low | duplication | `processor/processor_params.cpp` | S |
| **T3.6** | F033 | low | duplication | `parameters/*_params.h`, `plugins/shared/src/ui/parameter_helpers.h` | M |
| **T3.7** | F023 | low | duplication | `controller_view_sync.cpp`, `controller_mod_matrix.cpp`, new header | S |
| **T4.1** | F017 | medium | efficiency | `dsp/.../processors/phaser.h` **[SHARED]** | S |
| **T4.2** | F018 | medium | efficiency | `dsp/.../processors/phaser.h` **[SHARED]** | M |
| **T4.3** | F041 | low | efficiency | `dsp/.../processors/phaser.h`, `flanger.h` **[SHARED]** | S |
| **T4.4** | F016 | medium | efficiency | `dsp/.../processors/formant_filter.h` **[SHARED]** | M |
| **T4.5** | F040 | low | simd/efficiency | `dsp/.../processors/ring_modulator.h` **[SHARED]** | M |
| **T4.6** | F039 | low | efficiency | `engine/ruinae_voice.h` | S |
| **T4.7** | F020 | low | efficiency | `engine/ruinae_engine.h`, `dsp/.../systems/poly_synth_engine.h` **[SHARED]** | M |
| **T5.1** | F012 | medium | anti-pattern | `engine/ruinae_effects_chain.h` | L |
| **T5.2** | F026 | low | anti-pattern | `dsp/.../systems/harmonizer_engine.h` **[SHARED]** | S |
| **T5.3** | F038 | low | anti-pattern | `engine/ruinae_effects_chain.h`, `processor/processor.cpp` | S |
| **T5.4** | F036 | low | anti-pattern | `processor/processor.cpp` | S |
| **T5.5** | F037 | low | anti-pattern | `processor_params.cpp`, `processor.cpp`, `controller.cpp` | M |
| **T5.6** | F034 | low | anti-pattern | `processor/processor_params.cpp` | L |
| **T6.1** | — | — | validation | pluginval strictness 5 (Ruinae) | S |
| **T6.2** | — | — | validation | clang-tidy `-Target ruinae` and `-Target dsp` | S |

---

# PHASE 1 — Critical bugs

## T1.1 — Null the 9 mod-source/sidechain view pointers in `willClose` (F001)

**Files:** `plugins/ruinae/src/controller/controller.cpp` (function `Controller::willClose`, lines 631–734; confirmed nulling block ends at line 711 with `spectralBitsGroup_ = nullptr;`).

**Problem:** `willClose()` nulls ~60 frame-owned view pointers but omits 9: `lfo1WaveformDisplay_`, `lfo2WaveformDisplay_`, `chaosModDisplay_`, `runglerDisplay_`, `sampleHoldDisplay_`, `randomModDisplay_`, `sidechainIndicatorEnvFollower_`, `sidechainIndicatorPitchFollower_`, `sidechainIndicatorTransient_`. After the frame is destroyed these dangle; `setComponentState()` → `syncAllViews()` (controller.cpp:296) and `setParamNormalized()` (no `activeEditor_` guard) dereference them behind `if(ptr)` guards a dangling non-null pointer passes → use-after-free.

**Test FIRST** (`plugins/ruinae/tests/unit/controller/willclose_nulls_test.cpp`, add to CMake):
- Build the editor-lifecycle harness path (headless open → `willClose`). After `willClose`, assert each of the 9 accessors is null. Concretely: expose the pointers via a test hook OR reuse the existing headless open/close harness and, under ASan/valgrind, exercise: open MOD tab → close editor → call `setComponentState()` with a preset stream → confirm no dangling access. In Release this passes by luck; the behavioral assertion is `getRunglerDisplayPtr() == nullptr` etc. after `willClose`. If no accessor exists, add a private-friend test accessor rather than making members public.
- Confirm it FAILS (pointers non-null after close) before the fix.

**Fix:** Inside the `if (activeEditor_ == editor)` block, immediately after line 689 (`spectralBitsGroup_ = nullptr;`), add **nine individual assignments** (NOT a chained assignment — the members are heterogeneous pointer types and chaining will not compile):

```cpp
lfo1WaveformDisplay_ = nullptr;
lfo2WaveformDisplay_ = nullptr;
chaosModDisplay_ = nullptr;
runglerDisplay_ = nullptr;
sampleHoldDisplay_ = nullptr;
randomModDisplay_ = nullptr;
sidechainIndicatorEnvFollower_ = nullptr;
sidechainIndicatorPitchFollower_ = nullptr;
sidechainIndicatorTransient_ = nullptr;
```

**Build+test:** RUINAE block (0.3). If an ASan run is used, follow the ASan build steps in root CLAUDE.md (separate `build-asan` dir, Debug config).

**Do NOT:**
- Do NOT write `a_ = b_ = c_ = nullptr;` — different pointer types, will not compile.
- Do NOT null them inside `onTabChanged` only (that is T2.2's separate concern) — `willClose` is a distinct teardown path and must null them itself.
- Do NOT delete or reorder the existing ~60 assignments.

**Done when:** the 9 pointers are null after `willClose`; new test passes; `ruinae_tests` fully green; zero warnings.

---

# PHASE 2 — High-severity bugs, RT-safety, and remaining correctness bugs

> Ordering note: T2.1–T2.3 finish the UAF cluster started in Phase 1. T2.17→T2.18→T2.19 all edit `plugin_ids.h` and MUST run in that order (T2.17 deletes IDs that remove two collisions at the source, simplifying T2.18/T2.19).

## T2.1 — Null mod-source pointers on `ModSourceViewMode` switch (F002)

**Files:**
- `plugins/ruinae/src/controller/controller_view_sync.cpp` (`setParamNormalized`, existing tag-handling block near line 78; derefs at 311–408, 756–827).
- `plugins/ruinae/src/controller/controller_verify_view.cpp` (pointer-capture sites ~707–849).
- Mod-source display headers (shared UI): `rungler_display.h`, `lfo_waveform_display.h`, `chaos_mod_display.h`, `sample_hold_display.h`, `random_mod_display.h`.

**Problem:** The 9 views live inside the `ModSourceViewMode` `UIViewSwitchContainer` (tag `kModSourceViewModeTag` = 10019, `editor.uidesc` 2480–2483). Switching that dropdown destroys the shown template's children, but `setParamNormalized` has **no** `kModSourceViewModeTag` handler, so cached raw pointers dangle → UAF on the next automation/preset sync.

**Test FIRST:** Editor-lifecycle test (ASan/valgrind): open MOD tab → cycle `ModSourceViewMode` across all 10 values → call `syncAllViews()` (simulating preset load). Assert no dangling access / pointers are null after each switch. Confirm it FAILS pre-fix.

**Fix — choose the tag-based path (mechanical, lower risk):**
In `setParamNormalized` (controller_view_sync.cpp), next to the existing `kMainTabTag`/`kOscATypeId` nulling and **before** the `bulkParamLoad_` early-return (so it also runs during preset loads), add:

```cpp
if (tag == kModSourceViewModeTag) {
    lfo1WaveformDisplay_ = nullptr;
    lfo2WaveformDisplay_ = nullptr;
    chaosModDisplay_ = nullptr;
    runglerDisplay_ = nullptr;
    sampleHoldDisplay_ = nullptr;
    randomModDisplay_ = nullptr;
    sidechainIndicatorEnvFollower_ = nullptr;
    sidechainIndicatorPitchFollower_ = nullptr;
    sidechainIndicatorTransient_ = nullptr;
}
```

**Do NOT:**
- Do NOT change the registered VST3 parameter type of `kModSourceViewModeTag` (memory: swapping param type at the same ID breaks host editor load).
- Do NOT rely on `if(ptr)` guards downstream to "protect" — a dangling pointer is non-null and passes them.
- If you instead add `removed()` callbacks to the shared display classes, the three sidechain `CTextLabel`s have no project removed-callback and STILL need the tag-based nulling above — do not skip them.

**Build+test:** RUINAE block. (Tag path touches only Ruinae controller sources.)

**Done when:** `ModSourceViewMode` switch nulls all 9; ASan lifecycle test passes; `ruinae_tests` green.

---

## T2.2 — Extend `onTabChanged` to null the remaining MOD-tab residents (F003)

**Files:** `plugins/ruinae/src/controller/controller_settings.cpp` (`Controller::onTabChanged`, MOD-tab nulling block lines 37–45, currently nulls only `lfo1WaveformDisplay_`/`lfo2WaveformDisplay_`/`chaosModDisplay_`).

**Problem:** On a MainTab switch away from MOD, the MOD template + its children are destroyed, but `runglerDisplay_`, `sampleHoldDisplay_`, `randomModDisplay_`, and the three sidechain indicators are not nulled → dangling. The `kSidechainActiveId` branch (view_sync 403–408) fires on any sidechain-active param change regardless of tab and touches all three sidechain labels.

**Test FIRST:** Lifecycle test: open MOD tab → switch MainTab to Sound → fire a `kSidechainActiveId` param change → assert no dangling access (ASan). Confirm FAIL pre-fix.

**Fix:** Extend the MOD-tab block in `onTabChanged` to also clear:

```cpp
runglerDisplay_ = nullptr;
sampleHoldDisplay_ = nullptr;
randomModDisplay_ = nullptr;
sidechainIndicatorEnvFollower_ = nullptr;
sidechainIndicatorPitchFollower_ = nullptr;
sidechainIndicatorTransient_ = nullptr;
```

Optional (preferred for maintainability, but keep behavior identical): extract a private `resetModTabViewPointers()` that nulls ALL MOD-tab pointers (the 9 above **plus** mod grid, ring indicators, sync/note-value groups already nulled here) and call it from `onTabChanged` and from the `kModSourceViewModeTag` branch added in T2.1. If you extract the helper, update T2.1's inline block to call it instead — do this now so later diffs are not invalidated.

**Do NOT:**
- Do NOT remove the existing lfo1/lfo2/chaos nulls.
- Do NOT null these in `onTabChanged` and assume T1.1's `willClose` no longer needs them — both teardown paths are independent and each must null.

**Build+test:** RUINAE block.

**Done when:** all MOD-tab residents nulled on tab switch AND mod-source switch; test green.

---

## T2.3 — Prevent duplicate voice-slot allocation in unison steal loop (F004) **[SHARED KrateDSP — systems layer]**

**Files:** `dsp/include/krate/dsp/systems/voice_allocator.h`, `allocateNote()` — victim-group loop 943–963, `while (allocCount < needed)` loop 966–992, final assignment loop 1017–1037. Finders `findIdleVoiceAny()` (585–594), `findStealVictimSingle()` (607–614), `findVictimOldest`.

**Problem:** The initial idle-scan (905–912) temporarily marks each found voice `Active` so it is not re-found. The victim-group loop (943–963) and the `while` loop (966–992) do NOT mark or reserve the voices they select; voices only become `Active` in the final loop (1017+). So `findIdleVoiceAny()` returns the SAME idle index every iteration and `findStealVictimSingle()`/oldest can re-pick the same still-Active voice → `allocated[]` gets duplicate indices → duplicate NoteOn for one slot, fewer distinct voices than requested. Reachable by raising `unisonCount_` while notes are held (`setUnisonCount` at 365–369 does not re-provision).

**Test FIRST** (`dsp/tests/unit/systems/voice_allocator_test.cpp`; register in `dsp/tests/CMakeLists.txt` under the systems target): set 8 voices, unison 1, hold notes so voices 0–4 active and 5–7 idle; raise unison to 4; issue a new NoteOn; assert (a) all indices in `allocated[]` are distinct, and (b) the number of `NoteOn` events equals the number of distinct voices. Confirm it FAILS (e.g. produces `[0,5,5,5]`).

**Fix:** Reserve every voice as it is committed to `allocated[]` in BOTH loops:
1. In the victim-group loop (943–963), after `allocated[allocCount++] = vi;`, mark `voices_[vi].state = Active` and set `voices_[vi].timestamp = timestamp_`.
2. In the `while` loop (966–992), after selecting `idx` (idle or steal), before `allocated[allocCount++] = idx;`, mark `voices_[idx].state = Active` and `voices_[idx].timestamp = timestamp_`, and guard against re-picking an index already present in `allocated[0..allocCount)` (continue/break if the finder returns a duplicate).

Marking `Active` alone is insufficient for the steal branch because `findVictimOldest` re-picks by timestamp — that is why the `timestamp_` write is required. Preferred robust alternative: add an exclusion-set check to `findIdleVoiceAny`/`findStealVictimSingle` (or a thin wrapper) against the current `allocated[]` set. The final assignment loop (1017+) already re-writes state/timestamp/note authoritatively, so these reservations do not corrupt the end result.

**Do NOT:**
- Do NOT mark only the `while`-loop voices — the victim-group voices at 943–963 must be reserved too, or the first `while` steal duplicates one.
- Do NOT skip the `timestamp_` update on stolen voices — `Active` alone leaves oldest-strategy free to re-pick.
- Do NOT change the public API of `allocateNote` / `setUnisonCount`.

**Build+test:** DSP systems layer (`dsp_systems_tests`), then the **Cross-plugin regression** block (0.5) — Ruinae/Gradus/Membrum/etc. all consume `voice_allocator.h`.

**Done when:** allocated indices are always distinct; NoteOn count == distinct voice count; `dsp_systems_tests` + all plugin test targets green.

---

## T2.4 — Move voice-route resync off the audio thread (F019) — RT-SAFETY

**Files:** `plugins/ruinae/src/processor/processor_messaging.cpp` (`sendVoiceModRouteState`, `setBinary` at 170), `processor.cpp` (init pre-warm 145–153; process()-side `needVoiceRouteSync_` exchange + call 675–678), `processor_state.cpp` (sets `needVoiceRouteSync_ = true` at 379 in `applyPresetSnapshot`, which runs on the audio thread via the RTTransfer drain).

**Problem:** `IAttributeList::setBinary` in the SDK reference (`hostclasses.cpp`) does `new char[sizeInBytes]` every call. The `initialize()` pre-warm only creates the map node; it does NOT make `setBinary` allocation-free. So `sendVoiceModRouteState()` heap-allocates 224 bytes on the audio thread once per preset load. The comment at processor.cpp:145–146 is false.

**Test FIRST:** Add a Ruinae RT-safety/integration test that drives a preset apply through the processor and asserts the route-sync path does not run `setBinary` on the audio thread. Practical approach: assert `needVoiceRouteSync_` is no longer set by `applyPresetSnapshot` (after the fix the flag/exchange are removed), or add an allocation-tracking hook around the process() call and assert zero audio-thread allocations across a preset apply. Confirm FAIL pre-fix.

**Fix:** Send the route payload from the message/UI thread instead of via `process()`:
1. Delete the `needVoiceRouteSync_` flag, its store in `applyPresetSnapshot` (processor_state.cpp:379), and the exchange+call block in `process()` (processor.cpp:675–678).
2. Build the 224-byte buffer and call `setBinary("routeData", …)` + `sendMessage(voiceRouteSyncMsg_)` from a message-thread context — after `setState()` queues the snapshot, or in a controller-initiated request handled in `notify()`.
3. Fix the misleading comment at processor.cpp:145–146.

**Do NOT:**
- Do NOT "solve" this by re-warming `setBinary` in `initialize()` and leaving the call in `process()` — the pre-warm does not prevent the per-call allocation.
- Do NOT call `allocateMessage()` on the audio thread.

**Build+test:** RUINAE block, then **T6.1 pluginval** at the end of the phase (processor source changed).

**Done when:** no `setBinary`/`sendMessage` on the audio path for route sync; preset load still updates the controller's route display; `ruinae_tests` green.

---

## T2.5 — Pre-warm skip-message keys and move handshake sends off the audio thread (F042) — RT-SAFETY

**Files:** `plugins/ruinae/src/processor/processor.cpp` (one-time sends 659/665/671; arp skip loop 433–435; setMessageID region ~134–139), `processor_messaging.cpp` (`sendSkipEvent`: `setInt("lane",…)`/`setInt("step",…)`/`sendMessage` 127–129).

**Problem:** `process()` calls `sendMessage()` (a synchronous host call) on the audio thread; and `sendSkipEvent` inserts new attribute-map nodes (allocations) the first time each of the 6 skip messages is sent, because `lane`/`step` keys are not pre-populated in `initialize()`.

**Test FIRST:** Extend the RT-safety test (or add one) that drives arp playback with skip steps and asserts no audio-thread allocation on the skip path (allocation hook, or assert the keys exist post-`initialize()`). Confirm FAIL pre-fix (first skip send per lane allocates).

**Fix:**
1. In `initialize()`, right after each `skipMessages_[i]->setMessageID(...)`, pre-warm: `attrs->setInt("lane", 0); attrs->setInt("step", 0);` for all 6 messages so runtime `setInt` only overwrites existing nodes.
2. Move the three one-time pointer handshakes (659/665/671) out of `process()` into `setActive(true)` (main thread); keep the sent-flags only as re-activation guards. The handshake pointers are lifetime-stable member atomics, so this is behavior-preserving.

**Do NOT:**
- Do NOT reorder or change the skip-event payload semantics — hosts/controller parse `lane`/`step`.
- Do NOT leave the recurring skip `sendMessage` inside the arp loop if you can route it through the existing output-parameter mechanism (playheads at processor.cpp:443–465); at minimum apply step 1 so no allocation occurs.

**Build+test:** RUINAE block; pluginval deferred to T6.1.

**Done when:** skip keys pre-warmed (no first-send allocation); handshakes issued from `setActive`; `ruinae_tests` green.

---

## T2.6 — Remove reverb-crossfade tail re-processing (F006, F010)

**Files:** `plugins/ruinae/src/engine/ruinae_effects_chain.h`, `processReverbSlot()` — incoming reverb processes whole block at line 1061; completion block re-processes the remainder at ~1076–1081.

**Problem:** The incoming reverb already processed all `numSamples` in-place (1061). On `reverbCrossfadeAlpha_ >= 1.0`, `completeReverbCrossfade()` sets `activeReverbType_ = incomingReverbType_`, then the code re-runs `processReverbType(activeReverbType_, left+i+1, right+i+1, remaining)` over the already-wet tail → double-reverberation + spurious extra state advancement. The delay path (901–907) correctly just breaks.

**Test FIRST** (Ruinae effects test): start a reverb-type crossfade with an increment large enough to complete mid-block; assert the post-completion tail samples equal a single-pass incoming-reverb render (not double-processed). Confirm FAIL pre-fix.

**Fix:** In the completion branch, after `completeReverbCrossfade();`, delete the `size_t remaining = numSamples - i - 1; if (remaining > 0) { processReverbType(...); }` block and replace with a plain `return;` (mirroring the delay path). Optional comment: `// Remaining samples are 100% incoming reverb (already in left/right).`

**Do NOT:**
- Do NOT reset or re-warm the incoming reverb — `completeReverbCrossfade` intentionally resets only the outgoing reverb.
- Do NOT change the per-sample blend loop above the completion check.

**Build+test:** RUINAE block; pluginval deferred to T6.1.

**Done when:** tail matches single-pass incoming render; `ruinae_tests` green.

---

## T2.7 — Route only wet through reverb equal-power crossfade (fix +3 dB dry swell) (F011)

**Files:** `plugins/ruinae/src/engine/ruinae_effects_chain.h`: `setReverbParams` (~646–650), `processReverbSlot` blend (1064–1068). Reference the delay slot's wet-only architecture (~210–221) and `userDelayMixSmoother_`.

**Problem:** Both reverbs receive the same `ReverbParams` (mix passed unchanged, default 0.3), so each output carries an identical correlated dry component. Equal-power blend gains sum to `cos+sin = 1.414` at midpoint, so the dry signal swells ~+3 dB during every reverb-type crossfade. The delay slot avoids this by forcing per-type delays to 100% wet and mixing dry externally.

**Test FIRST:** With a correlated input (DC or sine), start a reverb-type crossfade and assert summed output level stays within ~0.3 dB of steady state across the midpoint (currently rises ~3 dB). Confirm FAIL pre-fix.

**Fix:** Mirror the delay slot:
1. In `setReverbParams`, copy the params, force `mix = 1.0` before `reverb_.setParams()`/`fdnReverb_.setParams()`, and store the user's requested mix in a new member `userReverbMix_` with a `OnePoleSmoother` (like `userDelayMixSmoother_`).
2. Before `processReverbSlot` runs, `memcpy` the dry `left`/`right` into a saved-dry buffer.
3. After the equal-power blend of the two WET-only reverb outputs, apply the external dry/wet mix per sample: `out = dryGain*savedDry + wetGain*wet` with `{dryGain,wetGain} = equalPowerGains(smoothedUserReverbMix)`.

Note freeze (FR-029) still works (drives inputGain→0 on the wet path). Do this AFTER T2.6 (both touch `processReverbSlot`; T2.6 first).

**Do NOT:**
- Do NOT apply a single uniform gain to the already-morphed blended output — that cannot express independent A/B (here dry-vs-wet) balance and does not fix the correlated-dry double-count.
- Do NOT change the default reverb mix value semantics exposed to the user.

**Build+test:** RUINAE block; pluginval deferred to T6.1.

**Done when:** correlated-input level flat (±0.3 dB) across crossfade; `ruinae_tests` green.

---

## T2.8 — Make EffectMix and AllVoice* modulation self-heal at offset 0 (F007)

**Files:** `plugins/ruinae/src/engine/ruinae_engine.h`: `processBlock` EffectMix path (~878); `processBlockPoly` AllVoice* paths (1902–1950); `processBlockMono` AllVoice* paths (1999–2035). Reference the unconditional global-filter path (860–875).

**Problem:** Global-filter cutoff/resonance and master gain are recomputed unconditionally each block (self-heal when the offset returns to 0). But EffectMix and every `AllVoice*` path are gated by `if (offset != 0.0f)`, so when a mod route is removed/disabled/depth→0, the last modulated value is orphaned (delay mix / voice cutoff / morph / gate rate / tilt / resonance / filter-env stay stuck) until the user re-touches the base param.

**Test FIRST:** Apply a nonzero offset to EffectMix (and one `AllVoiceFilterCutoff`), process a block, then set the offset to 0, process again; assert the delay mix / voice cutoff return to their base values. Confirm FAIL pre-fix.

**Fix:** Remove the `if (offset != 0.0f)` guards on these paths so they mirror the self-healing global-filter path:
1. EffectMix (~878): always `effectsChain_.setDelayMix(std::clamp(baseDelayMix_ + effectMixOffset, 0.0f, 1.0f));`
2. In both `processBlockPoly` and `processBlockMono`, drop each `if (allVoice*Offset != 0.0f)` guard and always compute `base (+/×) offset` and forward. `semitonesToRatio(0)=1` and `+0` leave the base unchanged.

If per-block forwarding to all voices is a perf concern, the fallback is a `wasNonZeroLastBlock` flag per destination that re-forwards the base once on the 1→0 transition — but prefer the unconditional path (matches the global-filter reference and is the same class of work already done there).

**Do NOT:**
- Do NOT "fix" this by writing `baseDelayMix_` back from the modulated value — the base must remain the user's value.
- Do NOT leave any of the ~7 destinations guarded; fix all of them consistently.

**Build+test:** RUINAE block; pluginval deferred to T6.1.

**Done when:** all listed destinations return to base at offset 0; `ruinae_tests` green.

---

## T2.9 — Apply OSC A/B levels to SpectralMorph input (F008)

**Files:** `plugins/ruinae/src/engine/ruinae_voice.h`: SpectralMorph branch feeding `spectralMorph_->processBlock(oscABuffer_, oscBBuffer_, …)` at 385–391; the level/morph recompute gated to `CrossfadeMix` at 457–476. Reference block-rate mod handling for spectral tilt (504–513) and osc pitch (515–528).

**Problem:** In SpectralMorph mode the raw `oscABuffer_`/`oscBBuffer_` are fed to `spectralMorph_` with no `oscALevel_`/`oscBLevel_` applied — the only level scaling lives in the `CrossfadeMix`-gated block. So OSC A/B Level knobs and the OscALevel/OscBLevel mod destinations have zero effect in SpectralMorph mode.

**Test FIRST:** In SpectralMorph mode, render with OSC A Level at 1.0 vs 0.0 and assert the output spectrum differs (A's contribution drops). Confirm FAIL pre-fix (identical output).

**Fix:** Before `spectralMorph_->processBlock()` (385–391), scale independent copies of the input buffers by `oscALevel_` / `oscBLevel_` (scale A and B separately to preserve relative spectral balance), then feed the scaled buffers. Because `spectralMorph_` is block-based, per-sample OscLevel modulation cannot be sample-accurate here — fold a block-representative modulated level into the pre-scale (block-rate) and document that OscLevel is block-rate in this mode.

**Do NOT:**
- Do NOT uniformly scale `spectralMorphBuffer_` (the morphed output) — a single output gain cannot express independent A-vs-B balance.
- Do NOT change the `CrossfadeMix` path.

**Build+test:** RUINAE block.

**Done when:** OSC A/B Level audibly affects SpectralMorph output; block-rate limitation documented in-code; `ruinae_tests` green.

---

## T2.10 — Re-apply flanger/chorus from atomics in `applyParamsToEngine` (preset-restore bug) (F015)

**Files:** `plugins/ruinae/src/processor/processor_params.cpp`: inline flanger switch (~113–152) and chorus switch (~156–200) inside `processParameterChanges`; `applyParamsToEngine` phaser block (728–742) as the pattern to mirror. `flanger_params.h`/`chorus_params.h` `handle*ParamChange` already denormalize+clamp into the atomics.

**Problem:** Every other effect re-applies from atomics in `applyParamsToEngine()`; flanger/chorus DSP is updated ONLY by live change-points via an inline switch (with duplicated magic scaling), and `applyParamsToEngine` never touches them. On preset load, `applyPresetSnapshot` calls `engine_.reset()` (resets flanger/chorus DSP) and the loaded atomics are never applied → flanger/chorus run at reset defaults until the user touches a control.

**Test FIRST:** Load a preset with non-default flanger rate/depth (and chorus), run one process block, and assert the flanger/chorus DSP getters reflect the loaded values. Confirm FAIL pre-fix.

**Fix:**
1. In `applyParamsToEngine()`, add flanger and chorus blocks mirroring the phaser block (728–742), reading the already-denormalized atomics: `engine_.effectsChain().flanger().setRate(flangerParams_.rateHz.load(relaxed)); setDepth(...); setFeedback(...); setMix(...); setStereoSpread(...); setWaveform(static_cast<Waveform>(...)); setTempoSync(...);` and map `noteValue` via `getNoteValueFromDropdown()`. Chorus additionally `setVoices(...)`.
2. DELETE the inline switch statements at 113–152 (flanger) and 156–200 (chorus), leaving only the `handleFlangerParamChange`/`handleChorusParamChange` atomic-store calls in `processParameterChanges`.

**Do NOT:**
- Do NOT re-introduce the magic scaling constants in `applyParamsToEngine` — `handle*ParamChange` already denormalized into the atomics; read the atomics directly.
- Do NOT keep both the inline switch AND the new applyParams block (double application).
- ⚠ **Cross-task note:** this DELETES the inline switches that F032 (T3.5) originally targeted. After T2.10, T3.5 is largely resolved — see T3.5.

**Build+test:** RUINAE block; pluginval deferred to T6.1.

**Done when:** preset-loaded flanger/chorus values are audible without touching a control; `ruinae_tests` green; `getState` still round-trips.

---

## T2.11 — Clear stale portamento state on note retrigger (F005)

**Files:** `plugins/ruinae/src/engine/ruinae_voice.h`: `noteOn()` (~229–276), `reset()` (~210–216), portamento guard in `processBlock` (365–376). Fields `portamentoProgress_`/`portamentoSourceFreq_`/`portamentoTargetFreq_` set only by `glideToFrequency()`.

**Problem:** Neither `reset()` nor `noteOn()` clears the portamento fields. If a glide is interrupted (progress < 1.0) and the voice is reused via a non-legato `noteOn()`, the first `processBlock` re-enters the glide branch with STALE source/target and overwrites the freshly-set `noteFrequency_`, sustaining the OLD note's pitch.

**Test FIRST:** Glide a voice with `portamentoTimeMs_ > 0`, advance one block so progress < 1.0, force the voice inactive, then `noteOn()` a new frequency; assert the first `processBlock` leaves `noteFrequency_` (and osc frequencies) at the NEW note, not an interpolated old-source→old-target value. Confirm FAIL pre-fix.

**Fix:** In `noteOn()` (non-legato retrigger path, alongside the other stale-state clears ~229), add:
```cpp
portamentoProgress_ = 1.0f;
portamentoSourceFreq_ = 0.0f;
portamentoTargetFreq_ = 0.0f;
```
Mirror the same three assignments in `reset()` (~210–216) as defensive hygiene.

**Do NOT:**
- Do NOT clear these in `glideToFrequency()` — legato glides depend on it and do not call `noteOn()`.
- Do NOT alter the `processBlock` guard condition.

**Build+test:** RUINAE block.

**Done when:** reused voice plays the new note pitch; test green.

---

## T2.12 — Mode-aware `getActiveVoiceCount()` (F025) **[SHARED KrateDSP — systems layer]**

**Files:** `dsp/include/krate/dsp/systems/poly_synth_engine.h`, `getActiveVoiceCount()` (577–579). Also verify `RuinaeEngine::getActiveVoiceCount` (`plugins/ruinae/src/engine/ruinae_engine.h:1611`) for the identical gap.

**Problem:** In Mono mode the allocator is never engaged, so `allocator_.getActiveVoiceCount()` stays 0 while voice 0 is sounding.

**Test FIRST** (`dsp/tests/unit/systems/poly_synth_engine_test.cpp`): `setMode(Mono); noteOn(60,100); REQUIRE(getActiveVoiceCount()==1); noteOff(60);` drain a block; `REQUIRE(getActiveVoiceCount()==0);`. Confirm FAIL pre-fix. Add an analogous Ruinae test if `RuinaeEngine` has the same gap.

**Fix:** Replace the body:
```cpp
if (mode_ == VoiceMode::Mono) return voices_[0].isActive() ? 1u : 0u;
return allocator_.getActiveVoiceCount();
```
Apply the same fix to `RuinaeEngine::getActiveVoiceCount` if it delegates unconditionally.

**Do NOT:** do NOT change the Poly path.

**Build+test:** `dsp_systems_tests` + **Cross-plugin regression** (0.5).

**Done when:** mono count is 1 while sounding; all plugin test targets green.

---

## T2.13 — RingModulator `reset()` snaps freq smoothers to effective frequency (F027) **[SHARED — processors layer]**

**Files:** `dsp/include/krate/dsp/processors/ring_modulator.h`, `reset()` (~305–307), `prepare()` (256–259) as reference.

**Problem:** `reset()` calls `freqSmoother_.reset()`/`freqSmootherR_.reset()` (zeroes current+target), so the next block ramps the carrier from ~0 Hz over 5 ms → audible chirp. `prepare()` correctly `snapTo(effectiveFreq)`.

**Test FIRST:** `prepare()` with a nonzero frequency, call `reset()`, process one block, assert the sample-0 carrier frequency is at steady-state (no ramp from 0). Confirm FAIL pre-fix.

**Fix:** In `reset()`, replace the two `reset()` calls with:
```cpp
const float effectiveFreq = computeEffectiveFrequency();
freqSmoother_.snapTo(effectiveFreq);
freqSmootherR_.snapTo(effectiveFreq);
```
Snap BOTH to plain `effectiveFreq` (no spread offset) — spread is re-applied via `setTarget()` in `processBlock`, matching `prepare()`.

**Do NOT:** do NOT add the per-channel spread offset into the snap.

**Build+test:** `dsp_processors_tests` + **Cross-plugin regression**.

**Done when:** no carrier ramp after reset; all plugin targets green.

---

## T2.14 — Recompute LFO fade-in increment in `prepare()` (F028) **[SHARED — primitives layer]**

**Files:** `dsp/include/krate/dsp/primitives/lfo.h`, `prepare()` (84–90).

**Problem:** `prepare()` recomputes phase/crossfade increments but not `fadeInIncrement_` (rate-dependent, only recomputed in `setFadeInTime`). Re-prepare at a new sample rate leaves a stale fade-in duration.

**Test FIRST:** `setFadeInTime(t)` at 44100, `prepare(88200)`, retrigger, verify the fade reaches full gain in `t` ms (not `t/2`). Confirm FAIL pre-fix.

**Fix:** In `prepare()`, add `updateFadeInIncrement();` after `updateCrossfadeIncrement()` and before `reset()`.

**Do NOT:** do NOT add anything to `reset()` — `fadeInIncrement_` is a rate-derived constant, not per-trigger state.

**Build+test:** `dsp_primitives_tests` + **Cross-plugin regression**.

**Done when:** fade duration correct after re-prepare; all plugin targets green.

---

## T2.15 — ChaosWaveshaper Henon: implement interpolation OR remove dead `prevHenonX_` (F029) **[SHARED — primitives layer]**

**Files:** `dsp/include/krate/dsp/primitives/chaos_waveshaper.h`: `prevHenonX_` (member init ~395, `resetModelState` 716, `updateHenon` 807), misleading comment 816–818, `updateAttractor` output at 664.

**Problem:** `updateHenon()` comments claim linear interpolation "for continuous output (FR-017)" but performs none; `prevHenonX_` is written and never read; `updateAttractor` derives output only from `state_.x`, so Henon output is a stepwise hold.

**Test FIRST:** Choose the option below and write its test.
- *If simplifying:* assert the file has no dead `prevHenonX_` (compile-level) and Henon output is documented as held. (A behavioral no-op; the real deliverable is removing the misleading comment/dead member.)
- *If implementing:* assert Henon output between map iterations is linearly interpolated (successive samples differ smoothly, not stepwise). Confirm FAIL pre-fix.

**Fix (preferred: honest simplification):** Delete `prevHenonX_` (member init, `resetModelState`, `updateHenon` write) and replace the false interpolation comment with "Henon output is held between map iterations." 
*(Full-implementation alternative, only if product wants continuous output:* in `updateHenon` compute `newNormalized = lerp(prevHenonX_/normFactor, state_.x/normFactor, henonPhase_)` into `normalizedX_`, AND guard `updateAttractor` (664) so it does not overwrite `normalizedX_` for the Henon model. Capture `prevHenonX_` before the map iterates.)

**Do NOT:** do NOT leave the comment claiming interpolation while doing none. Pick one path.

**Build+test:** `dsp_primitives_tests` + **Cross-plugin regression**. (Note `chaos_mod_source.h` has the same dead pattern — out of scope unless the executor is explicitly extending.)

**Done when:** no dead member + no false comment (or interpolation actually implemented); all plugin targets green.

---

## T2.16 — Document MIDI block-quantization (F022) — DOC ONLY

**Files:** `plugins/shared/src/midi/midi_event_dispatcher.h` (header comment ~70–83); one-line note in `plugins/ruinae/src/processor/processor_midi.cpp`.

**Problem:** `dispatchMidiEvents()` never reads `event.sampleOffset`; all note events are quantized to the block boundary. This is a by-design limitation, not a crash.

**Test FIRST:** None required (documentation change with no runtime surface). Skip TDD per its intent — but confirm no behavioral test regresses.

**Fix:** Add a header comment in `midi_event_dispatcher.h` stating dispatched note events are block-quantized and carry no `sampleOffset` by design; add a one-line note in `processor_midi.cpp` that engine triggers align to block start. No code behavior change.

**Do NOT:** do NOT attempt sub-block rendering here — disproportionate to a low finding.

**Build+test:** `shared_tests` + RUINAE block (both include the header).

**Done when:** limitation documented; builds green.

---

## T2.17 — Delete unused arp velocity-curve / transpose IDs (F024)

**Files:** `plugins/ruinae/src/plugin_ids.h` (`kArpVelocityCurveTypeId=3399`, `kArpVelocityCurveAmountId=3400`, `kArpTransposeId=3401`, lines 1216–1218); `plugins/ruinae/src/parameters/arpeggiator_params.h` (unused fields `velocityCurveType`/`velocityCurveAmount`/`transpose`, lines 157–159); test `plugins/ruinae/tests/.../arpeggiator_params_test.cpp` (2077–2078).

**Problem:** These three IDs are declared but never registered/processed/saved/loaded in Ruinae; the backing atomics are never read. Two of them (`3400`, `3401`) are the source of the collisions handled in T2.18/T2.19. Ruinae's arp engine consumes none of these v1.5 fields.

**Test FIRST:** Update the pinning test to the post-fix expectation (the constants no longer exist). Since this is dead-code removal, add/adjust a compile-level assertion: after removal, `arpeggiator_params_test.cpp` must not reference the deleted symbols. Confirm the current test references them (will fail to compile after deletion until updated) — update in lockstep.

**Fix:** Delete the three enum constants (1216–1218) and the three struct fields (arpeggiator_params.h:157–159). Remove/adjust the test lines that pin them.

**Do NOT:**
- Do NOT delete these if a cross-check shows Gradus preset-range parity requires the numeric IDs to stay reserved in Ruinae — verify against Gradus first (`plugins/gradus/src/plugin_ids.h`). If parity requires them, SKIP deletion and instead rely on T2.18 to relocate `kSidechainActiveId` out of the arp range. Record which path you took.
- Do NOT touch Gradus's arp IDs.

**Build+test:** RUINAE block.

**Done when:** unused IDs removed (or documented as reserved-for-parity + skipped); `ruinae_tests` compiles and passes.

---

## T2.18 — Relocate `kSidechainActiveId` out of the arp routing range (F009)

**Files:** `plugins/ruinae/src/plugin_ids.h` (`kSidechainActiveId=3400`, line 1243; arp range `kArpBaseId=3000 .. kArpEndId=3445`); single registration site `controller.cpp:218–220`; reads in `controller_view_sync.cpp` (403, 824); processor write `processor.cpp:547`.

**Problem:** `kSidechainActiveId=3400` sits inside the arp routing range and (before T2.17) aliases `kArpVelocityCurveAmountId=3400`. VST3 requires unique ParamIDs; `processParameterChanges` routes 3400 to `handleArpParamChange`.

**Test FIRST** (`ruinae_tests`, uniqueness guard): build the registered parameter set (or a `constexpr` array of all ParamIDs) and assert no duplicate IDs. Assert `kSidechainActiveId > kArpEndId`. Confirm FAIL pre-fix.

**Fix:** Move `kSidechainActiveId` to a dedicated hidden-output block above `kArpEndId`, e.g. `kSidechainActiveId = 3500,`. Update the registration site (controller.cpp:218–220) — the symbol-based reads/writes follow automatically. Since it is a hidden, read-only, non-persisted output param, no saved-state migration is needed.

**Do NOT:**
- Do NOT change its parameter TYPE or make it automatable.
- Do NOT pick a value that collides with any other block; verify uniqueness with the new test.

**Build+test:** RUINAE block; pluginval deferred to T6.1 (registration changed).

**Done when:** all ParamIDs unique; `kSidechainActiveId` outside arp range; sidechain indicator still updates; `ruinae_tests` green.

---

## T2.19 — Fix `kNumParameters` sentinel (F021)

**Files:** `plugins/ruinae/src/plugin_ids.h` (`kNumParameters=3401`, line 1246; `kArpEndId=3445`, 1240); test `arpeggiator_params_test.cpp:2078`.

**Problem:** Sentinel `kNumParameters=3401` is smaller than real IDs (arp block runs to 3445), violating the documented "one past highest ID" invariant.

**Test FIRST:** Change `arpeggiator_params_test.cpp:2078` from the stale literal `CHECK(kNumParameters == 3401)` to the invariant `CHECK(kNumParameters > kArpEndId);`. Confirm it FAILS with the current value (3401 !> 3445).

**Fix:** Set `kNumParameters = 3446` (one past `kArpEndId=3445`).

**Do NOT:** do NOT hard-code a new magic literal in the test — use the `> kArpEndId` invariant so it cannot go stale.

**Build+test:** RUINAE block.

**Done when:** sentinel > all IDs; invariant test passes; `ruinae_tests` green.

---

# PHASE 3 — Duplication / refactoring

> ⚠ Ordering is load-bearing: **T3.1 first** (it restructures the OSC param headers that T3.2 and T3.3 touch). Preset byte-compatibility must be preserved in every task here — guard each with a save/load round-trip test that asserts the **saved byte stream is unchanged** before vs. after.

## T3.1 — Consolidate OSC A/B param headers into one base-parameterized module (F013, F014)

**Files:** `plugins/ruinae/src/parameters/osc_a_params.h` (868 lines), `osc_b_params.h` (801 lines); new `plugins/ruinae/src/parameters/osc_params.h`. Reference the existing pattern in `lfo_params.h` + `lfo1_params.h`/`lfo2_params.h` and `env_params.h`.

**Problem:** The two headers are line-for-line identical modulo `A`↔`B` and ID base (100/110 vs 200/210). Struct, `handleOscXParamChange`, `registerOscXParams`, `formatOscXParam`, `saveOscXParams`, `loadOscXParams`, `loadOscXParamsToController` are all duplicated and already drifting (e.g. `formatOscAParam` uses intermediate `st/ct` locals; `formatOscBParam` does not).

**Preconditions to verify (read first):** For every type-specific field confirm `kOscA*Id - kOscATypeId == kOscB*Id - kOscBTypeId` in `plugin_ids.h`. If any offset differs, STOP and report — consolidation is unsafe.

**Test FIRST:** Add a round-trip test that (a) saves and loads OSC A and OSC B params and asserts value fidelity, and (b) captures the exact saved byte stream for both banks. Run it against the CURRENT code to record the golden bytes, then require the refactor to reproduce them byte-for-byte.

**Fix:** Create `osc_params.h` mirroring `lfo_params.h`: define `OscParams` struct once; `handleOscParamChange(params, base, id, value)`, `registerOscParams(parameters, base, labelPrefix)`, `formatOscParam(base, id, value, string)`, `saveOscParams`/`loadOscParams`, `loadOscParamsToController(base, streamer, setParam)` — all keyed off the block base (`kOscATypeId`/`kOscBTypeId`) with offset enums like the LFO file. Keep the shared `kParamIdToOscParam` table and `oscillator_types.h` include here. Reduce `osc_a_params.h`/`osc_b_params.h` to thin shims: `using OscAParams = OscParams;` plus inline forwarders (`handleOscAParamChange` → `handleOscParamChange(params, kOscATypeId, …)`, `registerOscAParams` → `registerOscParams(parameters, kOscATypeId, STR16("OSC A "))`, …) so processor/controller/preset/test call sites need no changes. Reconcile the existing tune/fine and `format` divergence to one form.

**Do NOT:**
- Do NOT reorder any serialized field — preset byte order must be identical (guard with the golden byte test).
- Do NOT change public call-site signatures (`handleOscAParamChange`, etc. must still exist as forwarders).
- Do NOT collapse until the offset precondition is verified for every field.

**Build+test:** RUINAE block; pluginval deferred to T6.1.

**Done when:** one implementation + two shims; saved bytes unchanged for both banks; `ruinae_tests` green.

---

## T3.2 — Route OSC dropdowns through the canonical string tables (remove triple copies) (F030)

**Files:** `plugins/ruinae/src/parameters/dropdown_mappings.h` (unused `k*Strings[]` arrays ~505+); OSC register function(s) — now unified in `osc_params.h` after T3.1 (was `osc_a_params.h:363`, `osc_b_params.h:319`). Helper reference: `createNoteValueDropdown` in `plugins/shared/src/ui/parameter_helpers.h` (takes `const TChar* const*` + count).

**Problem:** Each OSC dropdown exists in three hand-synced copies: the unused canonical table + inline literal in A + inline literal in B. After T3.1 the two inline copies collapse to one, but that one is still an inline literal, and the canonical table is still dead.

**Test FIRST:** Extend the OSC param round-trip test to assert dropdown string counts/labels match the canonical `k*Count` and `k*Strings` (guards that routing through the table produced the same labels). Confirm the current inline literals are not consuming the tables (they aren't) — the test formalizes the target.

**Fix:** Keep the `k*Strings[]` tables (single source of truth) and add/use a pointer+count overload `createDropdownParameterWithDefault(title, id, defaultIndex, const TChar* const* strings, int count)` (mirroring `createNoteValueDropdown`). Call it from the unified `registerOscParams` with `(kOscWaveformStrings, kOscWaveformCount)` etc., deleting the inline literal lists. Do the same for PD waveform, sync mode, chaos attractor/output, particle spawn/env, formant vowel, noise color. (Simplest alternative if you prefer: delete the unused `k*Strings[]` arrays and keep the `k*Count` constants — but prefer routing through the tables.)

**Do NOT:**
- Do NOT delete the `k*Count` constants — the normalize/denormalize math consumes them.
- Do NOT change label text or ordering (breaks host-saved indices only if reordered — keep order identical).

**Build+test:** RUINAE block; pluginval deferred to T6.1.

**Done when:** no inline dropdown literals in OSC registration; canonical tables consumed; `ruinae_tests` green.

---

## T3.3 — De-duplicate OSC value arrays and envelope apply blocks in `applyParamsToEngine` (F035)

**Files:** `plugins/ruinae/src/processor/processor_params.cpp`: OSC A array (269–300) + loop (301–303); OSC B array (309–339) + loop (341–343); Amp (438–463), Filter (465–490), Mod (492–517) envelope blocks.

**Problem:** OSC A/B build two identical 30-element arrays from atomics (differ only by `oscAParams_`/`oscBParams_`), then run the same loop over `kParamIdToOscParam`. The three envelope blocks are near-identical 25-line blocks differing only by struct + `setAmp*/setFilter*/setMod*` prefix.

**Test FIRST:** Assert a representative OSC param and each envelope's ADSR reach the engine after `applyParamsToEngine` (parameter-application coverage). Confirm behavior is unchanged after refactor (this guards the mechanical extraction).

**Fix:**
1. Add a file-local `template<class Pack, class Setter> void applyOscTypeParams(const Pack& p, Setter setParam)` that builds the 30-element array from `p.*` and runs the `for (i<kOscTypeSpecificParamCount) setParam(kParamIdToOscParam[i], values[i])` loop. Call twice with lambdas forwarding to `engine_.setOscAParam`/`setOscBParam`.
2. Factor a second helper for the ADSR blocks taking the env struct + setter lambdas (attack/decay/sustain/release/attackBezier/decayBezier/releaseBezier/attackCurve/decayCurve/releaseCurve + the `bezierEnabled>=0.5f` branch); call three times.

**Do NOT:**
- Do NOT reorder the value-array indices (they map positionally through `kParamIdToOscParam`).
- Do NOT change the `bezierEnabled` branch semantics.
- ⚠ This edits `applyParamsToEngine`, which T5.6 (god-function split) will later reorganize — that's fine (T5.6 runs last and adapts).

**Build+test:** RUINAE block.

**Done when:** ~35 duplicated lines removed; param-application tests green.

---

## T3.4 — Consolidate chorus/flanger param headers (F031)

**Files:** `plugins/ruinae/src/parameters/chorus_params.h`, `flanger_params.h`; new `modulation_effect_params.h`. Reference `lfo_params.h` pattern.

**Problem:** `RuinaeChorusParams`/`RuinaeFlangerParams` and their handle/register/format/save/load/loadToController surfaces are identical except: chorus has an extra `voices` param; rate scaling (chorus `×9.95`/10 Hz vs flanger `×4.95`/5 Hz); default stereo spread (180 vs 90).

**Test FIRST:** Preset round-trip byte-golden test for BOTH effects (capture current saved byte stream, require unchanged after refactor). Chorus's save/load inserts the `voices` int32 between `stereoSpread` and `waveform` — the golden test must pin this ordering.

**Fix:** Create `modulation_effect_params.h` parameterized by (a) base ParamID block, (b) rate max Hz, (c) optional `voices` presence (compile-time flag e.g. `constexpr bool HasVoices`), (d) default stereoSpread. The shared save/load takes the `HasVoices` flag so byte ordering stays exactly as today. Keep `chorus_params.h`/`flanger_params.h` as thin shims forwarding with their base ID, rate max, flag, and label prefix, preserving current signatures.

**Do NOT:**
- Do NOT reorder existing serialized fields — the `voices` int must remain between `stereoSpread` and `waveform` for chorus; flanger must have no voices slot.
- Do NOT change rate ranges or stereo-spread defaults.

**Build+test:** RUINAE block; pluginval deferred to T6.1.

**Done when:** one shared module + two shims; byte-golden unchanged; `ruinae_tests` green.

---

## T3.5 — (Resolved-by-T2.10) verify no flanger/chorus dispatch duplication remains (F032)

**Files:** `plugins/ruinae/src/processor/processor_params.cpp` (former inline switches 113–200; new `applyParamsToEngine` flanger/chorus blocks from T2.10).

**Problem/status:** F032 targeted the two near-duplicate inline dispatch switches — **T2.10 already DELETED them**. This task is now a verification + optional micro-dedup of the new `applyParamsToEngine` flanger/chorus blocks.

**Test FIRST:** None new required if T2.10's preset-restore test is green. Optionally assert a representative param (Feedback → `value*2-1`, StereoSpread → `value*360`) reaches each DSP object.

**Fix:** Confirm the inline switches (with their paired `NOLINT(bugprone-branch-clone)`) are gone. If the new `applyParamsToEngine` flanger/chorus blocks (T2.10) are themselves near-duplicates, optionally extract a file-local `template<class Fx> void applyModEffectFromAtomics(Fx& fx, const Pack& p, float rateMax)`. Otherwise mark this task complete (superseded).

**Do NOT:** do NOT re-create the inline `processParameterChanges` switches.

**Build+test:** RUINAE block.

**Done when:** no duplicated dispatch remains; `ruinae_tests` green (or task recorded as superseded by T2.10).

---

## T3.6 — Extract shared log-map helpers for normalized↔units (F033)

**Files:** `plugins/shared/src/ui/parameter_helpers.h` (add helpers; `Krate::Plugins` namespace, re-exported via `controller/parameter_helpers.h`); call sites `env_follower_params.h` (30–38 attack, 47–55 release), `lfo_params.h` (63–69 rate; 73–80 fadeIn is a partial-fit — leave separate), `pitch_follower_params.h` (31–39 minHz, 48–56 maxHz).

**Problem:** `units = min*pow(max/min, x)` (clamped) and inverse `log(u/min)/log(max/min)` (clamped) are hand-reimplemented in ≥4 places; `lfo_params` uses float-precision while env/pitch use double → inconsistent rounding.

**Test FIRST:** Golden round-trip test: for each of the 5 mapping pairs, sample normalized values, map to units and back, assert bit-stability against the CURRENT implementation (or intentionally document + pin the float→double normalization change). Confirm the refactor reproduces the pinned values.

**Fix:** Add to `parameter_helpers.h`:
```cpp
inline double logMapFromNormalized(double x, double mn, double mx){ double c=std::clamp(x,0.0,1.0); return std::clamp(mn*std::pow(mx/mn,c),mn,mx);}
inline double logMapToNormalized(double u, double mn, double mx){ double c=std::clamp(u,mn,mx); return std::clamp(std::log(c/mn)/std::log(mx/mn),0.0,1.0);}
```
Delegate the 5 pairs: envFollowerAttack (0.1,500), envFollowerRelease (1,5000), lfoRate (0.01,50), pitchFollowerMinHz (20,500), pitchFollowerMaxHz (200,5000), casting to float where the existing signature returns float. Standardize on double internally.

**Do NOT:**
- Do NOT fold `lfoFadeIn` (base 1.0, `value<0.001`→0 off-gate, no upper clamp) into the shared helper — leave it distinct.
- Do NOT commit a silent numeric change — either be bit-stable or explicitly document the float→double normalization shift with the golden test updated.

**Build+test:** `shared_tests` + RUINAE block (both consume the header).

**Done when:** 5 pairs delegate to shared helpers; round-trip golden green.

---

## T3.7 — Single definition of `kVoiceDestParamIds` (F023)

**Files:** `plugins/ruinae/src/controller/controller_view_sync.cpp` (51–61), `controller_mod_matrix.cpp` (20–30); new `plugins/ruinae/src/controller/mod_ring_dest_ids.h`.

**Problem:** The 8-entry destination→ParamID array is defined identically as `static constexpr` in two TUs; drift silently desyncs ring-indicator base-value sync vs. arc rebuild.

**Test FIRST:** Add a test asserting the mapping matches expected IDs and that `kVoiceDestParamIds.size() == Krate::Plugins::kVoiceDestNames.size()` from a single include. (The existing `static_assert` stays.) Confirm compilation after moving to the shared header.

**Fix:** Create `plugins/ruinae/src/controller/mod_ring_dest_ids.h` in namespace `Ruinae`, including `"plugin_ids.h"` and `"ui/mod_matrix_types.h"`; define `inline constexpr std::array<Steinberg::Vst::ParamID, Krate::Plugins::kNumVoiceDestinations> kVoiceDestParamIds = {{ ... }};` with the 8 mappings; keep the size `static_assert`. Include it in both TUs; delete both local copies.

**Do NOT:**
- Do NOT put it in the shared `plugins/shared/src/ui/mod_matrix_types.h` — that header is `Krate::Plugins` and cannot reference Ruinae IDs like `kFilterCutoffId`.
- Do NOT change the mapping order.

**Build+test:** RUINAE block.

**Done when:** one definition, both TUs include it; `ruinae_tests` green.

---

# PHASE 4 — SIMD / efficiency

> All Phase-4 DSP-header tasks are **[SHARED KrateDSP]** — run **Cross-plugin regression** (0.5) for each. Several tasks touch `phaser.h`; do T4.1 → T4.2 → T4.3 in that order (all edit the same functions). Where output changes bit-for-bit (T4.3), regenerate the affected golden/approval fixtures.

## T4.1 — Hoist the single allpass `tan` out of the phaser per-stage loop (F017) **[SHARED — processors]**

**Files:** `dsp/include/krate/dsp/processors/phaser.h`: `process()` loop 477–480; `processStereo()` L 561–563, R 584–586. `OnePoleAllpass::coeffFromFrequency`/`setCoefficient` in `one_pole_allpass.h`.

**Problem:** All N stages are swept to the same frequency, but `stage.setFrequency(sweepFreq)` (→ `std::tan`) is called per stage → up to 12 (24 stereo) redundant `tan`/sample.

**Test FIRST:** Bit-exact/golden phaser output test — capture current output, require identical after fix (this is a pure hoist, output must not change). Confirm harness in place.

**Fix:** Compute the coefficient once, then set it on all stages:
```cpp
const float aL = OnePoleAllpass::coeffFromFrequency(sweepFreq, sampleRate_);
for (int i = 0; i < numStages_; ++i) stagesL_[static_cast<size_t>(i)].setCoefficient(aL);
```
Do the same in `processStereo` for L (`sweepFreqL`) and R (`sweepFreqR`).

**Do NOT:** do NOT introduce SIMD (serial allpass recurrence; header-only Layer 2, Highway unreachable). Output must remain bit-identical.

**Build+test:** `dsp_processors_tests` + **Cross-plugin regression**.

**Done when:** 1 `tan`/sample/channel; phaser golden unchanged; all plugin targets green.

---

## T4.2 — Cache phaser sweep min/max (drop 2 of 3 `pow`/sample) (F018) **[SHARED — processors]**

**Files:** `dsp/include/krate/dsp/processors/phaser.h`: `calculateSweepFrequency()` (612–633), callers 475/551/552; `prepare()`/`reset()` for cache init.

**Problem:** 3 `std::pow`/call (6 stereo); two only depend on smoothed depth/center-freq (block-invariant in steady state) yet recomputed every sample.

**Test FIRST:** Bit-exact phaser sweep test capturing current output; require identical after fix (OnePoleSmoother reaches an exact float fixed point, so gating is bit-stable). Confirm FAIL is not expected — this must be output-preserving; the "test" pins output equality.

**Fix:** Add cache members `cachedDepth_`, `cachedCenterFreq_`, `cachedMinFreq_`, `cachedMaxFreq_`, `cachedLog2Ratio_` (reset in `prepare()`/`reset()`). Recompute `minFreq`/`maxFreq`/`log2Ratio` only when smoothed depth or center-freq differs from cache. Replace `std::pow(2.0f, ±octaves)` with `std::exp2f(±octaves)`; replace the per-sample `std::pow(freqRatio, lfoNorm)` with `std::exp2f(lfoNorm * cachedLog2Ratio_)`.

**Do NOT:**
- Do NOT use a FastMath exp2 polynomial — `fast_math.h` removed `fastExp` because MSVC std is faster. Use `std::exp2f`.
- Do NOT recompute the cache unconditionally.

**Build+test:** `dsp_processors_tests` + **Cross-plugin regression**.

**Done when:** ~2 `exp2`/sample in steady state, output bit-exact; all plugin targets green.

---

## T4.3 — Use `FastMath::fastTanh` in phaser/flanger feedback (F041) **[SHARED — processors]**

**Files:** `dsp/include/krate/dsp/processors/phaser.h` (`std::tanh` at 483, 565, 588), `flanger.h` (330, 358). Reference `chorus.h:32,305,335`.

**Problem:** Per-sample-per-channel `std::tanh` in feedback; chorus already uses the ~3× faster `FastMath::fastTanh`.

**Test FIRST:** This CHANGES output slightly (Padé approximant). Update/regenerate any phaser/flanger golden or approval fixtures FIRST to the new expected output, and add a bounded-error assertion (`|fastTanh(x)-tanh(x)| < tol` for the clamped feedback range). Confirm the old golden fails, then commit the regenerated golden alongside the fix.

**Fix:** Add `#include <krate/dsp/core/fast_math.h>` to `phaser.h` and `flanger.h` (matching `chorus.h:32`). Replace `std::tanh` → `FastMath::fastTanh` at the 5 listed sites.

**Do NOT:**
- Do NOT skip regenerating golden fixtures — output shifts and CI approval tests will fail otherwise.
- Do NOT forget the include (compile error).

**Build+test:** `dsp_processors_tests` + **Cross-plugin regression** (Iterum's `approval_tests` if any phaser/flanger goldens exist there).

**Done when:** feedback uses fastTanh; regenerated goldens green; all plugin targets green.

---

## T4.4 — Gate FormantFilter coefficient recompute on smoother settling (F016) **[SHARED — processors]**

**Files:** `dsp/include/krate/dsp/processors/formant_filter.h`: `process()` (508–509 unconditional `updateFilterCoefficients()`), `updateFilterCoefficients()` (loop 490), smoothers `freqSmoothers_`/`bwSmoothers_` with `isComplete()`.

**Problem:** `process()` recomputes all 5 biquads (5 sin + 5 cos + divides) every sample even when the smoothers have settled and coefficients are constant — the most expensive per-voice filter option.

**Test FIRST:** After settling with a constant target, assert biquad coefficients are bit-identical across successive `process()` calls; and that changing vowel/shift re-triggers coefficient movement. Confirm FAIL pre-fix (coeffs recomputed regardless, but the test that "changing param resumes movement" is the correctness guard).

**Fix:** Add `bool smoothersSettled() const noexcept` returning true only if all `freqSmoothers_[i].isComplete() && bwSmoothers_[i].isComplete()`. In `process()`: `if (!smoothersSettled()) updateFilterCoefficients();`. Parameter changes (`setFormantShift`/`setVowel`/`setGender`/`setBandwidthScale` → `calculateTargetFormants()` → `setTarget()`) re-arm the smoothers automatically, so no explicit dirty flag is needed.

**Do NOT:**
- Do NOT drop the recompute during transitions — click-free behavior and per-sample cutoff modulation depend on it (only skip when ALL smoothers are complete).
- Do NOT cache across a parameter change (the `isComplete()` gate handles this).

**Build+test:** `dsp_processors_tests` + **Cross-plugin regression**.

**Done when:** steady-state coeffs stable + no recompute; transitions still smooth; all plugin targets green.

---

## T4.5 — Cache RingModulator Gordon-Smith epsilon (stop per-sample `sin`) (F040) **[SHARED — processors]**

**Files:** `dsp/include/krate/dsp/processors/ring_modulator.h`: `generateCarrierSample()` Sine case (~444), per-channel state threading (sinState/cosState for L and R).

**Problem:** The Sine carrier recomputes `epsilon = 2*sin(pi*smoothedFreq/sampleRate)` every sample, defeating the magic-circle phasor. `smoothedFreq` is per-sample from a smoother that snaps exactly once settled.

**Test FIRST:** Assert steady-state carrier output is bit-identical whether epsilon is cached or recomputed (settled smoother → constant epsilon), and that a frequency change re-derives epsilon. Confirm the caching path reproduces output.

**Fix:** Add per-channel members `sineEpsilon_ = 0.0f, sineEpsilonFreq_ = -1.0f` (and R variants), passed by reference into `generateCarrierSample`. In the Sine case:
```cpp
if (smoothedFreq != epsilonFreqCache) {
    epsilonCache = 2.0f * std::sin(kPi * smoothedFreq / static_cast<float>(sampleRate_));
    epsilonFreqCache = smoothedFreq;
}
tickSineCarrier(sinState, cosState, epsilonCache, counter);
```
Reset `epsilonFreqCache` to `-1` in `reset()`/`prepare()` (interacts with T2.13's reset changes — apply after T2.13).

**Do NOT:**
- Do NOT hoist the `carrierWaveform_` switch out of the loop (negligible benefit; branch is perfectly predicted).
- Do NOT SIMD-ize the serial recurrence.

**Build+test:** `dsp_processors_tests` + **Cross-plugin regression**.

**Done when:** `sin` only fires while smoother moves; output bit-exact in steady state; all plugin targets green.

---

## T4.6 — Guard SVF second-stage `setCutoff` on slope count (F039)

**Files:** `plugins/ruinae/src/engine/ruinae_voice.h`: `setActiveFilterCutoff()` SVF cases (~1324–1325, both `filterSvf_.setCutoff(hz)` and `filterSvf2_.setCutoff(hz)`); `processActiveFilter` uses `filterSvf2_` only when `svfSlopeStages_ >= 2` (~1396); default `svfSlopeStages_{1}`.

**Problem:** `filterSvf2_.setCutoff(hz)` runs a `std::tan` every sample/voice even in single-stage mode where `filterSvf2_` is never processed → wasted transcendental (the result is never even read).

**Test FIRST:** Assert output is unchanged for both single-stage and 2-stage SVF configurations across the fix (bit-exact), and specifically that switching 1→2 stages produces a correct (primed) second stage on the first stage-2 sample. Confirm harness.

**Fix:** In `setActiveFilterCutoff`, replace the unconditional second-stage call with `if (svfSlopeStages_ >= 2) filterSvf2_.setCutoff(hz);` (keep `filterSvf_.setCutoff(hz)` unconditional). The per-sample `setActiveFilterCutoff` runs before `processActiveFilter` in the same iteration, so a 1→2 switch primes stage 2 before first use. Optional belt-and-suspenders: in `setFilterSvfSlope` when transitioning to 2, call `filterSvf2_.setCutoff(filterCutoffHz_); filterSvf2_.snapToTarget();`.

**Do NOT:**
- Do NOT also guard `setActiveFilterResonance` — `setResonance` has no transcendental and that path is already conditional; skip it.

**Build+test:** RUINAE block.

**Done when:** single-stage mode does not call stage-2 `setCutoff`; output bit-exact; `ruinae_tests` green.

---

## T4.7 — Sub-block chunk the mono voice loop (F020) **[partly SHARED]**

**Files:** `plugins/ruinae/src/engine/ruinae_engine.h`: `processBlockMono()` per-sample loop (~2043–2058). Same pattern (optional) in `dsp/include/krate/dsp/systems/poly_synth_engine.h:823` **[SHARED]**.

**Problem:** Mono drives voice 0 with `voices_[0].processBlock(&sample, 1)` per sample, re-running all per-call block-rate work (keytrack log2, `updateOscFrequencies` divisions, distortion/gate mod, osc setup) once per sample.

**Test FIRST:** Assert sample-accurate portamento is preserved (glide over a block moves pitch smoothly) AND a perf/behavioral test that mono render matches the reference within tolerance after chunking. Confirm harness. (Bit-exactness is NOT expected — chunking updates frequency at chunk boundaries; assert perceptual equivalence / within-tolerance.)

**Fix:** In `processBlockMono`, replace the numSamples-wide 1-sample loop with an outer loop over `kPortamentoChunk` (e.g. 32 samples). Per chunk: call `monoHandler_.processPortamento()` chunkSize times (cheap scalar ramp), use the final gliding frequency via `voices_[0].setFrequency()`, then `voices_[0].processBlock(chunkPtr, chunkSize)` once into a scratch buffer, and pan/sum into `mixBufferL_`/`mixBufferR_`. 32 samples ≈ 0.7 ms @44.1 kHz — perceptually transparent.

**Do NOT:**
- Do NOT switch to block-rate portamento wholesale — that reintroduces zipper (FR-009 requires sample-accurate glide).
- Do NOT claim FFT savings — SpectralMorph is STFT/hop-accumulator based; feeding 1 sample vs. a chunk runs the same number of FFTs. The win is block-rate voice overhead, not FFT.

**Build+test:** RUINAE block. If you also change `poly_synth_engine.h:823`, treat as **[SHARED]** and run **Cross-plugin regression**; otherwise scope to Ruinae only.

**Done when:** mono render matches reference within tolerance; portamento still smooth; targets green.

---

# PHASE 5 — Anti-pattern cleanup

> T5.6 (god-function split) runs LAST because it reorganizes `applyParamsToEngine`, which many earlier tasks edited. Do NOT reorder it earlier.

## T5.1 — Add fade state machines to delay/reverb enable/disable (F012)

**Files:** `plugins/ruinae/src/engine/ruinae_effects_chain.h`: `setDelayEnabled`/`setReverbEnabled` (322–323); delay skip 808 + external-mix gate 935; reverb call gate 1026. Reference: modulation slot `startModCrossfade` (336–347) and the harmonizer `FadingIn/On/FadingOut` machine (955–1005, enum 1364).

**Problem:** Enable/disable are hard bool flips — wet tails are cut instantly (click); on re-enable stale delay-line/tank content reappears abruptly. Inconsistent with modulation/harmonizer slots which crossfade.

**Test FIRST:** With an audible wet tail, toggle delay (then reverb) off and assert the output ramps down over the fade window instead of a one-block discontinuity (measure sample-to-sample delta stays below a click threshold). Confirm FAIL pre-fix (instant cut).

**Fix (mirror harmonizer fade machine — no new DSP primitives):**
- Delay: add a `DelayEnableFade` state (Off/FadingIn/On/FadingOut) + alpha/increment (~20 ms). On enable-from-Off: reset delay lines + comp delays, alpha=0, FadingIn. On disable-from-On: FadingOut, keep processing. In processChunk keep the delay branch running while state != Off; multiply the wet contribution of the external mix (939–940) by alpha (0→1 in / 1→0 out). At FadingOut→0: state=Off, stop processing + reset lines.
- Reverb: same pattern around 1026 — on disable-from-On keep calling `processReverbSlot`, crossfade its output toward a saved pre-reverb dry copy by alpha, reset the tank only after fade completes; on enable reset tank then fade wet in.

**Do NOT:**
- Do NOT reset the delay lines / reverb tank at the instant of disable — reset only after the fade completes (else the tail is still cut).
- ⚠ This edits `processReverbSlot` and the reverb dry/wet path — apply AFTER T2.6/T2.7 (Phase 2) which already reshaped that path; read the current state first.

**Build+test:** RUINAE block; pluginval deferred to T6.1.

**Done when:** enable/disable ramps click-free; `ruinae_tests` green.

---

## T5.2 — Advance muted-voice smoothers in harmonizer non-PhaseVocoder paths (F026) **[SHARED — systems]**

**Files:** `dsp/include/krate/dsp/systems/harmonizer_engine.h`: PitchSync muted branch (387–389), Standard muted branch (466–468); reference PhaseVocoder muted branch (243–253).

**Problem:** PhaseVocoder advances pitch/level/pan smoothers for muted voices; PitchSync and Standard skip the whole iteration, freezing smoothers → on unmute the voice resumes at full gain in one block (potential click) instead of a 5 ms ramp. Inconsistent across the three paths.

**Test FIRST:** Mute then unmute a voice in Standard (and PitchSync) mode via `setVoiceLevel`; assert the level ramps up over `kLevelSmoothTimeMs` on unmute rather than jumping in one block. Confirm FAIL pre-fix.

**Fix:** In the muted branch of PitchSync (387–389) and Standard (466–468), before `continue`, advance smoothers exactly as PhaseVocoder (244–251): set pitchSmoother target/advance, `levelSmoother.advanceSamples(numSamples)`, `panSmoother.advanceSamples(numSamples)`. Optionally factor a shared helper to prevent future drift.

**Do NOT:** do NOT add a mute-side ramp — the instant mute crosses −60 dB is already inaudible; the fix is only the unmute discontinuity.

**Build+test:** `dsp_systems_tests` + **Cross-plugin regression**.

**Done when:** all three paths advance muted-voice smoothers consistently; unmute ramps; all plugin targets green.

---

## T5.3 — Remove dead debug scaffolding (F038)

**Files:** `plugins/ruinae/src/engine/ruinae_effects_chain.h` (RUINAE_FX_CHAIN_DEBUG block, `s_logCounter`, `logFxChain`, `peakLevel` — 47–82); `plugins/ruinae/src/processor/processor.cpp` (RUINAE_PHASER_DEBUG/RUINAE_TGATE_DEBUG, `logPhaser`/`logTGate`, `s_logCounter`/`s_tgLogCounter`/`s_tgLastStep` — 31–98; call at 245; guarded traces 289–303, 395–396, 559–585).

**Problem:** Dead, self-flagged "remove after debugging" scaffolding (compiled out) including a cross-TU mutable global.

**Test FIRST:** None (removing dead, compiled-out code). Confirm `ruinae_tests` green before and after; rely on the build to catch any stray reference.

**Fix:** Delete the scaffolding in both files as listed (macros, log helpers incl. no-op stubs, the mutable counters, the `logPhaser` call at 245, and the `#if`-guarded trace blocks). If any tracing must be retained, replace with a single documented trace utility behind one macro — do not leave the duplicated per-file helpers.

**Do NOT:** do NOT leave the `logPhaser` no-op stub + its call site behind; remove both.

**Build+test:** RUINAE block.

**Done when:** no debug scaffolding remains; `ruinae_tests` green; zero warnings.

---

## T5.4 — Replace magic `16` with `RuinaeEngine::kMaxPolyphony` (F036)

**Files:** `plugins/ruinae/src/processor/processor.cpp:622` (`for (size_t i = 0; i < 16; ++i)` envelope-display scan); constant at `ruinae_engine.h:115` (`static constexpr size_t kMaxPolyphony = 16;`).

**Problem:** Bare literal `16` desyncs if polyphony capacity changes.

**Test FIRST:** None strictly needed (value-identical). Optionally a compile-time `static_assert` that the loop bound equals `kMaxPolyphony`. Confirm `ruinae_tests` green.

**Fix:** Replace the literal bound with `Krate::DSP::RuinaeEngine::kMaxPolyphony` (fully-qualified to match how `RuinaeEngine` is referenced in this TU).

**Do NOT:** do NOT introduce a new local copy of the constant; use the public engine constant directly.

**Build+test:** RUINAE block.

**Done when:** loop uses the named constant; `ruinae_tests` green.

---

## T5.5 — Single named constant for arp step count `32` (F037)

**Files:** `plugins/ruinae/src/processor/processor_params.cpp` (9 `< 32` loops: 431, 999, 1010, 1021, 1033, 1049, 1073, 1118, 1129; 8 `setLength(32)`); `processor.cpp:444` (`kMaxStepsF = 32.0f`); `controller.cpp:521` (`kMaxArpSteps = 32`). Canonical source: `dsp/include/krate/dsp/primitives/arp_lane.h:58` (`ArpLane<T,MaxSteps>::kMaxSteps`).

**Problem:** The domain constant `32` is re-spelled as raw literals across three files, inconsistently named.

**Test FIRST:** None strictly required (value-identical). Optionally `static_assert(kMaxArpSteps == static_cast<int>(Krate::DSP::ArpLane<float>::kMaxSteps));`. Confirm green.

**Fix:** Introduce a single header-shared constant referencing the canonical DSP value, e.g. in a Ruinae arp header: `static constexpr int kMaxArpSteps = static_cast<int>(Krate::DSP::ArpLane<float>::kMaxSteps);`. Replace the 9 loop bounds + 8 `setLength(32)` in `processor_params.cpp`, `kMaxStepsF` in `processor.cpp:444` (`static_cast<float>(kMaxArpSteps)`), and `kMaxArpSteps` in `controller.cpp:521`. For symmetry update the analogous loops in `arpeggiator_params.h`, `controller_view_sync.cpp`, `controller_verify_view.cpp`.

**Do NOT:** do NOT change the numeric value (32) or `ArpLane`'s default template arg; this is a naming/DRY change only.

**Build+test:** RUINAE block.

**Done when:** all `32`/`32.0f` step-count literals reference one shared constant; `ruinae_tests` green.

---

## T5.6 — Decompose `applyParamsToEngine()` god function (F034) — RUN LAST

**Files:** `plugins/ruinae/src/processor/processor_params.cpp`, `applyParamsToEngine()` (233–1152, ~920 lines after earlier edits); may split into a new `processor_apply.cpp` following the `processor_*` pattern.

**Problem:** A single ~920-line flat setter-dispatch function; Ruinae is the repo's cited decomposition model.

**Test FIRST:** None new — this is a pure, behavior-preserving decomposition. Require the full `ruinae_tests` (esp. parameter-application coverage from T3.3) to remain green with no behavior change.

**Fix:** Extract per-section private helpers (`applyGlobalParams`, `applyOscParams`, `applyFilterParams`, `applyDistortionParams`, `applyEnvParams`, `applyLfoParams`, `applyModMatrixParams`, `applyEffectsParams`, `applyArpParams`, …) mirroring the `handle*ParamChange` split; `applyParamsToEngine()` calls them in the SAME order. Keep in `processor_params.cpp` or a new `processor_apply.cpp`.

**Do NOT:**
- Do NOT add whole-function dirty-flag gating — `processor_state.cpp:368–376` relies on unconditional re-application after preset load; the arp section already has its own targeted `prevArp*` dirty-check. A blanket flag risks stale engine state on preset switch.
- Do NOT reorder the setter sequence (some engine state has order dependencies).
- Do NOT change any setter arguments — pure extraction.

**Build+test:** RUINAE block; pluginval at T6.1.

**Done when:** function decomposed into ordered helpers; behavior identical; `ruinae_tests` green.

---

# PHASE 6 — Validation gate

## T6.1 — pluginval strictness 5 on Ruinae

Run after all plugin-source changes (Phases 1–5):
```bash
"$CMAKE" --build build/windows-x64-release --config Release --target Ruinae
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
```
(The post-build copy to `C:/Program Files/Common Files/VST3/` may fail with a permission error — that is fine; the bundle at `build/windows-x64-release/VST3/Release/Ruinae.vst3/` is valid.)

**Done when:** pluginval passes at strictness 5 with no failures.

## T6.2 — clang-tidy (ruinae + dsp)

From a VS Developer PowerShell (regenerate `compile_commands.json` if CMakeLists or sources changed):
```powershell
cmake --preset windows-ninja
./tools/run-clang-tidy.ps1 -Target ruinae -BuildDir build/windows-ninja
./tools/run-clang-tidy.ps1 -Target dsp    -BuildDir build/windows-ninja
```
Capture output to a log file on the FIRST run and inspect the log (do not re-run to grep). Fix ALL warnings surfaced (not just new-code ones), including any lingering `NOLINT(bugprone-branch-clone)` suppressions that became unnecessary after T2.10/T3.5.

**Done when:** clang-tidy is clean for both `ruinae` and `dsp` targets.

---

## Final done-criteria (whole plan)

1. All 42 findings addressed (F006+F010 and F013+F014 merged; F032 superseded by T2.10; F024 may be reserved-for-parity if Gradus requires it — recorded).
2. Every task has a FIRST-written failing test (bugs) or a behavior-preserving guard test (refactors/efficiency), all now passing.
3. `ruinae_tests`, all five `dsp_*_tests`, and (for every **[SHARED]** task) `plugin_tests disrumpo_tests ruinae_tests innexus_tests gradus_tests membrum_tests shared_tests` are green.
4. Any golden/approval fixtures changed by T4.3 are regenerated and committed.
5. pluginval strictness 5 (Ruinae) passes; clang-tidy `ruinae` + `dsp` clean; zero compiler warnings.
6. No commits amended; nothing pushed.