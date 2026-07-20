# Ruinae Preset Plan — Arp Performance

The Arp Performance category is where Ruinae's arpeggiator/sequencer engine has to be the star, not a decoration bolted onto a generic voice. The two presets here are deliberately built as opposites so the category collectively demonstrates the breadth of the arp/sequencer feature set rather than one "arp on a saw" idea repeated. "Fill Cascade" is the *maximal, polyphonic, rising build-up* case: a glassy hard-sync + additive voice climbing 1/16 over two octaves, latch-accumulating held notes, with a host-fill condition lane that opens the pattern up during fills. "Ratio Stutter" is the *minimal, monophonic, semi-random* case: a sub-register 303-flavoured bass driven by Elektron ratio-conditions, ratchets, accents and slides, with an auto-wah quacking on every hit. Between them they exercise the condition lane (Fill/NotFill/First **and** the ratio conditions), latch modes, ratchets + ratchet-swing, the modifier lane (accent/slide), retrigger modes, and coprime velocity/gate lanes — and pull in otherwise-unused corners of the synth (Sync + Additive engines, Phase-Distortion engine, Env-Filter auto-wah, Wavefolder, Flanger, S&H stepped modulation) so no two presets in the category share a skeleton.

## 1. "Fill Cascade" -> "Fill Cascade"
- **Locate:** the block containing `p.name = "Fill Cascade"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp` (search for the exact `p.name` string; do not rely on line numbers, which drift as edits land).
- **Character:** A glassy hard-sync lead blended with an additive shimmer, climbing 1/16 over two octaves and piling latched notes into host-fill build-up cascades echoed by tempo-synced ping-pong.
- **Coverage:** condition lane Fill/NotFill/First; fillToggle; latchMode Add; retrigger; arp-mode-up; octave-range; tempo-sync; sync-engine; additive-engine; ladder-filter; filter-env (bug-fixed semitones); mod-matrix LFO2->FltCut SCurve; voice-route Env3->MorphPos; voice-route Velocity->FltCut; pingpong-delay; plate-reverb; voice spread/width; coprime lane lengths.
- **Rationale:** Identity comes from the Sync engine (glassy hard-sync master/slave saws, ratio 2.5) crossfaded with a 32-partial Additive shimmer instead of the stock dual-saw voice; the Env3->MorphPos voice route makes each note travel from sync to shimmer. The Ladder LP (type 4, 24 dB, 6 dB drive) with envAmount=30 (PLAIN semitones, correcting the 4000 bug) gives an audible per-note downward sweep, keyTrack 0.3 keeps it bright while the arp climbs. The unique mod gesture is a tempo-synced 1/4 LFO2 -> AllFltCut on an SCurve, distinct from every sibling's linear LFO->MorphPos skeleton. Wrapper is deliberately NOT a big hall: 1/8 PingPong echoes (crossfeed 0.8, width 140) reinforce the rising line and a tight Plate (size 0.35, mix 0.22) adds sheen. Arp latchMode=Add + fillToggle=1 make held notes accumulate into cascading build-ups; First on steps 1/9 and Fill on 5-8/13-16 shape the open-up; retrigger=Beat locks to transport; coprime 7/5 velocity/gate lanes keep it perpetually shifting. polyphony 12 + spread 0.4 give the cascade room to stack.
- **Replacement code:**

```cpp
    {
        PresetDef p;
        p.name = "Fill Cascade";
        p.category = "Arp Performance";
        RuinaePresetState& s = p.state;

        // --- VOICE: hard-sync lead (NOT another saw) blended with an additive shimmer ---
        // OscA = Sync engine: a sawtooth master hard-syncing a saw slave gives the
        // glassy, edgy sync-lead timbre no dual-saw template can produce.
        s.oscA.type = 3;             // Sync
        s.oscA.waveform = 1;         // master = Sawtooth
        s.oscA.syncWaveform = 1;     // slave  = Sawtooth
        s.oscA.syncRatio = 2.5f;     // bright detuned-formant sync zone
        s.oscA.syncMode = 0;         // Hard sync
        s.oscA.syncAmount = 1.0f;
        s.oscA.level = 0.85f;
        // OscB = Additive: 32 up-tilted partials add air so the blend can travel
        // from "sync" to "shimmer" under the MorphPos route below.
        s.oscB.type = 4;             // Additive
        s.oscB.additivePartials = 32;
        s.oscB.additiveTilt = 3.0f;  // +dB/oct -> bright top end
        s.oscB.additiveInharm = 0.0f;
        s.oscB.fineCents = 6.0f;     // slight detune -> ensemble width
        s.oscB.level = 0.5f;
        s.mixer.mode = 0;            // Crossfade (MorphPos = A<->B blend)
        s.mixer.position = 0.4f;     // start favouring the sync core

        // --- FILTER: 24 dB ladder with drive + a REAL filter-env sweep ---
        // envAmount is PLAIN semitones: +30 gives an audible per-note sweep, fixing
        // the historic 4000 'Hz-as-semitones' bug the acid template still carries.
        s.filter.type = 4;           // Ladder LP
        s.filter.ladderSlope = 4;    // 24 dB/oct
        s.filter.ladderDrive = 6.0f; // analog grit, level-safe
        s.filter.cutoffHz = 2500.0f;
        s.filter.resonance = 3.5f;   // singing, not self-oscillating
        s.filter.envAmount = 30.0f;  // +30 st sweep from the filter env
        s.filter.keyTrack = 0.3f;    // stays bright as the arp climbs octaves

        // Envelopes: snappy amp, plucky filter sweep that decays each step.
        s.ampEnv.attackMs = 4.0f;
        s.ampEnv.decayMs = 250.0f;
        s.ampEnv.sustain = 0.6f;
        s.ampEnv.releaseMs = 300.0f; // long enough to hear cascade tails
        s.ampEnv.decayCurve = 0.3f;  // exp-ish -> percussive attack
        s.filterEnv.attackMs = 2.0f;
        s.filterEnv.decayMs = 350.0f;
        s.filterEnv.sustain = 0.25f;
        s.filterEnv.releaseMs = 250.0f;
        s.filterEnv.decayCurve = 0.4f;

        // --- PER-PRESET MOD IDENTITY (unique gesture: LFO2 SCurve rhythmic filter) ---
        // LFO2 = tempo-synced 1/4 triangle breathing the whole-voice cutoff with an
        // S-curve response beneath the fast 1/16 arp.
        s.lfo2.shape = 1;            // Triangle
        s.lfo2.sync = 1;
        s.lfo2.depth = 1.0f;
        s.lfo2Ext.noteValue = 13;   // 1/4 note
        setModSlot(s, 0, kSrcLFO2, kDstAllFltCut, 0.35f, kCurveSCurve);
        // Voice routes: ModEnv sweeps the A<->B blend so each note morphs sync->shimmer;
        // velocity opens the filter for dynamic accents.
        setVoiceRoute(s, 0, kVSrcEnv3, kVDstMorphPos, 0.5f);
        setVoiceRoute(s, 1, kVSrcVelocity, kVDstFltCut, 0.4f);

        // --- WRAPPER: tempo-synced ping-pong echoes + small plate (NOT a big hall) ---
        s.delayEnabled = 1;
        s.delay.type = 2;            // PingPong
        s.delay.sync = 1;
        s.delay.noteValue = 10;     // 1/8 echoes reinforce the rising pattern
        s.delay.feedback = 0.45f;
        s.delay.mix = 0.35f;
        s.delay.pingPongCrossFeed = 0.8f;
        s.delay.pingPongWidth = 140.0f;
        s.reverbEnabled = 1;
        s.reverbType = 0;           // Plate (tight, not a diffuse wash)
        s.reverb.size = 0.35f;
        s.reverb.mix = 0.22f;
        s.reverb.damping = 0.5f;
        s.reverb.preDelayMs = 10.0f;

        // --- VOICE/GLOBAL: poly stack so latched notes can pile into a cascade ---
        s.global.polyphony = 12;
        s.global.spread = 0.4f;     // pan-spread the stacked voices
        s.global.width = 1.4f;

        // --- ARP: rising 1/16 over 2 octaves, host-fill build-up ---
        setArpEnabled(s, true);
        setArpMode(s, kModeUp);
        setTempoSync(s, true);
        setArpRate(s, kNote1_16);
        s.arp.octaveRange = 2;
        s.arp.latchMode = 2;        // Add -> held notes ACCUMULATE into the cascade
        s.arp.retrigger = 2;        // Beat -> pattern locks to host transport
        // Condition lane: First on the downbeats (steps 1 & 9), Fill on the
        // back-half runs (steps 5-8, 13-16) so the pattern opens up during fills.
        int32_t cond16[] = {
            15, kCondAlways, kCondAlways, kCondAlways,   // 15 = First (step 1)
            16, 16, 16, 16,                              // 16 = Fill  (steps 5-8)
            15, kCondAlways, kCondAlways, kCondAlways,   // First (step 9)
            16, 16, 16, 16                               // Fill  (steps 13-16)
        };
        setConditionLane(s, 16, cond16, /*fillToggle*/ 1); // fill latched ON -> audible by default
        // Coprime lane lengths (7 vs 5) -> velocity/gate never realign, motion never repeats.
        float vel7[] = {0.55f, 0.7f, 0.8f, 0.9f, 1.0f, 0.75f, 0.65f}; // rising accent ramp
        setVelocityLane(s, 7, vel7);
        float gate5[] = {0.9f, 0.7f, 0.95f, 0.6f, 0.8f};
        setGateLane(s, 5, gate5);

        presets.push_back(std::move(p));
    }
```

## 2. "Probability Waves" -> "Ratio Stutter"
- **Locate:** the block containing `p.name = "Probability Waves"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp` (search for the exact `p.name` string; do not rely on line numbers, which drift as edits land). Note the block is being renamed — the replacement sets `p.name = "Ratio Stutter"`.
- **Character:** A mono octave-down saw + Casio-CZ resonant-square bass stuttering through Elektron ratio-conditions and 2x ratchets, auto-wah quacking on every accented hit under sample-and-hold filter jumps.
- **Coverage:** arp ratio conditions (Ratio_A_B); ratchet lane; modifier lane accent; arp mode AsPlayed; condition-lane-ratio; step-accent; step-slide; phase-distortion-engine; env-filter (auto-wah); wavefolder-distortion; mod-matrix S&H->FltCut Stepped; mod-matrix LFO1->Resonance; voice-route Velocity->FltRes; voice-route Env3->DistDrive; mono/glide; flanger; tape-delay; coprime lane lengths; ratchet-swing.
- **Rationale:** Renamed from the generic 'Probability Waves' to 'Ratio Stutter' to name its true character: Elektron ratio-conditions (Ratio_1_2=6 on odd steps, Ratio_3_4=13 on evens) so the trig pattern only fully resolves every four loops. Voice is distinct from Fill Cascade and from the stock bass: PolyBLEP saw sub (-12) blended with a Casio-CZ PhaseDistortion resonant square (pdWaveform=1, pdDistortion 0.5) for buzzy grit. The signature filter is the almost-never-used Env Filter auto-wah (type 11): its 8 ms/120 ms envelope makes each ratcheted hit quack. Owned dirty flavour is the Wavefolder (drive 0.35, mix 0.4, level-safe). Unique mod story: 1/16-synced Sample&Hold -> AllFltCut on a Stepped curve for pseudo-random filter jumps, plus a slow free LFO1 -> AllResonance at negative amount for counter-drift; velocity->FltRes squelches accents, Env3->DistDrive blooms grit per note. Mono + legato portamento (40 ms) turns the single slide-flagged step into a 303 glide. Flanger (modulationType=2, unused by any other preset) and a warm 1/16 Tape slap replace reverb entirely so the sub stays tight. 2x ratchets on downbeats with ratchetSwing 58 add drum-like bursts; accent modifier lane, coprime 5/3 velocity/gate lanes keep the groove semi-random and alive.
- **Replacement code:**

```cpp
    {
        PresetDef p;
        p.name = "Ratio Stutter";
        p.category = "Arp Performance";
        RuinaePresetState& s = p.state;

        // --- VOICE: octave-down saw + a CZ phase-distortion "square" for resonant grit ---
        // OscA = PolyBLEP saw one octave down = the sub/bass fundamental.
        s.oscA.type = 0;             // PolyBLEP
        s.oscA.waveform = 1;         // Sawtooth
        s.oscA.tuneSemitones = -12.0f;
        s.oscA.level = 0.85f;
        // OscB = Phase Distortion (Casio-CZ): a resonant square rather than a plain
        // square osc -> hollow-but-buzzy edge a subtractive square can't give.
        s.oscB.type = 2;             // PhaseDistortion
        s.oscB.pdWaveform = 1;       // Square
        s.oscB.pdDistortion = 0.5f;  // DCW resonance for CZ bite
        s.oscB.tuneSemitones = -12.0f;
        s.oscB.level = 0.5f;
        s.mixer.position = 0.5f;

        // --- FILTER: envelope-follower auto-wah (type 11) for a talking stutter ---
        // A very under-used filter; its own attack/release make every ratcheted hit
        // "quack", reinforcing the semi-random Elektron feel.
        s.filter.type = 11;          // Env Filter (auto-wah)
        s.filter.cutoffHz = 350.0f;  // low base -> plenty of sweep room
        s.filter.resonance = 4.0f;   // vocal wah Q
        s.filter.envSubType = 0;     // LP response
        s.filter.envSensitivity = 6.0f;
        s.filter.envDepth = 0.8f;
        s.filter.envAttack = 8.0f;
        s.filter.envRelease = 120.0f;
        s.filter.envDirection = 0;   // sweep up on transients

        // --- DISTORTION: wavefolder = this preset's OWNED dirty flavour ---
        s.distortion.type = 4;       // Wavefolder
        s.distortion.foldType = 0;
        s.distortion.drive = 0.35f;  // moderate -> gritty, not harsh
        s.distortion.character = 0.5f;
        s.distortion.mix = 0.4f;     // blended so the sub stays solid

        // Envelopes: punchy bass amp; a fast mod-env blip drives the folder per step.
        s.ampEnv.attackMs = 2.0f;
        s.ampEnv.decayMs = 180.0f;
        s.ampEnv.sustain = 0.55f;
        s.ampEnv.releaseMs = 120.0f;
        s.ampEnv.decayCurve = 0.3f;
        s.modEnv.attackMs = 1.0f;
        s.modEnv.decayMs = 120.0f;
        s.modEnv.sustain = 0.0f;     // one-shot spike
        s.modEnv.releaseMs = 100.0f;

        // --- PER-PRESET MOD IDENTITY (unique: S&H stepped filter jumps) ---
        // Sample&Hold clocked at 1/16 jumps the cutoff in stepped increments ->
        // pseudo-random tonal stutter that pairs with the ratio-condition rhythm.
        s.sampleHold.rateHz = 8.0f;
        s.sampleHold.sync = 1;
        s.sampleHold.noteValue = 7;  // 1/16
        setModSlot(s, 0, kSrcSampleHold, kDstAllFltCut, 0.4f, kCurveStepped);
        // Slow free LFO nudges resonance the OTHER way (negative amount) for drift.
        s.lfo1.shape = 0;            // Sine
        s.lfo1.sync = 0;             // free-running
        s.lfo1.rateHz = 0.3f;
        s.lfo1.depth = 1.0f;
        setModSlot(s, 1, kSrcLFO1, kDstAllResonance, -0.2f, kCurveSCurve);
        // Voice routes: velocity -> resonance (accented steps squelch harder),
        // mod-env -> distortion drive (per-note grit bloom).
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltRes, 0.5f);
        setVoiceRoute(s, 1, kVSrcEnv3, kVDstDistDrive, 0.4f);

        // --- WRAPPER: mono 303-style glide + flanger + warm tape slap (NO reverb) ---
        s.global.voiceMode = 1;      // Mono -> tight, one-note-at-a-time stutter
        s.monoMode.legato = 1;
        s.monoMode.portamentoTimeMs = 40.0f;
        s.monoMode.portaMode = 1;    // Legato -> only slide-flagged steps glide
        s.global.width = 1.1f;
        s.modulationType = 2;        // Flanger (used by ZERO other presets)
        s.flanger.rateHz = 0.3f;
        s.flanger.depth = 0.4f;
        s.flanger.feedback = 0.3f;
        s.flanger.mix = 0.25f;
        s.flanger.stereoSpread = 120.0f;
        s.delayEnabled = 1;
        s.delay.type = 1;            // Tape (warm slapback, not a clean digital)
        s.delay.sync = 1;
        s.delay.noteValue = 7;       // 1/16 slap
        s.delay.feedback = 0.25f;
        s.delay.mix = 0.2f;
        s.delay.tapeSaturation = 0.6f;

        // --- ARP: AsPlayed sub groove with Elektron ratio conditions + ratchets ---
        setArpEnabled(s, true);
        setArpMode(s, kModeAsPlayed);
        setTempoSync(s, true);
        setArpRate(s, kNote1_16);
        s.arp.octaveRange = 1;       // stay in the sub register
        // Condition lane: Ratio_1_2 (fire 1st of every 2 loops) on odd steps,
        // Ratio_3_4 (fire 3rd of every 4 loops) on even steps -> non-repeating,
        // semi-random trig pattern that only fully resolves every 4 bars.
        int32_t cond16[] = {
            6, 13, 6, 13,   // 6 = Ratio_1_2 (odd), 13 = Ratio_3_4 (even)
            6, 13, 6, 13,
            6, 13, 6, 13,
            6, 13, 6, 13
        };
        setConditionLane(s, 16, cond16, /*fillToggle*/ 0);
        // Ratchet lane: 2x rolls on the downbeats -> drum-like stutter bursts.
        int32_t ratch8[] = {2, 1, 2, 1, 2, 1, 2, 1};
        setRatchetLane(s, 8, ratch8);
        s.arp.ratchetSwing = 58.0f;  // swung sub-steps inside each roll
        // Modifier lane: accent the downbeats, one slide step for a 303 glide.
        int32_t mod8[] = {
            kStepActive | kStepAccent, kStepActive,
            kStepActive | kStepAccent, kStepActive,
            kStepActive | kStepAccent, kStepActive,
            kStepActive | kStepAccent, kStepActive | kStepSlide
        };
        setModifierLane(s, 8, mod8, /*accentVel*/ 40, /*slideMs*/ 50.0f);
        // Coprime velocity (5) & gate (3) lanes -> perpetually shifting dynamics/length.
        float vel5[] = {1.0f, 0.6f, 0.85f, 0.5f, 0.9f};
        setVelocityLane(s, 5, vel5);
        float gate3[] = {0.5f, 0.8f, 0.35f};
        setGateLane(s, 3, gate3);

        presets.push_back(std::move(p));
    }
```
