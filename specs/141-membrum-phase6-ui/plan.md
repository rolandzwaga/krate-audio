# Implementation Plan: Membrum Phase 6 -- Macros, Acoustic/Extended Modes, and Custom Editor

**Branch**: `141-membrum-phase6-ui` | **Date**: 2026-04-12 | **Spec**: `specs/141-membrum-phase6-ui/spec.md`
**Input**: Feature specification from `/specs/141-membrum-phase6-ui/spec.md`

## Summary

Phase 6 is Membrum's playable debut: it replaces the host-generated 1,200+ parameter list with a full custom VSTGUI editor (1280x800 default, 1024x640 Compact, user-toggled) that exposes the 4x8 Pad Grid, a selected-pad editor, and a kit column. It lands the five per-pad Macros (Tightness, Brightness, Body Size, Punch, Complexity -- 160 new parameters) driven by a **Processor-side `MacroMapper`** running at control-rate inside `processParameterChanges()`. It delivers the Acoustic/Extended UI mode toggle via `UIViewSwitchContainer`, the 32x32 Tier 2 Coupling Matrix editor (with Reset, Solo, activity overlay), the kit and per-pad preset browsers (reusing `Krate::Plugins::PresetBrowserView`), output routing UI, choke group UI, voice management UI, and the Pitch Envelope promoted to a primary voice control. Ships as **v0.6.0** with **state version 6** (v5->v6 migration assigns macros=0.5; `kUiModeId` and `kEditorSizeId` are session-scoped and NOT written into the state blob). Zero audio-thread allocations; pad-glow published via a pre-allocated 1024-bit `std::atomic<uint32_t>[32]` bitfield; matrix activity via a pre-allocated 1024-bit `std::atomic<uint32_t>[32]` bitfield (one word per source pad, bits encoding active destination pads) over the same pattern.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: VST3 SDK 3.7.x, VSTGUI 4.12+, KrateDSP (no new DSP), Krate::Plugins shared UI (`ArcKnob`, `ToggleButton`, `ActionButton`, `ADSRDisplay`, `XYMorphPad`, `PresetBrowserView`, `SavePresetDialogView`, `CategoryTabBar`)
**Storage**: Binary state stream (VST3 IBStream); state version 6. Session-scoped params (`kUiModeId`, `kEditorSizeId`) are NOT serialized. Kit preset files (JSON per Phase 4 preset infrastructure) MAY encode `uiMode`.
**Testing**: Catch2 (`membrum_tests`, `dsp_tests`), pluginval strictness 5, `auval -v aumu Mb01 KrAu`, clang-tidy (`./tools/run-clang-tidy.ps1 -Target membrum`), AddressSanitizer (`-DENABLE_ASAN=ON`)
**Target Platform**: Windows (MSVC), macOS (Clang + AU wrapper), Linux (GCC)
**Project Type**: Monorepo plugin (`plugins/membrum/`)
**Performance Goals**: Total CPU within Phase 5 budget + <= 0.5% headroom with editor open, pad glow active, meters running, 8 voices sounding (SC-013). Pad selection -> panel refresh <= 16.7 ms (SC-004). Pad-glow visual latency <= 50 ms (SC-005). Matrix editor redraw capped at 30 Hz (R2 mitigation).
**Constraints**: Zero audio-thread allocations; no `std::mutex` / exceptions / I/O in audio thread; VSTGUI-only UI (no Win32/Cocoa); `MacroMapper` deterministic (offsets relative to registered defaults, not live values).
**Scale/Scope**: 2 new globals (`kUiModeId = 280`, `kEditorSizeId = 281`), 160 new per-pad macro params (offsets 37-41 x 32 pads), 1 new `editor.uidesc` (two top-level templates: `EditorDefault`, `EditorCompact`), 2 new custom VSTGUI views (`PadGridView`, `CouplingMatrixView`), 1 new processor component (`MacroMapper`), 2 new lock-free publishers (`PadGlowPublisher`, `MatrixActivityPublisher`), state v5->v6 migration. Kit Column also wires 4 existing Phase 5 global knobs (Global Coupling, Snare Buzz `kSnareBuzzId`, Tom Resonance `kTomResonanceId`, Coupling Delay `kCouplingDelayId = 273`) as UI-only controls with no new parameter allocations. Roughly 18 new source files, 14 new test files.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. VST3 Architecture Separation | PASS | `MacroMapper` lives Processor-side; Controller owns UI state only. Communication via `IParameterChanges` (macros -> underlying params are Processor-internal; no IMessage needed). Pad-glow and matrix activity use lock-free atomic bitfields (no IMessage, no DataExchange). Meters/CPU use `Steinberg::Vst::DataExchangeHandler` for compact periodic publication. |
| II. Real-Time Audio Thread Safety | PASS | `MacroMapper::apply()` is pure arithmetic on `PadConfig` + atomic parameter writes; no allocations. Pad-glow writes a single `std::atomic<uint32_t>` per audible pad per block (`memory_order_relaxed`). Matrix activity: same pattern. All buffers pre-allocated in `initialize()`. |
| III. Modern C++ Standards | PASS | C++20; RAII; `std::unique_ptr` for owned views; `constexpr` macro offset tables; no raw `new`/`delete` except where VSTGUI requires it (`createView` / `createSubController` return raw pointers per SDK contract). |
| IV. SIMD & DSP Optimization | PASS (N/A) | No new DSP. `MacroMapper` is ~10 multiply-adds per pad per block (<< 0.01% CPU). SIMD not beneficial; see SIMD analysis below. |
| V. VSTGUI Development | PASS | All UI is `UIDescription` XML + custom `CView` subclasses. `UIViewSwitchContainer` drives Acoustic/Extended swap. Editor-size toggle uses `VST3Editor::exchangeView()` between `EditorDefault` and `EditorCompact` templates. |
| VI. Cross-Platform Compatibility | PASS | No Win32/Cocoa. Preset save name entry via `Krate::Plugins::SavePresetDialogView` (already cross-platform). Modifier-key detection for audition (shift-click) uses VSTGUI `MouseEvent::modifiers`. |
| VII. Project Structure & Build System | PASS | New files follow monorepo layout (`plugins/membrum/src/ui/`, `plugins/membrum/src/processor/macro_mapper.{h,cpp}`, `plugins/membrum/resources/editor.uidesc`). CMake additions contained in `plugins/membrum/CMakeLists.txt`. |
| VIII. Testing Discipline | PASS | Test-first for `MacroMapper`, state v5->v6 migration, `PadGlowPublisher`/`MatrixActivityPublisher`, `CouplingMatrixView` activity/override round-trip, automated "every registered param reachable in editor" check (SC-002). |
| IX. Layered DSP Architecture | PASS | No new DSP layers touched. `MacroMapper` is plugin-local (Membrum namespace). |
| X. DSP Processing Constraints | PASS (N/A) | No saturation/oversampling/interpolation decisions introduced. |
| XI. Performance Budgets | PASS | Within 5% total. `MacroMapper` amortizes across one control-rate pass per block. Pad glow + matrix activity each: 32 atomic writes/frame max. |
| XII. Debugging Discipline | PASS | `vst-guide` skill consulted for `UIViewSwitchContainer`, `IDependent`, `DataExchangeHandler`, `exchangeView`. Ruinae/Innexus editors used as reference patterns. |
| XIII. Test-First Development | PASS | Every FR gets a failing test first. Tests enumerate Controller's registered parameter set and verify each appears in `editor.uidesc` control-tags (SC-002 automation). |
| XIV. Living Architecture Documentation | PASS | `specs/_architecture_/` updated at final phase with `MacroMapper`, `PadGlowPublisher`, `MatrixActivityPublisher`, `PadGridView`, `CouplingMatrixView` entries. |
| XV. ODR Prevention | PASS | All planned types verified unique via grep. See Codebase Research. |
| XVI. Honest Completion | PASS | Compliance table pre-seeded with `pending`; each FR/SC individually verified at implementation close. |
| XVII. Framework Knowledge Documentation | PASS | Research section documents `UIViewSwitchContainer` semantics, `VST3Editor::exchangeView()` contract, `DataExchangeHandler` lifecycle, `IDependent` deferred-update rules, Innexus/Ruinae precedents. |
| XVIII. Spec Numbering | PASS | 141 = 140 + 1; verified via `ls specs/ | grep -E '^[0-9]+'`. |

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step (per `tasks.md`, authored in `/speckit.tasks`)

**Required Check - Principle XV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XV - ODR Prevention)

*GATE: Completed BEFORE design. Verified via grep across `dsp/` and `plugins/`.*

### Mandatory Searches Performed

**Classes/Structs to be created**: `MacroMapper`, `PadGlowPublisher`, `MatrixActivityPublisher`, `PadGridView`, `CouplingMatrixView`, `PitchEnvelopeDisplay`, `MembrumEditorController` (sub-controller), `UiMode` (enum)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| `MacroMapper` | `grep -r "class MacroMapper\|struct MacroMapper" dsp/ plugins/` | No | Create new (`plugins/membrum/src/processor/macro_mapper.h`) |
| `PadGlowPublisher` | `grep -r "PadGlowPublisher" dsp/ plugins/` | No | Create new (`plugins/membrum/src/dsp/pad_glow_publisher.h`) |
| `MatrixActivityPublisher` | `grep -r "MatrixActivityPublisher" dsp/ plugins/` | No | Create new (`plugins/membrum/src/dsp/matrix_activity_publisher.h`) |
| `PadGridView` | `grep -r "class PadGridView" plugins/` | No | Create new (`plugins/membrum/src/ui/pad_grid_view.h`) |
| `CouplingMatrixView` | `grep -r "class CouplingMatrixView\|class MatrixEditor" plugins/` | No | Create new (`plugins/membrum/src/ui/coupling_matrix_view.h`) |
| `PitchEnvelopeDisplay` | `grep -r "class PitchEnvelopeDisplay" plugins/` | No | Create new (`plugins/membrum/src/ui/pitch_envelope_display.h`) -- patterned on `adsr_display.h` |
| `UiMode` enum | `grep -r "enum UiMode\|enum class UiMode" plugins/" | No | Create new (`plugins/membrum/src/ui/ui_mode.h`) |

**Utility Functions to be created**: `applyMacroTightness`, `applyMacroBrightness`, `applyMacroBodySize`, `applyMacroPunch`, `applyMacroComplexity`, `packGlowBits`, `unpackGlowBits`

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| `applyMacro*` | `grep -r "applyMacro" plugins/` | No | `macro_mapper.cpp` | Create new (private members of `MacroMapper`) |
| `packGlowBits` | `grep -r "packGlowBits\|quantizeAmplitude" plugins/` | No | `pad_glow_publisher.h` | Create new |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `ArcKnob` | `plugins/shared/src/ui/arc_knob.h` | plugin-shared | All knobs (macros, body controls, kit-level knobs) |
| `ToggleButton` | `plugins/shared/src/ui/toggle_button.h` | plugin-shared | Acoustic/Extended toggle, editor-size toggle, matrix Solo |
| `ActionButton` | `plugins/shared/src/ui/action_button.h` | plugin-shared | Audition triggers, matrix Reset |
| `OutlineBrowserButton` | `plugins/shared/src/ui/outline_button.h` | plugin-shared | Kit browser prev/next/browse, per-pad browser controls |
| `ADSRDisplay` | `plugins/shared/src/ui/adsr_display.h` | plugin-shared | Pattern reference for `PitchEnvelopeDisplay` |
| `XYMorphPad` | `plugins/shared/src/ui/xy_morph_pad.h` | plugin-shared | Material Morph editor (Extended mode) |
| `PresetBrowserView` | `plugins/shared/src/ui/preset_browser_view.h` | plugin-shared | Kit preset browser AND per-pad browser (two instances) |
| `SavePresetDialogView` | `plugins/shared/src/ui/save_preset_dialog_view.h` | plugin-shared | Save-as name entry for kit and per-pad presets |
| `PresetManager` | `plugins/shared/src/preset/preset_manager.h` | plugin-shared | Underlying preset save/load; two instances (kit scope, pad scope) |
| `CategoryTabBar` | `plugins/shared/src/ui/category_tab_bar.h` | plugin-shared | Preset browser tabs |
| `FieldsetContainer` | `plugins/shared/src/ui/fieldset_container.h` | plugin-shared | Group headings in Extended panel |
| `CouplingMatrix` | `plugins/membrum/src/dsp/coupling_matrix.h` | Membrum (Phase 5) | Data source for `CouplingMatrixView`; exposes `effectiveGain[32][32]`, `overrideGain`, `hasOverride`, Solo mask |
| `VoicePool` / `padConfigMut` | `plugins/membrum/src/voice_pool/voice_pool.h` | Membrum | Writable access for `MacroMapper` to store macro values, `outputBus`, `chokeGroup` |
| `Controller::createView` pattern | `plugins/innexus/src/controller/controller.cpp:1422` and `plugins/ruinae/src/controller/controller.cpp:312` | plugin | Reference for `VST3Editor` factory + `createSubController` + `didOpen` / `willClose` + preset-browser overlay add-to-frame |
| `DataExchangeHandler` integration | `plugins/innexus/src/processor/processor.h:728` + `processor.cpp:229-236` | plugin | Reference for CPU / meter / voice-count publication (`onActivate` / `onDeactivate` / `sendMainSynchronously`) |
| `IDependent` pattern | `plugins/ruinae/src/controller/controller.cpp` (post-315) | plugin | Reference for deferred `setVisible()` and `exchangeView` invocation on UI thread |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/` (all layers) -- no new DSP; nothing added
- [x] `plugins/shared/src/ui/` -- all reused components exist; no new shared UI created
- [x] `plugins/membrum/src/dsp/` -- `coupling_matrix.h` exists (Phase 5); new additions (`pad_glow_publisher.h`, `matrix_activity_publisher.h`) are unique names
- [x] `plugins/membrum/src/ui/` -- directory does not exist yet; will be created (FR-090 scope: pad grid + matrix view only are new custom views)
- [x] `plugins/membrum/resources/` -- no `editor.uidesc` exists yet (only `au-info.plist`, `auv3/`, `presets/`, `win32resource.rc`)
- [x] `specs/_architecture_/` -- no `MacroMapper` or `PadGlowPublisher` entries; will be added as final task
- [x] `plugin_ids.h` -- verified `kCouplingDelayId = 273` is last Phase 5 ID; Phase 6 range 280-299 is free

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types are unique across `dsp/` and `plugins/`. `PadGridView` / `CouplingMatrixView` / `PitchEnvelopeDisplay` are scoped inside the Membrum plugin namespace (`Membrum::UI`). `MacroMapper` is in the `Membrum` namespace with no collision. `UiMode` enum is inside `Membrum::UI` and does not collide with any other plugin's mode enum. The only borderline case is the name `PresetBrowserView` -- but we reuse the existing `Krate::Plugins::PresetBrowserView` rather than creating a new one.

## Dependency API Contracts (Principle XV Extension)

*GATE: Exact signatures copied from headers during planning.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| `PadConfig` | `couplingAmount` | `float couplingAmount = 0.5f;` (field) | Yes |
| `PadConfig` | fields for macros | new fields: `float macroTightness = 0.5f; float macroBrightness = 0.5f; float macroBodySize = 0.5f; float macroPunch = 0.5f; float macroComplexity = 0.5f;` (added this phase) | Yes (design) |
| `padParamId` | -- | `[[nodiscard]] constexpr int padParamId(int padIndex, int offset) noexcept` | Yes |
| `padOffsetFromParamId` | -- | `[[nodiscard]] constexpr int padOffsetFromParamId(int paramId) noexcept` (update `kPadActiveParamCountV6 = 42`, allow offsets 37-41) | Yes |
| `VoicePool::padConfig` | -- | `[[nodiscard]] const PadConfig& padConfig(int padIndex) const noexcept` | Yes |
| `VoicePool::padConfigMut` | -- | `[[nodiscard]] PadConfig& padConfigMut(int padIndex) noexcept` | Yes |
| `VoicePool::setPadConfigField` | -- | `void setPadConfigField(int padIndex, int offset, float normalizedValue) noexcept` (extend to accept offsets 37-41) | Yes |
| `CouplingMatrix::effectiveGain` | -- | `[[nodiscard]] float effectiveGain(int src, int dst) const noexcept` | Yes (Phase 5) |
| `CouplingMatrix::setOverride` | -- | `void setOverride(int src, int dst, float coeff) noexcept` | Yes (Phase 5) |
| `CouplingMatrix::clearOverride` | -- | `void clearOverride(int src, int dst) noexcept` | Yes (Phase 5) |
| `CouplingMatrix::hasOverride` | -- | `[[nodiscard]] bool hasOverride(int src, int dst) const noexcept` | Yes (Phase 5) |
| `Steinberg::Vst::DataExchangeHandler` | `onActivate` | `void onActivate(const ProcessSetup&)` | Yes (per Innexus) |
| `Steinberg::Vst::DataExchangeHandler` | `sendCurrentBlock` | `void sendCurrentBlock()` | Yes (per Innexus) |
| `VSTGUI::VST3Editor` | `exchangeView` | `bool exchangeView(UTF8StringPtr templateName)` | Yes (VSTGUI 4.12 `vst3editor.h`) |
| `VSTGUI::UIViewSwitchContainer` | `setCurrentViewIndex` | `void setCurrentViewIndex(int32_t index)` (driven automatically by `template-switch-control` parameter tag) | Yes |
| `VSTGUI::CFrame::addView` | -- | `bool addView(CView* view)` | Yes |
| `VSTGUI::CControl::setTag` / `setListener` | -- | standard | Yes |
| `PresetBrowserView` ctor | -- | `PresetBrowserView(const VSTGUI::CRect& size, PresetManager* presetManager, std::vector<std::string> tabLabels)` | Yes (preset_browser_view.h:58) |
| `PresetManager` save/load | -- | same API as Innexus/Ruinae use today | Yes |
| `EditControllerEx1::beginEdit/performEdit/endEdit` | -- | VST3 SDK standard | Yes |

### Header Files Read

- [x] `plugins/membrum/src/plugin_ids.h`
- [x] `plugins/membrum/src/dsp/pad_config.h`
- [x] `plugins/membrum/src/processor/processor.cpp` (getState/setState, parameter handling)
- [x] `plugins/shared/src/ui/preset_browser_view.h`
- [x] `plugins/innexus/src/controller/controller.cpp` (createView, createCustomView, createSubController, didOpen, DataExchange receiver)
- [x] `plugins/ruinae/src/controller/controller.cpp` (createView, template-switch patterns)
- [x] `plugins/innexus/src/processor/processor.h` / `processor.cpp` (DataExchangeHandler lifecycle)
- [x] `plugins/ruinae/resources/editor.uidesc` (UIViewSwitchContainer patterns)
- [x] `plugins/innexus/resources/editor.uidesc` (top-level Editor template with minSize/maxSize)

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| `UIViewSwitchContainer` | Switch control tag must be a StringListParameter with choice count matching the number of child templates | `kUiModeId` = 2 choices (Acoustic, Extended); child templates named accordingly |
| `VST3Editor::exchangeView` | Swapping templates destroys existing views; any `IDependent` subscribers held by child views MUST deregister in their destructors | Wire `removed()` to `removeDependent()` in `PadGridView`, `CouplingMatrixView`, etc. |
| `EditControllerEx1::setParamNormalized` | Called from ANY thread (automation, state load) -- never touch VSTGUI controls directly | Use `IDependent::update()` with deferred UI-thread dispatch |
| `PadConfig::padOffsetFromParamId` | Currently rejects `offset >= kPadActiveParamCountV5 (37)` | Bump to `kPadActiveParamCountV6 = 42` in Phase 6 |
| `DataExchangeHandler` | `sendCurrentBlock()` MUST be called from audio thread; block size is fixed at `onActivate()` | Allocate block struct of sizeof(MetersBlock) = 12 bytes; CPU + voice count + L/R peaks |
| `std::atomic<uint32_t>` | Lock-freedom must be verified once in `initialize()` | `static_assert(std::atomic<uint32_t>::is_always_lock_free)` |
| Shift-click detection | VSTGUI uses `CButtonState` and `MouseEvent::modifiers` | Check `event.modifiers.has(ModifierKey::Shift)` NOT a platform-specific virtual key |
| Session-scoped parameters | `kUiModeId` / `kEditorSizeId` must be registered as regular VST3 parameters (so they're automatable per FR-033) but excluded from `getState`/`setState` | In `getState`, simply do not write these; in `setState`, reset them to defaults before consuming blob. Note: the reset in `setState` happens in `Controller::setComponentState()` (which receives the processor state blob); the `Processor::getState`/`setState` simply excludes these IDs from the serialized blob. |
| `MacroMapper` delta math | Offsets are deltas from **registered default**, not live value | Cache registered defaults once in `MacroMapper::prepare()` from the Processor's parameter default table |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| `applyMacro*` | Plugin-specific (depends on Membrum param IDs and `PadConfig` layout) |
| `packGlowBits`/`unpackGlowBits` | Plugin-specific quantisation (5-bit amplitude buckets into per-pad 32-bit word) |
| `computeRegisteredDefaults` | Plugin-specific (reads Membrum's Controller default table) |

**Decision**: No Layer 0 extraction. All utilities are UI- or plugin-specific.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | NO | `MacroMapper` is purely forward: read 5 macros, compute 20-ish underlying-param deltas, write them. |
| **Data parallelism width** | 32 pads x 5 macros = 160 independent scalar computations per block | But the result is a parameter write; no hot loop per sample. |
| **Branch density in inner loop** | LOW | Per-macro logic is a handful of `lerp` / `exp` calls; no conditionals per sample. |
| **Dominant operations** | arithmetic + 1-2 transcendentals per macro application | `std::exp2`, `std::log`, `lerp` -- already low-cost at control-rate. |
| **Current CPU budget vs expected usage** | < 0.01% CPU expected | Completely dominated by the audio path and coupling engine. |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: `MacroMapper` runs once per audio block (typically 64-512 samples), not per sample. Total work is ~160 scalar multiply-adds per block. SIMD would save microseconds while adding complexity. The UI-side publishers (`PadGlowPublisher`, `MatrixActivityPublisher`) write one `uint32_t` atomically per active pad per block -- nothing to vectorize.

### Implementation Workflow

Not applicable (no Phase 2 SIMD).

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Early-out when `macroValue == 0.5` and not changed this block | ~80% savings in steady-state | LOW | YES |
| Dirty-rect redraw in `CouplingMatrixView` (only redraw cells whose `effectiveGain` changed) | Reduces UI CPU spike | MEDIUM | YES |
| 30 Hz cap on matrix editor redraw (R2 mitigation) | Bounds worst-case UI CPU | LOW | YES |
| Cache `registeredDefaults` table once (not per block) | ~100% savings on that lookup | LOW | YES (already planned) |

## Higher-Layer Reusability Analysis

**This feature's layer**: Plugin-local (Membrum UI + Processor integration)

**Related features at same layer**:
- Future Membrum v1 MIDI Learn UI (reuse `MidiCCManager` wiring to macro knobs)
- Gradus arp macro UI (future; `MacroMapper` pattern portable)
- Any future multi-pad instrument (`PadGridView` is theoretically configurable N x M)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| `MacroMapper` (pattern: control-rate forward driver from normalised macros to underlying params) | MEDIUM | Gradus, future Ruinae macro row | Keep local; extract pattern to `plugins/shared/` after 2nd use (Gradus) |
| `PadGridView` (configurable grid with selection, glow, shift-click audition) | MEDIUM | Future drum / step / pad instruments | Keep local; if a sibling needs it, extract to `plugins/shared/src/ui/` |
| `CouplingMatrixView` (NxN heat-map with click-to-edit + Solo) | LOW | Niche to sympathetic-resonance-style features | Keep local |
| `PadGlowPublisher` (32 atomic uint32_t bitfield pattern) | MEDIUM | Any lock-free 32-slot amplitude publisher | Keep local; extract to `dsp/include/krate/dsp/core/` after 2nd use |
| `PitchEnvelopeDisplay` | LOW | Niche to per-voice pitch envelopes | Keep local |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep `MacroMapper` plugin-local | First of its kind; no second consumer yet |
| Keep `PadGridView` plugin-local | No sibling pad-grid instrument shipping concurrently |
| Keep `PadGlowPublisher` plugin-local | Plugin-specific amplitude quantisation bucket count; extract later if reused |
| Reuse `PresetBrowserView` for BOTH kit and per-pad scopes | Two `PresetManager` instances with different root directories; no new browser code needed |

### Review Trigger

After implementing the next sibling feature that touches macros or a multi-pad grid:
- [ ] Does it need `MacroMapper` mechanics? -> Extract pattern to shared helper
- [ ] Does it need a configurable pad grid? -> Extract `PadGridView` to `plugins/shared/src/ui/`

## Project Structure

### Documentation (this feature)

```text
specs/141-membrum-phase6-ui/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output
│   ├── macro_mapper.h               # MacroMapper public contract
│   ├── pad_glow_publisher.h         # 1024-bit bitfield publisher contract
│   ├── matrix_activity_publisher.h  # 1024-bit matrix activity publisher contract
│   ├── meters_data_exchange.h       # DataExchange block schema (CPU/meters/voice count)
│   └── state_v6_migration.md        # State v5 -> v6 migration contract
├── checklists/
│   └── requirements.md  # Pre-existing
└── tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
plugins/membrum/
├── src/
│   ├── plugin_ids.h                          # MODIFIED: kUiModeId=280, kEditorSizeId=281,
│   │                                         #           kPadMacro* offsets 37-41, state v6,
│   │                                         #           kPhase6GlobalCount; static_asserts
│   ├── version.h.in                          # MODIFIED: 0.6.0 (driven by version.json)
│   ├── version.json                          # MODIFIED: "version": "0.6.0"
│   ├── dsp/
│   │   ├── pad_config.h                      # MODIFIED: 5 new fields (macroTightness, etc.),
│   │   │                                     #           kPadActiveParamCountV6=42
│   │   ├── pad_glow_publisher.h              # NEW: 32 x std::atomic<uint32_t>
│   │   └── matrix_activity_publisher.h       # NEW: 32 x std::atomic<uint32_t> (per-src activity mask)
│   ├── processor/
│   │   ├── processor.h                       # MODIFIED: add MacroMapper member, publishers,
│   │   │                                     #           DataExchangeHandler
│   │   ├── processor.cpp                     # MODIFIED: getState/setState v6, processParameterChanges
│   │   │                                     #           hooks for MacroMapper and publishers,
│   │   │                                     #           meters DataExchange publication
│   │   ├── macro_mapper.h                    # NEW: control-rate Processor-side component (see spec.md Key Entities)
│   │   └── macro_mapper.cpp                  # NEW
│   ├── controller/
│   │   ├── controller.h                      # MODIFIED: createView, createCustomView,
│   │   │                                     #           createSubController, DataExchange receiver,
│   │   │                                     #           didOpen/willClose, kit/pad preset manager handles
│   │   └── controller.cpp                    # MODIFIED: register kUiModeId, kEditorSizeId,
│   │                                         #           160 macro params; exclude from getState/setState
│   ├── parameters/
│   │   └── phase6_parameter_registration.h   # NEW: helper to register globals + macros
│   └── ui/                                   # NEW directory
│       ├── ui_mode.h                         # NEW: enum class UiMode { Acoustic, Extended }
│       ├── pad_grid_view.{h,cpp}             # NEW
│       ├── coupling_matrix_view.{h,cpp}      # NEW
│       ├── pitch_envelope_display.{h,cpp}    # NEW (patterned on adsr_display.h)
│       ├── membrum_editor_controller.{h,cpp} # NEW: sub-controller for pad grid / matrix wiring
│       └── editor_size_policy.h              # NEW: header-only helper (enum + constexpr constants for VST3Editor::exchangeView)
├── resources/
│   ├── editor.uidesc                         # NEW: two top-level templates (EditorDefault 1280x800,
│   │                                         #       EditorCompact 1024x640), UIViewSwitchContainer
│   │                                         #       for Acoustic/Extended, control-tags for all
│   │                                         #       Phase 1-6 params
│   ├── presets/                              # (existing; Phase 4)
│   ├── au-info.plist                         # UNCHANGED (no new buses)
│   └── auv3/audiounitconfig.h                # UNCHANGED
└── tests/
    ├── unit/
    │   ├── processor/
    │   │   ├── test_macro_mapper.cpp                 # NEW (FR-022, FR-023, SC-003, SC-006)
    │   │   ├── test_state_v6_migration.cpp           # NEW (FR-080..FR-084, SC-006, SC-007)
    │   │   ├── test_pad_glow_publisher.cpp           # NEW (FR-014, FR-101, SC-005)
    │   │   └── test_matrix_activity_publisher.cpp    # NEW (FR-052)
    │   ├── controller/
    │   │   ├── test_phase6_parameters.cpp            # NEW (FR-070, FR-071, FR-072)
    │   │   ├── test_ui_mode_session_scope.cpp        # NEW (FR-030, FR-081)
    │   │   ├── test_editor_size_session_scope.cpp    # NEW (FR-001, FR-040)
    │   │   └── test_param_reachability_in_editor.cpp # NEW (SC-002, FR-026)
    │   ├── ui/
    │   │   ├── test_pad_grid_view.cpp                # NEW (FR-010..FR-015, SC-004)
    │   │   ├── test_pitch_envelope_display.cpp      # NEW (FR-024)
    │   │   └── test_coupling_matrix_view.cpp         # NEW (FR-050..FR-054)
    │   └── preset/
    │       └── test_kit_uimode_roundtrip.cpp         # NEW (FR-030 preset override path)
    └── integration/
        └── test_editor_asan_lifecycle.cpp            # NEW (SC-014: open/close x100)
```

**Structure Decision**: Monorepo plugin layout preserved. A new `plugins/membrum/src/ui/` directory hosts custom views and the editor sub-controller; all shared UI is reused from `plugins/shared/src/ui/`. No new DSP layer code.

## Complexity Tracking

No constitution violations. All design decisions follow the Ruinae/Innexus editor precedent and the Phase 4/5 Membrum architecture.
