# Implementation Plan: Profundum Phase 12 — UI

**Spec:** `specs/profundum-phase12-ui/spec.md`, reviewed. Clarifications session 2026-10-23 ruled OQ-1 … OQ-10 and CQ-1 … CQ-8.
**Roadmap:** `specs/Profundum-roadmap.md` Phase 12 (lines 837–1027) and the Phase 11 → 12 handoff 12.1–12.4 (lines 827–835).
**Branch:** `synth/profundum` at `40f6ebca`, verified this session with `git branch --show-current` and `git log`.
**Date:** 2026-10-23
**Status:** PLAN. No code is written yet.

Stage 12a (the design documents) runs first. Until the user ratifies D1–D7, nothing changes under `plugins/profundum/`,
`dsp/` or `plugins/shared/` (FR-001, SC-001). §2 P-1 names the one place where that rule needs a ruling.

Every `file:line` below was opened and read in this planning session. Paths under `dsp/` are relative to
`dsp/include/krate/dsp/`, except those that start with `dsp/tests/`.

---

## 0. Summary

Phase 12 is plugin UI work. Its only change in `dsp/` is small, append-only and read-only (FR-075, FR-095):

- six `const noexcept` accessors on `ProfundumEngine`;
- five `const noexcept` accessors on `ProfundumVoice`.

The optional `BassStereoField` magnitude observer is **not** taken. The controller computes the SPREAD curve (§4.6.8), so
`bass_stereo_field.h` is not modified. `plugins/shared/` is not touched.

### 0.1 Design decisions

| # | Decision | FR / SC |
|---|---|---|
| D-1 | The spectrum producer reads the focus voice's per-interval scratch through new voice accessors. That scratch is `mainL_ / mainR_`, which the observer exposes as `charL / charR`. While the producer is active, the processor cuts each event slice at the engine's 32-sample grid points and harvests after every completed interval. The output is bit-identical by construction (§5.1). | FR-072, FR-075, SC-011, SC-022 |
| D-2 | The producer runs a 64-bin **double-precision** Goertzel bank with a periodic Hann window, N = round(8·fs/f0). It publishes one frame per completed window, and at most one frame per `process()`. | FR-071, FR-075, SC-010, SC-030 |
| D-3 | The processor tracks the focus voice without writing to the engine. It keeps its own note-arrival sequence numbers and reads four things per voice: `held()`, `active()`, the allocator note and the rendered flag. | FR-074, SC-025 |
| D-4 | The frame carries **power** (amplitude²). The view converts it to dB on the UI thread, so the audio thread never calls `log10`. | FR-071, FR-073 |
| D-5 | Transport copies the Vorago and Membrum mechanism. The handler is built in `connect`. The queue opens in `setActive(true)` and closes in `setActive(false)`. The controller's `notify` calls `DataExchangeReceiverHandler::onMessage`. **The producer is gated on an observed open queue, not on the handler's existence:** the first `process()` after activation probes the queue once (§4.4), and with no open queue the producer does no work (FR-071). | FR-071, FR-076, SC-012, SC-023 |
| D-6 | The SPREAD curve: c(f) comes from a controller-owned `BassStereoField`, through `setControls` then `reset()`. m comes from the FR-080 formula applied to the displayed values of 800, 801 and 805. | FR-080, SC-014 |
| D-7 | The morph component is built from stock and shared controls: segment buttons, `CTextEdit`, `CParamDisplay` and labels. No morph custom view is claimed. | FR-040 – FR-049 |
| D-8 | The `ADSRDisplay` (420–426) is wired through a law **adapter** in the controller. Its cubic-normalised ms are converted to plain ms and then to the registered logarithmic law. | FR-034, SC-024 |
| D-9 | Endpoint names are a 2 × 32-byte block appended at byte 510. This makes `kCurrentStateVersion = 3` and `kStateV3Bytes = 574`. One new controller → processor message is added, `kMsgEndpointName`. | FR-047, FR-096, SC-027 |
| D-10 | The editor sync timer is a `CVSTGUITimer` at 100 ms. It is created in `didOpen`, destroyed in `willClose`, and skips a tick while a parameter gesture is open. | FR-090, SC-015 |
| D-11 | The shared view-creator headers are included from `controller.cpp`, not `entry.cpp`. The test binary never compiles `entry.cpp`, so this is the only way the tests build the same view tree as the plugin. `entry.cpp` only gets its banner rewritten (§2 P-4). | FR-092, SC-002, SC-016 |

### 0.2 Areas touched in 12b

| Area | What changes |
|---|---|
| `dsp/…/systems/profundum_engine.h` | 6 public `const noexcept` accessors; append-only, no state, no behaviour change |
| `dsp/…/systems/profundum_voice.h` | 5 public `const noexcept` accessors over the existing scratch |
| `plugins/profundum/src/plugin_ids.h` | `kCurrentStateVersion = 3`, `kStateV2Version`, the name-block constants, `kStateV3Bytes`, `kMsgEndpointName` |
| `plugins/profundum/src/parameters/endpoint_names.h` | **New.** `EndpointName` POD, sanitiser and (de)serialiser, shared by processor and controller |
| `plugins/profundum/src/processor/` | **New** `spectrum_frame.h` and `spectrum_producer.h`. `processor.{h,cpp}` gains DataExchange, the producer, names and state v3 |
| `plugins/profundum/src/controller/` | `controller.{h,cpp}` gains the editor delegate, the receiver, the compare read path, names, descriptor, rings, highlight, zoom and the sync timer |
| `plugins/profundum/src/ui/` | **New** `harmonic_bar_view.{h,cpp}`, `spread_curve_view.{h,cpp}`, `panel_sub_controller.{h,cpp}`, `macro_ui_map.h`, `endpoint_descriptor.h` |
| `plugins/profundum/resources/editor.uidesc` | Replaced wholesale, with Main and Advanced pages |
| `plugins/profundum/src/entry.cpp` | FR-018 banner rewritten |
| `plugins/profundum/CMakeLists.txt`, `tests/CMakeLists.txt` | New sources, new TUs, `-fno-fast-math` list |
| `plugins/profundum/tests/profundum_test_fixture.h` | New `v2StreamOf` helper (a genuine v2 stream built field by field, SC-027); the `withEndpoints` / `withFields` guards accept 510 or 574 bytes (P-2) |
| `dsp/tests/CMakeLists.txt` | One new systems TU |
| Docs | `plugins/profundum/CLAUDE.md` (state v3 table, UI notes, tests table); `CHANGELOG.md` 0.1.0 bullets |

---

## 1. Verified reuse (real signatures, read this session)

### 1.1 Engine and voice (`systems/profundum_engine.h`, `systems/profundum_voice.h`)

**`ProfundumEngine`**

- Class at :94. `kMaxVoices = 8` (:99), `kControlInterval = 32` (:101).
- `void processBlock(float* left, float* right, std::size_t numSamples) noexcept` (:388). Each pass of its loop:
  - calls `gridStep()` when `gridPhase_ == 0`;
  - renders `m = min(numSamples − i, kControlInterval − gridPhase_)` samples through `renderChunk`;
  - advances `pos_` and `gridPhase_`.
- Everything after `private:` (:424) is private, including:
  - `voices_` (:1231) and `allocator_` (:1233);
  - `applied_` (:1261) and `monoActiveNote_` (:1272);
  - `rendering_` (:1274, commented "active at this interval's grid point");
  - `pos_` (:1277) and `gridPhase_` (:1278).
- MONO always plays voice 0 (`applyNoteOn`, :767–783: `ProfundumVoice& v0 = voices_[0]`). POLY uses `allocator_.noteOn` and
  the voice index carried by each `VoiceEvent` (:784–811).
- `ProfundumEngineConfig::placement` defaults to `PerVoice` (:85). PostSum is only a measurement configuration (:79–81,
  :968–971). The processor uses `makeProfundumEngineConfig()`, which returns `ProfundumEngineConfig{}` unchanged
  (`plugins/profundum/CLAUDE.md`). So in the plugin the post-Character signal exists per voice.

**`ProfundumVoice`**

- Public state queries: `kMaxChunk = kControlInterval` (:78), `audible()` (:358), `held()` (:359), `active()` (:360),
  `noteFrequency()` (:370), `currentCoreF0()` (:693).
- `render(const float* fRes, float* sumL, float* sumR, std::size_t n)` (:671). `private:` starts at :700.
- `renderSub` writes the Character output in place into `mainL_.data() + k0` / `mainR_` (:864, :910). The observer's
  `charL / charR` are spans of that scratch (:941).
- `anchorL_ / anchorR_` hold the Anchor before the gains are applied (:991).
- `std::size_t k_` is the position inside the current interval (:1053). `beginInterval` resets it to 0 (:379).
- There is a private `gridPhase()` at :717.

**`VoiceAllocator`**

- `getVoiceNote(size_t) const noexcept` returns the MIDI note, or −1 when the voice is idle (`systems/voice_allocator.h:406–409`).
- When a note is already playing, `noteOn` retriggers the existing voice (:237–242).

### 1.2 Observer (`systems/profundum_engine_observer.h`)

`ProfundumVoiceTaps { std::uint8_t voice; std::uint64_t sample; std::size_t n; std::span<const float> coreL, coreR, charL,
charR, anchorL, anchorR, gAmp, gD, …; }` (:61–66). It is a test seam only (:12–15). The SC-010 and SC-022 references read it
through `Processor::setEngineObserverForTest` (`processor.h:102`).

### 1.3 Processor (`plugins/profundum/src/processor/`)

- `class Processor : public AudioEffect, public ITimerCallback` (`processor.h:51`).
- `kSyncTimerIntervalMs = 50` (:54); `syncTimer_` (:201); `static_assert(sizeof(Processor) < 64u * 1024u)` (:207).
- There is no `connect` or `disconnect` override.
- `process()` (`processor.cpp:302–451`):
  - its first statement is `ScopedDenormalMode` (:303);
  - it runs the event-slice loop, which calls `engine_->processBlock(outL + cursor, outR + cursor, n)` and then
    `applyOutputGain` (:430–431);
  - `lastSliceCount_` counts event slices, and `midi_event_test.cpp:459, :466` asserts on that count (1 and 4).
- `dispatchEvent` (:833–861) calls `engine_->noteOn(pitch, …)` when velocity > 0.
- `setState` (:491–535). **Its v1 branch is `if (version < kCurrentStateVersion)` (:504).** Under v3 that condition would
  send a v2 stream into the v1 reset (fixed in §4.5).
- `getState` (:537–562) writes `kCurrentStateVersion` and then the blocks, ending with the 20-byte runtime blob.
- `notify` (:567–596) handles `kMsgCopyEndpoint`, `kMsgCompare` and `kMsgRequestSync`, then calls `flushControllerSync()`.
- `setActive` (:276–299) and `setupProcessing` (:228–270).

### 1.4 Controller (`plugins/profundum/src/controller/`)

- `class Controller : public EditControllerEx1, public IMidiMapping` (`controller.h:49`).
- `copyEndpoint` (:81), `setCompare` (:83), `compareModeForTest()` (:92), `mirror_` (:104), `compare_` (:105).
- `setComponentState` (`controller.cpp:91–144`):
  - **its v2 branch is `else if (version >= kCurrentStateVersion)` (:117)**, which a v3 build would skip for v2 streams;
  - it stops after endpoint B and never reads the 20-byte runtime blob.
- The `setParamNormalized` hook for 1001 (:184–207); `notify` (:260–304, compare branch :291–301); `createView` (:332–337).

### 1.5 Morph, macros, field and laws

**Morph parameters (`morph_params.h`)**

- `CompareMode { AB = 0, A = 1, B = 2 }` (:154).
- `formatMorphControlParam` prints 105 as `"%.0f%%"` of 100·p (:276–282).
- Anchor morph labels Hold / Blend / Yield (:178–179).

**`ProfundumMacroMatrix` (`profundum_macro_matrix.h`)**

- `kRows` (:75–92); `setMacros(const ProfundumMacroValues&)` (:174–179); `contributions() const noexcept` (:193–199).
- `contributionOf`: u = (m − 0.5)·2, then c = amount·applyModCurve(curve, |u|)·sign(u) (:243–246).
- `apply` adds and clamps the recipe and level fields (:204–219). For the amp times it calls `scaleTimeClamped`, which
  computes t·2^c clamped to [kMinEnvelopeTimeMs, kMaxEnvelopeTimeMs] (:261–268).
- Row `DarkBright → Shape` has amount 0 (:81). Row `CompressionAmount` (:86) is not a `MorphState` field (:200–203).

**Field table and limits**

- `kFieldTable` (`morph_state.h`): amp times Logarithmic over [tMin, tMax] (:203–210); sustain Linear [0, 1]; curves Linear
  [−1, 1] (:211–216); `field.crossoverHz` Logarithmic over [kMinCrossoverHz, kMaxCrossoverHz] (:222–224).
- `kMinEnvelopeTimeMs = 0.1f` and `kMaxEnvelopeTimeMs = 10000.0f` (`primitives/envelope_utils.h:51–52`).

**Plugin law helpers**

- `plainFromNormalized(const FieldInfo&, double)` and `normalizedFromPlain(const FieldInfo&, double)`
  (`sound_param_row.h:178–193`); `lawOf(leaf)` (:66).
- `kSoundRows` (`sound_param_table.h:63`) and `kParamRoutes` (:101).

**`BassStereoField` (`bass_stereo_field.h`)**

- The default constructor builds the law cache (:155–167). The class is non-copyable (:168–169).
- `setControls` only latches into `pending_` (:232–256).
- `reset()` (:196–228) applies `pending_`, snaps the crossover smoother and rebuilds the law cache. It calls the crossover's
  `prepare` only if `prepared_` is set.
- `widthCurve(float) const noexcept` (:397–403) returns `lawCurve` (:632–644).
- `updateLawCache` holds the `magnitudeScale_` formula (:647–657). `kMotionExcursion = 0.5f` (:78).

### 1.6 Shared UI (`plugins/shared/src/ui/`)

**`ArcKnob`**

- `setModulationRange(float)` stores a bipolar range (`arc_knob.h:72–79`); `getModulationRange()` (:81). It stores the new
  value only when `|range − modRange_| > 0.0005f` (:74–78), so the stored ring can differ from the computed one by up to 5e-4
  (§4.6.5).
- `drawModulationArc` draws `[value − |range|, value + |range|]`, which is **symmetric** (:338–345).
- The draw helpers are private (:232, :278–376).

**`ADSRDisplay`**

- `ParameterCallback = std::function<void(uint32_t, float)>` (`adsr_display.h:145`).
- `setParameterCallback`, `setBeginEditCallback`, `setEndEditCallback` (:355–357).
- `setAdsrBaseParamId` (:310) and `setCurveBaseParamId` (:330–334).
- `setAttackMs`, `setDecayMs`, `setSustainLevel`, `setReleaseMs` (:200–221); `set{Attack,Decay,Release}MaxMs` (:235–251).
- `timeMsToNormalized = cbrt(clamp(ms, kMinTimeMs, maxMs) / maxMs)` and its inverse `normalizedToTimeMs` (:1413–1425).
  `kMinTimeMs = 0.1f` (:68), `kDefaultMaxTimeMs = 10000.0f` (:73).
- A time drag emits `paramCallback_(id, timeMsToNormalized(ms, maxMs))` (:976–978). Sustain is emitted as the raw level
  (:1024). Curves are emitted as `(c + 1) · 0.5` (:1063, :1322–1326).
- Peak-level and bezier IDs are emitted only when they were set to a value above 0 (:988, :1192).
- `getAttackMs`, `getDecayMs`, `getSustainLevel`, `getReleaseMs` (:450–453), `getControlPointPosition(DragTarget)` (:495) and
  `onMouseDown`, `onMouseMoved`, `onMouseUp` (:618, :662, :700) are public; the class's only `private:` is at :740.
  `paramCallback_` (:1939) is private and has no getter.

**Highlight colour setters (FR-060)**

- `ArcKnob::setGuideColor` / `getGuideColor` (`arc_knob.h:93–94`).
- `BipolarSlider::setTrackColor` / `getTrackColor` (`bipolar_slider.h:76–77`).
- `ADSRDisplay::setControlPointColor` / `getControlPointColor` (`adsr_display.h:570–571`).

### 1.7 VSTGUI and the SDK

**`VST3Editor` (`vst3editor.h`)**

- `IVST3EditorDelegate::createCustomView`, `verifyView`, `didOpen`, `willClose`, `createSubController`, `onZoomChanged` (:40–67).
- `VST3Editor::setEditorSizeConstrains`, `setZoomFactor`, `getZoomFactor`, `setAllowedZoomFactors` (:115–123).
- A parameter-bound `CParamDisplay` gets a value-to-string function that calls `getParamStringByValue` when it draws
  (`vst3editor.cpp:146–147, :242–252`). Invalidating the display is therefore enough to re-format it.

**DataExchange (`public.sdk/source/vst/utility/dataexchange.{h,cpp}`)**

- `DataExchangeHandler`: `onConnect`, `onDisconnect`, `onActivate`, `onDeactivate`, `getCurrentOrNewBlock`, `sendCurrentBlock`
  (`dataexchange.h:66–96`).
- The fallback `openQueue` fails when `Timer::create` returns nullptr. Otherwise it sends `MessageIDQueueOpened` synchronously
  (`dataexchange.cpp:77–100`).
- `DataExchangeReceiverHandler::onMessage(IMessage*)` (`dataexchange.h:145`).
- `onActivate(const ProcessSetup&, bool forceUseMessageHandling = false)` returns `void` (`dataexchange.h:74`). The fallback's
  `openQueue` also fails when the host context is not an `IHostApplication` (`dataexchange.cpp:79–80`). `Impl::isOpen()` is
  private (:210). `getCurrentOrNewBlock()` (:373–377) goes through `Impl::lockBlock` (:245–268), which returns
  `InvalidDataExchangeBlock` when the queue is not open and returns the still-unsent current block when there is one
  (:249–250). A failed open is therefore silent: the processor can observe it only as an invalid block.

**Other VSTGUI pieces**

- `CVSTGUITimer(CallbackFunc&&, uint32_t fireTime = 100, bool doStart = true)` and `getFireTime()` (`cvstguitimer.h:23, :34`).
- `CView::registerViewEventListener(IViewEventListener*)` (`cview.h:406`).
- `IViewEventListener::viewOnEvent(CView*, Event&)` (`iviewlistener.h:57`).
- `EventType::MouseEnter` and `EventType::MouseExit` (`events.h:29–30`).
- `EditController::beginEdit`, `performEdit`, `endEdit` and `setDirty` are virtual (`vsteditcontroller.h:72–74, :91`).

### 1.8 Precedents copied, not reused

**Vorago producer**

- `connect` builds the handler with the block size rounded up to 32 (`plugins/vorago/src/processor/processor.cpp:265–293`).
- `setActive` calls `onActivate(processSetup)` and `onDeactivate()` (:233–245).
- Publishing is `getCurrentOrNewBlock`, `memcpy`, `sendCurrentBlock` (:554–563).
- Test seams `set…ForcedForTest` and `set…EnabledForTest` (`processor.h:96–115`). `DataExchangeHandler` is forward-declared
  (`processor.h:53`).

**Vorago consumer (`plugins/vorago/src/controller/controller.cpp`)**

- `queueOpened` asks for UI-thread dispatch, and the newest block wins (:303–329).
- `notify` passes the message to `dataExchangeReceiver_.onMessage` (:330–339).
- `createCustomView` builds the view rect from the `origin` and `size` attributes (:344–369).
- `willClose` zeroes every view pointer (:404–423).

**Vorago panel and views**

- `VoragoPanelSubController : VSTGUI::DelegationController` uses session tags ≥ 9000, and its pages are sibling containers
  (`plugins/vorago/src/ui/panel_sub_controller.h:42–81`).
- A view's timer lives from `attached` to `removed` (`plugins/vorago/src/ui/ecosystem_view.h:57–58, :88, :147`).

**Vorago connected-arm test pattern**

`kQueueMayBeUnavailable` and `queueAvailable(opened, step)` WARN-skip on Linux only
(`plugins/vorago/tests/integration/ecosystem_frame_test.cpp:107–132`).

**Profundum test infrastructure (`plugins/profundum/tests/profundum_test_fixture.h`)**

- `ConnectedPair` (:755–787).
- `ControllerFixture` with `RecordingComponentHandler` (:456–477).
- `CountingConnectionPoint` (:220–244).
- `RecordingConnectionPoint::hold` keeps messages instead of forwarding them, and `releaseHeld()` delivers them in order
  (:244–250, :263, :308).
- `writeV2Stream` is a `getState` call (:1006–1016). `withEndpoints` and `withFields` patch bytes below 510 of a stream they
  are given and `REQUIRE(bytes.size() == kStateV2Bytes)` (:1045, :1092). `v1Stream` builds a v1 stream field by field
  (:1062–1074). The v2 offsets are `kV2GlobalsAt` … `kV2SeedAt` (:982–986).

**Sub-band reading**

- `StereoFieldTest::bandReading(L, R, n, fs, fLo, fHi)` (`dsp/tests/unit/systems/bass_stereo_field_test_helpers.h:736`).
- Bars `kSubBandQBarDb = 1.0` and `kSubBandRhoBar = 0.99` (:95, :97).
- Used as in `plugins/profundum/tests/integration/mono_sum_params_test.cpp:338–360`.

**Independent DFT reference**

`goertzelPhasorWindowed(const float* x, const std::vector<double>& w, std::size_t L, double fs, double hz)` is a direct
double-precision sum (`dsp/tests/unit/systems/profundum_excitation_test_helpers.h:107–120`). Its directory is already on the
`profundum_tests` include path (`plugins/profundum/tests/CMakeLists.txt`).

**No production Goertzel exists.** A grep of `dsp/include`, `plugins/shared/src` and every `plugins/*/src` found Goertzel
helpers only in tests. The bank in §4.3 is therefore new and plugin-local. The spec's component table places it there too
("Processor … the Goertzel bank of FR-075").

---

## 2. Spec issues found while planning

None of these changes a ruling. Each one needs either an acknowledgement or a one-line ruling recorded in the FR / SC row it
belongs to (memory rule "ruling goes in the FR / SC row first").

- **P-1 — FR-001 vs FR-006 (blocks 12a).**
  - FR-006 requires a main-page render set **before** ratification.
  - FR-001 forbids any file outside the spec folder until ratification.
  - No existing tool can render Profundum: `tools/krate-render` drives only the Membrum processor
    (`tools/krate-render/src/main.cpp:1–9`).
  - **Recommendation:** rule one exception into FR-001. Allow a single hidden, test-only TU,
    `plugins/profundum/tests/integration/main_page_render_set_test.cpp` (`[.listen]`), plus its line in `tests/CMakeLists.txt`,
    to land before ratification. It writes WAVs to `specs/profundum-phase12-ui/evidence/listen/` and touches no shipping
    source. This is Open Question 1.
- **P-2 — SC-004 understates the state-test edits.**
  - v3 makes `getState` write 574 bytes instead of 510.
  - `kStateV2Bytes` appears on 36 lines in 7 test files (`grep -rn kStateV2Bytes plugins/profundum/tests`).
  - Some of those lines assert the length of a `getState` result. Examples: `state_roundtrip_test.cpp:238, :247, :261`,
    `lifecycle_test.cpp:508`, `determinism_test.cpp:72`, `state_restore_render_test.cpp:137` and
    `phase11_alloc_test.cpp:191`. Those are assertions about the current format and must change to `kStateV3Bytes`.
  - The patch helpers `withEndpoints` and `withFields` (`profundum_test_fixture.h:1045, :1092`) are a third class: they guard
    on `== kStateV2Bytes` but patch only bytes below 510 of a `getState` stream, so their guard accepts both lengths (§6.2
    P-2 audit).
  - Lines that **build or truncate** a v2 stream stay as they are.
  - SC-004's "moves only the assertion that names the current version" is therefore too narrow. The P-2 audit in §6
    classifies every line. No v1 or v2 *load* expectation changes. This is Open Question 2.
- **P-3 — The FR-075 cadence figure is wrong for C3.** This repo's C3 is 130.81 Hz (MIDI 48;
  `dsp/tests/unit/systems/profundum_core_spectral_test.cpp:78, :812`). With K = 8 periods per window, that gives
  130.81 / 8 = **16.4 Hz**, not "about 33 Hz". E1 gives 41.2 / 8 = 5.15 Hz, which matches the spec. SC-030 counts windows,
  not Hz, so no gate changes; only the spec text needs correcting.
- **P-4 — Where the view creators register.**
  - The spec's table says `entry.cpp` gets the shared view-creator includes.
  - `entry.cpp` is not compiled into `profundum_tests`: `tests/CMakeLists.txt` lists only `processor.cpp` and
    `controller.cpp` as plugin sources.
  - A view tree built in the tests would then silently drop every `ArcKnob`, `BipolarSlider` and `ADSRDisplay`, and SC-002 and
    SC-016 would measure a different tree from the one the plugin builds.
  - The includes therefore go in `controller.cpp`, as Vorago does (`plugins/vorago/src/controller/controller.cpp:35–40`).
    `entry.cpp` only gets its banner rewritten.
- **P-5 — The macro ring is drawn symmetrically.**
  - `ArcKnob` draws value ± |range| (`arc_knob.h:341–342`), and its draw helpers are private.
  - The ring stores the **signed** offset, and SC-028 checks it through `getModulationRange()`. On screen, though, it shows the
    offset's reach on both sides of the knob.
  - A one-sided ring would need either a shared-header change (fenced off) or a plugin-local re-implementation of
    `ArcKnob::draw`.
  - The plan ships the symmetric ring. This is Open Question 3.
- **P-6 — The views describe the Edit Target endpoint.**
  - The knobs show the endpoint selected by 1001 (Phase 11 behaviour).
  - The SPREAD curve (FR-080), the macro rings (FR-061) and the dimming (FR-044) are computed from those same displayed
    values, so they also describe that endpoint, not the morph-resolved state. D2 states this next to the curve.
  - The spectrum view (FR-025) is the only display of the actual resolved and modulated signal.
- **P-7 — The spectrum is measured before the amp envelope.**
  - The `charL / charR` scratch is taken before `g_amp` and `g_d` are applied (`profundum_voice.h:910–925`).
  - The bars therefore show harmonic **structure**, not loudness over time.
  - D2 rules the drawing as dB relative to the frame's strongest component, which is the natural reading of that data.
- **P-8 — FR-032 has no rule for display-only readouts.**
  - FR-040 asks for a 105 position readout (the registered string, SC-007(d)), and FR-035 asks for a 412 readout whose unit
    follows 414. Both are `CParamDisplay`s bound to a continuous ID.
  - FR-032 lists only `ArcKnob`, `BipolarSlider`, `CSlider` or a writing custom view for a continuous ID.
  - **Recommendation:** rule into the FR-032 row: "a display-only `CParamDisplay` bound to a continuous ID is allowed when the
    same page also binds that ID to a conforming writing control". `Profundum_UiClassRule` implements exactly that (§6.2), so
    a readout without a writing control fails. This is Open Question 6.
- **P-9 — The header preset field has no FR.**
  - Both roadmap wireframes draw a preset field (roadmap lines 927, 955), and the spec's reuse table names
    `PresetBrowserView` for it (spec.md:167). No FR or SC requires it, and Phase 13 owns presets.
  - The plan therefore does **not** build a preset field in 12b. If the user wants it, the ruling goes into a new FR / SC row
    first; Appendix A lists what the plan then adds. This is Open Question 7.

---

## 3. Stage 12a — design deliverables (spec folder only)

FR-002 fixes the seven files:

| Deliverable | File |
|---|---|
| D1 | `design/main-page.md` |
| D2 | `design/advanced-page.md` |
| D3 | `design/macro-mapping.md` |
| D4 | `design/morph-laws.md` |
| D5 | `design/voice-note.md` |
| D6 | `design/interaction-automation.md` |
| D7 | `design/accessibility-scaling.md` |

Every wireframe labels each control with its ID or "UI-only". 12a writes these files, the FR-006 renders (see P-1) and the
Clarifications scaffolding of §3.4, whose verdicts only the user fills. It writes nothing else.

### 3.1 Proposals for D1, D2 and D7 (for the user to ratify)

#### Designed size

960 × 600 px at 100 %, so 1920 × 1200 px at 200 %.

- The template sets `minSize = maxSize = 960, 600`.
- The editor calls `setEditorSizeConstrains({960,600},{960,600})`, scaled by the zoom (FR-014).
- Rationale: the Advanced page holds about 72 controls in nine sections. 960 × 600 fits three columns of about 300 px at a
  28 px knob-row pitch. The Main page fills the same frame.

#### Main page (D1)

Concept A's layout with Concept B's Anchor / Mass split. The page has a header and two rows.

- **Header:**
  - title;
  - no preset field in 12b (P-9);
  - voice mode 1 (two-segment);
  - glide 412: an `ArcKnob` that writes 412 (FR-003, FR-011), with a display-only `CParamDisplay` readout bound to 412 beside
    it. The readout's unit follows 414 (FR-035; P-8);
  - page strip MAIN / ADVANCED (UI-only);
  - zoom (UI-only).
- **Upper row, THE MASS** (`uidesc-label="mass-group"`):
  - `HarmonicBarView` #1, with an Anchor lane plus the Mass bars;
  - below it, `ArcKnob`s for Depth 200, Body 201, Edge 204, Fold 500, Mass 104 and Drop 400;
  - macros: Weight 100 (`ArcKnob`); Dark ↔ Bright 101 and Clean ↔ Violent 103 (`BipolarSlider`s, neutral 0.5);
  - Motion source 900 (four-segment).
- **Lower row** (`uidesc-label="morph-anchor"`, FR-022). It holds exactly two child containers side by side, so the Anchor's
  morph behaviour sits beside the morph component:
  - left, **THE ANCHOR** (`uidesc-label="anchor-group"`): level 302 (`ArcKnob`), character 300 (`COptionMenu`), and the
    `CTextLabel` MORPH BEHAVIOUR with the 1000-bound three-segment control (FR-012, SC-005);
  - right, **BASS MORPH** (`uidesc-label="morph-band"`; the morph component, FR-040):
    - A's name and descriptor;
    - the 105 `CSlider` (writes 105) and its display-only readout, a `CParamDisplay` bound to 105 (P-8);
    - B's name and descriptor;
    - A / A·B / B compare (UI-only, three-segment);
    - Copy A → B and Copy B → A (UI-only);
    - Edit Target 1001 (two-segment).

**FR-022 container, ruled here for D1.** The container FR-022 and SC-019 mean is the nearest ancestor of a 105-bound control
that carries `uidesc-label="morph-anchor"`. SC-019 asserts that this ancestor exists for every 105-bound control, and that a
1000-bound control descends from it (§6.2). SC-005's container is `anchor-group`, a child of `morph-anchor`, so the one 1000
control on Main satisfies both.

**Image-mode mirror on Main (FR-080): no.** The row is already full, and SOLID / SPREAD / WIDE shown without the SPREAD curve
invites a misreading of mono safety. D1 states yes or no, and SC-031(a) follows D1.

**Concept §14 delta table (FR-003; ratified together with D1, SC-001).** The source is `specs/Profundum-concept.md:647–678`.
D1 carries exactly these 11 rows. The verdicts and reasons are the plan's proposal; the user ratifies or changes them.

| §14 element | Verdict | Where in D1 | Reason |
|---|---|---|---|
| BASS title | replaced | header title PROFUNDUM | the product name; both roadmap wireframes title the header PROFUNDUM (roadmap lines 927, 955) |
| Spectrum centrepiece | kept | `HarmonicBarView` #1, upper row | FR-070: measured harmonic bars with an Anchor lane, never decoration (FR-025) |
| Weight knob | kept | macro Weight 100 | FR-003 macros |
| Body knob | kept | Body 201 | FR-003 "shape the mass" |
| Edge knob | kept | Edge 204 | FR-003 "shape the mass" |
| Mass knob | kept | Mass 104 | FR-003 "shape the mass" |
| Anchor knob | replaced | THE ANCHOR group: level 302, character 300, MORPH BEHAVIOUR 1000 | Concept B's Anchor region; one knob cannot carry level, waveform and morph behaviour (FR-003, FR-012) |
| Shift knob | moved to Advanced | Spectral Shift 205, Core spectrum section | FR-011 makes 205 Advanced-only; on Main, Dark ↔ Bright 101 drives it (`kRows` RecipeShift row, `profundum_macro_matrix.h:80`) |
| Fold knob | kept | Fold 500 | FR-003 "shape the mass" |
| Drop knob | kept | Drop 400 | FR-003 "shape the mass" |
| CLEAN ─ MORPH ─ VIOLENT bar | replaced | Clean ↔ Violent 103 (macros) and the BASS MORPH component with 105 (lower row) | the bar merged a macro axis with the A ↔ B morph; FR-003 names them as two controls (macro 103; Bass Morph 105 with its component) |

#### Advanced page (D2)

- The sections are exactly those of FR-004.
- MODULATION and SETTINGS are tabs inside the Advanced page container (FR-013).
- MODULATION holds Motion 900–914.
- SETTINGS holds the zoom control and the documented note "no run loop: spectrum empty" (FR-076).
- FR-004 places note behaviour 410–417 in Pitch & Envelopes and the global band 0 / 2 / 3 in Stereo & Output. This plan keeps
  that placement. SC-031(b) checks FR-004's containers, so D2 must not move an ID out of them.
- **Morph behaviour section** (`uidesc-label="section-morph-behaviour"`, FR-004):
  - Anchor Morph Mode 1000 (three-segment). FR-004 lists 1000 in both Sub Anchor and Morph behaviour, so D2 binds it in both
    sections;
  - Morph Ramp Time 1002 (`ArcKnob`, showing its registered display string);
  - a second A / A·B / B compare segment (session tag 9203) and a second pair of Copy buttons (9204, 9205), §4.7.
  - Both compare segments, Main and Advanced, show `compareMode()` and nothing else (FR-041). The panel tracks every compare
    segment (§4.7), and SC-007(b) checks both.

#### Spectrum drawing (D2; FR-070, FR-073, FR-074)

- **Vertical axis:** dB relative to the frame's strongest component (the Anchor included), from 0 to −60 dB.
- **Horizontal axis:** log₂ n for n = 1..64. This is the recipe's own axis (roadmap Key Decision 1).
- **Region shading:** Sub (n = 1), Body (2–8), Presence (9–64), at the roadmap's ruled boundaries.
- **Anchor bar:** in its own lane, left of the Mass bars, labelled ANCHOR.
- **Silence:** the bars decay linearly in drawn height to 0 over **300 ms**. This is the decay time SC-026 measures against.
- **Pitch-unstable frames** (§5.2) are drawn normally, because each is still a real measurement at the latched f0.
- **Focus rule:** D2 quotes the rule in §4.3.2 verbatim.

#### Tooltip statements (D2)

- **Field-sync readout (FR-051).** The 809 tooltip carries the folding statement: "Synced rates above 1 Hz fold into
  0.5–1 Hz." The plan recommends stating the folding rather than computing the effective rate, because the fold depends on
  the tempo.
- **Bipolar rest (FR-052).** The 911 tooltip: "Bipolar ENV rests at −amount between notes; Unipolar rests at the set value."
- **Mono guarantees (FR-024).** D2 writes one ruled string for each of 800–809, plus a WIDE caveat for 600–602.
  - Proposed core string: "Width is pan-only and constant-power; the generator's sub band stays at ρ ≥ 0.9989. Gravity 0 still
    attenuates side content below 2·f_c by −29 dB."
  - Proposed WIDE caveat: "In WIDE the sub-band correlation depends on the Character offset pair."

#### D7 values

- Minimum font: 10 px at 100 %.
- Minimum hit target: 20 × 20 px at 100 %, and 24 × 24 px for segment cells.
- Text contrast: at least 4.5 : 1 (WCAG 2.x relative luminance), or 3 : 1 for bold text of 14 px or more.
- Every bound control carries a tooltip.
- No free resize. Zoom steps are 100 / 125 / 150 / 200 %.

#### Endpoint descriptor table (D2, FR-048)

Proposed: three words, one per axis, computed from `mirror_[e]`.

| Word | Score s | Word when s < 0.33 / s < 0.66 / s ≥ 0.66 |
|---|---|---|
| Shape | s = 0.5·(1 − recipe.depth) + 0.5·recipe.body | Round / Full / Dense |
| Colour | s = 0.5·recipe.edge + 0.25·(recipe.shift + 1) | Dark / Warm / Bright |
| Force | s = max(foldAmount / 100, characterAmount, excitation.mix · excitation.drive) | Clean / Driven / Aggressive |

- Each threshold belongs to the upper band: s = 0.33 gives the second word, s = 0.66 the third.
- These are pure functions of the endpoint and are never stored.
- SC-027(d) gates whatever table D2 ratifies, so a change at ratification is only a table edit.

### 3.2 D3 and D4 are generated, not retyped (FR-005, FR-007)

A hidden probe case, `Profundum_UiContractTables` (`[.][probe]`) in `unit/controller/macro_ui_test.cpp`, prints two CSVs:

- **For D3:** each `kRows` row with its target's registered ID (from `kMacroTargetParamIds`, §4.10), its unit, and its
  "no control" and "inert" flags.
- **For D4:** each `kLawTable` row with its law, axis, lo and hi.

D3 and D4 paste these between `<!-- GENERATED-BEGIN … -->` and `<!-- GENERATED-END -->` markers. The probe is re-run at close,
and the compliance row cites the (empty) diff.

FR-001 and P-1 apply here too: in 12a, D3 and D4 are first drafted by reading the headers (the citations in §1.5), and in 12b
the probe regenerates them.

**SC-006(a) mapping (the "audibility row" column of D3).** These rows were read from Phase 9 handoff 12.2
(`specs/profundum-phase9-voice-engine/evidence/phase-handoffs.txt:178–199`) and from `evidence/audibility-table.csv`:

| Control | ID | Row |
|---|---|---|
| Fold | 500 | AT-03 |
| Anchor level | 302 | AT-10 |
| Mass | 104 | AT-26 |
| the rest | | resolved row by row in D3, against the CSV or against `Profundum_ParamPathAudibility` |

A main-page control with no gated row is a 12a defect (SC-006(a)).

### 3.3 FR-006 render set (subject to P-1)

`Profundum_MainPageRenderSet` (`[.listen]`) renders:

- **Base:** the default patch, with the `Processor` defaults and every Advanced-only ID at its registered default.
- **Variations:**
  - one render per D1 main-page control at its min, default and max;
  - four combined "patch" renders that D1 lists.
- **Notes:** E1 and C3 at 48 kHz, held for 3 s plus a 1 s tail.
- **Output:** 32-bit float stereo WAVs plus `manifest.txt`, in `specs/profundum-phase12-ui/evidence/listen/`.
- **WAV writer:** TU-local. `profundum_core_spectral_test.cpp:1561–1600` keeps its own writer TU-local too, and a grep finds
  no shared one.

The user writes the dated verdict in Clarifications. An agent never writes it.

### 3.4 FR-097 account of the Phase 9 listening verdicts (Clarifications; user-filled)

Before 12b starts, 12a adds to spec.md's Clarifications a table "Phase 9 listening verdicts (FR-097)" with exactly these eight
rows. The sources are `specs/profundum-phase9-voice-engine/evidence/phase-handoffs.txt:201–205` and the listening-sheet lines it
cites.

| Row | `listening-sheet.md` row | Verdict line | Status written by 12a |
|---|---|---|---|
| H-1 | :21 | :35 | pending |
| H-2 | :37 | :43 | pending |
| H-8 | :45 | :57 | pending |
| H-10 | :59 | :72 | pending |
| H-14 | :74 | :87 | pending |
| H-19 | :89 | :104 | pending |
| H-28(1) | :106 | :119 | pending |
| H-28(2) | :121 | :136 | pending |

- The agent writes each row as "pending" and nothing else. Only the user replaces a status with "given (YYYY-MM-DD): verdict".
  An agent never writes "given" or a verdict (FR-097, SC-001).
- A verdict that asks for a change is written first as a ruling in its owning FR / SC row, then referenced here (memory rule
  "ruling goes in the FR / SC row first").
- A row may still read "pending" at ratification. SC-001 accepts "pending"; it rejects a missing or blank row.
- The renders for these rows come from Phase 9's own `evidence/close/listen.sh` (handoff :201–205), which writes only under
  `specs/profundum-phase9-voice-engine/evidence/listen/`. Whether running it before ratification is allowed under FR-001 is
  folded into Open Question 1.

### 3.5 D5 and D6 content (FR-008, FR-009)

**D5, `design/voice-note.md`** (a cross-reference; it adds no behaviour). Sections:

1. Stealing and retrigger: deterministic and documented, cited to Phases 4 and 9. This includes the allocator's same-note
   retrigger (`voice_allocator.h:237–242`).
2. The POLY steal order: oldest released, then oldest active.
3. The Note Memory modes (415), quoted as the registered strings (`getParameterObject(415)->toString`).
4. A pointer to D2's focus rule (§4.3.2), which says which voice the spectrum shows in POLY.

Every statement cites its Phase 4 or Phase 9 source line.

**D6, `design/interaction-automation.md`.** Sections:

1. **Control classification.** Every control of D1 and D2 is listed as either "host parameter (ID)" or "UI-only (session
   tag)". The host-parameter rows are the `<control-tags>` of §4.12. The UI-only rows are exactly the §4.7 session-tag table.
2. Compare and Copy are UI-only messages (`kMsgCompare`, `kMsgCopyEndpoint`), and the compare writes no state. Compare A or B
   also drives 1001 through one gesture; A·B does not (FR-049).
3. Edit Target 1001 is registered and non-automatable (Phase 11 Q6).
4. The macros 100–103 and Morph Position 105 are automatable. MIDI learn is host-level only: no `IMidiLearn`, and
   `IMidiMapping` is unchanged (FR-098).
5. Session UI state, never persisted: the page, the Advanced tab, the zoom step and the highlight states. The endpoint names
   are not session state: they are persisted (FR-047).

**Drift guard.** The D6 UI-only list is copied into `ui_strings_test_table.h` as `kD6UiOnlyTags`, next to the D2 strings.
`Profundum_UiPlacement` (f) asserts that every tagged `CControl` in the built tree is either bound to a registered ID (< 9000)
or carries a tag in `kD6UiOnlyTags`, and that every entry of `kD6UiOnlyTags` is present in the tree. The compliance row diffs
D6's table against `kD6UiOnlyTags`, so D6, §4.7 and the uidesc cannot drift apart unnoticed.

---

## 4. Stage 12b — component design

### 4.1 Engine and voice read accessors (`dsp/`, Layer 3, append-only)

The accessors are appended to the **public** sections:

- in the engine, after `oversamplerStateFinite()` (`profundum_engine.h:422`);
- in the voice, after `stateFinite()` (`profundum_voice.h:695–698`).

They add no members, no writes and no includes.

```cpp
// profundum_engine.h — Phase 12 FR-075 / FR-095: display read path. const, allocation-free, behaviour-neutral.
/// Position inside the current 32-sample control interval; 0 exactly at a grid point (the next processBlock runs gridStep).
[[nodiscard]] std::size_t controlGridPhase() const noexcept { return gridPhase_; }
/// The engine's sample clock (reset() sets 0); ProfundumVoiceTaps::sample uses the same clock.
[[nodiscard]] std::uint64_t samplePosition() const noexcept { return pos_; }
/// The voice mode applied at the last grid point (pending_ applies there, C-9).
[[nodiscard]] VoiceMode appliedVoiceMode() const noexcept { return applied_.mode; }
/// Read-only view of voice v (v is clamped to kMaxVoices - 1).
[[nodiscard]] const ProfundumVoice& voiceForDisplay(std::size_t v) const noexcept {
    return voices_[std::min(v, kMaxVoices - 1)];
}
/// True when voice v rendered in the current (last begun) interval (rendering_, S6.6); false past kMaxVoices.
[[nodiscard]] bool voiceRenderedThisInterval(std::size_t v) const noexcept {
    return v < kMaxVoices && rendering_[v];
}
/// MIDI note voice v sounds: MONO -> voice 0 holds monoActiveNote_ (-1 none); POLY -> the allocator's note (-1 idle).
[[nodiscard]] int voiceMidiNote(std::size_t v) const noexcept {
    if (v >= kMaxVoices) return -1;
    if (applied_.mode == VoiceMode::Mono) return v == 0 ? monoActiveNote_ : -1;
    return allocator_.getVoiceNote(v);
}
```

```cpp
// profundum_voice.h — Phase 12 FR-075: the current interval's scratch, [0, k_). Valid until the next beginInterval.
[[nodiscard]] std::size_t intervalSamplesRendered() const noexcept { return k_; }
[[nodiscard]] std::span<const float> characterTapL() const noexcept { return {mainL_.data(), k_}; }
[[nodiscard]] std::span<const float> characterTapR() const noexcept { return {mainR_.data(), k_}; }
[[nodiscard]] std::span<const float> anchorTapL() const noexcept { return {anchorL_.data(), k_}; }
[[nodiscard]] std::span<const float> anchorTapR() const noexcept { return {anchorR_.data(), k_}; }
```

- Both headers already include `<span>` and `<algorithm>` (`profundum_engine.h:70–75`; the voice already uses `std::span` in
  its taps).
- The ODR sweep (§9) found 0 matches for every new name.
- The voice already has a **private** `gridPhase()` (:717). The engine accessor is therefore named `controlGridPhase`, to avoid
  two near-identical names.

**Validity contract.** The producer reads these accessors only after a `processBlock` call that rendered at least one sample
and returned with `controlGridPhase() == 0`. At that point:

- the voice's `k_ == intervalLen_ == 32`;
- `mainL_[0..32)` holds the whole completed interval, including samples written during an earlier `process()` call, because
  the scratch is indexed by position within the interval (:864).

The next `gridStep`, at the start of the next `processBlock`, calls `beginInterval`, which resets `k_` (:379).

### 4.2 `ProfundumSpectrumFrame` (`plugins/profundum/src/processor/spectrum_frame.h`)

Processor and controller share this header, as they share `plugin_ids.h`. It includes only the standard library.

```cpp
namespace Profundum {
inline constexpr std::uint32_t kProfundumFrameUserContextId = 0x50524644u;   // 'PRFD'; != 0x4D425452 / 0x53434C44 / 0x5645434F
inline constexpr std::size_t kSpectrumMaxPartials = 64;                     // FR-075 capacity
inline constexpr std::uint8_t kFrameFlagSilent = 0x01;          // no focus voice: numPartials == 0
inline constexpr std::uint8_t kFrameFlagPitchUnstable = 0x02;   // f0 drifted > kPitchStableTolerance in the window
inline constexpr std::uint8_t kFrameFlagPoly = 0x04;            // applied voice mode POLY

struct ProfundumSpectrumFrame {
    std::uint32_t sequence = 0;        // 0  monotonic per producer
    float sampleRate = 0.0f;           // 4  FR-073
    float f0Hz = 0.0f;                 // 8  the latched f0 of the window
    float anchorPower = 0.0f;          // 12 (sum aL^2 + sum aR^2) / N: a centred sine of amplitude a reads a^2
    std::uint32_t windowSamples = 0;   // 16 N
    std::uint8_t numPartials = 0;      // 20 count of §5.3; 0 when silent
    std::uint8_t flags = 0;            // 21
    std::uint8_t focusVoice = 0;       // 22
    std::uint8_t reserved = 0;         // 23
    std::uint64_t windowEndSample = 0; // 24 engine sample position one past the window's last sample
    float partialPower[kSpectrumMaxPartials] = {};  // 32 power at n*f0, n = index + 1 (§5.2); 0 past numPartials
};
static_assert(sizeof(ProfundumSpectrumFrame) == 288 && sizeof(ProfundumSpectrumFrame) <= 320);  // SC-030
static_assert(sizeof(ProfundumSpectrumFrame) % 32 == 0);   // the DataExchange block needs no padding (SC-023)
static_assert(std::is_trivially_copyable_v<ProfundumSpectrumFrame> && std::is_standard_layout_v<ProfundumSpectrumFrame>);
static_assert(offsetof(ProfundumSpectrumFrame, windowEndSample) == 24 &&
              offsetof(ProfundumSpectrumFrame, partialPower) == 32);
}
```

- The frame carries both the sample rate **and** f0, so partial n's frequency is always n·f0 at the frame's own rate (FR-073).
- It travels as native-endian raw bytes, as Vorago's frame does (`ecosystem_frame.h:11–13`).

### 4.3 `ProfundumSpectrumProducer` (`plugins/profundum/src/processor/spectrum_producer.h`, header-only)

A plugin-local class in namespace `Profundum`. The processor owns it. It holds about 3 KB of fixed-size state and never
allocates after construction.

```cpp
class ProfundumSpectrumProducer {
public:
    static constexpr int kPeriodsPerWindow = 8;                      // K (FR-075)
    static constexpr double kPitchStableTolerance = 1.0e-3;          // relative f0 drift flag (§5.2)
    static constexpr double kSilenceRepublishSeconds = 0.25;         // a silent producer re-publishes silence at 4 Hz
    static constexpr std::uint32_t kMinWindowSamples = 64;

    void prepare(double sampleRate) noexcept;             // control thread; stores fs, then reset()
    void reset() noexcept;                                // bounded; window off, focus none, seq table cleared
    void noteOnArrived(std::uint8_t note) noexcept;       // audio thread: noteSeq_[note] = ++arrivalCounter_
    /// Audio thread, once per completed engine interval (controlGridPhase() == 0 after a render): focus + feed.
    void onInterval(const Krate::DSP::ProfundumEngine& engine) noexcept;
    /// The seam that unit tests inject through: one interval's taps of the focus voice; f0Hz = currentCoreF0().
    void feedInterval(std::span<const float> cL, std::span<const float> cR, std::span<const float> aL,
                      std::span<const float> aR, float f0Hz, std::uint8_t focusVoice, bool poly,
                      std::uint64_t intervalStartSample) noexcept;
    /// No focus voice this interval (silence path).
    void feedSilence(std::uint64_t intervalEndSample) noexcept;
    /// Audio thread, end of process(): copies the newest staged frame once and returns true; false when none is staged.
    [[nodiscard]] bool takeFrame(ProfundumSpectrumFrame& out) noexcept;
    // test reads
    [[nodiscard]] std::uint32_t windowsCompleted() const noexcept;   // SC-030
    [[nodiscard]] int focusVoice() const noexcept;                    // -1 none (SC-025)
private:
    // window
    double fs_ = 48000.0; bool windowActive_ = false; std::uint32_t N_ = 0, k_ = 0; int count_ = 0;
    double f0Latched_ = 0.0, maxRelDev_ = 0.0; bool invalid_ = false;
    std::array<double, kSpectrumMaxPartials> coef_{}, s1L_{}, s2L_{}, s1R_{}, s2R_{};
    double hannC_ = 1.0, hannS_ = 0.0, rotC_ = 1.0, rotS_ = 0.0;      // periodic Hann phasor (§5.2)
    double anchorSq_ = 0.0;
    // focus
    std::array<std::uint64_t, 128> noteSeq_{}; std::uint64_t arrivalCounter_ = 0; int focus_ = -1;
    int focusOfWindow_ = -1;
    // output
    ProfundumSpectrumFrame staged_{}; bool hasStaged_ = false; std::uint32_t sequence_ = 0, windows_ = 0;
    std::uint32_t invalidWindows_ = 0;
    bool silentReported_ = false; std::uint64_t silentSince_ = 0; std::uint64_t silenceRepublishSamples_ = 12000;
};
```

#### 4.3.1 `onInterval` (audio thread; O(kMaxVoices) plus the feed)

1. Compute `focus` (§4.3.2).
2. If `focus < 0`, call `feedSilence(engine.samplePosition())` and return.
3. Otherwise take `const ProfundumVoice& v = engine.voiceForDisplay(focus)`.
4. If `v.intervalSamplesRendered() != 32`, the interval is incomplete: do not feed it.
5. Otherwise call `feedInterval(v.characterTapL(), v.characterTapR(), v.anchorTapL(), v.anchorTapR(), v.currentCoreF0(),
   focus, engine.appliedVoiceMode() == Poly, engine.samplePosition() − 32)`.

#### 4.3.2 Focus rule (FR-074; D2 states it verbatim)

> The focus is the sounding voice whose note was pressed most recently and is still held. When no voice is held, the last
> focus is kept while that voice still sounds (its release tail). When it stops, the frame reports silence.

- **MONO** (`appliedVoiceMode() == Mono`): `focus = 0` if `voiceRenderedThisInterval(0)`, else −1.
- **POLY:**
  - Candidates are the voices with `voiceRenderedThisInterval(v)`, `voiceForDisplay(v).held()` and
    `note = voiceMidiNote(v) ≥ 0`.
  - Among the candidates, pick the one with the greatest `noteSeq_[note]`. Ties cannot occur, because the counter only
    increases.
  - If no voice is held, keep `focus_` while `voiceRenderedThisInterval(focus_)` holds; otherwise the focus is −1.
- `Processor::dispatchEvent` writes `noteSeq_` through `noteOnArrived`, before it calls `engine_->noteOn` (§4.4). Consequences:
  - a same-note retrigger refreshes the sequence, and the allocator retriggers the same voice (`voice_allocator.h:237–242`);
  - a steal moves the focus to the stealing note's voice;
  - releasing the newest held note moves the focus back to the newest note still **held**. This is SC-025's fourth step
    ("returns to the earlier one while it sounds").
- A focus change restarts the window. No partial-window frame is published.

#### 4.3.3 `feedInterval` (audio thread)

1. If `!windowActive_` or `focusVoice != focusOfWindow_`, call `startWindow(f0Hz, focusVoice)`. If f0 is not usable
   (§5.3), call `feedSilence` instead and return.
2. Track drift: `maxRelDev_ = max(maxRelDev_, |f0Hz / f0Latched_ − 1|)`. The value is finite-checked by bit pattern with
   `Krate::DSP::detail::isFinite` (`core/db_utils.h`).
3. For each of the 32 samples:
   - if any of the four inputs is non-finite, set `invalid_ = true` and use 0 for that sample;
   - otherwise apply the window w = 0.5 − 0.5·`hannC_`, update the 2 × `count_` Goertzel states (§5.2), and add
     aL² + aR² to `anchorSq_`;
   - rotate the Hann phasor and increment `k_`;
   - when `k_ == N_`, call `finishWindow(intervalStart + i + 1)` and `startWindow(f0Hz, focus)`, then carry on with the rest of
     the interval.
4. `finishWindow`:
   - if `invalid_` is set or any power is non-finite, publish nothing and increment `invalidWindows_`;
   - otherwise fill `staged_` as listed below, then set `hasStaged_ = true`, increment `windows_`, and clear `silentReported_`.

   | Field | Value |
   |---|---|
   | `sequence` | `++sequence_` |
   | `sampleRate` | `fs_` |
   | `f0Hz` | `f0Latched_` |
   | `numPartials` | `count_` |
   | `partialPower[i]` | §5.2 for i < `count_`, 0 beyond |
   | `anchorPower` | `anchorSq_ / N_` |
   | `flags` | `kFrameFlagPitchUnstable` if `maxRelDev_` exceeds the tolerance; `kFrameFlagPoly` in POLY |
   | `windowEndSample` | the argument |

#### 4.3.4 `feedSilence`

1. Deactivate the window.
2. Stage a silence frame if either `!silentReported_` or `intervalEnd − silentSince_ ≥ silenceRepublishSamples_`. A silence
   frame has `flags = kFrameFlagSilent`, `numPartials = 0`, `f0Hz = 0`, every power 0 and `sampleRate = fs_`.
3. When a silence frame was staged, set `silentReported_ = true` and `silentSince_ = intervalEnd`.

This meets SC-010's "silence within one publish after the voice ends": the first interval with no focus voice stages the
silence frame, and the same `process()` call publishes it.

#### 4.3.5 `takeFrame`

If `hasStaged_` is set, copy `staged_` out, clear the flag and return true. Only the newest staged frame survives, which gives
"at most one per `process()`" (FR-071).

#### 4.3.6 RT notes

All state is fixed-size members. The producer never allocates, locks, throws or does I/O.

| Where | Transcendental calls |
|---|---|
| Per sample | none |
| Per window start (at most about 130 Hz, at C6) | `std::cos(2π/N)` and `std::sin(2π/N)` for the Hann phasor, and one `std::cos(ω₁)` to seed the Chebyshev recurrence (§5.2) |

The double-precision arithmetic is deliberate (§8 R-2).

### 4.4 Processor changes (`processor.{h,cpp}`)

**New members and seams**

```cpp
// processor.h (forward-declare Steinberg::Vst::DataExchangeHandler, as Vorago processor.h:53 does)
Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint* other) override;
Steinberg::tresult PLUGIN_API disconnect(Steinberg::Vst::IConnectionPoint* other) override;
// ---- Phase 12 test seams (shipping logic branches only on the gate) ----
void setSpectrumProducerEnabledForTest(bool on) noexcept { spectrumForced_ = on; }       // seam arm (SC-010 - SC-013, SC-030)
[[nodiscard]] const ProfundumSpectrumFrame& lastSpectrumFrameForTest() const noexcept { return lastFrame_; }
[[nodiscard]] std::uint32_t spectrumFramesPublishedForTest() const noexcept { return framesPublished_; }
[[nodiscard]] std::uint32_t spectrumMaxFramesPerProcessForTest() const noexcept { return maxFramesPerCall_; }
[[nodiscard]] const ProfundumSpectrumProducer& spectrumProducerForTest() const noexcept { return spectrum_; }
[[nodiscard]] std::uint64_t spectrumSkippedBlocksForTest() const noexcept { return skippedBlocks_; }
[[nodiscard]] bool spectrumQueueOpenForTest() const noexcept { return queueState_.load() == SpectrumQueueState::Open; }
[[nodiscard]] EndpointName endpointNameForTest(Krate::DSP::BassMorphEngine::Endpoint e) const;  // copies under the mutex
private:
void publishSpectrumFrame() noexcept;
std::unique_ptr<Steinberg::Vst::DataExchangeHandler> dataExchangeHandler_;
ProfundumSpectrumProducer spectrum_;
ProfundumSpectrumFrame lastFrame_{};
bool spectrumForced_ = false;
enum class SpectrumQueueState : std::uint8_t { Closed, ProbePending, Open };
std::atomic<SpectrumQueueState> queueState_{SpectrumQueueState::Closed};  // setActive / disconnect (control) and the probe (audio)
std::uint32_t framesPublished_ = 0, maxFramesPerCall_ = 0; std::uint64_t skippedBlocks_ = 0;
std::array<EndpointName, 2> names_{};   // FR-047; non-audio threads only, guarded by namesMutex_
mutable std::mutex namesMutex_;
```

`sizeof(Processor)` grows by about 3.5 KB and stays well under the 64 KB `static_assert` (`processor.h:207`). The mutex is
never taken on the audio thread: names are touched only by `notify`, `getState` and `setState`.

**`connect` / `disconnect`**

- Copy Vorago `processor.cpp:265–303`, with these settings:
  - `blockSize` = `sizeof(ProfundumSpectrumFrame)` rounded up to 32, which is 288;
  - `numBlocks = 4`;
  - `alignment = 32`;
  - `userContextID = kProfundumFrameUserContextId`.
- `disconnect` stores `queueState_ = Closed`, then calls `onDisconnect` and `reset()`.

**`setActive`, `setupProcessing`, `dispatchEvent`**

- `setActive(true)`: call `dataExchangeHandler_->onActivate(processSetup)` when the handler exists, then `spectrum_.reset()`,
  then store `queueState_ = handler ? ProbePending : Closed`.
- `setActive(false)`: store `queueState_ = Closed`, then call `onDeactivate()`, before the existing release and reset.
- `setupProcessing`: call `spectrum_.prepare(sr)` after `engine_->prepare`. This runs on the control thread.
- `dispatchEvent`: in the velocity > 0 branch, call `spectrum_.noteOnArrived(pitch)` before `engine_->noteOn`.

**`process()`**

**Queue probe and gate.** The SDK gives the processor no open / closed signal: `onActivate` returns `void` and `isOpen()` is
private (§1.7). So the first `process()` after activation observes it, once, before the slice loop:

```cpp
if (queueState_.load(std::memory_order_acquire) == SpectrumQueueState::ProbePending) {   // implies a handler exists
    const auto block = dataExchangeHandler_->getCurrentOrNewBlock();
    const bool open = block.blockID != Steinberg::Vst::InvalidDataExchangeBlockID && block.data != nullptr &&
                      block.size >= sizeof(ProfundumSpectrumFrame);
    queueState_.store(open ? SpectrumQueueState::Open : SpectrumQueueState::Closed, std::memory_order_release);
}
const bool spectrumOn = queueState_.load(std::memory_order_relaxed) == SpectrumQueueState::Open || spectrumForced_;
```

- Nothing has been sent in this activation and `numBlocks = 4`, so the queue cannot be full. An invalid block therefore means
  the queue did not open: no host `IDataExchangeHandler` and no `Timer`, or no `IHostApplication` (`dataexchange.cpp:79–85`).
- A valid block stays locked as the handler's current block until it is sent (`lockBlock`, :249–250), so the first publish
  reuses it and nothing is lost.
- The probe is one call per activation, on the audio thread as the SDK requires ("call only in process call",
  `dataexchange.h:90`). The fallback's blocks are allocated in `openQueue` (:86–93), not here. SC-012's connected arm measures
  the probe under `AllocationScope` (§6.2).
- A closed queue stays closed for the rest of the activation. The next `setActive(true)` probes again.

With the gate off, the slice loop is the Phase 11 code byte for byte. With it on, the call at :430 becomes:

```cpp
const std::size_t n = sliceEnd - cursor;
if (spectrumOn) {
    std::size_t done = 0;
    while (done < n) {
        const std::size_t toGrid = Krate::DSP::ProfundumEngine::kControlInterval - engine_->controlGridPhase();
        const std::size_t m = std::min(n - done, toGrid);
        engine_->processBlock(outL + cursor + done, outR + cursor + done, m);
        if (engine_->controlGridPhase() == 0) {
            spectrum_.onInterval(*engine_);
        }
        done += m;
    }
} else {
    engine_->processBlock(outL + cursor, outR + cursor, n);
}
applyOutputGain(outL + cursor, outR + cursor, n);   // unchanged
++lastSliceCount_;                                   // still counts EVENT slices (midi_event_test.cpp:459, :466)
```

After the loop and before `silenceFlags` is set, call `publishSpectrumFrame()`.

**`publishSpectrumFrame()` (audio thread)**

1. `if (!spectrum_.takeFrame(lastFrame_)) return;`. Then increment `framesPublished_` and update the per-call maximum.
2. If `queueState_` is not `Open`, return. This is the seam arm, or a closed queue.
3. Call `getCurrentOrNewBlock()`. If `blockID == InvalidDataExchangeBlockID`, `data == nullptr` or `size < sizeof(frame)`,
   increment `skippedBlocks_` and return. A skipped block is never retried (Vorago's :556–561 rule).
4. `memcpy` the frame into the block and call `sendCurrentBlock()`.

**Guard paths.** FR-071's "no-op when no queue is open" holds by the gate. With `queueState_` not `Open` and no seam,
`process()` runs the Phase 11 slice loop: `onInterval`, `feedInterval` and the Goertzel bank never run, and after the single
probe no block is requested. The unprepared path and the parameter-only path never reach the producer either. The
closed-queue case is gated by SC-023's closed-queue arm (§6.2).

### 4.5 State v3 and endpoint names (FR-047, FR-096, SC-027)

**`plugin_ids.h`**

```cpp
constexpr Steinberg::int32 kCurrentStateVersion = 3;
constexpr Steinberg::int32 kStateV2Version = 2;
constexpr std::size_t kEndpointNameMaxBytes = 31;              // UTF-8 bytes; 1 length byte + 31 = 32 per endpoint
constexpr std::size_t kEndpointNameBlockBytes = 32;
constexpr std::size_t kStateNamesOffset = kStateV2Bytes;       // 510: the Phase 11 "append after byte 510" rule
constexpr std::size_t kStateV3Bytes = kStateNamesOffset + 2 * kEndpointNameBlockBytes;   // 574
inline constexpr const char* kMsgEndpointName = "ProfundumEndpointName";   // ctl -> proc, int "endpoint", binary "name" (0..31 B)
static_assert(kStateV1Version + 1 == kStateV2Version && kStateV2Version + 1 == kCurrentStateVersion);
```

The line above replaces the existing `static_assert(kCurrentStateVersion == 2 && …)` (:48–49). The v2 layout asserts are
unchanged.

| Bytes | Block | Content |
|---|---|---|
| 0–509 | v2 body | unchanged, except the version word is 3 |
| 510–541 | name A | u8 length L (0–31), then L UTF-8 bytes, zero-padded to 31 |
| 542–573 | name B | same layout |

**`parameters/endpoint_names.h`** (new; used by processor and controller)

- `struct EndpointName { std::array<char, kEndpointNameMaxBytes> bytes{}; std::uint8_t length = 0; }`.
- `defaultEndpointName(Endpoint)` returns "A" or "B".
- `sanitizeEndpointName(std::string_view) -> EndpointName` applies these steps in order:
  1. drop control bytes (< 0x20 and 0x7F);
  2. drop invalid UTF-8 sequences;
  3. truncate at the last code-point boundary that fits in 31 bytes.

  An empty result means the caller uses the default name.
- `writeEndpointName(IBStreamer&, const EndpointName&)`.
- `readEndpointName(IBStreamer&, EndpointName&) -> bool` reads exactly 32 bytes. It rejects a length above 31 and invalid UTF-8.

**Processor**

- `setState`:
  - **Change :504 from `version < kCurrentStateVersion` to `version == kStateV1Version`.** A v1 stream resets as it does today,
    and the names become the defaults.
  - A v2 or v3 stream runs `loadV2Tail` as today.
    - For v2, the names then become the defaults.
    - For v3, each `readEndpointName` that succeeds replaces that endpoint's name. A short or invalid block keeps the prior
      name (the Phase 11 truncation rule), and the chain stops at the cut.
  - Every name write happens under `namesMutex_`.
- `getState` writes the two name blocks after the runtime blob, under the mutex.
- `notify` gains a `kMsgEndpointName` branch:
  - validate the endpoint (0 or 1);
  - sanitise the binary attribute;
  - store the result under the mutex.

  It does not call `flushControllerSync`, because nothing is reported back. `process()` never touches the names.

**Controller**

- `setComponentState`:
  - change `else if (version >= kCurrentStateVersion)` (:117) to `else if (version >= kStateV2Version)`;
  - after the endpoints, if `version >= 3` and the chain is intact, skip the 20-byte runtime blob (`readRaw` into a local), then
    read both names;
  - for v1 and v2, the names are the defaults.
- New `setEndpointName(Endpoint, std::string_view)`. It:
  1. sanitises the input;
  2. stores it in `names_[e]`, so the UI shows it at once;
  3. sends `kMsgEndpointName`;
  4. calls `setDirty(1u)`;
  5. refreshes the morph component.
- Copy A → B does **not** copy names. The processor's copy is `MorphState`-only (`applyPendingCopy`, `processor.cpp:647`).
  This is Open Question 4.

### 4.6 Controller (`controller.{h,cpp}`)

The class gains two bases, `public Steinberg::Vst::IDataExchangeReceiver` and `public VSTGUI::VST3EditorDelegate`, as in the
Vorago class head (`plugins/vorago/src/controller/controller.h:56–59`), and adds `DEF_INTERFACE(IDataExchangeReceiver)`.
`IMidiLearn` is **not** added (FR-098).

#### 4.6.1 DataExchange consumer (FR-076)

- **`queueOpened(uid, blockSize, dispatchOnBackgroundThread = false)`.** If `uid == kProfundumFrameUserContextId`, reset
  `cachedFrame_` to a silence frame and record `queueOpenCount_` and `lastBlockSize_` (SC-023).
- **`queueClosed`** increments `queueCloseCount_`.
- **`onDataExchangeBlocksReceived`** filters on uid and size, keeps the newest block (Vorago :314–329) and increments
  `framesReceived_`.
- **`notify`** first does `if (dataExchangeReceiver_.onMessage(message)) return kResultOk;`, then the existing Phase 11
  dispatch.

`cachedFrame_` is a POD owned by the UI thread. It is written even while the editor is closed (FR-091). The views read it
through a `const ProfundumSpectrumFrame*`.

#### 4.6.2 Delegate hooks (FR-092)

`createCustomView(name, attrs, …)` builds the view rect from the `origin` and `size` attributes (Vorago :352–357).

| Name | Creates |
|---|---|
| `"HarmonicBarView"` | `new UI::HarmonicBarView(rect, &cachedFrame_, variant)`. `variant` comes from the `bar-variant` attribute: `main` (the Anchor / Mass split is drawn) or `core`. Stored in `barViews_[k++]` (at most 2) |
| `"SpreadCurveView"` | `new UI::SpreadCurveView(rect, &spreadModel_)` |
| anything else | `nullptr` |

- **`createSubController("ProfundumPanel")`** returns `new UI::ProfundumPanelSubController(this, editor)`. Any other name
  returns `nullptr`.
- **`didOpen(editor)`:**
  1. store `activeEditor_`;
  2. call `setAllowedZoomFactors({1.0, 1.25, 1.5, 2.0})`, then `setZoomFactor(kZoomSteps[zoomIndex_])`, so the session's zoom
     survives a reopen;
  3. start `editorSyncTimer_` (§4.6.7);
  4. recompute the rings, the SPREAD model, the captions and the dimming.
- **`willClose`:**
  1. stop and release `editorSyncTimer_`;
  2. null `barViews_`, `spreadView_`, `panel_` and `activeEditor_` (FR-091).

There is no preset browser overlay in 12b (P-9; Appendix A if the user rules it in).

#### 4.6.3 Compare read path (FR-046, FR-041, FR-049)

```cpp
[[nodiscard]] CompareMode compareMode() const noexcept { return compare_; }        // public, production getter
[[nodiscard]] std::uint32_t compareModeChangeCount() const noexcept { return compareChanges_; }
void pressCompare(CompareMode m);   // the UI entry point (FR-049)
```

**Notification.** In `notify`'s `kMsgCompareMode` branch (:298), after `compare_ = …`, add:

```cpp
++compareChanges_;
if (panel_) panel_->onCompareModeChanged(compare_);
```

That is exactly one notification per accepted message (SC-007(b)). The listener is the panel pointer the controller holds from
the panel's `verifyView` registration until `willClose`. There is no `std::function` and no allocation.

**`pressCompare(m)`:**

1. Call `setCompare(m)`. This sends the one `kMsgCompare`.
2. If `m == A` or `m == B`:

   ```cpp
   const double v = (m == B) ? 1.0 : 0.0;
   beginEdit(1001); performEdit(1001, v); setParamNormalized(1001, v); endEdit(1001);
   ```

   This is the FR-043 path, and it fires the Phase 11 hook at `controller.cpp:184–207`.
3. For **A·B**, only the message is sent.

The compare control never writes its own displayed state: right after a click, the panel resets the segment to
`compareMode()` (§4.7).

#### 4.6.4 Gesture tracking and refresh hooks

**`beginEdit(tag)` / `endEdit(tag)` overrides.** They:

- keep the open-gesture count `openGestures_`;
- for tags 100–103, call `panel_->onMacroEdit(tag, true/false)` (SC-009's move-without-hover arm);
- forward to `EditControllerEx1`.

**`setParamNormalized`** (the existing override) additionally, while the editor is open:

| ID changed | Calls |
|---|---|
| 414 | `panel_->refreshPortamentoReadout()` (FR-035) |
| 1000 or 1001 | `panel_->refreshAnchorDimming()` (FR-044) |
| 100–103, or any target in `kMacroTargetParamIds` | `refreshMacroRings()` (FR-061) |
| 800–805 | `refreshSpreadModel()` (FR-080) |

`redisplayEditTarget()` and the `notify` path that replaces a mirror both call `refreshEndpointCaptions()`,
`refreshMacroRings()` and `refreshSpreadModel()`. They have to, because they change all 60 E-values through the base
`setParamNormalized`, which bypasses the override.

#### 4.6.5 Macro rings (FR-061, SC-028)

`macroRingRange(ParamID id) const noexcept -> float`:

1. Look up the target index t for `id` in `kMacroTargetParamIds` (§4.10). Return 0 if there is none, if t is
   `CompressionAmount`, or if `kRows[t].amount == 0`.
2. Build the macro values by copy-then-assign. `getParamNormalized` returns `ParamValue` (double) and `ProfundumMacroValues`
   has float members (`profundum_macro_matrix.h:58–63`), so a brace init would narrow (R-10):

   ```cpp
   Krate::DSP::ProfundumMacroValues v{};
   v.weight = static_cast<float>(getParamNormalized(100));
   v.darkBright = static_cast<float>(getParamNormalized(101));
   v.tightLoose = static_cast<float>(getParamNormalized(102));
   v.cleanViolent = static_cast<float>(getParamNormalized(103));
   Krate::DSP::ProfundumMacroMatrix mm;
   mm.setMacros(v);
   ```

   Then take `c = mm.contributions()[t]`. This is the engine's own math (`profundum_macro_matrix.h:193–199`). Return 0 if c
   is ±0.
3. Take `const FieldInfo& f = lawOf(row.leaf)`, with the row `kSoundRows[kParamRoutes[id].index]`, and
   `base = plainFromNormalized(f, getParamNormalized(id))`. Then apply the `apply` rules (:204–219, :261–268):
   - for the amp times, `mod = clamp(base · 2^c, kMinEnvelopeTimeMs, kMaxEnvelopeTimeMs)`;
   - for everything else, `mod = clamp(base + c, f.lo, f.hi)`.
4. Return `float(normalizedFromPlain(f, mod) − getParamNormalized(id))`. It is signed and lies in [−1, 1].

`refreshMacroRings()` calls `setModulationRange` on each registered `ArcKnob` target. A target bound to any other control type
draws no ring. The knob's value is never written (FR-005). Motion is not drawn.

**Dead band.** `ArcKnob::setModulationRange` keeps the stored value unless the new one differs from it by more than 0.0005
(`arc_knob.h:74–78`). The shared header is not changed. So `macroRingRange(id)` is the value of record, exact to the test's
1e-6, and each knob's `getModulationRange()` agrees with it within 5e-4 (SC-028, §6.2).

#### 4.6.6 Endpoint captions (FR-040, FR-048)

`endpointCaption(Endpoint e) -> std::string` returns the name, then " — ", then the three words of
`UI::describeEndpoint(mirror_[e])` (§4.11) joined by " · ". It is recomputed on every mirror replacement and every name change.
It is never stored or sent.

#### 4.6.7 Editor sync timer (FR-090, SC-015)

- `static constexpr std::uint32_t kEditorSyncIntervalMs = 100;`
- In `didOpen`:

  ```cpp
  editorSyncTimer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
      [this](VSTGUI::CVSTGUITimer*) { onEditorSyncTick(); }, kEditorSyncIntervalMs, true);
  ```

- In `willClose`: `editorSyncTimer_->stop(); editorSyncTimer_ = nullptr;`.
- `onEditorSyncTick()`: `if (openGestures_ > 0) return;` (see §8 R-6), then `sendMessageID(kMsgRequestSync)`.
- Test surface:
  - `hasEditorSyncTimerForTest()`;
  - `editorSyncIntervalForTest()`, which returns `getFireTime()`;
  - `editorSyncTickForTest()`, which calls `onEditorSyncTick()`.

#### 4.6.8 SPREAD model (FR-080, SC-014)

```cpp
struct SpreadCurveModel {
    std::array<float, 64> hz{}, w{};
    float crossoverHz = 120.0f, lowerKneeHz = 0.0f, upperKneeHz = 0.0f;
    bool wide = false; bool solid = false;
};
```

`field_` is a `std::unique_ptr<Krate::DSP::BassStereoField>` created in `initialize`. The class is non-copyable
(`bass_stereo_field.h:168–169`). `field_` is never prepared, so its `reset()` never calls the crossover's `prepare`
(:205–207).

`refreshSpreadModel()` (UI thread only):

1. Read the displayed plain values of 800 (mode index), 801, 802, 803, 804 and 805 through each row's law.
2. Build `BassStereoField::Controls c{}` by copy-then-assign, to avoid brace narrowing.
3. Call `field_->setControls(c); field_->reset();`.
4. Compute `m = (mode == Solid) ? 0 : spread · (1 − κ·μ)`, with κ = `BassStereoField::kMotionExcursion` and m_mot = 0.
5. For i = 0..63, set `hz[i] = 20 · (10000 / 20)^(i / 63)` and `w[i] = field_->widthCurve(hz[i]) · m`.
6. Set `crossoverHz`, the two knee marks and the `wide` and `solid` flags. The marks are the law's own knees
   (`updateLawCache`, `bass_stereo_field.h:648–651`; the constants are public at :74–75):
   - `lowerKneeHz = f_c · 2^(−kLowerKneeMaxOctaves · (1 − G))`, which is 2^uLow and moves with Gravity (FR-080's "Gravity
     knee");
   - `upperKneeHz = f_c · 2^kUpperKneeOctaves` = 4·f_c.

The Motion excursion band (m_mot = ±1) is **not** drawn. That is a proposal for D2, which FR-080 lets D2 rule on.

#### 4.6.9 Zoom (FR-014, SC-032)

- `static constexpr std::array<double, 4> kZoomSteps{1.0, 1.25, 1.5, 2.0};`
- `int zoomIndex_ = 0`, session state only.
- `setZoomStep(int)` clamps and stores the index, and calls `activeEditor_->setZoomFactor(kZoomSteps[i])` when the editor is
  open. It writes no parameter and no state.

#### 4.6.10 `ADSRDisplay` adapter (FR-034, SC-024)

The panel's `verifyView` finds the `ADSRDisplay` (by `dynamic_cast`) and calls `owner->wireAdsrDisplay(display)`, which:

- calls `setAdsrBaseParamId(420)`, `setCurveBaseParamId(424)` and `set{Attack,Decay,Release}MaxMs(kMaxEnvelopeTimeMs)`;
- calls `setBezierEnabled(false)`;
- sets `setParameterCallback([this](uint32_t id, float n) { … })`. The callback drops any ID outside 420–426. For the rest it
  computes `v = adsrToRegistered(id, n)`, then calls `setParamNormalized(id, v)` and `performEdit(id, v)`;
- sets `setBeginEditCallback` and `setEndEditCallback` to `beginEdit` and `endEdit`.

`adsrToRegistered(id, n)`:

| ID | Conversion |
|---|---|
| 420, 421, 423 | `normalizedFromPlain(lawOf(leaf), ADSRDisplay::normalizedToTimeMs(n, kMaxEnvelopeTimeMs))` |
| 422 (Linear [0, 1]) | n, because the display emits the raw sustain level (`adsr_display.h:1024`) |
| 424–426 | n, because the display's (c + 1)·0.5 (`adsr_display.h:1322–1326`) equals the registered Linear [−1, 1] law (`morph_state.h:211–216`) |

In the other direction, on `setParamNormalized` and on re-display, the controller calls
`display->setAttackMs(plainFromNormalized(law, v))` and the matching setters for the other fields. The clamps agree on both
ends: `kMinTimeMs` (0.1) equals `kMinEnvelopeTimeMs`, and `kDefaultMaxTimeMs` (10000) equals `kMaxEnvelopeTimeMs`.

### 4.7 `ProfundumPanelSubController` (`src/ui/panel_sub_controller.{h,cpp}`, namespace `Profundum::UI`)

`class ProfundumPanelSubController : public VSTGUI::DelegationController, public VSTGUI::IViewEventListener`.

**Session tags.** All are ≥ `kSessionTagBase = 9000`; the highest registered ID is 1002.

| Tag | Control |
|---|---|
| 9100 | page strip (Main / Advanced) |
| 9110 | Advanced tab strip (Sections / Modulation / Settings) |
| 9200 | compare segment A / A·B / B; segment index ↔ `CompareMode`: 0 → A, 1 → AB, 2 → B |
| 9201 | Copy A → B |
| 9202 | Copy B → A |
| 9203 | compare segment on Advanced, Morph behaviour section (same mapping as 9200) |
| 9204 | Copy A → B on Advanced |
| 9205 | Copy B → A on Advanced |
| 9210 | name A (`CTextEdit`) |
| 9211 | name B (`CTextEdit`) |
| 9220 | caption label A |
| 9221 | caption label B |
| 9300 | zoom segment |

**`verifyView`** reads the raw attributes: `uidesc-label` (`page-main`, `page-advanced`, `tab-…`, `anchor-group`),
`session-tag`, the control tag and `custom-view-name`. It:

- collects the page and tab containers;
- collects the controls bound to 300–306 (for dimming);
- collects the macro controls 100–103 and calls `registerViewEventListener(this)` on each for hover, unregistering in its
  destructor;
- collects the `ArcKnob`s bound to `kMacroTargetParamIds` targets (for the rings);
- collects **every** control bound to an ID in any macro's highlight set, whatever its class, for the highlight. In the
  planned tree these are `ArcKnob`s, the `BipolarSlider` for 205 and the `ADSRDisplay` (found by `dynamic_cast`, and treated
  as bound to 420–426). The panel records each control's class; SC-009 fails if a highlight-set ID is bound to a class the
  panel cannot highlight;
- collects the 412 `CParamDisplay`, **every** compare segment (9200 on Main, 9203 on Advanced) and the caption labels;
- registers itself with the owner (`owner->registerPanel(this)`).

**`valueChanged` for session tags.** The panel swallows these, along with their begin and end edits (Vorago
`panel_sub_controller.h:23–27`).

| Session control | Action |
|---|---|
| page or tab | `setVisible` on the sibling containers. The selection is controller session state (`activePage_`, `activeTab_`) and is never persisted (SC-017) |
| compare (9200 or 9203) | `owner->pressCompare(modeOf(segment))`, then at once every collected compare segment is reset to `indexOf(owner->compareMode())`. A click alone changes nothing (FR-041) |
| copy (9201 / 9204, 9202 / 9205) | `owner->copyEndpoint(A/B)` |
| name commit | `owner->setEndpointName(e, text)` |
| zoom | `owner->setZoomStep(i)` |

**Macro highlight.**

- `viewOnEvent`: `MouseEnter` on macro m calls `setHighlight(m, true)`. `MouseExit` calls `setHighlight(m, false)`, unless an
  edit gesture on m is open.
- `onMacroEdit(tag, open)` comes from the controller's `beginEdit` / `endEdit`. While a gesture is open, m stays highlighted
  even after the pointer leaves the control (SC-009).
- `setHighlight(m, on)` recolours every collected control whose ID is in `highlightSet(m)` (§4.10) with the `highlight` colour
  token, or restores its stored colour, then calls `invalid()`. The mechanism is per class (§1.6):
  - `ArcKnob`: `setGuideColor`;
  - `BipolarSlider`: `setTrackColor`;
  - `ADSRDisplay`: `setControlPointColor`. The display is highlighted when any of 420, 421 or 423 is in the set.
- `highlightedIdsForTest()` returns the current set. SC-009 checks the controls' colours as well, not only this set (§6.2).

**`refreshAnchorDimming()`.**

- `dim = (index(1000) == Hold) && (index(1001) == B)`.
- Each 300–306 control gets `setAlphaValue(dim ? 0.45f : 1.0f)`, and the "held at A" label becomes visible when dimmed.
- `setMouseEnabled` is never touched, so the controls stay editable (FR-044).
- Test read: `isAnchorDimmedForTest(id)`.

**Other refreshes.**

- `refreshPortamentoReadout()` calls `portaDisplay_->invalid()` and increments `portaReadoutInvalidations_` (test read
  `portaReadoutInvalidationsForTest()`, SC-018). The display's value-to-string function then re-queries
  `getParamStringByValue`, which reads the current 414 (`controller.cpp:165–167`).
- `onCompareModeChanged(m)` sets the selection of every collected compare segment.

### 4.8 `HarmonicBarView` (`src/ui/harmonic_bar_view.{h,cpp}`)

`class HarmonicBarView : public VSTGUI::CView`. Constructor: `(const CRect&, const ProfundumSpectrumFrame* frame, Variant v)`.

- **Timer lifetime.** `attached()` creates a 33 ms `CVSTGUITimer` (the Vorago rate, `ecosystem_view.h:88`). `removed()`
  stops and releases it (FR-091).
- **Timer callback `onTick(dtSeconds)`:**
  - If `frame_->sequence != lastSeq_`:
    - a silent frame sets `decaying_ = true`;
    - otherwise compute the target heights h_n = clamp((10·log10(P_n / P_ref) + 60) / 60, 0, 1), with
      P_ref = max(max_n P_n, anchorPower). The Anchor height uses the same formula;
    - then update `lastSeq_`.
  - While decaying, subtract `dt / 0.300` from every bar, floored at 0. The decay is linear and monotonic, and reaches 0
    within 300 ms (SC-026).
  - Call `invalid()`.
- **`draw`:** the Sub / Body / Presence shading, one bar per n ≤ numPartials at x = log₂ n, and the Anchor lane. The `main`
  variant also draws the ANCHOR / MASS split.
- **Display-only:** `onMouseDown` returns `kMouseEventNotHandled`. The view has no `IControlListener`, and since a `CView` has
  no tag it can never issue `performEdit` (SC-024).
- **Test surface:** `barHeightsForTest()`, `anchorHeightForTest()`, `onTickForTest(dt)`, `hasTimerForTest()`.

### 4.9 `SpreadCurveView` (`src/ui/spread_curve_view.{h,cpp}`)

`class SpreadCurveView : public VSTGUI::CView`, drawing from a `const SpreadCurveModel*`.

- It draws w(f) on a log-frequency axis from 20 Hz to 10 kHz: 64 points joined by straight segments.
- The vertical scale runs from 0 (MONO) to 1 (WIDE).
- It marks the law's two knees, `lowerKneeHz` (Gravity-dependent) and `upperKneeHz` = 4·f_c (§4.6.8).
  `kneeMarksForTest()` returns the two mark positions in Hz.
- In WIDE it shows the label "WIDE: Character offsets not drawn".
- It has no timer: the controller calls `invalid()` after `refreshSpreadModel()`.
- `sampledValuesForTest()` returns `model_->w`.

### 4.10 `macro_ui_map.h` (`src/ui/`, constexpr, namespace `Profundum::UI`)

```cpp
inline constexpr Steinberg::Vst::ParamID kNoParamId = 0xFFFFFFFFu;
inline constexpr std::array<Steinberg::Vst::ParamID, Krate::DSP::ProfundumMacroMatrix::kNumTargets> kMacroTargetParamIds{
    kCoreDepthId, kCoreBodyId, kAnchorLevelId,                                          // Weight
    kCoreEdgeId, kCoreShiftId, kFoldShapeId, kCharacterToneId,                          // Dark <-> Bright
    kAmpAttackId, kAmpDecayId, kAmpReleaseId, kDynamicsTransientAttackId, kNoParamId,   // Tight <-> Loose (no Compression control)
    kCoreExcitationMixId, kCoreExcitationDriveId, kFoldAmountId, kCharacterAmountId};   // Clean <-> Violent
/// FR-060: true for the targets of m's kRows rows with amount != 0 (CompressionAmount excluded).
[[nodiscard]] constexpr std::array<bool, kNumRoutedIds> highlightMask(Krate::DSP::ProfundumMacro m) noexcept;
```

The array is in `ProfundumMacroTarget` order (`profundum_macro_matrix.h:41–47`). `static_assert`s check that:

- every entry except `CompressionAmount` is a `RouteKind::Sound` route in `kParamRoutes` (`sound_param_table.h:101`);
- the `CompressionAmount` entry is `kNoParamId`;
- a loop over `kRows` confirms that `kRows[t].target == t`.

### 4.11 `endpoint_descriptor.h` (`src/ui/`)

`[[nodiscard]] std::array<const char*, 3> describeEndpoint(const Krate::DSP::MorphState& s) noexcept` implements the D2 table
(§3.1) as pure arithmetic on the state's fields. The thresholds are named constants that D2 cites.

### 4.12 `editor.uidesc` (replaced wholesale; FR-030 – FR-036)

**Tags and colours.**

- `<control-tags>` lists all 80 visible IDs by name. The eight Phase 10 names keep their values (FR-033, SC-003). The hidden
  IDs 4–7 are absent.
- The colour tokens are `bg`, `panel`, `text`, `text-dim`, `accent`, `highlight`, `anchor`, `mass` and `track`. D7 sets their
  values, and SC-019 computes contrast from them.

**Structure.**

- The template `editor` is 960 × 600 with `minSize = maxSize`, and has `sub-controller="ProfundumPanel"` on its root.
- Inside it: the header, then two sibling `CViewContainer`s labelled `uidesc-label="page-main"` and `"page-advanced"`
  (FR-030; no `UIViewSwitchContainer`).
- Inside Advanced: three sibling tab containers (FR-013).
- Each FR-004 section is a `CViewContainer` with `uidesc-label="section-<name>"` (SC-031(b)).
- Main holds `mass-group` and `morph-anchor`. `morph-anchor` holds exactly `anchor-group` and `morph-band` (§3.1).
  `anchor-group` holds a `CTextLabel` "MORPH BEHAVIOUR" and the 1000-bound segment (FR-012, SC-005). `morph-band` holds the
  105 slider and the rest of the morph component (FR-022, SC-019).
- Advanced `section-morph-behaviour` holds 1000, 1002, the compare segment 9203 and the Copy buttons 9204 / 9205 (FR-004,
  SC-031(b)).

**Control class per parameter kind** (FR-032):

| Kind | Control |
|---|---|
| Lists: 1, 2, 300, 301, 413, 414, 415, 416, 600, 800, 807, 809, 900, 901, 905, 907, 911, 1000 | `COptionMenu` or `CSegmentButton` |
| Two-label lists: 410, 411, 808, 906, 909, 1001 | toggle or two-segment |
| Continuous, bipolar: 101, 102, 103, 205, 902 | `BipolarSlider` |
| Continuous, other | `ArcKnob`; 105 is a `CSlider` |
| 412 on Main | `ArcKnob` (writes 412) plus a display-only `CParamDisplay` readout |
| Readouts of 105 and 412 | display-only `CParamDisplay`, allowed only beside a writing control of the same ID on the same page (P-8) |
| 417 | `ArcKnob` that honours the step count: the panel sets `CKnobBase` stepping from `getParameterInfo(417).stepCount` |
| 420–426 | `ADSRDisplay` (§4.6.10), plus `ArcKnob`s for the same IDs on Advanced |

**Tooltips.** Every bound control has `tooltip="…"` (SC-019). The tooltips for FR-024, FR-051 and FR-052 hold the D2-ruled
strings.

Edits after the first write use XSLT, which is repo practice for any change touching more than 3 controls.

### 4.13 `entry.cpp`

Only the banner changes, to: "FR-018: shared view creators are registered by `controller.cpp`'s `ui/*.h` includes; Phase 12,
P-4". There is no functional change.

---

## 5. Algorithms

### 5.1 Why the producer leaves the audio bit-identical (FR-072, SC-011, SC-022)

- `processBlock(n)` already places a chunk boundary at every grid point and at every call boundary (:388–403).
- The Phase 11 loop already has call boundaries at event slices.
- The sub-slicing adds call boundaries **only at grid points**, where the engine already splits.
- So the sequence of `gridStep` and `renderChunk(m)` calls, and with it every float operation, is unchanged.
- `applyOutputGain` still runs once per event slice.
- The producer reads only through `const` accessors.

A render with the producer on therefore equals one with it off, bit for bit (max |Δ| = 0). SC-011's two arms measure this for
the plugin, and SC-022 measures it for the engine alone (`Profundum_EngineDisplayAccessorNeutral`).

### 5.2 Measurement

**Window.**

- N = clamp(round(K·fs / f0), kMinWindowSamples, 2²⁰), with K = 8.
- Periodic Hann: w[k] = ½(1 − cos(2πk / N)) for k = 0..N−1.
- It is generated by a rotation in double precision, with Δ = 2π / N:
  (cᵢ₊₁, sᵢ₊₁) = (cᵢ·cosΔ − sᵢ·sinΔ, sᵢ·cosΔ + cᵢ·sinΔ).
- The phasor is renormalised by 1/√(c² + s²) every 4096 samples.

**Goertzel**, for each partial n = 1..count and each channel X ∈ {L, R}, with ω_n = 2π·n·f0 / fs and coefficient
c_n = 2cos ω_n:

- The coefficients come from the Chebyshev recurrence cos(nω) = 2cosω·cos((n−1)ω) − cos((n−2)ω), in double, seeded with
  cos 0 = 1 and cos ω.
- Per sample: s ← w[k]·x[k] + c_n·s₁ − s₂; s₂ ← s₁; s₁ ← s.
- After N samples: |X_n|² = s₁² + s₂² − c_n·s₁·s₂, which equals |Σ_k w[k]x[k]e^(−jω_n k)|².
- The loop runs partial-inner over contiguous `std::array<double, 64>`, so MSVC, Clang and GCC can vectorise it.

**Normalisation.**

- For a periodic Hann, Σw = N/2 exactly. The amplitude estimate is therefore A_X,n = 2|X_n| / (N/2) = 4|X_n| / N.
- The published value is **P_n = ½(A_L,n² + A_R,n²) = 8(|L_n|² + |R_n|²) / N²**. A centred sine of amplitude a at n·f0
  reads P_n = a².
- Anchor: P_anchor = (Σ aL² + Σ aR²) / N, unwindowed, over the same N samples. A centred sine of amplitude a reads a².

**f0 handling.**

- f0 is latched at window start from `currentCoreF0()`.
- Each interval, the relative drift |f0 / f0_latched − 1| updates `maxRelDev_`. A window with `maxRelDev_ > 1e-3` is flagged
  `kFrameFlagPitchUnstable`.
- Rationale for 1e-3: partial n moves by one bin (f0 / K) when its relative error reaches 1 / (K·n). For n = 64 that is
  1 / (8·64) ≈ 2e-3, so 1e-3 keeps every partial within half a bin.

**SC-010 reference** (test side, in double; independent of the producer). f0 and N are **not** taken from the frame, so a
producer that latched a wrong f0 or N cannot pass by measuring the reference at the same wrong frequencies:

1. f0_ref = `midiNoteToFrequency(note)` (`core/midi_utils.h`). The fixtures hold the pitch static (Motion 900 Off, Drop
   Amount 0, no bend), so this is the sounding f0. N_ref = clamp(round(8·fs / f0_ref), kMinWindowSamples, 2²⁰).
2. Before any level is compared, assert `frame.sampleRate == fs`, `frame.windowSamples == N_ref` and
   `|frame.f0Hz / f0_ref − 1| ≤ 1e-4`. The 1e-4 covers float rounding of f0 with margin and is ten times tighter than the
   drift flag's 1e-3, so a mislatched f0 fails here. If the core applies an offset the fixture does not disable, that is a
   finding, not a reason to widen the tolerance.
3. Take the observer's `charL / charR` of the focus voice over `[frame.windowEndSample − N_ref, frame.windowEndSample)`. The
   window's position is the only value taken from the frame; with the pitch static, a mispositioned window changes no level.
4. For L and R, compute `goertzelPhasorWindowed(x, hann(N_ref), N_ref, fs, n·f0_ref)`. This is a direct sum, not a
   recurrence.
5. Then P_ref,n = 8(|G_L|² + |G_R|²) / N_ref², and the Anchor reference is (Σ aL² + Σ aR²) / N_ref.
6. Compare the frame to the reference in dB.

### 5.3 Partial count and Nyquist (FR-073, SC-010, SC-030)

- `count = min(64, ⌊(fs/2) / f0⌋)`, minus 1 if `count·f0 ≥ fs/2`, so a partial exactly at Nyquist is excluded.
- f0 is unusable when it is non-finite, ≤ 0, or ≥ fs/2. The silence path is taken in that case.

| Case | count |
|---|---|
| E1 @ 48 kHz | 64 |
| C3 @ 48 kHz | 64 |
| C6 = 1046.5 Hz @ 44.1 kHz | 21 (the SC-010 Nyquist fixture) |

### 5.4 Cadence (SC-030)

Windows run back to back, so frames per second = f0 / K.

| Note | f0 (Hz) | Window (samples @ 48 kHz) | Frames per second |
|---|---|---|---|
| E1 | 41.20 | 9320 | 5.15 |
| C3 | 130.81 | 2936 | 16.35 (see P-3) |

Over 2 s at 512-sample blocks this gives ⌊96000 / 9320⌋ = 10 windows at E1 and 32 at C3, minus the window restarted at
note-on. At both notes a window is longer than a block, so no `process()` call publishes more than one frame.

### 5.5 Macro ring (FR-061)

The formula is in §4.6.5. For the log-law amp times (unclamped), Δnorm = c·ln 2 / ln(tMax / tMin), independent of the base
value. That gives a quick hand check for SC-028.

### 5.6 Contrast (SC-019)

- Linearise each sRGB channel: C ≤ 0.04045 → C / 12.92; otherwise ((C + 0.055) / 1.055)^2.4.
- Relative luminance: L = 0.2126·R + 0.7152·G + 0.0722·B over the linearised channels.
- Ratio = (L₁ + 0.05) / (L₂ + 0.05), where L₁ is the lighter colour.

The test reads the text colour, and the background colour of the nearest opaque ancestor, from the built view tree.

---

## 6. Test plan

**Conventions.**

- Every plugin case lives in `profundum_tests` (namespace `ProfundumPluginTest`) and is tagged `[profundum]` plus the tags
  shown.
- New TUs that check bit patterns join the `-fno-fast-math` block.
- There are no bit-exact float goldens. Every equality is either an in-binary A/B of two live renders (SC-011, SC-022) or a
  comparison against an independent double-precision computation with a stated tolerance.

### 6.1 Coverage map

TU paths are under `plugins/profundum/tests/` unless they start with `dsp/`. A TU marked † already exists and is amended.

| FR / SC | TU | TEST_CASE |
|---|---|---|
| SC-001, FR-003 (§14 delta table), FR-097 | — | Review row, not a test: the SC-001 ratification checklist (§3.1, §3.4, §6.2) |
| FR-008, FR-009 | — | Review row (§3.5). FR-009's UI-only list is also checked by `Profundum_UiPlacement` (f) |
| FR-031, SC-002 | `unit/controller/editor_layout_test.cpp` | `Profundum_UidescBindsEverySurfaceId` (`[ui]`) |
| FR-033, SC-003 | `unit/controller/editor_layout_test.cpp` | `Profundum_UidescTagIntegrity` (`[ui]`) |
| FR-032, SC-004 | `unit/controller/editor_layout_test.cpp`; `unit/state_v2_test.cpp`† and the P-2 sites | `Profundum_UiClassRule` (`[ui]`); the existing state cases |
| FR-012, FR-020, FR-024, FR-036, FR-045, FR-051, FR-052, FR-093, SC-005 | `unit/controller/editor_layout_test.cpp` | `Profundum_UiLabels` (`[ui]`) |
| FR-006, SC-006(c) | `integration/main_page_sanity_test.cpp` | `Profundum_MainPageSanity` (`[ui][render]`) |
| FR-040 – FR-043, FR-049, SC-007 | `unit/controller/morph_component_test.cpp` | `Profundum_MorphComponentUi` (`[ui][morph]`) |
| FR-044, SC-008 | `unit/controller/morph_component_test.cpp` | `Profundum_MorphLockDimming` (`[ui][morph]`) |
| FR-060, SC-009 | `unit/controller/macro_ui_test.cpp` | `Profundum_MacroHighlight` (`[ui]`) |
| FR-061, SC-028 | `unit/controller/macro_ui_test.cpp` | `Profundum_MacroModulationRing` (`[ui]`) |
| FR-098, SC-029 | `unit/controller/macro_ui_test.cpp` | `Profundum_MidiLearnHostLevel` (`[ui]`) |
| FR-005, FR-007 | `unit/controller/macro_ui_test.cpp` | `Profundum_UiContractTables` (`[.][probe]`) |
| FR-070 – FR-075, SC-010 | `integration/spectrum_frame_test.cpp` | `Profundum_SpectrumFrameMatchesSignal` (`[ui][spectrum]`) |
| FR-072, SC-011 | `integration/spectrum_frame_test.cpp` | `Profundum_SpectrumFrameDoesNotChangeAudio` (`[ui][spectrum]`) |
| FR-071, SC-012 | `integration/spectrum_frame_test.cpp` | `Profundum_SpectrumFrameAllocationFree` (`[ui][rt]`) |
| SC-013 | `integration/processor_cpu_test.cpp`† | `Profundum_ProcessorCpu`, `Profundum_ProcessorCpuFullSurface` |
| FR-080, SC-014 | `unit/controller/spread_curve_test.cpp` | `Profundum_SpreadCurveMatchesField` (`[ui]`) |
| FR-090, SC-015 | `unit/controller/editor_sync_test.cpp` | `Profundum_EditorSyncTimer` (`[ui][lifecycle]`) |
| FR-091, FR-092, SC-016 | `unit/controller/editor_lifecycle_test.cpp`† | `Profundum_EditorLifecycle` |
| FR-030, FR-013, SC-017 | `unit/controller/editor_layout_test.cpp` | `Profundum_Editor_PageSwitch` (`[ui]`) |
| FR-035, SC-018 | `unit/controller/morph_component_test.cpp` | `Profundum_UiPortamentoUnitFollowsGlideMode` (`[ui]`) |
| FR-010, FR-014, FR-022, FR-050, SC-019 | `unit/controller/editor_layout_test.cpp` | `Profundum_UiLayout` (`[ui]`) |
| FR-094, SC-020, SC-021 | — | Build and tool checks, not a test (§6.2) |
| FR-095, SC-022 | `dsp/tests/unit/systems/profundum_engine_display_test.cpp`; `integration/spectrum_frame_test.cpp` | `Profundum_EngineDisplayAccessorNeutral` (`[profundum][systems][display]`); `Profundum_SpectrumFrameMatchesSignal` |
| FR-076, SC-023 | `integration/spectrum_frame_test.cpp` | `Profundum_SpectrumFrameHandlerLifecycle` (`[ui][spectrum][lifecycle]`) |
| FR-034, SC-024 | `unit/controller/custom_view_laws_test.cpp` | `Profundum_UiCustomViewLaws` (`[ui]`) |
| FR-074, SC-025 | `integration/spectrum_frame_test.cpp` | `Profundum_SpectrumFrameFocusVoice` (`[ui][spectrum]`) |
| FR-073, SC-026 | `unit/spectrum_producer_test.cpp`; `integration/spectrum_frame_test.cpp` | `Profundum_SpectrumFrameSanitise` (`[ui][spectrum]`) |
| FR-047, FR-048, FR-096, SC-027 | `unit/state_v3_test.cpp`; `unit/controller/morph_component_test.cpp` | `Profundum_EndpointNames` (`[ui][morph][state]`) |
| FR-075, SC-030 | `unit/spectrum_producer_test.cpp`; `integration/spectrum_frame_test.cpp` | `Profundum_SpectrumFrameCadence` (`[ui][spectrum]`) |
| FR-004, FR-011, FR-070, FR-080, SC-031 | `unit/controller/editor_layout_test.cpp` | `Profundum_UiPlacement` (`[ui]`) |
| FR-014, SC-032 | `unit/controller/editor_layout_test.cpp` | `Profundum_UiZoom` (`[ui]`) |
| P-2 audit | the 7 test files | — (§6.2) |
| FR-006 renders (P-1) | `integration/main_page_render_set_test.cpp` | `Profundum_MainPageRenderSet` (`[.listen]`) |

### 6.2 Assertion strategy, case by case

**Review, tooling and audit rows**

- **SC-001** (review row, at plan / tasks review and again before the first 12b commit). Each item below is checked as present
  in spec.md's Clarifications **and dated**. A missing or undated item blocks 12b.
  1. The ratification of D1–D7 and of every Resolved Question, each also written into its owning FR / SC row.
  2. D1 together with its concept §14 delta table (FR-003): all 11 rows of §3.1 present, each with a verdict and a reason.
  3. The user's listening verdict on the FR-006 render set (SC-006(b)).
  4. The FR-097 account (§3.4): all eight rows present, each "given (date)" or "pending", none blank. Every change-requesting
     verdict is also present as a ruling in its owning FR / SC row.
  5. Every D2-ruled string an SC compares (SC-005): the 800–809 tooltips and the 600–602 WIDE caveat, the 809 and 911
     statements, plus the descriptor table (SC-027(d)) and the bar decay time (SC-026).

  The agent checks presence and dates; it never fills a verdict. Then `git log` shows no 12b-scope file changed before the
  ratification date.
- **FR-094, SC-020, SC-021** (build and tools).
  - pluginval at strictness 5.
  - Zero MSVC warnings.
  - `check-portability.js`, `lint-apple-globals.js`, and clang-tidy `profundum`, run by file list (memory note).
  - The macOS and Linux legs are recorded as PENDING-CI.
- **P-2 audit.** Classify each of the 36 `kStateV2Bytes` lines into exactly one class:
  - **asserts the length of a `getState` result** → `kStateV3Bytes`;
  - **builds a v2 or v1 stream from scratch, or truncates at a cut below 510** → unchanged;
  - **patches bytes below 510 of a stream it is given**: the helpers `withEndpoints` (`profundum_test_fixture.h:1045`) and
    `withFields` (:1092). Their callers pass `writeV2Stream`, which is a `getState` call (:1006–1016) and returns 574 bytes
    under v3. The guard changes from `== kStateV2Bytes` to `== kStateV2Bytes || == kStateV3Bytes`: both lengths carry the v2
    body at the same offsets, and the patch touches only bytes below 510.
  - `writeV2Stream` keeps its name (append-only test edits) and gains a comment that it returns the current format. A genuine
    v2 stream comes only from the new `v2StreamOf` (SC-027 below).

  The classified list goes in the run log.
- **FR-006 renders** (`Profundum_MainPageRenderSet`, P-1). Writes the WAVs and the manifest. The only assertion is finiteness.

**Editor layout and binding** (`unit/controller/editor_layout_test.cpp`)

- **`Profundum_UidescBindsEverySurfaceId`** (SC-002):
  - `unreachableParams(xml, all84, {4,5,6,7})` is empty.
  - The built tree uses a headless VST3Editor, with the same setup as the existing `EditorBindsSurface` section
    (`editor_lifecycle_test.cpp:163–203`).
  - In that tree, the set of tags below 9000 equals the 80 visible IDs, and contains none of 4–7.
- **`Profundum_UidescTagIntegrity`** (SC-003):
  - every `extractControlTagMap` value is a registered ID;
  - every tag name referenced by a control is declared;
  - the eight Phase 10 names keep their values.
- **`Profundum_UiClassRule`** (SC-004). Each bound control's class is checked against the kind reported by
  `getParameterObject(id)`:
  - a `StringListParameter` must be a menu, a segment control or a two-state toggle;
  - any other parameter must be an `ArcKnob`, a `BipolarSlider`, a `CSlider` or the `ADSRDisplay` (FR-032);
  - **the one exemption (P-8):** a display-only `CParamDisplay` (exactly that class, not a `CTextEdit`) bound to a continuous
    ID passes only if the same page also binds that ID to a conforming writing control. The test checks this for every
    readout (105 and 412 on Main), so a readout without a writing control fails;
  - 417's step count must be honoured.

  The state cases pass with only the P-2 audit edits.
- **`Profundum_UiLabels`** (SC-005):
  - list labels are compared as strings against the registered ones (`getParameterObject(301/1000)->toString`);
  - ruled text is compared against the D2 literals, copied from D2 into `ui_strings_test_table.h`;
  - every Main-page `CTextLabel` and tooltip is scanned case-insensitively for the forbidden words.
- **`Profundum_Editor_PageSwitch`** (SC-017):
  - every page container is present in the built tree;
  - switching pages shows exactly one container;
  - the page selection survives an editor close and reopen;
  - `getState` bytes are identical before and after page switches.
- **`Profundum_UiLayout`** (SC-019). At each zoom step, after `setZoomFactor`:
  - every bound control's rect lies inside the frame;
  - no two bound controls on a visible page overlap;
  - every hit target × zoom is at least the D7 minimum;
  - every font is at least the D7 minimum;
  - every bound control has a tooltip or an adjacent label;
  - text contrast meets D7 (§5.6);
  - FR-022: every 105-bound control has an ancestor with `uidesc-label="morph-anchor"`, and a 1000-bound control descends
    from that same container (§3.1). The `anchor-group` holding 1000 is a child of it, so SC-005 holds at the same time;
  - Main binds 900 and none of 901–914.
- **`Profundum_UiPlacement`** (SC-031). Walks the containers by `uidesc-label` and checks:
  - (a) the Main set;
  - (b) the FR-004 section containers. This includes `section-morph-behaviour`: 1000 and 1002 are bound inside it, and so
    are the compare segment 9203 and the Copy buttons 9204 / 9205. 1000 is also bound inside `section-sub-anchor`;
  - (f) the FR-009 drift guard (§3.5): every `CControl` with a tag ≥ 0 is either bound to a registered ID (< 9000) or carries
    a tag in `kD6UiOnlyTags`, and every entry of `kD6UiOnlyTags` is present in the tree;
  - (e) both bar views read `&cachedFrame_`, so their bar heights are equal after one `onTickForTest`.
- **`Profundum_UiZoom`** (SC-032):
  - `getZoomFactor()` after each step returns 1.0 / 1.25 / 1.5 / 2.0;
  - the parameter count stays 84;
  - `getState` bytes are identical before and after zoom changes;
  - the editor's size constraints equal the designed size.

**Main page render** (`integration/main_page_sanity_test.cpp`)

- **`Profundum_MainPageSanity`** (SC-006(c)). Renders D1's render set through `ProcessorFixture`, then checks:
  - every sample is finite (`detail::isFinite`);
  - the RMS after 0.2 s is above −60 dBFS;
  - the peak does not exceed the ID 3 default ceiling (`kDefaultCeilingDb` = −1 dBTP, as a linear value);
  - sub band on the observer's `coreL / coreR`: ρ ≥ 0.9989;
  - sub band on the output, via `bandReading(…, 0, f_c)`: ρ ≥ 0.99 and |Q| ≤ 1.0 dB, gated as in
    `mono_sum_params_test.cpp:341–360`.

**Morph component** (`unit/controller/morph_component_test.cpp`)

- **`Profundum_MorphComponentUi`** (SC-007). A `ConnectedPair` with `RecordingConnectionPoint`s
  (`profundum_test_fixture.h:755`) and a `RecordingComponentHandler`:
  - (a) each compare press sends exactly one `kMsgCompare` (A = 1, A·B = 0, B = 2). Pressing A or B also produces exactly one
    begin / perform / end on 1001, carrying the pressed endpoint. Pressing A·B produces zero `performEdit` calls and leaves
    1001's normalised value unchanged. A begin / perform / end on Morph Position 105, through the 105 control, sends no
    `kMsgCompare` and leaves `compareMode()` and `compareModeChangeCount()` unchanged.
  - (b) the reply is **held**. `Processor::notify(kMsgCompare)` replies synchronously through `flushControllerSync()`
    (`processor.cpp:567–596, :607–640`), so in a plain `ConnectedPair` the reply lands inside `pressCompare`, and a panel that
    showed the clicked segment directly would pass. So: set `toController.hold = true` (`profundum_test_fixture.h:263`) and
    press. Assert that every compare segment's index, `compareMode()` and `compareModeChangeCount()` are unchanged. Then call
    `toController.releaseHeld()` (:308) and assert that `compareMode()` equals the pressed mode, `compareModeChangeCount()` rose
    by exactly 1, and both compare segments (Main 9200, Advanced 9203) show it.
  - (c) each Copy button sends one `kMsgCopyEndpoint` with `from`, and `setDirty` is recorded.
  - (d) the readout string equals `getParamStringByValue(105, p)` for the five values of p, giving the literals "0%" … "100%".
  - (e) after selecting B, the 60 E-parameters equal `normalizedFromLeafWord(mirror_[B])`, and the host was restarted once.
- **`Profundum_MorphLockDimming`** (SC-008). Runs the 3 × 2 grid of 1000 × 1001 through `setParamNormalized`, with the
  editor open headless. Checks `isAnchorDimmedForTest(300..306)`, and that `getMouseEnabled()` stays true.
- **`Profundum_UiPortamentoUnitFollowsGlideMode`** (SC-018):
  - step 414 through its three values;
  - after each step, call the 412 display's installed value-to-string function and check the unit suffix changes;
  - after each 414 step, `portaReadoutInvalidationsForTest()` rose by exactly 1, and the panel's collected display is the
    412-bound `CParamDisplay` found in the built tree (the same pointer). A change to an unrelated ID (200) does not raise it.
    This proves that FR-035's trigger reaches the real display, which the formatter check alone does not;
  - 412's normalised value stays unchanged.

**Macros** (`unit/controller/macro_ui_test.cpp`)

- **`Profundum_MacroHighlight`** (SC-009):
  - synthetic `MouseEnterEvent` and `MouseExitEvent` are dispatched through `viewOnEvent`;
  - `beginEdit` / `endEdit` on 100–103 are driven with no hover;
  - for **every** bound control in the built tree, not only the `ArcKnob`s, its highlighted state equals membership of its ID
    in the set the test derives from `kRows` (rows with amount ≠ 0, minus Compression). The state is read through the
    control's class getter (`getGuideColor`, `getTrackColor`, `getControlPointColor`; §1.6) compared with the `highlight`
    token. Spectral Shift 205 (a `BipolarSlider`, Dark ↔ Bright amount 0.5) and the `ADSRDisplay` (Tight ↔ Loose 420 / 421 /
    423) are therefore checked;
  - a highlight-set ID bound to a control of any other class fails the case;
  - `highlightedIdsForTest()` equals the same set (a secondary check);
  - Dark ↔ Bright's set excludes 501.
- **`Profundum_MacroModulationRing`** (SC-028). For each macro value in {0, 0.25, 0.5, 0.75, 1}:
  - the controller's `macroRingRange(id)` equals the test's own independent re-derivation within 1e-6. The test rebuilds
    §4.6.5 from `kRows`, `applyModCurve` and the field table;
  - each bound `ArcKnob`'s `getModulationRange()` equals `macroRingRange(id)` within 5e-4, the shared knob's dead band
    (`arc_knob.h:74–78`; §4.6.5);
  - the neutral value draws 0: `macroRingRange` is exactly 0, and the knob is within 5e-4 of 0;
  - Shape and Compression draw 0;
  - knob values are unchanged;
  - rings are identical with Motion 900 Off and on Cycle;
  - two evaluations with the same inputs are identical.
- **`Profundum_MidiLearnHostLevel`** (SC-029):
  - the flags come from `getParameterInfo`;
  - `queryInterface(IMidiLearn::iid)` returns `kNoInterface`;
  - no label contains "Learn".
- **`Profundum_UiContractTables`** (FR-005, FR-007). Prints the D3 and D4 CSVs (§3.2).

**Spectrum** (`integration/spectrum_frame_test.cpp`, plus `unit/spectrum_producer_test.cpp` where noted)

- **`Profundum_SpectrumFrameMatchesSignal`** (SC-010; also SC-022's plugin-side agreement).
  - Fixtures per the spec: showcase A and B, the default patch, the fold fixture, and the Nyquist fixture (C6 @ 44.1 kHz).
    All MONO, after 0.5 s of settling.
  - Seam arm: `setSpectrumProducerEnabledForTest(true)`, plus an observer that collects `charL/R` and `anchorL/R` keyed by
    `sample`.
  - Connected arm: a `ConnectedPair`, `setActive(true)`, and `queueAvailable()` (the Vorago helper, copied).
  - Reference: §5.2.
  - Checks:
    - first, the frame's `sampleRate`, `windowSamples` and `f0Hz` match the independent fs, N_ref and f0_ref (§5.2 steps 1–2),
      so a mislatched f0 or N fails before any level is compared;
    - every partial at or above −60 dB relative to the strongest agrees within ±0.5 dB;
    - the reported count is ≤ ⌊Nyquist / f0⌋;
    - the Anchor agrees within ±0.5 dB;
    - after note-off, a silent frame arrives by the next publish after the observer's last voice chunk.
- **`Profundum_SpectrumFrameDoesNotChangeAudio`** (SC-011). Two live renders in one binary (note on / off, a 105 sweep, a
  POLY chord): seam on vs off, and connected vs unconnected. Max |Δ| must be 0 on both channels.
- **`Profundum_SpectrumFrameAllocationFree`** (SC-012). `AllocationScope` over 2000 `process()` calls, with notes and
  parameter changes, on both the seam and the connected arm. Allocations must be 0. On the connected arm the scope opens before
  the first `process()` after `setActive(true)`, so the queue probe (§4.4) is measured too.
- **`Profundum_SpectrumFrameFocusVoice`** (SC-025). POLY, following the spec's steps:
  - check `spectrumProducerForTest().focusVoice()`, and the frame's `focusVoice` and f0;
  - with Drop at 0 and no bend, f0 must equal `midiNoteToFrequency(note)`;
  - the steal arm runs twice and the two runs are compared.
- **`Profundum_SpectrumFrameSanitise`** (SC-026):
  1. `feedInterval` with a NaN / +Inf sample built from its bit pattern. No published frame may contain a non-finite float;
     each float is checked with `detail::isFinite`.
  2. Processor re-prepare from 48 to 96 kHz. The first frame after `setActive(true)` has `sampleRate == 96000`, and no later
     frame has 48000.
  3. `HarmonicBarView::onTickForTest(0.033)` after a silent frame. Heights never increase and reach 0 by 0.300 s.
  4. A `feedInterval` arm with several windows of tiny inputs (about 1e-38, plus the Anchor taps at the same level). Every
     staged power is finite and either normal or exactly 0 (bit-pattern classification). This backs R-4 on targets where
     `ScopedDenormalMode` does nothing.
- **`Profundum_SpectrumFrameCadence`** (SC-030):
  - E1 and C3, 2 s, 512-sample blocks;
  - frames published == `windowsCompleted()`, which must be within ±1 of ⌊(samples since note-on) / N⌋;
  - `spectrumMaxFramesPerProcessForTest() ≤ 1`;
  - the frame `static_assert`s compile;
  - the fold fixture's count equals min(64, ⌊Nyquist / f0⌋).
- **`Profundum_SpectrumFrameHandlerLifecycle`** (SC-023). On a `ConnectedPair`:
  - `setActive(true)` gives exactly one `queueOpened` (uid `0x50524644`, block size 288);
  - `setActive(false)` gives exactly one `queueClosed`;
  - deactivate, `setupProcessing(96k)`, activate gives exactly one close and one open;
  - the queue assertions WARN-skip on Linux only;
  - **closed-queue arm, every OS (FR-071).** A processor initialised with a host context that is not an `IHostApplication`,
    connected to a `CountingConnectionPoint`, then prepared and activated. The fallback `openQueue` fails
    (`dataexchange.cpp:79–80`). After 200 `process()` calls with a held note: `spectrumQueueOpenForTest()` is false,
    `spectrumProducerForTest().windowsCompleted() == 0`, `spectrumFramesPublishedForTest() == 0` and
    `spectrumSkippedBlocksForTest() == 0`. On Linux, the plain `ConnectedPair` arm (no run loop) must give the same readings
    when its queue did not open; this is asserted, not WARN-skipped.

**SPREAD curve** (`unit/controller/spread_curve_test.cpp`)

- **`Profundum_SpreadCurveMatchesField`** (SC-014):
  - the spec's grid is written through `setParamNormalized`;
  - `SpreadCurveView::sampledValuesForTest()` is compared with an independent `BassStereoField` (`setControls` + `reset()`)
    times the m formula, within |Δ| ≤ 1e-6;
  - plus the five sub-checks the spec lists;
  - FR-080's knee marking: `kneeMarksForTest()` equals f_c · 2^(−0.25 · (1 − G)) and 4·f_c within 1e-3 relative, across the
    Gravity and crossover axes. The lower mark moves with Gravity and the upper mark does not.

**Editor sync and lifecycle**

- **`Profundum_EditorSyncTimer`** (SC-015; `unit/controller/editor_sync_test.cpp`):
  - `didOpen` / `willClose` through a headless VST3Editor;
  - the timer interval equals 100, which is ≤ 100;
  - `editorSyncTickForTest()` sends exactly one `kMsgRequestSync` to a `CountingConnectionPoint`, and none after close;
  - a synchronous arm uses a real `ConnectedPair` with no timers pumped;
  - an extra check: a tick is skipped while a gesture is open (R-6).
- **`Profundum_EditorLifecycle`** (SC-016; amends `unit/controller/editor_lifecycle_test.cpp`):
  - `HarnessCycles` runs 10 cycles;
  - the `EditorBindsSurface` section is replaced by the SC-002 counts;
  - the tree holds exactly 2 `HarmonicBarView`s and 1 `SpreadCurveView`;
  - every view pointer is null after `willClose`;
  - a frame delivered while the editor is closed updates only `cachedFrame_`, and the views show it on reopen;
  - unknown names return `nullptr`;
  - attach / remove runs 10 times on each timer-owning view.

**DSP accessor** (`dsp/tests/unit/systems/profundum_engine_display_test.cpp`)

- **`Profundum_EngineDisplayAccessorNeutral`** (SC-022). Two engines, one of which probes the accessors after every call that
  ends on a grid point:
  - the two outputs are bit-identical;
  - at each completed interval, `characterTapL/R` and `anchorTapL/R` equal the observer's spans for that voice and sample
    (memcmp);
  - `controlGridPhase`, `samplePosition` and `voiceMidiNote` agree with the call log.

**Custom-view laws** (`unit/controller/custom_view_laws_test.cpp`)

- **`Profundum_UiCustomViewLaws`** (SC-024):
  - the `ADSRDisplay` is taken from the headless built tree: the instance the panel's `verifyView` wired, not one the test
    builds. `paramCallback_` is private with no getter (`adsr_display.h:1939`) and the shared header is fenced, so the case
    drives the display's own public mouse path: `onMouseDown` at `getControlPointPosition(target)`, `onMouseMoved` by a delta,
    then `onMouseUp` (§1.6). The value checked is the one `RecordingComponentHandler` records for `performEdit`. If
    `verifyView` never wired the adapter, the display emits its raw cubic value and the case fails at 420;
  - 420, 421, 423 at the clamped minimum (drag far left), near 100 ms (preset with `setAttackMs(100)` and the matching setters,
    then a 1-pixel drag) and the clamped maximum (drag far right); 422 and 424–426 at min, mid and max;
  - each recorded value equals `normalizedFromPlain(law, plain)` within 1e-6, where plain is the display's own value after the
    drag (`getAttackMs()` and the other getters, `adsr_display.h:450–453`). `toString` round-trips;
  - reverse path: `setParamNormalized(420, v)` for three values of v leaves the wired display with `getAttackMs()` equal to
    `plainFromNormalized(law, v)` within 1e-4 relative (float storage); the same for 421 and 423;
  - negative arm: a raw-wired `ADSRDisplay` emits a different value at 420;
  - `HarmonicBarView` mouse events produce 0 `performEdit` calls.

**Endpoint names** (`unit/state_v3_test.cpp` and `unit/controller/morph_component_test.cpp`)

- **`Profundum_EndpointNames`** (SC-027):
  - (a) committing in the `CTextEdit` sends exactly one `kMsgEndpointName`. A round trip through a fresh processor and
    controller includes a 31-byte name, and a multi-byte UTF-8 name truncated at a code-point boundary.
  - **v2 reference.** A new fixture helper, `v2StreamOf(const V2Fields&, const MorphState& a, const MorphState& b)`, builds a
    genuine 510-byte stream field by field, never from `getState`:
    1. a zeroed `kStateV2Bytes` buffer with the version word `kStateV2Version` at byte 0;
    2. `withFields` and `withEndpoints` on it (their guards accept 510);
    3. bytes [490, 510) from `BassMorphEngine::serializeRuntime` of a freshly constructed engine
       (`bass_morph_engine.h:234–262`).

    v1 streams come from the existing `v1Stream` (`profundum_test_fixture.h:1062`).
  - (b) `kCurrentStateVersion == 3`. `v2StreamOf(priorFieldsP(), …)` and a `v1Stream` load, the names are "A" and "B", and
    every restored value equals its pre-phase result (the existing v2 expectations, reused unchanged).
  - (c) load `v2StreamOf(f, a, b)` into a fresh processor, then call `getState`. The result is 574 bytes, its version word is
    3, and its bytes [4, 510) equal bytes [4, 510) of the `v2StreamOf` reference. The reference is built without `getState`,
    so the comparison can fail. The runtime blob round-trips because `getState` prefers the pending restore
    (`processor.cpp:555–557`). A second arm sets the values through parameters instead of `setState`, then decodes each v2
    field at its fixed pre-phase offset in the v3 stream (`kV2GlobalsAt` … `kV2SeedAt`, `kStateEndpointAOffset`,
    `kStateEndpointBOffset`) and compares it with the value set.
  - (d) the D2 descriptor table cases.
  - (e) two streams with identical names but different settings have equal lengths and differ only inside the settings bytes.
  - (f) each caption is non-empty and has a name and three words.

**CPU** (`integration/processor_cpu_test.cpp`, amended)

- **SC-013.** Each of the two existing cases gains a producer-on arm, via `setSpectrumProducerEnabledForTest(true)` on the same
  instance.
- The bars are unchanged: MONO 8 %, 4 voices 25 %, P / D ≤ 1.05, control 1.05.
- Run only through `node tools/run-cpu-tests.js profundum_tests`, with nothing else running.

---

## 7. Build integration and lanes

### 7.1 CMake changes

- **`plugins/profundum/CMakeLists.txt`.** Add:
  - `src/ui/harmonic_bar_view.{h,cpp}`, `src/ui/spread_curve_view.{h,cpp}`, `src/ui/panel_sub_controller.{h,cpp}`;
  - `src/ui/macro_ui_map.h`, `src/ui/endpoint_descriptor.h`;
  - `src/processor/spectrum_frame.h`, `src/processor/spectrum_producer.h`;
  - `src/parameters/endpoint_names.h`.
- **`plugins/profundum/tests/CMakeLists.txt`:**
  - Add the new TUs: `unit/spectrum_producer_test.cpp`, `unit/state_v3_test.cpp`,
    `unit/controller/{editor_layout,morph_component,macro_ui,spread_curve,editor_sync,custom_view_laws}_test.cpp`, and
    `integration/{spectrum_frame,main_page_sanity,main_page_render_set}_test.cpp`.
  - Add the three `src/ui/*.cpp` files to the "second compilation" block.
  - Add the bit-pattern TUs (`spectrum_producer_test`, `spectrum_frame_test`, `main_page_sanity_test`, `state_v3_test`) to
    the `-fno-fast-math` list. `processor_cpu_test.cpp` stays out of that list, as the `tests/CMakeLists.txt` comment
    requires.
- **`dsp/tests/CMakeLists.txt`:** add `unit/systems/profundum_engine_display_test.cpp` to the Profundum block (:787–816),
  with its comment line.

### 7.2 Build

Build the three targets `Profundum`, `profundum_tests` and `dsp_systems_tests` in **one** rebuild, then run their suites in
parallel.

### 7.3 Change-set lanes (FR-095)

This follows the root CLAUDE.md rule "run only the tests the change set can reach".

The modified shared headers are `profundum_engine.h` and `profundum_voice.h`. This session's `grep -rl` finds their consumers
only in:

- `dsp/tests/unit/systems/profundum_*`;
- `mass_dynamics_oversampling_test.cpp`;
- `plugins/profundum/`.

Ruinae, Seraphis, Vorago and Innexus include neither header, so their suites are outside the change set.

The lanes:

1. Per push: `dsp_systems_tests "[profundum]"` plus the cases in `mass_dynamics_oversampling_test`.
2. Per push: the full `profundum_tests`, excluding the timing tags.
3. The `[long]` cases of the Profundum dsp TUs that include the two headers. The accessors are inert; this runs because the
   consumer rule requires it, not because a change is suspected.
4. The CPU lane, alone, afterwards: `node tools/run-cpu-tests.js profundum_tests`. The `dsp_systems_tests` CPU lane would run
   only if a perf TU's header had changed; none did, so it is skipped.
5. pluginval.
6. One ASan Debug run of `profundum_tests "[lifecycle]"` (SC-016), in a separate build directory, concurrent with the release
   lanes.

---

## 8. Risks and mitigations

| # | Risk | Mitigation |
|---|---|---|
| R-1 | Sub-slicing changes the audio | It cannot, by construction (§5.1). SC-011 and SC-022 gate it at max \|Δ\| = 0. With the gate off, the code is the Phase 11 path byte for byte |
| R-2 | Goertzel precision at low ω (at E1, ω₁ ≈ 0.0054 rad, so c ≈ 2 − 3e-5) | States, coefficients and the Chebyshev recurrence are all double. The error is about N·ε / ω, far below 0.5 dB for N ≤ 2²⁰ |
| R-3 | CPU: 2 × 64 double updates per sample | The loop is partial-inner over contiguous arrays, so it vectorises. Only `count` partials run, and analysis runs only after the queue probe has observed an open queue, or while the seam forces it (§4.4). A closed queue costs one probe per activation. SC-013 measures it alone, and the bars are never relaxed. If a bar goes red, the K = 8 window and the 64-partial capacity are ruled (FR-075), so the only levers are the SIMD layout and skipping inactive intervals |
| R-4 | Denormals in the double Goertzel states during quiet tails | `ScopedDenormalMode` (`processor.cpp:303`) sets FTZ / DAZ only where `KRATE_HAS_SSE_DENORMAL_CONTROL` is defined, which is x86 (`core/scoped_denormal_mode.h:37–44`). On macOS arm64 its constructor and destructor are empty (:63–75), so it is not the mitigation. The argument is numeric: (a) the states are double, and their smallest inputs, a float sample of at least about 1e-38 times a Hann weight of at least (π/N)² ≈ 1e-11 at N = 2²⁰, stay far above the double denormal threshold of 2.2e-308; (b) the recurrence is marginally stable (its roots have modulus 1), so the states do not decay toward zero in silence; (c) the states reset at every window start, and `feedSilence` ends the window. `Profundum_SpectrumFrameSanitise` step 4 measures it |
| R-5 | `-ffast-math` and non-finite checks | Only bit-pattern `detail::isFinite`, never `std::isnan`. The test TUs are in the `-fno-fast-math` list |
| R-6 | During a drag, the editor sync tick re-displays a stale mirror and the knob jumps back. The processor's slot can lag the newest `performEdit` by up to one block | Ticks are skipped while `openGestures_ > 0` (§4.6.7). The spec's SC-015 checks run with no gesture open |
| R-7 | macOS `aligned_alloc` returns null for a block size that is not a multiple of 32 | The block is 288 bytes (`static_assert % 32 == 0`). The Vorago / Membrum round-up is kept anyway |
| R-8 | The Linux test harness has no run loop, so the queue never opens there | The connected arms WARN-skip on Linux only, via the Vorago helper. The seam arms run on every OS (FR-076) |
| R-9 | macOS global-name collisions (`MacTypes` `Rect`, `MAC`) in the new UI TUs | Everything lives in `Profundum::UI` or `Profundum`. No anonymous-namespace `Rect`, `Point` or `Style`. `tools/lint-apple-globals.js` runs before commit |
| R-10 | Narrowing in brace init (a Clang error) | `BassStereoField::Controls` and the frame fields use copy-then-assign or designated initialisers. No `{double}` into a float |
| R-11 | The state-branch bugs at `processor.cpp:504` and `controller.cpp:117` would silently reset v2 projects | SC-027(b) loads genuine v2 streams built by `v2StreamOf` and v1 streams from `v1Stream`, and the P-2 audit keeps every v2-load expectation |
| R-12 | View pointers outliving the frame (use-after-free) | `willClose` nulls every pointer; view timers live from `attached` to `removed`; the ASan lifecycle run (SC-016) |
| R-13 | `CTextEdit` UTF-8 input that is invalid or too long | `sanitizeEndpointName` runs at every entry point: the controller commit, the processor's `notify` and state load |
| R-14 | Clang-tidy hanging on a large TU (memory note) | Run tidy on the phase's TUs by file list, with a wall-clock ceiling |

---

## 9. ODR sweep (this session; `grep -rnw <Name> dsp/ plugins/ tools/ tests/`)

| Name | Matches | Disposition |
|---|---|---|
| `ProfundumSpectrumProducer`, `ProfundumSpectrumFrame`, `kProfundumFrameUserContextId`, `HarmonicBarView`, `SpreadCurveView`, `ProfundumPanelSubController`, `SpreadCurveModel`, `kMacroTargetParamIds`, `describeEndpoint`, `kMsgEndpointName`, `kEndpointNameMaxBytes`, `kEndpointNameBlockBytes`, `kStateNamesOffset`, `publishSpectrumFrame`, `kEditorSyncIntervalMs`, `hasEditorSyncTimerForTest` | 0 each | Claimed |
| `SpectrumQueueState`, `spectrumQueueOpenForTest`, `v2StreamOf`, `portaReadoutInvalidationsForTest`, `kneeMarksForTest`, `lowerKneeHz`, `upperKneeHz`, `kD6UiOnlyTags` | 0 each | Claimed |
| `controlGridPhase`, `voiceForDisplay`, `voiceMidiNote`, `voiceRenderedThisInterval`, `appliedVoiceMode`, `characterTapL`, `characterTapR`, `anchorTapL`, `anchorTapR`, `intervalSamplesRendered` | 0 each (in `dsp/` and `plugins/profundum/`) | Claimed as members |
| `gridPhase` | 32, including a private `ProfundumVoice::gridPhase()` (`profundum_voice.h:717`) | Not used; `controlGridPhase` instead |
| `voiceNote` | 32 (`VoiceAllocator`, `PolySynthEngine`, Ruinae) | Not used; `voiceMidiNote` instead |
| `kStateV3Bytes` | 39, all in `namespace Vorago` (`plugins/vorago/src/plugin_ids.h` and its tests) | Claimed in `namespace Profundum`. Different namespace and internal-linkage `constexpr`, so no ODR conflict; `kStateV2Bytes` already follows the same pattern |
| `HarmonicFrame` | 283 (`processors/harmonic_types.h:54`) | Forbidden; not used |

Each name is swept again in the implement stage before its first use.

---

## 10. Open questions (decisions left to the user)

1. **P-1.** May the hidden FR-006 render TU (test-only, `[.listen]`) land before ratification, as an exception to FR-001?
   Without it, the FR-006 render set cannot exist before ratification. The same ruling covers running Phase 9's
   `evidence/close/listen.sh` before ratification, so the user can give the FR-097 verdicts (§3.4).
2. **P-2.** Should SC-004's wording be amended to cover the current-format length assertions (`kStateV2Bytes` →
   `kStateV3Bytes` on `getState` results)? Every v1 and v2 load expectation stays unchanged either way.
3. **P-5.** Accept `ArcKnob`'s symmetric ring (it shows the macro's reach on both sides) for 1.0, or add a plugin-local
   one-sided ring? The one-sided ring means re-implementing `draw`: more code, but no shared-header change.
4. Should Copy A → B and Copy B → A also copy the endpoint **name**? The plan's default is no: names identify endpoints, and
   the copy is the sound.
5. Ratify the §3.1 proposals, or replace them in D1, D2 and D7:
   - the 960 × 600 designed size;
   - no image-mode mirror on Main;
   - the descriptor table;
   - the 300 ms bar decay;
   - the D7 values;
   - the 100 ms editor sync period;
   - the Main layout with the `morph-anchor` row (§3.1, FR-022) and the §14 delta table verdicts.
6. **P-8.** Rule into the FR-032 row that a display-only `CParamDisplay` bound to a continuous ID is allowed when the same page
   also binds that ID to a conforming writing control (the 105 and 412 readouts)?
7. **P-9.** Is a header preset field wanted in 12b? The plan's default is no: no FR or SC requires it, and Phase 13 owns
   presets. A yes needs a new FR / SC row first, and then Appendix A applies.

---

## Appendix A. Header preset field, only if the user rules it in (P-9)

None of this is in the 12b scope unless an FR / SC row is added first.

- `plugins/profundum/src/preset/profundum_preset_config.h` gains `makeProfundumPresetTabLabels()`: "All" followed by
  `makeProfundumPresetConfig().subcategoryNames` (today `{"Sub"}`), copied from `makeVoragoPresetTabLabels()`
  (`plugins/vorago/src/preset/vorago_preset_config.h:40`). The file's header makes the list additive-only, and the function
  only reads it. The file is added to §0.2.
- `PresetBrowserView(frame->getViewSize(), presetManager_.get(), makeProfundumPresetTabLabels())`: the shared constructor takes
  a `std::vector<std::string>` of tab labels (`plugins/shared/src/ui/preset_browser_view.h:58–60`). It is built in `didOpen`
  and closed in `willClose`, where its pointer is nulled.
- `createCustomView("PresetBrowserButton")` returns an `OutlineBrowserButton` (Vorago `controller.cpp:364–368`), with session
  tag 9000.
- Tests: SC-016's pointer-null check includes the browser pointer, and a case asserts that the overlay is created in
  `didOpen` and closed in `willClose`.

## Review notes

- **Preset browser (two review issues).** The plan used to copy Vorago's preset overlay, which named
  `makeVoragoPresetTabLabels()` with no Profundum equivalent, and it had no FR. Resolved by removing the preset field from
  12b (P-9, Open Question 7) instead of adding `makeProfundumPresetTabLabels()` now. Adding code for a feature no FR requires
  would be scope creep. The corrected design, including the tab-label function, is ready in Appendix A if the user rules it
  in.
- **SC-024, "invoke the display's own callback member".** Applied in substance, through a different mechanism. `paramCallback_`
  is private with no getter (`adsr_display.h:1939`), and the shared header is fenced, so a test hook on the display is not
  available. The case instead drives the built-tree instance through its public mouse path (§6.2), which reaches the
  installed callback and fails if the adapter was never wired.
- **FR-071 producer gate (two review issues, one fix).** The handler-presence gate was replaced by the one-time queue probe
  (§4.4). FR-071's "no-op" clause is kept as written; no ruling to relax it is requested.
- **FR-032 readouts.** A plan cannot rule an FR exemption. P-8 states the recommended ruling for the FR-032 row, Open Question
  6 asks for it, and `Profundum_UiClassRule` already enforces the narrow form.
