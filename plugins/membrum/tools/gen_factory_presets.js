#!/usr/bin/env node
// ==============================================================================
// Factory Kit Preset Generator for Membrum
// ==============================================================================
// Generates binary kit preset files in the v12 state codec format. Layout
// matches Membrum::State::writeKitBlob (see state_codec.{h,cpp}):
//
//   [int32 version = 12]
//   [int32 maxPolyphony]
//   [int32 stealPolicy]
//   For each of 32 pads:
//     [int32 exciterType][int32 bodyModel]
//     [52 x float64 sound, with choke/bus mirrored at indices 28/29]
//     [uint8 chokeGroup][uint8 outputBus]
//   [int32 selectedPadIndex]
//   [4 x float64: globalCoupling, snareBuzz, tomResonance, couplingDelayMs]
//   [32 x float64 per-pad couplingAmount]
//   [uint16 overrideCount] (+ entries)
//   [160 x float64 pad-major macros]
//   Optional session field [int32 uiMode] when emitted.
//
// Sound array layout (matches Membrum::State::PadSnapshot::sound, 52 doubles):
//   [0..27]  Phase 1..6 contiguous block (material, size, decay, ..., morphCurve)
//   [28..29] chokeGroup / outputBus float64 mirrors (uint8 below is authoritative)
//   [30..33] exciter secondary block (fmRatio, feedbackAmount,
//            noiseBurstDuration, frictionPressure)
//   [34..38] Phase 7 noise layer (mix, cutoff, resonance, decay, color)
//   [39..41] Phase 7 click layer (mix, contactMs, brightness)
//   [42..43] Phase 8A body damping (b1, b3) -- sentinel -1.0 = legacy derivation
//   [44..45] Phase 8C air-loading + modeScatter
//   [46..49] Phase 8D head/shell coupling (strength, secondaryEnabled,
//            secondarySize, secondaryMaterial)
//   [50]     Phase 8E tensionModAmt (nonlinear pitch glide)
//   [51]     Phase 8F enabled (per-pad on/off; default 1.0)
//
// Usage: node gen_factory_presets.js [output_dir]
//   Default output_dir: ../resources/presets/Kit Presets/
// ==============================================================================

const fs = require('fs');
const path = require('path');

// ---- Constants matching pad_config.h and data-model.md ----
const kNumPads = 32;
const kVersion = 12;
const kSoundSlotsPerPad = 52;

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
// Phase 8F: leave only the genuinely-crafted pads sounding. A factory kit
// wired to bulk-loop "filler" pads (e.g. one perc patch repeated 19 times
// across MIDI 38..67) just wastes user attention; better to silence them
// so re-enabling a slot is an explicit "I want this pad" gesture.
function disableUncraftedPads(pads, craftedIndices) {
    const crafted = new Set(craftedIndices);
    for (let i = 0; i < pads.length; i++) {
        if (!crafted.has(i)) {
            pads[i].enabled = 0.0;
        }
    }
}

function defaultPad() {
    return {
        exciterType: ExciterType.Impulse,
        bodyModel: BodyModelType.Membrane,
        // ---- Phase 1-6 sound block (sound[0..33] in PadSnapshot) ----
        // Phase 1 core
        material: 0.5, size: 0.5, decay: 0.3, strikePosition: 0.3, level: 0.8,
        // Phase 2 tone shaper
        tsFilterType: 0.0, tsFilterCutoff: 1.0, tsFilterResonance: 0.0,
        tsFilterEnvAmount: 0.5, tsDriveAmount: 0.0, tsFoldAmount: 0.0,
        tsPitchEnvStart: 0.0, tsPitchEnvEnd: 0.0, tsPitchEnvTime: 0.0,
        tsPitchEnvCurve: 0.0,
        tsFilterEnvAttack: 0.0, tsFilterEnvDecay: 0.1,
        tsFilterEnvSustain: 0.0, tsFilterEnvRelease: 0.1,
        // Unnatural zone
        modeStretch: 0.333333, decaySkew: 0.5,
        modeInjectAmount: 0.0, nonlinearCoupling: 0.0,
        // Material morph (off by default)
        morphEnabled: 0.0, morphStart: 1.0, morphEnd: 0.0,
        morphDuration: 0.095477, morphCurve: 0.0,
        // Routing (uint8 below is authoritative)
        chokeGroup: 0, outputBus: 0,
        // Exciter secondary
        fmRatio: 0.5, feedbackAmount: 0.0,
        noiseBurstDuration: 0.5, frictionPressure: 0.0,
        // Phase 5: per-pad coupling participation
        couplingAmount: 0.5,
        // Phase 6: macros (neutral)
        macroTightness: 0.5, macroBrightness: 0.5, macroBodySize: 0.5,
        macroPunch: 0.5, macroComplexity: 0.5,

        // ---- Phase 7+ late slots (sound[34..51]) ----
        // Phase 7 noise layer (always-on filtered noise running parallel to
        // the modal body; great for snare/hat realism, can be muted by mix=0).
        noiseLayerMix: 0.35,
        noiseLayerCutoff: 0.5,
        noiseLayerResonance: 0.2,
        noiseLayerDecay: 0.3,
        noiseLayerColor: 0.5,
        // Phase 7 attack click transient (raised-cosine filtered-noise burst).
        clickLayerMix: 0.5,
        clickLayerContactMs: 0.3,
        clickLayerBrightness: 0.6,
        // Phase 8A per-mode damping law. NB: PadConfig default is sentinel
        // (-1.0) which means "let the audio mapper derive from decay/material",
        // but VST3 parameters are normalised to [0, 1] and the kit-preset
        // browser load path goes through the controller's parameter system
        // -- so the sentinel cannot survive a load. We write explicit
        // mid-range defaults instead. b1 = 0.40 -> ~0.35 s decay floor
        // (won't dominate over the per-pad `decay` knob); b3 = 0.40 ->
        // moderate woody high-mode damping.
        bodyDampingB1: 0.40,
        bodyDampingB3: 0.40,
        // Phase 8C air-loading + modeScatter. airLoading=0.6 is the realistic
        // membrane default (Rossing 1982); modeScatter=0 keeps pure ratios.
        airLoading: 0.6,
        modeScatter: 0.0,
        // Phase 8D head/shell coupling (off by default).
        couplingStrength: 0.0,
        secondaryEnabled: 0.0,
        secondarySize: 0.5,
        secondaryMaterial: 0.4,
        // Phase 8E nonlinear tension modulation depth.
        tensionModAmt: 0.0,
        // Phase 8F per-pad enable toggle (1.0 = on).
        enabled: 1.0,
    };
}

function writePadToBuffer(buf, offset, pad) {
    let pos = offset;

    // int32: exciterType
    buf.writeInt32LE(pad.exciterType, pos); pos += 4;
    // int32: bodyModel
    buf.writeInt32LE(pad.bodyModel, pos); pos += 4;

    // 52 x float64. Layout matches Membrum::State::PadSnapshot::sound; see
    // the file-level comment block at the top for the slot map.
    const soundParams = [
        // [0..27] Phase 1-6 contiguous block.
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
        // [28..29] choke/bus float64 mirrors.
        pad.chokeGroup, pad.outputBus,
        // [30..33] exciter secondary block.
        pad.fmRatio, pad.feedbackAmount,
        pad.noiseBurstDuration, pad.frictionPressure,
        // [34..38] Phase 7 noise layer.
        pad.noiseLayerMix, pad.noiseLayerCutoff,
        pad.noiseLayerResonance, pad.noiseLayerDecay, pad.noiseLayerColor,
        // [39..41] Phase 7 click layer.
        pad.clickLayerMix, pad.clickLayerContactMs, pad.clickLayerBrightness,
        // [42..43] Phase 8A body damping (sentinel -1.0 = legacy derivation).
        pad.bodyDampingB1, pad.bodyDampingB3,
        // [44..45] Phase 8C air-loading + scatter.
        pad.airLoading, pad.modeScatter,
        // [46..49] Phase 8D head/shell coupling.
        pad.couplingStrength, pad.secondaryEnabled,
        pad.secondarySize, pad.secondaryMaterial,
        // [50] Phase 8E tension modulation.
        pad.tensionModAmt,
        // [51] Phase 8F per-pad enable.
        pad.enabled,
    ];

    if (soundParams.length !== kSoundSlotsPerPad) {
        throw new Error(
            `soundParams has ${soundParams.length} entries; expected ${kSoundSlotsPerPad}`);
    }

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

    // Dynamic size for v12: prefix + Phase 5 globals + per-pad coupling
    // amounts + override block + Phase 6 macros + optional session uiMode.
    // Per-pad bytes = 4 (excType) + 4 (bodyModel) + 52*8 (sound) + 1 + 1
    // = 426. Total prefix = 12 (header) + 32*426 + 4 (selectedPadIndex)
    // = 13648 bytes.
    const prefixSize = 4 + 4 + 4 + kNumPads * (4 + 4 + kSoundSlotsPerPad * 8 + 1 + 1) + 4;
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

    // Synth-domain global tweaks: zero airLoading, no shell coupling, no
    // mode scatter. Electronic kits earn their character from clean modal
    // pitches and (for 808-style) a long tension-modulated boom on the kick.

    // ---- Pad 0: 808 Kick (clean sub + boom-glide) ----
    pads[0].exciterType = ExciterType.Impulse;
    pads[0].bodyModel = BodyModelType.Membrane;
    pads[0].material = 0.15;
    pads[0].size = 0.9;
    pads[0].decay = 0.35;
    pads[0].level = 0.85;
    pads[0].tsPitchEnvStart = Math.log(200 / 20) / Math.log(100);
    pads[0].tsPitchEnvEnd   = Math.log(40  / 20) / Math.log(100);
    pads[0].tsPitchEnvTime = 0.06;
    // No air, no shell ring -- it's a synth.
    pads[0].airLoading       = 0.0;
    pads[0].couplingStrength = 0.0;
    pads[0].secondaryEnabled = 0.0;
    // Phase 8E: the iconic 808 boom-glide.
    pads[0].tensionModAmt = 0.30;
    // Phase 7: subtle low click for the beater illusion, no noise tail.
    pads[0].clickLayerMix        = 0.35;
    pads[0].clickLayerContactMs  = 0.20;
    pads[0].clickLayerBrightness = 0.30;
    pads[0].noiseLayerMix        = 0.0;

    // ---- Pads 2 & 4: 808 / electronic snares (noise tail + tonal body) ----
    for (const p of [2, 4]) {
        pads[p].exciterType = ExciterType.NoiseBurst;
        pads[p].bodyModel = BodyModelType.Membrane;
        pads[p].level = 0.8;
        pads[p].airLoading       = 0.0;
        pads[p].couplingStrength = 0.0;
        pads[p].secondaryEnabled = 0.0;
        pads[p].modeScatter      = 0.0;
        // The 808 snare hiss IS the noise layer.
        pads[p].noiseLayerMix      = 0.55;
        pads[p].noiseLayerCutoff   = 0.85;
        pads[p].noiseLayerColor    = 0.82;
        pads[p].noiseLayerDecay    = 0.30;
        pads[p].clickLayerMix      = 0.32;
        pads[p].clickLayerContactMs = 0.18;
    }
    pads[2].material = 0.55; pads[2].size = 0.45; pads[2].decay = 0.35;
    pads[2].noiseBurstDuration = (6 - 2) / 13.0;
    pads[4].material = 0.6;  pads[4].size = 0.4;  pads[4].decay = 0.3;
    pads[4].noiseBurstDuration = (5 - 2) / 13.0;

    // ---- Hats: pads 6 (closed) / 8 (pedal) / 10 (open), choke group 1 ----
    // For NoiseBody-based hats the noise layer is the dominant voice; turn
    // it up and silence the click.
    pads[6].exciterType = ExciterType.NoiseBurst;
    pads[6].bodyModel = BodyModelType.NoiseBody;
    pads[6].material = 0.92;
    pads[6].size = 0.1;
    pads[6].decay = 0.08;
    pads[6].level = 0.75;
    pads[6].chokeGroup = 1;
    pads[6].noiseBurstDuration = (3 - 2) / 13.0;
    pads[6].noiseLayerMix    = 0.85;
    pads[6].noiseLayerCutoff = 0.92;
    pads[6].noiseLayerColor  = 0.85;
    pads[6].noiseLayerDecay  = 0.10;
    pads[6].clickLayerMix    = 0.0;
    pads[6].airLoading       = 0.0;

    pads[8] = Object.assign({}, pads[6]);
    pads[8].material = 0.88; pads[8].size = 0.12; pads[8].decay = 0.06;
    pads[8].level = 0.7;     pads[8].noiseLayerDecay = 0.07;

    pads[10] = Object.assign({}, pads[6]);
    pads[10].material = 0.9; pads[10].size = 0.2; pads[10].decay = 0.5;
    pads[10].noiseLayerDecay = 0.55;

    // ---- Pad 13: Crash ----
    pads[13].exciterType = ExciterType.NoiseBurst;
    pads[13].bodyModel = BodyModelType.NoiseBody;
    pads[13].material = 0.95;
    pads[13].size = 0.35;
    pads[13].decay = 0.7;
    pads[13].level = 0.7;
    pads[13].noiseLayerMix    = 0.55;
    pads[13].noiseLayerCutoff = 0.92;
    pads[13].noiseLayerColor  = 0.82;
    pads[13].noiseLayerDecay  = 0.65;
    pads[13].airLoading       = 0.0;

    // ---- Toms: synth toms with the iconic 808 boom-thud glide ----
    // Earlier revision had end pitches at 45-110 Hz (all sub-bass), which on
    // most monitors collapsed every tom to "low thump" -- the absolute
    // fundamentals differed but the user couldn't perceive it. Lifted to
    // 80-260 Hz so the steady-state body sits in the mid-bass / low-mid
    // range where pitch is reliably audible. Material + decay are also
    // graded per tom so the spectrum shape and envelope character evolve
    // along with the fundamental, not just the absolute pitch.
    // ---- Toms: 808-style Mallet+Membrane with the iconic boom-thud glide
    // The original revision used Impulse, which produces a near-flat
    // spectrum dominated by the click itself -- the body's pitched tail
    // was too short / quiet to distinguish toms by ear. Switched to Mallet
    // exciter (longer attack, more body tone) and slightly longer decays
    // so each tom has an audible pitched body. Pitch envelope still
    // dominates the character (the iconic 808 thump-glide), but now you
    // can actually HEAR which tom is which.
    const tomPads = [5, 7, 9, 11, 12, 14];
    const tomSizes      = [0.85, 0.75, 0.65, 0.55, 0.48, 0.40];
    const tomPitchStart = [220,  260,  310,  370,  430,  500 ]; // Hz
    const tomPitchEnd   = [ 80,   95,  115,  140,  175,  220 ]; // Hz (musical)
    const tomPitchTime  = [0.50, 0.42, 0.36, 0.30, 0.24, 0.18];
    const tomMaterial   = [0.18, 0.25, 0.32, 0.40, 0.50, 0.60];
    const tomDecay      = [0.65, 0.58, 0.50, 0.43, 0.35, 0.28];
    const tomBodyB1     = [0.10, 0.15, 0.20, 0.25, 0.32, 0.42];
    const toLogNorm = (hz) => Math.log(hz / 20) / Math.log(100);
    for (let i = 0; i < tomPads.length; i++) {
        const p = tomPads[i];
        pads[p].exciterType = ExciterType.Mallet; // longer attack -> audible tail
        pads[p].bodyModel   = BodyModelType.Membrane;
        pads[p].material    = tomMaterial[i];
        pads[p].size        = tomSizes[i];
        pads[p].decay       = tomDecay[i];
        pads[p].level       = 0.85;
        pads[p].tsPitchEnvStart = toLogNorm(tomPitchStart[i]);
        pads[p].tsPitchEnvEnd   = toLogNorm(tomPitchEnd[i]);
        pads[p].tsPitchEnvTime  = tomPitchTime[i];
        pads[p].tsPitchEnvCurve = 1.0; // Linear -- audible glide, not a snap
        pads[p].airLoading       = 0.0;
        pads[p].couplingStrength = 0.0;
        pads[p].secondaryEnabled = 0.0;
        pads[p].tensionModAmt    = 0.30;
        // Body-dominant mix: trim noise + click so the body tail is what
        // dominates the perceived pitch, not an identical attack click.
        pads[p].noiseLayerMix    = 0.05;
        pads[p].clickLayerMix    = 0.05;
        pads[p].bodyDampingB1    = tomBodyB1[i];
        pads[p].bodyDampingB3    = 0.10; // mostly metallic, a hint of damping
    }

    // ---- FM-bell perc: clean metallic damping, no noise / no air ----
    // The bulk loop below assigns identical params to 19 pads, so we keep
    // pad 1 as a single "FM-bell perc" sample and disable the rest. The
    // user can re-enable + tweak any slot they want a unique sound on.
    const percPads = [1, 3, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31];
    for (const p of percPads) {
        pads[p].exciterType = ExciterType.FMImpulse;
        pads[p].bodyModel = BodyModelType.Bell;
        pads[p].material = 0.7;
        pads[p].size = 0.25;
        pads[p].decay = 0.2;
        pads[p].level = 0.75;
        pads[p].fmRatio = 0.4;
        pads[p].airLoading       = 0.0;
        pads[p].bodyDampingB3    = 0.0;   // metallic
        pads[p].noiseLayerMix    = 0.0;
        pads[p].clickLayerMix    = 0.15;
        pads[p].clickLayerBrightness = 0.7;
    }

    // Genuinely crafted pads in the 808 kit: kick, two snare variants, six
    // toms (different sizes), three hats (different decays), one crash.
    // Everything else falls through to the FM-bell loop and is identical;
    // disable those so the kit reads as "13 sounds wired", not "32 pads
    // where 19 are the same beep".
    disableUncraftedPads(pads, [
        0,                                    // kick
        2, 4,                                 // snares
        5, 7, 9, 11, 12, 14,                  // toms
        6, 8, 10,                             // hats
        13,                                   // crash
    ]);

    return pads;
}

// ==============================================================================
// Kit 2: Acoustic-Inspired
// ==============================================================================
function acousticKit() {
    const pads = [];
    for (let i = 0; i < kNumPads; i++) pads.push(defaultPad());

    // ---- Pad 0: Acoustic Kick (22" head, beater attack, shell ring) ----
    pads[0].exciterType = ExciterType.Mallet;
    pads[0].bodyModel = BodyModelType.Membrane;
    pads[0].material = 0.35;
    pads[0].size = 0.85;
    pads[0].decay = 0.25;
    pads[0].level = 0.85;
    pads[0].tsPitchEnvStart = Math.log(160 / 20) / Math.log(100);
    pads[0].tsPitchEnvEnd   = Math.log(50  / 20) / Math.log(100);
    pads[0].tsPitchEnvTime = 0.04;
    // Phase 8C: deep airLoading for the "less whistly, more thump" character.
    pads[0].airLoading = 0.78;
    // Phase 8D: shell coupling = realistic kick "body" under the head tone.
    pads[0].couplingStrength  = 0.35;
    pads[0].secondaryEnabled  = 1.0;
    pads[0].secondarySize     = 0.40;
    pads[0].secondaryMaterial = 0.60; // woodier shell
    // Phase 8E: small tension mod = subtle "kerthump" glide at high velocity.
    pads[0].tensionModAmt = 0.18;
    // Phase 7: prominent beater click, almost no noise tail.
    pads[0].clickLayerMix        = 0.70;
    pads[0].clickLayerContactMs  = 0.20;
    pads[0].clickLayerBrightness = 0.45;
    pads[0].noiseLayerMix        = 0.10;

    // ---- Pad 2: Acoustic Snare (snare wires + shell ring + stick tick) ----
    pads[2].exciterType = ExciterType.NoiseBurst;
    pads[2].bodyModel = BodyModelType.Membrane;
    pads[2].material = 0.5;
    pads[2].size = 0.5;
    pads[2].decay = 0.4;
    pads[2].level = 0.82;
    pads[2].noiseBurstDuration = (8 - 2) / 13.0;
    // Phase 7: snare-wire noise tail is the defining character.
    pads[2].noiseLayerMix        = 0.65;
    pads[2].noiseLayerCutoff     = 0.72;
    pads[2].noiseLayerResonance  = 0.15;
    pads[2].noiseLayerDecay      = 0.45;
    pads[2].noiseLayerColor      = 0.62;
    // Phase 7: stick attack tick.
    pads[2].clickLayerMix        = 0.55;
    pads[2].clickLayerContactMs  = 0.15;
    pads[2].clickLayerBrightness = 0.78;
    // Phase 8C: smaller head, modest airLoading; snare wires scatter modes.
    pads[2].airLoading  = 0.50;
    pads[2].modeScatter = 0.15;
    // Phase 8D: shell ring under the head.
    pads[2].couplingStrength  = 0.25;
    pads[2].secondaryEnabled  = 1.0;
    pads[2].secondarySize     = 0.55;
    pads[2].secondaryMaterial = 0.50;

    // ---- Pad 4: Side Stick (sharp wood click, no head ring) ----
    pads[4].exciterType = ExciterType.Impulse;
    pads[4].bodyModel = BodyModelType.Shell;
    pads[4].material = 0.8;
    pads[4].size = 0.2;
    pads[4].decay = 0.15;
    pads[4].level = 0.78;
    // Click dominates; no air, lots of mode scatter (wood-on-rim is noisy).
    pads[4].clickLayerMix        = 0.85;
    pads[4].clickLayerContactMs  = 0.10;
    pads[4].clickLayerBrightness = 0.85;
    pads[4].noiseLayerMix = 0.0;
    pads[4].airLoading    = 0.0;
    pads[4].modeScatter   = 0.40;

    // ---- Hi-hats: pads 6 (closed) / 8 (pedal) / 10 (open), choke group 1
    // Cymbals are metallic, so override bodyDampingB3 = 0 (pure flat damping)
    // -- the legacy decay-derived B3 would otherwise model wood/plastic.
    pads[6].exciterType = ExciterType.NoiseBurst;
    pads[6].bodyModel = BodyModelType.NoiseBody;
    pads[6].material = 0.88;
    pads[6].size = 0.15;
    pads[6].decay = 0.1;
    pads[6].level = 0.75;
    pads[6].chokeGroup = 1;
    pads[6].noiseLayerMix       = 0.70;
    pads[6].noiseLayerCutoff    = 0.86;
    pads[6].noiseLayerColor     = 0.78;
    pads[6].noiseLayerDecay     = 0.10;
    pads[6].clickLayerMix       = 0.18;
    pads[6].airLoading          = 0.0; // cymbals don't load air
    pads[6].modeScatter         = 0.35;
    pads[6].bodyDampingB3       = 0.0;  // metallic
    pads[6].bodyDampingB1       = 0.55; // short closed-hat decay

    pads[8] = Object.assign({}, pads[6]);
    pads[8].decay           = 0.05;
    pads[8].noiseLayerDecay = 0.06; // pedal: even shorter
    pads[8].bodyDampingB1   = 0.65;
    pads[8].chokeGroup      = 1;

    pads[10] = Object.assign({}, pads[6]);
    pads[10].decay           = 0.6;
    pads[10].noiseLayerDecay = 0.55;
    pads[10].bodyDampingB1   = 0.20; // open: long ringing decay
    pads[10].chokeGroup      = 1;

    // ---- Toms: pads 5,7,9,11,12,14 -- size graded high-to-low ----
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
        // Phase 8C: deeper toms have higher airLoading (Rossing 1982).
        pads[p].airLoading  = 0.70;
        pads[p].modeScatter = 0.10;
        // Phase 8A: toms ring longer than the global b1=0.4 default. Larger
        // toms decay slower; b1 grades with size index. b1 = 0.18 .. 0.28
        // gives t60 = ~0.78 s .. ~0.51 s across the six toms. Wood-leaning
        // b3 = 0.5 keeps the tonal modes bright and the high modes damped.
        pads[p].bodyDampingB1 = 0.18 + 0.02 * i;
        pads[p].bodyDampingB3 = 0.50;
        // Phase 8D: shell ring scaled with tom size; smaller toms have
        // proportionally smaller shells.
        pads[p].couplingStrength  = 0.40;
        pads[p].secondaryEnabled  = 1.0;
        pads[p].secondarySize     = 0.30 + 0.02 * i;
        pads[p].secondaryMaterial = 0.55;
        // Phase 8E: tom "kerthump" glide -- the canonical reason 8E exists.
        pads[p].tensionModAmt = 0.22;
        // Phase 7: head texture under the modal body, mallet attack.
        pads[p].noiseLayerMix        = 0.18;
        pads[p].noiseLayerCutoff     = 0.45;
        pads[p].clickLayerMix        = 0.50;
        pads[p].clickLayerContactMs  = 0.32;
        pads[p].clickLayerBrightness = 0.55;
    }

    // ---- Cymbals: pads 13,15,16,17,19,21,23 (crashes / rides) ----
    const cymbalPads = [13, 15, 16, 17, 19, 21, 23];
    for (const p of cymbalPads) {
        pads[p].exciterType = ExciterType.NoiseBurst;
        pads[p].bodyModel = BodyModelType.NoiseBody;
        pads[p].material = 0.95;
        pads[p].size = 0.3;
        pads[p].decay = 0.8;
        pads[p].level = 0.72;
        // Highly inharmonic, metallic, long decay, no air.
        pads[p].modeScatter   = 0.55;
        pads[p].airLoading    = 0.0;
        pads[p].bodyDampingB3 = 0.0;   // metallic damping law
        pads[p].bodyDampingB1 = 0.08;  // long sustain
        // Phase 7: bright shimmer noise tail.
        pads[p].noiseLayerMix       = 0.50;
        pads[p].noiseLayerCutoff    = 0.90;
        pads[p].noiseLayerColor     = 0.80;
        pads[p].noiseLayerDecay     = 0.70;
        pads[p].clickLayerMix       = 0.20;
        pads[p].clickLayerBrightness = 0.85;
    }

    // ---- Misc perc: pads 1,3,18,20,22,24..31 (one shared treatment) ----
    const percPads = [1, 3, 18, 20, 22, 24, 25, 26, 27, 28, 29, 30, 31];
    for (const p of percPads) {
        pads[p].exciterType = ExciterType.Mallet;
        pads[p].bodyModel = BodyModelType.Plate;
        pads[p].material = 0.7;
        pads[p].size = 0.3;
        pads[p].decay = 0.3;
        pads[p].level = 0.78;
        pads[p].airLoading           = 0.30;
        pads[p].modeScatter          = 0.18;
        pads[p].clickLayerMix        = 0.40;
        pads[p].clickLayerContactMs  = 0.25;
        pads[p].clickLayerBrightness = 0.65;
        pads[p].noiseLayerMix        = 0.12;
    }

    // Genuinely crafted pads: kick, snare, side stick, six toms, three
    // hats. Pad 13 gets the cymbal-loop treatment as the "Crash 1"
    // representative; pads 15..23 share the same params with 13, so we
    // disable them to avoid 7 identical cymbals. The 13-pad perc loop is
    // also a single shared sound -- disabled too. User can re-enable any
    // slot and tweak from there.
    disableUncraftedPads(pads, [
        0,                                    // kick
        2,                                    // snare
        4,                                    // side stick
        5, 7, 9, 11, 12, 14,                  // toms
        6, 8, 10,                             // hats
        13,                                   // Crash 1 (cymbal loop default)
    ]);

    return pads;
}

// ==============================================================================
// Kit 3: Experimental / FX
// ==============================================================================
function experimentalKit() {
    const pads = [];
    for (let i = 0; i < kNumPads; i++) pads.push(defaultPad());

    // ---- Pad 0: FM Kick (chaotic glide + ringing shell coupling) ----
    pads[0].exciterType = ExciterType.FMImpulse;
    pads[0].bodyModel = BodyModelType.Membrane;
    pads[0].material = 0.2;
    pads[0].size = 0.95;
    pads[0].decay = 0.4;
    pads[0].level = 0.85;
    pads[0].fmRatio = 0.6;
    pads[0].tsPitchEnvStart = Math.log(300 / 20) / Math.log(100);
    pads[0].tsPitchEnvEnd   = Math.log(30  / 20) / Math.log(100);
    pads[0].tsPitchEnvTime = 0.08;
    pads[0].airLoading       = 0.40;
    pads[0].modeScatter      = 0.20;
    // Phase 8D: shell ring couples back hard for a metallic kick body.
    pads[0].couplingStrength  = 0.50;
    pads[0].secondaryEnabled  = 1.0;
    pads[0].secondarySize     = 0.60;
    pads[0].secondaryMaterial = 0.85; // bright/metallic shell
    // Phase 8E: chaotic pitch glide on top of the FM ratio.
    pads[0].tensionModAmt = 0.50;
    pads[0].clickLayerMix       = 0.45;
    pads[0].clickLayerBrightness = 0.40;
    pads[0].noiseLayerMix       = 0.25;

    // ---- Pad 2: Feedback Snare (resonant shell + scatter) ----
    pads[2].exciterType = ExciterType.Feedback;
    pads[2].bodyModel = BodyModelType.Shell;
    pads[2].material = 0.6;
    pads[2].size = 0.4;
    pads[2].decay = 0.35;
    pads[2].level = 0.8;
    pads[2].feedbackAmount = 0.4;
    pads[2].modeScatter      = 0.30;
    pads[2].couplingStrength  = 0.40;
    pads[2].secondaryEnabled  = 1.0;
    pads[2].secondarySize     = 0.45;
    pads[2].secondaryMaterial = 0.70;
    pads[2].tensionModAmt     = 0.40;
    pads[2].noiseLayerMix     = 0.40;
    pads[2].noiseLayerCutoff  = 0.65;
    pads[2].clickLayerMix     = 0.35;

    // ---- Pad 4: Friction FX (long glide, lots of scatter) ----
    pads[4].exciterType = ExciterType.Friction;
    pads[4].bodyModel = BodyModelType.String;
    pads[4].material = 0.5;
    pads[4].size = 0.6;
    pads[4].decay = 0.7;
    pads[4].level = 0.75;
    pads[4].frictionPressure = 0.5;
    pads[4].modeScatter      = 0.50;
    pads[4].couplingStrength = 0.30;
    pads[4].tensionModAmt    = 0.25;
    pads[4].noiseLayerMix    = 0.30;

    // ---- Metal hats with material morph + mode scatter ----
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
    pads[6].modeScatter   = 0.45;
    pads[6].bodyDampingB3 = 0.0; // metallic
    pads[6].noiseLayerMix = 0.55;
    pads[6].noiseLayerCutoff = 0.90;
    pads[6].noiseLayerColor  = 0.85;

    pads[8] = Object.assign({}, pads[6]);
    pads[8].decay = 0.04;

    pads[10] = Object.assign({}, pads[6]);
    pads[10].decay = 0.6;

    // ---- Toms: inharmonic plates with shell coupling and pitch glide ----
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
        pads[p].modeStretch = 0.5;       // slightly inharmonic
        pads[p].nonlinearCoupling = 0.3;
        // Phase 8C: heavy mode scatter for "plates beaten by demons".
        pads[p].modeScatter = 0.50;
        pads[p].airLoading  = 0.20;
        // Phase 8D: shell ringing increases with size index.
        pads[p].couplingStrength  = 0.30 + 0.04 * i;
        pads[p].secondaryEnabled  = 1.0;
        pads[p].secondarySize     = 0.30 + 0.05 * i;
        pads[p].secondaryMaterial = 0.65;
        // Phase 8E: aggressive pitch glide.
        pads[p].tensionModAmt = 0.40;
        pads[p].clickLayerMix = 0.40;
        // Long-ringing chaotic plates (~0.95 s -> ~0.55 s).
        pads[p].bodyDampingB1 = 0.15 + 0.04 * i;
        pads[p].bodyDampingB3 = 0.40;
    }

    // ---- FX pads: every Phase 7+ knob spread across them for variety ----
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
        // Phase 7+: vary every late-phase knob across the FX bank so the
        // experimenter has a wide palette out of the box.
        pads[p].modeScatter      = 0.25 + (i % 7) * 0.06;     // 0.25..0.61
        pads[p].airLoading       = (i % 5) * 0.15;            // 0..0.6
        pads[p].couplingStrength = (i % 3 === 0) ? 0.40 : 0.0;
        pads[p].secondaryEnabled = (i % 3 === 0) ? 1.0 : 0.0;
        pads[p].secondarySize    = 0.35 + (i % 4) * 0.10;
        pads[p].secondaryMaterial = 0.50 + (i % 5) * 0.08;
        pads[p].tensionModAmt    = 0.20 + (i % 6) * 0.08;     // 0.20..0.60
        pads[p].noiseLayerMix    = 0.05 + (i % 5) * 0.10;     // 0.05..0.45
        pads[p].noiseLayerColor  = 0.30 + (i % 7) * 0.10;
        pads[p].clickLayerMix    = 0.10 + (i % 4) * 0.15;
        // Bell-bodied pads get metallic damping.
        if (pads[p].bodyModel === BodyModelType.Bell) {
            pads[p].bodyDampingB3 = 0.0;
        }
    }

    return pads;
}

// ==============================================================================
// Main
// ==============================================================================
const outputBase = process.argv[2] ||
    path.join(__dirname, '..', 'resources', 'presets', 'Kit Presets');

// Kit subcategories MUST match the hardcoded list in
// plugins/membrum/src/preset/membrum_preset_config.h
// (Acoustic, Electronic, Percussive, Unnatural). The browser filters by
// these names; anything else only shows up under "All". The Experimental
// FX kit is mapped to "Unnatural" (its umbrella for inharmonic / non-
// physical sounds and the home of the Unnatural Zone feature).
const kits = [
    { name: '808 Electronic Kit', subdir: 'Electronic', pads: electronicKit() },
    { name: 'Acoustic Studio Kit', subdir: 'Acoustic',  pads: acousticKit() },
    { name: 'Experimental FX Kit', subdir: 'Unnatural', pads: experimentalKit() },
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
