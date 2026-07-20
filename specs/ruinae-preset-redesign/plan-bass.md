# Ruinae Preset Plan — Bass

The Bass suite is redesigned so each of the 20 presets owns a distinct engine/filter/distortion/mod combination and, collectively, the presets exercise every functional area Ruinae can reach in a low register: every oscillator engine (PolyBLEP VA, Phase Distortion, Hard Sync, Additive, Chaos, Particle, Formant, Noise), every filter model (SVF LP/Notch/Peak, Ladder, Comb, Formant, EnvFilter), every distortion (TapeSaturator, Wavefolder, GranularDistortion, Spectral, RingModulator), all three mod-source families (LFO, Chaos, envelopes) into every reachable destination, both voice-route and mod-matrix paths (including the depth-scale and curve axes), the harmonizer PitchSync sub, and both delay flavours plus the phaser. Recurring fixes across the family: the `filter.envAmount` field is PLAIN semitones (range roughly -48..+48), so old "Hz-shaped" values (e.g. 4000, or a bare 30 read as Hz) are corrected to real semitone sweeps; `chaosMod` defaults to depth 0 (silent) and must be raised before it is routed; and where a spec target (detune, ringRatio, grain jitter) is not a mod destination, the movement is baked into the engine and the LFO is spent on a reachable destination instead. Each block below is a verbatim drop-in replacement.

## 1. "Sub Bass" -> "Sub Bass"
- **Locate:** the block containing `p.name = "Sub Bass"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A clean, phase-locked sine sub with parallel tape warmth, a slow breathing cutoff, and a PitchSync octave-double for earth-shaking foundational weight.
- **Coverage:**
  - engine: PolyBLEP (sine + triangle)
  - filter: SVF LP + keyTrack
  - osc start phase
  - distortion: TapeSaturator
  - mono voiceMode
  - harmonizer PitchSync sub-double
  - modEnv -> DistDrive voice route
  - free SCurve LFO1 -> AllFltCut mod slot
- **Rationale:** Keeps the pure sine+triangle sub identity but earns a distinct trait: explicit phase=0 on both oscs (owns the start-phase axis, guarantees a click-free onset), full keyTrack so the LP stays even across the range, and a parallel TapeSaturator (mix 0.6) for analog weight without muddying the fundamental. modEnv (400ms attack) routed to DistDrive blooms the tape grit after the transient - a gesture no sibling repeats. A 0.1Hz free SCurve LFO on cutoff keeps sustained notes alive. Harmonizer PitchSync at -12st adds a low-latency octave-down reinforcement, the tightest pitch mode for bass. No issues flagged; unchanged.
- **Replacement code:**
```cpp
    // "Sub Bass" - Clean foundational sub; owns osc start-phase + tape warmth + PitchSync sub-double
    {
        PresetDef p;
        p.name = "Sub Bass";
        p.category = "Bass";
        auto& s = p.state;
        // Pure sine fundamental one octave down, whisper of triangle harmonic
        s.oscA.type = 0;            // PolyBLEP
        s.oscA.waveform = 0;       // Sine
        s.oscA.tuneSemitones = -12.0f;
        s.oscA.level = 0.9f;
        s.oscA.phase = 0.0f;       // start at zero-crossing => click-free attack (owns start-phase axis)
        s.oscB.type = 0;
        s.oscB.waveform = 4;       // Triangle
        s.oscB.tuneSemitones = -12.0f;
        s.oscB.level = 0.2f;
        s.oscB.phase = 0.0f;       // both oscs phase-locked for a consistent, thump-free onset
        s.mixer.position = 0.15f;  // mostly sine
        // SVF LP with full key-tracking so the sub stays even across the keyboard
        s.filter.type = 0;         // SVF LP
        s.filter.cutoffHz = 500.0f;
        s.filter.resonance = 0.1f;
        s.filter.keyTrack = 1.0f;
        s.ampEnv.attackMs = 2.0f;
        s.ampEnv.decayMs = 150.0f;
        s.ampEnv.sustain = 0.85f;
        s.ampEnv.releaseMs = 120.0f;
        s.global.voiceMode = 1;    // Mono
        // Tape saturator: the one distinctive trait of the "reference" sub - analog weight
        s.distortion.type = 5;     // TapeSaturator
        s.distortion.tapeModel = 0;
        s.distortion.tapeSaturation = 0.4f;
        s.distortion.tapeBias = 0.5f;
        s.distortion.drive = 0.2f;
        s.distortion.mix = 0.6f;   // parallel warmth, keeps the clean fundamental intact
        // Mod-env slowly blooms the tape drive after the attack (distinctive gesture, unique to Sub)
        s.modEnv.attackMs = 400.0f;
        s.modEnv.decayMs = 600.0f;
        s.modEnv.sustain = 1.0f;
        s.modEnv.releaseMs = 300.0f;
        setVoiceRoute(s, 0, kVSrcEnv3, kVDstDistDrive, 0.3f);
        // A very slow, free, smoothstep LFO breathes the cutoff so held notes aren't dead-static
        s.lfo1.rateHz = 0.1f; s.lfo1.shape = 0; s.lfo1.depth = 1.0f; s.lfo1.sync = 0;
        setModSlot(s, 0, kSrcLFO1, kDstAllFltCut, 0.15f, kCurveSCurve);
        // Harmonizer PitchSync doubles an octave below for earth-shaking sub reinforcement
        s.harmonizerEnabled = 1;
        s.harmonizer.harmonyMode = 0;      // Chromatic (literal semitone interval)
        s.harmonizer.pitchShiftMode = 3;   // PitchSync (low-latency, tight for bass)
        s.harmonizer.numVoices = 1;
        s.harmonizer.voiceInterval[0] = -12; // one octave down
        s.harmonizer.voiceLevelDb[0] = -4.0f;
        s.harmonizer.dryLevelDb = 0.0f;
        s.harmonizer.wetLevelDb = -3.0f;
        presets.push_back(std::move(p));
    }
```

## 2. "Reese" -> "Reese"
- **Locate:** the block containing `p.name = "Reese"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A fat detuned-saw Reese that never sits still: an 8-stage phaser swirls, a slow free LFO breathes the morph, and Rossler chaos drifts the ladder cutoff into analog tape echoes.
- **Coverage:**
  - engine: PolyBLEP dual saw detune
  - filter: Ladder LP + ladderDrive/slope
  - Phaser FX
  - LFO2 -> AllMorphPos mod slot (SCurve)
  - chaosMod source -> AllFltCut (SCurve)
  - velocity -> cutoff voice route
  - mono voiceMode + legato + portamento
  - Tape delay
- **Rationale:** Widens the detune to 12 cents for an audibly faster Reese beat, then layers three independent motions so nothing is static: an 8-stage 0.25Hz phaser (the signature Reese swirl), a 0.15Hz free sine LFO2 on morph position with an SCurve curve (the beat 'breathes'), and Rossler chaosMod drifting the ladder cutoff (SCurve) for organic never-repeating movement - honoring the directive's 'chaos for organic movement' via the nearest reachable global dest since detune itself isn't a mod destination. Velocity->cutoff adds playability; a synced tape delay adds analog width without mud. Ladder drive 2.0 keeps it thick. No issues flagged; unchanged.
- **Replacement code:**
```cpp
    // "Reese" - Moving detuned-saw Reese: phaser + slow morph LFO + chaos filter drift
    {
        PresetDef p;
        p.name = "Reese";
        p.category = "Bass";
        auto& s = p.state;
        s.oscA.type = 0; s.oscA.waveform = 1; // Saw
        s.oscA.tuneSemitones = -12.0f; s.oscA.level = 0.8f;
        s.oscA.phase = 0.0f;
        s.oscB.type = 0; s.oscB.waveform = 1; // Saw
        s.oscB.tuneSemitones = -12.0f;
        s.oscB.fineCents = 12.0f;   // wider detune than before for a fatter, faster Reese beat
        s.oscB.level = 0.8f;
        s.mixer.position = 0.5f;
        s.filter.type = 4;          // Ladder LP
        s.filter.cutoffHz = 2400.0f;
        s.filter.resonance = 0.25f;
        s.filter.ladderSlope = 4;   // 24 dB/oct
        s.filter.ladderDrive = 2.0f;
        s.ampEnv.attackMs = 8.0f; s.ampEnv.decayMs = 300.0f;
        s.ampEnv.sustain = 0.85f; s.ampEnv.releaseMs = 250.0f;
        s.global.voiceMode = 1; s.monoMode.legato = 1; s.monoMode.portamentoTimeMs = 25.0f;
        // Phaser gives the classic swirling Reese motion
        s.modulationType = 1;       // Phaser
        s.phaser.rateHz = 0.25f; s.phaser.depth = 0.5f;
        s.phaser.feedback = 0.45f; s.phaser.mix = 0.35f;
        s.phaser.stages = 3;        // 8-stage
        // Slow free sine LFO drifts the A/B morph => the detune "breathes"
        s.lfo2.rateHz = 0.15f; s.lfo2.shape = 0; s.lfo2.depth = 0.5f; s.lfo2.sync = 0;
        setModSlot(s, 0, kSrcLFO2, kDstAllMorphPos, 0.4f, kCurveSCurve);
        // Chaos (Rossler) drifts the ladder cutoff for organic, never-repeating movement
        s.chaosMod.rateHz = 0.4f; s.chaosMod.type = 1; s.chaosMod.depth = 0.5f; s.chaosMod.sync = 0;
        setModSlot(s, 1, kSrcChaos, kDstAllFltCut, 0.25f, kCurveSCurve);
        // Velocity opens the filter for dynamic playing
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.45f);
        // Analog tape echo for width/depth without washing out the low end
        s.delayEnabled = 1;
        s.delay.type = 1;           // Tape
        s.delay.sync = 1; s.delay.noteValue = kNote1_8;
        s.delay.feedback = 0.3f; s.delay.mix = 0.18f;
        s.delay.tapeSaturation = 0.5f;
        presets.push_back(std::move(p));
    }
```

## 3. "Acid Bass" -> "Acid Bass"
- **Locate:** the block containing `p.name = "Acid Bass"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** An expressive 303 with a proper +30-semitone filter-env sweep, ladder overdrive grit, exponential velocity-to-cutoff and aftertouch-to-resonance, spat through a dotted digital delay.
- **Coverage:**
  - engine: PolyBLEP single saw
  - filter: Ladder LP + ladderDrive/slope + keyTrack
  - filterEnv corrected envAmount (+30 st)
  - velocity -> cutoff voice route (Exp curve)
  - aftertouch -> resonance voice route
  - mono voiceMode + legato + portamento
  - Digital delay
- **Rationale:** The definitive fix of the filter-env bug: envAmount = 30 semitones (audible strong sweep) instead of the template's nonsensical 4000. A single saw into a 24 dB ladder at res 0.8 with 4 dB drive is the 303 core; a fast filterEnv (1/260/0.05/140) with a +0.4 decayCurve gives the snappy pluck sweep. The distinctive mod gesture is an EXPONENTIAL velocity->cutoff route (curve set directly on the voiceRoute) plus aftertouch->resonance for live squelch - performance expression no sibling duplicates. A dotted-eighth digital delay with widened stereo gives the classic acid ping. keyTrack 0.3 keeps it usable up the neck. No issues flagged; unchanged.
- **Replacement code:**
```cpp
    // "Acid Bass" - Expressive 303: ladder drive, proper filter-env sweep, touch-controlled
    {
        PresetDef p;
        p.name = "Acid Bass";
        p.category = "Bass";
        auto& s = p.state;
        s.oscA.type = 0; s.oscA.waveform = 1; // Saw
        s.oscA.tuneSemitones = -12.0f; s.oscA.level = 0.9f;
        s.oscB.level = 0.0f;        // single-osc 303
        s.mixer.position = 0.0f;    // Osc A only
        s.filter.type = 4;          // Ladder LP
        s.filter.cutoffHz = 500.0f;
        s.filter.resonance = 0.8f;  // squelchy
        s.filter.envAmount = 30.0f; // PROPER semitone sweep (was the 4000 Hz-value bug in the acid template)
        s.filter.ladderSlope = 4;
        s.filter.ladderDrive = 4.0f; // ladder overdrive = the 303 grit
        s.filter.keyTrack = 0.3f;
        s.ampEnv.attackMs = 1.0f; s.ampEnv.decayMs = 200.0f;
        s.ampEnv.sustain = 0.4f; s.ampEnv.releaseMs = 90.0f;
        // Fast, deep filter env = the classic acid pluck-sweep
        s.filterEnv.attackMs = 1.0f; s.filterEnv.decayMs = 260.0f;
        s.filterEnv.sustain = 0.05f; s.filterEnv.releaseMs = 140.0f;
        s.filterEnv.decayCurve = 0.4f; // exp-ish decay for a snappier sweep
        s.global.voiceMode = 1; s.monoMode.legato = 1; s.monoMode.portamentoTimeMs = 35.0f;
        // Velocity scans cutoff with an exponential response (very 303)
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.6f);
        s.voiceRoutes[0].curve = static_cast<int8_t>(kCurveExp);
        // Aftertouch pushes resonance for live squelch control
        setVoiceRoute(s, 1, kVSrcAftertouch, kVDstFltRes, 0.35f);
        // Tight dotted digital delay for that classic acid ping
        s.delayEnabled = 1;
        s.delay.type = 0;           // Digital
        s.delay.sync = 1; s.delay.noteValue = kNote1_8D;
        s.delay.feedback = 0.35f; s.delay.mix = 0.22f;
        s.delay.digitalWidth = 140.0f;
        presets.push_back(std::move(p));
    }
```

## 4. "FM Bass" -> "FM Bass"
- **Locate:** the block containing `p.name = "FM Bass"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A metallic phase-distortion FM bass whose bright DoubleSine bite morphs into a square sub over each note, hardened by a parallel Lockhart wavefolder.
- **Coverage:**
  - engine: PhaseDistortion (DoubleSine) + PolyBLEP square
  - phaseMod + freqMod (owned on oscA)
  - filter: SVF LP
  - modEnv -> MorphPos voice route (Exp curve, evolving timbre)
  - distortion: Wavefolder (Lockhart)
  - mono voiceMode
- **Rationale:** CRITICAL FIX APPLIED: s.distortion.foldType changed from 0 to 2. Verified against distortion_params.h:210 - the Fold Type dropdown is {Triangle, Sine, Lockhart}, so 0=Triangle, 1=Sine, 2=Lockhart. foldType=2 now correctly selects the Lockhart model claimed by the character text and the 'distortion: Wavefolder (Lockhart)' coverage tag, so the chunk's sole wavefolder actually exercises the Lockhart fold path. All other values byte-identical. Design rationale unchanged: PhaseDistortion DoubleSine core with per-note evolution via modEnv->MorphPos (Exp curve) starting FM-heavy at morph 0.25 and settling toward the square sub; oscA.phaseMod=0.2 and freqMod=0.35 own both static FM fields for the metallic bite; a parallel Lockhart wavefolder (mix 0.45) hardens harmonics while preserving sub weight.
- **Replacement code:**
```cpp
    // "FM Bass" - Digital phase-distortion FM bass whose timbre evolves per note
    {
        PresetDef p;
        p.name = "FM Bass";
        p.category = "Bass";
        auto& s = p.state;
        s.oscA.type = 2;            // Phase Distortion
        s.oscA.pdWaveform = 3;      // DoubleSine (metallic FM bite)
        s.oscA.pdDistortion = 0.55f;
        s.oscA.tuneSemitones = -12.0f; s.oscA.level = 0.85f;
        s.oscA.phaseMod = 0.2f;     // static PM sidebands (owns phaseMod)
        s.oscA.freqMod = 0.35f;     // static FM index (owns freqMod on bass)
        s.oscB.type = 0; s.oscB.waveform = 2; // Square sub
        s.oscB.tuneSemitones = -12.0f; s.oscB.level = 0.5f;
        s.mixer.position = 0.25f;   // start FM-heavy (favor Osc A)...
        s.filter.type = 0;          // SVF LP
        s.filter.cutoffHz = 3800.0f; s.filter.resonance = 0.15f;
        s.ampEnv.attackMs = 1.0f; s.ampEnv.decayMs = 320.0f;
        s.ampEnv.sustain = 0.45f; s.ampEnv.releaseMs = 110.0f;
        s.global.voiceMode = 1;
        // Mod-env sweeps the A/B morph so the bright FM tone settles into the square sub
        // over the note - the "FM character" audibly evolves each key-press.
        s.modEnv.attackMs = 4.0f; s.modEnv.decayMs = 380.0f;
        s.modEnv.sustain = 0.25f; s.modEnv.releaseMs = 150.0f;
        setVoiceRoute(s, 0, kVSrcEnv3, kVDstMorphPos, 0.5f);
        s.voiceRoutes[0].curve = static_cast<int8_t>(kCurveExp);
        // Wavefolder adds extra digital harmonics for a harder metallic edge
        s.distortion.type = 4;      // Wavefolder
        s.distortion.foldType = 2;  // Lockhart
        s.distortion.drive = 0.35f;
        s.distortion.character = 0.5f;
        s.distortion.mix = 0.45f;   // parallel fold, keeps sub weight
        presets.push_back(std::move(p));
    }
```

## 5. "Wobble" -> "Wobble"
- **Locate:** the block containing `p.name = "Wobble"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** The dubstep modulation exemplar: a half-bar synced LFO sweep at 2x scale depth, a stepped morph LFO, chaos-jittered resonance and a note-tracked ring-mod growl.
- **Coverage:**
  - engine: PolyBLEP saw + square
  - filter: Ladder LP + ladderDrive/slope
  - synced LFO1 -> AllFltCut (scale x2 - owns scale axis)
  - synced LFO2 -> AllMorphPos (Stepped curve)
  - chaosMod source -> AllResonance
  - velocity -> cutoff voice route
  - distortion: RingModulator (NoteTrack)
  - mono voiceMode + legato
- **Rationale:** Builds on the existing rich three-slot design and pushes further: LFO1->cutoff now runs at a half-note (noteValue 16) for a slow half-bar wobble and, crucially, sets modMatrix.slots[0].scale = 3 (x2) so amount 0.5 delivers an effective ~1.0 deep sweep - owning the previously-dormant scale axis. LFO2->morph uses a Stepped curve for rhythmic timbral jumps rather than a smooth saw. chaosMod->resonance adds organic growl. A note-tracked RingModulator (mix 0.3, NoteTrack so it stays in tune) layers a metallic edge at the wobble peaks while preserving the low end - the chunk's ring-mod home. Velocity->cutoff scales intensity to playing dynamics. No issues flagged; unchanged.
- **Replacement code:**
```cpp
    // "Wobble" - Dubstep modulation exemplar: 3 synced/chaos mod slots + ring growl
    {
        PresetDef p;
        p.name = "Wobble";
        p.category = "Bass";
        auto& s = p.state;
        s.oscA.type = 0; s.oscA.waveform = 1; // Saw
        s.oscA.tuneSemitones = -12.0f; s.oscA.level = 0.8f;
        s.oscB.type = 0; s.oscB.waveform = 2; // Square
        s.oscB.tuneSemitones = -12.0f; s.oscB.level = 0.6f;
        s.mixer.position = 0.45f;
        s.filter.type = 4;          // Ladder LP
        s.filter.cutoffHz = 1600.0f; s.filter.resonance = 0.55f;
        s.filter.ladderSlope = 4; s.filter.ladderDrive = 4.0f;
        s.ampEnv.attackMs = 2.0f; s.ampEnv.decayMs = 200.0f;
        s.ampEnv.sustain = 0.85f; s.ampEnv.releaseMs = 150.0f;
        s.global.voiceMode = 1; s.monoMode.legato = 1;
        // Slot 0: tempo-synced sine LFO sweeps cutoff; scale x2 pushes a DEEP wobble
        s.lfo1.rateHz = 4.0f; s.lfo1.shape = 0; s.lfo1.depth = 1.0f; s.lfo1.sync = 1;
        s.lfo1Ext.noteValue = 16; // 1/2 note = slow half-bar wobble
        setModSlot(s, 0, kSrcLFO1, kDstAllFltCut, 0.5f);
        s.modMatrix.slots[0].scale = 3; // x2 => effective ~1.0 deep sweep (owns the scale axis)
        // Slot 1: synced saw LFO saws the morph at a different rate for grit complexity
        s.lfo2.rateHz = 2.0f; s.lfo2.shape = 2; s.lfo2.depth = 0.6f; s.lfo2.sync = 1;
        s.lfo2Ext.noteValue = kNote1_4;
        setModSlot(s, 1, kSrcLFO2, kDstAllMorphPos, 0.5f, kCurveStepped); // stepped = rhythmic morph jumps
        // Slot 2: chaos jitters resonance for organic, unpredictable growl
        s.chaosMod.rateHz = 1.5f; s.chaosMod.type = 0; s.chaosMod.depth = 0.3f;
        setModSlot(s, 2, kSrcChaos, kDstAllResonance, 0.2f);
        // Velocity sets the wobble intensity via cutoff
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.4f);
        // Note-tracked ring modulator adds a metallic growl at the wobble peaks
        s.distortion.type = 6;      // RingModulator
        s.distortion.ringFreqMode = 1; // NoteTrack (stays musical)
        s.distortion.ringWaveform = 0; // Sine
        s.distortion.mix = 0.3f;    // subtle - preserves the low end
        presets.push_back(std::move(p));
    }
```

## 6. "Chaos Sub" -> "Lorenz Maw"
- **Locate:** the block containing `p.name = "Chaos Sub"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** An unstable Lorenz-chaos sub that grinds and breathes over a pure sine anchor, ground down by tape saturation and stirred by an autonomous chaos-modulator into the cutoff.
- **Coverage:**
  - engine: Chaos (Lorenz)
  - filter: Ladder(slope/drive)
  - distortion: TapeSaturator
  - chaosMod source + synced LFO mod slots
  - mod matrix scale/curve axis (SCurve)
  - Tape delay
  - mono voiceMode
- **Rationale:** chaosAmount raised 0.3->0.55 and chaosCoupling 0.2 give the fundamental real instability instead of a tame dark saw. Ladder drive (6 dB) plus TapeSaturator (sat 0.6) provide the grind/warmth. The identity gesture is the previously-silent chaosMod source (depth raised 0->0.6) routed SCurve into all-voice cutoff, so the tone wanders autonomously; the tempo-synced LFO1->resonance (1/2 note) is a separate, locked layer. masterGain 0.9 compensates the drive+tape gain. Distinct from siblings via engine+filter-drive+two live mod sources, none reused elsewhere in this chunk.
- **Replacement code:**
```cpp
        // "Lorenz Maw" - Unstable Lorenz-chaos sub reinforced by a pure sine, ground
        // down by tape saturation and animated by the chaosMod source into cutoff.
        {
            PresetDef p;
            p.name = "Lorenz Maw";
            p.category = "Bass";
            auto& s = p.state;

            // OSC A: Chaos engine (Lorenz) as the fundamental — pushed hard enough to grind
            s.oscA.type = 5;              // Chaos
            s.oscA.chaosAttractor = 0;    // Lorenz
            s.oscA.chaosAmount = 0.55f;   // grind: enough to smear the fundamental (was 0.3, too tame)
            s.oscA.chaosCoupling = 0.2f;  // cross-axis instability => a living low end
            s.oscA.chaosOutput = 0;       // X axis
            s.oscA.tuneSemitones = -12.0f; s.oscA.level = 0.8f;
            // OSC B: pure sine one octave down — the anchor that keeps pitch legible
            s.oscB.type = 0; s.oscB.waveform = 0; // Sine
            s.oscB.tuneSemitones = -12.0f; s.oscB.level = 0.6f;
            s.mixer.position = 0.35f;     // mostly chaos, sine underpins

            // Ladder LP with input drive — the tube-y grind stage (Ladder slope/drive)
            s.filter.type = 4;            // Ladder
            s.filter.cutoffHz = 1800.0f; s.filter.resonance = 0.3f;
            s.filter.ladderSlope = 4;     // 24 dB/oct
            s.filter.ladderDrive = 6.0f;  // dB of input drive for extra grit

            // Tape saturation warms and compresses the chaotic smear
            s.distortion.type = 5;        // TapeSaturator
            s.distortion.drive = 0.5f; s.distortion.tapeSaturation = 0.6f; s.distortion.mix = 1.0f;

            s.ampEnv.attackMs = 3.0f; s.ampEnv.decayMs = 220.0f;
            s.ampEnv.sustain = 0.65f; s.ampEnv.releaseMs = 140.0f;

            // chaosMod source: an independent Lorenz LFO, raised from its silent default
            s.chaosMod.type = 0;          // Lorenz
            s.chaosMod.rateHz = 0.3f;     // slow drift
            s.chaosMod.depth = 0.6f;      // MUST raise — default 0 = silent source
            s.chaosMod.sync = 0;

            // Synced LFO1 for a tempo-locked resonance shimmer under the chaos
            s.lfo1.shape = 0;             // Sine
            s.lfo1.sync = 1;              // tempo-synced
            s.lfo1Ext.noteValue = 16;     // 1/2 note — slow pulse

            // Mod matrix: chaos -> cutoff (SCurve) is this preset's unique gesture;
            // synced LFO1 -> resonance adds tempo-locked movement.
            setModSlot(s, 0, kSrcChaos, kDstAllFltCut, 0.4f, kCurveSCurve);
            setModSlot(s, 1, kSrcLFO1, kDstAllResonance, 0.3f, kCurveLinear);

            // Subtle tape delay for depth without washing out the sub
            s.delayEnabled = 1;
            s.delay.type = 1;             // Tape
            s.delay.sync = 1; s.delay.noteValue = 10; // 1/8
            s.delay.feedback = 0.25f; s.delay.mix = 0.14f; s.delay.tapeSaturation = 0.5f;

            s.global.voiceMode = 1;       // Mono
            s.global.masterGain = 0.9f;   // headroom for tape + drive
            presets.push_back(std::move(p));
        }
```

## 7. "Ring Metal" -> "Anvil Ring"
- **Locate:** the block containing `p.name = "Ring Metal"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A clangy note-tracked ring-mod saw with a punchy low-mid peak, its inharmonic bell overtone swept by a free LFO across the peak filter and shimmered by a phaser.
- **Coverage:**
  - engine: PolyBLEP (single saw)
  - filter: SVF Peak (svfGain)
  - distortion: RingModulator (note-tracked ~1.5x)
  - LFO -> peak-filter sweep (Exp curve)
  - mod matrix scale axis (x2)
  - velocity -> distDrive voice route
  - Phaser FX
  - mono voiceMode
- **Rationale:** Ring ratio 0.079 normalized = 1.5x carrier, note-tracked, giving a genuinely inharmonic clang. The audit noted ringRatio can't be modulated (no matrix dest), so movement is honestly delivered by a free LFO2 sweeping the SVF Peak (svfGain +12 dB at 320 Hz) up across the ring's partials with an Exp curve and the scale axis at x2 for a wide swing — this is the sonic sleight-of-hand that makes the overtone appear to move. Velocity->distDrive makes the clang dynamic; the phaser adds slow metallic shimmer. Filter, engine-use, and mod story are all distinct from the other four.
- **Replacement code:**
```cpp
        // "Anvil Ring" - Note-tracked ring modulator on a single saw, focused by an
        // SVF Peak in the low-mids; a free LFO sweeps the peak across the ring's
        // inharmonics (ringRatio is not a mod destination, so we move the peak instead).
        {
            PresetDef p;
            p.name = "Anvil Ring";
            p.category = "Bass";
            auto& s = p.state;

            s.oscA.type = 0; s.oscA.waveform = 1; // Saw
            s.oscA.tuneSemitones = -12.0f; s.oscA.level = 0.82f;
            s.oscB.level = 0.0f;           // single osc
            s.mixer.position = 0.0f;       // OSC A only

            // SVF Peak: a low-mid bump gives the clang body/punch (covers svfGain)
            s.filter.type = 8;             // SVF Peak
            s.filter.cutoffHz = 320.0f;    // low-mid punch band (LFO sweeps it upward)
            s.filter.resonance = 1.5f;
            s.filter.svfGain = 12.0f;      // +12 dB peak boost
            s.filter.svfSlope = 1;

            // Ring modulator, carrier tracking the note ~1.5x for an inharmonic bell edge
            s.distortion.type = 6;         // RingModulator
            s.distortion.drive = 0.45f;
            s.distortion.ringFreqMode = 1; // NoteTrack
            s.distortion.ringRatio = 0.079f; // ~1.5x  (norm: (1.5-0.25)/15.75)
            s.distortion.ringWaveform = 0; // Sine carrier
            s.distortion.ringStereoSpread = 0.3f;
            s.distortion.mix = 0.5f;

            s.ampEnv.attackMs = 1.0f; s.ampEnv.decayMs = 220.0f;
            s.ampEnv.sustain = 0.6f; s.ampEnv.releaseMs = 110.0f;

            // Free-running LFO2 sweeps the peak filter across the ring's inharmonics so
            // the "bell overtone moves" — unique gesture (Exp curve, x2 scale axis).
            s.lfo2.shape = 0;              // Sine
            s.lfo2.sync = 0;               // free-run
            s.lfo2.rateHz = 0.8f;
            setModSlot(s, 0, kSrcLFO2, kDstAllFltCut, 0.5f, kCurveExp);
            s.modMatrix.slots[0].scale = 3; // x2 — exercise the untouched scale axis for a wide sweep

            // Velocity drives ring intensity so harder hits clang harder
            setVoiceRoute(s, 0, kVSrcVelocity, kVDstDistDrive, 0.4f);

            // Phaser adds an evolving metallic shimmer on top
            s.modulationType = 1;          // Phaser
            s.phaser.rateHz = 0.4f; s.phaser.depth = 0.5f;
            s.phaser.feedback = 0.4f; s.phaser.mix = 0.3f;
            s.phaser.stages = 1;           // dropdown idx 1 => 4 stages (moderate)
            s.phaser.centerFreqHz = 800.0f;

            s.global.voiceMode = 1;        // Mono
            s.global.masterGain = 0.9f;
            presets.push_back(std::move(p));
        }
```

## 8. "Grain Crunch" -> "Gravel Pit"
- **Locate:** the block containing `p.name = "Grain Crunch"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A saw+triangle sub with FM growl, crumbled by lively granular distortion, opened per-note by an EnvFilter auto-wah, and flipped between waves by a stepped LFO morph.
- **Coverage:**
  - engine: PolyBLEP dual (saw+tri) with phaseMod + freqMod
  - filter: EnvFilter (envSensitivity/depth/direction)
  - distortion: GranularDistortion (variation + jitter)
  - LFO -> morph (Stepped curve)
  - Digital delay
  - mono voiceMode
- **Rationale:** grainVariation raised 0.2->0.5 and grainJitter set 0.35 give the crumble inherent, non-static motion (the directive's LFO->jitter can't be a matrix dest, so I bake the movement into the grain engine and instead spend the LFO on a Stepped morph between saw and triangle — audibly rhythmic). oscA phaseMod/freqMod add FM growl. The EnvFilter (type 11) is a self-animating auto-wah covering envSensitivity/depth/direction — no other preset here uses it. Digital delay (vs Chaos Sub's tape) diversifies the wrapper. Distinct engine-treatment, filter, distortion, and mod gesture.
- **Replacement code:**
```cpp
        // "Gravel Pit" - Saw+triangle sub with FM growl, crumbled by granular
        // distortion (internal jitter keeps it alive), an auto-wah EnvFilter, a
        // digital delay, and a stepped LFO morph between the two waves.
        {
            PresetDef p;
            p.name = "Gravel Pit";
            p.category = "Bass";
            auto& s = p.state;

            s.oscA.type = 0; s.oscA.waveform = 1; // Saw
            s.oscA.tuneSemitones = -12.0f; s.oscA.level = 0.85f;
            s.oscA.phaseMod = 0.3f;        // FM-ish sidebands => growl
            s.oscA.freqMod = 0.2f;         // slight freq-mod grit
            s.oscB.type = 0; s.oscB.waveform = 2; // Triangle
            s.oscB.tuneSemitones = -12.0f; s.oscB.level = 0.45f;
            s.mixer.position = 0.35f;

            // EnvFilter auto-wah: input envelope opens the filter per note
            s.filter.type = 11;            // Env Filter
            s.filter.cutoffHz = 350.0f;    // base
            s.filter.resonance = 0.5f;
            s.filter.envSubType = 0;       // LP response
            s.filter.envSensitivity = 6.0f;
            s.filter.envDepth = 0.85f;
            s.filter.envAttack = 12.0f;
            s.filter.envRelease = 160.0f;
            s.filter.envDirection = 0;     // sweep up

            // Granular distortion with real variation + jitter so the crumble evolves
            s.distortion.type = 3;         // GranularDistortion
            s.distortion.drive = 0.5f;
            s.distortion.grainSize = 0.25f;
            s.distortion.grainDensity = 0.55f;
            s.distortion.grainVariation = 0.5f; // up from 0.2 — audible texture spread
            s.distortion.grainJitter = 0.35f;   // non-static crumble (no jitter mod dest, so bake it in)
            s.distortion.mix = 0.85f;

            s.ampEnv.attackMs = 1.0f; s.ampEnv.decayMs = 190.0f;
            s.ampEnv.sustain = 0.55f; s.ampEnv.releaseMs = 110.0f;

            // Stepped LFO morph flips between saw and triangle for a rhythmic timbre
            s.lfo1.shape = 0; s.lfo1.sync = 1; s.lfo1Ext.noteValue = 10; // 1/8
            setModSlot(s, 0, kSrcLFO1, kDstAllMorphPos, 0.3f, kCurveStepped);

            // Digital delay for grit-space
            s.delayEnabled = 1;
            s.delay.type = 0;              // Digital
            s.delay.sync = 1; s.delay.noteValue = 11; // 1/8 dotted groove
            s.delay.feedback = 0.2f; s.delay.mix = 0.16f;

            s.global.voiceMode = 1;
            s.global.masterGain = 0.9f;
            presets.push_back(std::move(p));
        }
```

## 9. "Sync Punch" -> "Sync Fist"
- **Locate:** the block containing `p.name = "Sync Punch"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** An aggressive hard-sync stab with a real downward filter sweep, Lockhart wavefolder bite, consistent-phase transient punch, and velocity/aftertouch performance control.
- **Coverage:**
  - engine: Hard Sync
  - filter: SVF LP (svfDrive)
  - filterEnv corrected envAmount (+28 st)
  - distortion: Wavefolder (Lockhart)
  - osc start phase
  - velocity -> cutoff + aftertouch -> res voice routes
  - mono voiceMode + legato + portamento
- **Rationale:** The core fix: filter.envAmount is PLAIN semitones (range -48..+48); the old 30 read like a stray Hz value giving almost no sweep, so I set +28 st for an audible downward filter movement per note, backed by a fast filterEnv (D170/S0.1) with a +0.4 decayCurve snap. Lockhart wavefolder (foldType 2) at drive 0.3/mix 0.7 adds controlled harmonic bite without runaway level. oscA.phase 0.15 gives a deterministic, punchy transient (covers osc start phase). Velocity->cutoff and aftertouch->resonance make it play expressively, and legato + 40 ms legato-only portamento give it stab-to-stab glide. Every knob traces to punch/expression; distinct from the other four in engine, filter, distortion, and routing.
- **Replacement code:**
```cpp
        // "Sync Fist" - Hard-sync saw with a REAL downward filter sweep (envAmount
        // fixed to +28 semitones, not the bogus 30 "Hz"), Lockhart wavefolder bite,
        // consistent start phase for punch, and velocity/aftertouch performance routes.
        {
            PresetDef p;
            p.name = "Sync Fist";
            p.category = "Bass";
            auto& s = p.state;

            s.oscA.type = 3;               // Sync
            s.oscA.syncRatio = 2.5f;
            s.oscA.syncWaveform = 1;       // Saw slave
            s.oscA.syncMode = 0;           // Hard
            s.oscA.tuneSemitones = -12.0f; s.oscA.level = 0.9f;
            s.oscA.phase = 0.15f;          // fixed start phase => consistent punchy transient
            s.oscB.level = 0.0f;
            s.mixer.position = 0.0f;

            // SVF LP (24 dB) with a touch of drive; the filter env sweeps it down hard
            s.filter.type = 0;             // SVF LP
            s.filter.cutoffHz = 500.0f;
            s.filter.resonance = 0.5f;
            s.filter.svfSlope = 1;         // 24 dB
            s.filter.svfDrive = 4.0f;
            s.filter.envAmount = 28.0f;    // FIX: +28 semitones of sweep (was 30 => near-zero)

            // Lockhart wavefolder adds harmonic bite on the attack
            s.distortion.type = 4;         // Wavefolder
            s.distortion.foldType = 2;     // Lockhart
            s.distortion.drive = 0.3f;     // moderate — folding gets loud fast
            s.distortion.mix = 0.7f;

            s.ampEnv.attackMs = 1.0f; s.ampEnv.decayMs = 200.0f;
            s.ampEnv.sustain = 0.5f; s.ampEnv.releaseMs = 100.0f;
            // Fast filter env => snappy downward sweep each note
            s.filterEnv.attackMs = 1.0f; s.filterEnv.decayMs = 170.0f;
            s.filterEnv.sustain = 0.1f; s.filterEnv.releaseMs = 90.0f;
            s.filterEnv.decayCurve = 0.4f; // exp-ish snap

            // Performance routes: velocity opens cutoff, aftertouch pushes resonance
            setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.5f);
            setVoiceRoute(s, 1, kVSrcAftertouch, kVDstFltRes, 0.4f);

            s.global.voiceMode = 1;        // Mono
            s.monoMode.legato = 1;
            s.monoMode.portamentoTimeMs = 40.0f; // subtle glide for stabs
            s.monoMode.portaMode = 1;      // Legato-only glide
            s.global.masterGain = 0.9f;
            presets.push_back(std::move(p));
        }
```

## 10. "Harmonic Sub" -> "Fifth Cellar"
- **Locate:** the block containing `p.name = "Harmonic Sub"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A saw+triangle sub thickened by a PitchSync harmonizer voice a fifth up that adds genuine new content, voiced through a key-tracked comb with slow pitch-drift and legato glide.
- **Coverage:**
  - engine: PolyBLEP dual (saw+tri)
  - filter: Comb (damping/keyTrack)
  - harmonizer PitchSync sub (a fifth up)
  - Env3 -> OSC B pitch voice route
  - LFO -> comb cutoff (free)
  - mono voiceMode + legato + portamento
- **Rationale:** The audit flagged that the old octave-down harmonizer just reinforced the existing -12 sub. Fix: harmonizer voiceInterval[0] = +7 (a perfect fifth UP) in PitchSync mode (pitchShiftMode 3) at -8 dB wet, so it adds a real new harmonic layer rather than louder sub. The Comb filter (keyTrack 1.0, damping 0.35) gives a hollow resonant body that follows pitch — a filter used by no sibling here. Movement comes from a slow Env3->OSC B pitch drift (subtle beating against OSC A) plus a slow free LFO2 detuning the comb resonance, and legato + 60 ms always-on portamento for glide. Levels are conservative (osc 0.8/0.45, wet -8 dB) so it stays clean and audible.
- **Replacement code:**
```cpp
        // "Fifth Cellar" - Saw+triangle sub thickened by a PitchSync harmonizer voice a
        // FIFTH up (adds new content instead of doubling the existing -12 sub), voiced
        // through a key-tracked comb, with an Env3 pitch drift and legato glide.
        {
            PresetDef p;
            p.name = "Fifth Cellar";
            p.category = "Bass";
            auto& s = p.state;

            s.oscA.type = 0; s.oscA.waveform = 1; // Saw
            s.oscA.tuneSemitones = -12.0f; s.oscA.level = 0.8f;
            s.oscB.type = 0; s.oscB.waveform = 2; // Triangle
            s.oscB.tuneSemitones = -12.0f; s.oscB.level = 0.45f;
            s.mixer.position = 0.35f;

            // Key-tracked comb filter => a hollow, resonant body that follows pitch
            s.filter.type = 6;             // Comb
            s.filter.cutoffHz = 1600.0f;
            s.filter.resonance = 0.35f;
            s.filter.combDamping = 0.35f;  // tame the HF in the comb feedback
            s.filter.keyTrack = 1.0f;      // comb resonance tracks the note

            s.ampEnv.attackMs = 2.0f; s.ampEnv.decayMs = 220.0f;
            s.ampEnv.sustain = 0.75f; s.ampEnv.releaseMs = 130.0f;

            // Harmonizer: PitchSync, a FIFTH up so it adds harmonic content, not more sub
            s.harmonizerEnabled = 1;
            s.harmonizer.harmonyMode = 0;    // Chromatic
            s.harmonizer.pitchShiftMode = 3; // PitchSync (glitch-free tracked shift)
            s.harmonizer.numVoices = 1;
            s.harmonizer.dryLevelDb = 0.0f;
            s.harmonizer.wetLevelDb = -8.0f;
            s.harmonizer.voiceInterval[0] = 7;   // +7 st = perfect fifth
            s.harmonizer.voiceLevelDb[0] = 0.0f;

            // Env3 (ModEnv) nudges OSC B pitch for slow beating/thickening — unique gesture
            s.modEnv.attackMs = 400.0f; s.modEnv.decayMs = 800.0f;
            s.modEnv.sustain = 0.6f; s.modEnv.releaseMs = 600.0f;
            setVoiceRoute(s, 0, kVSrcEnv3, kVDstOscBPitch, 0.15f);

            // Slow free LFO detunes the comb resonance slightly for a living body
            s.lfo2.shape = 0; s.lfo2.sync = 0; s.lfo2.rateHz = 0.2f;
            setModSlot(s, 0, kSrcLFO2, kDstAllFltCut, 0.15f, kCurveLinear);

            s.global.voiceMode = 1;        // Mono
            s.monoMode.legato = 1;
            s.monoMode.portamentoTimeMs = 60.0f;
            s.monoMode.portaMode = 0;      // always glide
            presets.push_back(std::move(p));
        }
```

## 11. "Vocal Bass" -> "Vocal Bass"
- **Locate:** the block containing `p.name = "Vocal Bass"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A talking, vowel-formant mono bass with a deep sine sub, formant-FILTER growl under aftertouch, legato glide, and a PitchSync octave-doubler beefing the low end.
- **Coverage:**
  - engine-formant
  - formant-filter
  - filterEnv-corrected-envAmount
  - mono-voiceMode+legato+portamento
  - harmonizer-PitchSync-sub
  - aftertouch->res-voice-route
  - synced-LFO1-mod-slot
  - osc-start-phase
- **Rationale:** Formant osc (type 7, vowel O) feeds a Formant FILTER (type 5) so two vowel stages stack into a genuine 'talking' bass; formantGender -0.3 pulls formants down for chest-voice weight. filter.envAmount is corrected to a PLAIN +18 semitones (was the family's near-zero/garbage values) for an audible attack shimmer. PitchSync harmonizer at -12 st (chromatic mode = semitones) doubles the octave for sub reinforcement. Two distinct mod gestures: a half-note SCurve LFO on morph position (vowel/sub pump) and aftertouch->resonance for a playable growl. Mono+legato+40ms portamento gives vocal phrasing. Start-phase 0.1 for a repeatable soft transient.
- **Replacement code:**
```cpp
    // "Vocal Bass" - Formant-osc + formant-FILTER vowel bass, PitchSync sub, aftertouch growl
    {
        PresetDef p;
        p.name = "Vocal Bass";
        p.category = "Bass";
        auto& s = p.state;
        // OSC A: Formant engine on vowel 'O' — the nasal vowel body of the bass
        s.oscA.type = 7;              // Formant
        s.oscA.formantVowel = 3;      // O
        s.oscA.formantMorph = 0.6f;   // sit just past O toward U for a rounder low vowel
        s.oscA.tuneSemitones = -12.0f;
        s.oscA.level = 0.9f;
        s.oscA.phase = 0.1f;          // slight start-phase offset = consistent soft attack transient
        // OSC B: pure sine two octaves down for sub weight
        s.oscB.type = 0; s.oscB.waveform = 0; // Sine
        s.oscB.tuneSemitones = -24.0f; s.oscB.level = 0.35f;
        s.mixer.position = 0.35f;     // favour the vowel, keep the sub present underneath
        // FORMANT FILTER (type 5) — a SECOND vowel stage; morph+gender for a masculine 'aw'
        s.filter.type = 5;
        s.filter.cutoffHz = 800.0f;
        s.filter.resonance = 0.4f;
        s.filter.formantMorph = 1.2f;   // filter vowel toward E — combs the formant osc for a talking timbre
        s.filter.formantGender = -0.3f; // shift formants down = chest-voice / male bass
        // Corrected filter-env: PLAIN semitones (+18 = audible vowel-brightening on attack)
        s.filter.envAmount = 18.0f;
        s.filterEnv.attackMs = 8.0f; s.filterEnv.decayMs = 220.0f;
        s.filterEnv.sustain = 0.5f; s.filterEnv.releaseMs = 180.0f;
        // Amp env — legato bass, moderate body
        s.ampEnv.attackMs = 5.0f; s.ampEnv.decayMs = 250.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 200.0f;
        // Mono + legato glide = expressive vocal phrasing
        s.global.voiceMode = 1;
        s.monoMode.legato = 1; s.monoMode.portamentoTimeMs = 40.0f;
        // Harmonizer PitchSync sub-doubler: one voice an octave down, formant-locked (chromatic)
        s.harmonizerEnabled = 1;
        s.harmonizer.harmonyMode = 0;    // Chromatic -> interval is in semitones
        s.harmonizer.pitchShiftMode = 3; // PitchSync
        s.harmonizer.numVoices = 1;
        s.harmonizer.voiceInterval[0] = -12; // octave-down doubling for reinforced sub
        s.harmonizer.voiceLevelDb[0] = -2.0f;
        s.harmonizer.dryLevelDb = 0.0f; s.harmonizer.wetLevelDb = -3.0f;
        // Mod identity #1: synced slow LFO1 sways the A/B morph = rhythmic vowel<->sub pump
        s.lfo1Ext.noteValue = 16;        // 1/2 note = lazy half-bar sway
        setModSlot(s, 0, kSrcLFO1, kDstAllMorphPos, 0.3f, kCurveSCurve);
        // Mod identity #2: aftertouch opens filter resonance = press for vocal growl
        setVoiceRoute(s, 0, kVSrcAftertouch, kVDstFltRes, 0.4f);
        presets.push_back(std::move(p));
    }
```

## 12. "Comb Pluck" -> "Gut String"
- **Locate:** the block containing `p.name = "Comb Pluck"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A deep upright-bass gut string: a pink-noise pluck excites a low, key-tracked comb resonator with a triangle sub an octave down, a fast filter-env brightness decay, and a warm vintage-tape slap — static and woody, with no LFO or random motion.
- **Coverage:**
  - engine-noise (pink exciter)
  - comb-filter + keyTrack + combDamping (Karplus resonator)
  - filterEnv-corrected-envAmount (bright-then-dull pluck)
  - osc-start-phase (repeatable clicky attack)
  - mono-voiceMode
  - Tape delay w/ tapeWear + tapeAge (static tape-resonator color)
  - velocity->cutoff voice route (dynamic pluck brightness)
  - deep -24 st bass register
- **Rationale:** Verified every field/setter against ruinae_preset_format.h: OscState.type/noiseColor/level/waveform/tuneSemitones/phase, MixerState.position, FilterState.type/cutoffHz/resonance/combDamping/keyTrack/envAmount, EnvelopeState fields, GlobalState.voiceMode, delayEnabled + DelayState.type/sync/timeMs/feedback/mix/tapeSaturation/tapeWear/tapeAge, and setVoiceRoute(s,slot,kVSrcVelocity,kVDstFltCut,amt). Directive execution: (1) Static/tape resonator vs Sinew's motion - the comb is completely un-modulated (no LFO/random/chaos source anywhere; the only route is velocity->cutoff, which is a dynamic response, not motion), and combDamping raised to 0.45 makes the string read as woody/damped/dead rather than a live ringing string. The tape delay is given real tape-resonator color (tapeWear 0.3 + tapeAge 0.4 on top of tapeSaturation 0.6), so its character is fixed/aged tape, the opposite of Sinew's moving-string treatment. (2) Register/FX shift so they don't read as one string: dropped a full octave - oscB sub to -24 st and comb base cutoff to 220 Hz (from the original 600 and the earlier -12 draft), planting Gut String firmly in deep upright-bass territory below Sinew's mid string. The pink exciter, corrected +28 st filter-env pluck decay (bright-then-dull), oscB.phase 0.25 clicky transient, mono voice, and tape FX round out a distinct Karplus bass identity.
- **Replacement code:**
```cpp
    // "Gut String" - deep upright-bass Karplus pluck: pink-noise burst -> LOW key-tracked comb,
    // triangle sub an octave under, STATIC tape-colored resonator + vintage tape slap.
    // Deliberately motion-free (no LFO/random) to contrast Sinew's modulated string.
    {
        PresetDef p;
        p.name = "Gut String";
        p.category = "Bass";
        auto& s = p.state;
        // OSC A: pink noise = the excitation 'pluck' feeding the comb (warmer/darker than white)
        s.oscA.type = 9;             // Noise
        s.oscA.noiseColor = 1;       // Pink - softer, gut-string excitation
        s.oscA.level = 0.55f;
        // OSC B: triangle sub a full OCTAVE below the comb pitch -> deep upright-bass body
        s.oscB.type = 0; s.oscB.waveform = 4; // Triangle
        s.oscB.tuneSemitones = -24.0f; s.oscB.level = 0.5f;
        s.oscB.phase = 0.25f;        // defined start phase = consistent, clicky pluck attack
        s.mixer.position = 0.4f;     // excitation slightly favoured so the comb 'speaks'
        // COMB FILTER (type 6): LOW base freq + keyTrack=1 -> a deep tuned gut string,
        // a register below Sinew. Higher combDamping = a woody, damped, STATIC resonator
        // (no ring-out motion) vs Sinew's live, moving string.
        s.filter.type = 6;
        s.filter.cutoffHz = 220.0f;  // low comb reference (keytrack scales it per-note)
        s.filter.resonance = 0.72f;  // rings, but tamed by damping below
        s.filter.combDamping = 0.45f;// strong HF loss in feedback = warm, dead, static string
        s.filter.keyTrack = 1.0f;    // comb follows pitch - essential for a tuned pluck
        // Corrected filter-env: PLAIN +28 st fast decay = bright pluck attack that dulls quickly
        s.filter.envAmount = 28.0f;
        s.filterEnv.attackMs = 1.0f; s.filterEnv.decayMs = 120.0f;
        s.filterEnv.sustain = 0.0f; s.filterEnv.releaseMs = 100.0f;
        // Amp env: instant attack, long decay to low sustain = a decaying pluck, not a pad
        s.ampEnv.attackMs = 1.0f; s.ampEnv.decayMs = 450.0f;
        s.ampEnv.sustain = 0.15f; s.ampEnv.releaseMs = 250.0f;
        s.global.voiceMode = 1;
        // Vintage TAPE slap-back: short, low feedback, and genuine tape-resonator color via
        // saturation + wear + age (static tape character, not the moving verb Sinew would use)
        s.delayEnabled = 1;
        s.delay.type = 1;            // Tape
        s.delay.sync = 0;            // free-run slap
        s.delay.timeMs = 180.0f;
        s.delay.feedback = 0.25f;
        s.delay.mix = 0.18f;
        s.delay.tapeSaturation = 0.6f; // warm the repeats
        s.delay.tapeWear = 0.3f;       // worn-tape HF loss = tape-resonator color
        s.delay.tapeAge = 0.4f;        // aged tape character on the slap
        // Mod identity: harder playing opens the comb = dynamic pluck brightness (velocity, NOT LFO)
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.4f);
        presets.push_back(std::move(p));
    }
```

## 13. "Folded Triangle" -> "Foldback"
- **Locate:** the block containing `p.name = "Folded Triangle"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A Lockhart-wavefolded triangle bass with a deep sub, punched through an SVF Peak band-boost, chaos-driven filter wobble, and velocity-scaled fold intensity.
- **Coverage:**
  - engine-polyblep-triangle
  - wavefolder-Lockhart
  - SVF-Peak-svfGain-low-mid-punch
  - filterEnv-corrected-envAmount
  - chaosMod-source+wobble
  - mono-voiceMode
  - velocity->distDrive-voice-route
- **Rationale:** Directive asks for a Lockhart fold with velocity->fold-depth so it isn't static; foldType 2 = Lockhart, drive 0.65 (~6.5x fold) makes the fold the hook, and setVoiceRoute velocity->DistDrive scales fold intensity with playing force. Filter is the category's SVF Peak (type 8) with svfGain +8 dB at 250 Hz for chesty low-mid punch (owning that must-cover). chaosMod is woken up (depth 0.5, was 0 by default) and routed to filter cutoff so the boosted peak wobbles — an organic growl and the chaosMod-source coverage. filter.envAmount corrected to +20 st sweeps the peak on attack. Sub sine at -24 keeps the low end stable against heavy folding; mix 0.9 preserves a dry sliver.
- **Replacement code:**
```cpp
    // "Foldback" - Lockhart-folded triangle, SVF Peak punch, chaos wobble, velocity->fold
    {
        PresetDef p;
        p.name = "Foldback";
        p.category = "Bass";
        auto& s = p.state;
        // OSC A: triangle — clean fundamental that folds into rich buzz
        s.oscA.type = 0; s.oscA.waveform = 4; // Triangle
        s.oscA.tuneSemitones = -12.0f; s.oscA.level = 0.85f;
        // OSC B: sub sine two octaves down for weight the folder can't erase
        s.oscB.type = 0; s.oscB.waveform = 0; // Sine
        s.oscB.tuneSemitones = -24.0f; s.oscB.level = 0.3f;
        s.mixer.position = 0.3f;
        // WAVEFOLDER (Lockhart) at real drive so the fold is the identity, not a subtle tint
        s.distortion.type = 4;       // Wavefolder
        s.distortion.foldType = 2;   // Lockhart (0=Triangle,1=Sine,2=Lockhart)
        s.distortion.drive = 0.65f;  // ~6.5x fold — buzzy but the sub keeps it controlled
        s.distortion.mix = 0.9f;     // leave a sliver of dry sub for stability
        // SVF PEAK filter (type 8): a low-mid band BOOST = the bass 'punch' knob for this category
        s.filter.type = 8;
        s.filter.cutoffHz = 250.0f;  // low-mid punch centre
        s.filter.resonance = 1.5f;
        s.filter.svfGain = 8.0f;     // +8 dB peak boost — chesty punch under the fold
        s.filter.svfDrive = 3.0f;    // gentle post saturation glues the boosted band
        // Corrected filter-env: PLAIN +20 st = the peak band sweeps up on attack
        s.filter.envAmount = 20.0f;
        s.filterEnv.attackMs = 1.0f; s.filterEnv.decayMs = 180.0f;
        s.filterEnv.sustain = 0.2f; s.filterEnv.releaseMs = 150.0f;
        s.ampEnv.attackMs = 1.0f; s.ampEnv.decayMs = 220.0f;
        s.ampEnv.sustain = 0.65f; s.ampEnv.releaseMs = 120.0f;
        s.global.voiceMode = 1;
        // Mod identity #1: Chaos LFO wobbles the peak centre = unstable analog 'growl'
        s.chaosMod.depth = 0.5f;     // raise it — chaosMod is silent by default
        s.chaosMod.rateHz = 3.0f;    // slow-ish organic wobble
        setModSlot(s, 0, kSrcChaos, kDstAllFltCut, 0.35f, kCurveExp, 15.0f);
        // Mod identity #2: velocity drives fold depth = play harder, fold harder
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstDistDrive, 0.5f);
        presets.push_back(std::move(p));
    }
```

## 14. "Spectral Crunch" -> "Bitrot"
- **Locate:** the block containing `p.name = "Spectral Crunch"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** An FM-gritted saw/square bass mangled by spectral magnitude-bitcrush (~6-bit FFT quantization), carved by an SVF Notch, with a stepped LFO glitching the spectral tilt and a lo-fi digital delay.
- **Coverage:**
  - engine-polyblep+phaseMod+freqMod
  - spectral-distortion+SpectralBitcrush-magnitude-quantization
  - SVF-Notch-filter
  - filterEnv-corrected-envAmount
  - stepped-LFO1-mod-slot
  - Digital-delay
  - velocity->cutoff-voice-route
  - mono-voiceMode
- **Rationale:** CRITICAL FIX: the original set spectralMode=1 (MagnitudeOnly), where spectralBits is a no-op — applyMagnitudeOnly() only runs the saturation curve/drive and never touches the quantization path, so no bitcrush occurred. Verified in spectral_distortion.h: only applySpectralBitcrush() (mode 3, SpectralDistortionMode::SpectralBitcrush) reads magnitudeBits_ and does std::round(mag*levels)*invLevels. spectralBits maps via ruinae_voice.h L986 as bits=1+clamp(x)*15, so 0.35 -> ~6.25 bits = an audible per-frame magnitude bitcrush that makes the 'Bitrot' name real. Because mode 3 does NOT run the saturation curve, the Diode spectralCurve would be inert, so it is removed entirely and dropped from the coverage tag rather than falsely claimed. Everything else is unchanged: phaseMod (0.35) on the saw + freqMod (0.25) on the square pre-grit the source (phaseMod+freqMod coverage); SVF Notch (type 3) distinguishes its curve from LP siblings with filter.envAmount corrected to +22 st sweeping the notch; the mod story stays a 1/16 STEPPED-curve LFO on spectral tilt plus velocity->cutoff; FX signature remains a deliberately aged Digital delay (digitalAge 0.4).
- **Replacement code:**
```cpp
    // "Bitrot" - FM-grit saw/square -> spectral bitcrush (magnitude quantize), SVF Notch, stepped-LFO glitch
    {
        PresetDef p;
        p.name = "Bitrot";
        p.category = "Bass";
        auto& s = p.state;
        // OSC A: saw with phase-mod = extra inharmonic sidebands feeding the crusher
        s.oscA.type = 0; s.oscA.waveform = 1; // Saw
        s.oscA.tuneSemitones = -12.0f; s.oscA.level = 0.8f;
        s.oscA.phaseMod = 0.35f;     // FM-ish grit before the spectral stage
        // OSC B: square with freq-mod = growl/detune motion in the low mids
        s.oscB.type = 0; s.oscB.waveform = 2; // Square/Pulse
        s.oscB.tuneSemitones = -12.0f; s.oscB.level = 0.35f;
        s.oscB.freqMod = 0.25f;
        s.mixer.position = 0.3f;
        // SVF NOTCH (type 3): scoops a mid band so it doesn't sit on its siblings' LP curve
        s.filter.type = 3;
        s.filter.cutoffHz = 700.0f;  // notch centre in the low-mids
        s.filter.resonance = 0.8f;
        // Corrected filter-env: PLAIN +22 st sweeps the notch on attack = moving hollow
        s.filter.envAmount = 22.0f;
        s.filterEnv.attackMs = 2.0f; s.filterEnv.decayMs = 200.0f;
        s.filterEnv.sustain = 0.2f; s.filterEnv.releaseMs = 150.0f;
        // SPECTRAL distortion: SpectralBitcrush mode — per-frame magnitude quantization = metallic FFT crunch
        s.distortion.type = 2;       // Spectral
        s.distortion.drive = 0.5f;
        s.distortion.spectralMode = 3;  // SpectralBitcrush — spectralBits now actually quantizes magnitudes
        s.distortion.spectralBits = 0.35f; // ~6.25 bits (0-1 -> [1,16], bits=1+x*15) = audible bitcrush
        s.distortion.mix = 0.8f;
        s.ampEnv.attackMs = 1.0f; s.ampEnv.decayMs = 190.0f;
        s.ampEnv.sustain = 0.55f; s.ampEnv.releaseMs = 110.0f;
        s.global.voiceMode = 1;
        // Lo-fi DIGITAL delay: aged, wide = the crunch smears into a broken-machine echo
        s.delayEnabled = 1;
        s.delay.type = 0;            // Digital
        s.delay.sync = 0;
        s.delay.timeMs = 250.0f;
        s.delay.feedback = 0.35f;
        s.delay.mix = 0.2f;
        s.delay.digitalAge = 0.4f;  // degrade the repeats
        s.delay.digitalWidth = 120.0f;
        // Mod identity #1: 1/16 STEPPED LFO jolts the spectral tilt = glitchy, quantized crunch shifts
        s.lfo1Ext.noteValue = 7;    // 1/16
        setModSlot(s, 0, kSrcLFO1, kDstAllSpecTilt, 0.4f, kCurveStepped);
        // Mod identity #2: velocity opens the notch region = dynamics move the hollow
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.4f);
        presets.push_back(std::move(p));
    }
```

## 15. "Particle Sub" -> "Swarm Sub"
- **Locate:** the block containing `p.name = "Particle Sub"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A bursting granular particle cloud over a deep sine sub, squelched by an EnvFilter auto-wah, swirled by a phaser, with a synced LFO breathing the grain/sub blend.
- **Coverage:**
  - engine-particle-Burst-spawnMode+envType
  - EnvFilter-auto-wah+envSensitivity+depth+direction
  - mono-voiceMode
  - Phaser-FX
  - synced-LFO2-mod-slot
  - dual-osc-sub
- **Rationale:** Directive: particle engine owning spawnMode + envType. Set Burst (2) spawn with a Blackman (3) grain window for rhythmic, smooth clusters, plus drift 0.15 for a living cloud, over a -24 sine sub for weight. The filter is the category's EnvFilter auto-wah (type 11) with envSensitivity/depth/direction wired so the cloud squelches on its own dynamics — filter.envAmount is deliberately 0 so the EnvFilter alone owns the motion. FX is a Phaser (modulationType 1), distinct from the delays on its siblings. Mod identity is a 1/4-note SCurve LFO2 on morph position, breathing the grain/sub blend — no sibling repeats that source/dest/curve combination.
- **Replacement code:**
```cpp
    // "Swarm Sub" - burst-particle cloud + sine sub, EnvFilter auto-wah, phaser swirl
    {
        PresetDef p;
        p.name = "Swarm Sub";
        p.category = "Bass";
        auto& s = p.state;
        // OSC A: Particle engine in BURST spawn mode = rhythmic grain clusters, not a smear
        s.oscA.type = 6;                 // Particle
        s.oscA.particleSpawnMode = 2;    // Burst (own this: 0=Regular,1=Random,2=Burst)
        s.oscA.particleEnvType = 3;      // Blackman grain window = smooth, low-click clusters
        s.oscA.particleScatter = 1.5f;   // gentle detune spread — stays bass-like
        s.oscA.particleDensity = 40.0f;  // dense but not a wall
        s.oscA.particleLifetime = 60.0f; // short grains = textured fizz on the low end
        s.oscA.particleDrift = 0.15f;    // slow pitch wander = living cloud
        s.oscA.tuneSemitones = -12.0f; s.oscA.level = 0.8f;
        // OSC B: sine sub two octaves down anchors the cloud
        s.oscB.type = 0; s.oscB.waveform = 0; // Sine
        s.oscB.tuneSemitones = -24.0f; s.oscB.level = 0.45f;
        s.mixer.position = 0.3f;
        // ENV FILTER (type 11) auto-wah: the cloud squelches under its own envelope
        s.filter.type = 11;
        s.filter.cutoffHz = 350.0f;      // base — the auto-wah sweeps up from here
        s.filter.resonance = 1.0f;
        s.filter.envSubType = 0;         // LP response
        s.filter.envSensitivity = 6.0f;  // +6 dB input drive into the follower
        s.filter.envDepth = 0.8f;        // strong sweep
        s.filter.envAttack = 15.0f;
        s.filter.envRelease = 180.0f;
        s.filter.envDirection = 0;       // Up — opens on transient, classic wah
        s.filter.envAmount = 0.0f;       // let the EnvFilter own the motion (no filterEnv stacking)
        s.ampEnv.attackMs = 3.0f; s.ampEnv.decayMs = 250.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 150.0f;
        s.global.voiceMode = 1;
        // Phaser FX = slow swirl across the grain texture (unique FX in this chunk)
        s.modulationType = 1;            // Phaser
        s.phaser.rateHz = 0.4f;
        s.phaser.depth = 0.5f;
        s.phaser.feedback = 0.4f;
        s.phaser.mix = 0.3f;
        s.phaser.stages = 2;             // 6-stage
        s.phaser.centerFreqHz = 600.0f;
        // Mod identity: 1/4-note synced LFO2 breathes the grain<->sub blend
        s.lfo2Ext.noteValue = 13;        // 1/4
        setModSlot(s, 0, kSrcLFO2, kDstAllMorphPos, 0.3f, kCurveSCurve);
        presets.push_back(std::move(p));
    }
```

## 16. "Echo Bass" -> "Echo Bass"
- **Locate:** the block containing `p.name = "Echo Bass"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** An all-tape dub sub: saw+triangle through tape saturation into a synced tape-echo slapback, with an LFO pumping the echo swells and a corrected per-note filter pluck.
- **Coverage:**
  - dual-va-osc
  - osc-start-phase
  - svf-lp-filter
  - filterEnv-corrected-envAmount
  - tape-saturator-distortion
  - tape-delay
  - synced-LFO-mod-slot
  - effectMix-mod-dest
  - velocity->cutoff-voice-route
  - mono-legato-portamento
- **Rationale:** Base 420 Hz SVF LP + 22-semitone filter-env (attack 2 / decay 180 / sustain 0.18) opens to ~1.4 kHz on attack then settles ~515 Hz => a real pluck, fixing the static-filter and bogus-envAmount complaints. Distortion.type=5 TapeSaturator (sat 0.6) plus a tape delay (feedback 0.42, wear 0.3, 1/8-dotted) makes it thematically all-tape. Its unique mod hook is LFO1(1/2)->EffectMix via SCurve, a destination no sibling uses, so the echoes swell rhythmically. Velocity->FltCut adds touch response. Kept mono/legato with light glide for dub basslines. Fix applied: oscB.waveform corrected from 2 (Square) to 4 (Triangle) per OscWaveform enum in polyblep_oscillator.h, matching the intended triangle sub.
- **Replacement code:**
```cpp
    // "Echo Bass" - all-tape dub bass: tape saturation + synced tape delay,
    // per-note SVF filter pluck, and an LFO pumping the echo swells.
    {
        PresetDef p;
        p.name = "Echo Bass";
        p.category = "Bass";
        auto& s = p.state;
        // Saw + triangle sub an octave down; triangle fills the body under the saw
        s.oscA.type = 0; s.oscA.waveform = 1;            // Saw
        s.oscA.tuneSemitones = -12.0f; s.oscA.level = 0.8f;
        s.oscA.phase = 0.0f;                             // reset to zero-cross => tight, repeatable attack
        s.oscB.type = 0; s.oscB.waveform = 4;            // Triangle
        s.oscB.tuneSemitones = -12.0f; s.oscB.level = 0.45f;
        s.mixer.position = 0.32f;                        // favour the saw
        // SVF LP with a CORRECTED filter-env sweep (semitones, not Hz) => plucky per note
        s.filter.type = 0;                               // SVF LP
        s.filter.cutoffHz = 420.0f; s.filter.resonance = 0.9f;
        s.filter.envAmount = 22.0f;                      // +22 st sweep (fixes the bogus-Hz envAmount bug class)
        s.filterEnv.attackMs = 2.0f; s.filterEnv.decayMs = 180.0f;
        s.filterEnv.sustain = 0.18f; s.filterEnv.releaseMs = 200.0f;
        s.filterEnv.decayCurve = 0.4f;                   // exp-ish snap on the pluck tail
        s.ampEnv.attackMs = 2.0f; s.ampEnv.decayMs = 200.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 130.0f;
        // Tape SATURATOR in the voice for analog grit before the tape ECHO
        s.distortion.type = 5;                           // TapeSaturator
        s.distortion.drive = 0.3f; s.distortion.character = 0.5f; s.distortion.mix = 0.6f;
        s.distortion.tapeModel = 0; s.distortion.tapeSaturation = 0.6f; s.distortion.tapeBias = 0.5f;
        // Synced tape delay slapback with wow/wear => dub echo character
        s.delayEnabled = 1;
        s.delay.type = 1;                                // Tape
        s.delay.sync = 1; s.delay.noteValue = kNote1_8D;
        s.delay.mix = 0.28f; s.delay.feedback = 0.42f;
        s.delay.tapeSaturation = 0.6f; s.delay.tapeWear = 0.3f; s.delay.tapeAge = 0.2f;
        // IDENTITY gesture: a half-note synced LFO pumps the effect (echo) mix - no sibling touches EffectMix
        s.lfo1.rateHz = 1.0f; s.lfo1.shape = 1;          // Triangle
        s.lfo1.sync = 1; s.lfo1Ext.noteValue = 16;       // 1/2 note
        setModSlot(s, 0, kSrcLFO1, kDstEffectMix, 0.35f, kCurveSCurve);
        // Harder hits open the filter
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.4f);
        s.global.voiceMode = 1;                          // Mono
        s.monoMode.legato = 1; s.monoMode.portamentoTimeMs = 12.0f;
        presets.push_back(std::move(p));
    }
```

## 17. "Phase Bass" -> "Phase Bass"
- **Locate:** the block containing `p.name = "Phase Bass"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A phase-thickened bass: FM-style phaseMod on both oscillators feeding a key-tracked comb filter and a deep phaser, with an LFO morphing the A/B blend and aftertouch pushing comb feedback.
- **Coverage:**
  - dual-va-osc
  - phaseMod-cross-osc
  - comb-filter
  - combDamping
  - keyTrack
  - phaser-modfx
  - synced-LFO-mod-slot
  - morphPos-mod-dest
  - aftertouch->res-voice-route
  - mono-legato-portamento
- **Rationale:** Two saws at -12 (7-cent detune) with phaseMod=0.45 on EACH osc create FM sidebands that fatten well beyond simple detune, owning phaseMod-on-bass. The comb filter (type 6, keyTrack=1, damping 0.5) is a genuinely different filter from every other bass and complements the phase theme with tuned hollow resonances. Phaser (0.35 Hz, 8 stages, feedback 0.55) supplies slow sweep. Unique mod hook: LFO2(1/4)->AllMorphPos, rhythmically sliding the A/B blend - a destination/source pair no sibling repeats. Aftertouch->FltRes makes comb feedback performable. Not flagged; unchanged.
- **Replacement code:**
```cpp
    // "Phase Bass" - phase-thickened bass: inter-osc phaseMod + a key-tracked comb
    // filter + a deep phaser, with an LFO morphing the A/B blend. Owns phaseMod on bass.
    {
        PresetDef p;
        p.name = "Phase Bass";
        p.category = "Bass";
        auto& s = p.state;
        s.oscA.type = 0; s.oscA.waveform = 1;            // Saw
        s.oscA.tuneSemitones = -12.0f; s.oscA.level = 0.8f;
        s.oscA.phaseMod = 0.45f;                         // FM-ish sidebands thicken the tone...
        s.oscB.type = 0; s.oscB.waveform = 1;            // Saw
        s.oscB.tuneSemitones = -12.0f; s.oscB.fineCents = 7.0f; s.oscB.level = 0.7f;
        s.oscB.phaseMod = 0.45f;                         // ...applied to both => a dense cross-modded pair
        s.mixer.position = 0.5f;
        // Comb filter, key-tracked => hollow phasey resonances that follow the note
        s.filter.type = 6;                               // Comb
        s.filter.cutoffHz = 200.0f; s.filter.resonance = 0.35f;
        s.filter.combDamping = 0.5f;                     // tame the metallic top so it stays a bass
        s.filter.keyTrack = 1.0f;                        // comb tunes with pitch
        s.ampEnv.attackMs = 3.0f; s.ampEnv.decayMs = 200.0f;
        s.ampEnv.sustain = 0.75f; s.ampEnv.releaseMs = 150.0f;
        // Deep slow phaser layered on top of the phaseMod thickness
        s.modulationType = 1;                            // Phaser
        s.phaser.rateHz = 0.35f; s.phaser.depth = 0.6f;
        s.phaser.feedback = 0.55f; s.phaser.mix = 0.4f; s.phaser.stages = 3; // 8 stages
        s.phaser.centerFreqHz = 450.0f;
        // IDENTITY gesture: a 1/4 synced LFO morphs the A/B mix position (no sibling uses MorphPos)
        s.lfo2.rateHz = 1.0f; s.lfo2.shape = 1;          // Triangle
        s.lfo2.sync = 1; s.lfo2Ext.noteValue = kNote1_4; // 1/4
        setModSlot(s, 0, kSrcLFO2, kDstAllMorphPos, 0.5f, kCurveSCurve);
        // Aftertouch drives comb feedback for expressive growl
        setVoiceRoute(s, 0, kVSrcAftertouch, kVDstFltRes, 0.5f);
        s.global.voiceMode = 1;
        s.monoMode.legato = 1; s.monoMode.portamentoTimeMs = 15.0f;
        presets.push_back(std::move(p));
    }
```

## 18. "Additive Sub" -> "Additive Sub"
- **Locate:** the block containing `p.name = "Additive Sub"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A pure additive sub-spectrum on a true 24 dB ladder, weight-doubled by a PitchSync harmonizer sub-octave, with a slow mod-envelope breathing the spectral tilt so the dark tone never sits still.
- **Coverage:**
  - additive-engine
  - additive-tilt
  - true-ladder-filter
  - ladderSlope
  - ladderDrive
  - keyTrack
  - modEnv->specTilt-voice-route
  - bezier-free-slow-modEnv
  - harmonizer-PitchSync-sub
  - mono-legato-portamento
- **Rationale:** Additive OSC A (8 partials, tilt -6 dB) plus a sine at -12 gives a clean, rounded sub. The audit noted the old block used SVF LP while calling it 'ladder'; this makes it a genuine Ladder (type 4, 24 dB, drive 3, keyTrack 0.5), covering ladder slope/drive. The static-and-featureless complaint is cured by a very slow modEnv (attack 800 / decay 1500 ms) routed ENV3->SpecTilt, so brightness slowly evolves per note. The PitchSync harmonizer (mode 3, one voice at -12, wet -3 dB) adds locked sub weight - the category's harmonizer-PitchSync-sub home. Not flagged; unchanged.
- **Replacement code:**
```cpp
    // "Additive Sub" - pure additive sub-spectrum on a ladder, sub-doubled by a
    // PitchSync harmonizer, with a slow mod-env breathing the spectral tilt.
    {
        PresetDef p;
        p.name = "Additive Sub";
        p.category = "Bass";
        auto& s = p.state;
        s.oscA.type = 4;                                 // Additive
        s.oscA.additivePartials = 8; s.oscA.additiveTilt = -6.0f; // dark, rolled-off partials
        s.oscA.tuneSemitones = -12.0f; s.oscA.level = 0.85f;
        s.oscB.type = 0; s.oscB.waveform = 0;            // Sine reinforcement
        s.oscB.tuneSemitones = -12.0f; s.oscB.level = 0.5f;
        s.mixer.position = 0.35f;
        // A TRUE ladder (24 dB) with drive + keytrack - the audit's mislabelled "ladder" made real
        s.filter.type = 4;                               // Ladder
        s.filter.cutoffHz = 900.0f; s.filter.resonance = 0.3f;
        s.filter.ladderSlope = 4;                        // 24 dB/oct
        s.filter.ladderDrive = 3.0f;                     // gentle valve warmth
        s.filter.keyTrack = 0.5f;                        // follows pitch so high notes stay open
        s.ampEnv.attackMs = 4.0f; s.ampEnv.decayMs = 250.0f;
        s.ampEnv.sustain = 0.8f; s.ampEnv.releaseMs = 140.0f;
        // Slow mod-env (ENV3) breathes spectral tilt so the dark sub is never fully static
        s.modEnv.attackMs = 800.0f; s.modEnv.decayMs = 1500.0f;
        s.modEnv.sustain = 0.55f; s.modEnv.releaseMs = 1200.0f;
        setVoiceRoute(s, 0, kVSrcEnv3, kVDstSpecTilt, 0.5f);
        // PitchSync harmonizer locks a sub-octave onto the note for extra weight
        s.harmonizerEnabled = 1;
        s.harmonizer.pitchShiftMode = 3;                 // PitchSync
        s.harmonizer.numVoices = 1;
        s.harmonizer.voiceInterval[0] = -12;             // one octave down
        s.harmonizer.voiceLevelDb[0] = -2.0f;
        s.harmonizer.dryLevelDb = 0.0f; s.harmonizer.wetLevelDb = -3.0f;
        s.global.voiceMode = 1;
        s.monoMode.legato = 1; s.monoMode.portamentoTimeMs = 20.0f;
        presets.push_back(std::move(p));
    }
```

## 19. "Auto Wah" -> "Auto Wah"
- **Locate:** the block containing `p.name = "Auto Wah"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A funky envelope-filter bass whose resonance is wobbled by a chaotic Lorenz LFO (scaled x2) for a living squelch, with aftertouch tipping the tone and a touch of FM growl underneath.
- **Coverage:**
  - dual-va-osc
  - osc-start-phase
  - freqMod
  - envelope-filter
  - envSensitivity
  - envDepth
  - envDirection-up
  - chaosMod-source
  - allResonance-mod-dest
  - scale-axis(x2)
  - aftertouch->morphPos-voice-route
  - mono-legato-portamento
- **Rationale:** Keeps the strong EnvFilter core (type 11, sensitivity 8, depth 0.8, direction Up) that gave the original its identity, but ends the 'nothing else moves' problem: chaosMod.depth raised to 0.6 and routed Chaos->AllResonance with scale=3 (x2) makes the resonance wobble unpredictably, the category's chaosMod-wobble home and one of the only presets to exercise the scale axis. osc phase=0.25 gives a repeatable wah attack; oscB freqMod=0.2 adds growl; aftertouch->MorphPos makes it expressive. Distinct source (Chaos) and dest (Resonance) so no sibling repeats the gesture. Fix applied: oscB.waveform corrected from 2 (Square) to 4 (Triangle) per OscWaveform enum in polyblep_oscillator.h, matching the intended triangle body.
- **Replacement code:**
```cpp
    // "Auto Wah" - funky envelope-filter bass with a chaotic LFO wobbling the
    // resonance for a living, squelchy wah. Owns EnvFilter + chaosMod on bass.
    {
        PresetDef p;
        p.name = "Auto Wah";
        p.category = "Bass";
        auto& s = p.state;
        s.oscA.type = 0; s.oscA.waveform = 1;            // Saw
        s.oscA.tuneSemitones = -12.0f; s.oscA.level = 0.85f;
        s.oscA.phase = 0.25f;                            // fixed start phase => consistent wah attack
        s.oscB.type = 0; s.oscB.waveform = 4;            // Triangle body
        s.oscB.tuneSemitones = -12.0f; s.oscB.level = 0.35f;
        s.oscB.freqMod = 0.2f;                           // slight FM growl sitting under the wah
        s.mixer.position = 0.25f;
        // Envelope FILTER (auto-wah) - opens upward on each note attack
        s.filter.type = 11;                              // EnvFilter
        s.filter.cutoffHz = 1400.0f; s.filter.resonance = 0.55f;
        s.filter.envSubType = 0;                         // LP response
        s.filter.envSensitivity = 8.0f; s.filter.envDepth = 0.8f;
        s.filter.envAttack = 3.0f; s.filter.envRelease = 60.0f;
        s.filter.envDirection = 0;                       // Up
        s.ampEnv.attackMs = 1.0f; s.ampEnv.decayMs = 200.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 100.0f;
        // IDENTITY gesture: a chaotic Lorenz LFO wobbles resonance (scale x2) => squelch that never repeats
        s.chaosMod.rateHz = 0.8f; s.chaosMod.type = 0;   // Lorenz
        s.chaosMod.depth = 0.6f;                         // must raise from 0 or the source is silent
        setModSlot(s, 0, kSrcChaos, kDstAllResonance, 0.4f, kCurveSCurve);
        s.modMatrix.slots[0].scale = 3;                  // x2 => exercise the untouched depth-scale axis
        // Aftertouch tips the wah's morph/centre for performable expression
        setVoiceRoute(s, 0, kVSrcAftertouch, kVDstMorphPos, 0.4f);
        s.global.voiceMode = 1;
        s.monoMode.legato = 1; s.monoMode.portamentoTimeMs = 15.0f;
        presets.push_back(std::move(p));
    }
```

## 20. "Massive" -> "Massive"
- **Locate:** the block containing `p.name = "Massive"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A big aggressive bass built on a resonant SVF-Peak low-mid boost (svfGain) and a Lockhart wavefolder, thickened by a half-cycle osc phase offset and FM growl, tamed to bass by a global lowpass, with velocity driving the fold.
- **Coverage:**
  - dual-va-osc
  - osc-start-phase
  - freqMod
  - svf-peak-filter(svfGain)
  - svfDrive
  - filterEnv-corrected-envAmount
  - wavefolder-lockhart-distortion
  - global-filter-LP
  - digital-delay(free-time)
  - synced-LFO-mod-slot
  - globalFltCut-mod-dest
  - velocity->distDrive-voice-route
  - mono-legato-portamento
- **Rationale:** Owns svfGain: filter is SVF Peak (type 8) at 180 Hz, Q 2.0, +9 dB boost with svfDrive 8 dB - a parallel-style low-mid punch no other bass has (the directive's 'ladder drive' aggression is supplied by svfDrive + the Lockhart wavefolder, since one voice filter cannot be both Ladder and Peak). A global LP at 4 kHz tames the fold's brightness so it remains a bass (also exercising the global filter). oscB phase=0.5 + freqMod=0.3 add hollow-fat transient and growl. Two distinct gestures: LFO2(1/1)->GlobalFltCut for slow top-end movement, and Velocity->DistDrive for dynamic fold. Note: distortion drive is only reachable per-voice, so the global env-follower route was intentionally avoided; a free-time digital delay (unsynced 170 ms) sets it apart from Echo Bass's tape echo. Not flagged; unchanged.
- **Replacement code:**
```cpp
    // "Massive" - big aggressive bass: SVF Peak low-mid punch (svfGain) + Lockhart
    // wavefolder + osc phase-offset thickness, tamed by a global LP, free-time delay.
    {
        PresetDef p;
        p.name = "Massive";
        p.category = "Bass";
        auto& s = p.state;
        s.oscA.type = 0; s.oscA.waveform = 1;            // Saw
        s.oscA.tuneSemitones = -12.0f; s.oscA.level = 0.85f;
        s.oscA.phase = 0.0f;
        s.oscB.type = 0; s.oscB.waveform = 1;            // Saw
        s.oscB.tuneSemitones = -12.0f; s.oscB.fineCents = 15.0f; s.oscB.level = 0.72f;
        s.oscB.phase = 0.5f;                             // half-cycle offset => fatter, hollow transient
        s.oscB.freqMod = 0.3f;                           // growl
        s.mixer.position = 0.5f;
        // SVF PEAK: a resonant low-mid bell boost for parallel-style punch (owns svfGain)
        s.filter.type = 8;                               // SVF Peak
        s.filter.cutoffHz = 180.0f; s.filter.resonance = 2.0f;
        s.filter.svfGain = 9.0f;                         // +9 dB low-mid punch
        s.filter.svfDrive = 8.0f;                        // post-filter saturation grit
        s.filter.envAmount = 14.0f;                      // small CORRECTED env sweep => attack punch on the peak
        s.filterEnv.attackMs = 2.0f; s.filterEnv.decayMs = 160.0f;
        s.filterEnv.sustain = 0.3f; s.filterEnv.releaseMs = 180.0f;
        // Lockhart wavefolder for harmonic aggression (mix<1 keeps the clean lows underneath)
        s.distortion.type = 4;                           // Wavefolder
        s.distortion.drive = 0.35f; s.distortion.foldType = 2; // Lockhart
        s.distortion.character = 0.5f; s.distortion.mix = 0.7f;
        // Global LP tames the wavefolder's bright top so it stays a BASS
        s.globalFilter.enabled = 1; s.globalFilter.type = 0; // LP
        s.globalFilter.cutoffHz = 4000.0f; s.globalFilter.resonance = 0.707f;
        s.ampEnv.attackMs = 2.0f; s.ampEnv.decayMs = 250.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 120.0f;
        // Free-time (un-synced) digital slap for width without tempo lock
        s.delayEnabled = 1;
        s.delay.type = 0;                                // Digital
        s.delay.sync = 0; s.delay.timeMs = 170.0f;
        s.delay.mix = 0.12f; s.delay.feedback = 0.2f; s.delay.digitalWidth = 130.0f;
        // IDENTITY gesture: a whole-note synced LFO sweeps the GLOBAL LP cutoff => slow tonal movement on top
        s.lfo2.rateHz = 1.0f; s.lfo2.shape = 0;          // Sine
        s.lfo2.sync = 1; s.lfo2Ext.noteValue = 19;       // 1/1 note
        setModSlot(s, 0, kSrcLFO2, kDstGlobalFltCut, 0.4f, kCurveSCurve);
        // Velocity drives the wavefolder amount for dynamic dirt
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstDistDrive, 0.4f);
        s.global.voiceMode = 1;
        s.monoMode.legato = 1; s.monoMode.portamentoTimeMs = 10.0f;
        presets.push_back(std::move(p));
    }
```
