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
    pads[10].bodyDampingB1 = 0.30; // open: long ringing decay
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
        pads[p].bodyDampingB1 = 0.30 + 0.02 * i;
        pads[p].bodyDampingB3 = 0.10;
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
        pads[p].bodyDampingB1 = 0.30;  // long sustain
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
        pads[p].bodyDampingB1 = 0.30 + 0.04 * i;
        pads[p].bodyDampingB3 = 0.10;
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
// Helpers shared by the new kits
// ==============================================================================
const toLogNorm = (hz) => Math.log(hz / 20) / Math.log(100);
const FilterType = { LP: 0.0, HP: 0.5, BP: 1.0 };

// ==============================================================================
// Acoustic Kit 2: Jazz Brushes
//   Sweep-and-tap brush snare, mallet kick, ride-led cymbals, mid toms.
// ==============================================================================
function jazzBrushesKit() {
    const pads = [];
    for (let i = 0; i < kNumPads; i++) pads.push(defaultPad());

    // Kick: soft mallet, deep airLoading, almost no click. Jazz kick "thump"
    // not "punch" -- macroPunch low, macroBodySize mid.
    pads[0].exciterType = ExciterType.Mallet;
    pads[0].bodyModel   = BodyModelType.Membrane;
    pads[0].material    = 0.45;  pads[0].size = 0.72; pads[0].decay = 0.32;
    pads[0].level = 0.78;
    pads[0].tsPitchEnvStart = toLogNorm(140);
    pads[0].tsPitchEnvEnd   = toLogNorm(60);
    pads[0].tsPitchEnvTime  = 0.05;
    pads[0].airLoading       = 0.78;
    pads[0].couplingStrength = 0.30;
    pads[0].secondaryEnabled = 1.0;
    pads[0].secondarySize    = 0.40; pads[0].secondaryMaterial = 0.55;
    pads[0].tensionModAmt    = 0.12;
    pads[0].clickLayerMix       = 0.28;
    pads[0].clickLayerContactMs = 0.40;
    pads[0].clickLayerBrightness = 0.22;
    pads[0].noiseLayerMix       = 0.06;
    pads[0].bodyDampingB1 = 0.40; pads[0].bodyDampingB3 = 0.10;
    pads[0].macroTightness = 0.45; pads[0].macroBrightness = 0.30;
    pads[0].macroBodySize = 0.55;  pads[0].macroPunch     = 0.32;
    pads[0].macroComplexity = 0.40;

    // Brush snare (sweep): long noiseBurst, warm noise color, slight HP
    // filter for the "rasp", morph layer to slowly damp the body during
    // the sweep, no click attack. The defining brush sound.
    //
    // The body's modal tail must die quickly here — the noise sweep IS
    // the sound. High decaySkew + low bodyDampingB1 leaves the fundamental
    // ringing forever (perceived as a sine wave hanging behind the brush)
    // because the host's amp envelope sustains at 1.0 until note-off and
    // some hosts don't send note-off when the user clicks a pad. Keep
    // body damping at the safe default and let the noise tail carry the
    // brush character.
    pads[2].exciterType = ExciterType.NoiseBurst;
    pads[2].bodyModel   = BodyModelType.Membrane;
    pads[2].material = 0.55; pads[2].size = 0.45; pads[2].decay = 0.30;
    pads[2].level = 0.75;
    pads[2].noiseBurstDuration = 0.85;       // long sweep
    pads[2].frictionPressure   = 0.60;
    pads[2].tsFilterType    = FilterType.HP;
    pads[2].tsFilterCutoff  = 0.25;          // gentle HP for rasp
    pads[2].tsFilterResonance = 0.10;
    pads[2].tsFilterEnvAmount = 0.30;
    pads[2].tsFilterEnvAttack  = 0.05;
    pads[2].tsFilterEnvDecay   = 0.20;
    pads[2].tsFilterEnvSustain = 0.30;
    pads[2].tsFilterEnvRelease = 0.40;
    pads[2].morphEnabled = 1.0;
    pads[2].morphStart = 0.55; pads[2].morphEnd = 0.30;
    pads[2].morphDuration = 0.35; pads[2].morphCurve = 0.6;
    pads[2].noiseLayerMix    = 0.70; pads[2].noiseLayerCutoff = 0.55;
    pads[2].noiseLayerColor  = 0.40; pads[2].noiseLayerDecay = 0.55;
    pads[2].noiseLayerResonance = 0.05;
    pads[2].clickLayerMix    = 0.0;          // brushes don't tick
    pads[2].airLoading  = 0.45; pads[2].modeScatter = 0.35;
    // decaySkew left at default (0.5 norm = 0.0 in [-1,+1]) so high modes
    // fade naturally with the fundamental.
    //
    // Phase 8D head/shell coupling MUST be off here. The shell bank uses
    // its own damping (b1 = 1.5..5.5 s^-1, t60 of 1-3 s) and its output
    // is summed back into the primary's excitation bus
    // (drum_voice.h:542). With long shell decay, body and shell keep
    // re-exciting each other forever -> primary modes never fall below
    // kSilenceThreshold -> voice never retires -> indefinite sine-like
    // ring at the shell's fundamental. Brushes don't carry shell ring
    // anyway; the noise sweep IS the sound.
    pads[2].couplingStrength = 0.0;  pads[2].secondaryEnabled = 0.0;
    // bodyDampingB3 stays low: with b3 = n·1e-3 s, a value like 0.45
    // gives 4.5e-4 s, which damps any mode above ~500 Hz almost
    // instantly while the fundamental keeps ringing -- the result is a
    // pitched beep instead of brush rasp. Keep b3 small so the body's
    // modal spectrum decays uniformly with the noise sweep.
    pads[2].bodyDampingB1 = 0.45; pads[2].bodyDampingB3 = 0.05;
    pads[2].macroTightness = 0.30; pads[2].macroBrightness = 0.50;
    pads[2].macroBodySize = 0.55;  pads[2].macroPunch = 0.20;
    pads[2].macroComplexity = 0.65;
    pads[2].couplingAmount = 0.65;

    // Brush snare (tap): same body, short tap with light click. Pad 4 is a
    // sister to pad 2.
    pads[4].exciterType = ExciterType.NoiseBurst;
    pads[4].bodyModel   = BodyModelType.Membrane;
    pads[4].material = 0.55; pads[4].size = 0.45; pads[4].decay = 0.30;
    pads[4].level = 0.78;
    pads[4].noiseBurstDuration = 0.30;
    pads[4].noiseLayerMix    = 0.45; pads[4].noiseLayerCutoff = 0.65;
    pads[4].noiseLayerColor  = 0.55; pads[4].noiseLayerDecay = 0.20;
    pads[4].clickLayerMix    = 0.40; pads[4].clickLayerContactMs = 0.18;
    pads[4].clickLayerBrightness = 0.55;
    pads[4].airLoading  = 0.45; pads[4].modeScatter = 0.18;
    pads[4].couplingStrength = 0.22; pads[4].secondaryEnabled = 1.0;
    pads[4].secondarySize = 0.50; pads[4].secondaryMaterial = 0.50;
    pads[4].bodyDampingB1 = 0.40; pads[4].bodyDampingB3 = 0.10;
    pads[4].macroTightness = 0.55; pads[4].macroBrightness = 0.55;
    pads[4].macroComplexity = 0.40;

    // Hi-hats: light closed (6) / pedal (8) / open (10). Choke group 1.
    pads[6].exciterType = ExciterType.NoiseBurst;
    pads[6].bodyModel   = BodyModelType.NoiseBody;
    pads[6].material = 0.85; pads[6].size = 0.13; pads[6].decay = 0.07;
    pads[6].level = 0.68; pads[6].chokeGroup = 1;
    // Bump noiseLayer mix to drown out the body's modal tail. The hat's
    // sizzle character is in the noise layer; the body just needs to
    // contribute attack transient.
    pads[6].noiseLayerMix = 0.85; pads[6].noiseLayerCutoff = 0.78;
    pads[6].noiseLayerColor = 0.65; pads[6].noiseLayerDecay = 0.08;
    pads[6].clickLayerMix = 0.18; pads[6].clickLayerContactMs = 0.10;
    // High modeScatter (0.70) breaks the plate-mode harmonic structure
    // so no single mode dominates. Without scatter, plate-mode 6-7 at
    // f0×9 sits at the strike-position amplitude peak and rings as a
    // pitched tone for the duration of the body decay -- exactly the
    // user-reported "monotonous beep" on hat / open hat. Cymbals are
    // physically inharmonic; this just matches reality.
    pads[6].airLoading = 0.0; pads[6].modeScatter = 0.70;
    pads[6].bodyDampingB3 = 0.0; pads[6].bodyDampingB1 = 0.60;
    pads[6].macroBrightness = 0.55; pads[6].macroTightness = 0.70;

    pads[8] = Object.assign({}, pads[6]);
    pads[8].decay = 0.05; pads[8].noiseLayerDecay = 0.06;
    pads[8].bodyDampingB1 = 0.70;

    pads[10] = Object.assign({}, pads[6]);
    pads[10].decay = 0.55; pads[10].noiseLayerDecay = 0.78;
    // The parallel noise layer is hardcoded to a bandpass filter in
    // noise_layer.h, and white noise through any BP at audible cutoff
    // produces a spectrally peaked output that reads as a pitched tone.
    // Push the cutoff to the top of the range (18 kHz, near Nyquist at
    // 48 kHz) so the audible spectrum is just the broadband rolloff
    // slope below the peak -- equivalent to a LP-shaped hiss, which is
    // what cymbal sizzle actually sounds like.
    pads[10].noiseLayerCutoff = 0.92;
    pads[10].noiseLayerResonance = 0.0;
    pads[10].noiseLayerMix = 0.55;
    // Body damping bumped to kill the plate-mode tail in ~30 ms.
    pads[10].bodyDampingB1 = 0.55;
    pads[10].bodyDampingB3 = 0.20;
    pads[10].modeScatter = 0.85;

    // Toms (6 at indices 5,7,9,11,12,14): mid-sized mallet, jazz tuned.
    const tomPads      = [5, 7, 9, 11, 12, 14];
    const tomSizes     = [0.72, 0.62, 0.55, 0.48, 0.42, 0.36];
    const tomMaterials = [0.40, 0.43, 0.46, 0.50, 0.55, 0.60];
    const tomDecays    = [0.45, 0.40, 0.36, 0.32, 0.28, 0.24];
    // Bump B1 floor so toms don't ring forever when the host doesn't
    // send note-off. ~0.30 → t60 ≈ 0.46 s for the lowest tom.
    const tomB1        = [0.30, 0.32, 0.34, 0.36, 0.38, 0.42];
    const tomPitchStart = [200, 240, 290, 340, 400, 470];
    const tomPitchEnd   = [110, 135, 165, 200, 240, 290];
    for (let i = 0; i < tomPads.length; i++) {
        const p = tomPads[i];
        pads[p].exciterType = ExciterType.Mallet;
        pads[p].bodyModel   = BodyModelType.Membrane;
        pads[p].material = tomMaterials[i];
        pads[p].size     = tomSizes[i];
        pads[p].decay    = tomDecays[i];
        pads[p].level    = 0.78;
        pads[p].tsPitchEnvStart = toLogNorm(tomPitchStart[i]);
        pads[p].tsPitchEnvEnd   = toLogNorm(tomPitchEnd[i]);
        pads[p].tsPitchEnvTime  = 0.10;
        pads[p].tsPitchEnvCurve = 1.0; // linear glide
        pads[p].airLoading      = 0.65;
        pads[p].modeScatter     = 0.10;
        pads[p].couplingStrength  = 0.32;
        pads[p].secondaryEnabled  = 1.0;
        pads[p].secondarySize     = 0.32 + 0.02 * i;
        pads[p].secondaryMaterial = 0.55;
        pads[p].tensionModAmt = 0.18;
        pads[p].noiseLayerMix = 0.15; pads[p].noiseLayerCutoff = 0.40;
        pads[p].clickLayerMix = 0.40; pads[p].clickLayerContactMs = 0.38;
        pads[p].clickLayerBrightness = 0.45;
        pads[p].bodyDampingB1 = tomB1[i]; pads[p].bodyDampingB3 = 0.10;
        pads[p].macroBodySize = 0.45 + 0.03 * i;
        pads[p].macroPunch     = 0.35;
        pads[p].macroBrightness = 0.40;
        pads[p].macroComplexity = 0.45;
    }

    // Ride cymbal (13): the centerpiece of jazz. Long bell ring, dense modes,
    // bright tip. Output bus 1 so user can mix ride separately from kit.
    pads[13].exciterType = ExciterType.NoiseBurst;
    pads[13].bodyModel   = BodyModelType.Bell;
    pads[13].material = 0.92; pads[13].size = 0.42; pads[13].decay = 0.85;
    pads[13].level = 0.74;
    pads[13].fmRatio = 0.35; pads[13].feedbackAmount = 0.05;
    pads[13].modeScatter = 0.28; pads[13].airLoading = 0.0;
    // Cymbals use NoiseBody whose modalMix is hardcoded at 0.6 in the
    // mapper -- the modal layer dominates over its internal noise mix.
    // With low b1 the dominant plate mode produces a pitched ring rather
    // than cymbal hash. Bump b1 to 0.40 (b1 ≈ 20, t60 ≈ 0.34 s) plus
    // strong b3 (HF rolloff) to mute the high modes that dominate the
    // strike-position amplitude distribution.
    pads[13].bodyDampingB3 = 0.30; pads[13].bodyDampingB1 = 0.40;
    pads[13].modeScatter = 0.85;
    pads[13].noiseLayerMix = 0.30; pads[13].noiseLayerCutoff = 0.85;
    // Force broadband (Q ≈ 0.3) so the LP filter doesn't peak near
    // cutoff -- otherwise zero-crossing analysis still picks up a stable
    // dominant freq at the resonance.
    pads[13].noiseLayerResonance = 0.0;
    pads[13].noiseLayerColor = 0.75; pads[13].noiseLayerDecay = 0.75;
    pads[13].clickLayerMix = 0.45; pads[13].clickLayerContactMs = 0.10;
    pads[13].clickLayerBrightness = 0.85;
    pads[13].outputBus = 1;
    pads[13].macroTightness = 0.30; pads[13].macroBrightness = 0.75;
    pads[13].macroComplexity = 0.55;

    // Crash (15): darker, shorter, wash cymbal.
    pads[15].exciterType = ExciterType.NoiseBurst;
    pads[15].bodyModel   = BodyModelType.NoiseBody;
    pads[15].material = 0.92; pads[15].size = 0.32; pads[15].decay = 0.65;
    pads[15].level = 0.70;
    pads[15].modeScatter = 0.55; pads[15].airLoading = 0.0;
    pads[15].bodyDampingB3 = 0.30; pads[15].bodyDampingB1 = 0.40;
    pads[15].modeScatter = 0.85;
    pads[15].noiseLayerMix = 0.55; pads[15].noiseLayerCutoff = 0.78;
    pads[15].noiseLayerColor = 0.62; pads[15].noiseLayerDecay = 0.60;
    pads[15].clickLayerMix = 0.20; pads[15].clickLayerBrightness = 0.70;
    pads[15].outputBus = 1;

    // Wood block / rim shot perc on pad 1 (E2 alt).
    pads[1].exciterType = ExciterType.Impulse;
    pads[1].bodyModel = BodyModelType.Plate;
    pads[1].material = 0.68; pads[1].size = 0.22; pads[1].decay = 0.20;
    pads[1].level = 0.72;
    pads[1].modeStretch = 0.50;
    pads[1].clickLayerMix = 0.70; pads[1].clickLayerContactMs = 0.12;
    pads[1].clickLayerBrightness = 0.75;
    pads[1].noiseLayerMix = 0.05;
    pads[1].airLoading = 0.10; pads[1].modeScatter = 0.20;
    pads[1].bodyDampingB1 = 0.50; pads[1].bodyDampingB3 = 0.10;

    disableUncraftedPads(pads, [
        0,                              // kick
        1,                              // wood block
        2, 4,                           // brush snare (sweep + tap)
        5, 7, 9, 11, 12, 14,            // toms
        6, 8, 10,                       // hats
        13, 15,                         // ride, crash
    ]);
    return pads;
}

// ==============================================================================
// Acoustic Kit 3: Rock Big Room
//   Maxed kick, big toms, bright cymbals, pronounced shell coupling.
// ==============================================================================
function rockBigRoomKit() {
    const pads = [];
    for (let i = 0; i < kNumPads; i++) pads.push(defaultPad());

    // Big-room kick: wood beater, BIG head, deep air, strong shell. tape-y
    // drive on the body for that "punched in the chest" character.
    pads[0].exciterType = ExciterType.Mallet;
    pads[0].bodyModel = BodyModelType.Membrane;
    pads[0].material = 0.35; pads[0].size = 0.95; pads[0].decay = 0.30;
    pads[0].level = 0.90;
    pads[0].tsPitchEnvStart = toLogNorm(180);
    pads[0].tsPitchEnvEnd   = toLogNorm(45);
    pads[0].tsPitchEnvTime  = 0.05;
    pads[0].tsDriveAmount   = 0.30;     // tape-style saturation
    pads[0].airLoading       = 0.85;
    pads[0].couplingStrength = 0.50;
    pads[0].secondaryEnabled = 1.0;
    pads[0].secondarySize    = 0.55; pads[0].secondaryMaterial = 0.50;
    pads[0].tensionModAmt    = 0.20;
    pads[0].clickLayerMix       = 0.85; pads[0].clickLayerContactMs = 0.18;
    pads[0].clickLayerBrightness = 0.50;
    pads[0].noiseLayerMix = 0.08;
    pads[0].bodyDampingB1 = 0.32; pads[0].bodyDampingB3 = 0.10;
    pads[0].macroTightness = 0.55; pads[0].macroBrightness = 0.40;
    pads[0].macroBodySize = 0.85;  pads[0].macroPunch = 0.85;
    pads[0].macroComplexity = 0.40;
    pads[0].couplingAmount = 0.75;

    // Crack snare: short noise burst, big shell ring, bright stick tick,
    // tape-driven for grit.
    pads[2].exciterType = ExciterType.NoiseBurst;
    pads[2].bodyModel = BodyModelType.Membrane;
    pads[2].material = 0.55; pads[2].size = 0.50; pads[2].decay = 0.35;
    pads[2].level = 0.88;
    pads[2].noiseBurstDuration = 0.40;
    pads[2].tsDriveAmount = 0.22;
    pads[2].tsFilterType = FilterType.BP;
    pads[2].tsFilterCutoff = 0.55;
    pads[2].tsFilterResonance = 0.20;
    pads[2].tsFilterEnvAmount = 0.40;
    pads[2].tsFilterEnvDecay = 0.18;
    pads[2].noiseLayerMix    = 0.70; pads[2].noiseLayerCutoff = 0.78;
    pads[2].noiseLayerResonance = 0.20;
    pads[2].noiseLayerColor  = 0.70; pads[2].noiseLayerDecay = 0.40;
    pads[2].clickLayerMix    = 0.65; pads[2].clickLayerContactMs = 0.12;
    pads[2].clickLayerBrightness = 0.85;
    pads[2].airLoading = 0.55; pads[2].modeScatter = 0.20;
    pads[2].couplingStrength = 0.45; pads[2].secondaryEnabled = 1.0;
    pads[2].secondarySize = 0.55; pads[2].secondaryMaterial = 0.50;
    pads[2].bodyDampingB1 = 0.42; pads[2].bodyDampingB3 = 0.10;
    pads[2].macroPunch = 0.85; pads[2].macroBrightness = 0.70;
    pads[2].macroComplexity = 0.50; pads[2].macroTightness = 0.65;
    pads[2].couplingAmount = 0.70;

    // Rim shot (4)
    pads[4].exciterType = ExciterType.Impulse;
    pads[4].bodyModel   = BodyModelType.Shell;
    pads[4].material = 0.70; pads[4].size = 0.30; pads[4].decay = 0.18;
    pads[4].level = 0.85;
    pads[4].modeStretch = 0.45;
    pads[4].clickLayerMix = 0.95; pads[4].clickLayerContactMs = 0.08;
    pads[4].clickLayerBrightness = 0.92;
    pads[4].noiseLayerMix = 0.20; pads[4].noiseLayerColor = 0.80;
    pads[4].noiseLayerDecay = 0.18;
    pads[4].airLoading = 0.10; pads[4].modeScatter = 0.40;
    pads[4].bodyDampingB1 = 0.50; pads[4].bodyDampingB3 = 0.30;
    pads[4].macroPunch = 0.95;

    // Cymbals: pads 6/8/10 hi-hats, 13/15/17 crash/ride.
    pads[6].exciterType = ExciterType.NoiseBurst;
    pads[6].bodyModel = BodyModelType.NoiseBody;
    pads[6].material = 0.92; pads[6].size = 0.18; pads[6].decay = 0.12;
    pads[6].level = 0.78; pads[6].chokeGroup = 1;
    pads[6].noiseLayerMix = 0.85; pads[6].noiseLayerCutoff = 0.90;
    pads[6].noiseLayerColor = 0.85; pads[6].noiseLayerDecay = 0.10;
    pads[6].clickLayerMix = 0.22;
    // Scatter ≥ 0.65 breaks plate-mode harmonic clumping so the body
    // doesn't expose a single dominant pitch under decay (cymbal-tone bug).
    pads[6].airLoading = 0.0; pads[6].modeScatter = 0.70;
    pads[6].bodyDampingB3 = 0.0; pads[6].bodyDampingB1 = 0.50;

    pads[8] = Object.assign({}, pads[6]);
    pads[8].decay = 0.06; pads[8].noiseLayerDecay = 0.07;
    pads[8].bodyDampingB1 = 0.65;

    pads[10] = Object.assign({}, pads[6]);
    pads[10].decay = 0.65; pads[10].noiseLayerDecay = 0.60;
    pads[10].bodyDampingB1 = 0.30;

    // Toms: deep, large, slow tension glide. Macro punch high.
    const tomPads      = [5, 7, 9, 11, 12, 14];
    const tomSizes     = [0.92, 0.85, 0.75, 0.65, 0.55, 0.48];
    const tomMaterial  = [0.30, 0.34, 0.38, 0.43, 0.50, 0.58];
    const tomDecay     = [0.65, 0.58, 0.50, 0.43, 0.36, 0.30];
    const tomPitchHi   = [180, 220, 270, 330, 400, 480];
    const tomPitchLo   = [70,  85, 105, 130, 165, 215];
    // B1 floor 0.26 → ~0.5 s tail for the deepest tom; rock toms aren't
    // supposed to sustain into the next bar.
    const tomB1        = [0.26, 0.28, 0.30, 0.33, 0.36, 0.40];
    for (let i = 0; i < tomPads.length; i++) {
        const p = tomPads[i];
        pads[p].exciterType = ExciterType.Mallet;
        pads[p].bodyModel   = BodyModelType.Membrane;
        pads[p].material = tomMaterial[i];
        pads[p].size     = tomSizes[i];
        pads[p].decay    = tomDecay[i];
        pads[p].level    = 0.85;
        pads[p].tsPitchEnvStart = toLogNorm(tomPitchHi[i]);
        pads[p].tsPitchEnvEnd   = toLogNorm(tomPitchLo[i]);
        pads[p].tsPitchEnvTime  = 0.08;
        pads[p].tsPitchEnvCurve = 1.0;
        pads[p].tsDriveAmount   = 0.18;   // tape grit on body
        pads[p].airLoading      = 0.78;
        pads[p].modeScatter     = 0.12;
        pads[p].couplingStrength = 0.55;
        pads[p].secondaryEnabled = 1.0;
        pads[p].secondarySize    = 0.40 + 0.02 * i;
        pads[p].secondaryMaterial = 0.55;
        pads[p].tensionModAmt    = 0.30;
        pads[p].noiseLayerMix    = 0.18; pads[p].noiseLayerCutoff = 0.45;
        pads[p].clickLayerMix    = 0.55; pads[p].clickLayerContactMs = 0.30;
        pads[p].clickLayerBrightness = 0.55;
        pads[p].bodyDampingB1 = tomB1[i]; pads[p].bodyDampingB3 = 0.10;
        pads[p].macroPunch = 0.85; pads[p].macroBodySize = 0.55 + 0.05 * i;
        pads[p].couplingAmount = 0.70;
    }

    // Crash 1 (13)
    pads[13].exciterType = ExciterType.NoiseBurst;
    pads[13].bodyModel = BodyModelType.NoiseBody;
    pads[13].material = 0.95; pads[13].size = 0.45; pads[13].decay = 0.85;
    pads[13].level = 0.78;
    pads[13].modeScatter = 0.55; pads[13].airLoading = 0.0;
    pads[13].bodyDampingB3 = 0.0; pads[13].bodyDampingB1 = 0.30;
    pads[13].noiseLayerMix = 0.65; pads[13].noiseLayerCutoff = 0.92;
    pads[13].noiseLayerColor = 0.85; pads[13].noiseLayerDecay = 0.75;
    pads[13].clickLayerMix = 0.30; pads[13].clickLayerBrightness = 0.85;
    pads[13].outputBus = 1;
    pads[13].macroBrightness = 0.85;

    // Ride (15) -- larger size, longer decay, bell ping.
    pads[15] = Object.assign({}, pads[13]);
    pads[15].size = 0.55; pads[15].decay = 0.95;
    pads[15].bodyModel = BodyModelType.Bell;
    pads[15].fmRatio = 0.30; pads[15].feedbackAmount = 0.05;
    pads[15].clickLayerMix = 0.55; pads[15].clickLayerBrightness = 0.92;
    pads[15].outputBus = 1;

    // Splash (17) -- short, very bright.
    pads[17] = Object.assign({}, pads[13]);
    pads[17].size = 0.22; pads[17].decay = 0.28;
    pads[17].noiseLayerDecay = 0.25;
    pads[17].outputBus = 1;

    return Object.assign(pads, {
        __opts: {
            maxPolyphony: 12,
            globalCoupling: 0.30,
            snareBuzz: 0.35,
            tomResonance: 0.45,
            couplingDelayMs: 1.2,
            // Sympathetic snare buzz when toms are hit. Codec clamps
            // coefficients to [0, 0.05] (CouplingMatrix::kMaxCoefficient),
            // so all override values live in that range.
            overrides: [
                { src: 5, dst: 2, coeff: 0.030 },
                { src: 7, dst: 2, coeff: 0.025 },
                { src: 9, dst: 2, coeff: 0.020 },
                { src: 0, dst: 2, coeff: 0.022 },
            ],
        },
        __crafted: [0, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 17],
    });
}

// ==============================================================================
// Acoustic Kit 4: Vintage Wood
//   Wood-shell emphasis, rim-shot snare, smaller toms, woodblocks. tape-y
//   drive throughout for vintage warmth.
// ==============================================================================
function vintageWoodKit() {
    const pads = [];
    for (let i = 0; i < kNumPads; i++) pads.push(defaultPad());

    // Wood-shell kick: smaller, woody material, prominent shell ring,
    // saturated body.
    pads[0].exciterType = ExciterType.Mallet;
    pads[0].bodyModel = BodyModelType.Membrane;
    pads[0].material = 0.28; pads[0].size = 0.78; pads[0].decay = 0.27;
    pads[0].level = 0.82;
    pads[0].tsPitchEnvStart = toLogNorm(150);
    pads[0].tsPitchEnvEnd   = toLogNorm(55);
    pads[0].tsPitchEnvTime  = 0.045;
    pads[0].tsDriveAmount   = 0.45;
    pads[0].tsFoldAmount    = 0.10;
    pads[0].airLoading       = 0.70;
    pads[0].couplingStrength = 0.55;
    pads[0].secondaryEnabled = 1.0;
    pads[0].secondarySize    = 0.42; pads[0].secondaryMaterial = 0.32;  // very woody
    pads[0].tensionModAmt    = 0.18;
    pads[0].clickLayerMix       = 0.62; pads[0].clickLayerContactMs = 0.25;
    pads[0].clickLayerBrightness = 0.40;
    pads[0].noiseLayerMix = 0.10;
    pads[0].bodyDampingB1 = 0.34; pads[0].bodyDampingB3 = 0.10;
    pads[0].macroBodySize = 0.70; pads[0].macroPunch = 0.65;
    pads[0].macroBrightness = 0.30; pads[0].macroComplexity = 0.45;

    // Wood-shell snare (rim-shot character): heavy modeScatter, prominent
    // shell coupling.
    pads[2].exciterType = ExciterType.NoiseBurst;
    pads[2].bodyModel = BodyModelType.Membrane;
    pads[2].material = 0.45; pads[2].size = 0.45; pads[2].decay = 0.32;
    pads[2].level = 0.82;
    pads[2].noiseBurstDuration = 0.35;
    pads[2].tsDriveAmount = 0.32;
    pads[2].tsFilterType = FilterType.HP;
    pads[2].tsFilterCutoff = 0.32;
    pads[2].tsFilterResonance = 0.18;
    pads[2].tsFilterEnvAmount = 0.45;
    pads[2].tsFilterEnvDecay = 0.15;
    pads[2].noiseLayerMix    = 0.55; pads[2].noiseLayerCutoff = 0.62;
    pads[2].noiseLayerColor  = 0.50; pads[2].noiseLayerDecay = 0.32;
    pads[2].clickLayerMix    = 0.70; pads[2].clickLayerContactMs = 0.16;
    pads[2].clickLayerBrightness = 0.70;
    pads[2].airLoading = 0.40; pads[2].modeScatter = 0.30;
    pads[2].couplingStrength = 0.55; pads[2].secondaryEnabled = 1.0;
    pads[2].secondarySize = 0.50; pads[2].secondaryMaterial = 0.32;  // wood
    pads[2].bodyDampingB1 = 0.44; pads[2].bodyDampingB3 = 0.10;
    pads[2].macroTightness = 0.70; pads[2].macroBrightness = 0.55;
    pads[2].macroComplexity = 0.55;

    // Side stick (4)
    pads[4].exciterType = ExciterType.Impulse;
    pads[4].bodyModel = BodyModelType.Shell;
    pads[4].material = 0.30; pads[4].size = 0.20; pads[4].decay = 0.18;
    pads[4].level = 0.78;
    pads[4].clickLayerMix = 0.92; pads[4].clickLayerContactMs = 0.10;
    pads[4].clickLayerBrightness = 0.78;
    pads[4].noiseLayerMix = 0.05;
    pads[4].airLoading = 0.0; pads[4].modeScatter = 0.55;
    pads[4].bodyDampingB1 = 0.42; pads[4].bodyDampingB3 = 0.10;
    pads[4].macroComplexity = 0.30;

    // Hi-hats: short, dry, low color (vintage-darker hat).
    pads[6].exciterType = ExciterType.NoiseBurst;
    pads[6].bodyModel = BodyModelType.NoiseBody;
    pads[6].material = 0.85; pads[6].size = 0.13; pads[6].decay = 0.08;
    pads[6].level = 0.72; pads[6].chokeGroup = 1;
    pads[6].noiseLayerMix = 0.85; pads[6].noiseLayerCutoff = 0.72;
    pads[6].noiseLayerColor = 0.55; pads[6].noiseLayerDecay = 0.08;
    pads[6].clickLayerMix = 0.20;
    // Scatter ≥ 0.65 breaks plate-mode harmonic clumping so the body
    // doesn't expose a single dominant pitch (cymbal-tone bug).
    pads[6].airLoading = 0.0; pads[6].modeScatter = 0.70;
    pads[6].bodyDampingB3 = 0.0; pads[6].bodyDampingB1 = 0.55;
    pads[6].macroBrightness = 0.40;

    pads[8] = Object.assign({}, pads[6]);
    pads[8].decay = 0.05; pads[8].noiseLayerDecay = 0.06;

    pads[10] = Object.assign({}, pads[6]);
    pads[10].decay = 0.45; pads[10].noiseLayerDecay = 0.42;
    pads[10].bodyDampingB1 = 0.30;

    // Toms: smaller, woodier than the Big Room kit. Shells use wood material.
    const tomPads      = [5, 7, 9, 11, 12, 14];
    const tomSizes     = [0.72, 0.62, 0.55, 0.48, 0.42, 0.35];
    const tomMaterial  = [0.30, 0.32, 0.36, 0.42, 0.48, 0.55];
    const tomDecay     = [0.42, 0.38, 0.34, 0.30, 0.26, 0.22];
    // B1 floor 0.30 keeps wood toms tight; t60 ~0.46 s for the lowest.
    const tomB1        = [0.30, 0.32, 0.35, 0.38, 0.42, 0.46];
    const tomPitchHi   = [200, 240, 290, 340, 400, 480];
    const tomPitchLo   = [95, 115, 140, 170, 210, 260];
    for (let i = 0; i < tomPads.length; i++) {
        const p = tomPads[i];
        pads[p].exciterType = ExciterType.Mallet;
        pads[p].bodyModel   = BodyModelType.Membrane;
        pads[p].material = tomMaterial[i];
        pads[p].size     = tomSizes[i];
        pads[p].decay    = tomDecay[i];
        pads[p].level    = 0.78;
        pads[p].tsPitchEnvStart = toLogNorm(tomPitchHi[i]);
        pads[p].tsPitchEnvEnd   = toLogNorm(tomPitchLo[i]);
        pads[p].tsPitchEnvTime  = 0.10;
        pads[p].tsPitchEnvCurve = 1.0;
        pads[p].tsDriveAmount   = 0.25;     // tape warmth
        pads[p].airLoading      = 0.55;
        pads[p].modeScatter     = 0.18;
        pads[p].couplingStrength = 0.50;
        pads[p].secondaryEnabled = 1.0;
        pads[p].secondarySize    = 0.32 + 0.02 * i;
        pads[p].secondaryMaterial = 0.30;     // wood shells
        pads[p].tensionModAmt    = 0.22;
        pads[p].noiseLayerMix    = 0.15; pads[p].noiseLayerCutoff = 0.42;
        pads[p].clickLayerMix    = 0.50; pads[p].clickLayerContactMs = 0.32;
        pads[p].clickLayerBrightness = 0.45;
        pads[p].bodyDampingB1 = tomB1[i]; pads[p].bodyDampingB3 = 0.10;
        pads[p].macroBodySize = 0.45 + 0.04 * i;
        pads[p].macroBrightness = 0.35;
    }

    // Crash 1 -- darker / warmer cymbal.
    pads[13].exciterType = ExciterType.NoiseBurst;
    pads[13].bodyModel = BodyModelType.NoiseBody;
    pads[13].material = 0.92; pads[13].size = 0.30; pads[13].decay = 0.65;
    pads[13].level = 0.72;
    pads[13].modeScatter = 0.55; pads[13].airLoading = 0.0;
    pads[13].bodyDampingB3 = 0.0; pads[13].bodyDampingB1 = 0.30;
    pads[13].noiseLayerMix = 0.55; pads[13].noiseLayerCutoff = 0.78;
    pads[13].noiseLayerColor = 0.65; pads[13].noiseLayerDecay = 0.60;
    pads[13].clickLayerMix = 0.18; pads[13].clickLayerBrightness = 0.65;

    // Wood block hi/lo (1, 3): pitched plates, dry, no air.
    pads[1].exciterType = ExciterType.Impulse;
    pads[1].bodyModel = BodyModelType.Plate;
    pads[1].material = 0.30; pads[1].size = 0.20; pads[1].decay = 0.18;
    pads[1].level = 0.75;
    pads[1].modeStretch = 0.55; pads[1].decaySkew = 0.30;
    pads[1].clickLayerMix = 0.78; pads[1].clickLayerContactMs = 0.10;
    pads[1].clickLayerBrightness = 0.80;
    pads[1].noiseLayerMix = 0.0;
    pads[1].airLoading = 0.0; pads[1].modeScatter = 0.18;
    pads[1].bodyDampingB1 = 0.45; pads[1].bodyDampingB3 = 0.10;

    pads[3] = Object.assign({}, pads[1]);
    pads[3].size = 0.30; pads[3].decay = 0.22;       // lower-pitched
    pads[3].material = 0.28;
    pads[3].clickLayerBrightness = 0.65;

    // Cowbell (15): brassy plate.
    pads[15].exciterType = ExciterType.Impulse;
    pads[15].bodyModel = BodyModelType.Bell;
    pads[15].material = 0.78; pads[15].size = 0.26; pads[15].decay = 0.30;
    pads[15].level = 0.75;
    pads[15].fmRatio = 0.45;
    pads[15].clickLayerMix = 0.55; pads[15].clickLayerContactMs = 0.10;
    pads[15].clickLayerBrightness = 0.70;
    pads[15].noiseLayerMix = 0.10;
    pads[15].airLoading = 0.05; pads[15].modeScatter = 0.20;
    pads[15].bodyDampingB3 = 0.0; pads[15].bodyDampingB1 = 0.40;
    pads[15].macroBrightness = 0.65;

    return Object.assign(pads, {
        __opts: {
            maxPolyphony: 10,
            globalCoupling: 0.20,
            snareBuzz: 0.30,
            tomResonance: 0.30,
            couplingDelayMs: 1.0,
        },
        __crafted: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15],
    });
}

// ==============================================================================
// Acoustic Kit 5: Orchestral
//   Timpani-like toms with strong tension mod, gongs, triangle, crotales.
// ==============================================================================
function orchestralKit() {
    const pads = [];
    for (let i = 0; i < kNumPads; i++) pads.push(defaultPad());

    // Timpani (kick slot): large head, strong tension glide (the orchestral
    // pitch-bend pedal). Long decay, deep airLoading, prominent shell.
    pads[0].exciterType = ExciterType.Mallet;
    pads[0].bodyModel = BodyModelType.Membrane;
    pads[0].material = 0.30; pads[0].size = 0.95; pads[0].decay = 0.85;
    pads[0].level = 0.82;
    pads[0].tsPitchEnvStart = toLogNorm(180);
    pads[0].tsPitchEnvEnd   = toLogNorm(85);
    pads[0].tsPitchEnvTime  = 0.10;
    pads[0].tsPitchEnvCurve = 1.0;
    pads[0].airLoading       = 0.92;     // huge body of air
    pads[0].couplingStrength = 0.45;
    pads[0].secondaryEnabled = 1.0;
    pads[0].secondarySize    = 0.65; pads[0].secondaryMaterial = 0.50;
    pads[0].tensionModAmt    = 0.55;     // pedal-bend character
    pads[0].clickLayerMix       = 0.32; pads[0].clickLayerContactMs = 0.28;
    pads[0].clickLayerBrightness = 0.30;
    pads[0].noiseLayerMix = 0.12;
    pads[0].bodyDampingB1 = 0.30; pads[0].bodyDampingB3 = 0.10;
    pads[0].macroBodySize = 0.95; pads[0].macroComplexity = 0.55;
    pads[0].couplingAmount = 0.85;

    // Bass drum (orchestral, soft mallet, very low pitch)
    pads[2].exciterType = ExciterType.Mallet;
    pads[2].bodyModel = BodyModelType.Membrane;
    pads[2].material = 0.32; pads[2].size = 0.92; pads[2].decay = 0.55;
    pads[2].level = 0.85;
    pads[2].tsPitchEnvStart = toLogNorm(110);
    pads[2].tsPitchEnvEnd   = toLogNorm(40);
    pads[2].tsPitchEnvTime  = 0.06;
    pads[2].airLoading       = 0.90;
    pads[2].couplingStrength = 0.40;
    pads[2].secondaryEnabled = 1.0;
    pads[2].secondarySize    = 0.55; pads[2].secondaryMaterial = 0.45;
    pads[2].tensionModAmt    = 0.20;
    pads[2].clickLayerMix       = 0.30; pads[2].clickLayerContactMs = 0.42;
    pads[2].clickLayerBrightness = 0.20;
    pads[2].noiseLayerMix = 0.08;
    pads[2].bodyDampingB1 = 0.30; pads[2].bodyDampingB3 = 0.08;
    pads[2].couplingAmount = 0.85;

    // Snare drum (orchestral wires, taut)
    pads[4].exciterType = ExciterType.NoiseBurst;
    pads[4].bodyModel = BodyModelType.Membrane;
    pads[4].material = 0.55; pads[4].size = 0.42; pads[4].decay = 0.45;
    pads[4].level = 0.82;
    pads[4].noiseBurstDuration = 0.55;
    pads[4].noiseLayerMix    = 0.75; pads[4].noiseLayerCutoff = 0.78;
    pads[4].noiseLayerResonance = 0.20;
    pads[4].noiseLayerColor  = 0.70; pads[4].noiseLayerDecay = 0.50;
    pads[4].clickLayerMix    = 0.55; pads[4].clickLayerContactMs = 0.12;
    pads[4].clickLayerBrightness = 0.78;
    pads[4].airLoading = 0.50; pads[4].modeScatter = 0.25;
    pads[4].couplingStrength = 0.32; pads[4].secondaryEnabled = 1.0;
    pads[4].secondarySize = 0.55; pads[4].secondaryMaterial = 0.50;
    pads[4].bodyDampingB1 = 0.30; pads[4].bodyDampingB3 = 0.10;
    pads[4].macroBrightness = 0.65; pads[4].macroComplexity = 0.55;

    // Timpani toms (5 timpani at 5,7,9,11,14): graded sizes + tension mod.
    const timpaniPads = [5, 7, 9, 11, 14];
    const timpaniSize = [0.92, 0.85, 0.78, 0.70, 0.62];
    const timpaniHi   = [180, 220, 280, 350, 440];
    const timpaniLo   = [80, 100, 130, 165, 215];
    const timpaniDecay = [0.80, 0.72, 0.65, 0.58, 0.50];
    const timpaniB1    = [0.10, 0.12, 0.14, 0.17, 0.21];
    for (let i = 0; i < timpaniPads.length; i++) {
        const p = timpaniPads[i];
        pads[p].exciterType = ExciterType.Mallet;
        pads[p].bodyModel = BodyModelType.Membrane;
        pads[p].material = 0.32 + 0.04 * i;
        pads[p].size = timpaniSize[i];
        pads[p].decay = timpaniDecay[i];
        pads[p].level = 0.80;
        pads[p].tsPitchEnvStart = toLogNorm(timpaniHi[i]);
        pads[p].tsPitchEnvEnd   = toLogNorm(timpaniLo[i]);
        pads[p].tsPitchEnvTime  = 0.12;
        pads[p].tsPitchEnvCurve = 1.0;
        pads[p].airLoading       = 0.85;
        pads[p].modeScatter      = 0.08;
        pads[p].couplingStrength = 0.40;
        pads[p].secondaryEnabled = 1.0;
        pads[p].secondarySize    = 0.45 + 0.02 * i;
        pads[p].secondaryMaterial = 0.50;
        pads[p].tensionModAmt    = 0.40;       // pedal-bend
        pads[p].noiseLayerMix    = 0.12; pads[p].noiseLayerCutoff = 0.40;
        pads[p].clickLayerMix    = 0.32; pads[p].clickLayerContactMs = 0.28;
        pads[p].clickLayerBrightness = 0.32;
        pads[p].bodyDampingB1 = timpaniB1[i]; pads[p].bodyDampingB3 = 0.10;
        pads[p].macroBodySize = 0.85 - 0.05 * i;
        pads[p].macroComplexity = 0.55;
        pads[p].couplingAmount = 0.80;
    }

    // Triangle (12): tiny Bell, super bright, very long decay, no air.
    pads[12].exciterType = ExciterType.Impulse;
    pads[12].bodyModel = BodyModelType.Bell;
    pads[12].material = 0.95; pads[12].size = 0.10; pads[12].decay = 0.85;
    pads[12].level = 0.65;
    pads[12].fmRatio = 0.45;
    pads[12].modeStretch = 0.55;
    pads[12].clickLayerMix = 0.55; pads[12].clickLayerContactMs = 0.08;
    pads[12].clickLayerBrightness = 0.95;
    pads[12].noiseLayerMix = 0.0;
    pads[12].airLoading = 0.0;
    pads[12].bodyDampingB3 = 0.0; pads[12].bodyDampingB1 = 0.30;
    pads[12].macroBrightness = 0.95; pads[12].macroComplexity = 0.30;

    // Hi-hats / suspended cymbal: pads 6 (closed), 8 (pedal), 10 (sus open
    // roll). Last is the orchestral suspended cymbal swell.
    pads[6].exciterType = ExciterType.NoiseBurst;
    pads[6].bodyModel = BodyModelType.NoiseBody;
    pads[6].material = 0.88; pads[6].size = 0.16; pads[6].decay = 0.15;
    pads[6].level = 0.70; pads[6].chokeGroup = 1;
    pads[6].noiseLayerMix = 0.65; pads[6].noiseLayerCutoff = 0.82;
    pads[6].noiseLayerColor = 0.70; pads[6].noiseLayerDecay = 0.12;
    pads[6].clickLayerMix = 0.20;
    pads[6].airLoading = 0.0; pads[6].modeScatter = 0.30;
    pads[6].bodyDampingB3 = 0.0; pads[6].bodyDampingB1 = 0.45;

    pads[8] = Object.assign({}, pads[6]);
    pads[8].decay = 0.06; pads[8].noiseLayerDecay = 0.07;
    pads[8].bodyDampingB1 = 0.65;

    // Suspended-cymbal roll: long decay, soft attack via morph layer
    // (filter sweeps brighter as it sustains).
    pads[10].exciterType = ExciterType.NoiseBurst;
    pads[10].bodyModel = BodyModelType.NoiseBody;
    pads[10].material = 0.95; pads[10].size = 0.42; pads[10].decay = 0.95;
    pads[10].level = 0.72;
    pads[10].morphEnabled = 1.0;
    pads[10].morphStart = 0.55; pads[10].morphEnd = 0.95;
    pads[10].morphDuration = 0.85; pads[10].morphCurve = 0.4;
    pads[10].modeScatter = 0.65; pads[10].airLoading = 0.0;
    pads[10].bodyDampingB3 = 0.0; pads[10].bodyDampingB1 = 0.30;
    pads[10].noiseLayerMix = 0.65; pads[10].noiseLayerCutoff = 0.82;
    pads[10].noiseLayerColor = 0.78; pads[10].noiseLayerDecay = 0.95;
    pads[10].clickLayerMix = 0.0;
    pads[10].decaySkew = 0.55;
    pads[10].outputBus = 1;

    // Gong (13): huge, very long decay, bell + heavy mode scatter.
    pads[13].exciterType = ExciterType.Mallet;
    pads[13].bodyModel = BodyModelType.Bell;
    pads[13].material = 0.85; pads[13].size = 0.85; pads[13].decay = 0.95;
    pads[13].level = 0.78;
    pads[13].fmRatio = 0.30; pads[13].feedbackAmount = 0.10;
    pads[13].modeStretch = 0.55;
    pads[13].morphEnabled = 1.0;
    pads[13].morphStart = 0.85; pads[13].morphEnd = 0.55;
    pads[13].morphDuration = 0.85; pads[13].morphCurve = 0.5;
    pads[13].modeScatter = 0.65; pads[13].airLoading = 0.0;
    pads[13].bodyDampingB3 = 0.0; pads[13].bodyDampingB1 = 0.30;
    pads[13].clickLayerMix = 0.30; pads[13].clickLayerContactMs = 0.22;
    pads[13].clickLayerBrightness = 0.30;
    pads[13].noiseLayerMix = 0.20; pads[13].noiseLayerCutoff = 0.55;
    pads[13].noiseLayerColor = 0.45; pads[13].noiseLayerDecay = 0.92;
    pads[13].decaySkew = 0.65;
    pads[13].tensionModAmt = 0.22;
    pads[13].outputBus = 1;
    pads[13].macroBodySize = 0.95; pads[13].macroComplexity = 0.85;
    pads[13].couplingAmount = 0.95;

    // Crotales hi/lo (15, 17): pitched bell with FM character.
    pads[15].exciterType = ExciterType.Mallet;
    pads[15].bodyModel = BodyModelType.Bell;
    pads[15].material = 0.92; pads[15].size = 0.18; pads[15].decay = 0.85;
    pads[15].level = 0.72;
    pads[15].fmRatio = 0.55;
    pads[15].clickLayerMix = 0.40; pads[15].clickLayerBrightness = 0.85;
    pads[15].noiseLayerMix = 0.0;
    pads[15].airLoading = 0.0;
    pads[15].bodyDampingB3 = 0.0; pads[15].bodyDampingB1 = 0.30;
    pads[15].outputBus = 1;
    pads[15].macroBrightness = 0.85;

    pads[17] = Object.assign({}, pads[15]);
    pads[17].size = 0.25; pads[17].fmRatio = 0.40;
    pads[17].material = 0.88;

    // Tubular bell (3): String body, very long decay.
    pads[3].exciterType = ExciterType.Mallet;
    pads[3].bodyModel = BodyModelType.String;
    pads[3].material = 0.85; pads[3].size = 0.55; pads[3].decay = 0.92;
    pads[3].level = 0.72;
    pads[3].modeStretch = 0.50;
    pads[3].clickLayerMix = 0.40; pads[3].clickLayerContactMs = 0.20;
    pads[3].clickLayerBrightness = 0.65;
    pads[3].noiseLayerMix = 0.10;
    pads[3].airLoading = 0.0;
    pads[3].bodyDampingB1 = 0.30; pads[3].bodyDampingB3 = 0.20;
    pads[3].decaySkew = 0.55;

    return Object.assign(pads, {
        __opts: {
            maxPolyphony: 16,
            globalCoupling: 0.45,        // sympathetic resonance ON
            snareBuzz: 0.20,
            tomResonance: 0.55,           // strong timpani-to-timpani sympathy
            couplingDelayMs: 1.6,
            // Sympathetic resonance between timpani.
            overrides: [
                { src: 5, dst: 7, coeff: 0.040 },
                { src: 7, dst: 9, coeff: 0.040 },
                { src: 9, dst: 11, coeff: 0.038 },
                { src: 11, dst: 14, coeff: 0.030 },
                { src: 0, dst: 5, coeff: 0.025 },
            ],
        },
        __crafted: [0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 17],
    });
}

// ==============================================================================
// Electronic Kit 2: 909 Drum Machine
//   Roland TR-909 character: tight kick, snappy snare, sizzly hats.
// ==============================================================================
function nineOhNineKit() {
    const pads = [];
    for (let i = 0; i < kNumPads; i++) pads.push(defaultPad());

    // 909 kick: short, punchy, mid-pitched, modest sub. No air, no shell.
    pads[0].exciterType = ExciterType.Impulse;
    pads[0].bodyModel = BodyModelType.Membrane;
    pads[0].material = 0.18; pads[0].size = 0.78; pads[0].decay = 0.22;
    pads[0].level = 0.85;
    pads[0].tsPitchEnvStart = toLogNorm(180);
    pads[0].tsPitchEnvEnd   = toLogNorm(55);
    pads[0].tsPitchEnvTime  = 0.04;
    pads[0].tsDriveAmount   = 0.20;     // 909 grit
    pads[0].airLoading       = 0.0;
    pads[0].couplingStrength = 0.0;
    pads[0].secondaryEnabled = 0.0;
    pads[0].tensionModAmt    = 0.10;
    pads[0].clickLayerMix       = 0.55; pads[0].clickLayerContactMs = 0.12;
    pads[0].clickLayerBrightness = 0.55;
    pads[0].noiseLayerMix = 0.05;
    pads[0].bodyDampingB1 = 0.42; pads[0].bodyDampingB3 = 0.30;
    pads[0].macroPunch = 0.85; pads[0].macroBodySize = 0.45;

    // 909 snare: bright, short, fizzy noise tail.
    pads[2].exciterType = ExciterType.NoiseBurst;
    pads[2].bodyModel = BodyModelType.Membrane;
    pads[2].material = 0.62; pads[2].size = 0.38; pads[2].decay = 0.20;
    pads[2].level = 0.82;
    pads[2].noiseBurstDuration = 0.30;
    pads[2].noiseLayerMix    = 0.75; pads[2].noiseLayerCutoff = 0.92;
    pads[2].noiseLayerColor  = 0.92; pads[2].noiseLayerDecay = 0.22;
    pads[2].clickLayerMix    = 0.40; pads[2].clickLayerContactMs = 0.10;
    pads[2].clickLayerBrightness = 0.92;
    pads[2].airLoading = 0.0; pads[2].modeScatter = 0.0;
    pads[2].bodyDampingB1 = 0.45; pads[2].bodyDampingB3 = 0.30;
    pads[2].macroBrightness = 0.85; pads[2].macroPunch = 0.65;

    // Rim shot (4): high-pitched click.
    pads[4].exciterType = ExciterType.Impulse;
    pads[4].bodyModel = BodyModelType.Plate;
    pads[4].material = 0.75; pads[4].size = 0.18; pads[4].decay = 0.10;
    pads[4].level = 0.78;
    pads[4].clickLayerMix = 0.92; pads[4].clickLayerBrightness = 0.95;
    pads[4].clickLayerContactMs = 0.08;
    pads[4].noiseLayerMix = 0.0;
    pads[4].bodyDampingB1 = 0.55; pads[4].bodyDampingB3 = 0.0;

    // Hi-hats: very bright, sizzly. 909 character.
    pads[6].exciterType = ExciterType.NoiseBurst;
    pads[6].bodyModel = BodyModelType.NoiseBody;
    pads[6].material = 0.95; pads[6].size = 0.10; pads[6].decay = 0.07;
    pads[6].level = 0.72; pads[6].chokeGroup = 1;
    pads[6].noiseLayerMix = 0.85; pads[6].noiseLayerCutoff = 0.95;
    pads[6].noiseLayerColor = 0.95; pads[6].noiseLayerDecay = 0.07;
    pads[6].clickLayerMix = 0.0;
    pads[6].airLoading = 0.0; pads[6].modeScatter = 0.0;
    pads[6].bodyDampingB3 = 0.0; pads[6].bodyDampingB1 = 0.65;

    pads[8] = Object.assign({}, pads[6]);
    pads[8].decay = 0.04; pads[8].noiseLayerDecay = 0.05;
    pads[8].bodyDampingB1 = 0.78;

    pads[10] = Object.assign({}, pads[6]);
    pads[10].decay = 0.42; pads[10].noiseLayerDecay = 0.40;
    pads[10].bodyDampingB1 = 0.30;

    // Toms: 909 toms have a fast pitch sweep + short body.
    const tomPads      = [5, 7, 9, 11, 12, 14];
    const tomSizes     = [0.65, 0.55, 0.48, 0.40, 0.34, 0.28];
    const tomMaterial  = [0.20, 0.25, 0.30, 0.36, 0.42, 0.50];
    const tomDecay     = [0.32, 0.28, 0.25, 0.22, 0.20, 0.18];
    const tomPitchHi   = [240, 290, 350, 420, 500, 590];
    const tomPitchLo   = [85, 100, 120, 145, 175, 210];
    for (let i = 0; i < tomPads.length; i++) {
        const p = tomPads[i];
        pads[p].exciterType = ExciterType.Impulse;
        pads[p].bodyModel = BodyModelType.Membrane;
        pads[p].material = tomMaterial[i];
        pads[p].size = tomSizes[i];
        pads[p].decay = tomDecay[i];
        pads[p].level = 0.78;
        pads[p].tsPitchEnvStart = toLogNorm(tomPitchHi[i]);
        pads[p].tsPitchEnvEnd   = toLogNorm(tomPitchLo[i]);
        pads[p].tsPitchEnvTime  = 0.10;
        pads[p].tsPitchEnvCurve = 0.0;       // exponential -- 909 character
        pads[p].airLoading = 0.0;
        pads[p].couplingStrength = 0.0;
        pads[p].secondaryEnabled = 0.0;
        pads[p].tensionModAmt = 0.20;
        pads[p].noiseLayerMix = 0.05;
        pads[p].clickLayerMix = 0.32; pads[p].clickLayerContactMs = 0.15;
        pads[p].clickLayerBrightness = 0.55;
        pads[p].bodyDampingB1 = 0.30; pads[p].bodyDampingB3 = 0.18;
        pads[p].macroPunch = 0.65;
    }

    // Crash (13): bright, medium decay, lots of scatter.
    pads[13].exciterType = ExciterType.NoiseBurst;
    pads[13].bodyModel = BodyModelType.NoiseBody;
    pads[13].material = 0.95; pads[13].size = 0.32; pads[13].decay = 0.55;
    pads[13].level = 0.72;
    pads[13].modeScatter = 0.50; pads[13].airLoading = 0.0;
    pads[13].bodyDampingB3 = 0.0; pads[13].bodyDampingB1 = 0.30;
    pads[13].noiseLayerMix = 0.65; pads[13].noiseLayerCutoff = 0.95;
    pads[13].noiseLayerColor = 0.92; pads[13].noiseLayerDecay = 0.55;

    // Clap (15): noise burst with double-trigger feel via long contact.
    pads[15].exciterType = ExciterType.NoiseBurst;
    pads[15].bodyModel = BodyModelType.NoiseBody;
    pads[15].material = 0.85; pads[15].size = 0.18; pads[15].decay = 0.18;
    pads[15].level = 0.78;
    pads[15].noiseBurstDuration = 0.55;
    pads[15].noiseLayerMix = 0.85; pads[15].noiseLayerCutoff = 0.78;
    pads[15].noiseLayerResonance = 0.40;
    pads[15].noiseLayerColor = 0.65; pads[15].noiseLayerDecay = 0.20;
    pads[15].clickLayerMix = 0.45; pads[15].clickLayerContactMs = 0.22;
    pads[15].clickLayerBrightness = 0.62;
    pads[15].airLoading = 0.0; pads[15].modeScatter = 0.40;
    pads[15].bodyDampingB1 = 0.50; pads[15].bodyDampingB3 = 0.0;
    pads[15].macroBrightness = 0.65; pads[15].macroComplexity = 0.55;

    return Object.assign(pads, {
        __opts: {
            maxPolyphony: 8,
            globalCoupling: 0.0, snareBuzz: 0.0,
            tomResonance: 0.0, couplingDelayMs: 1.0,
        },
        __crafted: [0, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15],
    });
}

// ==============================================================================
// Electronic Kit 3: LinnDrum / CR-78
//   Early digital drum machine: clean PCM-style hits + FM bell perc.
// ==============================================================================
function linnDrumKit() {
    const pads = [];
    for (let i = 0; i < kNumPads; i++) pads.push(defaultPad());

    // LinnDrum kick: medium-short, low fold for digital character.
    pads[0].exciterType = ExciterType.FMImpulse;
    pads[0].bodyModel = BodyModelType.Membrane;
    pads[0].material = 0.20; pads[0].size = 0.72; pads[0].decay = 0.28;
    pads[0].level = 0.85;
    pads[0].fmRatio = 0.30;
    pads[0].tsPitchEnvStart = toLogNorm(160);
    pads[0].tsPitchEnvEnd   = toLogNorm(50);
    pads[0].tsPitchEnvTime  = 0.04;
    pads[0].tsFoldAmount = 0.18;
    pads[0].airLoading = 0.0; pads[0].couplingStrength = 0.0;
    pads[0].clickLayerMix = 0.40; pads[0].clickLayerContactMs = 0.15;
    pads[0].clickLayerBrightness = 0.45;
    pads[0].noiseLayerMix = 0.06;
    pads[0].bodyDampingB1 = 0.38; pads[0].bodyDampingB3 = 0.30;
    pads[0].macroPunch = 0.55; pads[0].macroBodySize = 0.45;

    // LinnDrum snare: short crisp.
    pads[2].exciterType = ExciterType.NoiseBurst;
    pads[2].bodyModel = BodyModelType.Membrane;
    pads[2].material = 0.58; pads[2].size = 0.38; pads[2].decay = 0.22;
    pads[2].level = 0.80;
    pads[2].noiseBurstDuration = 0.32;
    pads[2].noiseLayerMix = 0.62; pads[2].noiseLayerCutoff = 0.78;
    pads[2].noiseLayerColor = 0.72; pads[2].noiseLayerDecay = 0.22;
    pads[2].clickLayerMix = 0.42; pads[2].clickLayerContactMs = 0.12;
    pads[2].clickLayerBrightness = 0.78;
    pads[2].airLoading = 0.0; pads[2].modeScatter = 0.05;
    pads[2].bodyDampingB1 = 0.40; pads[2].bodyDampingB3 = 0.30;

    // CR-78-style cowbell: FM bell.
    pads[4].exciterType = ExciterType.FMImpulse;
    pads[4].bodyModel = BodyModelType.Bell;
    pads[4].material = 0.78; pads[4].size = 0.22; pads[4].decay = 0.30;
    pads[4].level = 0.75;
    pads[4].fmRatio = 0.55;
    pads[4].clickLayerMix = 0.30; pads[4].clickLayerBrightness = 0.62;
    pads[4].noiseLayerMix = 0.08;
    pads[4].airLoading = 0.0;
    pads[4].bodyDampingB3 = 0.0; pads[4].bodyDampingB1 = 0.32;
    pads[4].macroBrightness = 0.65;

    // Hats: dirty, low color (CR-78 character).
    pads[6].exciterType = ExciterType.NoiseBurst;
    pads[6].bodyModel = BodyModelType.NoiseBody;
    pads[6].material = 0.78; pads[6].size = 0.12; pads[6].decay = 0.10;
    pads[6].level = 0.70; pads[6].chokeGroup = 1;
    pads[6].noiseLayerMix = 0.78; pads[6].noiseLayerCutoff = 0.65;
    pads[6].noiseLayerColor = 0.50; pads[6].noiseLayerDecay = 0.10;
    pads[6].clickLayerMix = 0.18;
    pads[6].airLoading = 0.0;
    pads[6].bodyDampingB3 = 0.0; pads[6].bodyDampingB1 = 0.55;

    pads[8] = Object.assign({}, pads[6]);
    pads[8].decay = 0.06; pads[8].noiseLayerDecay = 0.07;

    pads[10] = Object.assign({}, pads[6]);
    pads[10].decay = 0.40; pads[10].noiseLayerDecay = 0.38;
    pads[10].bodyDampingB1 = 0.30;

    // Toms: PCM-style, short, snappy.
    const tomPads = [5, 7, 9, 11, 12, 14];
    const tomSizes = [0.62, 0.55, 0.48, 0.42, 0.36, 0.30];
    const tomMat   = [0.25, 0.30, 0.36, 0.42, 0.50, 0.58];
    const tomHi    = [220, 270, 330, 400, 480, 570];
    const tomLo    = [90, 110, 135, 165, 200, 240];
    for (let i = 0; i < tomPads.length; i++) {
        const p = tomPads[i];
        pads[p].exciterType = ExciterType.Mallet;
        pads[p].bodyModel = BodyModelType.Membrane;
        pads[p].material = tomMat[i]; pads[p].size = tomSizes[i];
        pads[p].decay = 0.30 - 0.02 * i; pads[p].level = 0.75;
        pads[p].tsPitchEnvStart = toLogNorm(tomHi[i]);
        pads[p].tsPitchEnvEnd   = toLogNorm(tomLo[i]);
        pads[p].tsPitchEnvTime  = 0.08;
        pads[p].tsPitchEnvCurve = 0.0;
        pads[p].airLoading = 0.0; pads[p].modeScatter = 0.0;
        pads[p].tensionModAmt = 0.20;
        pads[p].noiseLayerMix = 0.06;
        pads[p].clickLayerMix = 0.32; pads[p].clickLayerBrightness = 0.55;
        pads[p].bodyDampingB1 = 0.32; pads[p].bodyDampingB3 = 0.20;
    }

    // Cabasa (1): NoiseBody short shake.
    pads[1].exciterType = ExciterType.NoiseBurst;
    pads[1].bodyModel = BodyModelType.NoiseBody;
    pads[1].material = 0.88; pads[1].size = 0.08; pads[1].decay = 0.08;
    pads[1].level = 0.65;
    pads[1].noiseBurstDuration = 0.18;
    pads[1].noiseLayerMix = 0.85; pads[1].noiseLayerCutoff = 0.85;
    pads[1].noiseLayerColor = 0.75; pads[1].noiseLayerDecay = 0.10;
    pads[1].clickLayerMix = 0.0;
    pads[1].airLoading = 0.0; pads[1].modeScatter = 0.0;
    pads[1].bodyDampingB3 = 0.0; pads[1].bodyDampingB1 = 0.55;

    // Clave (3): pitched FM bell click.
    pads[3].exciterType = ExciterType.FMImpulse;
    pads[3].bodyModel = BodyModelType.Bell;
    pads[3].material = 0.85; pads[3].size = 0.12; pads[3].decay = 0.18;
    pads[3].level = 0.75;
    pads[3].fmRatio = 0.62;
    pads[3].clickLayerMix = 0.65; pads[3].clickLayerBrightness = 0.85;
    pads[3].noiseLayerMix = 0.0;
    pads[3].airLoading = 0.0;
    pads[3].bodyDampingB3 = 0.0; pads[3].bodyDampingB1 = 0.40;

    // Crash (13)
    pads[13].exciterType = ExciterType.NoiseBurst;
    pads[13].bodyModel = BodyModelType.NoiseBody;
    pads[13].material = 0.92; pads[13].size = 0.30; pads[13].decay = 0.55;
    pads[13].level = 0.70;
    pads[13].modeScatter = 0.45; pads[13].airLoading = 0.0;
    pads[13].bodyDampingB3 = 0.0; pads[13].bodyDampingB1 = 0.30;
    pads[13].noiseLayerMix = 0.55; pads[13].noiseLayerCutoff = 0.78;
    pads[13].noiseLayerColor = 0.62; pads[13].noiseLayerDecay = 0.55;

    return Object.assign(pads, {
        __opts: {
            maxPolyphony: 8,
            globalCoupling: 0.0, snareBuzz: 0.0,
            tomResonance: 0.0, couplingDelayMs: 1.0,
        },
        __crafted: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14],
    });
}

// ==============================================================================
// Electronic Kit 4: Modular West Coast
//   Generative, lots of FM/Feedback, mode injection, sparse coupling matrix.
// ==============================================================================
function modularWestCoastKit() {
    const pads = [];
    for (let i = 0; i < kNumPads; i++) pads.push(defaultPad());

    // West Coast kick: Feedback exciter, large, with feedback runaway.
    pads[0].exciterType = ExciterType.Feedback;
    pads[0].bodyModel = BodyModelType.Membrane;
    pads[0].material = 0.25; pads[0].size = 0.85; pads[0].decay = 0.40;
    pads[0].level = 0.80;
    pads[0].feedbackAmount = 0.45;
    pads[0].tsPitchEnvStart = toLogNorm(220);
    pads[0].tsPitchEnvEnd   = toLogNorm(45);
    pads[0].tsPitchEnvTime  = 0.06;
    pads[0].tsFoldAmount    = 0.30;       // wavefolder body
    pads[0].airLoading = 0.0;
    pads[0].couplingStrength = 0.20; pads[0].secondaryEnabled = 1.0;
    pads[0].secondarySize = 0.45; pads[0].secondaryMaterial = 0.85;
    pads[0].tensionModAmt = 0.45;
    pads[0].clickLayerMix = 0.30; pads[0].clickLayerContactMs = 0.18;
    pads[0].clickLayerBrightness = 0.55;
    pads[0].noiseLayerMix = 0.20;
    pads[0].nonlinearCoupling = 0.40;
    pads[0].bodyDampingB1 = 0.30; pads[0].bodyDampingB3 = 0.30;
    pads[0].macroComplexity = 0.85; pads[0].macroPunch = 0.65;

    // Snare: FM + Plate, lots of mode inject + nonlinear coupling.
    pads[2].exciterType = ExciterType.FMImpulse;
    pads[2].bodyModel = BodyModelType.Plate;
    pads[2].material = 0.55; pads[2].size = 0.40; pads[2].decay = 0.35;
    pads[2].level = 0.78;
    pads[2].fmRatio = 0.55;
    pads[2].modeStretch = 0.55;
    // ModeInject is an undamped HarmonicOscillatorBank with no envelope of
    // its own. When the host doesn't send NoteOff (the typical pad-click
    // case), the amp envelope sustains at 1.0 forever and ModeInject's
    // sine tones ride on top of that as a constant DC of harmonic content
    // -- audible as an infinite ring at the body's f0. Zero it across the
    // board on all kits this generator emits; the feature is unsafe to
    // expose in a percussion preset until the DSP grows a built-in
    // release envelope.
    pads[2].modeInjectAmount = 0.0;
    pads[2].nonlinearCoupling = 0.45;
    pads[2].morphEnabled = 1.0;
    pads[2].morphStart = 0.55; pads[2].morphEnd = 0.80;
    pads[2].morphDuration = 0.30; pads[2].morphCurve = 0.6;
    pads[2].noiseLayerMix = 0.30; pads[2].noiseLayerCutoff = 0.65;
    pads[2].noiseLayerColor = 0.62; pads[2].noiseLayerDecay = 0.30;
    pads[2].clickLayerMix = 0.45; pads[2].clickLayerBrightness = 0.78;
    pads[2].airLoading = 0.0; pads[2].modeScatter = 0.45;
    pads[2].bodyDampingB1 = 0.30; pads[2].bodyDampingB3 = 0.30;

    // Sub-bell perc (4): FM Bell with feedback.
    pads[4].exciterType = ExciterType.FMImpulse;
    pads[4].bodyModel = BodyModelType.Bell;
    pads[4].material = 0.55; pads[4].size = 0.32; pads[4].decay = 0.55;
    pads[4].level = 0.75;
    pads[4].fmRatio = 0.72; pads[4].feedbackAmount = 0.30;
    pads[4].modeStretch = 0.50;
    pads[4].nonlinearCoupling = 0.30;
    pads[4].clickLayerMix = 0.45; pads[4].clickLayerBrightness = 0.85;
    pads[4].noiseLayerMix = 0.10;
    pads[4].airLoading = 0.0;
    pads[4].bodyDampingB3 = 0.0; pads[4].bodyDampingB1 = 0.30;
    pads[4].decaySkew = 0.45;

    // Friction string drone (1): low pad-like sustained tone.
    pads[1].exciterType = ExciterType.Friction;
    pads[1].bodyModel = BodyModelType.String;
    pads[1].material = 0.50; pads[1].size = 0.55; pads[1].decay = 0.85;
    pads[1].level = 0.65;
    pads[1].frictionPressure = 0.55;
    pads[1].modeStretch = 0.40;
    pads[1].nonlinearCoupling = 0.55;
    pads[1].morphEnabled = 1.0;
    pads[1].morphStart = 0.40; pads[1].morphEnd = 0.85;
    pads[1].morphDuration = 0.55; pads[1].morphCurve = 0.5;
    pads[1].decaySkew = 0.65;
    pads[1].noiseLayerMix = 0.20; pads[1].noiseLayerCutoff = 0.45;
    pads[1].clickLayerMix = 0.0;
    pads[1].bodyDampingB1 = 0.30; pads[1].bodyDampingB3 = 0.20;
    pads[1].outputBus = 1;
    pads[1].macroComplexity = 0.85;

    // Hats: FM bell + scatter.
    pads[6].exciterType = ExciterType.FMImpulse;
    pads[6].bodyModel = BodyModelType.Bell;
    pads[6].material = 0.92; pads[6].size = 0.10; pads[6].decay = 0.08;
    pads[6].level = 0.68; pads[6].chokeGroup = 1;
    pads[6].fmRatio = 0.78;
    pads[6].modeScatter = 0.55;
    pads[6].noiseLayerMix = 0.40; pads[6].noiseLayerCutoff = 0.92;
    pads[6].noiseLayerColor = 0.85; pads[6].noiseLayerDecay = 0.08;
    pads[6].clickLayerMix = 0.25; pads[6].clickLayerBrightness = 0.92;
    pads[6].airLoading = 0.0;
    pads[6].bodyDampingB3 = 0.0; pads[6].bodyDampingB1 = 0.55;

    pads[8] = Object.assign({}, pads[6]);
    pads[8].decay = 0.05; pads[8].fmRatio = 0.65;

    pads[10] = Object.assign({}, pads[6]);
    pads[10].decay = 0.50; pads[10].fmRatio = 0.55;
    pads[10].noiseLayerDecay = 0.48;

    // Toms: inharmonic plates with feedback.
    const tomPads = [5, 7, 9, 11, 12, 14];
    for (let i = 0; i < tomPads.length; i++) {
        const p = tomPads[i];
        pads[p].exciterType = (i % 2 === 0) ? ExciterType.FMImpulse : ExciterType.Feedback;
        pads[p].bodyModel = BodyModelType.Plate;
        pads[p].material = 0.40 + i * 0.06;
        pads[p].size = 0.85 - i * 0.10;
        pads[p].decay = 0.65 - i * 0.05;
        pads[p].level = 0.75;
        pads[p].fmRatio = 0.30 + i * 0.08;
        pads[p].feedbackAmount = 0.20 + (i % 3) * 0.10;
        pads[p].modeStretch = 0.30 + i * 0.05;
        // ModeInject is undamped without note-off; zero it.
        pads[p].modeInjectAmount = 0.0;
        pads[p].nonlinearCoupling = 0.40;
        pads[p].decaySkew = 0.50;
        pads[p].tsPitchEnvStart = toLogNorm(280 + i * 60);
        pads[p].tsPitchEnvEnd   = toLogNorm(70 + i * 25);
        pads[p].tsPitchEnvTime  = 0.10;
        pads[p].airLoading = 0.0;
        pads[p].modeScatter = 0.30 + (i % 4) * 0.10;
        pads[p].couplingStrength = 0.30; pads[p].secondaryEnabled = 1.0;
        pads[p].secondarySize = 0.40; pads[p].secondaryMaterial = 0.75;
        pads[p].tensionModAmt = 0.45;
        pads[p].noiseLayerMix = 0.18; pads[p].noiseLayerColor = 0.70;
        pads[p].clickLayerMix = 0.30; pads[p].clickLayerBrightness = 0.65;
        pads[p].bodyDampingB1 = 0.30 + 0.03 * i; pads[p].bodyDampingB3 = 0.30;
        pads[p].macroComplexity = 0.75;
        pads[p].couplingAmount = 0.65;
    }

    // Crash (13): Bell with mode scatter.
    pads[13].exciterType = ExciterType.FMImpulse;
    pads[13].bodyModel = BodyModelType.Bell;
    pads[13].material = 0.92; pads[13].size = 0.45; pads[13].decay = 0.78;
    pads[13].level = 0.72;
    pads[13].fmRatio = 0.45;
    pads[13].modeStretch = 0.55;
    pads[13].modeScatter = 0.65;
    pads[13].nonlinearCoupling = 0.45;
    pads[13].airLoading = 0.0;
    pads[13].bodyDampingB3 = 0.0; pads[13].bodyDampingB1 = 0.30;
    pads[13].noiseLayerMix = 0.40; pads[13].noiseLayerCutoff = 0.92;
    pads[13].noiseLayerColor = 0.85; pads[13].noiseLayerDecay = 0.72;
    pads[13].clickLayerMix = 0.35; pads[13].clickLayerBrightness = 0.85;

    return Object.assign(pads, {
        __opts: {
            maxPolyphony: 12,
            stealingPolicy: 1,
            globalCoupling: 0.65,         // chaotic cross-coupling
            snareBuzz: 0.0, tomResonance: 0.55,
            couplingDelayMs: 1.4,
            // Generative cross-couplings.
            overrides: [
                { src: 0, dst: 5, coeff: 0.045 },
                { src: 5, dst: 7, coeff: 0.040 },
                { src: 7, dst: 9, coeff: 0.035 },
                { src: 1, dst: 13, coeff: 0.050 },
                { src: 4, dst: 11, coeff: 0.030 },
            ],
        },
        __crafted: [0, 1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14],
    });
}

// ==============================================================================
// Electronic Kit 5: Trap Modern
//   Heavy 808 sub kick, snappy snares, crispy hats with scatter.
// ==============================================================================
function trapModernKit() {
    const pads = [];
    for (let i = 0; i < kNumPads; i++) pads.push(defaultPad());

    // Massive sub 808 kick: long decay, MAX tensionMod for the boom-glide.
    pads[0].exciterType = ExciterType.Impulse;
    pads[0].bodyModel = BodyModelType.Membrane;
    pads[0].material = 0.10; pads[0].size = 0.95; pads[0].decay = 0.65;
    pads[0].level = 0.92;
    pads[0].tsPitchEnvStart = toLogNorm(250);
    pads[0].tsPitchEnvEnd   = toLogNorm(35);
    pads[0].tsPitchEnvTime  = 0.06;
    pads[0].tsDriveAmount   = 0.18;
    pads[0].airLoading = 0.0; pads[0].couplingStrength = 0.0;
    pads[0].tensionModAmt = 0.85;
    pads[0].clickLayerMix = 0.42; pads[0].clickLayerContactMs = 0.18;
    pads[0].clickLayerBrightness = 0.32;
    pads[0].noiseLayerMix = 0.0;
    pads[0].decaySkew = 0.45;
    pads[0].bodyDampingB1 = 0.30; pads[0].bodyDampingB3 = 0.30;
    pads[0].macroPunch = 0.95; pads[0].macroBodySize = 0.95;

    // Modern crispy snare: bright, layered, prominent click.
    pads[2].exciterType = ExciterType.NoiseBurst;
    pads[2].bodyModel = BodyModelType.Membrane;
    pads[2].material = 0.65; pads[2].size = 0.42; pads[2].decay = 0.30;
    pads[2].level = 0.88;
    pads[2].noiseBurstDuration = 0.40;
    pads[2].tsFilterType = FilterType.HP;
    pads[2].tsFilterCutoff = 0.45;
    pads[2].tsFilterResonance = 0.30;
    pads[2].tsFilterEnvAmount = 0.55;
    pads[2].tsFilterEnvDecay = 0.18;
    pads[2].tsDriveAmount = 0.20;
    pads[2].noiseLayerMix = 0.78; pads[2].noiseLayerCutoff = 0.92;
    pads[2].noiseLayerResonance = 0.30;
    pads[2].noiseLayerColor = 0.95; pads[2].noiseLayerDecay = 0.32;
    pads[2].clickLayerMix = 0.85; pads[2].clickLayerContactMs = 0.10;
    pads[2].clickLayerBrightness = 0.95;
    pads[2].airLoading = 0.0; pads[2].modeScatter = 0.20;
    pads[2].bodyDampingB1 = 0.32; pads[2].bodyDampingB3 = 0.30;
    pads[2].macroBrightness = 0.95; pads[2].macroPunch = 0.85;
    pads[2].macroTightness = 0.85;

    // Snare layer (4): rim shot snap.
    pads[4].exciterType = ExciterType.Impulse;
    pads[4].bodyModel = BodyModelType.Plate;
    pads[4].material = 0.78; pads[4].size = 0.16; pads[4].decay = 0.10;
    pads[4].level = 0.85;
    pads[4].clickLayerMix = 0.95; pads[4].clickLayerContactMs = 0.06;
    pads[4].clickLayerBrightness = 0.95;
    pads[4].noiseLayerMix = 0.18; pads[4].noiseLayerColor = 0.92;
    pads[4].airLoading = 0.0; pads[4].modeScatter = 0.20;
    pads[4].bodyDampingB1 = 0.50; pads[4].bodyDampingB3 = 0.0;

    // Trap hats: 5 different decay variants for fast rolls (closed/closed2/
    // closed3/pedal/open).
    const hatPads = [6, 7, 8];                 // closed variants 1/2/3
    const hatDecays = [0.05, 0.08, 0.12];
    for (let i = 0; i < hatPads.length; i++) {
        const p = hatPads[i];
        pads[p].exciterType = ExciterType.NoiseBurst;
        pads[p].bodyModel = BodyModelType.NoiseBody;
        pads[p].material = 0.92; pads[p].size = 0.10;
        pads[p].decay = hatDecays[i]; pads[p].level = 0.72;
        pads[p].chokeGroup = 1;
        pads[p].noiseLayerMix = 0.85; pads[p].noiseLayerCutoff = 0.95;
        pads[p].noiseLayerColor = 0.95;
        pads[p].noiseLayerDecay = hatDecays[i];
        pads[p].noiseLayerResonance = 0.10;
        pads[p].clickLayerMix = 0.18; pads[p].clickLayerBrightness = 0.92;
        pads[p].airLoading = 0.0; pads[p].modeScatter = 0.25;
        pads[p].bodyDampingB3 = 0.0;
        pads[p].bodyDampingB1 = 0.78 - 0.10 * i;
    }

    pads[10].exciterType = ExciterType.NoiseBurst;
    pads[10].bodyModel = BodyModelType.NoiseBody;
    pads[10].material = 0.92; pads[10].size = 0.18; pads[10].decay = 0.55;
    pads[10].level = 0.72; pads[10].chokeGroup = 1;
    pads[10].noiseLayerMix = 0.78; pads[10].noiseLayerCutoff = 0.92;
    pads[10].noiseLayerColor = 0.92; pads[10].noiseLayerDecay = 0.50;
    pads[10].modeScatter = 0.30;
    pads[10].bodyDampingB3 = 0.0; pads[10].bodyDampingB1 = 0.30;

    // Toms: snappy with quick tension mod, bright.
    const tomPads = [5, 9, 11, 12, 14];
    const tomSizes = [0.65, 0.55, 0.45, 0.38, 0.32];
    const tomMat   = [0.20, 0.28, 0.36, 0.45, 0.55];
    const tomHi    = [240, 290, 360, 440, 540];
    const tomLo    = [80, 100, 130, 165, 210];
    for (let i = 0; i < tomPads.length; i++) {
        const p = tomPads[i];
        pads[p].exciterType = ExciterType.Impulse;
        pads[p].bodyModel = BodyModelType.Membrane;
        pads[p].material = tomMat[i]; pads[p].size = tomSizes[i];
        pads[p].decay = 0.30 - 0.03 * i; pads[p].level = 0.78;
        pads[p].tsPitchEnvStart = toLogNorm(tomHi[i]);
        pads[p].tsPitchEnvEnd   = toLogNorm(tomLo[i]);
        pads[p].tsPitchEnvTime  = 0.06;
        pads[p].tsPitchEnvCurve = 0.0;
        pads[p].tsDriveAmount   = 0.18;
        pads[p].airLoading = 0.0;
        pads[p].tensionModAmt = 0.55;
        pads[p].noiseLayerMix = 0.05;
        pads[p].clickLayerMix = 0.45; pads[p].clickLayerBrightness = 0.78;
        pads[p].bodyDampingB1 = 0.30 + 0.03 * i; pads[p].bodyDampingB3 = 0.30;
        pads[p].macroPunch = 0.78;
    }

    // Trap perc clave (1)
    pads[1].exciterType = ExciterType.Impulse;
    pads[1].bodyModel = BodyModelType.Bell;
    pads[1].material = 0.92; pads[1].size = 0.10; pads[1].decay = 0.15;
    pads[1].level = 0.78;
    pads[1].fmRatio = 0.65;
    pads[1].clickLayerMix = 0.85; pads[1].clickLayerBrightness = 0.95;
    pads[1].clickLayerContactMs = 0.06;
    pads[1].noiseLayerMix = 0.0;
    pads[1].airLoading = 0.0;
    pads[1].bodyDampingB3 = 0.0; pads[1].bodyDampingB1 = 0.42;

    // Crash (13)
    pads[13].exciterType = ExciterType.NoiseBurst;
    pads[13].bodyModel = BodyModelType.NoiseBody;
    pads[13].material = 0.95; pads[13].size = 0.32; pads[13].decay = 0.72;
    pads[13].level = 0.72;
    pads[13].modeScatter = 0.65; pads[13].airLoading = 0.0;
    pads[13].bodyDampingB3 = 0.0; pads[13].bodyDampingB1 = 0.30;
    pads[13].noiseLayerMix = 0.78; pads[13].noiseLayerCutoff = 0.95;
    pads[13].noiseLayerColor = 0.92; pads[13].noiseLayerDecay = 0.70;

    return Object.assign(pads, {
        __opts: {
            maxPolyphony: 16, stealingPolicy: 2,
            globalCoupling: 0.0, snareBuzz: 0.0,
            tomResonance: 0.0, couplingDelayMs: 1.0,
        },
        __crafted: [0, 1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14],
    });
}

// ==============================================================================
// Percussive Kit 1: Hand Drums
//   Congas, bongos, djembe, cajón -- mid-pitched membranes with hand-strike.
// ==============================================================================
function handDrumsKit() {
    const pads = [];
    for (let i = 0; i < kNumPads; i++) pads.push(defaultPad());

    // Conga lo (0)
    pads[0].exciterType = ExciterType.Impulse;
    pads[0].bodyModel = BodyModelType.Membrane;
    pads[0].material = 0.45; pads[0].size = 0.62; pads[0].decay = 0.40;
    pads[0].level = 0.80; pads[0].strikePosition = 0.45;
    pads[0].tsPitchEnvStart = toLogNorm(200);
    pads[0].tsPitchEnvEnd   = toLogNorm(150);
    pads[0].tsPitchEnvTime  = 0.04;
    pads[0].airLoading = 0.55; pads[0].modeScatter = 0.10;
    pads[0].couplingStrength = 0.32; pads[0].secondaryEnabled = 1.0;
    pads[0].secondarySize = 0.48; pads[0].secondaryMaterial = 0.40;
    pads[0].tensionModAmt = 0.20;
    pads[0].clickLayerMix = 0.50; pads[0].clickLayerContactMs = 0.18;
    pads[0].clickLayerBrightness = 0.55;
    pads[0].noiseLayerMix = 0.10; pads[0].noiseLayerColor = 0.40;
    pads[0].bodyDampingB1 = 0.30; pads[0].bodyDampingB3 = 0.10;
    pads[0].macroBodySize = 0.55;

    // Conga hi (2): smaller, brighter, strike near edge.
    pads[2].exciterType = ExciterType.Impulse;
    pads[2].bodyModel = BodyModelType.Membrane;
    pads[2].material = 0.50; pads[2].size = 0.50; pads[2].decay = 0.32;
    pads[2].level = 0.80; pads[2].strikePosition = 0.30;
    pads[2].tsPitchEnvStart = toLogNorm(280);
    pads[2].tsPitchEnvEnd   = toLogNorm(210);
    pads[2].tsPitchEnvTime  = 0.04;
    pads[2].airLoading = 0.50; pads[2].modeScatter = 0.10;
    pads[2].couplingStrength = 0.28; pads[2].secondaryEnabled = 1.0;
    pads[2].secondarySize = 0.40; pads[2].secondaryMaterial = 0.40;
    pads[2].tensionModAmt = 0.18;
    pads[2].clickLayerMix = 0.55; pads[2].clickLayerContactMs = 0.16;
    pads[2].clickLayerBrightness = 0.65;
    pads[2].noiseLayerMix = 0.12; pads[2].noiseLayerColor = 0.45;
    pads[2].bodyDampingB1 = 0.30; pads[2].bodyDampingB3 = 0.10;

    // Conga slap (4): edge strike with bright click, short decay.
    pads[4].exciterType = ExciterType.Impulse;
    pads[4].bodyModel = BodyModelType.Membrane;
    pads[4].material = 0.55; pads[4].size = 0.50; pads[4].decay = 0.18;
    pads[4].level = 0.85; pads[4].strikePosition = 0.10;     // very edge
    pads[4].airLoading = 0.40; pads[4].modeScatter = 0.30;
    pads[4].couplingStrength = 0.20; pads[4].secondaryEnabled = 1.0;
    pads[4].secondarySize = 0.40; pads[4].secondaryMaterial = 0.40;
    pads[4].clickLayerMix = 0.85; pads[4].clickLayerContactMs = 0.10;
    pads[4].clickLayerBrightness = 0.85;
    pads[4].noiseLayerMix = 0.15;
    pads[4].bodyDampingB1 = 0.45; pads[4].bodyDampingB3 = 0.10;
    pads[4].macroPunch = 0.85;

    // Bongo hi (6) / lo (8) -- smaller membranes.
    pads[6].exciterType = ExciterType.Impulse;
    pads[6].bodyModel = BodyModelType.Membrane;
    pads[6].material = 0.55; pads[6].size = 0.32; pads[6].decay = 0.28;
    pads[6].level = 0.80; pads[6].strikePosition = 0.30;
    pads[6].tsPitchEnvStart = toLogNorm(420);
    pads[6].tsPitchEnvEnd   = toLogNorm(350);
    pads[6].tsPitchEnvTime  = 0.04;
    pads[6].airLoading = 0.42; pads[6].modeScatter = 0.10;
    pads[6].couplingStrength = 0.25; pads[6].secondaryEnabled = 1.0;
    pads[6].secondarySize = 0.30; pads[6].secondaryMaterial = 0.40;
    pads[6].tensionModAmt = 0.22;
    pads[6].clickLayerMix = 0.55; pads[6].clickLayerContactMs = 0.15;
    pads[6].clickLayerBrightness = 0.72;
    pads[6].noiseLayerMix = 0.10;
    pads[6].bodyDampingB1 = 0.30; pads[6].bodyDampingB3 = 0.10;

    pads[8] = Object.assign({}, pads[6]);
    pads[8].size = 0.40; pads[8].decay = 0.32;
    pads[8].tsPitchEnvStart = toLogNorm(340);
    pads[8].tsPitchEnvEnd   = toLogNorm(280);

    // Djembe bass (10): large, deep, rattle via snareBuzz global.
    pads[10].exciterType = ExciterType.Impulse;
    pads[10].bodyModel = BodyModelType.Membrane;
    pads[10].material = 0.30; pads[10].size = 0.78; pads[10].decay = 0.45;
    pads[10].level = 0.85; pads[10].strikePosition = 0.50;
    pads[10].tsPitchEnvStart = toLogNorm(150);
    pads[10].tsPitchEnvEnd   = toLogNorm(85);
    pads[10].tsPitchEnvTime  = 0.05;
    pads[10].airLoading = 0.65; pads[10].modeScatter = 0.18;
    pads[10].couplingStrength = 0.40; pads[10].secondaryEnabled = 1.0;
    pads[10].secondarySize = 0.50; pads[10].secondaryMaterial = 0.30;  // wood shell
    pads[10].tensionModAmt = 0.22;
    pads[10].clickLayerMix = 0.40; pads[10].clickLayerContactMs = 0.22;
    pads[10].clickLayerBrightness = 0.40;
    pads[10].noiseLayerMix = 0.18; pads[10].noiseLayerColor = 0.45;
    pads[10].bodyDampingB1 = 0.30; pads[10].bodyDampingB3 = 0.10;
    pads[10].macroBodySize = 0.85;

    // Djembe slap (12)
    pads[12] = Object.assign({}, pads[10]);
    pads[12].decay = 0.20; pads[12].strikePosition = 0.10;
    pads[12].clickLayerMix = 0.85; pads[12].clickLayerBrightness = 0.78;
    pads[12].clickLayerContactMs = 0.12;
    pads[12].tsPitchEnvStart = toLogNorm(280);
    pads[12].tsPitchEnvEnd   = toLogNorm(220);

    // Cajón bass (5): plate front, deep.
    pads[5].exciterType = ExciterType.Impulse;
    pads[5].bodyModel = BodyModelType.Plate;
    pads[5].material = 0.32; pads[5].size = 0.62; pads[5].decay = 0.35;
    pads[5].level = 0.82; pads[5].strikePosition = 0.50;
    pads[5].modeStretch = 0.42;
    pads[5].airLoading = 0.30; pads[5].modeScatter = 0.15;
    pads[5].couplingStrength = 0.30; pads[5].secondaryEnabled = 1.0;
    pads[5].secondarySize = 0.50; pads[5].secondaryMaterial = 0.30;
    pads[5].clickLayerMix = 0.55; pads[5].clickLayerContactMs = 0.20;
    pads[5].clickLayerBrightness = 0.40;
    pads[5].noiseLayerMix = 0.12; pads[5].noiseLayerColor = 0.40;
    pads[5].bodyDampingB1 = 0.30; pads[5].bodyDampingB3 = 0.10;
    pads[5].tsPitchEnvStart = toLogNorm(180);
    pads[5].tsPitchEnvEnd   = toLogNorm(95);
    pads[5].tsPitchEnvTime  = 0.05;

    // Cajón slap (7)
    pads[7] = Object.assign({}, pads[5]);
    pads[7].decay = 0.20; pads[7].strikePosition = 0.15;
    pads[7].clickLayerMix = 0.85; pads[7].clickLayerBrightness = 0.78;
    pads[7].material = 0.50;

    // Frame drum (11): large, hand-tap, soft.
    pads[11].exciterType = ExciterType.Mallet;
    pads[11].bodyModel = BodyModelType.Membrane;
    pads[11].material = 0.40; pads[11].size = 0.85; pads[11].decay = 0.55;
    pads[11].level = 0.78;
    pads[11].airLoading = 0.78; pads[11].modeScatter = 0.20;
    pads[11].couplingStrength = 0.22;
    pads[11].clickLayerMix = 0.32; pads[11].clickLayerContactMs = 0.30;
    pads[11].clickLayerBrightness = 0.30;
    pads[11].noiseLayerMix = 0.20; pads[11].noiseLayerColor = 0.40;
    pads[11].bodyDampingB1 = 0.30; pads[11].bodyDampingB3 = 0.10;
    pads[11].decaySkew = 0.45;

    // Hand shaker (3): NoiseBody short.
    pads[3].exciterType = ExciterType.NoiseBurst;
    pads[3].bodyModel = BodyModelType.NoiseBody;
    pads[3].material = 0.85; pads[3].size = 0.12; pads[3].decay = 0.10;
    pads[3].level = 0.65;
    pads[3].noiseBurstDuration = 0.25;
    pads[3].noiseLayerMix = 0.85; pads[3].noiseLayerCutoff = 0.78;
    pads[3].noiseLayerColor = 0.65; pads[3].noiseLayerDecay = 0.10;
    pads[3].clickLayerMix = 0.0;
    pads[3].airLoading = 0.0; pads[3].modeScatter = 0.20;
    pads[3].bodyDampingB3 = 0.0; pads[3].bodyDampingB1 = 0.55;

    // Wood block (9): pitched plate.
    pads[9].exciterType = ExciterType.Impulse;
    pads[9].bodyModel = BodyModelType.Plate;
    pads[9].material = 0.32; pads[9].size = 0.18; pads[9].decay = 0.18;
    pads[9].level = 0.75;
    pads[9].modeStretch = 0.55;
    pads[9].clickLayerMix = 0.85; pads[9].clickLayerBrightness = 0.78;
    pads[9].clickLayerContactMs = 0.10;
    pads[9].noiseLayerMix = 0.0;
    pads[9].airLoading = 0.0;
    pads[9].bodyDampingB1 = 0.45; pads[9].bodyDampingB3 = 0.10;

    return Object.assign(pads, {
        __opts: {
            maxPolyphony: 12,
            globalCoupling: 0.30,
            snareBuzz: 0.15, tomResonance: 0.20,
            couplingDelayMs: 0.9,
        },
        __crafted: [0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12],
    });
}

// ==============================================================================
// Percussive Kit 2: Latin Perc
//   Claves, cowbell, agogo, timbales, cabasa, maracas, tambourine.
// ==============================================================================
function latinPercKit() {
    const pads = [];
    for (let i = 0; i < kNumPads; i++) pads.push(defaultPad());

    // Clave hi (0)
    pads[0].exciterType = ExciterType.Impulse;
    pads[0].bodyModel = BodyModelType.Bell;
    pads[0].material = 0.85; pads[0].size = 0.10; pads[0].decay = 0.18;
    pads[0].level = 0.80;
    pads[0].fmRatio = 0.62;
    pads[0].modeStretch = 0.50;
    pads[0].clickLayerMix = 0.92; pads[0].clickLayerContactMs = 0.06;
    pads[0].clickLayerBrightness = 0.92;
    pads[0].noiseLayerMix = 0.0;
    pads[0].airLoading = 0.0;
    pads[0].bodyDampingB3 = 0.0; pads[0].bodyDampingB1 = 0.40;
    pads[0].macroBrightness = 0.85;

    // Clave lo (2)
    pads[2] = Object.assign({}, pads[0]);
    pads[2].size = 0.16; pads[2].decay = 0.22;
    pads[2].fmRatio = 0.50;

    // Cowbell hi (4): brassy.
    pads[4].exciterType = ExciterType.FMImpulse;
    pads[4].bodyModel = BodyModelType.Bell;
    pads[4].material = 0.78; pads[4].size = 0.20; pads[4].decay = 0.30;
    pads[4].level = 0.78;
    pads[4].fmRatio = 0.55; pads[4].feedbackAmount = 0.10;
    pads[4].clickLayerMix = 0.55; pads[4].clickLayerContactMs = 0.10;
    pads[4].clickLayerBrightness = 0.78;
    pads[4].noiseLayerMix = 0.10; pads[4].noiseLayerColor = 0.65;
    pads[4].airLoading = 0.0; pads[4].modeScatter = 0.18;
    pads[4].bodyDampingB3 = 0.0; pads[4].bodyDampingB1 = 0.32;
    pads[4].macroBrightness = 0.75;

    // Cowbell lo (6)
    pads[6] = Object.assign({}, pads[4]);
    pads[6].size = 0.28; pads[6].decay = 0.40;
    pads[6].fmRatio = 0.42;
    pads[6].chokeGroup = 0; // override the .chokeGroup set on pad 4? not set, fine.

    // Agogo hi (8): high pitched bell.
    pads[8].exciterType = ExciterType.FMImpulse;
    pads[8].bodyModel = BodyModelType.Bell;
    pads[8].material = 0.85; pads[8].size = 0.14; pads[8].decay = 0.28;
    pads[8].level = 0.75;
    pads[8].fmRatio = 0.72;
    pads[8].clickLayerMix = 0.55; pads[8].clickLayerBrightness = 0.85;
    pads[8].noiseLayerMix = 0.0;
    pads[8].airLoading = 0.0;
    pads[8].bodyDampingB3 = 0.0; pads[8].bodyDampingB1 = 0.30;

    // Agogo lo (10)
    pads[10] = Object.assign({}, pads[8]);
    pads[10].size = 0.22; pads[10].fmRatio = 0.55;
    pads[10].decay = 0.35;

    // Timbale hi (12): membrane shell coupled.
    pads[12].exciterType = ExciterType.NoiseBurst;
    pads[12].bodyModel = BodyModelType.Membrane;
    pads[12].material = 0.55; pads[12].size = 0.40; pads[12].decay = 0.30;
    pads[12].level = 0.82;
    pads[12].noiseBurstDuration = 0.25;
    pads[12].tsPitchEnvStart = toLogNorm(380);
    pads[12].tsPitchEnvEnd   = toLogNorm(280);
    pads[12].tsPitchEnvTime  = 0.04;
    pads[12].airLoading = 0.40; pads[12].modeScatter = 0.18;
    pads[12].couplingStrength = 0.50; pads[12].secondaryEnabled = 1.0;
    pads[12].secondarySize = 0.40; pads[12].secondaryMaterial = 0.65;
    pads[12].tensionModAmt = 0.22;
    pads[12].clickLayerMix = 0.65; pads[12].clickLayerContactMs = 0.12;
    pads[12].clickLayerBrightness = 0.78;
    pads[12].noiseLayerMix = 0.30; pads[12].noiseLayerColor = 0.65;
    pads[12].bodyDampingB1 = 0.32; pads[12].bodyDampingB3 = 0.10;

    // Timbale lo (14)
    pads[14] = Object.assign({}, pads[12]);
    pads[14].size = 0.55; pads[14].decay = 0.40;
    pads[14].material = 0.50;
    pads[14].tsPitchEnvStart = toLogNorm(280);
    pads[14].tsPitchEnvEnd   = toLogNorm(200);
    pads[14].secondarySize = 0.50;

    // Cabasa (1): NoiseBody fast shake.
    pads[1].exciterType = ExciterType.NoiseBurst;
    pads[1].bodyModel = BodyModelType.NoiseBody;
    pads[1].material = 0.85; pads[1].size = 0.10; pads[1].decay = 0.10;
    pads[1].level = 0.65;
    pads[1].noiseBurstDuration = 0.25;
    pads[1].noiseLayerMix = 0.85; pads[1].noiseLayerCutoff = 0.85;
    pads[1].noiseLayerColor = 0.78; pads[1].noiseLayerDecay = 0.10;
    pads[1].clickLayerMix = 0.0;
    pads[1].airLoading = 0.0; pads[1].modeScatter = 0.30;
    pads[1].bodyDampingB3 = 0.0; pads[1].bodyDampingB1 = 0.55;

    // Maracas (3): noise burst longer.
    pads[3].exciterType = ExciterType.NoiseBurst;
    pads[3].bodyModel = BodyModelType.NoiseBody;
    pads[3].material = 0.78; pads[3].size = 0.14; pads[3].decay = 0.18;
    pads[3].level = 0.65;
    pads[3].noiseBurstDuration = 0.45;
    pads[3].noiseLayerMix = 0.85; pads[3].noiseLayerCutoff = 0.55;
    pads[3].noiseLayerColor = 0.50; pads[3].noiseLayerDecay = 0.18;
    pads[3].clickLayerMix = 0.0;
    pads[3].airLoading = 0.0; pads[3].modeScatter = 0.42;
    pads[3].bodyDampingB3 = 0.0; pads[3].bodyDampingB1 = 0.45;

    // Tambourine (5): NoiseBody + Bell secondary.
    pads[5].exciterType = ExciterType.NoiseBurst;
    pads[5].bodyModel = BodyModelType.NoiseBody;
    pads[5].material = 0.85; pads[5].size = 0.20; pads[5].decay = 0.22;
    pads[5].level = 0.72;
    pads[5].noiseBurstDuration = 0.30;
    pads[5].noiseLayerMix = 0.78; pads[5].noiseLayerCutoff = 0.92;
    pads[5].noiseLayerColor = 0.92; pads[5].noiseLayerDecay = 0.22;
    pads[5].clickLayerMix = 0.30; pads[5].clickLayerBrightness = 0.85;
    pads[5].airLoading = 0.0; pads[5].modeScatter = 0.55;
    pads[5].couplingStrength = 0.40; pads[5].secondaryEnabled = 1.0;
    pads[5].secondarySize = 0.20; pads[5].secondaryMaterial = 0.85;
    pads[5].bodyDampingB3 = 0.0; pads[5].bodyDampingB1 = 0.32;
    pads[5].macroComplexity = 0.65;

    // Triangle (7): tiny bell.
    pads[7].exciterType = ExciterType.Impulse;
    pads[7].bodyModel = BodyModelType.Bell;
    pads[7].material = 0.95; pads[7].size = 0.08; pads[7].decay = 0.85;
    pads[7].level = 0.65;
    pads[7].fmRatio = 0.55; pads[7].modeStretch = 0.55;
    pads[7].clickLayerMix = 0.55; pads[7].clickLayerBrightness = 0.95;
    pads[7].clickLayerContactMs = 0.06;
    pads[7].noiseLayerMix = 0.0;
    pads[7].airLoading = 0.0;
    pads[7].bodyDampingB3 = 0.0; pads[7].bodyDampingB1 = 0.30;

    // Guiro (9): Friction NoiseBody.
    pads[9].exciterType = ExciterType.Friction;
    pads[9].bodyModel = BodyModelType.NoiseBody;
    pads[9].material = 0.65; pads[9].size = 0.18; pads[9].decay = 0.30;
    pads[9].level = 0.70;
    pads[9].frictionPressure = 0.55;
    pads[9].noiseLayerMix = 0.65; pads[9].noiseLayerCutoff = 0.62;
    pads[9].noiseLayerColor = 0.55; pads[9].noiseLayerDecay = 0.30;
    pads[9].clickLayerMix = 0.0;
    pads[9].airLoading = 0.0; pads[9].modeScatter = 0.30;
    pads[9].bodyDampingB3 = 0.0; pads[9].bodyDampingB1 = 0.42;
    pads[9].decaySkew = 0.55;

    // Vibraslap (11): NoiseBody + scatter
    pads[11].exciterType = ExciterType.NoiseBurst;
    pads[11].bodyModel = BodyModelType.NoiseBody;
    pads[11].material = 0.85; pads[11].size = 0.22; pads[11].decay = 0.32;
    pads[11].level = 0.70;
    pads[11].noiseBurstDuration = 0.55;
    pads[11].noiseLayerMix = 0.85; pads[11].noiseLayerCutoff = 0.78;
    pads[11].noiseLayerResonance = 0.30;
    pads[11].noiseLayerColor = 0.62; pads[11].noiseLayerDecay = 0.32;
    pads[11].clickLayerMix = 0.30; pads[11].clickLayerContactMs = 0.30;
    pads[11].airLoading = 0.0; pads[11].modeScatter = 0.62;
    pads[11].bodyDampingB3 = 0.0; pads[11].bodyDampingB1 = 0.32;
    pads[11].macroComplexity = 0.78;

    // Bongo perc fillers at 13, 15
    pads[13].exciterType = ExciterType.Impulse;
    pads[13].bodyModel = BodyModelType.Membrane;
    pads[13].material = 0.55; pads[13].size = 0.25; pads[13].decay = 0.18;
    pads[13].level = 0.75; pads[13].strikePosition = 0.20;
    pads[13].airLoading = 0.30;
    pads[13].clickLayerMix = 0.65; pads[13].clickLayerBrightness = 0.65;
    pads[13].noiseLayerMix = 0.10;
    pads[13].bodyDampingB1 = 0.32; pads[13].bodyDampingB3 = 0.10;

    return Object.assign(pads, {
        __opts: {
            maxPolyphony: 12,
            globalCoupling: 0.10,
            snareBuzz: 0.0, tomResonance: 0.0,
            couplingDelayMs: 0.8,
        },
        __crafted: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14],
    });
}

// ==============================================================================
// Percussive Kit 3: Tabla
//   Pitched hand drums with strong tension-mod pitch bends. Bayan + dayan
//   bols.
// ==============================================================================
function tablaKit() {
    const pads = [];
    for (let i = 0; i < kNumPads; i++) pads.push(defaultPad());

    // Bayan (left, bass) -- pad 0
    pads[0].exciterType = ExciterType.Impulse;
    pads[0].bodyModel = BodyModelType.Membrane;
    pads[0].material = 0.30; pads[0].size = 0.72; pads[0].decay = 0.55;
    pads[0].level = 0.82; pads[0].strikePosition = 0.50;
    pads[0].tsPitchEnvStart = toLogNorm(180);
    pads[0].tsPitchEnvEnd   = toLogNorm(70);
    pads[0].tsPitchEnvTime  = 0.20;
    pads[0].tsPitchEnvCurve = 1.0;
    pads[0].airLoading = 0.62; pads[0].modeScatter = 0.10;
    pads[0].couplingStrength = 0.32; pads[0].secondaryEnabled = 1.0;
    pads[0].secondarySize = 0.45; pads[0].secondaryMaterial = 0.40;
    pads[0].tensionModAmt = 0.65;     // bayan pitch bend!
    pads[0].clickLayerMix = 0.45; pads[0].clickLayerContactMs = 0.18;
    pads[0].clickLayerBrightness = 0.45;
    pads[0].noiseLayerMix = 0.10; pads[0].noiseLayerColor = 0.42;
    pads[0].bodyDampingB1 = 0.30; pads[0].bodyDampingB3 = 0.05;
    pads[0].decaySkew = 0.55;
    pads[0].macroComplexity = 0.70;

    // Bayan slap (1) -- closed strike, less ring.
    pads[1] = Object.assign({}, pads[0]);
    pads[1].decay = 0.20; pads[1].strikePosition = 0.20;
    pads[1].clickLayerMix = 0.85; pads[1].clickLayerBrightness = 0.65;
    pads[1].tensionModAmt = 0.20;

    // Dayan (right, treble) -- pitched, ringing -- pad 2
    pads[2].exciterType = ExciterType.Impulse;
    pads[2].bodyModel = BodyModelType.Membrane;
    pads[2].material = 0.50; pads[2].size = 0.42; pads[2].decay = 0.58;
    pads[2].level = 0.80; pads[2].strikePosition = 0.40;
    pads[2].tsPitchEnvStart = toLogNorm(420);
    pads[2].tsPitchEnvEnd   = toLogNorm(380);
    pads[2].tsPitchEnvTime  = 0.04;
    pads[2].airLoading = 0.45; pads[2].modeScatter = 0.05;
    pads[2].couplingStrength = 0.35; pads[2].secondaryEnabled = 1.0;
    pads[2].secondarySize = 0.35; pads[2].secondaryMaterial = 0.45;
    pads[2].tensionModAmt = 0.25;
    pads[2].clickLayerMix = 0.55; pads[2].clickLayerContactMs = 0.12;
    pads[2].clickLayerBrightness = 0.75;
    pads[2].noiseLayerMix = 0.08;
    pads[2].decaySkew = 0.60;
    pads[2].bodyDampingB1 = 0.30; pads[2].bodyDampingB3 = 0.05;
    pads[2].nonlinearCoupling = 0.20;

    // Tin (right edge tap) -- pad 3
    pads[3] = Object.assign({}, pads[2]);
    pads[3].decay = 0.22; pads[3].strikePosition = 0.10;
    pads[3].clickLayerMix = 0.85; pads[3].clickLayerBrightness = 0.85;
    pads[3].tensionModAmt = 0.15; pads[3].decaySkew = 0.40;

    // Tha (open palm) -- pad 4
    pads[4].exciterType = ExciterType.Mallet;
    pads[4].bodyModel = BodyModelType.Membrane;
    pads[4].material = 0.50; pads[4].size = 0.42; pads[4].decay = 0.45;
    pads[4].level = 0.80; pads[4].strikePosition = 0.50;
    pads[4].airLoading = 0.42; pads[4].modeScatter = 0.10;
    pads[4].couplingStrength = 0.30; pads[4].secondaryEnabled = 1.0;
    pads[4].secondarySize = 0.35; pads[4].secondaryMaterial = 0.45;
    pads[4].tensionModAmt = 0.30;
    pads[4].clickLayerMix = 0.40; pads[4].clickLayerContactMs = 0.30;
    pads[4].clickLayerBrightness = 0.45;
    pads[4].noiseLayerMix = 0.15;
    pads[4].bodyDampingB1 = 0.30; pads[4].bodyDampingB3 = 0.05;
    pads[4].decaySkew = 0.50;

    // Na (rim ring) -- pad 5: morph layer for damped→ringing transition.
    pads[5].exciterType = ExciterType.Impulse;
    pads[5].bodyModel = BodyModelType.Membrane;
    pads[5].material = 0.55; pads[5].size = 0.42; pads[5].decay = 0.45;
    pads[5].level = 0.78; pads[5].strikePosition = 0.18;
    pads[5].morphEnabled = 1.0;
    pads[5].morphStart = 0.45; pads[5].morphEnd = 0.65;
    pads[5].morphDuration = 0.40; pads[5].morphCurve = 0.4;
    pads[5].airLoading = 0.38; pads[5].modeScatter = 0.45;
    pads[5].decaySkew = 0.50;  // override the 0.65 below — high decaySkew + low b3 produces pitched survival
    pads[5].couplingStrength = 0.38; pads[5].secondaryEnabled = 1.0;
    pads[5].secondarySize = 0.32; pads[5].secondaryMaterial = 0.50;
    pads[5].tensionModAmt = 0.18;
    pads[5].clickLayerMix = 0.55; pads[5].clickLayerBrightness = 0.78;
    pads[5].clickLayerContactMs = 0.14;
    pads[5].noiseLayerMix = 0.05;
    pads[5].decaySkew = 0.65;
    pads[5].nonlinearCoupling = 0.32;
    pads[5].bodyDampingB1 = 0.30; pads[5].bodyDampingB3 = 0.05;

    // Ge (bayan ring with high tension) -- pad 6
    pads[6] = Object.assign({}, pads[0]);
    pads[6].decay = 0.78; pads[6].tensionModAmt = 0.85;
    pads[6].decaySkew = 0.65;

    // Ka (closed bayan) -- pad 7
    pads[7] = Object.assign({}, pads[1]);
    pads[7].material = 0.30; pads[7].size = 0.65;

    // Tete (hand-damped tin) -- pad 8
    pads[8] = Object.assign({}, pads[3]);
    pads[8].decay = 0.10; pads[8].decaySkew = 0.30;

    // Drone tanpura swell on outputBus 1 -- pad 9: long sustained Friction
    // String drone for the tabla ensemble's sympathetic backdrop.
    pads[9].exciterType = ExciterType.Friction;
    pads[9].bodyModel = BodyModelType.String;
    pads[9].material = 0.55; pads[9].size = 0.65; pads[9].decay = 0.95;
    pads[9].level = 0.55;
    pads[9].frictionPressure = 0.32;
    pads[9].modeStretch = 0.40;
    pads[9].nonlinearCoupling = 0.45;
    pads[9].morphEnabled = 1.0;
    pads[9].morphStart = 0.55; pads[9].morphEnd = 0.65;
    pads[9].morphDuration = 0.85; pads[9].morphCurve = 0.5;
    pads[9].decaySkew = 0.85;
    pads[9].noiseLayerMix = 0.18; pads[9].noiseLayerCutoff = 0.45;
    pads[9].clickLayerMix = 0.0;
    pads[9].bodyDampingB1 = 0.30; pads[9].bodyDampingB3 = 0.18;
    pads[9].outputBus = 1;
    pads[9].macroComplexity = 0.85;

    return Object.assign(pads, {
        __opts: {
            maxPolyphony: 12,
            globalCoupling: 0.30,        // sympathetic resonance between bayan/dayan
            snareBuzz: 0.0,
            tomResonance: 0.45,
            couplingDelayMs: 1.1,
            overrides: [
                { src: 0, dst: 2, coeff: 0.038 },
                { src: 2, dst: 0, coeff: 0.025 },
                { src: 6, dst: 2, coeff: 0.030 },
            ],
        },
        __crafted: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9],
    });
}

// ==============================================================================
// Percussive Kit 4: World Metal
//   Kalimba, mbira, bell tree, crotales, singing bowl, wood blocks.
// ==============================================================================
function worldMetalKit() {
    const pads = [];
    for (let i = 0; i < kNumPads; i++) pads.push(defaultPad());

    // Kalimba pads (0-7): different materials = different tunings.
    const kalimbaPads = [0, 1, 2, 3, 4, 5, 6, 7];
    const kalimbaMat  = [0.42, 0.48, 0.55, 0.62, 0.68, 0.75, 0.82, 0.88];
    const kalimbaSize = [0.40, 0.36, 0.32, 0.28, 0.25, 0.22, 0.20, 0.18];
    for (let i = 0; i < kalimbaPads.length; i++) {
        const p = kalimbaPads[i];
        pads[p].exciterType = ExciterType.Mallet;
        pads[p].bodyModel = BodyModelType.Bell;
        pads[p].material = kalimbaMat[i]; pads[p].size = kalimbaSize[i];
        pads[p].decay = 0.65; pads[p].level = 0.72;
        pads[p].fmRatio = 0.42 + i * 0.02;
        pads[p].modeStretch = 0.40;
        pads[p].clickLayerMix = 0.45; pads[p].clickLayerContactMs = 0.18;
        pads[p].clickLayerBrightness = 0.65;
        pads[p].noiseLayerMix = 0.08;
        pads[p].airLoading = 0.0;
        pads[p].bodyDampingB3 = 0.0; pads[p].bodyDampingB1 = 0.30;
        pads[p].decaySkew = 0.55;
        pads[p].macroBrightness = 0.60 + i * 0.03;
    }

    // Mbira (8-11): String body, similar tuning but different timbre.
    const mbiraPads = [8, 9, 10, 11];
    const mbiraMat  = [0.50, 0.62, 0.72, 0.82];
    const mbiraSize = [0.32, 0.28, 0.24, 0.20];
    for (let i = 0; i < mbiraPads.length; i++) {
        const p = mbiraPads[i];
        pads[p].exciterType = ExciterType.Mallet;
        pads[p].bodyModel = BodyModelType.String;
        pads[p].material = mbiraMat[i]; pads[p].size = mbiraSize[i];
        pads[p].decay = 0.78; pads[p].level = 0.70;
        pads[p].modeStretch = 0.42;
        pads[p].nonlinearCoupling = 0.18;
        pads[p].clickLayerMix = 0.32; pads[p].clickLayerContactMs = 0.14;
        pads[p].clickLayerBrightness = 0.70;
        pads[p].noiseLayerMix = 0.12;
        pads[p].airLoading = 0.0;
        pads[p].bodyDampingB1 = 0.30; pads[p].bodyDampingB3 = 0.22;
        pads[p].decaySkew = 0.60;
    }

    // Bell tree (12): cascading bells.
    pads[12].exciterType = ExciterType.NoiseBurst;
    pads[12].bodyModel = BodyModelType.Bell;
    pads[12].material = 0.95; pads[12].size = 0.30; pads[12].decay = 0.85;
    pads[12].level = 0.70;
    pads[12].fmRatio = 0.55;
    pads[12].modeStretch = 0.55;
    pads[12].modeScatter = 0.55;
    pads[12].morphEnabled = 1.0;
    pads[12].morphStart = 0.85; pads[12].morphEnd = 0.55;
    pads[12].morphDuration = 0.55; pads[12].morphCurve = 0.3;
    pads[12].clickLayerMix = 0.55; pads[12].clickLayerBrightness = 0.92;
    pads[12].noiseLayerMix = 0.42; pads[12].noiseLayerCutoff = 0.92;
    pads[12].noiseLayerColor = 0.85; pads[12].noiseLayerDecay = 0.85;
    pads[12].airLoading = 0.0;
    pads[12].bodyDampingB3 = 0.0; pads[12].bodyDampingB1 = 0.30;
    pads[12].decaySkew = 0.78;

    // Crotales hi (13) / lo (14): pitched bells.
    pads[13].exciterType = ExciterType.Mallet;
    pads[13].bodyModel = BodyModelType.Bell;
    pads[13].material = 0.92; pads[13].size = 0.16; pads[13].decay = 0.85;
    pads[13].level = 0.72;
    pads[13].fmRatio = 0.62;
    pads[13].clickLayerMix = 0.45; pads[13].clickLayerBrightness = 0.92;
    pads[13].noiseLayerMix = 0.0;
    pads[13].airLoading = 0.0;
    pads[13].bodyDampingB3 = 0.0; pads[13].bodyDampingB1 = 0.30;
    pads[13].decaySkew = 0.55;

    pads[14] = Object.assign({}, pads[13]);
    pads[14].size = 0.22; pads[14].fmRatio = 0.45;

    // Singing bowl (15): Bell + Friction for bowed singing-bowl sustain.
    pads[15].exciterType = ExciterType.Friction;
    pads[15].bodyModel = BodyModelType.Bell;
    pads[15].material = 0.78; pads[15].size = 0.45; pads[15].decay = 0.95;
    pads[15].level = 0.65;
    pads[15].frictionPressure = 0.28;
    pads[15].modeStretch = 0.45;
    pads[15].nonlinearCoupling = 0.22;
    pads[15].morphEnabled = 1.0;
    pads[15].morphStart = 0.78; pads[15].morphEnd = 0.55;
    pads[15].morphDuration = 0.85; pads[15].morphCurve = 0.5;
    pads[15].tensionModAmt = 0.40;     // wobble character
    pads[15].decaySkew = 0.85;
    pads[15].noiseLayerMix = 0.10; pads[15].noiseLayerColor = 0.45;
    pads[15].clickLayerMix = 0.0;
    pads[15].airLoading = 0.0;
    pads[15].bodyDampingB3 = 0.0; pads[15].bodyDampingB1 = 0.30;
    pads[15].outputBus = 1;
    pads[15].macroComplexity = 0.85;

    // Wood blocks (16, 17)
    pads[16].exciterType = ExciterType.Impulse;
    pads[16].bodyModel = BodyModelType.Plate;
    pads[16].material = 0.30; pads[16].size = 0.18; pads[16].decay = 0.18;
    pads[16].level = 0.72;
    pads[16].modeStretch = 0.55;
    pads[16].clickLayerMix = 0.85; pads[16].clickLayerBrightness = 0.78;
    pads[16].clickLayerContactMs = 0.10;
    pads[16].noiseLayerMix = 0.0;
    pads[16].airLoading = 0.0;
    pads[16].bodyDampingB1 = 0.42; pads[16].bodyDampingB3 = 0.10;

    pads[17] = Object.assign({}, pads[16]);
    pads[17].size = 0.28; pads[17].material = 0.28; pads[17].decay = 0.22;

    // Tibetan tingsha (18) and Indian temple bell (19)
    pads[18].exciterType = ExciterType.Impulse;
    pads[18].bodyModel = BodyModelType.Bell;
    pads[18].material = 0.95; pads[18].size = 0.12; pads[18].decay = 0.92;
    pads[18].level = 0.65;
    pads[18].fmRatio = 0.78; pads[18].modeStretch = 0.55;
    pads[18].clickLayerMix = 0.50; pads[18].clickLayerBrightness = 0.95;
    pads[18].noiseLayerMix = 0.0;
    pads[18].airLoading = 0.0;
    pads[18].bodyDampingB3 = 0.0; pads[18].bodyDampingB1 = 0.30;
    pads[18].decaySkew = 0.65;

    pads[19].exciterType = ExciterType.Mallet;
    pads[19].bodyModel = BodyModelType.Bell;
    pads[19].material = 0.85; pads[19].size = 0.50; pads[19].decay = 0.92;
    pads[19].level = 0.70;
    pads[19].fmRatio = 0.30;
    pads[19].modeStretch = 0.50;
    pads[19].clickLayerMix = 0.30; pads[19].clickLayerBrightness = 0.65;
    pads[19].noiseLayerMix = 0.05;
    pads[19].airLoading = 0.0;
    pads[19].bodyDampingB3 = 0.0; pads[19].bodyDampingB1 = 0.30;
    pads[19].decaySkew = 0.78;
    pads[19].tensionModAmt = 0.18;

    return Object.assign(pads, {
        __opts: {
            maxPolyphony: 16,
            globalCoupling: 0.40,        // sympathetic resonance everywhere
            snareBuzz: 0.0, tomResonance: 0.0,
            couplingDelayMs: 1.4,
            overrides: [
                { src: 18, dst: 19, coeff: 0.045 },
                { src: 12, dst: 13, coeff: 0.030 },
                { src: 0, dst: 4, coeff: 0.022 },
                { src: 4, dst: 0, coeff: 0.022 },
            ],
        },
        __crafted: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19],
    });
}

// ==============================================================================
// Percussive Kit 5: Cajon & Frames
//   Cajón, frame drum, bodhran, dholak, riq.
// ==============================================================================
function cajonFramesKit() {
    const pads = [];
    for (let i = 0; i < kNumPads; i++) pads.push(defaultPad());

    // Cajón bass (0): plate front, deep, hand-strike.
    pads[0].exciterType = ExciterType.Impulse;
    pads[0].bodyModel = BodyModelType.Plate;
    pads[0].material = 0.30; pads[0].size = 0.65; pads[0].decay = 0.40;
    pads[0].level = 0.85; pads[0].strikePosition = 0.50;
    pads[0].modeStretch = 0.42;
    pads[0].tsPitchEnvStart = toLogNorm(150);
    pads[0].tsPitchEnvEnd   = toLogNorm(78);
    pads[0].tsPitchEnvTime  = 0.04;
    pads[0].airLoading = 0.45; pads[0].modeScatter = 0.18;
    pads[0].couplingStrength = 0.45; pads[0].secondaryEnabled = 1.0;
    pads[0].secondarySize = 0.55; pads[0].secondaryMaterial = 0.30;  // wood
    pads[0].tensionModAmt = 0.18;
    pads[0].clickLayerMix = 0.55; pads[0].clickLayerContactMs = 0.20;
    pads[0].clickLayerBrightness = 0.45;
    pads[0].noiseLayerMix = 0.12; pads[0].noiseLayerColor = 0.45;
    pads[0].bodyDampingB1 = 0.30; pads[0].bodyDampingB3 = 0.10;
    pads[0].macroBodySize = 0.78;

    // Cajón slap (2): edge, brighter, snappier.
    pads[2] = Object.assign({}, pads[0]);
    pads[2].decay = 0.22; pads[2].strikePosition = 0.10;
    pads[2].material = 0.42;
    pads[2].clickLayerMix = 0.85; pads[2].clickLayerBrightness = 0.78;
    pads[2].clickLayerContactMs = 0.10;
    pads[2].tsPitchEnvStart = toLogNorm(280);
    pads[2].tsPitchEnvEnd   = toLogNorm(220);
    pads[2].macroPunch = 0.85;

    // Cajón snare side (4): Wires + plate.
    pads[4].exciterType = ExciterType.NoiseBurst;
    pads[4].bodyModel = BodyModelType.Plate;
    pads[4].material = 0.40; pads[4].size = 0.45; pads[4].decay = 0.32;
    pads[4].level = 0.78;
    pads[4].noiseBurstDuration = 0.30;
    pads[4].modeStretch = 0.42; pads[4].decaySkew = 0.40;
    pads[4].noiseLayerMix = 0.65; pads[4].noiseLayerCutoff = 0.62;
    pads[4].noiseLayerColor = 0.55; pads[4].noiseLayerDecay = 0.30;
    pads[4].clickLayerMix = 0.45; pads[4].clickLayerBrightness = 0.62;
    pads[4].airLoading = 0.30; pads[4].modeScatter = 0.30;
    pads[4].couplingStrength = 0.40; pads[4].secondaryEnabled = 1.0;
    pads[4].secondarySize = 0.40; pads[4].secondaryMaterial = 0.30;
    pads[4].bodyDampingB1 = 0.30; pads[4].bodyDampingB3 = 0.10;

    // Frame drum tap (6): large, hand-soft.
    pads[6].exciterType = ExciterType.Mallet;
    pads[6].bodyModel = BodyModelType.Membrane;
    pads[6].material = 0.40; pads[6].size = 0.78; pads[6].decay = 0.50;
    pads[6].level = 0.78; pads[6].strikePosition = 0.35;
    pads[6].airLoading = 0.85; pads[6].modeScatter = 0.18;
    pads[6].couplingStrength = 0.30; pads[6].secondaryEnabled = 1.0;
    pads[6].secondarySize = 0.40; pads[6].secondaryMaterial = 0.32;
    pads[6].tensionModAmt = 0.20;
    pads[6].clickLayerMix = 0.40; pads[6].clickLayerContactMs = 0.30;
    pads[6].clickLayerBrightness = 0.40;
    pads[6].noiseLayerMix = 0.18; pads[6].noiseLayerColor = 0.40;
    pads[6].bodyDampingB1 = 0.30; pads[6].bodyDampingB3 = 0.10;

    // Frame drum slap (8) -- edge.
    pads[8] = Object.assign({}, pads[6]);
    pads[8].decay = 0.22; pads[8].strikePosition = 0.08;
    pads[8].clickLayerMix = 0.85; pads[8].clickLayerBrightness = 0.65;

    // Bodhran (10): frame drum with stick, bright tone.
    pads[10].exciterType = ExciterType.Impulse;
    pads[10].bodyModel = BodyModelType.Membrane;
    pads[10].material = 0.45; pads[10].size = 0.72; pads[10].decay = 0.40;
    pads[10].level = 0.80; pads[10].strikePosition = 0.40;
    pads[10].airLoading = 0.78; pads[10].modeScatter = 0.20;
    pads[10].couplingStrength = 0.32; pads[10].secondaryEnabled = 1.0;
    pads[10].secondarySize = 0.42; pads[10].secondaryMaterial = 0.30;
    pads[10].tensionModAmt = 0.30;
    pads[10].clickLayerMix = 0.65; pads[10].clickLayerContactMs = 0.18;
    pads[10].clickLayerBrightness = 0.62;
    pads[10].noiseLayerMix = 0.18;
    pads[10].bodyDampingB1 = 0.30; pads[10].bodyDampingB3 = 0.10;

    // Dholak hi (12) / lo (14)
    pads[12].exciterType = ExciterType.Impulse;
    pads[12].bodyModel = BodyModelType.Membrane;
    pads[12].material = 0.48; pads[12].size = 0.55; pads[12].decay = 0.45;
    pads[12].level = 0.80; pads[12].strikePosition = 0.32;
    pads[12].tsPitchEnvStart = toLogNorm(320);
    pads[12].tsPitchEnvEnd   = toLogNorm(220);
    pads[12].tsPitchEnvTime  = 0.08;
    pads[12].airLoading = 0.55; pads[12].modeScatter = 0.10;
    pads[12].couplingStrength = 0.50; pads[12].secondaryEnabled = 1.0;
    pads[12].secondarySize = 0.45; pads[12].secondaryMaterial = 0.32;
    pads[12].tensionModAmt = 0.30;
    pads[12].clickLayerMix = 0.55; pads[12].clickLayerBrightness = 0.65;
    pads[12].noiseLayerMix = 0.15; pads[12].noiseLayerColor = 0.50;
    pads[12].bodyDampingB1 = 0.30; pads[12].bodyDampingB3 = 0.10;
    pads[12].decaySkew = 0.50;

    pads[14] = Object.assign({}, pads[12]);
    pads[14].size = 0.65; pads[14].material = 0.40;
    pads[14].tsPitchEnvStart = toLogNorm(220);
    pads[14].tsPitchEnvEnd   = toLogNorm(140);
    pads[14].decay = 0.55;

    // Riq (frame drum + jingles) (5): NoiseBody secondary for jingles.
    pads[5].exciterType = ExciterType.Impulse;
    pads[5].bodyModel = BodyModelType.Membrane;
    pads[5].material = 0.50; pads[5].size = 0.42; pads[5].decay = 0.32;
    pads[5].level = 0.78; pads[5].strikePosition = 0.30;
    pads[5].airLoading = 0.45; pads[5].modeScatter = 0.42;
    pads[5].couplingStrength = 0.45; pads[5].secondaryEnabled = 1.0;
    pads[5].secondarySize = 0.20; pads[5].secondaryMaterial = 0.85;  // metal jingles
    pads[5].clickLayerMix = 0.65; pads[5].clickLayerBrightness = 0.78;
    pads[5].clickLayerContactMs = 0.12;
    pads[5].noiseLayerMix = 0.45; pads[5].noiseLayerCutoff = 0.85;
    pads[5].noiseLayerColor = 0.85; pads[5].noiseLayerDecay = 0.20;
    pads[5].bodyDampingB1 = 0.32; pads[5].bodyDampingB3 = 0.10;
    pads[5].macroComplexity = 0.78;

    // Pandeiro shake (7) -- jingle-led.
    pads[7].exciterType = ExciterType.NoiseBurst;
    pads[7].bodyModel = BodyModelType.NoiseBody;
    pads[7].material = 0.85; pads[7].size = 0.18; pads[7].decay = 0.20;
    pads[7].level = 0.72;
    pads[7].noiseBurstDuration = 0.40;
    pads[7].noiseLayerMix = 0.85; pads[7].noiseLayerCutoff = 0.92;
    pads[7].noiseLayerColor = 0.88; pads[7].noiseLayerDecay = 0.20;
    pads[7].clickLayerMix = 0.20; pads[7].clickLayerBrightness = 0.85;
    pads[7].airLoading = 0.0; pads[7].modeScatter = 0.55;
    pads[7].bodyDampingB3 = 0.0; pads[7].bodyDampingB1 = 0.32;

    // Tabla-style perc fillers (9, 11, 13)
    pads[9].exciterType = ExciterType.Impulse;
    pads[9].bodyModel = BodyModelType.Membrane;
    pads[9].material = 0.50; pads[9].size = 0.32; pads[9].decay = 0.30;
    pads[9].level = 0.75; pads[9].strikePosition = 0.30;
    pads[9].airLoading = 0.40;
    pads[9].clickLayerMix = 0.55; pads[9].clickLayerBrightness = 0.62;
    pads[9].noiseLayerMix = 0.10;
    pads[9].bodyDampingB1 = 0.32; pads[9].bodyDampingB3 = 0.10;

    pads[11] = Object.assign({}, pads[9]);
    pads[11].size = 0.42; pads[11].decay = 0.38; pads[11].material = 0.45;

    pads[13] = Object.assign({}, pads[9]);
    pads[13].size = 0.22; pads[13].decay = 0.20; pads[13].material = 0.55;
    pads[13].clickLayerBrightness = 0.78;

    return Object.assign(pads, {
        __opts: {
            maxPolyphony: 12,
            globalCoupling: 0.18,
            snareBuzz: 0.20, tomResonance: 0.30,
            couplingDelayMs: 1.0,
        },
        __crafted: [0, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14],
    });
}

// ==============================================================================
// Unnatural Kit 2: Glass Bell Garden
//   Bell-bodied tones with metallic damping, wide range of fmRatio + size.
//   Some pads use Friction for "bowed bell" sustain.
// ==============================================================================
function glassBellGardenKit() {
    const pads = [];
    for (let i = 0; i < kNumPads; i++) pads.push(defaultPad());

    // Pads 0-15: bells with graded material/size/fmRatio.
    for (let i = 0; i <= 15; i++) {
        const p = i;
        const useFriction = (i % 4 === 3);
        pads[p].exciterType = useFriction ? ExciterType.Friction
            : ((i % 3 === 0) ? ExciterType.Mallet : ExciterType.FMImpulse);
        pads[p].bodyModel = BodyModelType.Bell;
        pads[p].material = 0.40 + (i / 15) * 0.55;        // 0.40..0.95
        pads[p].size     = 0.10 + ((15 - i) / 15) * 0.60;  // small high pitches first
        pads[p].decay    = 0.55 + (i % 6) * 0.07;
        pads[p].level    = 0.68;
        pads[p].fmRatio  = 0.30 + (i * 0.04) % 0.65;
        pads[p].feedbackAmount = useFriction ? 0.0 : (i % 5) * 0.08;
        pads[p].frictionPressure = useFriction ? 0.30 + (i % 4) * 0.10 : 0.0;
        pads[p].modeStretch = 0.30 + (i % 7) * 0.06;
        pads[p].modeInjectAmount = 0.0;     // undamped, no note-off → infinite ring
        pads[p].nonlinearCoupling = (i % 4) * 0.10;
        pads[p].decaySkew = 0.55 + (i % 4) * 0.08;
        pads[p].morphEnabled = (i % 5 === 0) ? 1.0 : 0.0;
        if (pads[p].morphEnabled) {
            pads[p].morphStart = 0.40 + (i % 3) * 0.15;
            pads[p].morphEnd   = 0.85;
            pads[p].morphDuration = 0.45;
            pads[p].morphCurve = 0.5;
        }
        pads[p].modeScatter = 0.30 + (i % 5) * 0.08;
        pads[p].airLoading  = 0.0;
        pads[p].couplingStrength = (i % 3 === 0) ? 0.25 : 0.0;
        pads[p].secondaryEnabled = (i % 3 === 0) ? 1.0 : 0.0;
        pads[p].secondarySize    = 0.30 + (i % 4) * 0.08;
        pads[p].secondaryMaterial = 0.85;       // metallic shell
        pads[p].tensionModAmt = (i % 4) * 0.08;
        pads[p].noiseLayerMix = 0.05 + (i % 5) * 0.08;
        pads[p].noiseLayerCutoff = 0.85;
        pads[p].noiseLayerColor  = 0.78 - (i % 4) * 0.08;
        pads[p].noiseLayerDecay  = 0.65 + (i % 4) * 0.08;
        pads[p].clickLayerMix    = 0.32 + (i % 3) * 0.12;
        pads[p].clickLayerContactMs = 0.10 + (i % 3) * 0.06;
        pads[p].clickLayerBrightness = 0.65 + (i % 4) * 0.08;
        pads[p].bodyDampingB3 = 0.0;
        pads[p].bodyDampingB1 = 0.30 + (i % 7) * 0.04;
        pads[p].outputBus = (i % 8 === 7) ? 1 : 0;
        pads[p].macroBrightness = 0.55 + (i / 15) * 0.40;
        pads[p].macroComplexity = 0.55 + (i % 5) * 0.06;
        pads[p].couplingAmount = 0.55 + (i % 4) * 0.10;
    }

    return Object.assign(pads, {
        __opts: {
            maxPolyphony: 16,
            globalCoupling: 0.65,        // bells ring into each other
            snareBuzz: 0.0, tomResonance: 0.0,
            couplingDelayMs: 1.8,
            overrides: [
                { src: 0, dst: 4, coeff: 0.040 },
                { src: 2, dst: 6, coeff: 0.035 },
                { src: 4, dst: 8, coeff: 0.040 },
                { src: 6, dst: 10, coeff: 0.035 },
                { src: 8, dst: 12, coeff: 0.030 },
            ],
        },
        // All 16 pads are crafted -- nothing to disable.
        __crafted: Array.from({ length: 16 }, (_, i) => i),
    });
}

// ==============================================================================
// Unnatural Kit 3: Drone & Sustain
//   Friction + String / Membrane long sustains, feedback runaway.
// ==============================================================================
function droneSustainKit() {
    const pads = [];
    for (let i = 0; i < kNumPads; i++) pads.push(defaultPad());

    // Friction-bowed strings on pads 0-7 with varying material/size for
    // different drone pitches.
    for (let i = 0; i <= 7; i++) {
        const p = i;
        pads[p].exciterType = ExciterType.Friction;
        pads[p].bodyModel = (i % 2 === 0) ? BodyModelType.String : BodyModelType.Membrane;
        pads[p].material = 0.35 + (i / 7) * 0.50;
        pads[p].size     = 0.40 + (i % 4) * 0.10;
        pads[p].decay    = 0.92;
        pads[p].level    = 0.62;
        pads[p].frictionPressure = 0.35 + (i % 3) * 0.12;
        pads[p].modeStretch = 0.30 + (i % 6) * 0.06;
        pads[p].modeInjectAmount = 0.0;     // undamped, no note-off → infinite ring
        pads[p].nonlinearCoupling = 0.40 + (i % 4) * 0.10;
        pads[p].decaySkew = 0.85;
        pads[p].morphEnabled = (i % 3 === 0) ? 1.0 : 0.0;
        if (pads[p].morphEnabled) {
            pads[p].morphStart = 0.40; pads[p].morphEnd = 0.85;
            pads[p].morphDuration = 0.85; pads[p].morphCurve = 0.5;
        }
        pads[p].tsFilterType = FilterType.LP;
        pads[p].tsFilterCutoff = 0.45;
        pads[p].tsFilterResonance = 0.35;
        pads[p].tsFilterEnvAmount = 0.55;
        pads[p].tsFilterEnvAttack = 0.20;
        pads[p].tsFilterEnvDecay  = 0.40;
        pads[p].tsFilterEnvSustain = 0.55;
        pads[p].tsFilterEnvRelease = 0.65;
        pads[p].modeScatter = 0.10 + (i % 4) * 0.08;
        pads[p].airLoading  = 0.0;
        pads[p].couplingStrength = 0.18; pads[p].secondaryEnabled = 1.0;
        pads[p].secondarySize = 0.50; pads[p].secondaryMaterial = 0.65;
        pads[p].tensionModAmt = 0.25 + (i % 4) * 0.10;
        pads[p].noiseLayerMix = 0.18 + (i % 4) * 0.08;
        pads[p].noiseLayerCutoff = 0.50;
        pads[p].noiseLayerColor  = 0.40;
        pads[p].noiseLayerDecay  = 0.85;
        pads[p].clickLayerMix    = 0.0;
        pads[p].bodyDampingB1 = 0.30 + (i % 6) * 0.02;
        pads[p].bodyDampingB3 = 0.18 + (i % 4) * 0.05;
        pads[p].outputBus = i % 2;
        pads[p].macroComplexity = 0.78;
        pads[p].couplingAmount = 0.85;
    }

    // Feedback drones on pads 8-13: more chaotic sustain.
    for (let i = 8; i <= 13; i++) {
        const p = i;
        pads[p].exciterType = ExciterType.Feedback;
        pads[p].bodyModel = (i % 2 === 0) ? BodyModelType.Plate : BodyModelType.Shell;
        pads[p].material = 0.40 + ((i - 8) / 6) * 0.45;
        pads[p].size     = 0.45 + (i % 3) * 0.12;
        pads[p].decay    = 0.92;
        pads[p].level    = 0.62;
        pads[p].feedbackAmount = 0.40 + (i % 4) * 0.10;
        pads[p].modeStretch = 0.25 + (i % 5) * 0.08;
        pads[p].modeInjectAmount = 0.0;     // undamped, no note-off → infinite ring
        pads[p].nonlinearCoupling = 0.55;
        pads[p].decaySkew = 0.78;
        pads[p].tsFilterType = FilterType.BP;
        pads[p].tsFilterCutoff = 0.55;
        pads[p].tsFilterResonance = 0.45;
        pads[p].tsFilterEnvAmount = 0.30;
        pads[p].modeScatter = 0.40;
        pads[p].airLoading  = 0.20;
        pads[p].couplingStrength = 0.30;
        pads[p].tensionModAmt = 0.45;
        pads[p].noiseLayerMix = 0.30; pads[p].noiseLayerCutoff = 0.55;
        pads[p].noiseLayerColor = 0.55; pads[p].noiseLayerDecay = 0.85;
        pads[p].clickLayerMix = 0.0;
        pads[p].bodyDampingB1 = 0.30; pads[p].bodyDampingB3 = 0.20;
        pads[p].outputBus = 1;
        pads[p].couplingAmount = 0.85;
    }

    return Object.assign(pads, {
        __opts: {
            maxPolyphony: 8,
            stealingPolicy: 1,
            globalCoupling: 0.85,         // strong drone interplay
            snareBuzz: 0.0, tomResonance: 0.65,
            couplingDelayMs: 1.9,
            overrides: [
                { src: 0, dst: 4, coeff: 0.050 },
                { src: 4, dst: 0, coeff: 0.045 },
                { src: 2, dst: 6, coeff: 0.045 },
                { src: 8, dst: 12, coeff: 0.050 },
                { src: 1, dst: 5, coeff: 0.040 },
            ],
        },
        __crafted: Array.from({ length: 14 }, (_, i) => i),
    });
}

// ==============================================================================
// Unnatural Kit 4: Chaos Engine
//   Feedback + nonlinearCoupling + mode injection + extreme tension mod.
//   Maxed every nonlinear knob; result is intentionally chaotic.
// ==============================================================================
function chaosEngineKit() {
    const pads = [];
    for (let i = 0; i < kNumPads; i++) pads.push(defaultPad());

    for (let i = 0; i <= 13; i++) {
        const p = i;
        pads[p].exciterType = (i % 3 === 0) ? ExciterType.Feedback
            : (i % 3 === 1) ? ExciterType.FMImpulse : ExciterType.Friction;
        pads[p].bodyModel = [BodyModelType.Plate, BodyModelType.Shell,
                             BodyModelType.String, BodyModelType.Bell,
                             BodyModelType.Membrane, BodyModelType.NoiseBody][i % 6];
        pads[p].material = 0.20 + (i * 0.06) % 0.75;
        pads[p].size     = 0.25 + (i * 0.05) % 0.65;
        pads[p].decay    = 0.40 + (i % 7) * 0.07;
        pads[p].level    = 0.72;
        pads[p].fmRatio  = 0.20 + (i * 0.07) % 0.70;
        pads[p].feedbackAmount   = 0.40 + (i % 4) * 0.12;
        pads[p].frictionPressure = 0.30 + (i % 3) * 0.15;
        pads[p].modeStretch      = 0.40 + (i % 5) * 0.12;
        pads[p].modeInjectAmount = 0.0;     // undamped, no note-off → infinite ring
        pads[p].nonlinearCoupling = 0.65 + (i % 3) * 0.10;    // MAXED
        pads[p].decaySkew        = 0.40 + (i % 5) * 0.12;
        pads[p].morphEnabled = (i % 2 === 0) ? 1.0 : 0.0;
        if (pads[p].morphEnabled) {
            pads[p].morphStart = (i % 4) * 0.20;
            pads[p].morphEnd   = 0.85 - (i % 3) * 0.20;
            pads[p].morphDuration = 0.20 + (i % 5) * 0.15;
            pads[p].morphCurve = (i % 7) * 0.14;
        }
        pads[p].tsFilterType =
            [FilterType.LP, FilterType.HP, FilterType.BP][i % 3];
        pads[p].tsFilterCutoff    = 0.30 + (i * 0.07) % 0.60;
        pads[p].tsFilterResonance = 0.55 + (i % 4) * 0.10;
        pads[p].tsFilterEnvAmount = 0.55;
        pads[p].tsFilterEnvAttack = (i % 5) * 0.04;
        pads[p].tsFilterEnvDecay  = 0.10 + (i % 4) * 0.10;
        pads[p].tsDriveAmount     = 0.30 + (i % 4) * 0.12;
        pads[p].tsFoldAmount      = 0.20 + (i % 3) * 0.18;
        pads[p].modeScatter       = 0.55 + (i % 5) * 0.08;
        pads[p].airLoading        = (i % 4) * 0.20;
        pads[p].couplingStrength  = 0.40 + (i % 3) * 0.15;
        pads[p].secondaryEnabled  = 1.0;
        pads[p].secondarySize     = 0.30 + (i * 0.05) % 0.50;
        pads[p].secondaryMaterial = (i * 0.07) % 1.0;
        pads[p].tensionModAmt     = 0.78 + (i % 4) * 0.05;     // MAXED
        pads[p].noiseLayerMix     = 0.20 + (i % 5) * 0.10;
        pads[p].noiseLayerCutoff  = 0.30 + (i % 6) * 0.10;
        pads[p].noiseLayerResonance = 0.30 + (i % 4) * 0.12;
        pads[p].noiseLayerColor   = 0.30 + (i % 5) * 0.12;
        pads[p].noiseLayerDecay   = 0.40 + (i % 7) * 0.08;
        pads[p].clickLayerMix     = 0.20 + (i % 5) * 0.10;
        pads[p].clickLayerContactMs = 0.10 + (i % 4) * 0.10;
        pads[p].clickLayerBrightness = 0.40 + (i % 5) * 0.12;
        pads[p].bodyDampingB1 = 0.30 + (i % 6) * 0.05;
        pads[p].bodyDampingB3 = (pads[p].bodyModel === BodyModelType.Bell) ? 0.0
                              : 0.20 + (i % 4) * 0.10;
        pads[p].chokeGroup    = (i < 4) ? 0 : ((i % 2) + 1);   // pairs into chokes
        pads[p].outputBus     = i % 4;
        pads[p].tsPitchEnvStart = toLogNorm(180 + (i * 30) % 350);
        pads[p].tsPitchEnvEnd   = toLogNorm(40 + (i * 12) % 200);
        pads[p].tsPitchEnvTime  = 0.04 + (i % 5) * 0.04;
        pads[p].tsPitchEnvCurve = (i % 2) ? 1.0 : 0.0;
        pads[p].macroTightness  = 0.20 + (i % 5) * 0.15;
        pads[p].macroBrightness = 0.30 + (i % 4) * 0.18;
        pads[p].macroBodySize   = 0.30 + (i % 5) * 0.14;
        pads[p].macroPunch      = 0.40 + (i % 4) * 0.15;
        pads[p].macroComplexity = 0.85;       // chaos = MAX complexity
        pads[p].couplingAmount  = 0.78;
    }

    return Object.assign(pads, {
        __opts: {
            maxPolyphony: 12,
            stealingPolicy: 2,
            globalCoupling: 0.92,         // chaos all the way
            snareBuzz: 0.55,
            tomResonance: 0.78,
            couplingDelayMs: 1.7,
            overrides: [
                { src: 0, dst: 7, coeff: 0.050 },
                { src: 1, dst: 9, coeff: 0.050 },
                { src: 3, dst: 11, coeff: 0.050 },
                { src: 5, dst: 13, coeff: 0.050 },
                { src: 7, dst: 0, coeff: 0.045 },
                { src: 9, dst: 1, coeff: 0.045 },
            ],
        },
        __crafted: Array.from({ length: 14 }, (_, i) => i),
    });
}

// ==============================================================================
// Unnatural Kit 5: Ghost Bones
//   Sub-bell tones, very inharmonic (modeStretch maxed), high decaySkew so
//   sustain dominates attack. Feels like haunted resonance.
// ==============================================================================
function ghostBonesKit() {
    const pads = [];
    for (let i = 0; i < kNumPads; i++) pads.push(defaultPad());

    for (let i = 0; i <= 13; i++) {
        const p = i;
        const useFriction = (i % 5 === 4);
        pads[p].exciterType = useFriction ? ExciterType.Friction
            : (i % 3 === 0) ? ExciterType.Mallet
            : (i % 3 === 1) ? ExciterType.FMImpulse : ExciterType.Impulse;
        pads[p].bodyModel = (i % 2 === 0) ? BodyModelType.Bell : BodyModelType.String;
        pads[p].material = 0.30 + (i * 0.04) % 0.55;
        pads[p].size     = 0.20 + ((13 - i) / 13) * 0.55;
        pads[p].decay    = 0.85 - (i % 5) * 0.05;
        pads[p].level    = 0.65;
        pads[p].fmRatio  = 0.30 + (i * 0.05) % 0.50;
        pads[p].frictionPressure = useFriction ? 0.30 : 0.0;
        pads[p].modeStretch      = 0.65 + (i % 4) * 0.08;     // VERY inharmonic
        pads[p].modeInjectAmount = 0.0;     // undamped, no note-off → infinite ring
        pads[p].nonlinearCoupling = 0.20 + (i % 4) * 0.08;
        pads[p].decaySkew        = 0.78 + (i % 3) * 0.06;     // sustain-dominant
        pads[p].morphEnabled = (i % 3 !== 0) ? 1.0 : 0.0;
        if (pads[p].morphEnabled) {
            pads[p].morphStart = 0.40 + (i % 4) * 0.10;
            pads[p].morphEnd   = 0.80;
            pads[p].morphDuration = 0.65 + (i % 3) * 0.10;
            pads[p].morphCurve = 0.35 + (i % 4) * 0.10;
        }
        pads[p].tsFilterType = FilterType.HP;
        pads[p].tsFilterCutoff = 0.18 + (i % 4) * 0.10;
        pads[p].tsFilterResonance = 0.30;
        pads[p].tsFilterEnvAmount = 0.45;
        pads[p].tsFilterEnvDecay  = 0.30;
        pads[p].modeScatter = 0.40 + (i % 4) * 0.10;
        pads[p].airLoading  = 0.0;
        pads[p].couplingStrength = (i % 2) * 0.30;
        pads[p].secondaryEnabled = (i % 2) ? 1.0 : 0.0;
        pads[p].secondarySize    = 0.25 + (i % 3) * 0.08;
        pads[p].secondaryMaterial = 0.85;
        pads[p].tensionModAmt = 0.30 + (i % 4) * 0.12;
        pads[p].noiseLayerMix = 0.10 + (i % 5) * 0.06;
        pads[p].noiseLayerCutoff = 0.30 + (i % 6) * 0.10;
        pads[p].noiseLayerColor  = 0.20 + (i % 4) * 0.10;
        pads[p].noiseLayerDecay  = 0.85;
        pads[p].clickLayerMix    = 0.10 + (i % 4) * 0.08;
        pads[p].clickLayerBrightness = 0.55 + (i % 4) * 0.08;
        pads[p].bodyDampingB1 = 0.30 + (i % 5) * 0.02;
        pads[p].bodyDampingB3 = (pads[p].bodyModel === BodyModelType.Bell) ? 0.0
                              : 0.18;
        pads[p].outputBus = (i % 4 === 3) ? 1 : 0;
        pads[p].macroBrightness = 0.30 + (i % 4) * 0.12;
        pads[p].macroComplexity = 0.85;
        pads[p].macroBodySize   = 0.55;
        pads[p].couplingAmount  = 0.85;
    }

    return Object.assign(pads, {
        __opts: {
            maxPolyphony: 12,
            globalCoupling: 0.78,
            snareBuzz: 0.0,
            tomResonance: 0.55,
            couplingDelayMs: 2.0,
            overrides: [
                { src: 0, dst: 6, coeff: 0.045 },
                { src: 6, dst: 12, coeff: 0.040 },
                { src: 2, dst: 8, coeff: 0.040 },
                { src: 8, dst: 13, coeff: 0.035 },
            ],
        },
        __crafted: Array.from({ length: 14 }, (_, i) => i),
    });
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
//
// Each kit-builder function returns the pads array with optional `__opts`
// (passed through to writeKitPreset for global params + override matrix)
// and `__crafted` (pad indices to keep enabled; everything else gets
// disabled via disableUncraftedPads). Both fields are stripped before
// serialisation.
function buildKit(builder) {
    const pads = builder();
    const opts = pads.__opts || {};
    const crafted = pads.__crafted;
    delete pads.__opts;
    delete pads.__crafted;
    if (Array.isArray(crafted)) {
        disableUncraftedPads(pads, crafted);
    }
    return { pads, opts };
}

const kits = [
    // --- Acoustic (5) ---
    { name: 'Acoustic Studio Kit', subdir: 'Acoustic',  pads: acousticKit() },
    { name: 'Jazz Brushes',        subdir: 'Acoustic',  ...buildKit(jazzBrushesKit) },
    { name: 'Rock Big Room',       subdir: 'Acoustic',  ...buildKit(rockBigRoomKit) },
    { name: 'Vintage Wood',        subdir: 'Acoustic',  ...buildKit(vintageWoodKit) },
    { name: 'Orchestral',          subdir: 'Acoustic',  ...buildKit(orchestralKit) },

    // --- Electronic (5) ---
    { name: '808 Electronic Kit',  subdir: 'Electronic', pads: electronicKit() },
    { name: '909 Drum Machine',    subdir: 'Electronic', ...buildKit(nineOhNineKit) },
    { name: 'LinnDrum CR-78',      subdir: 'Electronic', ...buildKit(linnDrumKit) },
    { name: 'Modular West Coast',  subdir: 'Electronic', ...buildKit(modularWestCoastKit) },
    { name: 'Trap Modern',         subdir: 'Electronic', ...buildKit(trapModernKit) },

    // --- Percussive (5) ---
    { name: 'Hand Drums',          subdir: 'Percussive', ...buildKit(handDrumsKit) },
    { name: 'Latin Perc',          subdir: 'Percussive', ...buildKit(latinPercKit) },
    { name: 'Tabla',               subdir: 'Percussive', ...buildKit(tablaKit) },
    { name: 'World Metal',         subdir: 'Percussive', ...buildKit(worldMetalKit) },
    { name: 'Cajon and Frames',    subdir: 'Percussive', ...buildKit(cajonFramesKit) },

    // --- Unnatural (5) ---
    { name: 'Experimental FX Kit', subdir: 'Unnatural',  pads: experimentalKit() },
    { name: 'Glass Bell Garden',   subdir: 'Unnatural',  ...buildKit(glassBellGardenKit) },
    { name: 'Drone and Sustain',   subdir: 'Unnatural',  ...buildKit(droneSustainKit) },
    { name: 'Chaos Engine',        subdir: 'Unnatural',  ...buildKit(chaosEngineKit) },
    { name: 'Ghost Bones',         subdir: 'Unnatural',  ...buildKit(ghostBonesKit) },
];

for (const kit of kits) {
    const dir = path.join(outputBase, kit.subdir);
    fs.mkdirSync(dir, { recursive: true });

    const buf = writeKitPreset(kit.pads, kit.opts || {});
    const filePath = path.join(dir, `${kit.name}.memkit`);
    fs.writeFileSync(filePath, buf);

    console.log(`Wrote ${buf.length} bytes to ${filePath}`);
}

console.log(`Done. ${kits.length} factory kit presets generated.`);
