---
name: vst-guide
description: VST3 SDK and VSTGUI implementation patterns. Use when working on plugin UI, parameter handling, VSTGUI components, editor lifecycle, thread safety, controller code, reusable view templates, sub-controllers, or cross-platform compatibility. Covers parameter types, IDependent pattern, visibility controllers, control selection, template instantiation, tag remapping, and common pitfalls.
allowed-tools: Read, Grep, Glob, Bash
---

# VST3 SDK and VSTGUI Implementation Guide

This skill captures hard-won insights about VST3 SDK and VSTGUI that are not obvious from official documentation. These findings prevent repeating debugging sessions that waste hours.

## Quick Reference

- **Parameter types & helpers**: See [PARAMETERS.md](PARAMETERS.md)
- **Thread safety & IDependent**: See [THREAD-SAFETY.md](THREAD-SAFETY.md)
- **VSTGUI components & reusable templates**: See [UI-COMPONENTS.md](UI-COMPONENTS.md)
- **Control selection guide**: See [CONTROLS-REFERENCE.md](CONTROLS-REFERENCE.md)
- **Cross-platform patterns**: See [CROSS-PLATFORM.md](CROSS-PLATFORM.md)
- **Common pitfalls & incidents**: See [PITFALLS.md](PITFALLS.md)

---

## Framework Philosophy

When something doesn't work with VSTGUI or VST3 SDK:

1. **The framework is correct** - It's used in thousands of commercial plugins
2. **You are using it wrong** - The bug is in your usage, not the framework
3. **Read the source** - The SDK and VSTGUI are open source; read them
4. **Use SDK functions** - Don't reinvent conversions the SDK already provides
5. **Trust automatic bindings** - template-switch-control, menu population, etc.

---

## Key Principles

### Parameter Types Matter

The base `Parameter` class does NOT scale normalized values in `toPlain()` - it just returns the input unchanged. Use:

| Use Case | Parameter Type |
|----------|---------------|
| Continuous knob (0-1 range) | `Parameter` |
| Continuous range (e.g., 20Hz-20kHz) | `RangeParameter` |
| Discrete list (e.g., modes, types) | `StringListParameter` |

See [PARAMETERS.md](PARAMETERS.md) for details.

### Thread Safety is Non-Negotiable

`setParamNormalized()` can be called from **ANY thread** (automation, state loading, etc.). VSTGUI controls MUST only be manipulated on the UI thread.

**NEVER** do this:
```cpp
// BROKEN - setParamNormalized can be called from any thread!
tresult Controller::setParamNormalized(ParamID id, ParamValue value) {
    if (id == kTimeModeId) {
        delayTimeControl_->setVisible(value < 0.5f);  // CRASH!
    }
}
```

**ALWAYS** use the `IDependent` pattern with deferred updates. See [THREAD-SAFETY.md](THREAD-SAFETY.md).

### Feedback Loop Prevention (Built-in)

VST3Editor already prevents feedback loops in `valueChanged()`:

```cpp
void VST3Editor::valueChanged(CControl* pControl) {
    if (!pControl->isEditing())  // Only propagates USER edits
        return;
    // ... send normalized value to host
}
```

- `isEditing() == true`: User is actively manipulating the control
- `isEditing() == false`: Host is updating programmatically

**You don't need custom feedback prevention code.** VSTGUI handles this automatically.

---

## Source Code Locations

| Component | Location |
|-----------|----------|
| Parameter classes | `extern/vst3sdk/public.sdk/source/vst/vstparameters.cpp` |
| VST3Editor | `extern/vst3sdk/vstgui4/vstgui/plugin-bindings/vst3editor.cpp` |
| UIViewSwitchContainer | `extern/vst3sdk/vstgui4/vstgui/uidescription/uiviewswitchcontainer.cpp` |
| UIDescription (templates) | `extern/vst3sdk/vstgui4/vstgui/uidescription/uidescription.h` |
| IController (sub-controllers) | `extern/vst3sdk/vstgui4/vstgui/uidescription/icontroller.h` |
| DelegationController | `extern/vst3sdk/vstgui4/vstgui/uidescription/delegationcontroller.h` |
| COptionMenu | `extern/vst3sdk/vstgui4/vstgui/lib/controls/coptionmenu.cpp` |
| CViewContainer | `extern/vst3sdk/vstgui4/vstgui/lib/cviewcontainer.cpp` |
| CView | `extern/vst3sdk/vstgui4/vstgui/lib/cview.cpp` |

---

## Debugging Checklist

When VSTGUI/VST3 features don't work:

1. [ ] Add logging to trace actual values at each step
2. [ ] Check which parameter type you're using (`Parameter` vs `StringListParameter` vs `RangeParameter`)
3. [ ] Verify `toPlain()` returns expected values
4. [ ] Read the VSTGUI source in `extern/vst3sdk/vstgui4/vstgui/`
5. [ ] Read the VST3 SDK source in `extern/vst3sdk/public.sdk/source/vst/`
6. [ ] Check if automatic bindings are configured correctly in editor.uidesc
7. [ ] Verify control-tag names match parameter registration

---

## Additional Resources

For detailed information, see the supporting files:

- [PARAMETERS.md](PARAMETERS.md) - Parameter types, toPlain(), dropdown helpers, StringListParameter
- [THREAD-SAFETY.md](THREAD-SAFETY.md) - IDependent pattern, visibility controllers, editor lifecycle
- [UI-COMPONENTS.md](UI-COMPONENTS.md) - UIViewSwitchContainer, COptionMenu, CViewContainer visibility
- [CONTROLS-REFERENCE.md](CONTROLS-REFERENCE.md) - Control selection decision matrix, XML examples
- [CROSS-PLATFORM.md](CROSS-PLATFORM.md) - Custom views, file dialogs, paths, fonts
- [PITFALLS.md](PITFALLS.md) - Common mistakes, incident log, debugging lessons
