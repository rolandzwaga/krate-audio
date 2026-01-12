# Quick Start: Running Tests

## Build and Run All Tests

```bash
# Configure (first time or after CMakeLists.txt changes)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# Build everything (including tests and validator)
cmake --build build --config Debug

# Run unit tests
build\bin\Debug\dsp_tests.exe

# Run approval tests (golden master / regression tests)
build\bin\Debug\approval_tests.exe

# Run all tests via CTest
ctest --test-dir build -C Debug --output-on-failure
```

---

## Windows/MSVC Build Commands (Git Bash)

### Build Directory Location

The build directory is `build/` (not `build/windows-x64-debug/`).

### MSBuild from Git Bash

Use dash-prefix for parameters, not forward slash:

```bash
# CORRECT - Build specific test target with MSBuild
"/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" \
    "F:\\projects\\iterum\\build\\tests\\dsp_tests.vcxproj" \
    -p:Configuration=Debug -v:m

# WRONG - Forward slashes get misinterpreted in Git Bash
# ... /p:Configuration=Debug /v:m   <- DON'T DO THIS

# Force rebuild (clean + build)
"/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" \
    "F:\\projects\\iterum\\build\\tests\\dsp_tests.vcxproj" \
    -p:Configuration=Debug -v:m -t:Rebuild
```

### Test Executable Location

```bash
# Run all DSP tests
"F:/projects/iterum/build/bin/Debug/dsp_tests.exe"

# Run tests by tag
"F:/projects/iterum/build/bin/Debug/dsp_tests.exe" "[digital-delay]"
"F:/projects/iterum/build/bin/Debug/dsp_tests.exe" "[layer3]"

# List available tests
"F:/projects/iterum/build/bin/Debug/dsp_tests.exe" --list-tests
```

### Project File Locations

| Target | vcxproj Location |
|--------|------------------|
| dsp_tests | `build/tests/dsp_tests.vcxproj` |
| vst_tests | `build/tests/vst_tests.vcxproj` |
| approval_tests | `build/tests/approval_tests.vcxproj` |
| Iterum (plugin) | `build/Iterum.vcxproj` |

---

## VST3 Validation

```bash
# Full validation (all tests) - via CMake target
cmake --build build --config Debug --target validate_plugin

# Quick validation (basic conformity only)
cmake --build build --config Debug --target validate_plugin_quick

# Or run validator directly
build\bin\Debug\validator.exe "build\VST3\Debug\Iterum.vst3"

# Show all validator options
build\bin\Debug\validator.exe --help

# Run extensive tests (may take a long time)
build\bin\Debug\validator.exe -e "build\VST3\Debug\Iterum.vst3"
```

---

## Run Specific Tests

```bash
# Run only tests tagged [filter]
build\bin\Debug\dsp_tests.exe "[filter]"

# Run only regression tests
build\bin\Debug\approval_tests.exe "[regression]"

# Run only gain-related tests
build\bin\Debug\dsp_tests.exe "[gain]"

# Exclude slow tests
build\bin\Debug\dsp_tests.exe "~[slow]"

# List all available tests
build\bin\Debug\dsp_tests.exe --list-tests
```

---

## Build Verification Workflow

### MANDATORY: Verify Build Before Running Tests

When writing or modifying test code, follow this exact sequence:

1. **Write your test code** (following test-first development)

2. **Build the test target**:
   ```bash
   cmake --build build --config Debug --target dsp_tests
   ```

3. **Check for compilation errors**:
   - Look for `error` or `error C` in build output
   - If ANY errors appear, **STOP** and fix them

4. **Verify clean build**: Build must succeed with zero errors

5. **Check test binary timestamp**:
   ```bash
   ls -la build/bin/Debug/dsp_tests.exe
   ```
   Ensure the timestamp is recent (just updated)

6. **ONLY THEN run the tests**

---

## Troubleshooting: Tests Don't Appear

If tests don't show up in test lists or fail in unexpected ways, follow this checklist **IN ORDER**:

### 1. Verify Build Success (FIRST)

```bash
# Rebuild and check for errors
cmake --build build --config Debug --target dsp_tests 2>&1 | grep -i "error"
```

- If you see ANY compilation errors, fix them before proceeding
- Common errors: missing includes, type mismatches, syntax errors

### 2. Verify Test Binary Exists and Is Current

```bash
# Check if binary exists
ls -la build/bin/Debug/dsp_tests.exe

# Check timestamp - should be within last few minutes
stat build/bin/Debug/dsp_tests.exe
```

### 3. Verify Test Registration

- Check that test file is listed in `tests/CMakeLists.txt`
- For `dsp_tests`: File must be in `add_executable(dsp_tests ...)` block
- For `vst_tests`: File must be in `add_executable(vst_tests ...)` block

### 4. Force Clean Rebuild

```bash
# Delete object files and rebuild
cmake --build build --config Debug --target dsp_tests --clean-first
```

### 5. Check Catch2 Test Registration

```bash
# List all registered tests
build/bin/Debug/dsp_tests.exe --list-tests
```

### 6. LAST RESORT: CMake Reconfigure

```bash
# Only if steps 1-5 didn't reveal the issue
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug --target dsp_tests
```

---

## Real Example: The int32 Incident

**What Happened:**
1. Wrote `int32 currentMode = ...` in controller.cpp
2. Immediately tried to run tests
3. Tests didn't appear in test list
4. Blamed "CMake cache issues"
5. Spent 10+ attempts trying different test invocations
6. **Never checked build output**
7. Build output clearly showed: `error C2065: 'int32': undeclared identifier`
8. Should have been `Steinberg::int32`

**What Should Have Happened:**
1. Wrote code
2. **Built immediately**: `cmake --build build --config Debug --target vst_tests`
3. **Saw compilation error in output**
4. Fixed error (changed to `Steinberg::int32`)
5. Rebuilt successfully
6. Ran tests
7. Problem solved in 2 minutes instead of 20

---

## CMake Presets (Alternative)

```bash
cmake --preset windows-x64-debug        # Configure
cmake --build --preset windows-x64-debug # Build
ctest --preset windows-x64-debug         # Test
```

---

## AddressSanitizer (ASan)

Use ASan to detect memory errors (use-after-free, buffer overflows, etc.):

```bash
# Configure with ASan enabled
cmake -S . -B build-asan -G "Visual Studio 17 2022" -A x64 -DENABLE_ASAN=ON

# Build (use Debug for better stack traces)
cmake --build build-asan --config Debug

# Run tests - ASan will abort and report if memory errors occur
ctest --test-dir build-asan -C Debug --output-on-failure
```

**When to use ASan:**
- Investigating crashes that don't have clear stack traces
- After fixing editor lifecycle bugs (to verify no use-after-free)
- Before releases to catch latent memory issues

**Note:** ASan increases memory usage and slows execution. Use a separate build directory.
