# Research: Morph UI & Type-Specific Parameters

**Feature**: 006-morph-ui
**Date**: 2026-01-28
**Status**: Complete

---

## Research Tasks

### R1: VSTGUI UIViewSwitchContainer Usage

**Question**: How to configure UIViewSwitchContainer to switch between 26 type-specific templates based on distortion type parameter?

**Decision**: Use `template-switch-control` attribute bound to parameter control-tag.

**Rationale**:
- UIViewSwitchContainer is designed for exactly this use case
- The `template-switch-control` attribute binds directly to a parameter value
- When parameter changes, container automatically switches visible template
- No custom code needed for template switching logic

**Implementation**:
```xml
<view class="CViewSwitchContainer"
      origin="0, 0" size="300, 200"
      template-switch-control="band0-node0-type"
      animation-time="0">
    <template name="TypeParams_SoftClip" value="0"/>
    <template name="TypeParams_HardClip" value="1"/>
    <!-- ... 26 templates total ... -->
</view>
```

**Alternatives Considered**:
- Custom CViewContainer with manual switching - Rejected: Reinvents existing functionality
- Visibility controllers per template - Rejected: 26 controllers would be unwieldy

**Reference**: `vstgui4/uidescription/uiviewswitchcontainer.h`, `specs/Disrumpo/vstgui-implementation.md`

---

### R2: MorphPad Control Pattern

**Question**: What VSTGUI base class should MorphPad inherit from?

**Decision**: Inherit from `VSTGUI::CControl` with custom value handling.

**Rationale**:
- CControl provides value/getValue()/setValue() for VST3 parameter integration
- Unlike CXYPad which packs X/Y into single float, we need separate X/Y parameters
- CControl integrates naturally with control-tags system
- Event-based API (VSTGUI 4.11+) provides clean mouse handling

**Implementation Pattern**:
```cpp
class MorphPad : public VSTGUI::CControl {
public:
    void onMouseDownEvent(VSTGUI::MouseDownEvent& event) override {
        if (event.buttonState.isLeft()) {
            auto [x, y] = pixelToPosition(event.mousePosition);
            if (event.modifiers.has(ModifierKey::Alt)) {
                // Alt+drag: Node repositioning
                draggingNode_ = hitTestNode(event.mousePosition);
            } else {
                // Normal: Cursor movement
                setMorphPosition(x, y);
                draggingCursor_ = true;
            }
            event.consumed = true;
        }
    }
};
```

**Alternatives Considered**:
- CXYPad inheritance - Rejected: Packs X/Y into single value, doesn't fit our parameter model
- CView inheritance - Rejected: No built-in value handling for parameter binding

**Reference**: `vstgui4/lib/controls/cxypad.h`, `specs/Disrumpo/custom-controls.md`

---

### R3: Visibility Controller Pattern for Expand/Collapse

**Question**: How to implement per-band expand/collapse state?

**Decision**: Reuse existing VisibilityController pattern with IDependent.

**Rationale**:
- Pattern already implemented and tested in controller.cpp for band visibility
- IDependent mechanism provides thread-safe parameter observation
- Consistent with existing codebase patterns
- Controller::didOpen/willClose already manages lifecycle

**Implementation Pattern**:
```cpp
class ExpandedVisibilityController : public Steinberg::FObject,
                                      public Steinberg::IDependent {
public:
    ExpandedVisibilityController(
        Steinberg::Vst::EditControllerEx1* controller,
        Steinberg::Vst::ParamID expandedParamId,
        VSTGUI::CViewContainer* expandedContainer)
        : controller_(controller)
        , expandedParamId_(expandedParamId)
        , expandedContainer_(expandedContainer) {
        controller_->addDependency(expandedParamId_, this);
    }

    void PLUGIN_API update(FUnknown* changedUnknown, Steinberg::int32 message) override {
        if (message == Steinberg::Vst::IDependent::kChanged) {
            auto value = controller_->getParamNormalized(expandedParamId_);
            expandedContainer_->setVisible(value >= 0.5);
        }
    }
};
```

**Alternatives Considered**:
- Parameter listeners in custom control - Rejected: Less clean, duplicates pattern
- Animation-based expand/collapse - Rejected: Unnecessary complexity for initial implementation

**Reference**: `plugins/disrumpo/src/controller/controller.cpp` (existing VisibilityController)

---

### R4: Cross-Family Morph Visualization

**Question**: How to show both type panels when morphing between different families?

**Decision**: Side-by-side 50/50 split layout with opacity based on morph weight.

**Rationale**:
- Spec FR-038 explicitly requires "side-by-side equal split layout (fixed 50/50 width)"
- Opacity proportional to weight provides visual feedback of morph blend
- Below 10% weight, panel can collapse entirely (FR-039) to reduce clutter

**Implementation Approach**:
1. Detect when active nodes span different families (via DistortionFamily enum)
2. When cross-family: Show two UIViewSwitchContainers side-by-side
3. Set each container's alpha based on node weight
4. When same-family: Show single container at full opacity

**Alternatives Considered**:
- Show only dominant type - Simpler but loses cross-family visualization
- Overlay with blend - Harder to read individual parameters
- Tabbed interface - Doesn't show both simultaneously

**Reference**: Spec FR-038, `ui-mockups.md` Section 13

---

### R5: Morph Link Mode Implementation

**Question**: How should link modes map sweep frequency to morph position?

**Decision**: Implement 7 mapping functions as described in spec.

**Rationale**:
- Each mode serves distinct creative purpose
- Mapping is purely mathematical, easy to implement
- Controller computes mapped value and sends to MorphPad

**Mapping Functions**:

| Mode | Formula |
|------|---------|
| None | No change to manual position |
| Sweep Freq | `morphPos = (log(freq) - log(20)) / (log(20000) - log(20))` |
| Inverse | `morphPos = 1.0 - sweepFreqMapping` |
| EaseIn | `morphPos = pow(sweepFreqMapping, 2.0)` |
| EaseOut | `morphPos = 1.0 - pow(1.0 - sweepFreqMapping, 2.0)` |
| Hold-Rise | `morphPos = sweepFreqMapping < 0.5 ? 0.0 : (sweepFreqMapping - 0.5) * 2.0` |
| Stepped | `morphPos = floor(sweepFreqMapping * 4) / 4.0` |

**Reference**: Spec FR-034a through FR-034e

---

### R6: VSTGUI Event API (4.11+)

**Question**: What is the correct event handling pattern for VSTGUI 4.11+?

**Decision**: Use event-based API with MouseDownEvent, MouseMoveEvent, etc.

**Key Properties**:
- `event.mousePosition` - CPoint with mouse coordinates
- `event.buttonState.isLeft()` - Left button check
- `event.modifiers.has(ModifierKey::Shift)` - Modifier key check
- `event.modifiers.has(ModifierKey::Alt)` - Alt key check
- `event.consumed = true` - Mark event as handled
- `event.clickCount` - For double-click detection (== 2)

**Example Pattern**:
```cpp
void MorphPad::onMouseDownEvent(MouseDownEvent& event) {
    if (!event.buttonState.isLeft()) return;

    auto [x, y] = pixelToPosition(event.mousePosition);

    if (event.clickCount == 2) {
        // Double-click: Reset to center
        setMorphPosition(0.5f, 0.5f);
        event.consumed = true;
        return;
    }

    if (event.modifiers.has(ModifierKey::Shift)) {
        // Fine adjustment mode
        fineAdjustment_ = true;
    }

    if (event.modifiers.has(ModifierKey::Alt)) {
        // Node repositioning
        int nodeIndex = hitTestNode(event.mousePosition);
        if (nodeIndex >= 0) {
            draggingNode_ = nodeIndex;
            event.consumed = true;
            return;
        }
    }

    // Normal cursor drag
    setMorphPosition(x, y);
    draggingCursor_ = true;
    event.consumed = true;
}
```

**Reference**: `vstgui4/lib/events.h`, `specs/Disrumpo/custom-controls.md`

---

### R7: Node Category Colors

**Question**: What colors should be used for each distortion family?

**Decision**: Use colors defined in custom-controls.md Section 2.3.1.

**Color Mapping**:
```cpp
const std::map<DistortionFamily, CColor> kCategoryColors = {
    {DistortionFamily::Saturation,   CColor{0xFF, 0x6B, 0x35, 0xFF}},  // #FF6B35 Orange
    {DistortionFamily::Wavefold,     CColor{0x4E, 0xCD, 0xC4, 0xFF}},  // #4ECDC4 Teal
    {DistortionFamily::Digital,      CColor{0x95, 0xE8, 0x6B, 0xFF}},  // #95E86B Green
    {DistortionFamily::Rectify,      CColor{0xC7, 0x92, 0xEA, 0xFF}},  // #C792EA Purple
    {DistortionFamily::Dynamic,      CColor{0xFF, 0xCB, 0x6B, 0xFF}},  // #FFCB6B Yellow
    {DistortionFamily::Hybrid,       CColor{0xFF, 0x53, 0x70, 0xFF}},  // #FF5370 Red
    {DistortionFamily::Experimental, CColor{0x89, 0xDD, 0xFF, 0xFF}},  // #89DDFF Light Blue
};
```

**Family Assignments**:
- Saturation: D01-D06 (Soft Clip, Hard Clip, Tube, Tape, Fuzz, Asym Fuzz)
- Wavefold: D07-D09 (Sine Fold, Triangle Fold, Serge Fold)
- Rectify: D10-D11 (Full Rectify, Half Rectify)
- Digital: D12-D14, D18-D19 (Bitcrush, Sample Reduce, Quantize, Aliasing, Bitwise)
- Dynamic: D15 (Temporal)
- Hybrid: D16-D17, D26 (Ring Sat, Feedback, Allpass Res)
- Experimental: D20-D25 (Chaos, Formant, Granular, Spectral, Fractal, Stochastic)

**Reference**: `specs/Disrumpo/custom-controls.md` Section 2.3.1

---

### R8: Existing MorphEngine API

**Question**: What MorphEngine methods are needed for UI weight visualization?

**Decision**: Use getWeights(), getSmoothedX(), getSmoothedY() for visualization.

**Available Methods** (from morph_engine.h):
```cpp
// Get current computed weights for all nodes
[[nodiscard]] const std::array<float, kMaxMorphNodes>& getWeights() const noexcept;

// Get current smoothed morph X position
[[nodiscard]] float getSmoothedX() const noexcept;

// Get current smoothed morph Y position
[[nodiscard]] float getSmoothedY() const noexcept;
```

**Note**: These are for DSP-side access. UI will read parameter values via Controller, not directly from MorphEngine. Weight calculation for UI visualization should mirror the engine's algorithm:

```cpp
// Inverse distance weighting (p=2)
float calculateWeight(float cursorX, float cursorY, float nodeX, float nodeY) {
    float dx = cursorX - nodeX;
    float dy = cursorY - nodeY;
    float distSq = dx * dx + dy * dy;
    if (distSq < 0.0001f) return 1.0f;  // Very close to node
    return 1.0f / distSq;  // Inverse square distance
}
// Then normalize weights to sum to 1.0
```

**Reference**: `plugins/disrumpo/src/dsp/morph_engine.h`

---

## Summary

All research questions have been resolved:

| Topic | Resolution |
|-------|------------|
| UIViewSwitchContainer | Use template-switch-control attribute |
| MorphPad base class | Inherit from CControl |
| Expand/collapse | Reuse VisibilityController pattern |
| Cross-family visualization | 50/50 split with opacity |
| Link modes | 7 mapping functions documented |
| Event handling | VSTGUI 4.11+ event-based API |
| Node colors | Category color map from custom-controls.md |
| MorphEngine API | Weight calculation algorithm documented |

Ready for Phase 1 implementation.
