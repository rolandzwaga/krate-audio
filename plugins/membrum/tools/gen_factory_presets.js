#!/usr/bin/env node
// ==============================================================================
// Factory Kit Preset Generator for Membrum
// ==============================================================================
// Generates binary kit preset files in the unified v6 state codec format.
// Layout matches Membrum::State::writeKitBlob (see state_codec.{h,cpp}):
//   [int32 version = 6]
//   [int32 maxPolyphony]
//   [int32 stealPolicy]
//   For each of 32 pads:
//     [int32 exciterType][int32 bodyModel]
//     [34 x float64 sound (offsets 2-35, with choke/bus mirrored at 28/29)]
//     [uint8 chokeGroup][uint8 outputBus]
//   [int32 selectedPadIndex]
//   [4 x float64: gc, sb, tr, cd]
//   [32 x float64 per-pad couplingAmount]
//   [uint16 overrideCount] (+ entries)
//   [160 x float64 pad-major macros]
//   Optional session field [int32 uiMode] when emitted.
//
// Usage: node gen_factory_presets.js [output_dir]
//   Default output_dir: ../resources/presets/Kit Presets/
// ==============================================================================

const fs = require('fs');
const path = require('path');

// ---- Constants matching pad_config.h and data-model.md ----
const kNumPads = 32;
const kVersion = 6;

// Exciter types
const ExciterType = {
    Impulse: 0, Mallet: 1, NoiseBurst: 2,
    Friction: 3, FMImpulse: 4, Feedback: 5,
};

// Body model types
const BodyModelType = {
    Membrane: 0, Plate: 1, Shell: 2,
    String: 3, Bell: 4, NoiseBody: 5,
};

// Default PadConfig values (matching pad_config.h defaults)
function defaultPad() {
    return {
        exciterType: ExciterType.Impulse,
        bodyModel: BodyModelType.Membrane,
        // 34 float64 sound params (offsets 2-35)
        // offsets 2-6: core
        material: 0.5, size: 0.5, decay: 0.3, strikePosition: 0.3, level: 0.8,
        // offsets 7-20: tone shaper
        tsFilterType: 0.0, tsFilterCutoff: 1.0, tsFilterResonance: 0.0,
        tsFilterEnvAmount: 0.5, tsDriveAmount: 0.0, tsFoldAmount: 0.0,
        tsPitchEnvStart: 0.0, tsPitchEnvEnd: 0.0, tsPitchEnvTime: 0.0,
        tsPitchEnvCurve: 0.0,
        tsFilterEnvAttack: 0.0, tsFilterEnvDecay: 0.1,
        tsFilterEnvSustain: 0.0, tsFilterEnvRelease: 0.1,
        // offsets 21-24: unnatural zone
        modeStretch: 0.333333, decaySkew: 0.5,
        modeInjectAmount: 0.0, nonlinearCoupling: 0.0,
        // offsets 25-29: material morph
        morphEnabled: 0.0, morphStart: 1.0, morphEnd: 0.0,
        morphDuration: 0.095477, morphCurve: 0.0,
        // offset 30-31: choke/bus (as float64 then uint8)
        chokeGroup: 0, outputBus: 0,
        // offsets 32-35: exciter secondary
        fmRatio: 0.5, feedbackAmount: 0.0,
        noiseBurstDuration: 0.5, frictionPressure: 0.0,
        // Phase 5 offset 36: per-pad coupling participation
        couplingAmount: 0.5,
        // Phase 6 offsets 37-41: macros (neutral)
        macroTightness: 0.5, macroBrightness: 0.5, macroBodySize: 0.5,
        macroPunch: 0.5, macroComplexity: 0.5,
    };
}

function writePadToBuffer(buf, offset, pad) {
    let pos = offset;

    // int32: exciterType
    buf.writeInt32LE(pad.exciterType, pos); pos += 4;
    // int32: bodyModel
    buf.writeInt32LE(pad.bodyModel, pos); pos += 4;

    // 34 x float64: offsets 2-29 (28 continuous), then 30-31 (choke/bus), then 32-35 (4 secondary)
    const soundParams = [
        pad.material, pad.size, pad.decay, pad.strikePosition, pad.level,
        pad.tsFilterType, pad.tsFilterCutoff, pad.tsFilterResonance,
        pad.tsFilterEnvAmount, pad.tsDriveAmount, pad.tsFoldAmount,
        pad.tsPitchEnvStart, pad.tsPitchEnvEnd, pad.tsPitchEnvTime,
        pad.tsPitchEnvCurve,
        pad.tsFilterEnvAttack, pad.tsFilterEnvDecay,
        pad.tsFilterEnvSustain, pad.tsFilterEnvRelease,
        pad.modeStretch, pad.decaySkew, pad.modeInjectAmount,
        pad.nonlinearCoupling,
        pad.morphEnabled, pad.morphStart, pad.morphEnd,
        pad.morphDuration, pad.morphCurve,
        // offsets 30-31 as float64
        pad.chokeGroup, pad.outputBus,
        // offsets 32-35
        pad.fmRatio, pad.feedbackAmount,
        pad.noiseBurstDuration, pad.frictionPressure,
    ];

    for (const v of soundParams) {
        buf.writeDoubleLE(v, pos); pos += 8;
    }

    // uint8: chokeGroup
    buf.writeUInt8(pad.chokeGroup, pos); pos += 1;
    // uint8: outputBus
    buf.writeUInt8(pad.outputBus, pos); pos += 1;

    return pos;
}

function writeKitPreset(pads, opts = {}) {
    const {
        maxPolyphony = 8,
        stealingPolicy = 0,
        selectedPadIndex = 0,
        globalCoupling = 0.0,
        snareBuzz = 0.0,
        tomResonance = 0.0,
        couplingDelayMs = 1.0,
        overrides = [],
        uiMode = 0,
        includeSession = true,
    } = opts;

    // Dynamic size: v6 prefix (9040) + globals (32) + per-pad (256) +
    // overrideCount (2) + overrides*6 + macros (1280) + optional session (4
    // bytes for uiMode).
    const prefixSize = 4 + 4 + 4 + kNumPads * (4 + 4 + 34 * 8 + 1 + 1) + 4;
    const phase5Size = 4 * 8 + kNumPads * 8 + 2 + overrides.length * 6;
    const macrosSize = kNumPads * 5 * 8;
    const sessionSize = includeSession ? 4 : 0;
    const totalSize = prefixSize + phase5Size + macrosSize + sessionSize;

    const buf = Buffer.alloc(totalSize);
    let pos = 0;

    buf.writeInt32LE(kVersion, pos);        pos += 4;
    buf.writeInt32LE(maxPolyphony, pos);    pos += 4;
    buf.writeInt32LE(stealingPolicy, pos);  pos += 4;

    for (let i = 0; i < kNumPads; i++) {
        pos = writePadToBuffer(buf, pos, pads[i]);
    }

    buf.writeInt32LE(selectedPadIndex, pos); pos += 4;

    buf.writeDoubleLE(globalCoupling, pos);  pos += 8;
    buf.writeDoubleLE(snareBuzz, pos);       pos += 8;
    buf.writeDoubleLE(tomResonance, pos);    pos += 8;
    buf.writeDoubleLE(couplingDelayMs, pos); pos += 8;

    for (let i = 0; i < kNumPads; i++) {
        buf.writeDoubleLE(pads[i].couplingAmount ?? 0.5, pos); pos += 8;
    }

    buf.writeUInt16LE(overrides.length, pos); pos += 2;
    for (const ov of overrides) {
        buf.writeUInt8(ov.src, pos);     pos += 1;
        buf.writeUInt8(ov.dst, pos);     pos += 1;
        buf.writeFloatLE(ov.coeff, pos); pos += 4;
    }

    // Macros, pad-major.
    for (let i = 0; i < kNumPads; i++) {
        const p = pads[i];
        buf.writeDoubleLE(p.macroTightness  ?? 0.5, pos); pos += 8;
        buf.writeDoubleLE(p.macroBrightness ?? 0.5, pos); pos += 8;
        buf.writeDoubleLE(p.macroBodySize   ?? 0.5, pos); pos += 8;
        buf.writeDoubleLE(p.macroPunch      ?? 0.5, pos); pos += 8;
        buf.writeDoubleLE(p.macroComplexity ?? 0.5, pos); pos += 8;
    }

    if (includeSession) {
        buf.writeInt32LE(uiMode, pos); pos += 4;
    }

    if (pos !== totalSize) {
        throw new Error(`Expected ${totalSize} bytes, wrote ${pos}`);
    }

    return buf;
}

// ==============================================================================
// Kit 1: Electronic / 808-Style
// ==============================================================================
function electronicKit() {
    const pads = [];
    for (let i = 0; i < kNumPads; i++) pads.push(defaultPad());

    // Pad 0: 808 Kick
    pads[0].exciterType = ExciterType.Impulse;
    pads[0].bodyModel = BodyModelType.Membrane;
    pads[0].material = 0.15;
    pads[0].size = 0.9;
    pads[0].decay = 0.35;
    pads[0].level = 0.85;
    pads[0].tsPitchEnvStart = Math.log(200 / 20) / Math.log(100);
    pads[0].tsPitchEnvEnd = Math.log(40 / 20) / Math.log(100);
    pads[0].tsPitchEnvTime = 0.06; // 30ms

    // Pad 2: 808 Snare
    pads[2].exciterType = ExciterType.NoiseBurst;
    pads[2].bodyModel = BodyModelType.Membrane;
    pads[2].material = 0.55;
    pads[2].size = 0.45;
    pads[2].decay = 0.35;
    pads[2].level = 0.8;
    pads[2].noiseBurstDuration = (6 - 2) / 13.0;

    // Pad 4: Electronic Snare 2
    pads[4].exciterType = ExciterType.NoiseBurst;
    pads[4].bodyModel = BodyModelType.Membrane;
    pads[4].material = 0.6;
    pads[4].size = 0.4;
    pads[4].decay = 0.3;
    pads[4].level = 0.8;
    pads[4].noiseBurstDuration = (5 - 2) / 13.0;

    // Pad 6: Closed Hat (choke 1)
    pads[6].exciterType = ExciterType.NoiseBurst;
    pads[6].bodyModel = BodyModelType.NoiseBody;
    pads[6].material = 0.92;
    pads[6].size = 0.1;
    pads[6].decay = 0.08;
    pads[6].level = 0.75;
    pads[6].chokeGroup = 1;
    pads[6].noiseBurstDuration = (3 - 2) / 13.0;

    // Pad 8: Pedal Hat (choke 1)
    pads[8].exciterType = ExciterType.NoiseBurst;
    pads[8].bodyModel = BodyModelType.NoiseBody;
    pads[8].material = 0.88;
    pads[8].size = 0.12;
    pads[8].decay = 0.06;
    pads[8].level = 0.7;
    pads[8].chokeGroup = 1;

    // Pad 10: Open Hat (choke 1)
    pads[10].exciterType = ExciterType.NoiseBurst;
    pads[10].bodyModel = BodyModelType.NoiseBody;
    pads[10].material = 0.9;
    pads[10].size = 0.2;
    pads[10].decay = 0.5;
    pads[10].level = 0.75;
    pads[10].chokeGroup = 1;

    // Pad 13: Crash
    pads[13].exciterType = ExciterType.NoiseBurst;
    pads[13].bodyModel = BodyModelType.NoiseBody;
    pads[13].material = 0.95;
    pads[13].size = 0.35;
    pads[13].decay = 0.7;
    pads[13].level = 0.7;

    // Pads 5,7,9,11,12,14: Toms with varying sizes
    const tomPads = [5, 7, 9, 11, 12, 14];
    const tomSizes = [0.85, 0.75, 0.65, 0.55, 0.48, 0.4];
    for (let i = 0; i < tomPads.length; i++) {
        const p = tomPads[i];
        pads[p].exciterType = ExciterType.Impulse;
        pads[p].bodyModel = BodyModelType.Membrane;
        pads[p].material = 0.25;
        pads[p].size = tomSizes[i];
        pads[p].decay = 0.4;
        pads[p].level = 0.8;
        pads[p].tsPitchEnvStart = Math.log(120 / 20) / Math.log(100);
        pads[p].tsPitchEnvEnd = Math.log(60 / 20) / Math.log(100);
        pads[p].tsPitchEnvTime = 0.03;
    }

    // Perc pads: 1, 3, 18-31 use electronic perc settings
    const percPads = [1, 3, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31];
    for (const p of percPads) {
        pads[p].exciterType = ExciterType.FMImpulse;
        pads[p].bodyModel = BodyModelType.Bell;
        pads[p].material = 0.7;
        pads[p].size = 0.25;
        pads[p].decay = 0.2;
        pads[p].level = 0.75;
        pads[p].fmRatio = 0.4;
    }

    return pads;
}

// ==============================================================================
// Kit 2: Acoustic-Inspired
// ==============================================================================
function acousticKit() {
    const pads = [];
    for (let i = 0; i < kNumPads; i++) pads.push(defaultPad());

    // Pad 0: Acoustic Kick
    pads[0].exciterType = ExciterType.Mallet;
    pads[0].bodyModel = BodyModelType.Membrane;
    pads[0].material = 0.35;
    pads[0].size = 0.85;
    pads[0].decay = 0.25;
    pads[0].level = 0.85;
    pads[0].tsPitchEnvStart = Math.log(160 / 20) / Math.log(100);
    pads[0].tsPitchEnvEnd = Math.log(50 / 20) / Math.log(100);
    pads[0].tsPitchEnvTime = 0.04;

    // Pad 2: Acoustic Snare
    pads[2].exciterType = ExciterType.NoiseBurst;
    pads[2].bodyModel = BodyModelType.Membrane;
    pads[2].material = 0.5;
    pads[2].size = 0.5;
    pads[2].decay = 0.4;
    pads[2].level = 0.82;
    pads[2].noiseBurstDuration = (8 - 2) / 13.0;

    // Pad 4: Side Stick
    pads[4].exciterType = ExciterType.Impulse;
    pads[4].bodyModel = BodyModelType.Shell;
    pads[4].material = 0.8;
    pads[4].size = 0.2;
    pads[4].decay = 0.15;
    pads[4].level = 0.78;

    // Hi-hats with choke group 1
    pads[6].exciterType = ExciterType.NoiseBurst;
    pads[6].bodyModel = BodyModelType.NoiseBody;
    pads[6].material = 0.88;
    pads[6].size = 0.15;
    pads[6].decay = 0.1;
    pads[6].level = 0.75;
    pads[6].chokeGroup = 1;

    pads[8] = Object.assign(defaultPad(), pads[6]);
    pads[8].decay = 0.05;
    pads[8].chokeGroup = 1;

    pads[10] = Object.assign(defaultPad(), pads[6]);
    pads[10].decay = 0.6;
    pads[10].chokeGroup = 1;

    // Toms
    const tomPads = [5, 7, 9, 11, 12, 14];
    const tomSizes = [0.8, 0.7, 0.6, 0.5, 0.45, 0.4];
    for (let i = 0; i < tomPads.length; i++) {
        const p = tomPads[i];
        pads[p].exciterType = ExciterType.Mallet;
        pads[p].bodyModel = BodyModelType.Membrane;
        pads[p].material = 0.4;
        pads[p].size = tomSizes[i];
        pads[p].decay = 0.5;
        pads[p].level = 0.8;
    }

    // Cymbals
    const cymbalPads = [13, 15, 16, 17, 19, 21, 23];
    for (const p of cymbalPads) {
        pads[p].exciterType = ExciterType.NoiseBurst;
        pads[p].bodyModel = BodyModelType.NoiseBody;
        pads[p].material = 0.95;
        pads[p].size = 0.3;
        pads[p].decay = 0.8;
        pads[p].level = 0.72;
    }

    // Misc perc
    const percPads = [1, 3, 18, 20, 22, 24, 25, 26, 27, 28, 29, 30, 31];
    for (const p of percPads) {
        pads[p].exciterType = ExciterType.Mallet;
        pads[p].bodyModel = BodyModelType.Plate;
        pads[p].material = 0.7;
        pads[p].size = 0.3;
        pads[p].decay = 0.3;
        pads[p].level = 0.78;
    }

    return pads;
}

// ==============================================================================
// Kit 3: Experimental / FX
// ==============================================================================
function experimentalKit() {
    const pads = [];
    for (let i = 0; i < kNumPads; i++) pads.push(defaultPad());

    // Pad 0: FM Kick
    pads[0].exciterType = ExciterType.FMImpulse;
    pads[0].bodyModel = BodyModelType.Membrane;
    pads[0].material = 0.2;
    pads[0].size = 0.95;
    pads[0].decay = 0.4;
    pads[0].level = 0.85;
    pads[0].fmRatio = 0.6;
    pads[0].tsPitchEnvStart = Math.log(300 / 20) / Math.log(100);
    pads[0].tsPitchEnvEnd = Math.log(30 / 20) / Math.log(100);
    pads[0].tsPitchEnvTime = 0.08;

    // Pad 2: Feedback Snare
    pads[2].exciterType = ExciterType.Feedback;
    pads[2].bodyModel = BodyModelType.Shell;
    pads[2].material = 0.6;
    pads[2].size = 0.4;
    pads[2].decay = 0.35;
    pads[2].level = 0.8;
    pads[2].feedbackAmount = 0.4;

    // Pad 4: Friction FX
    pads[4].exciterType = ExciterType.Friction;
    pads[4].bodyModel = BodyModelType.String;
    pads[4].material = 0.5;
    pads[4].size = 0.6;
    pads[4].decay = 0.7;
    pads[4].level = 0.75;
    pads[4].frictionPressure = 0.5;

    // Metal hats with morph
    pads[6].exciterType = ExciterType.NoiseBurst;
    pads[6].bodyModel = BodyModelType.Bell;
    pads[6].material = 0.95;
    pads[6].size = 0.1;
    pads[6].decay = 0.08;
    pads[6].level = 0.7;
    pads[6].chokeGroup = 1;
    pads[6].morphEnabled = 1.0;
    pads[6].morphStart = 0.95;
    pads[6].morphEnd = 0.3;
    pads[6].morphDuration = 0.2;

    pads[8] = Object.assign({}, pads[6]);
    pads[8].decay = 0.04;

    pads[10] = Object.assign({}, pads[6]);
    pads[10].decay = 0.6;

    // Toms with mode stretch and nonlinear coupling
    const tomPads = [5, 7, 9, 11, 12, 14];
    const tomSizes = [0.85, 0.75, 0.65, 0.55, 0.45, 0.35];
    for (let i = 0; i < tomPads.length; i++) {
        const p = tomPads[i];
        pads[p].exciterType = ExciterType.Mallet;
        pads[p].bodyModel = BodyModelType.Plate;
        pads[p].material = 0.5;
        pads[p].size = tomSizes[i];
        pads[p].decay = 0.6;
        pads[p].level = 0.78;
        pads[p].modeStretch = 0.5; // slightly inharmonic
        pads[p].nonlinearCoupling = 0.3;
    }

    // FX pads: various strange sounds
    const fxPads = [1, 3, 13, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31];
    for (let i = 0; i < fxPads.length; i++) {
        const p = fxPads[i];
        pads[p].exciterType = (i % 2 === 0) ? ExciterType.FMImpulse : ExciterType.Feedback;
        pads[p].bodyModel = [BodyModelType.Bell, BodyModelType.String, BodyModelType.Shell, BodyModelType.Plate][i % 4];
        pads[p].material = 0.3 + (i * 0.03);
        pads[p].size = 0.2 + (i * 0.02);
        pads[p].decay = 0.3 + (i * 0.02);
        pads[p].level = 0.75;
        pads[p].modeStretch = 0.2 + (i * 0.04);
        pads[p].modeInjectAmount = 0.2;
        pads[p].nonlinearCoupling = 0.15;
        pads[p].fmRatio = 0.3 + (i * 0.02);
        pads[p].feedbackAmount = 0.1 + (i * 0.02);
    }

    return pads;
}

// ==============================================================================
// Main
// ==============================================================================
const outputBase = process.argv[2] ||
    path.join(__dirname, '..', 'resources', 'presets', 'Kit Presets');

const kits = [
    { name: '808 Electronic Kit', subdir: 'Electronic', pads: electronicKit() },
    { name: 'Acoustic Studio Kit', subdir: 'Acoustic', pads: acousticKit() },
    { name: 'Experimental FX Kit', subdir: 'Experimental', pads: experimentalKit() },
];

for (const kit of kits) {
    const dir = path.join(outputBase, kit.subdir);
    fs.mkdirSync(dir, { recursive: true });

    const buf = writeKitPreset(kit.pads);
    const filePath = path.join(dir, `${kit.name}.memkit`);
    fs.writeFileSync(filePath, buf);

    console.log(`Wrote ${buf.length} bytes to ${filePath}`);
}

console.log('Done. Factory kit presets generated.');
