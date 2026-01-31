# Quickstart: Progressive Disclosure & Accessibility

**Feature Branch**: `012-progressive-disclosure`
**Date**: 2026-01-31

## Overview

This feature adds progressive disclosure UI patterns and accessibility support to the Disrumpo multiband distortion plugin. It builds heavily on existing infrastructure (expand/collapse parameters, visibility controllers, IMidiMapping) and adds: smooth panel animation, generalized MIDI CC mapping, keyboard shortcuts, window resize, high contrast mode, and reduced motion support.

## Architecture

```
plugins/shared/src/
  platform/
    accessibility_helper.h      # NEW: Cross-platform OS accessibility detection
    accessibility_helper.cpp    # NEW: Win32/macOS/Linux implementations
  midi/
    midi_cc_manager.h           # NEW: Generalized MIDI CC mapping manager
    midi_cc_manager.cpp         # NEW: Mapping, MIDI Learn, 14-bit support

plugins/disrumpo/src/
  plugin_ids.h                  # MODIFY: Add kGlobalModPanelVisible, kGlobalMidiLearnActive, etc.
  controller/
    controller.h                # MODIFY: Add new controller members
    controller.cpp              # MODIFY: Animation, keyboard hook, context menu, resize
    keyboard_shortcut_handler.h # NEW: IKeyboardHook for Tab/Space/Arrow shortcuts
    keyboard_shortcut_handler.cpp
  resources/
    editor.uidesc               # MODIFY: Update minSize/maxSize, add modulation panel container
```

## Implementation Order

### Phase 1: Expand/Collapse Animation (US1, US6)
**Files**: controller.cpp (AnimatedExpandController replaces ContainerVisibilityController for expand)
**Dependencies**: None (extends existing expand/collapse mechanism)
**Tests**: Animation timing, mid-animation reversal, reduced motion bypass

### Phase 2: Window Resize (US2)
**Files**: controller.cpp (createView), editor.uidesc
**Dependencies**: None
**Tests**: Min/max bounds, aspect ratio constraint, size persistence

### Phase 3: Modulation Panel Toggle (US3)
**Files**: plugin_ids.h, controller.cpp (registerGlobalParams, didOpen)
**Dependencies**: None
**Tests**: Toggle visibility, active routings persist when hidden

### Phase 4: Keyboard Shortcuts (US4)
**Files**: keyboard_shortcut_handler.h/.cpp, controller.cpp (didOpen/willClose)
**Dependencies**: None
**Tests**: Tab/Shift+Tab focus cycling, Space bypass toggle, Arrow key adjustment

### Phase 5: Accessibility Detection (US7)
**Files**: accessibility_helper.h/.cpp (shared)
**Dependencies**: None
**Tests**: Mock-based detection tests per platform, integration with reduced motion

### Phase 6: High Contrast Mode (US7)
**Files**: controller.cpp, custom views (spectrum_display, morph_pad, etc.)
**Dependencies**: Phase 5 (accessibility detection)
**Tests**: Color contrast ratios, border width changes

### Phase 7: MIDI CC Mapping (US5, US8)
**Files**: midi_cc_manager.h/.cpp (shared), controller.cpp, plugin_ids.h
**Dependencies**: None
**Tests**: Mapping creation, MIDI Learn workflow, 14-bit CC, hybrid persistence

### Phase 8: Integration & Polish
**Dependencies**: All phases
**Tests**: pluginval, full preset round-trip, cross-platform build

## Key Patterns

### Animated Expand (extends existing pattern)
```cpp
// Existing: instant show/hide via ContainerVisibilityController
container->setVisible(shouldExpand);

// New: animated height transition
if (shouldExpand) {
    container->setVisible(true);
    auto* animator = frame->getAnimator();
    CRect targetRect = container->getViewSize();
    targetRect.setHeight(expandedHeight_);
    animator->addAnimation(container, "expand",
        new Animation::ViewSizeAnimation(targetRect, true),
        new Animation::CubicBezierTimingFunction::easyInOut(250));
} else {
    CRect targetRect = container->getViewSize();
    targetRect.setHeight(0);
    frame->getAnimator()->addAnimation(container, "expand",
        new Animation::ViewSizeAnimation(targetRect, true),
        new Animation::CubicBezierTimingFunction::easyInOut(250),
        [container](auto*) { container->setVisible(false); });
}
```

### Keyboard Hook Registration (in didOpen/willClose)
```cpp
void Controller::didOpen(VST3Editor* editor) {
    // ... existing code ...
    auto* frame = editor->getFrame();
    keyboardHandler_ = std::make_unique<KeyboardShortcutHandler>(this, frame, bandCount);
    frame->registerKeyboardHook(keyboardHandler_.get());
    frame->setFocusDrawingEnabled(true);
    frame->setFocusColor(CColor(0x3A, 0x96, 0xDD));  // Accent blue
    frame->setFocusWidth(2.0);
}

void Controller::willClose(VST3Editor* editor) {
    // ... existing deactivation code ...
    if (keyboardHandler_) {
        editor->getFrame()->unregisterKeyboardHook(keyboardHandler_.get());
        keyboardHandler_.reset();
    }
}
```

### MIDI Learn Context Menu
```cpp
COptionMenu* Controller::createContextMenu(const CPoint& pos, VST3Editor* editor) {
    ParamID paramId;
    if (!findParameter(pos, paramId, editor)) return nullptr;

    auto* menu = new COptionMenu();
    menu->addEntry("MIDI Learn", [this, paramId]() {
        midiCCManager_->startLearn(paramId);
    });

    uint8_t cc;
    if (midiCCManager_->getCCForParam(paramId, cc)) {
        menu->addEntry("Clear MIDI Learn", [this, paramId]() {
            midiCCManager_->removeMappingsForParam(paramId);
        });
        // ... "Save with Preset" checkbox ...
    }
    return menu;
}
```

## Build and Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure and build
"$CMAKE" --preset windows-x64-release
"$CMAKE" --build build/windows-x64-release --config Release

# Run Disrumpo tests
"$CMAKE" --build build/windows-x64-release --config Release --target disrumpo_tests
build/windows-x64-release/plugins/disrumpo/tests/Release/disrumpo_tests.exe

# Run pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Disrumpo.vst3"

# Run clang-tidy
./tools/run-clang-tidy.ps1 -Target disrumpo -BuildDir build/windows-ninja
```

## Constitution Compliance Notes

- **Principle V (VSTGUI)**: All UI via UIDescription and VSTGUI APIs. Animation uses built-in VSTGUI::Animation framework.
- **Principle VI (Cross-Platform)**: Platform-specific code ONLY in accessibility_helper.cpp behind #ifdef guards. All other code is cross-platform.
- **Principle VIII (Testing)**: Test-first for all phases. Focus on logic tests (thresholds, state transitions, MIDI mapping) that do not require VSTGUI runtime.
- **Principle XIV (ODR)**: All planned classes verified unique via grep search.
