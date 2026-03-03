# Quickstart: Disrumpo Plugin Skeleton

**Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)
**Date**: 2026-01-27

## Prerequisites

- Windows 11 with Visual Studio 2022
- CMake 3.20+ (use full path: `"C:\Program Files\CMake\bin\cmake.exe"`)
- Git (for submodule access to VST3 SDK)
- pluginval (in tools/ directory)

---

## Build Instructions

### 1. Configure

```bash
# From repository root
"C:\Program Files\CMake\bin\cmake.exe" --preset windows-x64-release
```

**Expected output:**
```
-- [DISRUMPO] Version: 0.1.0 (0.1.0)
-- Configuring done
-- Generating done
```

### 2. Build

```bash
# Build Disrumpo plugin
"C:\Program Files\CMake\bin\cmake.exe" --build build/windows-x64-release --config Release --target Disrumpo
```

**Expected output:**
```
[  X%] Building CXX object plugins/disrumpo/...
[100%] Linking CXX shared module VST3/Release/Disrumpo.vst3/Contents/x86_64-win/Disrumpo.vst3
```

### 3. Locate Plugin

The built plugin is at:
```
build/windows-x64-release/VST3/Release/Disrumpo.vst3/
```

**Note:** The post-build copy to `C:\Program Files\Common Files\VST3\` may fail due to permissions. This is fine - compilation succeeded.

---

## Validation

### Pluginval Level 1

```bash
tools/pluginval.exe --strictness-level 1 --validate "build/windows-x64-release/VST3/Release/Disrumpo.vst3"
```

**Expected result:**
```
All instruments passed!
```

### Manual DAW Testing

1. **Reaper**
   - Options > Preferences > Plug-ins > VST
   - Add path to build directory
   - Rescan, search for "Disrumpo"
   - Insert on track, verify it loads

2. **Ableton Live** (if available)
   - Options > Preferences > Plug-ins > VST3
   - Add custom folder path
   - Rescan, find "Disrumpo"
   - Insert on track

---

## Project Structure

```
plugins/disrumpo/
+-- CMakeLists.txt              # Build configuration
+-- version.json                # Version: 0.1.0
+-- src/
|   +-- entry.cpp               # Plugin factory
|   +-- plugin_ids.h            # FUIDs, parameter IDs
|   +-- version.h.in            # Template
|   +-- version.h               # Generated
|   +-- processor/
|   |   +-- processor.h
|   |   +-- processor.cpp       # Audio passthrough
|   +-- controller/
|       +-- controller.h
|       +-- controller.cpp      # No UI
+-- resources/
    +-- win32resource.rc.in     # Windows resources
```

---

## Key Files

### plugin_ids.h

Contains unique FUIDs and parameter ID definitions using the bit-encoded scheme.
**Note:** FUIDs shown below are placeholders - actual values are generated at implementation time using `uuidgen` or UUID v4 generator. The authoritative source is the implemented `plugin_ids.h` file.

```cpp
namespace Disrumpo {

// FUIDs: Generated unique identifiers (NOT same as Iterum)
// See plan.md for generation method
static const Steinberg::FUID kProcessorUID(0x????????, 0x????????, 0x????????, 0x????????);
static const Steinberg::FUID kControllerUID(0x????????, 0x????????, 0x????????, 0x????????);

enum ParameterIDs : Steinberg::Vst::ParamID {
    kInputGainId  = 0x0F00,  // Global input gain
    kOutputGainId = 0x0F01,  // Global output gain
    kGlobalMixId  = 0x0F02,  // Global dry/wet mix
};

// VST3 standard subcategory (appears in DAW plugin list)
constexpr const char* kSubCategories = Steinberg::Vst::PlugType::kFxDistortion;

} // namespace Disrumpo
```

### processor.cpp (Audio Passthrough)

```cpp
tresult PLUGIN_API Processor::process(ProcessData& data) {
    // Parameter changes
    if (data.inputParameterChanges)
        processParameterChanges(data.inputParameterChanges);

    // Audio passthrough
    if (data.numInputs == 0 || data.numOutputs == 0)
        return kResultOk;

    float** in = data.inputs[0].channelBuffers32;
    float** out = data.outputs[0].channelBuffers32;
    int32 numSamples = data.numSamples;

    // Copy input to output (bit-transparent passthrough)
    for (int32 ch = 0; ch < 2; ++ch) {
        if (in[ch] != out[ch]) {
            memcpy(out[ch], in[ch], numSamples * sizeof(float));
        }
    }

    return kResultOk;
}
```

---

## Debugging

### Build Errors

```bash
# If CMake configuration fails:
"C:\Program Files\CMake\bin\cmake.exe" --preset windows-x64-release --fresh

# If plugin doesn't appear in DAW:
# 1. Check build output for errors
# 2. Verify .vst3 folder exists
# 3. Rescan plugins in DAW
```

### Runtime Issues

```bash
# Debug build for better diagnostics
"C:\Program Files\CMake\bin\cmake.exe" --preset windows-x64-debug
"C:\Program Files\CMake\bin\cmake.exe" --build build/windows-x64-debug --config Debug --target Disrumpo
```

### Pluginval Failures

```bash
# More verbose output
tools/pluginval.exe --strictness-level 1 --verbose --validate "path/to/Disrumpo.vst3"
```

---

## Verification Checklist

### Build Verification
- [ ] CMake configures without errors (shows "DISRUMPO Version: 0.1.0")
- [ ] Plugin builds without warnings (zero warnings required)
- [ ] version.h generated with correct DISRUMPO_VERSION macro

### DAW Verification (Reaper)
- [ ] Plugin appears in Reaper plugin list under "Distortion" category
- [ ] Plugin loads without crash within 2 seconds (SC-002)
- [ ] Audio passes through unchanged (bit-transparent)
- [ ] All 3 parameters appear in automation lane dropdown
- [ ] Project save/load preserves parameter values exactly (SC-006)

### DAW Verification (Ableton Live - if available)
- [ ] Plugin appears in Live browser under "Audio Effects > Distortion"
- [ ] Plugin loads without crash
- [ ] Parameter automation is visible and functional

### Validation
- [ ] pluginval level 1 passes with zero failures

---

## Next Steps

After skeleton is complete:

1. **002-band-management** - Add crossover network
2. **003-distortion-integration** - Add distortion adapter
3. **004-vstgui-infrastructure** - Add UI (Week 4-5)

---

## Troubleshooting

### "CMake not found"

Use the full path:
```bash
"C:\Program Files\CMake\bin\cmake.exe" --version
```

### "Plugin not found in DAW"

1. Ensure build succeeded (check for .vst3 folder)
2. Add build output path to DAW's VST3 search paths
3. Rescan plugins
4. Check plugin is categorized under "Distortion"

### "Audio not passing through"

1. Verify input bus is connected
2. Check channel count (stereo only)
3. Build Debug config and check process() is called

### "State not persisting"

1. Check getState/setState implement IBStreamer correctly
2. Verify endianness matches (kLittleEndian)
3. Test with pluginval's state restore tests
