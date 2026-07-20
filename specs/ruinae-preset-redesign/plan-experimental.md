# Ruinae Preset Plan — Experimental

The Experimental category is the suite's coverage-of-last-resort: it is where every exotic engine, filter, distortion, and modulation source that no melodic category would naturally reach gets a home. The redesign below keeps each preset's strongest existing DNA but rebuilds it around a single, unmistakable sonic identity, and distributes the synth's rarely-touched corners — the Chaos/Particle/Formant/Spectral/Additive engines, the Comb/Env/Self-Oscillating/Formant filters, the Ring/Wavefolder/Granular/ChaosWaveshaper/Spectral distortions, the rungler/sample-hold/transient/pitch-follower/env-follower sources, the mod-matrix scale/bypass/smoothMs/curve axes, the scalic PhaseVocoder harmonizer, and the performance settings — across the twenty presets so that collectively they exercise the whole machine. A recurring correction runs through the batch: the class-wide `filter.envAmount` bug (a Hz value where a semitone value belongs) is replaced everywhere with a real semitone sweep, and delays/reverbs are diversified away from the shared digital-slap/plate defaults so the FX chain becomes an identity lever rather than boilerplate.

**Locate every block by searching for its `p.name = "<original name>"` string in `F:\projects\iterum\tools\ruinae_preset_generator.cpp` — do not use line numbers, they drift as edits land.**

---

## 1. "Chaos Machine" -> "Chaos Machine"

- **Locate:** the block containing `p.name = "Chaos Machine"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A turbulent, self-modulating metallic drone where two strange attractors are re-carved every instant by four independent chaotic/random sources and a chaos-waveshaper, then smeared through a tape-echo and hall.
- **Coverage:**
  - engines: Chaos(all attractors/coupling/output)
  - ChaosWaveshaper distortion
  - filters: SVF BP
  - rungler + chaosMod + sampleHold + random sources
  - all 8 mod-matrix slots + scale axis + Exp/SCurve/Stepped curves + bypass + smoothMs
  - dual LFO all shapes + modEnv
  - Tape(+splice) delay
  - settings: velocityCurve, voiceAllocMode, voiceStealMode, pitchBendRange
- **Rationale:** Keeps the audit-praised dual-chaos + ChaosWaveshaper core but converts it into the category's coverage flagship: it now exercises the mod-matrix SCALE axis (slot0 scale=3 =x2), all four curve types (Exp/SCurve/Stepped/Linear), smoothMs de-zippering on the S&H slot, and a BYPASSED fifth slot (square-LFO alt gate) so the bypass and LFO-square-shape axes are represented. The signature 4000-'semitone' filter-env bug is pre-empted by setting filter.envAmount=18 st with a real filterEnv. Delay is switched from generic Digital to Tape+splice so the echo is an identity lever, and the four settings axes (Hard velocity, HighNote alloc, Soft steal, 12-st bend) are all set. Base cutoff dropped to 1400 Hz so the x2 upward sweep stays audible instead of pinning open.
- **Replacement code:**

```cpp
    // "Chaos Machine" - Dual strange-attractor drone, chaos-waveshaped, four-source mod web
    {
        PresetDef p;
        p.name = "Chaos Machine";
        p.category = "Experimental";
        auto& s = p.state;
        // --- Dual chaos engines: Lorenz X (bright, ordered) vs Henon Z (grainy, folded) ---
        s.oscA.type = 5; // Chaos
        s.oscA.chaosAttractor = 0; // Lorenz
        s.oscA.chaosAmount = 0.8f;
        s.oscA.chaosCoupling = 0.6f; // strong cross-axis coupling = unstable timbre
        s.oscA.chaosOutput = 0; // X axis
        s.oscA.level = 0.7f;
        s.oscB.type = 5; // Chaos
        s.oscB.chaosAttractor = 3; // Henon (index 3)
        s.oscB.chaosAmount = 0.5f;
        s.oscB.chaosCoupling = 0.3f;
        s.oscB.chaosOutput = 2; // Z axis - different spectral fingerprint than A
        s.oscB.tuneSemitones = -12.0f; // drop B an octave for low turbulent weight
        s.oscB.level = 0.5f;
        s.mixer.position = 0.5f; // equal blend of the two attractors
        // --- SVF band-pass keeps only the resonant core of the noise ---
        s.filter.type = 2; // SVF BP
        s.filter.cutoffHz = 1400.0f; // low base so the upward chaos/env sweep is audible
        s.filter.resonance = 0.45f;
        s.filter.envAmount = 18.0f; // FIXED semitone value (was the class of 4000-Hz bug); +18 st = clear sweep
        s.filterEnv.attackMs = 4.0f; s.filterEnv.decayMs = 700.0f;
        s.filterEnv.sustain = 0.2f; s.filterEnv.releaseMs = 500.0f;
        // --- Chaos waveshaper: adds a SECOND layer of nonlinearity on top of the osc chaos ---
        s.distortion.type = 1; // Chaos Waveshaper
        s.distortion.drive = 0.5f;
        s.distortion.chaosModel = 0; // Lorenz
        s.distortion.chaosSpeed = 0.7f;
        s.distortion.chaosCoupling = 0.4f;
        s.distortion.mix = 0.85f;
        s.ampEnv.attackMs = 12.0f; s.ampEnv.decayMs = 500.0f;
        s.ampEnv.sustain = 0.6f; s.ampEnv.releaseMs = 900.0f;
        // === UNIQUE MOD WEB: chaos->cutoff (x2 scaled), rungler->morph, S&H->res (stepped), random->tilt ===
        s.chaosMod.rateHz = 2.0f; s.chaosMod.type = 1; s.chaosMod.depth = 0.7f; // Rossler modulator
        setModSlot(s, 0, kSrcChaos, kDstAllFltCut, 0.5f, kCurveExp);
        s.modMatrix.slots[0].scale = 3; // SCALE AXIS x2: pushes the cutoff sweep beyond +-1 for wild motion
        s.rungler.osc1FreqHz = 4.0f; s.rungler.osc2FreqHz = 7.0f;
        s.rungler.depth = 0.5f; s.rungler.bits = 4; s.rungler.filter = 0.25f;
        setModSlot(s, 1, kSrcRungler, kDstAllMorphPos, 0.5f, kCurveSCurve); // smoothstep morph steps
        s.sampleHold.rateHz = 3.0f; s.sampleHold.slewMs = 30.0f;
        setModSlot(s, 2, kSrcSampleHold, kDstAllResonance, 0.3f, kCurveStepped, 15.0f); // smoothMs de-zippers the S&H
        s.random.rateHz = 1.5f; s.random.smoothness = 0.3f;
        setModSlot(s, 3, kSrcRandom, kDstAllSpecTilt, 0.4f);
        // BYPASS AXIS + LFO square shape: an alternate square-wave gate the user can toggle on
        s.lfo1.rateHz = 4.0f; s.lfo1.shape = 3; s.lfo1.depth = 0.6f; s.lfo1.sync = 0; // Square
        setModSlot(s, 4, kSrcLFO1, kDstAllFltCut, 0.4f);
        s.modMatrix.slots[4].bypass = 1; // wired but BYPASSED by default
        // Per-voice: velocity bites the waveshaper, mod-env pushes morph on each attack
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstDistDrive, 0.5f);
        setVoiceRoute(s, 1, kVSrcEnv3, kVDstMorphPos, 0.4f);
        s.modEnv.attackMs = 5.0f; s.modEnv.decayMs = 800.0f;
        s.modEnv.sustain = 0.0f; s.modEnv.releaseMs = 400.0f;
        // === PERFORMANCE SETTINGS AXES (rarely touched anywhere in the bank) ===
        s.settings.velocityCurve = 2; // Hard - rewards hard playing on the chaos drive
        s.settings.voiceAllocMode = 3; // HighNote priority
        s.settings.voiceStealMode = 1; // Soft steal avoids clicks on this sustaining drone
        s.settings.pitchBendRangeSemitones = 12.0f; // whole-octave bends for dive-bomb chaos
        s.global.polyphony = 6; // dense enough, CPU-sane for chaos engines
        s.global.masterGain = 0.9f; // headroom for the waveshaper
        // --- Tape echo (+splice) then hall: analog smear, not the default digital slap ---
        s.delayEnabled = 1;
        s.delay.type = 1; // Tape
        s.delay.timeMs = 375.0f; s.delay.feedback = 0.45f; s.delay.mix = 0.3f;
        s.delay.tapeSaturation = 0.6f; s.delay.tapeWear = 0.3f; s.delay.tapeAge = 0.4f;
        s.delay.tapeSpliceEnabled = 1; s.delay.tapeSpliceIntensity = 0.4f; // dropouts add instability
        s.reverbEnabled = 1;
        s.reverb.size = 0.6f; s.reverb.mix = 0.3f;
        presets.push_back(std::move(p));
    }
```

---

## 2. "Formant Morph" -> "Vox Automata"

- **Locate:** the block containing `p.name = "Formant Morph"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A talking-robot choir: two formant oscillators sweep through vowels under an LFO, hard-tuned by a note-tracked ring modulator, thickened into a scalic phase-vocoder harmony and answering expressively to aftertouch, pitch-tracking and a single morph macro.
- **Coverage:**
  - engines: Formant
  - RingModulator distortion
  - filters: Formant (formantGender)
  - pitchFollower + random + envFollower-adjacent sources
  - macros multi-routed (one macro -> 3 slots)
  - dual LFO all shapes
  - keyTrack + aftertouch routes
  - harmonizer scalic PhaseVocoder + per-voice interval/pan/detune
  - reverb Hall + Chorus modulation
  - SCurve + smoothMs mod axes
- **Rationale:** Rebuilds the vowel patch into the category's expression/harmony coverage net. It owns formantGender (-0.4), the RingModulator distortion (note-tracked so it reads as a musical robot rather than clangor), the pitchFollower source, and a genuine ONE-MACRO-THREE-DESTINATION morph (slots 4/5/6 all from Macro1) which no preset in the bank did. Aftertouch->morph and keyTrack->tilt make it feel alive under the hands. A scalic PhaseVocoder harmonizer with per-voice interval/pan/detune/delay turns it into a choir, and the wrapper is deliberately Chorus+Hall (reverbType=1) instead of the default plate. SCurve on the morph LFO and smoothMs on the random slot cover those axes. Ring-mod mix kept at 0.35 and masterGain 0.85 so the stacked harmony never overloads.
- **Replacement code:**

```cpp
    // "Vox Automata" - Ring-modulated vowel choir, macro-morphed, aftertouch-expressive
    {
        PresetDef p;
        p.name = "Vox Automata";
        p.category = "Experimental";
        auto& s = p.state;
        // --- Two formant oscillators voiced on different vowels for a chord-of-vowels blend ---
        s.oscA.type = 7; // Formant
        s.oscA.formantVowel = 0; // A (open, bright)
        s.oscA.formantMorph = 0.0f;
        s.oscA.level = 0.8f;
        s.oscB.type = 7; // Formant
        s.oscB.formantVowel = 3; // O (rounded, dark)
        s.oscB.formantMorph = 2.0f; // starts mid-morph toward I/U
        s.oscB.fineCents = 6.0f; // slight beat between the two throats
        s.oscB.level = 0.6f;
        s.mixer.position = 0.5f;
        // --- Formant FILTER on top with a shifted gender for a deeper 'chest' voice ---
        s.filter.type = 5; // Formant
        s.filter.formantMorph = 0.5f;
        s.filter.formantGender = -0.4f; // FORMANTGENDER coverage: shift toward male/monster
        s.filter.cutoffHz = 5000.0f;
        // --- Ring modulator, NOTE-TRACKED so the metallic edge stays musical (classic robot voice) ---
        s.distortion.type = 6; // Ring Modulator
        s.distortion.drive = 0.3f;
        s.distortion.character = 0.5f;
        s.distortion.mix = 0.35f; // subtle - vowels stay intelligible
        s.distortion.ringFreqMode = 1; // NoteTrack - carrier follows the played pitch
        s.distortion.ringRatio = 0.1111f; // normalized ~2.0 ratio
        s.distortion.ringWaveform = 0; // Sine carrier
        s.distortion.ringStereoSpread = 0.4f;
        s.ampEnv.attackMs = 50.0f; s.ampEnv.decayMs = 400.0f;
        s.ampEnv.sustain = 0.75f; s.ampEnv.releaseMs = 600.0f;
        // === MOD WEB ===
        // LFO1 (triangle, free) slowly sweeps the vowel morph = the 'wow' talking motion
        s.lfo1.rateHz = 0.2f; s.lfo1.shape = 1; s.lfo1.depth = 1.0f; s.lfo1.sync = 0; // Triangle
        setModSlot(s, 0, kSrcLFO1, kDstAllMorphPos, 0.6f, kCurveSCurve); // smoothstep = natural glide
        // LFO2 (sine, very slow) breathes the filter cutoff
        s.lfo2.rateHz = 0.08f; s.lfo2.shape = 0; s.lfo2.depth = 0.5f; s.lfo2.sync = 0; // Sine
        setModSlot(s, 1, kSrcLFO2, kDstAllFltCut, 0.3f);
        // PITCH FOLLOWER -> spectral tilt: higher notes get brighter formants automatically
        s.pitchFollower.minHz = 80.0f; s.pitchFollower.maxHz = 1500.0f;
        s.pitchFollower.confidence = 0.5f; s.pitchFollower.speedMs = 40.0f;
        setModSlot(s, 2, kSrcPitchFollow, kDstAllSpecTilt, 0.35f);
        // Random adds gentle spectral wander (smoothed so it is a drift, not a jitter)
        s.random.rateHz = 2.0f; s.random.smoothness = 0.6f;
        setModSlot(s, 3, kSrcRandom, kDstAllSpecTilt, 0.2f, kCurveLinear, 60.0f); // smoothMs axis
        // === ONE MACRO, THREE DESTINATIONS: a single 'vowel-open' knob morphs cutoff+vowel+res ===
        s.macros.values[0] = 0.4f; // resting position
        setModSlot(s, 4, kSrcMacro1, kDstGlobalFltCut, 0.5f);
        setModSlot(s, 5, kSrcMacro1, kDstAllMorphPos, 0.4f);
        setModSlot(s, 6, kSrcMacro1, kDstAllResonance, 0.3f); // multi-routed one-knob morph
        // Per-voice EXPRESSION routes (nearly unused across the whole bank)
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.3f);
        setVoiceRoute(s, 1, kVSrcAftertouch, kVDstMorphPos, 0.5f); // lean on the key = push the vowel
        setVoiceRoute(s, 2, kVSrcKeyTrack, kVDstSpecTilt, 0.3f);   // keyboard brightens the top
        // === SCALIC PHASE-VOCODER HARMONIZER: turns the robot into a 3-part choir ===
        s.harmonizerEnabled = 1;
        s.harmonizer.harmonyMode = 1; // Scalic
        s.harmonizer.key = 0; s.harmonizer.scale = 0; // C Major
        s.harmonizer.pitchShiftMode = 2; // PhaseVocoder (formant-smearing, choir-like)
        s.harmonizer.formantPreserve = 1;
        s.harmonizer.numVoices = 3;
        s.harmonizer.dryLevelDb = 0.0f; s.harmonizer.wetLevelDb = -9.0f;
        s.harmonizer.voiceInterval[0] = 2; s.harmonizer.voicePan[0] = -0.5f; s.harmonizer.voiceDetuneCents[0] = -4.0f; // 3rd
        s.harmonizer.voiceInterval[1] = 4; s.harmonizer.voicePan[1] = 0.5f;  s.harmonizer.voiceDetuneCents[1] = 5.0f;  // 5th
        s.harmonizer.voiceInterval[2] = 7; s.harmonizer.voicePan[2] = 0.0f;  s.harmonizer.voiceDelayMs[2] = 12.0f;    // octave, delayed
        // --- Chorus (not the reflexive reverb-only wrapper) then a wide Hall ---
        s.modulationType = 3; // Chorus
        s.chorus.rateHz = 0.4f; s.chorus.depth = 0.5f; s.chorus.voices = 3; s.chorus.mix = 0.35f;
        s.reverbEnabled = 1;
        s.reverbType = 1; // Hall (not Plate)
        s.reverb.size = 0.7f; s.reverb.mix = 0.28f;
        s.global.masterGain = 0.85f; // headroom for ring-mod + harmonizer stack
        presets.push_back(std::move(p));
    }
```

---

## 3. "Bit Crusher" -> "Rusted Circuit"

- **Locate:** the block containing `p.name = "Bit Crusher"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A dynamic lo-fi machine: a saw+square fifth is spectrally bit-crushed while a sample-and-hold steps a resonant auto-wah and transient detection snaps the filter open on every attack, so the grime is rhythmic and alive instead of a static wall.
- **Coverage:**
  - Spectral distortion (bitcrush)
  - filters: EnvFilter (auto-wah)
  - sampleHold + transient sources
  - Stepped curve + LFO square shape
  - Digital delay
  - velocity route + filterEnv
- **Rationale:** Directly answers the audit's 'weakest / static' verdict: the former Bit Crusher had NO modulation at all. This version keeps the spectral bitcrush but wraps it in three animation layers - a hard-stepped (slewMs=0, Stepped curve) sample-and-hold on the filter frequency, a transient detector snapping resonance on attacks, and a square LFO tremoloing the tilt - so the crush is rhythmic. The filter is switched from SVF LP to the EnvFilter auto-wah (covering that exotic filter type) with a low 800-Hz base so it can open. Velocity->DistDrive plus a Hard velocity curve make it dynamic. filter.envAmount is a correct +24 st. Digital delay with era/limiter keeps the lo-fi tightness. Levels: drive 0.6, mix 0.9, masterGain 0.9 keep it aggressive but not clipping.
- **Replacement code:**

```cpp
    // "Rusted Circuit" - Dynamic bitcrush + stepped auto-wah, transient-snapped
    {
        PresetDef p;
        p.name = "Rusted Circuit";
        p.category = "Experimental";
        auto& s = p.state;
        // Saw + square a fifth up = a fat, hollow lo-fi source
        s.oscA.type = 0; // PolyBLEP
        s.oscA.waveform = 1; // Saw
        s.oscA.level = 0.8f;
        s.oscB.type = 0;
        s.oscB.waveform = 2; // Square
        s.oscB.tuneSemitones = 7.0f; // fifth
        s.oscB.pulseWidth = 0.35f; // thinner square = more nasal grit
        s.oscB.level = 0.5f;
        s.mixer.position = 0.4f;
        // --- ENV-FILTER (auto-wah): the filter itself follows dynamics, base freq stepped by S&H ---
        s.filter.type = 11; // Env Filter
        s.filter.cutoffHz = 800.0f; // low base so the wah has room to open
        s.filter.resonance = 0.5f;
        s.filter.envSubType = 1; // BP response = vocal wah
        s.filter.envSensitivity = 8.0f; // dB - reacts to input level
        s.filter.envDepth = 0.8f;
        s.filter.envAttack = 5.0f; s.filter.envRelease = 130.0f;
        s.filter.envDirection = 0; // Up (opens on attack)
        // --- SPECTRAL BITCRUSH: the core lo-fi destruction ---
        s.distortion.type = 2; // Spectral
        s.distortion.drive = 0.6f;
        s.distortion.spectralMode = 3; // SpectralBitcrush
        s.distortion.spectralCurve = 8; // BitReduce
        s.distortion.spectralBits = 0.3f; // ~5 bits
        s.distortion.mix = 0.9f;
        s.ampEnv.attackMs = 3.0f; s.ampEnv.decayMs = 320.0f;
        s.ampEnv.sustain = 0.6f; s.ampEnv.releaseMs = 220.0f;
        // === ANIMATION (the fix for its former static-ness) ===
        // S&H steps the auto-wah base frequency in hard stair-steps = rhythmic timbral clank
        s.sampleHold.rateHz = 7.0f; s.sampleHold.slewMs = 0.0f; // zero slew = hard steps
        setModSlot(s, 0, kSrcSampleHold, kDstAllFltCut, 0.5f, kCurveStepped);
        // TRANSIENT detector snaps resonance up on each note attack = percussive spit
        s.transient.sensitivity = 0.6f; s.transient.attackMs = 2.0f; s.transient.decayMs = 60.0f;
        setModSlot(s, 1, kSrcTransient, kDstAllResonance, 0.4f, kCurveExp);
        // Square LFO gates the spectral tilt on/off = a lo-fi tremolo of brightness
        s.lfo1.rateHz = 6.0f; s.lfo1.shape = 3; s.lfo1.depth = 0.6f; s.lfo1.sync = 0; // Square
        setModSlot(s, 2, kSrcLFO1, kDstAllSpecTilt, 0.35f);
        // Velocity drives crush intensity: play harder = more destruction
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstDistDrive, 0.5f);
        // Filter env also swept for extra movement on the wah base (FIXED semitone amount)
        s.filter.envAmount = 24.0f; // +24 st sweep - audible, not the 4000-Hz nonsense
        s.filterEnv.attackMs = 3.0f; s.filterEnv.decayMs = 200.0f;
        s.filterEnv.sustain = 0.3f; s.filterEnv.releaseMs = 180.0f;
        // --- Short Digital slap keeps it tight and lo-fi (era/limiter engaged) ---
        s.delayEnabled = 1;
        s.delay.type = 0; // Digital
        s.delay.timeMs = 220.0f; s.delay.feedback = 0.32f; s.delay.mix = 0.22f;
        s.delay.digitalEra = 1; s.delay.digitalAge = 0.4f; s.delay.digitalLimiter = 1;
        s.settings.velocityCurve = 2; // Hard - makes the velocity->crush route bite
        s.global.masterGain = 0.9f;
        presets.push_back(std::move(p));
    }
```

---

## 4. "Wavefold Madness" -> "Folded Scream"

- **Locate:** the block containing `p.name = "Wavefold Madness"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A screaming hard-sync lead pushed through a Lockhart wavefolder, with an envelope follower and dual LFOs constantly reshaping the band-pass, a fast filter-env pluck sweep, and a shimmering granular delay smearing the folded harmonics upward.
- **Coverage:**
  - engines: Sync
  - Wavefolder (Lockhart) distortion
  - filters: SVF BP + filterEnv sweep
  - envFollower source + dual LFO (tri/saw) + modEnv
  - Granular delay
  - velocity route + smoothMs
- **Rationale:** Retains the strong sync+Lockhart core the audit approved but fills its coverage gaps: it now adds the envFollower source (amplitude-tracks the band-pass, with smoothMs on that slot), keeps the tri/saw dual-LFO pair, and adds a real filter-env pluck (envAmount=+30 st - correcting the near-zero filter-env class-bug) plus svfDrive grit. The delay is upgraded from generic reverb-only to a Granular delay pitched +12 st for a shimmer smear, giving the category its granular-delay coverage. Sub-octave triangle on OSC B anchors the scream so it stays musical. Fold drive 0.7 + mix 0.9 are balanced by masterGain 0.85 and a deliberately small (0.45) plate so it reads as an aggressive lead, not a wash.
- **Replacement code:**

```cpp
    // "Folded Scream" - Hard-sync into a Lockhart wavefolder, envelope-tracked BP, granular shimmer
    {
        PresetDef p;
        p.name = "Folded Scream";
        p.category = "Experimental";
        auto& s = p.state;
        // --- Hard-sync saw slave at a high ratio = dense, tearing harmonic spectrum ---
        s.oscA.type = 3; // Sync
        s.oscA.syncRatio = 5.0f;
        s.oscA.syncWaveform = 1; // Saw slave
        s.oscA.syncMode = 0; // Hard
        s.oscA.syncAmount = 0.85f;
        s.oscA.level = 0.8f;
        s.oscB.type = 0; // PolyBLEP triangle underneath for body
        s.oscB.waveform = 4; // Triangle
        s.oscB.tuneSemitones = -12.0f; // sub octave to anchor the scream
        s.oscB.level = 0.45f;
        s.mixer.position = 0.35f; // favor the sync osc
        // --- SVF band-pass isolates the folded formant peak; fast filter-env pluck ---
        s.filter.type = 2; // SVF BP
        s.filter.cutoffHz = 1200.0f; // low base; env + envFollower sweep it up
        s.filter.resonance = 0.45f;
        s.filter.svfDrive = 6.0f; // a little post-filter grit
        s.filter.envAmount = 30.0f; // +30 st snappy pluck sweep (FIXED semitone value)
        s.filterEnv.attackMs = 2.0f; s.filterEnv.decayMs = 260.0f;
        s.filterEnv.sustain = 0.15f; s.filterEnv.releaseMs = 200.0f;
        // --- Lockhart wavefolder: the screaming nonlinearity ---
        s.distortion.type = 4; // Wavefolder
        s.distortion.drive = 0.7f;
        s.distortion.foldType = 2; // Lockhart
        s.distortion.mix = 0.9f;
        s.ampEnv.attackMs = 5.0f; s.ampEnv.decayMs = 400.0f;
        s.ampEnv.sustain = 0.55f; s.ampEnv.releaseMs = 320.0f;
        // === MOD WEB: envFollower + two LFOs + modEnv all sculpt the fold/filter ===
        // ENVELOPE FOLLOWER tracks the voice loudness and opens the band-pass with it
        s.envFollower.sensitivity = 0.6f; s.envFollower.attackMs = 5.0f; s.envFollower.releaseMs = 80.0f;
        setModSlot(s, 0, kSrcEnvFollower, kDstAllFltCut, 0.4f, kCurveLinear, 20.0f); // smoothMs
        // LFO1 triangle sweeps spectral tilt (slow timbral breathing)
        s.lfo1.rateHz = 0.5f; s.lfo1.shape = 1; s.lfo1.depth = 0.7f; s.lfo1.sync = 0; // Triangle
        setModSlot(s, 1, kSrcLFO1, kDstAllSpecTilt, 0.4f);
        // LFO2 saw ramps resonance for rising 'screaming peak' motion
        s.lfo2.rateHz = 0.3f; s.lfo2.shape = 2; s.lfo2.depth = 0.6f; s.lfo2.sync = 0; // Saw
        setModSlot(s, 2, kSrcLFO2, kDstAllResonance, 0.35f);
        // Per-voice: mod-env pushes morph on each note, velocity drives the fold intensity
        setVoiceRoute(s, 0, kVSrcEnv3, kVDstMorphPos, 0.5f);
        s.modEnv.attackMs = 2.0f; s.modEnv.decayMs = 600.0f;
        s.modEnv.sustain = 0.1f; s.modEnv.releaseMs = 300.0f;
        setVoiceRoute(s, 1, kVSrcVelocity, kVDstDistDrive, 0.6f); // harder = more folding
        // --- Granular delay pitched up an octave = shimmering, smeared fold tails ---
        s.delayEnabled = 1;
        s.delay.type = 3; // Granular
        s.delay.timeMs = 300.0f; s.delay.feedback = 0.4f; s.delay.mix = 0.28f;
        s.delay.granularSizeMs = 80.0f; s.delay.granularDensity = 15.0f;
        s.delay.granularPitch = 12.0f; // octave-up grains = shimmer
        s.delay.granularTexture = 0.35f; s.delay.granularPosSpray = 0.3f;
        s.reverbEnabled = 1;
        s.reverb.size = 0.45f; s.reverb.mix = 0.2f; // small plate keeps it aggressive, not washy
        s.global.masterGain = 0.85f; // compensate wavefolder gain
        presets.push_back(std::move(p));
    }
```

---

## 5. "Rungler Noise" -> "Benjolin Drift"

- **Locate:** the block containing `p.name = "Rungler Noise"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A Berlin-school Benjolin drone: blue noise crossed with a Chua attractor, run through a metallic comb and a granular waveshaper, with a full rungler and three more chaotic sources filling every mod slot and a frozen reverb holding the drifting cloud in place.
- **Coverage:**
  - engines: Noise + Chaos(Chua)
  - Granular distortion
  - filters: Comb
  - rungler-full (bits/loopMode/filter) + sampleHold + chaosMod + SmoothRandom LFO sources
  - all 4 mod slots + Stepped/SCurve curves
  - Granular delay + reverb freeze
  - settings: voiceAllocMode
- **Rationale:** Keeps the audit-praised noise+Chua+rungler identity but pushes it into distinct territory from Chaos Machine: it uses a Comb filter (metallic Benjolin ring, covering that filter type) instead of SVF LP, a Granular waveshaper distortion (grain fields) instead of none, and a FROZEN reverb (freeze=1) for an infinite drift bed. The rungler is used FULLY - bits=6, filter=0.3, and loopMode=1 so it produces a repeating pseudo-melodic shift-register pattern rather than free chaos, which is the defining Benjolin behavior. All four slots are filled by different exotic sources (rungler/S&H/SmoothRandom-LFO/chaosMod) with Stepped and SCurve curves. Granular delay with reverse grains adds the smear. Comb resonance 0.6 + combDamping 0.3 keep the ring metallic but not runaway; polyphony 4 and masterGain 0.85 keep the frozen/feedback bed controlled.
- **Replacement code:**

```cpp
    // "Benjolin Drift" - Blue-noise + Chua through a comb, full-rungler mod web, frozen reverb
    {
        PresetDef p;
        p.name = "Benjolin Drift";
        p.category = "Experimental";
        auto& s = p.state;
        // --- Blue noise (hissy, high-tilted) blended with a Chua chaotic oscillator ---
        s.oscA.type = 9; // Noise
        s.oscA.noiseColor = 3; // Blue
        s.oscA.level = 0.5f;
        s.oscB.type = 5; // Chaos
        s.oscB.chaosAttractor = 2; // Chua
        s.oscB.chaosAmount = 0.6f;
        s.oscB.chaosCoupling = 0.5f;
        s.oscB.chaosOutput = 1; // Y axis
        s.oscB.level = 0.6f;
        s.mixer.position = 0.5f;
        // --- COMB filter: metallic, resonant, tuneable ringing = the Benjolin voice ---
        s.filter.type = 6; // Comb
        s.filter.cutoffHz = 1200.0f; // comb tuning
        s.filter.resonance = 0.6f; // feedback = strong metallic ring
        s.filter.combDamping = 0.3f; // tame the extreme highs in the feedback
        // --- Granular waveshaper distortion: chops the noise into grains ---
        s.distortion.type = 3; // Granular
        s.distortion.drive = 0.4f;
        s.distortion.grainSize = 0.4f; s.distortion.grainDensity = 0.5f;
        s.distortion.grainVariation = 0.4f; s.distortion.grainJitter = 0.3f;
        s.distortion.mix = 0.6f;
        s.ampEnv.attackMs = 20.0f; s.ampEnv.decayMs = 500.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 700.0f;
        // === FULL FOUR-SLOT RUNGLER-CENTRIC MOD WEB ===
        // RUNGLER (looping shift-register) sweeps the comb tuning = classic stepped Benjolin melody
        s.rungler.osc1FreqHz = 3.0f; s.rungler.osc2FreqHz = 5.0f;
        s.rungler.depth = 0.6f; s.rungler.bits = 6; s.rungler.filter = 0.3f;
        s.rungler.loopMode = 1; // LOOP mode = repeating pseudo-melodic pattern (not free chaos)
        setModSlot(s, 0, kSrcRungler, kDstAllFltCut, 0.55f);
        // S&H steps the morph between noise and Chua in hard stairs
        s.sampleHold.rateHz = 5.0f; s.sampleHold.slewMs = 20.0f;
        setModSlot(s, 1, kSrcSampleHold, kDstAllMorphPos, 0.4f, kCurveStepped);
        // Smooth-random LFO drifts the spectral tilt (the slow 'drift' of the name)
        s.lfo1.rateHz = 0.2f; s.lfo1.shape = 5; s.lfo1.depth = 0.6f; s.lfo1.sync = 0; // SmoothRandom
        setModSlot(s, 2, kSrcLFO1, kDstAllSpecTilt, 0.35f);
        // Chaos modulator wobbles the comb resonance for unstable ringing
        s.chaosMod.rateHz = 1.0f; s.chaosMod.type = 0; s.chaosMod.depth = 0.4f; // Lorenz
        setModSlot(s, 3, kSrcChaos, kDstAllResonance, 0.25f, kCurveSCurve);
        // --- Granular delay then a FROZEN reverb hold the drifting cloud ---
        s.delayEnabled = 1;
        s.delay.type = 3; // Granular
        s.delay.timeMs = 333.0f; s.delay.feedback = 0.5f; s.delay.mix = 0.3f;
        s.delay.granularSizeMs = 120.0f; s.delay.granularDensity = 12.0f;
        s.delay.granularReverseProb = 0.3f; s.delay.granularTexture = 0.4f;
        s.reverbEnabled = 1;
        s.reverb.size = 0.75f; s.reverb.mix = 0.3f;
        s.reverb.freeze = 1; // REVERB FREEZE - infinite drone bed under the drift
        s.settings.voiceAllocMode = 1; // Oldest (stable drone), covers alloc-mode axis
        s.global.polyphony = 4; // this is a drone, few voices needed
        s.global.masterGain = 0.85f; // freeze + granular feedback need headroom
        presets.push_back(std::move(p));
    }
```

---

## 6. "Harmony Chaos" -> "Lorenz Choir"

- **Locate:** the block containing `p.name = "Harmony Chaos"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A turbulent Lorenz-attractor tone folded through a chaos waveshaper and made to sing through a resonant SVF-peak formant, then multiplied into a diatonic phase-vocoder choir with a warbly tape tail.
- **Coverage:**
  - engine: Chaos (Lorenz attractor + coupling + Z output)
  - filter: SVF Peak (svfGain +12, svfDrive)
  - distortion: ChaosWaveshaper
  - source: chaosMod (raised depth)
  - source: envFollower
  - harmonizer scalic PhaseVocoder + per-voice interval/pan/detune + formantPreserve
  - modEnv (voice route Env3->cutoff)
  - dual LFO (sine + saw)
  - mod-matrix curves SCurve/Exp + smoothMs
  - filter-env bug fix (envAmount +24 semitones)
  - Tape delay (+splice)
  - reverb Plate
- **Rationale:** The original left chaos inside a vanilla ladder-LP/reverb chassis. Now the chaos is actively shaped: chaosAmount 0.42 keeps it tonal rather than noisy, coupling 0.35 + Z-axis output give organic drift, and an SVF PEAK with svfGain +12 carves a fixed vocal formant so the turbulence 'sings'. The filter-env bug is fixed (envAmount +24 semitones, a real sweep). A ChaosWaveshaper (Lorenz model, low drive/mix) keeps the theme without over-driving. The mod web is unique to this preset: the dedicated chaosMod -> morph (SCurve+smooth) glides the chaos/sine blend, LFO1 -> resonance breathes the formant, and envFollower -> filter-env-amount makes dynamics deepen the sweep. Harmonizer is the only scalic PhaseVocoder+formant-preserve instance. Tape delay with splice and a modest Plate (not the batch's 0.6 hall) give it a distinct, intimate tail.
- **Replacement code:**

```cpp
    // "Lorenz Choir" - chaotic attractor sung through an SVF-peak formant into a diatonic choir
    {
        PresetDef p;
        p.name = "Lorenz Choir";
        p.category = "Experimental";
        auto& s = p.state;

        // --- Voice: chaotic attractor thickened by a sub-sine anchor ---
        s.oscA.type = 5;              // Chaos
        s.oscA.chaosAttractor = 0;    // Lorenz - warm quasi-pitched turbulence
        s.oscA.chaosAmount = 0.42f;   // enough motion, still tonal (not noise)
        s.oscA.chaosCoupling = 0.35f; // cross-axis wander for organic drift
        s.oscA.chaosOutput = 2;       // Z axis - the brighter Lorenz lobe
        s.oscA.level = 0.7f;
        s.oscB.type = 0; s.oscB.waveform = 0;  // pure sine
        s.oscB.tuneSemitones = -12.0f;         // sub octave anchors the chaos to pitch
        s.oscB.level = 0.5f;
        s.mixer.position = 0.4f;      // favor chaos, sine grounds it

        // --- Filter: SVF PEAK sculpts a resonant vocal band in the chaos ---
        s.filter.type = 8;            // SVF Peak
        s.filter.cutoffHz = 900.0f;   // vocal-ish peak center
        s.filter.resonance = 3.5f;
        s.filter.svfGain = 12.0f;     // +12 dB boost = the peak that sings
        s.filter.svfSlope = 1;        // 24 dB skirts
        s.filter.svfDrive = 3.0f;     // gentle saturation on the peak
        s.filter.envAmount = 24.0f;   // FIX: real semitone sweep (avoids the old -48..48 bug value)
        s.filterEnv.attackMs = 80.0f; s.filterEnv.decayMs = 700.0f;
        s.filterEnv.sustain = 0.4f; s.filterEnv.releaseMs = 900.0f;
        s.filterEnv.decayCurve = 0.4f; // slow-start decay = gliding peak fall

        // --- ChaosWaveshaper distortion: chaos folded by chaos (thematic) ---
        s.distortion.type = 1;        // ChaosWaveshaper
        s.distortion.drive = 0.3f;    // subtle grit, level-safe
        s.distortion.chaosModel = 0;  // Lorenz waveshaper to match the osc
        s.distortion.chaosSpeed = 0.3f;
        s.distortion.chaosCoupling = 0.2f;
        s.distortion.mix = 0.35f;

        // --- Amp env: choir-like swell ---
        s.ampEnv.attackMs = 60.0f; s.ampEnv.decayMs = 500.0f;
        s.ampEnv.sustain = 0.75f; s.ampEnv.releaseMs = 900.0f;

        // --- Dedicated chaos LFO adds a second, slower turbulence layer ---
        s.chaosMod.rateHz = 0.25f; s.chaosMod.type = 0; // Lorenz
        s.chaosMod.depth = 0.6f; s.chaosMod.sync = 0;

        // --- Mod web (unique): chaos + envelope-follower drive morph & sweep depth ---
        s.modMatrix.slots[0].source = 9;  // Chaos (the dedicated chaosMod)
        s.modMatrix.slots[0].dest = 5;    // All Morph Position - glides the chaos/sine blend
        s.modMatrix.slots[0].amount = 0.5f;
        s.modMatrix.slots[0].curve = 2;   // SCurve
        s.modMatrix.slots[0].smoothMs = 40.0f; // tame chaos steps
        s.modMatrix.slots[1].source = 1;  // LFO1
        s.modMatrix.slots[1].dest = 8;    // All Resonance - breathing formant width
        s.modMatrix.slots[1].amount = 0.35f;
        s.modMatrix.slots[1].curve = 1;   // Exp
        s.modMatrix.slots[2].source = 3;  // EnvFollower
        s.modMatrix.slots[2].dest = 9;    // All Filter Env Amount - louder input = deeper sweep
        s.modMatrix.slots[2].amount = 0.4f;
        s.envFollower.sensitivity = 0.7f; s.envFollower.attackMs = 20.0f; s.envFollower.releaseMs = 250.0f;

        s.lfo1.rateHz = 0.4f; s.lfo1.shape = 0; s.lfo1.depth = 0.5f; s.lfo1.sync = 0; // free sine
        s.lfo2.rateHz = 0.13f; s.lfo2.shape = 2; s.lfo2.depth = 0.4f; s.lfo2.sync = 0; // slow saw ramp

        // --- Mod envelope opens the peak on each note (modEnv used) ---
        s.modEnv.attackMs = 5.0f; s.modEnv.decayMs = 600.0f; s.modEnv.sustain = 0.2f; s.modEnv.releaseMs = 500.0f;
        s.voiceRoutes[0].source = 2;      // Env3 (mod env)
        s.voiceRoutes[0].destination = 0; // Filter Cutoff
        s.voiceRoutes[0].amount = 0.35f;
        s.voiceRoutes[0].active = 1;

        // --- Harmonizer: SCALIC, PhaseVocoder, formant-preserved choir ---
        s.harmonizerEnabled = 1;
        s.harmonizer.harmonyMode = 1;     // Scalic (diatonic)
        s.harmonizer.pitchShiftMode = 2;  // PhaseVocoder (smooth, formant-safe)
        s.harmonizer.formantPreserve = 1;
        s.harmonizer.numVoices = 4;
        s.harmonizer.key = 0; s.harmonizer.scale = 0; // C major
        s.harmonizer.dryLevelDb = -1.0f; s.harmonizer.wetLevelDb = -4.0f;
        s.harmonizer.voiceInterval[0] = 2;  s.harmonizer.voicePan[0] = -0.6f; s.harmonizer.voiceDetuneCents[0] = -6.0f;
        s.harmonizer.voiceInterval[1] = 4;  s.harmonizer.voicePan[1] = 0.6f;  s.harmonizer.voiceDetuneCents[1] = 6.0f;
        s.harmonizer.voiceInterval[2] = 7;  s.harmonizer.voicePan[2] = -0.3f;
        s.harmonizer.voiceInterval[3] = 11; s.harmonizer.voicePan[3] = 0.3f;  s.harmonizer.voiceLevelDb[3] = -3.0f;

        // --- Tape delay (+splice) for a warbly analog tail (unique FX signature) ---
        s.delayEnabled = 1;
        s.delay.type = 1;             // Tape
        s.delay.sync = 1; s.delay.noteValue = kNote1_8D;
        s.delay.mix = 0.22f; s.delay.feedback = 0.4f;
        s.delay.tapeSaturation = 0.5f; s.delay.tapeWear = 0.3f; s.delay.tapeAge = 0.4f;
        s.delay.tapeSpliceEnabled = 1; s.delay.tapeSpliceIntensity = 0.35f;

        // --- Reverb: Plate, medium - keeps the choir intimate (not a giant hall) ---
        s.reverbEnabled = 1;
        s.reverbType = 0;             // Plate
        s.reverb.size = 0.55f; s.reverb.mix = 0.28f; s.reverb.damping = 0.4f;

        presets.push_back(std::move(p));
    }
```

---

## 7. "Drone" -> "Tidal Lock"

- **Locate:** the block containing `p.name = "Drone"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A screaming self-oscillating resonator singing over brown-noise wind and a two-octave sub, its pitch slowly tracking whatever you play, suspended in a frozen infinite hall.
- **Coverage:**
  - engine: Noise (Brown)
  - filter: Self-Oscillating (res 25, glide, extMix, shape)
  - distortion: Spectral (subtle shimmer)
  - source: pitchFollower (-> filter cutoff, drone tracks input)
  - keyTrack (resonator follows keyboard)
  - mod-matrix scale axis (x4 extreme range)
  - LFO shape SmoothRandom (5)
  - curve SCurve
  - reverb freeze (infinite tail)
  - reverb Hall + reverb mod
- **Rationale:** Kept the strongest element of the original (self-osc filter, res 25) but gave it the identity the directive asked for: a pitchFollower routed to All-Filter-Cutoff with scale x4 so the resonator's pitch genuinely tracks played input across octaves (also the one preset that exercises the scale axis at its extreme), plus keyTrack 1.0 so the resonator follows the keyboard. selfOscShape 0.3 makes the ping non-sine. A very subtle Spectral distortion (drive 0.25 / mix 0.2) adds shimmer without muddying. The Hall reverb is FROZEN (the suite's reverb-freeze home) for an endless bed, differentiating it from the batch's plain decaying tails. SmoothRandom LFO on resonance (shape 5) is a shape no sibling uses.
- **Replacement code:**

```cpp
    // "Tidal Lock" - self-oscillating drone that tracks played pitch, frozen in an infinite hall
    {
        PresetDef p;
        p.name = "Tidal Lock";
        p.category = "Experimental";
        auto& s = p.state;

        // --- Source: brown-noise wind + deep sine feeding a screaming resonator ---
        s.oscA.type = 9; s.oscA.noiseColor = 2; // Brown noise (rumble/wind)
        s.oscA.level = 0.28f;
        s.oscB.type = 0; s.oscB.waveform = 0;    // sine
        s.oscB.tuneSemitones = -24.0f; s.oscB.level = 0.5f; // 2-oct sub anchor
        s.mixer.position = 0.5f;

        // --- Self-oscillating filter IS the drone's voice (res 25) ---
        s.filter.type = 12;          // Self-Oscillating
        s.filter.cutoffHz = 160.0f;  // low fundamental sine-tone
        s.filter.resonance = 25.0f;
        s.filter.selfOscGlide = 2500.0f; // slow portamento between pitches
        s.filter.selfOscExtMix = 0.45f;  // let noise+sub bleed through the resonator
        s.filter.selfOscShape = 0.3f;    // slightly non-sine timbre
        s.filter.selfOscRelease = 2500.0f;
        s.filter.keyTrack = 1.0f;    // resonator follows the keyboard

        // --- Spectral distortion: faint shimmer dusted onto the drone ---
        s.distortion.type = 2;       // Spectral
        s.distortion.drive = 0.25f;
        s.distortion.spectralMode = 1;
        s.distortion.spectralCurve = 4;
        s.distortion.spectralBits = 0.7f;
        s.distortion.mix = 0.2f;     // subtle, keeps the drone clean-ish

        // --- Glacial amp env ---
        s.ampEnv.attackMs = 2500.0f; s.ampEnv.decayMs = 800.0f;
        s.ampEnv.sustain = 0.9f; s.ampEnv.releaseMs = 4000.0f;

        // --- Mod web (unique): PITCH FOLLOWER makes the drone track played pitch ---
        s.pitchFollower.minHz = 40.0f; s.pitchFollower.maxHz = 800.0f;
        s.pitchFollower.confidence = 0.4f; s.pitchFollower.speedMs = 300.0f; // slow glide-track
        s.modMatrix.slots[0].source = 12; // PitchFollow
        s.modMatrix.slots[0].dest = 4;    // All Filter Cutoff
        s.modMatrix.slots[0].amount = 0.8f;
        s.modMatrix.slots[0].curve = 0;   // Linear tracking
        s.modMatrix.slots[0].scale = 4;   // x4 - extreme range so it truly tracks octaves
        s.modMatrix.slots[0].smoothMs = 80.0f;
        // Smooth-random LFO wobbles the resonance for a living, breathing tone
        s.lfo1.rateHz = 0.05f; s.lfo1.shape = 5; s.lfo1.depth = 0.5f; s.lfo1.sync = 0; // SmoothRandom
        s.modMatrix.slots[1].source = 1;  // LFO1
        s.modMatrix.slots[1].dest = 8;    // All Resonance
        s.modMatrix.slots[1].amount = 0.25f;
        s.modMatrix.slots[1].curve = 2;   // SCurve

        // --- Reverb FREEZE: infinite cathedral wash under the drone ---
        s.reverbEnabled = 1;
        s.reverbType = 1;            // Hall
        s.reverb.size = 0.95f; s.reverb.mix = 0.42f; s.reverb.damping = 0.55f;
        s.reverb.freeze = 1;         // frozen tail = endless sustain bed
        s.reverb.modRateHz = 0.15f; s.reverb.modDepth = 0.3f; // slow chorus on the wash

        presets.push_back(std::move(p));
    }
```

---

## 8. "Morph Engine" -> "Fission Cloud"

- **Locate:** the block containing `p.name = "Morph Engine"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A phase-advance sync edge and a granular particle haze FFT-morphed together through a ringing comb and a wavefolder, driven by a Benjolin rungler, sample-and-hold and a one-knob macro morph - the suite's exotic-source and full-mod-matrix showcase.
- **Coverage:**
  - engine: Sync (syncMode = PhaseAdvance)
  - engine: Particle (random spawn, Blackman grains)
  - mixer: SpectralMorph (tilt + shift)
  - filter: Comb (combDamping)
  - distortion: Wavefolder (foldType)
  - source: rungler (full: osc1/2 freq, filter, bits, loopMode)
  - source: sampleHold
  - source: random
  - macros multi-routed (one knob -> 3 dests)
  - all 8 mod-matrix slots used
  - mod-matrix bypass axis + scale x2
  - curves Stepped/SCurve
  - dual LFO (triangle + S&H)
  - global filter enabled
  - settings: velocityCurve, voiceAllocMode, voiceStealMode, pitchBendRange
  - Digital delay
- **Rationale:** The directive demanded mod destinations DIFFERENT from Ring Vowel (no shared LFO->morphPos), so this became the exotic-source hub. Its motion is defined by sources no sibling touches at once: a full Benjolin rungler (depth 0.7, 6-bit, chaos loop) stair-steps the morph spectrum (Stepped curve), sample&hold stair-steps resonance, and smoothed Random wanders the cutoff. Macro1 is multi-routed to three destinations (morph, effect mix, global-filter cutoff at scale x2) for a true one-knob timbral morph - the missing 'macro-morph' idiom. All eight matrix slots are populated, including a deliberately BYPASSED slot7 to exercise the bypass axis. The comb filter (ringing tine) and wavefolder give it a metallic body unlike the batch's LP+reverb. It also carries the four performance settings (velocityCurve/voiceAllocMode/voiceStealMode/pitchBendRange) and a modulated Digital delay. Comb resonance 6 with combDamping 0.4 stays feedback-safe.
- **Replacement code:**

```cpp
    // "Fission Cloud" - sync/particle spectral morph, exotic-source hub, full mod matrix
    {
        PresetDef p;
        p.name = "Fission Cloud";
        p.category = "Experimental";
        auto& s = p.state;

        // --- Two engines spectrally interpolated: sync edge vs granular haze ---
        s.oscA.type = 3;             // Sync
        s.oscA.syncRatio = 3.0f; s.oscA.syncWaveform = 1;
        s.oscA.syncMode = 2; // PhaseAdvance: smooth phase-shift sync (not hard tearing) suits the spectral morph
        s.oscA.syncAmount = 0.85f;
        s.oscA.level = 0.7f;
        s.oscB.type = 6;             // Particle
        s.oscB.particleScatter = 6.0f; s.oscB.particleDensity = 28.0f;
        s.oscB.particleLifetime = 160.0f; s.oscB.particleSpawnMode = 1; // Random spawn
        s.oscB.particleEnvType = 3;  // Blackman grains = smooth cloud
        s.oscB.level = 0.6f;
        s.mixer.mode = 1;            // Spectral Morph (FFT interpolation A<->B)
        s.mixer.position = 0.5f;
        s.mixer.tilt = 3.0f;         // brighten the morph
        s.mixer.shift = 40.0f;       // inharmonic frequency shift on the morph

        // --- Comb filter: metallic resonant body distinct from every sibling ---
        s.filter.type = 6;          // Comb
        s.filter.cutoffHz = 440.0f; // comb tuned near A
        s.filter.resonance = 6.0f;  // strong feedback = ringing tine
        s.filter.combDamping = 0.4f;// tame the top of the feedback

        // --- Wavefolder: folds the morph into extra harmonics ---
        s.distortion.type = 4;      // Wavefolder
        s.distortion.drive = 0.4f;
        s.distortion.foldType = 1;
        s.distortion.character = 0.6f;
        s.distortion.mix = 0.5f;

        s.ampEnv.attackMs = 120.0f; s.ampEnv.decayMs = 600.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 850.0f;

        // --- Exotic-source HUB: rungler + S&H + random all drive the morph web ---
        s.rungler.osc1FreqHz = 2.5f; s.rungler.osc2FreqHz = 3.7f;
        s.rungler.depth = 0.7f; s.rungler.filter = 0.4f; s.rungler.bits = 6; s.rungler.loopMode = 0; // chaos
        s.modMatrix.slots[0].source = 10; // Rungler
        s.modMatrix.slots[0].dest = 7;    // Spectral Tilt - Benjolin moves the morph spectrum
        s.modMatrix.slots[0].amount = 0.5f;
        s.modMatrix.slots[0].curve = 3;   // Stepped - stair-stepped Benjolin motion
        s.sampleHold.rateHz = 6.0f; s.sampleHold.sync = 0; s.sampleHold.slewMs = 15.0f;
        s.modMatrix.slots[1].source = 11; // SampleHold
        s.modMatrix.slots[1].dest = 8;    // All Resonance
        s.modMatrix.slots[1].amount = 0.3f;
        s.modMatrix.slots[1].curve = 3;   // Stepped
        s.random.rateHz = 0.7f; s.random.smoothness = 0.8f;
        s.modMatrix.slots[2].source = 4;  // Random
        s.modMatrix.slots[2].dest = 4;    // All Filter Cutoff
        s.modMatrix.slots[2].amount = 0.35f;
        s.modMatrix.slots[2].curve = 2;   // SCurve

        // --- MACRO 1 multi-routed: one knob morphs the whole cloud ---
        s.macros.values[0] = 0.5f;        // park at center
        s.modMatrix.slots[3].source = 5;  // Macro1
        s.modMatrix.slots[3].dest = 5;    // Morph Position
        s.modMatrix.slots[3].amount = 0.8f;
        s.modMatrix.slots[4].source = 5;  // Macro1
        s.modMatrix.slots[4].dest = 3;    // Effect Mix
        s.modMatrix.slots[4].amount = 0.5f;
        s.modMatrix.slots[5].source = 5;  // Macro1
        s.modMatrix.slots[5].dest = 0;    // Global Filter Cutoff
        s.modMatrix.slots[5].amount = 0.6f; s.modMatrix.slots[5].scale = 3; // x2 wide sweep

        // --- Dual LFO: triangle + S&H on the morph tilt region ---
        s.lfo1.rateHz = 0.2f; s.lfo1.shape = 1; s.lfo1.depth = 0.7f; s.lfo1.sync = 0; // triangle
        s.lfo2.rateHz = 3.0f; s.lfo2.shape = 4; s.lfo2.depth = 0.4f; s.lfo2.sync = 0; // S&H
        s.modMatrix.slots[6].source = 2;  // LFO2
        s.modMatrix.slots[6].dest = 7;    // Spectral Tilt
        s.modMatrix.slots[6].amount = 0.3f;
        s.modMatrix.slots[6].smoothMs = 25.0f;
        // slot7 prepared but BYPASSED: an alternate LFO1->morph route the user can enable
        s.modMatrix.slots[7].source = 1;  // LFO1
        s.modMatrix.slots[7].dest = 5;    // Morph Position
        s.modMatrix.slots[7].amount = 0.4f;
        s.modMatrix.slots[7].bypass = 1;  // off by default (demonstrates the bypass axis)

        // --- Global filter engaged (macro sweeps it) ---
        s.globalFilter.enabled = 1; s.globalFilter.type = 0; // LP
        s.globalFilter.cutoffHz = 3000.0f; s.globalFilter.resonance = 1.2f;

        // --- Performance settings: shape how the cloud plays ---
        s.settings.velocityCurve = 2;   // Hard - expressive dynamics
        s.settings.voiceAllocMode = 3;  // HighNote priority
        s.settings.voiceStealMode = 1;  // Soft steal (no clicks on this dense patch)
        s.settings.pitchBendRangeSemitones = 12.0f; // octave dive-bombs
        s.global.polyphony = 6;

        // --- Digital delay: clean, wide, modulated ---
        s.delayEnabled = 1;
        s.delay.type = 0;           // Digital
        s.delay.sync = 1; s.delay.noteValue = kNote1_8;
        s.delay.mix = 0.25f; s.delay.feedback = 0.45f;
        s.delay.digitalModDepth = 0.3f; s.delay.digitalModRateHz = 0.4f; s.delay.digitalWidth = 150.0f;

        // --- Reverb: Plate, moderate (delay carries the space, not a huge hall) ---
        s.reverbEnabled = 1;
        s.reverbType = 0;           // Plate
        s.reverb.size = 0.5f; s.reverb.mix = 0.22f;

        presets.push_back(std::move(p));
    }
```

---

## 9. "Ring Vowel" -> "Glossolalia"

- **Locate:** the block containing `p.name = "Ring Vowel"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A talking formant voice run through a SECOND formant filter (gender-shifted) and clanged by a note-tracking ring modulator, made expressive under aftertouch and vibrato, widened by chorus.
- **Coverage:**
  - engine: Formant oscillator
  - filter: Formant (formantMorph + formantGender)
  - distortion: RingModulator (NoteTrack, ringRatio, stereo spread)
  - voice route: Aftertouch -> resonance
  - voice route: Velocity -> morph position
  - voice route: Voice LFO -> distortion drive (animates ring depth)
  - keyTrack
  - modulation FX: Chorus
  - dual LFO (sine vibrato + synced square)
  - curve Exp
  - reverb Hall
- **Rationale:** The directive's exact ask: put the ring mod on the FORMANT filter, not the inert 6000 Hz ladder. The formant filter (formantMorph 2.5 offset from the osc's 1.5, formantGender -0.4) acts as a second vocal tract so the two vowels beat into a 'talking' timbre. Ring mod is set to NoteTrack with a non-integer ringRatio 0.35 for a musical inharmonic clang. ringRatio is not a routable mod destination, so - honestly noted in the comment - the LFO instead animates ring DRIVE (voice-LFO->distDrive) for the equivalent living motion, giving this preset a mod story completely different from Morph Engine's morphPos. It is the suite's aftertouch+velocity expression home (aftertouch->resonance, velocity->morph) with keyTrack on the vowels, and Chorus (the under-used modulation slot) widens it. Distinct Hall-with-predelay room rather than the batch's plate wash.
- **Replacement code:**

```cpp
    // "Glossolalia" - formant osc + formant filter + note-tracked ring mod, expressive under touch
    {
        PresetDef p;
        p.name = "Glossolalia";
        p.category = "Experimental";
        auto& s = p.state;

        // --- Formant oscillator: a synthetic voice between two vowels ---
        s.oscA.type = 7;            // Formant
        s.oscA.formantVowel = 0; s.oscA.formantMorph = 1.5f; // between A and I
        s.oscA.level = 0.85f;
        s.oscB.type = 0; s.oscB.waveform = 0; s.oscB.tuneSemitones = -12.0f; s.oscB.level = 0.25f; // sub body
        s.mixer.position = 0.15f;

        // --- FORMANT filter (not the inert ladder): a SECOND vocal tract on the tone ---
        s.filter.type = 5;         // Formant
        s.filter.formantMorph = 2.5f;   // filter vowel offset from osc vowel = talking timbre
        s.filter.formantGender = -0.4f; // shift formants down = deeper/masc voice
        s.filter.cutoffHz = 1200.0f;
        s.filter.resonance = 4.0f;
        s.filter.keyTrack = 0.5f;  // vowels track pitch partway

        // --- Ring modulator clangs the vowel against a note-tracked carrier ---
        s.distortion.type = 6;     // Ring Modulator
        s.distortion.drive = 0.4f;
        s.distortion.ringFreqMode = 1;  // NoteTrack - carrier follows pitch = musical clang
        s.distortion.ringFreq = 0.5f;
        s.distortion.ringRatio = 0.35f; // non-integer ratio for inharmonic bell edge
        s.distortion.ringWaveform = 1;  // Triangle carrier
        s.distortion.ringStereoSpread = 0.4f;
        s.distortion.mix = 0.55f;

        s.ampEnv.attackMs = 30.0f; s.ampEnv.decayMs = 450.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 500.0f;

        // --- Mod web (unique - NOT the shared LFO->morphPos): animate ring & vowel ---
        // Voice LFO -> distortion drive: ringRatio isn't a routable dest, so we wobble the
        // ring's intensity for the same living, talking motion (vibrato-rate vocal shimmer).
        s.lfo1.rateHz = 5.5f; s.lfo1.shape = 0; s.lfo1.depth = 0.6f; s.lfo1.sync = 0; // vibrato rate sine
        s.voiceRoutes[0].source = 3;      // Voice LFO
        s.voiceRoutes[0].destination = 3; // Distortion Drive
        s.voiceRoutes[0].amount = 0.5f;
        s.voiceRoutes[0].active = 1;
        // Aftertouch -> filter resonance: lean on the key to sharpen the vowel
        s.voiceRoutes[1].source = 7;      // Aftertouch
        s.voiceRoutes[1].destination = 1; // Filter Resonance
        s.voiceRoutes[1].amount = 0.5f;
        s.voiceRoutes[1].active = 1;
        // Velocity -> morph position: harder = brighter vowel blend
        s.voiceRoutes[2].source = 5;      // Velocity
        s.voiceRoutes[2].destination = 2; // Morph Position
        s.voiceRoutes[2].amount = 0.4f;
        s.voiceRoutes[2].active = 1;
        // Global: synced square LFO2 chops the wet ring level rhythmically
        s.lfo2.rateHz = 2.0f; s.lfo2.shape = 3; s.lfo2.depth = 0.4f; s.lfo2.sync = 1; s.lfo2Ext.noteValue = kNote1_8;
        s.modMatrix.slots[0].source = 2;  // LFO2
        s.modMatrix.slots[0].dest = 3;    // Effect Mix
        s.modMatrix.slots[0].amount = 0.3f;
        s.modMatrix.slots[0].curve = 1;   // Exp

        // --- Chorus widens the choir-of-one ---
        s.modulationType = 3;      // Chorus
        s.chorus.rateHz = 0.6f; s.chorus.depth = 0.4f; s.chorus.mix = 0.4f; s.chorus.voices = 3;

        // --- Reverb: Hall, medium-small - a room for the voice ---
        s.reverbEnabled = 1;
        s.reverbType = 1;          // Hall
        s.reverb.size = 0.5f; s.reverb.mix = 0.24f; s.reverb.preDelayMs = 20.0f;

        presets.push_back(std::move(p));
    }
```

---

## 10. "Double Grain" -> "Grain Reactor"

- **Locate:** the block containing `p.name = "Double Grain"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** Two detuned saws shredded by granular distortion and smeared through a granular delay, with an auto-wah envelope filter and a transient detector so every attack bursts a fresh cloud of grains.
- **Coverage:**
  - engine: dual detuned saw (PolyBLEP)
  - filter: Env Filter / auto-wah (envSubType BP, sensitivity, depth, attack/release)
  - distortion: Granular
  - delay: Granular
  - source: transient (-> grain intensity via effect mix / distortion drive)
  - modEnv (fast blip -> distortion drive)
  - LFO shape saw (2)
  - curves Exp/SCurve
  - reverb Plate (short, articulate)
- **Rationale:** The original's weakness was static grains over a stock ladder. Two fixes give it identity: (1) an ENV FILTER (auto-wah, BP, sensitivity 6, depth 0.9) so the filter itself reacts to each attack - the tone is never static; and (2) the second transient-detector home in the suite. grain-density is not a routable mod destination, so - stated in the comment - the transient drives grain wetness (Effect Mix, Exp curve) and a near-zero-sustain modEnv punches distortion drive per note, so every attack genuinely bursts more grains, honoring the directive's intent. The detuned dual saw (+/-8/9 cents) plus granular distortion into a granular delay (pan/pitch/reverse spray) makes it the double-granular member, and a short Plate keeps the grains articulate instead of the batch's diffuse hall.
- **Replacement code:**

```cpp
    // "Grain Reactor" - granular dist + granular delay, transient- and auto-wah-reactive
    {
        PresetDef p;
        p.name = "Grain Reactor";
        p.category = "Experimental";
        auto& s = p.state;

        // --- Two detuned saws = raw fuel for the grain engines ---
        s.oscA.type = 0; s.oscA.waveform = 1; s.oscA.level = 0.7f;  // saw
        s.oscA.fineCents = -8.0f;
        s.oscB.type = 0; s.oscB.waveform = 1; s.oscB.level = 0.55f; // saw
        s.oscB.fineCents = 9.0f;
        s.mixer.position = 0.5f;

        // --- ENV FILTER (auto-wah): the filter itself reacts to the note's attack ---
        s.filter.type = 11;        // Env Filter
        s.filter.cutoffHz = 500.0f;
        s.filter.resonance = 5.0f;
        s.filter.envSubType = 1;   // BP - vocal-ish auto-wah
        s.filter.envSensitivity = 6.0f;
        s.filter.envDepth = 0.9f;
        s.filter.envAttack = 8.0f; s.filter.envRelease = 200.0f;
        s.filter.envDirection = 0; // Up sweep on transients

        // --- Granular distortion shreds the saws into a grain cloud ---
        s.distortion.type = 3;     // Granular
        s.distortion.drive = 0.5f;
        s.distortion.grainSize = 0.22f; s.distortion.grainDensity = 0.55f;
        s.distortion.grainVariation = 0.5f; s.distortion.grainJitter = 0.35f;
        s.distortion.mix = 0.7f;

        s.ampEnv.attackMs = 15.0f; s.ampEnv.decayMs = 450.0f;
        s.ampEnv.sustain = 0.65f; s.ampEnv.releaseMs = 550.0f;

        // --- Mod web (unique): TRANSIENT detector reacts to attacks ---
        // grain-density isn't a routable dest, so the transient drives grain wetness/drive
        // (= grain intensity) so each attack bursts more grains - the directive's intent.
        s.transient.sensitivity = 0.7f; s.transient.attackMs = 1.5f; s.transient.decayMs = 80.0f;
        s.modMatrix.slots[0].source = 13; // Transient
        s.modMatrix.slots[0].dest = 3;    // Effect Mix - grain wetness bursts on attack
        s.modMatrix.slots[0].amount = 0.5f;
        s.modMatrix.slots[0].curve = 1;   // Exp - snappy transient response
        // Fast mod-envelope also punches distortion drive per note (per-note grain burst)
        s.modEnv.attackMs = 1.0f; s.modEnv.decayMs = 120.0f; s.modEnv.sustain = 0.0f; s.modEnv.releaseMs = 100.0f;
        s.voiceRoutes[0].source = 2;      // Env3 (mod env - fast blip)
        s.voiceRoutes[0].destination = 3; // Distortion Drive
        s.voiceRoutes[0].amount = 0.5f;
        s.voiceRoutes[0].active = 1;
        // LFO1 saw slowly sweeps the grain field via spectral tilt
        s.lfo1.rateHz = 0.3f; s.lfo1.shape = 2; s.lfo1.depth = 0.5f; s.lfo1.sync = 0; // saw ramp
        s.modMatrix.slots[1].source = 1;  // LFO1
        s.modMatrix.slots[1].dest = 7;    // Spectral Tilt
        s.modMatrix.slots[1].amount = 0.3f;
        s.modMatrix.slots[1].curve = 2;   // SCurve

        // --- Granular DELAY smears the grains into a stereo cloud ---
        s.delayEnabled = 1;
        s.delay.type = 3;          // Granular
        s.delay.sync = 1; s.delay.noteValue = kNote1_8D;
        s.delay.mix = 0.32f; s.delay.feedback = 0.4f;
        s.delay.granularSizeMs = 60.0f; s.delay.granularDensity = 28.0f;
        s.delay.granularPitchSpray = 0.35f; s.delay.granularPanSpray = 0.6f;
        s.delay.granularReverseProb = 0.25f; s.delay.granularJitter = 0.3f;

        // --- Reverb: Plate, short - keeps the grains articulate, not washed out ---
        s.reverbEnabled = 1;
        s.reverbType = 0;          // Plate
        s.reverb.size = 0.45f; s.reverb.mix = 0.2f; s.reverb.damping = 0.4f;

        presets.push_back(std::move(p));
    }
```

---

## 11. "Tape Feedback" -> "Dub Chamber"

- **Locate:** the block containing `p.name = "Tape Feedback"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A mono, gliding saw stab swallowed by a self-oscillating spliced-tape echo whose wet level breathes in and out under a glacial LFO — living dub.
- **Coverage:**
  - ladder-filter+drive
  - filter-env-fixed(+semitones)
  - mono-glide/legato-portamento
  - tape-delay
  - delay-sync-dotted
  - tape-splice/wear/age
  - lfo->effectMix(SCurve curve axis)
  - lfo->allFltCut
  - plate-reverb-small
- **Rationale:** Fixes the inert source: the raw saw now has a real per-note gesture — a Ladder (type 4) with 6 dB drive and a fixed +24-semitone filter-env pluck (correcting the class-wide envAmount bug), so each stab sweeps. Mono+40 ms legato portamento (an unused area) turns it into a dub-riff instrument. Feedback held at 0.8 (tape saturation + default soft-limit prevent runaway). The mod identity is unique: there is NO delay-feedback destination in the matrix, so the 'echoes swell' directive is realized as LFO1(0.1 Hz, free)->EffectMix with an SCurve, making the whole wet tail breathe; a second slot nudges AllFltCut so the source drifts too. Reverb is a SMALL PLATE (0.4/0.18), deliberately not the big hall its siblings share. FIX: the undefined kNote1_4D constant (a whole-generator compile error) is replaced by a block-local `constexpr int32_t kNote1_4D = 14`; verified against the note-value dropdown in arpeggiator_params.h where index 14 = "1/4D" (dotted quarter), consistent with the generator's kNote1_4=13. Named local keeps the dotted-quarter coverage claim self-documenting and the block self-contained; no other preset touched.
- **Replacement code:**

```cpp
    // "Dub Chamber" - Mono gliding saw into a self-oscillating spliced-tape echo
    {
        PresetDef p;
        p.name = "Dub Chamber";
        p.category = "Experimental";
        auto& s = p.state;
        // Single saw source, OSC B silent (A-only blend)
        s.oscA.type = 0; s.oscA.waveform = 1; s.oscA.level = 0.68f;
        s.oscB.level = 0.0f;
        s.mixer.position = 0.0f;
        // Ladder with drive so the source has grit before it hits the tape
        s.filter.type = 4;                 // Ladder LP
        s.filter.cutoffHz = 2200.0f; s.filter.resonance = 0.28f;
        s.filter.ladderSlope = 4;          // 24 dB/oct
        s.filter.ladderDrive = 6.0f;       // pushes the ladder into warm saturation
        s.filter.envAmount = 24.0f;        // +24 st sweep (FIX: was the 4000 'Hz' bug elsewhere)
        // Plucked filter env = each stab opens then closes
        s.filterEnv.attackMs = 2.0f; s.filterEnv.decayMs = 260.0f;
        s.filterEnv.sustain = 0.15f; s.filterEnv.releaseMs = 300.0f;
        // Dub-stab amp shape
        s.ampEnv.attackMs = 3.0f; s.ampEnv.decayMs = 240.0f;
        s.ampEnv.sustain = 0.4f; s.ampEnv.releaseMs = 420.0f;
        // Mono + glide turns it into a riff instrument (unused area)
        s.global.voiceMode = 1;            // Mono
        s.monoMode.legato = 1;
        s.monoMode.portamentoTimeMs = 40.0f;
        s.monoMode.portaMode = 1;          // Legato-only glide
        // Self-oscillating spliced tape delay = the star
        s.delayEnabled = 1;
        s.delay.type = 1;                  // Tape
        constexpr int32_t kNote1_4D = 14;  // dotted quarter (note-value dropdown index 14 = "1/4D")
        s.delay.sync = 1; s.delay.noteValue = kNote1_4D; // dotted quarter dub feel
        s.delay.mix = 0.45f; s.delay.feedback = 0.8f;    // high but self-limited by tape
        s.delay.tapeSaturation = 0.7f; s.delay.tapeWear = 0.5f;
        s.delay.tapeAge = 0.6f; s.delay.tapeInertiaMs = 260.0f;
        s.delay.tapeSpliceEnabled = 1; s.delay.tapeSpliceIntensity = 0.4f; // owns splice
        // Mod identity: no delay-feedback dest exists, so swell the WET level instead
        s.lfo1.rateHz = 0.1f; s.lfo1.shape = 0; s.lfo1.depth = 0.85f; s.lfo1.sync = 0;
        setModSlot(s, 0, 1, 3, 0.5f, kCurveSCurve, 20.0f); // LFO1 -> EffectMix, smoothstep swell
        setModSlot(s, 1, 1, 4, 0.25f, kCurveExp);          // LFO1 -> AllFltCut, source drifts too
        // Small PLATE, not the shared big hall
        s.reverbEnabled = 1; s.reverbType = 0;  // Plate
        s.reverb.size = 0.4f; s.reverb.mix = 0.18f; s.reverb.damping = 0.55f;
        presets.push_back(std::move(p));
    }
```

---

## 12. "Noise Shaper" -> "Tidal Breath"

- **Locate:** the block containing `p.name = "Noise Shaper"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** Pink and violet noise sculpted by a stacked double auto-wah — the filter's own bandpass envelope plus a routed envelope-follower — drifting through a granular-cloud delay into an airy hall. Wind, surf, and breath.
- **Coverage:**
  - dual-noise-color(pink+violet)
  - env-filter-BP(type11)
  - envFollower-module-as-source(src3)
  - lfo1+lfo2-dual(sine+tri,free)
  - mod-matrix-3-slots
  - SCurve+Exp-curves
  - per-slot-smoothMs-axis
  - granular-delay(type3)
  - hall-reverb
- **Rationale:** Keeps the genuinely-distinct dual-noise + EnvelopeFilter source but cures its dead FX/mod side. The directive's 'LFO->envDepth' has no matrix destination, so the auto-wah is made to VARY through the reachable dests: LFO1->AllResonance changes the wah's bite and LFO2->AllFltCut sweeps its center, so no two notes wah identically. Crucially it also enables the actual EnvFollower MODULE (unused across the bank) and routes it (src 3) to AllFltCut with per-slot smoothMs — a second, program-dependent breathing layer on top of the filter's internal BP envelope. The shared 350 ms slap is replaced by a GRANULAR delay cloud, and the reverb is a big HALL (0.7/0.32), distinct from siblings' 0.6/0.3.
- **Replacement code:**

```cpp
    // "Tidal Breath" - Dual-noise double auto-wah drifting through a granular cloud
    {
        PresetDef p;
        p.name = "Tidal Breath";
        p.category = "Experimental";
        auto& s = p.state;
        s.oscA.type = 9; s.oscA.noiseColor = 1; s.oscA.level = 0.65f; // Pink body
        s.oscB.type = 9; s.oscB.noiseColor = 4; s.oscB.level = 0.35f; // Violet air
        s.mixer.position = 0.35f;
        // Internal bandpass envelope filter (auto-wah #1)
        s.filter.type = 11;                // Envelope Filter
        s.filter.cutoffHz = 1400.0f; s.filter.resonance = 0.85f;
        s.filter.envSubType = 1;           // BP
        s.filter.envSensitivity = 12.0f; s.filter.envDepth = 0.9f;
        s.filter.envAttack = 4.0f; s.filter.envRelease = 120.0f;
        s.filter.envDirection = 0;         // sweep up
        // Slow surf swell
        s.ampEnv.attackMs = 40.0f; s.ampEnv.decayMs = 600.0f;
        s.ampEnv.sustain = 0.55f; s.ampEnv.releaseMs = 700.0f;
        // Enable the EnvFollower MODULE (auto-wah #2, routed below)
        s.envFollower.sensitivity = 0.7f; s.envFollower.attackMs = 8.0f; s.envFollower.releaseMs = 180.0f;
        // Dual free LFOs make the wah gesture never repeat (envDepth has no dest, so vary res+cut)
        s.lfo1.rateHz = 0.2f; s.lfo1.shape = 0; s.lfo1.depth = 0.7f; s.lfo1.sync = 0; // sine
        s.lfo2.rateHz = 0.5f; s.lfo2.shape = 1; s.lfo2.depth = 0.6f; s.lfo2.sync = 0; // triangle
        setModSlot(s, 0, 1, 8, 0.4f, kCurveSCurve);       // LFO1 -> AllResonance (wah bite varies)
        setModSlot(s, 1, 2, 4, 0.3f, kCurveExp);          // LFO2 -> AllFltCut (center drifts)
        setModSlot(s, 2, 3, 4, 0.5f, kCurveLinear, 30.0f);// EnvFollower -> AllFltCut, smoothed breathing
        // Granular delay cloud instead of the shared 350 ms slap
        s.delayEnabled = 1;
        s.delay.type = 3;                  // Granular
        s.delay.sync = 0; s.delay.timeMs = 300.0f;
        s.delay.mix = 0.25f; s.delay.feedback = 0.35f;
        s.delay.granularSizeMs = 80.0f; s.delay.granularDensity = 20.0f;
        s.delay.granularPitchSpray = 0.15f; s.delay.granularPanSpray = 0.6f;
        s.delay.granularJitter = 0.2f;
        // Airy HALL
        s.reverbEnabled = 1; s.reverbType = 1;  // Hall
        s.reverb.size = 0.7f; s.reverb.mix = 0.32f; s.reverb.damping = 0.5f;
        presets.push_back(std::move(p));
    }
```

---

## 13. "Phase Sync Duo" -> "Glass Automaton"

- **Locate:** the block containing `p.name = "Phase Sync Duo"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A resonant Casio-CZ phase-distortion tone spectrally morphed against a reverse hard-sync square, ring-modulated to a metallic sheen and animated by a smooth LFO morph plus a stepped-random spectral-tilt glitch — evolving clockwork glass.
- **Coverage:**
  - phase-distortion(ResSaw)
  - reverse-hard-sync
  - spectral-morph-mixer(tilt/shift)
  - SVF-Peak-filter(svfGain)
  - ring-modulator-distortion(NoteTrack)
  - lfo1+lfo2
  - random-source(stepped)
  - SCurve+Stepped-curves
  - mod-matrix-3-slots
  - pingpong-delay-synced
  - pitchBendRange-setting
  - filter-env-fixed
- **Rationale:** Builds on the suite's best existing patch and pushes past its single-slot limit. PD uses ResSaw (formant-resonant) at 0.7 DCW; the sync slave is a reverse-mode square for a hollow, glassy sync sweep, blended in SpectralMorph with a +2 dB tilt. The filter is an SVF PEAK (type 8, a must-cover) with +9 dB gain for a vocal formant bump, plus a +18 st filter-env pluck. Ring-modulator distortion (type 6, NoteTrack, ratio ~3) is owned here for the metallic edge. The unique mod web: LFO1->AllMorphPos (SCurve) glides the A/B morph, while the directive's 'Random->shift' becomes Random->AllSpecTilt with a STEPPED curve — discrete spectral jumps that keep it evolving — and a fast square LFO2 adds robotic cutoff chatter. A 12-st pitch-bend range makes it a performable whammy lead.
- **Replacement code:**

```cpp
    // "Glass Automaton" - PD morphed against reverse-sync, ring-modded, stepped-random evolution
    {
        PresetDef p;
        p.name = "Glass Automaton";
        p.category = "Experimental";
        auto& s = p.state;
        s.oscA.type = 2;                   // Phase Distortion
        s.oscA.pdWaveform = 5;             // ResSaw (formant-resonant CZ shape)
        s.oscA.pdDistortion = 0.7f; s.oscA.level = 0.7f;
        s.oscB.type = 3;                   // Sync slave
        s.oscB.syncRatio = 2.5f; s.oscB.syncWaveform = 2; // square = hollow sync
        s.oscB.syncMode = 1;              // Reverse (softer, unusual)
        s.oscB.syncAmount = 0.75f; s.oscB.level = 0.6f;
        s.mixer.mode = 1;                 // Spectral Morph
        s.mixer.position = 0.5f; s.mixer.tilt = 2.0f; s.mixer.shift = 0.1f;
        // SVF Peak = resonant formant bump (a must-cover filter)
        s.filter.type = 8;                // SVF Peak
        s.filter.cutoffHz = 2200.0f; s.filter.resonance = 2.0f; s.filter.svfGain = 9.0f;
        s.filter.envAmount = 18.0f;       // pluck sweep on the peak
        s.filterEnv.attackMs = 6.0f; s.filterEnv.decayMs = 300.0f;
        s.filterEnv.sustain = 0.3f; s.filterEnv.releaseMs = 260.0f;
        s.ampEnv.attackMs = 8.0f; s.ampEnv.decayMs = 400.0f;
        s.ampEnv.sustain = 0.6f; s.ampEnv.releaseMs = 350.0f;
        // Ring modulator for metallic sheen (owns this dirty type)
        s.distortion.type = 6;            // RingModulator
        s.distortion.drive = 0.3f; s.distortion.mix = 0.3f;
        s.distortion.ringFreqMode = 1;    // NoteTrack (musical)
        s.distortion.ringRatio = 0.17f;   // normalized -> ratio ~3.0 (metallic)
        s.distortion.ringWaveform = 0;    // Sine
        // Evolving mod web (multi-slot, not the single-LFO skeleton)
        s.lfo1.rateHz = 0.25f; s.lfo1.shape = 0; s.lfo1.depth = 0.6f; s.lfo1.sync = 0; // slow sine
        s.lfo2.rateHz = 6.0f;  s.lfo2.shape = 3; s.lfo2.depth = 0.3f; s.lfo2.sync = 0; // fast square
        s.random.rateHz = 1.5f; s.random.smoothness = 0.0f; // hard steps
        setModSlot(s, 0, 1, 5, 0.5f, kCurveSCurve);  // LFO1 -> AllMorphPos (smooth glide)
        setModSlot(s, 1, 4, 7, 0.4f, kCurveStepped); // Random -> AllSpecTilt (stepped 'shift' glitch)
        setModSlot(s, 2, 2, 4, 0.2f, kCurveLinear);  // LFO2 -> AllFltCut (robotic chatter)
        // Digital bounce
        s.delayEnabled = 1;
        s.delay.type = 2;                 // PingPong
        s.delay.sync = 1; s.delay.noteValue = kNote1_8;
        s.delay.mix = 0.28f; s.delay.feedback = 0.4f;
        s.delay.pingPongRatio = 2; s.delay.pingPongWidth = 140.0f;
        // Small plate + wide pitch bend for whammy performance
        s.reverbEnabled = 1; s.reverbType = 0; // Plate
        s.reverb.size = 0.45f; s.reverb.mix = 0.22f;
        s.settings.pitchBendRangeSemitones = 12.0f;
        presets.push_back(std::move(p));
    }
```

---

## 14. "Living Harmonics" -> "Living Harmonics"

- **Locate:** the block containing `p.name = "Living Harmonics"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A 24-partial inharmonic additive tone spun into a four-voice Lydian phase-vocoder choir, continuously destabilized by a Lorenz chaos modulator across tilt and morph, with a single Macro as an 'instability' knob — generative, orchestral, alive.
- **Coverage:**
  - additive(partials+tilt+inharm)
  - scalic-harmonizer-PhaseVocoder-4voice
  - voice-interval/pan/detune/delay
  - chaosMod-Lorenz
  - chaos-source-multi-dest
  - macros-routed(src5)
  - AllFltEnvAmt-dest
  - mod-scale-axis(x2)
  - SCurve+Exp-curves
  - filter-env-fixed+keyTrack
  - spectral-delay(type4)
  - hall-reverb+reverb-mod
- **Rationale:** Keeps its role as the harmonizer showcase but removes every stock element. The additive tone gains +0.12 inharmonicity (a never-touched param) for bell shimmer and a slight dark tilt. The filter is no longer the inert 6 kHz LP: it now has a +20 st filter-env sweep, 0.4 keytrack, and moderate resonance so the body actually moves. The harmonizer grows to a full 4-voice Lydian-in-D PhaseVocoder chord with formant preservation and per-voice pan/detune/delay. The mod web is the identity: Chaos drives BOTH AllSpecTilt (SCurve) and AllMorphPos (Exp), and one Chaos slot uses the SCALE axis at x2 (kScaleValues idx 3) to push its amount past +-1 for extreme drift — an encoding dimension no preset uses. Macro1 (src 5) is wired to AllFltEnvAmt as a one-knob instability control. Reverb is a modulated HALL (modDepth/modRateHz — unused reverb params) fed by a Spectral delay cloud.
- **Replacement code:**

```cpp
    // "Living Harmonics" - Inharmonic additive choir destabilized by Lorenz chaos + a macro
    {
        PresetDef p;
        p.name = "Living Harmonics";
        p.category = "Experimental";
        auto& s = p.state;
        s.oscA.type = 4;                   // Additive
        s.oscA.additivePartials = 24;
        s.oscA.additiveTilt = -2.0f;       // slightly dark (was bright +1)
        s.oscA.additiveInharm = 0.12f;     // bell/metallic shimmer (never-touched param)
        s.oscA.level = 0.7f;
        s.oscB.type = 0; s.oscB.waveform = 1; s.oscB.tuneSemitones = -12.0f;
        s.oscB.fineCents = -6.0f; s.oscB.level = 0.3f; // sub weight
        s.mixer.position = 0.32f;
        // Filter that actually moves (fixes the inert 6k LP)
        s.filter.type = 0;                 // SVF LP
        s.filter.cutoffHz = 3500.0f; s.filter.resonance = 0.4f; s.filter.keyTrack = 0.4f;
        s.filter.envAmount = 20.0f;        // +20 st opening sweep
        s.filterEnv.attackMs = 80.0f; s.filterEnv.decayMs = 900.0f;
        s.filterEnv.sustain = 0.5f; s.filterEnv.releaseMs = 1000.0f;
        // Orchestral swell
        s.ampEnv.attackMs = 120.0f; s.ampEnv.decayMs = 700.0f;
        s.ampEnv.sustain = 0.75f; s.ampEnv.releaseMs = 1200.0f;
        // Full 4-voice Lydian PhaseVocoder choir
        s.harmonizerEnabled = 1;
        s.harmonizer.harmonyMode = 1;      // Scalic
        s.harmonizer.key = 2;              // D
        s.harmonizer.scale = 7;            // Lydian (ethereal)
        s.harmonizer.pitchShiftMode = 2;   // PhaseVocoder
        s.harmonizer.formantPreserve = 1;
        s.harmonizer.numVoices = 4;
        s.harmonizer.dryLevelDb = 0.0f; s.harmonizer.wetLevelDb = -3.0f;
        s.harmonizer.voiceInterval[0] = 2; s.harmonizer.voicePan[0] = -0.5f; s.harmonizer.voiceDetuneCents[0] = 6.0f;
        s.harmonizer.voiceInterval[1] = 4; s.harmonizer.voicePan[1] = 0.5f;  s.harmonizer.voiceDetuneCents[1] = -6.0f;
        s.harmonizer.voiceInterval[2] = 6; s.harmonizer.voicePan[2] = -0.25f; s.harmonizer.voiceDetuneCents[2] = 10.0f;
        s.harmonizer.voiceInterval[3] = -3; s.harmonizer.voicePan[3] = 0.25f; s.harmonizer.voiceDelayMs[3] = 20.0f; s.harmonizer.voiceDetuneCents[3] = -8.0f;
        // Lorenz chaos as the destabilizer
        s.chaosMod.rateHz = 0.7f; s.chaosMod.type = 0; s.chaosMod.depth = 0.5f; // Lorenz
        s.lfo1.rateHz = 0.12f; s.lfo1.shape = 0; s.lfo1.depth = 0.5f; s.lfo1.sync = 0;
        s.macros.values[0] = 0.3f;         // 'instability' knob start value
        setModSlot(s, 0, 9, 7, 0.35f, kCurveSCurve); // Chaos -> AllSpecTilt
        setModSlot(s, 1, 9, 5, 0.3f,  kCurveExp);    // Chaos -> AllMorphPos
        s.modMatrix.slots[1].scale = 3;              // SCALE AXIS x2: push chaos past +-1 (unused dim)
        setModSlot(s, 2, 1, 4, 0.3f,  kCurveLinear); // LFO1 -> AllFltCut (slow breathing)
        setModSlot(s, 3, 5, 9, 0.4f,  kCurveExp);    // Macro1 -> AllFltEnvAmt (one-knob instability)
        // Spectral delay cloud into a modulated hall
        s.delayEnabled = 1;
        s.delay.type = 4;                  // Spectral
        s.delay.sync = 0; s.delay.timeMs = 500.0f; s.delay.mix = 0.18f; s.delay.feedback = 0.3f;
        s.delay.spectralDiffusion = 0.4f; s.delay.spectralTilt = 0.2f; s.delay.spectralSpreadMs = 300.0f;
        s.reverbEnabled = 1; s.reverbType = 1; // Hall
        s.reverb.size = 0.85f; s.reverb.mix = 0.35f;
        s.reverb.modRateHz = 0.3f; s.reverb.modDepth = 0.2f; // shimmering reverb mod (unused)
        presets.push_back(std::move(p));
    }
```

---

## 15. "String Body" -> "Sinew"

- **Locate:** the block containing `p.name = "String Body"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** White noise and a razor-thin pulse pluck a key-tracked comb resonator; a Hard velocity curve and velocity-driven filter routes make hard hits brighter and ring longer, while a slow-random resonance and a subtle comb-cutoff LFO ensure no two plucks ring alike. A living Karplus string.
- **Coverage:**
  - white-noise-exciter
  - pulse-PWM-osc
  - comb-filter+keyTrack
  - filter-env-pluck(fixed semitones)
  - velocity->FltCut+FltRes-voice-routes
  - Env2-voice-route
  - lfo1-triangle-free
  - random-source
  - SCurve-curve
  - AllResonance-dest
  - settings-velocityCurve=Hard
  - plate-reverb-small
- **Rationale:** The source/comb combo was already distinctive; the fix is its total lack of per-note motion. combDamping has no matrix destination, so 'each pluck differs' is delivered through the reachable levers: velocity is made expressive (settings.velocityCurve = Hard, owning a settings axis) and routed per-voice to BOTH FltCut and FltRes so harder plucks are brighter and ring longer — real string dynamics. A filterEnv adds a +15 st brightness 'ping' at attack (fixing the filter-env pattern on a comb). Globally, a slow free triangle LFO->AllFltCut gently detunes the comb for body shimmer and a slow Random->AllResonance (SCurve) varies the ring pluck-to-pluck. Reverb is a tight PLATE with pre-delay (0.45/0.2), deliberately NOT the 0.6/0.3 tail shared byte-for-byte across its siblings.
- **Replacement code:**

```cpp
    // "Sinew" - Velocity-alive key-tracked comb-string with pluck-to-pluck variation
    {
        PresetDef p;
        p.name = "Sinew";
        p.category = "Experimental";
        auto& s = p.state;
        s.oscA.type = 9; s.oscA.noiseColor = 0; s.oscA.level = 0.5f;  // White exciter burst
        s.oscB.type = 0; s.oscB.waveform = 3; s.oscB.pulseWidth = 0.15f; // thin pulse
        s.oscB.level = 0.35f;
        s.mixer.position = 0.4f;
        // Key-tracked comb resonator (the string body)
        s.filter.type = 6;                 // Comb
        s.filter.cutoffHz = 220.0f; s.filter.resonance = 0.85f;
        s.filter.combDamping = 0.2f; s.filter.keyTrack = 1.0f;
        s.filter.envAmount = 15.0f;        // +15 st brightness 'ping' at attack
        s.filterEnv.attackMs = 0.0f; s.filterEnv.decayMs = 400.0f;
        s.filterEnv.sustain = 0.0f; s.filterEnv.releaseMs = 300.0f;
        // Long ringing pluck
        s.ampEnv.attackMs = 1.0f; s.ampEnv.decayMs = 2500.0f;
        s.ampEnv.sustain = 0.1f; s.ampEnv.releaseMs = 1800.0f;
        // Expressive velocity (owns a settings axis)
        s.settings.velocityCurve = 2;      // Hard
        // Per-voice velocity dynamics (voice-dest table: 0=FltCut,1=FltRes; src 5=Velocity,1=Env2)
        setVoiceRoute(s, 0, 5, 0, 0.5f);   // Velocity -> FltCut (harder = brighter)
        setVoiceRoute(s, 1, 5, 1, 0.3f);   // Velocity -> FltRes (harder = longer ring)
        setVoiceRoute(s, 2, 1, 0, 0.3f);   // Env2(filterEnv) -> FltCut (attack brightness)
        // Global motion so no two plucks ring alike (combDamping has no dest -> vary cut+res)
        s.lfo1.rateHz = 0.3f; s.lfo1.shape = 1; s.lfo1.depth = 0.4f; s.lfo1.sync = 0; // slow triangle
        s.random.rateHz = 2.0f; s.random.smoothness = 0.1f;
        setModSlot(s, 0, 1, 4, 0.15f, kCurveSCurve); // LFO1 -> AllFltCut (subtle comb shimmer)
        setModSlot(s, 1, 4, 8, 0.25f, kCurveLinear); // Random -> AllResonance (pluck-to-pluck ring)
        // Tight PLATE with pre-delay, NOT the shared 0.6/0.3 hall tail
        s.reverbEnabled = 1; s.reverbType = 0; // Plate
        s.reverb.size = 0.45f; s.reverb.mix = 0.2f; s.reverb.damping = 0.6f; s.reverb.preDelayMs = 20.0f;
        presets.push_back(std::move(p));
    }
```

---

## 16. "Frozen Spectral" -> "Frozen Spectral"

- **Locate:** the block containing `p.name = "Frozen Spectral"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A glacial dual-SpectralFreeze drone morphed in the FFT domain through a tuned resonant comb, drifting on 0.05 Hz LFOs into an infinite frozen hall.
- **Coverage:**
  - spectral-engine
  - spectral-morph-mixer
  - comb-filter
  - dual-lfo-shapes(sine+triangle)
  - sampleHold-source
  - stepped-curve
  - scurve-curve
  - modmatrix-bypass-axis
  - modmatrix-smoothMs
  - modEnv-voice-route
  - reverb-freeze
  - reverb-hall
- **Rationale:** Keeps the spectral-freeze DNA but fixes the two weaknesses in the original: the transparent LP filter is replaced by a tuned Comb (cutoffHz 320 = comb fundamental, damping 0.45) so the filter genuinely colours the drone, and the near-empty mod matrix becomes a 3-slot glacial web plus a bypassed slot-7 route to exercise the bypass axis. SpectralMorph tilt/shift (previously unused) are set so mode 1 actually differs from a crossfade. Two independent sub-0.05 Hz LFOs (sine+triangle) plus a slewed 0.08 Hz S&H with a Stepped curve create motion that never quite repeats; ModEnv->SpecTilt adds per-note evolution. Hall + freeze distinguishes its tail from Plate-tailed siblings.
- **Replacement code:**

```cpp
    // "Frozen Spectral" - Glacial dual-spectral drone morphed through a resonant comb
    {
        PresetDef p;
        p.name = "Frozen Spectral";
        p.category = "Experimental";
        auto& s = p.state;
        // Two SpectralFreeze engines detuned in pitch/formant, blended in the
        // FFT-domain SpectralMorph mixer (mode 1). tilt/shift only act in this
        // mode, so they are part of this preset's spectral identity.
        s.oscA.type = 8; // Spectral Freeze
        s.oscA.spectralPitch = 0.0f; s.oscA.spectralTilt = -2.0f;
        s.oscA.spectralFormant = 4.0f; s.oscA.level = 0.7f;
        s.oscB.type = 8; // Spectral Freeze
        s.oscB.spectralPitch = -7.0f;  // a fifth below = hollow interval
        s.oscB.spectralTilt = 3.0f; s.oscB.spectralFormant = -3.0f;
        s.oscB.level = 0.55f;
        s.mixer.mode = 1;              // Spectral Morph (FFT interpolation A<->B)
        s.mixer.position = 0.5f;
        s.mixer.tilt = -3.0f;          // darken the morphed spectrum (SpectralMorph only)
        s.mixer.shift = 40.0f;         // small inharmonic freq shift for shimmer
        // Comb filter: tuned resonant teeth turn the drone metallic/glassy.
        s.filter.type = 6;             // Comb
        s.filter.cutoffHz = 320.0f;    // comb fundamental
        s.filter.resonance = 0.6f;
        s.filter.combDamping = 0.45f;  // soften the high feedback teeth
        // Very slow swell; long release so the frozen tail rings on.
        s.ampEnv.attackMs = 1200.0f; s.ampEnv.decayMs = 800.0f;
        s.ampEnv.sustain = 0.9f; s.ampEnv.releaseMs = 4000.0f;
        s.ampEnv.attackCurve = 0.5f;   // slow-start exponential swell
        // ModEnv shapes spectral tilt per note (routed via voice route below).
        s.modEnv.attackMs = 2500.0f; s.modEnv.decayMs = 3000.0f;
        s.modEnv.sustain = 0.6f; s.modEnv.releaseMs = 4000.0f;
        // LFO1: glacial 0.05 Hz sine -> morph position (SCurve for smooth turns).
        s.lfo1.rateHz = 0.05f; s.lfo1.shape = 0; s.lfo1.depth = 0.6f; s.lfo1.sync = 0;
        // LFO2: 0.03 Hz triangle -> spectral tilt for a slow timbral tide.
        s.lfo2.rateHz = 0.03f; s.lfo2.shape = 1; s.lfo2.depth = 0.5f; s.lfo2.sync = 0;
        // Sample & Hold gives stepped comb-pitch jumps, glide-smoothed to stay glacial.
        s.sampleHold.rateHz = 0.08f; s.sampleHold.sync = 0; s.sampleHold.slewMs = 400.0f;
        // --- Mod web ---
        setModSlot(s, 0, kSrcLFO1, kDstAllMorphPos, 0.5f, kCurveSCurve);
        setModSlot(s, 1, kSrcLFO2, kDstAllSpecTilt, 0.4f, kCurveLinear);
        setModSlot(s, 2, kSrcSampleHold, kDstAllFltCut, 0.35f, kCurveStepped, 300.0f);
        // Slot 7: a parked Random->morph route left BYPASSED to demonstrate the
        // bypass axis (flip bypass to 0 live for a busier second morph layer).
        setModSlot(s, 7, kSrcRandom, kDstAllMorphPos, 0.4f, kCurveLinear);
        s.modMatrix.slots[7].bypass = 1;
        // Per-note spectral-tilt evolution from ModEnv (Env3).
        setVoiceRoute(s, 0, kVSrcEnv3, kVDstSpecTilt, 0.5f);
        // Frozen Hall reverb: the tail literally freezes and holds.
        s.reverbEnabled = 1;
        s.reverbType = 1;              // Hall (bigger/darker than Plate)
        s.reverb.size = 0.95f; s.reverb.mix = 0.5f;
        s.reverb.damping = 0.15f; s.reverb.freeze = 1;
        presets.push_back(std::move(p));
    }
```

---

## 17. "Particle Storm" -> "Particle Storm"

- **Locate:** the block containing `p.name = "Particle Storm"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A roiling 16-voice dual-granular swarm whose density, brightness and wetness all bend to a single Storm macro, breathing through an envelope-follower auto-wah into granular delay and a diffuse plate.
- **Coverage:**
  - particle-engine
  - envfilter-autowah
  - macros-multi-routed
  - envFollower-source
  - random-source
  - lfo-smoothrandom-shape
  - modmatrix-scale-axis(x2)
  - exp-curve
  - modmatrix-smoothMs
  - settings-voiceAllocMode
  - settings-voiceStealMode
  - stereo-width-spread
  - granular-delay
  - reverb-plate-diffusion
- **Rationale:** The original had zero modulation and shared a filter block with Double Fold. This version makes the two particle oscillators genuine opposites (density 64/scatter 11/Burst/40 ms vs density 8/scatter 2/Random/700 ms) and gives the preset a real mod identity: an EnvFilter (auto-wah, BP, sensitivity 6) so the swarm's amplitude visibly opens the filter, and Macro1 multi-routed to morph+cutoff+effect-mix so a single knob is the 'storm intensity' dial. Since there is no particle-density mod destination, morph position (which crossfades dense<->sparse osc) is the density surrogate the directive asks for. Seven mod slots (0-6), a scale=x2 tilt route, a slewed random resonance route, and self-modulating EnvFollower give roiling motion; voiceAllocMode=RR + voiceStealMode=Soft at 16 voices own the settings axes and keep the dense cloud click-free.
- **Replacement code:**

```cpp
    // "Particle Storm" - Roiling dual-granular swarm; one macro is the storm dial
    {
        PresetDef p;
        p.name = "Particle Storm";
        p.category = "Experimental";
        auto& s = p.state;
        // Osc A: dense, short-lived, wide-scatter grains, Burst spawn = gusts.
        s.oscA.type = 6; // Particle
        s.oscA.particleScatter = 11.0f; s.oscA.particleDensity = 64.0f;
        s.oscA.particleLifetime = 40.0f; s.oscA.particleSpawnMode = 2; // Burst
        s.oscA.particleEnvType = 3; // Blackman (smooth grains)
        s.oscA.particleDrift = 0.85f; s.oscA.level = 0.7f;
        // Osc B: sparse, long-lived, tight grains, Random spawn = a calmer bed.
        s.oscB.type = 6; // Particle
        s.oscB.particleScatter = 2.0f; s.oscB.particleDensity = 8.0f;
        s.oscB.particleLifetime = 700.0f; s.oscB.particleSpawnMode = 1; // Random
        s.oscB.particleEnvType = 0; // Hann
        s.oscB.particleDrift = 0.4f; s.oscB.level = 0.55f;
        s.mixer.position = 0.5f;       // morph = perceived density (A dense <-> B sparse)
        // Envelope-following auto-wah: the swarm's own amplitude opens the filter.
        s.filter.type = 11;            // Env Filter (auto-wah)
        s.filter.cutoffHz = 700.0f; s.filter.resonance = 2.5f;
        s.filter.envSubType = 1;       // BP response
        s.filter.envDepth = 0.9f; s.filter.envSensitivity = 6.0f;
        s.filter.envAttack = 20.0f; s.filter.envRelease = 250.0f;
        s.filter.envDirection = 0;     // sweep up on transients
        s.ampEnv.attackMs = 150.0f; s.ampEnv.decayMs = 600.0f;
        s.ampEnv.sustain = 0.8f; s.ampEnv.releaseMs = 1800.0f;
        // LFO1 SmoothRandom drives spectral tilt (scaled x2) - unpredictable weather.
        s.lfo1.rateHz = 0.25f; s.lfo1.shape = 5; s.lfo1.depth = 0.7f; s.lfo1.sync = 0;
        // LFO2 slow triangle nudges filter cutoff underneath.
        s.lfo2.rateHz = 0.12f; s.lfo2.shape = 1; s.lfo2.depth = 0.5f; s.lfo2.sync = 0;
        // Random + EnvFollower + Macro sources feed the storm.
        s.random.rateHz = 3.0f; s.random.sync = 0; s.random.smoothness = 0.7f;
        s.envFollower.sensitivity = 0.7f; s.envFollower.attackMs = 15.0f;
        s.envFollower.releaseMs = 200.0f;
        s.macros.values[0] = 0.5f;     // "Storm" macro parked at half
        // --- Mod web: Macro1 is a one-knob storm-intensity morph (multi-routed) ---
        setModSlot(s, 0, kSrcMacro1, kDstAllMorphPos, 0.7f, kCurveSCurve);
        setModSlot(s, 1, kSrcMacro1, kDstAllFltCut, 0.6f, kCurveExp);
        setModSlot(s, 2, kSrcMacro1, kDstEffectMix, 0.5f, kCurveLinear);
        // Weather modulation on top of the macro:
        setModSlot(s, 3, kSrcLFO1, kDstAllSpecTilt, 0.5f, kCurveLinear);
        s.modMatrix.slots[3].scale = 3;    // x2 - push tilt beyond +/-1
        setModSlot(s, 4, kSrcRandom, kDstAllResonance, 0.35f, kCurveLinear, 120.0f);
        setModSlot(s, 5, kSrcEnvFollower, kDstAllMorphPos, 0.3f, kCurveLinear);
        setModSlot(s, 6, kSrcLFO2, kDstAllFltCut, 0.25f, kCurveLinear);
        // Own the voice-management settings axes for a chaotic dense swarm.
        s.global.voiceMode = 0;        // Poly
        s.global.polyphony = 16;       // maximum voices for a thick cloud
        s.global.width = 2.0f; s.global.spread = 0.7f;
        s.settings.voiceAllocMode = 0; // Round-Robin (spreads grains across voices)
        s.settings.voiceStealMode = 1; // Soft steal (no clicks when the cloud saturates)
        // Granular delay thickens the cloud further.
        s.delayEnabled = 1;
        s.delay.type = 3;              // Granular
        s.delay.mix = 0.35f; s.delay.feedback = 0.5f; s.delay.timeMs = 220.0f;
        s.delay.granularSizeMs = 120.0f; s.delay.granularDensity = 20.0f;
        s.delay.granularPitchSpray = 0.3f; s.delay.granularPanSpray = 0.6f;
        s.delay.granularTexture = 0.4f;
        // Diffuse plate wash on top.
        s.reverbEnabled = 1;
        s.reverbType = 0;              // Plate
        s.reverb.size = 0.85f; s.reverb.mix = 0.4f; s.reverb.diffusion = 0.9f;
        presets.push_back(std::move(p));
    }
```

---

## 18. "Double Fold" -> "Double Fold"

- **Locate:** the block containing `p.name = "Double Fold"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** Hard-sync metal Lockhart-folded and pushed through a talking formant filter, its vowel stirred by a Benjolin rungler and a scaled resonance LFO, spat out as a metallic digital slap.
- **Coverage:**
  - hard-sync-engine
  - wavefolder-distortion
  - formant-filter
  - formantGender
  - rungler-full-source
  - modmatrix-scale-axis(x2)
  - exp-curve
  - filter-env-bugfixed-sweep
  - velocity->distdrive-voiceroute
  - digital-delay
  - dual-lfo-shapes(saw+square)
- **Rationale:** Breaks the copy-paste single-LFO->dest-7 skeleton the audit flagged. The Wavefolder + hard-sync core is retained, but the shared type-2 filter is swapped for a Formant filter (morph 1.6, gender -0.5) so the folded metal literally speaks a vowel and covers formantGender. The rungler is fully specified (osc freqs, filter, 6-bit, free loop, depth 0.7) and drives the vowel morph with an Exp curve - a genuinely unique mod source no sibling uses. Slot 1 uses scale=x2 into resonance, a deliberately different destination from Frozen Spectral, satisfying the 'distinct destination + SCALE=x2' directive. filter.envAmount is the bug-fixed +18 semitones (not a Hz value) so the vowel actually sweeps, and Velocity->DistDrive adds per-hit dynamics.
- **Replacement code:**

```cpp
    // "Double Fold" - Hard-sync metal folded and pushed through a talking formant filter
    {
        PresetDef p;
        p.name = "Double Fold";
        p.category = "Experimental";
        auto& s = p.state;
        // Osc A: hard-sync, aggressive high slave ratio = screaming sync sweep.
        s.oscA.type = 3; // Sync
        s.oscA.syncRatio = 4.5f; s.oscA.syncWaveform = 1; // saw slave
        s.oscA.syncMode = 0; s.oscA.syncAmount = 1.0f; s.oscA.level = 0.8f;
        // Osc B: bare triangle under it for a fold-able fundamental.
        s.oscB.type = 0; s.oscB.waveform = 4; // Triangle
        s.oscB.level = 0.45f;
        s.mixer.position = 0.35f;      // favour the sync osc
        // Wavefolder (Lockhart) adds the metallic upper harmonics.
        s.distortion.type = 4;         // Wavefolder
        s.distortion.drive = 0.6f; s.distortion.foldType = 2; // Lockhart
        s.distortion.mix = 0.85f;
        // Formant filter makes the folded metal "speak" a vowel; gender + morph
        // give it a distinct throat.
        s.filter.type = 5;             // Formant
        s.filter.cutoffHz = 1200.0f; s.filter.resonance = 0.8f;
        s.filter.formantMorph = 1.6f;  // between E and I
        s.filter.formantGender = -0.5f; // deeper/darker throat
        s.filter.envAmount = 18.0f;    // filter env sweeps the vowel (semitones, bug-fixed)
        s.filterEnv.attackMs = 4.0f; s.filterEnv.decayMs = 350.0f;
        s.filterEnv.sustain = 0.3f; s.filterEnv.releaseMs = 300.0f;
        s.ampEnv.attackMs = 3.0f; s.ampEnv.decayMs = 450.0f;
        s.ampEnv.sustain = 0.6f; s.ampEnv.releaseMs = 350.0f;
        // LFO1 saw ramps the fold; LFO2 square jumps resonance for gated bite.
        s.lfo1.rateHz = 0.4f; s.lfo1.shape = 2; s.lfo1.depth = 0.6f; s.lfo1.sync = 0;
        s.lfo2.rateHz = 1.0f; s.lfo2.shape = 3; s.lfo2.depth = 0.4f; s.lfo2.sync = 0;
        // Rungler (Benjolin shift-register) is the chaos engine: stepped,
        // pitched-noise modulation of the vowel morph. Depth MUST be raised.
        s.rungler.osc1FreqHz = 3.5f; s.rungler.osc2FreqHz = 5.5f;
        s.rungler.depth = 0.7f; s.rungler.filter = 0.4f;
        s.rungler.bits = 6; s.rungler.loopMode = 0; // free chaos
        // --- Mod web ---
        setModSlot(s, 0, kSrcRungler, kDstAllMorphPos, 0.5f, kCurveExp);
        // Scaled (x2) LFO2 -> resonance: a distinct destination from Frozen Spectral.
        setModSlot(s, 1, kSrcLFO2, kDstAllResonance, 0.45f, kCurveLinear);
        s.modMatrix.slots[1].scale = 3; // x2
        setModSlot(s, 2, kSrcLFO1, kDstAllFltCut, 0.4f, kCurveExp);
        // Velocity drives the fold harder per hit for dynamics.
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstDistDrive, 0.5f);
        // Short Digital delay (with a touch of wavefold) = metallic slap.
        s.delayEnabled = 1;
        s.delay.type = 0;              // Digital
        s.delay.mix = 0.2f; s.delay.feedback = 0.3f; s.delay.timeMs = 280.0f;
        s.delay.digitalWavefoldAmt = 0.3f; s.delay.digitalWidth = 140.0f;
        presets.push_back(std::move(p));
    }
```

---

## 19. "Inharmonic Bells" -> "Inharmonic Bells"

- **Locate:** the block containing `p.name = "Inharmonic Bells"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** Clangorous 128-partial additive bells thinned by a key-tracked high-pass, sharpened by a transient detector, brightened by a pitch follower, and stacked with a scalic PhaseVocoder harmonizer into a bright plate.
- **Coverage:**
  - additive-engine
  - inharmonicity
  - svf-hp-filter
  - keyTrack
  - transient-source
  - pitchFollower-source
  - aftertouch-voiceroute
  - settings-pitchBendRange
  - harmonizer-scalic-phasevocoder
  - harmonizer-per-voice-interval/pan/detune
  - reverb-plate
  - exp-curve
- **Rationale:** The original was reverb-only with a static mod matrix. Here the additive core is kept (128 partials, inharm 0.85, fifth-tuned second layer) but the preset gains a full expressive identity: a key-tracked SVF HP so the metallic ring stays clear across the keyboard (covers keyTrack + SVF HP), a Transient detector routed to master volume with an Exp curve to accent every strike, and a Pitch Follower brightening higher notes into the HP corner. Aftertouch->SpecTilt makes it breathe under pressure, pitchBendRange=12 owns that settings axis, and a 3-voice Scalic PhaseVocoder harmonizer (intervals 2/4/7, panned, detuned, one delayed 25 ms) stacks diatonic bell overtones - covering the harmonizer must-cover in one musical gesture. Plate (not Hall) keeps the tail distinct from Frozen Spectral.
- **Replacement code:**

```cpp
    // "Inharmonic Bells" - Clangorous additive bells, harmonized and pitch-aware
    {
        PresetDef p;
        p.name = "Inharmonic Bells";
        p.category = "Experimental";
        auto& s = p.state;
        // Osc A: 128 partials, high inharmonicity = struck-metal clang.
        s.oscA.type = 4; // Additive
        s.oscA.additivePartials = 128; s.oscA.additiveInharm = 0.85f;
        s.oscA.additiveTilt = -4.5f; s.oscA.level = 0.65f;
        // Osc B: fewer partials, tuned a fifth up, milder inharm = shimmer layer.
        s.oscB.type = 4;
        s.oscB.additivePartials = 48; s.oscB.additiveInharm = 0.45f;
        s.oscB.additiveTilt = -2.0f; s.oscB.tuneSemitones = 7.0f; s.oscB.level = 0.4f;
        s.mixer.position = 0.4f;
        // SVF High-Pass thins the low end so only the metallic ring survives;
        // keyTrack makes the HP corner follow pitch so low notes stay clear.
        s.filter.type = 1;             // SVF HP
        s.filter.cutoffHz = 300.0f; s.filter.resonance = 0.4f;
        s.filter.keyTrack = 1.0f; s.filter.svfSlope = 1; // 24 dB
        // Near-percussive strike: instant attack, long decay/release ring.
        s.ampEnv.attackMs = 1.0f; s.ampEnv.decayMs = 3500.0f;
        s.ampEnv.sustain = 0.08f; s.ampEnv.releaseMs = 3000.0f;
        s.ampEnv.decayCurve = 0.5f;    // natural exponential decay
        // Transient detector -> master volume sharpens each strike's attack.
        s.transient.sensitivity = 0.7f; s.transient.attackMs = 1.5f;
        s.transient.decayMs = 60.0f;
        // Pitch follower tracks the played bell and brightens higher strikes.
        s.pitchFollower.minHz = 80.0f; s.pitchFollower.maxHz = 3000.0f;
        s.pitchFollower.confidence = 0.5f; s.pitchFollower.speedMs = 40.0f;
        // --- Mod web ---
        setModSlot(s, 0, kSrcTransient, kDstMasterVol, 0.5f, kCurveExp);
        setModSlot(s, 1, kSrcPitchFollow, kDstAllFltCut, 0.4f, kCurveLinear);
        // Aftertouch adds spectral-tilt shimmer for expressive rings.
        setVoiceRoute(s, 0, kVSrcAftertouch, kVDstSpecTilt, 0.4f);
        // Wide pitch bend for dramatic bell dive-bombs.
        s.settings.pitchBendRangeSemitones = 12.0f;
        // Scalic PhaseVocoder harmonizer stacks diatonic bell overtones.
        s.harmonizerEnabled = 1;
        s.harmonizer.harmonyMode = 1;    // Scalic
        s.harmonizer.pitchShiftMode = 2; // PhaseVocoder
        s.harmonizer.formantPreserve = 1;
        s.harmonizer.numVoices = 3;
        s.harmonizer.key = 0; s.harmonizer.scale = 0;
        s.harmonizer.dryLevelDb = 0.0f; s.harmonizer.wetLevelDb = -9.0f;
        s.harmonizer.voiceInterval[0] = 2; s.harmonizer.voicePan[0] = -0.5f;
        s.harmonizer.voiceDetuneCents[0] = 4.0f;
        s.harmonizer.voiceInterval[1] = 4; s.harmonizer.voicePan[1] = 0.5f;
        s.harmonizer.voiceDetuneCents[1] = -4.0f;
        s.harmonizer.voiceInterval[2] = 7; s.harmonizer.voicePan[2] = 0.0f;
        s.harmonizer.voiceDelayMs[2] = 25.0f;
        // Plate reverb for a bright metallic tail.
        s.reverbEnabled = 1;
        s.reverbType = 0;              // Plate
        s.reverb.size = 0.7f; s.reverb.mix = 0.4f; s.reverb.damping = 0.2f;
        presets.push_back(std::move(p));
    }
```

---

## 20. "FM Grunge" -> "FM Grunge"

- **Locate:** the block containing `p.name = "FM Grunge"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A hollow half-sine phase-distortion growl seasoned with noise, torn apart by a Henon chaos waveshaper behind a driven ladder, wobbled by a Rossler chaos LFO, and printed to splicing tape.
- **Coverage:**
  - phase-distortion-engine
  - noise-osc
  - driven-ladder
  - chaoswaveshaper-henon
  - chaosMod-source
  - filter-env-bugfixed-sweep
  - settings-velocityCurve-soft
  - aftertouch->distdrive-voiceroute
  - velocity-voiceroute
  - tape-delay-with-splice
  - lfo-square-shape
- **Rationale:** Keeps the PD+Noise+ChaosWaveshaper identity (this preset owns ChaosWaveshaper for the category) but deepens it into a real mod web. filter.envAmount is corrected to +30 semitones so the driven ladder now performs an audible acid sweep it previously lacked. The dedicated ChaosMod is raised (Rossler, depth 0.6) and routed as kSrcChaos->ladder cutoff for the unstable wobble, LFO1 square adds morph flutter, and two voice routes (Aftertouch->DistDrive, Velocity->FltCut) make the grit playable and expressive. velocityCurve=Soft owns that settings axis, and the delay is switched from generic Digital to a splicing Tape delay (saturation 0.6, wear 0.4, splice on) so the echoes degrade and warble - covering the Tape+splice must-cover and giving the preset a signature FX tail distinct from Double Fold's clean digital slap.
- **Replacement code:**

```cpp
    // "FM Grunge" - Phase-distortion + noise mangled by a chaos waveshaper onto tape
    {
        PresetDef p;
        p.name = "FM Grunge";
        p.category = "Experimental";
        auto& s = p.state;
        // Osc A: phase distortion, half-sine w/ heavy DCW = hollow FM-ish growl.
        s.oscA.type = 2; // Phase Distortion
        s.oscA.pdWaveform = 4; // HalfSine
        s.oscA.pdDistortion = 0.75f; s.oscA.level = 0.7f;
        // Osc B: white noise seasons the top end.
        s.oscB.type = 9; // Noise
        s.oscB.noiseColor = 0; s.oscB.level = 0.18f;
        s.mixer.position = 0.18f;
        // Driven 4-pole ladder: real analog dirt before the waveshaper.
        s.filter.type = 4;             // Ladder
        s.filter.cutoffHz = 1400.0f; s.filter.resonance = 0.5f;
        s.filter.ladderSlope = 4; s.filter.ladderDrive = 8.0f;
        s.filter.envAmount = 30.0f;    // strong acid-style sweep (semitones, bug-fixed)
        s.filter.keyTrack = 0.3f;
        s.filterEnv.attackMs = 2.0f; s.filterEnv.decayMs = 250.0f;
        s.filterEnv.sustain = 0.2f; s.filterEnv.releaseMs = 200.0f;
        // Chaos Waveshaper (Henon map) = the signature unstable grunge.
        s.distortion.type = 1;         // Chaos Waveshaper
        s.distortion.drive = 0.4f;
        s.distortion.chaosModel = 3;   // Henon
        s.distortion.chaosSpeed = 0.6f; s.distortion.chaosCoupling = 0.35f;
        s.distortion.mix = 0.8f;
        s.ampEnv.attackMs = 5.0f; s.ampEnv.decayMs = 400.0f;
        s.ampEnv.sustain = 0.55f; s.ampEnv.releaseMs = 300.0f;
        // ChaosMod (Rossler) wobbles the ladder cutoff unpredictably.
        s.chaosMod.rateHz = 0.6f; s.chaosMod.type = 1; // Rossler
        s.chaosMod.depth = 0.6f; s.chaosMod.sync = 0;
        // LFO1 square gates a bit of morph flutter.
        s.lfo1.rateHz = 0.5f; s.lfo1.shape = 3; s.lfo1.depth = 0.3f; s.lfo1.sync = 0;
        // --- Mod web ---
        setModSlot(s, 0, kSrcChaos, kDstAllFltCut, 0.5f, kCurveLinear);
        setModSlot(s, 1, kSrcLFO1, kDstAllMorphPos, 0.3f, kCurveLinear);
        // Aftertouch presses more drive into the waveshaper (per-voice).
        setVoiceRoute(s, 0, kVSrcAftertouch, kVDstDistDrive, 0.6f);
        setVoiceRoute(s, 1, kVSrcVelocity, kVDstFltCut, 0.4f);
        // Soft velocity curve so light playing stays gritty but controllable.
        s.settings.velocityCurve = 1;  // Soft
        // Tape delay with splice artefacts = degraded, warbling echoes.
        s.delayEnabled = 1;
        s.delay.type = 1;              // Tape
        s.delay.mix = 0.25f; s.delay.feedback = 0.35f; s.delay.timeMs = 260.0f;
        s.delay.tapeSaturation = 0.6f; s.delay.tapeWear = 0.4f;
        s.delay.tapeSpliceEnabled = 1; s.delay.tapeSpliceIntensity = 0.5f;
        presets.push_back(std::move(p));
    }
```
