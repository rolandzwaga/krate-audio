# Ruinae Preset Plan — Arp Modulation

The Arp Modulation category exists to prove one idea the old bank never really committed to: the arpeggiator is not just a note player, it is a *modulation source*. Every preset here routes `ArpPitch` (and, where it matters, macros, chaos, rungler and LFOs) into the timbre, so the melodic contour of the pattern reshapes the sound as it plays — higher notes bite brighter, morph further, swing harder, or drench wetter. The six presets are deliberately spread across the operating modes (MIDI, Mod, MIDI+Mod), the arp clock (synced vs. free-running `freeRate`), the mixer engines (crossfade vs. SpectralMorph), the oscillator engines (PolyBLEP, Wavetable, Formant, Additive, Sync, Chaos, Phase-Distortion), the mod-matrix curve types (Linear, Exp, SCurve, Stepped) and the FX types (ping-pong / tape / granular / digital delay, plate / hall reverb, chorus) so that no two members share an FX or engine fingerprint and the group *collectively* exercises the whole arp-modulation surface. They range from the tight mono 303 acid runner, through a talking vowel-to-additive morph sequence and a pad that uses the arp as a pure internal modulator, to a six-slot chaos/rungler/wavefolder kitchen-sink lead and a self-rewriting self-modulator hub.

## 1. "Arp Filter Sweep" -> "Arp Filter Sweep"
- **Locate:** the block containing `p.name = "Arp Filter Sweep"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A mono 303-style acid runner whose 24 dB ladder cutoff literally tracks melodic pitch — every arp step re-opens the filter, higher notes bite brighter, dotted ping-pong tail, deliberately dry.
- **Coverage:** ArpPitch source -> filter cutoff; ArpPitch -> ArpRate (self-mod); operatingMode Mod + MIDI+Mod; mod-matrix Exp/SCurve curves.
- **Rationale:** Keeps the directive's saw+square subtractive core but promotes OSC B to the Wavetable engine (a category-mandated Wavetable home) so the group isn't two PolyBLEP oscs. The envAmount is a real +28 semitones (not the 4000-Hz acid bug), giving an actually audible per-note filter stab from cutoff 900 Hz. Slot 0 ArpPitch->cutoff (Exp 0.75) is the identity gesture; slot 1 self-mod ArpPitch->ArpRate (subtle 0.15) and operatingMode=3 cover two must-have features without destabilising timing. Aftertouch->resonance is a near-unused expression route. Dry ping-pong (no reverb) makes its FX signature distinct from the reverb-heavy siblings.
- **Replacement code:**

```cpp
    // "Arp Filter Sweep" - every arp step re-opens the ladder; a mono acid runner
    // whose cutoff literally TRACKS melodic pitch (higher note = brighter).
    {
        PresetDef p;
        p.name = "Arp Filter Sweep";
        p.category = "Arp Modulation";
        auto& s = p.state;
        // --- Voice: PolyBLEP saw over a WAVETABLE square (gives the category a
        //     Wavetable home; the thin square adds a hollow reed edge under the saw) ---
        s.oscA.type = 0; s.oscA.waveform = 1;          // PolyBLEP Saw
        s.oscA.level = 0.85f;
        s.oscB.type = 1; s.oscB.waveform = 2;          // Wavetable Square
        s.oscB.pulseWidth = 0.35f;                     // thin/nasal square for bite
        s.oscB.phaseMod = 0.2f;                        // tiny PM grit on the table
        s.oscB.fineCents = 7.0f; s.oscB.level = 0.55f; // detune beat vs OSC A
        s.mixer.position = 0.42f;                      // favour the saw
        // --- Ladder LP, 24 dB, driven: the classic acid growl ---
        s.filter.type = 4;                             // Ladder
        s.filter.cutoffHz = 900.0f;                    // low resting cutoff to sweep UP from
        s.filter.resonance = 0.5f;                     // squelch, not self-osc
        s.filter.ladderSlope = 4;                      // 24 dB/oct
        s.filter.ladderDrive = 4.0f;                   // growl (tamed by moderate res + soft limit)
        s.filter.keyTrack = 0.3f;                      // filter follows the keyboard a touch
        s.filter.envAmount = 28.0f;                    // +28 SEMITONES of filter-env stab (audible!)
        // Punchy pluck envelopes
        s.ampEnv.attackMs = 2.0f;  s.ampEnv.decayMs = 180.0f;
        s.ampEnv.sustain = 0.55f;  s.ampEnv.releaseMs = 140.0f;
        s.ampEnv.decayCurve = 0.4f;                    // exp-ish decay = extra punch
        s.filterEnv.attackMs = 1.0f; s.filterEnv.decayMs = 220.0f;
        s.filterEnv.sustain = 0.08f; s.filterEnv.releaseMs = 120.0f;
        // --- Arp: MIDI+Mod so it both PLAYS and feeds the mod system ---
        s.arp.operatingMode = 3;                       // MIDI+Mod (covers operatingMode=3)
        s.arp.mode = 0;                                // Up
        s.arp.octaveRange = 2;
        s.arp.tempoSync = 1; s.arp.noteValue = kNote1_16;
        s.arp.gateLength = 68.0f;
        int32_t pitch8[] = {0, 2, 4, 7, 12, 7, 4, 2};  // rising/falling contour drives the sweep
        setPitchLane(s, 8, pitch8);
        // Slide + accent bits on the peak steps for a 303 glide feel
        // (0x01 active, 0x09 active+accent, 0x0D active+slide+accent)
        int32_t modF[] = {0x01, 0x01, 0x09, 0x01, 0x0D, 0x01, 0x09, 0x01};
        setModifierLane(s, 8, modF, 45, 55.0f);
        // --- THE gesture: arp pitch -> ladder cutoff (Exp so highs really pop) ---
        setModSlot(s, 0, kSrcArpPitch, kDstAllFltCut, 0.75f, kCurveExp);
        // Self-mod: higher notes nudge the arp a hair faster (covers ArpPitch->ArpRate)
        setModSlot(s, 1, kSrcArpPitch, kDstArpRate, 0.15f, kCurveLinear);
        // Free LFO drizzles morph movement so sustained steps still breathe
        s.lfo1.rateHz = 0.22f; s.lfo1.shape = 1; s.lfo1.depth = 0.4f; s.lfo1.sync = 0;
        setModSlot(s, 2, kSrcLFO1, kDstAllMorphPos, 0.2f);
        // Expression: velocity opens the filter, aftertouch adds squelch resonance
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.4f);
        setVoiceRoute(s, 1, kVSrcAftertouch, kVDstFltRes, 0.35f);
        // --- FX: dotted ping-pong tail, deliberately NO reverb (stays dry/rhythmic) ---
        s.delayEnabled = 1;
        s.delay.type = 2;                              // PingPong
        s.delay.mix = 0.22f; s.delay.feedback = 0.33f;
        s.delay.sync = 1; s.delay.noteValue = kNote1_8D;
        s.delay.pingPongWidth = 150.0f;
        // Mono legato acid voice with a short glide
        s.global.voiceMode = 1;                        // Mono
        s.monoMode.legato = 1; s.monoMode.portamentoTimeMs = 20.0f;
        s.monoMode.portaMode = 1;                      // glide only on legato overlaps
        presets.push_back(std::move(p));
    }
```

## 2. "Arp Morph Sequence" -> "Arp Morph Sequence"
- **Locate:** the block containing `p.name = "Arp Morph Sequence"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A talking arp: melodic pitch crossfades a vowel formant into a bright inharmonic additive spectrum via the Spectral-Morph mixer while tilting brightness, lilting with swing in a lush hall — and emitting its notes as MIDI.
- **Coverage:** ArpPitch -> MorphPos; ArpPitch -> SpecTilt; SpectralMorph mixer with arp; midiOut; mod-matrix Exp/SCurve curves.
- **Rationale:** The formant+additive Spectral-Morph combination is the group's only vocal engine, so it needs no gimmicks to stand apart. formantMorph=1.2 and additiveInharm=0.08 push both oscillators off their defaults so the crossfade sweeps between a real 'eee' and a shimmering bell stack. Slot 0 ArpPitch->MorphPos uses SCurve (smooth glide) and slot 1 ArpPitch->SpecTilt uses Exp — two distinct curves on the same source. midiOut=1 (a spoken sequence doubling as a MIDI generator) covers that dormant feature. Hall reverb with pre-delay and NO delay is a deliberately different FX fingerprint from the delay-forward siblings.
- **Replacement code:**

```cpp
    // "Arp Morph Sequence" - a TALKING arp: pitch crossfades a vowel formant into a
    // bright additive spectrum (Spectral-Morph mixer) and tilts brightness with the melody.
    {
        PresetDef p;
        p.name = "Arp Morph Sequence";
        p.category = "Arp Modulation";
        auto& s = p.state;
        // OSC A vowel formant, OSC B rich additive - blended in the FFT morph domain
        s.oscA.type = 7;                               // Formant
        s.oscA.formantVowel = 1;                       // 'E' base vowel
        s.oscA.formantMorph = 1.2f;                    // sit between E and I for a nasal 'eee'
        s.oscA.level = 0.8f;
        s.oscB.type = 4;                               // Additive
        s.oscB.additivePartials = 28; s.oscB.additiveTilt = 3.0f; // bright partial stack
        s.oscB.additiveInharm = 0.08f;                 // faint bell shimmer on top
        s.oscB.level = 0.6f;
        s.mixer.mode = 1;                              // SpectralMorph (FFT interp A<->B) - covers the feature
        s.mixer.position = 0.3f;                       // start near the vowel
        s.mixer.tilt = 2.0f;                           // morph-domain brightness lift
        // Open, gentle filter - the timbre motion lives in the MORPH, not the filter
        s.filter.type = 0; s.filter.cutoffHz = 6000.0f; s.filter.resonance = 0.18f;
        s.ampEnv.attackMs = 8.0f;  s.ampEnv.decayMs = 320.0f;
        s.ampEnv.sustain = 0.7f;   s.ampEnv.releaseMs = 260.0f;
        s.ampEnv.attackCurve = 0.3f;                   // soft vocal onset
        // Arp: UpDown with swing for a lilting spoken phrase
        s.arp.operatingMode = 1;                       // MIDI
        s.arp.mode = 2;                                // UpDown
        s.arp.octaveRange = 2;
        s.arp.tempoSync = 1; s.arp.noteValue = kNote1_16;
        s.arp.gateLength = 82.0f; s.arp.swing = 18.0f;
        s.arp.midiOut = 1;                             // emit arp notes as MIDI (covers midiOut)
        int32_t pitchM[] = {0, 3, 7, 10, 12, 10, 7, 3}; // minor-ish rise/fall
        setPitchLane(s, 8, pitchM);
        // pitch -> morph position (SCurve: smooth vowel<->additive glide)
        setModSlot(s, 0, kSrcArpPitch, kDstAllMorphPos, 0.6f, kCurveSCurve);
        // pitch -> spectral tilt (Exp: higher notes noticeably brighter)
        setModSlot(s, 1, kSrcArpPitch, kDstAllSpecTilt, 0.4f, kCurveExp);
        // Slow FREE LFO breathes the cutoff underneath so held tails move
        s.lfo2.rateHz = 0.11f; s.lfo2.shape = 0; s.lfo2.depth = 0.5f; s.lfo2.sync = 0;
        setModSlot(s, 2, kSrcLFO2, kDstAllFltCut, 0.28f);
        // Expression: velocity pushes the morph, keytrack pushes tilt (formant 'gender' feel)
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstMorphPos, 0.3f);
        setVoiceRoute(s, 1, kVSrcKeyTrack, kVDstSpecTilt, 0.25f);
        // FX: lush HALL only - vocal pad space, no delay clutter to muddy the words
        s.reverbEnabled = 1;
        s.reverbType = 1;                              // Hall
        s.reverb.size = 0.72f; s.reverb.mix = 0.3f; s.reverb.damping = 0.3f;
        s.reverb.preDelayMs = 25.0f;
        presets.push_back(std::move(p));
    }
```

## 3. "Arp Tilt Cascade" -> "Arp Tilt Cascade"
- **Locate:** the block containing `p.name = "Arp Tilt Cascade"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** The arp turns into a pure MODULATOR: held notes ring as a dark 56-partial additive pad while a free-running internal arp cascades spectral tilt (dark to brilliant) and reverb send from its stepping pitch — a blooming, drifting wash with no note gating.
- **Coverage:** ArpPitch -> SpecTilt; ArpPitch -> EffectMix; operatingMode Mod + MIDI+Mod; freeRate (non-synced arp); mod-matrix Exp/SCurve curves.
- **Rationale:** This is the boldest reinterpretation: instead of a gated arp, operatingMode=2 (Mod-only) runs the arp purely as an internal modulator over a sustained pad, which is exactly why the amp env is pad-shaped (120 ms attack, 0.8 sustain, 900 ms release) — held notes ring so the preset is never silent, and the free-running internal arp (tempoSync=0, freeRate=6.5 Hz) cascades tilt and reverb send. That single design choice covers operatingMode=2 AND freeRate while producing a genuinely unique sound. mode=5 (Diverge) is a valid literal that replaces the old mislabeled 'Converge=4' comment. slots[1].scale=3 (x2) exercises the never-used mod-matrix scale axis, letting ArpPitch->EffectMix reach an effective 0.8 depth. Tape delay + Plate reverb is a distinct FX pairing.
- **Replacement code:**

```cpp
    // "Arp Tilt Cascade" - the arp becomes a pure MODULATOR: notes are held as a
    // dark additive PAD while a FREE-RUNNING internal arp cascades spectral tilt and
    // reverb send from its stepping pitch. No note gating - just blooming motion.
    {
        PresetDef p;
        p.name = "Arp Tilt Cascade";
        p.category = "Arp Modulation";
        auto& s = p.state;
        s.oscA.type = 4;                               // Additive
        s.oscA.additivePartials = 56; s.oscA.additiveTilt = -3.0f; // dark, dense base
        s.oscA.level = 0.7f;
        s.oscB.type = 0; s.oscB.waveform = 1;          // PolyBLEP Saw body underneath
        s.oscB.level = 0.45f; s.oscB.fineCents = 5.0f;
        s.mixer.position = 0.4f;
        s.filter.type = 0; s.filter.cutoffHz = 9000.0f; s.filter.resonance = 0.12f;
        // PAD-shaped amp env so held notes ring as a sustained wash (audible in Mod-only mode)
        s.ampEnv.attackMs = 120.0f; s.ampEnv.decayMs = 500.0f;
        s.ampEnv.sustain = 0.8f;    s.ampEnv.releaseMs = 900.0f;
        s.ampEnv.attackCurve = 0.4f;
        // Arp in MOD-ONLY mode, FREE-RUNNING (not tempo-synced)
        s.arp.operatingMode = 2;                       // Mod (covers operatingMode=2)
        s.arp.mode = 5;                                // Diverge / inside-out (a real mode value)
        s.arp.octaveRange = 3;
        s.arp.tempoSync = 0;                           // free-run
        s.arp.freeRate = 6.5f;                         // 6.5 Hz cascade (covers freeRate)
        s.arp.gateLength = 60.0f;
        int32_t pitchC[] = {0, 5, -3, 7, -7, 12, 0, -12}; // wide leaps = big tilt swings
        setPitchLane(s, 8, pitchC);
        float velC[] = {1.0f, 0.6f, 0.85f, 0.5f, 1.0f, 0.7f, 0.9f, 0.45f};
        setVelocityLane(s, 8, velC);
        // pitch -> spectral tilt (Exp: dark->brilliant bloom)
        setModSlot(s, 0, kSrcArpPitch, kDstAllSpecTilt, 0.65f, kCurveExp);
        // pitch -> effect mix (SCurve), scaled x2 so top notes really drench in reverb
        setModSlot(s, 1, kSrcArpPitch, kDstEffectMix, 0.4f, kCurveSCurve);
        s.modMatrix.slots[1].scale = 3;                // x2 depth - uses the dormant scale axis
        // Slow free LFO wanders the cutoff so the pad never sits still
        s.lfo1.rateHz = 0.13f; s.lfo1.shape = 1; s.lfo1.depth = 0.5f; s.lfo1.sync = 0;
        setModSlot(s, 2, kSrcLFO1, kDstAllFltCut, 0.25f);
        // Mod-env blooms the filter on each held note
        setVoiceRoute(s, 0, kVSrcEnv3, kVDstFltCut, 0.35f);
        s.modEnv.attackMs = 40.0f; s.modEnv.decayMs = 600.0f;
        s.modEnv.sustain = 0.15f;  s.modEnv.releaseMs = 500.0f;
        // FX: Tape delay + PLATE reverb = spacious, slightly wobbly bloom
        s.delayEnabled = 1;
        s.delay.type = 1;                              // Tape
        s.delay.mix = 0.18f; s.delay.feedback = 0.28f;
        s.delay.sync = 1; s.delay.noteValue = kNote1_8;
        s.delay.tapeSaturation = 0.4f; s.delay.tapeAge = 0.2f;
        s.reverbEnabled = 1;
        s.reverbType = 0;                              // Plate
        s.reverb.size = 0.7f; s.reverb.mix = 0.3f; s.reverb.damping = 0.35f;
        presets.push_back(std::move(p));
    }
```

## 4. "Arp Chaos Matrix" -> "Arp Chaos Matrix"
- **Locate:** the block containing `p.name = "Arp Chaos Matrix"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** The antidote to same-ish: hard-sync plus Lorenz chaos morphed in the FFT domain and folded, driven by SIX mod slots (arp pitch, chaos LFO, synced LFO, rungler, and TWO arp self-mod routes) with swung ratchet rolls and a granular delay — gnarly, unpredictable, ever-shifting mono lead.
- **Coverage:** ArpPitch source -> filter cutoff; ArpPitch -> ArpGateLen (self-mod); ArpPitch -> ArpSwing (self-mod); SpectralMorph mixer with arp; rungler + chaosMod + wavefolder in an arp; mod-matrix Exp/SCurve curves.
- **Rationale:** The kitchen-sink member deliberately owns the exotic sources the whole bank neglects: chaosMod (depth raised to 0.6 or it's silent), rungler (depth 0.45, 5-bit for crunch), and a Wavefolder distortion. It expands to SIX mod slots including TWO arp self-mod routes — ArpPitch->ArpGateLen (0.25) and the newly-added ArpPitch->ArpSwing (0.2, SCurve) — so the arp continuously rewrites its own groove. Four distinct curves appear across the matrix (Exp, Linear, Stepped, SCurve). envAmount is a real +30 semitones. Swapping the old ping-pong for a Granular delay (with pitch spray + texture) diversifies delay type across the category per the suite philosophy, and the Sync+Chaos pair via SpectralMorph is unique in the group.
- **Replacement code:**

```cpp
    // "Arp Chaos Matrix" - the antidote to same-ish: hard-sync + Lorenz chaos through a
    // wavefolder, driven by SIX mod slots (arp pitch, chaos, LFO, rungler + TWO arp
    // SELF-mod routes) with swung ratchets. Gnarly, unpredictable, ever-shifting mono lead.
    {
        PresetDef p;
        p.name = "Arp Chaos Matrix";
        p.category = "Arp Modulation";
        auto& s = p.state;
        s.oscA.type = 3;                               // Sync
        s.oscA.syncRatio = 3.0f; s.oscA.syncWaveform = 1; // saw slave
        s.oscA.syncMode = 0; s.oscA.syncAmount = 0.85f;
        s.oscA.level = 0.8f;
        s.oscB.type = 5;                               // Chaos
        s.oscB.chaosAttractor = 0;                     // Lorenz
        s.oscB.chaosAmount = 0.45f; s.oscB.chaosCoupling = 0.2f;
        s.oscB.chaosOutput = 1;                        // Y axis
        s.oscB.level = 0.4f;
        s.mixer.mode = 1;                              // SpectralMorph (chaos<->sync FFT blend)
        s.mixer.position = 0.4f;
        s.filter.type = 4; s.filter.cutoffHz = 1800.0f; s.filter.resonance = 0.42f;
        s.filter.ladderSlope = 4; s.filter.envAmount = 30.0f; // +30 SEMITONES stab (audible!)
        s.ampEnv.attackMs = 3.0f; s.ampEnv.decayMs = 300.0f;
        s.ampEnv.sustain = 0.6f;  s.ampEnv.releaseMs = 200.0f;
        s.filterEnv.attackMs = 1.0f; s.filterEnv.decayMs = 240.0f;
        s.filterEnv.sustain = 0.15f; s.filterEnv.releaseMs = 150.0f;
        // Arp: drunk walk with ratchet rolls, spice and humanize
        s.arp.operatingMode = 1;
        s.arp.mode = 7;                                // Walk (drunk / +-1 random walk)
        s.arp.octaveRange = 2;
        s.arp.tempoSync = 1; s.arp.noteValue = kNote1_16;
        s.arp.gateLength = 65.0f; s.arp.swing = 20.0f;
        s.arp.spice = 0.4f; s.arp.humanize = 0.25f;
        int32_t pitchX[] = {0, 3, -2, 5, 7, -4, 12, -7};
        setPitchLane(s, 8, pitchX);
        int32_t ratchX[] = {1, 1, 2, 1, 1, 3, 1, 2};  // rolls for drum-like bursts
        setRatchetLane(s, 8, ratchX);
        s.arp.ratchetSwing = 62.0f;                    // swung sub-steps inside the rolls
        // ---- SIX mod slots ----
        // 0: arp pitch -> cutoff (Exp)
        setModSlot(s, 0, kSrcArpPitch, kDstAllFltCut, 0.5f, kCurveExp);
        // 1: chaos LFO -> morph position (raise chaosMod.depth first!)
        s.chaosMod.rateHz = 2.2f; s.chaosMod.type = 0; s.chaosMod.depth = 0.6f;
        setModSlot(s, 1, kSrcChaos, kDstAllMorphPos, 0.45f);
        // 2: synced saw LFO -> spectral tilt
        s.lfo1.rateHz = 0.3f; s.lfo1.shape = 2; s.lfo1.depth = 0.7f; s.lfo1.sync = 1;
        s.lfo1Ext.noteValue = kNote1_4;
        setModSlot(s, 2, kSrcLFO1, kDstAllSpecTilt, 0.4f);
        // 3: rungler -> resonance (Stepped = quantized crunch; raise rungler.depth first!)
        s.rungler.osc1FreqHz = 3.0f; s.rungler.osc2FreqHz = 5.5f;
        s.rungler.depth = 0.45f; s.rungler.bits = 5;
        setModSlot(s, 3, kSrcRungler, kDstAllResonance, 0.3f, kCurveStepped);
        // 4: SELF-MOD arp pitch -> arp gate length (covers ArpPitch->ArpGateLen)
        setModSlot(s, 4, kSrcArpPitch, kDstArpGateLen, 0.25f);
        // 5: SELF-MOD arp pitch -> arp swing (covers ArpPitch->ArpSwing)
        setModSlot(s, 5, kSrcArpPitch, kDstArpSwing, 0.2f, kCurveSCurve);
        // Voice routes: velocity drives the fold, mod-env sweeps the morph
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstDistDrive, 0.4f);
        setVoiceRoute(s, 1, kVSrcEnv3, kVDstMorphPos, 0.3f);
        s.modEnv.attackMs = 2.0f; s.modEnv.decayMs = 400.0f;
        s.modEnv.sustain = 0.0f;  s.modEnv.releaseMs = 200.0f;
        // Wavefolder = the dirty character owned by THIS preset
        s.distortion.type = 4;                         // Wavefolder
        s.distortion.drive = 0.35f; s.distortion.foldType = 1; s.distortion.mix = 0.8f;
        // Mono glide + GRANULAR delay (a delay TYPE none of the ping-pong siblings use)
        s.global.voiceMode = 1;
        s.monoMode.legato = 1; s.monoMode.portamentoTimeMs = 15.0f;
        s.delayEnabled = 1;
        s.delay.type = 3;                              // Granular
        s.delay.mix = 0.22f; s.delay.feedback = 0.3f;
        s.delay.sync = 1; s.delay.noteValue = kNote1_8;
        s.delay.granularSizeMs = 120.0f; s.delay.granularDensity = 14.0f;
        s.delay.granularPitchSpray = 0.15f; s.delay.granularTexture = 0.3f;
        presets.push_back(std::move(p));
    }
```

## 5. "Arp FX Depth" -> "Arp FX Depth"
- **Locate:** the block containing `p.name = "Arp FX Depth"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A bouncy euclidean saw/triangle player where arp pitch pushes the wet send and the spice while a single MACRO hand-sweeps the spectral morph — one-knob performance arp drenched in chorus, digital delay and hall.
- **Coverage:** ArpPitch -> EffectMix; ArpPitch -> ArpSpice; macro source in arp; SpectralMorph mixer with arp; mod-matrix Exp/SCurve curves.
- **Rationale:** Directly fixes the audit's dead-route concern: the old preset routed Macro1->MorphPos over a standard (crossfade) mixer where morph does nothing, so this version sets mixer.mode=1 (SpectralMorph), making the macro an actually audible one-knob timbre sweep — and the macro is parked at 0.35 so it's live the instant the preset loads. OSC B is promoted to the Wavetable engine (triangle) for engine diversity. ArpPitch->EffectMix (SCurve) and ArpPitch->ArpSpice cover the FX-depth features. The FX signature is intentionally the wettest of the group and the only one using the Chorus modulation slot plus a modulated Digital delay, so even though its oscillators are the most conventional, its motion and space make it distinct.
- **Replacement code:**

```cpp
    // "Arp FX Depth" - bouncy euclidean saw/triangle where arp pitch pushes the wet send
    // AND the spice, plus a MACRO hand-sweeps the spectral morph. A one-knob performance
    // arp drenched in chorus + delay + hall.
    {
        PresetDef p;
        p.name = "Arp FX Depth";
        p.category = "Arp Modulation";
        auto& s = p.state;
        s.oscA.type = 0; s.oscA.waveform = 1; s.oscA.level = 0.7f; // PolyBLEP Saw
        s.oscB.type = 1; s.oscB.waveform = 4;          // Wavetable Triangle
        s.oscB.tuneSemitones = 12.0f;                  // octave-up sparkle
        s.oscB.phaseMod = 0.15f;                       // subtle table motion
        s.oscB.level = 0.4f;
        s.mixer.mode = 1;                              // SpectralMorph so Macro->Morph is AUDIBLE
        s.mixer.position = 0.35f;
        s.filter.type = 4; s.filter.cutoffHz = 4200.0f; s.filter.resonance = 0.28f;
        s.filter.ladderSlope = 4;
        s.ampEnv.attackMs = 12.0f; s.ampEnv.decayMs = 460.0f;
        s.ampEnv.sustain = 0.7f;   s.ampEnv.releaseMs = 380.0f;
        // Arp: as-played, euclidean 5/8 with heavy swing
        s.arp.operatingMode = 1;
        s.arp.mode = 8;                                // AsPlayed
        s.arp.octaveRange = 1;
        s.arp.tempoSync = 1; s.arp.noteValue = kNote1_8;
        s.arp.gateLength = 88.0f; s.arp.swing = 30.0f;
        setEuclidean(s, true, 5, 8, 1);                // E(5,8) rot 1 - cinquillo bounce
        int32_t pitchF[] = {0, 4, 7, 12, 0, -3, 5, 10};
        setPitchLane(s, 8, pitchF);
        float gateF[] = {1.0f, 0.5f, 1.5f, 0.3f, 1.0f, 0.7f, 1.2f, 0.4f}; // staccato/legato mix
        setGateLane(s, 8, gateF);
        // pitch -> effect mix (SCurve): higher notes = wetter
        setModSlot(s, 0, kSrcArpPitch, kDstEffectMix, 0.5f, kCurveSCurve);
        // pitch -> arp spice (covers ArpPitch->ArpSpice): highs get more variation
        setModSlot(s, 1, kSrcArpPitch, kDstArpSpice, 0.35f);
        // MACRO 1 = one-knob performance morph over the spectral mixer (covers macro-in-arp)
        s.macros.values[0] = 0.35f;                    // parked mid so morph is live from load
        setModSlot(s, 2, kSrcMacro1, kDstAllMorphPos, 0.7f, kCurveLinear);
        // Free LFO for gentle cutoff drift
        s.lfo1.rateHz = 0.28f; s.lfo1.shape = 0; s.lfo1.depth = 0.5f; s.lfo1.sync = 0;
        setModSlot(s, 3, kSrcLFO1, kDstAllFltCut, 0.3f);
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.45f);
        setVoiceRoute(s, 1, kVSrcKeyTrack, kVDstSpecTilt, 0.25f);
        // FX chain: CHORUS + Digital delay + HALL = deep wet bed
        s.modulationType = 3;                          // Chorus (an under-used effect)
        s.chorus.rateHz = 0.4f; s.chorus.depth = 0.5f; s.chorus.mix = 0.35f;
        s.chorus.voices = 3; s.chorus.stereoSpread = 180.0f;
        s.delayEnabled = 1;
        s.delay.type = 0;                              // Digital (distinct delay type)
        s.delay.mix = 0.26f; s.delay.feedback = 0.35f;
        s.delay.sync = 1; s.delay.noteValue = kNote1_8D;
        s.delay.digitalModDepth = 0.2f; s.delay.digitalWidth = 130.0f;
        s.reverbEnabled = 1;
        s.reverbType = 1;                              // Hall
        s.reverb.size = 0.62f; s.reverb.mix = 0.3f;
        presets.push_back(std::move(p));
    }
```

## 6. "Arp Self Modulator" -> "Arp Self Modulator"
- **Locate:** the block containing `p.name = "Arp Self Modulator"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A brittle, self-animating diatonic up-down arpeggio in free-running mono-legato where the arp pitch drives its own rate, swing, spice, brightness and wet level — high notes play faster, brighter and wetter, so the sequence never settles into a loop.
- **Coverage:** phase-distortion-engine; operatingMode Mod + MIDI+Mod; freeRate (non-synced arp); ArpPitch source -> filter cutoff; ArpPitch -> ArpRate (self-mod); ArpPitch -> ArpGateLen (self-mod); ArpPitch -> ArpSwing (self-mod); ArpPitch -> ArpSpice; ArpPitch -> EffectMix; ArpPitch -> MorphPos; arp-modifier-lane; arp-pitch-lane; lfo1; mod-matrix Exp/SCurve curves; voice-routes; mono-legato-portamento; filter-envelope; delay-fx.
- **Rationale:** Preserves the audit's standout core (PD ResSaw + square sub, ladder LP, pluck ADSR, mono legato, synced delay) but executes the directive's specific asks that the old block missed. operatingMode=3 (MIDI+Mod) plus tempoSync=0 with freeRate=8.0 makes this the one preset that owns both the Mod/MIDI+Mod operating mode and the free-running arp clock. The ArpPitch self-modulation is expanded from 3 to 6 destinations covering the category's must-cover ArpPitch->{ArpRate, ArpGateLen, ArpSwing, cutoff, ArpSpice, EffectMix}; the negative gate-length amount (-0.25) is deliberate so high notes shorten toward staccato while low notes stay legato, a musically opposite motion to the rate/swing pushes that keeps the pattern from feeling one-dimensional. Curve variety is real: Exp on rate and EffectMix (top of range rips/floods), SCurve on cutoff (vocal sweep), Linear on swing. envAmount corrected to a sane +26 semitones for an audible per-note pluck sweep. freeRate=8Hz is musically ~1/16 at 120bpm, so the baseline is sane and ArpPitch only accelerates from there. All destinations/sources/curves verified against the k* constants in the generator (kSrcArpPitch=14, kDstArpRate=10, kDstArpGateLen=11, kDstArpSwing=13, kDstAllFltCut=4, kDstArpSpice=14, kDstEffectMix=3, kDstAllMorphPos=5, kCurveExp/SCurve/Linear=1/2/0) and every arp field (operatingMode/tempoSync/freeRate/gateLength/octaveRange) against ArpState in ruinae_preset_format.h.
- **Replacement code:**

```cpp
    // "Arp Self Modulator" - the arp's own pitch is its only modulator:
    //   higher notes -> faster, swung-er, spicier, brighter, wetter.
    //   This preset OWNS operatingMode=MIDI+Mod and the free-running (non-synced)
    //   arp clock, so it is the self-animating hub of the Arp Modulation set.
    {
        PresetDef p;
        p.name = "Arp Self Modulator";
        p.category = "Arp Modulation";
        auto& s = p.state;
        // OSC A: Phase-Distortion ResSaw = a resonant CZ-buzz that sweeps with note pitch
        s.oscA.type = 2;          // Phase Distortion
        s.oscA.pdWaveform = 5;    // ResSaw (formant-y resonant sweep)
        s.oscA.pdDistortion = 0.5f; // half DCW = strong but not screaming
        s.oscA.level = 0.8f;
        // OSC B: square sub an octave down for weight under the buzz
        s.oscB.type = 0; s.oscB.waveform = 2; // PolyBLEP Square
        s.oscB.tuneSemitones = -12.0f; s.oscB.level = 0.4f;
        s.mixer.position = 0.3f;  // favour the ResSaw, keep sub as a bed
        // Ladder LP with envelope movement; cutoff is ALSO an ArpPitch target below
        s.filter.type = 4;        // Ladder
        s.filter.cutoffHz = 2600.0f; s.filter.resonance = 0.4f;
        s.filter.ladderSlope = 4; s.filter.ladderDrive = 2.0f;
        s.filter.envAmount = 26.0f; // +26 st = an audible per-note pluck sweep (was a sane 20)
        // Amp: pluck-ish so each step has a transient the self-mod can articulate
        s.ampEnv.attackMs = 2.0f; s.ampEnv.decayMs = 250.0f;
        s.ampEnv.sustain = 0.5f; s.ampEnv.releaseMs = 150.0f;
        s.filterEnv.attackMs = 1.0f; s.filterEnv.decayMs = 200.0f;
        s.filterEnv.sustain = 0.1f; s.filterEnv.releaseMs = 100.0f;
        // --- ARP: free-running MIDI+Mod, self-modulated ---
        s.arp.operatingMode = 3;  // MIDI+Mod: plays the pattern AND emits mod
        s.arp.mode = 2;           // UpDown
        s.arp.octaveRange = 2;
        s.arp.tempoSync = 0;      // FREE-RUN: this preset owns freeRate
        s.arp.freeRate = 8.0f;    // ~1/16 @120bpm baseline; ArpPitch pushes it faster
        s.arp.gateLength = 70.0f;
        int32_t pitchS[] = {0, 2, 4, 5, 7, 9, 11, 12}; // diatonic climb over an octave
        setPitchLane(s, 8, pitchS);
        // Modifier lane: accents on the downbeats, slides into the octave leaps
        int32_t modS[] = {
            kStepActive | kStepAccent, kStepActive, kStepActive,
            kStepActive | kStepSlide, kStepActive | kStepAccent,
            kStepActive, kStepActive, kStepActive | kStepSlide
        };
        setModifierLane(s, 8, modS, 35, 80.0f);
        // --- The self-modulation matrix: ArpPitch -> everything ---
        // higher notes = faster clock (exp so the top of the range really rips)
        setModSlot(s, 0, kSrcArpPitch, kDstArpRate,    0.30f, kCurveExp);
        // higher notes = shorter gate (staccato at the top, legato at the bottom)
        setModSlot(s, 1, kSrcArpPitch, kDstArpGateLen, -0.25f, kCurveLinear);
        // higher notes = more swing/groove push
        setModSlot(s, 2, kSrcArpPitch, kDstArpSwing,   0.25f);
        // higher notes = brighter (smoothstep so the sweep feels vocal, not linear)
        setModSlot(s, 3, kSrcArpPitch, kDstAllFltCut,  0.40f, kCurveSCurve);
        // higher notes = more probabilistic variation (spice)
        setModSlot(s, 4, kSrcArpPitch, kDstArpSpice,   0.30f);
        // higher notes = wetter tail (pitch pushes the delay/reverb send up)
        setModSlot(s, 5, kSrcArpPitch, kDstEffectMix,  0.20f, kCurveExp);
        // Slow free LFO drifts the PD morph so the buzz breathes between notes
        s.lfo1.rateHz = 0.2f; s.lfo1.shape = 1; s.lfo1.depth = 0.5f; s.lfo1.sync = 0;
        setModSlot(s, 6, kSrcLFO1, kDstAllMorphPos, 0.30f);
        // Performance voice routes: velocity opens cutoff, pressure adds resonance
        setVoiceRoute(s, 0, kVSrcVelocity,   kVDstFltCut, 0.5f);
        setVoiceRoute(s, 1, kVSrcAftertouch, kVDstFltRes, 0.3f);
        // Mono legato with a short glide so the slide steps actually portamento
        s.global.voiceMode = 1;
        s.monoMode.legato = 1; s.monoMode.portamentoTimeMs = 25.0f;
        // Tempo-synced dotted-8th delay tail (delay stays synced even though arp free-runs)
        s.delayEnabled = 1;
        s.delay.mix = 0.22f; s.delay.feedback = 0.32f;
        s.delay.sync = 1; s.delay.noteValue = kNote1_8;
        presets.push_back(std::move(p));
    }
```
