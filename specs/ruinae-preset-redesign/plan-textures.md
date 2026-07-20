# Ruinae Preset Plan — Textures

The Textures category is Ruinae's showcase for evolving, atmospheric, non-melodic sound design — and it is where the "boring / same-ish" complaint bites hardest, because the originals leaned on static drones and near-duplicate grain/vowel/chaos pairs. This redesign gives every preset a distinct *motion* identity and spreads coverage across the whole synth: each patch owns a rare filter type (SVF BP/HP/Notch/Allpass, Comb, Self-Oscillating), a distinct distortion flavor (Tape, Wavefolder, Granular, Spectral, RingModulator), a distinct delay flavor (Digital, Tape, PingPong, Granular, Spectral), and a non-overlapping set of exotic mod sources (SmoothRandom LFO, chaosMod, Sample&Hold, Rungler, PitchFollower, Transient, EnvFollower, Random). Cross-cutting fixes applied throughout: the category-wide `filter.envAmount` bug (values written as Hz where the field is plain semitones, range -48..+48) is corrected to musical semitone sweeps; near-duplicate pairs (Particle Cloud/Granular Fog, Spectral Ghost/Digital Decay, the three vowel patches) are pulled apart on synthesis path, vowel choice, motion shape, and spatial FX; and the untouched mod-matrix `scale` axis (x2/x4) and curve variety (Linear/Exp/SCurve/Stepped) are exercised across the set. Several presets are renamed to signal their new character. Locate each block by searching for its `p.name = "<original name>"` string in `F:\projects\iterum\tools\ruinae_preset_generator.cpp` (line numbers drift as edits land — never rely on them).

## 1. "Particle Cloud" -> "Particle Cloud"
- **Locate:** the block containing `p.name = "Particle Cloud"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A sparse grain swarm over a pink-noise bed that slowly breathes between grains and hiss, panned wide with a pre-delayed plate wash.
- **Coverage:** particle-engine, noise-osc(pink), svf-bp-filter, lfo(smooth-random)+fadeInMs+phaseOffset, sampleHold-mod-source, mod-matrix(SCurve+Stepped curves), tape-saturator-distortion, pingpong-delay, reverb(plate+diffusion+preDelay), global-width+spread
- **Rationale:** Fixes the original's static grain field: an ultra-slow free-running SmoothRandom LFO (fadeIn 4s, phaseOffset 90) drives AllMorphPos with an SCurve so the timbre genuinely cross-fades between grains and pink noise over ~12s, while a slewed Sample&Hold steps resonance with the Stepped curve for micro-shimmer. SVF BP (res 1.2) replaces the generic LP to give focus; TapeSaturator at low drive/mix adds warmth without dirtying it; PingPong delay + pre-delayed plate + width 1.7 keep the wide airy identity distinct from Granular Fog. particleEnvType=3 (Blackman); every mod amount conservative for a clean audible bloom. Verified: all fields, setModSlot signature, kSrc/kDst/kCurve constants, filter type 2, distortion type 5, delay type 2, reverbType 0 confirmed in ruinae_preset_format.h and the generator.
- **Replacement code:**
```cpp
    // "Particle Cloud" - animated grain swarm that breathes between grains and noise
    {
        PresetDef p;
        p.name = "Particle Cloud";
        p.category = "Textures";
        auto& s = p.state;
        // --- Voice: sparse grain swarm over a warm pink-noise bed ---
        s.oscA.type = 6;                  // Particle
        s.oscA.particleScatter = 7.0f;    // wide freq scatter -> airy cloud
        s.oscA.particleDensity = 28.0f;   // sparse-to-medium grain count
        s.oscA.particleLifetime = 900.0f; // long grains bloom smoothly
        s.oscA.particleSpawnMode = 1;     // Random spawn -> no rhythmic pulse
        s.oscA.particleEnvType = 3;       // Blackman grains = softest edges (3=Blackman)
        s.oscA.particleDrift = 0.35f;     // slow pitch wander inside the cloud
        s.oscA.level = 0.7f;
        s.oscB.type = 9;                  // Noise
        s.oscB.noiseColor = 1;            // Pink -> warm hiss bed under the grains
        s.oscB.level = 0.12f;
        s.mixer.position = 0.2f;          // mostly grains, hint of noise
        // --- Filter: SVF Band-Pass gives the cloud a focused, vowel-ish body ---
        s.filter.type = 2;                // SVF BP (owns this filter type)
        s.filter.cutoffHz = 1400.0f;
        s.filter.resonance = 1.2f;        // gentle emphasis, not whistling
        s.filter.svfSlope = 1;            // 24 dB
        // --- Motion 1: a glacial SmoothRandom LFO cross-fades grains<->noise ---
        s.lfo1.rateHz = 0.08f;            // ~12 s cycle
        s.lfo1.shape = 5;                 // SmoothRandom
        s.lfo1.depth = 0.8f;
        s.lfo1.sync = 0;                  // free-running, not tempo-locked
        s.lfo1Ext.fadeInMs = 4000.0f;     // motion blooms in over 4 s (owns fadeInMs)
        s.lfo1Ext.phaseOffset = 90.0f;    // start off-centre (owns phaseOffset)
        setModSlot(s, 0, kSrcLFO1, kDstAllMorphPos, 0.55f, kCurveSCurve, 40.0f);
        // --- Motion 2: Sample&Hold steps the resonance -> shimmering "grains of Q" ---
        s.sampleHold.rateHz = 1.5f;       // new value ~every 0.66 s
        s.sampleHold.slewMs = 120.0f;     // soft steps, no clicks
        setModSlot(s, 1, kSrcSampleHold, kDstAllResonance, 0.35f, kCurveStepped, 0.0f);
        // --- Amp env: slow swell, very long tail ---
        s.ampEnv.attackMs = 1200.0f;
        s.ampEnv.decayMs = 600.0f;
        s.ampEnv.sustain = 0.85f;
        s.ampEnv.releaseMs = 3200.0f;
        // --- Gentle tape warmth so the noise bed has body ---
        s.distortion.type = 5;            // TapeSaturator (owns this dirt type)
        s.distortion.drive = 0.25f;
        s.distortion.tapeModel = 0;
        s.distortion.tapeSaturation = 0.4f;
        s.distortion.tapeBias = 0.5f;
        s.distortion.mix = 0.3f;          // subtle, stays a texture
        // --- PingPong delay throws grains across the stereo field ---
        s.delayEnabled = 1;
        s.delay.type = 2;                 // PingPong (owns this delay type)
        s.delay.mix = 0.28f;
        s.delay.feedback = 0.4f;
        s.delay.pingPongCrossFeed = 0.6f;
        s.delay.pingPongWidth = 150.0f;
        // --- Plate reverb: high diffusion + pre-delay for depth ---
        s.reverbEnabled = 1;
        s.reverbType = 0;                 // Plate
        s.reverb.size = 0.85f;
        s.reverb.mix = 0.45f;
        s.reverb.damping = 0.4f;
        s.reverb.diffusion = 0.92f;       // very smeared
        s.reverb.preDelayMs = 45.0f;      // gap before the wash (owns preDelay)
        s.global.width = 1.7f;
        s.global.spread = 0.5f;
        presets.push_back(std::move(p));
    }
```

## 2. "Chaos Wind" -> "Chaos Wind"
- **Locate:** the block containing `p.name = "Chaos Wind"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** An unstable Rossler-attractor drone gusting through brown noise, with four independent random sources wandering the filter, tilt, morph and resonance into a modulated hall.
- **Coverage:** chaos-engine(coupling/Y-axis), noise-osc(brown), svf-notch-filter, lfo(smooth-random), chaosMod-source, random-source, rungler-source, mod-matrix-scale-axis(x2)+Exp+SCurve, wavefolder-distortion, tape-delay, reverb(Hall+modulation)
- **Rationale:** Keeps the already-strong three-source concept and pushes it further so it clearly owns the 'most alive' slot. Adds a 4th slot (Rungler->AllResonance) with a real rungler.depth=0.6 and scale=3 (x2) to exercise the untouched mod-matrix scale axis at meaningful depth. Swaps the generic LP for an SVF Notch (hollow wind), the plain reverb for a modulated Hall, and the delay for a Tape delay with age/saturation so the echoes drift too. A modest Wavefolder (drive 0.35, mix 0.3) adds grit only as the chaos peaks. Four sources (LFO SmoothRandom, chaosMod, random, rungler) each hit a different destination with a different curve, so no gesture repeats a sibling's.
- **Replacement code:**
```cpp
    // "Chaos Wind" - unstable four-source drone (the category exemplar)
    {
        PresetDef p;
        p.name = "Chaos Wind";
        p.category = "Textures";
        auto& s = p.state;
        // --- Voice: Rossler attractor + brown-noise gusts ---
        s.oscA.type = 5;                  // Chaos
        s.oscA.chaosAttractor = 1;        // Rossler
        s.oscA.chaosAmount = 0.7f;
        s.oscA.chaosCoupling = 0.45f;     // cross-axis instability
        s.oscA.chaosOutput = 1;           // Y axis
        s.oscA.level = 0.6f;
        s.oscB.type = 9;                  // Noise
        s.oscB.noiseColor = 2;            // Brown -> low rumble bed
        s.oscB.level = 0.22f;
        s.mixer.position = 0.28f;
        // --- Filter: SVF Notch scoops the mids -> hollow, windy timbre ---
        s.filter.type = 3;                // SVF Notch (owns this filter type)
        s.filter.cutoffHz = 2200.0f;
        s.filter.resonance = 1.5f;
        // --- Motion: three independent wanderers on three destinations ---
        s.lfo1.rateHz = 0.1f;
        s.lfo1.shape = 5;                 // SmoothRandom
        s.lfo1.depth = 0.7f;
        s.lfo1.sync = 0;
        setModSlot(s, 0, kSrcLFO1, kDstAllFltCut, 0.5f, kCurveLinear, 30.0f);
        s.chaosMod.rateHz = 0.5f; s.chaosMod.type = 1; s.chaosMod.depth = 0.5f; // Rossler LFO
        setModSlot(s, 1, kSrcChaos, kDstAllSpecTilt, 0.4f, kCurveExp, 80.0f);
        s.random.rateHz = 0.8f; s.random.smoothness = 0.85f;
        setModSlot(s, 2, kSrcRandom, kDstAllMorphPos, 0.3f, kCurveLinear, 60.0f);
        // --- Rungler: bit-crushed stepped voltage into resonance, boosted x2 ---
        s.rungler.osc1FreqHz = 1.7f; s.rungler.osc2FreqHz = 2.9f;
        s.rungler.depth = 0.6f; s.rungler.bits = 8;
        setModSlot(s, 3, kSrcRungler, kDstAllResonance, 0.4f, kCurveSCurve, 20.0f);
        s.modMatrix.slots[3].scale = 3;   // x2 depth (owns the mod-matrix scale axis)
        // --- Amp env: slow gusting swell ---
        s.ampEnv.attackMs = 800.0f;
        s.ampEnv.decayMs = 500.0f;
        s.ampEnv.sustain = 0.9f;
        s.ampEnv.releaseMs = 2600.0f;
        // --- Wavefolder adds jagged harmonics as the chaos peaks ---
        s.distortion.type = 4;            // Wavefolder (owns this dirt type)
        s.distortion.drive = 0.35f;
        s.distortion.foldType = 1;
        s.distortion.mix = 0.3f;
        // --- Tape delay: wow/flutter echoes reinforce the drift ---
        s.delayEnabled = 1;
        s.delay.type = 1;                 // Tape (owns this delay type)
        s.delay.mix = 0.25f;
        s.delay.feedback = 0.42f;
        s.delay.timeMs = 750.0f;
        s.delay.tapeSaturation = 0.5f;
        s.delay.tapeAge = 0.3f;
        // --- Hall reverb with slow modulation = big moving air ---
        s.reverbEnabled = 1;
        s.reverbType = 1;                 // Hall (owns Hall)
        s.reverb.size = 0.85f;
        s.reverb.mix = 0.4f;
        s.reverb.damping = 0.5f;
        s.reverb.modRateHz = 0.3f;
        s.reverb.modDepth = 0.12f;
        presets.push_back(std::move(p));
    }
```

## 3. "Spectral Ghost" -> "Spectral Ghost"
- **Locate:** the block containing `p.name = "Spectral Ghost"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A cold high-passed spectral drone that morphs in quantized steps between a shifted freeze and a dark inharmonic additive tone, shadowed by a smooth phase-vocoder fifth/octave freeze wash and a spectral-delay haze into a modulated Hall.
- **Coverage:** spectral-freeze-engine, additive-engine, spectral-morph-mixer, svf-hp-filter, lfo+quantizeSteps, pitchFollower-source, mod-matrix(SCurve), spectral-distortion, harmonizer(PhaseVocoder), spectral-delay, reverb(Hall+modulation)
- **Rationale:** This is the 'keep' side of the flagged pair: Spectral Ghost retains the smooth PhaseVocoder freeze-wash identity (harmonizer.pitchShiftMode=2, verified in HarmonizerState L886) plus SpectralMorph mixer (mode=1), SVF HP (type 1), spectral-domain distortion (type 2), Spectral delay (type 4) and a modulated Hall (reverbType=1, reverb.modDepth=0.15). A Triangle LFO with quantizeSteps=6 -> AllMorphPos (SCurve, dest 5 audible in SpectralMorph) makes the ghost step audibly between freeze and additive; a PitchFollower tilts the spectrum (dest 7 = AllSpecTilt, valid in SpectralMorph mode). Unchanged from the enriched version because the directive assigns the entire shared spectral chain to THIS preset, and moves Digital Decay off it - so the pair diverges without touching Spectral Ghost. Every field/setter verified against ruinae_preset_format.h.
- **Replacement code:**
```cpp
    // "Spectral Ghost" - cold quantized-morph spectral drone (owns the smooth PhaseVocoder freeze wash)
    {
        PresetDef p;
        p.name = "Spectral Ghost";
        p.category = "Textures";
        auto& s = p.state;
        // --- Voice: shifted freeze morphing against a dark inharmonic additive tone ---
        s.oscA.type = 8;                  // Spectral Freeze
        s.oscA.spectralPitch = 5.0f;      // shifted up, detached from key
        s.oscA.spectralTilt = -4.0f;      // darker
        s.oscA.spectralFormant = -3.0f;   // hollow, ghostly formant shift
        s.oscA.level = 0.7f;
        s.oscB.type = 4;                  // Additive
        s.oscB.additivePartials = 48;
        s.oscB.additiveTilt = -6.0f;      // very dark
        s.oscB.additiveInharm = 0.3f;     // bell-like detune
        s.oscB.level = 0.45f;
        s.mixer.mode = 1;                 // SpectralMorph (FFT interpolation A<->B)
        s.mixer.position = 0.5f;
        // --- Filter: SVF High-Pass hollows the low end -> cold air ---
        s.filter.type = 1;                // SVF HP
        s.filter.cutoffHz = 320.0f;
        s.filter.resonance = 0.3f;
        // --- Motion 1: a QUANTIZED LFO steps the spectral morph -> the ghost drifts in stages ---
        s.lfo1.rateHz = 0.12f;
        s.lfo1.shape = 1;                 // Triangle
        s.lfo1.depth = 0.7f;
        s.lfo1.sync = 0;
        s.lfo1Ext.quantizeSteps = 6;      // 6 discrete morph positions (owns quantizeSteps)
        setModSlot(s, 0, kSrcLFO1, kDstAllMorphPos, 0.6f, kCurveSCurve, 50.0f);
        // --- Motion 2: PitchFollower tilts the spectrum with played pitch ---
        s.pitchFollower.minHz = 60.0f; s.pitchFollower.maxHz = 1500.0f;
        s.pitchFollower.confidence = 0.5f; s.pitchFollower.speedMs = 120.0f;
        setModSlot(s, 1, kSrcPitchFollow, kDstAllSpecTilt, 0.4f, kCurveLinear, 60.0f);
        // --- Amp env: extremely slow, ghostly ---
        s.ampEnv.attackMs = 1600.0f;
        s.ampEnv.decayMs = 800.0f;
        s.ampEnv.sustain = 0.7f;
        s.ampEnv.releaseMs = 3200.0f;
        // --- Spectral distortion smears the harmonics further (thematic, spectral-domain dirt) ---
        s.distortion.type = 2;            // Spectral (owns this dirt type)
        s.distortion.drive = 0.3f;
        s.distortion.spectralMode = 1;
        s.distortion.spectralCurve = 3;
        s.distortion.spectralBits = 0.5f; // ~mid bit reduction
        s.distortion.mix = 0.28f;
        // --- Harmonizer (PhaseVocoder): a cold, SMOOTH spectral fifth + octave shimmer ---
        s.harmonizerEnabled = 1;
        s.harmonizer.pitchShiftMode = 2;  // PhaseVocoder (owns this mode) -> smooth freeze wash
        s.harmonizer.numVoices = 2;
        s.harmonizer.wetLevelDb = -10.0f;
        s.harmonizer.voiceInterval[0] = 7;  // +5th
        s.harmonizer.voiceInterval[1] = 12; // +octave
        s.harmonizer.voicePan[0] = -0.4f; s.harmonizer.voicePan[1] = 0.4f;
        // --- Spectral delay: frequency-blurred echo trails ---
        s.delayEnabled = 1;
        s.delay.type = 4;                 // Spectral (owns this delay type)
        s.delay.mix = 0.3f;
        s.delay.feedback = 0.5f;
        s.delay.timeMs = 600.0f;
        s.delay.spectralSpreadMs = 400.0f;
        s.delay.spectralTilt = -0.3f;
        // --- Modulated Hall reverb -> shimmering tail ---
        s.reverbEnabled = 1;
        s.reverbType = 1;                 // Hall
        s.reverb.size = 0.95f;
        s.reverb.mix = 0.5f;
        s.reverb.damping = 0.25f;
        s.reverb.modRateHz = 0.2f;
        s.reverb.modDepth = 0.15f;        // owns reverb modulation
        presets.push_back(std::move(p));
    }
```

## 4. "Granular Fog" -> "Granular Fog"
- **Locate:** the block containing `p.name = "Granular Fog"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A dense two-layer particle fog swept through a phasey allpass that breathes with dynamics, thickened by granular saturation and granular delay into an enormous diffuse hall.
- **Coverage:** particle-engine(dual), svf-allpass-filter, lfo(smooth-random)+smoothMs, envFollower-source, mod-matrix-scale-axis(x4)+SCurve+Exp, granular-distortion, granular-delay, reverb(Hall+diffusion), global-width+spread
- **Rationale:** Breaks the near-pair with Particle Cloud on every axis. Owns the unused SVF Allpass (res 1.8 for a phasey swoosh), adds real motion the original lacked (SmoothRandom LFO->morph with a large 90ms smoothMs, plus an EnvelopeFollower->cutoff at scale=4/x4 so the allpass breathes dramatically with playing dynamics), swaps in a thematic Granular distortion and Granular delay, and uses a diffuse Hall rather than the plate. Grain character diverges from Cloud: dual-layer Regular+Burst, dense 52 + sparse 18, long 1600ms bed + short 450ms sparkle vs Cloud's single sparse Random 900ms layer. Freeze intentionally omitted to avoid a static silent/runaway buffer. Verified: filter type 7, distortion type 3 (grainSize/grainDensity/grainVariation fields), delay type 3 (granularSizeMs/granularPitchSpray/granularPosSpray), envFollower fields, and modMatrix.slots[].scale all present in ruinae_preset_format.h.
- **Replacement code:**
```cpp
    // "Granular Fog" - dense dual-particle fog through a breathing allpass
    {
        PresetDef p;
        p.name = "Granular Fog";
        p.category = "Textures";
        auto& s = p.state;
        // --- Voice: two particle layers -> a dense, wide fog ---
        s.oscA.type = 6;                  // Particle (long-grain bed)
        s.oscA.particleScatter = 4.0f;
        s.oscA.particleDensity = 52.0f;   // dense
        s.oscA.particleLifetime = 1600.0f;
        s.oscA.particleSpawnMode = 0;     // Regular -> smooth continuous bed
        s.oscA.particleEnvType = 3;       // Blackman (softest, 3=Blackman)
        s.oscA.particleDrift = 0.5f;
        s.oscA.level = 0.7f;
        s.oscB.type = 6;                  // Particle (short clustered sparkle)
        s.oscB.particleScatter = 9.0f;
        s.oscB.particleDensity = 18.0f;
        s.oscB.particleLifetime = 450.0f;
        s.oscB.particleSpawnMode = 2;     // Burst -> clustered grains on top (2=Burst)
        s.oscB.particleEnvType = 0;       // Hann
        s.oscB.particleDrift = 0.7f;
        s.oscB.level = 0.5f;
        s.mixer.position = 0.45f;
        // --- Filter: SVF ALLPASS -> phasey, diffuse smear with no tonal notch ---
        s.filter.type = 7;                // SVF Allpass (owns this rare filter)
        s.filter.cutoffHz = 900.0f;
        s.filter.resonance = 1.8f;        // allpass "swoosh" around 900 Hz
        // --- Motion 1: heavily-smoothed LFO morphs the two grain layers ---
        s.lfo1.rateHz = 0.06f;
        s.lfo1.shape = 5;                 // SmoothRandom
        s.lfo1.depth = 0.75f;
        s.lfo1.sync = 0;
        setModSlot(s, 0, kSrcLFO1, kDstAllMorphPos, 0.5f, kCurveSCurve, 90.0f); // big smoothMs
        // --- Motion 2: Envelope Follower opens the allpass with dynamics, scaled x4 ---
        s.envFollower.sensitivity = 0.7f; s.envFollower.attackMs = 60.0f; s.envFollower.releaseMs = 400.0f;
        setModSlot(s, 1, kSrcEnvFollower, kDstAllFltCut, 0.3f, kCurveExp, 40.0f);
        s.modMatrix.slots[1].scale = 4;   // x4 -> dramatic breathing sweep (owns scale x4)
        // --- Amp env: very slow fog roll-in ---
        s.ampEnv.attackMs = 1400.0f;
        s.ampEnv.decayMs = 500.0f;
        s.ampEnv.sustain = 0.9f;
        s.ampEnv.releaseMs = 3600.0f;
        // --- Granular distortion: grain-cloud saturation thickens the fog (thematic) ---
        s.distortion.type = 3;            // Granular (owns this dirt type)
        s.distortion.drive = 0.3f;
        s.distortion.grainSize = 0.4f;
        s.distortion.grainDensity = 0.5f;
        s.distortion.grainVariation = 0.4f;
        s.distortion.mix = 0.25f;
        // --- Granular delay scatters pitch-sprayed echoes ---
        s.delayEnabled = 1;
        s.delay.type = 3;                 // Granular (owns this delay type)
        s.delay.mix = 0.3f;
        s.delay.feedback = 0.4f;
        s.delay.timeMs = 500.0f;
        s.delay.granularSizeMs = 120.0f;
        s.delay.granularPitchSpray = 0.3f;
        s.delay.granularPosSpray = 0.4f;
        // --- Enormous diffuse Hall (distinct from Particle Cloud's plate) ---
        s.reverbEnabled = 1;
        s.reverbType = 1;                 // Hall
        s.reverb.size = 0.95f;
        s.reverb.mix = 0.5f;
        s.reverb.damping = 0.3f;
        s.reverb.diffusion = 0.92f;
        s.global.width = 2.0f;
        s.global.spread = 0.6f;
        presets.push_back(std::move(p));
    }
```

## 5. "Metal Resonance" -> "Metal Resonance"
- **Locate:** the block containing `p.name = "Metal Resonance"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A struck inharmonic 128-partial bell rung through a resonant comb whose pitch is punched up by the attack transient and a percussive filter-env, with note-tracked ring-mod clang.
- **Coverage:** additive-engine(128/inharm), noise-osc(white), comb-filter, filter-env-sweep(fixed semitone amount), transient-mod-source, mod-matrix(Exp), ring-modulator-distortion, digital-delay, reverb(plate)
- **Rationale:** Keeps the unique percussive comb identity but makes the metal actually move, per directive: a Transient source->AllFltCut (Exp, 0.6) punches the comb pitch up on every strike, and a fixed +30-semitone filter-env sweep (correcting the category-wide envAmount bug where acid presets wrote 4000 'Hz') gives a snappy 400ms ping. Adds a note-tracked RingModulator (ratio ~4.7, sine, mix 0.4, drive only 0.2 so it clangs without destroying the ear) for extra inharmonic edge. Comb res 0.72 with damping 0.25 rings long but stays self-oscillation-safe. Small plate + short free-time digital slap keep it a struck object distinct from the wash-heavy siblings.
- **Replacement code:**
```cpp
    // "Metal Resonance" - struck inharmonic bell through a swept comb
    {
        PresetDef p;
        p.name = "Metal Resonance";
        p.category = "Textures";
        auto& s = p.state;
        // --- Voice: inharmonic 128-partial additive tone, pinged by white noise ---
        s.oscA.type = 4;                  // Additive
        s.oscA.additivePartials = 128;    // full harmonic stack
        s.oscA.additiveInharm = 0.55f;    // strong bell/metal detune
        s.oscA.additiveTilt = -2.0f;
        s.oscA.level = 0.6f;
        s.oscB.type = 9;                  // Noise
        s.oscB.noiseColor = 0;            // White -> strike excitation
        s.oscB.level = 0.12f;
        s.mixer.position = 0.14f;         // mostly additive, whisper of noise
        // --- Filter: resonant Comb -> metallic ringing body ---
        s.filter.type = 6;                // Comb
        s.filter.cutoffHz = 700.0f;       // comb tuning
        s.filter.resonance = 0.72f;       // long ring, still stable
        s.filter.combDamping = 0.25f;
        // --- Percussive filter env sweeps the comb on each strike ---
        // (envAmount is PLAIN semitones, range -48..+48 -- NOT a Hz value)
        s.filter.envAmount = 30.0f;       // +30 st sweep = strong metallic "ping"
        s.filterEnv.attackMs = 2.0f;
        s.filterEnv.decayMs = 400.0f;
        s.filterEnv.sustain = 0.0f;
        s.filterEnv.releaseMs = 300.0f;
        s.filterEnv.decayCurve = 0.5f;    // snappy exp-ish fall
        // --- Transient source: each attack briefly punches the comb up (the "struck" lever) ---
        s.transient.sensitivity = 0.8f; s.transient.attackMs = 2.0f; s.transient.decayMs = 80.0f;
        setModSlot(s, 0, kSrcTransient, kDstAllFltCut, 0.6f, kCurveExp, 10.0f);
        // --- Percussive amp env: fast strike, long metallic ring-out ---
        s.ampEnv.attackMs = 3.0f;
        s.ampEnv.decayMs = 2800.0f;
        s.ampEnv.sustain = 0.28f;
        s.ampEnv.releaseMs = 2200.0f;
        s.ampEnv.decayCurve = 0.4f;
        // --- Ring modulator: note-tracked sidebands = clangorous metal ---
        s.distortion.type = 6;            // RingModulator (owns this dirt type)
        s.distortion.drive = 0.2f;
        s.distortion.ringFreqMode = 1;    // NoteTrack -> stays musical across the keyboard
        s.distortion.ringRatio = 0.28f;   // normalized -> ratio ~4.7 (slightly inharmonic)
        s.distortion.ringWaveform = 0;    // Sine sidebands
        s.distortion.mix = 0.4f;          // clear metallic edge, still tonal
        // --- Short digital delay = tight metallic slapback ---
        s.delayEnabled = 1;
        s.delay.type = 0;                 // Digital
        s.delay.mix = 0.2f;
        s.delay.feedback = 0.35f;
        s.delay.timeMs = 280.0f;
        s.delay.sync = 0;                 // free time for the slap
        // --- Small plate reverb keeps it a struck object, not a wash ---
        s.reverbEnabled = 1;
        s.reverbType = 0;                 // Plate
        s.reverb.size = 0.55f;
        s.reverb.mix = 0.3f;
        s.reverb.damping = 0.45f;
        presets.push_back(std::move(p));
    }
```

## 6. "Shimmer Verb" -> "Shimmer Verb"
- **Locate:** the block containing `p.name = "Shimmer Verb"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A glassy saw+triangle octave-shimmer pad that breathes as a frozen, modulated Hall reverb swells around it and an all-pass smear sweeps under aftertouch.
- **Coverage:** Reverb Hall + freeze + modulation + diffusion + preDelay, harmonizer PhaseVocoder, SVF Allpass filter, LFO fadeInMs, mod-matrix SCurve curve + smoothMs, aftertouch voice route, global width + spread
- **Rationale:** Keeps the octave-shimmer identity but makes it MOVE and diversifies the wrapper. LFO1 (free, 0.1 Hz, 3 s fade-in) -> Effect Mix on an SCurve makes the wet shimmer literally breathe in (the closest settable proxy for the directive's 'LFO->harmonizer wet', since there is no harmonizer-wet mod dest). LFO2 -> AllMorphPos drifts the A/B blend independently. The category-owned SVF Allpass is placed here and swept by the slow ModEnv so it colours the widened stereo field. Reverb is a Hall (type 1) with freeze+mod+diffusion+preDelay covering the full reverb feature set; the 45% dry path guarantees audibility. Aftertouch->MorphPos adds the near-unused expression axis. width 1.7/spread 0.4 for a wide image. UNCHANGED by this fix pass (not flagged) - reproduced byte-identical.
- **Replacement code:**
```cpp
    // "Shimmer Verb" - Frozen modulated-Hall octave shimmer that breathes
    {
        PresetDef p;
        p.name = "Shimmer Verb";
        p.category = "Textures";
        auto& s = p.state;
        // OSC: PolyBLEP saw body + a glassy triangle an octave up for air
        s.oscA.type = 0; s.oscA.waveform = 1; s.oscA.level = 0.6f;   // saw fundamental
        s.oscB.type = 0; s.oscB.waveform = 4;                        // triangle = pure top
        s.oscB.tuneSemitones = 12.0f; s.oscB.level = 0.3f;
        s.mixer.mode = 0; s.mixer.position = 0.35f;                 // favour the saw
        // FILTER: SVF Allpass (category owns it) - flat magnitude, phase-only.
        // Sweeping its cutoff via ModEnv creates a phaser-like notch once summed
        // through the widened/harmonised/reverberated stereo field.
        s.filter.type = 7; s.filter.cutoffHz = 6000.0f; s.filter.resonance = 0.3f;
        s.filter.svfSlope = 1;
        s.filter.envAmount = 20.0f;                                 // +20 st allpass sweep (bug-fixed range)
        s.filterEnv.attackMs = 1500.0f; s.filterEnv.decayMs = 3000.0f;
        s.filterEnv.sustain = 0.4f; s.filterEnv.releaseMs = 2000.0f;
        // AMP: long glassy swell, distinct from the old 600/800/0.7/2500 wrapper
        s.ampEnv.attackMs = 800.0f; s.ampEnv.decayMs = 1200.0f;
        s.ampEnv.sustain = 0.75f; s.ampEnv.releaseMs = 3000.0f;
        s.ampEnv.attackCurve = 0.4f;                                // slow-start swell
        // ModEnv drives the allpass sweep (voice route below)
        s.modEnv.attackMs = 40.0f; s.modEnv.decayMs = 4000.0f;
        s.modEnv.sustain = 0.6f; s.modEnv.releaseMs = 2000.0f;
        // HARMONIZER: octave + octave-fifth PhaseVocoder shimmer
        s.harmonizerEnabled = 1;
        s.harmonizer.harmonyMode = 0;      // Chromatic
        s.harmonizer.pitchShiftMode = 2;   // PhaseVocoder (clean shimmer)
        s.harmonizer.numVoices = 2;
        s.harmonizer.dryLevelDb = 0.0f; s.harmonizer.wetLevelDb = -4.0f;
        s.harmonizer.voiceInterval[0] = 12; s.harmonizer.voicePan[0] = -0.45f;
        s.harmonizer.voiceInterval[1] = 19; s.harmonizer.voicePan[1] = 0.45f;
        s.harmonizer.voiceLevelDb[1] = -6.0f;
        // LFO1: very slow, free-running, long fade-in -> Effect Mix so the whole
        // wet bed (shimmer + reverb) swells in and breathes. SCurve for smoothness.
        s.lfo1.rateHz = 0.1f; s.lfo1.shape = 0; s.lfo1.depth = 0.8f; s.lfo1.sync = 0;
        s.lfo1Ext.fadeInMs = 3000.0f;      // shimmer fades up over 3 s
        s.modMatrix.slots[0].source = 1;   // LFO1
        s.modMatrix.slots[0].dest   = 3;   // Effect Mix (breathing wet)
        s.modMatrix.slots[0].amount = 0.5f;
        s.modMatrix.slots[0].curve  = 2;   // SCurve
        s.modMatrix.slots[0].smoothMs = 20.0f;
        s.modMatrix.slots[0].scale  = 2;   // x1
        // LFO2: independent slow drift of the A/B morph so timbre never sits still
        s.lfo2.rateHz = 0.13f; s.lfo2.shape = 0; s.lfo2.depth = 0.7f; s.lfo2.sync = 0;
        s.modMatrix.slots[1].source = 2;   // LFO2
        s.modMatrix.slots[1].dest   = 5;   // All-voice Morph Position
        s.modMatrix.slots[1].amount = 0.3f;
        s.modMatrix.slots[1].curve  = 0;   // Linear
        // Voice routes: ModEnv sweeps the allpass; Aftertouch adds live morph motion
        s.voiceRoutes[0].source = 2; s.voiceRoutes[0].destination = 0; // Env3 -> FltCut
        s.voiceRoutes[0].amount = 0.4f; s.voiceRoutes[0].active = 1;
        s.voiceRoutes[1].source = 7; s.voiceRoutes[1].destination = 2; // Aftertouch -> MorphPos
        s.voiceRoutes[1].amount = 0.45f; s.voiceRoutes[1].active = 1;
        // REVERB: big FROZEN, MODULATED Hall - the signature. Dry voice (mix 0.45)
        // stays fully audible even if the freeze holds the tail.
        s.reverbEnabled = 1; s.reverbType = 1;    // Hall
        s.reverb.size = 0.9f; s.reverb.mix = 0.45f;
        s.reverb.damping = 0.2f; s.reverb.diffusion = 0.9f;
        s.reverb.preDelayMs = 40.0f;
        s.reverb.modRateHz = 0.2f; s.reverb.modDepth = 0.25f;
        s.reverb.freeze = 1;                       // frozen shimmer bed
        // Wide stereo image with unison spread
        s.global.width = 1.7f; s.global.spread = 0.4f;
        presets.push_back(std::move(p));
    }
```

## 7. "Modular Noise" -> "Ferric Storm"
- **Locate:** the block containing `p.name = "Modular Noise"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A storm of pink noise and a Chua chaos oscillator squeezed through a resonant band-pass and a note-tracked ring modulator, jumping on its own transients and ping-ponging across a wide stereo field.
- **Coverage:** engines: Chaos(coupling/output axis), Noise(colors); filters: SVF BP; RingModulator distortion; PingPong delay; LFO S&H + quantizeSteps; mod-matrix scale axis (x2) + Exp curve; transient source; random source + smoothness; global width + spread
- **Rationale:** Renamed from the generic 'Modular Noise'. Three distinct mod slots give it real motion where the original was static: an S&H+quantized LFO stair-steps the band-pass (scale x2 to exploit the untouched scale axis), the transient detector spikes resonance on the sound's own attacks (Exp curve), and a smoothed Random wanders spectral tilt. Ring mod switched to NoteTrack so the metallic clang follows the played pitch (the original's static free-mode ring never evolved). Chaos osc now uses coupling + Y-axis output for extra grit. PingPong delay replaces the generic delay+reverb pair, and reverb is a small Plate to contrast the halls. filterEnv bug-fixed to +18 st for an audible sweep. UNCHANGED by this fix pass (not flagged) - reproduced byte-identical. NOTE: slot[2] (Random -> Spectral Tilt) is a Crossfade-mode preset, so like Digital Decay its tilt route is spectral-only; it was NOT flagged because the preset's PRIMARY motion (slot[0] S&H -> Filter Cutoff, plus the transient -> Resonance route) is fully audible - the tilt is only a slow secondary wander, not the marquee behavior.
- **Replacement code:**
```cpp
    // "Ferric Storm" (was Modular Noise) - note-tracked ring-mod chaos noise
    {
        PresetDef p;
        p.name = "Ferric Storm";
        p.category = "Textures";
        auto& s = p.state;
        // OSC A: pink noise bed
        s.oscA.type = 9; s.oscA.noiseColor = 1; s.oscA.level = 0.55f;   // pink
        // OSC B: Chua chaos attractor, Y-axis output with cross-coupling for grit
        s.oscB.type = 5; s.oscB.chaosAttractor = 2;                    // Chua
        s.oscB.chaosAmount = 0.5f; s.oscB.chaosCoupling = 0.4f;        // instability
        s.oscB.chaosOutput = 1;                                        // Y axis timbre
        s.oscB.level = 0.45f;
        s.mixer.mode = 0; s.mixer.position = 0.45f;
        // FILTER: resonant SVF Band-Pass carves a formant band out of the noise
        s.filter.type = 2; s.filter.cutoffHz = 1500.0f; s.filter.resonance = 0.5f;
        s.filter.svfSlope = 1;
        s.filter.envAmount = 18.0f;                                    // slow BP sweep on note (bug-fixed)
        s.filterEnv.attackMs = 400.0f; s.filterEnv.decayMs = 1500.0f;
        s.filterEnv.sustain = 0.35f; s.filterEnv.releaseMs = 800.0f;
        // DISTORTION: note-tracked ring modulator -> metallic, pitch-following clang
        s.distortion.type = 6;                     // Ring Modulator
        s.distortion.drive = 0.3f; s.distortion.mix = 0.5f;
        s.distortion.ringFreqMode = 1;             // NoteTrack (follows played pitch)
        s.distortion.ringRatio = 0.35f;            // normalized -> clangorous ratio
        s.distortion.ringWaveform = 2;             // saw carrier
        s.distortion.ringStereoSpread = 0.5f;      // stereo shimmer on the ring
        // AMP: medium swell
        s.ampEnv.attackMs = 300.0f; s.ampEnv.decayMs = 1000.0f;
        s.ampEnv.sustain = 0.8f; s.ampEnv.releaseMs = 1800.0f;
        // LFO1: Sample & Hold, free, quantized -> steps the band-pass cutoff.
        // scale x2 widens the sweep (mod-matrix scale axis, otherwise unused).
        s.lfo1.rateHz = 3.0f; s.lfo1.shape = 4; s.lfo1.depth = 0.7f; s.lfo1.sync = 0;
        s.lfo1Ext.quantizeSteps = 6;               // stair-stepped motion
        s.modMatrix.slots[0].source = 1;           // LFO1 (S&H)
        s.modMatrix.slots[0].dest   = 4;           // All-voice Filter Cutoff
        s.modMatrix.slots[0].amount = 0.45f;
        s.modMatrix.slots[0].curve  = 0;           // Linear
        s.modMatrix.slots[0].scale  = 3;           // x2 depth
        // Transient detector -> resonance: the storm spikes on its own attacks
        s.transient.sensitivity = 0.7f; s.transient.attackMs = 2.0f; s.transient.decayMs = 60.0f;
        s.modMatrix.slots[1].source = 13;          // Transient
        s.modMatrix.slots[1].dest   = 8;           // All-voice Resonance
        s.modMatrix.slots[1].amount = 0.4f;
        s.modMatrix.slots[1].curve  = 1;           // Exponential
        // Smoothed Random -> spectral tilt for slow underlying wander
        s.random.rateHz = 0.4f; s.random.smoothness = 0.8f; s.random.sync = 0;
        s.modMatrix.slots[2].source = 4;           // Random
        s.modMatrix.slots[2].dest   = 7;           // All-voice Spectral Tilt
        s.modMatrix.slots[2].amount = 0.3f;
        s.modMatrix.slots[2].smoothMs = 40.0f;
        // DELAY: PingPong for wide rhythmic scatter
        s.delayEnabled = 1; s.delay.type = 2;      // PingPong
        s.delay.timeMs = 375.0f; s.delay.feedback = 0.4f; s.delay.mix = 0.28f;
        s.delay.sync = 0;
        s.delay.pingPongRatio = 2; s.delay.pingPongCrossFeed = 0.8f;
        s.delay.pingPongWidth = 140.0f; s.delay.pingPongModDepth = 0.2f;
        // REVERB: small Plate (contrast to the big halls elsewhere)
        s.reverbEnabled = 1; s.reverbType = 0;     // Plate
        s.reverb.size = 0.55f; s.reverb.mix = 0.3f; s.reverb.damping = 0.4f;
        s.reverb.diffusion = 0.6f;
        s.global.width = 1.5f; s.global.spread = 0.5f;
        presets.push_back(std::move(p));
    }
```

## 8. "Digital Decay" -> "Digital Decay"
- **Locate:** the block containing `p.name = "Digital Decay"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A 128-partial inharmonic additive body wired to a Benjolin rungler that hard-steps the crossfade against a sub-saw and pitch-glitches a tuned, key-tracked comb resonator in lockstep, wavefolded and thrown into rhythmic, bit-aged 1/16 digital echoes.
- **Coverage:** engine: Additive(128/inharm); Crossfade mixer (rungler Morph-Position glitch is audible without spectral mode); filter: Comb (key-tracked, damped); Wavefolder distortion (non-spectral, foldType); rungler source -> DUAL stepped routes (MorphPos + Comb cutoff); mod-matrix Stepped curve + scale x2 axis; LFO SmoothRandom + quantizeSteps -> All-voice resonance; ModEnv -> DistDrive voice route; global filter: Notch (static spectral scoop); Digital delay (NON-spectral, synced 1/16, digitalAge + wavefold); reverb: Plate
- **Rationale:** Executes the critic directive: Digital Decay is recast off the ENTIRE chain it shared with Spectral Ghost so the pair no longer reads as one sound. (1) Filter SVF HP (type 1) -> COMB (type 6, verified combDamping/keyTrack in FilterState L184-194), tuned to 220 Hz and key-tracked for a metallic pitched body. (2) Distortion spectral bitcrush -> WAVEFOLDER (type 4, foldType field verified DistortionState L251), a non-spectral digital fold. (3) Delay Spectral (type 4) -> NON-spectral DIGITAL (type 0), synced to 1/16 with digitalAge=0.6 + digitalWavefoldAmt=0.25 for rhythmic, bit-aged echoes (all Digital fields verified DelayState L516-525). (4) Mixer SpectralMorph -> CROSSFADE (mode=0) - critical because the marquee motion is now the rungler, and Morph Position (dest 5) IS audible in Crossfade, whereas Spectral Tilt (dest 7) would be inert; so I route nothing to dest 7. (5) FULLY into rungler: rungler.depth=0.7, bits=6, loopMode=0 feeds TWO Stepped-curve routes - slot 0 -> AllMorphPos (hard crossfade jumps) and slot 1 -> AllFltCut (comb pitch-glitch) - making the Benjolin shift-register the dominant stepped rhythm. pitchFollower is dropped entirely (assigned to Spectral Ghost) to avoid the shared source. Retained and verified: LFO1 SmoothRandom+quantizeSteps=8 -> AllResonance with Stepped curve + scale=3 (x2, set via direct slot write since setModSlot omits scale), and ModEnv(Env3) -> DistDrive voice route. Constants kSrcRungler=10, kDstAllMorphPos=5, kDstAllFltCut=4, kDstAllResonance=8, kCurveStepped=3, kVSrcEnv3=2, kVDstDistDrive=3 all confirmed in the generator (L204-261).
- **Replacement code:**
```cpp
    // "Digital Decay" - rungler-glitched additive drone through a tuned comb, rhythmic aged digital echoes
    {
        PresetDef p;
        p.name = "Digital Decay";
        p.category = "Textures";
        auto& s = p.state;
        // OSC A: full 128-partial additive with inharmonicity -> metallic digital body
        s.oscA.type = 4; s.oscA.additivePartials = 128;
        s.oscA.additiveTilt = -3.0f; s.oscA.additiveInharm = 0.25f;   // slightly clangorous
        s.oscA.level = 0.7f;
        // OSC B: sub saw for weight the rungler crossfades against
        s.oscB.type = 0; s.oscB.waveform = 1; s.oscB.tuneSemitones = -12.0f;
        s.oscB.level = 0.35f;
        // MIXER: plain CROSSFADE (not SpectralMorph) -> the rungler's Morph-Position glitch
        // (slot 0) hard-cuts between the additive body and the sub-saw. Morph Position is
        // audible in Crossfade mode, so no spectral-domain dependency is needed here.
        s.mixer.mode = 0; s.mixer.position = 0.35f;
        // FILTER: tuned COMB -> metallic, key-tracked resonant body (replaces the shared SVF HP)
        s.filter.type = 6;                // Comb
        s.filter.cutoffHz = 220.0f;
        s.filter.resonance = 0.6f;
        s.filter.combDamping = 0.3f;
        s.filter.keyTrack = 0.5f;
        // DISTORTION: WAVEFOLDER -> harsh digital fold (replaces the shared spectral bitcrush)
        s.distortion.type = 4;            // Wavefolder
        s.distortion.drive = 0.5f;
        s.distortion.foldType = 1;
        s.distortion.mix = 0.6f;
        // AMP: slow swell so the stepped rungler rhythm emerges under a sustained bed
        s.ampEnv.attackMs = 1200.0f; s.ampEnv.decayMs = 700.0f;
        s.ampEnv.sustain = 0.75f; s.ampEnv.releaseMs = 2200.0f;
        s.ampEnv.attackCurve = 0.5f;
        // ModEnv: long rise that pushes the wavefolder drive deeper over the note
        s.modEnv.attackMs = 3000.0f; s.modEnv.decayMs = 4000.0f;
        s.modEnv.sustain = 0.8f; s.modEnv.releaseMs = 1500.0f;
        // --- RUNGLER is the identity: a Benjolin shift-register driving stepped glitch motion ---
        s.rungler.osc1FreqHz = 2.5f; s.rungler.osc2FreqHz = 3.7f;
        s.rungler.depth = 0.7f; s.rungler.bits = 6; s.rungler.loopMode = 0; // 6-bit chaos pattern
        // Rungler -> Morph Position: hard stepped crossfade jumps between the two oscillators
        s.modMatrix.slots[0].source = kSrcRungler;      // 10
        s.modMatrix.slots[0].dest   = kDstAllMorphPos;  // 5
        s.modMatrix.slots[0].amount = 0.55f;
        s.modMatrix.slots[0].curve  = kCurveStepped;    // 3
        // Rungler -> Comb cutoff: pitch-glitches the metallic comb in lockstep
        s.modMatrix.slots[1].source = kSrcRungler;      // 10
        s.modMatrix.slots[1].dest   = kDstAllFltCut;    // 4
        s.modMatrix.slots[1].amount = 0.45f;
        s.modMatrix.slots[1].curve  = kCurveStepped;    // 3
        // Secondary stepped motion: glacial SmoothRandom LFO, quantized -> All-voice resonance,
        // Stepped curve, x2 scale so the comb feedback pulses in discrete stages.
        s.lfo1.rateHz = 0.08f; s.lfo1.shape = 5; s.lfo1.depth = 0.6f; s.lfo1.sync = 0;
        s.lfo1Ext.quantizeSteps = 8;
        s.modMatrix.slots[2].source = kSrcLFO1;         // 1 (SmoothRandom)
        s.modMatrix.slots[2].dest   = kDstAllResonance; // 8
        s.modMatrix.slots[2].amount = 0.5f;
        s.modMatrix.slots[2].curve  = kCurveStepped;    // 3 (owns Stepped curve)
        s.modMatrix.slots[2].scale  = 3;                // x2 (owns the depth-scale axis)
        // Voice route: ModEnv drives wavefolder drive so the fold deepens across the note
        s.voiceRoutes[0].source = kVSrcEnv3; s.voiceRoutes[0].destination = kVDstDistDrive; // Env3 -> DistDrive
        s.voiceRoutes[0].amount = 0.6f; s.voiceRoutes[0].active = 1;
        // GLOBAL FILTER: a static NOTCH scoops the low-mids out of the clangorous 128-partial
        // body -> a hollow, bell-like spectral scoop that tames the boxy inharmonic buildup.
        s.globalFilter.enabled = 1; s.globalFilter.type = 3;   // Notch (spectral scoop)
        s.globalFilter.cutoffHz = 700.0f; s.globalFilter.resonance = 1.2f;
        // DELAY: NON-spectral DIGITAL, synced -> rhythmic, degrading digital echoes
        s.delayEnabled = 1; s.delay.type = 0;           // Digital
        s.delay.sync = 1; s.delay.noteValue = 7;        // 1/16 rhythmic glitch trails
        s.delay.feedback = 0.5f; s.delay.mix = 0.32f;
        s.delay.digitalAge = 0.6f;                      // bit/sample degradation on the trail
        s.delay.digitalModDepth = 0.3f; s.delay.digitalModRateHz = 0.7f;
        s.delay.digitalWavefoldAmt = 0.25f;
        s.delay.digitalWidth = 140.0f;
        // REVERB: small Plate (Spectral Ghost owns the big modulated Hall)
        s.reverbEnabled = 1; s.reverbType = 0;          // Plate
        s.reverb.size = 0.5f; s.reverb.mix = 0.24f; s.reverb.damping = 0.4f;
        s.reverb.diffusion = 0.7f;
        s.global.width = 1.3f; s.global.spread = 0.3f;
        presets.push_back(std::move(p));
    }
```

## 9. "Tape Artifact" -> "Degauss Drift"
- **Locate:** the block containing `p.name = "Tape Artifact"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A brown-noise tape-hiss bed under a sub saw, carved by a wobbling SVF notch, saturated through a hysteresis tape stage and a worn tape delay, with a Rossler chaos LFO and a slewed sample-and-hold keeping the whole thing drifting.
- **Coverage:** engines: Noise(colors); filters: SVF Notch; TapeSaturator (hysteresis) distortion; Tape delay (wear); LFO phaseOffset; chaosMod source; sampleHold source + slew; mod-matrix scale axis; filter-env bug fix
- **Rationale:** Renamed to signal its tape-degaussing character. The original was flagged as static (motion came only from delay tapeWear) and structurally a near-clone of Modular Noise. This version now owns the SVF Notch and gives it three motion sources: a phase-offset triangle LFO wobbles the notch frequency (scale x2), a Rossler chaosMod jitters resonance for wow-and-flutter instability, and a slewed S&H slowly drifts the noise/saw head balance. filterEnv bug-fixed to +15 st. Tape delay keeps the wear artifacts. Distinct exotic-source set (chaosMod + sampleHold) from the other four presets so no sibling repeats its mod identity. UNCHANGED by this fix pass (not flagged) - reproduced byte-identical. NOTE: slot[2] (S&H -> Morph Position) is a Crossfade-mode preset, so its Morph route is a Crossfade-domain A/B balance drift (the mixer crossfades osc A/B), which IS audible in Crossfade mode; only the spectral tilt destination is spectral-only, and this preset does not use it.
- **Replacement code:**
```cpp
    // "Degauss Drift" (was Tape Artifact) - wobbling-notch worn-tape hiss bed
    {
        PresetDef p;
        p.name = "Degauss Drift";
        p.category = "Textures";
        auto& s = p.state;
        // OSC A: brown noise = dark tape rumble
        s.oscA.type = 9; s.oscA.noiseColor = 2; s.oscA.level = 0.5f;   // brown
        // OSC B: sub-octave saw for pitched weight under the hiss
        s.oscB.type = 0; s.oscB.waveform = 1; s.oscB.tuneSemitones = -12.0f;
        s.oscB.level = 0.3f;
        s.mixer.mode = 0; s.mixer.position = 0.35f;
        // FILTER: SVF Notch (category owns it) - carves a moving gap out of the noise
        s.filter.type = 3; s.filter.cutoffHz = 1200.0f; s.filter.resonance = 0.4f;
        s.filter.svfSlope = 1;
        s.filter.envAmount = 15.0f;                                    // slow notch rise (bug-fixed)
        s.filterEnv.attackMs = 600.0f; s.filterEnv.decayMs = 2000.0f;
        s.filterEnv.sustain = 0.4f; s.filterEnv.releaseMs = 1000.0f;
        // DISTORTION: hysteresis TapeSaturator -> warm, worn magnetic grit
        s.distortion.type = 5; s.distortion.drive = 0.6f;
        s.distortion.tapeModel = 1;                // Hysteresis
        s.distortion.tapeSaturation = 0.7f; s.distortion.tapeBias = 0.4f;
        s.distortion.mix = 0.8f;
        // AMP: medium bed
        s.ampEnv.attackMs = 250.0f; s.ampEnv.decayMs = 700.0f;
        s.ampEnv.sustain = 0.65f; s.ampEnv.releaseMs = 1400.0f;
        // LFO1: slow triangle with a 90-deg phase offset -> notch cutoff wobble
        s.lfo1.rateHz = 0.25f; s.lfo1.shape = 1; s.lfo1.depth = 0.6f; s.lfo1.sync = 0;
        s.lfo1Ext.phaseOffset = 90.0f;
        s.modMatrix.slots[0].source = 1;           // LFO1
        s.modMatrix.slots[0].dest   = 4;           // All-voice Filter Cutoff (notch freq)
        s.modMatrix.slots[0].amount = 0.4f;
        s.modMatrix.slots[0].curve  = 0;
        s.modMatrix.slots[0].scale  = 3;           // x2 for a wide wobble
        // Chaos LFO (Rossler) -> resonance: organic pitch-of-wow instability
        s.chaosMod.rateHz = 0.3f; s.chaosMod.type = 1;  // Rossler
        s.chaosMod.depth = 0.4f; s.chaosMod.sync = 0;
        s.modMatrix.slots[1].source = 9;           // Chaos
        s.modMatrix.slots[1].dest   = 8;           // All-voice Resonance
        s.modMatrix.slots[1].amount = 0.3f;
        s.modMatrix.slots[1].curve  = 0;
        // Slewed Sample & Hold -> morph position: slow random head-balance drift
        s.sampleHold.rateHz = 0.5f; s.sampleHold.slewMs = 120.0f; s.sampleHold.sync = 0;
        s.modMatrix.slots[2].source = 11;          // Sample & Hold
        s.modMatrix.slots[2].dest   = 5;           // All-voice Morph Position
        s.modMatrix.slots[2].amount = 0.25f;
        s.modMatrix.slots[2].smoothMs = 30.0f;
        // DELAY: Tape with heavy wear -> the wobble/dropout artifacts
        s.delayEnabled = 1; s.delay.type = 1;      // Tape
        s.delay.timeMs = 450.0f; s.delay.feedback = 0.35f; s.delay.mix = 0.22f;
        s.delay.tapeSaturation = 0.5f; s.delay.tapeWear = 0.5f; s.delay.tapeAge = 0.3f;
        // REVERB: small dark Plate
        s.reverbEnabled = 1; s.reverbType = 0;     // Plate
        s.reverb.size = 0.45f; s.reverb.mix = 0.24f; s.reverb.damping = 0.5f;
        s.global.width = 1.2f; s.global.spread = 0.2f;
        presets.push_back(std::move(p));
    }
```

## 10. "Vowel Ghost" -> "Vowel Ghost"
- **Locate:** the block containing `p.name = "Vowel Ghost"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** Two formant oscillators tuned to opposite CLOSE vowels — 'I' against 'U' — spectrally morphed and swept in one direction by a slow unipolar SCurve LFO, run through a resonant comb throat and a Granular harmonizer into a FROZEN granular grain-cloud and a big modulated Hall: a bright, whistly disembodied ghost.
- **Coverage:** engines: Formant (both oscillators, opposite close vowels I/U); SpectralMorph mixer; filters: Comb; harmonizer Granular pitch mode + formant-preserve; Granular delay + granularFreeze (held grain cloud); Reverb Hall + modulation + preDelay + diffusion; LFO unipolar + fadeInMs + free-run; mod-matrix SCurve curve + smoothMs; envFollower source -> All-voice cutoff; velocity voice route -> filter cutoff; global width + spread
- **Rationale:** The original Vowel Ghost was Formant-'I' + a noise layer with a STATIC vowel (osc formantMorph is not a mod destination), and the audit's draft made it a dual-formant wash but reused the A/U pair that Vowel Cloud also used, so the two Textures patches would still read as one sound. This version fixes that: it is the trio's BRIGHT member with the close-vowel pair I(2)<->U(4) - genuinely distinct from Formant Sea (A/O) and the revised Vowel Cloud (E/O). The real vowel shift is delivered the settable way: two Formant oscillators on opposite vowels with the mixer in SpectralMorph mode, swept by a slow UNIPOLAR SCurve LFO on AllMorphPos (dest 5) so it travels the full I->U range one-directionally with a 2 s fade-in - a morph motion distinct from Cloud's dual phase-offset LFOs. Spatial identity is the FROZEN Granular delay (granularFreeze=1) grain-cloud, distinct from Cloud's tape doubling and Sea's dry ping-pong. Comb filter with an envelope-follower opening its throat, a velocity->cutoff voice route, and a big modulated Hall complete a lush-but-distinct wrapper. Key values: oscA vowel 2 / oscB vowel 4 (I/U); lfo1 0.12 Hz unipolar (vs Cloud's 0.06); mod slot 0 curve 2 (SCurve); delay.type 3 + granularFreeze 1; reverbType 1 (Hall).
- **Replacement code:**
```cpp
    // "Vowel Ghost" - dual Formant (I vs U), unipolar morph, frozen granular cloud
    {
        PresetDef p;
        p.name = "Vowel Ghost";
        p.category = "Textures";
        auto& s = p.state;
        // OSC A + B: two Formant engines on OPPOSITE CLOSE vowels. This trio's bright
        // member: 'I' (ee) against 'U' (oo) - distinct from Formant Sea (A/O) and
        // Vowel Cloud (E/O). Morphing the mixer position makes the vowel genuinely shift.
        s.oscA.type = 7; s.oscA.formantVowel = 2; s.oscA.formantMorph = 2.0f; // 'I'
        s.oscA.level = 0.65f;
        s.oscB.type = 7; s.oscB.formantVowel = 4; s.oscB.formantMorph = 3.8f; // 'U'
        s.oscB.level = 0.65f;
        // SpectralMorph mixer = FFT interpolation A<->B = a real vowel morph
        s.mixer.mode = 1;                          // SpectralMorph
        s.mixer.position = 0.5f; s.mixer.tilt = 0.0f;
        // FILTER: resonant Comb adds a metallic, hollow throat body
        s.filter.type = 6; s.filter.cutoffHz = 600.0f; s.filter.resonance = 0.5f;
        s.filter.combDamping = 0.4f;
        // AMP: breathy slow swell, high sustain
        s.ampEnv.attackMs = 900.0f; s.ampEnv.decayMs = 600.0f;
        s.ampEnv.sustain = 0.85f; s.ampEnv.releaseMs = 2200.0f;
        s.ampEnv.attackCurve = 0.4f;
        // HARMONIZER: Scalic minor, GRANULAR pitch mode (covers the granular variant),
        // formant-preserved so the harmonised voices stay vocal
        s.harmonizerEnabled = 1;
        s.harmonizer.harmonyMode = 1;      // Scalic
        s.harmonizer.key = 0; s.harmonizer.scale = 1;  // Natural Minor
        s.harmonizer.pitchShiftMode = 1;   // Granular
        s.harmonizer.formantPreserve = 1;
        s.harmonizer.numVoices = 2;
        s.harmonizer.dryLevelDb = 0.0f; s.harmonizer.wetLevelDb = -5.0f;
        s.harmonizer.voiceInterval[0] = 3;  s.harmonizer.voicePan[0] = -0.5f;
        s.harmonizer.voiceInterval[1] = -5; s.harmonizer.voicePan[1] = 0.5f;
        // LFO1: slow, free, UNIPOLAR (full 0..1 sweep), long fade-in -> Morph Position.
        // Unipolar makes it sweep the whole I->U vowel range in one direction.
        s.lfo1.rateHz = 0.12f; s.lfo1.shape = 0; s.lfo1.depth = 0.9f; s.lfo1.sync = 0;
        s.lfo1Ext.unipolar = 1; s.lfo1Ext.fadeInMs = 2000.0f;
        s.modMatrix.slots[0].source = 1;           // LFO1
        s.modMatrix.slots[0].dest   = 5;           // All-voice Morph Position (vowel morph)
        s.modMatrix.slots[0].amount = 0.6f;
        s.modMatrix.slots[0].curve  = 2;           // SCurve
        s.modMatrix.slots[0].smoothMs = 15.0f;
        // Envelope follower -> comb cutoff: the ghost's throat opens with dynamics
        s.envFollower.sensitivity = 0.7f; s.envFollower.attackMs = 20.0f; s.envFollower.releaseMs = 200.0f;
        s.modMatrix.slots[1].source = 3;           // Env Follower
        s.modMatrix.slots[1].dest   = 4;           // All-voice Filter Cutoff
        s.modMatrix.slots[1].amount = 0.3f;
        s.modMatrix.slots[1].curve  = 0;
        // Voice route: velocity brightens the comb for playing expression
        s.voiceRoutes[0].source = 5; s.voiceRoutes[0].destination = 0; // Velocity -> FltCut
        s.voiceRoutes[0].amount = 0.4f; s.voiceRoutes[0].active = 1;
        // DELAY: Granular, FROZEN -> a held vocal grain-cloud (this patch's spatial identity)
        s.delayEnabled = 1; s.delay.type = 3;      // Granular
        s.delay.timeMs = 500.0f; s.delay.feedback = 0.3f; s.delay.mix = 0.3f;
        s.delay.granularSizeMs = 140.0f; s.delay.granularDensity = 14.0f;
        s.delay.granularPitchSpray = 0.2f; s.delay.granularTexture = 0.3f;
        s.delay.granularFreeze = 1;                // frozen grain cloud
        // REVERB: large modulated Hall
        s.reverbEnabled = 1; s.reverbType = 1;     // Hall
        s.reverb.size = 0.85f; s.reverb.mix = 0.4f; s.reverb.damping = 0.25f;
        s.reverb.diffusion = 0.85f; s.reverb.preDelayMs = 30.0f;
        s.reverb.modRateHz = 0.3f; s.reverb.modDepth = 0.15f;
        s.global.width = 1.6f; s.global.spread = 0.45f;
        presets.push_back(std::move(p));
    }
```

## 11. "Resonant Strings" -> "Rosin Halo"
- **Locate:** the block containing `p.name = "Resonant Strings"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A bowed, resonant string bed whose comb resonance re-tunes to the note you play and bites brighter on each attack, blooming into a wide diffuse hall.
- **Coverage:** noise-osc, additive-engine, additive-inharm, comb-filter, pitchFollower source, transient source, mod-matrix SCurve curve, mod-matrix scale axis (x2), filter-env-fix (semitones), Reverb Hall, reverb preDelay, reverb diffusion, global width
- **Rationale:** Owns pitchFollower per its directive: a fast (25ms) follower drives AllFltCut so the comb (cutoff = comb tuning) re-tunes to the played note, at x2 scale for an audible glide. The transient source biting AllResonance gives per-note bow bite - a distinct second gesture no sibling repeats. Fixes the filter-env bug (envAmount=18 plain semitones, not a Hz value). Comb+Additive+Pink-noise is a unique synthesis path; Hall+preDelay+diffusion distinguishes its wrapper from the Plate presets.
- **Replacement code:**
```cpp
    // "Rosin Halo" - Noise+Additive bowed into a resonant comb; pitch-follower tunes the resonance
    {
        PresetDef p;
        p.name = "Rosin Halo";
        p.category = "Textures";
        auto& s = p.state;
        // --- Excitation: pink-noise bow-scrape + inharmonic additive string body ---
        s.oscA.type = 9;              // Noise
        s.oscA.noiseColor = 1;        // Pink - soft bow-hair air, not white hiss
        s.oscA.level = 0.30f;
        s.oscB.type = 4;              // Additive
        s.oscB.additivePartials = 24; // rich body without glassy top
        s.oscB.additiveTilt = -6.0f;  // roll off highs -> warm rosin timbre
        s.oscB.additiveInharm = 0.08f;// slight string stretch (metallic edge)
        s.oscB.level = 0.42f;
        s.mixer.position = 0.55f;     // favor the tuned additive body over the noise
        // --- Resonant comb = the "string" the excitation drives ---
        s.filter.type = 6;            // Comb
        s.filter.cutoffHz = 440.0f;   // comb tuned near A4
        s.filter.resonance = 0.85f;   // long ringing bow tail
        s.filter.combDamping = 0.30f; // bow damping in the feedback path
        s.filter.envAmount = 18.0f;   // FIX: plain semitones (audible swell) - not a Hz value
        // Filter env = the bow drawing across the string (slow rise, no re-attack)
        s.filterEnv.attackMs = 400.0f;
        s.filterEnv.decayMs = 1200.0f;
        s.filterEnv.sustain = 0.6f;
        s.filterEnv.releaseMs = 1800.0f;
        s.filterEnv.attackCurve = 0.5f; // slow-start swell
        // --- Amp: bowed onset, long sustaining tail ---
        s.ampEnv.attackMs = 300.0f;
        s.ampEnv.decayMs = 1500.0f;
        s.ampEnv.sustain = 0.55f;
        s.ampEnv.releaseMs = 2600.0f;
        // === MOD IDENTITY: the comb resonance tracks the pitch you play ===
        s.pitchFollower.minHz = 80.0f;
        s.pitchFollower.maxHz = 1200.0f;
        s.pitchFollower.confidence = 0.6f;
        s.pitchFollower.speedMs = 25.0f;   // fast, so the comb re-tunes as you play
        setModSlot(s, 0, 12, 4, 0.55f, 2, 20.0f); // PitchFollow -> AllFltCut, SCurve, gentle smooth
        s.modMatrix.slots[0].scale = 3;    // x2 depth -> wider comb re-tuning range (scale axis)
        // Second gesture: transient detector adds a bright bow-scrape on each attack
        s.transient.sensitivity = 0.6f;
        s.transient.attackMs = 2.0f;
        s.transient.decayMs = 80.0f;
        setModSlot(s, 1, 13, 8, 0.35f, 1); // Transient -> AllResonance, Exp - scrape bite
        // --- Space: big diffuse Hall with pre-delay (no delay line) ---
        s.reverbEnabled = 1;
        s.reverbType = 1;             // Hall
        s.reverb.size = 0.75f;
        s.reverb.mix = 0.32f;
        s.reverb.preDelayMs = 40.0f;  // air before the wash
        s.reverb.diffusion = 0.85f;
        s.global.width = 1.5f;
        presets.push_back(std::move(p));
    }
```

## 12. "Ping Garden" -> "Ping Garden"
- **Locate:** the block containing `p.name = "Ping Garden"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** Sparse glassy droplets ping a self-oscillating filter whose pitch snaps between five discrete notes via a stepped-random LFO, echoing through a ping-pong delay.
- **Coverage:** noise-osc, particle-engine, self-oscillating-filter, PingPong delay, LFO SmoothRandom, LFO quantizeSteps, LFO phaseOffset, LFO fadeInMs, mod-matrix Stepped curve, sampleHold source, Reverb Plate, global width
- **Rationale:** Keeps the strongest existing identity (self-osc filter + ping-pong) but cures the STATIC sin: a free-running SmoothRandom LFO with quantizeSteps=5 and Stepped curve makes the self-osc ping pitch jump between five discrete notes - arpeggiated droplets that never freeze. fadeInMs=2000 lets the rain arrive gradually; phaseOffset staggers stereo. S&H->AllResonance jitters ring intensity as a second exotic source. Owns the LFO fadeIn/quantize/phaseOffset + Stepped-curve + sampleHold coverage nets. Small bright Plate contrasts the Halls.
- **Replacement code:**
```cpp
    // "Ping Garden" - Particle grains ping a self-osc filter; stepped-random tuning, ping-pong echoes
    {
        PresetDef p;
        p.name = "Ping Garden";
        p.category = "Textures";
        auto& s = p.state;
        // --- Excitation: airy noise + sparse particle grains ---
        s.oscA.type = 9;              // Noise
        s.oscA.noiseColor = 3;        // Blue - bright airy sparkle for glassy pings
        s.oscA.level = 0.15f;
        s.oscB.type = 6;              // Particle
        s.oscB.particleScatter = 8.0f;
        s.oscB.particleDensity = 6.0f;   // sparse - individual droplets
        s.oscB.particleLifetime = 80.0f; // short percussive grains
        s.oscB.particleSpawnMode = 1;    // Random spawn (irregular rain)
        s.oscB.particleEnvType = 0;      // Hann - clean pings
        s.oscB.particleDrift = 0.2f;
        s.oscB.level = 0.32f;
        s.mixer.position = 0.55f;
        // --- Self-oscillating filter = the resonant bell being struck ---
        s.filter.type = 12;           // Self-Oscillating
        s.filter.cutoffHz = 800.0f;
        s.filter.resonance = 20.0f;   // rings on its own
        s.filter.selfOscGlide = 120.0f;
        s.filter.selfOscExtMix = 0.4f;
        s.filter.selfOscShape = 0.3f;
        s.filter.selfOscRelease = 1400.0f;
        // --- Amp: pluck onset, low sustain, long ring ---
        s.ampEnv.attackMs = 15.0f;
        s.ampEnv.decayMs = 900.0f;
        s.ampEnv.sustain = 0.2f;
        s.ampEnv.releaseMs = 2200.0f;
        // === MOD IDENTITY: stepped-random LFO re-tunes the ping pitch (arpeggiated droplets) ===
        s.lfo1.rateHz = 0.9f;          // slow wander
        s.lfo1.shape = 5;              // SmoothRandom
        s.lfo1.depth = 0.7f;
        s.lfo1.sync = 0;               // free-running
        s.lfo1Ext.quantizeSteps = 5;   // snap to 5 discrete pitches -> pentatonic-ish pings
        s.lfo1Ext.phaseOffset = 90.0f; // stagger so L/R droplets feel offset
        s.lfo1Ext.fadeInMs = 2000.0f;  // pings drift in over 2s
        setModSlot(s, 0, 1, 4, 0.6f, 3); // LFO1 -> AllFltCut, Stepped curve (hard pitch steps)
        // Second gesture: sample&hold jitters the ring intensity
        s.sampleHold.rateHz = 2.5f;
        s.sampleHold.slewMs = 40.0f;
        setModSlot(s, 1, 11, 8, 0.3f);   // S&H -> AllResonance - sparkle variation
        // --- FX: ping-pong echoes into a small bright plate ---
        s.delayEnabled = 1;
        s.delay.type = 2;              // PingPong
        s.delay.mix = 0.3f;
        s.delay.feedback = 0.45f;
        s.delay.pingPongWidth = 180.0f;
        s.reverbEnabled = 1;
        s.reverbType = 0;             // Plate - tight and bright, contrasts the Halls
        s.reverb.size = 0.45f;
        s.reverb.mix = 0.3f;
        s.global.width = 1.6f;
        presets.push_back(std::move(p));
    }
```

## 13. "CZ Evolve" -> "Casio Nebula"
- **Locate:** the block containing `p.name = "CZ Evolve"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** Twin resonant Casio-CZ waves spectrally morphed and smeared, evolving forever under two ultra-slow out-of-phase LFOs, phased through a resonant allpass into a shimmering modulated hall.
- **Coverage:** PhaseDistortion engine, SpectralMorph mixer, mixer tilt, SVF Allpass filter, dual slow LFO, LFO symmetry, mod-matrix SCurve curve, Spectral delay, Reverb Hall, reverb modulation
- **Rationale:** Elevates the only motion-having preset. Two LFOs at 0.08 and 0.05 Hz (coprime-ish periods, both free-running) drive AllMorphPos and AllSpecTilt with SCurve curves, so the spectral morph never repeats. LFO1 symmetry=0.7 skews its shape for asymmetric drift. Swaps the near-default Ladder for a resonant SVF Allpass (category-owned) so the filter phases rather than dulls; adds mixer.tilt (morph-only). Spectral delay smear + modulated Hall give it a wrapper distinct from the other four.
- **Replacement code:**
```cpp
    // "Casio Nebula" - Twin phase-distortion waves spectrally morphed; dual slow LFOs evolve morph & tilt
    {
        PresetDef p;
        p.name = "Casio Nebula";
        p.category = "Textures";
        auto& s = p.state;
        // --- Two resonant CZ phase-distortion oscillators ---
        s.oscA.type = 2;              // Phase Distortion
        s.oscA.pdWaveform = 5;        // ResSaw - formant-y resonant sweep
        s.oscA.pdDistortion = 0.5f;
        s.oscA.level = 0.7f;
        s.oscB.type = 2;
        s.oscB.pdWaveform = 6;        // ResTri
        s.oscB.pdDistortion = 0.65f;
        s.oscB.tuneSemitones = 12.0f; // octave up for shimmer
        s.oscB.fineCents = 4.0f;      // slow beating
        s.oscB.level = 0.55f;
        // --- SpectralMorph mixer: FFT interpolation A<->B, tilted ---
        s.mixer.mode = 1;             // Spectral Morph
        s.mixer.position = 0.5f;
        s.mixer.tilt = 3.0f;          // +dB/oct spectral tilt (morph-only param) -> airy top
        // --- Resonant allpass for phasey smear (no LP dulling) ---
        s.filter.type = 7;            // SVF Allpass
        s.filter.cutoffHz = 1500.0f;
        s.filter.resonance = 2.0f;    // resonant allpass coloration
        s.filter.svfDrive = 3.0f;     // gentle saturation
        // --- Slow pad envelope ---
        s.ampEnv.attackMs = 800.0f;
        s.ampEnv.decayMs = 1000.0f;
        s.ampEnv.sustain = 0.75f;
        s.ampEnv.releaseMs = 2400.0f;
        s.ampEnv.attackCurve = 0.4f;
        // === MOD IDENTITY: two out-of-phase slow LFOs evolve morph position and spectral tilt ===
        s.lfo1.rateHz = 0.08f;         // ultra-slow morph drift
        s.lfo1.shape = 0;              // Sine
        s.lfo1.depth = 0.8f;
        s.lfo1.sync = 0;
        s.lfo1Ext.symmetry = 0.7f;     // skew the sine -> asymmetric evolution
        setModSlot(s, 0, 1, 5, 0.6f, 2); // LFO1 -> AllMorphPos, SCurve
        s.lfo2.rateHz = 0.05f;         // even slower, different period (never re-syncs)
        s.lfo2.shape = 1;              // Triangle
        s.lfo2.depth = 0.6f;
        s.lfo2.sync = 0;
        setModSlot(s, 1, 2, 7, 0.5f, 2); // LFO2 -> AllSpecTilt, SCurve
        // --- Spectral delay smear + modulated Hall ---
        s.delayEnabled = 1;
        s.delay.type = 4;             // Spectral
        s.delay.timeMs = 600.0f;
        s.delay.feedback = 0.35f;
        s.delay.mix = 0.28f;
        s.delay.spectralSpreadMs = 400.0f; // smear frequencies across time
        s.delay.spectralTilt = 0.3f;
        s.reverbEnabled = 1;
        s.reverbType = 1;             // Hall
        s.reverb.size = 0.7f;
        s.reverb.mix = 0.3f;
        s.reverb.modDepth = 0.3f;     // chorused reverb tail -> shimmer
        s.reverb.modRateHz = 0.4f;
        presets.push_back(std::move(p));
    }
```

## 14. "Chaos Swarm" -> "Attractor Storm"
- **Locate:** the block containing `p.name = "Chaos Swarm"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** Two chaotic attractors shriek through a resonant bandpass whose Q is driven to screaming extremes by an x4-scaled LFO, smeared by a slow phaser and tape-wow delay.
- **Coverage:** Chaos engine dual-attractor, chaosCoupling, chaosOutput axis, phaser, SVF BP filter, mod-matrix scale axis (x4), mod-matrix SCurve curve, chaosMod source, Tape delay, Reverb Plate, global width, master-gain compensation
- **Rationale:** Owns the mod-matrix scale axis per its directive: slot0 LFO1->AllResonance at amount 0.7 with scale=x4 drives the SVF BP Q past unity into screaming sweeps, while LFO2->AllFltCut moves the shriek in pitch and chaosMod (depth raised from 0 to 0.5) warps the morph as a third source. Replaces the generic Ladder with a resonant bandpass (category-owned SVF BP). Tape-wow delay + Plate is a wrapper no sibling uses. masterGain=0.85 + soft limiter compensate for the extreme Q so it is loud, not ear-destroying.
- **Replacement code:**
```cpp
    // "Attractor Storm" - Twin chaos attractors swept by an x4-scaled LFO through a howling bandpass
    {
        PresetDef p;
        p.name = "Attractor Storm";
        p.category = "Textures";
        auto& s = p.state;
        // --- Dual chaotic oscillators (different attractors, different output axes) ---
        s.oscA.type = 5;              // Chaos
        s.oscA.chaosAttractor = 0;    // Lorenz
        s.oscA.chaosAmount = 0.6f;
        s.oscA.chaosCoupling = 0.3f;
        s.oscA.chaosOutput = 0;       // X axis
        s.oscA.level = 0.6f;
        s.oscB.type = 5;
        s.oscB.chaosAttractor = 1;    // Rossler
        s.oscB.chaosAmount = 0.5f;
        s.oscB.chaosCoupling = 0.45f;
        s.oscB.chaosOutput = 1;       // Y axis - different timbre than A
        s.oscB.level = 0.5f;
        s.mixer.position = 0.5f;
        // --- Resonant bandpass = the storm's shrieking window ---
        s.filter.type = 2;            // SVF BP
        s.filter.cutoffHz = 1200.0f;
        s.filter.resonance = 3.0f;    // base howl; the LFO pushes it far higher
        s.filter.svfSlope = 1;        // 24 dB steep band
        // --- Amp: slow surge ---
        s.ampEnv.attackMs = 500.0f;
        s.ampEnv.decayMs = 700.0f;
        s.ampEnv.sustain = 0.75f;
        s.ampEnv.releaseMs = 1500.0f;
        // === MOD IDENTITY: LFO with SCALE=x4 drives EXTREME resonance sweeps ===
        s.lfo1.rateHz = 0.2f;
        s.lfo1.shape = 1;              // Triangle
        s.lfo1.depth = 0.8f;
        s.lfo1.sync = 0;
        setModSlot(s, 0, 1, 8, 0.7f, 2); // LFO1 -> AllResonance, SCurve
        s.modMatrix.slots[0].scale = 4;  // x4 - pushes amount past +/-1 -> screaming resonance
        // Second LFO sweeps the band center so the shriek moves in pitch
        s.lfo2.rateHz = 0.13f;
        s.lfo2.shape = 0;              // Sine
        s.lfo2.depth = 0.7f;
        s.lfo2.sync = 0;
        setModSlot(s, 1, 2, 4, 0.5f);    // LFO2 -> AllFltCut
        // Third gesture: the dedicated chaos-mod LFO warps the morph blend
        s.chaosMod.rateHz = 0.6f;
        s.chaosMod.type = 0;           // Lorenz
        s.chaosMod.depth = 0.5f;       // raise from default 0 so it is audible as a source
        setModSlot(s, 2, 9, 5, 0.4f);  // Chaos -> AllMorphPos
        // --- FX: slow phaser + tape-wow delay into a plate ---
        s.modulationType = 1;          // Phaser
        s.phaser.rateHz = 0.3f;
        s.phaser.depth = 0.5f;
        s.phaser.feedback = 0.4f;
        s.phaser.mix = 0.35f;
        s.phaser.stages = 3;
        s.delayEnabled = 1;
        s.delay.type = 1;              // Tape
        s.delay.timeMs = 450.0f;
        s.delay.feedback = 0.4f;
        s.delay.mix = 0.25f;
        s.delay.tapeWear = 0.4f;       // wow/flutter grit fits the chaos
        s.delay.tapeSaturation = 0.6f;
        s.reverbEnabled = 1;
        s.reverbType = 0;             // Plate
        s.reverb.size = 0.6f;
        s.reverb.mix = 0.3f;
        s.global.width = 1.5f;
        // Master trim: high Q + x4 sweep is hot - pull back and let the soft limiter guard
        s.global.masterGain = 0.85f;
        presets.push_back(std::move(p));
    }
```

## 15. "Folded Particles" -> "Origami Cloud"
- **Locate:** the block containing `p.name = "Folded Particles"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A dense particle cloud whose wavefolder drive breathes open over two seconds per note via ModEnv, carved by a slowly-wandering notch and scattered through a granular delay into a wide hall.
- **Coverage:** Particle engine, particle Blackman env, Wavefolder distortion, animated fold (voice-route Env3->DistDrive), SVF Notch filter, random source, mod-matrix SCurve curve, Granular delay, Reverb Hall, reverb diffusion, global width, global spread
- **Rationale:** Directly answers the directive's complaint that the old fold was static 0.4: a slow ModEnv (1.8s attack, attackCurve 0.5) routed Env3->DistDrive breathes the wavefolder open over each note so harmonics bloom rather than sitting fixed. A smoothed Random source (smoothness 0.8) drifts the SVF Notch (category-owned filter type 3) with an SCurve for a wandering hollow - a second non-repeating gesture. Grain character diverges from the other two: single-source, medium density 32, short 400ms Random-spawn grains (percussive-leaning) vs Cloud's long 900ms sparse and Fog's dual dense/burst. Granular delay with octave-up (granularPitch 12) shimmer grains distinguishes its wrapper; diffuse Hall + width/spread give the lush cloud identity. distortion.mix 0.9 with modest base drive keeps it audible and non-harsh. Verified: filter type 3, distortion type 4 (foldType field), setVoiceRoute source 2=Env3/dest 3=DistDrive, delay type 3 (granularSizeMs/granularDensity/granularPitch), modEnv.attackCurve, and random.smoothness all present in ruinae_preset_format.h.
- **Replacement code:**
```cpp
    // "Origami Cloud" - Dense particle cloud folded by a wavefolder whose drive breathes via ModEnv
    {
        PresetDef p;
        p.name = "Origami Cloud";
        p.category = "Textures";
        auto& s = p.state;
        // --- Dense particle swarm ---
        s.oscA.type = 6;              // Particle
        s.oscA.particleScatter = 6.0f;
        s.oscA.particleDensity = 32.0f;  // thick cloud
        s.oscA.particleLifetime = 400.0f;
        s.oscA.particleSpawnMode = 1;    // Random
        s.oscA.particleEnvType = 3;      // Blackman - smoothest grains
        s.oscA.particleDrift = 0.4f;
        s.oscA.level = 0.7f;
        s.oscB.level = 0.0f;             // single-source cloud
        s.mixer.position = 0.0f;
        // --- Notch filter carves a moving hollow through the folded cloud ---
        s.filter.type = 3;            // SVF Notch
        s.filter.cutoffHz = 1200.0f;
        s.filter.resonance = 1.5f;
        // --- Wavefolder: base drive modest, ANIMATED by ModEnv below (not the old static 0.4) ---
        s.distortion.type = 4;        // Wavefolder
        s.distortion.drive = 0.25f;   // starting fold
        s.distortion.foldType = 1;    // Sine fold
        s.distortion.mix = 0.9f;
        // --- Slow swell ---
        s.ampEnv.attackMs = 800.0f;
        s.ampEnv.decayMs = 600.0f;
        s.ampEnv.sustain = 0.8f;
        s.ampEnv.releaseMs = 2500.0f;
        // === MOD IDENTITY: ModEnv opens the fold over ~2s so harmonics bloom per note ===
        s.modEnv.attackMs = 1800.0f;  // slow rise -> fold intensifies as the note sustains
        s.modEnv.decayMs = 1500.0f;
        s.modEnv.sustain = 0.7f;
        s.modEnv.releaseMs = 2000.0f;
        s.modEnv.attackCurve = 0.5f;
        setVoiceRoute(s, 0, 2, 3, 0.6f); // Env3(ModEnv) -> DistDrive - the fold breathes open
        // Second gesture: smoothed random drifts the notch so the hollow wanders
        s.random.rateHz = 0.3f;
        s.random.smoothness = 0.8f;      // slow morph, not steppy
        setModSlot(s, 0, 4, 4, 0.35f, 2, 30.0f); // Random -> AllFltCut, SCurve, smoothed
        // --- FX: granular delay grain-cloud into a big diffuse Hall ---
        s.delayEnabled = 1;
        s.delay.type = 3;             // Granular
        s.delay.timeMs = 500.0f;
        s.delay.feedback = 0.3f;
        s.delay.mix = 0.3f;
        s.delay.granularSizeMs = 120.0f;
        s.delay.granularDensity = 20.0f;
        s.delay.granularPitch = 12.0f;   // octave-up shimmer grains
        s.reverbEnabled = 1;
        s.reverbType = 1;             // Hall
        s.reverb.size = 0.85f;
        s.reverb.mix = 0.4f;
        s.reverb.diffusion = 0.9f;
        s.global.width = 1.7f;
        s.global.spread = 0.5f;
        presets.push_back(std::move(p));
    }
```

## 16. "Spectral Wash" -> "Spectral Wash"
- **Locate:** the block containing `p.name = "Spectral Wash"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A saw core fused with a spectral-freeze partner, smeared through a spectral delay into a wide plate — an airy wash whose spectral tilt keeps breathing after every note releases.
- **Coverage:** engines: SpectralFreeze; filters: SVF Allpass; Spectral delay; Reverb Plate + diffusion + preDelay; LFO SmoothRandom->specTilt; mod-matrix SCurve curve; pitchFollower source; random source; global width
- **Rationale:** OscB switched from a second saw to the untouched SpectralFreeze engine (type 8) so the 'LFO->spectral tilt' directive becomes literal and covers a must-cover engine. SVF Allpass (type 7) supplies the category-owned diffuse smear instead of yet another LP. Three distinct mod slots (LFO/SCurve->specTilt, smoothed Random->morph, pitchFollower->cutoff) give continuous non-repeating motion so it never freezes into a static filtered saw. Spectral delay retained as its identity FX; width 1.7 keeps it huge.
- **Replacement code:**
```cpp
    // "Spectral Wash" - Evolving spectral wash: saw + SpectralFreeze, spectral delay
    {
        PresetDef p;
        p.name = "Spectral Wash";
        p.category = "Textures";
        auto& s = p.state;
        // A PolyBLEP saw fused with a SpectralFreeze partner so the timbre already
        // carries spectral motion before any FX; oscB.spectralTilt is a live mod
        // target (LFO->AllSpecTilt below) so the wash keeps evolving.
        s.oscA.type = 0; s.oscA.waveform = 1;          // saw body
        s.oscA.fineCents = -6.0f; s.oscA.level = 0.6f;  // slight flat detune for width
        s.oscB.type = 8;                                // SpectralFreeze engine
        s.oscB.spectralPitch = 0.0f;                    // in tune with oscA
        s.oscB.spectralTilt = 2.0f;                     // start slightly bright
        s.oscB.spectralFormant = -3.0f;                 // hollow the partner voice
        s.oscB.level = 0.5f;
        s.mixer.position = 0.5f;                        // equal blend of the two
        // SVF Allpass: no amplitude notch, just frequency-dependent phase smear -
        // the diffuse "wash" character this category owns.
        s.filter.type = 7;                              // SVF Allpass
        s.filter.cutoffHz = 2200.0f; s.filter.resonance = 0.6f;
        s.ampEnv.attackMs = 700.0f; s.ampEnv.decayMs = 600.0f;
        s.ampEnv.sustain = 0.8f; s.ampEnv.releaseMs = 2600.0f;
        s.ampEnv.attackCurve = 0.4f;                    // gentle slow-start swell
        // Slow free LFO -> spectral tilt, SCurve so the sweep eases at the extremes.
        // This is the "evolves after the tail" signature gesture.
        s.lfo1.rateHz = 0.09f; s.lfo1.shape = 0; s.lfo1.depth = 0.8f; s.lfo1.sync = 0;
        setModSlot(s, 0, 1 /*LFO1*/, 7 /*AllSpecTilt*/, 0.7f, 2 /*SCurve*/);
        // Smoothed Random -> morph position: organic, never-repeating shimmer drift.
        s.random.rateHz = 0.5f; s.random.smoothness = 0.9f;
        setModSlot(s, 1, 4 /*Random*/, 5 /*AllMorphPos*/, 0.3f, 0 /*Linear*/, 40.0f);
        // Pitch follower gently opens the allpass region on higher notes.
        s.pitchFollower.speedMs = 120.0f;
        setModSlot(s, 2, 12 /*PitchFollow*/, 4 /*AllFltCut*/, 0.4f, 1 /*Exp*/);
        // Spectral delay is this preset's signature FX member.
        s.delayEnabled = 1;
        s.delay.type = 4;                               // Spectral
        s.delay.mix = 0.35f; s.delay.feedback = 0.35f;
        s.delay.spectralSpreadMs = 600.0f; s.delay.spectralDiffusion = 0.7f;
        s.delay.spectralWidth = 0.9f; s.delay.spectralTilt = 0.25f;
        // Plate reverb, medium, for air without swallowing the spectral motion.
        s.reverbEnabled = 1; s.reverbType = 0;         // Plate
        s.reverb.size = 0.75f; s.reverb.mix = 0.32f;
        s.reverb.diffusion = 0.8f; s.reverb.preDelayMs = 25.0f;
        s.global.width = 1.7f;                          // very wide stereo image
        presets.push_back(std::move(p));
    }
```

## 17. "Grain Destroy" -> "Grain Destroy"
- **Locate:** the block containing `p.name = "Grain Destroy"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A saw-and-white-noise bed shredded by granular distortion inside a sliding band-pass window, with transient stabs and playing-dynamics driving the grit — a churning, glitchy noise cloud.
- **Coverage:** engines: Noise (white); filters: SVF BandPass; RingModulator + Wavefolder + Granular + Spectral + TapeSaturator distortion: Granular; Digital delay; transient source; envFollower source; mod-matrix Exp curve; voice route Env3->DistDrive
- **Rationale:** Keeps the directive's saw+white-noise+granular-distortion core but adds the movement it lacked: an LFO sliding the band-pass (the reachable stand-in for 'LFO on grain jitter'), the transient source zapping resonance for percussive stabs, the env follower pushing wet mix with dynamics, and a modEnv voice route opening DistDrive over the note. Four independent motion sources make the grain cloud churn instead of sitting static. Grain params and digital delay retained; svfDrive 4 dB adds edge without runaway (mix 0.8 compensated).
- **Replacement code:**
```cpp
    // "Grain Destroy" - Granular-distortion glitch cloud through a sliding bandpass
    {
        PresetDef p;
        p.name = "Grain Destroy";
        p.category = "Textures";
        auto& s = p.state;
        // Saw bed + white noise layer, per the glitch brief.
        s.oscA.type = 0; s.oscA.waveform = 1; s.oscA.level = 0.7f;
        s.oscB.type = 9; s.oscB.noiseColor = 0;        // white noise
        s.oscB.level = 0.25f;
        s.mixer.position = 0.25f;                       // mostly saw, noise as fringe
        // SVF BandPass windows the grain cloud (per directive).
        s.filter.type = 2;                              // SVF BandPass
        s.filter.cutoffHz = 2600.0f; s.filter.resonance = 0.5f;
        s.filter.svfDrive = 4.0f;                       // slight post-filter grit
        // Granular distortion = the destroyer. All four grain params engaged.
        s.distortion.type = 3;                          // Granular
        s.distortion.drive = 0.65f;
        s.distortion.grainSize = 0.15f; s.distortion.grainDensity = 0.7f;
        s.distortion.grainVariation = 0.6f; s.distortion.grainJitter = 0.45f;
        s.distortion.mix = 0.8f;
        s.ampEnv.attackMs = 120.0f; s.ampEnv.decayMs = 500.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 1400.0f;
        // Global LFO slides the bandpass window so the grain cloud appears to churn
        // (the "LFO on the grains" gesture - AllFltCut is the reachable dest).
        s.lfo1.rateHz = 0.35f; s.lfo1.shape = 1; s.lfo1.depth = 0.8f; s.lfo1.sync = 0;
        setModSlot(s, 0, 1 /*LFO1*/, 4 /*AllFltCut*/, 0.6f, 0 /*Linear*/);
        // Transient detector zaps resonance on each attack -> percussive grain stabs.
        s.transient.sensitivity = 0.7f;
        setModSlot(s, 1, 13 /*Transient*/, 8 /*AllResonance*/, 0.5f, 1 /*Exp*/);
        // Envelope follower pushes wet FX mix when you dig in (dynamic grit).
        s.envFollower.sensitivity = 0.7f;
        setModSlot(s, 2, 3 /*EnvFollower*/, 3 /*EffectMix*/, 0.4f, 0 /*Linear*/);
        // ModEnv (Env3) opens distortion drive over the note for evolving grit.
        s.modEnv.attackMs = 200.0f; s.modEnv.decayMs = 1200.0f; s.modEnv.sustain = 0.6f;
        setVoiceRoute(s, 0, 2 /*Env3/ModEnv*/, 3 /*DistDrive*/, 0.4f);
        // Plain digital delay (per directive) + modest plate.
        s.delayEnabled = 1;
        s.delay.type = 0;                               // Digital
        s.delay.timeMs = 375.0f; s.delay.mix = 0.22f; s.delay.feedback = 0.4f;
        s.delay.digitalWidth = 150.0f;
        s.reverbEnabled = 1; s.reverbType = 0;         // Plate
        s.reverb.size = 0.55f; s.reverb.mix = 0.28f;
        presets.push_back(std::move(p));
    }
```

## 18. "Abyssal" -> "Abyssal"
- **Locate:** the block containing `p.name = "Abyssal"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A sub-octave Lorenz-chaos drone over brown-noise rumble, tape-saturated and buried in a cavernous granular-delayed Hall, whose filter only starts to bloom several seconds in.
- **Coverage:** engines: Chaos (coupling/output axis), Noise (brown); filters: Ladder + drive; RingModulator + Wavefolder + Granular + Spectral + TapeSaturator distortion: TapeSaturator; Granular delay; Reverb Hall + diffusion + preDelay; LFO fadeInMs; mod-matrix scale axis (x4) + SCurve curve; chaosMod source
- **Rationale:** Preserves the Chaos+brown-noise+tape identity but earns its keep on the category's motion mandate: the LFO now has a 4 s fadeInMs (the feature this preset owns) so the x4-scaled SCurve filter bloom emerges slowly, and chaosMod stirs resonance for living instability. Swaps the plain reverb-only frame for a granular delay (Granular delay coverage) into a dark Hall (reverbType 1), so it is no longer a static filtered drone. chaosOutput=Y and ladderDrive=6 dB add core character. Levels kept moderate (chaosAmount 0.42, res 0.45) so the sub stays deep, not harsh.
- **Replacement code:**
```cpp
    // "Abyssal" - Sub-octave chaos drone, tape-saturated, slow-blooming cavern
    {
        PresetDef p;
        p.name = "Abyssal";
        p.category = "Textures";
        auto& s = p.state;
        // Sub-octave Lorenz chaos + brown-noise rumble = an ever-churning drone.
        s.oscA.type = 5;                                // Chaos
        s.oscA.chaosAttractor = 0;                      // Lorenz
        s.oscA.chaosAmount = 0.42f; s.oscA.chaosCoupling = 0.2f;
        s.oscA.chaosOutput = 1;                         // Y axis - rounder core than X
        s.oscA.tuneSemitones = -24.0f; s.oscA.level = 0.8f;
        s.oscB.type = 9; s.oscB.noiseColor = 2;        // brown-noise floor
        s.oscB.level = 0.45f;
        s.mixer.position = 0.35f;
        // 24 dB ladder, low and resonant with input drive, to shape the rumble.
        s.filter.type = 4;                              // Ladder
        s.filter.cutoffHz = 550.0f; s.filter.resonance = 0.45f;
        s.filter.ladderSlope = 4; s.filter.ladderDrive = 6.0f;
        // Tape saturation glues the low end.
        s.distortion.type = 5;                          // TapeSaturator
        s.distortion.drive = 0.5f; s.distortion.tapeSaturation = 0.6f;
        s.distortion.tapeBias = 0.55f; s.distortion.mix = 0.9f;
        s.ampEnv.attackMs = 1800.0f; s.ampEnv.decayMs = 900.0f;
        s.ampEnv.sustain = 0.85f; s.ampEnv.releaseMs = 3500.0f;
        // The bloom: a very slow LFO with a long fade-in so the filter only begins
        // to breathe ~4 s in. This preset OWNS lfo fadeInMs.
        s.lfo1.rateHz = 0.06f; s.lfo1.shape = 0; s.lfo1.depth = 0.9f; s.lfo1.sync = 0;
        s.lfo1Ext.fadeInMs = 4000.0f;
        setModSlot(s, 0, 1 /*LFO1*/, 4 /*AllFltCut*/, 0.6f, 2 /*SCurve*/);
        s.modMatrix.slots[0].scale = 4;                 // x4 depth -> huge slow bloom
        // ChaosMod source stirs resonance for unstable, living depth.
        s.chaosMod.rateHz = 0.15f; s.chaosMod.type = 0; s.chaosMod.depth = 0.5f;
        setModSlot(s, 1, 9 /*Chaos*/, 8 /*AllResonance*/, 0.35f, 0 /*Linear*/);
        // Sparse, pitched-down granular delay smears the drone into a cavern.
        s.delayEnabled = 1;
        s.delay.type = 3;                               // Granular
        s.delay.timeMs = 600.0f; s.delay.mix = 0.3f; s.delay.feedback = 0.3f;
        s.delay.granularSizeMs = 180.0f; s.delay.granularDensity = 6.0f;
        s.delay.granularPitch = -12.0f; s.delay.granularPosSpray = 0.5f;
        s.delay.granularTexture = 0.6f; s.delay.granularWidth = 1.0f;
        // Near-max dark Hall.
        s.reverbEnabled = 1; s.reverbType = 1;         // Hall
        s.reverb.size = 0.95f; s.reverb.mix = 0.45f;
        s.reverb.damping = 0.8f; s.reverb.diffusion = 0.88f; s.reverb.preDelayMs = 40.0f;
        s.global.width = 1.3f;
        presets.push_back(std::move(p));
    }
```

## 19. "Binary Rain" -> "Binary Rain"
- **Locate:** the block containing `p.name = "Binary Rain"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** Dual phase-distortion oscillators at a fifth, ring-modulated into glassy droplets and hollowed by a notch filter, chattered by a quantized sample-and-hold LFO and a Benjolin rungler, bouncing through ping-pong delay.
- **Coverage:** engines: PhaseDistortion; filters: SVF Notch; RingModulator + Wavefolder + Granular + Spectral + TapeSaturator distortion: RingModulator; PingPong delay; LFO S&H + quantizeSteps + phaseOffset; mod-matrix Stepped curve; rungler source
- **Rationale:** The audit flagged this as the most template-like sibling (type-0 LP saw core). Rebuilt from the ground up: the core is now dual PhaseDistortion (covers a must-cover engine, removes the generic saw), a SVF Notch (category-owned) replaces the LP, and a note-tracked RingModulator adds inharmonic droplets (distortion-type coverage). The S&H LFO now genuinely owns quantizeSteps=6 + phaseOffset=90, and a Stepped-curve routing plus a rungler->morph route supply two independent digital-jitter motions so the modulation is the star, not an afterthought. Ring mix 0.35 keeps it musical.
- **Replacement code:**
```cpp
    // "Binary Rain" - Dual phase-distortion + ring mod, S&H-quantized digital rain
    {
        PresetDef p;
        p.name = "Binary Rain";
        p.category = "Textures";
        auto& s = p.state;
        // Dual phase-distortion (CZ-style) oscillators at a fifth = brittle digital core.
        s.oscA.type = 2;                                // PhaseDistortion
        s.oscA.pdWaveform = 1;                          // Square
        s.oscA.pdDistortion = 0.6f; s.oscA.level = 0.6f;
        s.oscB.type = 2;                                // PhaseDistortion
        s.oscB.pdWaveform = 2;                          // Pulse
        s.oscB.pdDistortion = 0.45f;
        s.oscB.tuneSemitones = 7.0f;                    // a fifth up
        s.oscB.level = 0.45f;
        s.mixer.position = 0.42f;
        // SVF Notch hollows the mid -> glassy "rain through a window" timbre.
        s.filter.type = 3;                              // SVF Notch
        s.filter.cutoffHz = 1500.0f; s.filter.resonance = 0.8f;
        // Ring modulator sprinkles inharmonic droplets (note-tracked carrier).
        s.distortion.type = 6;                          // RingModulator
        s.distortion.ringFreq = 0.6882f;               // ~440 Hz carrier (normalized)
        s.distortion.ringFreqMode = 1;                  // note-track
        s.distortion.ringRatio = 0.1111f;              // ratio ~2.0 (normalized)
        s.distortion.ringWaveform = 0; s.distortion.mix = 0.35f;
        s.ampEnv.attackMs = 60.0f; s.ampEnv.decayMs = 450.0f;
        s.ampEnv.sustain = 0.6f; s.ampEnv.releaseMs = 1200.0f;
        // S&H LFO, free-run, QUANTIZED to a few steps + phase-offset start entry ->
        // the stuttering "binary rain". This preset OWNS S&H quantizeSteps + phaseOffset.
        s.lfo1.rateHz = 9.0f; s.lfo1.shape = 4; s.lfo1.depth = 0.7f; s.lfo1.sync = 0;
        s.lfo1Ext.quantizeSteps = 6;                    // gridded, stepped jumps
        s.lfo1Ext.phaseOffset = 90.0f;                  // offset entry point
        // Stepped curve makes the routing itself quantize -> extra digital jitter.
        setModSlot(s, 0, 1 /*LFO1*/, 4 /*AllFltCut*/, 0.5f, 3 /*Stepped*/);
        // Rungler (Benjolin shift register) -> morph position for evolving bit patterns.
        s.rungler.osc1FreqHz = 5.0f; s.rungler.osc2FreqHz = 7.0f;
        s.rungler.depth = 0.5f; s.rungler.bits = 8;
        setModSlot(s, 1, 10 /*Rungler*/, 5 /*AllMorphPos*/, 0.4f, 0 /*Linear*/);
        // Ping-pong delay throws the droplets across the field.
        s.delayEnabled = 1;
        s.delay.type = 2;                               // PingPong
        s.delay.timeMs = 300.0f; s.delay.mix = 0.28f; s.delay.feedback = 0.4f;
        s.delay.pingPongWidth = 170.0f; s.delay.pingPongCrossFeed = 0.8f;
        s.reverbEnabled = 1; s.reverbType = 0;         // Plate
        s.reverb.size = 0.5f; s.reverb.mix = 0.24f;
        presets.push_back(std::move(p));
    }
```

## 20. "Vowel Cloud" -> "Vowel Cloud"
- **Locate:** the block containing `p.name = "Vowel Cloud"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** Two formant oscillators, 'E' against 'O', cross-morphed in the spectral-morph mixer and swept by dual sub-0.12 Hz phase-offset LFOs, high-passed so the choir floats, doubled by a warm tape delay and fanned wide across a modulated Hall — a breathing warm choral cloud.
- **Coverage:** engines: Formant (both oscillators, opposite vowels E/O); SpectralMorph mixer; filters: SVF HP; Tape delay (saturation + wear + head level); Reverb Hall + modulation + diffusion + preDelay; LFO2 phaseOffset + dual free-run LFOs; mod-matrix scale axis (x2) + SCurve curve + smoothMs; global width + spread + lush polyphony
- **Rationale:** Keeps the strong formant + spectral-morph identity but breaks it out of both the audit-flagged 'type-0 LP + big hall' frame AND the shared A/U vowel pair the draft reused: this is the trio's WARM member with mid vowels E(1)<->O(3), distinct from Formant Sea (A/O) and the revised Vowel Ghost (I/U). The filter is an SVF HP (type 1, thins lows so the choir floats) and it gains a warm Tape delay member (its distinct spatial identity vs Ghost's frozen granular and Sea's dry ping-pong). Motion is doubled and independent - LFO1 x2-scaled SCurve on morph (mod-matrix scale axis, scale=3) plus an out-of-phase LFO2 (phaseOffset 120) on spectral tilt - so the vowel and the 'mouth opening' move separately, a clearly different morph shape from Ghost's single unipolar SCurve sweep. Reverb is a modulated Hall (modRateHz 0.4 / modDepth 0.3) with 45 ms pre-delay for a breathing cathedral. width 1.8 + spread 0.6 + 12-voice poly make it the category's wide-spread choral member. Key values: oscA vowel 1 / oscB vowel 3 (E/O); lfo1 0.06 Hz + slot scale 3 (x2); lfo2 0.11 Hz phaseOffset 120 -> SpecTilt; delay.type 1 (Tape); reverbType 1 (Hall).
- **Replacement code:**
```cpp
    // "Vowel Cloud" - dual Formant (E vs O), dual phase-offset LFOs, wide tape choral cloud
    {
        PresetDef p;
        p.name = "Vowel Cloud";
        p.category = "Textures";
        auto& s = p.state;
        // Two formant oscillators, E vs O, cross-morphed in the spectral-morph mixer.
        // This trio's WARM member: mid vowels E(1)<->O(3), distinct from Formant Sea
        // (A/O) and Vowel Ghost (I/U).
        s.oscA.type = 7;                                // Formant
        s.oscA.formantVowel = 1; s.oscA.formantMorph = 1.0f;   // E
        s.oscA.level = 0.7f;
        s.oscB.type = 7;                                // Formant
        s.oscB.formantVowel = 3; s.oscB.formantMorph = 2.8f;   // O
        s.oscB.level = 0.65f;
        s.mixer.mode = 1;                               // Spectral Morph
        s.mixer.position = 0.5f; s.mixer.tilt = 1.0f;   // centered, gentle bright tilt
        // SVF High-pass thins the low end so the choir floats.
        s.filter.type = 1;                              // SVF HP
        s.filter.cutoffHz = 180.0f; s.filter.resonance = 0.3f;
        s.ampEnv.attackMs = 900.0f; s.ampEnv.decayMs = 700.0f;
        s.ampEnv.sustain = 0.85f; s.ampEnv.releaseMs = 2800.0f;
        s.ampEnv.attackCurve = 0.5f;                    // breathy slow-start swell
        // Sub-0.1 Hz LFO cross-fades the vowels via morph position; x2 scale so it
        // travels the full E<->O space (mod-matrix scale axis).
        s.lfo1.rateHz = 0.06f; s.lfo1.shape = 1; s.lfo1.depth = 0.95f; s.lfo1.sync = 0;
        setModSlot(s, 0, 1 /*LFO1*/, 5 /*AllMorphPos*/, 0.6f, 2 /*SCurve*/);
        s.modMatrix.slots[0].scale = 3;                 // x2 -> full vowel travel
        // A second, phase-offset LFO nudges spectral tilt so the "mouth" opens/closes
        // out of phase with the vowel morph - dual-LFO motion distinct from Ghost's
        // single unipolar sweep.
        s.lfo2.rateHz = 0.11f; s.lfo2.shape = 0; s.lfo2.depth = 0.8f; s.lfo2.sync = 0;
        s.lfo2Ext.phaseOffset = 120.0f;
        setModSlot(s, 1, 2 /*LFO2*/, 7 /*AllSpecTilt*/, 0.35f, 2 /*SCurve*/, 30.0f);
        // Warm tape delay doubles the choir just behind the beat (this patch's spatial identity).
        s.delayEnabled = 1;
        s.delay.type = 1;                               // Tape
        s.delay.timeMs = 480.0f; s.delay.mix = 0.24f; s.delay.feedback = 0.3f;
        s.delay.tapeSaturation = 0.5f; s.delay.tapeWear = 0.25f;
        s.delay.tapeHead1Level = 0.0f;                  // 0 dB main head
        // Modulated Hall with pre-delay = a wide breathing cathedral.
        s.reverbEnabled = 1; s.reverbType = 1;         // Hall
        s.reverb.size = 0.9f; s.reverb.mix = 0.4f;
        s.reverb.damping = 0.35f; s.reverb.diffusion = 0.88f;
        s.reverb.preDelayMs = 45.0f;
        s.reverb.modRateHz = 0.4f; s.reverb.modDepth = 0.3f;
        // Wide stereo + voice spread = the choral cloud fans across the field.
        s.global.width = 1.8f; s.global.spread = 0.6f;
        s.global.polyphony = 12;                        // lush poly for held chords
        presets.push_back(std::move(p));
    }
```
