# Krate Audio Monorepo — Consolidation Audit Report

## 1. Executive Summary

The monorepo is, on the whole, **well-factored**. The shared DSP library (`dsp/`) is disciplined about Layer-0 utilities, the preset/update/UI infrastructure already lives in `plugins/shared/`, and most processors correctly compose primitives rather than re-rolling DSP. The duplication that remains falls into three honest buckets:

1. **Build-side scaffolding (the single largest, lowest-risk win).** `CMakeLists.txt`, `version.h.in`, `win32resource.rc.in`, and `entry.cpp` are copy-paste-with-rename across all six plugins. None touches the audio thread, none crosses component separation. This is ~1,080 lines of pure mechanical duplication with a clean fix (one shared CMake module + neutral templates + a factory macro).

2. **Test scaffolding (the largest raw line count).** `IParamValueQueue`/`IParameterChanges` mocks, `IEventList` MIDI mocks, and `ProcessSetup` factories are reimplemented dozens of times. ~3,000 lines of mockable boilerplate, test-only (zero RT/cross-platform/component risk), but high-churn (50+ files).

3. **Half-finished cross-plugin consolidations.** Several components already live in `plugins/shared/` but a subset of plugins were left carrying byte-identical private copies (Iterum's `parameter_helpers.h`/`note_value_ui.h`, the `OutlineButton` family, `FrameInvalidationGuard`, `editParamWithNotify`, `createComponentStateStream`). These are "finish the migration" tasks, not new abstractions.

Intra-plugin, the strongest wins are **config-driven parameter packs** (Ruinae's ADSR env trio and LFO1/LFO2 are near-verbatim duplicates) and **shared arp-lane base/templates** (chord vs inversion lanes are ~600 lines of near-identical code). DSP duplication is real but small per-site and dominated by **correctness/single-source-of-truth value over raw LOC** (one-pole coefficient, xorshift32 name collision, RBJ biquad switch duplicated within `biquad.h`).

**Headline realistic total LOC reduction: ~9,200 lines** across all `consolidate` + `investigate` candidates. The bulk (~5,600) is build + test scaffolding. I have deliberately discounted every candidate's optimistic estimate to net-of-new-shared-code figures.

A recurring theme: **many DSP and parameter-decode candidates save near-zero net lines** (one-liner → named-call swaps) — their real value is single-source-of-truth and drift prevention, not line count. These are flagged honestly below.

---

## 2. Ranked Top Recommendations (impact-to-effort)

Status legend: ✅ DONE (merged/committed) · ⬜ not started.

| Rank | Title | Scope | Est. lines saved | Effort | Status |
|------|-------|-------|------------------|--------|--------|
| 1 | Shared CMake helper module (version parse, AU targets, post-build copy, warnings) | cross-plugin (build) | 650 | medium | ✅ DONE — `cmake/KratePlugin.cmake`, PR #229 |
| 2 | Shared `IParamValueQueue`/`IParameterChanges` test mock header | cross-plugin (test) | 2400 | medium | ✅ DONE — `tests/test_helpers/vst_param_changes.h`, branch `test/shared-param-change-mocks` |
| 3 | Shared `IEventList` + note-on/off MIDI test helpers | cross-plugin (test) | 450 | medium | ✅ DONE — `tests/test_helpers/vst_event_list.h`, branch `test/shared-event-list-mock` |
| 4 | Ruinae ADSR env param files (amp/filter/mod) → one config-driven file | intra-area:ruinae | 400 | medium | ✅ DONE — `plugins/ruinae/src/parameters/env_params.h`, branch `refactor/ruinae-env-param-config` |
| 5 | Shared arp-lane base + enum-popup template (chord/inversion/condition/modifier) | intra-area:shared | 600 | medium | ✅ DONE — `plugins/shared/src/ui/enum_popup_arp_lane.h`, branch `refactor/shared-enum-popup-arp-lane` |
| 6 | OutlineButton CView base (+ PresetBrowserButton/SavePresetButton) hoist | cross-plugin (UI) | 300 | medium | ✅ DONE — `plugins/shared/src/ui/outline_button.h`, branch `refactor/ranks-6-to-9` |
| 7 | Single shared `version.h.in` + `win32resource.rc.in` templates | cross-plugin (build) | 320 | low | ✅ DONE — `cmake/version.h.in` + `cmake/win32resource.rc.in`, branch `refactor/ranks-6-to-9` |
| 8 | Ruinae LFO1/LFO2 param files → base-ID-parameterized functions | intra-area:ruinae | 175 | low | ✅ DONE — `plugins/ruinae/src/parameters/lfo_params.h`, branch `refactor/ranks-6-to-9` |
| 9 | Iterum `formatXxxParam` snprintf display helpers | intra-area:iterum | 170 | low | ✅ DONE — `plugins/iterum/src/parameters/param_display.h`, branch `refactor/ranks-6-to-9` |
| 10 | Iterum controller re-serializer → host-delegation (also fixes Freeze save bug) | intra-area:iterum | 170 | low | ✅ DONE — `createComponentStateStream` host-delegation, branch `refactor/ranks-10-to-13` |
| 11 | Per-lane ViewCreator registration template (shared arp lanes) | intra-area:shared | 180 | low | ✅ DONE — `plugins/shared/src/ui/arp_lane_view_creator.h`, branch `refactor/ranks-10-to-13` |
| 12 | Iterum private `parameter_helpers.h` + `note_value_ui.h` → shared wrappers | cross-plugin (param) | 235 | low | ✅ DONE — Iterum/Innexus wrappers over shared, branch `refactor/ranks-10-to-13` |
| 13 | `entry.cpp` plugin-factory macro | cross-plugin (build) | 110 | low | ✅ DONE — `plugins/shared/src/plugin_factory.h`, branch `refactor/ranks-10-to-13` |
| 14 | Iterum 11 per-mode param packs → descriptor-driven boilerplate | intra-area:iterum | 500 | high | ⬜ |

Effort key: **low** = mechanical, single PR, low test risk. **medium** = touches many files or needs golden/round-trip re-verification. **high** = larger refactor with state-format or escape-hatch complexity.

---

## 3. Detailed Findings by Scope

### 3.1 Cross-Plugin — Build & Bootstrap (strongest area)

**Per-plugin `CMakeLists.txt` boilerplate** (rank 1, ~650 net) — ✅ DONE (PR #229, `cmake/KratePlugin.cmake`: `krate_plugin_read_version` / `_configure_generated_files` / `_platform_setup` (KIND instrument|effect) / `_install_to_system` / `_set_warnings`; −1161/+82 across the CMakeLists). Token-neutralized `.in` templates (rank 7) deliberately left out of that PR.
Byte-identical in 4 regions across all 6 plugins:
- version.json parse preamble: `iterum/CMakeLists.txt:10-55`, `gradus:8-55`, `membrum:8-53`, `disrumpo:10-55`, `ruinae:10-55`, `innexus:10-55` (differ only by UPPERCASE prefix).
- AUv2 `FetchContent(AudioUnitSDK-1.1.0)` + AUv3 `smtg_add_auv3_app`: `gradus:121-222` vs `membrum:236-337`.
- `VSTWORK_COPY_TO_VST3_FOLDER` POST_BUILD copy: `gradus:235-251` vs `membrum:342-358` (byte-identical).
- MSVC/GCC warnings block: `gradus:256-274` (byte-identical everywhere).

Proposed: `cmake/KratePlugin.cmake` with `krate_read_plugin_version`, `krate_configure_plugin_generated_files`, `krate_add_plugin_au_targets(<target> BUNDLE_ID ENTITLEMENTS KIND instrument|effect)`, `krate_plugin_postbuild_install`, `krate_set_plugin_warnings`. **Critical divergence to preserve:** iterum/disrumpo are *effects* (extra `effect/ViewController.m`, `Main.storyboard`, `drumLoop.wav`); the others are *instruments* — the helper must take a KIND arg. Leave source lists, FUIDs, ParamID enums, and per-plugin extra targets (membrum's `membrum_dsp`/`membrum_preset_io`, ruinae's factory-preset install) local. macOS AU branch cannot be CI-verified on this Windows-primary repo — manual Xcode build check before merge.

**`version.h.in` + `win32resource.rc.in` templates** (rank 7, ~320 net combined) — ✅ DONE (branch `refactor/ranks-6-to-9`): single `cmake/version.h.in` + `cmake/win32resource.rc.in` with neutral `@KRATE_PLUGIN_*@` vars; `krate_plugin_read_version` mirrors version.json into `KRATE_PLUGIN_*` and sets a default `KRATE_PLUGIN_UI_VERSION` (`"<Name> v" VERSION_STR`); Iterum overrides to the bare `"v" VERSION_STR`. All 12 per-plugin `.in` files deleted; generated `version.h`/`.rc` verified per plugin; all 6 reconfigure + build clean.
6 structurally identical `version.h.in` (`iterum/src/version.h.in:10-37` etc.) differing only by `@PREFIX_*@` token and one `UI_VERSION_STR` line (iterum: `"v" VERSION_STR`; others `"@PREFIX_NAME@ v" VERSION_STR`). `VendorEmail` hardcoded identically (`rbzwaga@gmail.com`, line 34). 6 near-identical `.rc.in` (`iterum/resources/win32resource.rc.in:17-48` etc.); membrum omits a comment block (cosmetic). Proposed: single `cmake/version.h.in` + `cmake/win32resource.rc.in` with neutral `@KRATE_PLUGIN_*@` vars (incl. `@KRATE_PLUGIN_UI_VERSION@` for the one legitimate per-plugin difference), driven by the rank-1 helper. **Land bundled with the CMake helper** — done standalone, each CMakeLists still needs neutral-var + `configure_file` blocks, gaining little.

**`entry.cpp` factory skeleton** (rank 13, ~110 net)
`BEGIN_FACTORY_DEF` + 2×`DEF_CLASS2` + `END_FACTORY` character-identical modulo namespace/name: `iterum:32-68`, `disrumpo:30-66`, `ruinae:42-78`, `innexus:36-72`, `gradus:25-57`, `membrum:23-55`. Proposed: `plugins/shared/src/plugin_factory.h` with `KRATE_DEFINE_PLUGIN_FACTORY(Namespace, NameLiteral, SubCategories)`. Each entry.cpp keeps its own `ui/*.h` includes (drive ViewCreator registration — genuinely unique). **Handle plugin-name plumbing divergence:** iterum/ruinae/innexus/gradus `#define stringPluginName` locally; membrum gets it from `version.h`; disrumpo defines a separate `stringDisrumpoPluginName`. Macro taking the literal sidesteps this but local `#define`s must be removed.

### 3.2 Cross-Plugin — Test Scaffolding (largest raw count)

**`IParamValueQueue`/`IParameterChanges` mocks** (rank 2, ~2400 net) — ✅ DONE (branch `test/shared-param-change-mocks`: `tests/test_helpers/vst_param_changes.h`; 63 files migrated to `using` aliases, −3373/+417, ~2950 net). Container stores queues by value so `clear()`+`addChange` reuse capacity (keeps the zero-allocation processor tests green). Left local as predicted: `*Output*` capturers (arp_preset_e2e / arp_integration), innexus `VoiceMode*`, the two golden byte-identical tests, and membrum `test_dc_offset_trace` function-local `struct` mocks. All 5 plugin suites build warning-free and pass.
~60 queue subclasses + ~75 changes subclasses; plain single-value input body verbatim-identical except class name and addRef/release style. Confirmed at `ruinae/tests/integration/processor_audio_test.cpp:91-178`, `disrumpo/tests/integration/processor_audio_output_test.cpp:65-110`, `innexus/tests/integration/test_modulator_sync.cpp:71`, `ruinae/tests/unit/processor/arp_integration_test.cpp:103-247`. Proposed: header-only `Krate::Test::ParamValueQueue` + `ParameterChanges` in **`tests/test_helpers/`** (the existing INTERFACE lib already on every plugin test target's include path — *not* `plugins/shared/tests` which is a normal test target). **Do NOT merge output-capturing variants** (`ArpOutputParamQueue`, `E2EOutputParamQueue`) — they verify processor *writes* and are a genuinely different mock. ~45-55 of ~60 pairs consolidatable. Test-only — no RT/cross-platform constraint.

**`IEventList` MIDI mock + note-on/off helpers** (rank 3, ~450 net) — ✅ DONE (branch `test/shared-event-list-mock`: `tests/test_helpers/vst_event_list.h` with `EventList` (noteId −1) + `EventListNoteIdEqPitch` via a virtual `noteIdFor()`; 41 mocks aliased, −1690/+131). Actual mock count was higher than estimated (65 found). Kept local: stateful/synthesizer one-shot lists (`*NoteOnEvents` that build events inline), extra-helper mocks (`noteOn()`/`makeNoteOn()`/`addPitchBend`), non-`events_` storage accessed directly by the test, and the golden byte-identical tests. All affected suites build warning-free and pass.
~52 `IEventList` subclasses; 6 interface methods byte-identical everywhere sampled (`test_midi_event_dispatcher.cpp:24-47`, `test_output_noise.cpp:43-58`, `ruinae_byte_identical_post_lane10_test.cpp:47-56`, `midi_events_test.cpp:34-55`). Home: `tests/test_helpers/` (existing INTERFACE lib). **Respect divergence:** method names (addNoteOn/noteOn/makeNoteOn), and especially `noteId` semantics (most use -1, but `test_output_noise.cpp:68` and `test_stereo_spread_integration.cpp:127` set `noteId=pitch`) — expose as explicit params. **Caution:** `ruinae_byte_identical_post_lane10_test.cpp` and gradus `live_mode_byte_identical_test.cpp` are golden regression tests — migrate individually, preserving exact Event field values.

**`makeProcessSetup()` + test rate/block constants** (~200 net, investigate)
`gradus_vst_tests.cpp:31-42`, `innexus_vst_tests.cpp:35-47`, `membrum_vst_tests.cpp:46-54` byte-identical; 22 `makeSetup` defs + 27 `kTestSampleRate` constants. Signatures diverge (`makeSetup()`/`(double)`/`(double,int32)`), block sizes diverge (512 vs 128). Collapses to `makeProcessSetup(double sr=44100, int32 block=512)` but touching ~40 files is pure churn — **fold in opportunistically when those files are next touched**, not a dedicated sweep.

**Stereo-out render scaffolding** (~350 net, investigate)
~80 files build `outL/outR` + `AudioBusBuffers` + `ProcessData`. A templated `Krate::Test::StereoRenderContext` suits the dominant 0-in/stereo-out case only. Excludes legitimate divergence: numOutputs=0 drain/RT tests, input-bus effect tests, membrum multi-bus (`test_kit_switch_infinite_ring.cpp:1416-1455`). Disrumpo's `ProcessorFixture` is a separate justified abstraction.

### 3.3 Cross-Plugin — Controller / UI / Preset Glue

**OutlineButton CView base + button subclasses** (rank 6, ~300 net) — ✅ DONE (branch `refactor/ranks-6-to-9`): `Krate::Plugins::OutlineButton` (CView, color-parameterized via `OutlineButtonColors`, dark defaults) + `OutlineActionButton` (std::function) added to the existing `plugins/shared/src/ui/outline_button.h`; a shared `drawOutlineButton()` renderer is now used by both the CView family and the pre-existing CControl `OutlineBrowserButton` (no 3rd parallel path). ruinae/innexus/gradus/disrumpo dropped their local copies (dark convenience ctor keeps call sites byte-identical incl. ruinae's Arp/ADSR frame colors); Iterum keeps its light theme via a color triple; Membrum's name-colliding `ui/outline_button.h` was renamed `ui/membrum_buttons.h` (re-exports the shared types, keeps the local `IconExpandActionButton`) and its `controller.h` forward-decl of `OutlineActionButton` was retargeted to `Krate::Plugins`. All 6 plugins build clean + pass pluginval strictness-5.
`OutlineButton` base in 5 plugins: `ruinae/controller_presets.cpp:125`, `gradus:26`, `innexus:31`, `iterum/custom_views.h:27`, `disrumpo/custom_buttons.h:23`, plus membrum (`outline_button.h:15`). Thin `PresetBrowserButton`/`SavePresetButton` repeat in all 5. **Legitimate divergence to parameterize, not flatten:** iterum is *light theme* (`CColor(208,208,208)` frame, `(0,0,0,20)` hover fill, `(102,102,102)` font); membrum hardcodes `(64,64,72)` and adds `setTitle()`+`OutlineActionButton`. Proposed: shared CView base parameterized by colors (dark defaults) + a `std::function onClick` variant, so controllers do `new OutlineActionButton(rect, "Presets", [this]{ openPresetBrowser(); })` and drop local subclasses. **Avoid creating a 3rd parallel path** next to the existing `CControl OutlineBrowserButton` (`outline_button.h:26`) — ideally extract a shared render base both derive from. Membrum already proves the `OutlineActionButton` lambda pattern works.

**Iterum private copies already shared elsewhere** (rank 12, ~235 net combined)
- `iterum/controller/parameter_helpers.h:39-136` is semantically identical to `plugins/shared/src/ui/parameter_helpers.h:21-98`. Ruinae/Gradus are already thin re-export wrappers; only Iterum was left behind. Replace body with the 11-line `using` wrapper.
- `iterum/parameters/note_value_ui.h:41-82` and `innexus/parameters/note_value_ui.h:21-55` carry the full 30-entry table already in `plugins/shared/src/ui/note_value_ui.h:21-55`. Convert both to wrappers.
No CMake change (shared dir already PUBLIC and linked). Call sites unqualified inside plugin namespace — zero edits.

**Disrumpo hand-inlines the 30-entry note list 3×** (~15 net)
`parameter_registration.cpp:274-279,385-390,421-426`. Use shared `createNoteValueDropdown(...)`. **Behavioral care:** the 3 lists currently default to index 0 and LFO1/LFO2 omit `kIsList`; the helper sets default index 10 and adds `kIsList`. Both are deliberate consistency improvements but change registered param defaults/flags — build + pluginval after.

**`editParamWithNotify`** (~25 net, investigate)
5 controllers character-identical (`ruinae/controller_presets.cpp:615-622`, gradus `:211-218`, innexus `:436-443`, disrumpo `controller.cpp:1881-1887`, iterum `:1419-1430`). **Exclude membrum** (`controller.cpp:1001-1008`) — different edit ordering. Free function `Krate::Plugins::editParamWithNotify(EditController&, ParamID, double)`. Keep per-plugin forwarding methods (ruinae `preset_browser_test.cpp` calls it directly). Low payoff.

**`createComponentStateStream` host-delegation variant** (~22 net)
Byte-identical in ruinae `:452-467`, gradus `:173-186`, innexus `:178-191`. iterum/disrumpo correctly excluded (they re-serialize). Free function in `plugins/shared/src/preset`. Keep thin forwarders (ruinae test calls it).

**`FrameInvalidationGuard`** (~22 net)
Char-identical: iterum `controller.h:232-246`, ruinae `:423-437`, disrumpo `:463-477`. Move to `plugins/shared/src/ui/frame_invalidation_guard.h` as `Krate::Plugins::FrameInvalidationGuard`. gradus/innexus adoption optional.

**Visibility controllers + recursive tag-search** (~210 + ~120 net)
`VisibilityController` byte-identical iterum (`visibility_controller.h:36-166`) vs disrumpo (`visibility_controllers.h:35-132`). `ContainerVisibilityController` diverges (iterum has range-mode ctor). Recursive `ViewIterator`+`asViewContainer` walk reimplemented ~9-10× (iterum `:132/:275/:407`, disrumpo `:99/:200/:318`, `animated_expand_controller.cpp:108-124`, innexus `controller.cpp:2036/2082/2128/2183/2206`). Proposed: `plugins/shared/src/ui/param_visibility_controller.h` (base with `applyVisibility(bool)` hook) + `plugins/shared/src/ui/view_tree_search.h` with templated `findViewByTag<T>` + thin `findControlsByTag`/`findContainerByChildTag`. **These two candidates overlap — coordinate to avoid double-counting savings.** Disrumpo's 7 plugin-specific controllers and innexus's lazy member-caching stay local.

**Syncable knob sub-controller** (~80 net)
iterum `DelayTimeSyncController` vs innexus `ModulatorSubController` share resync + tag-swap (`delay_time_sync_controller.cpp:18-47/92-109` vs `modulator_sub_controller.cpp:27-52/101-117`). Shared base in `plugins/shared/src/ui/syncable_knob_sub_controller.h` taking `(freeTag, syncTag, noteValueTag)`. Innexus subclasses to add `getTagForName` remapping + `ModulatorActivityView` (legitimate divergence). Only 2 consumers — clears the "2nd use" bar but barely.

**Update-config factory + lifecycle wiring** (~52 + ~18 net)
4 byte-identical `*_update_config.h` (only namespace/factory-name/the verbatim endpoint URL differ). Proposed `inline makeUpdateConfig(name, version)` + `kKrateVersionsEndpoint` constant in `plugins/shared/src/update/update_checker_config.h`. Lifecycle member-pair + init/terminate repeat in 4 controllers — optional composable `UpdateCheckerHost` struct (composition, opt-in; gradus/membrum have no update wiring). createView blocks are a separate, harder helper — exclude.

### 3.4 Cross-Plugin — Parameter / Serialization

**Param-pack serialization idiom + arp scale tables**
- Per-section save/load/loadToController repeats ~30× in ruinae + mirrored in iterum/gradus. **Recommend a narrow shared header of `writeAtomic`/`readAtomicRequired`/`readAtomicTolerant` primitives only** (high value, low risk). Do NOT attempt a single 3-in-one descriptor: `loadToController` uses bespoke per-field affine maps, EOF-tolerance is positional/format-gated, and `getState`/`setState` interleave version gates not generatable from a table (`ruinae/processor_state.cpp:173-238`).
- Stepped-index decode `static_cast<int>(norm*(count-1)+0.5)` appears 42× in Iterum + 149× idiom in Ruinae. Add `decodeSteppedIndex/decodeBool/mapRange` to a **new** `plugins/shared/src/parameters/param_decode.h` (processor-side; existing `parameter_helpers.h` is UI-flavored). **Net ~55** — call sites keep their `.store()` wrappers and bespoke clamps. Do NOT merge affine/exp curve mappers (per-section choices).
- String128 display formatters: 3 divergent conventions exist; membrum already wrote the exact proposed API (`controller.cpp:2136-2217`). Lift those into `plugins/shared/src/ui/format_helpers.h`, fold disrumpo's `format_helpers.h:17-44`. **~110 net** — denorm math stays per-plugin, only the leaf format line unifies.
- Arp scale remap tables byte-identical: `ruinae/dropdown_mappings.h:610-651` vs `gradus:16-57`. Hoist to a neutral-namespace shared header. **~32 net** — value is drift prevention, not LOC.

### 3.5 DSP (Layer-disciplined; mostly correctness wins)

| Finding | Files | Net | Note |
|---------|-------|-----|------|
| RBJ biquad switch dup *within* `biquad.h` (`calculate` vs `calculateConstexpr`) | `biquad.h:684-765` vs `:811-892` | 80 | **Highest DSP win.** `template<class Trig> computeRBJ(...)` with `StdTrig`/`ConstexprTrig` policies. Keep StdTrig explicit for -ffast-math behavior. Off hot path. |
| yin reimplements `FFTAutocorrelation` | `yin_pitch_detector.h:132-163/317-342/520-524` | 80 | Compose `FFTAutocorrelation`; add `computeRaw()` (YIN needs unnormalized). Re-pass YIN tests. |
| additive_oscillator hand-rolls IFFT overlap-add | `additive_oscillator.h:578-602` | 55 | Use `OverlapAdd` (like `residual_synthesizer.h:65`). Drop `fftSize/3.5` fudge → `N/2` scale. **Re-baseline SC-007/THD/tilt tests.** |
| xorshift32 dup (`XorShift32` struct vs `Xorshift32` class) | `core/random.h:40-91` vs `core/xorshift32.h:24-57` | 45 | Keep the class; add `seedFromId`/`nextFloatSigned`; migrate `waveguide_string.h`, `impact_exciter.h`, `mode_inject.h`. **`nextFloat()` semantics differ ([0,1) vs [-1,1]) — check each site; golden re-baseline.** Real value: remove one-capital-letter name collision. |
| FDN Hadamard/Householder scalar↔SIMD dup | `fdn_reverb.h:696/749` vs `fdn_reverb_simd.cpp:82/126` | 35 | Extract scalar reference + unify constants only; leave Highway kernels. Don't fuse scalar+SIMD behind one API. |
| Smoothed dry/wet mix loop in 9 effects | `reverse_delay.h:194`, `multi_tap_delay.h:660`, `shimmer_delay.h:764`… | 45 | Layer-1 `DryWetMixer` for the clean per-sample cases; document granular/spectral/freeze exceptions. Value = consistency. |
| Per-block dry-scratch pattern | `digital_delay.h:600`, `shimmer_delay.h:744`… | 25 | `DryWetBuffer` for digital/shimmer/reverse/freeze only; exclude spectral/ducking. |
| One-pole tau coeff `exp(-1/(tau*fs))` | ~18 sites (`biquad.h:628`, `svf.h:531`, `modal_resonator_bank.h:173`…) | 15 | `core/smoothing_coeff.h`. **Multi-variant + bit-exactness staging required** — investigate, multi-PR. |
| One-pole cutoff coeff `exp(-2pi*fc/fs)` | `one_pole.h:135/236`, `dc_blocker.h:233`, `body_resonance.h:623` | 6 | Mainly to unify inconsistent pi spellings (`bow_exciter.h:105` hardcodes pi). |
| equal-power pan `(pan+1)*kPi*0.25` | `harmonizer_engine.h:352/438/516` (3 copies), `unison_engine.h:360`, `timevar_comb_bank.h:768` | 12 | `core/stereo_utils.h`. **Must reproduce `*kPi*0.25f` verbatim** (not the proposed `*kHalfPi*0.5`) for bit-exactness. Leave the 2 `panNorm*kHalfPi` [0,1] sites. |
| `nextPowerOf2` dup | `delay_line.h:26` vs `rolling_capture_buffer.h:210-220` | 10 | Minimal: include `delay_line.h`, drop private copy. |
| DCBlocker2/BowedModeBPF hand-rolled RBJ | `dc_blocker.h:357-380`, `modal_resonator_bank.h:37-47` | 8+10 | **Coefficient-only swap; keep DF1/bare-TDF2 process loops** (NaN-guard/denormal differences). Reconcile clamp ranges first. |
| Catmull-Rom dup, equal-power MIX, semitone/cents | `delay_line.h:257-263`, `fdn_reverb.h:374`, `pitch_utils.h:129` | 4-6 each | Trivial drop-ins. Catmull-Rom is bit-identical (zero golden risk). |

### 3.6 Intra-Plugin

**Ruinae** — ADSR env trio (rank 4, ~400) — ✅ DONE (branch `refactor/ruinae-env-param-config`): single config-driven `parameters/env_params.h` now holds an `EnvParams` base + `AmpEnvParams`/`FilterEnvParams`/`ModEnvParams` (defaults-only derived structs), a 20-entry `EnvField` table (member ptr + ID offset + map kind) driving base-parameterized `handleEnvParamChange`/`saveEnvParams`/`loadEnvParams`/`loadEnvParamsToController`/`formatEnvParam`/`registerEnvParams`, plus thin per-envelope wrappers preserving the historical named API. The three old files became 2-line re-export shims, so zero processor/controller/preset/test call-site edits. Exact STR16 labels (`registerEnvParams` composes prefix+suffix via `UString`), normalized register defaults, stream order, and backward-compat (A/D/S/R mandatory; curve/Bezier optional) preserved. ~−411 net lines; ruinae_tests (256 cases / 13530 assertions) + pluginval strictness-5 green; build warning-free. (clang-tidy not run: LLVM absent in this env.) Original spec differed only in 2 default ms values, label prefixes, and ID set; `plugin_ids.h:353-433` lays IDs out as 3 contiguous +100-offset blocks. LFO1/LFO2 (rank 8, ~175): `lfo2_params.h` verbatim-duplicates `lfo1_params.h` modulo `+100` IDs; base-ID-parameterized functions. — ✅ DONE (branch `refactor/ranks-6-to-9`): single `parameters/lfo_params.h` holds `LFOParams` + base-ID-parameterized `handleLFOParamChange`/`registerLFOParams`/`formatLFOParam`/save/load/extended/toController (offset switch on `id - base`, label prefix passed in); `lfo1_params.h`/`lfo2_params.h` are now thin shims preserving the historical named API, so zero processor/controller/preset/test call-site edits. ruinae_tests (706 cases) + pluginval green. The 29-section 6-function template is real but **NOT a uniform descriptor** (investigate, scope to save/load scaffolding of ~20 simple hard-fail sections; osc/arp/delay have versioned/backward-compat loads). Stepped-dropdown decode (`controller_view_sync.cpp:104-238`) — a helper *already exists* (`paramInt` lambda); route remaining if-blocks through it (~15).

**Iterum** — `formatXxxParam` snprintf helpers (rank 9, ~170) — ✅ DONE (branch `refactor/ranks-6-to-9`): new `parameters/param_display.h` with `formatParamText(String128, fmt, ...)` (varargs + `format(printf)` attribute) collapses the repeated `char[32]`/snprintf/UString/return idiom across all 11 packs (bbd/digital/multitap/pingpong/freeze/tape/granular/pattern_freeze/shimmer/reverse/spectral); output bit-identical, plugin_tests (240 cases) + pluginval green; controller re-serializer → host-delegation (rank 10, ~170, **also fixes a latent Freeze preset-save bug** at `controller.cpp:1290-1307` writing the old legacy layout — needs a round-trip test). 11 per-mode packs (rank 14, ~500) — keep named atomic structs (read directly by DSP), table-drive only boilerplate; treat freeze/pattern_freeze/multitap/tape handlers as partial. Note-value/toggle decode (`bbd_params.h:64` etc.) ~8 net (correctness, not LOC).

**Disrumpo** — IDependent boilerplate + tag-search (~115, overlaps cross-plugin tag-search); distortion-type + note-value lists (~40, names already authoritative in `dsp/distortion_types.h:214`); `getParamStringByValue` idiom (~28); per-band/node name arrays via UString128 generation (~55, investigate — exclude the version-gated serialization loops); common-node denorm ranges (~20, fixes a folds-round drift).

**Innexus** — mod1/mod2 processor blocks verbatim (~48, group into `ModBundle` array, ripples into state save/load); DetectedADSR/RecalledADSR handlers (~18, shared `logNorm` free fn + attribute helper); dead `InnexusParams` scaffolding deletable while keeping `releaseTimeFromNormalized/ToNormalized` (~45).

**Gradus** — `constructArpLanes` per-lane blocks (~105, two typed/templated helpers; the 9-lane single-template premise is false — different setter names/scales); begin/perform/set/end quad (~27, a helper `editParamWithNotify` *already exists* at `controller.h:108`, route ~8 sites through it, leave `piano_roll_view.h` standalone); `syncViewsFromParams` enum lanes (~26); lane pointer-array dedup in `controller_arp.cpp` (~12 — the view_sync arrays use a *different* ring-geometry index space, do not merge).

**Membrum** — modal body mappers `map()` + `lerp()` (~65, traits-parameterized for plate/shell/bell only; exclude Noise/Membrane — FR-031 bit-identity); modal body wrappers Membrane/Plate/Shell/Bell → `template<class Mapper> ModalBody` (~150, Shell's `decayScale` via policy param).

**Shared arp lanes** — chord/inversion near-identical (~600, `EnumPopupArpLane` template, keep 2 named concrete shells + ViewCreators for uidesc strings) — ✅ DONE (branch `refactor/shared-enum-popup-arp-lane`): `EnumPopupArpLane<Traits>` + `EnumPopupArpLaneCreator<LaneT>` in `enum_popup_arp_lane.h`; `ArpChordLane`/`ArpInversionLane` are now trait-driven shells (count/divisor/labels/lane-type-id/view-names only). −596 net; ruinae/gradus/shared tests + pluginval strictness-5 green; condition (custom invert table + tooltips + `setStepCondition` API) and modifier (dot-grid bitmask) deliberately excluded; IArpLane interface boilerplate across 4 lanes → `ArpLaneBase` (~260, non-template, exclude `ArpLaneEditor`); per-lane ViewCreator template (~180); trail/skip/playhead overlay free helper (~135, 4 enum lanes only — `arp_lane_editor.h:1227` uses different geometry); `drawMiniPreview`/`drawDisabledOverlay` free functions (~65, exclude modifier's dot-based preview).

---

## 4. Proposed New Shared Abstractions

| Location | Abstraction | Rough API |
|----------|-------------|-----------|
| `cmake/KratePlugin.cmake` | Plugin build helpers | `krate_read_plugin_version`, `krate_configure_plugin_generated_files`, `krate_add_plugin_au_targets(KIND ...)`, `krate_plugin_postbuild_install`, `krate_set_plugin_warnings` |
| `cmake/version.h.in`, `cmake/win32resource.rc.in` | Single generated templates | neutral `@KRATE_PLUGIN_*@` substitution vars |
| `plugins/shared/src/plugin_factory.h` | Factory macro | `KRATE_DEFINE_PLUGIN_FACTORY(Namespace, NameLiteral, SubCategories)` |
| `tests/test_helpers/vst_param_changes.h` | Param-change mocks | `Krate::Test::ParamValueQueue`, `ParameterChanges`, `makeProcessSetup(double=44100,int32=512)` |
| `tests/test_helpers/midi_events.h` | MIDI mocks | `Krate::Test::EventList`, `makeNoteOn(pitch,vel,ch=0,off=0,noteId=-1)`, `makeNoteOff(...)` |
| `plugins/shared/src/ui/view_tree_search.h` | View-tree search | `findViewByTag<T>(root,tag)`, `findControlsByTag`, `findContainerByChildTag` |
| `plugins/shared/src/ui/param_visibility_controller.h` | Visibility base | `ParamVisibilityControllerBase` + `applyVisibility(bool)` hook |
| `plugins/shared/src/ui/format_helpers.h` | String128 formatters | `formatPercent/formatMs/formatHz/formatDb/formatSignedFloat(value, String128)` (lift from membrum) |
| `plugins/shared/src/ui/frame_invalidation_guard.h` | RAII guard | `Krate::Plugins::FrameInvalidationGuard(VST3Editor*)` |
| `plugins/shared/src/ui/syncable_knob_sub_controller.h` | Sync-knob base | `SyncableKnobSubController(freeTag,syncTag,noteValueTag,parent)` |
| `plugins/shared/src/parameters/param_decode.h` | Processor-side decode | `decodeSteppedIndex(norm,count)`, `decodeBool(norm)`, `mapRange(norm,lo,hi)` (constexpr/inline) |
| `plugins/shared/src/preset/controller_state_helpers.h` | State glue | `editParamWithNotify(EditController&,ParamID,double)`, `createComponentStateStream(EditController&)` |
| `plugins/shared/src/update/update_checker_config.h` (extend) | Update config | `makeUpdateConfig(name,version)`, `kKrateVersionsEndpoint`; optional `UpdateCheckerHost` |
| `dsp/core/smoothing_coeff.h` | Layer-0 coeffs | `onePoleCoeffFromTau/FromMs/FromCutoff(...)` constexpr noexcept |
| `dsp/core/stereo_utils.h` (extend) | Equal-power pan | `equalPowerPan(pan, &gainL, &gainR)` — verbatim `(pan+1)*kPi*0.25f` |
| `plugins/shared/src/ui/arp_lane_base.h` + `enum_popup_arp_lane.h` | Arp lane base/template | `ArpLaneBase : CControl, IArpLane`; `EnumPopupArpLane<count,divisor,labels,typeId>`; `ArpLaneCreator<ViewT>` |

---

## 5. Appendix — Considered but Rejected

- **Versioned getState/setState envelope** — version validation diverges (range vs allowlist vs none vs migration); EOF contracts diverge; iterum writes no version int. A shared helper would thread per-plugin policies adding ~as much as it removes, at preset-format-break risk. ~10-15 net. Skip.
- **`notify()` IMessage prologue** — 3-4 lines of shallow SDK boilerplate; early-return value diverges per site; the int64↔ParamValue bit-cast exists in *one* plugin only (gradus); disrumpo citation was wrong (a send path, no notify override). Skip.
- **PresetManager instantiation block** — already shared at the substantive layer; only an ~8-line handshake repeats, and the varying save/load lambdas (ruinae arp branch, membrum dual-manager post-load) can't be removed. <10 net. Skip.
- **open/closePresetBrowser stubs** — substantive views already shared; remaining glue carries real divergence (iterum dynamic subcategory, ruinae mutual-exclusion). ~18 net. Skip.
- **Disrumpo MorphPad vs shared XYMorphPad** — different backgrounds (IDW node-graph vs fixed bilinear), no crosshair in disrumpo, incompatible base classes (FObject+IDependent vs copyable CControl). ~20 genuinely common lines behind 180 claimed. Skip.
- **Update createView blocks / open-close polling** — real dup is only iterum↔disrumpo (~20 lines); innexus uses a different uniform pattern, ruinae has no such branches. The config-factory headers (rank-12-adjacent) are the real win, filed separately. Skip the createView part.
- **`plugin_ids.h` skeleton** — only a ~14-line include/comment preamble is common; FUIDs/ParamID enums/state-version are intentionally unique and load-bearing. 0 extractable. Skip.
- **MemoryStream round-trip test helper** — the bulk is irreducible plugin-specific byte-layout; innexus uses a custom IBStream, gradus loads file fixtures. ~40 net behind 250 claimed. Skip.
- **SmoothedBiquad inline smoother** — same IIR topology but different coefficient convention (tau vs 99%-time + clamp), different completion/denormal semantics, different API; per-sample audio-thread behavior change for ~22 lines. Skip (add a comment instead).
- **SchroederAllpass vs DiffusionNetwork::AllpassStage** — different topology (what's stored, linear vs allpass interpolation, per-sample-delay modulation). Legitimately distinct. Skip.
- **spectral_freeze overlap-add** — its linear-sum COLA is *correct* for its no-analysis-window path; OverlapAdd's w² norm assumes a Hann analysis window — a naive swap is a ~2.5 dB gain error. Output ring + pre-fill ramp also can't be replaced. Skip.
- **reverb.h interpolation** — cubic already delegates; only a 1-line linear blend overlaps an existing helper; contiguous-buffer design must stay. ~1 net. Skip.
- **Two SPSC ring buffers** — divergent contracts (reject-on-full FIFO vs overwrite-on-overflow stream; count+read+write vs single cumulative; pop vs block-copy). Skip until a 3rd consumer.
- **FeedbackNetwork vs FlexibleFeedbackNetwork** — fundamentally different feedback timing (per-sample loop vs block-deferred ~11.6ms latency); cross-feed vs route-mix/hot-swap don't overlap; heavy DSP already shared via primitives. Skip.
- **ModulationEngine vs ModulationMatrix** — different semantics at every step (range-scaled vs normalized, curves vs none, single vs dual-endpoint); non-overlapping consumers. Skip.
- **Iterum per-mode dispatch chains** — struct-type binding + incompatible signatures defeat a unified registry; only 2 of 6 lists branch; one is on the audio thread. ~20 net. Skip.
- **Ruinae chorus/flanger/phaser structs** — NOT identical (phaser interleaves `stages` mid-stream; distinct range constants); high-blast-radius state code. Skip.
- **Innexus 5-view draw boilerplate** — ~3 truly-shared lines, divergent bg colors/semantics, 3 different updateData signatures; real dup is intra-file in `confidence_indicator_view.cpp`. Skip.
- **Innexus display-data fan-out** — non-uniform view signatures; works only via empty-struct coincidence; 2 call sites. ~10 net. Skip.
- **Gradus 32-step lane save/load** — load side diverges (per-lane clamp bounds validating untrusted preset data, per-block EOF/migration policies). ~40 net behind 120, at migration-break risk. Skip.
- **Membrum BodyBank vs ExciterBank** — every visitor signature differs (sharedBank threading, trigger vs noteOn semantics, lastOutput tracking); ~10-15 trivial common lines. Skip.
- **Membrum exciter wrappers** — only 2 of 6 are thin ImpactExciter wrappers; the other 4 are bespoke; std::variant static dispatch forbids a virtual base. ~0 net. Skip.
- **Membrum toPadSnapshot/applyPadSnapshot** — not a clean mirror (asymmetric clamp/sentinel/skip policies); member-pointer table can't express policies; different key domain from controller offset tables. Skip.
- **Highway SIMD dispatch scaffolding** — the `HWY_TARGET_INCLUDE` self-path is intrinsically per-file; export blocks are function-specific; macroizing obscures framework idiom. ~30 net behind 120. Skip.
- **One-pole coeff "40+ sites"** — actually 5 mathematically distinct, non-interchangeable formulas; a shared helper already exists (`smoother.h:77`) with a different convention; one-liner swaps net ~0 LOC. Skip (narrow investigate only).
- **Envelope-follower "reimplemented"** — 5 of 7 cited sites already use the canonical `EnvelopeFollower`; the one genuine inline site is a SIMD batch kernel that reusing the scalar class would de-vectorize. ~0 net. Skip.
- **Inline dB↔linear** — accurate finding but 1:1 expression swaps (~0 LOC); `dbToGain` routes through software `constexprExp` differing at 6th-7th decimal in per-bin hot loops feeding golden tests. Cost > cosmetic benefit. Skip.
- **Resonator-bank SoA scaffolding** — shared *pattern* not code; field sets, counts (96/48/64/16), coefficient computation, SIMD call signatures, and tail logic all diverge; common smoothing already factored into OnePoleSmoother. ~0 net. Skip.