# Ruinae Preset Plan — Arp Chords

The Arp Chords category is where Ruinae's harmonic-arpeggiator machinery — the chord, inversion, pitch, gate, ratchet and velocity lanes, voicing modes, scale quantization, and the Chord/AsPlayed/Converge/Diverge arp motions — must be shown off. The five members were previously near-clones of the four `setSynthPad`/`setSynthBass` templates, differing mostly in chord-lane content, so they read as one sound with different harmony. The redesign gives each preset a genuinely different **voice engine** (Wavetable, Ladder-bass, dual-Additive, Formant-vocal, Hard-sync), a **distinct mod identity** (free-run LFO on morph, stepped Random on resonance, dual slow drifts, aftertouch expression, dual velocity-to-filter/drive), and a **distinct FX wrapper** (Plate vs Hall vs Digital-echo vs Spectral-delay vs dotted-1/16 + tape). Collectively they exercise every chord lane type (Dyad/Triad/Seventh/Ninth), every inversion (Root/First/Second/Third), every voicing mode (Close/Drop2/Spread/Random), lane speed/polymetry, swing, ratchets, and the filter-env bug-class fix (envAmount expressed in semitones, not stray Hz).

## 1. "Diatonic Triads" -> "Cathedral Triads"

- **Locate:** the block containing `p.name = "Diatonic Triads"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp` (rename to `"Cathedral Triads"`).
- **Character:** A clean wavetable choir-organ that fires full C-major block triads together every 1/8, breathing slowly under a chorus into a bright plate.
- **Coverage:** chord lane Triad; voicingMode Close; scaleType Major + rootNote; scaleQuantizeInput; arp mode Chord(9); reverb (Plate) differentiation; LFO->MorphPos SCurve mod identity; Wavetable engine.
- **Rationale:** Swaps the old saw+saw setSynthPad clone for a genuine Wavetable engine (type 1) filtered through a Formant filter (type 5) with a female-leaning formant set, giving an unmistakable choir-organ voice no sibling shares. Its unique mod gesture is a slow free-run triangle LFO on morph position with an SCurve (0.45), plus a second sine LFO breathing the formant cutoff. Chorus (modulationType 3) + a Plate reverb (reverbType 0) make its wrapper distinct from the halls used by the pad siblings. Chord arp mode 9 with a constant Triad chord lane and Close voicing delivers the directive's consonant block-triad member.
- **Replacement code:**

```cpp
    // "Cathedral Triads" - Wavetable choir-organ, Chord-mode block triads (Plate)
    {
        PresetDef p;
        p.name = "Cathedral Triads";
        p.category = "Arp Chords";
        auto& s = p.state;
        // --- Voice: Wavetable core (OSC A) morphed against a soft PolyBLEP triangle
        //     body (OSC B) so the mixer position = a gentle choir 'aah' motion.
        s.oscA.type = 1;                 // Wavetable engine (never used elsewhere)
        s.oscA.waveform = 2;             // Square base table -> hollow organ core
        s.oscA.level = 0.85f;
        s.oscA.phaseMod = 0.22f;         // faint inharmonic shimmer = choral 'air'
        s.oscB.type = 0;                 // PolyBLEP body
        s.oscB.waveform = 4;             // Triangle -> smooth, pure fundament
        s.oscB.tuneSemitones = 12.0f;    // octave up = 8'+4' organ registration
        s.oscB.level = 0.5f;
        s.mixer.mode = 0;                // Crossfade keeps the tone CLEAN (not FFT-washy)
        s.mixer.position = 0.42f;        // favour the wavetable core
        // --- Filter: Formant (type 5) gives the choir its vowel colour
        s.filter.type = 5;               // Formant filter
        s.filter.cutoffHz = 2600.0f;
        s.filter.resonance = 0.3f;
        s.filter.formantMorph = 0.8f;    // sit between A and E vowels
        s.filter.formantGender = 0.15f;  // nudge toward a female/choir formant set
        // --- Amp env: organ-like, slow-ish swell with high sustain
        s.ampEnv.attackMs = 60.0f;
        s.ampEnv.decayMs = 400.0f;
        s.ampEnv.sustain = 0.85f;
        s.ampEnv.releaseMs = 900.0f;
        // --- Mod identity: slow free-run triangle LFO drifts the A/B morph (choir sway)
        s.lfo1.rateHz = 0.30f;           // very slow
        s.lfo1.shape = 1;                // Triangle
        s.lfo1.sync = 0;                 // free-running, unrelated to tempo
        setModSlot(s, 0, kSrcLFO1, kDstAllMorphPos, 0.45f, kCurveSCurve);
        // Second, slower LFO breathes the formant cutoff a touch
        s.lfo2.rateHz = 0.17f;
        s.lfo2.shape = 0;                // Sine
        s.lfo2.sync = 0;
        setModSlot(s, 1, kSrcLFO2, kDstAllFltCut, 0.18f, kCurveLinear);
        // Mod envelope adds a gentle per-note filter bloom
        s.modEnv.attackMs = 250.0f;
        s.modEnv.decayMs = 600.0f;
        s.modEnv.sustain = 0.6f;
        setVoiceRoute(s, 0, kVSrcEnv3, kVDstFltCut, 0.30f);
        // --- Chorus widens the choir
        s.modulationType = 3;            // Chorus
        s.chorus.rateHz = 0.4f;
        s.chorus.depth = 0.35f;
        s.chorus.voices = 3;
        s.chorus.mix = 0.4f;
        // --- FX: bright PLATE reverb (this preset's reverb identity)
        s.reverbEnabled = 1;
        s.reverbType = 0;                // Plate
        s.reverb.size = 0.62f;
        s.reverb.mix = 0.32f;
        s.reverb.damping = 0.35f;
        s.reverb.diffusion = 0.7f;
        // --- Arp: Chord mode fires all held notes together each step
        setArpEnabled(s, true);
        setArpMode(s, 9);                // Chord (all tones together) - literal 9
        setTempoSync(s, true);
        setArpRate(s, kNote1_8);
        setArpGateLength(s, 85.0f);
        s.arp.octaveRange = 1;           // chord mode: keep register tight
        s.arp.scaleType = 0;             // Major
        s.arp.rootNote = 0;              // C
        s.arp.scaleQuantizeInput = 1;    // snap held notes into C major
        setVoicingMode(s, 0);            // Close voicing = block triads
        // Chord lane: constant diatonic triads
        int32_t chords4[] = {kChordTriad, kChordTriad, kChordTriad, kChordTriad};
        setChordLane(s, 4, chords4);
        float vel4[] = {0.9f, 0.75f, 0.85f, 0.72f};
        setVelocityLane(s, 4, vel4);
        presets.push_back(std::move(p));
    }
```

## 2. "Minor 7th Pulse" -> "Minor 7th Pulse"

- **Locate:** the block containing `p.name = "Minor 7th Pulse"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp` (name unchanged).
- **Character:** A dark octave-down saw+square Ladder bass firing A-minor 7th stabs in first inversion at staccato 1/16, with stepped-random resonance jitter and a dotted-1/8 echo.
- **Coverage:** chord lane Seventh/Dyad; inversion lane First; scaleType NaturalMinor + rootNote; scaleQuantizeInput; arp mode AsPlayed(8); gate lane; delay (Digital dotted-1/8) differentiation; filter-env bug fix (+22 semitones); Random->Resonance Stepped mod identity; Ladder filter (real type 4).
- **Rationale:** Keeps the octave-down saw+square bass concept but corrects the waveform (square = 2) and, critically, fixes the filter-env bug: envAmount is now +22 semitones (audible sweep) instead of the garbage 4000. It uses a genuine Ladder (type 4) with drive, which the old setSynthBass never did. Its unique mod gesture is a tempo-synced Random source on resonance with a Stepped curve, so each stab squelches differently - something no sibling uses. Digital dotted-1/8 delay and no reverb keep it tight and distinct from the reverb-drenched pads; masterGain 0.85 compensates for drive + resonance.
- **Replacement code:**

```cpp
    // "Minor 7th Pulse" - Dark octave-down Ladder-bass 7th stabs, stepped-res jitter
    {
        PresetDef p;
        p.name = "Minor 7th Pulse";
        p.category = "Arp Chords";
        auto& s = p.state;
        // --- Voice: sub-octave saw (A) + square (B), classic reese-ish bass
        s.oscA.type = 0;                 // PolyBLEP
        s.oscA.waveform = 1;             // Saw
        s.oscA.tuneSemitones = -12.0f;   // one octave down
        s.oscA.level = 0.8f;
        s.oscB.type = 0;                 // PolyBLEP
        s.oscB.waveform = 2;             // Square (real square = 2, not 3)
        s.oscB.tuneSemitones = -12.0f;
        s.oscB.fineCents = 4.0f;         // slight beat for thickness
        s.oscB.level = 0.45f;
        s.mixer.position = 0.5f;
        // --- Filter: a REAL Moog ladder (type 4), 24 dB, driven for growl
        s.filter.type = 4;               // Ladder (audit: templates wrongly used SVF here)
        s.filter.ladderSlope = 4;        // 24 dB/oct
        s.filter.ladderDrive = 6.0f;     // dB of input drive -> harmonic grit
        s.filter.cutoffHz = 900.0f;      // low, so the env sweep is audible
        s.filter.resonance = 3.0f;
        s.filter.envAmount = 22.0f;      // BUG FIX: +22 SEMITONES (was 4000 Hz nonsense)
        // --- Envelopes: tight plucked bass
        s.ampEnv.attackMs = 2.0f;
        s.ampEnv.decayMs = 180.0f;
        s.ampEnv.sustain = 0.35f;
        s.ampEnv.releaseMs = 140.0f;
        s.filterEnv.attackMs = 1.0f;
        s.filterEnv.decayMs = 130.0f;    // fast decay = snappy sweep per stab
        s.filterEnv.sustain = 0.0f;
        s.filterEnv.releaseMs = 120.0f;
        s.filterEnv.decayCurve = 0.4f;   // exp-ish decay = punchier pluck
        s.global.masterGain = 0.85f;     // headroom for drive + resonance
        // --- Mod identity: synced S/H-style Random -> resonance in stepped jumps,
        //     so every stab has a slightly different squelch.
        s.random.sync = 1;
        s.random.noteValue = kNote1_8;
        s.random.smoothness = 0.0f;      // hard jumps
        setModSlot(s, 0, kSrcRandom, kDstAllResonance, 0.25f, kCurveStepped);
        // Velocity opens the ladder for dynamics
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.35f);
        // --- FX: Digital delay, dotted-1/8 (rhythmic depth), NO reverb (stays tight)
        s.delayEnabled = 1;
        s.delay.type = 0;                // Digital
        s.delay.sync = 1;
        s.delay.noteValue = kNote1_8D;
        s.delay.mix = 0.22f;
        s.delay.feedback = 0.32f;
        // --- Arp: as-played 1/16 staccato 7th chords
        setArpEnabled(s, true);
        setArpMode(s, kModeAsPlayed);
        setTempoSync(s, true);
        setArpRate(s, kNote1_16);
        setArpGateLength(s, 55.0f);
        s.arp.scaleType = 1;             // NaturalMinor
        s.arp.rootNote = 9;              // A
        s.arp.scaleQuantizeInput = 1;    // snap input into A minor
        // Chord lane: 7th chords on the beat, dyads on the off
        int32_t chords8[] = {
            kChord7th, kChordNone, kChordDyad, kChordNone,
            kChord7th, kChordNone, kChordDyad, kChordNone
        };
        setChordLane(s, 8, chords8);
        // Inversion lane: emphasise FIRST inversion for the dark stab voicing
        int32_t inv8[] = {
            kInv1st, kInvRoot, kInv1st, kInvRoot,
            kInv1st, kInvRoot, kInv1st, kInvRoot
        };
        setInversionLane(s, 8, inv8);
        // Staccato gate carving
        float gate8[] = {0.6f, 0.3f, 0.45f, 0.3f, 0.6f, 0.3f, 0.5f, 0.3f};
        setGateLane(s, 8, gate8);
        float vel8[] = {1.0f, 0.5f, 0.8f, 0.45f, 0.95f, 0.5f, 0.75f, 0.45f};
        setVelocityLane(s, 8, vel8);
        presets.push_back(std::move(p));
    }
```

## 3. "Chord Cascade" -> "Dorian Cascade"

- **Locate:** the block containing `p.name = "Chord Cascade"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp` (rename to `"Dorian Cascade"`).
- **Character:** An evolving additive D-Dorian pad whose polymetric chord (L5, 1.5x) and inversion (L7, 0.5x) lanes phase against each other under Converge motion, drifting through a hall and a spectral delay.
- **Coverage:** chord lane Triad/Dyad/Seventh; inversion lane Third; voicingMode Random; scaleType Dorian + rootNote; scaleQuantizeInput; arp mode Converge(4); chord/inversion lane speed (1.5/0.5); pitch lane; reverb (Hall) + Spectral delay differentiation; LFO->SpecTilt + smoothed Random->MorphPos mod identity; Additive engine.
- **Rationale:** Replaces the setSynthPad saw clone with a dual-Additive engine whose spectral tilt and inharmonicity are the whole point, then modulates additiveTilt via an LFO (SpecTilt dest, SCurve) and morph position via a heavily-smoothed Random - two independent slow drifts that are its unique identity. It is the only preset here using chord/inversion lane SPEED (1.5x vs 0.5x) so the L5 chord lane and L7 inversion lane phase out of sync under Converge motion for genuinely evolving harmony, with voicingMode Random adding octave scatter. Hall reverb + Spectral delay give it an FX signature distinct from the plate and digital-echo siblings; the inversion lane reaches Third (kInv3rd).
- **Replacement code:**

```cpp
    // "Dorian Cascade" - Additive polymetric harmony that phases out of sync
    {
        PresetDef p;
        p.name = "Dorian Cascade";
        p.category = "Arp Chords";
        auto& s = p.state;
        // --- Voice: two Additive oscillators, one octave apart, for a rich
        //     spectral pad whose tilt/inharmonicity we can modulate live.
        s.oscA.type = 4;                 // Additive engine
        s.oscA.additivePartials = 24;    // rich but not harsh
        s.oscA.additiveTilt = -3.0f;     // gently darker top
        s.oscA.additiveInharm = 0.08f;   // faint bell shimmer
        s.oscA.level = 0.8f;
        s.oscB.type = 4;                 // Additive shimmer octave
        s.oscB.additivePartials = 8;     // sparse = airy overtone layer
        s.oscB.additiveTilt = 2.0f;
        s.oscB.tuneSemitones = 12.0f;
        s.oscB.level = 0.4f;
        s.mixer.position = 0.45f;
        // --- Filter: SVF bandpass keeps the cascade mid-focused and clear
        s.filter.type = 2;               // SVF_BP
        s.filter.svfSlope = 1;           // 24 dB
        s.filter.cutoffHz = 1200.0f;
        s.filter.resonance = 1.2f;
        s.filter.envAmount = 18.0f;      // slow filter opening (semitones)
        // --- Envelopes: slow swelling pad
        s.ampEnv.attackMs = 300.0f;
        s.ampEnv.decayMs = 600.0f;
        s.ampEnv.sustain = 0.7f;
        s.ampEnv.releaseMs = 1200.0f;
        s.filterEnv.attackMs = 400.0f;
        s.filterEnv.decayMs = 800.0f;
        s.filterEnv.sustain = 0.5f;
        s.filterEnv.releaseMs = 1000.0f;
        // --- Mod identity: LFO on the additive spectral tilt + a HEAVILY smoothed
        //     random on morph position = two independent slow drifts.
        s.lfo1.rateHz = 0.22f;
        s.lfo1.shape = 1;                // Triangle
        s.lfo1.sync = 0;                 // free-run
        setModSlot(s, 0, kSrcLFO1, kDstAllSpecTilt, 0.4f, kCurveSCurve);
        s.random.sync = 0;
        s.random.rateHz = 0.4f;
        s.random.smoothness = 0.8f;      // slow, wandering (not stepped)
        setModSlot(s, 1, kSrcRandom, kDstAllMorphPos, 0.3f, kCurveSCurve);
        setVoiceRoute(s, 0, kVSrcEnv3, kVDstFltCut, 0.35f);
        // --- FX: HALL reverb + SPECTRAL delay (distinct from the plate/digital siblings)
        s.reverbEnabled = 1;
        s.reverbType = 1;                // Hall
        s.reverb.size = 0.85f;
        s.reverb.mix = 0.4f;
        s.reverb.damping = 0.3f;
        s.reverb.diffusion = 0.8f;
        s.delayEnabled = 1;
        s.delay.type = 4;                // Spectral delay
        s.delay.sync = 1;
        s.delay.noteValue = kNote1_8;
        s.delay.mix = 0.28f;
        s.delay.feedback = 0.45f;
        s.delay.spectralFFTSize = 2;     // 2048
        s.delay.spectralSpreadMs = 400.0f;
        s.delay.spectralDiffusion = 0.5f;
        s.delay.spectralTilt = 0.2f;
        // --- Arp: Converge motion, polymetric harmony lanes
        setArpEnabled(s, true);
        setArpMode(s, 4);                // Converge (outside-in) - literal 4
        setTempoSync(s, true);
        setArpRate(s, kNote1_16);
        setArpGateLength(s, 88.0f);
        s.arp.octaveRange = 2;
        s.arp.scaleType = 4;             // Dorian
        s.arp.rootNote = 2;              // D
        s.arp.scaleQuantizeInput = 1;
        setVoicingMode(s, 3);            // Random octave displacement adds to the cascade
        // Chord lane length 5, running at 1.5x speed
        int32_t chords5[] = {kChordTriad, kChordDyad, kChord7th, kChordTriad, kChordNone};
        setChordLane(s, 5, chords5);
        s.arp.chordLaneSpeed = 1.5f;     // harmony cycles faster than the clock
        // Inversion lane length 7, running at 0.5x speed -> phases against chords
        int32_t inv7[] = {kInvRoot, kInv1st, kInv2nd, kInvRoot, kInv1st, kInv2nd, kInv3rd};
        setInversionLane(s, 7, inv7);
        s.arp.inversionLaneSpeed = 0.5f; // inversions crawl -> long non-repeating harmony
        // Pitch lane length 3 for extra polymetry
        int32_t pitch3[] = {0, 7, -5};
        setPitchLane(s, 3, pitch3);
        float vel4[] = {0.9f, 0.6f, 0.8f, 0.55f};
        setVelocityLane(s, 4, vel4);
        presets.push_back(std::move(p));
    }
```

## 4. "Spread Ninths" -> "Lydian Aurora"

- **Locate:** the block containing `p.name = "Spread Ninths"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp` (rename to `"Lydian Aurora"`).
- **Character:** An airy F-Lydian formant-vocal pad playing wide spread 9th chords in second inversion, descending under Diverge motion, morphable by aftertouch into a saw bed, drenched in a huge modulated hall.
- **Coverage:** chord lane Ninth; inversion lane Second; voicingMode Spread; scaleType Lydian + rootNote; scaleQuantizeInput; arp mode Diverge(5); velocity swell; reverb (huge modulated Hall) differentiation; Aftertouch->MorphPos + Aftertouch->Res mod identity; Formant engine; stereo width/spread.
- **Rationale:** The only preset built on the Formant oscillator (type 7), morphed against a saw bed and filtered by an SVF High-Shelf (type 10) for the airy sheen - a completely different timbre from the additive/wavetable siblings. Its signature mod gesture is aftertouch routed to BOTH morph position and filter resonance, one of the most under-used sources in the bank, making it come alive under pressure. voicingMode Spread + all-9th chord lane + Second-inversion emphasis and Diverge(5) arp motion satisfy the wide-voiced directive; the huge modulated Hall (mod depth 0.15, size 0.92, diffusion 0.88) plus wide stereo width/spread is its distinct wrapper.
- **Replacement code:**

```cpp
    // "Lydian Aurora" - Formant-vocal spread 9th pad, aftertouch-morphed, huge hall
    {
        PresetDef p;
        p.name = "Lydian Aurora";
        p.category = "Arp Chords";
        auto& s = p.state;
        // --- Voice: Formant (vocal) oscillator A over an airy saw bed B; the mixer
        //     morphs vocal<->saw, and we hand that morph to AFTERTOUCH.
        s.oscA.type = 7;                 // Formant engine
        s.oscA.formantVowel = 0;         // A
        s.oscA.formantMorph = 1.5f;      // drift toward E/I -> brighter 'aah/eee'
        s.oscA.level = 0.8f;
        s.oscB.type = 0;                 // PolyBLEP saw bed
        s.oscB.waveform = 1;             // Saw
        s.oscB.fineCents = 7.0f;         // shimmer detune
        s.oscB.level = 0.4f;
        s.mixer.position = 0.5f;
        // --- Filter: SVF High-Shelf boosts the air above cutoff for the 'aurora' sheen
        s.filter.type = 10;              // SVF_HighShelf
        s.filter.cutoffHz = 3000.0f;
        s.filter.resonance = 0.4f;
        s.filter.svfSlope = 1;
        s.filter.svfGain = 6.0f;         // +6 dB of air
        // --- Amp env: long, slow, ambient swell
        s.ampEnv.attackMs = 500.0f;
        s.ampEnv.decayMs = 800.0f;
        s.ampEnv.sustain = 0.75f;
        s.ampEnv.releaseMs = 1500.0f;
        // --- Mod identity: expressive AFTERTOUCH - pressure morphs vocal->saw AND
        //     blooms resonance (an almost-unused source across the whole bank).
        setVoiceRoute(s, 0, kVSrcAftertouch, kVDstMorphPos, 0.5f);
        setVoiceRoute(s, 1, kVSrcAftertouch, kVDstFltRes, 0.3f);
        // A slow shimmer LFO on cutoff keeps it alive when no pressure is applied
        s.lfo1.rateHz = 0.15f;
        s.lfo1.shape = 0;                // Sine
        s.lfo1.sync = 0;
        setModSlot(s, 0, kSrcLFO1, kDstAllFltCut, 0.2f, kCurveLinear);
        // --- Wide stereo image for the 'aurora'
        s.global.width = 1.6f;           // plain 0-2 multiplier
        s.global.spread = 0.5f;          // voice pan spread
        // --- FX: enormous, diffuse, MODULATED hall (its unmistakable reverb identity)
        s.reverbEnabled = 1;
        s.reverbType = 1;                // Hall
        s.reverb.size = 0.92f;
        s.reverb.mix = 0.5f;
        s.reverb.damping = 0.25f;
        s.reverb.diffusion = 0.88f;
        s.reverb.preDelayMs = 25.0f;
        s.reverb.modRateHz = 0.3f;
        s.reverb.modDepth = 0.15f;       // lush chorused tail
        // --- Arp: slow descending Diverge 1/4, wide spread 9th chords
        setArpEnabled(s, true);
        setArpMode(s, 5);                // Diverge (inside-out) - literal 5
        setTempoSync(s, true);
        setArpRate(s, kNote1_4);
        setArpGateLength(s, 100.0f);
        s.arp.scaleType = 7;             // Lydian
        s.arp.rootNote = 5;              // F
        s.arp.scaleQuantizeInput = 1;
        setVoicingMode(s, 2);            // Spread -> alternate notes up an octave
        int32_t chords4[] = {kChord9th, kChord9th, kChord7th, kChord9th};
        setChordLane(s, 4, chords4);
        // Inversion lane emphasises SECOND inversion for the wide, rootless-ish top
        int32_t inv4[] = {kInv2nd, kInvRoot, kInv2nd, kInv1st};
        setInversionLane(s, 4, inv4);
        // Velocity swell into the pattern
        float vel4[] = {0.5f, 0.65f, 0.82f, 1.0f};
        setVelocityLane(s, 4, vel4);
        presets.push_back(std::move(p));
    }
```

## 5. "Stab Machine" -> "Stab Machine"

- **Locate:** the block containing `p.name = "Stab Machine"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp` (name unchanged).
- **Character:** A punchy hard-sync G-Mixolydian funk machine firing Drop2-voiced triad/7th stabs with 15% swing, ratchets and velocity-driven tape grit into a dotted-1/16 echo.
- **Coverage:** chord lane Triad/Dyad/Seventh; inversion lane First/Second; voicingMode Drop2; scaleType Mixolydian + rootNote; scaleQuantizeInput; arp mode AsPlayed(8); arp swing; ratchet lane (polymetric); velocity lane accents; spice; delay (Digital dotted-1/16) differentiation; filter-env bug fix (+32 semitones); distortion TapeSaturator; Velocity->DistDrive + Velocity->FltCut mod identity; Sync engine.
- **Rationale:** Swaps the setSynthBass saw+square clone for a hard-sync oscillator (type 3) over a sub square, filtered by a driven SVF LP whose env sweep is fixed to +32 semitones (the old bass presets wrote near-zero or 4000). Its unique mod gesture is a dual velocity route - to filter cutoff AND distortion drive - so accented hits get brighter and dirtier, the essence of a funk stab, backed by a Tape saturator (distortion type 5) that no sibling uses. Drop2 voicing (mode 1), 15% swing, a polymetric L8 ratchet lane against the L16 chord/inversion/velocity lanes, spice 0.2 for per-step variation, and a dotted-1/16 digital delay make it the syncopated member; masterGain 0.85 keeps drive + distortion in headroom.
- **Replacement code:**

```cpp
    // "Stab Machine" - Hard-sync funk stabs, Drop2, velocity-driven tape grit
    {
        PresetDef p;
        p.name = "Stab Machine";
        p.category = "Arp Chords";
        auto& s = p.state;
        // --- Voice: hard-sync oscillator (A) for the aggressive stab formant, plus
        //     a sub square (B) for weight underneath the funk.
        s.oscA.type = 3;                 // Sync engine
        s.oscA.syncRatio = 2.5f;         // sits in the classic sync sweet spot
        s.oscA.syncWaveform = 1;         // Saw slave -> bright, cutting
        s.oscA.syncMode = 0;             // Hard sync
        s.oscA.syncAmount = 1.0f;
        s.oscA.level = 0.85f;
        s.oscB.type = 0;                 // PolyBLEP sub
        s.oscB.waveform = 2;             // Square
        s.oscB.tuneSemitones = -12.0f;
        s.oscB.level = 0.4f;
        s.mixer.position = 0.4f;
        // --- Filter: SVF LP, 24 dB, driven, with a STRONG env pluck sweep
        s.filter.type = 0;               // SVF_LP
        s.filter.svfSlope = 1;           // 24 dB
        s.filter.svfDrive = 6.0f;        // dB post-filter saturation -> bite
        s.filter.cutoffHz = 700.0f;      // low so the pluck sweep is dramatic
        s.filter.resonance = 2.5f;
        s.filter.envAmount = 32.0f;      // BUG-FIX-CLASS value: +32 SEMITONES pluck
        // --- Envelopes: extremely tight stab
        s.ampEnv.attackMs = 1.0f;
        s.ampEnv.decayMs = 140.0f;
        s.ampEnv.sustain = 0.25f;
        s.ampEnv.releaseMs = 90.0f;
        s.filterEnv.attackMs = 1.0f;
        s.filterEnv.decayMs = 120.0f;
        s.filterEnv.sustain = 0.0f;
        s.filterEnv.releaseMs = 90.0f;
        s.filterEnv.decayCurve = 0.5f;   // snappy exponential pluck
        s.global.masterGain = 0.85f;     // headroom for drive + distortion
        // --- Distortion: Tape saturator gives the stabs their punch
        s.distortion.type = 5;           // TapeSaturator
        s.distortion.drive = 0.35f;
        s.distortion.character = 0.5f;
        s.distortion.mix = 0.5f;
        s.distortion.tapeSaturation = 0.5f;
        // --- Mod identity: velocity opens the filter AND pushes distortion drive,
        //     so hard hits are brighter AND dirtier - the funk 'accent' response.
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.4f);
        setVoiceRoute(s, 1, kVSrcVelocity, kVDstDistDrive, 0.5f);
        // --- FX: Digital delay, dotted-1/16 groove, widened
        s.delayEnabled = 1;
        s.delay.type = 0;                // Digital
        s.delay.sync = 1;
        s.delay.noteValue = kNote1_16D;
        s.delay.mix = 0.18f;
        s.delay.feedback = 0.28f;
        s.delay.digitalWidth = 130.0f;
        // --- Arp: funky as-played 1/16 with swing
        setArpEnabled(s, true);
        setArpMode(s, kModeAsPlayed);
        setTempoSync(s, true);
        setArpRate(s, kNote1_16);
        setArpGateLength(s, 50.0f);
        setArpSwing(s, 15.0f);
        s.arp.scaleType = 5;             // Mixolydian
        s.arp.rootNote = 7;              // G
        s.arp.scaleQuantizeInput = 1;
        setVoicingMode(s, 1);            // Drop2 (2nd-from-top note down an octave)
        s.arp.spice = 0.2f;              // per-step randomisation ~= 'one step random' feel
        // Chord lane: sparse Triad/7th stabs with rests
        int32_t chords16[] = {
            kChordTriad, kChordNone, kChordNone, kChord7th,
            kChordNone, kChordTriad, kChordNone, kChordNone,
            kChordDyad,  kChordNone, kChordNone, kChord7th,
            kChordNone, kChordTriad, kChordNone, kChordNone
        };
        setChordLane(s, 16, chords16);
        // Inversion lane: mix First/Second for tighter Drop2 voicings
        int32_t inv16[] = {
            kInvRoot, kInvRoot, kInvRoot, kInv1st,
            kInvRoot, kInv2nd, kInvRoot, kInvRoot,
            kInv1st, kInvRoot, kInvRoot, kInv2nd,
            kInvRoot, kInv1st, kInvRoot, kInvRoot
        };
        setInversionLane(s, 16, inv16);
        // Ratchet lane length 8 -> polymetric against the L16 chord lane
        int32_t ratch8[] = {1, 1, 2, 1, 1, 1, 3, 1};
        setRatchetLane(s, 8, ratch8);
        // Velocity accents drive the filter/dist routes
        float vel16[] = {
            1.0f, 0.4f, 0.3f, 0.85f,
            0.3f, 0.95f, 0.3f, 0.3f,
            0.75f, 0.3f, 0.3f, 0.9f,
            0.3f, 0.7f, 0.3f, 0.3f
        };
        setVelocityLane(s, 16, vel16);
        presets.push_back(std::move(p));
    }
```
