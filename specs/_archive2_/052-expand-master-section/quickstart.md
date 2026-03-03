# Quickstart: 052-expand-master-section

**Feature**: Expand Master Section into Voice & Output Panel
**Branch**: `052-expand-master-section`

---

## What This Feature Does

Restructures the Ruinae synth plugin's "MASTER" panel (120x160px) into a "Voice & Output" panel with a more compact layout. Adds a gear icon placeholder (for future settings drawer) and Width/Spread knob placeholders (for future stereo controls). No new parameters are introduced; all existing parameters remain fully functional.

---

## Files Changed

### C++ Changes (1 file)

| File | Change |
|------|--------|
| `plugins/shared/src/ui/toggle_button.h` | Add `kGear` to `IconStyle` enum, implement `drawGearIcon()`, update ViewCreator registration |

### UIDESC Changes (1 file)

| File | Change |
|------|--------|
| `plugins/ruinae/resources/editor.uidesc` | Restructure Master section: rename, reposition controls, add gear icon + Width/Spread placeholders |

### Test Changes (1 file, new or extended)

| File | Change |
|------|--------|
| `plugins/shared/tests/test_toggle_button.cpp` | New test file: gear icon style creation and value conversion |

### Build Changes (1 file)

| File | Change |
|------|--------|
| `plugins/shared/tests/CMakeLists.txt` | Add `test_toggle_button.cpp` to shared_tests |

---

## Build & Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure
"$CMAKE" --preset windows-x64-release

# Build shared_tests (for gear icon unit tests)
"$CMAKE" --build build/windows-x64-release --config Release --target shared_tests

# Run shared tests
build/windows-x64-release/plugins/shared/tests/Release/shared_tests.exe

# Build Ruinae plugin
"$CMAKE" --build build/windows-x64-release --config Release --target Ruinae

# Run pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
```

---

## Key Design Decisions

1. **Gear icon is inert** (no control-tag). It does nothing when clicked in this phase. Phase 5 will add a control-tag to make it toggle a settings drawer.

2. **Width/Spread knobs have no control-tag**. They render as normal ArcKnobs but produce no audio effect. Phase 1.3 will define parameter IDs, register them, and bind them via control-tags.

3. **No parameter changes whatsoever**. Parameter IDs 0 (MasterGain), 2 (Polyphony), and 3 (SoftLimit) are unchanged. Presets are fully compatible.

4. **Pixel positions are guidelines, not absolutes**. Hard constraints: 120x160px boundary, 4px minimum spacing between controls.

---

## Static Analysis (Clang-Tidy)

```bash
# Generate compile_commands.json (from VS Developer PowerShell)
"$CMAKE" --preset windows-ninja

# Run clang-tidy on shared code
./tools/run-clang-tidy.ps1 -Target shared -BuildDir build/windows-ninja

# Run clang-tidy on ruinae code
./tools/run-clang-tidy.ps1 -Target ruinae -BuildDir build/windows-ninja
```

---

## Verification

- Open Ruinae in a DAW after building
- Confirm panel title reads "Voice & Output"
- Confirm Output knob, Polyphony dropdown, and Soft Limit toggle are functional
- Confirm gear icon is visible and does nothing when clicked
- Confirm Width and Spread knobs are visible and do nothing audible
- Run pluginval at strictness 5
- Run clang-tidy and fix any errors (see Static Analysis section above)
