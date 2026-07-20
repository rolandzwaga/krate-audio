# Ruinae Preset Plan — Arp Generative

This category exists to prove that Ruinae's arpeggiator is a *generative* instrument, not a step sequencer with a shuffle button. Every preset here must earn its "generative" label by making the pattern audibly *evolve*: the two members below attack that from opposite ends of the signal path. "Vox Oracle" pushes randomness into the **pattern domain** — probability condition lanes and coprime velocity lanes so accents and note-density never realign — over a dual-Formant vowel voice that literally talks. "Chaos Garden" pushes randomness into the **source domain** — a live Rössler chaos oscillator plus a chaosMod LFO mutating morph and resonance while a Walk-mode arp randomizes note order. Together they cover distinct engines (Formant, Chaos/Rossler), the arp Walk mode, spice/humanize, the full Prob10..Prob90 condition-lane range, chaosMod and smoothed-Random mod sources, the plain-semitones filter-env fix, and both identity reverbs (Plate and Hall). Neither preset reuses the setSynth* template helpers; each is built field-by-field for a distinct sonic identity.

## 1. "Spice Evolver" -> "Vox Oracle"

- **Locate:** the block containing `p.name = "Spice Evolver";` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp` (search for the string, do not trust line numbers — they drift as edits land).
- **Character:** A dual-formant vowel-synth lead whose vowel actually talks — a slow free LFO and note velocity sweep the A↔O morph while a Prob10→Prob90 condition ramp makes the 1/16 up-arp breathe like generative speech.
- **Coverage:** distinct engines (Formant); spice; humanize; condition lane probabilities (Prob10..Prob90); filter-env bug fix (plain semitones); LFO1->MorphPos SCurve mod identity; velocity->MorphPos voice route; SVF band-pass formant filter; plate reverb (identity FX).
- **Rationale:** Directive #1 asks for a formant vowel lead animated by spice/humanize + a Prob10->Prob90 condition ramp. The vowel genuinely "talks" because BOTH oscillators sit on the Formant engine parked at opposite vowels (A/E vs O/U); sweeping mixer morph position — the one destination that crossfades A<->B — sweeps the vowel = speech-like. The morph is driven two ways for a distinct mod identity no sibling repeats: a slow free-running LFO1 (rate 0.35 Hz, sync=0) on kDstAllMorphPos with an SCurve, plus a velocity->MorphPos voice route so louder notes open the vowel. Filter is SVF band-pass at 1.2 kHz res 2.5 to spotlight the formant band, and the filter-env bug is fixed (envAmount=16 plain semitones, not a Hz value) with a plucky filterEnv so each probabilistic note gets a consonant attack. Condition lane ramps up then back (Prob10->90->10) so the arp breathes; velocity lane length 7 is coprime with the 16-step condition lane so accents never realign. Intimate Plate reverb (mix 0.2) keeps it a lead rather than a wash. Everything is audible at moderate velocity; res 2.5 and drive-free filter avoid ear-destroyers.
- **Replacement code:**

```cpp
    // T034: "Vox Oracle" (was "Spice Evolver") - pattern-domain randomness member
    {
        PresetDef p;
        p.name = "Vox Oracle";
        p.category = "Arp Generative";

        // --- Voice: dual FORMANT oscillator = vowel synthesis (a "talking" line) ---
        // OSC A sits in the A->E vowel region, OSC B in the O->U region; crossfading
        // between them with the mixer morph literally sweeps the vowel = speech-like.
        p.state.oscA.type = 7;             // Formant engine
        p.state.oscA.formantVowel = 0;     // base vowel A
        p.state.oscA.formantMorph = 0.6f;  // parked just past A toward E (bright)
        p.state.oscA.level = 0.85f;
        p.state.oscB.type = 7;             // Formant engine
        p.state.oscB.formantVowel = 3;     // base vowel O
        p.state.oscB.formantMorph = 3.4f;  // parked between O and U (dark/round)
        p.state.oscB.fineCents = 6.0f;     // tiny beat so the two vowels don't phase-lock
        p.state.oscB.level = 0.7f;
        p.state.mixer.mode = 0;            // Crossfade A<->B
        p.state.mixer.position = 0.4f;     // start biased to the bright vowel

        // --- Filter: gentle SVF band-pass to spotlight the vocal formant band ---
        p.state.filter.type = 2;           // SVF BP
        p.state.filter.cutoffHz = 1200.0f; // centered on the vowel formant region
        p.state.filter.resonance = 2.5f;   // narrow enough to sing, not whistle
        p.state.filter.svfSlope = 0;       // 12 dB - keep it open/airy
        p.state.filter.envAmount = 16.0f;  // +16 st per-note bloom (FIXED: plain semitones)
        // Plucky filter env so each probabilistic note gets a little consonant "attack"
        p.state.filterEnv.attackMs = 4.0f;
        p.state.filterEnv.decayMs = 180.0f;
        p.state.filterEnv.sustain = 0.25f;
        p.state.filterEnv.releaseMs = 160.0f;

        // --- Amp env: lead-ish, enough release to hear the vowel tail ---
        p.state.ampEnv.attackMs = 8.0f;
        p.state.ampEnv.decayMs = 250.0f;
        p.state.ampEnv.sustain = 0.75f;
        p.state.ampEnv.releaseMs = 200.0f;

        // --- MOD IDENTITY: the vowel actually TALKS ---
        // (1) slow free-running LFO drifts the morph across the vowel space (all voices)
        p.state.lfo1.rateHz = 0.35f;       // ~3 s vowel cycle
        p.state.lfo1.shape = 0;            // sine
        p.state.lfo1.sync = 0;             // free-running, not tempo-locked
        p.state.lfo1.depth = 1.0f;
        setModSlot(p.state, 0, kSrcLFO1, kDstAllMorphPos, 0.55f, kCurveSCurve);
        // (2) velocity opens the vowel per-note: harder hits = brighter/more-open vowel
        setVoiceRoute(p.state, 0, kVSrcVelocity, kVDstMorphPos, 0.45f);

        // --- FX: intimate PLATE, not a wash - this is a lead, keep it up front ---
        p.state.reverbEnabled = 1;
        p.state.reverbType = 0;            // Plate
        p.state.reverb.size = 0.4f;
        p.state.reverb.mix = 0.2f;
        p.state.reverb.damping = 0.5f;

        // --- ARP: Up 1/16 x2 oct, animated purely by pattern-domain randomness ---
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUp);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        p.state.arp.octaveRange = 2;
        p.state.arp.spice = 0.7f;          // probabilistic lane variation
        p.state.arp.humanize = 0.3f;       // micro-timing/vel jitter = human feel
        // Condition lane ramps Prob10 -> Prob90 -> back: the line thins & thickens like breath
        int32_t cond16[] = {
            kCondProb10, kCondProb25, kCondProb50, kCondProb75,
            kCondProb90, kCondProb90, kCondProb75, kCondProb50,
            kCondProb25, kCondProb10, kCondProb50, kCondProb90,
            kCondProb25, kCondProb75, kCondAlways, kCondAlways
        };
        setConditionLane(p.state, 16, cond16, 0);
        // Velocity lane length 7 (coprime with 16) so accents never realign = endless variation
        float vel7[] = {0.7f, 0.95f, 0.55f, 1.0f, 0.6f, 0.85f, 0.45f};
        setVelocityLane(p.state, 7, vel7);

        presets.push_back(std::move(p));
    }
```

## 2. "Chaos Garden" -> "Chaos Garden"

- **Locate:** the block containing `p.name = "Chaos Garden";` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp` (search for the string, do not trust line numbers — they drift as edits land).
- **Character:** A Rössler-chaos oscillator over a clean sine sub, its timbre live-mutated by a slow chaosMod LFO routed to morph AND resonance while a Walk-mode 1/16 arp randomizes note order — a self-evolving generative garden drenched in a big diffuse Hall.
- **Coverage:** distinct engines (Chaos/Rossler); chaosMod as mod-matrix source; arp mode Walk; spice; humanize; condition lane probabilities (Prob10..Prob90); Random mod source (smoothed) -> filter cutoff; filter-env bug fix (plain semitones); ladder filter + drive; hall reverb (identity FX).
- **Rationale:** Directive #2 wants a Rössler chaos oscillator in Walk mode with chaosMod (depth ~0.5) routed to MorphPos/resonance so timbre mutates while note order randomizes. oscA is the Chaos engine, attractor=1 (Rossler), chaosAmount=0.35 (quasi-pitched, deliberately below the noise/scream region) anchored by a sine sub an octave down on oscB so the result stays musical. The mod identity is source-domain: chaosMod.type=1/depth=0.5 (raised from its silent 0 default) feeds the mod matrix as kSrcChaos into TWO slots — kDstAllMorphPos (SCurve, sweeps chaos<->sine blend) and kDstAllResonance (Exp) — plus a third slot with a heavily-smoothed Random source (smoothness 0.8) wandering kDstAllFltCut, giving continuous drift no sibling repeats. Ladder filter with 6 dB drive fattens and tames the chaos; the filter-env bug is fixed (envAmount=24 plain semitones) with a slow filterEnv for per-note bloom. Pad-like amp env (60 ms attack, 1200 ms release) + polyphony 12 + spread 0.4 let Walk-mode notes overlap into the drenched Hall (size 0.85, mix 0.45, diffusion 0.8). Pitch lane length 7 is coprime with the 16-step condition lane for a long non-repeating melody. spice 0.9/humanize 0.4 push the pattern randomness. Levels and drive are moderate with the sine anchor keeping output controlled — no silent or destructive extremes.
- **Replacement code:**

```cpp
    // T035: "Chaos Garden" - source-domain randomness member (drenched in reverb)
    {
        PresetDef p;
        p.name = "Chaos Garden";
        p.category = "Arp Generative";

        // --- Voice: CHAOS oscillator (Rossler) over a sine sub for tonal anchoring ---
        // OSC A is the chaotic attractor (quasi-pitched, mutating); OSC B is a clean
        // sub sine an octave down so the garden stays musical, not pure noise.
        p.state.oscA.type = 5;             // Chaos engine
        p.state.oscA.chaosAttractor = 1;   // Rossler
        p.state.oscA.chaosAmount = 0.35f;  // quasi-pitched sweet spot, not a scream
        p.state.oscA.chaosCoupling = 0.2f; // gentle cross-axis instability
        p.state.oscA.chaosOutput = 0;      // X axis
        p.state.oscA.level = 0.8f;
        p.state.oscB.type = 0;             // PolyBLEP
        p.state.oscB.waveform = 0;         // Sine sub-body
        p.state.oscB.tuneSemitones = -12.0f;
        p.state.oscB.level = 0.55f;
        p.state.mixer.mode = 0;            // Crossfade
        p.state.mixer.position = 0.5f;     // even blend; chaosMod will sweep it

        // --- Filter: driven Ladder LP for warmth; resonance is a moving target ---
        p.state.filter.type = 4;           // Ladder
        p.state.filter.ladderSlope = 4;    // 24 dB/oct
        p.state.filter.ladderDrive = 6.0f; // a little grit to tame + fatten the chaos
        p.state.filter.cutoffHz = 2200.0f;
        p.state.filter.resonance = 3.0f;   // base resonance (chaosMod pushes it around)
        p.state.filter.envAmount = 24.0f;  // +24 st sweep per note (FIXED: plain semitones)
        p.state.filterEnv.attackMs = 40.0f;
        p.state.filterEnv.decayMs = 800.0f;
        p.state.filterEnv.sustain = 0.4f;
        p.state.filterEnv.releaseMs = 900.0f;

        // --- Amp env: pad-like bloom so walked notes overlap & feed the reverb ---
        p.state.ampEnv.attackMs = 60.0f;
        p.state.ampEnv.decayMs = 600.0f;
        p.state.ampEnv.sustain = 0.7f;
        p.state.ampEnv.releaseMs = 1200.0f;
        p.state.global.polyphony = 12;     // lush overlap for the long release
        p.state.global.spread = 0.4f;      // pan-spread the voices for width

        // --- MOD IDENTITY: chaosMod is the LIVE randomness source (source-domain) ---
        p.state.chaosMod.type = 1;         // Rossler chaos LFO
        p.state.chaosMod.rateHz = 0.8f;    // slow evolving mutation
        p.state.chaosMod.depth = 0.5f;     // MUST raise - defaults to 0 (silent source)
        p.state.chaosMod.sync = 0;
        // Route chaos to BOTH morph and resonance so the timbre mutates on two axes...
        setModSlot(p.state, 0, kSrcChaos, kDstAllMorphPos, 0.7f, kCurveSCurve);
        setModSlot(p.state, 1, kSrcChaos, kDstAllResonance, 0.5f, kCurveExp);
        // ...and a smoothed Random wanders the cutoff for extra source-domain drift
        p.state.random.rateHz = 0.5f;
        p.state.random.smoothness = 0.8f;  // glide between random values, no zipper
        setModSlot(p.state, 2, kSrcRandom, kDstAllFltCut, 0.35f, kCurveLinear, 20.0f);

        // --- FX: drenched HALL - the "garden" is a big diffuse space ---
        p.state.reverbEnabled = 1;
        p.state.reverbType = 1;            // Hall
        p.state.reverb.size = 0.85f;
        p.state.reverb.mix = 0.45f;
        p.state.reverb.damping = 0.35f;
        p.state.reverb.diffusion = 0.8f;
        p.state.reverb.preDelayMs = 20.0f;

        // --- ARP: WALK mode randomizes note ORDER while chaosMod mutates timbre ---
        setArpEnabled(p.state, true);
        setArpMode(p.state, 7);            // Walk (drunk +/-1 random walk; no named const)
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        p.state.arp.octaveRange = 2;
        p.state.arp.spice = 0.9f;          // heavy probabilistic lane variation
        p.state.arp.humanize = 0.4f;
        // Pitch lane length 7 (coprime with the 16-step condition lane) = long non-repeating melody
        int32_t pitch7[] = {0, 3, 5, 7, 10, 5, 3};
        setPitchLane(p.state, 7, pitch7);
        // Condition lane keeps the density breathing under the Walk
        int32_t cond16[] = {
            kCondAlways, kCondProb90, kCondProb50, kCondProb75,
            kCondProb25, kCondProb90, kCondProb50, kCondProb10,
            kCondProb75, kCondProb50, kCondProb90, kCondProb25,
            kCondAlways, kCondProb50, kCondProb75, kCondProb25
        };
        setConditionLane(p.state, 16, cond16, 0);

        presets.push_back(std::move(p));
    }
```
