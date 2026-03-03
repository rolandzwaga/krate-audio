# Research: ModMatrixGrid -- Modulation Routing UI

**Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

## R-001: VSTGUI CViewContainer Custom Subclass Patterns

### Context
ModMatrixGrid must manage dynamic child views (route rows, tab bar, heatmap) and needs a scrollable area. Need to determine the correct VSTGUI base class and subclassing pattern.

### Decision
ModMatrixGrid subclasses `VSTGUI::CViewContainer`.

### Rationale
- CViewContainer provides child view management (`addView`/`removeView`), mouse event routing to children, and automatic drawing of child views.
- The existing `FieldsetContainer` class in the codebase (`plugins/shared/src/ui/fieldset_container.h`) demonstrates this exact pattern: subclass CViewContainer, override `drawBackgroundRect()`, register via `ViewCreatorAdapter` with `getBaseViewName()` returning `kCViewContainer`.
- FieldsetContainer is ~317 lines, header-only, with a ViewCreator struct at the bottom. ModMatrixGrid follows the same structure.

### Alternatives Considered
| Alternative | Why Rejected |
|---|---|
| CView with manual child management | Would duplicate CViewContainer's child management logic |
| CScrollView as base | CScrollView is a specialized container for scrollable content; better used as a child of ModMatrixGrid for the route list area only |
| Custom IViewContainer | Unnecessary -- CViewContainer already provides all needed functionality |

### Evidence
- `FieldsetContainer` constructor: `explicit FieldsetContainer(const VSTGUI::CRect& size)` calls CViewContainer base
- `drawBackgroundRect()` override draws custom background before children
- `CLASS_METHODS(FieldsetContainer, CViewContainer)` macro for RTTI
- ViewCreator uses `getBaseViewName()` -> `UIViewCreator::kCViewContainer`

---

## R-002: Bipolar Slider Implementation

### Context
FR-007 requires a bipolar slider centered at zero with range [-1.0, +1.0]. Fill extends left from center for negative and right for positive. Need to determine if any existing VSTGUI control supports this or if a new CControl is needed.

### Decision
Create a new `BipolarSlider` class extending `VSTGUI::CControl`.

### Rationale
- VSTGUI's built-in `CSlider` fills from one end (min or max), not from center. Its internal value semantics assume unipolar fill direction.
- The VST boundary requires all values to be normalized [0.0, 1.0]. Bipolar [-1.0, +1.0] maps to normalized via: `normalized = (bipolar + 1.0f) / 2.0f`.
- The Amount parameters should be registered as `RangeParameter(min=-1.0, max=1.0)` so `toPlain()` and `toNormalized()` handle the conversion automatically.
- The BipolarSlider's `draw()` method renders:
  1. A track background (full width)
  2. A center tick mark at the 0.5 normalized position (always visible, FR-008)
  3. A fill that extends from center: left for values < 0.5 norm, right for values > 0.5 norm
  4. A numeric label showing the plain value with sign prefix ("+0.72", "-0.35", "0.00")
- Fine adjustment (Shift key, FR-009): follows XYMorphPad pattern with `kFineAdjustmentScale = 0.1f`

### Alternatives Considered
| Alternative | Why Rejected |
|---|---|
| CSlider with custom draw override | CSlider's internal value semantics fight bipolar rendering |
| CKnobBase subclass | Wrong control type (slider, not knob) |
| Two CSliders (one for neg, one for pos) | Overly complex, poor UX |

### Implementation Notes
- Constructor signature matches existing CControl pattern: `(const CRect& size, IControlListener* listener, int32_t tag)`
- Copy constructor for ViewCreator `newCopy()` support
- Mouse handling: store `dragStartValue_` and `dragStartPixelX_` on mouseDown, compute delta on mouseMove. Shift key detected via `buttons.getModifierState()`.
- `beginEdit()` / `endEdit()` called on mouseDown/mouseUp for host undo support (FR: SC-001).
- Escape key cancels drag and restores pre-drag value (following XYMorphPad pattern).

---

## R-003: ModRingIndicator Overlay Architecture

### Context
FR-020 through FR-030 require colored arc overlays on destination knobs showing modulation ranges. Need to determine how to overlay a custom view on existing ArcKnob instances without modifying ArcKnob.

### Decision
ModRingIndicator is a custom `VSTGUI::CView` placed as a sibling of the destination knob in `.uidesc`, positioned to exactly overlay the knob's bounds.

### Rationale
- ArcKnob already has a single-color modulation arc via `drawModulationArc()` with `modRange_` and `modColor_` fields. However, ModRingIndicator needs multi-source stacked arcs with different colors per source, which would require `modRange_` to become an array of structs.
- Modifying ArcKnob to support multi-source arcs would complicate its clean single-purpose design and affect all existing ArcKnob instances.
- The overlay approach (sibling CView with identical bounds) keeps ArcKnob unchanged. ModRingIndicator draws its arcs on top of the knob's guide ring but under the knob's value indicator by using appropriate radii.
- In `.uidesc`, the overlay is placed after the knob so it draws on top. The overlay must be `transparent="true"` and `mouse-enabled="true"` for click/hover detection.

### Arc Rendering Details
- Each arc uses `CGraphicsPath::addArc()` with the knob's bounding rect, following ArcKnob's `valueToAngleDeg()` convention (135 degrees = min, 315 degrees = max, clockwise).
- Arc start angle = `valueToAngleDeg(baseValue)`
- Arc end angle = `valueToAngleDeg(clamp(baseValue + amount, 0.0, 1.0))`
- For negative amounts: arc extends in the opposite direction
- Stroke width: 2-4px (FR-060), configurable via ViewCreator attribute
- Arc radius: slightly inside the knob's main arc radius to distinguish from the value arc

### Stacking Rules
- Up to `kMaxVisibleArcs = 4` individual arcs, each in its source color
- Most recently added route on top (drawn last)
- Beyond 4 arcs: merge remaining into a single composite gray arc
- Bypassed routes are excluded from rendering

### Alternatives Considered
| Alternative | Why Rejected |
|---|---|
| Extend ArcKnob with multi-source support | Complicates ArcKnob, affects all instances |
| Child view of ArcKnob | CKnobBase is not a CViewContainer, cannot host children |
| Timer-based refresh | Explicitly rejected by spec FR-030 |

---

## R-004: IDependent Pattern for Parameter Observation

### Context
ModRingIndicator, ModMatrixGrid, and ModHeatmap must react to parameter changes for redrawing. Need to establish the correct observation pattern.

### Decision
Use the standard VST3 IDependent pattern.

### Rationale
- The VST3 SDK's `IDependent` mechanism (via `FObject::addDependent()` / `update()`) is the standard way for UI components to observe parameter changes.
- When a parameter changes (via host automation, user interaction, or state load), `Parameter::changed()` is called, which notifies all registered dependents.
- The `update()` callback is delivered on the UI thread (deferred via `IRunLoop`), making it safe to call `setDirty()` to trigger a redraw.
- This is the same pattern used by ADSRDisplay for multi-parameter observation and by the ContainerVisibilityController pattern documented in THREAD-SAFETY.md.

### Integration Pattern
```
Controller registers parameters -> Components observe via IDependent
  Parameter changes -> update() called on UI thread -> setDirty() -> redraw
```

For ModRingIndicator:
1. Controller creates ModRingIndicator instances during editor creation
2. Controller adds ModRingIndicator as dependent on relevant modulation parameters
3. When modulation parameters change, `update()` is called
4. ModRingIndicator reads the current parameter values and redraws

### Alternatives Considered
| Alternative | Why Rejected |
|---|---|
| Direct parameter polling with timer | Wasteful, spec explicitly forbids timer |
| Custom observer interface | IDependent is the SDK standard |
| CControl value binding | ModRingIndicator is not bound to a single parameter |

---

## R-005: Controller-Mediated Cross-Component Communication

### Context
FR-027 requires that clicking a ModRingIndicator arc selects the corresponding route in ModMatrixGrid. FR-037 requires the same for ModHeatmap cells. These components must communicate without direct coupling.

### Decision
Controller mediates all cross-component communication via method calls.

### Rationale
- Spec FR-027 explicitly states: "ModRingIndicator MUST call a controller method (e.g., `selectModulationRoute(source, destination)`) that mediates the route selection."
- XYMorphPad already demonstrates the `setController()` pattern for holding a reference to the EditController.
- The communication flow:
  1. ModRingIndicator/ModHeatmap receives click
  2. Calls `controller_->selectModulationRoute(sourceIndex, destIndex)`
  3. Controller finds ModMatrixGrid and calls `grid->selectRoute(sourceIndex, destIndex)`
  4. ModMatrixGrid highlights the route and scrolls to it if needed

### Implementation
- Add `selectModulationRoute(int sourceIndex, int destIndex)` method to the Controller class
- Controller holds a weak pointer/reference to ModMatrixGrid (set during editor creation)
- ModRingIndicator and ModHeatmap hold a pointer to the Controller (set via `setController()`)

### Alternatives Considered
| Alternative | Why Rejected |
|---|---|
| Direct view-to-view communication | Violates VSTGUI architecture, creates tight coupling |
| Broadcast/notification system | Overengineered for 3 communicating components |
| Shared state object | Adds unnecessary indirection |

---

## R-006: Scrollable Route List

### Context
FR-061 requires the route list to be scrollable if routes exceed the visible height. Need to determine the VSTGUI approach.

### Decision
Use `VSTGUI::CScrollView` as a child container within ModMatrixGrid for the route list area.

### Rationale
- VSTGUI's `CScrollView` is the standard cross-platform scrollable container
- It handles: vertical scrollbar rendering, mouse wheel events, content size management, smooth scrolling
- Route rows are added as child views of the CScrollView
- CScrollView automatically shows/hides scrollbar based on content vs visible height

### Layout Within ModMatrixGrid
```
ModMatrixGrid (CViewContainer, 450 x ~250)
  +-- Tab Bar area (custom drawn, 450 x 24)
  +-- CScrollView (route list area, 430 x ~150)
  |   +-- Route Row 0 (430 x 28 or 56 expanded)
  |   +-- Route Row 1
  |   +-- ...
  |   +-- [+ Add Route] button area
  +-- ModHeatmap (300 x 80-100)
```

### Alternatives Considered
| Alternative | Why Rejected |
|---|---|
| Manual scroll offset tracking | Reinvents CScrollView functionality |
| No scrolling, fixed height | Rejected by spec FR-061 |
| CDataBrowser for route list | Too table-oriented; route rows have complex mixed controls |

---

## R-007: Tooltip Rendering

### Context
FR-028 and FR-038 require tooltips on hover over ModRingIndicator arcs and ModHeatmap cells.

### Decision
Use VSTGUI's built-in tooltip system via `CView::setTooltipText()` with dynamic updates on mouse move.

### Rationale
- VSTGUI has built-in tooltip support via `CTooltipSupport`, automatically installed on `CFrame`
- `CView::setTooltipText(UTF8StringPtr)` sets the tooltip for any view
- For position-dependent tooltips (different text based on which arc/cell is hovered), update the tooltip text in `onMouseMoved()` based on hit testing
- This is cross-platform (Principle VI) and requires no custom rendering

### Implementation
- ModRingIndicator: In `onMouseMoved()`, determine which arc (if any) the cursor is over. Call `setTooltipText("ENV 2 -> Filter Cutoff: +0.72")` with the appropriate text.
- ModHeatmap: In `onMouseMoved()`, determine which cell the cursor is over. Call `setTooltipText(...)` with full names and amount.
- When cursor is not over any arc/cell, set tooltip to empty string.

### Alternatives Considered
| Alternative | Why Rejected |
|---|---|
| Custom tooltip drawing (text near cursor) | Platform-inconsistent, manual positioning |
| No tooltips | Rejected by spec FR-028, FR-038 |

---

## R-008: Parameter ID Layout

### Context
FR-043 through FR-045 define 56 parameters (IDs 1300-1355) for global modulation routes.

### Decision
Follow the spec's exact layout. Add parameter IDs to the existing ParameterIDs enum in plugin_ids.h.

### Rationale
- The spec explicitly defines the ID mapping (FR-043, FR-044)
- Ruinae already has kNumParameters = 2000, with mod matrix base params at 1300-1323
- Detail params 1324-1355 fit within the existing 1300-1399 allocation
- No kNumParameters update needed

### Parameter Registration
- **Source parameters** (8): `StringListParameter` with 10 items (Global) or 7 items (Voice). But since these are VST parameters (Global tab only), they use the full 10-source list.
- **Destination parameters** (8): `StringListParameter` with 11 items (Global destinations including forwarded voice).
- **Amount parameters** (8): `RangeParameter(min=-1.0, max=1.0)` for bipolar range.
- **Curve parameters** (8): `StringListParameter` with 4 items (Linear, Exponential, Logarithmic, S-Curve).
- **Smooth parameters** (8): `RangeParameter(min=0.0, max=100.0)` in milliseconds.
- **Scale parameters** (8): `StringListParameter` with 5 items (x0.25, x0.5, x1, x2, x4).
- **Bypass parameters** (8): `Parameter` treated as boolean (0.0 or 1.0).

### kNumParameters Update
- Ruinae already has `kNumParameters = 2000` -- no update needed.
- Detail param IDs 1324-1355 fit within the existing allocation.
- VST3 state loading is parameter-ID-based, not index-based, so new parameters simply get their default values in old presets.

---

## R-009: Voice Route IMessage Protocol

### Context
FR-046 requires bidirectional IMessage communication for voice routes (not VST parameters).

### Decision
Define two IMessage types: `"VoiceModRouteUpdate"` (controller -> processor) and `"VoiceModRouteState"` (processor -> controller).

### Rationale
- Voice routes are per-voice and must not be exposed as host automation (FR-041)
- IMessage is the VST3-standard mechanism for non-parameter communication (Constitution Principle I)
- Bidirectional protocol ensures the UI always reflects the processor's authoritative state (spec clarification from 2026-02-10)

### Protocol Details

**Controller -> Processor: Individual route update**
```
Message ID: "VoiceModRouteUpdate"
Attributes:
  "slotIndex"   (int64):  0-15
  "source"      (int64):  ModSource enum value
  "destination" (int64):  ModDestination enum value
  "amount"      (float):  -1.0 to +1.0
  "curve"       (int64):  0-3
  "smoothMs"    (float):  0.0 to 100.0
  "scale"       (int64):  0-4
  "bypass"      (int64):  0 or 1
  "active"      (int64):  0 or 1
```

**Processor -> Controller: Full state sync**
```
Message ID: "VoiceModRouteState"
Attributes:
  "routeCount"  (int64):  number of active routes (0-16)
  "routeData"   (binary): 16 x sizeof(VoiceModRoute) bytes, packed struct array
```

### When Processor Sends State Back
- After receiving a `VoiceModRouteUpdate`
- After a preset load (`setComponentState()`)
- After state restore from host

### Alternatives Considered
| Alternative | Why Rejected |
|---|---|
| Per-field messages | Too chatty, risk of partial state |
| VST parameters | Explicitly forbidden by spec for voice routes |
| Shared memory | Violates VST3 architecture separation |
