#!/usr/bin/env node
//
// diff-midi-golden.js
// =============================================================================
// Diff a captured arp MIDI dump against its golden and explain every delta.
//
// Both files use the dump format of the arp golden harnesses
// ("[<sample>] noteOn  <pitch> <velocity>" / "[<sample>] noteOff <pitch>").
// Events are aligned by a longest-common-subsequence match in which two events
// are "the same" when kind, pitch and velocity are equal and the timestamps are
// within the cross-toolchain tolerance of tests/test_helpers/midi_golden_compare.h
// (8 samples). Everything left over is a delta, printed as hunks.
//
// Each hunk is then classified against the signature of the truncating PPQ
// resync that commit 8be7406d replaced with std::llround: at a block boundary
// the arp computed a position one sample short, so one step fired twice, one
// sample apart. That leaves, in the OLD golden:
//
//   doubled step   two golden events exactly one sample apart, the later one
//                  golden-only (the spurious second firing of the step).
//   spurious early step
//                  the second firing advanced the pattern by one step (also
//                  when it hit a rest and left no visible pair), so from there
//                  on the golden plays each pattern step one step length EARLY:
//                  golden-only events, shifted later by one step length, match
//                  captured-only events (e.g. 170888 + 5512 = 176400). Each
//                  further spurious step adds one more step of shift.
//   edge           an event left over within one step length of where a shift
//                  starts (the step the golden skipped), of an input change
//                  (the held notes differ across it) or of the end of the run.
//
// The signature classifier cannot follow a preset whose later steps depend on
// the step count (a random lane, humanize or a chaos source draws per step, so
// after one spurious step every later draw differs; swing parity flips). For
// those, --ab gives the causal check: <oldCodeDir> holds captures of the same
// harness built from the same tree with ONLY the musical-time lines of 8be7406d
// reverted to their truncating casts (ArpeggiatorCore::syncToMusicalPosition's
// newSampleCounter and Ruinae's blockCtx.transportPositionSamples, which
// Retrigger: Beat reads). When that capture matches the golden (the harness's
// own comparison: count, order, kind, pitch, velocity exact, timing within 8
// samples), every delta between the golden and the new capture comes from
// those lines, and the file is "explained (A/B)".
//
// Usage:
//   node tools/diff-midi-golden.js <captured.txt> <golden.txt> [--verbose]
//   node tools/diff-midi-golden.js --dirs <capturedDir> <goldenDir> [--only a,b] [--ab <oldCodeDir>] [--verbose]
//     (directory mode pairs files by name and prints one summary row per file)
//   --edges a,b,c  replaces the SC-004b input-change / end-of-run samples
// Exit code: 0 when every delta is classified, 1 otherwise, 2 on usage error.
// =============================================================================

'use strict';

const fs = require('fs');
const path = require('path');

const TOLERANCE = 8; // tests/test_helpers/midi_golden_compare.h kTimingToleranceSamples

// Input changes of the SC-004b harness (makeStandardMidiSequence in
// plugins/ruinae/tests/unit/ruinae_byte_identical_post_lane10_test.cpp:
// 0, 5, 10, 15 and 45 s at 44.1 kHz) and the end of its 60 s run (whole
// 512-sample blocks). Override with --edges a,b,c for another harness.
let EDGES = [0, 220500, 441000, 661500, 1984500, Math.floor(2646000 / 512) * 512];

function parse(text, file) {
  const out = [];
  const lines = text.split(/\r?\n/);
  for (let i = 0; i < lines.length; ++i) {
    const line = lines[i];
    if (line.trim() === '') continue;
    const m = /^\[(-?\d+)\]\s+(noteOn|noteOff)\s+(\d+)(?:\s+(\d+))?\s*$/.exec(line);
    if (!m) throw new Error(`${file}:${i + 1}: unparseable line: ${line}`);
    out.push({
      t: Number(m[1]),
      on: m[2] === 'noteOn',
      pitch: Number(m[3]),
      vel: m[2] === 'noteOn' ? Number(m[4]) : 0,
      line: i + 1,
      idx: out.length,
    });
  }
  return out;
}

function same(a, b, shift = 0) {
  return a.on === b.on && a.pitch === b.pitch && a.vel === b.vel &&
         Math.abs(a.t + shift - b.t) <= TOLERANCE;
}

function fmt(e) {
  return `[${e.t}] ${e.on ? 'noteOn ' : 'noteOff'} ${e.pitch}${e.on ? ' ' + e.vel : ''}`;
}

// Tolerance-aware LCS. Returns hunks of { g: [...], c: [...] } (golden-only and
// captured-only runs between matched anchors) plus the matched-pair count.
function align(cap, gold) {
  const n = gold.length;
  const m = cap.length;
  // DP over suffixes, Uint16 rows are enough (dumps hold < 65536 events).
  const W = m + 1;
  const dp = new Uint16Array((n + 1) * W);
  for (let i = n - 1; i >= 0; --i) {
    for (let j = m - 1; j >= 0; --j) {
      dp[i * W + j] = same(gold[i], cap[j])
        ? dp[(i + 1) * W + j + 1] + 1
        : Math.max(dp[(i + 1) * W + j], dp[i * W + j + 1]);
    }
  }
  const hunks = [];
  let cur = null;
  let matched = 0;
  let i = 0;
  let j = 0;
  const flush = () => { if (cur) { hunks.push(cur); cur = null; } };
  while (i < n || j < m) {
    if (i < n && j < m && same(gold[i], cap[j]) &&
        dp[i * W + j] === dp[(i + 1) * W + j + 1] + 1) {
      flush();
      ++matched; ++i; ++j;
    } else if (j < m && (i >= n || dp[i * W + j + 1] >= dp[(i + 1) * W + j])) {
      (cur ??= { g: [], c: [] }).c.push(cap[j++]);
    } else {
      (cur ??= { g: [], c: [] }).g.push(gold[i++]);
    }
  }
  flush();
  return { hunks, matched };
}

// Greedy one-to-one matching of golden-only events, shifted by `shift`, onto
// captured-only events. Returns the indices left unmatched on both sides.
function matchShifted(g, c, shift, gSkip) {
  const usedC = new Set();
  const leftG = [];
  for (let a = 0; a < g.length; ++a) {
    if (gSkip.has(a)) continue;
    let hit = -1;
    for (let b = 0; b < c.length; ++b) {
      if (!usedC.has(b) && same(g[a], c[b], shift)) { hit = b; break; }
    }
    if (hit >= 0) usedC.add(hit); else leftG.push(a);
  }
  const leftC = [];
  for (let b = 0; b < c.length; ++b) if (!usedC.has(b)) leftC.push(b);
  return { leftG, leftC };
}

// Classify one hunk. `gold` is the full golden stream, used to find the event
// one sample before a golden-only event (the first firing of a doubled step).
function classify(h, gold) {
  const res = { doubled: [], shift: null, shifted: 0, ratchetCut: [], unexplained: [] };

  // 1. Doubled-step pairs: a golden-only event one sample after another golden event.
  const doubledIdx = new Set();
  h.g.forEach((e, a) => {
    const prev = gold[e.idx - 1]; // the golden event directly before it
    if (prev && e.t - prev.t === 1) {
      doubledIdx.add(a);
      res.doubled.push({ first: prev, second: e });
    }
  });

  // 2. One-step shift: try every candidate shift (captured-only minus
  //    golden-only onset, same pitch/kind/velocity) and keep the one that
  //    explains the most events. The step length is read from the data, not
  //    assumed, because presets use different note values.
  //    Each further spurious step advances the golden by one more step, so the
  // shift is applied in rounds: the best shift first, then the best one for
  // what is left, as long as a round explains at least kMinShiftRound events.
  const kMinShiftRound = 4;
  let gIdx = h.g.map((_, a) => a).filter((a) => !doubledIdx.has(a));
  let cIdx = h.c.map((_, b) => b);
  res.shifts = [];
  for (let round = 0; round < 6 && gIdx.length > 0 && cIdx.length > 0; ++round) {
    const g = gIdx.map((a) => h.g[a]);
    const c = cIdx.map((b) => h.c[b]);
    const cand = new Map();
    for (const ge of g) {
      for (const ce of c) {
        if (ce.on === ge.on && ce.pitch === ge.pitch && ce.vel === ge.vel && ce.t > ge.t) {
          cand.set(ce.t - ge.t, (cand.get(ce.t - ge.t) || 0) + 1);
        }
      }
    }
    // A repeating pattern also matches at "one step plus whole pattern
    // periods" (49612 = 9 x 5512 for an 8-step pattern). Among the shifts that
    // explain nearly as much as the best one, take the smallest: the step.
    const tried = [];
    for (const [d] of [...cand.entries()].sort((x, y) => y[1] - x[1]).slice(0, 24)) {
      tried.push({ shift: d, ...matchShifted(g, c, d, new Set()) });
    }
    if (tried.length === 0) break;
    const score = (r) => (g.length - r.leftG.length);
    const top = Math.max(...tried.map(score));
    const best = tried.filter((r) => score(r) >= 0.9 * top).sort((x, y) => x.shift - y.shift)[0];
    const explained = g.length - best.leftG.length;
    if (explained < kMinShiftRound) break;
    res.shifts.push({ shift: best.shift, events: explained });
    res.shifted += explained;
    gIdx = best.leftG.map((k) => gIdx[k]);
    cIdx = best.leftC.map((k) => cIdx[k]);
  }
  res.shift =res.shifts.length ? res.shifts[0].shift : null;

  // 3. Region edges: a shifted region starts where the spurious step fired
  //    (the captured side plays the step the golden skipped) and ends at the
  //    next input change or the end of the run (the golden's last early steps
  //    have no captured counterpart before it, or the held notes differ across
  //    it). An event left over within one step length of such an edge is an
  //    edge event of the shift, not a new delta.
  const step = res.shifts.length ? Math.min(...res.shifts.map((s) => s.shift)) : 0;
  const start = Math.min(h.g[0]?.t ?? Infinity, h.c[0]?.t ?? Infinity);
  const nearEdge = (t) => step > 0 &&
    (Math.abs(t - start) <= step + TOLERANCE ||
     EDGES.some((x) => Math.abs(t - x) <= step + TOLERANCE));
  res.edge = 0;
  for (const a of gIdx) {
    if (nearEdge(h.g[a].t)) ++res.edge; else res.unexplained.push({ side: 'golden', e: h.g[a] });
  }
  for (const b of cIdx) {
    if (nearEdge(h.c[b].t)) ++res.edge; else res.unexplained.push({ side: 'captured', e: h.c[b] });
  }
  return res;
}

// The SC-004b harness's own comparison (midi_golden_compare.h compareMidiGolden):
// returns null when it would pass, else the reason.
function harnessCompare(a, g) {
  if (a.length !== g.length) return `event count ${a.length} vs ${g.length}`;
  for (let k = 0; k < a.length; ++k) {
    if (a[k].on !== g[k].on || a[k].pitch !== g[k].pitch || a[k].vel !== g[k].vel) {
      return `event ${k} differs: ${fmt(a[k])} vs ${fmt(g[k])}`;
    }
    if (Math.abs(a[k].t - g[k].t) > TOLERANCE) return `event ${k} timing ${fmt(a[k])} vs ${fmt(g[k])}`;
  }
  return null;
}

function diffPair(capFile, goldFile, verbose, log) {
  const cap = parse(fs.readFileSync(capFile, 'utf8'), capFile);
  const gold = parse(fs.readFileSync(goldFile, 'utf8'), goldFile);
  const { hunks, matched } = align(cap, gold);
  const sum = {
    captured: cap.length, golden: gold.length, matched, hunks: hunks.length,
    doubled: 0, spurious: 0, shifted: 0, edge: 0, shifts: new Set(), unexplained: 0, ok: true,
  };
  for (const h of hunks) {
    const r = classify(h, gold);
    sum.doubled += r.doubled.length;
    sum.shifted += r.shifted;
    sum.spurious += r.shifts.length;
    sum.edge += r.edge;
    for (const s of r.shifts) sum.shifts.add(s.shift);
    sum.unexplained += r.unexplained.length;
    if (verbose || r.unexplained.length > 0) {
      log(`  hunk: golden-only ${h.g.length} (lines ${h.g[0]?.line ?? '-'}..${h.g.at(-1)?.line ?? '-'}),` +
          ` captured-only ${h.c.length}`);
      for (const d of r.doubled) log(`    doubled step: ${fmt(d.first)} -> ${fmt(d.second)}`);
      for (const s of r.shifts) log(`    spurious early step: ${s.events} golden events sit ${s.shift} samples early`);
      if (r.edge) log(`    shift-region edge events: ${r.edge}`);
      const shown = verbose ? r.unexplained : r.unexplained.slice(0, 12);
      for (const u of shown) log(`    UNEXPLAINED ${u.side}: ${fmt(u.e)}`);
      if (shown.length < r.unexplained.length) log(`    ... ${r.unexplained.length - shown.length} more`);
    }
  }
  sum.ok = sum.unexplained === 0;
  return sum;
}

function main(argv) {
  const verbose = argv.includes('--verbose');
  let args = argv.filter((a) => a !== '--verbose');
  const ei = args.indexOf('--edges');
  if (ei >= 0) {
    EDGES = args[ei + 1].split(',').map(Number);
    args = args.filter((_, k) => k !== ei && k !== ei + 1);
  }
  const log = (s) => process.stdout.write(s + '\n');

  if (args[0] === '--dirs') {
    const [, capDir, goldDir, ...rest] = args;
    if (!capDir || !goldDir) { usage(); return 2; }
    const oi = rest.indexOf('--only');
    const only = oi >= 0 ? new Set(rest[oi + 1].split(',')) : null;
    const ai = rest.indexOf('--ab');
    const abDir = ai >= 0 ? rest[ai + 1] : null;
    const names = fs.readdirSync(goldDir).filter((f) => f.endsWith('.txt')).sort();
    let allOk = true;
    log('file\tgolden\tcaptured\tdelta\tdoubled\tspurious\tshifted\tedge\tshift\tunexplained\tverdict');
    for (const name of names) {
      const key = name.replace(/^ruinae_factory_/, '').replace(/_golden_midi\.txt$/, '');
      if (only && !only.has(key) && !only.has(name)) continue;
      const capFile = path.join(capDir, name);
      if (!fs.existsSync(capFile)) { log(`${key}\tmissing capture`); allOk = false; continue; }
      const lines = [];
      const s = diffPair(capFile, path.join(goldDir, name), verbose, (x) => lines.push(x));
      const byteSame = fs.readFileSync(capFile, 'utf8') === fs.readFileSync(path.join(goldDir, name), 'utf8');
      let verdict = byteSame ? 'identical' : (s.hunks === 0 ? 'timing-only' : (s.ok ? 'explained' : 'UNEXPLAINED'));
      if (verdict === 'UNEXPLAINED' && abDir) {
        const oldFile = path.join(abDir, name);
        const ab = fs.existsSync(oldFile)
          ? harnessCompare(parse(fs.readFileSync(oldFile, 'utf8'), oldFile),
                           parse(fs.readFileSync(path.join(goldDir, name), 'utf8'), name))
          : 'no old-code capture';
        if (ab === null) { verdict = 'explained (A/B)'; s.ok = true; }
        else verdict = `UNEXPLAINED (A/B: ${ab})`;
      }
      log(`${key}\t${s.golden}\t${s.captured}\t${s.captured - s.golden}\t${s.doubled}\t${s.spurious}\t` +
          `${s.shifted}\t${s.edge}\t` +
          `${[...s.shifts].join('/') || '-'}\t${s.unexplained}\t${verdict}`);
      for (const l of lines) log(l);
      if (!s.ok) allOk = false;
    }
    return allOk ? 0 : 1;
  }

  if (args.length !== 2) { usage(); return 2; }
  const s = diffPair(args[0], args[1], verbose, log);
  log(`golden ${s.golden}, captured ${s.captured}, matched ${s.matched}, hunks ${s.hunks}, ` +
      `doubled ${s.doubled}, spurious ${s.spurious}, shifted ${s.shifted} ` +
      `(shift ${[...s.shifts].join('/') || '-'}), edge ${s.edge}, unexplained ${s.unexplained}`);
  return s.ok ? 0 : 1;
}

function usage() {
  process.stderr.write('usage: diff-midi-golden.js <captured> <golden> [--verbose]\n' +
                       '       diff-midi-golden.js --dirs <capturedDir> <goldenDir> [--only a,b] [--ab <oldCodeDir>] [--verbose]\n' +
                       '       --edges a,b,c replaces the SC-004b input-change / end-of-run samples\n');
}

process.exitCode = main(process.argv.slice(2));
