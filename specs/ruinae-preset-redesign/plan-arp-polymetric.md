# Ruinae Preset Plan — Arp Polymetric

This category exists to make the arpeggiator's polymetric lane engine audible as a *musical* device, not a novelty. Both presets run several arp lanes whose lengths are mutually coprime so the pattern never repeats within a musically useful window, but they attack the space from opposite ends: "3x5x7 Evolving" holds all lanes at integer speed 1.0 and lets a glacial free LFO morph the harmonic body so *timbre* evolves across the 105-step cycle, while "Reich Drift" leaves lane lengths short but runs the pitch and ratchet lanes at *fractional* speeds (1.5 / 0.75) so the riff phases out of alignment and slowly re-locks, Steve-Reich style. Between them they exercise coprime lane lengths, both integer and fractional per-lane speed multipliers, pitch/ratchet/velocity/gate lanes, octaveMode, the mod-matrix scale axis, and two distinct wrapper FX chains (plate reverb vs. ping-pong delay). Neither uses the boring template helpers.

## 1. "3x5x7 Evolving" -> "3x5x7 Evolving"
- **Locate:** the block containing `p.name = "3x5x7 Evolving"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A dual-Additive morph pad whose harmonic body inhales and exhales under three integer-coprime lanes (3/5/7) that never realign — timbre, not just rhythm, evolves.
- **Coverage:** coprime lane lengths (3/5/7); pitch lane; ratchet lane; octaveMode; LFO->morph background motion; per-lane speed multipliers (all 1.0, integer-coprime showcase).
- **Rationale:** Directive nailed: OSC A/B are BOTH Additive (engine #4, the largest dormant territory) at identical pitch so the mixer crossfade is pure timbre; a 0.05 Hz free Sine LFO on MorphPos (amount 0.9, SCurve) makes the harmonic body drift open/shut — the audible "evolving" gesture. Lanes are exactly 3 (velocity) / 5 (pitch) / 7 (ratchet), all speed 1.0 — the integer-coprime showcase (105-step cycle). Second free LFO2->SpecTilt phases against the first; ArpPitch->FltCut with scale=3 (x2) turns the never-repeating pitch grid into timbral motion and exercises the untouched scale axis. envAmount=+18 st fixes the 4000 bug with a real slow filter breath. Plate (not hall) + octaveMode=1 diversify the wrapper and cover octaveMode.
- **Replacement code:**

```cpp
    // T032: "3x5x7 Evolving" — integer coprime lanes (3/5/7) over a dual-Additive
    // morph pad; a glacial free LFO sweeps the A<->B blend so the harmonic body
    // breathes beneath a grid that won't repeat for 3*5*7 = 105 steps. NO template.
    {
        PresetDef p;
        p.name = "3x5x7 Evolving";
        p.category = "Arp Polymetric";

        // --- Voice: two Additive oscillators at the SAME pitch, so the mixer morph
        //     is PURE timbre. A = dark/hollow (few partials, falling tilt);
        //     B = bright/rich (many partials, faint inharmonic shimmer).
        p.state.oscA.type = 4;                 // Additive
        p.state.oscA.additivePartials = 8;     // organ-ish, hollow body
        p.state.oscA.additiveTilt = -6.0f;     // dark: HF rolled off
        p.state.oscA.additiveInharm = 0.0f;    // pure harmonic
        p.state.oscA.level = 0.85f;
        p.state.oscB.type = 4;                 // Additive
        p.state.oscB.additivePartials = 48;    // bright, spectrally rich
        p.state.oscB.additiveTilt = 5.0f;      // tilted up: airy top end
        p.state.oscB.additiveInharm = 0.15f;   // faint bell/glass shimmer
        p.state.oscB.level = 0.85f;
        p.state.mixer.mode = 0;                // Crossfade
        p.state.mixer.position = 0.5f;         // start mid-morph; LFO sweeps it

        // --- Filter: 24 dB SVF LP with a SLOW filter-env "inhale". Fixes the classic
        //     bug: envAmount is SEMITONES (-48..+48), so +18 is a real, audible sweep.
        p.state.filter.type = 0;               // SVF LP
        p.state.filter.cutoffHz = 3800.0f;
        p.state.filter.resonance = 0.35f;
        p.state.filter.svfSlope = 1;           // 24 dB
        p.state.filter.envAmount = 18.0f;      // +18 st slow filter breath (was buggy 4000)
        p.state.filter.keyTrack = 0.3f;        // brighter up the keyboard
        p.state.filterEnv.attackMs = 900.0f;   // very slow open
        p.state.filterEnv.decayMs = 2500.0f;
        p.state.filterEnv.sustain = 0.6f;
        p.state.filterEnv.releaseMs = 1800.0f;

        // --- Amp env: classic pad swell/tail.
        p.state.ampEnv.attackMs = 400.0f;
        p.state.ampEnv.decayMs = 1500.0f;
        p.state.ampEnv.sustain = 0.75f;
        p.state.ampEnv.releaseMs = 1200.0f;

        // --- Wide poly so overlapping arp tails spread across the field.
        p.state.global.width = 1.4f;           // stereo widen (0..2)
        p.state.global.spread = 0.4f;          // pan spread across voices
        p.state.global.polyphony = 12;         // room for long tails to stack

        // --- Mod identity: THREE independent motions, none shared with the sibling.
        // (0) Glacial FREE LFO1 -> morph position: the A<->B spectrum drifts open and
        //     shut over ~20 s beneath the grid. This is the "evolving" core.
        p.state.lfo1.rateHz = 0.05f;           // ~20 s cycle
        p.state.lfo1.shape = 0;                // Sine
        p.state.lfo1.depth = 1.0f;
        p.state.lfo1.sync = 0;                 // FREE-running (not tempo-locked)
        setModSlot(p.state, 0, kSrcLFO1, kDstAllMorphPos, 0.9f, kCurveSCurve);
        // (1) A second, unrelated slow triangle LFO2 -> spectral tilt, so the two
        //     brightness motions phase against each other.
        p.state.lfo2.rateHz = 0.08f;
        p.state.lfo2.shape = 1;                // Triangle
        p.state.lfo2.depth = 0.6f;
        p.state.lfo2.sync = 0;
        setModSlot(p.state, 1, kSrcLFO2, kDstAllSpecTilt, 0.3f);
        // (2) Arp pitch -> filter cutoff: every polymetric step is a different pitch,
        //     so each nudges the cutoff — the never-repeating grid becomes timbral.
        setModSlot(p.state, 2, kSrcArpPitch, kDstAllFltCut, 0.35f, kCurveExp);
        p.state.modMatrix.slots[2].scale = 3;  // x2 depth (exercises the dormant scale axis)

        // --- Wrapper: a PLATE (not the default diffuse hall) keeps the top shimmery.
        p.state.reverbEnabled = 1;
        p.state.reverbType = 0;                // Plate
        p.state.reverb.size = 0.55f;
        p.state.reverb.mix = 0.28f;
        p.state.reverb.damping = 0.35f;

        // --- Arp: integer-coprime lanes 3/5/7, ALL at default speed 1.0. The lengths
        //     alone guarantee non-repetition; the sibling owns fractional speeds.
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUp);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 90.0f);      // slight overlap for a legato pad grid
        p.state.arp.octaveRange = 2;
        p.state.arp.octaveMode = 1;            // Interleaved octaves (exercises octaveMode)
        float vel3[] = {0.6f, 0.85f, 1.0f};            // length 3
        setVelocityLane(p.state, 3, vel3);
        int32_t pitch5[] = {0, 5, 7, 3, 10};           // length 5
        setPitchLane(p.state, 5, pitch5);
        int32_t ratch7[] = {1, 1, 2, 1, 1, 3, 1};      // length 7, occasional rolls
        setRatchetLane(p.state, 7, ratch7);

        presets.push_back(std::move(p));
    }
```

## 2. "4x5 Shifting" -> "Reich Drift"
- **Locate:** the block containing `p.name = "4x5 Shifting"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A gritty hard-sync 303-style bass whose pitch lane runs 1.5x and ratchet lane 0.75x against 1.0 lanes, so the riff slides out of phase and slowly re-locks, Steve-Reich style.
- **Coverage:** coprime lane lengths (4/5/6); per-lane speed multipliers (pitch/ratchet); fractional lane speeds (0.75, 1.5) for phasing; pitch lane; ratchet lane; LFO->morph background motion (LFO->cutoff variant).
- **Rationale:** Directive delivered: OSC A is the Sync engine (type 3, hard-sync slave, ratio 2.5) tuned -12 for an octave-down bass, layered over a -24 square sub; the FRACTIONAL-speed axis is the star — pitchLaneSpeed=1.5 and ratchetLaneSpeed=0.75 against 1.0 velocity/gate lanes make the riff phase out and re-lock (true Reich drift), with lane lengths 4/5/6 present. Alternating ratchet {1,2,1,2,1,2} is the shuffling double-hit groove. Bug fixed: envAmount=+30 st drives a real Ladder acid sweep (drive 6 dB, res 0.5) instead of the inaudible 4000. Mono+legato+35 ms porta gives 303 glide. It owns TapeSaturator distortion, driven per-note by ENV3->DistDrive; a tempo-synced SmoothRandom LFO->cutoff (shape 5, an unused waveform) keeps the phasing riff re-colouring. PingPong delay and NO reverb diversify the wrapper away from the sibling's plate. Renamed from the generic "4x5 Shifting" to the evocative "Reich Drift".
- **Replacement code:**

```cpp
    // T033: "Reich Drift" (was "4x5 Shifting") — hard-sync bass whose PITCH and
    // RATCHET lanes run at FRACTIONAL speeds (1.5 / 0.75) against 1.0 lanes, so the
    // riff phases out and slowly re-aligns against the beat. NO template.
    {
        PresetDef p;
        p.name = "Reich Drift";
        p.category = "Arp Polymetric";

        // --- Voice: OSC A = hard-Sync engine (buzzy, formant-rich slave) an octave
        //     down; OSC B = a square sub two octaves down for low-end weight.
        p.state.oscA.type = 3;                 // Sync
        p.state.oscA.tuneSemitones = -12.0f;   // octave-down bass
        p.state.oscA.syncRatio = 2.5f;         // bright classic sync formant
        p.state.oscA.syncWaveform = 1;         // Saw slave (aggressive)
        p.state.oscA.syncMode = 0;             // Hard sync
        p.state.oscA.syncAmount = 1.0f;
        p.state.oscA.level = 0.9f;
        p.state.oscB.type = 0;                 // PolyBLEP
        p.state.oscB.waveform = 3;             // Square
        p.state.oscB.tuneSemitones = -24.0f;   // deep sub
        p.state.oscB.level = 0.45f;
        p.state.mixer.position = 0.4f;         // favour the sync bite, keep the sub

        // --- Filter: driven Ladder with a fast, deep env sweep. Bug fixed: envAmount
        //     is SEMITONES, +30 = a punchy acid sweep (the old 4000 was inaudible).
        p.state.filter.type = 4;               // Ladder LP
        p.state.filter.cutoffHz = 380.0f;      // low base so the env sweep is obvious
        p.state.filter.resonance = 0.5f;
        p.state.filter.ladderSlope = 4;        // 24 dB/oct
        p.state.filter.ladderDrive = 6.0f;     // dB of drive for bass grit
        p.state.filter.envAmount = 30.0f;      // +30 st acid sweep (was buggy 4000)
        p.state.filter.keyTrack = 0.4f;
        p.state.filterEnv.attackMs = 1.0f;     // instant snap
        p.state.filterEnv.decayMs = 180.0f;
        p.state.filterEnv.sustain = 0.15f;
        p.state.filterEnv.releaseMs = 120.0f;

        // --- Amp env: tight plucky bass.
        p.state.ampEnv.attackMs = 2.0f;
        p.state.ampEnv.decayMs = 220.0f;
        p.state.ampEnv.sustain = 0.5f;
        p.state.ampEnv.releaseMs = 140.0f;

        // --- Mono + glide: 303-style legato so held riff notes slide into each other.
        p.state.global.voiceMode = 1;          // Mono
        p.state.monoMode.legato = 1;
        p.state.monoMode.portamentoTimeMs = 35.0f;
        p.state.monoMode.portaMode = 1;        // glide on legato only

        // --- Grit: Tape-Saturator distortion (this preset OWNS that dirty type). A
        //     per-note mod-env transient pushes the drive so each hit spits.
        p.state.distortion.type = 5;           // TapeSaturator
        p.state.distortion.drive = 0.35f;
        p.state.distortion.character = 0.5f;
        p.state.distortion.mix = 0.8f;
        p.state.modEnv.attackMs = 1.0f;
        p.state.modEnv.decayMs = 120.0f;
        p.state.modEnv.sustain = 0.0f;
        p.state.modEnv.releaseMs = 80.0f;

        // --- Mod identity (distinct from the sibling's smooth 3-LFO drift):
        // (voice 0) Velocity -> cutoff: dynamics open the filter per step.
        setVoiceRoute(p.state, 0, kVSrcVelocity, kVDstFltCut, 0.45f);
        // (voice 1) Mod-env (ENV3) -> distortion drive: a spitting grit transient per note.
        setVoiceRoute(p.state, 1, kVSrcEnv3, kVDstDistDrive, 0.5f);
        // (global 0) Tempo-synced SmoothRandom LFO1 -> all-voice cutoff: a stepped
        //     filter wander locked to the grid, so the phasing riff keeps re-colouring.
        p.state.lfo1.rateHz = 4.0f;
        p.state.lfo1.shape = 5;                // SmoothRandom (rarely-used shape)
        p.state.lfo1.depth = 1.0f;
        p.state.lfo1.sync = 1;                 // tempo-locked
        p.state.lfo1Ext.noteValue = kNote1_16; // 1/16 wander
        setModSlot(p.state, 0, kSrcLFO1, kDstAllFltCut, 0.25f, kCurveLinear);

        // --- Wrapper: PingPong delay (NO reverb) throws the shuffling ratchets across
        //     the stereo field — reinforces the "shifting" motion, diversifies the FX.
        p.state.delayEnabled = 1;
        p.state.delay.type = 2;                // PingPong
        p.state.delay.sync = 1;
        p.state.delay.noteValue = kNote1_8;
        p.state.delay.feedback = 0.35f;
        p.state.delay.mix = 0.22f;
        p.state.delay.pingPongCrossFeed = 0.8f;
        p.state.delay.pingPongWidth = 130.0f;

        // --- Arp: lanes 4/5/6, with PITCH @1.5x and RATCHET @0.75x — the fractional
        //     speeds phase the riff against the beat and each other (the whole point).
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeAsPlayed);    // hold one note; the lanes build the riff
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 75.0f);
        p.state.arp.octaveRange = 1;           // stay in the bass register
        float vel4[] = {0.7f, 1.0f, 0.6f, 0.9f};             // length 4 @ 1.0
        setVelocityLane(p.state, 4, vel4);
        float gate5[] = {0.9f, 0.6f, 1.0f, 0.7f, 0.5f};      // length 5 @ 1.0
        setGateLane(p.state, 5, gate5);
        int32_t pitch5[] = {0, 12, 7, 0, -5};                // length 5 octave/fifth riff
        setPitchLane(p.state, 5, pitch5);
        p.state.arp.pitchLaneSpeed = 1.5f;     // pitch lane runs 1.5x — phases AHEAD
        int32_t ratch6[] = {1, 2, 1, 2, 1, 2};               // length 6, alternating double-hits
        setRatchetLane(p.state, 6, ratch6);
        p.state.arp.ratchetLaneSpeed = 0.75f;  // ratchet lane runs 0.75x — drags BEHIND

        presets.push_back(std::move(p));
    }
```
