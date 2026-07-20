# Ruinae Preset Plan — Arp Euclidean

This category is a trio of Euclidean-rhythm arpeggiator presets, but the redesign gives each one a fully distinct sonic identity so they never read as the same patch with a different hit count. Three axes are held mutually exclusive across the three members: (1) **synthesis engine** — Wavetable, Phase-Distortion DoubleSine, and Particle-swarm, each of which was previously under-used or unused; (2) **per-preset modulation gesture** — a Stepped S&H morph, a slow free-running LFO morph, and a synced hard-stepped Random-resonance sparkle, with no two presets sharing a source→destination route; (3) **FX/space** — one dry percussive member, one warm tape-delay member, one dry high-energy member. Each preset also carries its own scale colour (E-Phrygian, D-Dorian, A-natural-minor), Euclidean signature (E(3,8), E(5,16), E(7,16)), octave range, and gate/velocity lane shaping so the collection covers `euclideanEnabled/hits/steps/rotation`, arp swing, gate lanes, velocity lanes, and three separate voice engines. All three replace the legacy filter-env `envAmount` bug (values like 4000 that were being treated as raw Hz) with sensible semitone sweep depths.

## 1. "Tresillo E(3,8)" -> "Obsidian Tresillo"

- **Locate:** the block containing `p.name = "Tresillo E(3,8)"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A dry, metallic Wavetable pluck in E-Phrygian that spits a tresillo E(3,8) with a hard filter-envelope tick and per-note wavetable-blend stepping — no reverb, all rhythm.
- **Coverage:** `euclideanEnabled/hits/steps/rotation`; E(3,8) tresillo; scaleType Phrygian + rootNote; Wavetable engine; octaveRange.
- **Rationale:** `type=1` selects the Wavetable engine (the audit's biggest gap); `phaseMod=0.35` gives the wavetable an FM sheen so it never reads as a saw. SVF LP 24 dB with resonance 3.5 and the corrected `envAmount=+32` semitones (replacing the nonsensical 4000) makes a real closing-tick sweep since `filterEnv.sustain=0`. The S&H->MorphPos Stepped route is the per-preset gesture no sibling repeats — it re-picks the A/B table mix on every 16th. E-Phrygian root=4/scale=6. No reverb keeps it the dry percussive member of the trio.
- **Replacement code:**

```cpp
    {
        PresetDef p;
        p.name = "Obsidian Tresillo";
        p.category = "Arp Euclidean";
        RuinaePresetState& s = p.state;

        // --- Voice: Wavetable pluck (gives the never-used Wavetable engine a home) ---
        s.oscA.type = 1;            // Wavetable
        s.oscA.waveform = 1;        // Saw table
        s.oscA.phaseMod = 0.35f;    // wavetable PM -> metallic FM sheen; unmistakably not a plain saw
        s.oscA.level = 0.85f;
        s.oscB.type = 1;            // Wavetable
        s.oscB.waveform = 2;        // Square table
        s.oscB.tuneSemitones = 12.0f;   // octave-up bite on the attack transient
        s.oscB.fineCents = 4.0f;    // hair of detune so the two tables shimmer
        s.oscB.level = 0.4f;
        s.mixer.mode = 0;           // Crossfade
        s.mixer.position = 0.4f;    // favor OscA body

        // --- Filter: 24 dB SVF LP with a REAL (bug-free) filter-env pluck sweep ---
        s.filter.type = 0;          // SVF LP
        s.filter.svfSlope = 1;      // 24 dB/oct for a tight edge
        s.filter.cutoffHz = 1100.0f;
        s.filter.resonance = 3.5f;  // singing, not whistling
        s.filter.envAmount = 32.0f; // +32 semitones sweep (FIXED: plain semitones, not the old 4000 Hz bug)
        s.filter.keyTrack = 0.5f;   // keep plucks even across the keyboard

        // --- Envelopes: short percussive body, zero wash ---
        s.ampEnv.attackMs = 2.0f;
        s.ampEnv.decayMs = 170.0f;
        s.ampEnv.sustain = 0.12f;
        s.ampEnv.releaseMs = 120.0f;
        s.ampEnv.decayCurve = 0.4f; // exp-ish decay = snappier pluck tail
        s.filterEnv.attackMs = 1.0f;
        s.filterEnv.decayMs = 150.0f;
        s.filterEnv.sustain = 0.0f; // sweep fully closes -> percussive "tick"
        s.filterEnv.releaseMs = 110.0f;

        // --- Mod identity (unique to this preset): S&H steps the A/B wavetable blend
        //     every 16th with a Stepped curve, so each tresillo hit picks a new table mix. ---
        s.sampleHold.sync = 1;
        s.sampleHold.noteValue = kNote1_16;
        setModSlot(s, 0, kSrcSampleHold, kDstAllMorphPos, 0.35f, kCurveStepped);
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.4f); // velocity opens the tick brighter

        // --- Scale color: E Phrygian (dark, Spanish half-step above the root) ---
        s.arp.scaleType = 6;        // Phrygian (DSP enum order)
        s.arp.rootNote = 4;         // E

        // --- Arp: E(3,8) tresillo, tight & dry ---
        setArpEnabled(s, true);
        setArpMode(s, kModeUp);
        setTempoSync(s, true);
        setArpRate(s, kNote1_16);
        setArpGateLength(s, 55.0f);  // tight staccato so the tresillo speaks
        s.arp.octaveRange = 1;
        setEuclidean(s, true, 3, 8, 0);
        float vel8[] = {1.0f, 0.7f, 0.72f, 0.9f, 0.7f, 0.72f, 0.85f, 0.7f}; // accent the 3 struck positions
        setVelocityLane(s, 8, vel8);

        // Dry percussive member: NO reverb - the rhythm carries it.
        presets.push_back(std::move(p));
    }
```

## 2. "Bossa E(5,16)" -> "Marimba Bossa"

- **Locate:** the block containing `p.name = "Bossa E(5,16)"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A warm FM-marimba mallet built from a Phase-Distortion DoubleSine voice in D-Dorian, playing a rotated, swung E(5,16) through a light tape delay with a slow free LFO drifting its timbre.
- **Coverage:** `euclideanEnabled/hits/steps/rotation`; E(5,16); scaleType Dorian; PhaseDistortion DoubleSine (FM-ish); arp swing; octaveRange.
- **Rationale:** `type=2` + `pdWaveform=3` + `pdDistortion=0.55` is a Phase-Distortion DoubleSine — an FM-ish mallet with an overtone bloom on the attack, clearly not a saw pad; a -12 sine sub gives it body. Corrected `envAmount=+20` semitones plus low `ampEnv.sustain=0.1` makes a wooden mallet strike. The per-preset gesture is a FREE (`sync=0`) 0.28 Hz LFO -> MorphPos on an S-curve, a slow non-metric drift distinct from Tresillo's Stepped S&H. D-Dorian (root=2/scale=4), 12% swing, and a light Tape delay (type=1, mix 0.2) make it the warm syncopated member — the only one that ships with delay, keeping the FX wrapper an identity lever.
- **Replacement code:**

```cpp
    {
        PresetDef p;
        p.name = "Marimba Bossa";
        p.category = "Arp Euclidean";
        RuinaePresetState& s = p.state;

        // --- Voice: Phase-Distortion "DoubleSine" = FM-marimba mallet tone ---
        s.oscA.type = 2;            // Phase Distortion (Casio-CZ style)
        s.oscA.pdWaveform = 3;      // DoubleSine (the two-lobe FM-ish shape)
        s.oscA.pdDistortion = 0.55f;// DCW depth -> bell/marimba overtone bloom on the attack
        s.oscA.level = 0.85f;
        s.oscB.type = 0;            // PolyBLEP sine sub for weight + a pitch anchor
        s.oscB.waveform = 0;        // Sine
        s.oscB.tuneSemitones = -12.0f;
        s.oscB.level = 0.35f;
        s.mixer.position = 0.35f;   // favor the PD mallet

        // --- Filter: gentle SVF LP with a short env pop for the mallet strike ---
        s.filter.type = 0;          // SVF LP
        s.filter.svfSlope = 1;      // 24 dB
        s.filter.cutoffHz = 3200.0f;
        s.filter.resonance = 1.2f;
        s.filter.envAmount = 20.0f; // +20 st strike sweep (FIXED semitones)

        // --- Envelopes: mallet pluck - fast attack, low sustain, medium tail ---
        s.ampEnv.attackMs = 3.0f;
        s.ampEnv.decayMs = 260.0f;
        s.ampEnv.sustain = 0.1f;
        s.ampEnv.releaseMs = 220.0f;
        s.ampEnv.decayCurve = 0.35f; // wooden mallet decay
        s.filterEnv.attackMs = 1.0f;
        s.filterEnv.decayMs = 200.0f;
        s.filterEnv.sustain = 0.05f;
        s.filterEnv.releaseMs = 180.0f;

        // --- Mod identity (unique): a slow FREE LFO gently morphs the mallet timbre
        //     across the bar via an S-curve, so the pattern breathes without any metric pulse. ---
        s.lfo1.rateHz = 0.28f;      // very slow
        s.lfo1.sync = 0;            // free-running (default is synced) -> non-metric drift
        s.lfo1.shape = 0;           // Sine
        setModSlot(s, 0, kSrcLFO1, kDstAllMorphPos, 0.25f, kCurveSCurve, 20.0f);
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.35f); // dynamics under the fingers

        // --- Scale color: D Dorian (warm minor with the bright major 6th) ---
        s.arp.scaleType = 4;        // Dorian
        s.arp.rootNote = 2;         // D

        // --- Arp: E(5,16) rotated + swung, medium detache gate ---
        setArpEnabled(s, true);
        setArpMode(s, kModeUp);
        setTempoSync(s, true);
        setArpRate(s, kNote1_16);
        setArpGateLength(s, 68.0f); // medium detache
        setArpSwing(s, 12.0f);      // 12% swing = bossa lilt
        s.arp.octaveRange = 1;
        setEuclidean(s, true, 5, 16, 2); // rotation=2 shifts the syncopation off the downbeat

        // --- Light tape delay for warmth (identity wrapper, NOT a big diffuse hall) ---
        s.delayEnabled = 1;
        s.delay.type = 1;           // Tape
        s.delay.sync = 1;
        s.delay.noteValue = kNote1_8;
        s.delay.feedback = 0.25f;
        s.delay.mix = 0.2f;
        s.delay.tapeSaturation = 0.5f;
        s.delay.tapeInertiaMs = 250.0f;

        presets.push_back(std::move(p));
    }
```

## 3. "Samba E(7,16)" -> "Granular Samba"

- **Locate:** the block containing `p.name = "Samba E(7,16)"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A dense Particle-swarm texture in A natural minor firing a busy E(7,16) across two octaves, shaped by a real accent gate lane and a driven ladder filter with per-step random resonance sparkle.
- **Coverage:** `euclideanEnabled/hits/steps/rotation`; E(7,16); Particle engine; gate lane accents; octaveRange.
- **Rationale:** `type=6` with scatter 3 / density 24 / lifetime 60 ms / Random spawn is exactly the directive's dense granular cloud; a low triangle OscB anchors pitch so the swarm stays musical. A Ladder filter (`type=4`) with 6 dB drive adds grit the SVF siblings lack, and `envAmount=+24` semitones is a real sweep. The per-preset gesture is a synced, hard-stepped Random->AllResonance route (Exp curve) giving each of the 7 hits its own resonant peak — distinct from Tresillo's Stepped-morph and Bossa's slow-LFO-morph. The gate lane with >1.0 legato accents on beats 0/4/8/12 and <1.0 cuts between them supplies the requested dynamic contour, reinforced by a matching velocity lane. A-minor (root=9/scale=1), octaveRange=2 — the granular energetic member, kept dry for maximum energy.
- **Replacement code:**

```cpp
    {
        PresetDef p;
        p.name = "Granular Samba";
        p.category = "Arp Euclidean";
        RuinaePresetState& s = p.state;

        // --- Voice: dense Particle swarm (the granular engine's showcase) ---
        s.oscA.type = 6;            // Particle
        s.oscA.particleScatter = 3.0f;   // moderate freq scatter
        s.oscA.particleDensity = 24.0f;  // dense cloud
        s.oscA.particleLifetime = 60.0f; // short grains -> percussive fizz
        s.oscA.particleSpawnMode = 1;    // Random spawn -> lively, non-mechanical
        s.oscA.particleEnvType = 0;      // Hann (smooth grain windows)
        s.oscA.particleDrift = 0.15f;    // slight pitch wander for organic motion
        s.oscA.level = 0.85f;
        s.oscB.type = 0;            // triangle anchors the pitch under the cloud
        s.oscB.waveform = 4;        // Triangle
        s.oscB.level = 0.3f;
        s.mixer.position = 0.3f;    // favor the particle cloud

        // --- Filter: Ladder LP with drive for grit on the busy pattern ---
        s.filter.type = 4;          // Ladder
        s.filter.ladderSlope = 4;   // 24 dB
        s.filter.ladderDrive = 6.0f;// input drive -> harmonic thickening/warmth
        s.filter.cutoffHz = 3800.0f;
        s.filter.resonance = 2.0f;
        s.filter.envAmount = 24.0f; // +24 st sweep (FIXED semitones)

        // --- Envelopes: percussive with a little tail so the grains bloom ---
        s.ampEnv.attackMs = 2.0f;
        s.ampEnv.decayMs = 200.0f;
        s.ampEnv.sustain = 0.2f;
        s.ampEnv.releaseMs = 170.0f;
        s.filterEnv.attackMs = 1.0f;
        s.filterEnv.decayMs = 170.0f;
        s.filterEnv.sustain = 0.1f;
        s.filterEnv.releaseMs = 150.0f;

        // --- Mod identity (unique): per-STEP Random resonance sparkle, hard-stepped and
        //     synced to the 16th grid, so each of the 7 hits gets a different resonant peak. ---
        s.random.sync = 1;
        s.random.noteValue = kNote1_16;
        s.random.smoothness = 0.0f; // hard steps, no glide
        setModSlot(s, 0, kSrcRandom, kDstAllResonance, 0.35f, kCurveExp);
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.4f); // velocity opens the cloud

        // --- Scale color: A natural minor ---
        s.arp.scaleType = 1;        // NaturalMinor
        s.arp.rootNote = 9;         // A

        // --- Arp: dense E(7,16) across 2 octaves ---
        setArpEnabled(s, true);
        setArpMode(s, kModeUp);
        setTempoSync(s, true);
        setArpRate(s, kNote1_16);
        setArpGateLength(s, 70.0f);
        s.arp.octaveRange = 2;      // wider melodic range for the busy 7-hit pattern
        setEuclidean(s, true, 7, 16, 0);

        // --- REAL accent GATE lane: dynamic contour over the 7 hits ---
        // >1.0 gates overlap into the next step (legato accents on strong beats 0/4/8/12);
        // <1.0 gates cut short elsewhere. This gives the busy pattern a breathing groove.
        float gate16[] = {1.4f, 0.6f, 0.8f, 0.6f, 1.3f, 0.6f, 0.8f, 0.7f,
                          1.4f, 0.6f, 0.8f, 0.6f, 1.2f, 0.7f, 0.9f, 0.6f};
        setGateLane(s, 16, gate16);
        // velocity lane reinforces the same accent contour
        float vel16[] = {1.0f, 0.6f, 0.72f, 0.6f, 0.95f, 0.6f, 0.75f, 0.65f,
                         1.0f, 0.6f, 0.72f, 0.6f, 0.9f, 0.65f, 0.8f, 0.6f};
        setVelocityLane(s, 16, vel16);

        presets.push_back(std::move(p));
    }
```
