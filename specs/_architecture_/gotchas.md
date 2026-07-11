# Cross-Cutting Gotchas

Failure modes that span plugins and recur. This is an **index into the canonical docs** plus a home
for the few repo-wide lessons that had no in-repo home. Do not restate a rule that already lives in a
skill or a spec doc — link it, so the fix lands in one place.

> If you hit a gotcha not listed here and it cost you real time, add a one-line entry pointing at the
> authoritative doc (or capture it here if none exists).

---

## Build / install

- **Install via the build target, never hand-copy a single VST3 file.** A manual one-file copy leaves
  the installed bundle without `Contents/Resources/editor.uidesc` → blank UI + crash on unload. Use
  the `krate_plugin_install_*` targets (`cmake/KratePlugin.cmake`); presets go to
  `%PROGRAMDATA%/Krate Audio/<plugin>/` via `krate_plugin_install_presets`, **not** into the bundle.
- **Presets are not in the bundle.** They live at `C:\ProgramData\Krate Audio\{plugin}\`. Regenerated
  presets must be installed there (the auto-install POST_BUILD step, or by hand) — rebuilding the
  `.vst3` alone does not refresh them.
- **CMake on Windows needs the full path.** The Python cmake shim in PATH does not work — use
  `"C:/Program Files/CMake/bin/cmake.exe"`. See root `CLAUDE.md` → Build Commands.
- **The POST_BUILD copy to `C:/Program Files/Common Files/VST3/` may fail on permissions.** That is
  expected and harmless — compilation already succeeded; the built bundle is under
  `build/windows-x64-release/VST3/Release/`.

## Cross-platform / numerical

- **`-ffast-math` breaks `std::isnan()` and folds `infinity()`/`quiet_NaN()` to finite garbage.**
  macOS CI uses `-ffast-math`. Detect NaN by bit manipulation on a source file built `-fno-fast-math`;
  build non-finite **test** inputs from bit patterns through a `volatile` sink. Canonical:
  [`vst-guide/CROSS-PLATFORM.md`](../../.claude/skills/vst-guide/CROSS-PLATFORM.md), root `CLAUDE.md`
  → Cross-Platform Compatibility.
- **MSVC vs Clang diverge at the 7th–8th decimal.** Use `Approx().margin()` in tests and
  `std::setprecision(6)` in approval tests.
- **Windows CI is pinned to `windows-2022` / VS 2022 (MSVC v143).** VS 2026 / v144 changes FP codegen
  and breaks bit-exact DSP tests — do not move to `windows-latest`.
- **Denormals:** enable FTZ/DAZ (`enableFTZDAZ()` / `_MM_SET_FLUSH_ZERO_MODE`). The DSP test mains do
  this in `dsp_test_main.cpp`.

## Real-time audio thread

- **No allocation / lock / exception / I/O on the audio thread.** Single source of the *rules*:
  Constitution Principle II; canonical how-to + forbidden-ops list:
  [`dsp-architecture/REALTIME-SAFETY.md`](../../.claude/skills/dsp-architecture/REALTIME-SAFETY.md);
  review lens with severity table: `.claude/skills/code-review/DSP-REVIEW.md`.
- **Processor must work without a Controller.** Never cross-include processor/controller headers;
  communicate via `IMessage`. See [`vst-guide/THREAD-SAFETY.md`](../../.claude/skills/vst-guide/THREAD-SAFETY.md).

## VST3 parameters / state / editor

- **Never swap a registered param's type at the same ParamID.** Changing RangeParameter ↔
  StringListParameter at one ID can break editor load in DAWs that cache param metadata. Use a new
  ParamID or a custom `CView`. Also see [`vst-guide/PITFALLS.md`](../../.claude/skills/vst-guide/PITFALLS.md).
- **State versioning / backward compatibility:** every stream-format change needs a version bump +
  migration. Canonical: [`plugin-state-persistence.md`](plugin-state-persistence.md).
- **Editor-teardown use-after-free** is the #1 crash class. The shared open/close-cycle harness
  ([`project_editor_lifecycle_harness`]) catches it but only has teeth under ASan/valgrind — a Release
  pass proves nothing. See the `crash-triage` agent for the triage flow.

## AU wrapper / macOS

- **Adding any audio input bus (incl. sidechain) requires updating BOTH AU config files:**
  `resources/au-info.plist` (add the channel config) AND `resources/auv3/audiounitconfig.h`
  (`kSupportedNumChannels`, packed digit **pairs** — `02` = 0-in/2-out; `0222` = "0/2 + 2/2").
  `0022` is wrong (parses as `(0,0)+(2,2)`). Auxiliary sidechain buses use `0` flags, not
  `kDefaultActive`; `process()` must handle `data.numInputs == 0`. `auval -v <type> <sub> <mfr>` must
  pass. audiounitconfig.h is generated from a `.h.in` via CMake.

## Tests / tooling

- **DSP tests are 5 per-layer executables** (`dsp_core_tests`, `dsp_primitives_tests`,
  `dsp_processors_tests`, `dsp_systems_tests`, `dsp_effects_tests`) sharing `dsp_test_main.cpp` — no
  single `dsp_tests.exe`. Build/run only the layer you touched.
- **Test sources are listed explicitly, not globbed.** A new sibling test file that is not added to
  its target's CMakeLists silently does not run. Register it.
- **Assert timbre, not just difference,** in perceptual render tests — a "the output changed" check
  passes even when the change is wrong. See `.claude/skills/testing-guide/ANTI-PATTERNS.md`.
- **`catch_discover_tests` registers case names, not exe names** — `ctest -R <exe>` matches nothing;
  filter by test-case name or run the exe directly.

## Global allocator hazard (tests)

- **A test globally overriding `operator delete`→`free()` under `-fvisibility=hidden`** frees
  libstdc++ `std::string`s with `free()` → flaky SIGSEGV, invisible to ASan, caught only by valgrind.
  There is a valgrind-linux CI lane for exactly this class.
