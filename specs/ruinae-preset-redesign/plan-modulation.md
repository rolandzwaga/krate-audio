# Ruinae Preset Plan — Modulation

The Modulation category is the coverage backstop for the whole redesign: these three presets exist to prove that every modulation subsystem the synth exposes actually reaches audio. Between them they must exercise all eight mod-matrix slots, both LFOs and their extended parameters, every exotic modulation source (chaos, rungler, S&H, random, env-follower, transient, pitch-follower), all four matrix curve types plus the scale and bypass axes, the full voice-route grid (velocity/keytrack/aftertouch/env3/VoiceLFO/Gate), the macro system as true one-knob morphs, Bezier envelope segments, and the euclidean trance gate. Each patch is built around a different modulation philosophy so they read as distinct instruments rather than three copies of the same template: "Modulation Maze" is an autonomous never-repeating wash, "Velocity Canvas" puts everything under the fingers via velocity/aftertouch, and "Macro Performer" is a hand-swept macro instrument. The common failure the audit flagged — a dead filter envelope whose `envAmount` was a bug value — is fixed here by expressing `envAmount` in real semitones (+24/+32) so the sweep is audible.

## 1. "Modulation Maze" -> "Modulation Maze"

- **Locate:** the block containing `p.name = "Modulation Maze"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp` (search for the `p.name` string, not a line number — line numbers drift as edits land).
- **Character:** A frozen-spectrum-into-granular-swarm wash where all eight mod slots, both drifting LFOs, chaos, rungler, S&H and a euclidean trance gate keep every dimension in constant, never-repeating motion.
- **Coverage:** spectral-freeze-engine; particle-engine; spectral-morph-mixer; mod-matrix (all 8 slots); mod-matrix scale axis (x0.25 + x0.5 + x2); mod-matrix curves (SCurve/Linear/Exp/Stepped); mod-matrix smoothMs; LFO1 smooth-random; LFO2 sine; both-LFOs-full (fadeIn/symmetry/quantizeSteps/phaseOffset/unipolar/retrigger); chaos-mod source; random source; sample-hold source; rungler source; env-follower source; transient source; voice-routes (velocity/keytrack/aftertouch/env3/VoiceLFO/Gate); GateDepth dest; trance-gate (euclidean); mod-envelope routed; Bezier attack curve (amp env); filter-env-fix (semitones +24); global-filter; reverb-freeze; Hall reverb; spectral-delay; stereo width/spread.
- **Rationale:** Keeps the excellent original engine pairing (SpectralFreeze x Particle in SpectralMorph) but goes from 6 to all 8 mod slots and adds the missing directive items: a Bezier amp-attack (soft exp onset), envFollower + transient sources, a euclidean trance gate that unlocks the Gate/VoiceLFO voice-route sources and the GateDepth dest, and the scale axis (slot1 x2, slot3 x0.5) plus all four curve types. `envAmount` is now +24 semitones so the filter env is actually audible (was previously the class-wide dead-sweep problem). Rossler chaos (type 1), rungler bits=6 and S&H slew give three independent stepped/chaotic layers; the two LFOs at 0.08/0.12 Hz with quadrature phaseOffset and quantizeSteps beat against each other so nothing repeats. Hall + frozen reverb + spectral delay is a distinct wrapper from the siblings' plates.
- **Replacement code:**

```cpp
    // "Modulation Maze" - the everything-moves coverage backstop
    {
        PresetDef p;
        p.name = "Modulation Maze";
        p.category = "Modulation";
        auto& s = p.state;

        // --- Engines: frozen spectrum (A) spectrally morphed into a granular swarm (B) ---
        s.oscA.type = 8;                  // Spectral Freeze
        s.oscA.spectralTilt = 1.0f;       // bright, airy top
        s.oscA.spectralPitch = 0.0f;      // locked to key
        s.oscA.spectralFormant = -2.0f;   // formants down -> hollow, vocal-ish body
        s.oscA.level = 0.7f;
        s.oscB.type = 6;                  // Particle
        s.oscB.particleScatter = 5.0f;    // wide detuned cloud
        s.oscB.particleDensity = 20.0f;
        s.oscB.particleLifetime = 400.0f; // long grains -> smooth, not fizzy
        s.oscB.particleSpawnMode = 1;     // Random -> non-repeating texture
        s.oscB.particleEnvType = 3;       // Blackman grain window (smooth, low click)
        s.oscB.particleDrift = 0.3f;      // slow pitch wander
        s.oscB.level = 0.5f;
        s.mixer.mode = 1;                 // Spectral Morph (FFT interpolation A<->B)
        s.mixer.position = 0.5f;
        s.mixer.tilt = -3.0f;             // gently darken the morphed spectrum

        // Ladder LP; filter env in SEMITONES (+24 = ~2-octave sweep, not the old bug value)
        s.filter.type = 4;                // Ladder
        s.filter.cutoffHz = 2200.0f;
        s.filter.resonance = 0.35f;
        s.filter.ladderSlope = 4;         // 24 dB/oct
        s.filter.envAmount = 24.0f;       // audible upward sweep
        s.filterEnv.attackMs = 400.0f; s.filterEnv.decayMs = 3000.0f;
        s.filterEnv.sustain = 0.4f; s.filterEnv.releaseMs = 2000.0f;

        // Amp env: long swell + tail, with a BEZIER attack for a soft exponential onset
        s.ampEnv.attackMs = 300.0f; s.ampEnv.decayMs = 800.0f;
        s.ampEnv.sustain = 0.75f; s.ampEnv.releaseMs = 1800.0f;
        s.ampEnv.bezierEnabled = 1.0f;    // custom attack shape
        s.ampEnv.bezierAttackCp1X = 0.25f; s.ampEnv.bezierAttackCp1Y = 0.04f; // slow start...
        s.ampEnv.bezierAttackCp2X = 0.55f; s.ampEnv.bezierAttackCp2Y = 0.35f; // ...then bloom

        // --- Two free-running LFOs at close rates => slow phasing/beating ---
        s.lfo1.rateHz = 0.08f; s.lfo1.shape = 5;   // Smooth Random
        s.lfo1.depth = 0.8f; s.lfo1.sync = 0;
        s.lfo1Ext.fadeInMs = 2000.0f;    // modulation fades in over 2 s
        s.lfo1Ext.symmetry = 0.35f;      // skewed random contour
        s.lfo1Ext.quantizeSteps = 6;     // stair-stepped random (extra LFO axis)
        s.lfo1Ext.retrigger = 0;         // free-run (don't reset per note)
        s.lfo2.rateHz = 0.12f; s.lfo2.shape = 0;   // Sine
        s.lfo2.depth = 0.6f; s.lfo2.sync = 0;
        s.lfo2Ext.fadeInMs = 3000.0f;
        s.lfo2Ext.phaseOffset = 90.0f;   // quadrature vs LFO1 -> drifting motion
        s.lfo2Ext.unipolar = 1;          // push cutoff only upward
        s.lfo2Ext.retrigger = 0;

        // --- ALL 8 mod-matrix slots: every exotic source drives something ---
        // LFO1 -> morph position (smoothstep so the morph glides)
        setModSlot(s, 0, kSrcLFO1, kDstAllMorphPos, 0.6f, kCurveSCurve, 40.0f);
        // LFO2 -> all-voice cutoff, scaled x2 for a big slow filter breath
        setModSlot(s, 1, kSrcLFO2, kDstAllFltCut, 0.5f, kCurveLinear, 30.0f);
        s.modMatrix.slots[1].scale = 3;   // x2 depth (scale axis)
        // Chaos attractor -> spectral tilt (exp curve emphasises the peaks)
        s.chaosMod.rateHz = 0.5f; s.chaosMod.type = 1; s.chaosMod.depth = 0.5f; // Rossler
        setModSlot(s, 2, kSrcChaos, kDstAllSpecTilt, 0.35f, kCurveExp, 100.0f);
        // Random -> resonance (subtle x0.5 wobble)
        s.random.rateHz = 1.5f; s.random.smoothness = 0.6f;
        setModSlot(s, 3, kSrcRandom, kDstAllResonance, 0.3f, kCurveLinear, 50.0f);
        s.modMatrix.slots[3].scale = 1;   // x0.5 depth
        // Sample & Hold -> effect mix, STEPPED curve for abrupt space changes
        s.sampleHold.rateHz = 0.5f; s.sampleHold.slewMs = 120.0f;
        setModSlot(s, 4, kSrcSampleHold, kDstEffectMix, 0.3f, kCurveStepped);
        // Rungler (Benjolin) -> global filter cutoff (chaotic stepped shimmer)
        s.rungler.osc1FreqHz = 2.0f; s.rungler.osc2FreqHz = 3.5f;
        s.rungler.depth = 0.3f; s.rungler.bits = 6;
        setModSlot(s, 5, kSrcRungler, kDstGlobalFltCut, 0.25f, kCurveLinear, 20.0f);
        // Envelope follower -> filter-env amount (dynamics reshape the sweep)
        s.envFollower.sensitivity = 0.6f; s.envFollower.attackMs = 15.0f; s.envFollower.releaseMs = 200.0f;
        setModSlot(s, 6, kSrcEnvFollower, kDstAllFltEnvAmt, 0.3f, kCurveExp);
        // Transient detector -> master volume (tiny accent on note onsets)
        s.transient.sensitivity = 0.5f; s.transient.attackMs = 2.0f; s.transient.decayMs = 60.0f;
        setModSlot(s, 7, kSrcTransient, kDstMasterVol, 0.12f, kCurveLinear);
        s.modMatrix.slots[7].scale = 0;   // x0.25 - keeps the transient->volume accent very fine (scale axis)

        // --- Trance gate: gentle euclidean pulse woven into the wash ---
        s.tranceGate.enabled = 1;
        s.tranceGate.numSteps = 16;
        s.tranceGate.depth = 0.5f;              // 50% dips - pulses without chopping
        s.tranceGate.tempoSync = 1; s.tranceGate.noteValue = kNote1_8;
        s.tranceGate.euclideanEnabled = 1;
        s.tranceGate.euclideanHits = 5;         // E(5,16) sparse groove
        s.tranceGate.euclideanRotation = 0;

        // --- Voice routes: performance + the gate & voice-LFO sources ---
        setVoiceRoute(s, 0, kVSrcVelocity,   kVDstFltCut,    0.4f);
        setVoiceRoute(s, 1, kVSrcKeyTrack,   kVDstMorphPos,  0.3f);
        setVoiceRoute(s, 2, kVSrcEnv3,       kVDstSpecTilt,  0.35f);  // modEnv routed
        setVoiceRoute(s, 3, kVSrcAftertouch, kVDstFltRes,    0.4f);
        setVoiceRoute(s, 4, kVSrcVoiceLFO,   kVDstMorphPos,  0.25f);  // per-voice LFO wobble
        setVoiceRoute(s, 5, kVSrcGate,       kVDstFltCut,    0.5f);   // gate chops cutoff rhythmically
        setVoiceRoute(s, 6, kVSrcEnv3,       kVDstGateDepth, 0.3f);   // modEnv shapes gate depth
        setVoiceRoute(s, 7, kVSrcKeyTrack,   kVDstSpecTilt,  0.2f);
        s.modEnv.attackMs = 300.0f; s.modEnv.decayMs = 2000.0f;
        s.modEnv.sustain = 0.2f; s.modEnv.releaseMs = 1000.0f;

        // Global filter as a final character/safety LP (rungler + LFO target it)
        s.globalFilter.enabled = 1; s.globalFilter.type = 0;   // LP
        s.globalFilter.cutoffHz = 8000.0f; s.globalFilter.resonance = 1.2f;

        // Wrapper: big frozen HALL + spectral delay = infinite evolving space.
        // freeze=1 holds an evolving bed; damping 0.6 + mix 0.3 + soft-limit keep level bounded.
        s.reverbEnabled = 1; s.reverbType = 1;   // Hall
        s.reverb.size = 0.85f; s.reverb.mix = 0.3f;
        s.reverb.damping = 0.6f;
        s.reverb.diffusion = 0.85f;
        s.reverb.freeze = 1;
        s.delayEnabled = 1;
        s.delay.type = 4;                        // Spectral
        s.delay.mix = 0.2f; s.delay.feedback = 0.3f;
        s.delay.spectralDiffusion = 0.6f; s.delay.spectralTilt = 0.3f;
        s.delay.spectralSpreadMs = 400.0f;

        s.global.width = 1.6f; s.global.spread = 0.35f;
        s.global.polyphony = 12;                 // lush overlap
        presets.push_back(std::move(p));
    }
```

## 2. "Velocity Canvas" -> "Velocity Canvas"

- **Locate:** the block containing `p.name = "Velocity Canvas"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp` (search for the `p.name` string, not a line number — line numbers drift as edits land).
- **Character:** A saw-plus-additive playable body that lives entirely under the fingers: eight velocity/aftertouch/keytrack voice routes with per-route exp/S-curve/linear shaping and x2/x0.5 depth scaling turn dynamics into brightness, additive morph, wavefolder grit and pitch bloom.
- **Coverage:** virtual-analog-osc; additive-engine; crossfade-mixer; voice-routes (8 slots, velocity-heavy); per-route CURVE (exp/scurve/linear); per-route SCALE axis (x2 + x0.5); per-route smoothMs; aftertouch source; keytrack source; OscBPitch dest; SpecTilt dest; DistDrive dest; filter-env Bezier (decay); filter-env-fix (semitones +32); mod-envelope routed; ladder-drive; wavefolder-distortion; pitch-follower source; mod-matrix bypass axis; settings velocityCurve=Hard; settings voiceStealMode/voiceAllocMode; Plate reverb; Tape delay; stereo spread.
- **Rationale:** The audit flagged that the original collapsed to a plain dark saw on a static render because its only autonomous motion was one tiny LFO. Fix: give it two always-on mod slots (pitchFollower->cutoff so higher pitches self-brighten, LFO1->morph drift) plus a third slot deliberately bypassed to exercise the bypass axis without adding motion. The eight voice routes now set curve/scale/smoothMs directly (setVoiceRoute only writes source/dest/amount/active), which is the whole point of this preset: velocity->cutoff is exp (soft stays dark), velocity->morph is S-curve at x2 depth, aftertouch->resonance is S-curve with 30 ms smoothing, and env3->OscBPitch is a scaled x0.5 exp bloom. `envAmount`=+32 semitones fixes the dead filter-env; `ladderDrive`=4 dB and the velocity-gated wavefolder give the hard-hit grit. Settings (Hard velocity curve, Soft steal, HighNote alloc) and the Plate+Tape wrapper make it distinct from both siblings.
- **Replacement code:**

```cpp
    // "Velocity Canvas" - expression owns everything; per-route curve/scale/smooth showcase
    {
        PresetDef p;
        p.name = "Velocity Canvas";
        p.category = "Modulation";
        auto& s = p.state;

        // Saw (A) + Additive (B, one octave up) - a bright/dark playable hybrid body
        s.oscA.type = 0; s.oscA.waveform = 1; s.oscA.level = 0.8f;   // PolyBLEP saw
        s.oscB.type = 4;                          // Additive
        s.oscB.additivePartials = 32;             // rich harmonics for hard hits to reveal
        s.oscB.additiveTilt = -2.0f;              // mellow at rest; velocity morphs it in
        s.oscB.additiveInharm = 0.08f;            // faint metallic sheen
        s.oscB.tuneSemitones = 12.0f;             // octave up
        s.oscB.level = 0.5f;
        s.mixer.mode = 0;                         // Crossfade (leave SpectralMorph to siblings)
        s.mixer.position = 0.35f;                 // mostly saw at rest; velocity pushes to additive

        // Ladder LP with gentle drive; filter env in SEMITONES (+32 velocity-scaled sweep)
        s.filter.type = 4; s.filter.cutoffHz = 1600.0f; s.filter.resonance = 0.3f;
        s.filter.ladderSlope = 4;
        s.filter.ladderDrive = 4.0f;              // saturated ladder character
        s.filter.envAmount = 32.0f;

        s.ampEnv.attackMs = 4.0f; s.ampEnv.decayMs = 500.0f;
        s.ampEnv.sustain = 0.6f; s.ampEnv.releaseMs = 400.0f;

        // Filter env with a BEZIER decay -> percussive open then a long-tailed close
        s.filterEnv.attackMs = 1.0f; s.filterEnv.decayMs = 380.0f;
        s.filterEnv.sustain = 0.12f; s.filterEnv.releaseMs = 260.0f;
        s.filterEnv.bezierEnabled = 1.0f;
        s.filterEnv.bezierDecayCp1X = 0.15f; s.filterEnv.bezierDecayCp1Y = 0.75f; // fast initial fall
        s.filterEnv.bezierDecayCp2X = 0.45f; s.filterEnv.bezierDecayCp2Y = 0.25f; // then long tail

        // Mod env (fast pluck) for the OscB pitch-bloom route
        s.modEnv.attackMs = 3.0f; s.modEnv.decayMs = 300.0f;
        s.modEnv.sustain = 0.0f; s.modEnv.releaseMs = 200.0f;

        // --- 8 velocity-heavy voice routes exercising per-route CURVE + SCALE + smoothMs ---
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut,    0.7f);   // vel -> cutoff
        s.voiceRoutes[0].curve = static_cast<int8_t>(kCurveExp);    // exp: soft hits stay dark
        s.voiceRoutes[0].smoothMs = 8.0f;
        setVoiceRoute(s, 1, kVSrcVelocity, kVDstMorphPos,  0.4f);   // vel -> morph toward additive
        s.voiceRoutes[1].curve = static_cast<int8_t>(kCurveSCurve);
        s.voiceRoutes[1].scale = static_cast<int8_t>(3);           // x2 depth (scale axis)
        setVoiceRoute(s, 2, kVSrcVelocity, kVDstDistDrive, 0.3f);   // vel -> wavefolder drive
        s.voiceRoutes[2].curve = static_cast<int8_t>(kCurveLinear);
        setVoiceRoute(s, 3, kVSrcKeyTrack, kVDstFltCut,    0.5f);   // higher notes brighter
        s.voiceRoutes[3].curve = static_cast<int8_t>(kCurveLinear);
        s.voiceRoutes[3].smoothMs = 20.0f;
        setVoiceRoute(s, 4, kVSrcKeyTrack, kVDstSpecTilt,  0.3f);
        s.voiceRoutes[4].curve = static_cast<int8_t>(kCurveExp);
        setVoiceRoute(s, 5, kVSrcAftertouch, kVDstFltRes,  0.5f);   // pressure adds resonance
        s.voiceRoutes[5].curve = static_cast<int8_t>(kCurveSCurve);
        s.voiceRoutes[5].smoothMs = 30.0f;
        setVoiceRoute(s, 6, kVSrcAftertouch, kVDstMorphPos,0.3f);   // pressure adds additive
        setVoiceRoute(s, 7, kVSrcEnv3,     kVDstOscBPitch, 0.2f);   // mod-env pitch bloom on B
        s.voiceRoutes[7].curve = static_cast<int8_t>(kCurveExp);
        s.voiceRoutes[7].scale = static_cast<int8_t>(1);           // x0.5 - subtle bloom
        s.voiceRoutes[7].smoothMs = 5.0f;

        // Wavefolder that only bites on hard velocity (via route 2)
        s.distortion.type = 4;                    // Wavefolder
        s.distortion.drive = 0.15f; s.distortion.foldType = 0;    // Triangle fold
        s.distortion.mix = 0.6f;

        // Mod matrix: pitch-follower brightness + LFO drift + a BYPASSED alternate route
        s.pitchFollower.minHz = 80.0f; s.pitchFollower.maxHz = 2000.0f;
        s.pitchFollower.confidence = 0.5f; s.pitchFollower.speedMs = 40.0f;
        setModSlot(s, 0, kSrcPitchFollow, kDstAllFltCut, 0.25f, kCurveLinear, 25.0f);
        s.lfo1.rateHz = 0.1f; s.lfo1.shape = 5; s.lfo1.depth = 0.3f; s.lfo1.sync = 0;
        setModSlot(s, 1, kSrcLFO1, kDstAllMorphPos, 0.15f, kCurveSCurve);
        // Prepared-but-off alternate movement (demonstrates the per-slot BYPASS axis)
        setModSlot(s, 2, kSrcLFO2, kDstAllResonance, 0.2f, kCurveLinear);
        s.modMatrix.slots[2].bypass = 1;

        // Performance settings: hard velocity, soft steal, high-note-priority allocation
        s.settings.velocityCurve = 2;    // Hard
        s.settings.voiceStealMode = 1;   // Soft
        s.settings.voiceAllocMode = 3;   // HighNote

        // Wrapper: small PLATE verb + a warm TAPE delay (no big hall here)
        s.reverbEnabled = 1; s.reverbType = 0;   // Plate
        s.reverb.size = 0.4f; s.reverb.mix = 0.18f; s.reverb.damping = 0.5f;
        s.delayEnabled = 1;
        s.delay.type = 1;                        // Tape
        s.delay.timeMs = 220.0f; s.delay.feedback = 0.25f; s.delay.mix = 0.15f;
        s.delay.sync = 0;
        s.global.spread = 0.2f;
        presets.push_back(std::move(p));
    }
```

## 3. "Macro Performer" -> "Macro Performer"

- **Locate:** the block containing `p.name = "Macro Performer"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp` (search for the `p.name` string, not a line number — line numbers drift as edits land).
- **Character:** A vocal saw-plus-formant morph patch where all four macros are true one-knob morphs, each wired across multiple global destinations using the mod-matrix scale and stepped-curve axes, ready to be swept live over a tempo-synced plate space.
- **Coverage:** virtual-analog-osc; formant-engine; spectral-morph-mixer; mixer tilt; mod-matrix (all 8 slots); 4 macros each to MULTIPLE dests (one-knob morph); mod-matrix Stepped curve; mod-matrix Exp/SCurve/Linear curves; mod-matrix scale axis (x2); mod-envelope Bezier (release); mod-envelope routed; voice-routes (velocity/keytrack/aftertouch/VoiceLFO/env3); VoiceLFO source; SpecTilt dest; global-filter (macro target); masterGain trim; settings pitchBendRange; settings tuningReferenceHz; settings gainCompensation; Plate reverb; tempo-synced Digital delay.
- **Rationale:** The original only had Macro4 hitting two destinations; the other three macros were single-target, so the 'one-knob morph' promise was unfulfilled and the patch was static at rest. Now every macro spans multiple global dests: Macro1 = cutoff(exp)+globalFltRes so one knob simultaneously brightens the voice filter and tightens the global filter; Macro2 = morph(smoothstep)+specTilt for a coupled timbre sweep; Macro3 = effectMix+filterEnvAmount with the latter at scale x2 (the scale axis) so 'Space' also deepens the sweep; Macro4 = resonance(Stepped, the last unused curve type)+global cutoff for a gritty edge. Autonomous life comes from a VoiceLFO->SpecTilt route (covering the VoiceLFO source) and the Bezier mod-env release routed to morph, so it's never dead when parked. Digital tempo-synced delay + plate and the settings block (12 st bend, 432 Hz reference, gain comp, 0.85 master trim) round out the coverage the other two didn't claim.
- **Replacement code:**

```cpp
    // "Macro Performer" - four one-knob morphs, built to be played by hand
    {
        PresetDef p;
        p.name = "Macro Performer";
        p.category = "Modulation";
        auto& s = p.state;

        // Saw (A) + Formant vowel (B) morphed spectrally -> a vocal saw ready to sweep
        s.oscA.type = 0; s.oscA.waveform = 1; s.oscA.level = 0.7f;   // PolyBLEP saw
        s.oscB.type = 7;                          // Formant
        s.oscB.formantVowel = 1;                  // 'E'
        s.oscB.formantMorph = 1.5f;               // sit between E and I for a nasal edge
        s.oscB.level = 0.6f;
        s.mixer.mode = 1;                         // Spectral Morph
        s.mixer.position = 0.3f;
        s.mixer.tilt = 2.0f;                      // brighten the morphed spectrum a touch

        s.filter.type = 4; s.filter.cutoffHz = 2600.0f; s.filter.resonance = 0.25f;
        s.filter.ladderSlope = 4;

        s.ampEnv.attackMs = 10.0f; s.ampEnv.decayMs = 400.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 500.0f;

        // Mod env with a BEZIER release for an expressive, non-linear tail (routed to morph)
        s.modEnv.attackMs = 5.0f; s.modEnv.decayMs = 350.0f;
        s.modEnv.sustain = 0.4f; s.modEnv.releaseMs = 800.0f;
        s.modEnv.bezierEnabled = 1.0f;
        s.modEnv.bezierReleaseCp1X = 0.2f; s.modEnv.bezierReleaseCp1Y = 0.85f;  // hang, then...
        s.modEnv.bezierReleaseCp2X = 0.6f; s.modEnv.bezierReleaseCp2Y = 0.2f;   // ...drop away

        // Macros parked at neutral - the patch is meant to be swept live
        s.macros.values[0] = 0.5f; // "Brightness"
        s.macros.values[1] = 0.5f; // "Morph"
        s.macros.values[2] = 0.3f; // "Space"
        s.macros.values[3] = 0.0f; // "Edge"

        // --- All 8 slots: each macro is a one-knob morph across MULTIPLE destinations ---
        // Macro 1 "Brightness": all-voice cutoff (exp) + global-filter resonance
        setModSlot(s, 0, kSrcMacro1, kDstAllFltCut,    0.7f, kCurveExp);
        setModSlot(s, 1, kSrcMacro1, kDstGlobalFltRes, 0.4f, kCurveLinear);
        // Macro 2 "Morph": morph position (smoothstep) + spectral tilt
        setModSlot(s, 2, kSrcMacro2, kDstAllMorphPos,  0.8f, kCurveSCurve);
        setModSlot(s, 3, kSrcMacro2, kDstAllSpecTilt,  0.5f, kCurveLinear);
        // Macro 3 "Space": effect mix + filter-env amount, the latter scaled x2 for a huge sweep
        setModSlot(s, 4, kSrcMacro3, kDstEffectMix,    0.6f, kCurveLinear);
        setModSlot(s, 5, kSrcMacro3, kDstAllFltEnvAmt, 0.5f, kCurveLinear);
        s.modMatrix.slots[5].scale = 3;   // x2 depth (scale axis)
        // Macro 4 "Edge": resonance in STEPPED jumps + global-filter cutoff bite
        setModSlot(s, 6, kSrcMacro4, kDstAllResonance, 0.4f, kCurveStepped);
        setModSlot(s, 7, kSrcMacro4, kDstGlobalFltCut, 0.5f, kCurveExp);

        // Voice routes: keyboard expression + the mod-env (Bezier release) drives morph
        setVoiceRoute(s, 0, kVSrcVelocity,   kVDstFltCut,   0.4f);
        setVoiceRoute(s, 1, kVSrcKeyTrack,   kVDstMorphPos, 0.25f);
        setVoiceRoute(s, 2, kVSrcAftertouch, kVDstFltRes,   0.35f);
        setVoiceRoute(s, 3, kVSrcEnv3,       kVDstMorphPos, 0.3f);  // modEnv routed (Bezier release)
        setVoiceRoute(s, 4, kVSrcVoiceLFO,   kVDstSpecTilt, 0.2f);  // gentle autonomous timbre shimmer

        // Global filter is the target for Macro1 (res) and Macro4 (cutoff)
        s.globalFilter.enabled = 1; s.globalFilter.type = 0;   // LP
        s.globalFilter.cutoffHz = 6000.0f; s.globalFilter.resonance = 0.9f;

        // Wrapper: PLATE verb + tempo-synced DIGITAL delay (1/8)
        s.reverbEnabled = 1; s.reverbType = 0;   // Plate
        s.reverb.size = 0.6f; s.reverb.mix = 0.3f;
        s.delayEnabled = 1;
        s.delay.type = 0;                        // Digital
        s.delay.mix = 0.2f; s.delay.feedback = 0.3f;
        s.delay.sync = 1; s.delay.noteValue = kNote1_8;

        // Global performance settings + master trim
        s.global.masterGain = 0.85f;                  // headroom for macro-driven resonance peaks
        s.settings.pitchBendRangeSemitones = 12.0f;   // wide bend for expressive leads
        s.settings.tuningReferenceHz = 432.0f;        // alt concert pitch
        s.settings.gainCompensation = 1;              // keep level steady as the filter moves
        presets.push_back(std::move(p));
    }
```
