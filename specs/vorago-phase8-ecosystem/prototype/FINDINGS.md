# Vorago Phase 8 — Ecosystem prototype findings

**Status: round 2 complete (2026-09-15) — sufficient to write the spec from.** Boundedness is settled
(§1, re-fuzzed under the round-2 rules in §5); the rule set, its defaults and the success metrics
are settled in §4–§6. §2–§3 are round-1 history and remain correct; round 1's §5 ("open problems")
is superseded by §6 below.

The roadmap mandates this prototype before the Phase 8 spec: *"an offline Node.js prototype
simulating the agent graph + rule set... The rule set is tuned there — where iteration is seconds,
not audio-thread builds. The spec then encodes the proven rules."* This file records what the
simulation actually showed, including the parts that contradict the roadmap's design sketch.

Run it: `node run.js trace|fuzz|ablate|sweep|seeds|determinism` from this directory.
The RNG is a bit-exact port of `dsp/include/krate/dsp/core/random.h` (`Xorshift32` +
`deriveStreamSeed`), so seeds and stream-splitting transfer to the C++ component unchanged.

---

## 1. Boundedness — and the two guards it actually requires

**Final state: 1000 randomised configurations, 0 non-finite, 0 pool-negative, 0 unbounded.**
Energy lives in a closed system (reservoir pool + resource cells + agents) and total energy is
invariant. Measured drift over a 30-minute run: **2.4e-15 to 1.9e-13 relative** — float noise.

**But this was NOT true of the first design, and the correction is the finding.** An earlier
version of this document claimed boundedness was structural "by construction". Fuzzing disproved
it twice. Closure requires two guards that are not obvious, and both belong in the C++ spec as
requirements rather than as implementation detail:

**Guard 1 — one withdrawal budget per step.** Cell regrowth and the global feed each capped their
draw against a pool that did not yet know about the other, so together they could overdraw.
**591/1000 configs** drove the pool negative this way. Both draws must decrement a single running
balance.

**Guard 2 — a transfer is limited by what the source holds, enforced in two passes.**
This is the subtle one. The exchange rule was *antisymmetric* — every transfer added to one agent
exactly what it removed from another, so the pairwise sum was provably zero. That looks like
conservation is structural, and it is why the original claim was made. It is not sufficient: an
agent can be asked to give away more than it has, and clamping it at zero charges the shortfall to
the pool — an uncapped withdrawal. After Guard 1, **468/1000 configs** still failed this way.

The fix is to defer the flows, accumulate each agent's desired outflow, compute
`scale_i = min(1, e_i / desired_i)`, then apply each pair scaled by `min(scale_i, scale_j)`.
Scaling *both sides of a pair by the same factor* is what preserves antisymmetry while making
solvency structural; a solvent agent is never throttled. This also raised the alive fraction from
6.4% to **14.6%**, since agents that stay solvent stay lively.

**The lesson worth carrying:** antisymmetry gives conservation only among participants who can
actually pay. In C++ this class of defect would have surfaced as a slow, configuration-dependent
energy leak, months later, under an audio-thread debugger.

**Determinism under seed** holds — same seed reproduces a bit-identical trajectory.

---

## 2. What broke (the roadmap's sketch does not work as written)

### 2.1 A single global energy pool makes every agent a copy of every other

The first draft had agents feeding from one shared scalar pool, as the roadmap implies. Result:
mean pairwise correlation between agent outputs of **0.84–0.99**. Thirty-two agents producing one
modulation shape thirty-two times — useless as the "bank of `ModulationSource`s" Phase 8 is
supposed to deliver.

Fix that worked: make the resource **spatial**. A field of resource cells around the habitat, each
regrowing independently, grazed locally. Agents deplete their own neighbourhood, compete with
whoever is nearby, and must move. Correlation fell to **0.44–0.46**, which is at this system's
noise floor (see §3.1).

### 2.2 Pure diffusion homogenises into a dead flat soup

The roadmap names "exchange energy" as a core rule. Taken literally — energy flowing from richer
to poorer neighbours — it is an averaging operator, and averaging destroys structure. Measured
entropy **4.998 out of a 5.000 maximum**: all 32 agents holding near-identical energy, nothing
happening. Adjacent seeds correlated at **0.9989**.

### 2.3 Neighbourhood width is the most consequential parameter, and it is not close

At `kernelSigma = 0.12` (each agent's patch spanning an eighth of the habitat) every neighbourhood
overlapped every other: activity 0.06, correlation 0.84. At **0.03**: activity **0.32**,
correlation **0.44**, zero frozen agents. Same rules, same everything else. Locality is what
creates individuality.

### 2.4 Deficit-proportional regrowth silently erases the spatial structure it is meant to create

The first spatial version shared one regeneration budget across cells in proportion to how empty
each was. That pulls every cell toward the same level, so the field comes out uniform and the
spatial model buys nothing. The tell was unmistakable in the sweep: `resourceCells`, `grazeRate`
and `maxSpeed` produced **byte-identical rows** across their whole tested range — three parameters
doing measurably nothing.

Fix: each cell regrows logistically and independently. A grazed patch stays depleted while an
ungrazed one fills. After the change those parameters visibly matter.

### 2.5 Predation above the neutral point is unbounded, and below it does nothing

`predation` blends the exchange rule between diffusive (strong feeds weak) and predatory (weak
feeds strong).

Measured **before** Guard 2 (§1) existed:

| predation | activity | frozen agents | bounded (pre-Guard-2) |
|---|---|---|---|
| 0.0 | 0.222 | 0 | yes |
| 0.25 | 0.222 | 0 | yes |
| 0.5 | 0.215 | 0 | yes |
| 0.75 | 0.081 | 22 | **no** |
| 1.0 | 0.057 | 23 | **no** |

The unboundedness in the last two rows is **fixed** — it was the uncapped-overdraw defect Guard 2
closes, and predation 0.75–1.0 at `exchangeRate` 3.0 now holds conservation to ~5e-14. Do not read
this table as "predation is unsafe"; read it as how the defect was first observed.

What survives the fix is the other half: **the safe settings are indistinguishable from each
other** (activity 0.215–0.222 across predation 0–0.5). The agent-to-agent exchange rule does not
earn its place on the evidence so far — spatial grazing competition already does that job. Whether
predation becomes useful now that it is solvent is untested and belongs with open problem 1.

This was caught by an ablation run in which *"no exchange" came out bit-identical to baseline, to
every printed digit* — because the default `predation: 0.5` makes the `(1 - 2·predation)` factor
exactly zero. The rule had been silently disabled in the very configuration being validated.

---

## 3. The roadmap's proposed success metrics do not measure what they claim

This matters more than any parameter value, because these metrics would become the Phase 8 spec's
success criteria and would then be enforced against a C++ implementation.

### 3.1 An absolute correlation threshold is meaningless here

These signals are slow — autocorrelation decays over ~150 s — so a 900 s window contains only a
handful of *independent* samples, and `|correlation|` between genuinely unrelated slow signals runs
high by chance. Measured noise floor, taken between **different agents in the same run** (as
unrelated as two signals in this system get):

| window | noise floor | cross-seed correlation |
|---|---|---|
| 900 s | 0.606 | 0.570 |
| 3600 s | 0.501 | 0.458 |

Cross-seed correlation is *below* the floor at both durations: two seeds produce outputs as
different as two different agents within one voice. Judged against a fixed 0.5 threshold, this
same configuration was declared "SEED-BLIND — voices would sound alike". **Any decorrelation
criterion must be stated relative to the measured within-run floor, never as an absolute number.**

### 3.2 Entropy of the energy distribution is permutation-invariant

The roadmap proposes "energy distribution entropy keeps changing over 30 min" as the
non-triviality metric. Shannon entropy cannot see *which* agent holds the energy. Energy sloshing
between agents in a fixed pattern holds entropy exactly constant while every agent's output swings
— the metric calls the liveliest case frozen. Conversely, agents drifting in lockstep keep entropy
moving while producing one signal copied N times.

Entropy also barely discriminated in practice: it sat near 4.9–5.0 across every regime tested in
the first two rounds, including regimes that differed by 5× in per-agent activity.

Proposed replacements, both of which track what a DSP consumer actually receives:
- **per-agent activity** — mean over agents of `stddev(eᵢ)/grand mean`; and
- **inter-agent decorrelation** — mean `|corr(eᵢ, eⱼ)|`, judged against the §3.1 noise floor.

Keep entropy as a secondary descriptive statistic; do not gate on it.

### 3.3 "No limit cycle" needs a recurrence test, not a maximum

The first detector reported the maximum autocorrelation over all lags ≥ 10 s. For any smooth
signal that maximum is always at the shortest lag scanned, so it reported *"limit cycle: 0.909 at
lag 9.0 s"* for a 30-minute run whose scan window merely started at 9 s. It was measuring
smoothness.

A cycle is a **recurrence**: the autocorrelation must first decay (the signal forgets itself), and
only a peak *after* that decay is evidence of repetition. Rewritten that way, the same run reports
0.198–0.200 at ~130–170 s — no short cycle.

---

## 4. Round 2 (2026-09-15) — what the fixed-window statistic revealed

Round 1's verdicts were taken on a statistic that scaled with run length. Round 2 replaced it with
a **fixed 600 s late window** at the 1 Hz sample grid: per-agent activity (`std/grand mean` of each
agent's energy over the window), the frozen count (activity < 0.02) and mean pairwise |corr|, all
over the same window, with one verdict function: *alive* = late activity ≥ 0.10 and ≤ 25 % of the
agents frozen, *cycle* = a post-decay autocorrelation peak > 0.8, else *frozen*. `node run.js
duration` runs the same configuration at 600, 900, 1200, 1800 and 3600 s to show whether the
statistic moves with run length.

**The round-1 "current best configuration" was a decaying transient.** Its whole-run activity of
0.32 came from the first ten minutes. Over any later 600 s window it read 0.04–0.07 with 12–14 of
32 agents frozen, at every run length tried, on three seeds. Round 1's "still moving" verdict at
1800 s was the tail of that transient; its "frozen" verdict at 1200 s was correct.

Three structural defects were behind it, found by inspecting the final state rather than the
statistics:

**4.1 Regrowth was order-biased.** At steady state the pool is empty (agents hold ~96 % of the
budget; the leak trickles back), so cell regrowth is pool-limited — and the regrowth loop handed the
available energy to cells in index order. Cells 0–19 took every joule every step; cells 20–63 had
resource at 1e-234 after 30 minutes. Seventy percent of the habitat was a permanent desert, and any
agent that wandered into it starved and stayed starved. Fix: sum the desired regrowth first and
scale every cell by what the pool can pay. Frozen count went from 12–14 to **0** on its own.

**4.2 Grazing was share-normalised, so appetite never touched the flux.** A cell handed out a fixed
amount per step and split it by demand share, so a lone grazer ate the same whatever its appetite
said; the phase gate only decided who won a contested cell. Fix: each agent asks for
`grazeRate · res · demand` per second and the cell caps the total at what it holds. Appetite and
hunger now set the flux, and a resting agent lets its patch regrow. (`grazeRate` default 1.5 → 0.75:
at 1.5 the field still sat under 3 % full.)

**4.3 Attraction with no short-range repulsion collapsed the population into clumps.** After
30 minutes the 32 agents occupied six distinct positions to two decimals. Co-located agents receive
identical grazing input and produce one modulation signal copied, and a clump that has starved
exerts no affinity force (force ∝ neighbour energy), feels none, and never moves again. Fix: a
kind-independent, energy-independent repulsion inside `crowdingRadius` (0.02). Distinct positions
went from 6 to 31.

Two further mechanisms were added and measured:

- **Foraging** (`forageRate`): movement up the local resource gradient, so movement has a job.
  Load-bearing in 1D (−56 % activity without it); within noise in 2D (+3 %), where the population
  does not clump the same way. Kept as a rule because it is what makes movement *mean* anything
  (path per agent per hour 2.9 with it, 0.85 without) and because Phase 10's habitat will be 2D but
  its agent count and kernel will move under the macros.
- **Refuge floor** (`preyFloor`): an agent can only transfer away what it holds above the floor.
  Without it predation ≥ 0.6 parks a third of the agents at zero — a cliff 0.05 from any predatory
  default. At 0.016 (half the mean per-agent energy) no agent freezes up to predation 0.85.

Two things that were on by default were found to be harmful and are now off:

- **Kuramoto synchronization** at 0.03: aligned phases mean aligned appetites, i.e. the agents feed
  in unison and their energies correlate. Removing it nearly doubled activity in 1D (+86 %) and
  halved correlation; in 2D at the final defaults re-adding it costs 18 % activity. It stays as a
  knob — a Phase-10 *coherence* macro can dial it in deliberately — not as a default rule. The
  roadmap's "synchronize" survives as an *option*, and the spec should say so.
- **Satiation-limited grazing** (`satiation`): costs 31 % activity and raises correlation in 2D.
  Kept as a knob, default 0.

**2D wins.** With the round-2 rules, the 2D torus gives +70 % activity and −38 % correlation over 1D
in the 1D-default ablation, and the final defaults are 2D. It also costs **4× fewer pair
operations** (5.7 M vs 25 M per 30-minute run): neighbourhoods are sparser in two dimensions. In the
final-defaults ablation 1D reads +15 % activity but +14 % correlation, so 1D is livelier and less
individual; the decorrelation and the cost decide it.

---

## 5. Current best configuration (round 2)

Promoted to `defaultConfig()` in `ecosystem-sim.js`. Changes from round 1: `dimensions` 1 → 2,
`grazeRate` 1.5 → 0.75, `predation` 0.5 → 0.55, `syncRate` 0.03 → 0, new `forageRate` 0.01,
`crowding` 0.05 / `crowdingRadius` 0.02, `preyFloor` 0.016, `satiation` 0 (off); proportional
regrowth and demand-scaled grazing are not knobs, they are the corrected rules.

30-minute run, seed 0xC0FFEE: energy drift 2.9e-15 (bounded); **late-window activity 0.44**, 0 of
32 frozen, late pairwise |corr| 0.18; worst post-decay autocorrelation 0.175 at 557 s (no short
cycle); 5.7 M pair ops.

**Duration stability** (three seeds): late activity 0.430 / 0.434 / 0.433 / 0.442 / 0.459 at 600 /
900 / 1200 / 1800 / 3600 s, zero frozen at every length, the same verdict on every seed at every
length. The statistic no longer depends on how long you looked.

**Ablation at the final defaults** (three seeds, 1800 s; effect = change in late activity):

| removed / changed | activity | Δ | corr Δ | reading |
|---|---|---|---|---|
| appetite gate | 0.019 | −96 % | +323 % | **the load-bearing rule**; without it 26 of 32 freeze |
| exchange → diffusive (predation 0.25) | 0.292 | −34 % | +8 % | exchange character matters |
| satiation on (0.06) | 0.307 | −31 % | −15 % | harmful; off by default |
| movement (affinity + forage) | 0.338 | −23 % | +4 % | load-bearing |
| exchange → strongly predatory (0.75) | 0.352 | −20 % | +2 % | over-predation costs activity |
| sync on (0.03, the old default) | 0.361 | −18 % | +9 % | harmful; off by default |
| exchange off (predation 0.5) | 0.385 | −13 % | −7 % | mild predation earns +13 % |
| affinity movement only removed | 0.399 | −10 % | −8 % | |
| refuge floor removed | 0.522 | +18 % | +7 % | the floor costs activity at 0.55; its job is the cliff at ≥ 0.6 |
| 1D habitat | 0.508 | +15 % | +14 % | livelier, less individual, 4× the pair ops |
| forage removed | 0.453 | +3 % | +12 % | within noise in 2D (load-bearing in 1D) |
| crowding removed | 0.433 | −2 % | −2 % | within noise in 2D (decisive in 1D) |
| freq drift removed | 0.453 | +2 % | +3 % | its job is anti-limit-cycle; cheap; kept |

**Seeds** (eight seeds, 900 s): per-agent cross-seed |corr| 0.13 against a within-run floor of 0.12.
Both are at the level unrelated slow signals reach by chance in a 900 s window, so the seed verdict
is now "seed-blind when above 1.5× the floor", not "above the floor" — a comparison of two noisy
estimates should not flip on a hair. Adjacent-seed entropy correlation −0.02. Determinism: same seed,
bit-identical.

**Liveness fuzz, sane box** (`node run.js fuzz 500 sane`): every knob randomised inside the band a
Phase-10 macro would plausibly reach, and every knob asserted to vary across the batch (the fuzzer
exits with a coverage failure otherwise — open problem 7 is structural now). First pass, with the
box still allowing `leakExponent` up to 2 and forcing satiation on: **36.2 % alive, 0 unbounded**.
Death predictors from that pass: `leakExponent` (63 % alive in its lowest tercile → 13 % in the
highest: at per-agent energies of ~0.03 a superlinear leak all but vanishes, agents fill to capacity
and sit), `appetiteDepth` (25 % → 47 %), `kernelSigma` (49 % → 28 %, tighter is livelier),
`predation` (25 % → 43 %, safe now that the floor exists), `leakRate` (25 % → 43 %). The box was
narrowed to `leakExponent` ∈ [1, 1.3], `appetiteDepth` ∈ [0.6, 1], satiation off in 70 % of
configs; second pass: **83.0 % alive, 0 unbounded** (415 of 500). Remaining predictors are gentle slopes, not cliffs: satiation on 91 % → 67 %, grazeRate 89 % → 75 %, kernelSigma 89 % → 78 %, leakExponent 90 % → 79 %; crowding, leakRate, appetiteDepth and preyFloor all favour their upper terciles by ~10 points.

**Boundedness fuzz, hostile box** (`node run.js fuzz 500`): every knob over its full plausible
range including dead-by-construction values, with the round-2 rules (proportional regrowth,
demand-scaled grazing, refuge-floor scaling of exchange): **500 configurations, 0 non-finite,
0 pool-negative, 0 unbounded**; 5.4 % alive, which in a box that includes zero regrowth, zero
grazing and a leak that empties an agent in a second is informational only. Guards 1 and 2 of §1
survive the new rules unchanged; the refuge floor is a stricter form of Guard 2.

---

## 6. Open problems — status after round 2

1. **Which rules survive** — settled by the ablation above. Load-bearing: phase-gated appetite,
   movement (affinity + foraging), exchange with a mildly predatory character behind a refuge floor,
   spatial resource with proportional regrowth and demand-scaled grazing, crowding repulsion. Off
   by default but kept as knobs: synchronization, satiation. Decorative at the final defaults but
   cheap and kept for their 1D / macro-box roles: foraging, crowding, frequency drift. The roadmap's
   "attract / repel / synchronize / exchange" framing survives with one correction: synchronize is
   a macro, not a rule.
2. **Liveness criterion** — settled: fixed-window statistics, verdict stable from 600 s to 3600 s.
3. **Musical calibration of activity** — NOT settled here and cannot be, offline. Late activity 0.44
   means each agent's energy swings ±44 % of the population mean over ten minutes; whether that is
   the right modulation depth is decided by the consumer mapping in Phase 10, where the spec should
   put a listening criterion (krate-render of a Phase-2/3/5 component driven by a recorded agent
   trace) rather than pin a number now.
4. **Movement** — settled: it was inert because of the clumping (4.3), not because it was weak.
   Load-bearing in the final ablation (−23 %).
5. **1D vs 2D** — settled: 2D, on decorrelation and on cost.
6. **The alive island** — widened from 14.6 % (hostile box, round-1 rules) to 36 % (sane box,
   first pass) and to the second-pass figure above with the narrowed box. The spec's Phase-10
   guidance: macros must keep `leakExponent` ≤ 1.3 and `appetiteDepth` ≥ 0.6; predation is safe
   over its whole range because of the refuge floor. Whether a further freeze-resistance mechanism
   is worth its cost is a spec question with the number in hand, not an open one.
7. **Fuzzer coverage** — structural: `assertCoverage()` fails the run if any numeric knob is missing
   or never varies.

---

## 7. Recommendation

Write the Phase 8 spec from this. The rule set to encode is §5's `defaultConfig()` with the round-2
rules (proportional regrowth, demand-scaled grazing, refuge-floor exchange scaling, crowding,
foraging), the two guards of §1 as requirements, and success criteria in the form §3 and §4
established: boundedness under the hostile fuzz; liveness as fixed-window per-agent activity and
frozen count with a duration-stability arm; decorrelation judged against the within-run floor;
recurrence-based cycle detection; a determinism harness; a coverage assertion in the C++ fuzz; and
open problem 3's listening criterion deferred to the consumer.
