# Crossover de-duplication plan (one shared multiband crossover)

Date: 2026-11-09. Source: read-only investigation of `audit/disrumpo` after commit 8df18288. User rulings at the end.

## Finding

There is exactly one real duplicate. Disrumpo's `CrossoverNetwork`
(`plugins/disrumpo/src/dsp/crossover_network.h`) and KrateDSP's `Crossover3Way` / `Crossover4Way`
(`dsp/include/krate/dsp/processors/crossover_filter.h:473-924`) build the same cascaded LR4 tree with the same
D'Appolito allpass layout (band k gets AP(f_{k+1} … f_{N-2}) in ascending order). Differences: Disrumpo has a
runtime band count 1–4, dry-path compensation (T1.09), frequency preservation across band-count changes (T1.03,
T2.08), set-then-snap on prepare (T1.03), allpass Q 0.7071f; the shared classes have fixed 3/4 bands, Q 1/√2
(8df18288), atomics, an order-dependent frequency clamp, and glide-on-first-prepare. All use float32 `Biquad`
allpasses (follow-up 7); the LR4 sections run in double (68255845).

Production users: `Crossover3Way` — none (tests only). `Crossover4Way` — `FractalDistortion` Multiband mode only,
compensation off. `CrossoverNetwork` — Disrumpo processor L/R.

Not duplicates (stay as they are): `CrossoverLR4` itself (the building block; Profundum's `BassStereoField` needs a
2-way side-channel split), `BodyResonance`'s first-order split, `LowpassBiquadD` splits in HarmonicFold /
ProfundumCore (different design; merging would change Profundum), Disrumpo's redistribution / log defaults /
20 kHz cap (product policy → wrapper), `AliasingEffect`'s parallel Butterworth band isolation (different filter).

## Design

`template <int MaxBands> class MultibandCrossover` in `crossover_filter.h` (Layer 2, next to an unchanged
`CrossoverLR4`); delete `Crossover3Way` / `Crossover4Way`.

```cpp
template <int MaxBands> class MultibandCrossover {   // N-1 CrossoverLR4 + allpass layout
  void prepare(double sr) noexcept;             // set-then-snap: no glide
  void reset() noexcept;
  void setActiveBands(int n) noexcept;          // 1..MaxBands; joining LR4s/allpasses start clean
  void setCrossoverFrequency(int i, float hz) noexcept; // stored for every index, active or not
  float getCrossoverFrequency(int i) const noexcept;
  void setAllpassCompensation(bool on) noexcept; void setSmoothingTime(float ms) noexcept;
  void process(float x, std::array<float, MaxBands>& bands) noexcept;
  [[nodiscard]] float processDryCompensation(float x) noexcept; // AP(f0..f_{n-2})
};
```

Plain (non-atomic) setters (every caller sets on the audio thread; the atomics' thread-safety claim was false
anyway: the setters also write a plain smoother). Disrumpo keeps a thin `CrossoverNetwork` wrapper holding one
`MultibandCrossover<4>` plus its policy (1–4 clamp, 20 kHz cap from `state_codec.h`, log defaults,
`frequenciesInitialized_` / `frequencyWasSet_`, `redistributeIncreasing`, `debugSetBandCountCalls`).

## Steps (each its own commit, each gated)

1. Add `MultibandCrossover`; port the generic tests (flat sum N=2–4 with/without compensation, D-164 one-octave
   cases, dry alignment, join-clean, re-prepare keeps frequencies). Gate `dsp_processors_tests` + every executable
   `tools/header-consumers.js` names (expect `dsp_systems_tests`, `profundum_tests`). ~1 day.
2. Move `FractalDistortion` to `MultibandCrossover<4>` (compensation per ruling 3), delete 3Way/4Way, port the ~40
   3Way/4Way test cases; fix the dead `FractalDistortion::setCrossoverFrequency` (ruling 6). Gate crossover +
   fractal tags, Disrumpo Fractal adapter cases, Profundum consumers. ~0.5–1 day.
3. Turn Disrumpo's `CrossoverNetwork` into the wrapper. Gate `crossover_network_test.cpp` (17 cases),
   `processor_audio_output_test.cpp` [D-018][D-023], `processor_lifecycle_state_test.cpp` [D-046][D-022][D-003],
   `band_management_integration_test.cpp`, `distortion_adapter_test.cpp` crossover cases, pluginval. ~1 day.
4. Double-precision allpasses that track the smoothed LR4 frequency (lift `CrossoverLR4::Section` into `detail`,
   double RBJ allpass design, read-only current-frequency getter on `CrossoverLR4`); add 192 kHz back to the D-018
   dry-alignment test. Closes follow-up 7. ~0.5 day.
5. Docs: `specs/_architecture_/layer-2-processors.md`, `symbols.json`, `plugin-architecture.md`,
   `processors/README.md`, and `dsp/CLAUDE.md` (ruling 7).

No bit-exact goldens pin the crossover output, so each step's safety net is the tolerance tests plus a one-off
hidden A/B probe (old vs new) whose numbers go in the commit message.

## User rulings (2026-11-09)

1. Allpass Q: exact 1/√2 everywhere; Disrumpo is not bit-identical (A/B difference measured and reported).
2. Fractal start-up: snap on prepare like every caller; the first-prepare glide from 1000 Hz goes away.
3. Fractal Multiband compensation: decide after the queued Fractal measurement (band-sum deviation off vs on).
4. `AliasingEffect` stays as it is (different filter, not a duplicate).
5. The order-dependent frequency clamp is dropped; callers keep their frequencies ordered; the FR-016 clamp tests
   are removed with a note.
6. Fix the dead `FractalDistortion::setCrossoverFrequency` in this work.
7. Same-layer includes are accepted: document in `dsp/CLAUDE.md` that a layer may include headers of its own layer
   (no cycles), matching the 36 processors headers that already do.
