# Tasks: Vorago Phase 13 — UI (Concept-First Editor + Ecosystem View)

**Spec:** `specs/vorago-phase13-ui/spec.md` · **Plan:** `specs/vorago-phase13-ui/plan.md` (both are normative; section refs below are plan sections unless marked "spec")
**Branch:** `feat/vorago-phase1-events-modulation` (base `e86fc1f8`)
**Date:** 2026-09-26 · **Status:** TASKS — nothing implemented

---

## How to read this file

- Tasks are numbered `T001…` and grouped. **Groups run strictly in order.** Inside a group, tasks tagged
  **[P]** create only NEW files that no other task in that group touches, so they may run in parallel.
  A task that edits an existing (shared) file sits alone in its own sequential group.
- Every task follows the repo's canonical order: **write the failing test first → implement → zero
  warnings → tests pass.** Where a task creates only test code, its implementation lands in a later,
  named task, and the test is authored before that implementation exists.
- **CMake registration happens once, in T020 (final group).** The test lists in
  `plugins/vorago/tests/CMakeLists.txt` and `dsp/tests/CMakeLists.txt` are ENUMERATED
  (`plugins/vorago/tests/CMakeLists.txt:5-7`, `dsp/tests/CMakeLists.txt:492-494`), so a new TU does not build
  or run until T020 lists it. Consequence: between groups, do **not** try to build `vorago_tests` or `Vorago`
  — once `controller.cpp` references `EcosystemView` / `VoragoPanelSubController` (T014), both targets fail
  to link until T020 lists the new `.cpp` files. Each task's "Verify" line names the target and filter
  that proves it; that verification runs in Group 14 (T021 onward). `dsp_systems_tests` is the only
  target that could be checked earlier, and it too waits for T020.
- **Build command (Windows, always the full path):**
  `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target <target>`
  Run a suite directly: `build/windows-x64-release/bin/Release/<target>.exe "<TestName>" 2>&1 | tail -5`
  (positional filter, never `ctest -R`).
- **Cross-cutting rules for every task:** no allocation/lock/exception/IO on the audio thread; NaN/Inf
  checks by bit pattern only (`Krate::DSP::detail::isFinite`, `dsp/include/krate/dsp/core/db_utils.h:125-129`),
  never `std::isnan`; no brace-init narrowing (explicit `static_cast`); designated initialisers for
  `EcosystemEngine::PrepareConfig`; no bit-exact float goldens (A/B inside one binary or live-value
  compares only); names follow the root `CLAUDE.md` table; namespace `Vorago` / `Vorago::UI`, never a
  `using namespace` of both `Vorago` and `Seraphis` in one TU.
- **Out of scope for every task:** commits (handled outside this workflow), any new registered parameter,
  any state-format change, any edit to an existing `dsp/` function body, any Seraphis file.

### Plan decisions, ruled 2026-09-26 (spec Clarifications, plan stage R-1 … R-7)

- R-1 (D-1): the header preset button is the shared `Krate::Plugins::OutlineBrowserButton`
  (`plugins/shared/src/ui/outline_button.h:79`); **no** `PresetBrowserButton` class and **no**
  `controller_presets.cpp` are created (D-3).
- R-4 (D-2): second seam `setEcosystemFrameEnabledForTest(bool)` (gate only) beside FR-026's forced seam.
- R-2: **no** `SavePresetDialogView` — the browser's own save dialog is the save path. No
  `savePresetDialogView_` member and no `savePresetDialogViewForTest()` accessor; any task text below that
  still names them is superseded by this line.
- R-3: `habitatBrightness(v) = v <= 0 ? 0 : v >= 1 ? 1 : clamp((20·log10(v) + 60) / 60, 0, 1)` (dB-normalized).
- R-5: SC-016 (b) uses timer destruction + ASan + a 150 ms sleep, no platform pump.
- R-6: T019 bumps `version.json` to 0.2.0 and writes one `[0.2.0]` entry covering Phases 12 and 13.
- R-7: single CMake registration task (T020) in the last group, accepted as in Phase 12.

---

## Group 1 — frame payload (sequential; everything below includes it)

### T001 — `EcosystemFrame` POD header (FR-020, spec C-2)

- **Create:** `plugins/vorago/src/processor/ecosystem_frame.h` (new).
- **Test first:** the header's own `static_assert`s are the compile-time test; the runtime mirror is
  written in T009 (`Vorago_EcosystemFrameLayout`). Write the asserts before the struct body so the first
  compile of any includer fails if the layout is wrong.
- **Implement:** transcribe plan §3.1 exactly:
  - includes `<cstddef> <cstdint> <type_traits>` **only** (no processor, controller or `dsp/` header — FR-020);
  - `namespace Vorago { inline constexpr std::size_t kMaxFrameAgents = 48; inline constexpr std::size_t kMaxFrameLinks = 64; inline constexpr std::uint32_t kEcosystemFrameUserContextId = 0x5645434Fu; }` ('VECO');
  - `struct EcosystemFrame` with the fields, in order: `std::uint32_t sequence; std::uint8_t activeVoices, focusVoice, agentCount, linkCount; float voiceLevel; float agentX[48], agentY[48], agentGlow[48]; std::uint8_t agentKind[48], agentDormant[48], linkA[64], linkB[64]; float linkStrength[64]; float linkFlowScale;` — raw C arrays, every member default-initialised to zero (`= {}` / `= 0`);
  - `static_assert(sizeof(EcosystemFrame) == 1072, "C-2 layout: 8+4+3*192+48+48+64+64+256+4");`
    `static_assert(std::is_trivially_copyable_v<EcosystemFrame>);`
    `static_assert(std::is_standard_layout_v<EcosystemFrame>);`
    `static_assert(offsetof(EcosystemFrame, linkStrength) == 812);`
    `static_assert(offsetof(EcosystemFrame, linkFlowScale) == 1068);`
  - a banner comment: shared by processor and controller (like `plugin_ids.h`), one-way processor →
    controller, memcpy'd, native endianness.
- **ODR:** `grep -rnE "(class|struct) EcosystemFrame\b" dsp/ plugins/` must return only this file.
- **Verify:** compiles as part of T021's build (any includer); runtime mirror in T009 →
  `vorago_tests "Vorago_EcosystemFrameLayout"`.

---

## Group 2 — failing tests and self-contained new UI view (all [P], new files only)

### T002 [P] — SC-019 accessor test (`dsp_systems_tests`) — test only; implementation is T008

- **Create:** `dsp/tests/unit/systems/ecosystem_engine_pair_accessors_test.cpp` (new).
- **Write** `TEST_CASE("EcosystemEngine_PairAccessors", "[systems][ecosystem][pairs]")` per plan §9.1:
  - Fixture: `Krate::DSP::EcosystemEngine eng; eng.setSeed(0x5EED1234u); eng.prepare(48000.0, Krate::DSP::EcosystemEngine::PrepareConfig{.agentCount = 32});`
    `stepSamples = eng.getStepIntervalChunks() * Krate::DSP::EcosystemEngine::kControlChunkSamples;`
    `numSteps = ceil(30.0 * 48000.0 / stepSamples)`.
  - Test-local verbatim copy of the two-compare torus delta in an anonymous namespace
    (`d > 0.5 → d - 1.0; d < -0.5 → d + 1.0; else d`) — `EcosystemEngine::wrapDelta`
    (`ecosystem_engine.h:1111-1119`) is private and FR-030 forbids widening it.
  - Per step: snapshot `x_i = getAgentPositionX(i)`, `y_i`, `e_i = getAgentEnergy(i)` for all
    `i < getAgentCount()`, plus `sigma = double(getKernelSigma())`, `rate = double(getExchangeRate())`,
    `pred = double(getPredation())`; `prev = getControlStepCount()`; `eng.processChunk(stepSamples)`;
    `REQUIRE(eng.getControlStepCount() == prev + 1)`.
  - Reference for every `i < j`: `dx = td(x_j - x_i)`, `dy = td(y_j - y_i)`, `d2 = dx*dx + dy*dy`;
    `twoSigSq = 2.0*sigma*sigma`; `inv = twoSigSq > 0 ? 1.0/twoSigSq : 0.0`;
    pre-test skip if `d2 > twoSigSq * 13.815510557964274 * (1.0 + 1e-9)`; `w = std::exp(-d2 * inv)`;
    keep iff `w >= 1e-6`; `ref = rate * w * (e_j - e_i) * (1.0 - (2.0 * pred))`. Before writing it, open
    `ecosystem_engine.h:1380-1464` and `:2036-2041` and match the expression **association exactly**
    (plan §9.1 step 3); if the engine's `dx/dy` sign convention or product grouping differs from the
    above, copy the engine's.
  - Asserts per step: the set of `(getPairAgentA(p), getPairAgentB(p))` for `p < getPairInteractionCount()`
    equals the reference set (`std::set<std::pair<size_t,size_t>>`); `A < B` and `B < getAgentCount()` for
    every `p`; for each pair `std::fabs(getPairFlow(p) - ref) <= 1e-12 * std::max(std::fabs(ref), 1e-300)`.
  - `SECTION("PredationHalf")`: fresh engine, `setPredation(0.5f)`, step 200 times: every `getPairFlow(p) == 0.0`
    exactly, and `getPairInteractionCount() > 0` was seen at least once (`REQUIRE(sawPairs)`).
  - `SECTION("OutOfRange")`: after stepping, for `p ∈ {getPairInteractionCount(), getPairInteractionCount()+1, SIZE_MAX}`:
    `getPairAgentA(p) == 0`, `getPairAgentB(p) == 0`, `getPairFlow(p) == 0.0`; a default-constructed,
    unprepared engine returns `0 / 0 / 0.0` at `p = 0`.
- **Red state:** does not compile until T008 adds the three getters.
- **Verify (after T020):** `dsp_systems_tests "EcosystemEngine_PairAccessors"` passes; T020 puts this TU in
  the `-fno-fast-math` list so both sides of the compare are IEEE.

### T003 [P] — `EcosystemView` statics + view class + its test (FR-050, FR-051, SC-014, SC-015, SC-016 (b))

- **Create (new):** `plugins/vorago/tests/unit/controller/ecosystem_view_test.cpp`,
  `plugins/vorago/src/ui/ecosystem_view.h`, `plugins/vorago/src/ui/ecosystem_view.cpp`.
- **Test first** (`ecosystem_view_test.cpp`; tag every case `[vorago][ui][ecosystem]`, add `[lifecycle]` to
  AttachRemove only). Rect `r = CRect(350, 36, 750, 436)` unless stated.
  - `Vorago_EcosystemView_Mapping` (SC-014): `mapToView(0,0,r)` and `mapToView(1-1e-6, 1-1e-6, r)` inside `r`;
    `agentStyle` over 256 glow steps in `[0,1]`: alpha and radius monotonic non-decreasing; `agentStyle(0,false)`
    is `filled == false`, `alpha >= kAgentOutlineMinAlpha` and `kAgentOutlineMinAlpha > 0`; `agentStyle(g, true).filled == false`
    for every step. `torusLinkSegments` on 400×400: `(0.4,0.4)->(0.6,0.5)` → 1 piece; `(0.95,0.5)->(0.05,0.5)` → 2;
    `(0.5,0.95)->(0.5,0.05)` → 2; `(0.95,0.95)->(0.05,0.05)` → `count >= 2`; every piece's endpoints inside
    `r` ±1e-9; total length `== |d|·400 ± 1.0 px` where `d = (td(bx-ax), td(by-ay))`.
    `needsRedraw(s,s,0) == false`, `needsRedraw(s,s+1,0) == true`, `needsRedraw(s,s,1e-3f) == true`.
    `isDrawableLink`: false for `l >= linkCount`, `linkA >= agentCount`, `linkB >= agentCount`, `linkA == linkB`; true for a valid link.
    `emptyHabitatGridLines(r)`: 6 segments, exactly 3 vertical at `x = 450, 550, 650` and 3 horizontal at `y = 136, 236, 336`.
    `linkAlpha`: with scale 2.0, monotonic non-decreasing over 256 strengths in `[0, 200]`; `linkAlpha(2,2) == 0.5f`;
    `linkAlpha(96,2) < 1.0f`; `== 0.0f` for strength `0`, `-1`, and for scale `0`, `-1`.
    `habitatBrightness` (R-3, ε = 1e-6): `0.01 → 1/3`, `0.1 → 2/3`, `0.5 → 0.899657`, `1 → 1.0f`; exactly
    `0.0f` at `0`, `-1` and `1e-3`; exactly `1.0f` at `2`; monotonic non-decreasing over 256 steps in `[-1, 2]`.
    `activeVoicesText(n) == std::to_string(n)` for `{0, 1, 6, 255}`.
  - `Vorago_EcosystemView_LinkFade` (SC-015): hand-built frame (agentCount 8, one drawable link 1–2,
    `linkStrength = 1.0f`, `linkFlowScale = 1.0f`); `FadeTable t{}`; `stepLinkFade(t, f, false, 0)` → entry
    `(1,2) >= linkAlpha(1,1) = 0.5f`. Then remove the link (`linkCount = 0`) and tick `n` times with
    `dt = 1/30`: after each tick `entry <= exp(-t/0.6) * 0.5f + 1e-6` (t = n·dt); once it would fall below
    `1/255` it is exactly `0.0f`. `dt = 1.0` produces the same result as `dt = 0.25`. `clear = true` zeroes every entry.
    **Live-view arm** (no attach): `EcosystemFrame f{}` owned by the test; `auto* v = new EcosystemView(r, &f)`
    (release via `v->forget()`); frame A (focus 0, agentCount 8, one drawable link) → `onTimerForTest(0.0f) > 0`;
    `++f.sequence`, `linkCount = 0` → `onTimerForTest(1e-6f) >= 0.99f * prev`; `f.focusVoice = 1` →
    `onTimerForTest(1e-6f) == 0.0f` exactly. Repeat from a fresh link with `agentCount` 8 → 9 instead → `== 0.0f`.
  - `Vorago_EcosystemView_DrawList` (FR-051, C-6 clauses 2,3,6,7,8): `auto list = std::make_unique<EcosystemView::DrawList>()`.
    (a) frame agentCount 8, `voiceLevel 0.5f`, mixed glow/dormant/kinds 0..4, fade built by `stepLinkFade` from
    3 drawable links → 8 `Agent` items and 3 `Link` items; each Agent: `alpha == agentStyle(glow,dormant).alpha * 0.5f`,
    `filled == agentStyle(..).filled`, `colourIndex == agentKind`, `centre == mapToView(x,y,r)`; each Link:
    `alpha == fade[i,j] * 0.5f`, `width == kLinkMaxWidth * fade[i,j]`. Repeat at `voiceLevel 0` (every alpha 0)
    and `2.0f` (unscaled). (b) `agentCount = 0` with stale fade entries and links in the frame → exactly 6 items,
    all `Grid`, equal to `emptyHabitatGridLines(r)`. (c) a frame link with `linkB = agentCount` and one with
    `linkA == linkB` through `stepLinkFade` → no Link item for either; a hand-set fade entry `(40,45)` with
    `agentCount = 32` → no Link item. (d) exactly one `Text` item whose text equals
    `activeVoicesText(activeVoices)` for `activeVoices ∈ {0, 3, 6}`.
  - `Vorago_EcosystemView_AttachRemove` `[lifecycle]` (SC-016 b): `Krate::TestSupport::ensureVstguiInitialized()`
    (`tests/test_helpers/editor_lifecycle_harness.h:63`); `auto parent = VSTGUI::makeOwned<VSTGUI::CViewContainer>(CRect(0,0,1100,760))`;
    `auto frame = std::make_unique<EcosystemFrame>()`; view over it; 10 cycles of `view->attached(parent)` →
    `REQUIRE(hasTimerForTest())`, `view->removed(parent)` → `REQUIRE(!hasTimerForTest())`. Variant: after the
    last `removed`, `frame.reset()`, sleep 150 ms in 10 ms slices, then destroy the view (ASan run in T024 proves no access).
- **Implement** plan §6.1–§6.3 exactly: `class Vorago::UI::EcosystemView : public VSTGUI::CView` with the
  constants, static functions, `Style`/`Segment`/`Segments`/`FadeTable`/`DrawKind`/`DrawItem`/`DrawList`,
  `kMaxFadePairs = 1128`, `kMaxDrawItems = 1183`, `std::unique_ptr<DrawList> drawList_` allocated once in the
  ctor; the math table §6.2 (mapToView, agentStyle, linkAlpha, habitatBrightness, activeVoicesText,
  needsRedraw, isDrawableLink, emptyHabitatGridLines, torusLinkSegments with Liang–Barsky clipping and a
  local 4-line `td` — do **not** include `ecosystem_engine.h`, plan §0 item 7), `stepLinkFade` (decay before
  raise, floor `1/255`, `dt` clamped to `[0, 0.25]`), `buildDrawList` (pure), `draw` (background `eco-bg` +
  replay only), `attached`/`removed` owning a `CVSTGUITimer` at 33 ms (Seraphis `cloud_view.cpp:386-402`
  shape), `tick()` → `onTimerForTest(dt)`. The 8-entry palette is looked up once from the named uidesc colours
  `eco-partial, eco-resonator, eco-noise, eco-feedback, eco-ghost, eco-link, eco-grid, text-dim` with fixed
  fallbacks. Drawing uses `drawLine/drawEllipse/drawString/setFrameColor/setFillColor/setLineWidth` only.
  Includes: `processor/ecosystem_frame.h` (POD only) + VSTGUI; never `ecosystem_frame_builder.h`, never a processor header.
- **ODR:** `grep -rnE "(class|struct) (EcosystemView|DrawList|DrawItem)\b" dsp/ plugins/` → only this file.
- **Verify (after T020):** `vorago_tests "Vorago_EcosystemView_*"` all pass; zero warnings.

### T004 [P] — controller consumer test (SC-013, FR-040) — test only; implementation is T014

- **Create:** `plugins/vorago/tests/unit/controller/ecosystem_consumer_test.cpp` (new).
- **Write** `TEST_CASE("Vorago_Controller_ConsumesEcosystemFrame", "[vorago][controller][ecosystem]")`:
  controller `owned(new Vorago::Controller())`, `initialize(nullptr)`;
  `TBool bg = true; controller->queueOpened(kEcosystemFrameUserContextId, 1072, bg); REQUIRE(bg == false);`
  build three `EcosystemFrame`s with `sequence = 1, 2, 3` as `DataExchangeBlock{data, size = 1072, blockID = 0..2}`
  in one array → `onDataExchangeBlocksReceived(kEcosystemFrameUserContextId, 3, blocks, false)` →
  `cachedEcosystemFrame().sequence == 3`; a block of `size = 1071` with sequence 9 → still 3; a full block
  with context `0x12345678` and sequence 10 → still 3. IMessage fallback: `Steinberg::Vst::HostMessage`
  (`public.sdk/source/vst/hosting/hostclasses.h`) with ID `"DataExchange"`, int attribute `"UserContextID"` =
  `0x5645434F`, binary `"Data"` = frame with sequence 7 → `notify(msg) == kResultOk` and cache `sequence == 7`.
  Forwarding probe: `HostMessage` with ID `"TextMessage"` and string attribute `"Text"` →
  `notify == kResultOk` **and** cache `sequence` still 7 (only `ComponentBase::notify` → `receiveText` can
  produce that `kResultOk`, `vstcomponentbase.cpp:91-107`, `:153-156`). Before relying on the fallback
  attribute names, open `extern/vst3sdk/public.sdk/source/vst/utility/dataexchange.cpp:44-49` and use the
  exact constants there.
- **Red state:** does not compile until T014.
- **Verify (after T020):** `vorago_tests "Vorago_Controller_ConsumesEcosystemFrame"`.

### T005 [P] — producer integration tests (SC-001, 006, 007a, 008, 009, 010, 011, handler lifecycle) — test only; implementation is T009/T010

- **Create:** `plugins/vorago/tests/integration/ecosystem_frame_test.cpp` (new). Uses
  `VoragoTest::ProcessorFixture` (`plugins/vorago/tests/vorago_test_fixture.h:166-300`) and includes
  `processor/ecosystem_frame_builder.h` for the reference functions. Tag `[vorago][integration][ecosystem]`.
- **Write** these cases exactly as plan §9.2 rows describe (numbers are normative):
  - `Vorago_EcosystemFrame_DoesNotChangeAudio` (SC-001): two fixtures, seed index 0, 512-sample blocks at
    48 kHz, note-on 48 at sample 0, note-off at 45 s, 60 s total (5625 blocks). A forced on, B off.
    Captured L and R equal element by element with `==`. Non-vacuity: A's publish-attempt counter == A's
    process-call counter == 5625, and at least one A frame has `agentCount > 0 && linkCount > 0`.
  - `Vorago_EcosystemFrame_MatchesEngine` (SC-006): forced seam, held note 48, 20 s of 512 blocks. After every
    block, with `v = frame.focusVoice` and `eco = proc->engineForTest()->getVoice(v).ecosystem()`:
    `agentCount == eco.getAgentCount()`; for `i < agentCount`: `agentX[i] == sanitizeFrameFloat(eco.getAgentPositionX(i))`,
    same for Y, `agentGlow[i] == ecosystemEnergyGlow(eco.getAgentEnergy(i), eco.getAgentCount(), eco.getEnergyBudget())`,
    `agentKind[i] == static_cast<std::uint8_t>(eco.getAgentKind(i))`, `agentDormant[i] == (eco.isAgentDormant(i) ? 1 : 0)`;
    every entry `>= agentCount` / `>= linkCount` is `0`; `voiceLevel == sanitizeFrameFloat(getVoiceLevel(v))`;
    `activeVoices == getActiveVoiceCount()`; `linkFlowScale == sanitizeFrameFloat(eco.getEnergyBudget() / double(agentCount))`
    (0 when agentCount 0). Non-vacuity: some frame has two agents with share ≥ 2, different energies, different
    `agentGlow`; if never reached, WARN and repeat that arm on a directly prepared engine (48 agents,
    `setExchangeRate(3.0f)`) — never drop it. WARN-record min/median/max `voiceLevel` while held (OQ-3 evidence).
  - `Vorago_EcosystemFrame_LinksAreStrongest` (SC-007 a): forced seam, shipped defaults, 20 s. After every block
    build the reference from the focus voice's `eco` (all `p < getPairInteractionCount()`, drop `getPairFlow(p) == 0.0`,
    sort `|flow|` descending, take `min(64, n)`), and assert: multiset of `linkStrength[0..linkCount)` equals the
    multiset of `static_cast<float>(|ref|)`; each carried `(linkA, linkB)` is a recorded pair whose `|flow|` narrows
    to that strength; `linkA/B < agentCount`; `linkA != linkB`.
  - `Vorago_EcosystemFrame_Cadence` (SC-008): **enabled seam** (`setEcosystemFrameEnabledForTest(true)`), 48 kHz.
    (1) process-call counter +1 for blocks with 0, 1 and 1024 events and for a 2048-sample block; +0 for each
    early-return shape (`processNoOutputs` at `vorago_test_fixture.h:248`, null `channelBuffers32`, one channel,
    `numSamples = 0`, null `outL`, unprepared fixture). (2) align the step phase by rendering 512·k samples, then
    16 × 32-sample calls → attempts +1 exactly. (3) one 2048-sample call → +1. (4) seven 32-sample calls inside one
    step → +0, the boundary-crossing call → +1. (5) notes on slots 0–3 (polyphony 4), then a `kPolyphonyId` change
    to the "1" entry mid-step → the next call +1 and `focusVoice` changed. (6) seam off, no handler → both counters
    +0 over 100 calls.
  - `Vorago_EcosystemFrame_FocusVoice` (SC-009): forced seam; every `SECTION` first sets `kEnvelopeReleaseId` to the
    plain value 200 ms (normalized through the same mapping `plugins/vorago/src/parameters/envelope_params.h` uses
    at `:163-165`). Rows: one note → its slot, `'a'`; two notes → later slot, `'a'`; release the later → earlier
    slot, `'a'`; all released → `'a'` while `Releasing`, loop until `getVoiceState(v) == Idle` with
    `REQUIRE(elapsedSeconds <= 60.0)`, then `'c'` and `agentCount == 0`, and `'b'` never seen; shrink 4 → 1 with
    slots 0–3 held → focus 0, `'a'`; orphan tail: four notes at polyphony 4, hold slot 3, release others, render
    until slots 0–2 Idle (≤ 60 s), shrink to 1 → at least one frame with `getVoiceState(3) == Idle`, focus 3, `'b'`
    while `getVoiceLevel(3) > 1e-4f`; then `'c'` with `agentCount == 0`. WARN-record time-to-Idle (risk R-4:
    if > 60 s, STOP and report; never raise the bound).
  - `Vorago_EcosystemFrame_Determinism` (SC-010): two forced fixtures, same seed and script (note-on 48 and 55 at
    0, 2000 × 512 blocks); every frame field equal with `==` (arrays via `std::equal`, never `memcmp` on floats).
    Seed index 0 vs 1: some `agentX` differs within 200 blocks.
  - `Vorago_EcosystemFrame_AllocationFree` (SC-011): forced seam, polyphony 2; pre-built `Krate::Test::EventList`
    and `ParameterChanges` with capacity reserved up front (pattern of
    `plugins/vorago/tests/integration/automation_rt_test.cpp:115-119`); 2000 blocks of seeded random note on/off,
    CC64 points on ID 4, automation of 201, 206, 403, each `processBlock` inside `TestHelpers::AllocationScope`
    → `getAllocationCount() == 0`; after every block every frame float passes `Krate::DSP::detail::isFinite`.
    **Connected arm:** `ConnectedFixture` (anonymous namespace, Innexus `test_data_exchange_pipeline.cpp:108-163`
    order): `Steinberg::Vst::HostApplication host`; `proc.initialize(&host)`; real `Vorago::Controller`
    `ctrl.initialize(&host)`; both sides `connect`; `setupProcessing` + `setActive(true)` **outside** any scope;
    held note 48; ≥ 64 × 512-sample blocks each inside an `AllocationScope` → 0 allocations; attempts advanced
    ≥ 64 and skipped-blocks advanced ≥ 60. Queue-open precondition: record `"DataExchangeQueueOpened"`; on
    `SMTG_OS_LINUX` a missing queue is `WARN`ed and only the queue-dependent asserts are skipped (the 0-allocation
    assert still runs); on Windows/macOS it is a `REQUIRE`.
  - `Vorago_EcosystemFrame_HandlerLifecycle` (FR-021, FR-026, C-2 clauses 1, 2(iii), 7): `ConnectedFixture` with a
    test-local recording `IConnectionPoint` peer that copies the message ID and the ints `"UserContextID"`,
    `"BlockSize"` inside `notify`. No seam anywhere. Steps (1)–(6) of plan §9.2 row, with these exact numbers:
    unconnected 10 calls → process counter +0; `setActive(false)`, `connect(peer)`, `setActive(true)` → exactly one
    `"DataExchangeQueueOpened"` with `UserContextID == 0x5645434F` and `BlockSize == 1072`; no note: call → attempts
    +1, next → +0; `setActive(false)`, `disconnect`, `connect`, `setActive(true)` → next call +1, the one after +0;
    held note 48, 512 blocks: fills 1–4 skipped +0, fill 5 skipped +1, fill 6 skipped +1; `setActive(false)` →
    exactly one `"DataExchangeQueueClosed"` with the VECO id; `disconnect`, `setActive(true)`, 10 note-holding
    calls → all three counters +0. Linux: (2), (4), (5) WARN-skipped as above.
- **Red state:** does not compile until T009 + T010.
- **Verify (after T020):** `vorago_tests "Vorago_EcosystemFrame_*"`.

### T006 [P] — editor layout / binding / page / Gravity tests (SC-002–005, 017, 018 session arm, 022) — test only; implementation is T013–T017

- **Create:** `plugins/vorago/tests/unit/controller/editor_layout_test.cpp` (new). Reads the uidesc from
  `VORAGO_RESOURCES_DIR "/editor.uidesc"` (defined in `plugins/vorago/tests/CMakeLists.txt`); uses
  `Krate::Test::extractControlTagMap` / `unreachableParams` (`tests/test_helpers/uidesc_reachability.h:43`, `:88`),
  `VoragoTest::kExpectedParams` (`plugins/vorago/tests/unit/param_table_expected.h:66`; list = `flags & kIsList`,
  hidden = `flags & kIsHidden`). Tag `[vorago][controller][ui]`.
- **Write:**
  - `Vorago_UidescBindsEverySurfaceId` (SC-002): `unreachableParams(xml, all108, {4, 5})` empty; multiset of
    `control-tag="…"` values maps to exactly 106 elements, each registered non-hidden ID once; 4 and 5 appear 0 times;
    every element with `control-tag` has a non-empty `tooltip`.
  - `Vorago_UidescTagTable` (SC-003): every `<control-tag>` value is an ID in `kExpectedParams`; every referenced
    name is declared; the 14 Phase 11 names keep their values (`MasterGain=0, Polyphony=1, MacroDarkness=100 …
    MacroMass=111`); every name equals its `plugin_ids.h` enumerator minus `k`/`Id`.
  - `Vorago_UidescViewClassRule` (SC-004): anonymous-namespace stack-based element scan (`<view`, `<template`,
    `</view>`, `/>`) → class per bound view. Continuous (84 rows) → `ArcKnob`, except 0 and 3 → `CSlider`;
    list (22 rows) → `COptionMenu`, except 1115 and 1403 → `CCheckBox`; 100–111 → `ArcKnob`.
  - `Vorago_UidescLayout` (SC-005): template `size == minSize == maxSize == 1100, 760`; the seven labelled
    regions resolved to window coords: `header (0,0,1100,36)`, `concept-band (0,36,1100,436)`,
    `macros-left (0,36,350,436)`, `ecosystem (350,36,750,436)`, `macros-right (750,36,1100,436)`,
    `page-strip (0,436,1100,464)`, `page-area (0,464,1100,760)`; 12 macros ≥ 80×80 inside their block; bound IDs
    in `macros-left` are exactly 100–105 and in `macros-right` 106–111, and sorting each block by `(top,left)`
    yields ascending IDs; every page knob ≤ 48×48 inside `page-area`; each macro/page control has a sibling
    `CTextLabel` with no `control-tag` and `mouse-enabled="false"`; per `page-N` the descendant ID set equals spec
    C-4 (page-0 {200–206,1300,1301}; page-1 {300–302,310–313,320–323,330–333,340–343,350–353};
    page-2 {400–403,500,501,510–515}; page-3 {1000–1005,1200–1206}; page-4 {600,601,610–612,700–702};
    page-5 {1100–1115}; page-6 {800,900,1400–1403,1500–1502}); union 90 (106 − 4 − 12), no page ID outside `page-area`;
    every `class=` ∈ {CViewContainer, CTextLabel, CSlider, COptionMenu, CCheckBox, CSegmentButton, CView, ArcKnob};
    every `custom-view-name=` ∈ {EcosystemView, PresetBrowserButton}.
  - `Vorago_Editor_PageSwitch` (SC-017): headless `VST3Editor` open idiom from
    `editor_lifecycle_test.cpp:154-200`; find the 7 containers whose `uidesc-label` is `page-N` (via the
    `labelAttrID` attribute walk), the `CSegmentButton`, the `EcosystemView`. First open of a fresh controller:
    exactly `page-0` visible, `getSelectedSegment() == 0`. For k in 0..6: `setSelectedSegment(k); valueChanged();`
    → exactly container k visible; ecosystem view and the 12 macros visible at unchanged rects. Close, reopen →
    still page k.
  - `Vorago_Editor_PageSwitchIsSessionOnly` (SC-018 session arm): recording `IComponentHandler` (shape of
    `plugins/seraphis/tests/integration/preset_load_test.cpp:1-80`); drive every page change on the built
    segment button as `beginEdit(); setSelectedSegment(k); valueChanged(); endEdit();` over all 7 pages with
    repeats and back to 0 → 0 begin/perform/end; `getParameterCount() == 108` before and after; every
    `getParamNormalized` unchanged. Same for the preset button with the full `OutlineBrowserButton::onMouseDown`
    triple (`outline_button.h:79-131`) → still 0 edits.
  - `Vorago_GravityMacro_AnchorModeDisplay` (SC-022): Arm A (Free = `indexToNormalized(0, 3)` set **before**
    open, `param_mapping.h:62`) → knob tooltip (`getAttribute(kCViewTooltipAttribute)`) is the inert text, label
    `"Gravity (inert)"`, `gravityDisplayInertForTest() == true`; Arm A′ Keyed (`1`); Arm B open Hybrid → close →
    set Free → reopen inert; close → set Hybrid → reopen: normal tooltip, label `"Gravity"`, flag false; Arm C live:
    open Hybrid, set Free → inert without reopen, Keyed → inert, Hybrid → normal. Throughout, knob
    `getAlphaValue()`, `getMouseEnabled()`, value unchanged; recording handler sees 0 edits.
- **Red state:** fails until T013–T017.
- **Verify (after T020):** `vorago_tests "Vorago_Uidesc*"`, `"Vorago_Editor_*"`, `"Vorago_GravityMacro_*"`.

### T007 [P] — preset browser round-trip test (SC-023) — test only; implementation is T014

- **Create:** `plugins/vorago/tests/integration/preset_browser_test.cpp` (new).
- **Write** `TEST_CASE("Vorago_PresetBrowser_SaveLoadRoundTrip", "[vorago][integration][preset]")`: a
  `ProcessorFixture` and a real `Controller`; a component-handler stub that records begin/perform/end and answers
  `IComponent` by forwarding to the fixture's processor (Seraphis `preset_load_test.cpp` shape).
  (1) headless editor open → `presetBrowserViewForTest() != nullptr`; drive the preset button (`onMouseDown`
  triple on the built `OutlineBrowserButton`) → browser `isOpen()`; close editor with browser open →
  `presetBrowserViewForTest() == nullptr` (R-2: there is no save dialog).
  (2) `createComponentStateStream()` → stream bytes equal `proc->getState` bytes, length 428.
  (3) randomize all 106 non-hidden IDs via `setParamNormalized` with a seeded `Xorshift32`, list IDs snapped to
  their steps. (4) `loadComponentStateWithEdits(stream)` → `true`; all 106 values equal the captured ones; the
  handler holds exactly one Begin/Perform/End triple per changed ID; a second controller fed the same stream
  through `setComponentState` has identical values. (5) a stream with version 3 → `false`, 0 edits, values unchanged.
- **Red state:** does not compile until T014.
- **Verify (after T020):** `vorago_tests "Vorago_PresetBrowser_SaveLoadRoundTrip"`.

---

## Group 3 — the one `dsp/` edit (sequential; shared header)

### T008 — `EcosystemEngine` pair accessors (FR-030, FR-031, C-3) — makes T002 green

- **Edit:** `dsp/include/krate/dsp/systems/ecosystem_engine.h`.
- **Test first:** T002 (already written, currently red).
- **Implement:** directly after `getPairInteractionCount()` (`ecosystem_engine.h:961`) add exactly the three
  `[[nodiscard]] … const noexcept` getters of plan §2 — `getPairAgentA(std::size_t p)` →
  `(p < pairCount_) ? static_cast<std::size_t>(pairI_[p]) : std::size_t{0}`, `getPairAgentB` over `pairJ_`,
  `getPairFlow` → `(p < pairCount_) ? pairFlow_[p] : 0.0` — with the doxygen from plan §2 (contract class (i),
  recorded pre-stage-3 flow, identically 0 at predation 0.5). Members `pairI_/pairJ_/pairFlow_/pairCount_`
  exist at `:2459-2461`. **Do not** touch the include block (`:72-80`), any function body, or `kConfigKnobCount`.
- **ODR:** `grep -rn "getPairAgentA\|getPairAgentB\|getPairFlow" dsp/ plugins/` → only this header + T002 + later consumers.
- **Verify (after T020):** `dsp_systems_tests` full exe green (FR-031: no existing case edited) and
  `"EcosystemEngine_PairAccessors"` green.

---

## Group 4 — frame builder (sequential; single new-file task, depends on T008)

### T009 — pure frame-fill functions + their unit test (FR-020a, FR-027, SC-006 fn arm, SC-007 b–d, SC-011 fn arm)

- **Create (new):** `plugins/vorago/tests/unit/ecosystem_frame_builder_test.cpp`,
  `plugins/vorago/src/processor/ecosystem_frame_builder.h`.
- **Test first** (tag `[vorago][ecosystem][builder]`; this TU goes in the `-fno-fast-math` list in T020):
  - `Vorago_EcosystemFrameLayout`: `REQUIRE(sizeof(EcosystemFrame) == 1072)`,
    `offsetof(EcosystemFrame, linkStrength) == 812`, `offsetof(EcosystemFrame, linkFlowScale) == 1068`,
    `std::is_trivially_copyable_v<EcosystemFrame>`.
  - `Vorago_EcosystemEnergyGlow`: budget 1, agentCount 32, `energy = share / 32` for
    shares `{0, .5, 1, 2, 4, 8, 16, 48}` → strictly increasing after share 0; `== 0.5f` at share 1; `< 1.0f` at 48;
    `0.0f` at share −1; `0.0f` for a quiet NaN energy built as `volatile std::uint64_t b = 0x7FF8000000000000ull;`
    → `std::bit_cast<double>(b)`.
  - `Vorago_SanitizeFrameFloat`: qNaN `0x7FF8000000000000`, sNaN `0x7FF0000000000001`, +Inf `0x7FF0000000000000`,
    −Inf `0xFFF0000000000000` (all from bits via `volatile`), `1e300`, `-1e300` → exactly `0.0f`; `0.25`, `-3.5`,
    `double(FLT_MAX)` → `static_cast<float>(v)`.
  - `Vorago_SelectStrongestLinks`: `std::array<std::uint16_t, EcosystemEngine::kMaxPairs> scratch{}`; engine
    stepped one step at a time (T002's stepping). (b) `PrepareConfig{.agentCount = 48}`, `setKernelSigma(0.35f)`:
    `getPairInteractionCount() == 1128` on at least one step; reference = sort `|flow|` desc, drop zeros, take 64;
    multiset of `linkStrength` == multiset of `static_cast<float>(|ref|)` exactly; every `(linkA, linkB)` is a
    recorded pair whose `|flow|` narrows to that strength; `linkCount == 64`. (c) `setPredation(0.5f)`:
    `linkCount == 0` every step, `pairCount > 0` seen. (d) `agentCount = 1`: `linkCount == 0`. Every arm:
    `linkA/B < agentCount`, `linkA != linkB`, entries at/above `linkCount` are zero.
- **Implement** plan §3.2 exactly: includes `processor/ecosystem_frame.h`, `<krate/dsp/systems/ecosystem_engine.h>`,
  `<krate/dsp/core/db_utils.h>`, `<algorithm> <cmath> <cstddef> <cstdint> <limits> <span>`; `namespace Vorago`;
  `static_assert(kMaxFrameAgents == Krate::DSP::EcosystemEngine::kMaxAgents)`; `static_assert(EcosystemEngine::kMaxAgents <= 255)`;
  `inline noexcept` `sanitizeFrameFloat(double)` (bit-pattern finite check via `Krate::DSP::detail::isFinite`, then
  `|v| > FLT_MAX → 0.0f`, else `static_cast<float>`), `ecosystemEnergyGlow(double, std::size_t, double)`
  (`share/(1+share)` with the guards of §3.2), `selectStrongestLinks(eco, span<uint16_t, kMaxPairs>, frame)`
  (compact non-zero finite flows → `std::nth_element` with `|flow(a)| > |flow(b)|` when `m > 64` → write
  `linkA/B` as explicit `static_cast<std::uint8_t>`, `linkStrength = sanitizeFrameFloat(std::fabs(flow))` → zero
  `[k, 64)` → `linkCount = static_cast<std::uint8_t>(k)`). Banner: **processor-only; never included by `ui/` or
  `controller/`.**
- **ODR:** `grep -rn "\bsanitizeFrameFloat\b\|\becosystemEnergyGlow\b\|\bselectStrongestLinks\b" dsp/ plugins/` →
  only this header, its tests and T005.
- **Verify (after T020):** `vorago_tests "Vorago_EcosystemFrameLayout" "Vorago_EcosystemEnergyGlow" "Vorago_SanitizeFrameFloat" "Vorago_SelectStrongestLinks"`.

---

## Group 5 — processor producer (sequential; shared files `processor.h` + `processor.cpp`)

### T010 — DataExchange producer (FR-021–FR-027, C-2) — makes T005 green

- **Edit:** `plugins/vorago/src/processor/processor.h`, `plugins/vorago/src/processor/processor.cpp`.
- **Test first:** T005 (written, red).
- **Implement** plan §4.1–§4.3:
  - `processor.h`: forward-declare `namespace Steinberg::Vst { class DataExchangeHandler; }`; include
    `processor/ecosystem_frame.h`; public `connect`/`disconnect` overrides; the seams
    `setEcosystemFrameForcedForTest`, `setEcosystemFrameEnabledForTest`, `lastPublishedFrameForTest`,
    `ecosystemFrameProcessCallCountForTest`, `ecosystemFramePublishAttemptCountForTest`,
    `ecosystemFrameSkippedBlockCountForTest`, `ecosystemFocusRuleForTest`; private `publishEcosystemFrame()`,
    `std::unique_ptr<Steinberg::Vst::DataExchangeHandler> dataExchangeHandler_`, `EcosystemFrame pendingFrame_{}`,
    `std::array<std::uint16_t, Krate::DSP::EcosystemEngine::kMaxPairs> linkScratch_{}`, the `frame*` state of
    §4.1 (`frameResyncPending_` is `std::atomic<bool>{true}`), `kEcosystemFrameSilenceLevel = 1.0e-4f`. The
    existing `static_assert(sizeof(Processor) < 64u * 1024u)` (`processor.h:280`) must still compile.
    Because the destructor now owns an incomplete-type `unique_ptr`, make sure `~Processor` is defined in
    `processor.cpp` (or already out of line) — check before editing.
  - `processor.cpp`: include `processor/ecosystem_frame_builder.h` and
    `public.sdk/source/vst/utility/dataexchange.h`. `connect`/`disconnect` copied from Seraphis
    `processor.cpp:1194-1220` with config `blockSize = sizeof(EcosystemFrame)`, `numBlocks = 4`, `alignment = 32`,
    `userContextID = kEcosystemFrameUserContextId`; after `onConnect`, `frameResyncPending_.store(true, relaxed)`;
    `disconnect` → `onDisconnect(other)` then `dataExchangeHandler_.reset()`. In `setActive` (`processor.cpp:227-245`):
    `true` → if handler, `onActivate(processSetup)`; `false` → if handler, `onDeactivate()` **before** the existing
    release/silence block. `publishEcosystemFrame()` body in the normative order of plan §4.3 steps 0–7 (gate,
    process counter, focus rule a/b/c over `VoragoEngine::kMaxVoices`, habitat read, trigger with `!=`, attempt
    counter, fill through T009's functions and `const` getters only, zero-fill tails, transport with the
    invalid-id/null/size check and skipped counter). Call it exactly once between `numPedalPoints_ = 0;`
    (`processor.cpp:391`) and `data.outputs[0].silenceFlags = 0;` (`:393`); no early return reaches it.
- **RT:** no allocation, lock, exception or I/O in `publishEcosystemFrame`; no `std::exp`; no `std::isnan`.
- **ODR:** re-run the plan §12 sweep for `publishEcosystemFrame`, `setEcosystemFrameEnabledForTest`,
  `kEcosystemFrameSilenceLevel` → only Vorago.
- **Verify (after T020):** `vorago_tests "Vorago_EcosystemFrame_*"` green; all pre-existing `vorago_tests` cases green.

---

## Group 6 — CPU gate (sequential; shared file)

### T011 — arm PF + worst-case link timing (SC-012, SC-007 e)

- **Edit:** `plugins/vorago/tests/integration/processor_cpu_test.cpp`.
- **Test (this task is test-only):** in `Vorago_ProcessorCpu` (`[vorago][.perf][performance]`) add `fixturePF`
  configured exactly like arm P plus `setEcosystemFrameForcedForTest(true)`; the trial loop times **P, PF, D**
  interleaved per trial (same `kTrials = 16`, `kBlocksPerTrial = 100`, `processor_cpu_test.cpp:72-74`);
  `REQUIRE(pfBestNs <= kWrapperOverheadCeiling * dBestNs)` with the unchanged `kWrapperOverheadCeiling = 1.05`
  (`:75`); `WARN` the PF/P ratio; the existing P gate unchanged. Add `TEST_CASE("Vorago_SelectStrongestLinks_WorstCase", "[vorago][.perf][performance]")`:
  T009 (b) configuration stepped until `getPairInteractionCount() == 1128`, best of 16 trials × 1000 calls to
  `selectStrongestLinks`, `WARN` ns/call and ns/call ÷ (512/48000 s); **not gated**. Add a "Phase 13 (SC-012)"
  paragraph to the file banner. Never relax a budget.
- **Verify (T023, isolated):** `node tools/run-cpu-tests.js vorago_tests` alone, nothing else running.

---

## Group 7 — preset tab labels (sequential; shared file)

### T012 — `makeVoragoPresetTabLabels()` (plan D-4)

- **Edit:** `plugins/vorago/src/preset/vorago_preset_config.h`.
- **Test first:** add to `Vorago_EditorLifecycle`'s `PresetConfigIsLive` section is **not** allowed here (that file
  is T017's); instead the assertion `makeVoragoPresetTabLabels() == std::vector<std::string>{"All", "Drones"}`
  lives in T017's edit (write it there first).
- **Implement:** `[[nodiscard]] inline std::vector<std::string> makeVoragoPresetTabLabels()` = `{"All"}` followed
  by `makeVoragoPresetConfig().subcategoryNames`, copied from Seraphis
  `plugins/seraphis/src/preset/seraphis_preset_config.h:52-60`; add `<string> <vector>` includes if absent.
- **ODR:** `grep -rn "makeVoragoPresetTabLabels" plugins/` → only this header and its callers.
- **Verify (after T020):** `vorago_tests "Vorago_EditorLifecycle"`.

---

## Group 8 — page sub-controller (single new-file task)

### T013 — `VoragoPanelSubController` (C-5, FR-043, FR-055, FR-056, SC-017, SC-018)

- **Create (new):** `plugins/vorago/src/ui/panel_sub_controller.h`, `plugins/vorago/src/ui/panel_sub_controller.cpp`.
- **Test first:** T006's `Vorago_Editor_PageSwitch`, `Vorago_Editor_PageSwitchIsSessionOnly` and the Gravity
  capture in `Vorago_GravityMacro_AnchorModeDisplay` (written, red).
- **Implement** plan §5.3: in `namespace Vorago::UI`: `kSessionTagBase = 9000`, `kPresetButtonTag = 9000`,
  `kPageStripTag = 9100`, `kVoragoPageCount = 7`; `class VoragoPanelSubController : public VSTGUI::DelegationController`
  modelled on `plugins/seraphis/src/ui/edit_sub_controller.h:93`. `verifyView` reads raw `UIAttributes`:
  `uidesc-label="page-N"` (N 0..6) on a `CViewContainer` → `pages_[N]`, visible iff `N == owner_->activePage()`;
  `session-tag="pages"` on the `CSegmentButton` → `setTag(kPageStripTag)`, `setListener(this)`,
  `setSelectedSegment(activePage)`; `custom-view-name="PresetBrowserButton"` → `setTag(kPresetButtonTag)`,
  `setListener(this)`; tag `kMacroGravityId` + `dynamic_cast<Krate::Plugins::ArcKnob*>` → `owner_->registerGravityViews(knob, nullptr)`;
  `uidesc-label="macro-gravity-label"` on a `CTextLabel` → `registerGravityViews(nullptr, label)` (the controller
  merges non-null arguments); otherwise parent-null-guarded `DelegationController::verifyView`.
  `valueChanged`: page strip → `owner_->setActivePage(int(getSelectedSegment()))` + `applyPage`; preset button →
  `owner_->openPresetBrowser()`; any tag `>= kSessionTagBase` returns without forwarding; others → parent
  (null-guarded). `controlBeginEdit`/`controlEndEdit`: swallow session tags, forward others. `applyPage(k)`:
  `setVisible(i == k)` on non-null pages, `invalid()` on the page-area parent; no resize, no remove.
  `panel_sub_controller.cpp` includes `<ui/arc_knob.h>` so `gArcKnobCreator` (`arc_knob.h:716`) is registered in
  `vorago_tests`, which does not compile `entry.cpp` (plan R-9).
- **ODR:** `grep -rnE "(class|struct) VoragoPanelSubController\b" dsp/ plugins/` → only this file;
  `kSessionTagBase` also exists as `Seraphis::UI::kSessionTagBase` (`edit_sub_controller.h:62`) — different
  namespace, never both `using`'d in one TU.
- **Verify (after T020):** T006's page and Gravity cases.

---

## Group 9 — controller (sequential; shared files `controller.h` + `controller.cpp`)

### T014 — controller: consumer, custom views, sub-controller, overlays, providers, Gravity (FR-040–FR-043, FR-070–FR-073, FR-080–FR-081) — makes T004, T007 green

- **Edit:** `plugins/vorago/src/controller/controller.h`, `plugins/vorago/src/controller/controller.cpp`.
- **Test first:** T004, T006 (Gravity, page), T007 (written, red).
- **Implement** plan §5.1–§5.6:
  - `controller.h`: add base `Steinberg::Vst::IDataExchangeReceiver` and `DEF_INTERFACE(Steinberg::Vst::IDataExchangeReceiver)`
    in the existing `DEFINE_INTERFACES` block (`controller.h:52-54`, keep `DELEGATE_REFCOUNT`); declarations of
    `queueOpened`, `queueClosed`, `onDataExchangeBlocksReceived`, `notify`, `update`, `createCustomView`,
    `createSubController`, `didOpen`, `willClose`, `activePage`, `setActivePage` (clamp `[0, 6]`),
    `openPresetBrowser`, `registerGravityViews`, `refreshGravityDisplay`, the test seams
    (`cachedEcosystemFrame`, `ecosystemViewForTest`, `gravityDisplayInertForTest`, `presetBrowserViewForTest`,
    — R-2: **no** `savePresetDialogViewForTest`), `createComponentStateStream`, `loadComponentStateWithEdits`, private
    `applyStateStream` template and the members of §5.1. Includes only `processor/ecosystem_frame.h` from the
    processor side (POD; FR-020). **Rewrite the banner** (`controller.h:12-13`) to the Phase 13 state (§5.1 text).
  - `controller.cpp`: consumer per §5.2 (Membrum `controller.cpp:1697-1759` shape plus the context-ID check);
    `createCustomView` per §5.4 (`"EcosystemView"` → `new UI::EcosystemView(viewRect, &cachedFrame_)`;
    `"PresetBrowserButton"` → `new Krate::Plugins::OutlineBrowserButton(viewRect, nullptr, -1, "PRESETS")`; else
    `nullptr`); `createSubController("VoragoPanel")` → `new UI::VoragoPanelSubController(this, editor)`, else `nullptr`;
    `didOpen` builds `PresetBrowserView(frameSize, presetManager_.get(), makeVoragoPresetTabLabels())` and adds
    it to the frame (R-2: no `SavePresetDialogView`, no `savePresetDialogView_`, no
    `savePresetDialogViewForTest()`), then `addDependent` on `kResonanceAnchorModeId`'s `Parameter` and
    `refreshGravityDisplay()`; `willClose` closes an open browser before nulling, nulls every raw view
    pointer, removes the dependent; `terminate()` removes a still-registered dependent before the existing reset.
    Providers (§5.5): right after `presetManager_` is built (`controller.cpp:81-82`) set the state and load
    providers; update the comment at `:79-80`. Refactor `setComponentState` (`controller.cpp:91-157`) into
    `template <typename SetParam> tresult applyStateStream(IBStream*, SetParam&&)` **without behaviour change**;
    `setComponentState` passes the existing `setParamNormalized` lambda; `loadComponentStateWithEdits` passes
    `beginEdit; setParamNormalized; performEdit(id, getParamNormalized(id)); endEdit` and returns `== kResultOk`
    (a `version > kCurrentStateVersion` stream is rejected before any setter runs). `createComponentStateStream`
    is Ruinae `controller_presets.cpp:372-385` verbatim. Gravity (§5.6): `update` calls
    `EditControllerEx1::update` first, then `refreshGravityDisplay()` on `kChanged` for the observed parameter;
    `refreshGravityDisplay` computes `indexFromNormalized(getParamNormalized(kResonanceAnchorModeId), kNumResonanceAnchorModes)`,
    `gravityInert_ = idx != kResonanceAnchorModeDefault`, sets tooltip (`kGravityTip` / `kGravityInertTip`, file-scope
    `constexpr const char*`, `kGravityTip` equal to the uidesc tooltip string in T015) and label (`"Gravity"` /
    `"Gravity (inert)"`) + `invalid()`; **no** `setAlphaValue`, `setMouseEnabled` or parameter write.
- **Verify (after T020):** `vorago_tests "Vorago_Controller_ConsumesEcosystemFrame" "Vorago_PresetBrowser_SaveLoadRoundTrip" "Vorago_GravityMacro_AnchorModeDisplay"`;
  Phase 12 state suites (`state_roundtrip_test.cpp`, `state_v2_test.cpp`, `Vorago_ParameterInfoTable`) green
  **unedited** (SC-018).

---

## Group 10 — `editor.uidesc` (sequential; shared file)

### T015 — replace the editor wholesale (FR-001–FR-006, FR-010–FR-013, FR-055–FR-056, C-1, C-4)

- **Edit:** `plugins/vorago/resources/editor.uidesc` (replace the whole file; per the user's standing rule,
  generate the XML with a script — Node.js, not Python — rather than hand-typing 106 bindings; the script lives
  in the scratchpad, not the repo).
- **Test first:** T006's `Vorago_Uidesc*`, `Vorago_Editor_PageSwitch` (red).
- **Implement** plan §7: colours `bg, text, text-dim, track, accent` (keep values from the current file `:9-15`) +
  `eco-bg, eco-grid, eco-link, eco-partial, eco-resonator, eco-noise, eco-feedback, eco-ghost, macro-arc` as rgba
  literals, no bitmaps; fonts `label-font` (Arial 11), `macro-font` (Arial 13), `title-font` (Arial 16);
  control-tags: the 14 existing names with their values (`editor.uidesc:20-33`) + 92 new names in ascending ID
  order, each the `plugin_ids.h` enumerator minus `k`/`Id` (106 total; none for 4, 5). Template tree of §7.2:
  `editor` 1100×760 (size = minSize = maxSize), `sub-controller="VoragoPanel"`; `header` (title label,
  `CView custom-view-name="PresetBrowserButton"` 90×22, CSlider MasterGain, COptionMenu Polyphony, COptionMenu
  Seed, CSlider OutputSaturation, each with label); `concept-band` with `macros-left`/`macros-right` (6 cells
  175×133 each, ArcKnob 96×96 + mouse-disabled label; Gravity's label carries
  `uidesc-label="macro-gravity-label"`), `CView custom-view-name="EcosystemView" uidesc-label="ecosystem"` at
  local `350,0` size `400,400`; `CSegmentButton uidesc-label="page-strip" session-tag="pages"` with
  `segment-names="Cloud,Noise,Resonance,Body,Sub / Smear,Space,Life"` and no control-tag; `page-area` with 7
  sibling `CViewContainer uidesc-label="page-0".."page-6"`, each 1100×296 transparent (never a
  `UIViewSwitchContainer`). Page grid §7.3: 68×74 cells; continuous → ArcKnob 44×44 at `(cellX+12, cellY+4)` +
  label 68×14 at `(cellX, cellY+52)`; list → COptionMenu 96×18 with label above; 1115 and 1403 → CCheckBox 60×18;
  page rows exactly as the §7.3 table. Every bound view has a non-empty `tooltip`; Gravity's tooltip equals
  T014's `kGravityTip`.
- **Verify (after T020):** `vorago_tests "Vorago_Uidesc*" "Vorago_Editor_*" "Vorago_EditorLifecycle"`.

---

## Group 11 — `entry.cpp` (sequential; shared file)

### T016 — register `ArcKnob`, rewrite the FR-018 banner (FR-041)

- **Edit:** `plugins/vorago/src/entry.cpp`.
- **Implement:** add `#include <ui/arc_knob.h>` (Seraphis `plugins/seraphis/src/entry.cpp:34` form) which
  instantiates `gArcKnobCreator`; rewrite the banner at `entry.cpp:12-15` to state Phase 13 registers the shared
  `ArcKnob` creator here and that `EcosystemView` and the preset button are created by `createCustomView`, which
  needs no registration.
- **Verify:** `Vorago` target builds (T021); pluginval opens the editor with 106 controls (T025).

---

## Group 12 — lifecycle test edit (sequential; shared file)

### T017 — `Vorago_EditorLifecycle` updated (SC-016 a, FR-041, FR-042, FR-071)

- **Edit:** `plugins/vorago/tests/unit/controller/editor_lifecycle_test.cpp`.
- **Test (edit is the test):** `HarnessCycles` (`:143-148`) → `cycles = 10`. Replace `EditorBindsFourteenControls`
  (`:154-200`) with `SECTION("EditorBindsSurface")`: `collectBoundControls` (`:65-86`) filtered to
  `getTag() < Vorago::UI::kSessionTagBase` → exactly **106**; tag set = all 108 IDs except 4 and 5; exactly one
  `EcosystemView` (`dynamic_cast` walk); `ecosystemViewForTest() != nullptr` while open, `== nullptr` after close;
  `presetBrowserViewForTest()` non-null while open (R-2: no save dialog accessor); call `openPresetBrowser()`
  → `isOpen()` before closing; `== nullptr` after close; `createCustomView("Nope", …) == nullptr`;
  `createSubController("Nope", …) == nullptr`. In `PresetConfigIsLive` add
  `REQUIRE(Vorago::makeVoragoPresetTabLabels() == std::vector<std::string>{"All", "Drones"})`. Keep
  `CreateViewNames` (`:202-…`) unchanged. Update the file banner (`:6-20`).
- **Verify (after T020):** `vorago_tests "Vorago_EditorLifecycle"`.

---

## Group 13 — docs (sequential; shared files)

### T018 — `plugins/vorago/CLAUDE.md` (FR-061) and remove `.gitkeep` (FR-060)

- **Edit:** `plugins/vorago/CLAUDE.md` (the `ui/` "empty (`.gitkeep`) until Phase 13" line at `:15` and the
  skeleton line `:11`); **delete** `plugins/vorago/src/ui/.gitkeep`.
- **Content:** `ui/` holds `ecosystem_view.{h,cpp}` and `panel_sub_controller.{h,cpp}`; the frame data path
  (one-way, change-triggered, `'VECO'`, 1072-byte `EcosystemFrame`); `ecosystem_frame_builder.h` is
  processor-only; the `{4, 5}` reachability allowlist; the page table (plan §7.3); session tags `>= 9000`, never a
  ParamID; the two frame seams (forced = gate + trigger, enabled = gate only).
- **Verify:** read-back in the compliance pass (no test observes docs).

### T019 — `version.json` 0.2.0 and `plugins/vorago/CHANGELOG.md` (FR-063, R-6)

- **Edit:** `plugins/vorago/version.json` (`"version": "0.1.0"` → `"0.2.0"`; nothing else in the file) and
  `plugins/vorago/CHANGELOG.md`.
- **Heading (R-6):** one new `## [0.2.0] - 2026-09-26` section above `## [0.1.0] - 2026-09-24`
  (`CHANGELOG.md:8`), in the same voice as the 0.1.0 entry (a short lead paragraph, then `### Added`,
  `### Changed` where warranted, `### Known limitations`).
- **Content:** it covers **two phases**, because Phase 12 left no entry. Phase 12 (read
  `specs/vorago-phase12-parameters/spec.md` Overview and `plugins/vorago/src/plugin_ids.h` for the facts): the
  twelve macros are now live; every engine parameter is exposed (108 registered, 106 saved), with log tapers on
  time/frequency/rate controls; a 16-entry seed table with immediate live reseed; sustain pedal (CC64) and
  channel pressure (into the Pressure macro); state format v2 (v1 projects still load); the 0.1.0 "Known
  limitations" that Phase 12 removed must not be repeated. Phase 13 (this spec): the concept-first interface
  (twelve large macros, seven engine pages), the live ecosystem view (agents glow with energy, links fade with
  exchange strength, the habitat dims with the voice), the preset browser, the Gravity "inert" hint when
  Resonance Anchor is not Hybrid. Known limitations: no factory presets yet (Phase 14); the window is fixed at
  1100 × 760; the ecosystem view follows the newest note only.
- **Verify:** `node -e "JSON.parse(require('fs').readFileSync('plugins/vorago/version.json','utf8'))"` exits 0 and
  the file's `version` is `0.2.0`; `grep -c '^## \[0.2.0\]' plugins/vorago/CHANGELOG.md` prints 1.

---

## Group 14 — integration (sequential)

### T020 — CMake registration (single task; FR-060)

- **Edit:** `dsp/tests/CMakeLists.txt`, `plugins/vorago/CMakeLists.txt`, `plugins/vorago/tests/CMakeLists.txt`.
  - `dsp/tests/CMakeLists.txt`: add `unit/systems/ecosystem_engine_pair_accessors_test.cpp` after
    `unit/systems/ecosystem_engine_nonfinite_test.cpp` (`:491`), with a one-line comment
    (Vorago Phase 13 SC-019); add the same path to the `-fno-fast-math -fno-finite-math-only` list after
    `unit/systems/ecosystem_engine_nonfinite_test.cpp` (`:1022`) with a comment (reference must associate as the
    engine does).
  - `plugins/vorago/CMakeLists.txt` (`smtg_add_vst3plugin` list, `:23-66`): under Processor add
    `src/processor/ecosystem_frame.h`, `src/processor/ecosystem_frame_builder.h`; add a `# UI` block with
    `src/ui/ecosystem_view.h`, `src/ui/ecosystem_view.cpp`, `src/ui/panel_sub_controller.h`,
    `src/ui/panel_sub_controller.cpp`.
  - `plugins/vorago/tests/CMakeLists.txt`: under a `# Phase 13 (specs/vorago-phase13-ui)` comment add
    `unit/ecosystem_frame_builder_test.cpp`, `integration/ecosystem_frame_test.cpp`,
    `unit/controller/ecosystem_consumer_test.cpp`, `unit/controller/ecosystem_view_test.cpp`,
    `unit/controller/editor_layout_test.cpp`, `integration/preset_browser_test.cpp`; in the second-compilation
    block (`:44-47`) add `${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/ecosystem_view.cpp` and
    `${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/panel_sub_controller.cpp`; in the `-fno-fast-math` list (`:95-127`) add
    **only** `unit/ecosystem_frame_builder_test.cpp` (comment: NaN/Inf by bit pattern). Leave
    `integration/ecosystem_frame_test.cpp` and `integration/processor_cpu_test.cpp` out (plan §10 rationale).
- **Verify:** reconfigure is implicit on build; T021.

### T021 — build, zero warnings (FR-062, SC-021 MSVC arm)

- Build `dsp_systems_tests`, `Vorago`, `vorago_tests` with the full CMake path, capturing each log to a file
  in the scratchpad. **Zero** warnings and errors on all three (the post-build copy to
  `C:/Program Files/Common Files/VST3/` may fail with a permission error; that is not a compile failure).
  Fix every warning in the touched files before continuing.

### T022 — full-suite run (FR-031, SC-001–SC-011, SC-013–SC-019, SC-022, SC-023)

- `build/windows-x64-release/bin/Release/dsp_systems_tests.exe 2>&1 | tail -5` → all pass (FR-031: no existing case edited).
- `build/windows-x64-release/bin/Release/vorago_tests.exe 2>&1 | tail -5` → all pass, including every Phase 11/12
  case unedited. Capture the full output to a log once; never re-run a slow suite just to see output.
- Evidence checks: `git diff --stat dsp/tests` lists only the new TU and `dsp/tests/CMakeLists.txt`;
  `git diff --stat plugins/vorago/tests/unit/param_table_expected.h` is empty; `git diff --stat plugins/seraphis`
  is empty; `git diff dsp/include` touches only `ecosystem_engine.h` and only adds the three getters.
- Record the WARN outputs of SC-006 (held `voiceLevel` range, OQ-3) and SC-009 (time-to-Idle, R-4).

### T023 — CPU gate, isolated (SC-012, SC-007 e)

- With nothing else running (no build, no other suite, no clang-tidy), `node tools/run-cpu-tests.js vorago_tests`.
  `Vorago_ProcessorCpu` PF ≤ 1.05 × D must pass; record PF/P and the worst-case ns/call. A failure is re-run once
  alone after the machine has idled before being treated as a defect; never relax the budget.

### T024 — ASan lifecycle run (SC-016)

- Configure `-DENABLE_ASAN=ON` in a separate build dir (Debug), build `vorago_tests`, run
  `vorago_tests.exe "[lifecycle]"`; must be clean, including `Vorago_EcosystemView_AttachRemove`'s
  destroy-after-remove variant. Record the log path.

### T025 — pluginval (SC-020)

- `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Vorago.vst3"` passes.

### T026 — clang-tidy (FR-062, SC-021)

- `./tools/run-clang-tidy.ps1 -Target vorago -BuildDir build/windows-ninja` and `-Target dsp`: zero findings on the
  touched files (Windows: never the `.sh`).

### T027 — portability check (FR-062, SC-021)

- `node tools/check-portability.js` → clean; then `wsl --shutdown`. Any finding in a touched file is fixed and
  T021–T022 re-run for the affected target.
