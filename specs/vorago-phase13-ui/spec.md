# Feature Specification: Vorago Phase 13 — UI (Concept-First Editor + Ecosystem View)

**Spec slug:** `vorago-phase13-ui`
**Roadmap:** `specs/Vorago-roadmap.md` → Part B, Phase 13 (lines 560–567); Part B preamble (lines 519–522,
*"Follows the Seraphis Part B template nearly verbatim"*); Cross-Cutting Constraints (lines 600–624)
**Depends on:** Phase 12 (`fbf1a039`, 108 registered IDs, state v2 = 428 bytes, ✅), Phase 11 (`75a71e10`,
plugin scaffold + editor-lifecycle enrollment, ✅), Phase 8 (`EcosystemEngine`, ✅), Phase 10
(`VoragoEngine` / `VoragoVoice`, ✅)
**Template:** `specs/seraphis-phase11-ui/spec.md` — the shipped Seraphis UI phase (its cloud-frame data path,
custom-view lifecycle and binding-completeness criteria are the model; its per-partial *editing* surface has
no Vorago counterpart and is not copied)
**Status:** DRAFT — specification only, no implementation
**Date:** 2026-09-26 (branch `feat/vorago-phase1-events-modulation` at `e86fc1f8`)

---

## Overview

Every one of Vorago's 108 registered parameters exists, denormalizes, persists and reaches the DSP
(`plugins/vorago/src/controller/controller.cpp:62-77` registers them in band order), but the editor is still
the Phase 11 placeholder: a 420 × 520 px `CViewContainer` (`plugins/vorago/resources/editor.uidesc:35`) with
fourteen stock controls and a banner that says *"Phase 11 PLACEHOLDER — Phase 13 replaces this file wholesale;
stock views only"* (`editor.uidesc:3`). Ninety-two of the 106 user-visible IDs have no control at all.

The roadmap's Phase 13 text (lines 564–567) is four commitments, and this spec is the binding reading of them:

1. **VSTGUI only** — no platform view, ever (root `CLAUDE.md`, *Cross-Platform Requirement*).
2. **Concept-first layout** — the twelve concept macros dominate the window; the engine panels sit beneath.
3. **One signature visualization, the ecosystem view** — the live agent habitat of the focus voice's
   `EcosystemEngine`: agents as glowing points, energy as brightness, interactions as fading links, fed by
   **DataExchange piggyback** on the Membrum `MetersBlock` pattern, **no new queues**.
4. **No param-type swaps on registered IDs, ever.**

Four facts read out of the code this session shape the design:

1. **Everything the view draws per agent is already public, const and allocation-free.**
   `VoragoEngine::getVoice(std::size_t) const` returns `const VoragoVoice&` (`vorago_engine.h:1280`),
   `VoragoVoice::ecosystem() const` returns `const EcosystemEngine&` (`vorago_voice.h:1514`), and
   `EcosystemEngine` exposes `getAgentPositionX/Y` (`ecosystem_engine.h:896`, `:899`, on the unit torus
   `[0, 1)` — `wrap01` at `:1134`), `getAgentKind` (`:893`), `getAgentEnergy` (`:890`), `getAgentOutput`
   (`:886`, the `[0, 1]` published value) and `getAgentCount` (`:905`).
2. **The interactions are NOT public.** Stage 2 of every simulation step records each surviving pair into
   private scratch — `pairI_[pairCount_] = i; pairJ_[pairCount_] = j; pairFlow_[pairCount_] = flow;`
   (`ecosystem_engine.h:1461-1464`, members at `:2459-2461`) — and the only public read is the count,
   `getPairInteractionCount()` (`:961`). "Interactions as fading links" therefore needs **one additive,
   read-only accessor group on a shipped Layer 3 header** (C-3). The alternative — a controller-side guess
   from agent distance — would draw links the simulation did not make, and is rejected.
3. **The Vorago processor has no DataExchange, no `connect`/`disconnect`, and no `notify`.** Its public
   overrides are `initialize/terminate/setBusArrangements/setupProcessing/setActive/process/setState/getState`
   (`plugins/vorago/src/processor/processor.h:68-78`). All three connection-point overrides and the handler
   member are Phase 13 additions, copied from Membrum (`plugins/membrum/src/processor/processor.cpp:1137-1162`).
4. **The carried CPU gate would not see the producer.** Phase 11/12's wrapper-overhead gate times arm P as
   bare `proc->process(data)` calls on an unconnected `Processor` (`plugins/vorago/tests/integration/processor_cpu_test.cpp:1-30`,
   ceiling `kWrapperOverheadCeiling = 1.05` at `:75`). A producer that runs only when a DataExchange handler
   exists would be skipped by that arm, so the gate would pass vacuously. C-2 clause 6 adds a test seam that
   forces the producer on, and SC-012 re-runs the gate with it on — Seraphis hit the identical trap
   (`specs/seraphis-phase11-ui/spec.md` C-2 clause 7).

---

## Clarifications

### Session 2026-09-26

- **Q1 — Preset-browser UI: Phase 13 or Phase 14?** → (a) Phase 13 ships the preset-browser UI: header
  preset control, `PresetBrowserView`/`SavePresetDialogView` and both controller providers (state + load);
  SC-023 goes live. Rationale: Vorago state is parameters only, so the load provider needs no
  controller-to-processor message; every other Krate plugin wires both providers. [FR-070, FR-071, FR-072,
  FR-073, SC-023]
- **Q2 — Surface the anchor-mode / Gravity inertness?** → (a) Dynamic label/tooltip on the Gravity knob,
  driven by the controller's cached `kResonanceAnchorModeId` value; display-only, writes no parameter, no
  dimming; SC-022 live. [FR-080, FR-081, SC-022]
- **Q3 — Which voice(s) does the ecosystem view show when more than one note sounds?** → (a) Focus voice
  only, using the Seraphis rule as written (newest note); frame stays at its fixed size; no overlay, no UI
  voice selector, no controller-to-processor channel. Confirms the design already carried by C-2 clause 4 as
  final, not provisional. [FR-023, SC-009]
- **Q4 — Publish cadence: once per `process()` call, or only when the habitat changed?** → (b) Publish only
  when the habitat changed: fill a frame only when the focus voice's `getControlStepCount()` advanced, or
  focus/agentCount changed, or on the first call after connect; at most one fill per simulation step, cost
  independent of host buffer size; SC-008 rewritten around step changes. [FR-022, FR-026, SC-008, SC-012]
- **Q5 — Link width/alpha: normalized per frame, or absolute?** → (b) Absolute link scaling: `|flow|` scaled
  against the mean per-agent share (`energyBudget / agentCount`) with a fixed soft clip so magnitude is
  visible frame to frame; the frame carries the scale as one extra `float` (`linkFlowScale`). [FR-020,
  FR-020a, FR-024, FR-050, SC-006, SC-014, SC-015]
- **Q6 — What do the carried `voiceLevel` and `activeVoices` fields do in the view?** → (b) `voiceLevel`
  scales the whole habitat's brightness (a releasing voice fades before rule (c) empties the view) and
  `activeVoices` is drawn as a small count; both get C-6 clauses and SC-014 arms; no field in the frame is
  dead. [FR-050, FR-051, SC-006, SC-014]

### Plan stage (2026-09-26)

- **R-1 — Header preset button (plan D-1).** → Reuse the shared `Krate::Plugins::OutlineBrowserButton`
  (`plugins/shared/src/ui/outline_button.h:79`), exactly as Seraphis ships it
  (`plugins/seraphis/src/controller/controller.cpp:478`). No plugin-local `PresetBrowserButton` class and no
  `controller_presets.cpp`; the `custom-view-name` stays `"PresetBrowserButton"`. The ODR row is amended.
  [FR-042, FR-070, SC-023]
- **R-2 — `SavePresetDialogView` has no opener.** → (c) Drop it and amend FR-071: the header carries only the
  browser button, and `PresetBrowserView` has its own save dialog (`preset_browser_view.cpp:615-620`), which
  is how Seraphis ships (no separate dialog). Rejected: a header Save button (session tag 9001) and building
  an unreachable view. [FR-042, FR-071, SC-016, SC-023]
- **R-3 — `habitatBrightness` mapping (Q6 follow-up).** The raw `getVoiceLevel` is a linear chunk-peak
  follower (`vorago_voice.h:2151-2154`); a held drone peaking at 0.1–0.25 would draw at 10–25 % alpha. →
  dB-normalized: `habitatBrightness(v) = clamp((20·log10(v) + 60) / 60, 0, 1)`, `0` at or below `0`,
  `1` at or above `1`: silence 0, −40 dB (0.01) → 1/3, −20 dB (0.1) → 2/3, −6.02 dB (0.5) → 0.8997, full
  scale 1. The frame still carries the raw `voiceLevel` (SC-006 unchanged); the mapping lives in the view.
  [FR-050, SC-014]
- **R-4 — Gate-only test seam (plan D-2).** → Add `setEcosystemFrameEnabledForTest(bool)`, which opens the
  gate as if a handler existed and leaves C-2 clause 2's natural trigger in place, beside FR-026's forced seam
  (which forces gate **and** trigger, for SC-012). SC-008's cadence arms run under the enabled seam.
  [FR-026, SC-008, SC-012]
- **R-5 — SC-016 (b) timer pump.** → Accept: no portable headless pump exists and a Win32 pump in a test is
  platform code. SC-016 (b) asserts `hasTimerForTest() == false` after `removed()`, sleeps 150 ms in 10 ms
  slices after destroying the frame owner, and relies on the recorded ASan run for the no-access proof.
  [SC-016]
- **R-6 — CHANGELOG heading and version (FR-063).** → Bump `version.json` to **0.2.0** and write one
  `## [0.2.0] - <date>` entry covering the Phase 12 parameter surface (which left no entry) and the Phase 13
  interface, matching the Seraphis one-entry-per-phase pattern (`plugins/seraphis/CHANGELOG.md` 0.3.0,
  0.4.0). Rejected: an `[Unreleased]` heading (no Krate plugin uses one) and deferring to Phase 14.
  [FR-063]
- **R-7 — Task format.** → Accept the single CMake registration task in the last group, as every phase has
  run: red/green is by authoring order; the integration group observes the real red/green. [FR-060]

---

## Scope

Phase 13 ships, and nothing else:

1. **`resources/editor.uidesc`, replaced wholesale** with the concept-first layout of C-1 (fixed window;
   header; concept band = twelve large macro knobs around the ecosystem view; engine-panel band beneath with
   seven pages). VSTGUI only.
2. **Exactly one new custom `CView`: `Vorago::UI::EcosystemView`** — the signature visualization. The
   macros are twelve instances of the existing shared `Krate::Plugins::ArcKnob` (`plugins/shared/src/ui/arc_knob.h:49`);
   every engine-panel control is a stock or shared view. The page switch is handled by one
   `VSTGUI::DelegationController` subclass, which is not a `CView` (C-5).
3. **The ecosystem-frame data path**: one POD payload `Vorago::EcosystemFrame`, published by the processor
   once per `process()` call through a `Steinberg::Vst::DataExchangeHandler`, consumed by the controller as an
   `IDataExchangeReceiver` — the Membrum `MetersBlock` pattern, no new queue, no polling IMessage loop (C-2).
4. **One additive read-only accessor group on `EcosystemEngine`** exposing the most recent step's recorded
   pair table (C-3). No behaviour change; no Seraphis consumer (the header's only includer is
   `vorago_voice.h`).
5. **Every visible parameter bound exactly once** (106 IDs), with a two-entry allowlist for the two hidden
   performance controllers (C-4).
6. **Tests**: uidesc binding completeness and per-view class rule, editor-lifecycle harness with the custom
   view, frame content/cadence/determinism/RT-safety, link selection, controller consumer, view mapping
   functions, the carried wrapper-overhead CPU gate with the producer forced on, pluginval strictness 5.
7. **The decisions earlier specs handed to this phase, now ruled (see *Clarifications*).** The preset-browser
   UI (Phase 11 FR-050: *"The browser UI belongs to Phase 13/14"*) is ruled Phase 13 (Q1): the header preset
   control, `PresetBrowserView` (with its built-in save dialog, R-2) and both controller providers ship, and
   SC-023 is live.
   Whether the UI surfaces the anchor-mode / Gravity inertness (Phase 12 edge case: *"Phase 13 may surface
   it"*) is ruled yes, display-only (Q2): SC-022 is live. A third — Phase 9 FR-037 naming Phase 13 as a
   consumer of `CavernVerb::getDamperOffsetOctaves` — is already settled by the roadmap (see *Non-goals*).

## Non-goals (what other phases own, or nobody does)

| Owned by | What |
|---|---|
| Phase 14 (`vorago-phase14-presets-release`) | Factory presets, the final category set (categories only grow; `Drones` is permanent — `plugins/vorago/CLAUDE.md` decision 2), the all-presets long-render sweep, `getTailSamples()` revisit, the release gate |
| Nobody (roadmap line 567) | Any change to a registered parameter's type, range, default, flags or ID |

Within Phase 13:

- **No new registered parameter and no state-format change.** `kCurrentStateVersion` stays 2 and
  `kStateV2Bytes` stays 428 (`plugins/vorago/src/plugin_ids.h:23`, `:28`). UI state — the active engine page,
  link fade memory — is controller session state, never a `ParamID`, never in the stream.
- **No editing surface on the ecosystem view.** It is display-only. The roadmap asks for a visualization; no
  agent is dragged, spawned or woken from the UI, so there is **no controller → processor channel** in this
  phase.
- **No `dsp/` behaviour change.** The only `dsp/` edit is C-3's additive const accessors. Any edit to an
  existing function body in `dsp/` means the design has left this spec.
- **No second visualization.** No cloud view, spectrum, meters, or CPU readout (roadmap: *"One signature
  visualization"*).
- **No damper-offset display.** Phase 9 FR-037 (`specs/vorago-phase9-cavern-space/spec.md:609-612`) and its
  plan (`plan.md:204-205`, *"what Phase 13 will visualise"*) named Phase 13 as a consumer of
  `CavernVerb::getDamperOffsetOctaves` (`dsp/include/krate/dsp/effects/cavern_verb.h:829`). The roadmap's
  single-visualization rule (line 565) overrides that earlier-phase note, so no damper display ships and the
  accessor stays test-only.
- **No macro-reaction animation** beyond what the frame carries. The roadmap does not ask for Seraphis-style
  "rings perturb the view"; the ecosystem view shows the real simulation, which already responds to the Life
  macro through the engine.
- **No reuse of `Seraphis::UI::MacroRingKnob` / `CloudView` / `DrawerContainer`.** They are plugin-local to
  Seraphis (`plugins/seraphis/src/ui/macro_ring_knob.h:55`); moving one into `plugins/shared/` would touch
  Seraphis and is out of scope. The shared `ArcKnob` is what `MacroRingKnob` itself derives from, so the reuse
  point is the base, not the Seraphis subclass.
- **No resizable window, no zoom, no `IPlugViewContentScaleSupport` work.**

---

## Existing components (verified this session)

Every row was opened this session; signatures are quoted from the cited line.

| Component | Header / file (layer) | What Phase 13 reuses — verified signature |
|---|---|---|
| `EcosystemEngine` | `dsp/include/krate/dsp/systems/ecosystem_engine.h` (L3, includes Layer 0 + stdlib only, `:70-80`) | The frame source. `[[nodiscard]] float getAgentOutput(std::size_t i) const noexcept` (`:886`); `[[nodiscard]] double getAgentEnergy(std::size_t i) const noexcept` (`:890`); `[[nodiscard]] Kind getAgentKind(std::size_t i) const noexcept` (`:893`); `[[nodiscard]] double getAgentPositionX(std::size_t i) const noexcept` / `getAgentPositionY` (`:896`, `:899`); `[[nodiscard]] std::size_t getAgentCount() const noexcept` (`:905`); `[[nodiscard]] bool isAgentDormant(std::size_t i) const noexcept` (`:829`); `[[nodiscard]] std::size_t getPairInteractionCount() const noexcept` (`:961`, *"on the most recent simulation step (NOT a cumulative total)"*); `[[nodiscard]] std::uint64_t getControlStepCount() const noexcept` (`:1001`); `[[nodiscard]] double getEnergyBudget() const noexcept` (`:921`), `[[nodiscard]] std::size_t getStepIntervalChunks() const noexcept` (`:923`); rule knobs `setKernelSigma(float)` (`:469`, clamped `[0.01, 0.35]`), `getKernelSigma()` (`:476`), `getExchangeRate()` (`:485`), `setPredation(float)` (`:491`), `getPredation()` (`:497`), defaults `kernelSigma_ = 0.03f`, `exchangeRate_ = 0.35f`, `predation_ = 0.55f` (`:2354-2356`); `kMinAgents = 1` (`:153`), `kMinEnergyBudget = 1.0e-3` (`:214`), `kControlChunkSamples = 64` (`:182`), `PrepareConfig::agentCount` (`:304`, clamped `[kMinAgents, kMaxAgents]`). **`getAgentOutput` is not energy:** `publish()` computes `raw = kOutputAnchor * energy_[i] * agentCount_ / energyBudget_ * gate_[i]`, clamps it to `1.0`, and snaps `≤ kWakeSilenceEpsilon` to `0` (`:2303-2325`), where `gate_` ramps to `0` for a dormant or zero-wake agent (`:2303-2305`) — so it is the *heard* share, zero for a dormant agent whatever its energy, and flat at 1 for every agent at ≥ 2 × the mean share. Constants `kMaxAgents = 48` (`:152`), `kNumKinds = 5` (`:168`), `kMaxPairs = 1128` (`:172-173`), `static_assert(kMaxAgents <= 255, "pair index arrays are std::uint8_t")` (`:174`). `enum class Kind : std::uint8_t { Partial = 0, Resonator = 1, Noise = 2, Feedback = 3, Ghost = 4 }` (`:282-288`, *"APPEND ONLY"*). Private pair scratch `std::array<std::uint8_t, kMaxPairs> pairI_{}, pairJ_{}; std::array<double, kMaxPairs> pairFlow_{}; std::size_t pairCount_` (`:2459-2461`), written at `:1461-1464`, where `flow = exchangeRate * w * (ej - ei) * exchangeSign` (`:1460`) and is **identically 0 at `predation == 0.5`** (comment `:1449-1458`). Habitat: 2-D torus, positions confined to `[0, 1)` (`wrap01`, `:1134`), shortest separation by `wrapDelta` (`:1111`). Getter contract: indexed getters are neutral on an out-of-range index (`:843-851`). **Only includer in the tree: `vorago_voice.h`** (grep this session) — Seraphis does not consume it. |
| `VoragoVoice` | `dsp/include/krate/dsp/systems/vorago_voice.h` (L3) | `[[nodiscard]] const EcosystemEngine& ecosystem() const noexcept { return ecosystem_; }` (`:1514`). Prepare defaults: `ecosystemAgents = 32` (`:200`), `ecosystemStepChunks = 8` (`:205`, one step per 512 samples). The voice's only calls on `ecosystem_` that configure it are `prepare` (`:518`, agent count from `cfg.ecosystemAgents`, `:490`), `setSeed` (`:1546`) and `reset` (`:1605`); nothing in `plugins/vorago/src` sets `ecosystemAgents`, `setKernelSigma` or `setPredation` (grep this session), and the only ecosystem parameter is `kEcosystemDepthId = 900` (`plugin_ids.h:173`). **The product therefore always runs 32 agents at kernelSigma 0.03 / predation 0.55** (≈ 34 surviving pairs of 496 at the defaults, `ecosystem_engine.h:1556-1557`); 48 agents, a raised kernel or `predation = 0.5` are component-level states only. |
| `VoragoEngine` | `dsp/include/krate/dsp/systems/vorago_engine.h` (L3) | `static constexpr std::size_t kMaxVoices = 6` (`:225`); `[[nodiscard]] std::size_t getActiveVoiceCount() const noexcept` (`:1252`); `[[nodiscard]] float getVoiceLevel(std::size_t index) const noexcept` (`:1274`); `[[nodiscard]] VoiceState getVoiceState(std::size_t index) const noexcept` (`:1277`); `[[nodiscard]] const VoragoVoice& getVoice(std::size_t index) const noexcept` (`:1280`, clamps an out-of-range index to 0); `[[nodiscard]] std::uint64_t getVoiceAllocationSerial(std::size_t index) const noexcept` (`:1287`, *"Strictly increasing across note events; 0 means 'never allocated'"*). `setPolyphony` → `allocator_.setVoiceCount` stores `VoiceState::Idle` for every slot `>= count` immediately (`voice_allocator.h:334-350`); a slot still sounding becomes an **orphan tail** (`vorago_engine.h:565-576`), Idle to the allocator but audible. A released voice stays `Releasing` until `VoragoVoice::isFinished()` — `quiescentChunks_ >= quiescentChunksToRetire_`, 10 s below −90 dBFS (`vorago_voice.h:938-944`, `:2395`) — by which point its level is below `1e-4`. |
| Vorago processor | `plugins/vorago/src/processor/processor.{h,cpp}` | `tresult PLUGIN_API Processor::process(ProcessData& data)` (`processor.cpp:247`): shape guards and the not-ready silence path return early (`:256-286`); the slice loop ends at `:386-390`; `return kResultOk` at `:394`. `setActive` (`:227-245`). `std::unique_ptr<Krate::DSP::VoragoEngine> engine_` (`processor.h:191`). **No `connect`, `disconnect`, `notify` override and no DataExchange member** (`processor.h:68-78` is the complete override list). |
| Vorago controller | `plugins/vorago/src/controller/controller.{h,cpp}` | `class Controller : public Steinberg::Vst::EditControllerEx1, public Steinberg::Vst::IMidiMapping, public VSTGUI::VST3EditorDelegate` (`controller.h:29-31`) — **the delegate base already exists**; the banner *"No createCustomView / verifyView: stock views only in Phase 11 (FR-055)"* (`controller.h:12-13`) is rewritten by this phase. `createView` returns `new VSTGUI::VST3Editor(this, "editor", "editor.uidesc")` (`controller.cpp:179-186`, the whole file is 207 lines). `presetManager_` built with no state/load providers, *"No state/load providers until the Phase 13 browser exists"* (`controller.cpp:79-82`). No `getState`/`setState` override (controller.h, grep this session) — the controller has no stream of its own. Not an `IDataExchangeReceiver`; no `notify` override. |
| Vorago entry | `plugins/vorago/src/entry.cpp` | Banner *"FR-018: this file MUST NOT include any ui/*.h … Phase 13 adds the view-creator includes here when the real interface lands"* (`:12-15`). |
| Vorago IDs | `plugins/vorago/src/plugin_ids.h` | 108 enumerators, bands at `:84-224`; `kSustainPedalId = 4`, `kChannelPressureId = 5` hidden and never persisted (`:90-91`); `kCurrentStateVersion = 2` (`:23`); `kStateV2Bytes = 428` (`:28`). Registered kinds (`plugins/vorago/tests/unit/param_table_expected.h`): **22 list rows** (1, 2, 310–313, 320–323, 403, 510–515, 1004, 1005, 1115, 1200, 1403), **2 hidden rows** (4, 5), 84 continuous visible rows. |
| Membrum producer | `plugins/membrum/src/processor/processor.cpp` | `connect` builds `DataExchangeHandler::Config` with `blockSize = sizeof(MetersBlock)`, `numBlocks = 4`, `alignment = 32`, `userContextID = kMetersDataExchangeUserContextId`, then `onConnect(other, getHostContext())` (`:1137-1153`); `disconnect` → `onDisconnect` + `reset()` (`:1156-1162`); `onActivate(setup)` / `onDeactivate()` in `setActive` (`:1110-1126`); publish once per `process()`: `auto block = dataExchangeHandler_->getCurrentOrNewBlock(); if (block.blockID != Steinberg::Vst::InvalidDataExchangeBlockID && block.data != nullptr && block.size >= sizeof(MetersBlock)) { … std::memcpy(block.data, &mb, sizeof(MetersBlock)); dataExchangeHandler_->sendCurrentBlock(); }` (`:849-872`). Member `std::unique_ptr<Steinberg::Vst::DataExchangeHandler> dataExchangeHandler_` behind a forward declaration (`processor.h:33-34`, `:154`). |
| Membrum payload | `plugins/membrum/src/processor/meters_block.h` | POD `struct MetersBlock` (`:20-31`), `static_assert(sizeof(MetersBlock) == 44, …)` (`:33-34`), `inline constexpr std::uint32_t kMetersDataExchangeUserContextId = 0x4D425452u; // 'MBTR'` (`:38`). |
| Membrum consumer | `plugins/membrum/src/controller/controller.{h,cpp}` | Base `public Steinberg::Vst::IDataExchangeReceiver` (`controller.h:44`), `DEF_INTERFACE(Steinberg::Vst::IDataExchangeReceiver)` (`:146`), member `Steinberg::Vst::DataExchangeReceiverHandler dataExchangeReceiver_{this}` (`:365`). `queueOpened` sets `dispatchOnBackgroundThread = false` (`controller.cpp:1697-1704`); `onDataExchangeBlocksReceived` memcpys the most recent valid block into a cached POD (`:1712-1726`); `notify` forwards the IMessage fallback through `dataExchangeReceiver_.onMessage(message)` (`:1747-1759`). Raw view pointers zeroed in `willClose()` (`controller.h:191-199`); the view owns a 30 Hz `CVSTGUITimer` cancelled in `removed()` (`plugins/membrum/src/ui/pad_grid_view.h:30-37`). |
| Seraphis UI (template, not reused code) | `plugins/seraphis/src/processor/cloud_frame.h`, `processor.cpp` | `struct CloudFrame` (`cloud_frame.h:24`), `static_assert(sizeof(CloudFrame) == 808, …)` (`:42`), `kCloudFrameUserContextId = 0x53434C44u` (`:46`); `void Processor::publishCloudFrame() noexcept` (`processor.cpp:4098`) with the focus-voice rule (greatest allocation serial among non-idle slots → else retain previous while `getVoiceLevel(previous) > kCloudFrameSilenceLevel` → else slot 0, `:4130-4155`); `kCloudFrameSilenceLevel = 1.0e-4f` (`processor.h:268`). Seraphis plan R-10 (`specs/seraphis-phase11-ui/plan.md:2312`): drawer pages are plain child containers toggled with `setVisible`, never a `UIViewSwitchContainer`, so every page is in the built tree. |
| `ArcKnob` | `plugins/shared/src/ui/arc_knob.h` (`Krate::Plugins`) | `class ArcKnob : public VSTGUI::CKnobBase` (`:49`); ctor `ArcKnob(const VSTGUI::CRect& size, VSTGUI::IControlListener* listener, int32_t tag)` (`:53-55`); `struct ArcKnobCreator : VSTGUI::ViewCreatorAdapter` with `getViewName() → "ArcKnob"` (`:560`), base `UIViewCreator::kCControl` (`:562-564`); `inline ArcKnobCreator gArcKnobCreator;` — *"Include this header from each plugin's entry.cpp to register the view type"* (`:714-716`). |
| `PresetBrowserView` / `PresetManager` | `plugins/shared/src/ui/preset_browser_view.h`, `plugins/shared/src/preset/preset_manager.h` | `class PresetBrowserView : public VSTGUI::CViewContainer, …` (`:53`), ctor at `:58`, `open()` / `open(const std::string&)` / `openWithSaveDialog(const std::string&)` (`:64-66`). `using StateProvider = std::function<Steinberg::IBStream*()>` (`preset_manager.h:41`), `using LoadProvider = std::function<bool(Steinberg::IBStream*, const PresetInfo&)>` (`:47`), `setStateProvider` (`:126`), `setLoadProvider` (`:129`). Used by this phase (Q1, FR-070–FR-073). |
| Reachability helper | `tests/test_helpers/uidesc_reachability.h` | `inline std::map<std::string, int> extractControlTagMap(const std::string& xml)` (`:43`); `inline std::vector<int> unreachableParams(const std::string& uidescXml, const std::vector<int>& registeredIds, …allowlist)` (`:88-89`); banner: params without a control-tag *"will be reported as 'unreachable' … pass those params in via the allowlist"* (`:13-28`). |
| Editor-lifecycle harness | `tests/test_helpers/editor_lifecycle_harness.h` | `inline void exerciseEditorLifecycle(Steinberg::Vst::EditController& controller, const char* templateName, const std::string& uidescAbsolutePath, int cycles = 3)` (`:102-105`); builds the full view tree, fires `verifyView`/`didOpen`, then `willClose` (`:9-16`). **It never attaches the frame:** it passes a null parent (`:109-122`), `CFrame::open` returns at `if (!systemWin || isAttached ()) return false;` (`extern/vst3sdk/vstgui4/vstgui/lib/cframe.cpp:208-209`) before `attached (this)` (`:220`), and `CFrame::attached` is the only path that calls `pV->attached (this)` on children (`:239-240`) — so no view's `attached()`/`removed()` runs under the harness. Vorago's enrollment: `TEST_CASE("Vorago_EditorLifecycle", "[vorago][controller][ui][lifecycle]")` (`plugins/vorago/tests/unit/controller/editor_lifecycle_test.cpp:89`), sections `HarnessCycles` (`:143-148`) and `EditorBindsFourteenControls` (`:154-161`) — **the latter is superseded by this phase** (FR-041). |
| Allocation / fingerprint helpers | `tests/test_helpers/allocation_detector.h`, `render_fingerprint.h` | `class AllocationDetector` (`:48`), `class AllocationScope` (`:111`); `struct RenderFingerprint` (`render_fingerprint.h:63`), `kSampleTolerance = 5.0e-4f` (`:58`). |
| Carried CPU gate | `plugins/vorago/tests/integration/processor_cpu_test.cpp` | `TEST_CASE("Vorago_ProcessorCpu", "[vorago][.perf][performance]")` (`:164`); `kWrapperOverheadCeiling = 1.05` (`:75`); arm P = bare `process()` on an unconnected processor (`:8-11`). Phase 12's name for it: SC-016 (`specs/vorago-phase12-parameters/spec.md:902`). |

---

## New components

### ODR sweep — run this session

Command: `grep -rnE "(class|struct) <Name>\b" dsp/ plugins/ tools/ tests/`; free functions / constants swept as
`grep -rn "\b<name>\b" dsp/ plugins/ tools/ tests/`.

| Candidate name | Result | Disposition |
|---|---|---|
| `EcosystemView` | **0 matches** | **CLAIMED** — `Vorago::UI::EcosystemView`, `plugins/vorago/src/ui/ecosystem_view.{h,cpp}` |
| `EcosystemFrame` | **0 matches** | **CLAIMED** — `Vorago::EcosystemFrame`, `plugins/vorago/src/processor/ecosystem_frame.h` (POD beside the processor, as `meters_block.h` / `cloud_frame.h` sit beside theirs) |
| `kEcosystemFrameUserContextId` | **0 matches** | **CLAIMED** — same header, value `0x5645434Fu` ('VECO'); distinct from the tree's two named IDs `0x4D425452` (Membrum) and `0x53434C44` (Seraphis) and from the `0` Disrumpo/Innexus use |
| `VoragoPanelSubController` | **0 matches** | **CLAIMED** — `Vorago::UI::VoragoPanelSubController`, `plugins/vorago/src/ui/panel_sub_controller.{h,cpp}`; a `VSTGUI::DelegationController`, not a `CView` |
| `getPairAgentA`, `getPairAgentB`, `getPairFlow` | **0 matches each** | **CLAIMED** — `EcosystemEngine` member functions (C-3) |
| `publishEcosystemFrame` | **0 matches** | **CLAIMED** — `Vorago::Processor` private member function |
| `selectStrongestLinks`, `ecosystemEnergyGlow`, `sanitizeFrameFloat`, `ecosystem_frame_builder` | **0 matches each** | **CLAIMED** — `Vorago::` free functions in `plugins/vorago/src/processor/ecosystem_frame_builder.h` (C-2 clauses 3, 5; FR-027) |
| `agentDormant` | **0 matches** | **CLAIMED** — `EcosystemFrame` field |
| `agentStyle`, `torusLinkSegments`, `isDrawableLink`, `emptyHabitatGridLines`, `kAgentOutlineMinAlpha`, `hasTimerForTest` | **0 matches each** | **CLAIMED** — `EcosystemView` static members / seam (C-6) |
| `needsRedraw` | **4 matches**, all a local `bool` in `plugins/disrumpo/src/controller/views/spectrum_display.cpp:164-173` | **CLAIMED** as the static member `EcosystemView::needsRedraw`; class scope, no clash with a function-local variable |
| `ecosystemViewForTest`, `ecosystemFocusRuleForTest` | **0 matches each** | **CLAIMED** — controller / processor test seams (FR-042, C-2 clause 7) |
| `linkAlpha`, `habitatBrightness`, `activeVoicesText` | **0 matches each** (re-swept this session, Q5/Q6) | **CLAIMED** — `EcosystemView` static members (C-6 clauses 3, 7, 8) |
| `linkFlowScale` | **0 matches** (re-swept this session, Q5) | **CLAIMED** — `EcosystemFrame` field |
| `ecosystemFrameProcessCallCountForTest` | **0 matches** (re-swept this session, Q4) | **CLAIMED** — `Vorago::Processor` test seam (C-2 clause 7) |
| `EcosystemLink`, `HabitatView`, `AgentView`, `EcosystemSnapshot`, `AgentSnapshot`, `EcosystemFrameBlock`, `HabitatFrame`, `AgentLink`, `VoragoEditSubController`, `VoragoSubController`, `PanelSubController`, `EcosystemViewCreator`, `ConceptKnob`, `MacroKnob`, `getPairInteraction` | **0 matches each** | swept, **not claimed** — recorded so the negative is visibly tested |
| `MacroRingKnob` | **2 matches** — `plugins/seraphis/src/ui/macro_ring_knob.h:55` (class), `plugins/seraphis/src/controller/controller.h:60` (fwd decl) | **NOT CLAIMED** — Seraphis-local; Vorago uses the shared `ArcKnob` directly |
| `PresetBrowserButton` (Q1) | **1 match** — `class PresetBrowserButton : public OutlineButton` in `Ruinae::` (`plugins/ruinae/src/controller/controller_presets.cpp:126`) | **NOT CLAIMED (R-1)** — Vorago creates the shared `Krate::Plugins::OutlineBrowserButton` (`plugins/shared/src/ui/outline_button.h:79`) directly, as Seraphis does; only the `custom-view-name` string `"PresetBrowserButton"` is used |

Near-name hazards recorded: `Krate::DSP::EcosystemEngine` (`ecosystem_engine.h:143`) and
`Vorago::EcosystemParams` (`plugins/vorago/src/parameters/ecosystem_params.h:32`) share the `Ecosystem` stem
with the claimed names but no identifier; the leaf's `Vorago::` vs `Seraphis::` rule (no TU `using namespace`
both) applies to every new TU.

### New and changed components

| Component | Layer / file | Nature |
|---|---|---|
| `EcosystemEngine::getPairAgentA/B`, `getPairFlow` | L3 `dsp/include/krate/dsp/systems/ecosystem_engine.h` | Three additive `const noexcept` read accessors over the existing private pair scratch (C-3). No new member, no new include (the header stays Layer 0 + stdlib, `:70-80`). |
| `struct EcosystemFrame`, `kEcosystemFrameUserContextId`, `kMaxFrameLinks` | plugin, `src/processor/ecosystem_frame.h` (new) | POD payload (now 1072 bytes, `linkFlowScale` field, Q5) + `static_assert(sizeof)` (C-2). |
| `ecosystemEnergyGlow`, `selectStrongestLinks`, `sanitizeFrameFloat` | plugin, `src/processor/ecosystem_frame_builder.h` (new; processor side only, includes `ecosystem_frame.h` + `ecosystem_engine.h`) | Pure frame-fill functions, testable without a `Processor` (FR-020a). |
| `class UI::EcosystemView` | plugin, `src/ui/ecosystem_view.{h,cpp}` (new) | `VSTGUI::CView` subclass; the one custom view; 30 Hz timer; link fade memory; static `linkAlpha`, `habitatBrightness`, `activeVoicesText` (C-6, Q5, Q6). |
| `class UI::VoragoPanelSubController` | plugin, `src/ui/panel_sub_controller.{h,cpp}` (new) | `VSTGUI::DelegationController`; owns the page selector and toggles the seven page containers (C-5). |
| Processor: `DataExchangeHandler` member, `connect`/`disconnect` overrides, handler activate/deactivate in `setActive`, `publishEcosystemFrame()` (cadence-gated, Q4), focus-voice state, link-selection scratch, test seams | plugin, `src/processor/processor.{h,cpp}` (extended) | C-2. |
| Controller: `IDataExchangeReceiver` base + three entry points, `DataExchangeReceiverHandler` member, `notify` pass-through, `createCustomView`, `createSubController`, `willClose`, cached frame, Gravity-macro label/tooltip observer on `kResonanceAnchorModeId` (Q2) | plugin, `src/controller/controller.{h,cpp}` (extended) | C-2, C-5, C-6, FR-080/081. |
| Controller: `presetManager_` state/load providers, `presetBrowserView_`, header `OutlineBrowserButton` (Q1, R-1, R-2) | plugin, `src/controller/controller.{h,cpp}` (extended; no new file) | FR-070–FR-073. |
| `editor.uidesc` | plugin, `resources/` (replaced) | C-1, C-4; header preset button (Q1). |
| `entry.cpp` | plugin (extended) | `#include "ui/arc_knob.h"` (registers `ArcKnobCreator`, `arc_knob.h:714-716`); FR-018's no-UI-include banner rewritten. |

---

## Conventions decided in this spec

### C-1. The layout — fixed window, concept band over engine band

The window is **fixed at 1100 × 760 px** (no resizing, per *Non-goals*). Rects are `(left, top, right, bottom)`.

```
+--------------------------------------------------------------------------------+
| VORAGO  [Preset]               Gain  Voices  Seed  Saturation                   |  header, H = 36
+--------------------------------------------------------------------------------+
|  (Darkness) (Age)     +----------------------------+     (Entropy) (Pressure)  |
|  (Density)  (Movement)|      ECOSYSTEM VIEW        |     (Weight)  (Fog)       |  concept band
|  (Gravity)  ...       |  agents = glowing points   |     (Life)    (Depth)     |  y 36 .. 436
|                       |  links = fading lines      |               (Mass)      |
|                       +----------------------------+                           |
+--------------------------------------------------------------------------------+
| [Cloud][Noise][Resonance][Body][Sub/Smear][Space][Life]                        |  page strip, H = 28
|  one page of stock controls, the other six hidden                              |  page area
+--------------------------------------------------------------------------------+
```

| Element | Rect | Note |
|---|---|---|
| Window | `(0, 0, 1100, 760)` | fixed |
| Header | `(0, 0, 1100, 36)` | global controls (IDs 0, 1, 2, 3) + preset browser button (Q1, FR-070) |
| Concept band | `(0, 36, 1100, 436)` | twelve macros + the ecosystem view |
| Left macro block | `(0, 36, 350, 436)` | macros 100–105 (Darkness…Entropy), 2 columns × 3 rows |
| Ecosystem view | `(350, 36, 750, 436)` | 400 × 400 — square, because the habitat is the unit torus |
| Right macro block | `(750, 36, 1100, 436)` | macros 106–111 (Pressure…Mass), 2 columns × 3 rows |
| Page strip | `(0, 436, 1100, 464)` | seven-segment selector |
| Page area | `(0, 464, 1100, 760)` | seven sibling page containers, exactly one visible |

"Macros dominate" is made testable: each macro knob is **≥ 80 × 80 px**, every engine-page knob is
**≤ 48 × 48 px**, and the concept band is the larger of the two bands (400 px vs 324 px). Macro order is the
`VoragoMacro` enumerator order (`vorago_macro_matrix.h:106-…`, `id - 100 == static_cast<int>(VoragoMacro::X)`).

### C-2. The ecosystem-frame data path — one payload, one direction, published on habitat change (Q4)

```cpp
namespace Vorago {
inline constexpr std::size_t kMaxFrameAgents = 48;   // == EcosystemEngine::kMaxAgents (static_assert)
inline constexpr std::size_t kMaxFrameLinks  = 64;   // strongest-|flow| pairs carried per frame
struct EcosystemFrame {                    // POD, memcpy'd, native endianness (same host)
    std::uint32_t sequence        = 0;     // +1 per publish; wrap is benign
    std::uint8_t  activeVoices    = 0;     // VoragoEngine::getActiveVoiceCount()      (:1252)
    std::uint8_t  focusVoice      = 0;     // clause 4
    std::uint8_t  agentCount      = 0;     // 0 .. 48; 0 = "no habitat to draw"
    std::uint8_t  linkCount       = 0;     // 0 .. kMaxFrameLinks
    float         voiceLevel      = 0.0f;  // VoragoEngine::getVoiceLevel(focus)       (:1274)
    float         agentX[48]      = {};    // [0, 1) torus, from getAgentPositionX     (:896)
    float         agentY[48]      = {};    // [0, 1) torus, from getAgentPositionY     (:899)
    float         agentGlow[48]   = {};    // [0, 1), ecosystemEnergyGlow(getAgentEnergy) (:890), clause 3
    std::uint8_t  agentKind[48]   = {};    // EcosystemEngine::Kind, 0 .. 4            (:282)
    std::uint8_t  agentDormant[48] = {};   // 1 iff isAgentDormant(i)                  (:829)
    std::uint8_t  linkA[64]       = {};    // agent index, < agentCount
    std::uint8_t  linkB[64]       = {};    // agent index, < agentCount, != linkA
    float         linkStrength[64] = {};   // |flow| of the recorded pair, >= 0, finite
    float         linkFlowScale   = 0.0f;  // Q5: getEnergyBudget()/agentCount (mean per-agent energy, the
                                            // absolute reference the view divides linkStrength by); 0 iff
                                            // agentCount == 0
};
// 8 + 4 + 3*192 + 48 + 48 + 64 + 64 + 256 + 4 = 1072 bytes, alignment 4, no padding
// (the uint8 run ends at offset 812, a multiple of 4; linkFlowScale sits at offset 1068).
static_assert(sizeof(EcosystemFrame) == 1072);
inline constexpr std::uint32_t kEcosystemFrameUserContextId = 0x5645434Fu;  // 'VECO'
}
```

1. **Handler config** copies Membrum's (`processor.cpp:1141-1147`): `blockSize = sizeof(EcosystemFrame)`,
   `numBlocks = 4`, `alignment = 32`, `userContextID = kEcosystemFrameUserContextId`. Created in `connect()`,
   destroyed in `disconnect()`, `onActivate`/`onDeactivate` from `setActive`.
2. **Cadence: evaluated once per `process()` call that reached the slice loop, after the loop, never per
   slice — but only published when the habitat changed (Q4).** The slice loop subdivides at every event,
   pedal point and the 2048 cap (`processor.cpp:358-390`), so a per-slice publish would drain the 4-block
   queue inside one call; early-return paths (`:256-286`) evaluate and publish nothing. After the loop, the
   producer checks a trigger and fills + publishes exactly when at least one of these holds, otherwise it
   does nothing (no fill, no queue write):
   - (i) `getVoice(focus).ecosystem().getControlStepCount()` differs from the value recorded at the previous
     fill (the habitat actually advanced);
   - (ii) `focusVoice` or `agentCount` differs from the previous fill (a focus hand-off or voice-count change
     is drawable immediately, not held for the next step boundary);
   - (iii) this is the first evaluation since `connect()` (nothing has been published yet).

   This makes at most one publish per `process()` call, same as before, but the publish **rate now tracks the
   ≈ 94 Hz simulation-step rate at 512/48 kHz (the natural cadence of the data), not the host's callback
   rate**: small buffers (e.g. 32 samples) evaluate the trigger every call but only fill on the call that
   crosses a step boundary, and a single large buffer spanning several steps still yields at most one publish
   for that call, carrying the step reached at the end of the slice loop (steps between evaluations are not
   separately visible — same collapsing the old per-process cadence already accepted). Cost is therefore
   independent of host buffer size.
3. **Field sources — brightness is energy, not heard output (decision).** Roadmap line 566 says *"energy as
   brightness"*. `getAgentOutput` does not show that: it is multiplied by the wake/dormancy gate (a dormant
   agent with a large store reads 0) and it clamps at twice the mean share (`ecosystem_engine.h:2303-2325`).
   So brightness comes from `getAgentEnergy(i)` (`:890`) through one pure function in
   `src/processor/ecosystem_frame_builder.h`:
   ```cpp
   // share = energy * agentCount / energyBudget  (1.0 == the mean share; budget >= kMinEnergyBudget, :214)
   // glow  = share / (1 + share)                 (mean -> 0.5, like kOutputAnchor :231; 2x -> 0.667,
   //                                              10x -> 0.909; strictly increasing, never saturates)
   [[nodiscard]] float ecosystemEnergyGlow(double energy, std::size_t agentCount, double energyBudget) noexcept;
   ```
   It is called as `ecosystemEnergyGlow(getAgentEnergy(i), getAgentCount(), getEnergyBudget())`. A negative
   share returns `0.0f`, and the result passes through `sanitizeFrameFloat` (FR-027). Dormancy is carried as a
   separate field, `agentDormant[i] = isAgentDormant(i) ? 1 : 0` (`:829`), and C-6 clause 2 draws it as an
   outline, so a dormant agent that holds a lot of energy shows up bright-outlined and not dark. The heard
   output `getAgentOutput` is **not** carried. Positions narrow `double → float` with an explicit
   `static_cast`. Entries at `i >= agentCount` and links at `l >= linkCount` are **zero-filled on every
   publish, never stale**.
4. **Focus voice** — the Seraphis rule verbatim (`plugins/seraphis/src/processor/processor.cpp:4126-4158`),
   over `VoragoEngine::kMaxVoices` (not the polyphony): (a) among slots with `getVoiceState(v) != Idle`, the one
   with the greatest `getVoiceAllocationSerial(v)`; (b) else the previous focus while
   `getVoiceLevel(previous) > kEcosystemFrameSilenceLevel` (`1.0e-4f`, Seraphis's value); (c) else slot 0 **with
   `agentCount = 0` and `linkCount = 0`** — no voice sounds, so there is no habitat to draw. **Final (Q3):**
   the view shows the focus voice only — no multi-voice overlay, no UI voice selector, no controller →
   processor channel is added in this phase (*Non-goals*).
5. **Links = the strongest `kMaxFrameLinks` pairs of the most recent simulation step**, by `|getPairFlow(p)|`,
   excluding pairs whose flow is exactly `0.0`. Selection is a **pure free function** in
   `src/processor/ecosystem_frame_builder.h`, testable against a directly prepared `EcosystemEngine` without a
   `Processor`:
   ```cpp
   void selectStrongestLinks(const Krate::DSP::EcosystemEngine& eco,
                             std::span<std::uint16_t, Krate::DSP::EcosystemEngine::kMaxPairs> scratch,
                             EcosystemFrame& frame) noexcept;   // writes linkCount, linkA/B, linkStrength
   ```
   The processor passes a fixed member scratch (`std::array<std::uint16_t, EcosystemEngine::kMaxPairs>`);
   the function uses `std::nth_element` (in place, no allocation), so the cost is O(pairCount) and bounded by
   `kMaxPairs = 1128`. `1128` is a **component-level** bound (48 agents, whole torus in cutoff). The product
   runs 32 agents at the default kernel (see *Existing components*, `VoragoVoice` row), so it sees at most 496
   pairs and about 34 in practice. Order within the carried set is unspecified;
   the set is what SC-007 asserts. At `predation == 0.5` every recorded flow is 0 (`ecosystem_engine.h:1449-1460`),
   so `linkCount == 0` and the view honestly shows "no exchange". This is a component-level state that no
   Vorago parameter reaches. **`linkFlowScale` (Q5)** is filled in the same pass as `getEnergyBudget() /
   static_cast<double>(agentCount)` (0 when `agentCount == 0`, matching rule (c)) and passes through
   `sanitizeFrameFloat` (FR-027) like every other frame float.
6. **Enable rule and test seam.** The producer body runs when `dataExchangeHandler_ != nullptr` **or** a test
   seam `setEcosystemFrameForcedForTest(bool)` is on. The fill writes a member `EcosystemFrame pendingFrame_`
   (readable through `lastPublishedFrameForTest()`); only the handoff to the queue requires the handler. There
   is **no editor-open gate**: that would need a controller → processor message, which this phase does not
   add (roadmap *"no new queues"*); the producer's cost is paid while connected and is bounded by SC-012. The
   same seam, when on, **also forces clause 2's trigger true on every evaluation** — otherwise, once Q4's
   cadence change lands, a forced-but-idle habitat (no step advance) would take the cheap no-fill path and
   SC-012's CPU gate would measure that cheap path instead of the worst case, passing vacuously (the same
   trap Overview fact 4 already names for the handler-existence half of this seam).
7. **Three counters.** `ecosystemFrameProcessCallCountForTest()` counts every `process()` call that reaches
   the slice loop (the per-call trigger evaluation, regardless of outcome). `ecosystemFramePublishAttemptCountForTest()`
   counts every call where clause 2's trigger held and a fill + queue handoff was attempted — no longer one
   per `process()` call (Q4). `ecosystemFrameSkippedBlockCountForTest()` counts attempts (from the publish-attempt
   counter) where `getCurrentOrNewBlock()` returned `InvalidDataExchangeBlockID`; recorded, never gated — a
   published frame can still outrun the 30 Hz redraw, so skipped blocks are expected. A fourth seam,
   `char ecosystemFocusRuleForTest() const noexcept`, returns `'a'`, `'b'` or `'c'`: whichever part of the
   clause 4 rule chose the last frame's focus (SC-009).
8. **Direction is one-way.** Processor → controller only.

### C-3. The additive `EcosystemEngine` link accessors

```cpp
/// Pair p of the most recent simulation step's recorded interaction table (stage 2),
/// p < getPairInteractionCount(). Neutral on an out-of-range index: 0 / 0 / 0.0.
[[nodiscard]] std::size_t getPairAgentA(std::size_t p) const noexcept;  // pairI_[p]
[[nodiscard]] std::size_t getPairAgentB(std::size_t p) const noexcept;  // pairJ_[p], > A
[[nodiscard]] double      getPairFlow(std::size_t p)   const noexcept;  // pairFlow_[p], signed, pre-scale
```

They join the class's *indexed-getter* contract class (i) (`ecosystem_engine.h:843-851`): neutral on an
out-of-range index only, read nothing out of range. `getPairFlow` reports the **recorded** (pre-stage-3-scale)
flow — the exchange the rule wanted — and says so in its doxygen. No member, no include, no existing line of
`simulationStep()` changes; `kConfigKnobCount` (`:270`) is unchanged because these are not knobs.

### C-4. Binding — 106 IDs, each exactly once; allowlist exactly {4, 5}

| Surface | IDs | Count |
|---|---|---|
| Header | 0 (Master Gain), 1 (Polyphony), 2 (Seed), 3 (Output Saturation) | 4 |
| Concept band | 100–111 (twelve `ArcKnob`s) | 12 |
| Page 1 **Cloud** | 200–206, 1300–1301 (Bloom) | 9 |
| Page 2 **Noise** | 300–302, 310–313, 320–323, 330–333, 340–343, 350–353 | 23 |
| Page 3 **Resonance** | 400–403, 500–501, 510–515 (Ecology) | 12 |
| Page 4 **Body** | 1000–1005, 1200–1206 (Envelope) | 13 |
| Page 5 **Sub / Smear** | 600–601, 610–612, 700–702 | 8 |
| Page 6 **Space** | 1100–1115 | 16 |
| Page 7 **Life** | 800 (Events), 900 (Ecosystem), 1400–1403 (Ghost), 1500–1502 (Life) | 9 |
| | | **106** |

`kSustainPedalId` (4) and `kChannelPressureId` (5) are `kIsHidden` performance controllers
(`plugin_ids.h:90-91`) reached through `IMidiMapping`; they are the complete allowlist. **Per-view class
rule:** continuous IDs → `ArcKnob` (or `CSlider` in the header); list IDs → `COptionMenu`, except the two-state
lists 1115 (Space Freeze) and 1403 (Ghost Event Triggers), which may be `CCheckBox` or the shared
`ToggleButton`. No binding changes any `Parameter` object; no second registration; no type swap. The existing
`<control-tags>` block (`editor.uidesc:19-34`) is extended, never renumbered — a tag name maps to its
`ParamID` value exactly.

### C-5. The page switch — all seven pages present in the built tree

Following Seraphis plan R-10: the seven pages are **sibling `CViewContainer`s** in the XML, exactly one
visible; never a `UIViewSwitchContainer`, which instantiates only the active template and would leave six
pages' controls absent from the built tree. The page strip is a stock `CSegmentButton` with **no parameter
tag**; `VoragoPanelSubController` (returned from `createSubController`) receives its `valueChanged` and calls
`setVisible` on the pages. The active page is controller session state (default page 0), survives editor
close/reopen within a session, and is never persisted.

### C-6. The ecosystem view — drawing contract

1. **Mapping.** `px = rect.left + x * rect.width()`, `py = rect.top + y * rect.height()` over the square view
   (the torus maps to the whole square).
2. **Agents** — filled circles; colour by `Kind` from a fixed 5-entry palette (named uidesc colours
   `eco-partial`, `eco-resonator`, `eco-noise`, `eco-feedback`, `eco-ghost`); **alpha and radius both
   monotonic non-decreasing in `agentGlow`** (the "glow", which is energy, C-2 clause 3). An agent with
   `agentDormant[i] == 1` is drawn as an **unfilled outline** in its kind colour whose alpha follows the same
   glow mapping, so its energy stays visible while its silence is marked. `agentGlow == 0` draws a faint outline
   (alpha floor `kAgentOutlineMinAlpha`), so every agent stays locatable. The per-agent draw style is decided by
   one static function, `EcosystemView::agentStyle(glow, dormant) → {filled, alpha, radius}`.
3. **Links** — drawn the *short way round the torus* (the `wrapDelta` rule, `ecosystem_engine.h:1111`). With
   `d = (wrapDelta(xB − xA), wrapDelta(yB − yA))`, the unwrapped segment `p → p + d` (`p` = A's position) is
   drawn in each tile translation `t ∈ {0, −sx} × {0, −sy}`, where `sx = sign(pA.x + d.x)` if `pA.x + d.x ∉ [0, 1)`
   (else only 0), and likewise for y. That gives 1 piece (no wrap), 2 (one edge) or up to 4 (corner wrap), each
   clipped to the view, and none dropped. The pieces come from one static function,
   `EcosystemView::torusLinkSegments(ax, ay, bx, by, rect) → up to 4 clipped segments`. **Width/alpha scale
   absolutely, not per-frame-relative (Q5):** `alpha = EcosystemView::linkAlpha(linkStrength[l],
   frame.linkFlowScale)`, a static, `CFrame`-free function implementing the fixed soft clip
   `s / (s + max(scale, 0))` (the same `share / (1 + share)` shape as `ecosystemEnergyGlow`, C-2 clause 3) —
   `0` at `s <= 0`, `0.5` at `s == scale` (a flow at the mean per-agent energy), approaching `1` without
   reaching it. A quiet economy therefore reads dim and a violent one reads bright, frame to frame, instead of
   every frame's strongest link always drawing full width. Width follows the same mapping as alpha.
4. **Fading.** The view keeps a `48 × 48` alpha table (upper triangle used) in session memory. Each redraw:
   a link present in the newest frame sets its alpha to `max(current, linkAlpha(linkStrength[l],
   linkFlowScale))`; every other entry decays by `exp(-Δt / kLinkFadeSeconds)` with `kLinkFadeSeconds = 0.6f`;
   entries below `1/255` are zeroed. Δt is the timer's measured interval, clamped to `[0, 0.25]` s so a
   stalled UI does not snap everything dark. A frame with a different `focusVoice` or `agentCount` than the
   previous one clears the table (indices no longer name the same agents).
5. **Cadence.** A 30 Hz `CVSTGUITimer` owned by the view, created in `attached()`, cancelled in `removed()`
   (Membrum `pad_grid_view.h:30-37`); each tick reads the controller's cached frame and invalidates only if
   `static bool EcosystemView::needsRedraw(std::uint32_t prevSeq, std::uint32_t seq, float maxFadeAlpha)`
   returns true (`seq != prevSeq || maxFadeAlpha > 0`). Test seam `bool hasTimerForTest() const noexcept`
   reports whether the timer currently exists. The timer callback reads controller-owned memory through a
   pointer the view receives at construction, so the timer must not outlive `removed()`.
6. **Empty habitat.** `agentCount == 0` draws the background and a dim 4 × 4 grid only.
7. **Habitat brightness (Q6, R-3).** Every agent's and link's drawn alpha (from `agentStyle` / `linkAlpha`) is
   multiplied by `static float EcosystemView::habitatBrightness(float voiceLevel) noexcept`, which returns the
   dB-normalized value `clamp((20 · log10(voiceLevel) + 60) / 60, 0, 1)` — `0` at or below `0` (no log of
   zero), `1` at or above `1`, so `0.01 → 1/3`, `0.1 → 2/3`, `0.5 → 0.8997` — because `voiceLevel` is a
   raw linear peak follower and a held drone typically peaks at 0.1–0.25 (a linear map would draw it at
   10–25 % alpha). A releasing
   focus voice's `voiceLevel` decays toward silence, so its whole habitat visibly dims and fades **before**
   rule (c) of C-2 clause 4 empties the view (`agentCount = 0`) once the voice retires. `habitatBrightness` is
   a static, `CFrame`-free function (FR-050).
8. **Active-voice readout (Q6).** `activeVoices` is drawn as a small numeric readout in a fixed corner of the
   view (top-left), display-only and non-interactive; its text comes from
   `static std::string EcosystemView::activeVoicesText(std::uint8_t activeVoices) noexcept`, trivially
   `std::to_string(activeVoices)`. It does not affect habitat drawing in any other way, and carries no
   parameter.

---

## Functional Requirements

### A. Layout and `editor.uidesc`

- **FR-001** `resources/editor.uidesc` MUST be replaced wholesale; the Phase 11 placeholder banner
  (`editor.uidesc:3-8`) and its 420 × 520 template MUST be gone. (Roadmap line 564 *"VSTGUI only"*; Phase 11
  spec line 66.)
- **FR-002** The `editor` template MUST be fixed at 1100 × 760 (`size`, `minSize`, `maxSize` equal) and
  contain the header, concept band, page strip and page area at the C-1 rects. Each of the seven C-1 regions
  below the window MUST exist in the XML as its own view, carrying a fixed `uidesc-label`: `header`,
  `concept-band`, `macros-left`, `ecosystem` (the `EcosystemView` itself), `macros-right`, `page-strip` and
  `page-area`. Every region except `ecosystem` and `page-strip` is a `CViewContainer`, and every rect is given
  in window coordinates as C-1 lists it (nested origins are resolved by the test).
- **FR-003** The twelve macros MUST be `ArcKnob` views bound to 100–111, each ≥ 80 × 80 px, in `VoragoMacro`
  order, six in the left block and six in the right (C-1). (Roadmap line 564 *"the macro concepts dominate"*.)
- **FR-004** Every engine-page knob MUST be ≤ 48 × 48 px and lie inside the page area. (Roadmap line 565
  *"engine panels beneath"*.)
- **FR-005** Every view class used MUST be a stock VSTGUI class, a `plugins/shared/src/ui/` class, or
  `EcosystemView`. No platform API, no bitmap that exists on one OS only.
- **FR-006** Every bound control MUST carry a `tooltip`, and every macro and page control MUST sit beside an
  untagged, mouse-disabled `CTextLabel` naming it.

### B. Parameter binding

- **FR-010** Each of the 106 non-hidden registered IDs MUST be bound by exactly one view, per C-4's table.
- **FR-011** IDs 4 and 5 MUST NOT be bound; `{4, 5}` is the complete reachability allowlist.
- **FR-012** Every `<control-tag>` MUST map to a registered `ParamID` and every bound tag MUST be declared;
  existing tag names (`editor.uidesc:20-33`) keep their values.
- **FR-013** Bound view classes MUST obey C-4's per-view class rule.
- **FR-014** No registered parameter's type, range, default, flags, step count, title or ID MAY change;
  the count stays 108. (Roadmap line 567.)
- **FR-015** No UI state (active page, fade table, view mode) MAY become a parameter or enter the state
  stream; `kCurrentStateVersion` stays 2 and the stream stays 428 bytes.

### C. The ecosystem-frame producer (processor, audio thread)

- **FR-020** `src/processor/ecosystem_frame.h` MUST define `EcosystemFrame`, `kMaxFrameAgents`,
  `kMaxFrameLinks` and `kEcosystemFrameUserContextId` exactly as C-2, with
  `static_assert(sizeof(EcosystemFrame) == 1072)` and `static_assert(std::is_trivially_copyable_v<EcosystemFrame>)`.
  `EcosystemFrame` MUST carry `linkFlowScale` (Q5). It MUST include no processor, controller or `dsp/` header
  (it is shared by both, like `plugin_ids.h`).
- **FR-020a** `src/processor/ecosystem_frame_builder.h` (processor side only) MUST define the three pure free
  functions `ecosystemEnergyGlow` (C-2 clause 3), `selectStrongestLinks` (C-2 clause 5) and
  `sanitizeFrameFloat` (FR-027), all in `namespace Vorago`, all `noexcept`, all allocation-free, and MUST hold
  `static_assert(kMaxFrameAgents == Krate::DSP::EcosystemEngine::kMaxAgents)`. `publishEcosystemFrame()` MUST
  fill the frame — including `linkFlowScale` from `getEnergyBudget() / agentCount` (Q5, C-2 clause 5) — only
  through them, plus direct `const` getter reads.
- **FR-021** The processor MUST override `connect`/`disconnect` and create/destroy a
  `DataExchangeHandler` with C-2 clause 1's config; `setActive(true)` MUST call `onActivate`, `setActive(false)`
  MUST call `onDeactivate`, each only when the handler exists.
- **FR-022** `publishEcosystemFrame()` MUST be evaluated once per `process()` call that reached the slice
  loop, after it, and on no early-return path, but MUST fill and publish the frame only when C-2 clause 2's
  trigger holds (the focus voice's simulation step advanced, focus/agentCount changed, or this is the first
  evaluation since `connect()`) — at most one publish per `process()` call, at a rate that tracks the
  simulation step rate rather than the host callback rate (C-2 clause 2, Q4).
- **FR-023** The frame MUST be filled from the focus voice (C-2 clause 4) via `getVoice(focus).ecosystem()`,
  with the field sources of C-2 clause 3, zero-filling unused entries. Brightness (`agentGlow`) MUST be derived
  from `getAgentEnergy(i)` through `ecosystemEnergyGlow`, **not** from `getAgentOutput(i)`, and dormancy MUST be
  carried separately in `agentDormant` from `isAgentDormant(i)`. (Roadmap line 566 *"energy as brightness"*.)
  The view MUST show only this one focus voice's habitat — no overlay of other voices, no UI voice selector,
  no controller → processor channel (Q3, *Non-goals*).
- **FR-024** Links MUST be the strongest `min(kMaxFrameLinks, nonzero pairs)` recorded pairs by `|flow|`, via
  C-3's accessors and a fixed member scratch; `linkStrength = |flow|` (C-2 clause 5). The frame MUST also
  carry `linkFlowScale = getEnergyBudget() / agentCount` (`0` when `agentCount == 0`), the absolute reference
  the view scales `linkStrength` against (Q5).
- **FR-025** The producer MUST be allocation-free, lock-free, exception-free and I/O-free, and MUST read the
  engine only through `const` accessors.
- **FR-026** Enable rule, test seams and the three counters MUST be as C-2 clauses 6–7. The
  `setEcosystemFrameForcedForTest(true)` seam MUST force both the handler-existence check and C-2 clause 2's
  cadence trigger, so a forced call always performs a full fill (Q4; otherwise SC-012's CPU gate would measure
  the cheap no-fill path and pass vacuously). A second seam `setEcosystemFrameEnabledForTest(bool)` (R-4)
  MUST open the gate only (as if a handler existed) and leave clause 2's natural trigger in place, so that
  SC-008's cadence is observable headlessly; both seams default off and neither is reachable from a host.
- **FR-027** Every float written to the frame MUST be finite. Every float passes through
  `[[nodiscard]] float sanitizeFrameFloat(double v) noexcept`, which checks for a non-finite value with a bit
  pattern, not `std::isnan`, and returns `0.0f` for it. It also returns `0.0f` when a finite double overflows
  on narrowing to float.

### D. `EcosystemEngine` accessors (dsp/, Layer 3)

- **FR-030** `EcosystemEngine` MUST gain `getPairAgentA`, `getPairAgentB`, `getPairFlow` as C-3, and nothing
  else. The header's include block (`ecosystem_engine.h:70-80`) MUST be unchanged.
- **FR-031** Every existing `dsp_systems_tests` case MUST stay green with no test edited; the existing
  ecosystem determinism and conservation cases in particular.

### E. The controller consumer

- **FR-040** `Controller` MUST derive from `Steinberg::Vst::IDataExchangeReceiver`, declare it in
  `DEFINE_INTERFACES`, own a `DataExchangeReceiverHandler dataExchangeReceiver_{this}`, set
  `dispatchOnBackgroundThread = false` in `queueOpened`, copy the most recent block whose `size >=
  sizeof(EcosystemFrame)` into a cached frame in `onDataExchangeBlocksReceived` (ignoring blocks for another
  `userContextID` or too small), and override `notify` to forward the IMessage fallback through
  `dataExchangeReceiver_.onMessage` before `EditControllerEx1::notify`. (Membrum `controller.cpp:1697-1759`.)
- **FR-041** The controller header banner (`controller.h:12-13`) and `entry.cpp`'s FR-018 banner (`:12-15`)
  MUST be rewritten to describe the Phase 13 state; `Vorago_EditorLifecycle`'s `EditorBindsFourteenControls`
  section MUST be replaced by a section asserting the new built-tree binding count (106).
- **FR-042** `createCustomView` MUST create `EcosystemView` for `custom-view-name="EcosystemView"` and a
  header preset-browser button for `custom-view-name="PresetBrowserButton"` (Q1, FR-070), and return
  `nullptr` for any other name; the `EcosystemView` receives a pointer to the controller's cached frame; any
  raw view pointer the controller keeps (the `EcosystemView` and the preset browser overlay; R-2)
  MUST be zeroed in `willClose()`, the `EcosystemView` one observable through the test seam
  `[[nodiscard]] const UI::EcosystemView* ecosystemViewForTest() const noexcept`.
- **FR-043** `createSubController` MUST return a `VoragoPanelSubController` for its name and `nullptr`
  otherwise (C-5).

### F. The ecosystem view

- **FR-050** `EcosystemView` MUST implement C-6 clauses 1–8. Its mapping, agent style, link alpha, fade,
  redraw predicate, link-geometry arithmetic, habitat brightness and active-voice text (`agentStyle`,
  `linkAlpha`, `torusLinkSegments`, `needsRedraw`, the fade step, `habitatBrightness`, `activeVoicesText`)
  MUST live in free functions or static members that can be tested without a `CFrame`. It MUST expose
  `hasTimerForTest()` (C-6 clause 5).
- **FR-051** The view MUST never read the processor, never allocate per tick beyond VSTGUI's own draw calls,
  and MUST tolerate a frame with `agentCount = 0`, `linkCount = 0`, `linkFlowScale = 0`, or out-of-range
  indices (a link whose endpoint is `>= agentCount` is skipped). The skip decision MUST be made by the static
  predicate `EcosystemView::isDrawableLink(const EcosystemFrame&, std::size_t l) noexcept`, and the
  empty-habitat grid MUST come from the static function `EcosystemView::emptyHabitatGridLines(rect)`. Every
  drawn agent and link alpha MUST be scaled by `habitatBrightness(voiceLevel)`, and `activeVoices` MUST be
  rendered via `activeVoicesText` (Q6) — no carried frame field is unused.

### G. Page switching

- **FR-055** The seven pages MUST exist in the XML as sibling containers inside the page area, with the C-4
  ID sets; exactly one is visible at any time; the default is page 0 (Cloud).
- **FR-056** Switching pages MUST NOT unmount, resize or hide the concept band or the ecosystem view.

### H. Build, registration, cross-cutting

- **FR-060** `plugins/vorago/CMakeLists.txt` and `plugins/vorago/tests/CMakeLists.txt` MUST list every new
  `.cpp` (the test list is enumerated, not globbed); `src/ui/.gitkeep` MUST be removed once `src/ui/` has
  files.
- **FR-061** `plugins/vorago/CLAUDE.md` MUST be updated: `ui/` no longer empty; the frame/data-path rule; the
  `{4, 5}` allowlist; the page table.
- **FR-062** The build MUST be zero-warning on MSVC; `node tools/check-portability.js` and clang-tidy
  (`-Target vorago` and `-Target dsp`) MUST report zero findings on the touched files.
- **FR-063** `plugins/vorago/version.json` MUST be bumped to `0.2.0` and `plugins/vorago/CHANGELOG.md`
  MUST gain one `## [0.2.0]` entry covering the Phase 12 parameter surface and the Phase 13 interface (R-6).

### I. Preset browser (Q1)

- **FR-070** The header (C-1) MUST carry a preset-browser control, created by `createCustomView` for
  `custom-view-name="PresetBrowserButton"` (FR-042): the shared `Krate::Plugins::OutlineBrowserButton`
  (`plugins/shared/src/ui/outline_button.h:79`), created exactly as Seraphis does
  (`plugins/seraphis/src/controller/controller.cpp:478`; R-1 — no plugin-local class); clicking it opens the
  preset browser overlay.
- **FR-071** On editor open (`verifyView`/`didOpen`), the controller MUST construct one
  `Krate::Plugins::PresetBrowserView` sized to the frame and added as a full-frame overlay child (Seraphis
  `controller.cpp` shape); saving goes through the browser's own save dialog, and no separate
  `SavePresetDialogView` is constructed (R-2). The raw pointer MUST be nulled in `willClose()` (FR-042).
- **FR-072** `presetManager_` MUST be given a state provider that returns the processor's own state, obtained
  via `Steinberg::FUnknownPtr<Steinberg::Vst::IComponent>(getComponentHandler())` and `IComponent::getState()`
  into a fresh `MemoryStream` (Ruinae `controller_presets.cpp:372-385`) — the standard `IComponent` accessor,
  not a bespoke controller → processor message, so this satisfies *Non-goals*' "no controller → processor
  channel" (OQ-1 rationale).
- **FR-073** `presetManager_` MUST be given a load provider that reads the version-prefixed stream with the
  same per-band inverse-mapping helpers `Controller::setComponentState` already uses (`loadV2Tail` and its
  `loadXParamsToController` calls), applying each value via `beginEdit`/`performEdit`/`endEdit` (not raw
  `setParamNormalized`), so host automation and undo observe every restored value; a stream version greater
  than `kCurrentStateVersion` MUST be rejected exactly as `setComponentState` does.

### J. Gravity anchor-mode display (Q2)

- **FR-080** The Gravity concept macro knob (`kMacroGravityId = 104`, concept band, C-1) MUST carry a dynamic
  label/tooltip driven by the controller's cached `kResonanceAnchorModeId` (`403`) value: when the anchor
  mode is not `Hybrid`, the text MUST indicate the macro is inert (its only routed target, `ResonanceGravity`,
  is unused outside `Hybrid` — `vorago_voice.h:573`, Phase 12 edge case); when it is `Hybrid`, the normal
  label/tooltip MUST show. Display-only — no parameter is written, and the knob's interactivity, alpha and
  enabled state are unchanged (no dimming).
- **FR-081** The controller MUST observe `kResonanceAnchorModeId` (its own registered parameter; no new
  message or connection) and update the Gravity macro knob's label/tooltip synchronously with every value
  change, including the value already current when the editor opens.

---

## Success Criteria

All test names are sketches; tags follow the plugin's `[vorago]…` convention. Timed arms stay `[.perf]` and
run only through `node tools/run-cpu-tests.js vorago_tests`, alone.

- **SC-001 — Producer is read-only (audio unchanged).** `Vorago_EcosystemFrame_DoesNotChangeAudio`: same
  build, same process, two `Processor` instances, same seed and event script (note-on 48 at block 0, 60 s at
  512/48 kHz, one note-off at 45 s), seam forced **on** in one and **off** in the other. Output buffers MUST be
  sample-for-sample equal (an A/B inside one binary, not a stored golden). Non-vacuity: the forced arm's
  attempt counter equals its `process()` count and at least one frame has `agentCount > 0` and `linkCount > 0`.
- **SC-002 — Binding completeness.** `Vorago_UidescBindsEverySurfaceId`: `unreachableParams(xml, all108,
  {4, 5})` is empty; the multiset of bound tags over the XML has exactly 106 elements, each registered ID other
  than 4/5 exactly once; IDs 4 and 5 appear zero times; every view carrying a `control-tag` also carries a
  non-empty `tooltip` attribute (FR-006).
- **SC-003 — Tag table integrity.** Every `<control-tag>` value is a registered ID; every `control-tag`
  attribute used by a view is declared; the 14 Phase 11 tag names keep their values.
- **SC-004 — Per-view class rule.** For each bound view, its class matches C-4's rule for the ID's
  registered kind (22 list rows, 84 continuous visible rows); 100–111 are `ArcKnob`.
- **SC-005 — Layout.** Parsing the XML: the template is 1100 × 760 with equal min/max. All seven C-1 regions
  are found by `uidesc-label` (FR-002), and each one's rect, resolved to window coordinates, matches C-1
  exactly. Every macro view and every page control has a sibling `CTextLabel` that has no `control-tag` and
  has `mouse-enabled="false"` (FR-006). All twelve macro views are ≥ 80 × 80 and inside their blocks; every page knob is ≤ 48 ×
  48 and inside the page area; the ecosystem view is exactly `(350, 36, 750, 436)`.
- **SC-006 — Frame content equals the engine.** `Vorago_EcosystemFrame_MatchesEngine`: after every block of
  a 20 s render (seed index 0, one held note), for the focus voice `v`: `agentCount == ecosystem.getAgentCount()`;
  for each `i < agentCount`, `agentX/Y[i] == static_cast<float>(getAgentPositionX/Y(i))`,
  `agentKind[i] == static_cast<uint8_t>(getAgentKind(i))`,
  `agentGlow[i] == ecosystemEnergyGlow(getAgentEnergy(i), getAgentCount(), getEnergyBudget())` and
  `agentDormant[i] == (isAgentDormant(i) ? 1 : 0)` (exact — same values, same function, same casts); all entries
  above the counts are exactly zero. `voiceLevel == engine.getVoiceLevel(focus)` and `activeVoices ==
  engine.getActiveVoiceCount()` (Q6 — the two carried-but-previously-undrawn fields), and `linkFlowScale ==
  getEnergyBudget() / agentCount` (`0` when `agentCount == 0`) (Q5). Non-vacuity: over the render, at least
  two agents in one frame have shares `≥ 2` (≥ 2 × mean) with differing energies **and** differing
  `agentGlow`, which shows the top of the range is not flattened. The function arm `Vorago_EcosystemEnergyGlow`
  asserts that `ecosystemEnergyGlow` is strictly increasing over shares `{0, 0.5, 1, 2, 4, 8, 16, 48}`, equals
  `0.5f` at share 1, is `< 1` at share 48, and is `0.0f` for a negative share. If the render cannot reach the
  share `≥ 2` condition, the non-vacuity arm moves to a directly prepared `EcosystemEngine` and says so. It is
  never dropped.
- **SC-007 — Link selection is the strongest set.** Reference for every arm: sort all
  `getPairInteractionCount()` pairs by `|flow|`, drop exact zeros, take `min(64, n)`. Ties may be broken by any
  rule, so the test asserts on the multiset of strengths and checks that every carried pair is a recorded pair.
  Every arm also asserts `linkA/B < agentCount` and `linkA != linkB`.
  - **(a) Product level.** `Vorago_EcosystemFrame_LinksAreStrongest`: `Processor` at the shipped defaults
    (32 agents, kernelSigma 0.03, predation 0.55). At every block the carried set equals the reference. This is
    the only arm the plugin can reach.
  - **(b)–(d) Component level.** `Vorago_SelectStrongestLinks` calls `selectStrongestLinks` (C-2 clause 5)
    directly against an `EcosystemEngine` that the test prepares itself, stepping it one simulation step at a
    time and checking after every step:
    - (b) `PrepareConfig::agentCount = 48`, `setKernelSigma(0.35f)`: asserts `getPairInteractionCount() == 1128`
      on at least one step (the whole torus is inside the cutoff at σ = 0.35), and the carried set equals the
      reference with `linkCount == 64`.
    - (c) `setPredation(0.5f)`: `linkCount == 0` on every step, with `getPairInteractionCount() > 0` asserted so
      the arm is non-vacuous.
    - (d) `PrepareConfig::agentCount = 1`: `linkCount == 0`.
  - **(e) Worst-case timing, `[.perf]`.** `Vorago_SelectStrongestLinks_WorstCase` times `selectStrongestLinks`
    at `getPairInteractionCount() == 1128` (the arm (b) configuration), taking the best of 16 trials of 1 000
    calls, and WARN-records ns/call and ns/call ÷ one 512-sample block period at 48 kHz. It is recorded, not
    gated: 1128 is not a product state, and the product-level cost is gated by SC-012.
- **SC-008 — Cadence tracks the habitat, not the callback (Q4).** `Vorago_EcosystemFrame_Cadence`: the
  process-call counter increments by exactly 1 per `process()` that reaches the slice loop, including blocks
  with 0, 1 and 1024 events and a 2048-sample block, and does not increment on any of the six early-return
  shapes (`processor.cpp:256-286`). The publish-attempt counter increments **only** when C-2 clause 2's
  trigger holds: driving the processor with 32-sample buffers for one full simulation step's worth of samples
  (512 at the default) yields exactly **1** publish attempt, not 16; a single 2048-sample block yields exactly
  1. Forcing a focus change or an `agentCount` change (polyphony shrink) between two `process()` calls with no
  step advance still yields a publish attempt on the very next call. Holding the same focus/`agentCount`
  across many small-buffer calls with no step advance yields 0 further publish attempts until the step
  advances. Neither counter increments when the seam is off and no handler exists.
- **SC-009 — Focus-voice rule.** `Vorago_EcosystemFrame_FocusVoice`. The producer records which clause of
  C-2 clause 4 chose the focus in each frame through the seam `ecosystemFocusRuleForTest() → 'a' | 'b' | 'c'`.
  Every row asserts both the focus and the rule:
  | Script | Expected |
  |---|---|
  | one note | its slot, rule (a) |
  | two notes | the later-allocated slot, rule (a) |
  | release the later of two | the earlier slot (still `Active`), rule (a) |
  | all released | the last focus is retained by **rule (a)** while `getVoiceState(v) == Releasing`. The render runs until `getVoiceState(v) == Idle`, bounded at ≤ 60 s of audio (release + the 10 s quiescence counter, `vorago_voice.h:938-944`), and then asserts rule (c) with `agentCount == 0`. A voice reaches retirement below −90 dBFS, which is under `1e-4`, so rule (b) is not expected on this path, and the test asserts that it never fires here. |
  | shrink 4 → 1 with notes held on slots 0–3 | focus becomes 0 in the next frame, rule (a). Slots 1–3 are `Idle` at once (`voice_allocator.h:334-350`). |
  | **orphan tail (rule b)**: four notes at polyphony 4, the one on slot 3 held; release the other three and render until slots 0–2 are `Idle` (≤ 60 s); then shrink 4 → 1 | the frame after the shrink has `getVoiceState(3) == Idle` and focus 3 by **rule (b)** while `getVoiceLevel(3) > 1e-4` (`vorago_engine.h:565-576`). This is asserted on at least one frame, so the row is non-vacuous. Once the level drops to ≤ 1e-4 the rule is (c) and `agentCount == 0`. |- **SC-010 — Determinism and seed sensitivity.** Two processors, same seed index and script → the frame
  sequences are identical field-for-field for 2 000 blocks. Seed index 0 vs 1 → at least one differing
  `agentX` within the first 200 blocks.
- **SC-011 — RT safety.** `Vorago_EcosystemFrame_AllocationFree`: under `AllocationScope`, 2 000 `process()`
  calls with the seam on, random note-on/off (including steals at polyphony 2), pedal and parameter changes →
  **0 allocations**; the producer takes no lock (code review item) and the frame's floats are all finite
  (bit-pattern check). Function arm `Vorago_SanitizeFrameFloat` passes `sanitizeFrameFloat` quiet-NaN,
  signalling-NaN, +Inf and −Inf built from bit patterns via `volatile`, and a finite `1e300`. Each must return
  exactly `0.0f`. A finite in-range value comes back as `static_cast<float>(v)`. `ecosystemEnergyGlow` given a
  NaN energy also returns `0.0f`.
- **SC-012 — Carried CPU gate with the producer on.** `Vorago_ProcessorCpu` gains arm **PF** = arm P with
  `setEcosystemFrameForcedForTest(true)`, which forces both the handler-existence check and C-2 clause 2's
  cadence trigger (Q4), so every call in this arm performs a full fill — otherwise, once the trigger only
  fires on a step advance, the arm would measure the cheap no-fill path and pass vacuously (the same trap
  Overview fact 4 already names for the handler-existence half of the seam).
  **REQUIRE(PF_best ≤ 1.05 × D_best)** — the unchanged
  `kWrapperOverheadCeiling` (`processor_cpu_test.cpp:75`), which is the phase's CPU budget (roadmap line 608
  *"CPU budgets are FRs"*; the roadmap sets no Phase 13-specific figure). PF, P and D are timed **interleaved in
  one trial loop** (the Phase 9 `measureTrio` ruling), and `PF/P` is WARN-recorded. The existing P gate stays.
- **SC-013 — Controller consumer.** `Vorago_Controller_ConsumesEcosystemFrame`: feeding
  `onDataExchangeBlocksReceived` 3 valid blocks caches the last; a block of `sizeof - 1` bytes is ignored and
  the cache is unchanged; a full-size block delivered with a `userContextID` other than
  `kEcosystemFrameUserContextId` is ignored and the cache is unchanged; an IMessage fallback payload delivered through `notify` reaches the cache (via
  `DataExchangeReceiverHandler::onMessage`) and a non-DataExchange message still reaches
  `EditControllerEx1::notify`.
- **SC-014 — View arithmetic.** `Vorago_EcosystemView_Mapping`, with ε = 1e-6: the corners `(0,0)` and
  `(1-ε,1-ε)` map inside the rect. In `agentStyle`, glow → alpha and glow → radius are monotonic non-decreasing
  over 256 steps of `[0, 1]`. At glow 0 the style is an outline with alpha `≥ kAgentOutlineMinAlpha > 0`, and
  `dormant = true` gives an unfilled style at every glow (C-6 clause 2). `torusLinkSegments` is checked on four
  cases. In each, every piece lies inside the rect and the total clipped length is within 1 px of
  `|d| · width`, where `d` is the `wrapDelta` separation:
  - no wrap `(0.4, 0.4) → (0.6, 0.5)` gives 1 piece;
  - x-only wrap `(0.95, 0.5) → (0.05, 0.5)` gives 2 pieces;
  - y-only wrap `(0.5, 0.95) → (0.5, 0.05)` gives 2 pieces;
  - corner wrap `(0.95, 0.95) → (0.05, 0.05)` gives every non-empty piece, and none is dropped.

  Redraw predicate: `needsRedraw(s, s, 0) == false`, `needsRedraw(s, s + 1, 0) == true` and
  `needsRedraw(s, s, 1e-3f) == true`. `EcosystemView::isDrawableLink(frame, l)` returns false for
  `l >= linkCount`, `linkA[l] >= agentCount`, `linkB[l] >= agentCount` and `linkA[l] == linkB[l]`, and true for
  a valid link (FR-051). `EcosystemView::emptyHabitatGridLines(rect)` returns exactly 3 vertical and
  3 horizontal interior lines, the 4 × 4 grid (C-6 clause 6). **`linkAlpha` (Q5):** for a fixed
  `linkFlowScale > 0`, output is monotonic non-decreasing in `linkStrength` over 256 steps of a wide range,
  equals `0.5f` when `linkStrength == linkFlowScale`, is `< 1` at `linkStrength = 48 * linkFlowScale`, and is
  exactly `0.0f` for `linkStrength <= 0` or `linkFlowScale <= 0`. **`habitatBrightness` (Q6, R-3):** with
  ε = 1e-6, `0.01 → 1/3`, `0.1 → 2/3`, `0.5 → 0.899660`, `1 → 1.0f`; exactly `0.0f` at `0`, `-1` and
  `1e-3` (−60 dB, the floor); exactly `1.0f` at `2`; monotonic non-decreasing over 256 steps in `[-1, 2]`. **`activeVoicesText` (Q6):** equals
  `std::to_string(activeVoices)` for `{0, 1, 6, 255}`.
- **SC-015 — Link fade.** `Vorago_EcosystemView_LinkFade`: a link present once then absent has alpha
  `≤ exp(-t / 0.6)` (+1e-6) after `t` seconds of simulated ticks and exactly 0 once below 1/255; a present
  link is never below its `linkAlpha(linkStrength, linkFlowScale)` value (Q5); Δt > 0.25 s is clamped; a
  `focusVoice` change zeroes the table; an `agentCount` change with the same `focusVoice` also zeroes the
  table (C-6 clause 4).
- **SC-016 — Editor lifecycle with the custom view.** The headless harness does **not** attach views: it
  passes a null parent, so `CFrame::open` returns before `attached (this)`
  (`editor_lifecycle_harness.h:109-122`, `cframe.cpp:208-209`, `:220`, `:239-240`). It therefore covers
  view-tree construction and teardown only, and never runs `EcosystemView::attached()`/`removed()` or the timer.
  Two arms cover the two halves:
  - **(a) Tree construction/teardown.** `Vorago_EditorLifecycle`'s `HarnessCycles` runs **10** cycles (up from
    3) and passes. The replacement binding section counts **106** bound controls in the built tree and finds
    exactly one `EcosystemView`. During the open phase `ecosystemViewForTest() != nullptr`, and after
    `willClose` `ecosystemViewForTest() == nullptr` (FR-042). `createCustomView` with an unknown name and
    `createSubController` with an unknown name each return `nullptr` (FR-042, FR-043).
  - **(b) Attach/remove and the timer.** `Vorago_EcosystemView_AttachRemove` builds an `EcosystemView` inside a
    `CViewContainer` and calls `attached(parent)` / `removed(parent)` explicitly for 10 cycles. After every
    attach it asserts `hasTimerForTest() == true`, and after every remove `hasTimerForTest() == false`. A
    variant destroys the owner of the cached frame (a heap-allocated stand-in for the controller's cache)
    immediately after `removed()` and then sleeps 150 ms in 10 ms slices before destroying the view (R-5: there
    is no portable headless timer pump, and a platform pump is forbidden in tests; the destroyed-timer
    assertion is the guarantee, the ASan run below is the no-access proof). It must not touch the freed frame.
  - One ASan run (`-DENABLE_ASAN=ON`, Debug) of `vorago_tests "[lifecycle]"` must be clean, covering both arms
    and including (b)'s destroy-after-remove variant, and its log is recorded.
- **SC-017 — Page switch.** `Vorago_Editor_PageSwitch`: in the built tree, all seven page containers exist;
  selecting each segment `k` makes exactly container `k` visible; the ecosystem view and all twelve macro
  knobs remain visible and at their rects throughout; closing and reopening the editor restores the last page.
- **SC-018 — Surface frozen.** Phase 12's `Vorago_ParameterInfoTable` (the 108-row expected table) and the
  state round-trip / v1-load / truncation suites pass with **no edit** to `param_table_expected.h`.
  `Vorago_Editor_PageSwitchIsSessionOnly` runs on the controller, since page state is controller-side (C-5): a
  recording `IComponentHandler` is installed, and a scripted sequence of page switches through
  `VoragoPanelSubController` covers every page, repeats and returns to 0. The test asserts **zero**
  `beginEdit`/`performEdit`/`endEdit` calls, `getParameterCount() == 108` before and after, and every
  parameter's normalized value unchanged. The Vorago controller has no `getState` override (*Existing
  components*), so it has no controller stream to compare. If one is added in this phase, its output must also
  be byte-identical before and after.
- **SC-019 — Accessor correctness.** `EcosystemEngine_PairAccessors` (dsp_systems_tests): over a seeded 30 s
  run, for every step, `getPairAgentA(p) < getPairAgentB(p) < getAgentCount()` for all `p < getPairInteractionCount()`;
  and the flow check compares **per pair**, not per-agent sums. Stage 2 reads start-of-step `energy_`/`x_`/`y_`
  (`ecosystem_engine.h:1408-1413`, `:1426-1427`, `:1449`), and later stages change them. So the fixture drives
  the engine **one simulation step at a time**: it calls `processChunk` for exactly
  `getStepIntervalChunks() * kControlChunkSamples` samples and checks that `getControlStepCount()` advanced by 1.
  Before each step it snapshots every agent's position and energy, plus `getKernelSigma()`,
  `getExchangeRate()` and `getPredation()`. From the snapshot it recomputes the recorded pair set (every
  `i < j` whose `wrapDelta` separation gives `w = exp(-d²/(2σ²)) >= 1e-6`) and each
  `ref = exchangeRate * w * (ej - ei) * (1 - 2*predation)`, with the products associating as at `:1460`. It
  asserts that the set of `(getPairAgentA(p), getPairAgentB(p))` equals the reference set and that, for each
  pair, `|getPairFlow(p) − ref| <= 1e-12 * max(|ref|, 1e-300)`. A setup with `predation = 0.5` asserts every
  flow is exactly `0.0`. Out-of-range `p` returns `0 / 0 / 0.0`. All pre-existing `dsp_systems_tests` cases pass with no test file edited
  (`git diff --stat dsp/tests` shows only the new case).
- **SC-020 — pluginval.** `tools/pluginval.exe --strictness-level 5 --validate
  "build/windows-x64-release/VST3/Release/Vorago.vst3"` passes (the editor-open tests exercise the new UI).
- **SC-021 — Portability and lint.** Zero MSVC warnings on `Vorago` and `vorago_tests`; `node
  tools/check-portability.js` clean (followed by `wsl --shutdown`); clang-tidy `vorago` and `dsp` zero
  findings on touched files.
- **SC-022 — Gravity anchor-mode display (Q2).** `Vorago_GravityMacro_AnchorModeDisplay`: with
  `kResonanceAnchorModeId` ≠ `Hybrid`, the Gravity macro knob's (`kMacroGravityId`) label/tooltip carries the
  inert indication; at `Hybrid` it does not; the knob's alpha/enabled state is unchanged in both cases (no
  dimming, FR-080); switching `kResonanceAnchorModeId` while the editor is open updates the label/tooltip
  without a re-open, and no parameter is written by the display update itself.
- **SC-023 — Preset browser round trip (Q1).** `Vorago_PresetBrowser_SaveLoadRoundTrip`: the header preset
  control (`custom-view-name="PresetBrowserButton"`) opens `PresetBrowserView` (FR-070/FR-071); driving the
  state provider (FR-072) captures a stream via `IComponent::getState()`; after randomizing every one of the
  106 non-hidden parameters, driving the load provider (FR-073) with that stream restores all 106 values to
  their captured normalized values, matching what a direct `setComponentState` call with the same stream
  produces; every restored parameter's edit is observed by a recording `IComponentHandler` as a
  `beginEdit`/`performEdit`/`endEdit` triple (FR-073), and a stream version greater than
  `kCurrentStateVersion` is rejected (returns `false`, no parameter changed).

---

## Edge cases

**RT-safety boundaries**
- `process()` before `setupProcessing` / after `terminate`: the not-ready path returns before the slice loop
  (`processor.cpp:280-286`) — no publish, no counter increment (SC-008).
- Host without DataExchange support: the SDK falls back to IMessage; the controller's `notify` pass-through
  handles it (FR-040). The IMessage allocation happens inside the SDK's fallback on the host's queue thread
  path, not in the producer's fill; SC-011 measures the unconnected seam path, which is the audio-thread code
  this phase writes.
- Queue full: `getCurrentOrNewBlock()` returns `InvalidDataExchangeBlockID`; the frame is still produced into
  `pendingFrame_`, the skip counter increments, nothing blocks.
- `setActive(true)` with a handler opens the queue (the SDK allocates on the host thread with audio stopped);
  that path is not audio-thread-reachable.
- Worst-case link selection: the Vorago product always runs 32 agents with fixed kernelSigma 0.03 and
  predation 0.55 (*Existing components*, `VoragoVoice` row), so it sees at most 496 pairs and about 34 in
  practice. SC-012 gates that product cost. `pairCount = 1128` (48 agents, whole torus within cutoff) is a
  **component-level** bound, not a product state. `nth_element` over 1128 indices is bounded; correctness is
  covered by SC-007 (b) and the cost is WARN-recorded by SC-007 (e).

**Parameter extremes**
- Polyphony 1: focus is always slot 0. Polyphony 6: rule scans all six (`kMaxVoices`).
- `predation = 0.5`: zero links (SC-007 c). `kernelSigma` max: many pairs, capped at 64. Both are
  component-level only; no Vorago parameter reaches either knob.
- Ecosystem depth 0 (`kEcosystemDepthId`): the simulation still runs and is still drawn — depth scales the
  routing, not the economy.
- Agent count below 48 (prepare-time, 32 by default): `agentCount` carries the real value; unused slots zero.
- Space Freeze on: no effect on the frame (the ecosystem is per voice, the reverb global).

**Sample-rate changes**
- `setupProcessing` at 44.1/96/192 kHz re-prepares the engine; simulation step period stays 512 samples, so
  the step rate (and link churn) scales with the sample rate, and — because publish now follows step
  advances (Q4) — so does the publish rate: fewer, cheaper `process()` calls evaluate false between steps at
  low rates, more steps complete per wall-clock second at high rates. The fade constant is wall-clock (UI
  side), so perceived fade is rate-independent.
- A re-prepare resets the ecosystem; the next frame's positions jump; the view clears its fade table on the
  `agentCount`/`focusVoice` change rule only — a same-count reset leaves ≤ 0.6 s of stale fading links, which
  is accepted (visual only).

**Seed determinism**
- Same seed + same script → identical frames (SC-010). A live seed change reseeds voices
  (`rewindIdleVoices()`, Phase 12) — positions re-deal, which the view shows as a jump; accepted.

**UI lifecycle**
- Editor closed: the view and its timer do not exist; the controller keeps caching frames (cheap memcpy).
- Two editors open on one controller: each `EcosystemView` reads the same cached frame; fade tables are
  per view.
- Frame arrives with `linkA/B >= agentCount` (corrupt or future layout): link skipped (FR-051).

---

## Decisions this phase inherited (resolved — see Clarifications)

These are the decisions earlier specs handed to this phase; both are now ruled (*Clarifications*, session
2026-09-26). The third, Phase 9 FR-037's damper-offset visualisation, was already settled by the roadmap's
single-visualization rule (see *Non-goals*) and was never open. The switch from heard output to energy as
the brightness source was likewise never open: C-2 clause 3 and FR-023 decide it, because roadmap line 566
names energy.

- **Preset-browser UI: Phase 13 or Phase 14? → Phase 13 (Q1).** Phase 11 FR-050
  (`specs/vorago-phase11-plugin-scaffold/spec.md:604-610`) says *"The browser **UI** belongs to Phase
  13/14"*, and Phase 11 plan (`plan.md:860`) defers the state/load providers *"until Phase 13"*. Every other
  Krate plugin wires both providers (`plugins/ruinae/src/controller/controller.cpp:229-241`). Ruled Phase
  13: it is UI, it needs the header slot C-1 reserves, and Vorago's state is parameters only (no
  non-parameter payload like Seraphis's spectral slots), so the load provider applies a stream through the
  existing `setComponentState` inverse mappings plus `performEdit` per ID with no controller → processor
  message (FR-070–FR-073, SC-023).
- **Surface the anchor-mode / Gravity inertness? → Yes, display-only (Q2).** Phase 12's edge cases
  (`specs/vorago-phase12-parameters/spec.md:991-992`): anchor mode away from `Hybrid` makes Gravity's only
  macro row inert — *"recorded, not prevented; Phase 13 may surface it."* Ruled yes: a label/tooltip state on
  the Gravity macro knob driven by the controller's `kResonanceAnchorModeId` value, no parameter written, no
  behaviour change (FR-080, FR-081, SC-022).

---

## Traceability

| Roadmap statement (lines) | FR | SC |
|---|---|---|
| "VSTGUI only" (564) | FR-001, FR-005 | SC-005, SC-020, SC-021 |
| "Concept-first layout: the macro concepts dominate; engine panels beneath" (564–565) | FR-002–FR-004, FR-055, FR-056 | SC-005, SC-017 |
| "ecosystem view — live agent habitat (agents as glowing points, energy as brightness, interactions as fading links)" (565–566) | FR-023, FR-024, FR-030, FR-050, FR-051 | SC-006, SC-007, SC-014, SC-015, SC-019 |
| "via DataExchange piggyback (Membrum MetersBlock pattern — no new queues)" (566–567) | FR-020, FR-020a, FR-021, FR-022, FR-026, FR-040 | SC-001, SC-008, SC-013 |
| "No param-type swaps on registered IDs, ever" (567) | FR-014, FR-015 | SC-018 |
| RT safety (602–603) | FR-025, FR-027 | SC-011 |
| CPU budgets are FRs (608) | FR-025 | SC-012 |
| No bit-exact float goldens (618) | — | SC-001 is an in-binary A/B; SC-006/SC-010 compare live values |
| Portability (619–620) | FR-062 | SC-021 |
| Shared-component changes keep consumers green (622–624) | FR-031 | SC-019 |
| Phase 11 deferral of `src/ui/`, custom views, DataExchange (Phase 11 spec line 66) | FR-041–FR-043, FR-060, FR-061 | SC-016 |
| Every binding reachable (Part B template, Seraphis C-3) | FR-010–FR-013 | SC-002–SC-004 |
| Phase 11 FR-050 preset-browser UI deferral, ruled Phase 13 (Q1) | FR-070–FR-073 | SC-023 |
| Phase 12 anchor-mode/Gravity inertness, ruled surfaced (Q2) | FR-080, FR-081 | SC-022 |

---

## Review notes

- **Brightness source (fidelity review, major).** The spec now follows option (1). `agentGlow` is derived from
  `getAgentEnergy` through `ecosystemEnergyGlow` (share/(1+share)), which does not saturate, and dormancy is
  carried separately in `agentDormant`. The frame grows from 1020 to 1072 bytes (1068 plus Q5's
  `linkFlowScale`), and the static_assert changes to match. The heard output `getAgentOutput` is dropped from
  the frame, because the roadmap names energy.
- **SC-007 (e) is recorded, not gated.** The worst case of 1128 pairs cannot occur in the product (32 agents,
  fixed kernel), so no product budget applies to it. The product-level producer cost stays gated at 1.05 × D by
  SC-012, and that gate has not been relaxed. The new arm adds a measurement where there was none; it does not
  replace a gate.
- No review issue was rejected.
