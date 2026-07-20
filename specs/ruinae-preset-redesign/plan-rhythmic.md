# Ruinae Preset Plan — Rhythmic

The Rhythmic bank's job is motion: every preset here is built around the TranceGate and/or the arpeggiator, so the sameness complaint is most acute — the old set was a stack of near-identical gated dual-saws whose only variation lived in the delay. This redesign gives each of the 20 presets a distinct *engine + filter + modulation-source + FX* fingerprint while making the bank collectively exercise the full rhythmic feature surface: TranceGate step/depth/attack/release, Euclidean and retrigger modes, the Gate/Transient/Chaos/Random/S&H voice-sources and mod-matrix routes, the arp ratchet/velocity lanes, every distortion flavour (Tape/Ring/Spectral-bitcrush), every delay type (Digital/Tape/PingPong/Granular), both reverb variants (Plate/Hall), the harmonizer, and the under-used engines (Additive, Particle, Wavetable, Sync, PhaseDistortion, SpectralFreeze, Chaos) and filter types (Ladder, SVF Peak, Comb, Formant, Env-Filter). A recurring correction across the bank: the broken `envAmount ≈ 4000` idiom is replaced everywhere with real semitone values (+14…+30 st) so filter-env sweeps are actually audible.

Locate every block by searching for its `p.name = "<original name>"` string in `F:\projects\iterum\tools\ruinae_preset_generator.cpp` — line numbers drift as edits land, so never rely on them.

## 1. "Trance Gate Pad" -> "Trance Gate Pad"
- **Locate:** the block containing `p.name = "Trance Gate Pad"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A lush 16-step gated pad whose every chop also re-sculpts the spectrum via an FFT morph between a saw and a square, drifting under two tempo-synced LFOs.
- **Coverage:** TranceGate numSteps + stepLevels + depth + rate + attack/release; Gate voice-source -> MorphPos; dual LFO tempo-synced; filterEnv corrected; reverb (Hall); Chorus FX; mod-matrix.
- **Rationale:** The identity break from the old dual-saw clone is the SpectralMorph mixer (mode=1, bank-unique) fed a saw vs a square so the A<->B morph is audibly different timbres, THEN driving that morph from three places: LFO2 (slow synced sine), and crucially the GATE voice-source (kVSrcGate->kVDstMorphPos 0.6) so each rhythmic chop lands on a new tone. LFO1 is a fast synced saw on cutoff locked to the gate (SCurve). filterEnv.envAmount uses the corrected +18 semitones (not the broken 4000) for a slow ladder bloom. Chorus + Hall + width 1.5/spread 0.35 make it lush; both LFOs sync=1 satisfies the dual-synced-LFO quota.
- **Replacement code:**
```cpp
    // "Trance Gate Pad" - lush gated pad where the gate also morphs the timbre
    {
        PresetDef p;
        p.name = "Trance Gate Pad";
        p.category = "Rhythmic";
        auto& s = p.state;
        // --- Saw vs SQUARE blended through the (bank-unique) SpectralMorph mixer, so
        //     morph position is a real FFT timbre morph, not just a crossfade.
        s.oscA.type = 0;            // PolyBLEP
        s.oscA.waveform = 1;       // Saw
        s.oscA.level = 0.7f;
        s.oscB.type = 0;
        s.oscB.waveform = 2;       // Square -> gives the morph something to travel to
        s.oscB.fineCents = 9.0f;   // analog beating
        s.oscB.level = 0.65f;
        s.mixer.mode = 1;          // SpectralMorph (unused elsewhere in the bank)
        s.mixer.position = 0.4f;
        // --- Ladder LP with a CORRECTED audible filter-env swell (semitones, not Hz)
        s.filter.type = 4;         // Ladder LP
        s.filter.cutoffHz = 2600.0f;
        s.filter.resonance = 0.32f;
        s.filter.ladderSlope = 3;  // 18 dB/oct, softer than full 24
        s.filter.envAmount = 18.0f; // +18 st bloom (was the broken 4000 idiom)
        s.filterEnv.attackMs = 350.0f;
        s.filterEnv.decayMs = 1400.0f;
        s.filterEnv.sustain = 0.35f;
        s.filterEnv.releaseMs = 900.0f;
        // Amp: slow pad swell
        s.ampEnv.attackMs = 120.0f;
        s.ampEnv.decayMs = 500.0f;
        s.ampEnv.sustain = 0.85f;
        s.ampEnv.releaseMs = 900.0f;
        // --- 16-step trance gate, soft edges so it pulses rather than clicks
        s.tranceGate.enabled = 1;
        s.tranceGate.numSteps = 16;
        s.tranceGate.tempoSync = 1;
        s.tranceGate.noteValue = kNote1_16;
        s.tranceGate.depth = 0.9f;
        s.tranceGate.attackMs = 6.0f;
        s.tranceGate.releaseMs = 35.0f;
        float tg[32]{};
        tg[0]=1.0f; tg[1]=0.6f; tg[2]=0.0f; tg[3]=1.0f;
        tg[4]=0.8f; tg[5]=0.0f; tg[6]=1.0f; tg[7]=0.4f;
        tg[8]=1.0f; tg[9]=0.7f; tg[10]=0.0f; tg[11]=1.0f;
        tg[12]=0.9f; tg[13]=0.0f; tg[14]=0.5f; tg[15]=1.0f;
        for (int i = 0; i < 32; ++i) s.tranceGate.stepLevels[i] = tg[i];
        // --- Dual TEMPO-SYNCED LFOs: fast synced saw chops cutoff with the gate,
        //     slow synced sine drifts the spectral morph.
        s.lfo1.rateHz = 4.0f; s.lfo1.shape = 2; // Saw
        s.lfo1.depth = 0.6f;  s.lfo1.sync = 1;
        s.lfo1Ext.noteValue = kNote1_8;
        setModSlot(s, 0, kSrcLFO1, kDstAllFltCut, 0.35f, kCurveSCurve);
        s.lfo2.rateHz = 0.5f; s.lfo2.shape = 0; // Sine
        s.lfo2.depth = 0.5f;  s.lfo2.sync = 1;
        s.lfo2Ext.noteValue = 19; // 1/1 whole-note, very slow drift
        setModSlot(s, 1, kSrcLFO2, kDstAllMorphPos, 0.30f, kCurveSCurve);
        // --- Signature move: the GATE itself drives the spectral morph, so every
        //     chop also lands on a different timbre (Gate voice-source -> MorphPos).
        setVoiceRoute(s, 0, kVSrcGate, kVDstMorphPos, 0.6f);
        setVoiceRoute(s, 1, kVSrcVelocity, kVDstFltCut, 0.25f);
        // --- Lush wrapper: gentle chorus + wide HALL (contrasts the strings' Plate)
        s.modulationType = 3;      // Chorus
        s.chorus.rateHz = 0.4f; s.chorus.depth = 0.4f; s.chorus.voices = 3;
        s.chorus.mix = 0.35f;
        s.reverbEnabled = 1;
        s.reverbType = 1;          // Hall
        s.reverb.size = 0.7f; s.reverb.mix = 0.28f; s.reverb.damping = 0.4f;
        s.global.width = 1.5f;
        s.global.spread = 0.35f;
        s.global.polyphony = 12;
        presets.push_back(std::move(p));
    }
```

## 2. "Pumping Lead" -> "Sidechain Sync"
- **Locate:** the block containing `p.name = "Pumping Lead"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A monophonic hard-sync lead that ducks like a real compressor via its transient detector, glides between notes, and bites with a note-tracked ring modulator.
- **Coverage:** hard-sync engine; transient source (sidechain feel); mono glide; filterEnv corrected; TranceGate 4-step pump; RingModulator distortion; Phaser FX; PingPong delay; Gate voice-source -> GateDepth (via velocity); reverb (Plate).
- **Rationale:** Escapes the dual-saw template entirely: OSC A is the Sync engine (syncRatio 2.5, hard) with a quiet -12 sub on OSC B. The 'real' sidechain is kSrcTransient->kDstMasterVol at NEGATIVE -0.55 (Exp), so each note attack ducks the master exactly like a compressor keyed off the transient; sensitivity raised to 0.7. The 4-step gate with slow 20ms attack gives the pumped swell. Mono + legato portamento (55ms, portaMode=1) makes the sync sweeps slur. distortion.type=6 RingModulator with ringFreqMode=1 NoteTrack + ringRatio 0.1111 (~2.0) stays musical at mix 0.3. Phaser (6-stage) + synced PingPong 1/8D echo. filterEnv.envAmount corrected to +28 st for the per-note zap.
- **Replacement code:**
```cpp
    // "Sidechain Sync" - mono hard-sync lead ducked by its own transient detector
    {
        PresetDef p;
        p.name = "Sidechain Sync";
        p.category = "Rhythmic";
        auto& s = p.state;
        // --- Hard-sync engine on OSC A, quiet sub saw on OSC B for weight
        s.oscA.type = 3;           // Sync
        s.oscA.waveform = 1;
        s.oscA.syncRatio = 2.5f;   // classic sync-sweep zone
        s.oscA.syncWaveform = 1;   // Saw slave = aggressive
        s.oscA.syncMode = 0;       // Hard
        s.oscA.syncAmount = 1.0f;
        s.oscA.level = 0.9f;
        s.oscB.type = 0;
        s.oscB.waveform = 1;
        s.oscB.tuneSemitones = -12.0f; // sub for body
        s.oscB.level = 0.35f;
        s.mixer.position = 0.3f;   // favour the sync osc
        // --- SVF LP, bright, with a CORRECTED filter-env snap on each note
        s.filter.type = 0;         // SVF LP
        s.filter.cutoffHz = 3500.0f;
        s.filter.resonance = 0.3f;
        s.filter.svfDrive = 4.0f;  // a little edge
        s.filter.envAmount = 28.0f; // +28 st zap (corrected semitone amount)
        s.filterEnv.attackMs = 2.0f;
        s.filterEnv.decayMs = 180.0f;
        s.filterEnv.sustain = 0.25f;
        s.filterEnv.releaseMs = 120.0f;
        s.ampEnv.attackMs = 4.0f;
        s.ampEnv.decayMs = 250.0f;
        s.ampEnv.sustain = 0.8f;
        s.ampEnv.releaseMs = 160.0f;
        // --- MONO with legato glide: portamento slurs the sync sweeps
        s.global.voiceMode = 1;    // Mono
        s.monoMode.priority = 0;   // Last
        s.monoMode.legato = 1;
        s.monoMode.portamentoTimeMs = 55.0f;
        s.monoMode.portaMode = 1;  // glide on legato only
        // --- 4-step sidechain-shaped pump gate (slow rise = the pumped swell)
        s.tranceGate.enabled = 1;
        s.tranceGate.numSteps = 4;
        s.tranceGate.tempoSync = 1;
        s.tranceGate.noteValue = kNote1_16;
        s.tranceGate.depth = 1.0f;
        s.tranceGate.attackMs = 20.0f;
        s.tranceGate.releaseMs = 60.0f;
        s.tranceGate.stepLevels[0] = 0.0f; s.tranceGate.stepLevels[1] = 0.6f;
        s.tranceGate.stepLevels[2] = 1.0f; s.tranceGate.stepLevels[3] = 0.85f;
        // --- The REAL sidechain: transient detector ducks master volume on every
        //     attack (negative amount = duck), so it pumps like a keyed compressor.
        s.transient.sensitivity = 0.7f;
        s.transient.attackMs = 1.0f;
        s.transient.decayMs = 140.0f;
        setModSlot(s, 0, kSrcTransient, kDstMasterVol, -0.55f, kCurveExp);
        // Harder hits duck the gate deeper for playing dynamics (-> GateDepth)
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstGateDepth, 0.4f);
        // --- Metallic bite: note-tracked ring modulator, blended so pitch survives
        s.distortion.type = 6;     // RingModulator
        s.distortion.drive = 0.4f;
        s.distortion.mix = 0.3f;
        s.distortion.ringFreqMode = 1;   // NoteTrack (stays musical)
        s.distortion.ringRatio = 0.1111f; // ~2.0 ratio (normalized encoding)
        s.distortion.ringWaveform = 0;   // Sine
        // --- Phaser sweep + synced PingPong echo
        s.modulationType = 1;      // Phaser
        s.phaser.rateHz = 0.3f; s.phaser.depth = 0.6f; s.phaser.stages = 2; // 6-stage
        s.phaser.feedback = 0.4f; s.phaser.mix = 0.4f;
        s.delayEnabled = 1;
        s.delay.type = 2;          // PingPong
        s.delay.sync = 1; s.delay.noteValue = kNote1_8D;
        s.delay.feedback = 0.35f; s.delay.mix = 0.22f;
        s.delay.pingPongWidth = 140.0f;
        s.reverbEnabled = 1;
        s.reverbType = 0;          // Plate
        s.reverb.size = 0.4f; s.reverb.mix = 0.15f;
        presets.push_back(std::move(p));
    }
```

## 3. "Stutter Bass" -> "Stutter Bass"
- **Locate:** the block containing `p.name = "Stutter Bass"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** An octave-down mono saw+square bass through a driven ladder, machine-gunned by a fast gate whose retriggerDepth fires sub-hit rolls inside each step, spitting harder as the gate pumps its tape distortion.
- **Coverage:** TranceGate retriggerDepth (machine-gun); TapeSaturator distortion; Gate voice-source -> DistDrive; filterEnv corrected; Digital delay; Ladder drive/slope; mono voice mode.
- **Rationale:** Owns retriggerDepth: 0.85 on a 16-step 1/16 gate re-fires the amp envelope inside each step for the machine-gun rolls, with near-zero gate attack/release (0.5/4ms) for hard edges. Distinct from the other four by the driven ladder (ladderDrive 7dB, slope 4) plus a corrected +30 st filterEnv thump per hit. distortion.type=5 TapeSaturator (tapeModel 1, saturation 0.6) is its owned dirty type; the GATE output drives distortion drive (kVSrcGate->kVDstDistDrive 0.4) so stutters spit. A subtle synced Digital 1/16 delay (mix 0.15) adds a dub tail without muddying the low end. Osc levels kept moderate to compensate the ladder drive.
- **Replacement code:**
```cpp
    // "Stutter Bass" - octave-down mono bass, machine-gun retrigger stutter
    {
        PresetDef p;
        p.name = "Stutter Bass";
        p.category = "Rhythmic";
        auto& s = p.state;
        // --- Octave-down saw + detuned square, mono, through a driven ladder
        s.oscA.type = 0;
        s.oscA.waveform = 1;       // Saw
        s.oscA.tuneSemitones = -12.0f;
        s.oscA.level = 0.85f;
        s.oscB.type = 0;
        s.oscB.waveform = 2;       // Square
        s.oscB.tuneSemitones = -12.0f;
        s.oscB.fineCents = -4.0f;  // slight beat for thickness
        s.oscB.level = 0.5f;
        s.mixer.position = 0.4f;
        s.filter.type = 4;         // Ladder LP
        s.filter.cutoffHz = 900.0f;
        s.filter.resonance = 0.45f;
        s.filter.ladderSlope = 4;  // 24 dB/oct
        s.filter.ladderDrive = 7.0f; // grit (osc levels moderated to compensate)
        // CORRECTED filter-env: a hard +30 st thump on every hit
        s.filter.envAmount = 30.0f;
        s.filterEnv.attackMs = 1.0f;
        s.filterEnv.decayMs = 90.0f;
        s.filterEnv.sustain = 0.15f;
        s.filterEnv.releaseMs = 70.0f;
        s.ampEnv.attackMs = 1.0f;
        s.ampEnv.decayMs = 140.0f;
        s.ampEnv.sustain = 0.7f;
        s.ampEnv.releaseMs = 70.0f;
        s.global.voiceMode = 1;    // Mono
        // --- Machine-gun stutter: fast gate + retriggerDepth re-fires the envelope
        //     inside each step for sub-hit rolls (this preset owns retriggerDepth).
        s.tranceGate.enabled = 1;
        s.tranceGate.numSteps = 16;
        s.tranceGate.tempoSync = 1;
        s.tranceGate.noteValue = kNote1_16;
        s.tranceGate.depth = 1.0f;
        s.tranceGate.attackMs = 0.5f;
        s.tranceGate.releaseMs = 4.0f;
        s.tranceGate.retriggerDepth = 0.85f; // the machine-gun character
        float bg[32]{};
        bg[0]=1;bg[1]=1;bg[2]=0;bg[3]=1; bg[4]=0;bg[5]=1;bg[6]=1;bg[7]=0;
        bg[8]=1;bg[9]=0;bg[10]=1;bg[11]=1; bg[12]=0;bg[13]=1;bg[14]=0;bg[15]=1;
        for (int i = 0; i < 32; ++i) s.tranceGate.stepLevels[i] = bg[i];
        // --- Tape saturation for warmth + bite (this preset owns TapeSaturator)
        s.distortion.type = 5;     // TapeSaturator
        s.distortion.drive = 0.5f;
        s.distortion.mix = 0.7f;
        s.distortion.tapeModel = 1;
        s.distortion.tapeSaturation = 0.6f;
        s.distortion.tapeBias = 0.55f;
        // --- The gate output pumps distortion drive so stutters spit harder
        //     (Gate voice-source -> DistDrive).
        setVoiceRoute(s, 0, kVSrcGate, kVDstDistDrive, 0.4f);
        setVoiceRoute(s, 1, kVSrcVelocity, kVDstFltCut, 0.3f);
        // --- Subtle synced DIGITAL delay for a dub tail (kept low = tight low end)
        s.delayEnabled = 1;
        s.delay.type = 0;          // Digital
        s.delay.sync = 1; s.delay.noteValue = kNote1_16;
        s.delay.feedback = 0.3f; s.delay.mix = 0.15f;
        s.delay.digitalWidth = 130.0f;
        presets.push_back(std::move(p));
    }
```

## 4. "Gate Strings" -> "Gate Strings"
- **Locate:** the block containing `p.name = "Gate Strings"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A bowed saw + additive-shimmer string section, rhythmically gated, widened by a flanger ensemble and thickened by a diatonic 5th+octave harmonizer into a tape delay and plate reverb.
- **Coverage:** Additive engine; TranceGate 8-step 1/8; filterEnv corrected; Flanger FX; harmonizer; Tape delay; reverb (Plate); LFO -> morph (crossfade shimmer).
- **Rationale:** Breaks the dual-saw core by making OSC B the Additive engine (24 partials, -6 dB/oct tilt, 0.04 inharmonicity) for silky string overtones a triangle can't give. Its own mod identity: a slow free-run sine LFO on morph position crossfades bowed-saw<->additive-shimmer so the tone breathes (a gesture no sibling repeats), plus velocity->morph so harder bowing brings more shimmer. filterEnv.envAmount corrected to a gentle +14 st bow-swell with 400ms attack. Owns the Flanger here (ensemble widening, 0.25Hz) and the harmonizer (Scalic, +5th/+octave panned L/R for a full section). Tape delay + PLATE reverb deliberately contrast the Pad's Chorus+Hall.
- **Replacement code:**
```cpp
    // "Gate Strings" - additive-shimmer string section, gated, flanged, harmonized
    {
        PresetDef p;
        p.name = "Gate Strings";
        p.category = "Rhythmic";
        auto& s = p.state;
        // --- Bowed saw OSC A + an ADDITIVE shimmer OSC B for airy upper harmonics
        s.oscA.type = 0;
        s.oscA.waveform = 1;       // Saw
        s.oscA.level = 0.6f;
        s.oscB.type = 4;           // Additive
        s.oscB.additivePartials = 24;   // rich but not buzzy
        s.oscB.additiveTilt = -6.0f;    // roll off the top = silky
        s.oscB.additiveInharm = 0.04f;  // faint string inharmonicity
        s.oscB.fineCents = 6.0f;
        s.oscB.level = 0.5f;
        s.mixer.position = 0.45f;
        s.filter.type = 0;         // SVF LP
        s.filter.cutoffHz = 3200.0f;
        s.filter.resonance = 0.18f;
        // CORRECTED filter-env: a gentle bow-swell opening as the note sustains
        s.filter.envAmount = 14.0f;
        s.filterEnv.attackMs = 400.0f;
        s.filterEnv.decayMs = 900.0f;
        s.filterEnv.sustain = 0.6f;
        s.filterEnv.releaseMs = 800.0f;
        s.ampEnv.attackMs = 220.0f; // slow bow attack
        s.ampEnv.decayMs = 600.0f;
        s.ampEnv.sustain = 0.8f;
        s.ampEnv.releaseMs = 900.0f;
        // --- 8-step gentle rhythmic gate (1/8) with alternating dynamics
        s.tranceGate.enabled = 1;
        s.tranceGate.numSteps = 8;
        s.tranceGate.tempoSync = 1;
        s.tranceGate.noteValue = kNote1_8;
        s.tranceGate.depth = 0.75f;
        s.tranceGate.attackMs = 12.0f;
        s.tranceGate.releaseMs = 55.0f;
        s.tranceGate.stepLevels[0] = 1.0f; s.tranceGate.stepLevels[1] = 0.5f;
        s.tranceGate.stepLevels[2] = 0.85f; s.tranceGate.stepLevels[3] = 0.35f;
        s.tranceGate.stepLevels[4] = 1.0f; s.tranceGate.stepLevels[5] = 0.45f;
        s.tranceGate.stepLevels[6] = 0.9f; s.tranceGate.stepLevels[7] = 0.3f;
        // --- Own mod identity: slow free-run sine crossfades saw<->additive shimmer
        s.lfo1.rateHz = 0.2f; s.lfo1.shape = 0; // Sine
        s.lfo1.depth = 0.5f;  s.lfo1.sync = 0;  // free-run breathing
        setModSlot(s, 0, kSrcLFO1, kDstAllMorphPos, 0.30f, kCurveSCurve);
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstMorphPos, 0.30f); // harder bow = more shimmer
        // --- Ensemble: a Flanger widens it into a string-machine (owns Flanger)
        s.modulationType = 2;      // Flanger
        s.flanger.rateHz = 0.25f; s.flanger.depth = 0.5f; s.flanger.feedback = 0.2f;
        s.flanger.mix = 0.35f; s.flanger.stereoSpread = 120.0f;
        // --- Harmonizer stacks a diatonic 5th + octave for a full section
        s.harmonizerEnabled = 1;
        s.harmonizer.harmonyMode = 1;   // Scalic
        s.harmonizer.numVoices = 2;
        s.harmonizer.wetLevelDb = -8.0f;
        s.harmonizer.voiceInterval[0] = 4; // +5th (scale degrees)
        s.harmonizer.voiceInterval[1] = 7; // +octave
        s.harmonizer.voicePan[0] = -0.4f;
        s.harmonizer.voicePan[1] = 0.4f;
        // --- Synced TAPE delay + PLATE reverb (contrast to the Pad's Hall)
        s.delayEnabled = 1;
        s.delay.type = 1;          // Tape
        s.delay.sync = 1; s.delay.noteValue = kNote1_8;
        s.delay.feedback = 0.28f; s.delay.mix = 0.2f;
        s.delay.tapeSaturation = 0.4f;
        s.reverbEnabled = 1;
        s.reverbType = 0;          // Plate
        s.reverb.size = 0.55f; s.reverb.mix = 0.25f;
        s.global.width = 1.4f;
        s.global.spread = 0.25f;
        s.global.polyphony = 12;
        presets.push_back(std::move(p));
    }
```

## 5. "Choppy Texture" -> "Choppy Texture"
- **Locate:** the block containing `p.name = "Choppy Texture"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A drifting granular-particle + pink-noise cloud chopped by a Euclidean E(5,8) gate, gurgled by a chaos LFO, spectrally bit-crushed, and smeared through a granular delay into a huge diffuse hall.
- **Coverage:** Particle engine; Noise engine; Euclidean gate mode; chaosMod source; Random source; Spectral distortion (SpectralBitcrush); Granular delay; reverb (Hall).
- **Rationale:** Keeps the particle+noise identity but deepens it: particleEnvType=3 (Blackman) and particleDrift 0.3 give a smoother, slowly-wandering cloud. The Euclidean gate E(5,8) (euclideanEnabled, hits=5, numSteps=8, rotation=1) chops it. Its owned exotic sources: chaosMod (Rossler, depth 0.6) -> AllResonance for gurgling resonance, plus a smoothed Random -> AllFltCut for extra life. distortion.type=2 Spectral in SpectralBitcrush mode (spectralMode=3) with a CORRECTED normalized spectralBits=0.333f (=(6-1)/15, i.e. ~6 magnitude bits) genuinely crushes the grains into a metallic haze -- the previous 6.0f was clamped to 1.0 (=16 bits = no crush at all), silently defeating the effect. delay.type=3 Granular (pitch/pos spray) + a big Hall (size 0.8, diffusion 0.9) make the wash. Sits apart from the four gated tonal presets as the pure-texture member.
- **Replacement code:**
```cpp
    // "Choppy Texture" - granular particle + noise cloud, Euclidean-gated, chaos-driven
    {
        PresetDef p;
        p.name = "Choppy Texture";
        p.category = "Rhythmic";
        auto& s = p.state;
        // --- Granular particle cloud + pink noise
        s.oscA.type = 6;           // Particle
        s.oscA.particleScatter = 5.0f;
        s.oscA.particleDensity = 28.0f;
        s.oscA.particleLifetime = 220.0f;
        s.oscA.particleSpawnMode = 1; // Random
        s.oscA.particleEnvType = 3;   // Blackman grains = smoother cloud
        s.oscA.particleDrift = 0.3f;  // slow pitch wander
        s.oscA.level = 0.7f;
        s.oscB.type = 9;           // Noise
        s.oscB.noiseColor = 1;     // Pink
        s.oscB.level = 0.18f;
        s.mixer.position = 0.25f;
        s.filter.type = 0;         // SVF LP
        s.filter.cutoffHz = 4500.0f;
        s.filter.resonance = 0.25f;
        s.ampEnv.attackMs = 25.0f;
        s.ampEnv.decayMs = 400.0f;
        s.ampEnv.sustain = 0.7f;
        s.ampEnv.releaseMs = 600.0f;
        // --- EUCLIDEAN gate E(5,8): 5 hits spread over 8 sixteenths
        s.tranceGate.enabled = 1;
        s.tranceGate.numSteps = 8;
        s.tranceGate.tempoSync = 1;
        s.tranceGate.noteValue = kNote1_16;
        s.tranceGate.depth = 0.9f;
        s.tranceGate.attackMs = 3.0f;
        s.tranceGate.releaseMs = 18.0f;
        s.tranceGate.euclideanEnabled = 1;
        s.tranceGate.euclideanHits = 5;   // E(5,8)
        s.tranceGate.euclideanRotation = 1;
        // --- chaosMod gurgles the resonance for an ever-shifting texture
        s.chaosMod.rateHz = 0.8f;
        s.chaosMod.type = 1;       // Rossler
        s.chaosMod.depth = 0.6f;   // must raise depth or the source is silent
        s.chaosMod.sync = 0;
        setModSlot(s, 0, kSrcChaos, kDstAllResonance, 0.4f, kCurveLinear);
        // A slow smoothed random nudges the filter for extra life
        s.random.rateHz = 0.5f; s.random.smoothness = 0.8f;
        setModSlot(s, 1, kSrcRandom, kDstAllFltCut, 0.25f, kCurveSCurve);
        // --- Spectral bit-crush smears the grains into a metallic haze (owns Spectral)
        //     spectralBits is NORMALIZED 0-1 (maps to 1-16 bits in the voice); use
        //     0.333f = (6-1)/15 for ~6 bits, and SpectralBitcrush mode (3) for a true crush.
        s.distortion.type = 2;     // Spectral
        s.distortion.drive = 0.4f;
        s.distortion.mix = 0.35f;
        s.distortion.spectralMode = 3;    // SpectralBitcrush (true bit-crush)
        s.distortion.spectralBits = 0.333f; // ~6 bits (normalized: (6-1)/15)
        // --- Granular delay + big diffuse HALL wash
        s.delayEnabled = 1;
        s.delay.type = 3;          // Granular
        s.delay.sync = 1; s.delay.noteValue = kNote1_8;
        s.delay.feedback = 0.4f; s.delay.mix = 0.3f;
        s.delay.granularSizeMs = 120.0f;
        s.delay.granularDensity = 18.0f;
        s.delay.granularPitchSpray = 0.3f;
        s.delay.granularPosSpray = 0.4f;
        s.reverbEnabled = 1;
        s.reverbType = 1;          // Hall
        s.reverb.size = 0.8f; s.reverb.mix = 0.4f; s.reverb.diffusion = 0.9f;
        s.global.width = 1.4f;
        presets.push_back(std::move(p));
    }
```

## 6. "Harmony Gate" -> "Harmony Gate"
- **Locate:** the block containing `p.name = "Harmony Gate"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A wide detuned dual-saw pad chopped into 1/16 gate pulses, thickened by scalic 3rd+5th harmony voices and set in constant motion by two tempo-synced LFOs, a phaser sweep, and a Macro-morph knob.
- **Coverage:** TranceGate numSteps + stepLevels + depth + attack/release; Gate voice-source -> MorphPos route; harmonizer; dual LFO tempo-synced; filterEnv corrected; Phaser FX; reverb; Macro (one-knob morph); engines: PolyBLEP dual-saw.
- **Rationale:** Keeps the harmonizer identity the audit praised but fixes every 'static voice' complaint: the old version had no LFO, no matrix, no filter-env. Now cutoff starts at 2200 Hz with envAmount=+22 st (a genuine semitone value, not the old bug pattern) so the Ladder sweep is audible; two tempo-synced LFOs (1/1 morph, 1/4 cutoff) plus a Macro-1 morph knob and Gate->MorphPos keep the sustain alive between gate chops. Hall reverb (reverbType=1) and a slow 2-stage phaser move it out of the shared 'plate default'. Values are moderate (depths 0.3-0.5, res 0.35) so nothing distorts.
- **Replacement code:**
```cpp
    // "Harmony Gate" - detuned dual-saw + scalic harmonizer, twin-LFO motion
    {
        PresetDef p;
        p.name = "Harmony Gate";
        p.category = "Rhythmic";
        auto& s = p.state;
        // --- Voice: classic warm virtual-analog dual saw, wide unison feel ---
        s.global.width = 1.5f;      // wide stereo bed for the harmony voices
        s.global.spread = 0.3f;     // pan-spread the poly stack
        s.global.polyphony = 8;
        s.oscA.type = 0; s.oscA.waveform = 1; s.oscA.level = 0.75f;      // saw A
        s.oscB.type = 0; s.oscB.waveform = 1;                            // saw B
        s.oscB.fineCents = 9.0f; s.oscB.level = 0.6f;                    // +9c beating = analog thickness
        s.mixer.position = 0.45f;                                       // slight A bias
        // --- Ladder LP with a REAL filter-env sweep (env is +semitones, not Hz) ---
        s.filter.type = 4;                        // Ladder LP for warmth
        s.filter.cutoffHz = 2200.0f;              // start dark so the sweep is audible
        s.filter.resonance = 0.35f;
        s.filter.ladderSlope = 4;                 // 24 dB/oct
        s.filter.ladderDrive = 4.0f;              // gentle input push
        s.filter.envAmount = 22.0f;               // +22 st sweep (bug-fixed: NOT a Hz value)
        s.filter.keyTrack = 0.3f;                 // track pitch a little
        s.filterEnv.attackMs = 5.0f; s.filterEnv.decayMs = 260.0f;
        s.filterEnv.sustain = 0.35f; s.filterEnv.releaseMs = 400.0f;
        // --- Amp: soft pad body, long enough to hear the gate chop the sustain ---
        s.ampEnv.attackMs = 40.0f; s.ampEnv.decayMs = 400.0f;
        s.ampEnv.sustain = 0.75f; s.ampEnv.releaseMs = 550.0f;
        // --- 1/16 gate rhythm ---
        s.tranceGate.enabled = 1; s.tranceGate.numSteps = 16;
        s.tranceGate.tempoSync = 1; s.tranceGate.noteValue = kNote1_16;
        s.tranceGate.depth = 0.85f;
        s.tranceGate.attackMs = 4.0f; s.tranceGate.releaseMs = 25.0f;
        float tg[] = {1,1,0,1, 1,0,1,0, 1,1,0,1, 0,1,1,0};
        for (int i = 0; i < 16; ++i) s.tranceGate.stepLevels[i] = tg[i];
        // --- Scalic 3rd + 5th harmony, panned wide (the identity of this preset) ---
        s.harmonizerEnabled = 1;
        s.harmonizer.harmonyMode = 1;             // Scalic
        s.harmonizer.key = 0; s.harmonizer.scale = 0; // C Major
        s.harmonizer.pitchShiftMode = 1;          // Granular
        s.harmonizer.numVoices = 2;
        s.harmonizer.dryLevelDb = 0.0f; s.harmonizer.wetLevelDb = -4.0f;
        s.harmonizer.voiceInterval[0] = 2; s.harmonizer.voicePan[0] = -0.45f; // 3rd left
        s.harmonizer.voiceInterval[1] = 4; s.harmonizer.voicePan[1] = 0.45f;  // 5th right
        // --- Dual tempo-synced LFOs give the sustain constant movement ---
        s.lfo1.shape = 0; s.lfo1.depth = 1.0f; s.lfo1.sync = 1;   // sine, synced
        s.lfo1Ext.noteValue = 19;                                 // 1/1 = very slow morph drift
        s.lfo2.shape = 1; s.lfo2.depth = 1.0f; s.lfo2.sync = 1;   // triangle, synced
        s.lfo2Ext.noteValue = 13;                                 // 1/4 = mid filter shimmer
        setModSlot(s, 0, kSrcLFO1, kDstAllMorphPos, 0.4f, kCurveSCurve); // smooth morph wander
        setModSlot(s, 1, kSrcLFO2, kDstAllFltCut,  0.3f, kCurveExp);     // filter breathing
        // --- Macro 1 = one-knob timbral morph over cutoff (performance handle) ---
        s.macros.values[0] = 0.4f;
        setModSlot(s, 2, kSrcMacro1, kDstAllFltCut, 0.5f, kCurveLinear);
        // --- Gate output nudges the A/B morph so each hit has a tiny timbral flick ---
        setVoiceRoute(s, 0, kVSrcGate, kVDstMorphPos, 0.3f);
        setVoiceRoute(s, 1, kVSrcVelocity, kVDstFltCut, 0.4f);           // dynamics -> brightness
        // --- Slow phaser + a real HALL (not the reflexive plate) ---
        s.modulationType = 1;                      // Phaser
        s.phaser.rateHz = 0.25f; s.phaser.depth = 0.5f; s.phaser.feedback = 0.4f;
        s.phaser.mix = 0.35f; s.phaser.stages = 2; s.phaser.centerFreqHz = 900.0f;
        s.reverbEnabled = 1; s.reverbType = 1;     // Hall
        s.reverb.size = 0.6f; s.reverb.mix = 0.25f; s.reverb.damping = 0.4f;
        presets.push_back(std::move(p));
    }
```

## 7. "Ring Pulse" -> "Ring Pulse"
- **Locate:** the block containing `p.name = "Ring Pulse"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A hard-sync square screaming through a note-tracked ring modulator, played monophonically with glide; each 1/16 gate hit pumps the ring drive so the metallic clang re-strikes in time, bouncing across a ping-pong delay.
- **Coverage:** TranceGate numSteps + stepLevels + depth + retriggerDepth + attack/release; Gate voice-source -> DistDrive route; RingModulator distortion; PingPong delay; LFO tempo-synced; mono glide; Comb filter; engines: Sync.
- **Rationale:** The old preset was a plain PolyBLEP square; the ring-mod alone carried it. I swap OSC A to the Sync engine (syncRatio 2.5, square slave) for a genuinely metallic core, then read the directive's 'LFO->ringDepth so the clang moves between gate hits' two ways that ARE reachable in the format: a voice route Gate->DistDrive (0.6) makes the ring drive literally re-strike on every gate hit, and a tempo-synced LFO1->AllFltCut opens the Comb filter between hits so the timbre keeps shifting. Comb filter (type 6) replaces the recycled ladder-LP. Mono+glide gives it a distinct performance feel and covers the category's mono-glide quota. ringRatio 0.2 (normalized) lands an inharmonic ~3.4x carrier; drive 0.45/mix 0.55 stays aggressive without destroying level.
- **Replacement code:**
```cpp
    // "Ring Pulse" - mono hard-sync square + ring modulator, gate-pumped clang
    {
        PresetDef p;
        p.name = "Ring Pulse";
        p.category = "Rhythmic";
        auto& s = p.state;
        // --- Mono with glide: a single aggressive metallic voice ---
        s.global.voiceMode = 1;      // Mono
        s.global.polyphony = 1;
        s.global.width = 1.1f;
        s.monoMode.priority = 0;     // Last-note
        s.monoMode.legato = 1;
        s.monoMode.portamentoTimeMs = 40.0f;
        s.monoMode.portaMode = 1;    // glide only on legato
        // --- Hard-sync engine on OSC A: square master, square slave for a biting edge ---
        s.oscA.type = 3;             // Sync engine
        s.oscA.waveform = 2;         // square base tone
        s.oscA.syncRatio = 2.5f;     // sweetspot sync formant
        s.oscA.syncWaveform = 3;     // square slave = hollow/hard
        s.oscA.syncMode = 0;         // Hard sync
        s.oscA.syncAmount = 1.0f;
        s.oscA.level = 0.85f;
        s.oscB.level = 0.0f;         // single oscillator
        s.mixer.position = 0.0f;     // OSC A only
        // --- Comb filter tunes the metallic resonance instead of a plain LP ---
        s.filter.type = 6;           // Comb
        s.filter.cutoffHz = 3000.0f; // comb tuning
        s.filter.combDamping = 0.3f;
        s.filter.resonance = 0.5f;
        // --- Ring modulator: note-tracked so the clang stays musical ---
        s.distortion.type = 6;                 // Ring Modulator
        s.distortion.drive = 0.45f;
        s.distortion.ringFreqMode = 1;         // NoteTrack
        s.distortion.ringRatio = 0.2f;         // normalized -> ~3.4x ratio, inharmonic bite
        s.distortion.ringWaveform = 0;         // sine carrier
        s.distortion.ringStereoSpread = 0.4f;  // widen the metallic image
        s.distortion.character = 0.5f;
        s.distortion.mix = 0.55f;
        // --- Percussive amp, short so gate stabs read as discrete hits ---
        s.ampEnv.attackMs = 1.0f; s.ampEnv.decayMs = 180.0f;
        s.ampEnv.sustain = 0.55f; s.ampEnv.releaseMs = 90.0f;
        // --- Full-depth 1/16 gate with retrigger stutter ---
        s.tranceGate.enabled = 1; s.tranceGate.numSteps = 8;
        s.tranceGate.tempoSync = 1; s.tranceGate.noteValue = kNote1_16;
        s.tranceGate.depth = 1.0f;
        s.tranceGate.attackMs = 1.0f; s.tranceGate.releaseMs = 8.0f;
        s.tranceGate.retriggerDepth = 0.3f;    // subtle intra-step stutter
        float tg[] = {1,0,1,0, 1,1,0,1};
        for (int i = 0; i < 8; ++i) s.tranceGate.stepLevels[i] = tg[i];
        // --- Synced LFO opens the comb between hits so the timbre never sits still ---
        s.lfo1.shape = 0; s.lfo1.depth = 1.0f; s.lfo1.sync = 1;
        s.lfo1Ext.noteValue = kNote1_8;
        setModSlot(s, 0, kSrcLFO1, kDstAllFltCut, 0.5f, kCurveExp);
        // --- The GATE output drives distortion drive: clang re-strikes on every hit ---
        setVoiceRoute(s, 0, kVSrcGate, kVDstDistDrive, 0.6f);
        // --- Ping-pong echo throws the stabs across the field ---
        s.delayEnabled = 1;
        s.delay.type = 2;            // Ping-Pong
        s.delay.sync = 1; s.delay.noteValue = kNote1_8;
        s.delay.mix = 0.22f; s.delay.feedback = 0.32f;
        s.delay.pingPongWidth = 150.0f;
        presets.push_back(std::move(p));
    }
```

## 8. "Tape Groove" -> "Tape Groove"
- **Locate:** the block containing `p.name = "Tape Groove"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A wavetable saw over a square sub, driven into tape saturation and a dotted-8th tape echo, with a transient-detector opening the filter on every gate hit for a sidechain-like analog pump.
- **Coverage:** TranceGate stepLevels (velocity-graded) + depth + attack/release; Env3 voice-source -> GateDepth route + Velocity -> FltCut; transient source (sidechain feel); TapeSaturator distortion; Tape delay; filterEnv corrected; Plate reverb; engines: Wavetable + PolyBLEP sub.
- **Rationale:** The audit said strip the tape delay and it's a generic gated saw. Fix: OSC A becomes the Wavetable engine (a genuine gap in the whole bank) with a touch of self phaseMod for table movement, and the voice path gets a TapeSaturator distortion (type 5) so the 'analog' character lives in the SYNTH, not only the echo. envAmount=+18 st gives the SVF LP a working sweep (old preset had none). The standout mod identity is the Transient detector -> AllFltCut: every note onset pops the filter for a sidechain-like pump, and Env3->GateDepth slowly evolves the groove. Dotted-8th tape delay + Plate reverb (reverbType=0) keep the laid-back signature. Depths 0.3-0.4, drive 0.3 keep it warm, never harsh.
- **Replacement code:**
```cpp
    // "Tape Groove" - wavetable saw + square sub, tape saturation, transient-pumped filter
    {
        PresetDef p;
        p.name = "Tape Groove";
        p.category = "Rhythmic";
        auto& s = p.state;
        // --- OSC A: Wavetable engine (an engine nothing else in the bank uses) ---
        s.oscA.type = 1;             // Wavetable
        s.oscA.waveform = 1;         // saw table
        s.oscA.level = 0.75f;
        s.oscA.phaseMod = 0.15f;     // gentle self-PM = subtle wavetable movement
        s.oscB.type = 0; s.oscB.waveform = 2;   // square sub
        s.oscB.tuneSemitones = -12.0f; s.oscB.level = 0.45f;
        s.mixer.position = 0.35f;
        // --- SVF LP with post-drive + a corrected filter-env sweep ---
        s.filter.type = 0;           // SVF LP
        s.filter.cutoffHz = 3200.0f;
        s.filter.resonance = 0.3f;
        s.filter.svfSlope = 1;       // 24 dB
        s.filter.svfDrive = 6.0f;    // saturated LP for warmth
        s.filter.envAmount = 18.0f;  // +18 st sweep (bug-fixed semitone value)
        s.filter.keyTrack = 0.2f;
        s.filterEnv.attackMs = 8.0f; s.filterEnv.decayMs = 320.0f;
        s.filterEnv.sustain = 0.4f; s.filterEnv.releaseMs = 300.0f;
        // --- Tape-saturator distortion in the voice path for analog grit ---
        s.distortion.type = 5;       // Tape Saturator
        s.distortion.drive = 0.3f;
        s.distortion.tapeModel = 0;
        s.distortion.tapeSaturation = 0.5f;
        s.distortion.tapeBias = 0.5f;
        s.distortion.character = 0.5f;
        s.distortion.mix = 0.6f;
        // --- Laid-back amp ---
        s.ampEnv.attackMs = 3.0f; s.ampEnv.decayMs = 300.0f;
        s.ampEnv.sustain = 0.6f; s.ampEnv.releaseMs = 220.0f;
        // --- Softer 1/8 gate with a velocity-graded groove ---
        s.tranceGate.enabled = 1; s.tranceGate.numSteps = 8;
        s.tranceGate.tempoSync = 1; s.tranceGate.noteValue = kNote1_8;
        s.tranceGate.depth = 0.85f;
        s.tranceGate.attackMs = 8.0f; s.tranceGate.releaseMs = 40.0f;
        float tg[] = {1,0.7f,0.4f,1, 0.6f,0.3f,0.9f,0.5f};
        for (int i = 0; i < 8; ++i) s.tranceGate.stepLevels[i] = tg[i];
        // --- Transient detector = sidechain feel: each note-onset ducks/opens the filter ---
        s.transient.sensitivity = 0.6f; s.transient.attackMs = 2.0f; s.transient.decayMs = 60.0f;
        setModSlot(s, 0, kSrcTransient, kDstAllFltCut, 0.4f, kCurveExp);
        // --- Slow mod-env slowly evolves the gate depth over a phrase ---
        s.modEnv.attackMs = 200.0f; s.modEnv.decayMs = 800.0f;
        s.modEnv.sustain = 0.5f; s.modEnv.releaseMs = 600.0f;
        setVoiceRoute(s, 0, kVSrcEnv3, kVDstGateDepth, 0.3f);   // evolving gate depth
        setVoiceRoute(s, 1, kVSrcVelocity, kVDstFltCut, 0.3f);  // dynamics -> brightness
        // --- Warm dotted-8th tape delay, small plate room ---
        s.delayEnabled = 1;
        s.delay.type = 1;            // Tape
        s.delay.sync = 1; s.delay.noteValue = kNote1_8D;
        s.delay.mix = 0.26f; s.delay.feedback = 0.36f;
        s.delay.tapeInertiaMs = 300.0f;
        s.delay.tapeSaturation = 0.4f; s.delay.tapeWear = 0.3f;
        s.reverbEnabled = 1; s.reverbType = 0;   // Plate
        s.reverb.size = 0.4f; s.reverb.mix = 0.15f;
        presets.push_back(std::move(p));
    }
```

## 9. "Grain Scatter" -> "Grain Scatter"
- **Locate:** the block containing `p.name = "Grain Scatter"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A granular particle-swarm oscillator flung through an auto-wah env filter and a chaotic Rossler modulator, then diffused by a flanger and a pitch/pan-sprayed granular delay into a restless cloud.
- **Coverage:** TranceGate numSteps + stepLevels + depth + attack/release; Gate voice-source -> MorphPos route; chaosMod source; Flanger FX; Spectral distortion; Granular delay; Env Filter (auto-wah) filter type; Hall reverb; engines: Particle.
- **Rationale:** The audit noted all the interesting spray lived in the delay, not the voice. I move the scatter INTO the synth: OSC A becomes the Particle engine (scatter 6 st, density 24, random spawn, drift) which is a literal grain swarm and matches the name. The filter is the under-used Env Filter auto-wah (type 11, BP) so the swarm self-sweeps. The mod identity is the Chaos modulator (Rossler, depth 0.6 raised off its silent default) driving SpecTilt + Resonance for evolving instability, plus Gate->MorphPos. A Flanger (an FX absent elsewhere in this chunk) and light Spectral distortion diffuse it further before the pitch/pan-sprayed granular delay and a Hall (not the shared plate). All depths <=0.6, distortion mix 0.3 keep it a texture, not noise.
- **Replacement code:**
```cpp
    // "Grain Scatter" - particle-swarm osc, auto-wah, chaos + flanger + granular delay
    {
        PresetDef p;
        p.name = "Grain Scatter";
        p.category = "Rhythmic";
        auto& s = p.state;
        s.global.width = 1.4f; s.global.spread = 0.4f;
        // --- OSC A: Particle engine - a literal grain swarm (on-theme) ---
        s.oscA.type = 6;                    // Particle
        s.oscA.particleScatter = 6.0f;      // wide freq scatter = detuned cloud
        s.oscA.particleDensity = 24.0f;     // dense swarm
        s.oscA.particleLifetime = 120.0f;   // medium grains
        s.oscA.particleSpawnMode = 1;       // Random spawn = organic
        s.oscA.particleEnvType = 0;         // Hann grains
        s.oscA.particleDrift = 0.3f;        // slow pitch wander
        s.oscA.level = 0.7f;
        s.oscB.type = 0; s.oscB.waveform = 1;   // saw glue underneath
        s.oscB.fineCents = 6.0f; s.oscB.level = 0.45f;
        s.mixer.position = 0.4f;
        // --- Env Filter (auto-wah): the swarm self-sweeps on its own envelope ---
        s.filter.type = 11;                 // Env Filter
        s.filter.cutoffHz = 800.0f;         // base of the wah
        s.filter.resonance = 2.0f;
        s.filter.envSubType = 1;            // BP wah
        s.filter.envSensitivity = 6.0f;
        s.filter.envDepth = 0.8f;
        s.filter.envAttack = 8.0f; s.filter.envRelease = 180.0f;
        s.filter.envDirection = 0;          // sweep up
        // --- Amp ---
        s.ampEnv.attackMs = 3.0f; s.ampEnv.decayMs = 300.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 300.0f;
        // --- Busy 1/16 gate ---
        s.tranceGate.enabled = 1; s.tranceGate.numSteps = 16;
        s.tranceGate.tempoSync = 1; s.tranceGate.noteValue = kNote1_16;
        s.tranceGate.depth = 0.9f;
        s.tranceGate.attackMs = 2.0f; s.tranceGate.releaseMs = 15.0f;
        float tg[] = {1,0,0.5f,1, 0,1,0,0.7f, 1,0.3f,0,1, 0.8f,0,1,0};
        for (int i = 0; i < 16; ++i) s.tranceGate.stepLevels[i] = tg[i];
        // --- Chaos modulator (Rossler) evolves spectral tilt + resonance unpredictably ---
        s.chaosMod.type = 1;                // Rossler
        s.chaosMod.rateHz = 0.8f;
        s.chaosMod.depth = 0.6f;            // MUST be > 0 or the source is silent
        s.chaosMod.sync = 0;
        setModSlot(s, 0, kSrcChaos, kDstAllSpecTilt,  0.4f, kCurveLinear);
        setModSlot(s, 1, kSrcChaos, kDstAllResonance, 0.3f, kCurveLinear);
        // --- Gate flicks the A/B morph so grains re-color per hit ---
        setVoiceRoute(s, 0, kVSrcGate, kVDstMorphPos, 0.35f);
        // --- Light spectral distortion smears the cloud further ---
        s.distortion.type = 2;              // Spectral
        s.distortion.drive = 0.25f;
        s.distortion.spectralMode = 0;
        s.distortion.mix = 0.3f;
        // --- Flanger jet-swirl (an FX no other preset here uses) ---
        s.modulationType = 2;               // Flanger
        s.flanger.rateHz = 0.3f; s.flanger.depth = 0.6f; s.flanger.feedback = 0.2f;
        s.flanger.mix = 0.4f; s.flanger.stereoSpread = 120.0f;
        // --- Pitch/pan-sprayed granular delay = the diffuse scatter tail ---
        s.delayEnabled = 1;
        s.delay.type = 3;                   // Granular
        s.delay.mix = 0.32f; s.delay.feedback = 0.3f;
        s.delay.granularSizeMs = 60.0f; s.delay.granularDensity = 20.0f;
        s.delay.granularPitchSpray = 0.3f; s.delay.granularPanSpray = 0.6f;
        s.reverbEnabled = 1; s.reverbType = 1;  // Hall
        s.reverb.size = 0.55f; s.reverb.mix = 0.22f;
        presets.push_back(std::move(p));
    }
```

## 10. "Euclidean Bells" -> "Euclidean Bells"
- **Locate:** the block containing `p.name = "Euclidean Bells"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** An inharmonic 48-partial additive bell with a bright peak-filter ping and an octave-sine shimmer, struck on a Euclidean 7-in-16 pattern, ringing long through a ping-pong echo and plate with LFO-swept spectral tilt.
- **Coverage:** TranceGate depth + attack/release + Euclidean gate mode; Velocity -> FltCut voice route; Additive engine; SVF Peak filter; filterEnv corrected; LFO -> SpecTilt (tempo-synced); PingPong delay; Plate reverb; engines: Additive + PolyBLEP sine.
- **Rationale:** Already the category standout (only Additive/Euclidean preset), so I keep that DNA and fix its 'partials are static, can't evolve' flaw. envAmount=+30 st through an SVF Peak filter (type 8, +6 dB gain, res 3) gives a bright metallic ping on every strike the old ladder-LP couldn't; an octave-up sine adds shimmer. The mod identity is a tempo-synced LFO1->AllSpecTilt (SCurve) that slowly tilts the partial balance so the long 1.6s decay actually breathes, plus Velocity->FltCut for playable dynamics. Ping-pong delay + a Plate reverb (reverbType=0, distinct from Ring Pulse which uses no reverb) keep the tail wide. Euclidean E(7,16) preserved as the rhythmic signature. Feedback 0.32/mix values stay musical, no runaway.
- **Replacement code:**
```cpp
    // "Euclidean Bells" - inharmonic additive bell on a Euclidean gate, long ringing tail
    {
        PresetDef p;
        p.name = "Euclidean Bells";
        p.category = "Rhythmic";
        auto& s = p.state;
        // --- OSC A: Additive engine, inharmonic partials = struck-metal timbre ---
        s.oscA.type = 4;                    // Additive
        s.oscA.additivePartials = 48;       // rich partial stack
        s.oscA.additiveInharm = 0.35f;      // stretched/inharmonic = bell
        s.oscA.additiveTilt = -2.0f;        // gently darker top
        s.oscA.level = 0.7f;
        s.oscB.type = 0; s.oscB.waveform = 0;   // pure sine
        s.oscB.tuneSemitones = 12.0f;       // octave-up shimmer
        s.oscB.level = 0.28f;
        s.mixer.position = 0.2f;            // mostly the additive bell
        // --- SVF Peak filter accents the strike band; env gives a bright transient ping ---
        s.filter.type = 8;                  // SVF Peak
        s.filter.cutoffHz = 2500.0f;
        s.filter.resonance = 3.0f;
        s.filter.svfGain = 6.0f;            // +6 dB peak on the bell band
        s.filter.svfSlope = 1;
        s.filter.envAmount = 30.0f;         // +30 st bright ping on attack (bug-fixed value)
        s.filterEnv.attackMs = 1.0f; s.filterEnv.decayMs = 500.0f;
        s.filterEnv.sustain = 0.1f; s.filterEnv.releaseMs = 800.0f;
        // --- Bell amp: instant strike, long ringing decay, near-zero sustain ---
        s.ampEnv.attackMs = 1.0f; s.ampEnv.decayMs = 1600.0f;
        s.ampEnv.sustain = 0.12f; s.ampEnv.releaseMs = 1200.0f;
        // --- Euclidean 7-in-16 gate = the rhythmic signature ---
        s.tranceGate.enabled = 1; s.tranceGate.numSteps = 16;
        s.tranceGate.tempoSync = 1; s.tranceGate.noteValue = kNote1_16;
        s.tranceGate.depth = 1.0f;
        s.tranceGate.attackMs = 1.0f; s.tranceGate.releaseMs = 5.0f;
        s.tranceGate.euclideanEnabled = 1;
        s.tranceGate.euclideanHits = 7;     // E(7,16)
        s.tranceGate.euclideanRotation = 0;
        // --- Slow synced LFO drifts spectral tilt so the ringing tail shimmers ---
        s.lfo1.shape = 0; s.lfo1.depth = 1.0f; s.lfo1.sync = 1;
        s.lfo1Ext.noteValue = 16;           // 1/2 note = slow shimmer
        setModSlot(s, 0, kSrcLFO1, kDstAllSpecTilt, 0.3f, kCurveSCurve);
        // --- Harder strikes ring brighter ---
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.5f);
        // --- Ping-pong echo + plate keep the bells ringing across the stereo field ---
        s.delayEnabled = 1;
        s.delay.type = 2;                   // Ping-Pong
        s.delay.sync = 1; s.delay.noteValue = kNote1_8;
        s.delay.mix = 0.28f; s.delay.feedback = 0.32f;
        s.delay.pingPongWidth = 160.0f;
        s.reverbEnabled = 1; s.reverbType = 0;  // Plate
        s.reverb.size = 0.7f; s.reverb.mix = 0.32f; s.reverb.damping = 0.35f;
        presets.push_back(std::move(p));
    }
```

## 11. "Wobble Gate" -> "Wobble Gate"
- **Locate:** the block containing `p.name = "Wobble Gate"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** Sub-octave dubstep growl: two detuned VA oscillators an octave down through a driven resonant ladder, cutoff wobbled by a 1/4 triangle LFO while a 1/8 sine morphs timbre, chopped by a 4-step gate whose output crunches tape saturation and whose transients duck the master for a sidechain pump.
- **Coverage:** TranceGate 4-step + stepLevels + depth + rate + attack/release; Gate voice-source -> DistDrive route; Gate voice-source -> MorphPos route; transient source (sidechain feel); TapeSaturator distortion; filterEnv corrected; dual LFO tempo-synced; mono glide.
- **Rationale:** Cutoff starts at 700 Hz so the 1/4 triangle LFO on AllFltCut produces the classic dubstep sweep; the fixed +26 st filter-env adds a per-note pluck the original lacked. Two synced LFOs with different note values (1/4 vs 1/8) and different curves keep the timbre from repeating. Gate->DistDrive and Gate->MorphPos make the tape crunch and morph track the rhythm. Transient->MasterVol at -0.18 is the sidechain pump. masterGain 0.9 compensates for ladderDrive 3.0 + tape drive. Coverage tag corrected: the code sets only distortion.type=5 (TapeSaturator, verified DistortionState in ruinae_preset_format.h:236); RingModulator (type 6) and SpectralDistortion (type 2) are NOT exercised here, so the tag now lists only TapeSaturator to avoid falsely counting those areas as covered.
- **Replacement code:**
```cpp
    // "Wobble Gate" - Sub-octave dubstep growl; the mod-matrix exemplar
    {
        PresetDef p;
        p.name = "Wobble Gate";
        p.category = "Rhythmic";
        auto& s = p.state;
        // Two VA oscillators an octave down; saw + square for a fat sub-growl
        s.oscA.type = 0; s.oscA.waveform = 1; // Sawtooth
        s.oscA.tuneSemitones = -12.0f; s.oscA.level = 0.85f;
        s.oscB.type = 0; s.oscB.waveform = 3; // Pulse/Square adds hollow body
        s.oscB.tuneSemitones = -12.0f; s.oscB.pulseWidth = 0.35f; // thinner = more edge
        s.oscB.fineCents = -7.0f; s.oscB.level = 0.55f; // slight beat for analog thickness
        s.mixer.position = 0.45f;
        // Driven resonant ladder is the wobble's voice
        s.filter.type = 4; // Ladder
        s.filter.cutoffHz = 700.0f; // starts low so the LFO opens it dramatically
        s.filter.resonance = 0.55f;
        s.filter.ladderSlope = 4; s.filter.ladderDrive = 3.0f;
        // FIXED filter-env: plain semitones (was the category's 4000 bug elsewhere).
        // +26 st per-note snap gives each gate hit a plucky attack under the wobble.
        s.filter.envAmount = 26.0f;
        s.filterEnv.attackMs = 4.0f; s.filterEnv.decayMs = 180.0f;
        s.filterEnv.sustain = 0.2f; s.filterEnv.releaseMs = 120.0f;
        s.filterEnv.decayCurve = 0.4f; // punchy exponential drop
        s.ampEnv.attackMs = 2.0f; s.ampEnv.decayMs = 200.0f;
        s.ampEnv.sustain = 0.85f; s.ampEnv.releaseMs = 120.0f;
        // Dual tempo-synced LFOs, different shapes/rates => movement that never lines up
        s.lfo1.rateHz = 2.0f; s.lfo1.shape = 1; s.lfo1.depth = 1.0f; s.lfo1.sync = 1; // Triangle
        s.lfo1Ext.noteValue = kNote1_4;   // the main wobble tempo
        setModSlot(s, 0, kSrcLFO1, kDstAllFltCut, 0.65f, kCurveSCurve);
        s.lfo2.rateHz = 1.0f; s.lfo2.shape = 0; s.lfo2.depth = 0.5f; s.lfo2.sync = 1; // Sine
        s.lfo2Ext.noteValue = kNote1_8;   // faster timbral shimmer on the morph
        setModSlot(s, 1, kSrcLFO2, kDstAllMorphPos, 0.4f, kCurveExp);
        // Sidechain feel: transient detector ducks master on each note attack
        s.transient.sensitivity = 0.7f; s.transient.attackMs = 2.0f; s.transient.decayMs = 90.0f;
        setModSlot(s, 2, kSrcTransient, kDstMasterVol, -0.18f);
        // Gate output drives BOTH distortion crunch and morph => rhythmically alive
        setVoiceRoute(s, 0, kVSrcGate, kVDstDistDrive, 0.35f);
        setVoiceRoute(s, 1, kVSrcGate, kVDstMorphPos, 0.25f);
        s.distortion.type = 5; // Tape Saturator
        s.distortion.drive = 0.25f; s.distortion.tapeSaturation = 0.45f; s.distortion.tapeBias = 0.55f;
        s.tranceGate.enabled = 1;
        s.tranceGate.numSteps = 4;
        s.tranceGate.tempoSync = 1; s.tranceGate.noteValue = kNote1_16;
        s.tranceGate.depth = 0.75f;
        s.tranceGate.attackMs = 2.0f; s.tranceGate.releaseMs = 20.0f;
        s.tranceGate.stepLevels[0] = 1.0f;  s.tranceGate.stepLevels[1] = 0.35f;
        s.tranceGate.stepLevels[2] = 0.85f; s.tranceGate.stepLevels[3] = 0.5f;
        s.global.voiceMode = 1; // mono for a focused bass
        s.global.masterGain = 0.9f; // headroom for the drive + resonance
        s.monoMode.legato = 1; s.monoMode.portamentoTimeMs = 30.0f; // slides between wobbles
        presets.push_back(std::move(p));
    }
```

## 12. "Spectral Chop" -> "Spectral Chop"
- **Locate:** the block containing `p.name = "Spectral Chop"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** Frozen-spectrum stutter: a bright spectral-freeze drone over a dark hollow additive layer, its cutoff sweeping on every 16-step chop while a slow free-running LFO tilts the whole spectrum, washed through a Hall so the fragments smear into a cathedral.
- **Coverage:** SpectralFreeze engine; Additive engine; TranceGate 16-step + stepLevels + full depth + attack/release; LFO -> AllSpecTilt (free-running, non-repeating gesture); filterEnv corrected; reverb (Hall variant).
- **Rationale:** The original relied entirely on engine + gate and was otherwise a default patch. Adding a +30 st filter-env means every chop sweeps, and a 0.12 Hz free-running LFO on AllSpecTilt makes the frozen spectrum drift so the drone evolves over many bars. OscB switches to the never-used Additive engine (8 partials, -8 dB/oct) for a dark organ bed. reverbType 1 (Hall, preDelay 20 ms) diversifies the wrapper away from the suite's default small plate.
- **Replacement code:**
```cpp
    // "Spectral Chop" - Frozen-spectrum stutter with evolving spectral tilt
    {
        PresetDef p;
        p.name = "Spectral Chop";
        p.category = "Rhythmic";
        auto& s = p.state;
        // Spectral-freeze drone is the timbral core
        s.oscA.type = 8; // Spectral Freeze
        s.oscA.spectralTilt = 2.0f;  // bright, glassy top
        s.oscA.spectralPitch = 0.0f;
        s.oscA.spectralFormant = 3.0f; // shift formants up for a vocal-ish sheen
        s.oscA.level = 0.8f;
        // Dark additive under-layer (few partials, downward tilt) fills the low-mid
        s.oscB.type = 4; // Additive
        s.oscB.additivePartials = 8; s.oscB.additiveTilt = -8.0f; // mellow, organ-like
        s.oscB.additiveInharm = 0.05f; // faint bell edge
        s.oscB.tuneSemitones = -12.0f; s.oscB.level = 0.3f;
        s.mixer.position = 0.3f; // favour the spectral engine
        // SVF LP with a real filter-env so each chop MOVES (fixes the inert original)
        s.filter.type = 0; // SVF LP
        s.filter.cutoffHz = 3000.0f; s.filter.resonance = 0.3f;
        s.filter.envAmount = 30.0f; // +30 st sweep per note
        s.filterEnv.attackMs = 5.0f; s.filterEnv.decayMs = 260.0f;
        s.filterEnv.sustain = 0.3f; s.filterEnv.releaseMs = 300.0f;
        s.ampEnv.attackMs = 5.0f; s.ampEnv.decayMs = 300.0f;
        s.ampEnv.sustain = 0.75f; s.ampEnv.releaseMs = 250.0f;
        // Slow FREE-RUNNING LFO tilts the spectrum -> the drone evolves across bars
        s.lfo1.rateHz = 0.12f; s.lfo1.shape = 0; s.lfo1.depth = 1.0f; s.lfo1.sync = 0; // Sine, free
        setModSlot(s, 0, kSrcLFO1, kDstAllSpecTilt, 0.5f, kCurveSCurve);
        // Hard 16-step chop, full depth, near-instant edges = stutter
        s.tranceGate.enabled = 1;
        s.tranceGate.numSteps = 16;
        s.tranceGate.tempoSync = 1; s.tranceGate.noteValue = kNote1_16;
        s.tranceGate.depth = 1.0f;
        s.tranceGate.attackMs = 1.0f; s.tranceGate.releaseMs = 5.0f;
        {
            float tg[16] = {1,1,0,0, 1,0,1,1, 0,1,1,0, 1,1,0,1};
            for (int i = 0; i < 16; ++i) s.tranceGate.stepLevels[i] = tg[i];
        }
        // Hall reverb (not the default plate) smears fragments into a cathedral
        s.reverbEnabled = 1;
        s.reverbType = 1; // Hall
        s.reverb.size = 0.65f; s.reverb.mix = 0.28f; s.reverb.damping = 0.35f;
        s.reverb.preDelayMs = 20.0f;
        presets.push_back(std::move(p));
    }
```

## 13. "Ping Pong Lead" -> "Bounce Lead"
- **Locate:** the block containing `p.name = "Ping Pong Lead"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** Detuned dual-saw mono lead with legato glide, its cutoff breathing on a synced 1/8 LFO, thickened by a slow deep flanger and thrown across the stereo field by a dotted-1/8 ping-pong delay.
- **Coverage:** PingPong delay; Flanger FX; mono glide; dual LFO tempo-synced; filterEnv corrected; reverb.
- **Rationale:** The original was flagged as the weakest identity (near-template saw pair + one delay). It now owns the never-used Flanger (slow 0.3 Hz, feedback 0.45, 120 deg spread) as a second stereo motion source alongside the dotted-1/8 ping-pong, plus a synced 1/8 LFO breathing the cutoff and a fixed +18 st filter-env pluck. svfDrive 3 dB adds edge; wider 11 ct detune and 25 ms legato make it a playable lead rather than a template.
- **Replacement code:**
```cpp
    // "Bounce Lead" (was Ping Pong Lead) - flanged dual-saw lead bouncing in stereo
    {
        PresetDef p;
        p.name = "Bounce Lead";
        p.category = "Rhythmic";
        auto& s = p.state;
        // Classic detuned saw pair, but earns its identity from FX + motion
        s.oscA.type = 0; s.oscA.waveform = 1; s.oscA.level = 0.8f;
        s.oscB.type = 0; s.oscB.waveform = 1;
        s.oscB.fineCents = 11.0f; s.oscB.level = 0.55f; // wider detune than a plain pad
        s.mixer.position = 0.4f;
        s.filter.type = 0; // SVF LP
        s.filter.cutoffHz = 4200.0f; s.filter.resonance = 0.28f;
        s.filter.svfDrive = 3.0f; // a little grit on the lead
        // FIXED filter-env: +18 st gives the lead a plucky opening on each note
        s.filter.envAmount = 18.0f;
        s.filterEnv.attackMs = 4.0f; s.filterEnv.decayMs = 200.0f;
        s.filterEnv.sustain = 0.4f; s.filterEnv.releaseMs = 180.0f;
        s.ampEnv.attackMs = 3.0f; s.ampEnv.decayMs = 220.0f;
        s.ampEnv.sustain = 0.6f; s.ampEnv.releaseMs = 200.0f;
        // Synced LFO gently breathes the cutoff so sustained notes never sit still
        s.lfo1.rateHz = 1.0f; s.lfo1.shape = 1; s.lfo1.depth = 0.5f; s.lfo1.sync = 1; // Triangle
        s.lfo1Ext.noteValue = kNote1_8;
        setModSlot(s, 0, kSrcLFO1, kDstAllFltCut, 0.25f);
        // FLANGER (the category's never-used modulation FX) = deep swirling body
        s.modulationType = 2; // Flanger
        s.flanger.rateHz = 0.3f; s.flanger.depth = 0.65f; s.flanger.feedback = 0.45f;
        s.flanger.mix = 0.4f; s.flanger.stereoSpread = 120.0f; s.flanger.waveform = 1; // Triangle
        // Dotted-1/8 ping-pong delay bounces the lead across the field
        s.delayEnabled = 1;
        s.delay.type = 2; // PingPong
        s.delay.sync = 1; s.delay.noteValue = kNote1_8D;
        s.delay.mix = 0.35f; s.delay.feedback = 0.42f;
        s.delay.pingPongWidth = 180.0f;
        s.delay.pingPongModDepth = 0.15f; s.delay.pingPongModRateHz = 0.5f;
        // Light plate keeps it from sounding dry between echoes
        s.reverbEnabled = 1; s.reverbType = 0; // Plate
        s.reverb.size = 0.3f; s.reverb.mix = 0.15f;
        s.global.voiceMode = 1;
        s.monoMode.legato = 1; s.monoMode.portamentoTimeMs = 25.0f; // expressive glides
        presets.push_back(std::move(p));
    }
```

## 14. "Chaos Beat" -> "Rossler Riot"
- **Locate:** the block containing `p.name = "Chaos Beat"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** An unstable Rossler-attractor tone against a triangle, its cutoff and resonance jerked around in stepped jumps by a dedicated chaos modulator, carved into a 5-in-8 Euclidean rhythm and blurred through a granular delay cloud.
- **Coverage:** Chaos (Rossler) engine; chaosMod source; mod-matrix Chaos -> FltCut (Stepped curve) + Chaos -> Resonance; Euclidean gate E(5,8); Granular delay; filterEnv corrected; dual LFO tempo-synced.
- **Rationale:** Renamed for identity. Keeps the Rossler chaos engine and chaosMod source but sharpens both: chaosCoupling 0.2 destabilises the tone, and the matrix now uses TWO chaos routes with a distinctive Stepped curve on cutoff (jerky filter jumps no sibling repeats) plus chaos->resonance. The delay moves from default-ish digital to a Granular cloud (covers granular delay). Fixed +24 st filter-env with +0.3 decay curve snaps each E(5,8) hit. masterGain 0.85 tames chaos spikes.
- **Replacement code:**
```cpp
    // "Rossler Riot" (was Chaos Beat) - chaos-driven Euclidean stutter
    {
        PresetDef p;
        p.name = "Rossler Riot";
        p.category = "Rhythmic";
        auto& s = p.state;
        // Chaos oscillator (Rossler attractor) = the unstable, ever-shifting core
        s.oscA.type = 5; s.oscA.chaosAttractor = 1; // Rossler
        s.oscA.chaosAmount = 0.45f; s.oscA.chaosCoupling = 0.2f; // extra cross-axis instability
        s.oscA.chaosOutput = 0; s.oscA.level = 0.7f;
        s.oscB.type = 0; s.oscB.waveform = 4; // Triangle anchors pitch under the chaos
        s.oscB.level = 0.45f;
        s.mixer.position = 0.35f;
        s.filter.type = 0; // SVF LP
        s.filter.cutoffHz = 2500.0f; s.filter.resonance = 0.35f;
        // FIXED filter-env: +24 st fast decay makes each Euclidean hit snap
        s.filter.envAmount = 24.0f;
        s.filterEnv.attackMs = 3.0f; s.filterEnv.decayMs = 160.0f;
        s.filterEnv.sustain = 0.25f; s.filterEnv.releaseMs = 150.0f;
        s.filterEnv.decayCurve = 0.3f;
        s.ampEnv.attackMs = 2.0f; s.ampEnv.decayMs = 200.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 160.0f;
        // Dedicated chaos modulator feeds the matrix as kSrcChaos
        s.chaosMod.rateHz = 3.0f; s.chaosMod.type = 0; s.chaosMod.depth = 0.6f; s.chaosMod.sync = 0; // Lorenz
        // Chaos -> cutoff in STEPPED jumps (its signature gesture) + chaos -> resonance
        setModSlot(s, 0, kSrcChaos, kDstAllFltCut, 0.5f, kCurveStepped);
        setModSlot(s, 1, kSrcChaos, kDstAllResonance, 0.3f, kCurveLinear);
        // A slow synced LFO adds a second, orderly layer of cutoff motion for contrast
        s.lfo1.rateHz = 0.5f; s.lfo1.shape = 1; s.lfo1.depth = 0.4f; s.lfo1.sync = 1; // Triangle
        s.lfo1Ext.noteValue = kNote1_4;
        setModSlot(s, 2, kSrcLFO1, kDstAllMorphPos, 0.3f);
        // Euclidean E(5,8) gate = the world-rhythm signature (only Euclidean member)
        s.tranceGate.enabled = 1;
        s.tranceGate.numSteps = 8;
        s.tranceGate.tempoSync = 1; s.tranceGate.noteValue = kNote1_16;
        s.tranceGate.depth = 0.85f;
        s.tranceGate.attackMs = 2.0f; s.tranceGate.releaseMs = 15.0f;
        s.tranceGate.euclideanEnabled = 1; s.tranceGate.euclideanHits = 5; s.tranceGate.euclideanRotation = 0;
        // Granular delay smears the chaotic hits into a texture cloud
        s.delayEnabled = 1;
        s.delay.type = 3; // Granular
        s.delay.sync = 1; s.delay.noteValue = kNote1_8;
        s.delay.mix = 0.3f; s.delay.feedback = 0.35f;
        s.delay.granularSizeMs = 80.0f; s.delay.granularDensity = 20.0f;
        s.delay.granularPitch = 0.0f; s.delay.granularTexture = 0.3f; s.delay.granularWidth = 1.0f;
        s.global.masterGain = 0.85f; // chaos can spike; keep headroom
        presets.push_back(std::move(p));
    }
```

## 15. "Vocal Sequence" -> "Vowel Sequencer"
- **Locate:** the block containing `p.name = "Vocal Sequence"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A saw+triangle pair sung through a formant filter whose vowel-formants slide up and down on a synced sawtooth LFO (real formant-shift motion, not the old no-op morph route), gated into an 8-step talking rhythm with a touch of plate.
- **Coverage:** Formant filter; LFO -> formant shift (AllFltCut on Formant filter, synced); filterEnv corrected (formant shift attack); TranceGate 8-step + stepLevels; reverb (Plate).
- **Rationale:** The original's LFO->MorphPos never swept the vowel (MorphPos = osc A/B crossfade, confirmed in ruinae_voice.h). This version routes the synced saw LFO to AllFltCut, which on a Formant filter maps to formant-shift (voice.h:1394-1397) producing genuine vocal 'wow' motion, reinforced by a fixed +18 st filter-env shift on each attack. formantMorph 1.6 + gender +0.3 pick a bright articulate vowel. Everything else (gate pattern, small plate) supports diction rather than washing it out.
- **Replacement code:**
```cpp
    // "Vowel Sequencer" (was Vocal Sequence) - talking formant sequence
    {
        PresetDef p;
        p.name = "Vowel Sequencer";
        p.category = "Rhythmic";
        auto& s = p.state;
        s.oscA.type = 0; s.oscA.waveform = 1; s.oscA.level = 0.75f; // Saw = rich source for formants
        s.oscB.type = 0; s.oscB.waveform = 4; s.oscB.level = 0.4f;  // Triangle softens the top
        s.mixer.position = 0.35f;
        // Formant filter: pick a bright vowel and shade it a little female
        s.filter.type = 5; // Formant
        s.filter.formantMorph = 1.6f;   // between E and I -> present, articulate
        s.filter.formantGender = 0.3f;  // slightly higher formants
        // NOTE: for the Formant filter, 'cutoff' maps to FORMANT SHIFT (semitones).
        // 1000 Hz = 0 semitone shift baseline; the LFO/env below move it => vowel motion.
        s.filter.cutoffHz = 1000.0f;
        // FIXED filter-env in real semitones: +18 st shift gives a 'wow' on each attack
        s.filter.envAmount = 18.0f;
        s.filterEnv.attackMs = 8.0f; s.filterEnv.decayMs = 220.0f;
        s.filterEnv.sustain = 0.4f; s.filterEnv.releaseMs = 200.0f;
        s.ampEnv.attackMs = 4.0f; s.ampEnv.decayMs = 280.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 220.0f;
        // Synced saw LFO -> AllFltCut = continuous formant-shift sweep = 'talking'.
        // (The old preset routed LFO->MorphPos, which only crossfades A/B and does NOT
        //  move the vowel; AllFltCut on a Formant filter is the correct lever.)
        s.lfo1.rateHz = 1.0f; s.lfo1.shape = 2; s.lfo1.depth = 1.0f; s.lfo1.sync = 1; // Saw
        s.lfo1Ext.noteValue = kNote1_4;
        setModSlot(s, 0, kSrcLFO1, kDstAllFltCut, 0.5f, kCurveSCurve);
        // 8-step gate carves the vowel phrase into a rhythmic sequence
        s.tranceGate.enabled = 1;
        s.tranceGate.numSteps = 8;
        s.tranceGate.tempoSync = 1; s.tranceGate.noteValue = kNote1_16;
        s.tranceGate.depth = 0.9f;
        s.tranceGate.attackMs = 3.0f; s.tranceGate.releaseMs = 20.0f;
        {
            float tg[8] = {1.0f, 0.5f, 1.0f, 0.2f, 0.85f, 0.4f, 1.0f, 0.3f};
            for (int i = 0; i < 8; ++i) s.tranceGate.stepLevels[i] = tg[i];
        }
        // Small plate adds a hint of room without washing out the diction
        s.reverbEnabled = 1; s.reverbType = 0; // Plate
        s.reverb.size = 0.35f; s.reverb.mix = 0.2f; s.reverb.damping = 0.5f;
        presets.push_back(std::move(p));
    }
```

## 16. "Sidechain Wash" -> "Sidechain Wash"
- **Locate:** the block containing `p.name = "Sidechain Wash"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A wide tape-saturated dual-saw pad that breathes: each note-attack ducks the master (transient sidechain), a rising 4-step ramp pumps it, and a fifth/octave harmonizer washes it into a slow Hall.
- **Coverage:** TranceGate numSteps + stepLevels + depth + rate + attack/release; Gate voice-source -> MorphPos route; transient source (sidechain feel); TapeSaturator distortion; Tape delay; harmonizer; dual LFO tempo-synced; filterEnv corrected; reverb (Hall); mod matrix (3 slots).
- **Rationale:** Keeps the directive's dual-saw wash but rebuilds its identity from mod, not tone. The transient->MasterVol slot (amount -0.7, Exp curve) is the literal sidechain duck the directive asks for and is the category's second transient user. envAmount goes from the old inaudible values to +16 semitones (a real swell). TapeSaturator (type 5) + Tape delay give a cohesive tape voice no other rhythmic preset owns; the harmonizer 5th+octave and dual tempo-synced LFOs (1/2 and 1/4) plus a Hall (reverbType 1) keep it far from the plate-drenched default wash. Gate->MorphPos adds per-step timbral pulse.
- **Replacement code:**
```cpp
// "Sidechain Wash" - Transient-ducked tape pad pumped by a rising gate ramp
    {
        PresetDef p;
        p.name = "Sidechain Wash";
        p.category = "Rhythmic";
        auto& s = p.state;
        // Dual detuned saws, splayed in cents for a wide analog bed
        s.oscA.type = 0; s.oscA.waveform = 1; s.oscA.fineCents = -6.0f; s.oscA.level = 0.70f;
        s.oscB.type = 0; s.oscB.waveform = 1; s.oscB.fineCents = 7.0f;  s.oscB.level = 0.60f;
        s.mixer.mode = 0; s.mixer.position = 0.5f;        // centre crossfade; LFO1 sweeps it
        // SVF LP with a slow, AUDIBLE filter-env swell (envAmount is in SEMITONES)
        s.filter.type = 0; s.filter.cutoffHz = 2400.0f; s.filter.resonance = 0.35f;
        s.filter.envAmount = 16.0f; s.filter.keyTrack = 0.2f; s.filter.svfDrive = 3.0f;
        // Slow pad amp + a long gradual filter swell for the wash
        s.ampEnv.attackMs = 180.0f; s.ampEnv.decayMs = 600.0f;
        s.ampEnv.sustain = 0.85f; s.ampEnv.releaseMs = 1200.0f; s.ampEnv.attackCurve = 0.4f;
        s.filterEnv.attackMs = 500.0f; s.filterEnv.decayMs = 900.0f;
        s.filterEnv.sustain = 0.45f; s.filterEnv.releaseMs = 900.0f;
        // Rising 4-step 1/4 gate ramp = the pump shape
        s.tranceGate.enabled = 1; s.tranceGate.numSteps = 4;
        s.tranceGate.tempoSync = 1; s.tranceGate.noteValue = kNote1_4;
        s.tranceGate.depth = 1.0f; s.tranceGate.attackMs = 25.0f; s.tranceGate.releaseMs = 140.0f;
        s.tranceGate.stepLevels[0] = 0.0f; s.tranceGate.stepLevels[1] = 0.45f;
        s.tranceGate.stepLevels[2] = 0.75f; s.tranceGate.stepLevels[3] = 1.0f;
        // TapeSaturator (type 5) for the warm, glued tape colour
        s.distortion.type = 5; s.distortion.drive = 0.35f; s.distortion.character = 0.6f;
        s.distortion.mix = 0.8f; s.distortion.tapeModel = 0;
        s.distortion.tapeSaturation = 0.55f; s.distortion.tapeBias = 0.45f;
        // IDENTITY: transient detector ducks the master -> sidechain-pump feel
        // (transient is unipolar; negative amount dips level on each attack then recovers)
        s.transient.sensitivity = 0.8f; s.transient.attackMs = 2.0f; s.transient.decayMs = 180.0f;
        setModSlot(s, 0, kSrcTransient, kDstMasterVol, -0.7f, kCurveExp);
        // Dual TEMPO-SYNCED LFOs keep the timbre in motion under sustain
        s.lfo1.rateHz = 0.5f; s.lfo1.shape = 0; s.lfo1.depth = 0.8f; s.lfo1.sync = 1;
        s.lfo1Ext.noteValue = 16; // 1/2 note slow morph drift
        s.lfo2.rateHz = 0.75f; s.lfo2.shape = 1; s.lfo2.depth = 0.5f; s.lfo2.sync = 1;
        s.lfo2Ext.noteValue = 13; // 1/4 note cutoff shimmer
        setModSlot(s, 1, kSrcLFO1, kDstAllMorphPos, 0.6f, kCurveSCurve);
        setModSlot(s, 2, kSrcLFO2, kDstAllFltCut, 0.25f, kCurveLinear);
        // Gate output also opens the A/B morph per step for extra rhythmic motion
        setVoiceRoute(s, 0, kVSrcGate, kVDstMorphPos, 0.4f);
        // Harmonizer: a fifth + octave shimmer thickens the wash
        s.harmonizerEnabled = 1; s.harmonizer.harmonyMode = 1; // scalic
        s.harmonizer.numVoices = 2; s.harmonizer.wetLevelDb = -10.0f;
        s.harmonizer.voiceInterval[0] = 7;  s.harmonizer.voiceLevelDb[0] = -6.0f; s.harmonizer.voicePan[0] = -0.4f;
        s.harmonizer.voiceInterval[1] = 12; s.harmonizer.voiceLevelDb[1] = -9.0f; s.harmonizer.voicePan[1] = 0.4f;
        // Light TAPE delay, then a big Hall reverb
        s.delayEnabled = 1; s.delay.type = 1; // Tape
        s.delay.sync = 1; s.delay.noteValue = kNote1_8; s.delay.feedback = 0.3f; s.delay.mix = 0.18f;
        s.delay.tapeSaturation = 0.4f; s.delay.tapeWear = 0.2f;
        s.reverbEnabled = 1; s.reverbType = 1; // Hall
        s.reverb.size = 0.8f; s.reverb.mix = 0.32f; s.reverb.damping = 0.35f; s.reverb.preDelayMs = 20.0f;
        s.global.width = 1.5f; s.global.spread = 0.4f; s.global.polyphony = 8;
        presets.push_back(std::move(p));
    }
```

## 17. "Phase Pulse" -> "Phase Pulse"
- **Locate:** the block containing `p.name = "Phase Pulse"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A vocal, formant-filtered saw/triangle pulse swept by a tempo-synced phaser under an 8-step gate, its vowel brightness wandering endlessly on a slow Rossler chaos LFO.
- **Coverage:** TranceGate numSteps + stepLevels + depth + rate + attack/release; Gate voice-source -> GateDepth route; Phaser FX; Formant filter (type 5); chaosMod source; filterEnv corrected; dual LFO (chaos + synced LFO1); mod matrix (2 slots).
- **Rationale:** Honors the directive (saw + tri, syncing phaser, 8-step gate) but swaps the plain SVF LP for the Formant filter (type 5, morph 1.5, gender -0.3) so it literally says vowels — a filter type zero other rhythmic presets touch. The chaosMod (Rossler) -> AllFltCut slot is a mod identity no sibling repeats: the vowel brightness drifts chaotically. filterEnv is a real +20 st sweep. Gate->GateDepth makes the pulse breathe. Distinct from Sidechain Wash despite both being saw-based because filter, FX and mod story are entirely different.
- **Replacement code:**
```cpp
// "Phase Pulse" - Formant-vowel pulse, phaser-swept, chaos-wandered
    {
        PresetDef p;
        p.name = "Phase Pulse";
        p.category = "Rhythmic";
        auto& s = p.state;
        // Saw + a triangle sub-voice for body
        s.oscA.type = 0; s.oscA.waveform = 1; s.oscA.level = 0.78f;
        s.oscB.type = 0; s.oscB.waveform = 4; s.oscB.fineCents = 5.0f; s.oscB.level = 0.5f; // Triangle
        s.mixer.position = 0.4f;
        // FORMANT filter (type 5) => vocal aah/ooh pulses instead of the usual LP monoculture
        s.filter.type = 5; s.filter.cutoffHz = 1200.0f; s.filter.resonance = 0.6f;
        s.filter.formantMorph = 1.5f;    // between E and I
        s.filter.formantGender = -0.3f;  // slightly larger throat
        s.filter.envAmount = 20.0f;      // filterEnv sweeps the vowel (semitones, corrected)
        s.ampEnv.attackMs = 40.0f; s.ampEnv.decayMs = 400.0f;
        s.ampEnv.sustain = 0.75f; s.ampEnv.releaseMs = 450.0f;
        s.filterEnv.attackMs = 8.0f; s.filterEnv.decayMs = 350.0f;
        s.filterEnv.sustain = 0.3f; s.filterEnv.releaseMs = 250.0f;
        // Syncing PHASER (modulation slot) = the swept-pulse identity
        s.modulationType = 1;
        s.phaser.rateHz = 2.0f; s.phaser.depth = 0.75f; s.phaser.feedback = 0.6f;
        s.phaser.mix = 0.55f; s.phaser.stages = 3; s.phaser.centerFreqHz = 900.0f;
        s.phaser.stereoSpread = 120.0f; s.phaser.sync = 1; s.phaser.noteValue = kNote1_4;
        // 8-step 1/8 gate
        s.tranceGate.enabled = 1; s.tranceGate.numSteps = 8;
        s.tranceGate.tempoSync = 1; s.tranceGate.noteValue = kNote1_8;
        s.tranceGate.depth = 0.8f; s.tranceGate.attackMs = 10.0f; s.tranceGate.releaseMs = 45.0f;
        float tg[] = {1.0f,0.5f,0.85f,0.3f, 1.0f,0.6f,0.9f,0.25f};
        for (int i = 0; i < 8; ++i) s.tranceGate.stepLevels[i] = tg[i];
        // chaosMod source (Rossler, free-run) wanders the vowel brightness -> never repeats
        s.chaosMod.rateHz = 0.4f; s.chaosMod.type = 1; s.chaosMod.depth = 0.7f; s.chaosMod.sync = 0;
        setModSlot(s, 0, kSrcChaos, kDstAllFltCut, 0.4f, kCurveSCurve);
        // A slow synced LFO wobbles resonance on the opposite curve for a talking motion
        s.lfo1.rateHz = 0.5f; s.lfo1.shape = 2; s.lfo1.depth = 0.4f; s.lfo1.sync = 1;
        s.lfo1Ext.noteValue = 13; // 1/4 note
        setModSlot(s, 1, kSrcLFO1, kDstAllResonance, 0.3f, kCurveExp);
        // Gate output opens its own depth for a breathing pulse envelope
        setVoiceRoute(s, 0, kVSrcGate, kVDstGateDepth, 0.5f);
        s.global.width = 1.4f;
        presets.push_back(std::move(p));
    }
```

## 18. "Complex Pattern" -> "Fractal Grid"
- **Locate:** the block containing `p.name = "Complex Pattern"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A metallic ring-modulated saw+sub pluck machine-gunned across a dense 32-step 1/16 grid that a Euclidean E(23,32) mask carves into a shifting techno pattern, echoed by a gritty synced digital delay.
- **Coverage:** TranceGate numSteps + stepLevels + depth + rate + attack/release; Euclidean gate mode; Gate voice-source -> DistDrive route; RingModulator distortion; Digital delay; filterEnv corrected; ladder filter.
- **Rationale:** Renamed from the generic 'Complex Pattern'. Fixes the audit's tiny envAmount=12 to +22 st on a real Ladder filter (was mislabelled), so the pluck now chirps. The RingModulator (type 6, NoteTrack, octave ratio) gives it a metallic bell identity absent everywhere else, and Gate->DistDrive makes that metal pulse with the grid. euclideanEnabled with E(23,32) is the category's Euclidean-gate owner and keeps the dense 32-step pattern evolving. Digital delay (type 0, dotted-eighth, era 1) replaces the shared mix=0.15/fb=0.2 delay that made the old rhythmic presets same-ish.
- **Replacement code:**
```cpp
// "Fractal Grid" - Ring-modulated pluck on a dense Euclidean 32-step grid
    {
        PresetDef p;
        p.name = "Fractal Grid";
        p.category = "Rhythmic";
        auto& s = p.state;
        // Saw + sub-triangle an octave down = punchy pluck body with weight
        s.oscA.type = 0; s.oscA.waveform = 1; s.oscA.level = 0.72f;
        s.oscB.type = 0; s.oscB.waveform = 4; s.oscB.tuneSemitones = -12.0f; s.oscB.level = 0.55f;
        s.mixer.position = 0.38f;
        // Ladder LP (type 4) with a STRONG corrected filter env (semitones, +22)
        s.filter.type = 4; s.filter.cutoffHz = 900.0f; s.filter.resonance = 0.5f;
        s.filter.envAmount = 22.0f; s.filter.ladderSlope = 4; s.filter.ladderDrive = 4.0f;
        // Punchy pluck amp; fast snappy filter env
        s.ampEnv.attackMs = 2.0f; s.ampEnv.decayMs = 220.0f;
        s.ampEnv.sustain = 0.55f; s.ampEnv.releaseMs = 160.0f; s.ampEnv.decayCurve = 0.4f;
        s.filterEnv.attackMs = 1.0f; s.filterEnv.decayMs = 170.0f;
        s.filterEnv.sustain = 0.15f; s.filterEnv.releaseMs = 120.0f; s.filterEnv.decayCurve = 0.5f;
        // RingModulator (type 6), note-tracked octave => metallic bell edge on each pluck
        s.distortion.type = 6; s.distortion.drive = 0.3f; s.distortion.mix = 0.4f;
        s.distortion.ringFreqMode = 1;      // NoteTrack
        s.distortion.ringRatio = 0.1111f;   // normalized -> ~2.0 (octave sidebands)
        s.distortion.ringWaveform = 1;      // Triangle carrier (softer than square)
        s.distortion.ringStereoSpread = 0.3f;
        // Dense 32-step 1/16 gate, further carved by a EUCLIDEAN mask E(23,32)
        s.tranceGate.enabled = 1; s.tranceGate.numSteps = 32;
        s.tranceGate.tempoSync = 1; s.tranceGate.noteValue = kNote1_16;
        s.tranceGate.depth = 0.95f; s.tranceGate.attackMs = 1.0f; s.tranceGate.releaseMs = 8.0f;
        s.tranceGate.euclideanEnabled = 1; s.tranceGate.euclideanHits = 23; s.tranceGate.euclideanRotation = 3;
        float tg32[] = {
            1,0.6f,0.8f,0.4f, 1,0.7f,0.5f,0.9f, 1,0.5f,0.85f,0.6f, 0.7f,1,0.4f,0.8f,
            1,0.6f,0.9f,0.5f, 0.8f,1,0.6f,0.7f, 1,0.5f,0.8f,0.9f, 0.6f,1,0.5f,0.85f
        };
        for (int i = 0; i < 32; ++i) s.tranceGate.stepLevels[i] = tg32[i];
        // Gate output pumps the ring-mod drive -> the metallic edge tracks the rhythm
        setVoiceRoute(s, 0, kVSrcGate, kVDstDistDrive, 0.5f);
        // Synced DIGITAL delay with vintage age + a wider image
        s.delayEnabled = 1; s.delay.type = 0; // Digital
        s.delay.sync = 1; s.delay.noteValue = kNote1_8D; s.delay.mix = 0.2f; s.delay.feedback = 0.35f;
        s.delay.digitalEra = 1; s.delay.digitalAge = 0.3f; s.delay.digitalWidth = 140.0f;
        s.global.width = 1.3f;
        presets.push_back(std::move(p));
    }
```

## 19. "Ratchet Fury" -> "Ratchet Fury"
- **Locate:** the block containing `p.name = "Ratchet Fury"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A resonant phase-distortion CZ lead played mono with legato glide, machine-gunned by an up-arp with a 4x ratchet lane and heavy ratchet-swing, jet-flanged and bounced across a ping-pong delay.
- **Coverage:** arp ratchet lane (up to 4x) + ratchetSwing; arp velocity lane; PhaseDistortion engine (type 2); Flanger FX; PingPong delay; mono glide (portamento); filterEnv corrected; Gate/velocity voice route; dual LFO (synced) + mod matrix.
- **Rationale:** This was the audit's worst offender - a bare setSynthLead with only arp lanes for identity. Now it fully abandons the template: both oscs are PhaseDistortion (type 2, ResSaw + Pulse PD) for a resonant CZ voice, mono legato glide (portamento 45ms) makes the 4x ratchet arp slide like a 303, and Flanger (modulationType 2) plus PingPong delay (type 2) give it FX no sibling shares. filterEnv fixed to +26 st so ratchet bursts chirp; velocity->cutoff accents the lane. Keeps the strong ratchet lane {1,2,1,4,1,3,4,2} and ratchetSwing 66 that make it the category's ratchet owner.
- **Replacement code:**
```cpp
// "Ratchet Fury" - Phase-distortion lead machine-gunned by a 4x ratchet arp
    {
        PresetDef p;
        p.name = "Ratchet Fury";
        p.category = "Rhythmic";
        auto& s = p.state;
        // OFF the saw-lead template: PHASE DISTORTION engine (resonant CZ voices)
        s.oscA.type = 2; s.oscA.pdWaveform = 5; s.oscA.pdDistortion = 0.7f; s.oscA.level = 0.85f; // ResSaw
        s.oscB.type = 2; s.oscB.pdWaveform = 2; s.oscB.pdDistortion = 0.55f; // Pulse PD
        s.oscB.tuneSemitones = -12.0f; s.oscB.fineCents = 4.0f; s.oscB.level = 0.5f;
        s.mixer.position = 0.42f;
        // Ladder LP with an audible env sweep so each ratchet burst chirps
        s.filter.type = 4; s.filter.cutoffHz = 1500.0f; s.filter.resonance = 0.45f;
        s.filter.envAmount = 26.0f; s.filter.ladderDrive = 5.0f; s.filter.keyTrack = 0.4f;
        s.ampEnv.attackMs = 3.0f; s.ampEnv.decayMs = 180.0f;
        s.ampEnv.sustain = 0.55f; s.ampEnv.releaseMs = 140.0f;
        s.filterEnv.attackMs = 2.0f; s.filterEnv.decayMs = 120.0f;
        s.filterEnv.sustain = 0.25f; s.filterEnv.releaseMs = 100.0f; s.filterEnv.decayCurve = 0.4f;
        // MONO with legato glide => 303-style slides between arp notes
        s.global.voiceMode = 1; s.global.polyphony = 1;
        s.monoMode.priority = 0; s.monoMode.legato = 1;
        s.monoMode.portamentoTimeMs = 45.0f; s.monoMode.portaMode = 1; // legato-only glide
        // ARP with the heavy RATCHET lane (up to 4x) - the identity of this preset
        setArpEnabled(s, true); setArpMode(s, kModeUp); setTempoSync(s, true);
        setArpRate(s, kNote1_16); setArpGateLength(s, 65.0f); s.arp.octaveRange = 2;
        float vel8[] = {1.0f, 0.55f, 0.8f, 0.5f, 1.0f, 0.65f, 0.9f, 0.45f};
        setVelocityLane(s, 8, vel8);
        int32_t ratch8[] = {1, 2, 1, 4, 1, 3, 4, 2};
        setRatchetLane(s, 8, ratch8);
        s.arp.ratchetSwing = 66.0f;
        // FLANGER (modulation slot) gives the ratchet rolls a jet-sweep tail
        s.modulationType = 2;
        s.flanger.rateHz = 0.3f; s.flanger.depth = 0.7f; s.flanger.feedback = 0.5f;
        s.flanger.mix = 0.45f; s.flanger.stereoSpread = 110.0f;
        // Velocity drives cutoff so accented arp steps open up
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.5f);
        // A slow synced LFO breathes the resonance across bars
        s.lfo1.rateHz = 0.25f; s.lfo1.shape = 0; s.lfo1.depth = 0.4f; s.lfo1.sync = 1;
        s.lfo1Ext.noteValue = 19; // whole note
        setModSlot(s, 0, kSrcLFO1, kDstAllResonance, 0.3f, kCurveSCurve);
        // PINGPONG delay bounces the ratchet bursts across the stereo field
        s.delayEnabled = 1; s.delay.type = 2; // PingPong
        s.delay.sync = 1; s.delay.noteValue = kNote1_8; s.delay.mix = 0.22f; s.delay.feedback = 0.35f;
        s.delay.pingPongWidth = 150.0f; s.delay.pingPongCrossFeed = 0.8f;
        presets.push_back(std::move(p));
    }
```

## 20. "Glitch Step" -> "Glitch Step"
- **Locate:** the block containing `p.name = "Glitch Step"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A bit-crushed triangle-and-blue-noise stab, its cutoff and spectral tilt scrambled in hard stepped jumps by a free S&H LFO and a Sample&Hold source, chopped by a hard 16-step gate with stutter retrigger and smeared through a granular delay cloud.
- **Coverage:** TranceGate numSteps + stepLevels + depth + rate + attack/release + retriggerDepth; Gate voice-source -> DistDrive route; Spectral distortion (bitcrush); Noise engine (type 9); Sample&Hold source; dual LFO (free S&H + synced); Granular delay; mod matrix (3 slots, Stepped curve); filterEnv corrected.
- **Rationale:** Already the least samey preset; this deepens it without changing its soul. Adds a second (synced tri) LFO for the dual-LFO requirement, a Sample&Hold source into AllSpecTilt (both S&H-family sources now used), and switches both scramble slots to the Stepped curve so the modulation lands in hard quantized jumps rather than smooth sweeps - true glitch motion. retriggerDepth 0.6 adds stutter, Gate->DistDrive pumps the bitcrush rhythmically, and the shared mix=0.15/fb=0.2 digital delay is replaced by a Granular delay (type 3, pitch-spray + reverse) that turns the tail into a cloud. filterEnv corrected to +18 st.
- **Replacement code:**
```cpp
// "Glitch Step" - Bit-crushed noise stabs, S&H-scrambled under a hard gate
    {
        PresetDef p;
        p.name = "Glitch Step";
        p.category = "Rhythmic";
        auto& s = p.state;
        // Triangle body + a layer of BLUE noise for digital hiss
        s.oscA.type = 0; s.oscA.waveform = 4; s.oscA.level = 0.7f;   // Triangle
        s.oscB.type = 9; s.oscB.noiseColor = 3; s.oscB.level = 0.18f; // Blue noise
        s.mixer.position = 0.18f;
        s.filter.type = 0; s.filter.cutoffHz = 3800.0f; s.filter.resonance = 0.3f;
        s.filter.envAmount = 18.0f; // corrected semitone sweep
        // SPECTRAL distortion as a bitcrusher/decimator
        s.distortion.type = 2; s.distortion.drive = 0.4f; s.distortion.mix = 0.85f;
        s.distortion.spectralMode = 3; s.distortion.spectralCurve = 4; s.distortion.spectralBits = 0.3f;
        s.ampEnv.attackMs = 1.0f; s.ampEnv.decayMs = 140.0f;
        s.ampEnv.sustain = 0.55f; s.ampEnv.releaseMs = 70.0f;
        s.filterEnv.attackMs = 1.0f; s.filterEnv.decayMs = 120.0f; s.filterEnv.sustain = 0.2f;
        // Hard 16-step 1/16 gate with RETRIGGER DEPTH for stutter accents
        s.tranceGate.enabled = 1; s.tranceGate.numSteps = 16;
        s.tranceGate.tempoSync = 1; s.tranceGate.noteValue = kNote1_16;
        s.tranceGate.depth = 1.0f; s.tranceGate.attackMs = 0.5f; s.tranceGate.releaseMs = 3.0f;
        s.tranceGate.retriggerDepth = 0.6f;
        float tg[] = {1,0,0,1, 0,1,1,0, 1,1,0,0, 0,1,1,1};
        for (int i = 0; i < 16; ++i) s.tranceGate.stepLevels[i] = tg[i];
        // DUAL LFOs: lfo1 free S&H scrambles cutoff, lfo2 synced pushes the morph
        s.lfo1.rateHz = 8.0f; s.lfo1.shape = 4; s.lfo1.depth = 0.6f; s.lfo1.sync = 0; // free S&H
        s.lfo2.rateHz = 2.0f; s.lfo2.shape = 1; s.lfo2.depth = 0.5f; s.lfo2.sync = 1; // synced tri
        s.lfo2Ext.noteValue = 10; // 1/8
        setModSlot(s, 0, kSrcLFO1, kDstAllFltCut, 0.45f, kCurveStepped);  // stepped => hard glitch jumps
        setModSlot(s, 1, kSrcLFO2, kDstAllMorphPos, 0.4f, kCurveSCurve);
        // Sample & Hold source scrambles the spectral tilt for extra digital chatter
        s.sampleHold.rateHz = 6.0f; s.sampleHold.sync = 0; s.sampleHold.slewMs = 2.0f;
        setModSlot(s, 2, kSrcSampleHold, kDstAllSpecTilt, 0.5f, kCurveStepped);
        // Gate output pumps the bitcrush drive on every hit
        setVoiceRoute(s, 0, kVSrcGate, kVDstDistDrive, 0.5f);
        // GRANULAR delay smears the stutters into a glitch cloud
        s.delayEnabled = 1; s.delay.type = 3; // Granular
        s.delay.sync = 1; s.delay.noteValue = kNote1_16; s.delay.mix = 0.22f; s.delay.feedback = 0.3f;
        s.delay.granularSizeMs = 80.0f; s.delay.granularDensity = 20.0f;
        s.delay.granularPitchSpray = 0.2f; s.delay.granularReverseProb = 0.25f;
        s.global.width = 1.3f;
        presets.push_back(std::move(p));
    }
```
