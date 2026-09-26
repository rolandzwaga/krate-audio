# Implementation Plan: Vorago Phase 13 — UI (Concept-First Editor + Ecosystem View)

**Spec:** `specs/vorago-phase13-ui/spec.md` (reviewed; Clarifications Q1–Q6 encoded)
**Roadmap:** `specs/Vorago-roadmap.md` Part B, Phase 13 (lines 560–567)
**Branch:** `feat/vorago-phase1-events-modulation` at `e86fc1f8`
**Date:** 2026-09-26
**Status:** PLAN — no implementation

Every `file:line` below was opened in this planning session. Where this plan departs from the spec's
wording, the departure is listed in §11 with its reason, and the ones that need a user ruling are listed
again under *Open questions*.

---

## 0. What this plan decides (read first)

1. **One `dsp/` edit, three getters.** `EcosystemEngine` gains `getPairAgentA/B` and `getPairFlow`,
   read-only views of the private pair scratch at `ecosystem_engine.h:2459-2461` (§2). No other line
   under `dsp/include` changes.
2. **The producer is a gated, change-triggered fill.** `Processor::publishEcosystemFrame()` runs once
   after the slice loop. It evaluates the focus voice and a *habitat-changed* trigger on every call, and
   only fills and hands off a frame when the trigger holds (Q4). The fill goes only through three pure
   functions in `ecosystem_frame_builder.h` plus `const` getters (§3, §4).
3. **Two test seams, not one.** FR-026's `setEcosystemFrameForcedForTest(true)` forces the gate **and**
   the trigger, as the spec requires. SC-008 cannot be measured under that seam, because it forces the
   trigger. So the plan adds `setEcosystemFrameEnabledForTest(bool)`, which opens the gate only, as if a
   handler existed, and leaves the natural trigger in place (§4.4, §11 D-2). Neither seam is the only coverage of the transport: a third,
   seam-free fixture connects the processor to a real `DataExchangeHandler` over the SDK's IMessage
   fallback, with no message pump, and tests `connect`/`setActive`/transport/queue-full/`disconnect`
   and the allocation-freedom of the connected path (§4.4, `Vorago_EcosystemFrame_HandlerLifecycle`).
4. **The header preset button reuses the shared `Krate::Plugins::OutlineBrowserButton`**
   (`plugins/shared/src/ui/outline_button.h:79`), which is how Seraphis ships it
   (`plugins/seraphis/src/controller/controller.cpp:477-479`). The spec proposed a new plugin-local
   `PresetBrowserButton` class. That class is not created, because the shared control already does the
   job (§5.5, §11 D-1). **Ruled R-1 (spec Clarifications, plan stage).**
5. **One sub-controller does all session UI routing.** `VoragoPanelSubController` sits on the template
   root. It is modelled on `Seraphis::UI::SeraphisEditSubController` (`plugins/seraphis/src/ui/edit_sub_controller.h`).
   It owns the page strip, the preset button click, and the capture of the Gravity knob and its label.
   Controls that have no `ParamID` carry session tags `>= 9000`. The sub-controller swallows their
   `valueChanged`/`controlBeginEdit`/`controlEndEdit`, so none of them reaches `performEdit` (§5.3, SC-018).
6. **The Gravity display is an `IDependent` observer on `kResonanceAnchorModeId`.** It uses the same
   mechanism, and the same synchronous thread semantics, that VSTGUI's own bound controls use
   (`vst3editor.cpp:100-110`, `:174`). The controller overrides `update()`. It adds itself as a dependent
   in `didOpen` and removes itself in `willClose`. No new class is needed (§5.6).
7. **`EcosystemView` keeps its own 4-line torus delta.** It does not include `ecosystem_engine.h`. The UI
   TU must not pull a 2.5k-line Layer 3 header in for one static function. The local copy mirrors
   `wrapDelta` (`ecosystem_engine.h:1111-1119`, which is private anyway, after `private:` at `:1042`), and SC-014's wrap cases pin it (§6.3).

---

## 1. Verified facts this plan builds on

### 1.1 `EcosystemEngine` — `dsp/include/krate/dsp/systems/ecosystem_engine.h`

| Fact | Cite |
|---|---|
| Include block: `core/db_utils.h`, `core/random.h`, `<algorithm> <array> <cmath> <cstddef> <cstdint>`, plus a comment saying the set "is asserted line-by-line" | `:72-80` |
| `kMaxAgents = 48`, `kMinAgents = 1`, `kNumKinds = 5`, `kMaxPairs = kMaxAgents*(kMaxAgents-1)/2` + `static_assert(kMaxPairs == 1128)`, `static_assert(kMaxAgents <= 255, "pair index arrays are std::uint8_t")` | `:152-174` |
| `kMinEnergyBudget = 1.0e-3`, `kWakeSilenceEpsilon = 1.0e-6f`, `kOutputAnchor = 0.5` | `:214`, `:221`, `:231` |
| `kNeighbourWeightCutoff = 1.0e-6` | `:246` |
| `enum class Kind : std::uint8_t { Partial = 0, Resonator, Noise, Feedback, Ghost = 4 }`, APPEND ONLY | `:282-288` |
| `PrepareConfig{agentCount = 32, resourceCells = 64, energyBudget = 1.0, initialPoolFraction = 0.5, stepIntervalChunks}`; designated initialisers required | `:304-310` |
| `void prepare(double sampleRate, const PrepareConfig&) noexcept` clamps `agentCount_` into `[kMinAgents, kMaxAgents]`, sets `dt_ = stepChunks*64/sr`, calls `clearCounters()`, zeroes `samplePhase_`/`chunkPhase_` | `:336-378` |
| `reset()` and `setSeed()` → `clearCounters()` → **`getControlStepCount()` returns to 0** | `:388-407` |
| `void processChunk(std::size_t numSamples) noexcept`. The residues persist across calls, and a step fires every `stepChunks_ * 64` samples | `:431-450` |
| `setKernelSigma(float)` clamps to `[0.01, 0.35]` then calls `refreshKernelDerivatives()`; `setPredation(float)` clamps to `[0, 1]`; getters `getKernelSigma/getExchangeRate/getPredation` | `:469-497` |
| `isAgentDormant(i)` | `:829-831` |
| Three-class getter contract; class (i) indexed getters are neutral only on an out-of-range index | `:843-878` |
| `getAgentOutput` `:886`, `getAgentEnergy` `:890` (double), `getAgentKind` `:893`, `getAgentPositionX/Y` `:896/:899` (double), `getAgentCount` `:905`, `getEnergyBudget` `:921`, `getStepIntervalChunks` `:923` | as cited |
| `getPairInteractionCount()` — "on the most recent simulation step (NOT a cumulative total)" | `:959-961` |
| `getControlStepCount()` | `:1001` |
| `static double wrapDelta(double d)` two-compare form, result in `[-0.5, +0.5]`; `wrap01` keeps positions in `[0, 1)` | `:1111-1136` |
| `simulationStep()` starts `++stepCount_` | `:1206-1207` |
| Stage 2 records every surviving pair: `flow = exchangeRate * w * (ej - ei) * exchangeSign`, with `exchangeSign = 1.0 - (2.0 * predation)`, `w = std::exp(-d2 * invTwoSigmaSq)`, pre-test `d2 > cutDistSq`, normative test `w < kNeighbourWeightCutoff`; `pairI_[pairCount_] = uint8(i); pairJ_ = uint8(j); pairFlow_ = flow; ++pairCount_` with `j > i` | `:1380-1464` |
| `refreshKernelDerivatives()`: `sigma = double(kernelSigma_)`, `invTwoSigmaSq_ = 1/(2*sigma*sigma)`, `cutDistSq_ = 2σ²·13.8155…·(1+1e-9)` | `:2036-2041` |
| `publish()`: `raw = kOutputAnchor * energy * agentCount / energyBudget * gate`, clamped to 1, snapped to 0 at or below `kWakeSilenceEpsilon` | `:2297-2325` |
| Members `std::array<std::uint8_t, kMaxPairs> pairI_{}, pairJ_{}; std::array<double, kMaxPairs> pairFlow_{}; std::size_t pairCount_ = 0;` | `:2459-2461` |

### 1.2 `VoragoVoice` / `VoragoEngine`

| Fact | Cite |
|---|---|
| `VoragoVoice::Config::ecosystemAgents = 32`, `ecosystemStepChunks = 8` (clamped to [8, 64]) | `vorago_voice.h:200`, `:205` |
| `ecosystem_.prepare(sampleRate_, EcosystemEngine::PrepareConfig{.agentCount = agents, …})` | `vorago_voice.h:490`, `:518` |
| `[[nodiscard]] const EcosystemEngine& ecosystem() const noexcept` | `vorago_voice.h:1514` |
| `noteOn` does **not** reset the ecosystem (it touches cloud, bodies, resonance and envelope only), so a retrigger keeps the step count running | `vorago_voice.h:905-926` |
| `level_` is a linear chunk-peak follower with a 100 ms release: `level_ = (peak > level_) ? peak : peak + (level_ - peak) * coeff` | `vorago_voice.h:2151-2154`, `:290` |
| `isFinished()`: `quiescentChunks_ >= quiescentChunksToRetire_` (10 s under the tail threshold) | `vorago_voice.h:938-944`, `:297` |
| `VoragoEngine::kMaxVoices = 6` | `vorago_engine.h:225` |
| `setPolyphony` → `allocator_.setVoiceCount`, then `noteOff` + `orphanTail_` for slots still sounding | `vorago_engine.h:565-578` |
| `getActiveVoiceCount()` counts only slots `< polyphony_` that are non-Idle (an orphan tail is not counted) | `vorago_engine.h:1252-1260` |
| `getVoiceLevel` `:1274`, `getVoiceState` `:1277`, `getVoice` (clamps an out-of-range index to 0) `:1280-1282`, `getVoiceAllocationSerial` (strictly increasing, 0 = never) `:1285-1289` | as cited |
| Default envelope release **45 000 ms** (`VoragoVoice::kDefaultReleaseMs`) | `plugins/vorago/src/parameters/envelope_params.h:65` |

### 1.3 Vorago plugin (what is extended)

| Fact | Cite |
|---|---|
| Processor overrides: `initialize/terminate/setBusArrangements/setupProcessing/setActive/process/getLatencySamples/setState/getState`. No `connect`/`disconnect`/`notify` | `processor.h:68-78` |
| Test seams are const accessors in the public section; `static_assert(sizeof(Processor) < 64u * 1024u)` | `processor.h:81-113`, `:280` |
| `setupProcessing` ends with `AudioEffect::setupProcessing(setup)`, which stores `processSetup` (`vstaudioeffect.cpp:151-158`) | `processor.cpp:219` (the not-ready branch also calls it at `:180`) |
| `setActive` (only re-arms the gain snap on true; releases, silences and resets on false) | `processor.cpp:227-245` |
| `process`: six early returns (`numOutputs/outputs`, `channelBuffers32`, `numChannels < 2`, `numSamples <= 0`, `outL/outR` null, not-ready) | `processor.cpp:256-286` |
| Slice loop `while (cursor < total) { … renderSlice … }` | `processor.cpp:358-389` |
| `numPedalPoints_ = 0;` at `:391`, `silenceFlags = 0` at `:393`, `return kResultOk;` at `:394` | as cited |
| Controller: bases `EditControllerEx1, IMidiMapping, VST3EditorDelegate`; `DEFINE_INTERFACES … DEF_INTERFACE(IMidiMapping)` | `controller.h:29-57` |
| Banner "No createCustomView / verifyView: stock views only in Phase 11 (FR-055)" | `controller.h:12-13` |
| `presetManager_` is built with no providers ("No state/load providers until the Phase 13 browser exists") | `controller.cpp:79-82` |
| `setComponentState`: version guard `version > kCurrentStateVersion → kResultFalse` (`:102-104`); `setParam` lambda over `setParamNormalized` (`:110`); `loadV2Tail` lambda (`:111-127`); v1 default-tail synthesis (`:130-155`) | as cited |
| Every `load*ToController` helper is `template <typename SetParamFunc>` | `global_params.h:235-256`, `cloud_params.h:230` |
| `createView` returns `new VSTGUI::VST3Editor(this, "editor", "editor.uidesc")` | `controller.cpp:179-186` |
| `kResonanceAnchorModeId = 403`, list `{Free, Keyed, Hybrid}`, Hybrid == index 2, `kNumResonanceAnchorModes = 3`, `indexFromNormalized(double n, int count)` | `resonance_params.h:11`, `:50`, `:57-62`, `:147-148`; `param_mapping.h:57` |
| 108 IDs in bands, `kMacroGravityId = 104`, `kSustainPedalId = 4`, `kChannelPressureId = 5` | `plugin_ids.h:84-224` |
| `kCurrentStateVersion = 2`, `kStateV2Bytes = 428` | `plugin_ids.h:23`, `:28` |
| Placeholder uidesc: banner `:3-8`; colors `:9-15`; 14 control-tags `:19-34`; 420 × 520 template `:35-36` | `editor.uidesc` |
| `entry.cpp` FR-018 banner (no ui includes) | `entry.cpp:12-15` |
| Plugin source list is enumerated; the comment says every `.cpp` goes in BOTH lists | `plugins/vorago/CMakeLists.txt:20-69` |
| Test list is enumerated; the plugin `.cpp` files are compiled a second time; there is a `-fno-fast-math` block; `processor_cpu_test.cpp` is deliberately left out of that block | `plugins/vorago/tests/CMakeLists.txt:7-61`, `:95-127` |
| `src/ui/` holds only `.gitkeep` | `ls -a plugins/vorago/src/ui` |
| CHANGELOG has a single `## [0.1.0] - 2026-09-24` entry; `version.json` is 0.1.0; no `[Unreleased]` heading exists in any plugin CHANGELOG | `plugins/vorago/CHANGELOG.md:8` |

### 1.4 Templates reused as code or pattern

| Fact | Cite |
|---|---|
| Seraphis `connect`: `AudioEffect::connect`; on `kResultTrue` build `DataExchangeHandler(this, configCallback)` with `blockSize/numBlocks=4/alignment=32/userContextID`, then `onConnect(other, getHostContext())`. `disconnect`: `onDisconnect` + `reset()` | `plugins/seraphis/src/processor/processor.cpp:1194-1220` |
| Seraphis `onActivate` from `setActive`, host thread; the fallback path allocates (`dataexchange.cpp:76-105`) | `processor.cpp:992-1020` |
| Seraphis focus rule and transport block (`getCurrentOrNewBlock` → invalid-id/null/size check → memcpy → `sendCurrentBlock`), skipped-block counter | `processor.cpp:4121-4225` |
| Membrum consumer: `queueOpened` sets `dispatchOnBackgroundThread = false`; `onDataExchangeBlocksReceived` memcpys the last valid block; `notify` → `dataExchangeReceiver_.onMessage(message)` → else `EditControllerEx1::notify` | `plugins/membrum/src/controller/controller.cpp:1697-1759` |
| SDK receiver interface: `queueOpened(DataExchangeUserContextID, uint32 blockSize, TBool& dispatchOnBackgroundThread)`, `queueClosed(id)`, `onDataExchangeBlocksReceived(id, uint32 numBlocks, DataExchangeBlock* blocks, TBool onBackgroundThread)` | `extern/vst3sdk/pluginterfaces/vst/ivstdataexchange.h:184-210` |
| IMessage fallback format: ID `"DataExchange"`, int `"UserContextID"`, binary `"Data"` | `extern/vst3sdk/public.sdk/source/vst/utility/dataexchange.cpp:44-49`, `:179-185`, `:411-432` |
| `dataexchange.cpp` is part of the `sdk` library, so no source-list entry is needed | `extern/vst3sdk/cmake/modules/SMTG_VST3_SDK.cmake:270` |
| `DataExchangeHandler::onConnect(other, hostContext)` stores the connection and takes `IDataExchangeHandler` from `hostContext`; when the host context does not implement it, `Impl::openQueue` builds the `MessageHandler` fallback | `dataexchange.cpp:320-325`, `:212-230` |
| Fallback `MessageHandler::openQueue`: returns false if `hostApp` or `connection` is null; `Timer::create(this, 1)` (false if null); resizes the three ring buffers and `aligned_alloc`s `numBlocks` blocks; then **synchronously** `connection->notify` a `"DataExchangeQueueOpened"` message carrying int `"UserContextID"` and int `"BlockSize"`. `closeQueue` frees and notifies `"DataExchangeQueueClosed"` with `"UserContextID"` | `dataexchange.cpp:44-49`, `:77-105`, `:107-135` |
| Fallback `lockBlock` pops `rtOnlyBuffer` then `realtimeBuffer` and returns `nullptr` when both are empty; `freeBlock(true)` pushes into `messageBuffer`. Only `onTimer` moves blocks back from `messageBuffer` to `realtimeBuffer`, so an **unpumped** queue accepts exactly `numBlocks` sends and then `getCurrentOrNewBlock()` returns `InvalidDataExchangeBlock`. Neither lock nor free allocates (pre-sized ring buffers) | `dataexchange.cpp:137-172`, `:174-190`, `:245-289`, `:373-384` |
| `onActivate` calls the config callback and opens the queue; `onDeactivate` closes it; `onDisconnect` closes it and clears the connection | `dataexchange.cpp:337-358`, `:328-334` |
| `Timer::create` on Linux returns `nullptr` unless a factory was injected with `InjectCreateTimerFunction`; Windows and macOS build a platform timer | `extern/vst3sdk/base/source/timer.cpp:339-353`, `:306-313`, `:169-175` |
| `HostApplication : IHostApplication` (does not implement `IDataExchangeHandler`); `hostclasses.cpp` is already compiled into `vorago_tests` | `public.sdk/source/vst/hosting/hostclasses.h:32-47`; `plugins/vorago/tests/CMakeLists.txt:53-55` |
| `ComponentBase::notify` returns `receiveText(...)` for a `"TextMessage"` with a `"Text"` string, else `kResultFalse`; the default `receiveText` returns `kResultOk` | `public.sdk/source/vst/vstcomponentbase.cpp:91-107`, `:153-156` |
| `CControl::beginEdit` calls `listener->controlBeginEdit`, then `getFrame()->beginEdit(tag)`; `VST3Editor::beginEdit/endEdit` are deliberate no-ops, so the **listener** is the only path from a control's begin/end edit to `EditController::beginEdit` | `vstgui/lib/controls/ccontrol.cpp:186-197`; `vstgui/plugin-bindings/vst3editor.cpp:691-701` |
| Seraphis `createCustomView` builds the rect from the `origin`/`size` attributes; `"PresetButton"` → `new Krate::Plugins::OutlineBrowserButton(viewRect, nullptr, -1, "PRESETS")` | `plugins/seraphis/src/controller/controller.cpp:445-481` |
| `OutlineBrowserButton : CControl`; ctor `(const CRect&, IControlListener*, int32_t tag, std::string title, CColor frame = {64,64,72})`; `onMouseDown`: `beginEdit(); setValueNormalized(1); valueChanged(); setValueNormalized(0); endEdit();` | `plugins/shared/src/ui/outline_button.h:79-131` |
| Seraphis sub-controller: `DelegationController` with `valueChanged/controlBeginEdit/controlEndEdit/verifyView` overrides, session tags `>= 9000` | `plugins/seraphis/src/ui/edit_sub_controller.h` |
| Seraphis `togglePresetBrowser`: `new PresetBrowserView(frame->getViewSize(), presetManager_.get(), tabLabels)`, `frame->addView`, `open()`; `willClose` calls `presetBrowserView_->close()` before nulling | `plugins/seraphis/src/controller/controller.cpp:525-534`, `:571-599` |
| `makeSeraphisPresetTabLabels()` = `{"All"} + config.subcategoryNames` | `plugins/seraphis/src/preset/seraphis_preset_config.h:52-60` |
| Ruinae state provider: `FUnknownPtr<IComponent>(getComponentHandler())`, `component->getState(new MemoryStream)`, seek 0 | `plugins/ruinae/src/controller/controller_presets.cpp:372-385` |
| Ruinae overlays in `didOpen`: `PresetBrowserView` and `SavePresetDialogView(frameSize, pm, categories)` added to the frame | `plugins/ruinae/src/controller/controller.cpp:575-589` |
| `PresetBrowserView` ctor `(const CRect&, PresetManager*, std::vector<std::string>)`, `open()`, `close()`, `isOpen()` | `plugins/shared/src/ui/preset_browser_view.h:53-67` |
| `SavePresetDialogView(const CRect&, PresetManager*, std::vector<std::string> categories = {})`, `open(const std::string&)`, `close()` | `plugins/shared/src/ui/save_preset_dialog_view.h:40-51` |
| `PresetBrowserView` builds its **own** internal save dialog | `plugins/shared/src/ui/preset_browser_view.cpp:503`, `:615-620` |
| `PresetManager::loadPreset` uses the load provider when `processor_ == nullptr`, and hands it a `ReadOnlyBStream` over the component chunk | `plugins/shared/src/preset/preset_manager.cpp:153-196` |
| `ArcKnob : CKnobBase`; creator `"ArcKnob"` with attributes `arc-color`, `guide-color`, `indicator-length`, `arc-width`; `inline ArcKnobCreator gArcKnobCreator;` — "Include this header from each plugin's entry.cpp" | `plugins/shared/src/ui/arc_knob.h:49`, `:560-600`, `:714-716` |
| Seraphis registers ArcKnob from `entry.cpp` as `#include <ui/arc_knob.h>` (`:34`). Its test binary gets it transitively through `edit_sub_controller.cpp` → `macro_ring_knob.h` → `arc_knob.h` | `plugins/seraphis/src/entry.cpp:34`, `ui/edit_sub_controller.cpp:9`, `ui/macro_ring_knob.h:35` |
| Seraphis `CloudView` timer: `attached` creates `owned(new CVSTGUITimer(lambda, ms))`; `removed` calls `timer_->stop(); timer_ = nullptr;` | `plugins/seraphis/src/ui/cloud_view.cpp:386-402` |
| `CView::attached(parent)` works without a frame (`parentFrame` may be null); `CView::removed` clears the flags | `extern/vst3sdk/vstgui4/vstgui/lib/cview.cpp:446-487` |
| `CView::setTooltipText(text)` stores the attribute `kCViewTooltipAttribute = 'cvtt'` | `cview.cpp:1492-1498`, `cview.h:42` |
| `CSegmentButton::setSelectedSegment(uint32_t)`, `getSelectedSegment()` | `vstgui/lib/controls/csegmentbutton.h:74-76` |
| The `uidesc-label` attribute is applied by the stock view creator only (`viewcreator.cpp:166-172`). A `createCustomView` view never receives it, but `verifyView` gets the raw `UIAttributes` | `uidescription/viewcreator/viewcreator.cpp` |
| `Parameter::setNormalized` → `changed()` → `UpdateHandler::triggerUpdates`, which runs **synchronously on the caller's thread**; `deferUpdate` is the async path | `public.sdk/source/vst/vstparameters.cpp:62-78`, `base/source/fobject.cpp:196-211` |
| VST3Editor's bound-control updates use the same `addDependent`/`update` mechanism | `vstgui/plugin-bindings/vst3editor.cpp:100-110`, `:174` |
| `EditControllerEx1::update(FUnknown*, int32)` handles `ProgramList` only, so an override must call the base first | `public.sdk/source/vst/vsteditcontroller.cpp:455-463` |
| Bit-pattern finiteness: `detail::isFinite(double)` via `opaqueDoubleBits`, which survives `-ffast-math` | `dsp/include/krate/dsp/core/db_utils.h:125-129` |

### 1.5 Test infrastructure

| Fact | Cite |
|---|---|
| `VoragoTest::ProcessorFixture`: `prepare(sr, maxBlock)` = `setupProcessing` + `setActive(true)`; `processBlock(n, ev, pc)` builds `ProcessData` on the stack and uses `REQUIRE`; `renderScript(script, total, blockPattern)`; never calls `connect()` | `plugins/vorago/tests/vorago_test_fixture.h:166-300` |
| RT test pattern: `TestHelpers::AllocationScope scope;` around each `processBlock`, then read `AllocationDetector::instance().getAllocationCount()` | `plugins/vorago/tests/integration/automation_rt_test.cpp:115-119` |
| CPU gate: `BareProcessCall`, `DirectChain`; P and D interleaved per trial (`kTrials = 16`, `kBlocksPerTrial = 100`); `REQUIRE(pBestNs <= kWrapperOverheadCeiling * dBestNs)` | `processor_cpu_test.cpp:95-266` |
| `Vorago_EditorLifecycle` sections `PresetConfigIsLive`, `HarnessCycles`, `EditorBindsFourteenControls`. `collectBoundControls` counts every `CControl` with `getTag() >= 0` | `plugins/vorago/tests/unit/controller/editor_lifecycle_test.cpp:63-200` |
| `exerciseEditorLifecycle(controller, templateName, path, cycles = 3)` → `view->attached(nullptr, platform)` (builds the tree, fires `didOpen`), then `removed()` (fires `willClose`) | `tests/test_helpers/editor_lifecycle_harness.h:102-125` |
| `unreachableParams(xml, registeredIds, allowlist)` | `tests/test_helpers/uidesc_reachability.h:88-91` |
| Seraphis preset-load test: a component-handler stub that records begin/perform/end and answers `IComponent` by forwarding to a real `Processor` | `plugins/seraphis/tests/integration/preset_load_test.cpp:1-80` |
| Innexus has an IMessage-fallback DataExchange round-trip test | `plugins/innexus/tests/integration/test_data_exchange_pipeline.cpp:211` |
| Innexus `PipelineFixture` connects a real processor and controller headlessly: `HostApplication host`; `proc.initialize(&host)`; `proc.connect(ctrlConn)`; `setupProcessing`; `setActive(true)` | `test_data_exchange_pipeline.cpp:108-163` |
| `ProcessorFixture` initializes with a **null** host context, so a handler built on it could never open the fallback queue (`hostApp` null) | `vorago_test_fixture.h:186` |
| `AllocationScope` / `AllocationDetector` | `tests/test_helpers/allocation_detector.h:48`, `:111-120` |
| DSP ecosystem TUs are registered at `dsp/tests/CMakeLists.txt:488-491`; the `-fno-fast-math` list entry is at `:1022` | as cited |

---

## 2. DSP change — `EcosystemEngine` pair accessors (FR-030, FR-031, SC-019)

**File:** `dsp/include/krate/dsp/systems/ecosystem_engine.h` (Layer 3). The include block at `:72-80` is not
touched. The new code sits in the Diagnostics block, directly after `getPairInteractionCount()` (`:961`),
and joins the indexed-getter contract class (i).

```cpp
/// @brief Pair @p p of the most recent simulation step's recorded interaction
///        table (stage 2), p < getPairInteractionCount().
///
/// Contract class (i) (see the three-class banner above): NEUTRAL ON AN
/// OUT-OF-RANGE INDEX ONLY (0 / 0 / 0.0), before and after prepare(), and never
/// reads out of range. getPairAgentA(p) < getPairAgentB(p) < getAgentCount()
/// for every in-range p (stage 2 records j > i).
[[nodiscard]] std::size_t getPairAgentA(std::size_t p) const noexcept {
    return (p < pairCount_) ? static_cast<std::size_t>(pairI_[p]) : std::size_t{0};
}
[[nodiscard]] std::size_t getPairAgentB(std::size_t p) const noexcept {
    return (p < pairCount_) ? static_cast<std::size_t>(pairJ_[p]) : std::size_t{0};
}
/// The RECORDED, signed, PRE-stage-3-scale flow - the exchange the rule asked
/// for, not the joules stage 3 actually moved after its spare/want scaling.
/// Identically 0.0 at predation == 0.5 (see the FR-021 trap at stage 2).
[[nodiscard]] double getPairFlow(std::size_t p) const noexcept {
    return (p < pairCount_) ? pairFlow_[p] : 0.0;
}
```

- `pairCount_ <= kMaxPairs` always holds (the static_assert at `:172-173`, and the increment at `:1464`), so
  `p < pairCount_` bounds the `std::array` reads.
- `pairCount_` is zeroed by `clearCounters()` (`:2123`), so an unprepared object reads 0 for all three,
  which satisfies both class (i) and class (ii) at once.
- `kConfigKnobCount` (`:270`) stays 28. These are not knobs, so `EcosystemEngine_FuzzCoverageIsComplete`
  is unaffected.
- No Seraphis consumer is affected. `ecosystem_engine.h`'s only includer is `vorago_voice.h` (spec,
  *Existing components*).

---

## 3. Frame payload and pure builder functions

### 3.1 `plugins/vorago/src/processor/ecosystem_frame.h` (new; shared by processor and controller)

The spec's C-2 is transcribed exactly. The header includes `<cstddef> <cstdint> <type_traits>` only (FR-020).

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace Vorago {

inline constexpr std::size_t kMaxFrameAgents = 48;  // == EcosystemEngine::kMaxAgents (asserted in the builder)
inline constexpr std::size_t kMaxFrameLinks = 64;
inline constexpr std::uint32_t kEcosystemFrameUserContextId = 0x5645434Fu;  // 'VECO'

struct EcosystemFrame {
    std::uint32_t sequence = 0;
    std::uint8_t activeVoices = 0;
    std::uint8_t focusVoice = 0;
    std::uint8_t agentCount = 0;
    std::uint8_t linkCount = 0;
    float voiceLevel = 0.0f;
    float agentX[kMaxFrameAgents] = {};
    float agentY[kMaxFrameAgents] = {};
    float agentGlow[kMaxFrameAgents] = {};
    std::uint8_t agentKind[kMaxFrameAgents] = {};
    std::uint8_t agentDormant[kMaxFrameAgents] = {};
    std::uint8_t linkA[kMaxFrameLinks] = {};
    std::uint8_t linkB[kMaxFrameLinks] = {};
    float linkStrength[kMaxFrameLinks] = {};
    float linkFlowScale = 0.0f;
};
static_assert(sizeof(EcosystemFrame) == 1072, "C-2 layout: 8+4+3*192+48+48+64+64+256+4");
static_assert(std::is_trivially_copyable_v<EcosystemFrame>);
static_assert(std::is_standard_layout_v<EcosystemFrame>);
static_assert(offsetof(EcosystemFrame, linkStrength) == 812);   // the uint8 run ends 4-aligned
static_assert(offsetof(EcosystemFrame, linkFlowScale) == 1068);

}  // namespace Vorago
```

Raw C arrays are used here instead of `std::array`. The payload is memcpy'd across a process boundary, and
this matches `CloudFrame`/`MetersBlock`. The `offsetof` asserts (with `<cstddef>`) guard the no-padding
claim that the spec makes in prose.

### 3.2 `plugins/vorago/src/processor/ecosystem_frame_builder.h` (new; processor side only)

This header includes `ecosystem_frame.h`, `<krate/dsp/systems/ecosystem_engine.h>`, `<krate/dsp/core/db_utils.h>`,
`<algorithm> <cmath> <cstddef> <cstdint> <limits> <span>`. **It is never included by `ui/` or `controller/`.**
Everything in it is `inline`, `noexcept` and allocation-free.

```cpp
namespace Vorago {
static_assert(kMaxFrameAgents == Krate::DSP::EcosystemEngine::kMaxAgents);  // FR-020a
static_assert(Krate::DSP::EcosystemEngine::kMaxAgents <= 255);               // uint8 link / agent indices

/// FR-027. Non-finite (bit pattern, never std::isnan) -> 0.0f; |v| > FLT_MAX -> 0.0f
/// (the narrowing would be UB, [conv.double]); else static_cast<float>(v).
[[nodiscard]] inline float sanitizeFrameFloat(double v) noexcept {
    if (!Krate::DSP::detail::isFinite(v)) return 0.0f;
    constexpr double kMax = static_cast<double>(std::numeric_limits<float>::max());
    if (v > kMax || v < -kMax) return 0.0f;
    return static_cast<float>(v);
}

/// C-2 clause 3. share = energy * agentCount / energyBudget; glow = share / (1 + share).
[[nodiscard]] inline float ecosystemEnergyGlow(double energy, std::size_t agentCount,
                                               double energyBudget) noexcept {
    if (!Krate::DSP::detail::isFinite(energy) || !Krate::DSP::detail::isFinite(energyBudget)
        || !(energyBudget > 0.0)) {
        return 0.0f;
    }
    const double share = energy * static_cast<double>(agentCount) / energyBudget;
    if (!(share > 0.0) || !Krate::DSP::detail::isFinite(share)) return 0.0f;  // negative / zero share
    return sanitizeFrameFloat(share / (1.0 + share));
}

/// C-2 clause 5. Writes linkCount, linkA/B, linkStrength; zero-fills [linkCount, kMaxFrameLinks).
inline void selectStrongestLinks(const Krate::DSP::EcosystemEngine& eco,
                                 std::span<std::uint16_t, Krate::DSP::EcosystemEngine::kMaxPairs> scratch,
                                 EcosystemFrame& frame) noexcept;
}  // namespace Vorago
```

**Algorithm for `selectStrongestLinks`.** The cost is O(n), with n ≤ `kMaxPairs` = 1128, and it allocates nothing.

1. `n = min(eco.getPairInteractionCount(), kMaxPairs)`.
2. Compact: for `p < n`, read `f = eco.getPairFlow(p)`. If `f != 0.0 && detail::isFinite(f)`, store
   `scratch[m++] = uint16(p)`. This excludes exact zeros, as C-2 clause 5 requires. It also skips a
   non-finite flow, which only the stage-1 trap could produce, so that FR-027 holds without calling a
   `NaN` "strongest".
3. `k = min(m, kMaxFrameLinks)`. If `m > k`:
   `std::nth_element(scratch.begin(), scratch.begin() + k, scratch.begin() + m, stronger)`, where
   `stronger(a, b) = |flow(a)| > |flow(b)|`. That is a strict weak order over finite doubles. `nth_element`
   is in place, allocates nothing, and is deterministic for identical input on one standard library, which
   SC-010 relies on.
4. For `l < k`: `p = scratch[l]`; `linkA[l] = uint8(getPairAgentA(p))`; `linkB[l] = uint8(getPairAgentB(p))`;
   `linkStrength[l] = sanitizeFrameFloat(std::fabs(getPairFlow(p)))`.
5. Zero `linkA/linkB/linkStrength` over `[k, 64)`, then set `linkCount = uint8(k)`.

The order within the carried set is unspecified (spec C-2 clause 5). SC-007 asserts the *multiset of
strengths* and pair membership, so tie-breaking does not matter.

---

## 4. Processor — the producer (FR-021 – FR-027)

### 4.1 Header additions (`processor.h`)

```cpp
namespace Steinberg::Vst { class DataExchangeHandler; }   // Membrum processor.h:33-34 pattern
#include "processor/ecosystem_frame.h"

// public:
Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint* other) override;
Steinberg::tresult PLUGIN_API disconnect(Steinberg::Vst::IConnectionPoint* other) override;

// ---- Phase 13 test seams (never branched on by shipping logic except the two gates) ----
void setEcosystemFrameForcedForTest(bool on) noexcept { frameForced_ = on; }       // FR-026: gate + trigger
void setEcosystemFrameEnabledForTest(bool on) noexcept { frameEnabled_ = on; }     // plan D-2: gate only
[[nodiscard]] const EcosystemFrame& lastPublishedFrameForTest() const noexcept { return pendingFrame_; }
[[nodiscard]] std::uint64_t ecosystemFrameProcessCallCountForTest() const noexcept { return frameProcessCalls_; }
[[nodiscard]] std::uint64_t ecosystemFramePublishAttemptCountForTest() const noexcept { return framePublishAttempts_; }
[[nodiscard]] std::uint64_t ecosystemFrameSkippedBlockCountForTest() const noexcept { return frameSkippedBlocks_; }
[[nodiscard]] char ecosystemFocusRuleForTest() const noexcept { return frameFocusRule_; }

// private:
void publishEcosystemFrame() noexcept;
std::unique_ptr<Steinberg::Vst::DataExchangeHandler> dataExchangeHandler_;  // built in connect()
EcosystemFrame pendingFrame_{};                                              // 1072 B
std::array<std::uint16_t, Krate::DSP::EcosystemEngine::kMaxPairs> linkScratch_{};  // 2256 B
std::uint32_t frameSequence_ = 0;
std::uint64_t frameLastStep_ = 0;
std::size_t frameFocusVoice_ = 0;       // last EVALUATED focus (rule (b) reads it)
std::size_t frameLastFilledFocus_ = 0;  // focus at the last FILL (trigger (ii))
std::size_t frameLastAgentCount_ = 0;   // agentCount at the last FILL (trigger (ii))
std::atomic<bool> frameResyncPending_{true};  // trigger (iii): set true by connect()
char frameFocusRule_ = 'c';
bool frameForced_ = false;
bool frameEnabled_ = false;
std::uint64_t frameProcessCalls_ = 0, framePublishAttempts_ = 0, frameSkippedBlocks_ = 0;
static constexpr float kEcosystemFrameSilenceLevel = 1.0e-4f;  // Seraphis kCloudFrameSilenceLevel
```

The size grows by about 3.4 KiB, and the `sizeof(Processor) < 64 KiB` assert (`processor.h:280`) must still
hold. T-step 4 checks it by compiling.

`frameResyncPending_` is atomic because `connect()` runs on the host's UI thread. The existing flags use the
same pattern: `forcePushPending_` (`processor.h:265`) is also an atomic written off the audio thread.

### 4.2 `connect` / `disconnect` / `setActive` (`processor.cpp`)

`connect` and `disconnect` are copied from Seraphis `processor.cpp:1194-1220`. The config is
`blockSize = sizeof(EcosystemFrame)`, `numBlocks = 4`, `alignment = 32`,
`userContextID = kEcosystemFrameUserContextId`. After `onConnect`, `connect` also calls
`frameResyncPending_.store(true, relaxed)`. `disconnect` calls `onDisconnect(other)` and then
`dataExchangeHandler_.reset()`, which closes the gate again.

`setActive(true)`: if the handler exists, call `dataExchangeHandler_->onActivate(processSetup)`. `processSetup`
is the `AudioEffect` member that `setupProcessing` filled (`vstaudioeffect.cpp:151-158`, and Vorago's
`setupProcessing` calls the base at `processor.cpp:219`), so no second copy of the rate or block size is
kept. `setActive(false)`: if the handler exists, call `onDeactivate()` **before** the existing
release/silence block. Both calls run on the host thread with audio stopped. The fallback allocation in
`onActivate` (`dataexchange.cpp:76-105`) is therefore off the audio thread, as the spec's edge-case list says.

### 4.3 `publishEcosystemFrame()` — body order (normative)

It is called exactly once, between `numPedalPoints_ = 0;` (`:391`) and `data.outputs[0].silenceFlags = 0;`
(`:393`). No early-return path reaches it (FR-022, SC-008).

```
0. GATE   if (dataExchangeHandler_ == nullptr && !frameForced_ && !frameEnabled_) return;
1.        ++frameProcessCalls_;                                   // C-2 clause 7, counter 1
2. FOCUS  (C-2 clause 4, over VoragoEngine::kMaxVoices, EVERY evaluation - 6 reads)
            best = none
            for v in [0, kMaxVoices): if getVoiceState(v) != Idle and serial(v) > best.serial -> best = v
            if best exists:                        focus = best;              rule = 'a'
            elif getVoiceLevel(frameFocusVoice_) > kEcosystemFrameSilenceLevel:
                                                   focus = frameFocusVoice_;  rule = 'b'
            else:                                  focus = 0;                 rule = 'c'
            frameFocusVoice_ = focus; frameFocusRule_ = rule;
3. HABITAT eco = engine_->getVoice(focus).ecosystem();
           agentCount = (rule == 'c') ? 0 : eco.getAgentCount();
           step       = (rule == 'c') ? 0 : eco.getControlStepCount();
4. TRIGGER resync = frameResyncPending_.exchange(false, relaxed);
           fire = frameForced_ || resync || step != frameLastStep_
                  || focus != frameLastFilledFocus_ || agentCount != frameLastAgentCount_;
           if (!fire) return;                                   // no fill, no queue write (Q4)
5.        ++framePublishAttempts_;                               // counter 2
           frameLastStep_ = step; frameLastFilledFocus_ = focus; frameLastAgentCount_ = agentCount;
6. FILL   f.sequence = ++frameSequence_;
           f.activeVoices = uint8(min(getActiveVoiceCount(), 255));
           f.focusVoice = uint8(focus); f.agentCount = uint8(agentCount);
           f.voiceLevel = sanitizeFrameFloat(getVoiceLevel(focus));
           for i < agentCount:
               agentX[i]  = sanitizeFrameFloat(eco.getAgentPositionX(i));
               agentY[i]  = sanitizeFrameFloat(eco.getAgentPositionY(i));
               agentGlow[i] = ecosystemEnergyGlow(eco.getAgentEnergy(i), agentCount, eco.getEnergyBudget());
               agentKind[i] = uint8(eco.getAgentKind(i));
               agentDormant[i] = eco.isAgentDormant(i) ? 1 : 0;
           zero agentX/Y/Glow/Kind/Dormant over [agentCount, 48)
           if agentCount > 0: selectStrongestLinks(eco, linkScratch_, f);
                              f.linkFlowScale = sanitizeFrameFloat(eco.getEnergyBudget() / double(agentCount));
           else:              zero links; f.linkCount = 0; f.linkFlowScale = 0.0f;
7. TRANSPORT (Seraphis processor.cpp:4210-4225, verbatim shape)
           if handler == nullptr: return;          // seam-only path: frame stays in pendingFrame_
           block = handler->getCurrentOrNewBlock();
           if invalid id || data == nullptr || size < sizeof(EcosystemFrame): ++frameSkippedBlocks_; return;
           std::memcpy(block.data, &pendingFrame_, sizeof(EcosystemFrame)); handler->sendCurrentBlock();
```

Notes:

- **Rule (c) takes the step from voice 0 as 0.** Slot 0 is Idle in rule (c), so its counter does not advance
  anyway. Using a sentinel means that a re-prepare (`clearCounters`, `:2116`) cannot cause a spurious fill
  while nothing is sounding.
- **The trigger compares, it does not test "greater than"** (`!=`). `reset()`, `setSeed()` and a live
  reseed (`rewindIdleVoices`) all send the step count back to 0 (§1.1), and that is a habitat change that
  must be drawn.
- **Per-call cost.** When the trigger misses, the call costs 6 state reads, up to 6 serial reads and 3
  compares. When it fills, it costs 32 × 5 getter reads, one `std::exp`-free glow per agent (one divide),
  an O(pairCount) compaction over about 34 pairs at the product defaults, and one 1072-byte memcpy. There
  are no transcendentals and no allocation.
- `activeVoices` is clamped to 255 even though `kMaxVoices = 6`, so that the `uint8` cast is not a narrowing
  that clang-tidy could flag.

### 4.4 Why two seams (plan D-2), and the seam-free handler fixture

FR-026 requires the forced seam to force the trigger, so that SC-012 measures a full fill on every call.
Under that seam, every call publishes, so SC-008's *"32-sample buffers over one simulation step's worth of
samples give exactly 1 publish attempt"* is not observable. `setEcosystemFrameEnabledForTest(true)` opens the
gate while keeping the natural trigger, which is what the SC-008 cadence arithmetic needs. It is read at
step 0 only, so the shipping code path is unchanged.

**The seams are not the only coverage of the handler.** `ProcessorFixture` initializes with a null host
context (`vorago_test_fixture.h:186`), so it cannot open a queue. A second fixture, `ConnectedFixture`
(test-local, in `ecosystem_frame_test.cpp`), follows the Innexus `PipelineFixture` shape
(`test_data_exchange_pipeline.cpp:108-163`): `HostApplication host`, `proc.initialize(&host)`,
`proc.connect(peer)`, `setupProcessing`, `setActive(true)`. `HostApplication` does not implement
`IDataExchangeHandler`, so `onConnect` leaves the exchange handler null and the fallback `MessageHandler` is
used (`dataexchange.cpp:212-230`, `:320-325`). That fallback is deterministic **without** a message pump:
`openQueue` notifies `"DataExchangeQueueOpened"` (with `UserContextID` and `BlockSize`) synchronously on the
calling thread (`:77-105`), and an unpumped queue accepts exactly `numBlocks = 4` sends before `lockBlock`
returns `nullptr` (`:137-172`). The timer only moves data (`:174-190`) and is never pumped here, so no
platform code is needed. The peer is either a test-local recording `IConnectionPoint` (it copies the message
ID and the two int attributes out in `notify`, because the message is released after the call) or a real
`Vorago::Controller`. The one platform caveat is `Timer::create`, which returns `nullptr` on Linux without an
injected factory (`base/source/timer.cpp:339-353`), so `openQueue` fails there. The connected arms
therefore check a `queueOpened` flag first: on `SMTG_OS_LINUX` a closed queue is `WARN`-recorded and the
queue-dependent assertions are skipped; on Windows and macOS it is a `REQUIRE` failure. The arms are never
omitted, and the gate/connect/disconnect assertions that need no open queue run on all three OSes (R-15).

---

## 5. Controller (FR-040 – FR-043, FR-070 – FR-073, FR-080 – FR-081)

### 5.1 Header (`controller.h`) — additions

```cpp
#include "processor/ecosystem_frame.h"                         // POD only (FR-020)
#include "public.sdk/source/vst/utility/dataexchange.h"        // DataExchangeReceiverHandler
#include "pluginterfaces/vst/ivstdataexchange.h"

namespace Krate::Plugins { class PresetBrowserView; class ArcKnob; }
namespace VSTGUI { class CTextLabel; }
namespace Vorago::UI { class EcosystemView; }

class Controller : public Steinberg::Vst::EditControllerEx1,
                   public Steinberg::Vst::IMidiMapping,
                   public Steinberg::Vst::IDataExchangeReceiver,      // NEW
                   public VSTGUI::VST3EditorDelegate {
public:
    // IDataExchangeReceiver (FR-040)
    void PLUGIN_API queueOpened(Steinberg::Vst::DataExchangeUserContextID, Steinberg::uint32,
                                Steinberg::TBool& dispatchOnBackgroundThread) override;
    void PLUGIN_API queueClosed(Steinberg::Vst::DataExchangeUserContextID) override;
    void PLUGIN_API onDataExchangeBlocksReceived(Steinberg::Vst::DataExchangeUserContextID userContextID,
                                                 Steinberg::uint32 numBlocks,
                                                 Steinberg::Vst::DataExchangeBlock* blocks,
                                                 Steinberg::TBool onBackgroundThread) override;
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override;

    // IDependent (FR-081): EditControllerEx1::update is called first (vsteditcontroller.cpp:455-463)
    void PLUGIN_API update(Steinberg::FUnknown* changedUnknown, Steinberg::int32 message) override;

    // VST3EditorDelegate (FR-042, FR-043, FR-071)
    VSTGUI::CView* createCustomView(VSTGUI::UTF8StringPtr name, const VSTGUI::UIAttributes&,
                                    const VSTGUI::IUIDescription*, VSTGUI::VST3Editor*) override;
    VSTGUI::IController* createSubController(VSTGUI::UTF8StringPtr name, const VSTGUI::IUIDescription*,
                                             VSTGUI::VST3Editor*) override;
    void didOpen(VSTGUI::VST3Editor* editor) override;
    void willClose(VSTGUI::VST3Editor* editor) override;

    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::Vst::IMidiMapping)
        DEF_INTERFACE(Steinberg::Vst::IDataExchangeReceiver)
    END_DEFINE_INTERFACES(EditControllerEx1)

    // Session surface the sub-controller drives (C-5; never a ParamID)
    [[nodiscard]] int activePage() const noexcept { return activePage_; }
    void setActivePage(int page) noexcept;                     // clamps [0, 6]
    void openPresetBrowser();
    void registerGravityViews(Krate::Plugins::ArcKnob* knob, VSTGUI::CTextLabel* label) noexcept;
    void refreshGravityDisplay() noexcept;                     // FR-080

    // Test seams
    [[nodiscard]] const EcosystemFrame& cachedEcosystemFrame() const noexcept { return cachedFrame_; }
    [[nodiscard]] const UI::EcosystemView* ecosystemViewForTest() const noexcept { return ecosystemView_; }
    [[nodiscard]] bool gravityDisplayInertForTest() const noexcept { return gravityInert_; }
    [[nodiscard]] const Krate::Plugins::PresetBrowserView* presetBrowserViewForTest() const noexcept;

    // Preset providers (FR-072, FR-073) - public for SC-023
    [[nodiscard]] Steinberg::IBStream* createComponentStateStream();          // caller owns (Ruinae shape)
    bool loadComponentStateWithEdits(Steinberg::IBStream* state);

private:
    template <typename SetParam> Steinberg::tresult applyStateStream(Steinberg::IBStream*, SetParam&&);
    EcosystemFrame cachedFrame_{};
    Steinberg::Vst::DataExchangeReceiverHandler dataExchangeReceiver_{this};
    UI::EcosystemView* ecosystemView_ = nullptr;                  // frame-owned; zeroed in willClose
    Krate::Plugins::PresetBrowserView* presetBrowserView_ = nullptr;
    VSTGUI::VST3Editor* activeEditor_ = nullptr;
    Krate::Plugins::ArcKnob* gravityKnob_ = nullptr;
    VSTGUI::CTextLabel* gravityLabel_ = nullptr;
    Steinberg::Vst::Parameter* anchorParamObserved_ = nullptr;   // non-null only while a dependent
    int activePage_ = 0;
    bool gravityInert_ = false;
};
```

`createComponentStateStream`, `openPresetBrowser`, `refreshGravityDisplay`, `setActivePage`,
`registerGravityViews`, `applyStateStream`, `loadComponentStateWithEdits` and `activePage` are **member
functions of `Vorago::Controller`**. Ruinae, Disrumpo, Gradus and Innexus declare `openPresetBrowser` /
`createComponentStateStream` on their own `Controller` classes in their own namespaces. That is a
per-class member name, not an ODR hazard (sweep in §11).

**Header banner (FR-041).** The banner at `controller.h:12-13` (*"No createCustomView / verifyView: stock
views only in Phase 11 (FR-055)"*) is rewritten to describe the Phase 13 state: the controller is the
`IDataExchangeReceiver` consumer of the one-way `'VECO'` `EcosystemFrame` queue (cache only, UI thread);
`createCustomView` creates `EcosystemView` and the `PresetBrowserButton` (`OutlineBrowserButton`);
`createSubController` returns `VoragoPanelSubController`, which routes the session-tag controls (page strip,
preset button) and captures the Gravity views; and the controller is an `IDependent` observer of
`kResonanceAnchorModeId` for the Gravity display (FR-080/FR-081). The banner rewrite is part of
implementation step 6 (§13); it is a comment edit and is checked by reading the header in the compliance
pass (no test can observe a comment).

### 5.2 DataExchange consumer (FR-040, SC-013)

- `queueOpened` → `dispatchOnBackgroundThread = false`. `queueClosed` → no-op.
- `onDataExchangeBlocksReceived`: if `userContextID != kEcosystemFrameUserContextId`, return. Otherwise walk
  the blocks and memcpy the **last** one with `data != nullptr && size >= sizeof(EcosystemFrame)` into
  `cachedFrame_`. Membrum's loop is the model (`controller.cpp:1712-1726`); the context-ID check is added.
- `notify(msg)`: `if (msg && dataExchangeReceiver_.onMessage(msg)) return kResultOk;` otherwise
  `return EditControllerEx1::notify(msg);`.
- `cachedFrame_` is written and read on the UI thread only (dispatch on the main thread), so no lock is needed.

### 5.3 `VoragoPanelSubController` — `plugins/vorago/src/ui/panel_sub_controller.{h,cpp}` (new)

```cpp
namespace Vorago::UI {
inline constexpr std::int32_t kSessionTagBase     = 9000;  // never a ParamID (max registered is 1502)
inline constexpr std::int32_t kPresetButtonTag    = 9000;
inline constexpr std::int32_t kPageStripTag       = 9100;
inline constexpr int kVoragoPageCount = 7;

class VoragoPanelSubController : public VSTGUI::DelegationController {
public:
    VoragoPanelSubController(Vorago::Controller* owner, VSTGUI::IController* parent);
    void valueChanged(VSTGUI::CControl* control) override;
    void controlBeginEdit(VSTGUI::CControl* control) override;
    void controlEndEdit(VSTGUI::CControl* control) override;
    VSTGUI::CView* verifyView(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
                              const VSTGUI::IUIDescription* description) override;
private:
    Vorago::Controller* owner_;
    std::array<VSTGUI::CViewContainer*, kVoragoPageCount> pages_{};
    VSTGUI::CSegmentButton* pageStrip_ = nullptr;
    void applyPage(int page) noexcept;
};
}
```

**How it resolves the XML.** It reads the raw `UIAttributes` in `verifyView`, so it also works for
`createCustomView` views, which the view factory never decorates:

| XML marker | Action in `verifyView` |
|---|---|
| `uidesc-label="page-N"` on a `CViewContainer`, with N in 0..6 | `pages_[N] = container`; `setVisible(N == owner_->activePage())` |
| `session-tag="pages"` on the `CSegmentButton` | `setTag(kPageStripTag)`, `setListener(this)`, `setSelectedSegment(activePage)` |
| `custom-view-name="PresetBrowserButton"` (the created `OutlineBrowserButton`) | `setTag(kPresetButtonTag)`, `setListener(this)` |
| control tag `kMacroGravityId` (104) and `dynamic_cast<ArcKnob*>` | `owner_->registerGravityViews(knob, nullptr-or-label)` |
| `uidesc-label="macro-gravity-label"` on a `CTextLabel` | `owner_->registerGravityViews(nullptr-or-knob, label)` |
| anything else | `DelegationController::verifyView` (parent-null guarded, as Seraphis does) |

- `valueChanged`: tag `kPageStripTag` → `owner_->setActivePage(int(getSelectedSegment()))` + `applyPage`;
  tag `kPresetButtonTag` → `owner_->openPresetBrowser()`; any tag `>= kSessionTagBase` → **return without
  forwarding**; everything else → the parent (null-guarded).
- `controlBeginEdit` / `controlEndEdit`: session tags are **swallowed**. `CSegmentButton` and
  `OutlineBrowserButton::onMouseDown` both call `beginEdit`/`endEdit`, and forwarding them would reach
  `EditController::beginEdit(9000)` and break SC-018.
- `applyPage(k)`: `setVisible(i == k)` on each non-null `pages_[i]`, then `invalid()` on the page-area
  parent. No resize and no remove (FR-056). The concept band is not a descendant of any page.
- It is created by `Controller::createSubController("VoragoPanel")` and owned by the template root view
  (`uidescription.cpp:741-755`, as cited in Seraphis `controller.h:145-148`).

`setActivePage` clamps to `[0, kVoragoPageCount - 1]` and stores `activePage_`. It is never persisted and
never a parameter (FR-015, C-5).

### 5.4 `createCustomView` / `didOpen` / `willClose`

- `createCustomView`: build `viewRect` from the `origin`/`size` attributes (Seraphis `controller.cpp:453-460`).
  - `"EcosystemView"` → `ecosystemView_ = new UI::EcosystemView(viewRect, &cachedFrame_)`, return it.
  - `"PresetBrowserButton"` → `new Krate::Plugins::OutlineBrowserButton(viewRect, nullptr, -1, "PRESETS")`
    (plan D-1). The sub-controller gives it a listener and a tag.
  - anything else → `nullptr`.
- `didOpen(editor)`: `activeEditor_ = editor`. Build `presetBrowserView_` over `frame->getViewSize()` and
  `addView` it (Seraphis shape; R-2: no `SavePresetDialogView` — the browser's own save dialog is the save
  path, so `savePresetDialogView_` and `savePresetDialogViewForTest()` are **not** added). The tab labels are
  `makeVoragoPresetTabLabels()`, a new inline helper in `preset/vorago_preset_config.h` copied from
  Seraphis `:52-60`, which yields `{"All", "Drones"}`. Then call `anchorParamObserved_ = parameters.getParameter(kResonanceAnchorModeId)`,
  `anchorParamObserved_->addDependent(this)`, and `refreshGravityDisplay()` (FR-081 "value already current
  when the editor opens").
- `willClose`: if the browser is open, `presetBrowserView_->close()` (this unhooks the keyboard hook while
  the frame is alive, as in Seraphis `:529-533`); null it (R-2: there is no save dialog to close).
  `ecosystemView_ = nullptr`, `gravityKnob_ = gravityLabel_ = nullptr`, `activeEditor_ = nullptr`. If
  `anchorParamObserved_` is set, call `removeDependent(this)` and null it. The order puts the dependent
  removal before the view pointers could dangle.
- `openPresetBrowser()`: if `presetBrowserView_` is null or open, do nothing. Otherwise call `open()`.
  The browser's own `Save` path uses its internal dialog (`preset_browser_view.cpp:615`).
- `terminate()`: if `anchorParamObserved_` is still set (a host closed without `willClose`), call
  `removeDependent` first. Then the existing reset.

### 5.5 Preset providers (FR-072, FR-073, SC-023)

In `initialize()`, directly after `presetManager_` is built (`controller.cpp:81-82`):

```cpp
presetManager_->setStateProvider([this]() -> Steinberg::IBStream* { return createComponentStateStream(); });
presetManager_->setLoadProvider([this](Steinberg::IBStream* s, const Krate::Plugins::PresetInfo&) {
    return loadComponentStateWithEdits(s);
});
```

- `createComponentStateStream()` is Ruinae's body verbatim (`controller_presets.cpp:372-385`):
  `FUnknownPtr<IComponent>(getComponentHandler())`, `new MemoryStream`, `getState`, seek to 0. It returns
  null when the host does not expose `IComponent`. Ownership passes to `PresetManager` (`preset_manager.cpp:290`).
- **Refactor `setComponentState` without changing it (FR-073).** The body at `:96-157` moves into
  `template <typename SetParam> tresult applyStateStream(IBStream*, SetParam&& setParam)`, with the
  `setParam` lambda at `:110` becoming the argument. The helpers are already templated on `SetParamFunc`.
  - `setComponentState(s)` → `applyStateStream(s, [this](ParamID id, double v){ setParamNormalized(id, v); })`.
    This is byte-for-byte the same behaviour, and SC-018 re-runs the Phase 12 state suites with no edits
    to prove it.
  - `loadComponentStateWithEdits(s)` → `applyStateStream(s, [this](ParamID id, double v){ beginEdit(id);
    setParamNormalized(id, v); performEdit(id, getParamNormalized(id)); endEdit(id); }) == kResultOk`.
    The version guard (`version > kCurrentStateVersion → kResultFalse`) is shared, so a future-version
    stream is rejected **before any setter runs**. Note that `performEdit` sends the **clamped** normalized
    value that `setParamNormalized` stored.
- **Hidden IDs 4 and 5 are not in the stream** (`plugin_ids.h:90-91`, FR-045 of Phase 12), so the load
  provider never edits them.

### 5.6 Gravity anchor-mode display (FR-080, FR-081, SC-022)

- `update(changedUnknown, message)`: call `EditControllerEx1::update(changedUnknown, message)` first. Then,
  if `anchorParamObserved_ != nullptr`, `message == IDependent::kChanged`, and `changedUnknown` is that
  parameter (compare via `FUnknownPtr<Vst::Parameter>` / `FCast`), call `refreshGravityDisplay()`.
- `refreshGravityDisplay()`:
  `idx = indexFromNormalized(getParamNormalized(kResonanceAnchorModeId), kNumResonanceAnchorModes)`;
  `gravityInert_ = (idx != kResonanceAnchorModeDefault)`, where the default *is* `Hybrid`
  (`resonance_params.h:57-58`). If `gravityKnob_` is set, call `setTooltipText(inert ? kGravityInertTip :
  kGravityTip)`. If `gravityLabel_` is set, call `setText(inert ? "Gravity (inert)" : "Gravity")` and
  `invalid()`. **It calls no `setAlphaValue`, `setMouseEnabled` or parameter write** (FR-080: no dimming).
- The tooltip strings are file-scope `constexpr const char*` in `controller.cpp`. `kGravityTip` is the same
  text the uidesc gives the knob (a single source: the uidesc attribute is the initial value, and the
  controller sets the same literal back).
- **Thread semantics:** this is synchronous on the thread that calls `setParamNormalized`, which is exactly the
  thread VST3Editor's own bound controls update on (`vst3editor.cpp:100-110`, `:174`). This is the reading
  FR-081's "synchronously" requires. See risk R-6.

---

## 6. `EcosystemView` — `plugins/vorago/src/ui/ecosystem_view.{h,cpp}` (new; FR-050, FR-051)

### 6.1 Class shape

```cpp
namespace Vorago::UI {
class EcosystemView : public VSTGUI::CView {
public:
    EcosystemView(const VSTGUI::CRect& size, const EcosystemFrame* frame);  // frame = controller cache
    ~EcosystemView() override;
    void draw(VSTGUI::CDrawContext* context) override;
    bool attached(VSTGUI::CView* parent) override;   // creates the 30 Hz timer
    bool removed(VSTGUI::CView* parent) override;    // stops + releases it
    [[nodiscard]] bool hasTimerForTest() const noexcept { return timer_ != nullptr; }
    /// The tick body, with an injected dt (SC-015). Returns the max fade alpha it computed, so a
    /// test can observe the clear predicate on a live view.
    [[nodiscard]] float onTimerForTest(float dtSeconds);

    struct Style { bool filled; float alpha; float radius; };
    struct Segment { VSTGUI::CPoint a, b; };
    struct Segments { std::array<Segment, 4> pieces{}; std::size_t count = 0; };
    using FadeTable = std::array<float, kMaxFrameAgents * kMaxFrameAgents>;   // upper triangle used

    static constexpr float kAgentOutlineMinAlpha = 0.25f;
    static constexpr float kAgentMinRadius = 2.0f, kAgentMaxRadius = 9.0f;   // px, 400-px habitat
    static constexpr float kLinkFadeSeconds = 0.6f;
    static constexpr float kLinkMaxWidth = 2.5f;
    static constexpr float kFadeFloor = 1.0f / 255.0f;
    static constexpr float kMaxTickSeconds = 0.25f;
    static constexpr std::uint32_t kTimerMs = 33;     // ~30 Hz (Membrum pad_grid_view.h:30-37)

    [[nodiscard]] static VSTGUI::CPoint mapToView(float x, float y, const VSTGUI::CRect& r) noexcept;
    [[nodiscard]] static Style agentStyle(float glow, bool dormant) noexcept;
    [[nodiscard]] static float linkAlpha(float strength, float flowScale) noexcept;
    [[nodiscard]] static Segments torusLinkSegments(float ax, float ay, float bx, float by,
                                                    const VSTGUI::CRect& r) noexcept;
    [[nodiscard]] static bool isDrawableLink(const EcosystemFrame& f, std::size_t l) noexcept;
    [[nodiscard]] static std::array<Segment, 6> emptyHabitatGridLines(const VSTGUI::CRect& r) noexcept;
    [[nodiscard]] static bool needsRedraw(std::uint32_t prevSeq, std::uint32_t seq, float maxFadeAlpha) noexcept;
    [[nodiscard]] static float habitatBrightness(float voiceLevel) noexcept;
    [[nodiscard]] static std::string activeVoicesText(std::uint8_t activeVoices) noexcept;
    /// C-6 clause 4. Returns the max alpha left in the table (the needsRedraw input).
    static float stepLinkFade(FadeTable& table, const EcosystemFrame& f, bool clear, float dtSeconds) noexcept;

    /// FR-051 draw contract as data (see §6.3). draw() only replays this list.
    enum class DrawKind : std::uint8_t { Grid, Link, Agent, Text };
    struct DrawItem {
        DrawKind kind = DrawKind::Grid;
        Segments segments{};            // Grid: 1 piece; Link: torusLinkSegments pieces
        VSTGUI::CPoint centre{};        // Agent centre / Text anchor
        float radius = 0.0f;            // Agent
        std::uint8_t colourIndex = 0;   // Agent: Kind 0..4; Link: 5 (eco-link); Grid: 6 (eco-grid); Text: 7 (text-dim)
        float alpha = 0.0f;
        float width = 0.0f;             // Link / Grid line width
        bool filled = false;            // Agent
        std::uint8_t agentA = 0, agentB = 0;   // Link endpoints / Agent index (A), for tests
        std::array<char, 4> text{};     // Text: activeVoicesText, NUL-terminated (<= 3 digits)
    };
    static constexpr std::size_t kMaxFadePairs = kMaxFrameAgents * (kMaxFrameAgents - 1) / 2;  // 1128
    static constexpr std::size_t kMaxDrawItems = 6 + kMaxFadePairs + kMaxFrameAgents + 1;     // 1183
    struct DrawList { std::array<DrawItem, kMaxDrawItems> items{}; std::size_t count = 0; };
    /// Pure, CFrame-free. Clears @p out, then appends: agentCount == 0 -> exactly the 6
    /// emptyHabitatGridLines and nothing else; otherwise one Link item per non-zero (i<j) fade
    /// entry with i, j < agentCount (alpha = e * habitatBrightness(voiceLevel), width =
    /// kLinkMaxWidth * e), then one Agent item per i < agentCount (alpha = agentStyle(..).alpha *
    /// habitatBrightness(voiceLevel)), then one Text item = activeVoicesText(activeVoices).
    static void buildDrawList(const EcosystemFrame& f, const FadeTable& fade, const VSTGUI::CRect& r,
                              DrawList& out) noexcept;
private:
    const EcosystemFrame* frame_;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
    FadeTable fade_{};
    std::unique_ptr<DrawList> drawList_;   // ~0.2 MB, allocated once in the ctor (UI thread), never per tick
    std::uint32_t lastSeq_ = 0;
    std::uint8_t lastFocus_ = 0, lastAgentCount_ = 0;
    bool haveSeen_ = false;
    std::chrono::steady_clock::time_point lastTick_{};
};
}
```

### 6.2 The math (normative)

| Function | Definition |
|---|---|
| `mapToView` | `(r.left + x·r.getWidth(), r.top + y·r.getHeight())` (C-6 clause 1) |
| `agentStyle(g, dormant)` | `g' = clamp(g, 0, 1)`; `alpha = kAgentOutlineMinAlpha + (1 − kAgentOutlineMinAlpha)·g'`; `radius = kAgentMinRadius + (kAgentMaxRadius − kAgentMinRadius)·sqrt(g')`; `filled = !dormant && g' > 0`. Both alpha and radius are monotonic non-decreasing in `g`. At `g = 0` the style is an outline with alpha 0.25. `dormant` means unfilled at every `g` (C-6 clause 2). |
| `linkAlpha(s, k)` | `(s > 0 && k > 0) ? s / (s + k) : 0` (C-6 clause 3). `s == k` gives exactly 0.5f. `s = 48k` gives 48/49 < 1. Width = `kLinkMaxWidth · linkAlpha`. |
| `habitatBrightness(v)` | `v <= 0 → 0; v >= 1 → 1; else clamp((20·log10(v) + 60) / 60, 0, 1)` (C-6 clause 7, R-3: dB-normalized, floor at −60 dB). `0.01 → 1/3`, `0.1 → 2/3`, `0.5 → 0.899660`. It is applied at draw time to every agent and link alpha, and is **not** stored in the fade table. |
| `activeVoicesText(n)` | `std::to_string(n)`. At most 3 digits, which sits inside every standard library's SSO buffer, so there is no heap allocation (FR-051). |
| `needsRedraw(p, s, m)` | `s != p || m > 0` |
| `isDrawableLink(f, l)` | `l < f.linkCount && f.linkA[l] < f.agentCount && f.linkB[l] < f.agentCount && f.linkA[l] != f.linkB[l]` (FR-051) |
| `emptyHabitatGridLines(r)` | vertical `x = r.left + k·w/4` and horizontal `y = r.top + k·h/4`, for k = 1, 2, 3 |

**`torusLinkSegments` (C-6 clause 3), in unit coordinates, then mapped:**

```
td(d) = d > 0.5 ? d − 1 : (d < −0.5 ? d + 1 : d)        // mirrors ecosystem_engine.h:1111-1119
dx = td(bx − ax), dy = td(by − ay)
ex = ax + dx, ey = ay + dy
sx = ex >= 1 ? +1 : (ex < 0 ? −1 : 0);   sy likewise
for tx in {0, −sx} (just {0} if sx == 0), ty in {0, −sy} (just {0} if sy == 0):
    clip segment (ax+tx, ay+ty) → (ex+tx, ey+ty) to [0,1]² with Liang–Barsky
    keep it if its clipped length > 1e-9, then map both ends with mapToView
```

The four translated copies tile the torus, so the clipped lengths sum to `|d|`. After mapping to the square
rect that is `|d|·width`, which is what SC-014 asserts (1 px). The sum is over the pieces that are
non-empty, so a corner wrap whose middle piece degenerates to a point simply yields fewer pieces, and none
is dropped.

**`stepLinkFade(table, f, clear, dt)` (C-6 clause 4):**

```
if clear: table.fill(0)
decay = exp(−clamp(dt, 0, kMaxTickSeconds) / kLinkFadeSeconds)
for every (i<j) entry: e *= decay; if e < kFadeFloor: e = 0
for l < f.linkCount with isDrawableLink(f, l):
    (i, j) = minmax(linkA, linkB); e_ij = max(e_ij, linkAlpha(linkStrength[l], f.linkFlowScale))
return max entry
```

Decay is applied **before** the present links are raised. This gives SC-015's two properties: a present
link is never below its own `linkAlpha`, and an absent link is `≤ exp(−t/0.6)` after `t` seconds.

### 6.3 Tick, draw and lifecycle

- `attached`: `CView::attached`; if that succeeded and there is no timer, create
  `timer_ = owned(new CVSTGUITimer([this](CVSTGUITimer*){ tick(); }, kTimerMs))` and set
  `lastTick_ = now()`. (Seraphis `cloud_view.cpp:386-394`.)
- `removed`: `timer_->stop(); timer_ = nullptr;`, then `CView::removed` (Seraphis `:396-402`). The lambda
  captures `this` only. It reads `frame_`, which points at controller-owned memory, and the timer is
  destroyed here, so it cannot outlive the view (C-6 clause 5).
- `tick()`: `dt = seconds since lastTick_`; `onTimerForTest(dt)`.
- `onTimerForTest(dt)`: `clear = haveSeen_ && (f.focusVoice != lastFocus_ || f.agentCount != lastAgentCount_)`;
  `maxA = stepLinkFade(fade_, *frame_, clear, dt)`; `if (!haveSeen_ || needsRedraw(lastSeq_, f.sequence, maxA)) invalid();`
  then record `lastSeq_`, `lastFocus_`, `lastAgentCount_`, set `haveSeen_ = true`, and **return `maxA`**.
  The fade step runs **before** the predicate, so the predicate sees the decayed maximum.
- **`buildDrawList` (pure) + `draw` (replay).** Every FR-051 draw decision is made in the static
  `buildDrawList(f, fade, r, out)` (§6.1), which touches no `CDrawContext` and no `CFrame`, so it is
  asserted directly (`Vorago_EcosystemView_DrawList`, §9.2):
  1. `out.count = 0`;
  2. if `agentCount == 0`: append the 6 `emptyHabitatGridLines(r)` as `Grid` items (colour 6) and return
     (C-6 clause 6), with no link, agent or text item;
  3. `b = habitatBrightness(f.voiceLevel)`;
  4. for each non-zero `(i<j)` fade entry with `i < agentCount && j < agentCount`: one `Link` item,
     `segments = torusLinkSegments(agent i, agent j, r)`, colour 5, alpha `e·b`, width `kLinkMaxWidth·e`.
     An entry whose index is `>= agentCount` produces no item. Frame links reach the fade table only through
     `stepLinkFade`, which admits them only via `isDrawableLink`, so an invalid frame link never becomes a
     fade entry;
  5. for each agent `i < agentCount`: `s = agentStyle(agentGlow[i], agentDormant[i] != 0)`; one `Agent` item
     at `mapToView(agentX[i], agentY[i], r)`, `radius = s.radius`, `filled = s.filled`,
     colour `agentKind[i]` (clamped to 0..4), alpha `s.alpha·b`;
  6. one `Text` item carrying `activeVoicesText(f.activeVoices)`.

  `draw(ctx)`: fill the background (`eco-bg`), call `buildDrawList(*frame_, fade_, getViewSize(), *drawList_)`,
  then replay the items in order: `Grid`/`Link` → `setFrameColor(palette[colour]` with the item alpha`)`,
  `setLineWidth(width)`, `drawLine` per piece; `Agent` → `drawEllipse` filled or stroked; `Text` →
  `drawString` at the top-left in `label-font`. `draw` makes **no** decision of its own beyond the colour
  lookup (the 8-entry palette is looked up once in the ctor from the named colours, with fixed fallbacks).
  Drawing uses `CDrawContext::drawLine`, `drawEllipse`, `drawString`, `setFrameColor`, `setFillColor` and
  `setLineWidth` only. No `CGraphicsPath` is used, and the draw list is preallocated, so this code adds no
  per-draw allocation.
- **Fade entries are keyed by agent index.** Links drawn from the fade table use the *current* frame's
  positions, so a faded link follows its agents as they move. Indices whose agent is `>= agentCount` are
  skipped. The table is cleared on focus or count change.

---

## 7. `editor.uidesc` — replaced wholesale (FR-001 – FR-006, FR-010 – FR-013, FR-055 – FR-056)

### 7.1 Resources

- **Colours**: the existing five (`bg`, `text`, `text-dim`, `track`, `accent`), plus `eco-bg`, `eco-grid`,
  `eco-link`, `eco-partial`, `eco-resonator`, `eco-noise`, `eco-feedback`, `eco-ghost`, `macro-arc`. They
  are defined as rgba literals, with no bitmaps (FR-005).
- **Fonts**: `label-font` (Arial 11), `macro-font` (Arial 13), `title-font` (Arial 16). Arial falls back on
  Linux through VSTGUI's font mapping, which is the same situation as the Phase 11 file.
- **Control tags**: the 14 existing names keep their values (`:20-33`, FR-012). 92 new names are added in
  ascending-ID order, each spelled as the `ParamID` enumerator minus its `k` prefix and `Id` suffix (for
  example `CloudRichness` = 200), so SC-003 can check them mechanically against `plugin_ids.h`. That makes
  **106 tags**, with none for 4 or 5.

### 7.2 Tree (window coordinates in comments; nested origins are local)

```
template "editor" CViewContainer 1100x760 (min = max), sub-controller="VoragoPanel", bg
├─ CViewContainer uidesc-label="header" origin 0,0 size 1100,36
│    CTextLabel "VORAGO" (title-font) · CView custom-view-name="PresetBrowserButton" 90x22
│    [label+CSlider MasterGain 120x16] [label+COptionMenu Polyphony 60x18]
│    [label+COptionMenu Seed 80x18] [label+CSlider OutputSaturation 120x16]
├─ CViewContainer uidesc-label="concept-band" origin 0,36 size 1100,400
│   ├─ CViewContainer uidesc-label="macros-left"  origin 0,0   size 350,400
│   │    6 cells 175x133: ArcKnob 96x96 at cell (40|214, 10) + CTextLabel below (mouse-enabled=false)
│   │    row-major: Darkness Age / Density Movement / Gravity Entropy
│   │    Gravity's CTextLabel carries uidesc-label="macro-gravity-label"
│   ├─ CView custom-view-name="EcosystemView" uidesc-label="ecosystem" origin 350,0 size 400,400
│   └─ CViewContainer uidesc-label="macros-right" origin 750,0 size 350,400
│        row-major: Pressure Weight / Fog Life / Depth Mass
├─ CSegmentButton uidesc-label="page-strip" session-tag="pages" origin 0,436 size 1100,28
│    segment-names="Cloud,Noise,Resonance,Body,Sub / Smear,Space,Life" (no control-tag)
└─ CViewContainer uidesc-label="page-area" origin 0,464 size 1100,296
     7 x CViewContainer uidesc-label="page-0".."page-6" origin 0,0 size 1100,296, transparent
```

Macro knob cells: in the left block, columns start at x = 0 and 175, and rows start at y = 0, 133 and 266.
Each knob sits at `(col + 40 | col + 39, row + 10)` with size 96 × 96, and its label is at `(col + 20, row + 110)`
with size 135 × 16. The right block uses the same local coordinates.

### 7.3 Page grid (the rule every page follows)

The page area is 1100 × 296. Cells are **68 px wide × 74 px tall**, which gives up to 16 columns and 4 rows.
The page title strip is omitted, because the segment button already names the page.

- **Continuous ID** → `ArcKnob` **44 × 44** at `(cellX + 12, cellY + 4)` (≤ 48, FR-004), with a label of
  `68 × 14` at `(cellX, cellY + 52)`.
- **List ID** → `COptionMenu` **96 × 18** spanning 1.5 cells, with its label above. Freeze (1115) and
  Ghost Event Triggers (1403) → `CCheckBox` 60 × 18. This is stock, so no second creator registration is
  needed (C-4 permits `CCheckBox`).

| Page | Rows (one row = one functional group) |
|---|---|
| 0 Cloud | r0: 200–206 (7 knobs) · r1: 1300, 1301 (Bloom) |
| 1 Noise | r0: 300, 301, 302 · r1–r4 are not available (4 rows max), so **columns = slots**: for slot s (0–3), a 4-cell-wide column group holds `Model` menu (310+s), `Type` menu (320+s), then knobs 330+s, 340+s, 350+s — laid out in rows 1–3 of that group. 3 + 4 × 5 = 23 |
| 2 Resonance | r0: 400, 401, 402, menu 403 · r1: 500, 501 · r2: menus 510–515 (6 × 1.5 cells) |
| 3 Body | r0: 1000–1003, menus 1004, 1005 · r1: menu 1200, knobs 1201–1206 |
| 4 Sub / Smear | r0: 600, 601, 610, 611, 612 · r1: 700, 701, 702 |
| 5 Space | r0: 1100–1107 · r1: 1108–1114, checkbox 1115 |
| 6 Life | r0: 800, 900 · r1: 1400, 1401, 1402, checkbox 1403 · r2: 1500, 1501, 1502 |

Every bound view carries a non-empty `tooltip` (FR-006). Every macro and page control has an untagged
`CTextLabel` sibling with `mouse-enabled="false"`. The page containers are **siblings** under `page-area`,
never a `UIViewSwitchContainer` (C-5, Seraphis R-10).

---

## 8. `entry.cpp`, `CLAUDE.md`, `CHANGELOG.md`

- `controller.h` (FR-041): rewrite the banner at `:12-13` as described in §5.1.
- `entry.cpp`: add `#include <ui/arc_knob.h>` (Seraphis `entry.cpp:34` form), which registers `ArcKnobCreator`.
  Rewrite the FR-018 banner (`:12-15`) to say that Phase 13 registers the shared `ArcKnob` here and that
  `EcosystemView` / the preset button are created by `createCustomView`, which needs no registration.
- **The test binary needs the creator too.** `vorago_tests` does not compile `entry.cpp`, so
  `ui/panel_sub_controller.cpp`, which `dynamic_cast`s to `ArcKnob` for the Gravity capture, includes
  `<ui/arc_knob.h>`. `gArcKnobCreator` is an `inline` variable (`arc_knob.h:716`), so one instance exists
  program-wide however many TUs include it.
- `plugins/vorago/CLAUDE.md` (FR-061): `ui/` holds `ecosystem_view`, `panel_sub_controller`; the frame data
  path (one-way, change-triggered, `'VECO'`); the `{4, 5}` allowlist; the page table of §7.3; the session-tag
  range `>= 9000`; and the rule that `ecosystem_frame_builder.h` is processor-only.
- `plugins/vorago/version.json` → `0.2.0` and `plugins/vorago/CHANGELOG.md` (FR-063, R-6): one
  `## [0.2.0] - <date>` entry above `[0.1.0]` with user-facing bullets for the Phase 12 parameter surface
  (the 92 engine parameters, the twelve macros now live, log tapers, seeds, sustain pedal, channel pressure,
  state v2) and the Phase 13 interface, the ecosystem view, the preset browser and the Gravity hint.

---

## 9. Test plan

Tags follow `[vorago]…`. Timed cases are `[.perf]` and run only via `node tools/run-cpu-tests.js vorago_tests`,
alone. Any case whose measured wall time exceeds about 15 s **and** whose assertions do not depend on the
toolchain is additionally tagged `[long]`. That decision is made from the first measured run, not guessed.
SC-011's RT arm, SC-013 and the layout and lifecycle cases are never tagged `[long]`.

### 9.1 `dsp_systems_tests` — `dsp/tests/unit/systems/ecosystem_engine_pair_accessors_test.cpp` (new)

**`EcosystemEngine_PairAccessors`** `[systems][ecosystem][pairs]` — SC-019:

- Fixture: `EcosystemEngine eng; eng.setSeed(0x5EED1234u); eng.prepare(48000.0, {.agentCount = 32});`.
  `stepSamples = eng.getStepIntervalChunks() * EcosystemEngine::kControlChunkSamples`.
- For each of the ⌈30 s / step⌉ steps:
  1. Snapshot `x, y, e` for every agent, plus `σ = double(getKernelSigma())`, `rate = double(getExchangeRate())`
     and `pred = double(getPredation())`.
  2. Call `processChunk(stepSamples)` and `REQUIRE(getControlStepCount() == prev + 1)`.
  3. Build the reference by recomputing, for every `i<j`, `d2` via a test-local copy of the two-compare
     `wrapDelta`. `EcosystemEngine::wrapDelta` (`:1111`) sits after `private:` (`:1042`), so the test
     cannot call it, and FR-030 forbids widening its access. The copy is verbatim, `d > 0.5 → d − 1`,
     `d < −0.5 → d + 1`. Then compute
     `inv = (2σ²>0) ? 1/(2σ²) : 0`, the pre-test `d2 > 2σ²·13.815510557964274·(1+1e-9)`, `w = std::exp(-d2 * inv)`
     and `w >= 1e-6`. These are the **same expressions and association** as `:1426-1444` and `:2037-2041`.
     Then `ref = rate * w * (ej - ei) * (1.0 - (2.0 * pred))`.
  4. Assert that the set of `(A, B)` equals the reference set, and that `A < B < getAgentCount()`. For each
     pair, `|getPairFlow(p) − ref| <= 1e-12 * max(|ref|, 1e-300)`.
- Section `PredationHalf`: `setPredation(0.5f)`. Every flow is exactly `0.0`, and `getPairInteractionCount() > 0`
  is asserted at least once.
- Section `OutOfRange`: `p = getPairInteractionCount()`, `p + 1`, `SIZE_MAX` all return `0 / 0 / 0.0`, as
  does an unprepared object at `p = 0`.
- **The TU goes in the `-fno-fast-math` list** (`dsp/tests/CMakeLists.txt`, near `:1022`). The reference
  must associate exactly as the engine. Header-only code is compiled inside this TU for both sides, so the
  flag keeps both sides IEEE together. The flag does not move any baseline, because the TU has no timing arm.
- FR-031 evidence: `git diff --stat dsp/tests` lists only this TU and its two CMake lines, and the whole of
  `dsp_systems_tests` runs green.

### 9.2 `vorago_tests` — new and edited TUs

| TU | TEST_CASE | Covers | Strategy |
|---|---|---|---|
| `unit/ecosystem_frame_builder_test.cpp` (new, **-fno-fast-math**) | `Vorago_EcosystemFrameLayout` | FR-020 | `static_assert`s are mirrored as `REQUIRE(sizeof == 1072)` and `offsetof(linkStrength) == 812` so the count reaches the test report |
| same | `Vorago_EcosystemEnergyGlow` | SC-006 function arm | budget 1, agentCount 32, `energy = share/32` for shares `{0, .5, 1, 2, 4, 8, 16, 48}`: strictly increasing after share 0; `== 0.5f` at share 1; `< 1.0f` at 48; `0.0f` at share −1; `0.0f` for a quiet-NaN energy built from bits through `volatile std::uint64_t` → `std::bit_cast<double>` |
| same | `Vorago_SanitizeFrameFloat` | SC-011 function arm, FR-027 | qNaN `0x7FF8000000000000`, sNaN `0x7FF0000000000001`, ±Inf, `1e300`, `-1e300` → exactly `0.0f`; `0.25`, `-3.5`, `FLT_MAX` → `static_cast<float>(v)` |
| same | `Vorago_SelectStrongestLinks` | SC-007 (b)(c)(d) | Directly prepared engine, stepped one step at a time (the §9.1 stepping). **(b)** `agentCount = 48`, `setKernelSigma(0.35f)`: `getPairInteractionCount() == 1128` on at least one step (it is every step: `d²max = 0.5 < cutDistSq ≈ 3.39`); reference = sort `|flow|` descending, drop zeros, take 64; compare the multisets of `linkStrength` against `static_cast<float>(|ref|)` exactly; every `(linkA, linkB)` is a recorded pair whose `|flow|` narrows to that strength; `linkCount == 64`. **(c)** `setPredation(0.5f)`: `linkCount == 0` on every step, with `pairCount > 0` seen. **(d)** `agentCount = 1`: `linkCount == 0`. Every arm: `linkA/B < agentCount`, `linkA != linkB`, and entries at or above `linkCount` are zero |
| `integration/ecosystem_frame_test.cpp` (new) | `Vorago_EcosystemFrame_DoesNotChangeAudio` | SC-001 | Two `ProcessorFixture`s, seed index 0, 512-sample blocks, note-on 48 at sample 0, note-off at 45 s, 60 s total. A: `setEcosystemFrameForcedForTest(true)`; B: off. `REQUIRE(capturedL_A == capturedL_B)` and the same for R, element by element with `==`. This is an in-binary A/B and no golden is stored. Non-vacuity: A's attempt counter equals A's process count (5625), and at least one frame has `agentCount > 0 && linkCount > 0` |
| same | `Vorago_EcosystemFrame_MatchesEngine` | SC-006 | Forced seam, one held note (48), 20 s, 512 blocks. After **every** block, focus `v = frame.focusVoice`; `eco = proc->engineForTest()->getVoice(v).ecosystem()`; each field is compared with `==` against the same getter, cast and function (§4.3 step 6); entries above the counts are `== 0`; `voiceLevel == getVoiceLevel(v)`; `activeVoices == getActiveVoiceCount()`; `linkFlowScale == sanitizeFrameFloat(getEnergyBudget()/agentCount)`. Non-vacuity: some frame has two agents with share ≥ 2, different energies and different `agentGlow`. If the render never produces that, the arm moves to a directly prepared engine (48 agents, `setExchangeRate(3.0f)`), the switch is WARNed, and the arm is never dropped. Also WARN-records the min/median/max of `voiceLevel` while held, as evidence for Open Question 3 |
| same | `Vorago_EcosystemFrame_LinksAreStrongest` | SC-007 (a) | Forced seam, shipped defaults, 20 s. After every block, the §9.2 reference is built from the focus voice's `eco` and the multiset comparison is run |
| same | `Vorago_EcosystemFrame_Cadence` | SC-008 | **Enabled seam** (plan D-2), 48 kHz, step = 512 samples. (1) process-call counter +1 for blocks with 0, 1 and 1024 events and for a 2048-sample block; +0 for each of the six early-return shapes (`processNoOutputs`, null `channelBuffers32`, one channel, `numSamples = 0`, null `outL`, unprepared fixture). (2) With the step phase aligned by first rendering 512 × k samples, then 16 × 32-sample calls → **attempts +1**. (3) One 2048-sample call → +1. (4) Seven × 32-sample calls inside one step with no advance → +0, then the call that crosses the boundary → +1. (5) With notes on slots 0–3, a mid-step `kPolyphonyId` → "1" change: the next call → +1, and `focusVoice` changes. (6) Gate closed (seam off, no handler) → both counters +0 across 100 calls |
| same | `Vorago_EcosystemFrame_FocusVoice` | SC-009 | Forced seam. Each row is a `SECTION` with a **short release** (`kEnvelopeReleaseId` set to the plain value 200 ms via `offsetLogToNormalized`), so that retirement fits the 60 s bound (risk R-4). Rows are as in the spec table; each asserts `focusVoice` and `ecosystemFocusRuleForTest()`. "All released": the loop runs until `getVoiceState(v) == Idle` with `REQUIRE(elapsed <= 60 s)`, and asserts `'b'` is never seen. The orphan row asserts rule `'b'` on at least one frame after the shrink, then `'c'` with `agentCount == 0` |
| same | `Vorago_EcosystemFrame_Determinism` | SC-010 | Two forced fixtures, same seed and same script (note-on 48 + 55 at 0, 2000 × 512 blocks): every frame compared field by field with `==` (arrays via `std::equal`, never `memcmp` on floats). Seed index 0 vs 1: some `agentX` differs within 200 blocks |
| same | `Vorago_EcosystemFrame_AllocationFree` | SC-011 | Forced seam, polyphony 2 (steals), a pre-built `Krate::Test::EventList` and `ParameterChanges` whose capacity is reserved up front (the automation_rt_test pattern). 2000 blocks with seeded random note-on/off, CC64 points on ID 4 and automation of 201, 206 and 403, **each block under `TestHelpers::AllocationScope`** → 0 allocations. After every block, every frame float passes `Krate::DSP::detail::isFinite`. **Connected arm (no seam)** — `ConnectedFixture` (§4.4) with a real `Vorago::Controller` as the peer (`ctrl.initialize(&host)`, both sides `connect`ed, Innexus `PipelineFixture` order), then `setupProcessing` + `setActive(true)` **outside** any scope (the fallback `openQueue` allocates there, on the host thread, by design). Held note 48, then ≥ 64 × 512-sample blocks at 48 kHz (every block crosses a step boundary), each block under `TestHelpers::AllocationScope` → 0 allocations. This runs step 7 for real: lock, memcpy and send on the first 4 fills, then the queue-full skipped-block branch (no pump). Non-vacuity: `ecosystemFramePublishAttemptCountForTest()` advanced by ≥ 64 and `ecosystemFrameSkippedBlockCountForTest()` advanced by ≥ 60 (so both the send and the skip paths ran inside a scope). Queue-open precondition per §4.4 (Linux: WARN + skip the queue-dependent asserts; the 0-allocation assert still runs) |
| same | `Vorago_EcosystemFrame_HandlerLifecycle` | FR-021, FR-026 counters, C-2 clause 1, C-2 clause 2 trigger (iii), C-2 clause 7 | `ConnectedFixture` with a **recording `IConnectionPoint`** peer (records message ID, `UserContextID`, `BlockSize`). No seam is set anywhere in this case. (1) Prepared and active but not yet connected: 10 process calls → process-call counter +0 (gate closed). (2) `setActive(false)`, `connect(peer)`, `setActive(true)` → exactly one `"DataExchangeQueueOpened"` recorded, with `UserContextID == 0x5645434F` and `BlockSize == 1072` (`sizeof(EcosystemFrame)`); this fails if `onActivate` is not called from `setActive` or if the config callback is wrong. (3) **Trigger (iii):** with no note (rule `'c'`, step 0, agentCount 0 — equal to the initial `frameLast*` values) one call → attempts +1, a second call → +0. **Resync on reconnect:** `setActive(false)`, `disconnect(peer)`, `connect(peer)`, `setActive(true)`, with nothing else changed → the next call → attempts +1, the one after → +0 (proves `connect()` re-arms `frameResyncPending_`; the member's initial `true` alone cannot produce this second +1). The recorder is cleared here. (4) Held note 48, 512-sample blocks at 48 kHz (one step per block): counting every fill since the most recent queue open (the resync fill of (3) included), fills 1–4 send (skipped +0), fill 5 → `ecosystemFrameSkippedBlockCountForTest()` +1, fill 6 → +1 again (no pump, so nothing is returned to the queue). (5) `setActive(false)` → exactly one `"DataExchangeQueueClosed"` with `UserContextID == 0x5645434F`. (6) `disconnect(peer)`, then `setActive(true)` again and 10 note-holding calls → process-call, attempt and skipped counters all +0 (the handler is reset, so the gate is closed). Queue-open precondition per §4.4: on Linux, (2), (4) and (5) are WARN-skipped; (1), (3) including the reconnect resync, and (6) run everywhere |
| `integration/processor_cpu_test.cpp` (edited) | `Vorago_ProcessorCpu` | SC-012 | New `fixturePF` with `setEcosystemFrameForcedForTest(true)`; same notes and defaults sync as P. The trial loop becomes **P, PF, D** per trial (all interleaved, `measureTrio` ruling). `REQUIRE(pfBestNs <= kWrapperOverheadCeiling * dBestNs)`; WARN `PF/P`; the existing P gate is unchanged. The banner gains a "Phase 13 (SC-012)" paragraph |
| same | `Vorago_SelectStrongestLinks_WorstCase` `[.perf]` | SC-007 (e) | The (b) configuration stepped until `pairCount == 1128`. Best of 16 trials × 1000 calls to `selectStrongestLinks`. WARN ns/call and ns/call ÷ (512/48000 s). Not gated. It lives here and not in the `-fno-fast-math` TU, so that its figure matches the shipped build flags |
| `unit/controller/ecosystem_consumer_test.cpp` (new) | `Vorago_Controller_ConsumesEcosystemFrame` | SC-013, FR-040 | `Controller` initialized. `queueOpened` sets the flag false. Three valid blocks with sequences 1, 2, 3 in one call → cache `sequence == 3`. A `sizeof − 1` block → cache unchanged. A full block with context `0x12345678` → unchanged. The IMessage fallback is a `HostMessage` with ID `"DataExchange"`, int `"UserContextID"` = `'VECO'` and binary `"Data"` = a frame with sequence 7; `notify` → `kResultOk` and cache `sequence == 7`. **Forwarding probe:** a `HostMessage` with ID `"TextMessage"` and a `"Text"` string attribute — `ComponentBase::notify` routes it to `receiveText`, which returns `kResultOk` (`vstcomponentbase.cpp:91-107`, `:153-156`), whereas an override that swallowed non-DataExchange messages would return something else. Assert `notify == kResultOk` **and** the cache is unchanged. A `"TextMessage"` is not a DataExchange ID, so `onMessage` returns false and only the base can produce that `kResultOk` |
| `unit/controller/ecosystem_view_test.cpp` (new, `[lifecycle]` on the attach case) | `Vorago_EcosystemView_Mapping` | SC-014 | All static functions, no `CFrame`. `torusLinkSegments` on a 400 × 400 rect at `(350, 36)`: the four spec cases, with piece counts 1, 2, 2 and ≥ 2 (corner, **every** non-empty piece kept); each piece inside the rect (±1e-9); total length `|d|·400 ± 1 px`. Plus every row of SC-014 as written |
| same | `Vorago_EcosystemView_LinkFade` | SC-015 | `stepLinkFade` over a hand-built frame: present once, then absent for `t = n·dt` → `≤ exp(-t/0.6) + 1e-6`; exactly 0 after dropping below 1/255; a present link `≥ linkAlpha`; `dt = 1.0` behaves as 0.25; `clear = true` on a focus change and on an `agentCount` change zeroes the table. **Live-view arm:** a view constructed over a test-owned `EcosystemFrame` (no attach needed). Frame A (focus 0, agentCount 8, one drawable link) → `onTimerForTest(0.0f)` returns `> 0`. Same frame, `sequence` +1, no links → `onTimerForTest(1e-6f)` returns `≥ 0.99 ×` the previous value (decay only, the non-vacuity control). Then `focusVoice = 1`, no links → `onTimerForTest(1e-6f)` returns **exactly 0** (cleared, not decayed). Repeat from a fresh link with `agentCount` 8 → 9 instead of the focus change → exactly 0 |
| same | `Vorago_EcosystemView_DrawList` | FR-051, C-6 clauses 2, 3, 6, 7, 8 | Static `buildDrawList` only, 400 × 400 rect at `(350, 36)`, `DrawList` on the heap. **(a)** A frame with `agentCount = 8`, `voiceLevel = 0.5f`, mixed glow/dormant/kinds, and a fade table built by `stepLinkFade` from 3 drawable links: every `Agent` item's alpha `== agentStyle(glow, dormant).alpha * 0.5f`, `filled == agentStyle(...).filled`, `colourIndex == agentKind`, centre `== mapToView`; every `Link` item's alpha `== fade[i,j] * 0.5f` and width `== kLinkMaxWidth * fade[i,j]`; 8 agent items and 3 link items. Repeated at `voiceLevel = 0` (every alpha 0) and `2.0` (alpha unscaled). **(b)** `agentCount = 0` (with stale fade entries and links present in the frame) → exactly 6 items, all `Grid`, equal to `emptyHabitatGridLines(r)`. **(c)** A frame link with `linkB = agentCount` and one with `linkA == linkB`, passed through `stepLinkFade`, then `buildDrawList` → no `Link` item for either; a hand-set fade entry `(i=40, j=45)` with `agentCount = 32` → no `Link` item. **(d)** Exactly one `Text` item, and its text `== activeVoicesText(frame.activeVoices)` for `activeVoices` ∈ {0, 3, 6} |
| same | `Vorago_EcosystemView_AttachRemove` `[lifecycle]` | SC-016 (b) | `ensureVstguiInitialized()`; `auto parent = makeOwned<CViewContainer>(rect)`; heap `EcosystemFrame` stand-in; 10 × `view->attached(parent)` → `hasTimerForTest()`, then `view->removed(parent)` → `!hasTimerForTest()`. Variant: remove, delete the stand-in, sleep 150 ms in 10 ms slices, then destroy the view. There must be no access, which ASan checks. See risk R-5 for why no platform pump is run |
| `unit/controller/editor_layout_test.cpp` (new) | `Vorago_UidescBindsEverySurfaceId` | SC-002 | `unreachableParams(xml, all108, {4, 5})` is empty. The multiset of `control-tag="…"` attribute values maps to 106 distinct registered IDs, each once. 4 and 5 appear 0 times. Every element with `control-tag` has a non-empty `tooltip` |
| same | `Vorago_UidescTagTable` | SC-003 | `extractControlTagMap(xml)`: every value is in `VoragoTest::kExpectedParams`; every used `control-tag` name is declared; the 14 Phase 11 names keep their values |
| same | `Vorago_UidescViewClassRule` | SC-004 | A stack-based element scan in an anonymous namespace (`<view`, `<template`, `</view>`, `/>`) → per bound view its `class`. Continuous IDs (from `kExpectedParams`, 84 rows) → `ArcKnob`, or `CSlider` for 0 and 3; the 22 list rows → `COptionMenu`, or `CCheckBox` for 1115/1403; 100–111 → `ArcKnob` |
| same | `Vorago_UidescLayout` | SC-005 | The same scan, resolving each node's rect to window coordinates by summing ancestor origins. It then checks the template size and min/max, the 7 labelled regions' rects, the macro sizes ≥ 80 × 80 inside their blocks, page knob sizes ≤ 48 × 48 inside `page-area`, the ecosystem rect `(350, 36, 750, 436)`, and a sibling label for every macro and page control (no `control-tag`, `mouse-enabled="false"`). **FR-003 order:** the bound IDs inside `macros-left` are exactly 100–105 and inside `macros-right` exactly 106–111; sorting each block's knobs by resolved `(top, left)` (row-major) yields 100, 101, …, 105 and 106, …, 111. **FR-055 ID sets:** for each `page-N` container, the set of `control-tag` IDs among its descendants equals C-4's row for that page (page-0 Cloud {200–206, 1300, 1301} … page-6 Life {800, 900, 1400–1403, 1500–1502}, transcribed as a table in the test); the union is 94 and no page ID appears outside `page-area`. **FR-005 class whitelist (all views, bound or not):** every `class=` value is in {`CViewContainer`, `CTextLabel`, `CSlider`, `COptionMenu`, `CCheckBox`, `CSegmentButton`, `CView`, `ArcKnob`}, and every `custom-view-name=` value is in {`EcosystemView`, `PresetBrowserButton`} |
| same | `Vorago_Editor_PageSwitch` | SC-017 | Headless `VST3Editor` (the `EditorBindsFourteenControls` open idiom). Find the 7 page containers via the sub-controller's captured pointers (walk the built tree for `CViewContainer`s whose `labelAttrID` attribute is `page-N`), the segment button and the `EcosystemView`. For k in 0..6: `setSelectedSegment(k)` + `valueChanged()` → exactly container k visible; ecosystem view and 12 macros visible, at unchanged rects. Close, reopen → the page is still k. **Default (FR-055):** on the **first** open of a fresh controller, before any segment change, exactly `page-0` is visible and `getSelectedSegment() == 0` |
| same | `Vorago_Editor_PageSwitchIsSessionOnly` | SC-018 (session arm) | A recording `IComponentHandler` (the preset_load_test stub shape) installed on the controller. The scripted page sequence covers all 7, with repeats, and returns to 0. **Every** change is driven on the real built `CSegmentButton` as `control->beginEdit(); setSelectedSegment(k); control->valueChanged(); control->endEdit();`, so the sub-controller's `controlBeginEdit`/`controlEndEdit` receive the session tag (the listener is the only path to `EditController::beginEdit`, `ccontrol.cpp:186-197`, `vst3editor.cpp:691-701`) → **0** begin/perform/end; `getParameterCount() == 108` before and after; every `getParamNormalized` unchanged. The preset button is driven the same way, with the full `OutlineBrowserButton::onMouseDown` triple (`beginEdit(); setValueNormalized(1); valueChanged(); setValueNormalized(0); endEdit();`, `outline_button.h:79-131`) → still 0 edits. A sub-controller that forwarded session-tag begin/end edits fails this case |
| same | `Vorago_GravityMacro_AnchorModeDisplay` | SC-022, FR-081 | **Arm A — value current at open (Free):** on a fresh controller, `setParamNormalized(kResonanceAnchorModeId, indexToNormalized(0, 3))` (Free) **before** the editor opens; open it; with no further parameter change → the knob tooltip (`getAttribute(kCViewTooltipAttribute)`) is the inert text, the label text is `"Gravity (inert)"` and `gravityDisplayInertForTest() == true`. This arm fails if `didOpen` skips `refreshGravityDisplay()`, because the uidesc's static tooltip, the static label and `gravityInert_`'s initial `false` are all the normal state. **Arm A′ — Keyed:** the same with `indexToNormalized(1, 3)`. **Arm B — change while closed:** open at Hybrid (normal), close, set Free with the editor closed, reopen → inert with no further change; close, set Hybrid, reopen → normal tooltip, label `"Gravity"`, flag `false` (catches a stale flag). **Arm C — live:** with the editor open at Hybrid, → normal; `setParamNormalized(403, Free)` with no re-open → inert tooltip and label; Keyed → inert; Hybrid → normal. Throughout, the knob's `getAlphaValue()`, `getMouseEnabled()` and value are unchanged, and the recording handler sees **0** edits |
| `unit/controller/editor_lifecycle_test.cpp` (edited) | `Vorago_EditorLifecycle` | SC-016 (a), FR-041 | `HarnessCycles` → `cycles = 10`. `EditorBindsFourteenControls` is **replaced** by `EditorBindsSurface`: `collectBoundControls` filtered to `tag < UI::kSessionTagBase` (session tags are ≥ 0 and would otherwise be counted), giving **106** controls; tag set = all IDs except 4 and 5; exactly one `EcosystemView` (`dynamic_cast` walk); `ecosystemViewForTest() != nullptr` while open and `== nullptr` after `removed()`; `presetBrowserViewForTest() != nullptr` while open, and **`== nullptr` after close** (FR-042, FR-071; R-2: no save dialog). Before closing, the browser is opened with `openPresetBrowser()` (`isOpen()` true), so the `willClose` `close()`-then-null path of §5.4 runs, and runs under the SC-016 ASan `[lifecycle]` run; `createCustomView("Nope", …) == nullptr`; `createSubController("Nope", …) == nullptr`. The banner is updated |
| `integration/preset_browser_test.cpp` (new) | `Vorago_PresetBrowser_SaveLoadRoundTrip` | SC-023 | A real `ProcessorFixture` plus a real `Controller`, with the handler stub answering `IComponent` by forwarding to the processor (Seraphis `preset_load_test.cpp` shape). (1) Headless editor open: `presetBrowserViewForTest() != nullptr`; the preset button click → browser `isOpen()`. After the editor closes (browser still open at close): `presetBrowserViewForTest() == nullptr` (R-2: no save dialog). (2) State provider → stream; its bytes equal `proc->getState` bytes (428). (3) Randomize all 106 via `setParamNormalized` with a seeded `Xorshift32`, list IDs snapped to their steps. (4) Load provider with the stream → returns true; all 106 values equal the captured ones; the recording handler holds exactly one Begin/Perform/End triple per changed ID; a second controller fed the same stream through `setComponentState` has identical values. (5) A stream with version 3 → `false`, 0 edits, all values unchanged |

### 9.3 Existing suites that must pass unedited

SC-018 / FR-014 / FR-015: `Vorago_ParameterInfoTable`, `state_roundtrip_test.cpp`, `state_v2_test.cpp` (v1-load,
truncation), and all Phase 12 pack TUs. `git diff --stat plugins/vorago/tests/unit/param_table_expected.h`
must be empty. `setComponentState`'s refactor (§5.5) is the only code these touch.

### 9.4 Non-test gates

- SC-016: one ASan run (`-DENABLE_ASAN=ON`, Debug), `vorago_tests "[lifecycle]"`, log recorded.
- SC-020: `tools/pluginval.exe --strictness-level 5 --validate build/windows-x64-release/VST3/Release/Vorago.vst3`.
- SC-021: zero MSVC warnings on `Vorago` and `vorago_tests`; `node tools/check-portability.js`, then
  `wsl --shutdown`; `./tools/run-clang-tidy.ps1 -Target vorago` and `-Target dsp`, with zero findings on
  the touched files.

---

## 10. Build integration

| File | Change |
|---|---|
| `dsp/tests/CMakeLists.txt` | add `unit/systems/ecosystem_engine_pair_accessors_test.cpp` after `:491`, and to the `-fno-fast-math` list near `:1022` |
| `plugins/vorago/CMakeLists.txt` | sources: `src/processor/ecosystem_frame.h`, `src/processor/ecosystem_frame_builder.h`, `src/ui/ecosystem_view.h`, `src/ui/ecosystem_view.cpp`, `src/ui/panel_sub_controller.h`, `src/ui/panel_sub_controller.cpp` |
| `plugins/vorago/tests/CMakeLists.txt` | new TUs: `unit/ecosystem_frame_builder_test.cpp`, `integration/ecosystem_frame_test.cpp`, `unit/controller/ecosystem_consumer_test.cpp`, `unit/controller/ecosystem_view_test.cpp`, `unit/controller/editor_layout_test.cpp`, `integration/preset_browser_test.cpp`; second compilation: `../src/ui/ecosystem_view.cpp`, `../src/ui/panel_sub_controller.cpp`; `-fno-fast-math`: **only** `unit/ecosystem_frame_builder_test.cpp` (NaN/Inf by bit pattern). `ecosystem_frame_test.cpp` is left out on purpose: its comparisons are `==` between two reads of one compiled path, and SC-011's finiteness check uses `detail::isFinite`, which is fast-math safe (the Seraphis `cloud_frame_test.cpp` rationale, `plugins/seraphis/tests/CMakeLists.txt:153-165`). `processor_cpu_test.cpp` stays out, as it is today |
| `plugins/vorago/src/ui/.gitkeep` | deleted (FR-060) |
| `plugins/vorago/src/preset/vorago_preset_config.h` | `makeVoragoPresetTabLabels()` |

No `controller_presets.cpp` is needed: the providers are two short functions and the preset button is the
shared class, so the Ruinae split has nothing to split.

**Targets.** Build `dsp_systems_tests`, `Vorago` and `vorago_tests` with the full CMake path. Run
`build/windows-x64-release/bin/Release/dsp_systems_tests.exe` and `…/vorago_tests.exe` directly.
`[.perf]` runs happen only through `node tools/run-cpu-tests.js vorago_tests`, alone. `seraphis_tests` is
**not** affected, because no header it includes changes; confirming that is a `git diff --stat` check, not a run.

---

## 11. Risks and mitigations

| # | Risk | Mitigation |
|---|---|---|
| R-1 | **UB narrowing:** `static_cast<float>(1e300)` is undefined behaviour, not Inf | `sanitizeFrameFloat` range-checks against `FLT_MAX` in double before the cast (§3.2); SC-011 covers ±1e300 |
| R-2 | **`std::isnan` under fast-math** folds away | All finiteness checks go through `Krate::DSP::detail::isFinite` (`db_utils.h:125`); test NaNs are built from bits through `volatile`; `tools/lint-nonfinite-symbols.js` stays clean |
| R-3 | **Cross-TU float exactness** (SC-006 compares with `==` between `processor.cpp` and a test TU; `/fp:fast` COMDAT hazard in memory `reference_comdat_fp_fast_render_golden_context`) | The compared quantities are a double→float cast of the same getter, and one divide/add chain in a single `inline` function. None has a contraction opportunity. If an exact compare fails on any leg, it is surfaced as a finding (the premise was wrong) and **not** loosened silently |
| R-4 | **SC-009's ≤ 60 s retirement** may not hold: `VoragoVoice` keeps bodies, ecology and resonance running as the tail (`vorago_voice.h:928-936`), and the default release is 45 s | Rows set a 200 ms release. The first run WARN-records time-to-Idle. If it is still > 60 s, stop and surface it (probe before ruling), with the bound unchanged |
| R-5 | **No portable headless timer pump**: VSTGUI timers fire from the platform run loop, and a Win32 `PeekMessage` pump in a test is platform code | SC-016 (b) relies on `hasTimerForTest() == false` after `removed()` (the timer object is destroyed) plus the ASan run. The "pump ≥ 100 ms" becomes a 150 ms sleep, which proves nothing on its own. This is recorded as Open Question 5 |
| R-6 | **Gravity update runs on the caller of `setParamNormalized`** (PITFALLS: never touch views off the UI thread) | FR-081 requires synchronous updates. The same thread contract already applies to every VSTGUI-bound control (`vst3editor.cpp:174`), and VST3 specifies `IEditController::setParamNormalized` as UI thread. The update only calls `setTooltipText`/`setText`/`invalid` on two views and never allocates on the audio thread (the controller never runs there) |
| R-7 | **Data race on `dataExchangeHandler_`** if a host disconnects mid-process | Same exposure as Membrum/Seraphis (`processor.cpp:1212-1220`); hosts disconnect after processing stops. `frameResyncPending_` is atomic. No new lock (RT rule) |
| R-8 | **Session tags counted as bound controls** (`collectBoundControls` counts `getTag() >= 0`) | Filter to `< kSessionTagBase` in the edited lifecycle test (§9.2) |
| R-9 | **`ArcKnob` unregistered in `vorago_tests`** (no `entry.cpp`) → the uidesc silently drops 12 + 80 knobs and SC-016 (a) sees fewer than 106 | `panel_sub_controller.cpp` includes `<ui/arc_knob.h>` (§8). SC-016 (a)'s 106 count catches any regression |
| R-10 | **`ui/` include-path shadowing**: Vorago `src/ui/` and shared `plugins/shared/src/ui/` share the `ui/` prefix | New file names (`ecosystem_view.h`, `panel_sub_controller.h`) are absent from `plugins/shared/src/ui/` (checked). Shared headers are included with `<ui/…>`, following Seraphis |
| R-11 | **Same-slot reset with an equal step count** could skip one publish | The trigger uses `!=`, so a collision needs the new count to equal the recorded one on an evaluation boundary. At worst this delays a frame by one simulation step (about 10.7 ms). The view accepts ≤ 0.6 s of stale fade on a same-count reset (spec *Edge cases*) |
| R-12 | **`sizeof(Processor)` assert** (`processor.h:280`) | About 3.4 KiB is added, against a large margin. It is a compile-time check |
| R-13 | **Denormals** in fade decay | Entries below 1/255 are zeroed each tick. The UI thread is not under `ScopedDenormalMode`, and the floor stops tails from reaching the subnormal range |
| R-15 | **Fallback queue unavailable on Linux** (`Timer::create` returns `nullptr` without an injected factory, `base/source/timer.cpp:339-353`), so `openQueue` fails and the connected arms of `HandlerLifecycle`/`AllocationFree` would have no queue | The arms check the recorded `"DataExchangeQueueOpened"`: on `SMTG_OS_LINUX` a missing queue is `WARN`-recorded and only the queue-dependent assertions are skipped; on Windows and macOS it is a `REQUIRE`. The gate, trigger-(iii), disconnect and 0-allocation assertions run on every OS. The arms are never omitted. No `InjectCreateTimerFunction` is added, because it is declared only in the Linux branch of the SDK and would put a platform `#if` in the test |
| R-14 | **MSVC-only green** | `check-portability.js` before commit; designated initialisers for `PrepareConfig` in tests; no brace narrowing (`uint8(k)` casts are explicit); no aligned SIMD introduced |

---

## 12. Decisions made by this plan (departures from spec wording)

- **D-1 — No new `PresetBrowserButton` class.** FR-070 asks for "a small `plugins/shared/src/ui/`
  (`OutlineButton`) based button, plugin-local like Ruinae's", and the ODR table claims `Vorago::PresetBrowserButton`.
  The shared `Krate::Plugins::OutlineBrowserButton` (`outline_button.h:79`) is already a complete
  listener-based preset button, and Seraphis ships exactly that (`controller.cpp:477-479`). Building a
  plugin-local copy would duplicate existing code. The custom-view-name stays `"PresetBrowserButton"`
  (FR-042), and the click is routed through the sub-controller. **Ruled R-1.**
- **D-2 — Second seam `setEcosystemFrameEnabledForTest`** (§4.4). The ODR sweep this session found 0 matches.
  **Ruled R-4.**
- **D-3 — No `controller_presets.cpp` file.** It follows from D-1 (§10).
- **D-4 — `makeVoragoPresetTabLabels()`** is added to `vorago_preset_config.h` (sweep: 0 matches), so that
  the browser's tabs cannot drift from the config.
- **D-5 — Gravity inert label text** `"Gravity (inert)"`, with tooltip text chosen by the implementer. SC-022
  asserts the inert/normal *states*, not the wording.

**ODR sweep for names this plan adds (run this session, `grep -rn "\b<name>\b" dsp/ plugins/ tools/ tests/`):**
`setEcosystemFrameEnabledForTest`, `setEcosystemFrameForcedForTest`, `ecosystemFramePublishAttemptCountForTest`,
`ecosystemFrameSkippedBlockCountForTest`, `kEcosystemFrameSilenceLevel`, `kMaxFrameAgents`, `kMaxFrameLinks`,
`kLinkFadeSeconds`, `refreshGravityDisplay`, `kVoragoPageCount`, `kPageStripTag`, `stepLinkFade`,
`makeVoragoPresetTabLabels`, and (added by the review revision) `buildDrawList`, `DrawList`, `DrawItem`,
`DrawKind`, `kMaxDrawItems`, `kMaxFadePairs`, `savePresetDialogViewForTest`, `ConnectedFixture` all returned **0**.
The draw-list types are nested in `Vorago::UI::EcosystemView`, and `ConnectedFixture` is test-local in an
anonymous namespace. `lastPublishedFrameForTest` has hits only as a
`Seraphis::Processor` member (`plugins/seraphis/src/processor/processor.h:608`), and `openPresetBrowser` only
as members of other plugins' `Controller`s (Disrumpo, Gradus, Innexus, Ruinae). Both are class-scoped members
in other namespaces, so there is no clash.

---

## 13. Implementation order

1. **SC-019 first (failing test).** Write the accessor test → it fails to compile. Add the three getters (§2)
   → build `dsp_systems_tests` → full suite green.
2. **Builder.** `ecosystem_frame.h` and `ecosystem_frame_builder.h` + `ecosystem_frame_builder_test.cpp`
   (layout, glow, sanitize, SC-007 b–d) → green.
3. **Producer.** Write `ecosystem_frame_test.cpp` (SC-001, 006, 007a, 008, 009, 010, 011 including its
   connected arm, and `HandlerLifecycle`) against the seams and `ConnectedFixture`, so it fails. Implement §4 and build with zero warnings. Measure the SC-009 time-to-Idle before judging
   R-4.
4. **CPU gate.** Add arm PF + the SC-007 (e) case. Build, then run `node tools/run-cpu-tests.js vorago_tests`
   alone and record the figures.
5. **Consumer + view statics.** `ecosystem_consumer_test.cpp`, `ecosystem_view_test.cpp` including
   `Vorago_EcosystemView_DrawList` and the live-view `LinkFade` arm (red). Implement §5.1–5.2 and §6,
   with `draw()` as a pure replay of `buildDrawList` (green).
6. **Sub-controller, uidesc, entry, banners.** Write `editor_layout_test.cpp` and edit the lifecycle test
   (red). Implement §5.3–5.4, §7 and §8 (green), and check that 106 are bound in the built tree. Rewrite the
   `controller.h:12-13` banner (§5.1) and the `entry.cpp` banner (§8) in this step.
7. **Presets + Gravity.** Write `preset_browser_test.cpp` and the SC-022 case (red). Implement §5.5–5.6
   (green). Re-run the Phase 12 state suites unedited.
8. **Docs.** `CLAUDE.md`, `version.json` 0.2.0 + `CHANGELOG.md` (R-6), delete `.gitkeep`.
9. **Gates.** Full `vorago_tests` + `dsp_systems_tests`; pluginval 5; ASan `[lifecycle]`; check-portability
   + `wsl --shutdown`; clang-tidy `vorago` and `dsp`.

---

## Open questions — all ruled 2026-09-26 (spec Clarifications, plan stage: R-1 … R-6; R-7 = task format)

1. **Preset button (D-1).** → **R-1: reuse the shared button.** Reuse the shared `OutlineBrowserButton`, as this plan does, instead of FR-070's
   plugin-local subclass. Confirm, and the spec's FR-070 wording and ODR row get amended to match.
2. **`SavePresetDialogView` has no opener.** FR-071 constructs it, but C-1's header has only the browser button,
   and `PresetBrowserView` carries its own internal save dialog (`preset_browser_view.cpp:615-620`). Options:
   (a) construct it as ruled, though it will be unreachable; (b) add a header "Save" button (session tag 9001)
   that opens it; (c) drop it and amend FR-071. → **R-2: (c).** `SavePresetDialogView` is not built; §5 above is amended.
3. **`habitatBrightness` = raw linear voice peak (Q6).** `getVoiceLevel` is a linear chunk-peak follower
   (`vorago_voice.h:2151-2154`). A held drone with a typical per-voice peak of about 0.1–0.25 would draw its
   whole habitat at 10–25 % alpha, and a slow attack stays near-invisible for its whole rise. The plan
   implements Q6 as ruled and WARN-records the held `voiceLevel` range in SC-006. A perceptual map (for example
   dB-normalized) would need a spec amendment. → **R-3: dB-normalized**, `clamp((20·log10(v) + 60) / 60, 0, 1)`,
   spec C-6 clause 7 and SC-014 amended; §6.2 above amended.
4. **Gate-only seam (D-2).** SC-008 cannot be measured under FR-026's forced seam, which forces the trigger.
   The plan adds `setEcosystemFrameEnabledForTest`. → **R-4: confirmed**; spec FR-026 amended.
5. **SC-016 (b) "pump the VSTGUI timer loop ≥ 100 ms".** There is no portable headless pump, and a Win32
   pump in a test would be platform code. The plan relies on timer destruction (`hasTimerForTest() == false`)
   plus ASan, and sleeps 150 ms. → **R-5: accepted**; spec SC-016 (b) amended.
6. **CHANGELOG heading (FR-063).** `version.json` is 0.1.0, no plugin uses an `[Unreleased]` heading, and
   version bumps belong to the release flow. Should the Phase 13 entry go under a new `## [Unreleased]`
   heading, or be held for Phase 14's release entry? (Phase 12 also left no CHANGELOG entry.) → **R-6: bump
   `version.json` to 0.2.0 and write one `[0.2.0]` entry covering Phases 12 and 13** (Seraphis pattern);
   spec FR-063 amended.
