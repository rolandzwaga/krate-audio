// =============================================================================
// Vorago Phase 8 prototype — CLI driver
// =============================================================================
//   node run.js trace       [seconds]   Baseline run, CSV trace + summary
//   node run.js duration                Same config at 600..3600 s: is the
//                                       liveness statistic run-length-stable?
//   node run.js ablate      [seconds]   Turn each rule off in turn, 3 seeds;
//                                       effect sizes on the late-window metrics
//   node run.js movement    [seconds]   moveRate x forageRate: does anything
//                                       actually travel, and does it matter?
//   node run.js satiation   [seconds] [forage]
//                                       satiation x grazeRate grid, 3 seeds
//   node run.js dims        [seconds]   1D vs 2D habitat, 5 seeds
//   node run.js fuzz        [configs] [sane]
//                                       Random rule configs; boundedness +
//                                       liveness; every knob asserted varied;
//                                       'sane' restricts to the macro-reachable
//                                       box and prints death predictors
//   node run.js determinism             Same seed twice -> identical; adjacent seeds differ
//   node run.js sweep       [seconds]   Grid over predation x capacity x leakExponent
//   node run.js seeds       [n]         Seed sensitivity: do different seeds diverge?
//
// All output CSV goes to ./out/ next to this script.
// =============================================================================

'use strict';

const fs = require('fs');
const path = require('path');
const sim = require('./ecosystem-sim');

const OUT = path.join(__dirname, 'out');
fs.mkdirSync(OUT, { recursive: true });

const cmd = process.argv[2] || 'trace';
const arg = process.argv[3] && !Number.isNaN(Number(process.argv[3])) ? Number(process.argv[3]) : null;
const flag = process.argv.slice(3).find(a => Number.isNaN(Number(a))) || null;

const SEEDS3 = [0xc0ffee, 0xbeef01, 0x51e7a2];
const SEEDS5 = [0xc0ffee, 0xbeef01, 0x51e7a2, 0x0badf00d, 0x1234abcd];

function fmt(x, d = 4) {
  if (!Number.isFinite(x)) return String(x);
  return x.toFixed(d);
}

// -----------------------------------------------------------------------------
// trace — one baseline run at default config
// -----------------------------------------------------------------------------
function cmdTrace() {
  const seconds = arg || 1800; // 30 min, the roadmap's non-triviality window
  console.log(`# baseline trace: ${seconds}s at default config, seed 0xC0FFEE`);
  const r = sim.run({}, 0xc0ffee, seconds, { trace: true });

  const csv = ['t,total_energy,pool,entropy,max_agent_energy'];
  for (const row of r.trace) {
    csv.push([fmt(row.t, 2), fmt(row.total, 9), fmt(row.pool, 6),
              fmt(row.entropy, 6), fmt(row.maxAgent, 6)].join(','));
  }
  fs.writeFileSync(path.join(OUT, 'baseline_trace.csv'), csv.join('\n'));

  // Per-agent energy at 1 Hz, for eyeballing who is alive when.
  const ec = ['t,' + r.trace[0].energies.map((_, i) => `agent${i}`).join(',')];
  for (const row of r.trace) {
    ec.push([fmt(row.t, 2), ...row.energies.map(v => fmt(v, 6))].join(','));
  }
  fs.writeFileSync(path.join(OUT, 'baseline_agents.csv'), ec.join('\n'));

  report(r);
  console.log(`\nwrote out/baseline_trace.csv, out/baseline_agents.csv`);
}

function report(r) {
  const maxH = Math.log2(r.sim.cfg.agentCount);
  console.log(`  non-finite            : ${r.nonFinite}`);
  console.log(`  total energy  min/max : ${fmt(r.minTotal, 9)} / ${fmt(r.maxTotal, 9)}  (budget 1.0)`);
  console.log(`  energy drift          : ${r.energyDrift.toExponential(3)}  ${r.bounded ? '[BOUNDED]' : '[LEAK/GROWTH]'}`);
  console.log(`  max agent energy      : ${fmt(r.maxAgent, 6)}`);
  console.log(`  activity whole / late : ${fmt(r.agentActivity)} / ${fmt(r.lateActivity)}  (late window ${fmt(r.lateWindowSeconds, 0)} s)`);
  console.log(`  frozen agents  late   : ${r.lateFrozen} / ${r.sim.cfg.agentCount}`);
  console.log(`  pair |corr| whole/late: ${fmt(r.meanAgentPairCorr)} / ${fmt(r.latePairCorr)}`);
  console.log(`  path per agent per h  : ${fmt(r.meanPathPerHour, 3)} habitat units   resource fill ${fmt(r.meanResourceFill, 3)}`);
  console.log(`  entropy mean/std      : ${fmt(r.entropyMean)} / ${fmt(r.entropyStd)}   (max possible ${fmt(maxH, 3)}; descriptive only)`);
  console.log(`  worst autocorr        : ${fmt(r.worstAutocorr, 3)} at lag ${fmt(r.worstAutocorrLagSeconds, 1)}s ${r.worstAutocorr > sim.kCycleAutocorr ? '[LIMIT CYCLE?]' : '[no short cycle]'}`);
  console.log(`  verdict               : ${sim.liveness(r)}`);
  console.log(`  pair ops              : ${r.pairOps.toLocaleString()}`);
}

// -----------------------------------------------------------------------------
// duration — is the liveness statistic stable in the run length?
// -----------------------------------------------------------------------------
function cmdDuration() {
  const durations = [600, 900, 1200, 1800, 3600];
  console.log(`# duration stability: default config, seeds ${SEEDS3.map(s => s.toString(16)).join(' ')}`);
  console.log(`# late window ${sim.kLateWindowSeconds} s; a stable statistic changes little across rows\n`);
  const head = 'seconds'.padEnd(9) + 'whole act'.padStart(10) + 'late act'.padStart(10) +
               'late frozen'.padStart(12) + 'late corr'.padStart(10) + 'H late std'.padStart(11) +
               '  verdicts';
  console.log(head);
  console.log('-'.repeat(head.length + 12));
  const rows = ['seconds,seed,whole_activity,late_activity,late_frozen,late_corr,entropy_late_std,verdict'];
  for (const s of durations) {
    const runs = SEEDS3.map(seed => sim.run({}, seed, s, {}));
    for (let i = 0; i < runs.length; i++) {
      const r = runs[i];
      rows.push([s, SEEDS3[i], fmt(r.agentActivity), fmt(r.lateActivity), r.lateFrozen,
                 fmt(r.latePairCorr), fmt(r.entropyLateStd), sim.liveness(r)].join(','));
    }
    console.log(
      String(s).padEnd(9) +
      fmt(sim.mean(runs.map(r => r.agentActivity))).padStart(10) +
      fmt(sim.mean(runs.map(r => r.lateActivity))).padStart(10) +
      fmt(sim.mean(runs.map(r => r.lateFrozen)), 1).padStart(12) +
      fmt(sim.mean(runs.map(r => r.latePairCorr))).padStart(10) +
      fmt(sim.mean(runs.map(r => r.entropyLateStd))).padStart(11) +
      '  ' + runs.map(r => sim.liveness(r)).join(' '));
  }
  fs.writeFileSync(path.join(OUT, 'duration.csv'), rows.join('\n'));
  console.log(`\nwrote out/duration.csv`);
}

// -----------------------------------------------------------------------------
// ablate — which rules actually earn their place? Three seeds, effect sizes
// on the late-window metrics against the baseline.
// -----------------------------------------------------------------------------
function ablationVariants() {
  const d = sim.defaultConfig();
  return [
    ['baseline',           {}],
    ['no exchange',        { exchangeRate: 0 }],
    ['exchange pred .25',  { predation: 0.25 }],
    ['exchange pred .75',  { predation: 0.75 }],
    ['no movement',        { moveRate: 0, forageRate: 0 }],
    ['no affinity move',   { moveRate: 0 }],
    ['no forage',          { forageRate: 0 }],
    ['no crowding',        { crowding: 0 }],
    ['satiation .06',      { satiation: 0.06 }],
    ['sync .03 (old dflt)', { syncRate: 0.03 }],
    ['1D habitat',         { dimensions: 1 }],
    ['no refuge floor',    { preyFloor: 0 }],
    ['no freq drift',      { freqDrift: 0 }],
    ['no appetite gate',   { appetiteDepth: 0 }],
    ['sync+no gate',       { syncRate: 0.03, appetiteDepth: 0 }],
  ].filter(([name]) => !(name === 'no forage' && d.forageRate === 0));
}

function cmdAblate() {
  const seconds = arg || 1800;
  const variants = ablationVariants();
  console.log(`# ablation: ${seconds}s each, seeds ${SEEDS3.map(s => s.toString(16)).join(' ')}; late window ${sim.kLateWindowSeconds} s\n`);
  const head = 'variant'.padEnd(20) + 'late act'.padStart(9) + 'd%'.padStart(7) +
               'frozen'.padStart(8) + 'late corr'.padStart(10) + 'd%'.padStart(7) +
               'path/h'.padStart(8) + 'ac'.padStart(7) + '  verdicts';
  console.log(head);
  console.log('-'.repeat(head.length + 14));

  const rows = ['variant,late_activity,activity_delta_pct,late_frozen,late_corr,corr_delta_pct,path_per_hour,worst_ac,verdicts'];
  let base = null;
  for (const [name, over] of variants) {
    const runs = SEEDS3.map(seed => sim.run(over, seed, seconds, {}));
    const act = sim.mean(runs.map(r => r.lateActivity));
    const frozen = sim.mean(runs.map(r => r.lateFrozen));
    const corr = sim.mean(runs.map(r => r.latePairCorr));
    const pathH = sim.mean(runs.map(r => r.meanPathPerHour));
    const ac = sim.mean(runs.map(r => r.worstAutocorr));
    const verdicts = runs.map(r => sim.liveness(r)).join(' ');
    if (!base) base = { act, corr };
    const dAct = 100 * (act - base.act) / base.act;
    const dCorr = 100 * (corr - base.corr) / base.corr;
    console.log(
      name.padEnd(20) + fmt(act, 3).padStart(9) + fmt(dAct, 0).padStart(7) +
      fmt(frozen, 1).padStart(8) + fmt(corr, 3).padStart(10) + fmt(dCorr, 0).padStart(7) +
      fmt(pathH, 2).padStart(8) + fmt(ac, 2).padStart(7) + '  ' + verdicts);
    rows.push([name, fmt(act), fmt(dAct, 1), fmt(frozen, 2), fmt(corr), fmt(dCorr, 1),
               fmt(pathH, 3), fmt(ac, 3), verdicts].join(','));
  }
  fs.writeFileSync(path.join(OUT, 'ablation.csv'), rows.join('\n'));
  console.log(`\n  d% = change against baseline (late activity: more is livelier; late corr: less is more individual)`);
  console.log(`  A rule EARNS ITS PLACE when removing it moves activity or correlation by >= 20 % across seeds.`);
  console.log(`\nwrote out/ablation.csv`);
}

// -----------------------------------------------------------------------------
// movement — moveRate x forageRate: does anything travel, and does it matter?
// -----------------------------------------------------------------------------
function cmdMovement() {
  const seconds = arg || 1800;
  const moveRates = [0, 0.2, 1.0];
  const forageRates = [0, 0.001, 0.003, 0.01, 0.03];
  console.log(`# movement: moveRate x forageRate, ${seconds}s, seeds ${SEEDS3.map(s => s.toString(16)).join(' ')}\n`);
  const head = 'move'.padEnd(6) + 'forage'.padEnd(8) + 'path/h'.padStart(8) + 'late act'.padStart(10) +
               'frozen'.padStart(8) + 'late corr'.padStart(10) + 'fill'.padStart(7) + '  verdicts';
  console.log(head);
  console.log('-'.repeat(head.length + 14));
  const rows = ['move_rate,forage_rate,path_per_hour,late_activity,late_frozen,late_corr,resource_fill,verdicts'];
  for (const m of moveRates) {
    for (const f of forageRates) {
      const runs = SEEDS3.map(seed => sim.run({ moveRate: m, forageRate: f }, seed, seconds, {}));
      const pathH = sim.mean(runs.map(r => r.meanPathPerHour));
      const act = sim.mean(runs.map(r => r.lateActivity));
      const frozen = sim.mean(runs.map(r => r.lateFrozen));
      const corr = sim.mean(runs.map(r => r.latePairCorr));
      const fill = sim.mean(runs.map(r => r.meanResourceFill));
      const verdicts = runs.map(r => sim.liveness(r)).join(' ');
      console.log(String(m).padEnd(6) + String(f).padEnd(8) + fmt(pathH, 2).padStart(8) +
                  fmt(act, 3).padStart(10) + fmt(frozen, 1).padStart(8) + fmt(corr, 3).padStart(10) +
                  fmt(fill, 3).padStart(7) + '  ' + verdicts);
      rows.push([m, f, fmt(pathH, 3), fmt(act), fmt(frozen, 2), fmt(corr), fmt(fill, 3), verdicts].join(','));
    }
  }
  fs.writeFileSync(path.join(OUT, 'movement.csv'), rows.join('\n'));
  console.log(`\n  path/h = mean habitat units travelled per agent per hour (1.0 = once around the torus)`);
  console.log(`\nwrote out/movement.csv`);
}

// -----------------------------------------------------------------------------
// satiation — satiation x grazeRate: can the field survive its grazers?
// -----------------------------------------------------------------------------
function cmdSatiation() {
  const seconds = arg || 1800;
  const satiations = [0, 0.03, 0.06, 0.1, 0.2];
  const grazeRates = [0.3, 0.75, 1.5];
  const forage = flag === 'forage' ? 0.01 : 0;
  console.log(`# satiation x grazeRate, forageRate ${forage}, ${seconds}s, seeds ${SEEDS3.map(s => s.toString(16)).join(' ')}\n`);
  const head = 'satiat'.padEnd(8) + 'graze'.padEnd(7) + 'late act'.padStart(10) + 'frozen'.padStart(8) +
               'late corr'.padStart(10) + 'fill'.padStart(7) + 'path/h'.padStart(8) + 'ac'.padStart(6) + '  verdicts';
  console.log(head);
  console.log('-'.repeat(head.length + 14));
  const rows = ['satiation,graze_rate,forage_rate,late_activity,late_frozen,late_corr,resource_fill,path_per_hour,worst_ac,verdicts'];
  for (const s of satiations) {
    for (const g of grazeRates) {
      const runs = SEEDS3.map(seed => sim.run({ satiation: s, grazeRate: g, forageRate: forage }, seed, seconds, {}));
      const act = sim.mean(runs.map(r => r.lateActivity));
      const frozen = sim.mean(runs.map(r => r.lateFrozen));
      const corr = sim.mean(runs.map(r => r.latePairCorr));
      const fill = sim.mean(runs.map(r => r.meanResourceFill));
      const pathH = sim.mean(runs.map(r => r.meanPathPerHour));
      const ac = sim.mean(runs.map(r => r.worstAutocorr));
      const verdicts = runs.map(r => sim.liveness(r)).join(' ');
      console.log(String(s).padEnd(8) + String(g).padEnd(7) + fmt(act, 3).padStart(10) + fmt(frozen, 1).padStart(8) +
                  fmt(corr, 3).padStart(10) + fmt(fill, 3).padStart(7) + fmt(pathH, 2).padStart(8) +
                  fmt(ac, 2).padStart(6) + '  ' + verdicts);
      rows.push([s, g, forage, fmt(act), fmt(frozen, 2), fmt(corr), fmt(fill, 3), fmt(pathH, 3), fmt(ac, 3), verdicts].join(','));
    }
  }
  fs.writeFileSync(path.join(OUT, `satiation${forage ? '_forage' : ''}.csv`), rows.join('\n'));
  console.log(`\nwrote out/satiation${forage ? '_forage' : ''}.csv`);
}

// -----------------------------------------------------------------------------
// dims — 1D vs 2D habitat, five seeds
// -----------------------------------------------------------------------------
function cmdDims() {
  const seconds = arg || 1800;
  console.log(`# habitat dimensions: ${seconds}s, seeds ${SEEDS5.map(s => s.toString(16)).join(' ')}\n`);
  const head = 'dims'.padEnd(6) + 'late act'.padStart(10) + 'frozen'.padStart(8) + 'late corr'.padStart(10) +
               'path/h'.padStart(8) + 'fill'.padStart(7) + 'pair ops'.padStart(14) + '  verdicts';
  console.log(head);
  console.log('-'.repeat(head.length + 20));
  const rows = ['dims,seed,late_activity,late_frozen,late_corr,path_per_hour,resource_fill,pair_ops,verdict'];
  for (const dims of [1, 2]) {
    const runs = SEEDS5.map(seed => sim.run({ dimensions: dims }, seed, seconds, {}));
    for (let i = 0; i < runs.length; i++) {
      const r = runs[i];
      rows.push([dims, SEEDS5[i], fmt(r.lateActivity), r.lateFrozen, fmt(r.latePairCorr),
                 fmt(r.meanPathPerHour, 3), fmt(r.meanResourceFill, 3), r.pairOps, sim.liveness(r)].join(','));
    }
    console.log(String(dims).padEnd(6) +
      fmt(sim.mean(runs.map(r => r.lateActivity)), 3).padStart(10) +
      fmt(sim.mean(runs.map(r => r.lateFrozen)), 1).padStart(8) +
      fmt(sim.mean(runs.map(r => r.latePairCorr)), 3).padStart(10) +
      fmt(sim.mean(runs.map(r => r.meanPathPerHour)), 2).padStart(8) +
      fmt(sim.mean(runs.map(r => r.meanResourceFill)), 3).padStart(7) +
      Math.round(sim.mean(runs.map(r => r.pairOps))).toLocaleString().padStart(14) +
      '  ' + runs.map(r => sim.liveness(r)).join(' '));
  }
  fs.writeFileSync(path.join(OUT, 'dims.csv'), rows.join('\n'));
  console.log(`\nwrote out/dims.csv`);
}

// -----------------------------------------------------------------------------
// fuzz — random rule configurations, the roadmap's boundedness criterion
// -----------------------------------------------------------------------------
function randomAffinity(rng, lo, hi) {
  const A = [];
  for (let i = 0; i < sim.KIND_COUNT; i++) {
    A.push([]);
    for (let j = 0; j < sim.KIND_COUNT; j++) A[i].push(rng.range(lo, hi));
  }
  // Symmetrise: an asymmetric affinity matrix means i pulls j while j pushes i,
  // which is a momentum source. Positions are bounded by the torus anyway, but
  // the C++ component should make this structural too.
  for (let i = 0; i < sim.KIND_COUNT; i++)
    for (let j = i + 1; j < sim.KIND_COUNT; j++) A[j][i] = A[i][j];
  return A;
}

// The HOSTILE box: every knob over its full plausible range, including values
// that are dead by construction (no regrowth, no grazing, a leak that empties
// an agent in a second). This is the BOUNDEDNESS fuzz; liveness in this box
// is informational.
function randomConfig(rng) {
  return {
    agentCount: Math.round(rng.range(24, 48)),
    dimensions: rng.nextUnipolar() < 0.5 ? 1 : 2,
    energyBudget: 1.0,
    initialPoolFraction: rng.range(0.1, 0.9),
    kernelSigma: rng.range(0.01, 0.35),
    exchangeRate: rng.range(0.0, 3.0),
    predation: rng.range(0.0, 1.0),
    capacity: rng.range(0.01, 1.0),
    leakExponent: rng.range(1.0, 2.5),
    moveRate: rng.range(0.0, 0.5),
    maxSpeed: rng.range(0.001, 0.05),
    forageRate: rng.range(0.0, 0.05),
    syncRate: rng.range(0.0, 0.5),
    resourceCells: Math.round(rng.range(8, 96)),
    cellCapacity: rng.range(0.005, 0.2),
    regenRate: rng.range(0.0, 1.0),
    grazeRate: rng.range(0.0, 3.0),
    feedRate: rng.range(0.0, 1.0),
    leakRate: rng.range(0.0, 1.0),
    freqLo: rng.range(0.0005, 0.005),
    freqHi: rng.range(0.006, 0.05),
    freqDrift: rng.range(0, 0.0002),
    appetiteDepth: rng.range(0, 1),
    satiation: rng.range(0.0, 0.5),
    crowding: rng.range(0.0, 0.2),
    crowdingRadius: rng.range(0.005, 0.05),
    preyFloor: rng.range(0.0, 0.05),
    affinity: randomAffinity(rng, -2.0, 2.0),
  };
}

// The SANE box: the region Phase 10's concept macros would plausibly reach —
// every knob within a musically reasonable band around the default, nothing
// dead by construction. This is the LIVENESS fuzz: the fraction alive here is
// the number that tells Phase 10 whether it needs to constrain its macros.
function randomConfigSane(rng) {
  const d = sim.defaultConfig();
  return {
    agentCount: Math.round(rng.range(24, 48)),
    dimensions: 2,
    energyBudget: 1.0,
    initialPoolFraction: rng.range(0.3, 0.7),
    kernelSigma: rng.range(0.02, 0.06),
    exchangeRate: rng.range(0.0, 1.0),
    predation: rng.range(0.0, 1.0),
    capacity: rng.range(0.05, 1.0),
    // leakExponent > ~1.3 kills: at per-agent energies of ~0.03 a superlinear
    // leak all but vanishes, agents fill to capacity and sit (sane fuzz round
    // 2: 63 % alive at 1.0-1.33, 13 % at 1.65-2.0). Macros stay in [1, 1.3].
    leakExponent: rng.range(1.0, 1.3),
    moveRate: rng.range(0.0, 0.5),
    maxSpeed: rng.range(0.01, 0.05),
    forageRate: rng.range(0.0, Math.max(d.forageRate * 3, 0.01)),
    syncRate: rng.range(0.0, 0.1),
    resourceCells: Math.round(rng.range(32, 96)),
    cellCapacity: rng.range(0.02, 0.1),
    regenRate: rng.range(0.02, 0.2),
    grazeRate: rng.range(0.5, 3.0),
    feedRate: 0.0,
    leakRate: rng.range(0.02, 0.15),
    freqLo: rng.range(0.001, 0.003),
    freqHi: rng.range(0.01, 0.03),
    freqDrift: rng.range(0, 0.0001),
    // A shallow gate starves the dynamics (25 % alive at 0.4-0.6 vs 47 % at
    // 0.8-1.0 in sane fuzz round 2): the gate is THE load-bearing rule.
    appetiteDepth: rng.range(0.6, 1.0),
    // Satiation is OFF by default and cost 31 % activity in ablation; the box
    // leaves it off in most configs and probes a mild setting in the rest.
    satiation: rng.nextUnipolar() < 0.7 ? 0.0 : rng.range(0.02, 0.06),
    crowding: rng.range(Math.max(d.crowding * 0.5, 0.01), Math.max(d.crowding * 2, 0.05)),
    crowdingRadius: rng.range(0.01, 0.04),
    preyFloor: rng.range(0.002, 0.02),
    affinity: randomAffinity(rng, -1.5, 1.5),
  };
}

// EVERY knob must be randomised. The first version of the fuzzer silently
// omitted predation, capacity, leakExponent and the whole resource field, so
// predation sat at its 0.5 default — the exact value that zeroes the exchange
// rule — and reported "0 unbounded / 1000" while never visiting the regime
// already known to break conservation. This check makes that structural: a
// numeric key of defaultConfig() that is missing from the generated configs,
// or that never varies across the batch, fails the fuzz run outright.
// (feedRate is pinned to 0 in the sane box by design and is exempt there.)
function assertCoverage(configs, exempt) {
  const keys = Object.keys(sim.defaultConfig()).filter(k => k !== 'affinity');
  const problems = [];
  for (const k of keys) {
    if (exempt.includes(k)) continue;
    const vals = configs.map(c => c[k]);
    if (vals.some(v => v === undefined)) { problems.push(`${k}: missing from generated config`); continue; }
    const lo = Math.min(...vals), hi = Math.max(...vals);
    if (!(hi > lo)) problems.push(`${k}: never varies (${lo})`);
  }
  return problems;
}

function cmdFuzz() {
  const count = arg || 1000;
  const sane = flag === 'sane';
  const seconds = 900; // >= late window + settle
  const gen = sane ? randomConfigSane : randomConfig;
  console.log(`# fuzz${sane ? ' (SANE box)' : ' (hostile box)'}: ${count} random rule configs x ${seconds}s`);
  const meta = new sim.Xorshift32(sane ? 0x5a5e0001 : 0xf0f0f0);

  const configs = [], seeds = [];
  for (let k = 0; k < count; k++) {
    const cfgSeed = meta.next();
    seeds.push(cfgSeed);
    configs.push(gen(new sim.Xorshift32(cfgSeed)));
  }
  const coverage = assertCoverage(configs, sane ? ['feedRate', 'dimensions', 'energyBudget'] : ['energyBudget']);
  if (coverage.length) {
    console.log('  COVERAGE FAILURE — the fuzzer proves nothing about these knobs:');
    for (const p of coverage) console.log('    ' + p);
    process.exit(2);
  }
  console.log(`  coverage              : every knob varied across the batch [OK]`);

  let unbounded = 0, nonFinite = 0, poolNeg = 0;
  const verdictCount = { alive: 0, frozen: 0, cycle: 0, unbounded: 0 };
  const worst = { drift: 0, seed: 0, predation: 0 };
  const numericKeys = Object.keys(sim.defaultConfig()).filter(k => k !== 'affinity' && k !== 'energyBudget');
  const rows = ['config,seed,bounded,pool_negative,drift,late_activity,late_frozen,late_corr,path_per_hour,resource_fill,worst_ac,verdict,' + numericKeys.join(',')];
  const results = [];

  for (let k = 0; k < count; k++) {
    const cfg = configs[k];
    const r = sim.run(cfg, seeds[k], seconds, { sampleEvery: 94 }); // 1 Hz
    if (r.nonFinite) nonFinite++;
    if (r.poolWentNegative) poolNeg++;
    if (!r.bounded) {
      unbounded++;
      if (r.energyDrift > worst.drift) {
        worst.drift = r.energyDrift; worst.seed = seeds[k]; worst.predation = cfg.predation;
      }
    }
    const v = sim.liveness(r);
    verdictCount[v]++;
    results.push({ cfg, v, r });
    rows.push([k, seeds[k], r.bounded ? 1 : 0, r.poolWentNegative ? 1 : 0, r.energyDrift.toExponential(3),
               fmt(r.lateActivity), r.lateFrozen, fmt(r.latePairCorr), fmt(r.meanPathPerHour, 3),
               fmt(r.meanResourceFill, 3), fmt(r.worstAutocorr, 3), v,
               ...numericKeys.map(key => fmt(cfg[key], 5))].join(','));
    if ((k + 1) % 100 === 0) process.stdout.write(`  ${k + 1}/${count}\r`);
  }
  fs.writeFileSync(path.join(OUT, sane ? 'fuzz_sane_results.csv' : 'fuzz_results.csv'), rows.join('\n'));

  console.log(`\n  configs               : ${count}`);
  console.log(`  non-finite            : ${nonFinite}   ${nonFinite === 0 ? '[OK]' : '[FAIL]'}`);
  console.log(`  pool went negative    : ${poolNeg}`);
  console.log(`  unbounded             : ${unbounded}   ${unbounded === 0 ? '[OK]' : '[FAIL]'}`);
  if (unbounded) console.log(`    worst drift ${worst.drift.toExponential(3)} at seed ${worst.seed} (predation ${fmt(worst.predation,3)})`);
  console.log(`  --- liveness split (late window ${sim.kLateWindowSeconds} s) ---`);
  for (const v of ['alive', 'frozen', 'cycle', 'unbounded']) {
    console.log(`  ${v.padEnd(22)}: ${String(verdictCount[v]).padStart(5)}  (${(100 * verdictCount[v] / count).toFixed(1)}%)`);
  }

  // Death predictors: for each knob, the alive fraction in its lowest and
  // highest tercile. A knob whose terciles differ a lot is one the macros must
  // respect; one whose terciles agree is not what kills configurations.
  const alive = results.filter(x => x.v === 'alive').length;
  if (alive > 0 && alive < count) {
    console.log(`\n  death predictors (alive fraction, lowest tercile -> highest tercile of each knob):`);
    const preds = [];
    for (const key of numericKeys) {
      const sorted = results.slice().sort((a, b) => a.cfg[key] - b.cfg[key]);
      const t = Math.floor(sorted.length / 3);
      if (t < 5) continue;
      const lo = sorted.slice(0, t), hi = sorted.slice(sorted.length - t);
      const fLo = lo.filter(x => x.v === 'alive').length / t;
      const fHi = hi.filter(x => x.v === 'alive').length / t;
      preds.push({ key, fLo, fHi, gap: Math.abs(fHi - fLo), loRange: [sorted[0].cfg[key], sorted[t - 1].cfg[key]], hiRange: [sorted[sorted.length - t].cfg[key], sorted[sorted.length - 1].cfg[key]] });
    }
    preds.sort((a, b) => b.gap - a.gap);
    for (const p of preds) {
      console.log(`    ${p.key.padEnd(20)} ${(100 * p.fLo).toFixed(0).padStart(4)}% [${fmt(p.loRange[0], 3)}..${fmt(p.loRange[1], 3)}]  ->  ${(100 * p.fHi).toFixed(0).padStart(4)}% [${fmt(p.hiRange[0], 3)}..${fmt(p.hiRange[1], 3)}]`);
    }
  }
  console.log(`\nwrote out/${sane ? 'fuzz_sane_results.csv' : 'fuzz_results.csv'}`);
}

// -----------------------------------------------------------------------------
// determinism
// -----------------------------------------------------------------------------
function cmdDeterminism() {
  console.log('# determinism');
  const a = sim.run({}, 12345, 300, {});
  const b = sim.run({}, 12345, 300, {});
  let identical = a.hSeries.length === b.hSeries.length;
  if (identical) {
    for (let i = 0; i < a.hSeries.length; i++) {
      if (a.hSeries[i] !== b.hSeries[i]) { identical = false; break; }
    }
  }
  console.log(`  same seed, bit-identical entropy series : ${identical} ${identical ? '[OK]' : '[FAIL]'}`);

  const c = sim.run({}, 12346, 300, {});
  const corr = correlation(a.hSeries, c.hSeries);
  console.log(`  adjacent seed correlation               : ${fmt(corr, 4)} ${Math.abs(corr) < 0.5 ? '[decorrelated]' : '[SUSPICIOUS]'}`);
}

function correlation(a, b) {
  const n = Math.min(a.length, b.length);
  const ma = sim.mean(a.slice(0, n)), mb = sim.mean(b.slice(0, n));
  let num = 0, da = 0, db = 0;
  for (let i = 0; i < n; i++) {
    num += (a[i] - ma) * (b[i] - mb);
    da += (a[i] - ma) ** 2;
    db += (b[i] - mb) ** 2;
  }
  return da > 0 && db > 0 ? num / Math.sqrt(da * db) : 0;
}

// -----------------------------------------------------------------------------
// sweep — locate the regime that is alive rather than flat or frozen
// -----------------------------------------------------------------------------
function cmdSweep() {
  const seconds = arg || 900;
  const predations = [0.5, 0.6, 0.7, 0.8, 0.9, 1.0];
  const capacities = [0.02, 0.04, 0.08, 0.15, 1.0];
  const leakExps = [1.0, 1.5, 2.0];
  console.log(`# sweep: predation x capacity x leakExponent, ${seconds}s each, 3 seeds each\n`);

  const rows = ['predation,capacity,leak_exp,late_activity,late_frozen,late_corr,worst_ac,ac_lag_s,seed_corr,bounded'];
  const results = [];
  for (const p of predations) {
    for (const cap of capacities) {
      for (const le of leakExps) {
        const over = { predation: p, capacity: cap, leakExponent: le };
        const runs = SEEDS3.map(s => sim.run(over, s, seconds, { sampleEvery: 94 }));
        const ok = runs.every(r => r.bounded && !r.nonFinite);
        const act = sim.mean(runs.map(r => r.lateActivity));
        const frozen = sim.mean(runs.map(r => r.lateFrozen));
        const corr = sim.mean(runs.map(r => r.latePairCorr));
        const ac = sim.mean(runs.map(r => r.worstAutocorr));
        const acLag = sim.mean(runs.map(r => r.worstAutocorrLagSeconds));
        // Seed sensitivity: mean pairwise correlation between seeds. LOW is good.
        const sc = (Math.abs(correlation(runs[0].hSeries, runs[1].hSeries)) +
                    Math.abs(correlation(runs[0].hSeries, runs[2].hSeries)) +
                    Math.abs(correlation(runs[1].hSeries, runs[2].hSeries))) / 3;
        const verdicts = runs.map(r => sim.liveness(r));
        results.push({ p, cap, le, act, frozen, corr, ac, acLag, sc, ok, verdicts });
        rows.push([p, cap, le, fmt(act), fmt(frozen, 2), fmt(corr), fmt(ac, 3),
                   fmt(acLag, 1), fmt(sc, 3), ok ? 1 : 0].join(','));
      }
    }
  }
  fs.writeFileSync(path.join(OUT, 'sweep.csv'), rows.join('\n'));

  console.log('pred  cap    leak   late act  frozen  late corr  worst ac  seed corr  verdicts');
  console.log('-'.repeat(84));
  for (const r of results) {
    console.log(
      String(r.p).padEnd(6) + String(r.cap).padEnd(7) + String(r.le).padEnd(7) +
      fmt(r.act, 3).padStart(9) + fmt(r.frozen, 1).padStart(8) +
      fmt(r.corr, 3).padStart(11) + fmt(r.ac, 3).padStart(10) +
      fmt(r.sc, 3).padStart(11) + '  ' + r.verdicts.join(' '));
  }
  const candidates = results.filter(r => r.ok && r.verdicts.every(v => v === 'alive'));
  console.log(`\n  alive on all three seeds: ${candidates.length}/${results.length}`);
  console.log(`\nwrote out/sweep.csv`);
}

// -----------------------------------------------------------------------------
// seeds — do per-voice seeds actually produce different voices?
// -----------------------------------------------------------------------------
function cmdSeeds() {
  const n = arg || 8;
  const seconds = 900;
  console.log(`# seed sensitivity: ${n} seeds x ${seconds}s at default config`);
  const runs = [];
  const rng = new sim.Xorshift32(0x5EED);
  for (let i = 0; i < n; i++) runs.push(sim.run({}, rng.next(), seconds, { sampleEvery: 94 }));

  // Correlate PER-AGENT trajectories, not entropy. Entropy is permutation-
  // invariant, so two seeds can share an entropy curve while every agent's
  // actual modulation output differs — and agent i drives target i in both
  // voices, so agent-wise comparison is the one that answers "would these two
  // voices sound alike?". Measuring this on entropy reported 0.85 (seed-blind)
  // for a configuration whose per-agent signals are far less alike.
  const agentCount = runs[0].agentSeries[0].length;
  function agentCol(run, i) { return run.agentSeries.map(row => row[i]); }

  let sum = 0, count = 0, worst = 0, hSum = 0, hCount = 0;
  for (let i = 0; i < n; i++) {
    for (let j = i + 1; j < n; j++) {
      hSum += Math.abs(correlation(runs[i].hSeries, runs[j].hSeries)); hCount++;
      let perAgent = 0;
      for (let a = 0; a < agentCount; a++) {
        perAgent += Math.abs(correlation(agentCol(runs[i], a), agentCol(runs[j], a)));
      }
      perAgent /= agentCount;
      sum += perAgent; count++;
      if (perAgent > worst) worst = perAgent;
    }
  }
  // NOISE FLOOR. These signals are slow (decorrelation ~150 s), so a 900 s
  // window holds only a handful of INDEPENDENT samples and |correlation|
  // between genuinely unrelated slow signals runs high by chance — measured
  // 0.61 at 900 s, 0.50 at 3600 s. An absolute threshold is therefore
  // meaningless: judging against 0.5 declared this configuration "seed-blind"
  // when its cross-seed correlation was BELOW the floor. The reference is
  // different agents within the SAME run, which are as unrelated as two
  // signals in this system ever get.
  let floor = 0, floorCount = 0;
  const c0 = [];
  for (let a = 0; a < agentCount; a++) c0.push(agentCol(runs[0], a));
  for (let a = 0; a < agentCount; a++) {
    for (let b = a + 1; b < agentCount; b++) {
      floor += Math.abs(correlation(c0[a], c0[b])); floorCount++;
    }
  }
  const noiseFloor = floor / floorCount;

  const meanCorr = sum / count;
  // The floor is itself an estimate from the same short window, so "at or
  // below" is a comparison of two noisy numbers (round 2: 0.13 vs 0.12 at the
  // final defaults, both at the level unrelated slow signals reach by chance).
  // Seed-blindness is a MULTIPLE of the floor, not a hair above it.
  const kSeedBlindFactor = 1.5;
  console.log(`  mean |corr| PER-AGENT between seeds : ${fmt(meanCorr, 4)}`);
  console.log(`  worst pair                          : ${fmt(worst, 4)}`);
  console.log(`  noise floor (diff agents, same run) : ${fmt(noiseFloor, 4)}`);
  console.log(`  verdict                             : ${meanCorr <= noiseFloor * kSeedBlindFactor ? '[voices differ — within 1.5x the floor]' : '[SEED-BLIND — above 1.5x the floor]'}`);
  console.log(`  (entropy-based figure, for contrast) : ${fmt(hSum / hCount, 4)}  <- permutation-invariant, misleading`);
  console.log(`  entropy mean spread across seeds    : ${fmt(Math.min(...runs.map(r => r.entropyMean)), 3)} .. ${fmt(Math.max(...runs.map(r => r.entropyMean)), 3)}`);
}

// -----------------------------------------------------------------------------
switch (cmd) {
  case 'trace': cmdTrace(); break;
  case 'duration': cmdDuration(); break;
  case 'ablate': cmdAblate(); break;
  case 'movement': cmdMovement(); break;
  case 'satiation': cmdSatiation(); break;
  case 'dims': cmdDims(); break;
  case 'fuzz': cmdFuzz(); break;
  case 'determinism': cmdDeterminism(); break;
  case 'sweep': cmdSweep(); break;
  case 'seeds': cmdSeeds(); break;
  default:
    console.error(`unknown command: ${cmd}`);
    process.exit(1);
}
