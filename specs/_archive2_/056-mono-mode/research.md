# Research: Mono Mode Conditional Panel

**Date**: 2026-02-15 | **Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

## Research Summary

This spec has no NEEDS CLARIFICATION items. All decisions were resolved during spec clarification. The following research validates the chosen approach against the existing codebase.

## Research Task 1: Visibility Group Pattern Validation

**Question**: Does the existing sync toggle visibility pattern (6 implementations) support the Poly/Mono swap use case without modification?

**Decision**: Yes -- the identical pattern applies directly.

**Rationale**: The visibility group pattern works as follows:
1. Two overlapping CViewContainer elements in uidesc, one initially visible and one hidden
2. A `custom-view-name` attribute on each container for identification in `verifyView()`
3. A parameter change triggers visibility toggle via `setParamNormalized()` with null checks
4. Initial visibility set in `verifyView()` by reading current parameter value

The Poly/Mono swap maps exactly:
- `polyGroup_` = visible when kVoiceModeId < 0.5 (like `lfo1RateGroup_` when kLFO1SyncId < 0.5)
- `monoGroup_` = visible when kVoiceModeId >= 0.5 (like `lfo1NoteValueGroup_` when kLFO1SyncId >= 0.5)

**Alternatives considered**:
- UIViewSwitchContainer: More complex, designed for multi-state switching. Overkill for a binary toggle. Would require template views and a switch-control binding. All 6 existing groups use the simpler CViewContainer approach.
- Sub-controller: Would require a new class. The current inline pattern in the main controller is simpler and consistent.

**Evidence**: Examined all 6 existing implementations:
- `controller.cpp:510-514` -- LFO1 sync toggle
- `controller.cpp:515-518` -- LFO2 sync toggle
- `controller.cpp:519-522` -- Chaos sync toggle
- `controller.cpp:524-528` -- Delay sync toggle
- `controller.cpp:529-533` -- Phaser sync toggle
- `controller.cpp:534-538` -- TranceGate sync toggle

All follow the same 2-line pattern: `if (group_) group_->setVisible(condition);`

## Research Task 2: StringListParameter Auto-Population

**Question**: Do COptionMenu controls automatically populate their items from StringListParameter registrations?

**Decision**: Yes -- the VST3Editor binding handles this automatically.

**Rationale**: The `registerMonoModeParams()` function creates Priority and PortaMode as StringListParameter via `createDropdownParameter()`. When a COptionMenu's `control-tag` matches a StringListParameter, the VST3Editor framework automatically populates the menu items from the parameter's string list. This is documented in the vst-guide PARAMETERS.md skill.

**Evidence**: The existing Polyphony dropdown (control-tag="Polyphony", tag=2) works this way -- it shows "1 voice" through "32 voices" without any manual menu population in the controller. Similarly, all other COptionMenu dropdowns bound to StringListParameter instances auto-populate.

**Alternatives considered**: Manual population via `menu->addEntry()` in `verifyView()`. Rejected because the framework handles this automatically and all existing dropdowns use auto-population.

## Research Task 3: ArcKnob Size in Single-Row Layout

**Question**: What size ArcKnob fits in the single-row (18px height) MonoGroup container?

**Decision**: Use an 18x18 mini ArcKnob. This fits the 18px row height exactly with no overflow.

**Rationale**: The original two-row layout proposed a 28x28 ArcKnob at y=18, which would extend to panel y=82 (y=36+18+28) -- overlapping the Output knob at panel y=58. The analysis step (A1/A13) caught this critical overlap. The resolution was to use a single-row layout with all 4 controls at y=0 within the 112x18px MonoGroup container. An 18x18 ArcKnob fits exactly within the 18px row height.

**Alternative considered**: Two-row layout with 28x28 ArcKnob. Rejected because it physically overlaps the Output knob (only 4px gap between y=54 and y=58). The 18x18 mini knob is compact but functional -- the user can still drag to adjust portamento time, and the tooltip shows the exact value.

## Research Task 4: Thread Safety of setParamNormalized Visibility Toggle

**Question**: Is the visibility toggle in `setParamNormalized()` thread-safe?

**Decision**: Yes, with the existing null-check guard pattern.

**Rationale**: Per the vst-guide THREAD-SAFETY.md, `setParamNormalized()` can be called from any thread (automation, state loading). The existing pattern uses null-checked view pointers: `if (polyGroup_) polyGroup_->setVisible(...)`. When the editor is not open, the pointers are null and the call is a no-op. When the editor is open, the VSTGUI framework marshals the visibility change to the UI thread internally via `CView::setVisible()`.

All 6 existing sync toggle groups use this exact approach without any deferred update mechanism or explicit thread marshaling. The null-check is the safety mechanism.

**Evidence**: All existing toggles at `controller.cpp:510-538` use the same `if (ptr) ptr->setVisible(...)` pattern.

## No Remaining Unknowns

All NEEDS CLARIFICATION items from the Technical Context have been resolved. No external library research was needed. No context7 queries were required -- this is a pure internal codebase pattern replication task.
