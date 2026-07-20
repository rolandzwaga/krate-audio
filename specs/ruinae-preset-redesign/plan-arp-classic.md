# Ruinae Preset Plan — Arp Classic

The Arp Classic bank exists to showcase Ruinae's arpeggiator transport and its per-step sequencer lanes, but the legacy trio leaned on `setSynthLead`/`setSynthPad` clones that all read as the same detuned-saw-through-SVF voice with only the arp mode changed. The redesign gives each preset a genuinely different voice engine (dual PolyBLEP pulse, dual Formant vowel, driven Ladder saw), a corrected audible filter-envelope sweep (semitone units, not the legacy 4000 Hz bug), and a single unrepeated modulation gesture that becomes its sonic signature. Collectively the three cover the Up / Down / DownUp arp modes, latch and retrigger behaviour, interleaved octaves, all four sequencer lanes (velocity, gate, pitch), and tempo-synced LFO routing — so the category demonstrates the arp system end-to-end rather than three tempos of one patch.

## 1. "Basic Up 1/16" -> "Pulse Climb"
- **Locate:** the block containing `p.name = "Basic Up 1/16"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A hollow, bright dual-pulse PolyBLEP lead that latches and crescendos as an up-arp climbs the keyboard, the filter blooming exponentially with each rising note.
- **Coverage:** operatingMode=MIDI; arp mode Up; latchMode Hold; retrigger Note/Beat; tempoSync; noteValue (1/16, 1/8, 1/8T); gateLength; velocity lane; gate lane; octaveRange; PWM pulse engine.
- **Rationale:** Ditches setSynthLead entirely for a two-pulse PolyBLEP voice (waveform=3, duty 0.2/0.8) — the mirrored pulse widths beat into a hollow reed tone that no saw sibling shares. envAmount is a correct +30 semitones (not the 4000 bug) driving a fast filterEnv for a real 16th pluck. latchMode=1 + a rising 0.7->1.0 velocity lane deliver the 'climb that crescendos' brief. Its signature mod gesture — ArpPitch->AllFltCut on an EXPONENTIAL curve — makes the filter flare open toward the top of the ascent, a gesture neither sibling uses.
- **Replacement code:**
```cpp
    // T024: "Pulse Climb" — hollow dual-PolyBLEP PULSE lead, latched up-arp that crescendos
    {
        PresetDef p;
        p.name = "Pulse Climb";
        p.category = "Arp Classic";
        // --- Voice: two PolyBLEP pulse oscs at opposite duty cycles for a hollow, reedy tone ---
        p.state.oscA.type = 0;            // PolyBLEP
        p.state.oscA.waveform = 3;        // Pulse
        p.state.oscA.pulseWidth = 0.2f;   // thin/nasal side of the PWM sweet spot
        p.state.oscA.level = 0.85f;
        p.state.oscB.type = 0;            // PolyBLEP
        p.state.oscB.waveform = 3;        // Pulse
        p.state.oscB.pulseWidth = 0.8f;   // mirror duty -> the two pulses beat into a hollow chorus
        p.state.oscB.fineCents = 7.0f;    // slight detune for width without saw-thickness
        p.state.oscB.level = 0.7f;
        p.state.mixer.position = 0.5f;    // equal blend of the two duty cycles
        // --- Filter: SVF LP with an AUDIBLE env sweep (semitones, NOT the 4000 Hz bug) ---
        p.state.filter.type = 0;          // SVF LP
        p.state.filter.cutoffHz = 2600.0f;
        p.state.filter.resonance = 2.2f;  // enough bite for a pluck, well short of whistling
        p.state.filter.envAmount = 30.0f; // +30 st: strong per-note pluck sweep
        p.state.filter.keyTrack = 0.4f;   // higher notes stay bright as the arp climbs
        // Fast filter env = 16th-note pluck; low sustain so each step re-attacks
        p.state.filterEnv.attackMs = 1.0f;
        p.state.filterEnv.decayMs = 120.0f;
        p.state.filterEnv.sustain = 0.15f;
        p.state.filterEnv.releaseMs = 90.0f;
        // Plucky amp: quick attack, moderate decay, half sustain so steps articulate
        p.state.ampEnv.attackMs = 2.0f;
        p.state.ampEnv.decayMs = 180.0f;
        p.state.ampEnv.sustain = 0.5f;
        p.state.ampEnv.releaseMs = 140.0f;
        // --- Arp transport: classic Up @1/16, LATCHED so the climb sustains hands-free ---
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUp);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 72.0f); // slight separation keeps the pulses plucky
        setArpSwing(p.state, 0.0f);
        p.state.arp.octaveRange = 2;
        p.state.arp.latchMode = 1;        // Hold: notes latch, the pattern runs on its own
        // Rising velocity lane -> genuine crescendo as the arp ascends
        float velRise8[] = {0.70f, 0.74f, 0.78f, 0.83f, 0.87f, 0.91f, 0.95f, 1.0f};
        setVelocityLane(p.state, 8, velRise8);
        float gate8[] = {0.85f, 0.85f, 0.85f, 0.85f, 0.85f, 0.85f, 0.85f, 0.85f};
        setGateLane(p.state, 8, gate8);
        // --- Mod identity (unique to this preset): arp pitch opens the filter EXPONENTIALLY,
        //     so the top of the climb flares open far more than the bottom ---
        setModSlot(p.state, 0, kSrcArpPitch, kDstAllFltCut, 0.45f, kCurveExp);
        // Velocity adds resonance bite so the crescendo also sharpens tonally
        setVoiceRoute(p.state, 0, kVSrcVelocity, kVDstFltRes, 0.3f);
        presets.push_back(std::move(p));
    }
```

## 2. "Down 1/8" -> "Vowel Descent"
- **Locate:** the block containing `p.name = "Down 1/8"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A descending choir built from two formant oscillators tuned to different vowels, slowly morphing A-to-E under a free LFO while an interleaved 2-octave down-arp cascades through a Hall.
- **Coverage:** operatingMode=MIDI; arp mode Down; octaveMode Interleaved; tempoSync; noteValue (1/16, 1/8, 1/8T); gateLength; velocity lane; gate lane; octaveRange; Formant engine.
- **Rationale:** Replaces setSynthPad's saw voice with two Formant oscillators (type=7) parked on different vowels (A and E), then uses the mixer Crossfade + a free-running 0.18 Hz LFO1 routed to AllMorphPos (S-curve) to physically morph 'Ah'<->'Eh' — a true vocal gesture, and the only preset using LFO1->MorphPos with that curve. octaveMode=1 (Interleaved) and a Down 1/8 near-legato arp give the descending-choir cascade the brief asks for; a moderate Hall (reverbType=1) supplies air without the stock huge-diffuse wash.
- **Replacement code:**
```cpp
    // T025: "Vowel Descent" — dual Formant-osc choir, LFO-morphed A->E, interleaved down-arp
    {
        PresetDef p;
        p.name = "Vowel Descent";
        p.category = "Arp Classic";
        // --- Voice: two FORMANT oscillators on different vowels; the mixer crossfade IS the vowel ---
        p.state.oscA.type = 7;            // Formant
        p.state.oscA.formantVowel = 0;    // A
        p.state.oscA.formantMorph = 0.0f; // sit on pure A
        p.state.oscA.level = 1.0f;
        p.state.oscB.type = 7;            // Formant
        p.state.oscB.formantVowel = 1;    // E
        p.state.oscB.formantMorph = 0.0f; // sit on pure E
        p.state.oscB.fineCents = -6.0f;   // gentle detune -> a small ensemble/choir shimmer
        p.state.oscB.level = 1.0f;
        p.state.mixer.mode = 0;           // Crossfade: position blends the A voice into the E voice
        p.state.mixer.position = 0.3f;    // start mostly on 'Ah'
        // --- Filter: warm SVF LP, gentle motion (choir, not pluck) ---
        p.state.filter.type = 0;          // SVF LP
        p.state.filter.cutoffHz = 3500.0f;
        p.state.filter.resonance = 0.5f;
        p.state.filter.envAmount = 14.0f; // small, soft swell (semitones, correct units)
        // Slow bloom so each descending step blossoms rather than clicks
        p.state.filterEnv.attackMs = 90.0f;
        p.state.filterEnv.decayMs = 400.0f;
        p.state.filterEnv.sustain = 0.6f;
        p.state.filterEnv.releaseMs = 700.0f;
        // Choir amp: slow-ish attack, long release for a vocal wash
        p.state.ampEnv.attackMs = 120.0f;
        p.state.ampEnv.decayMs = 400.0f;
        p.state.ampEnv.sustain = 0.78f;
        p.state.ampEnv.releaseMs = 900.0f;
        // --- Free-running slow LFO to sweep the vowel crossfade A<->E ---
        p.state.lfo1.rateHz = 0.18f;      // ~5.5 s per cycle: a slow 'aaah-eeeh' morph
        p.state.lfo1.shape = 0;           // Sine
        p.state.lfo1.depth = 1.0f;
        p.state.lfo1.sync = 0;            // free-running, not tempo-locked -> organic drift
        // --- Arp transport: Down @1/8, INTERLEAVED octaves across 2 octaves ---
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeDown);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_8);
        setArpGateLength(p.state, 95.0f); // near-legato so the choir notes bleed into each other
        setArpSwing(p.state, 0.0f);
        p.state.arp.octaveRange = 2;
        p.state.arp.octaveMode = 1;       // Interleaved: octaves weave rather than block-sequence
        float vel8[] = {0.85f, 0.8f, 0.85f, 0.8f, 0.85f, 0.8f, 0.85f, 0.8f};
        setVelocityLane(p.state, 8, vel8);
        float gate8[] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
        setGateLane(p.state, 8, gate8);
        // --- Mod identity (unique): the slow LFO crossfades the two vowels via morph position,
        //     with an S-curve so it lingers on each vowel before gliding to the next ---
        setModSlot(p.state, 0, kSrcLFO1, kDstAllMorphPos, 0.7f, kCurveSCurve);
        // --- Wrapper: a moderate HALL (not the reflex big diffuse pad wash) ---
        p.state.reverbEnabled = 1;
        p.state.reverbType = 1;           // Hall
        p.state.reverb.size = 0.7f;
        p.state.reverb.mix = 0.35f;
        p.state.reverb.damping = 0.45f;
        presets.push_back(std::move(p));
    }
```

## 3. "UpDown 1/8T" -> "Triplet Bounce"
- **Locate:** the block containing `p.name = "UpDown 1/8T"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A gritty detuned dual-saw lead through a driven ladder filter, bouncing a DownUp triplet arp (descend-then-ascend) with beat-retrigger, hard velocity accents, staccato/legato gate alternation and a +12 pitch lift.
- **Coverage:** operatingMode=MIDI; arp mode DownUp; retrigger Note/Beat; tempoSync; noteValue (1/16, 1/8, 1/8T); gateLength; velocity lane; gate lane; pitch lane; octaveRange; PolyBLEP dual-saw; driven Ladder filter; LFO2 sync -> AllResonance (stepped); velocity -> filter cutoff.
- **Rationale:** Directive: fill the DownUp(3) arp-mode coverage hole with an Arp Classic preset — this is my only preset and it is Arp Classic, so it is the target. Sole functional change vs the prior draft: setArpMode switched from kModeUpDown(2) to literal 3 (DownUp), which has no named constant per the inventory (Converge/Diverge/DownUp/Walk/Chord pass literals). DownUp is the exact mirror of UpDown (descends first, then ascends), so the 'Triplet Bounce' name and the bounce character hold perfectly while the suite now instantiates mode 3. All identity work is retained: the SVF LP becomes a DRIVEN Ladder (type=4, ladderDrive 6 dB, resonance 3) for grit distinct from the SVF siblings; envAmount is fixed to a real +26 st pluck; retrigger=2 (Beat), a 0.6/1.0 accent velocity lane, a 0.5/1.2 staccato/legato gate lane and a {0,0,+12,0} pitch lane deliver the rhythmic-articulation brief. Signature gesture (unrepeated by siblings): LFO2 synced to 1/8T -> AllResonance on a STEPPED curve = rhythmic ladder squelch locked to the triplets, reinforced by velocity->cutoff so accents brighten. The Tie-flag (kStepTie) hole is out of scope here — it targets an Arp Acid/Performance preset not in my revise list. Every field/setter verified against ruinae_preset_format.h (fineCents L88, envAmount L188, ladderSlope L190, ladderDrive L191, arp.octaveRange L921, arp.retrigger L929) and the generator constants (kSrcLFO2=2, kDstAllResonance=8, kCurveStepped=3, kVSrcVelocity=5, kVDstFltCut=0, kNote1_8T=9).
- **Replacement code:**
```cpp
    // T026: "Triplet Bounce" — detuned dual-saw through a driven ladder, articulated DownUp triplet arp
    {
        PresetDef p;
        p.name = "Triplet Bounce";
        p.category = "Arp Classic";
        setSynthLead(p.state);            // start from the classic detuned dual-saw, then re-voice it
        // Widen the detune a touch so the bounce has more chorus movement than the flat lead
        p.state.oscB.fineCents = 13.0f;
        // --- Swap the SVF LP for a DRIVEN LADDER: warmer, grittier, distinct from the SVF siblings ---
        p.state.filter.type = 4;          // Ladder
        p.state.filter.cutoffHz = 2400.0f;
        p.state.filter.resonance = 3.0f;  // vocal-ish ladder resonance, self-osc-safe
        p.state.filter.ladderSlope = 4;   // 24 dB/oct
        p.state.filter.ladderDrive = 6.0f;// input drive for saturated grit
        p.state.filter.envAmount = 26.0f; // AUDIBLE +26 st pluck sweep (fixes the near-zero legacy value)
        // Fast filter env for a triplet pluck that re-articulates every step
        p.state.filterEnv.attackMs = 1.0f;
        p.state.filterEnv.decayMs = 130.0f;
        p.state.filterEnv.sustain = 0.3f;
        p.state.filterEnv.releaseMs = 120.0f;
        // Punchy amp so the triplet accents land
        p.state.ampEnv.attackMs = 3.0f;
        p.state.ampEnv.decayMs = 170.0f;
        p.state.ampEnv.sustain = 0.6f;
        p.state.ampEnv.releaseMs = 150.0f;
        // --- Tempo-synced LFO2 (1/8T) drives resonance in STEPPED jumps locked to the triplets ---
        p.state.lfo2.rateHz = 4.0f;       // overridden by sync, kept sane
        p.state.lfo2.shape = 1;           // Triangle
        p.state.lfo2.depth = 1.0f;
        p.state.lfo2.sync = 1;            // tempo-locked
        p.state.lfo2Ext.noteValue = kNote1_8T; // matches the arp rate -> resonance ticks with the bounce
        // --- Arp transport: DownUp @1/8T, RETRIGGER on beat for a locked-in triplet groove ---
        //     DownUp (mode 3, no named constant) fills the arp-mode coverage hole left by the suite.
        setArpEnabled(p.state, true);
        setArpMode(p.state, 3);           // DownUp: descend then ascend (mirror of the UpDown siblings)
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_8T);
        setArpGateLength(p.state, 80.0f);
        setArpSwing(p.state, 0.0f);
        p.state.arp.octaveRange = 2;
        p.state.arp.retrigger = 2;        // Beat: pattern restarts on each beat for tight sync
        // Hard accent lane: alternating 0.6/1.0
        float velAccent8[] = {0.6f, 1.0f, 0.6f, 1.0f, 0.6f, 1.0f, 0.6f, 1.0f};
        setVelocityLane(p.state, 8, velAccent8);
        // Shaping gate lane: staccato/legato alternation carves the triplet rhythm
        float gateShape8[] = {0.5f, 1.2f, 0.5f, 1.2f, 0.5f, 1.2f, 0.5f, 1.2f};
        setGateLane(p.state, 8, gateShape8);
        // Small pitch lane gives the triplet bounce melodic lift the flat siblings lack
        int32_t pitch4[] = {0, 0, 12, 0};
        setPitchLane(p.state, 4, pitch4);
        // --- Mod identity (unique): LFO2 @1/8T -> resonance in STEPPED jumps = rhythmic ladder squelch;
        //     plus velocity opening the cutoff so the 0.6/1.0 accents also brighten ---
        setModSlot(p.state, 0, kSrcLFO2, kDstAllResonance, 0.4f, kCurveStepped);
        setVoiceRoute(p.state, 0, kVSrcVelocity, kVDstFltCut, 0.4f);
        presets.push_back(std::move(p));
    }
```
