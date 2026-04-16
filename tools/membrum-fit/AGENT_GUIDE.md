# `tools/membrum-fit` — Agent Guide

> Comprehensive reference for future AI sessions. Read this before touching
> the tool. Pair with [`README.md`](README.md) (user-facing usage),
> [`specs/membrum-fit-tool.md`](../../specs/membrum-fit-tool.md) (the original
> design brainstorm), and the seventeen `tools/membrum-fit:` git commits in
> `git log --oneline | grep membrum-fit`.

## What this tool is

An offline native-C++20 command-line analysis-by-synthesis fitter for
Membrum. Ingests WAV drum samples and emits Membrum-format `.vstpreset`
files (per-pad and full v6 kit) plus optional JSON intermediates, by
rendering candidate `PadConfig`s through the *production* Membrum DSP
and minimising a perceptual loss between candidate and target.

The tool MUST share its render path with the live Membrum plugin so the
loss surface BOBYQA optimises is the same surface the user hears at
runtime. That's enforced by Phase 0 of the build (see "Targets" below).

## Repository layout

```
tools/membrum-fit/
├── CMakeLists.txt              # add_subdirectory'd by root if VSTWORK_BUILD_MEMBRUM_FIT=ON
├── README.md                   # User-facing intro
├── AGENT_GUIDE.md              # This file
├── src/
│   ├── entry.cpp               # main() for the membrum_fit executable
│   ├── gen_entry.cpp           # main() for membrum_fit_gen (corpus tool)
│   ├── main.{h,cpp}            # fitSample() + runMembrumFit() (top-level pipeline)
│   ├── cli.{h,cpp}             # CLI11 argument parser
│   ├── types.h                 # Shared data structs (LoadedSample, ModalDecomposition, ...)
│   ├── loader.{h,cpp}          # dr_wav + linear resample + -1 dBFS normalise
│   ├── segmentation.{h,cpp}    # Spectral-flux onset + RMS gate
│   ├── features.{h,cpp}        # Attack-window features (LAT/flatness/...)
│   ├── exciter_classifier.{h,cpp}  # Hand-crafted rule tree
│   ├── body_classifier.{h,cpp}     # Shift-search log-ratio scoring
│   ├── tone_shaper_fit.{h,cpp}     # Pitch env + filter ADSR + drive/fold
│   ├── unnatural_fit.{h,cpp}       # ModeInject + DecaySkew from residual
│   ├── modal/
│   │   ├── matrix_pencil.{h,cpp}   # Hua & Sarkar 1990 MP on Eigen JacobiSVD
│   │   ├── esprit.{h,cpp}          # Delegates to MP (TLS variant deferred)
│   │   └── mode_selection.{h,cpp}  # MDL-style 95%-energy cutoff
│   ├── mapper_inversion/
│   │   ├── membrane_inverse.{h,cpp}    # f0 = 500 * 0.1^size
│   │   ├── plate_inverse.{h,cpp}       # f0 = 800 * 0.1^size
│   │   ├── shell_inverse.{h,cpp}       # f0 = 1500 * 0.1^size
│   │   ├── bell_inverse.{h,cpp}        # f_nominal = 800 * 0.1^size
│   │   ├── string_inverse.{h,cpp}      # f0 = 800 * 0.1^size; brightness=1-material
│   │   └── noise_body_inverse.{h,cpp}  # plate-like + noise filter from centroid
│   ├── refinement/
│   │   ├── render_voice.{h,cpp}    # RenderableMembrumVoice (DrumVoice harness)
│   │   ├── loss.{h,cpp}            # MSS + MFCC + log-envelope L1
│   │   ├── bobyqa_refine.{h,cpp}   # NLopt LN_BOBYQA wrapper
│   │   └── cmaes_refine.{h,cpp}    # NLopt GN_CRS2_LM (replaces LGPL libcmaes)
│   ├── ingestion/
│   │   ├── wav_dir.{h,cpp}         # JSON map: {midi: wav}
│   │   └── sfz_ingest.{h,cpp}      # In-house ~200-line SFZ parser
│   └── preset_io/
│       ├── pad_preset_writer.{h,cpp}   # 284-byte v1 blob in container
│       ├── kit_preset_writer.{h,cpp}   # v6 blob in container
│       └── json_writer.{h,cpp}         # Spec 141 §10 JSON schema
└── tests/
    ├── CMakeLists.txt
    └── unit/
        ├── test_smoke.cpp                  # provides Catch2 main()
        ├── test_loader.cpp
        ├── test_loader_advanced.cpp        # stereo, 48k resample
        ├── test_segmentation.cpp
        ├── test_features.cpp
        ├── test_body_classifier.cpp        # String + Shell
        ├── test_body_classifier_full.cpp   # Membrane + Plate + Bell + NoiseBody
        ├── test_pitch_env_detector.cpp
        ├── test_unnatural_fit.cpp
        ├── test_sfz_ingest.cpp
        ├── test_sfz_notename.cpp           # "c2", "f#2" syntax
        ├── test_wav_dir.cpp
        ├── test_cli_e2e.cpp                # full pipeline through fitSample()
        ├── test_cli_parse.cpp              # parseCli() argument validation
        ├── test_golden_kick.cpp            # render -> fit -> assert MSS < 6.0
        ├── test_golden_corpus.cpp          # [.corpus] N-config sweep (hidden)
        ├── modal/
        │   ├── test_matrix_pencil.cpp      # 2-mode infinite-SNR
        │   ├── test_mp_snr.cpp             # SNR sweep at inf/40/20 dB
        │   └── test_mode_selection.cpp
        ├── mapper_inversion/
        │   ├── test_membrane_inverse.cpp           # FR-055 defaults
        │   ├── test_membrane_roundtrip.cpp         # 100x size round-trip
        │   ├── test_all_inversions_defaults.cpp    # FR-055 for all 6 bodies
        │   └── test_all_body_roundtrips.cpp        # 100x per body for the 5 non-Membrane
        ├── refinement/
        │   ├── test_loss.cpp               # MSS/MFCC/env identity + monotonicity
        │   ├── test_bobyqa.cpp             # never-increases-loss guarantee
        │   └── test_global_crs.cpp         # CRS smoke
        └── preset_io/
            ├── test_pad_writer.cpp                 # writes valid .vstpreset
            ├── test_pad_blob_roundtrip.cpp         # blob fields survive write/read
            ├── test_kit_writer_bytes.cpp           # byte-exact vs state codec
            ├── test_pad_json.cpp                   # JSON pad round-trip
            └── test_json_writer.cpp                # JSON kit round-trip
```

## Targets and the build graph

Phase 0 of the implementation extracted the Membrum DSP into two libraries
that BOTH the live plugin and `tools/membrum-fit` consume — this is what
makes "render through the production DSP" possible without dragging the
plugin's Processor/Controller into the tool. See
[`plugins/membrum/CMakeLists.txt`](../../plugins/membrum/CMakeLists.txt).

```
KrateDSP                  (existing, dsp/)
   |
   v
membrum_dsp INTERFACE     (header-only Membrum DSP: bodies, exciters,
   |                       ToneShaper, UnnaturalZone, PadConfig, DrumVoice;
   |                       NO VST3 SDK includes)
   v
membrum_preset_io STATIC  (state_codec.{h,cpp} + membrum_preset_container,
   |                       links VST3 SDK)
   v
                        +---------------------+
                        |                     |
        membrum_fit_core STATIC          Membrum.vst3
        (this tool's pipeline)         (live plugin)
                        |
        +---------------+---------------+
        |                               |
   membrum_fit (CLI)            membrum_fit_gen (corpus tool)
   membrum_fit_tests (Catch2)
```

The plugin and the tool share `state_codec` and `MemoryStream` so kit/pad
preset bytes are guaranteed identical regardless of the producer
(verified by `test_kit_writer_bytes.cpp`).

## External dependencies

| Dep | Source | License | Used by |
|---|---|---|---|
| dr_wav | vendored `extern/dr_libs/dr_wav.h` | Public Domain / MIT-0 | loader, gen tool, tests |
| Eigen 3.4 | FetchContent (gitlab.com/libeigen/eigen) imported as INTERFACE | MPL-2 | matrix_pencil |
| CLI11 v2.4.2 | FetchContent (CLIUtils/CLI11) | BSD-3 | cli.cpp |
| NLopt v2.7.1 | FetchContent (stevengj/nlopt), gated by MEMBRUM_FIT_ENABLE_NLOPT | BSD/MIT for the algos used | bobyqa_refine, cmaes_refine |
| nlohmann/json | FetchContent (existing root pull) | MIT | json_writer, gen tool, ingestion |
| KrateDSP | sibling CMake target (`dsp/`) | project | render_voice via DrumVoice |
| membrum_dsp | new INTERFACE in `plugins/membrum/CMakeLists.txt` | project | render_voice, body_classifier, mapper_inversion |
| membrum_preset_io | new STATIC in `plugins/membrum/CMakeLists.txt` | project | preset writers |

CMake options that gate the tool (root `CMakeLists.txt`):

- `VSTWORK_BUILD_MEMBRUM_FIT` (default ON) — builds the tool + Eigen + CLI11 + (gated) NLopt
- `MEMBRUM_FIT_ENABLE_NLOPT` (default ON) — fetches NLopt for the BOBYQA / CRS paths
- `MEMBRUM_FIT_ENABLE_CMAES` (default OFF) — kept available for future libcmaes (LGPL); CRS via NLopt is the default global path

## Pipeline contract

Each stage is a pure function of `(input struct) -> (output struct)`.
No globals, no hidden state. End-to-end driver:
[`fitSample`](src/main.h) at `src/main.cpp`.

```
loadSample(wav, targetSr) -> std::optional<LoadedSample>
                                  |
                                  v
segmentSample(samples, sr) -> SegmentedSample (onset / decay window)
                                  |
                                  v
extractAttackFeatures(samples, seg, sr) -> AttackFeatures (LAT / flatness / ...)
                                  |
                                  v
classifyExciter(features, FullSixWay) -> Membrum::ExciterType
                                  |
                                  v
extractModesMatrixPencil(decay, sr, modelOrder) -> ModalDecomposition
                                  |
                                  v
classifyBody(modes, features) -> BodyScoreList (best -> Membrum::BodyModelType)
                                  |
                                  v
invert<Body>(modes, features, sr) -> Membrum::PadConfig
                                  |
                                  v
fitToneShaper(samples, seg, sr, modes, &cfg)
fitUnnaturalZone(samples, seg, sr, modes, fittedBody, &cfg)
                                  |
                                  v
RenderableMembrumVoice + refineBOBYQA(ctx, voice) -> RefineResult
       (optional: refineGlobalCRS when --global)
                                  |
                                  v
writePadPreset(path, padConfig, name, subcat) -> bool
writeKitPreset(path, pads[32], name, subcat) -> bool
writeKitJson / writePadJson (optional)
```

## Critical invariants

1. **FR-055 / spec §4.7**: every mapper inversion MUST emit normalised
   neutral PadConfig defaults for any field it doesn't touch
   (`modeStretch=0.333333`, `decaySkew=0.5`, `couplingAmount=0.5`,
   all macros = 0.5, `morphEnabled=0`, `morphDuration=0.095477`).
   Tested by `test_all_inversions_defaults.cpp`.

2. **String semantic-inversion**: `string_mapper.h:53` defines
   `brightness = 1 - material`. The String inversion MUST preserve that —
   high brightness → LOW material. Inversion at
   [`mapper_inversion/string_inverse.cpp`](src/mapper_inversion/string_inverse.cpp).

3. **Sample-rate consistency** (spec §9 risk #10): `RenderableMembrumVoice::prepare(sr)`
   MUST be called with the same sample rate the modal extraction used.
   Otherwise modal frequencies drift.

4. **Byte-exact preset compatibility**: kit/pad blobs MUST be identical to
   what the live plugin's `state_codec.cpp` produces. Both consumers link
   the SAME `membrum_preset_io` library — there is no parallel codec.
   Regression test: `test_kit_writer_bytes.cpp`.

5. **NLopt gotcha**: `nlopt_set_maxeval(opt, 0)` means "no limit", not
   "zero iterations". The refine wrappers short-circuit when
   `ctx.maxEvals <= 0` to avoid a memory runaway. See commit `417bc5c9`.

6. **Coupling impossible from isolated samples** (spec §9 risk #7): the
   kit writer always emits `couplingAmount=0.5` and globals = 0. Phase 6
   macros always 0.5 (spec §9 risk #8).

## Build and run

Standard preset-driven flow:

```bash
"/c/Program Files/CMake/bin/cmake.exe" --preset windows-x64-release
"/c/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target membrum_fit
"/c/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target membrum_fit_tests
build/windows-x64-release/bin/Release/membrum_fit_tests.exe 2>&1 | tail -3
```

CLI invocation patterns (note: global `--max-evals 0` etc go BEFORE
the subcommand because of how CLI11 handles fallthrough):

```bash
# Per-pad, deterministic only (no BOBYQA), fast
membrum_fit --max-evals 0 per-pad input.wav out/preset.vstpreset

# Per-pad with BOBYQA refinement + JSON intermediate
membrum_fit per-pad input.wav out/preset.vstpreset --json --max-evals 100

# Kit from JSON map (paths relative to JSON's parent dir)
membrum_fit kit kit.json out/Kit.vstpreset

# Kit from SFZ
membrum_fit kit acoustic.sfz out/Acoustic.vstpreset

# With ESPRIT modal method + global escape
membrum_fit per-pad input.wav out.vstpreset --modal-method esprit --global
```

## Performance notes

Empirically measured on Windows + MSVC + Eigen FetchContent build:

- Loader: <50 ms for typical WAVs.
- Segmentation + features: <50 ms total.
- **Matrix Pencil: ~4 minutes per call** (Eigen complex JacobiSVD on
  a 2731×1365 Hankel matrix dominates). This is by far the slowest
  stage. Decay length is hard-capped at 4096 samples in
  [`matrix_pencil.cpp`](src/modal/matrix_pencil.cpp); without this cap a
  22k-sample decay was taking >15 min per call.
- BOBYQA refinement: each eval is ~50 ms (render + MSS + MFCC + env on
  a 0.5 s buffer). 100 evals ≈ 5 s wall time. Spec aspiration was 15 ms
  per eval; we're ~3× off.
- Per-pad fit (deterministic only, BOBYQA off): ~4 min.
- Per-pad fit with BOBYQA 100 evals: ~4 min + 5 s.
- Kit fit (5 pads, BOBYQA off): ~20 min.
- Full kit (32 pads, BOBYQA off): ~2 hours. With BOBYQA at 100 evals:
  ~2 h + 3 min.

The Matrix Pencil bottleneck is the obvious next perf target. Options:

- Use NLopt's BSD CRS for modal extraction instead of MP (very different
  algorithm, would need testing).
- Replace `Eigen::JacobiSVD` on `MatrixXcd` with `BDCSVD` or a power-
  iteration thin-SVD that only computes the top-K singular vectors.
- Batch SVD across configs in the corpus sweep.
- Compile with `-DEIGEN_USE_LAPACKE` (huge build complication).

## Known gaps versus spec §8 exit gates

| Gate | Status |
|---|---|
| Phase 1: 100% body-class accuracy on Kick golden corpus | NOT MET — corpus shows 0.67 on N=3 |
| Phase 1: < −25 dB MSS on Kick golden corpus | NOT MET (MSS gate is in log-mag L1 units, not dB; current value 0.030) |
| Phase 2: real public acoustic kit fits & sounds recognisable | DEMONSTRATED end-to-end on a 5-pad real kit (commit `417bc5c9`) |
| Phase 3: real cymbal/bell < −22 dB MSS | NOT MEASURED on real samples |
| Phase 4: SFZ kit produces ready-to-load preset | parser tested, end-to-end run with a public SFZ kit not done |

The aspirational corpus N=200 is parameterised; runtime constrained the
default to N=3 in CI. Sweep on demand: `membrum_fit_tests "[corpus]"`
with N edited in `test_golden_corpus.cpp`.

## Maintenance pointers for future agents

- **Adding a new pipeline stage**: model after `tone_shaper_fit.{h,cpp}`.
  Pure function in/out; no globals; one Catch2 test file under
  `tests/unit/` mirroring the directory layout.
- **Changing PadConfig**: every change to
  [`plugins/membrum/src/dsp/pad_config.h`](../../plugins/membrum/src/dsp/pad_config.h)
  ripples through:
  1. `state_codec.h/.cpp` — serialisation
  2. `bobyqa_refine.cpp::fieldPtr` — optimisable parameter index map
  3. `json_writer.cpp::padToJson` — JSON schema
  4. `gen_entry.cpp::padConfigFromJson` — golden-corpus loader
  5. `test_all_inversions_defaults.cpp::requireDefaultsPreserved` — invariant check
- **Adding a new external dep**: pattern in root
  [`CMakeLists.txt`](../../CMakeLists.txt) is FetchContent under the
  `if(VSTWORK_BUILD_MEMBRUM_FIT)` block. Prefer permissively licensed
  (BSD/MIT/Apache/MPL); LGPL/GPL must be opt-in only.
- **Test running**: single binary, sub-second to build incrementally,
  tests run in ~10s for the default suite. The `[corpus]` test is
  hidden — must pass `[corpus]` to invoke. Always pipe through `tail`
  to avoid spamming the chat with intermediate output:
  `membrum_fit_tests.exe 2>&1 | tail -5`.
- **CI integration**: tests are auto-discovered via
  `catch_discover_tests` so `ctest --test-dir build/windows-x64-release`
  picks them up.
- **Pluginval**: this tool produces presets the live plugin loads. Run
  pluginval after any change that affects either side of the
  membrum_preset_io boundary:
  `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3"`

## Commit timeline (most recent last)

- `d7e8a615` — Phase 0: extract `membrum_dsp` + `membrum_preset_io`
- `01cb88b8` — Phase 1.1 scaffold + Eigen/NLopt/CLI11 fetched
- `9945932e` — Phase 2+3+4 algorithms (bodies/classifier/ADSR/SFZ/MDL/Unnatural)
- `57319f7c` — Golden Kick + byte-exact kit writer tests
- `8a27c8f8` — Corpus sweep + classifier fix + MP perf cap (4096 samples)
- `c01ed868` — `--global` escape via NLopt CRS (replaces LGPL libcmaes)
- `8c3f46c1` — CLI end-to-end smoke test
- `554f3c2e` — Shift-search body classifier + 6-body coverage
- `17c8f46f` — Membrane mapper round-trip (spec §7)
- `c547fa34` — BOBYQA + features + segmentation + JSON tests
- `dcd4254f` — Loss identity + pad-blob + pad-JSON + pitch-env tests
- `a1cfa663` — WAV-dir + SFZ note-name + mode-selection tests
- `e3e47f69` — Per-body size round-trips for the 5 non-Membrane bodies
- `99b2e815` — Stereo loader + 48k resample + CLI parser tests
- `850581fe` — README per spec §5.1 directory structure
- `3a87bcb7` — gitignore Claude wakeup-scheduler lock
- `417bc5c9` — `--max-evals 0` short-circuit + real-kit demo

`git log --oneline -- tools/membrum-fit/` is the authoritative list.

## Where to load fitted presets

Membrum reads from:

- Kits: `C:\ProgramData\Krate Audio\Membrum\Kits\{Acoustic,Electronic,Percussive,Unnatural}\`
- Pads: `C:\ProgramData\Krate Audio\Membrum\Pads\{Kick,Snare,Tom,Hat,Cymbal,Perc,Tonal,FX}\`

The fitter writes to wherever you point it; copy the output `.vstpreset`
into the matching subdirectory and refresh the preset browser inside
the plugin.

## Out of scope (deferred per spec §1.2 / §8 Phase 5)

- Multi-velocity round-robin fitting
- Choke-group clustering from sample correlation
- Pad-to-pad coupling matrix inference from co-played samples
- Real-time / live operation
- Sample-layer playback presets (Membrum is synthesis-only)
- libcmaes integration (LGPL containment per spec §6)
- Reverb / room decorrelation modelling

These are flagged research items in the original spec and intentionally
unimplemented.
