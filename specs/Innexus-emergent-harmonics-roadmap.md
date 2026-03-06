# Innexus Emergent Harmonics Roadmap

Origin: Concepts from `Innexus-initial-plan.md` (sections 6, 8) that were dropped during initial implementation. This roadmap captures the full design intent for two future specs.

---

## Spec A: Harmonic Physics ✅ COMPLETE (spec 122, branch `122-harmonic-physics`)

**Goal:** Make the harmonic model behave like a physical system rather than a set of independent sine waves.

**Insertion point:** Between frame source (morph/blend/filter) and `oscillatorBank_.loadFrame()` in `processor.cpp` (~line 806). All three milestones operate as `HarmonicFrame` transforms at this location.

**Existing infrastructure:**
- `Partial` struct already has `stability` (tracking confidence) and `age` (frames since birth) fields
- `HarmonicFrame` has `globalAmplitude` (RMS of source) available for energy budgeting
- `applyModulatorAmplitude()` already demonstrates per-partial amplitude modification at this insertion point
- Parameter IDs 700-799 are available (next free range after M6's 600-649)

### Milestone A1: Nonlinear Energy Mapping

**Concept (plan section 8.6):** Gentle compression/saturation on harmonic amplitudes before synthesis. Prevents one dominant harmonic from creating harsh timbres. Simulates acoustic saturation where loud partials naturally compress.

**DSP approach:**
```
For each partial i in frame:
    amp[i] = tanh(drive * amp[i]) / tanh(drive)
```
Where `drive` maps from the user parameter (1.0 = bypass, higher = more compression). This preserves sign, normalizes output range, and smoothly compresses peaks.

**Parameters:**
| Name | ID | Range | Default | Description |
|------|----|-------|---------|-------------|
| Warmth | kWarmthId (700) | 0.0-1.0 | 0.0 | Amount of amplitude compression (0 = bypass) |

**Implementation:**
- New header-only function in `plugins/innexus/src/dsp/` (e.g. `harmonic_physics.h`)
- ~20 lines: iterate partials, apply soft saturation to amplitudes
- Call site: `processor.cpp` after `applyModulatorAmplitude()`, before `loadFrame()`
- Smoother for the Warmth parameter (zipper-free)

**Test criteria:**
- Bypass (warmth=0): output frame identical to input frame
- With warmth: peak partial amplitude is reduced, quieter partials are relatively boosted
- Energy is redistributed, not created (total RMS should not increase)
- All-zero frame passes through unchanged

---

### Milestone A2: Harmonic Coupling

**Concept (plan section 8.1):** Neighboring harmonics share energy, creating spectral viscosity. When a harmonic is excited, its neighbors are subtly pulled along. This is what makes physical resonators (strings, tubes, membranes) sound connected rather than synthetic.

**DSP approach:**
```
For each partial i (1 to N):
    coupled_amp[i] = amp[i] * (1 - amount)
                   + (amp[i-1] + amp[i+1]) * 0.5 * amount
```
With boundary handling (partial 0 and N+1 treated as zero). Optional: weighted by distance (exponential falloff for coupling beyond immediate neighbors).

**Parameters:**
| Name | ID | Range | Default | Description |
|------|----|-------|---------|-------------|
| Coupling | kCouplingId (701) | 0.0-1.0 | 0.0 | Energy sharing between neighbors (0 = independent) |

**Implementation:**
- Add to `harmonic_physics.h` alongside warmth
- ~30 lines: single pass with temp buffer for neighbor amplitudes (avoid read-after-write)
- Must preserve total energy: normalize output so sum-of-squares equals input sum-of-squares
- Smoother for parameter

**Test criteria:**
- Bypass (coupling=0): output identical to input
- Single isolated partial: energy spreads to neighbors proportionally
- Energy conservation: total RMS in equals total RMS out
- Frequency coupling NOT included (keep this amplitude-only for simplicity; frequency coupling can be a future extension)

---

### Milestone A3: Harmonic Dynamics (Agent System)

**Concept (plan sections 6.2, 3.4, 3.5):** Each harmonic is a stateful "agent" with inertia, persistence, and entropy. Instead of instantly adopting each new analysis frame, the dynamics processor maintains an internal model that analysis *nudges*. This creates emergent timbral behavior: stable harmonics resist change, weak harmonics fade naturally, and total energy is conserved.

**DSP approach — HarmonicPhysics class (consolidated, see plan.md):**

Internal state per partial (i = 0..47), stored as struct-of-arrays for cache efficiency:
```
struct AgentState {
    std::array<float, kMaxPartials> amplitude{};    // Smoothed output amplitude
    std::array<float, kMaxPartials> velocity{};     // Rate of change (for momentum)
    std::array<float, kMaxPartials> persistence{};  // How long this partial has been stable [0,1]
    std::array<float, kMaxPartials> energyShare{};  // Allocated portion of energy budget
};
```

Per-frame update logic:
```
On new analysis frame:
  1. Compute target delta for each partial:
     delta[i] = analysisFrame.amp[i] - agent[i].amplitude

  2. Apply inertia (stability resists change):
     effectiveDelta[i] = delta[i] * (1.0 - stability * agent[i].persistence)

  3. Apply entropy (natural decay unless reinforced):
     agent[i].amplitude *= (1.0 - entropyRate)
     agent[i].amplitude += effectiveDelta[i]

  4. Update persistence:
     if |delta[i]| < threshold:
         agent[i].persistence = min(1.0, persistence + growthRate)
     else:
         agent[i].persistence *= decayFactor

  5. Energy conservation (optional, controlled by budget param):
     totalEnergy = sum(agent[i].amplitude^2)
     if totalEnergy > energyBudget:
         scale all amplitudes by sqrt(energyBudget / totalEnergy)

  6. Write agent amplitudes into output HarmonicFrame
```

**Parameters:**
| Name | ID | Range | Default | Description |
|------|----|-------|---------|-------------|
| Stability | kStabilityId (702) | 0.0-1.0 | 0.0 | Inertia / resistance to change (0 = instant follow) |
| Entropy | kEntropyId (703) | 0.0-1.0 | 0.0 | Natural decay rate (0 = infinite sustain, 1 = fast fade) |

**Implementation (consolidated with A1/A2 during planning):**
- Dynamics is implemented as `applyDynamics()` inside the unified `HarmonicPhysics` class in `plugins/innexus/src/dsp/harmonic_physics.h` (not a separate file or class)
- Maintains internal `AgentState` struct with four parallel arrays of `kMaxPartials` floats
- `prepare(sampleRate, hopSize)`, `reset()`, invoked via `processFrame(HarmonicFrame& frame)` (in-place)
- Called in `processor.cpp` at the same insertion point, after coupling and warmth
- `Processor` holds a single `HarmonicPhysics harmonicPhysics_` member and forwards all four parameters

**Interaction with A1/A2:**
- Processing order: Input frame -> Coupling (A2) -> Warmth (A1) -> Dynamics (A3) -> loadFrame()
- Coupling feeds energy to neighbors BEFORE dynamics applies inertia (so coupled energy can persist)
- Warmth compresses AFTER coupling (so coupled peaks don't dominate)
- Dynamics is last: it adds temporal memory on top of the spatial (coupling) and amplitude (warmth) processing

**Test criteria:**
- Stability=0, Entropy=0: output tracks input exactly (pass-through)
- Stability=1: output barely changes when input changes dramatically (high inertia)
- Entropy=1: harmonics fade to zero quickly without reinforcement
- Energy conservation: when budget is active, total energy never exceeds budget
- Persistence grows for stable partials, decays for changing ones
- Reset clears all agent state

**Relationship to existing features:**
- `Responsiveness` parameter (kResponsivenessId 303) currently controls live analysis smoothing in `LiveAnalysisPipeline`. Harmonic Dynamics is conceptually similar but operates post-analysis on the frame itself, and adds entropy + energy conservation. These are complementary, not redundant: Responsiveness smooths the *analysis*, Stability smooths the *synthesis response* to analysis.
- The existing `Partial.stability` and `Partial.age` fields from analysis could optionally inform the agent's initial persistence (high-confidence partials start with more inertia).

---

## Spec B: Analysis Feedback Loop

**Goal:** Create a self-evolving timbral system by feeding the synth's output back into its own analysis pipeline.

**Concept (plan section 6.3):**
```
analysis_input = sidechain * (1 - feedback) + synth_output * feedback
```
At low feedback: subtle self-reinforcing resonances. At high feedback: harmonics crystallize into attractor states — emergent tonal behavior.

**Prerequisite:** Spec A should be completed first. Coupling + Dynamics provide the stability mechanisms that prevent feedback from diverging.

### Signal Flow Change

Current flow:
```
sidechain --> LiveAnalysisPipeline --> frame --> [physics] --> oscBank --> output
```

New flow:
```
sidechain ----+
              |
              v
         [feedback mixer] --> LiveAnalysisPipeline --> frame --> [physics] --> oscBank --> output
              ^                                                                            |
              |                                                                            |
              +---- feedbackBuffer * feedbackAmount <---------------------------------------+
```

### Implementation

**Feedback buffer:**
- `std::vector<float> feedbackBuffer_` sized to max block size, allocated in `setActive()`
- Each process() call: capture mono output into feedbackBuffer_ AFTER synthesis
- Next process() call: mix feedbackBuffer_ with sidechainMono BEFORE `pushSamples()`
- One block of latency in the feedback path (acceptable; analysis already has STFT latency)

**Energy limiting (critical for stability):**
```
Per-sample in feedback path:
    fbSample = feedbackBuffer[s] * feedbackAmount
    // Soft limiter to prevent runaway
    fbSample = tanh(fbSample * 2.0) * 0.5
    mixedInput[s] = sidechain[s] * (1.0 - feedbackAmount) + fbSample
```

Additionally, a per-frame amplitude ceiling in the dynamics processor (Spec A's energy budget) acts as a secondary safety net.

**Parameters:**
| Name | ID | Range | Default | Description |
|------|----|-------|---------|-------------|
| FeedbackAmount | kAnalysisFeedbackId (710) | 0.0-1.0 | 0.0 | Mix of synth output into analysis input (0 = no feedback) |
| FeedbackDecay | kAnalysisFeedbackDecayId (711) | 0.0-1.0 | 0.2 | Entropy leak in feedback path (prevents infinite buildup) |

**Sidechain-only feature:** Feedback only applies in sidechain mode (`InputSource::kSidechain`). In sample mode there's no continuous analysis to feed back into, so feedback is bypassed.

**Interaction with freeze:** When freeze is active, feedback is automatically bypassed (frozen frame shouldn't be modified by feedback). On freeze disengage, feedback buffer is cleared to prevent stale audio from contaminating the re-engaged analysis.

### Test criteria

- FeedbackAmount=0: signal flow identical to current (no regression)
- FeedbackAmount=1.0 with silence input: synth output feeds fully back, should converge to stable attractor (not diverge) thanks to energy limiting
- FeedbackDecay prevents infinite sustain: with no sidechain input and feedback=1.0, output should eventually decay to silence
- Freeze engagement clears feedback buffer
- Sample mode: feedback has no effect
- CPU: feedback mixing adds negligible overhead (one multiply-add per sample)
- Energy limiting: output RMS never exceeds a defined ceiling regardless of feedback amount

### Safety mechanisms (mandatory)

1. **Soft limiter** in feedback path (tanh)
2. **Energy budget** from Spec A's dynamics processor
3. **Feedback decay** parameter (entropy leak)
4. **Hard output clamp** already exists in `HarmonicOscillatorBank::kOutputClamp`
5. **Confidence gate** in live analysis: if feedback produces garbage, confidence drops and auto-freeze engages

---

## Parameter ID Summary

| ID | Name | Spec | Milestone |
|----|------|------|-----------|
| 700 | kWarmthId | A | A1 |
| 701 | kCouplingId | A | A2 |
| 702 | kStabilityId | A | A3 |
| 703 | kEntropyId | A | A3 |
| 710 | kAnalysisFeedbackId | B | - |
| 711 | kAnalysisFeedbackDecayId | B | - |

---

## Implementation Order

```
Spec A: Harmonic Physics
  A1: Nonlinear Energy Mapping   (~1 day)
  A2: Harmonic Coupling           (~3-4 days)
  A3: Harmonic Dynamics            (~1-2 weeks)

Spec B: Analysis Feedback Loop
  (after Spec A)                   (~1 week)
```

## Files Likely Touched

**Spec A (as designed in plan.md — consolidated single-class implementation):**
- NEW: `plugins/innexus/src/dsp/harmonic_physics.h` (unified `HarmonicPhysics` class: warmth, coupling, and dynamics in one header)
- MOD: `plugins/innexus/src/plugin_ids.h` (new parameter IDs 700-703)
- MOD: `plugins/innexus/src/processor/processor.h` (HarmonicPhysics member, 4 atomics, 4 smoothers)
- MOD: `plugins/innexus/src/processor/processor.cpp` (call applyHarmonicPhysics() before each loadFrame site; state version 6→7)
- MOD: `plugins/innexus/src/controller/controller.cpp` (register 4 new parameters)
- NEW: `plugins/innexus/tests/unit/processor/test_harmonic_physics.cpp` (all three processors tested here)
- NEW: `plugins/innexus/tests/integration/test_harmonic_physics_integration.cpp`
- NEW: `plugins/innexus/tests/unit/vst/test_state_v7.cpp`

**Spec B:**
- MOD: `plugins/innexus/src/plugin_ids.h` (feedback parameter IDs)
- MOD: `plugins/innexus/src/processor/processor.h` (feedback buffer, params)
- MOD: `plugins/innexus/src/processor/processor.cpp` (feedback mixing in process loop)
- MOD: `plugins/innexus/src/controller/controller.cpp` (register feedback params)
- NEW: `plugins/innexus/tests/integration/test_analysis_feedback.cpp`

---

## Design Principles (from initial plan, still applicable)

1. **Stability over accuracy** — the system should never diverge or produce garbage
2. **Prediction over detection** — dynamics processor predicts, analysis nudges
3. **Behavior over parameters** — fewer knobs that do more, not more knobs that do less
4. **Imperfection over mathematical purity** — coupling and warmth add controlled messiness
5. **Memory enables musicality** — persistence and entropy give harmonics temporal identity
6. **Analysis nudges, it does not dictate** — stability parameter controls how much
