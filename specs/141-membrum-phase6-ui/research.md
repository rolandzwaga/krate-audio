# Phase 0 Research -- Membrum Phase 6 UI

**Spec**: `specs/141-membrum-phase6-ui/spec.md`
**Date**: 2026-04-12

This document consolidates research findings for the Phase 6 plan. It resolves the technical unknowns that were NOT covered by the 5 clarifications in the spec (those are already settled) and documents framework-level decisions with citations.

---

## 1. VSTGUI Template Switching for Dual Editor Sizes (1280x800 / 1024x640)

**Decision**: Use two independent top-level templates in `editor.uidesc` (`EditorDefault`, `EditorCompact`) and switch between them at runtime via `VSTGUI::VST3Editor::exchangeView(templateName)`. Drive the switch from the `kEditorSizeId` parameter via the `IDependent`/deferred-update pattern on the UI thread.

**Rationale**: `VSTGUI::UIViewSwitchContainer` is designed for swapping child views **within** a parent of a fixed outer size; it cannot change the editor's outer window size. `VST3Editor::exchangeView()` replaces the entire top-level template and its size is reported back to the host via `IPlugView::getSize()`, which is exactly what the host needs to resize the editor window. Innexus demonstrates the per-template `minSize`/`maxSize` mechanism (`plugins/innexus/resources/editor.uidesc:188-189`: `<template name="Editor" ... size="800, 1035" minSize="800, 1035" maxSize="800, 1035">`). We apply the same `minSize == maxSize == template size` idiom to each of the two templates so the host cannot resize them arbitrarily, preserving FR-001's "only snaps to one of the two template sizes" rule.

**Alternatives considered**:
- **A single template + `UIViewSwitchContainer` holding two full layouts stacked**: rejected -- the editor's outer window stays at one size, so the "Compact" layout would waste space in a 1280x800 window. Fails FR-001.
- **Custom `IPlugView` resize**: rejected -- reimplements what `exchangeView` already provides and breaks the Ruinae/Innexus precedent.
- **Host-driven continuous resize with breakpoint**: explicitly rejected by the spec's Clarification #5.

**Sources**:
- VSTGUI 4.12 `vstgui/uidescription/vst3editor.h` -- `exchangeView(UTF8StringPtr templateName)` method
- `plugins/innexus/resources/editor.uidesc:188-189` -- top-level template minSize/maxSize idiom
- `plugins/ruinae/resources/editor.uidesc:1063-1066, 2480-2484` -- `UIViewSwitchContainer` template-switch-control usage (distinguish from top-level swap)
- `extern/vst3sdk/vstgui4/vstgui/plugin-bindings/vst3editor.cpp` -- `exchangeView` implementation detail (calls `open()` on the new template, updates `IPlugFrame::resizeView`)

---

## 2. UIViewSwitchContainer for Acoustic/Extended Selected-Pad Panel

**Decision**: Use `UIViewSwitchContainer` with `template-switch-control="UiMode"` bound to the `kUiModeId` `StringListParameter`. Declare two child templates: `SelectedPadAcoustic` and `SelectedPadExtended`. Both templates bind to the same per-pad parameters (via the Phase 4 selected-pad proxy), so swapping templates costs nothing in parameter state.

**Rationale**: Ruinae already uses this pattern extensively (`editor.uidesc:1063-1066` for FilterType, `2480-2484` for ModSourceViewMode, `3798-3802` for OscAType). The pattern is proven, automatic, and requires no custom code. VSTGUI handles the view lifecycle (destruction/creation) on template swap, which means `IDependent` subscribers inside the child views MUST deregister in their destructors (documented as a gotcha in plan.md).

**Alternatives considered**:
- **Manual `setVisible(false)` for Extended controls**: rejected -- leaves Extended views in the view hierarchy, burning memory and potentially receiving param updates. Also violates FR-031 ("MUST use `UIViewSwitchContainer`").
- **Separate top-level templates per mode**: rejected -- would force a full editor-size swap on mode change, which is wrong (mode is orthogonal to size).

**Sources**:
- `extern/vst3sdk/vstgui4/vstgui/uidescription/uiviewswitchcontainer.h` / `.cpp`
- `plugins/ruinae/resources/editor.uidesc` (13+ UIViewSwitchContainer instances)

---

## 3. VST3 DataExchange vs Atomic Bitfield for Lock-Free Publishing

**Decision**: Use two publication mechanisms:
- **Pad glow & matrix activity** (high-rate, tiny payload): pre-allocated `std::array<std::atomic<uint32_t>, 32>` bitfields. Audio thread writes with `memory_order_relaxed`; UI thread reads at <= 30 Hz with `memory_order_acquire`. No DataExchange handler needed.
- **Meters + CPU + active-voice count** (low-rate, small fixed struct): `Steinberg::Vst::DataExchangeHandler` with a 12-byte block `MetersBlock { float peakL, peakR; uint16_t activeVoices; uint16_t cpuPermille; }`, sent once per block via `sendMainSynchronously`.

**Rationale**: Atomic bitfields are the lowest-overhead path when the payload fits in a cache line and the consumer is polled on a UI timer. Innexus demonstrates the DataExchange path for periodic display data (`plugins/innexus/src/processor/processor.h:728`, `processor.cpp:229-236`: `onActivate` / `onDeactivate`). DataExchange involves a queue, a handler, and a receiver callback -- overkill for a 4-byte pad-glow word. But DataExchange is the right tool for the meters/CPU/voice-count bundle because the receiver callback runs on the UI thread and the handler owns the memory (no separate allocator).

FR-014 and FR-101 explicitly mandate the 1024-bit bitfield path for pad glow. FR-043 and FR-044 allow either DataExchange or lock-free atomics for meters; DataExchange is cleaner for a compound struct.

**Alternatives considered**:
- **Single DataExchange block for everything**: rejected -- would bundle pad-glow (needs sub-frame update) with CPU (needs only 10 Hz), wasting bandwidth.
- **SPSC ring buffer**: rejected -- adds complexity with no benefit over a single-word atomic for a pure "latest value" use case.

**Sources**:
- `extern/vst3sdk/pluginterfaces/vst/ivstdataexchange.h` -- `IDataExchangeHandler`, `IDataExchangeReceiver` API
- `plugins/innexus/src/processor/processor.cpp:229-236` -- `DataExchangeHandler::onActivate/onDeactivate` lifecycle
- `plugins/innexus/src/controller/controller.cpp:1703-1740` -- `queueOpened`, `onDataExchangeBlocksReceived` receiver pattern

---

## 4. VSTGUI Custom View Lifecycle (PadGridView, CouplingMatrixView)

**Decision**: Register custom views via `VST3EditorDelegate::createCustomView` (the Innexus pattern at `controller.cpp:1434-1547`). Hold raw pointers to the views in the Controller (`padGridView_`, `couplingMatrixView_`). Clear these pointers in `VST3EditorDelegate::willClose()`. Views own their internal state; they register as `IDependent` on the parameters they read (pad glow bitfield polling via a `CVSTGUITimer`).

**Rationale**: Innexus already follows this exact pattern for `HarmonicDisplayView`, `ConfidenceIndicatorView`, `EvolutionPositionView`, etc. (see `controller.cpp:1466-1501`). The controller holds the raw pointer lifetime-bound to the editor (destroyed on editor close), not the controller. `willClose()` zeroes the pointer to prevent use-after-free if a DataExchange block arrives after editor close.

**Alternatives considered**:
- **Views as independent VSTGUI templates**: rejected -- custom rendering for the 32x32 matrix and the 4x8 pad grid is easier with a single `CView::draw()` override than fighting VSTGUI's layout system.
- **Views in the UIDescription as a generic `CViewContainer`**: rejected -- cannot easily host custom draw code.

**Sources**:
- `plugins/innexus/src/controller/controller.cpp:1434-1549` -- `createCustomView` dispatch table
- `plugins/innexus/src/controller/controller.cpp:1576-1700` -- `didOpen`/`willClose` lifecycle
- `extern/vst3sdk/vstgui4/vstgui/plugin-bindings/vst3editor.cpp` -- view ownership during `exchangeView`

---

## 5. Macro-Knob UX Precedents (u-he, Arturia, XLN Addictive Drums, Diva, NI Battery)

**Decision**: Model macros as **forward-only drivers with per-pad storage** (matches Spec 135's "on top of the detailed parameters"). Macro at 0.5 = neutral (zero delta from registered default). No bidirectional snap-back when underlying params move. Capture final delta magnitudes as named `constexpr` values in `macro_mapper.cpp` after listening tests.

**Rationale**:
- **u-he Diva**: macros drive multiple targets with independent curves; editing an underlying param does not update the macro. This matches our FR-023.
- **Arturia V Collection** (e.g., Pigments): macros are normalised drivers; underlying param positions are "base + macro offset". Our design mirrors this precisely with deltas-from-registered-default.
- **XLN Addictive Drums**: per-pad macro controls (Attack, Sustain, Damp, Pitch) are the primary UX surface for fast drum shaping. This validates the per-pad (not global) scope of Membrum's macros.
- **NI Battery**: per-pad macros are a standard drum-instrument pattern.
- **Serum/Phase Plant "macros"**: global (not per-pad) macros with mod-matrix targeting -- more flexible but more complex. Rejected for Membrum Phase 6 because a mod-matrix is explicitly out of scope (Spec 135 open question).

**Rationale for delta-from-registered-default (Clarification #1)**:
- **Preset-independent**: a macro at 0.5 always produces exactly the preset's authored values. Users can author presets without caring about macro state.
- **Deterministic**: no race between "current live value" and "macro-driven value" when automation is mid-flight.
- **Easy to invert conceptually**: the user sees macros as "offsets from the preset's sound", which is what macros actually are.

**Alternatives considered**:
- **Bidirectional macros** (moving an underlying param snaps the macro value): rejected by FR-022's "forward driver" rule and by Spec 135's "on top of" language.
- **Macro deltas relative to live values**: rejected by Clarification #1 (determinism).
- **Absolute macro targets**: rejected because macro=0.5 could not be "neutral" without re-writing every preset.

---

## 6. Steinberg IPlugView Size Negotiation

**Decision**: Implement `IPlugView::getSize()` by returning the current template's size (1280x800 or 1024x640). Implement `onSize()` to reject out-of-range sizes (the host is told via `checkSizeConstraint`). Implement `canResize()` to return `kResultFalse` (user-controlled toggle only; no host-initiated continuous resize). When the user flips `kEditorSizeId`, call `VST3Editor::exchangeView()` which triggers `IPlugFrame::resizeView()` internally -- the host then reshapes the outer window.

**Rationale**: The spec explicitly rejects a host-driven resize breakpoint (Clarification #5). `canResize() == false` is the correct VST3 signal. `exchangeView` handles the host re-query internally. Same pattern as Innexus's fixed-size editor but with two discrete sizes instead of one.

**Sources**:
- `extern/vst3sdk/pluginterfaces/gui/iplugview.h` -- `IPlugView::getSize`, `onSize`, `canResize`, `checkSizeConstraint`
- `extern/vst3sdk/vstgui4/vstgui/plugin-bindings/vst3editor.cpp` -- `exchangeView` calls `IPlugFrame::resizeView`
- Innexus fixed-size editor (no `canResize`)

---

## 7. Kit Preset Serialization of UI Mode (Session-Scope + Preset Override)

**Decision**: The `kUiModeId` parameter is registered as a regular VST3 parameter (so it's automatable per FR-033) but is **excluded from `IBStream` getState/setState**. Kit preset files (JSON, per the Phase 4 `PresetManager` infrastructure) MAY include an optional `"uiMode": "Acoustic"|"Extended"` field at the top level. When loading a kit preset:
  1. `PresetManager` reads the JSON.
  2. If `uiMode` is present, the Controller's `onKitPresetLoaded()` handler calls `setParamNormalized(kUiModeId, mode)` on the UI thread.
  3. If absent (v4/v5 presets), `kUiModeId` keeps its current value.
On plugin instantiation or a `setState` call that carries no preset, `kUiModeId` defaults to `Acoustic`.

**Rationale**: This implements Clarification #4 literally. DAW project state does not persist the mode (not written to `IBStream`), but kit presets can. The VST3 parameter remains automatable (FR-033) because it's registered in the Controller's parameter list -- absence from `IBStream` only affects persistence, not automation.

**Alternatives considered**:
- **Write `kUiModeId` into `IBStream`**: rejected by Clarification #4 ("DAW project state does NOT persist the mode independently of a kit preset").
- **Store `kUiModeId` in the Controller's private config file**: rejected -- fragile across hosts, violates "session-scoped" rule (survives across sessions via an external file).

**Sources**:
- `plugins/shared/src/preset/preset_manager.h` -- preset JSON format
- `plugins/membrum/src/processor/processor.cpp:670-805` -- existing state v5 getState/setState

---

## 8. Editor-Size Toggle Persistence (Session-Scope)

**Decision**: Same pattern as `kUiModeId`: registered VST3 parameter (automatable), but NOT written to `IBStream`. Unlike `kUiModeId`, **kit presets do NOT encode editor size** -- it's purely a user-preference display choice, orthogonal to the sound. On every plugin instantiation and every `setState`, `kEditorSizeId` resets to `Default` (1280x800).

**Rationale**: Clarification #5 specifies `kEditorSizeId` is session-scoped with no preset override. A user who opens an older kit preset gets Default size regardless of what size they had last time.

---

## 9. VSTGUI Modifier Keys for Shift-Click Audition

**Decision**: In `PadGridView::onMouseDown`, check `const auto& buttons = event.mouseButtons;` and `const auto& modifiers = event.modifiers;` to distinguish click vs shift-click vs right-click. Map `ModifierKey::Shift` and `MouseButton::Right` to "audition without changing selection" (FR-013).

**Rationale**: VSTGUI's `MouseEvent::modifiers` is cross-platform. Win32/Cocoa/X11 all deliver Shift/Ctrl/Alt through the same abstraction. Right-click is natively supported by `MouseButton::Right`. This is the ONLY correct path per Constitution Principle VI (no native Win32/Cocoa).

**Sources**:
- `extern/vst3sdk/vstgui4/vstgui/lib/events.h` -- `MouseEvent`, `ModifierKey`, `MouseButton`

---

## 10. Static Check: Every Parameter Reachable from Editor (SC-002)

**Decision**: A new test `test_param_reachability_in_editor.cpp` (a) enumerates all registered parameters via `Controller::getParameterCount()` + `getParameterInfo`, (b) parses `editor.uidesc` as XML, (c) extracts all `control-tag` `tag` values, (d) for each registered param, asserts either: the param ID appears as a control-tag, OR the param ID is in the macro mapping table (for Acoustic-mode reachability), OR the param is explicitly listed as a session-scoped non-UI param (`kUiModeId`, `kEditorSizeId`). Any miss fails the test.

**Rationale**: FR-026 ("Every parameter registered in Phases 1 through 5 MUST be reachable via the Extended-mode editor") and SC-002 ("100% of parameters MUST be reachable") require machine-verification. A manual audit of 1,200+ parameters is not reliable.

**Alternatives considered**:
- **Hand-curated list of expected params**: rejected -- drifts out of sync. The registered parameter set is the source of truth.
- **Runtime check via VST3Editor's view tree**: rejected -- runtime iteration of the view tree is fragile and misses the macro-mapping fallback.

---

## 11. Preset Browser Tabs for Membrum

**Decision**: Kit preset browser tab labels: `["Factory", "User", "Acoustic", "Electronic", "Percussive", "Unnatural"]`. Per-pad preset browser tab labels: `["Factory", "User", "Kick", "Snare", "Tom", "Hat", "Cymbal", "Perc", "Tonal", "FX"]` (matches the Phase 4 category set). Two separate `PresetManager` instances each rooted at its own directory (`{ProgramData}/Krate Audio/Membrum/Kits/` and `.../Pads/`).

**Rationale**: Reuses `Krate::Plugins::PresetBrowserView` unchanged. Tab labels align with Phase 4's existing preset categories.

**Sources**:
- `plugins/shared/src/ui/preset_browser_view.h:58-60` -- constructor accepts `std::vector<std::string> tabLabels`
- `plugins/innexus/src/controller/controller.cpp:1610-1612` -- `PresetBrowserView(frameSize, presetManager_.get(), getInnexusTabLabels())`

---

## Summary of Decisions

| Area | Decision | Status |
|------|----------|--------|
| Dual editor sizes | `VST3Editor::exchangeView` between `EditorDefault` and `EditorCompact` templates | Resolved |
| Acoustic/Extended swap | `UIViewSwitchContainer` bound to `kUiModeId` | Resolved |
| Pad glow / matrix activity publication | 32 x `std::atomic<uint32_t>` bitfields, 128 bytes total each | Resolved |
| Meters / CPU / voice count publication | `Steinberg::Vst::DataExchangeHandler` with 12-byte `MetersBlock` | Resolved |
| Custom view lifecycle | `createCustomView` + controller-held raw pointers + `willClose` cleanup | Resolved |
| Macro semantics | Forward-only driver, per-pad, delta-from-registered-default, neutral at 0.5 | Resolved (Clarification #1) |
| Macro execution location | Processor audio thread, once per block in `processParameterChanges()` | Resolved (Clarification #2) |
| Editor-size negotiation | `canResize() == false`, user toggle drives `exchangeView` | Resolved |
| Kit preset UI-mode override | Optional `"uiMode"` field in kit preset JSON | Resolved (Clarification #4) |
| Editor-size persistence | Session-scoped, not in preset, not in IBStream | Resolved (Clarification #5) |
| Shift-click audition | VSTGUI `MouseEvent::modifiers`, no native code | Resolved |
| Parameter-reachability check | Automated XML scan + macro table scan | Resolved |
| Preset browser reuse | Two `PresetManager` instances, one `PresetBrowserView` class | Resolved |

**NEEDS CLARIFICATION**: None. All technical unknowns resolved through research; all user-facing ambiguities resolved in the spec's Clarifications session (2026-04-12).
