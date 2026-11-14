// Writes the two pre-Rungler Disrumpo processor-state fixtures (fix plan T2.15, D-055):
//   disrumpo_state_v8_pre_rungler.bin  - kPresetVersion 8 layout, last written by
//                                        044e5fff (parent of eb2e03c5, 2026-02-01)
//   disrumpo_state_v9_pre_rungler.bin  - kPresetVersion 9 layout of the released
//                                        disrumpo/v0.9.0 and v0.9.1 (Processor::getState)
// No historical build was run: both streams follow the writers read with
//   git show disrumpo/v0.9.1:plugins/disrumpo/src/processor/processor.cpp
//   git show 044e5fff:plugins/disrumpo/src/processor/processor.cpp
// field by field (little endian). v8 differs from v9 only by the per-node shape slots
// and per-type shadow storage, which v9 appends to every morph node. Neither writes the
// Rungler block (v11) or the instance-ID trailer.
//
// Both use the pre-Rungler ModSource numbering (SampleHold = 10, PitchFollower = 11,
// Transient = 12; 05a3ddb7 inserted Rungler = 10 on 2026-02-16). Routings:
//   r0: source 10 (S&H)       -> dest 0  (Input Gain),          amount 0.5
//   r1: source 11 (Pitch)     -> dest 14 (old band 1 Drive: 6 + 1*6 + 2), amount -0.25
//   r2: source 12 (Transient) -> dest 3  (Sweep Frequency),     amount 0.75
//   r3: source 9  (Chaos)     -> dest 2  (Global Mix),          amount 0.3
// Everything else holds neutral values.
//
// Usage: node make_pre_rungler_fixtures.js   (writes next to this script)
'use strict';
const fs = require('fs');
const path = require('path');

function writer() {
  const bytes = [];
  const b = Buffer.alloc(4);
  return {
    i8(v) { bytes.push(v & 0xff); },
    i32(v) { b.writeInt32LE(v); bytes.push(...b); },
    f32(v) { b.writeFloatLE(v); bytes.push(...b); },
    done() { return Buffer.from(bytes); },
  };
}

function stream(version) {
  const w = writer();
  w.i32(version);
  w.f32(0.5); w.f32(0.5); w.f32(1.0);  // input gain, output gain, global mix
  w.i32(4);  // band count
  for (let band = 0; band < 4; band++) { w.f32(0); w.f32(0); w.i8(0); w.i8(0); w.i8(0); }  // gain, pan, solo, bypass, mute
  for (const hz of [200, 1000, 5000]) w.f32(hz);  // crossovers (Hz)
  // Sweep core: enable, freq, width, intensity (normalized), falloff, morph link
  w.i8(0); w.f32(0.566); w.f32(0.286); w.f32(0.25); w.i8(1); w.i8(0);
  // Sweep LFO: enable, rate, waveform, depth, sync, note index (old NoteValue*3+modifier)
  w.i8(0); w.f32(0.606); w.i8(0); w.f32(0); w.i8(0); w.i8(6);
  // Sweep envelope: enable, attack, release, sensitivity
  w.i8(0); w.f32(0.091); w.f32(0.184); w.f32(0.5);
  w.i32(2); w.f32(0); w.f32(0); w.f32(1); w.f32(1);  // custom curve: 2 breakpoints
  for (let l = 0; l < 2; l++) { w.f32(0.5); w.i8(0); w.f32(0); w.i8(0); w.i8(6); w.i8(0); w.i8(1); }  // LFO 1, LFO 2
  w.f32(0.1); w.f32(0.1); w.f32(0.5); w.i8(0);  // envelope follower
  w.f32(0.1); w.f32(0); w.i8(0);                // random
  w.i8(0); w.f32(0.1); w.f32(0);                // chaos
  w.i8(0); w.f32(0.1); w.f32(0);                // sample & hold
  w.f32(0); w.f32(0); w.f32(0.5); w.f32(0);     // pitch follower
  w.f32(0.5); w.f32(0); w.f32(0);               // transient
  for (let m = 0; m < 4; m++) { w.f32(0); w.f32(0); w.f32(1); w.i8(0); }  // macros
  const routings = [[10, 0, 0.5], [11, 14, -0.25], [12, 3, 0.75], [9, 2, 0.3]];
  for (let r = 0; r < 32; r++) {
    const [src, dest, amount] = routings[r] || [0, 0, 0];
    w.i8(src); w.i32(dest); w.f32(amount); w.i8(0);  // source, dest, amount, curve
  }
  for (let band = 0; band < 4; band++) {
    w.f32(0.5); w.f32(0.5); w.i8(0); w.i8(2); w.f32(0);  // X, Y, mode, active nodes, smoothing
    for (let n = 0; n < 4; n++) {
      w.i8(0); w.f32(1); w.f32(1); w.f32(4000); w.f32(0); w.f32(1); w.f32(16);  // SoftClip, drive, mix, tone, bias, folds, bits
      if (version >= 9) {
        for (let s = 0; s < 10; s++) w.f32(0.5);            // shape slots
        for (let t = 0; t < 26 * 10; t++) w.f32(0.5);       // per-type shadow (26 types x 10 slots)
      }
    }
  }
  return w.done();
}

for (const v of [8, 9]) {
  const file = path.join(__dirname, `disrumpo_state_v${v}_pre_rungler.bin`);
  fs.writeFileSync(file, stream(v));
  console.log(`${file}: ${fs.statSync(file).size} bytes`);
}
