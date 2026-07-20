# Ruinae Preset Plan — Arp Acid

The two Arp Acid presets are rebuilt as a deliberate analog/digital pair so the category spans two genuinely different synthesis paths instead of two step patterns over the same template. "Acid Line 303" becomes the analog pole: one raw saw through a driven 24 dB ladder, mono with legato glide, dry. "Acid Stab" becomes the digital pole: a Casio-CZ phase-distortion ResSaw through a driven SVF, kept alive by a hard-stepped sample-and-hold and Elektron trig-ratios, with a wet ping-pong tail. Both fix the `setSynthAcid` `filter.envAmount = 4000` bug (envAmount is plain semitones, -48..+48), which is the single biggest reason the old acid presets sounded lifeless — the filter envelope was never producing a musical sweep. Neither preset uses the shared template helper; both are built from scratch so they carry none of its bugs, and their mod identities are intentionally disjoint (ArpPitch->cutoff vs stepped S&H->cutoff) so they never sound like the same patch.

## 1. "Acid Line 303" -> "Acid Line 303"
- **Locate:** the block containing `p.name = "Acid Line 303"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** The definitive analog TB-303: a single saw squelching through a driven 24 dB ladder with a fast, deep filter-envelope sweep, played mono with legato glide so slides bend between notes.
- **Coverage:** filterEnv with corrected envAmount (semitones); ladder filter + ladderDrive; modifier lane slide+accent (accentVelocity, slideTime); pitch lane; ArpPitch->filter cutoff mod slot; velocity->filterRes voice route; aftertouch->filterRes voice route.
- **Rationale:** Rebuilt from scratch (no setSynthAcid) so it carries none of the template's bugs. The signature fix is `filter.envAmount = 32.0f` (plain semitones) replacing the nonsensical 4000; combined with a fast filterEnv (decay 140ms, sustain 0.05) this makes every note a real 'wow' sweep. `type=4` selects the actual Ladder (the template mislabels SVF type 0 as 'Ladder'), `ladderSlope=4` gives the 24 dB/oct fatness and `ladderDrive=6` adds transistor grit. Mono+legato+`portaMode=1` means the slide-flagged steps (3,7) glide, the defining 303 gesture. ArpPitch->cutoff (exp) makes higher riff notes brighter; velocity+aftertouch->resonance make it playable. This is the ANALOG pole of the pair.
- **Replacement code:**

```cpp
    // T027: "Acid Line 303" - analog ladder acid (single saw, mono glide)
    {
        PresetDef p;
        p.name = "Acid Line 303";
        p.category = "Arp Acid";
        auto& s = p.state;

        // --- Voice: one raw saw, Osc B silent (classic 303 topology) ---
        s.oscA.type = 0;            // PolyBLEP
        s.oscA.waveform = 1;       // Sawtooth
        s.oscA.level = 0.9f;
        s.oscB.level = 0.0f;       // single-oscillator acid
        s.mixer.position = 0.0f;   // Osc A only

        // --- Ladder LP: the analog half of the pair (NOT SVF) ---
        s.filter.type = 4;         // Ladder LP (type 4, not the template's SVF)
        s.filter.cutoffHz = 600.0f;   // low base so the env-sweep is the timbre
        s.filter.resonance = 0.75f;   // squelchy but pre-self-osc
        s.filter.ladderSlope = 4;     // 24 dB/oct - fat 303 slope
        s.filter.ladderDrive = 6.0f;  // transistor grit that thickens the saw
        s.filter.keyTrack = 0.3f;     // higher notes sit a touch brighter
        // FIX the setSynthAcid bug: envAmount is PLAIN SEMITONES (-48..+48),
        // not Hz. +32 st = a strong, musically-scaled sweep (was 4000 = garbage).
        s.filter.envAmount = 32.0f;

        // --- Amp env: plucky, some sustain so slides ring between steps ---
        s.ampEnv.attackMs = 1.0f;
        s.ampEnv.decayMs = 180.0f;
        s.ampEnv.sustain = 0.4f;
        s.ampEnv.releaseMs = 90.0f;

        // --- Filter env: fast snap = the 'wow' on every note ---
        s.filterEnv.attackMs = 1.0f;
        s.filterEnv.decayMs = 140.0f;
        s.filterEnv.sustain = 0.05f;
        s.filterEnv.releaseMs = 80.0f;
        s.filterEnv.decayCurve = 0.5f; // exp-ish snap, not linear

        // --- Mono + legato glide: slides in the modifier lane actually bend ---
        s.global.voiceMode = 1;    // Mono
        s.global.polyphony = 1;
        s.monoMode.legato = 1;
        s.monoMode.portamentoTimeMs = 60.0f;
        s.monoMode.portaMode = 1;  // glide only on legato/slide steps

        // --- Arp: 1/16 up-run with a moving pitch line ---
        setArpEnabled(s, true);
        setArpMode(s, kModeUp);
        setTempoSync(s, true);
        setArpRate(s, kNote1_16);
        setArpGateLength(s, 60.0f);
        float vel8[] = {0.75f, 0.75f, 0.75f, 0.75f,
                        0.75f, 0.75f, 0.75f, 0.75f};
        setVelocityLane(s, 8, vel8);
        int32_t pitch8[] = {0, 0, 3, 0, 5, 0, 3, 7}; // riff that opens on the highs
        setPitchLane(s, 8, pitch8);
        // Slides on 3 & 7 (glide), accent on 5, slide+accent on 7
        int32_t mod8[] = {
            kStepActive,
            kStepActive,
            kStepActive | kStepSlide,               // glide up to the 3rd
            kStepActive,
            kStepActive | kStepAccent,              // punch the 5th
            kStepActive,
            kStepActive | kStepSlide | kStepAccent, // slide+accent into the octave
            kStepActive
        };
        setModifierLane(s, 8, mod8, 100, 60.0f);   // accentVel=100, slideTime=60ms

        // --- Mod identity: arp pitch opens the ladder (higher note = brighter) ---
        setModSlot(s, 0, kSrcArpPitch, kDstAllFltCut, 0.5f, kCurveExp);
        // Playable squelch: velocity AND aftertouch push resonance
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltRes, 0.4f);
        setVoiceRoute(s, 1, kVSrcAftertouch, kVDstFltRes, 0.3f);
        presets.push_back(std::move(p));
    }
```

## 2. "Acid Stab" -> "Digital Acid Stab"
- **Locate:** the block containing `p.name = "Acid Stab"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A hard, metallic Casio-CZ acid stab: a resonant phase-distortion ResSaw through a driven SVF low-pass, static in pitch but kept alive by a stepped sample-and-hold filter stutter, Elektron trig-ratios that thin the pattern, and a wet ping-pong tail.
- **Coverage:** filterEnv with corrected envAmount (semitones); PhaseDistortion engine (ResSaw); modifier lane slide+accent (accentVelocity, slideTime); condition lane (probabilities/ratios); arp swing.
- **Rationale:** Moves the sibling to a completely different synthesis path: `oscA.type=2` (PhaseDistortion) with `pdWaveform=5` (ResSaw) and `pdDistortion=0.7` gives the hard, hollow Casio-CZ bite, and it runs through an SVF LP (type 0) with `svfDrive=4` rather than the ladder - so the pair genuinely spans analog vs digital, not just two step patterns. `filter.envAmount=24.0f` fixes the 4000 bug on this preset too. Because the pitch line is flat, motion comes from (a) a tempo-synced hard-stepped Sample&Hold on cutoff (kCurveStepped) - a unique mod gesture the analog sibling never uses, satisfying the no-repeat rule - and (b) Elektron trig-ratios (Ratio_1_2=6, Ratio_3_4=13) that thin the pattern over loops. 10% arp swing and a wet ping-pong tail complete a distinct, lively identity while the analog Acid Line stays dry and mono. Resonance 3.0 + drive 4 with a fully-decaying amp env stays punchy and safe, not an ear-destroyer.
- **Replacement code:**

```cpp
    // T028: "Digital Acid Stab" - phase-distortion acid, the digital counterpart
    {
        PresetDef p;
        p.name = "Digital Acid Stab";
        p.category = "Arp Acid";
        auto& s = p.state;

        // --- Voice: Casio-CZ phase distortion (the DIGITAL half of the pair) ---
        s.oscA.type = 2;           // PhaseDistortion engine
        s.oscA.pdWaveform = 5;     // ResSaw - resonant formant character
        s.oscA.pdDistortion = 0.7f;// strong DCW = metallic, hollow bite
        s.oscA.level = 0.9f;
        s.oscB.level = 0.0f;       // single osc
        s.mixer.position = 0.0f;   // Osc A only

        // --- SVF LP (distinct from Acid Line's ladder) with post-filter drive ---
        s.filter.type = 0;         // SVF LP
        s.filter.cutoffHz = 900.0f;
        s.filter.resonance = 3.0f; // SVF Q: resonant edge without whistling
        s.filter.svfSlope = 1;     // 24 dB cascaded
        s.filter.svfDrive = 4.0f;  // grit that hardens the stab
        // FIX the 4000 bug here too: +24 semitones of real sweep on the SVF.
        s.filter.envAmount = 24.0f;

        // --- Amp env: tight percussive chop (fully decays each step) ---
        s.ampEnv.attackMs = 1.0f;
        s.ampEnv.decayMs = 90.0f;
        s.ampEnv.sustain = 0.0f;
        s.ampEnv.releaseMs = 50.0f;

        // --- Filter env: sharp attack transient on each stab ---
        s.filterEnv.attackMs = 1.0f;
        s.filterEnv.decayMs = 110.0f;
        s.filterEnv.sustain = 0.0f;
        s.filterEnv.releaseMs = 60.0f;
        s.filterEnv.decayCurve = 0.4f;

        // --- Arp: static single-pitch 1/16 stab, tight gate, 10% swing ---
        setArpEnabled(s, true);
        setArpMode(s, kModeAsPlayed);
        setTempoSync(s, true);
        setArpRate(s, kNote1_16);
        setArpGateLength(s, 40.0f);
        setArpSwing(s, 10.0f);     // subtle groove push (mustCover: arp swing)
        float vel8[] = {0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f};
        setVelocityLane(s, 8, vel8);
        int32_t pitch8[] = {0, 0, 0, 0, 0, 0, 0, 0}; // no melody - identity is timbre
        setPitchLane(s, 8, pitch8);
        // Every step accented (hard chop); slideTime unused but set sanely
        int32_t mod8[] = {
            kStepActive | kStepAccent, kStepActive | kStepAccent,
            kStepActive | kStepAccent, kStepActive | kStepAccent,
            kStepActive | kStepAccent, kStepActive | kStepAccent,
            kStepActive | kStepAccent, kStepActive | kStepAccent
        };
        setModifierLane(s, 8, mod8, 110, 40.0f);
        // Elektron trig-conditions carve the rhythm instead of a pitch line:
        // Ratio_1_2 (6) = fire on 1st of every 2 loops; Ratio_3_4 (13) = 3rd of 4.
        int32_t cond8[] = {kCondAlways, 6, kCondAlways, 13,
                           kCondAlways, 6, kCondAlways, 13};
        setConditionLane(s, 8, cond8, 0);

        // --- Mod identity (must NOT repeat Acid Line's ArpPitch->cutoff): ---
        // tempo-synced S&H steps the filter for a digital, glitchy stutter.
        s.sampleHold.sync = 1;         // lock steps to tempo
        s.sampleHold.noteValue = kNote1_8;
        s.sampleHold.slewMs = 0.0f;    // hard steps = digital character
        setModSlot(s, 0, kSrcSampleHold, kDstAllFltCut, 0.3f, kCurveStepped);
        // Velocity opens the SVF for accent dynamics
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.35f);

        // --- FX signature: wet synced ping-pong (analog sibling stays dry) ---
        s.delayEnabled = 1;
        s.delay.type = 2;          // PingPong
        s.delay.sync = 1;
        s.delay.noteValue = kNote1_8;
        s.delay.feedback = 0.35f;
        s.delay.mix = 0.3f;
        s.delay.pingPongCrossFeed = 0.8f;
        s.delay.pingPongWidth = 140.0f;
        presets.push_back(std::move(p));
    }
```
