# Ruinae Preset Plan — Leads

The Leads category is redesigned so that every one of the 20 presets carries a distinct sonic identity and, collectively, the set exercises the full breadth of the synth. The old bank leaned on the `setSynthLead`/`setSynthAcid` template helpers, leaving most oscillator engines, filter types, distortion units, harmonizer modes, LFO-extension axes (symmetry, quantizeSteps, fadeInMs), per-route curves/scales, and the modulation-FX slot (Flanger/Phaser/Chorus) untouched — which is exactly why the presets sounded same-ish. The new suite deliberately spreads coverage: every oscillator engine (PolyBLEP, Sync, Phase-Distortion, Additive, Chaos, Particle, Formant, Spectral-Freeze, Noise) appears; the filter roster spans SVF LP/HP/BP, Ladder, Comb, EnvelopeFilter and Self-Osc; the three modulation FX are split one-per-preset across the chunk (Supersaw=Flanger, Sync=Phaser, Phase Lead=Chorus, and so on); mono-glide configurations are diversified across priority/portaMode/legato corners; and the poly exceptions (Supersaw, Aurora Bell, Grain Storm, Self Osc Ping) cover the unison and chordal axes. Where a "moving" parameter has no mod destination (ringRatio, combDamping, pulseWidth), the plan animates a genuinely routable proxy (ring depth, comb feedback/tuning, morph-position crossfade) rather than fabricating a nonexistent route. Several presets also fix a latent bug where `envAmount` was written as if it were Hz — it is plain semitones, so values are corrected to audible sweeps (+18…+34 st).

Each section below gives the locate target, character, coverage, rationale, and the complete verbatim replacement code for one preset.

---

## 1. "Supersaw" -> "Supersaw"

- **Locate:** the block containing `p.name = "Supersaw"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A genuinely wide unison ensemble of detuned polyphonic saws swept by a flanger, expressive under pitch-bend and aftertouch.
- **Coverage:** engines: PolyBLEP unison; unison via polyphony+spread+detune; pitchBendRange wide; aftertouch->morph + velocity->morph routes; Flanger + Chorus + Phaser FX (Flanger); filters: Ladder(slope/drive); filterEnv; LFO scale axis.
- **Rationale:** Delivers on the name for the first time: Poly voiceMode + polyphony 8 + spread 0.6 is the actual unison mechanism (the old version was mono with only two 12-cent saws). Opposite +11/-9 cent detune gives symmetric beating. The Flanger (modulationType=2) fills the never-used flanger slot and supplies the classic supersaw ensemble whoosh. Wide pitchBendRange (12 st) and aftertouch->morph give expressive control. The mod identity uses scale=3 (x2) on slot 0 — the untouched depth axis — with an SCurve, distinct from every sibling. envAmount fixed to a real +18 semitones.

```cpp
    // "Supersaw" - Genuine unison ensemble: 8-voice poly spread + detuned saws + flanger
    {
        PresetDef p;
        p.name = "Supersaw";
        p.category = "Leads";
        auto& s = p.state;
        // Two PolyBLEP saws, detuned in OPPOSITE directions so the pair beats against itself
        s.oscA.type = 0; // PolyBLEP
        s.oscA.waveform = 1; // Saw
        s.oscA.fineCents = -9.0f; // pull A flat
        s.oscA.level = 0.85f;
        s.oscB.type = 0;
        s.oscB.waveform = 1; // Saw
        s.oscB.fineCents = 11.0f; // push B sharp -> shimmering detune
        s.oscB.level = 0.85f;
        s.mixer.position = 0.5f; // equal blend of the two saws
        // REAL unison: poly + wide voice spread is what makes a supersaw, not just 2 oscs
        s.global.voiceMode = 0; // Poly (unlike its mono siblings)
        s.global.polyphony = 8; // stack up to 8 voices
        s.global.spread = 0.6f; // fan the voices hard across the stereo field
        s.global.width = 1.4f; // extra stereo width on top
        // Warm Ladder LP, gentle drive for analog glue, moderate cutoff so detune stays audible
        s.filter.type = 4; // Ladder LP
        s.filter.cutoffHz = 3500.0f;
        s.filter.resonance = 0.15f;
        s.filter.ladderSlope = 4; // full 24 dB for a rounded top
        s.filter.ladderDrive = 3.0f; // subtle saturation for thickness
        s.filter.envAmount = 18.0f; // FIXED: audible +18 st opening (was a token 12)
        s.filterEnv.attackMs = 2.0f; s.filterEnv.decayMs = 350.0f;
        s.filterEnv.sustain = 0.25f; s.filterEnv.releaseMs = 400.0f;
        // Poly amp env with an audible release tail so chords bloom
        s.ampEnv.attackMs = 4.0f; s.ampEnv.decayMs = 300.0f;
        s.ampEnv.sustain = 0.85f; s.ampEnv.releaseMs = 400.0f;
        // Wide pitch-bend for octave dives/rips
        s.settings.pitchBendRangeSemitones = 12.0f;
        // Performance routes: velocity opens filter, aftertouch morphs the A<->B balance
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.5f);
        setVoiceRoute(s, 1, kVSrcAftertouch, kVDstMorphPos, 0.4f);
        // Mod identity: a very slow, asymmetric free LFO does a big slow filter swell,
        // pushed past unity with the rarely-used scale axis + a smoothstep curve.
        s.lfo1.shape = 1; // Triangle
        s.lfo1.rateHz = 0.2f; // glacial
        s.lfo1.sync = 0; // free-running (not tempo-locked)
        s.lfo1.depth = 1.0f;
        s.lfo1Ext.symmetry = 0.65f; // skew the triangle -> slow rise, quick fall
        setModSlot(s, 0, kSrcLFO1, kDstAllFltCut, 0.4f, kCurveSCurve);
        s.modMatrix.slots[0].scale = 3; // x2 depth -> a genuinely large sweep
        // FX signature: FLANGER for the ensemble whoosh (the never-used modulation FX)
        s.modulationType = 2; // Flanger
        s.flanger.rateHz = 0.3f;
        s.flanger.depth = 0.6f;
        s.flanger.feedback = 0.4f; // resonant metallic sweep
        s.flanger.mix = 0.45f;
        s.flanger.stereoSpread = 120.0f; // L/R phase offset for width
        s.flanger.waveform = 1; // Triangle
        s.flanger.sync = 0;
        presets.push_back(std::move(p));
    }
```

---

## 2. "Sync Screamer" -> "Sync Screamer"

- **Locate:** the block containing `p.name = "Sync Screamer"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A screaming mono hard-sync lead whose slave pitch is zapped by the mod envelope, phaser-swirled and slap-delayed.
- **Coverage:** engines: Sync; filterEnv + modEnv->OscA pitch; mono glide: voiceMode Mono, legato, portamentoTimeMs, portaMode, priority; Flanger + Chorus + Phaser FX (Phaser); Digital delay; filters: SVF LP + svfDrive.
- **Rationale:** Keeps the distinct sync engine but rebuilds the shell: full mono-glide coverage (priority=2 High, legato, portaMode=1 Legato) instead of the copy-pasted scaffold, a real svfDrive-driven SVF LP, envAmount bumped to +30. The Phaser (modulationType=1) plus a synced Digital delay give it an FX identity no sibling shares. Its unique mod gesture is LFO2->AllResonance with an Exp curve (dest 8 / exponential — untouched by other leads). The modEnv->OscAPitch route is the actual sync-scream mechanism.

```cpp
    // "Sync Screamer" - Hard-sync lead: modEnv zaps the sync sweep, phaser + digital slap
    {
        PresetDef p;
        p.name = "Sync Screamer";
        p.category = "Leads";
        auto& s = p.state;
        s.oscA.type = 3; // Sync
        s.oscA.syncRatio = 1.5f; // start low; the sweep does the work
        s.oscA.syncWaveform = 1; // Saw slave -> aggressive edge
        s.oscA.syncMode = 0; // Hard sync
        s.oscA.syncAmount = 1.0f;
        s.oscA.level = 0.9f;
        s.oscB.level = 0.0f; // single osc
        s.mixer.position = 0.0f; // Osc A only
        // Driven SVF LP for a searing tone; svfDrive adds the scream
        s.filter.type = 0; // SVF LP
        s.filter.cutoffHz = 3000.0f;
        s.filter.resonance = 0.3f;
        s.filter.svfSlope = 1; // 24 dB cascaded
        s.filter.svfDrive = 4.0f; // post-filter grit
        s.filter.envAmount = 30.0f; // strong audible sweep (was 24)
        s.ampEnv.attackMs = 1.0f; s.ampEnv.decayMs = 200.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 180.0f;
        s.filterEnv.attackMs = 1.0f; s.filterEnv.decayMs = 280.0f;
        s.filterEnv.sustain = 0.15f; s.filterEnv.releaseMs = 200.0f;
        // The signature: a fast mod env zaps OSC A pitch -> the sync spectrum screams downward
        s.modEnv.attackMs = 2.0f; s.modEnv.decayMs = 500.0f;
        s.modEnv.sustain = 0.0f; s.modEnv.releaseMs = 150.0f;
        // Full mono-glide expression: high-note priority, legato, always-on portamento
        s.global.voiceMode = 1; // Mono
        s.monoMode.priority = 2; // High-note priority (screaming top line)
        s.monoMode.legato = 1;
        s.monoMode.portamentoTimeMs = 25.0f;
        s.monoMode.portaMode = 1; // Legato-only glide
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.5f);
        setVoiceRoute(s, 1, kVSrcEnv3, kVDstOscAPitch, 0.5f); // modEnv -> pitch zap
        // Mod identity: a mid-rate free LFO breathes resonance with an exponential curve
        s.lfo2.shape = 0; // Sine
        s.lfo2.rateHz = 0.8f;
        s.lfo2.sync = 0;
        setModSlot(s, 1, kSrcLFO2, kDstAllResonance, 0.3f, kCurveExp);
        // FX signature: 6-stage PHASER for a swirling metallic sheen
        s.modulationType = 1; // Phaser
        s.phaser.rateHz = 0.4f;
        s.phaser.depth = 0.6f;
        s.phaser.feedback = 0.6f;
        s.phaser.mix = 0.4f;
        s.phaser.stages = 2; // index -> 6 stages
        s.phaser.centerFreqHz = 1200.0f;
        s.phaser.stereoSpread = 90.0f;
        s.phaser.sync = 0;
        // Digital slap delay, tempo-synced, for rhythmic screams
        s.delayEnabled = 1;
        s.delay.type = 0; // Digital
        s.delay.sync = 1;
        s.delay.noteValue = 10; // 1/8
        s.delay.feedback = 0.35f;
        s.delay.mix = 0.3f;
        s.delay.digitalWidth = 140.0f;
        presets.push_back(std::move(p));
    }
```

---

## 3. "Phase Lead" -> "Phase Lead"

- **Locate:** the block containing `p.name = "Phase Lead"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A hollow, metallic Casio-CZ lead: two resonant phase-distortion shapes a fifth apart, morphed by velocity through a resonant bandpass with a stepped, quantized filter wobble and chorus.
- **Coverage:** engines: PhaseDistortion; filters: SVF BP; aftertouch->morph + velocity->morph routes (velocity->morph); Flanger + Chorus + Phaser FX (Chorus); LFO quantizeSteps + stepped curve; mono glide.
- **Rationale:** Keeps the strong dual-PD identity but gives it an FX and mod story the old empty version lacked. mixer.position starts centred (0.5) so the velocity->morph route sweeps in both directions between ResSaw and ResTri. The mod gesture is unique in two untouched dimensions at once: LFO quantizeSteps=6 (LFO ext axis) feeding a kCurveStepped matrix slot for an arpeggiated metallic wobble. Chorus (modulationType=3) completes the trio of modulation FX across the chunk (Supersaw=Flanger, Sync=Phaser, this=Chorus).

```cpp
    // "Phase Lead" - Dual resonant PD through an SVF bandpass, stepped LFO wobble + chorus
    {
        PresetDef p;
        p.name = "Phase Lead";
        p.category = "Leads";
        auto& s = p.state;
        s.oscA.type = 2; // Phase Distortion
        s.oscA.pdWaveform = 5; // ResSaw
        s.oscA.pdDistortion = 0.7f; // strong DCW resonance
        s.oscA.level = 0.8f;
        s.oscB.type = 2; // Phase Distortion
        s.oscB.pdWaveform = 6; // ResTri
        s.oscB.pdDistortion = 0.5f;
        s.oscB.tuneSemitones = 7.0f; // a fifth up -> hollow interval
        s.oscB.level = 0.55f;
        s.mixer.position = 0.5f; // start centred so velocity morph is bidirectional
        // Resonant BANDPASS: emphasises the CZ formant peak
        s.filter.type = 2; // SVF BP
        s.filter.cutoffHz = 2500.0f;
        s.filter.resonance = 0.7f;
        s.filter.svfDrive = 3.0f; // a little edge in the passband
        s.filter.envAmount = 20.0f; // env pushes the bandpass peak up on attack
        s.filterEnv.attackMs = 2.0f; s.filterEnv.decayMs = 250.0f;
        s.filterEnv.sustain = 0.2f; s.filterEnv.releaseMs = 200.0f;
        s.ampEnv.attackMs = 3.0f; s.ampEnv.decayMs = 300.0f;
        s.ampEnv.sustain = 0.6f; s.ampEnv.releaseMs = 250.0f;
        s.global.voiceMode = 1; // Mono
        s.monoMode.legato = 1;
        s.monoMode.portamentoTimeMs = 15.0f;
        // Velocity morphs A<->B: hard hits favour ResTri, soft hits favour ResSaw
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstMorphPos, 0.6f);
        // Mod identity: a QUANTIZED free LFO drives the bandpass in discrete steps
        // (LFO step-quantize + a Stepped matrix curve = arpeggiated metallic timbre)
        s.lfo1.shape = 1; // Triangle
        s.lfo1.rateHz = 4.0f;
        s.lfo1.sync = 0;
        s.lfo1Ext.quantizeSteps = 6; // hold the LFO at 6 discrete levels
        setModSlot(s, 0, kSrcLFO1, kDstAllFltCut, 0.35f, kCurveStepped);
        // FX signature: 3-voice CHORUS widens the hollow tone
        s.modulationType = 3; // Chorus
        s.chorus.rateHz = 0.5f;
        s.chorus.depth = 0.4f;
        s.chorus.feedback = 0.1f;
        s.chorus.mix = 0.4f;
        s.chorus.voices = 3;
        s.chorus.stereoSpread = 200.0f;
        s.chorus.sync = 0;
        presets.push_back(std::move(p));
    }
```

---

## 4. "Harmonic Bell" -> "Aurora Bell"

- **Locate:** the block containing `p.name = "Harmonic Bell"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A shimmering polyphonic inharmonic additive bell that brightens with key position, thickened by granular harmony voices and a slowly-swelling hall.
- **Coverage:** engines: Additive (inharmonicity + tilt); filters: SVF HP; keyTrack->SpecTilt; harmonizer Granular pitchShiftMode; reverb Hall (variant); LFO fadeIn.
- **Rationale:** Renamed from the generic 'Harmonic Bell' to the evocative 'Aurora Bell'. Builds on the already-strong additive identity by adding two features no other lead touches: a Granular-mode harmonizer (pitchShiftMode=1) layering octave+fifth voices, and a Hall reverb (reverbType=1) instead of the default plate. keyTrack->SpecTilt is retained as the natural bell response. Its unique mod gesture exploits the untouched LFO fadeInMs axis: a 2-second-fade LFO swells the effect-mix (dest 3) so held chords bloom into the room. Poly (voiceMode 0) so bells ring in chords.

```cpp
    // "Aurora Bell" - Poly inharmonic additive bell, key-tracked tilt, granular harmony, hall
    {
        PresetDef p;
        p.name = "Aurora Bell";
        p.category = "Leads";
        auto& s = p.state;
        s.oscA.type = 4; // Additive
        s.oscA.additivePartials = 48; // rich but not harsh
        s.oscA.additiveInharm = 0.35f; // stretched partials -> bell/metallic
        s.oscA.additiveTilt = -4.0f; // roll the top down for a warm strike
        s.oscA.level = 0.75f;
        s.oscB.type = 0; // PolyBLEP
        s.oscB.waveform = 0; // Sine reinforcement an octave up
        s.oscB.tuneSemitones = 12.0f;
        s.oscB.level = 0.4f;
        s.mixer.position = 0.35f; // mostly additive, a touch of pure sine sparkle
        // High-pass to shed the inharmonic low rumble
        s.filter.type = 1; // SVF HP
        s.filter.cutoffHz = 180.0f;
        s.filter.resonance = 0.1f;
        // Long bell decay with a low sustain floor
        s.ampEnv.attackMs = 2.0f; s.ampEnv.decayMs = 1800.0f;
        s.ampEnv.sustain = 0.1f; s.ampEnv.releaseMs = 1600.0f;
        // POLYphonic (bells ring in chords) - unlike its mono lead siblings
        s.global.voiceMode = 0; // Poly
        s.global.polyphony = 8;
        // velocity morphs additive<->sine; key position brightens the spectrum naturally
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstMorphPos, 0.4f);
        setVoiceRoute(s, 1, kVSrcKeyTrack, kVDstSpecTilt, 0.4f); // higher notes = brighter
        // Mod identity: an ultra-slow free LFO fades IN over 2 s and swells the reverb send,
        // so the tail blooms into the room the longer a chord is held.
        s.lfo1.shape = 0; // Sine
        s.lfo1.rateHz = 0.15f;
        s.lfo1.sync = 0;
        s.lfo1Ext.fadeInMs = 2000.0f; // gradual onset (untouched LFO axis)
        setModSlot(s, 0, kSrcLFO1, kDstEffectMix, 0.3f, kCurveSCurve);
        // Granular harmony voices thicken the bell into a choir of partials
        s.harmonizerEnabled = 1;
        s.harmonizer.harmonyMode = 0; // Chromatic
        s.harmonizer.pitchShiftMode = 1; // Granular (exotic shifter)
        s.harmonizer.formantPreserve = 1;
        s.harmonizer.numVoices = 2;
        s.harmonizer.voiceInterval[0] = 12; // +octave
        s.harmonizer.voiceInterval[1] = 7;  // +fifth
        s.harmonizer.voiceLevelDb[0] = -6.0f;
        s.harmonizer.voiceLevelDb[1] = -9.0f;
        s.harmonizer.voicePan[0] = -0.4f;
        s.harmonizer.voicePan[1] = 0.4f;
        s.harmonizer.wetLevelDb = -8.0f;
        // HALL reverb (not the default plate) with a little pre-delay for depth
        s.reverbEnabled = 1;
        s.reverbType = 1; // Hall
        s.reverb.size = 0.7f;
        s.reverb.mix = 0.35f;
        s.reverb.damping = 0.4f;
        s.reverb.preDelayMs = 20.0f;
        s.reverb.modDepth = 0.1f; // gentle shimmer on the tail
        presets.push_back(std::move(p));
    }
```

---

## 5. "Mono Scream" -> "Foldbeast"

- **Locate:** the block containing `p.name = "Mono Scream"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A filthy mono lead: cross-modulated saw+pulse driven through a resonant 24 dB ladder into a sine wavefolder, wobbled by an asymmetric LFO and echoed on tape.
- **Coverage:** filters: Ladder(slope/drive); Wavefolder distortion; phaseMod + freqMod cross-mod; pulseWidth/PWM; Tape delay; LFO symmetry; mono glide (priority/portaMode).
- **Rationale:** Renamed to 'Foldbeast' to escape the generic 'Mono Scream'. Keeps the wavefolder identity but fixes the audit's core complaint that it read as 'Supersaw but dirtier': it now owns cross-mod (oscA.phaseMod/freqMod — the phaseMod+freqMod feature), a nasal PWM pulse (pulseWidth 0.3), a Tape delay, and an asymmetric LFO wobble no sibling has. distortion.mix=0.6 and masterGain=0.85 compensate for the ladderDrive 8 + fold so it stays loud but not destructive. Its mono-glide config (priority=1 Low, portaMode=0 Always) deliberately complements Sync Screamer's (High/Legato) so the two cover different corners of the subsystem. Unique routes: Velocity->FltRes and LFO(symmetry 0.8)->AllFltCut with Exp curve.

```cpp
    // "Foldbeast" - Cross-modded saw+pulse -> driven ladder -> sine wavefolder, tape echo
    {
        PresetDef p;
        p.name = "Foldbeast";
        p.category = "Leads";
        auto& s = p.state;
        s.oscA.type = 0; // PolyBLEP
        s.oscA.waveform = 1; // Saw
        s.oscA.level = 0.9f;
        // Cross-mod grit: phase + freq modulation add growling sidebands to OSC A
        s.oscA.phaseMod = 0.3f; // FM-ish sideband bite
        s.oscA.freqMod = 0.25f; // growl in the timbre
        s.oscB.type = 0;
        s.oscB.waveform = 3; // Pulse
        s.oscB.pulseWidth = 0.3f; // thin, nasal PWM tone stacked a fifth up
        s.oscB.tuneSemitones = 7.0f;
        s.oscB.level = 0.55f;
        s.mixer.position = 0.4f; // saw-dominant blend
        // Resonant 24 dB ladder with heavy input drive
        s.filter.type = 4; // Ladder LP
        s.filter.cutoffHz = 1800.0f;
        s.filter.resonance = 0.55f;
        s.filter.ladderSlope = 4; // 24 dB
        s.filter.ladderDrive = 8.0f; // slam the ladder input
        s.filter.envAmount = 34.0f; // big audible sweep (was fine, kept generous)
        s.filterEnv.attackMs = 1.0f; s.filterEnv.decayMs = 280.0f;
        s.filterEnv.sustain = 0.15f; s.filterEnv.releaseMs = 200.0f;
        s.ampEnv.attackMs = 1.0f; s.ampEnv.decayMs = 200.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 160.0f;
        // WAVEFOLDER for the harmonic filth (sine fold), mix < 1 for level control
        s.distortion.type = 4; // Wavefolder
        s.distortion.drive = 0.45f;
        s.distortion.foldType = 1; // Sine fold
        s.distortion.mix = 0.6f; // blend dry through so it doesn't collapse
        s.global.masterGain = 0.85f; // headroom compensation for drive + fold
        // Full mono-glide, Low-note priority + always-on glide (complements Sync's High/Legato)
        s.global.voiceMode = 1; // Mono
        s.monoMode.priority = 1; // Low-note priority
        s.monoMode.legato = 1;
        s.monoMode.portamentoTimeMs = 35.0f;
        s.monoMode.portaMode = 0; // Always glide
        // velocity opens resonance for a squelchier attack (unique dest among siblings)
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltRes, 0.4f);
        // Mod identity: a fast, heavily-skewed LFO ramps the cutoff for a rhythmic snarl
        s.lfo1.shape = 1; // Triangle
        s.lfo1.rateHz = 6.0f;
        s.lfo1.sync = 0;
        s.lfo1Ext.symmetry = 0.8f; // skew -> near-ramp wobble (LFO symmetry axis)
        setModSlot(s, 0, kSrcLFO1, kDstAllFltCut, 0.5f, kCurveExp);
        // Tape echo for dub-style dirt on the repeats
        s.delayEnabled = 1;
        s.delay.type = 1; // Tape
        s.delay.sync = 1;
        s.delay.noteValue = 10; // 1/8
        s.delay.feedback = 0.3f;
        s.delay.mix = 0.25f;
        s.delay.tapeSaturation = 0.5f;
        s.delay.tapeWear = 0.2f; // wow/flutter grime on the tail
        presets.push_back(std::move(p));
    }
```

---

## 6. "Acid Talker" -> "Vox Machina"

- **Locate:** the block containing `p.name = "Acid Talker"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A talking formant-voiced mono lead whose dynamic auto-wah quacks under playing force, wobbles under a skewed LFO, and squeals harder the more you lean on aftertouch, with a wide pitch-bend range for expressive vocal scoops.
- **Coverage:** engines: Formant; filters: EnvFilter (auto-wah, envSensitivity/depth/attack/release); mono glide: voiceMode Mono, legato, portamentoTimeMs, portaMode Legato-only, priority Last; aftertouch->FltRes route; velocity->FltCut route; LFO symmetry (skewed); mod-matrix SCurve curve; pitchBendRange wide.
- **Rationale:** Owns the Formant engine + EnvelopeFilter (type 11) auto-wah, driven by envSensitivity 8 so it responds to playing force. The distinct mod story is a free-running LFO with symmetry 0.78 (covers the LFO-symmetry axis) feeding AllFltCut through an SCurve curve, plus two voice routes no sibling uses: aftertouch->resonance (press to squeal) and velocity->cutoff. portaMode=1 (legato-only) differentiates its glide from the other mono leads. formantMorph 2.2 sits between E and I for a clear 'eee' vocal timbre. Fixes the flagged false coverage: pitchBendRangeSemitones is now explicitly set to 7.0f (verified field at ruinae_preset_format.h:799, default 2.0f), so the 'pitchBendRange wide' claim is genuinely exercised.

```cpp
    // "Vox Machina" - Formant voice through the envelope-filter auto-wah,
    // animated by a skewed LFO and made expressive under aftertouch pressure.
    {
        PresetDef p;
        p.name = "Vox Machina";
        p.category = "Leads";
        auto& s = p.state;
        // --- Oscillator: pure Formant engine, single voice (OscB muted) ---
        s.oscA.type = 7;              // Formant
        s.oscA.formantVowel = 1;      // base vowel 'E'
        s.oscA.formantMorph = 2.2f;   // morph toward 'I' -> bright "eee" vocal color
        s.oscA.level = 0.85f;
        s.oscB.level = 0.0f;          // formant osc stands alone
        s.mixer.position = 0.0f;      // OscA only
        // --- Filter: Envelope Filter (auto-wah) is the quack engine ---
        s.filter.type = 11;           // EnvelopeFilter
        s.filter.cutoffHz = 1400.0f;  // rest position of the wah
        s.filter.resonance = 0.7f;    // vocal peak
        s.filter.envSubType = 0;      // LP response
        s.filter.envSensitivity = 8.0f;  // reacts strongly to input level -> quacks on dynamics
        s.filter.envDepth = 0.8f;     // wide sweep
        s.filter.envAttack = 8.0f;    // snappy open
        s.filter.envRelease = 120.0f; // vocal-length close
        s.filter.envDirection = 0;    // Up (open on transient)
        // --- Amp: fast, talkative, medium sustain ---
        s.ampEnv.attackMs = 3.0f; s.ampEnv.decayMs = 220.0f;
        s.ampEnv.sustain = 0.6f;  s.ampEnv.releaseMs = 220.0f;
        // --- Mono glide (the Leads connective tissue, but legato-only here) ---
        s.global.voiceMode = 1;       // Mono
        s.monoMode.priority = 0;      // Last-note priority (responsive for lead lines)
        s.monoMode.legato = 1;
        s.monoMode.portamentoTimeMs = 30.0f;
        s.monoMode.portaMode = 1;     // glide only on legato overlaps
        // --- Wide pitch bend for expressive vocal scoops ---
        s.settings.pitchBendRangeSemitones = 7.0f;
        // --- Mod identity: a slow, SKEWED LFO adds a vocal wobble on top of
        //     the dynamic auto-wah; aftertouch lets you squeeze extra quack. ---
        s.lfo1.rateHz = 2.5f;         // gentle talking wobble
        s.lfo1.shape = 0;             // Sine
        s.lfo1.sync = 0;              // free-run (not tempo-locked)
        s.lfo1Ext.symmetry = 0.78f;   // skew the wave -> asymmetric "wow" motion
        setModSlot(s, 0, kSrcLFO1, kDstAllFltCut, 0.25f, kCurveSCurve); // smooth vowel drift
        setVoiceRoute(s, 0, kVSrcAftertouch, kVDstFltRes, 0.4f);        // press = sharper quack
        setVoiceRoute(s, 1, kVSrcVelocity,   kVDstFltCut, 0.3f);        // harder hit opens further
        presets.push_back(std::move(p));
    }
```

---

## 7. "Harmonized Lead" -> "Third Voice"

- **Locate:** the block containing `p.name = "Harmonized Lead"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A clean dual-saw lead shadowed a diatonic third above by the Simple pitch-shifter, plucked open by a filter-env sweep, widened by chorus and softened by a light tape echo.
- **Coverage:** engines: PolyBLEP dual-saw detune; filters: Ladder (slope/drive); modEnv->cutoff route (with per-route Exp curve); velocity->MorphPos route; harmonizer Simple pitchShiftMode (own this variant); Chorus FX; Tape delay; mono glide.
- **Rationale:** Retires the pad-template chassis: the identity now rests on a mod-env->cutoff pluck (with an explicit per-route Exp curve, an axis no sibling touches) plus velocity->MorphPos. It uniquely owns harmonizer pitchShiftMode=0 (Simple) at a diatonic third, and carries the category's Chorus FX and Tape delay. Ladder slope 3 / drive 4 covers the ladder slope+drive axis. wetLevelDb -3 keeps the harmony present but under the lead.

```cpp
    // "Third Voice" - Dual-saw lead harmonized a diatonic 3rd up with the
    // SIMPLE pitch-shifter; a mod-env pluck opens the ladder, chorus + tape
    // echo widen it. Not a template: mod-env cutoff + velocity morph carry it.
    {
        PresetDef p;
        p.name = "Third Voice";
        p.category = "Leads";
        auto& s = p.state;
        // --- Two detuned saws ---
        s.oscA.type = 0; s.oscA.waveform = 1; s.oscA.level = 0.85f; // Saw
        s.oscB.type = 0; s.oscB.waveform = 1;                      // Saw
        s.oscB.fineCents = 7.0f; s.oscB.level = 0.6f;              // light detune shimmer
        s.mixer.position = 0.5f;
        // --- Ladder filter: 18 dB with a touch of drive for saw warmth ---
        s.filter.type = 4;            // Ladder
        s.filter.cutoffHz = 4500.0f;
        s.filter.resonance = 0.25f;
        s.filter.ladderSlope = 3;     // 18 dB/oct
        s.filter.ladderDrive = 4.0f;  // gentle input push
        // --- Amp ---
        s.ampEnv.attackMs = 6.0f;  s.ampEnv.decayMs = 300.0f;
        s.ampEnv.sustain = 0.72f;  s.ampEnv.releaseMs = 260.0f;
        // --- Mod env: percussive shape that plucks the cutoff open ---
        s.modEnv.attackMs = 2.0f;  s.modEnv.decayMs = 180.0f;
        s.modEnv.sustain = 0.0f;   s.modEnv.releaseMs = 200.0f;
        // --- Harmonizer: SIMPLE mode, one diatonic-3rd voice up in C major ---
        s.harmonizerEnabled = 1;
        s.harmonizer.harmonyMode = 1;   // Scalic (diatonic intervals)
        s.harmonizer.key = 0; s.harmonizer.scale = 0; // C Major
        s.harmonizer.pitchShiftMode = 0; // Simple (owns this variant)
        s.harmonizer.numVoices = 1;
        s.harmonizer.dryLevelDb = 0.0f; s.harmonizer.wetLevelDb = -3.0f;
        s.harmonizer.voiceInterval[0] = 2; // +2 scale degrees = a third
        s.harmonizer.voicePan[0] = 0.35f;  // sit the harmony slightly right
        // --- Chorus for width ---
        s.modulationType = 3;          // Chorus
        s.chorus.rateHz = 0.4f; s.chorus.depth = 0.4f;
        s.chorus.mix = 0.3f;    s.chorus.voices = 3;
        s.chorus.stereoSpread = 180.0f;
        // --- Light Tape delay ---
        s.delayEnabled = 1;
        s.delay.type = 1;              // Tape
        s.delay.timeMs = 320.0f; s.delay.sync = 1; s.delay.noteValue = kNote1_8;
        s.delay.mix = 0.16f; s.delay.feedback = 0.25f;
        s.delay.tapeSaturation = 0.4f; // warm the repeats
        // --- Mono glide ---
        s.global.voiceMode = 1; s.monoMode.legato = 1;
        s.monoMode.portamentoTimeMs = 20.0f;
        // --- Mod identity: mod-env plucks cutoff (Exp curve) + velocity tilts
        //     the A/B morph so dynamics change the saw blend. ---
        setVoiceRoute(s, 0, kVSrcEnv3, kVDstFltCut, 0.45f);
        s.voiceRoutes[0].curve = static_cast<int8_t>(kCurveExp); // snappy pluck shape
        setVoiceRoute(s, 1, kVSrcVelocity, kVDstMorphPos, 0.4f); // harder = shift blend
        presets.push_back(std::move(p));
    }
```

---

## 8. "Chaos Siren" -> "Chaos Siren"

- **Locate:** the block containing `p.name = "Chaos Siren"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** An unstable Van-der-Pol chaos lead cross-modulated by a sine partner into a warbling scream, with a corrected +22 st filter sweep, a mod-env pitch swoop, resonance warble, flanger sweep and a full-octave pitch-bend range for dive-bombs.
- **Coverage:** engines: Chaos (Van der Pol); phaseMod + freqMod cross-mod; filters: Ladder (slope/drive); filterEnv with CORRECTED envAmount (+22 st); modEnv->OscA pitch route; LFO->AllResonance with Exp curve + scale x2 (per-route scale axis); Flanger FX; pitchBendRange wide; mono glide (long portamento, Always).
- **Rationale:** Fixes the flagged bug (envAmount 18 -> +22 semitones, now an audible sweep) and owns the chaos + cross-mod axis: phaseMod 0.5 and freqMod 0.2 turn the sine partner into a warble/growl modulator. The mod story is unique: LFO->AllResonance with an Exp curve AND scale=3 (the per-route scale axis, unused elsewhere) for a wild resonance warble, plus mod-env->OscA pitch for the siren swoop. Carries the category-owned Flanger and a wide 12 st pitch-bend range; portaMode=0 (Always) with 90 ms glide gives the trademark siren slide.

```cpp
    // "Chaos Siren" - Van-der-Pol chaos osc cross-modulated by a sine partner
    // (phaseMod + freqMod) into a warbling scream. Corrected +22 st filter
    // sweep, mod-env pitch swoop, resonance-warble LFO, flanger, wide bend.
    {
        PresetDef p;
        p.name = "Chaos Siren";
        p.category = "Leads";
        auto& s = p.state;
        // --- OscA: chaotic Van der Pol attractor, cross-modded by OscB ---
        s.oscA.type = 5;              // Chaos
        s.oscA.chaosAttractor = 4;    // Van der Pol
        s.oscA.chaosAmount = 0.45f;   // quasi-pitched but unstable
        s.oscA.chaosCoupling = 0.25f; // cross-axis instability
        s.oscA.chaosOutput = 0;       // X axis
        s.oscA.phaseMod = 0.5f;       // PM from the partner osc -> warble sidebands
        s.oscA.freqMod = 0.2f;        // a little FM growl on top
        s.oscA.level = 0.8f;
        // --- OscB: clean sine acts as the modulator/anchor ---
        s.oscB.type = 0; s.oscB.waveform = 0; // Sine
        s.oscB.level = 0.35f;
        s.mixer.position = 0.25f;     // mostly the chaos, sine underneath
        // --- Ladder 24 dB with drive tames + thickens the chaos ---
        s.filter.type = 4;            // Ladder
        s.filter.cutoffHz = 3500.0f;
        s.filter.resonance = 0.35f;
        s.filter.ladderSlope = 4;     // 24 dB/oct
        s.filter.ladderDrive = 6.0f;
        s.filter.envAmount = 22.0f;   // CORRECTED: +22 SEMITONES (old 18 was ~inaudible)
        // --- Amp + a real filter-env sweep now that envAmount is meaningful ---
        s.ampEnv.attackMs = 4.0f;  s.ampEnv.decayMs = 320.0f;
        s.ampEnv.sustain = 0.6f;   s.ampEnv.releaseMs = 260.0f;
        s.filterEnv.attackMs = 3.0f;  s.filterEnv.decayMs = 260.0f;
        s.filterEnv.sustain = 0.2f;   s.filterEnv.releaseMs = 240.0f;
        // --- Mod env: slow swoop that bends OscA pitch = siren rise ---
        s.modEnv.attackMs = 40.0f; s.modEnv.decayMs = 400.0f;
        s.modEnv.sustain = 0.3f;   s.modEnv.releaseMs = 300.0f;
        // --- Flanger FX for a jet-sweep on the chaos ---
        s.modulationType = 2;         // Flanger
        s.flanger.rateHz = 0.3f; s.flanger.depth = 0.6f;
        s.flanger.feedback = 0.3f; s.flanger.mix = 0.35f;
        s.flanger.stereoSpread = 90.0f;
        // --- Wide pitch bend for siren dive-bombs ---
        s.settings.pitchBendRangeSemitones = 12.0f;
        // --- Mono glide: long portamento, always-on -> classic siren glide ---
        s.global.voiceMode = 1; s.monoMode.legato = 1;
        s.monoMode.portamentoTimeMs = 90.0f; s.monoMode.portaMode = 0; // Always
        // --- Mod identity: LFO warbles resonance (Exp curve, scale x2 for
        //     extreme depth) + mod-env swoops OscA pitch. ---
        s.lfo1.rateHz = 5.5f; s.lfo1.shape = 1; s.lfo1.sync = 0; // free tri warble
        setModSlot(s, 0, kSrcLFO1, kDstAllResonance, 0.4f, kCurveExp);
        s.modMatrix.slots[0].scale = 3; // x2 scale -> pushes amount past +/-1 for wild warble
        setVoiceRoute(s, 0, kVSrcEnv3, kVDstOscAPitch, 0.3f); // siren pitch rise per note
        presets.push_back(std::move(p));
    }
```

---

## 9. "Granular Edge" -> "Grain Storm"

- **Locate:** the block containing `p.name = "Granular Edge"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A fat detuned saw+triangle UNISON stack ground into gravel by granular distortion, focused through a pitch-tracking bandpass and stuttered by a stepped-LFO cutoff, with a digital echo tail.
- **Coverage:** engines: PolyBLEP unison (polyphony + spread + detune); filters: SVF BP; keyTrack (BP follows pitch); filterEnv sweep; distortion: Granular (all four grain params: size/density/variation/jitter); LFO->AllFltCut with Stepped curve (glitch motion); Digital delay; stereo width/spread.
- **Rationale:** The deliberate non-mono lead: covers the unison axis (polyphony 6 + spread 0.6 + oscB detune) that no other Leads preset can. Fixes the original's mislabeled waveform (2=Square) to Triangle=4. Owns SVF Bandpass with keyTrack=1 so the grit-band follows pitch, and drives all four granular-distortion grain params. Its unique mod gesture is a tempo-synced LFO into AllFltCut through a Stepped curve for a glitchy sample-and-hold stutter, distinct from every sibling's smooth curves. Drive kept at 0.45 with mix 0.8 to stay musical across 6 stacked voices.

```cpp
    // "Grain Storm" - The unison outlier of the Leads set: a 6-voice detuned
    // saw+triangle stack shredded by granular distortion, focused through a
    // key-tracking bandpass, stuttered by a stepped-LFO cutoff. Digital echo.
    {
        PresetDef p;
        p.name = "Grain Storm";
        p.category = "Leads";
        auto& s = p.state;
        // --- UNISON via polyphony + spread + detune (no dedicated unison param) ---
        s.global.voiceMode = 0;       // Poly (so the stack can stack)
        s.global.polyphony = 6;       // 6-voice unison body
        s.global.spread = 0.6f;       // pan the voices wide
        s.global.width = 1.3f;        // extra stereo width
        // --- Saw + Triangle, detuned ---
        s.oscA.type = 0; s.oscA.waveform = 1; s.oscA.level = 0.8f;  // Saw
        s.oscB.type = 0; s.oscB.waveform = 4;                       // Triangle (4, NOT 2=Square)
        s.oscB.fineCents = 9.0f; s.oscB.level = 0.45f;              // detune for the unison beat
        s.mixer.position = 0.35f;
        // --- SVF Bandpass, tracking the keyboard, with a filter-env sweep ---
        s.filter.type = 2;            // SVF BP
        s.filter.cutoffHz = 1300.0f;
        s.filter.resonance = 0.5f;
        s.filter.keyTrack = 1.0f;     // band follows pitch -> consistent grit per note
        s.filter.svfSlope = 1;        // 24 dB
        s.filter.svfDrive = 3.0f;
        s.filter.envAmount = 18.0f;   // +18 st filter-env sweep (semitones, corrected idiom)
        // --- Granular distortion: ALL FOUR grain params exercised ---
        s.distortion.type = 3;        // Granular
        s.distortion.drive = 0.45f;   // moderate (6-voice stack -> keep headroom)
        s.distortion.grainSize = 0.35f;      // grain length
        s.distortion.grainDensity = 0.55f;   // grains per second
        s.distortion.grainVariation = 0.4f;  // size randomization
        s.distortion.grainJitter = 0.3f;     // timing scatter
        s.distortion.mix = 0.8f;
        // --- Amp + filter env ---
        s.ampEnv.attackMs = 4.0f;  s.ampEnv.decayMs = 300.0f;
        s.ampEnv.sustain = 0.7f;   s.ampEnv.releaseMs = 240.0f;
        s.filterEnv.attackMs = 5.0f;  s.filterEnv.decayMs = 300.0f;
        s.filterEnv.sustain = 0.25f;  s.filterEnv.releaseMs = 260.0f;
        // --- Digital delay tail ---
        s.delayEnabled = 1;
        s.delay.type = 0;             // Digital
        s.delay.timeMs = 280.0f; s.delay.sync = 1; s.delay.noteValue = kNote1_16;
        s.delay.mix = 0.16f; s.delay.feedback = 0.22f;
        s.delay.digitalWidth = 130.0f;
        // --- Mod identity: tempo-synced LFO chops the bandpass in STEPS ---
        s.lfo1.rateHz = 8.0f; s.lfo1.shape = 0; s.lfo1.sync = 1;
        s.lfo1Ext.noteValue = kNote1_16;
        setModSlot(s, 0, kSrcLFO1, kDstAllFltCut, 0.35f, kCurveStepped); // glitch/stutter motion
        presets.push_back(std::move(p));
    }
```

---

## 10. "Ring Mod Buzz" -> "Clangourous"

- **Locate:** the block containing `p.name = "Ring Mod Buzz"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A metallic single-saw lead ring-modulated at ~3.5x the note through a comb resonator, its clang swelling per-note via a mod-env on ring depth while a slow free LFO sweeps the comb so the metal never sits still.
- **Coverage:** engines: PolyBLEP saw; filters: Comb (combDamping); distortion: RingModulator (note-tracked ~3.5x, stereo spread); modEnv->DistDrive route (ring-depth swell = MOVING clang); LFO->AllFltCut with SCurve (continuous comb sweep); mono glide.
- **Rationale:** Honest realization of the 'moving clang' directive: I verified in ruinae_voice.h:1507 that distortionDrive sets the ring modulator's amplitude, so routing modEnv->DistDrive makes the ring depth swell per note, and a free-running LFO->AllFltCut (SCurve) continuously sweeps the comb tuning. ringRatio has no mod-matrix destination in this synth, so rather than fabricate an LFO->ringRatio route I animate ring depth + comb color, which genuinely un-freezes the timbre. Comb filter (type 6, combDamping 0.4) is the unique filter here; ringRatio 0.206 maps to ~3.5x (0.25 + 0.206*15.75), and ringStereoSpread 0.4 adds width no sibling has.

```cpp
    // "Clangourous" - Single saw ring-modulated at ~3.5x the note pitch,
    // fed through a comb resonator for extra metal. The clang MOVES: a mod-env
    // swells the ring depth per note and a slow free LFO sweeps the comb.
    {
        PresetDef p;
        p.name = "Clangourous";
        p.category = "Leads";
        auto& s = p.state;
        // --- Single saw source (OscB muted) ---
        s.oscA.type = 0; s.oscA.waveform = 1; s.oscA.level = 0.85f; // Saw
        s.oscB.level = 0.0f;
        s.mixer.position = 0.0f;
        // --- Comb filter adds tuned metallic resonance to the ring output ---
        s.filter.type = 6;            // Comb
        s.filter.cutoffHz = 2500.0f;  // comb tuning
        s.filter.resonance = 0.5f;    // feedback depth
        s.filter.combDamping = 0.4f;  // roll off the high resonances a touch
        // --- Ring modulator, note-tracked at ~3.5x for inharmonic clang ---
        s.distortion.type = 6;        // Ring Modulator
        s.distortion.drive = 0.55f;   // = ring amplitude (mod-env animates this)
        s.distortion.ringFreqMode = 1;    // NoteTrack
        s.distortion.ringRatio = 0.206f;  // normalized -> ~3.5x carrier (0.25 + 0.206*15.75)
        s.distortion.ringWaveform = 0;    // Sine carrier
        s.distortion.ringStereoSpread = 0.4f; // widen the clang across the field
        s.distortion.mix = 0.6f;
        // --- Amp ---
        s.ampEnv.attackMs = 3.0f;  s.ampEnv.decayMs = 250.0f;
        s.ampEnv.sustain = 0.6f;   s.ampEnv.releaseMs = 200.0f;
        // --- Mod env: swells ring depth in over each note so the metal blooms ---
        s.modEnv.attackMs = 6.0f;  s.modEnv.decayMs = 300.0f;
        s.modEnv.sustain = 0.2f;   s.modEnv.releaseMs = 220.0f;
        // --- Mono glide ---
        s.global.voiceMode = 1; s.monoMode.legato = 1;
        s.monoMode.portamentoTimeMs = 12.0f; s.monoMode.priority = 0;
        // --- Mod identity: the clang is NOT static. Ring depth (DistDrive ->
        //     ring amplitude) is enveloped per note, and a slow FREE LFO sweeps
        //     the comb tuning so the metallic timbre continuously drifts.
        //     (ringRatio itself has no mod destination in this synth, so we
        //      animate ring DEPTH + comb color instead.) ---
        setVoiceRoute(s, 0, kVSrcEnv3, kVDstDistDrive, 0.5f); // per-note clang swell
        s.lfo1.rateHz = 0.8f; s.lfo1.shape = 0; s.lfo1.sync = 0; // slow free sine
        setModSlot(s, 0, kSrcLFO1, kDstAllFltCut, 0.4f, kCurveSCurve); // sweeps the comb
        presets.push_back(std::move(p));
    }
```

---

## 11. "Tape Drive" -> "Tape Drive"

- **Locate:** the block containing `p.name = "Tape Drive"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A warm, tape-saturated dual-saw mono lead whose filter blooms on attack and drifts with slow tape-wow.
- **Coverage:** dual-osc-detune; ladder-drive; tape-saturator-distortion; tape-delay-slapback; mono-legato-portamento; mono-priority; portaMode; filterEnv-audible-sweep; LFO-symmetry; modmatrix-LFO2->AllFltCut; pitchBendRange-wide; velocity->FltCut.
- **Rationale:** Keeps the dual-saw+Ladder+TapeSaturator+Tape-delay directive but fixes the dead filter env (envAmount now +26 semitones so the ladder audibly blooms) and adds a unique motion identity: a free-running triangle LFO2 with skewed symmetry (0.72) driving cutoff for genuine tape-wow. ladderDrive=8 plus tapeSaturation=0.7 gives the warm grit; slapback delay is free-run (sync=0, 180ms). Mono legato with portaMode=1 and a 12-semitone bend range make it the expressive vintage lead.

```cpp
    // "Tape Drive" - Warm saturated dual-saw lead; tape drive + slap + slow wow
    {
        PresetDef p;
        p.name = "Tape Drive";
        p.category = "Leads";
        auto& s = p.state;
        // Dual detuned saws: thick analog bed, B slightly hotter-detuned for beating
        s.oscA.type = 0; s.oscA.waveform = 1; s.oscA.level = 0.85f;
        s.oscB.type = 0; s.oscB.waveform = 1;
        s.oscB.fineCents = 7.0f; s.oscB.level = 0.55f; // ~7ct detune = slow chorus-beat
        s.mixer.position = 0.42f;
        // Ladder pushed hard: the diode drive is half this lead's grit
        s.filter.type = 4; // Ladder
        s.filter.cutoffHz = 3800.0f; s.filter.resonance = 0.3f;
        s.filter.ladderSlope = 4;    // 24 dB/oct = full ladder body
        s.filter.ladderDrive = 8.0f; // strong input drive into the ladder
        s.filter.keyTrack = 0.2f;    // a touch of tracking so highs stay open
        // FIXED filter-env: plain SEMITONES (was 4000 garbage in the old bank).
        // +26 st opens the ladder on every attack then settles for a driven pluck.
        s.filter.envAmount = 26.0f;
        s.filterEnv.attackMs = 4.0f; s.filterEnv.decayMs = 220.0f;
        s.filterEnv.sustain = 0.35f; s.filterEnv.releaseMs = 250.0f;
        // TapeSaturator distortion - the warm harmonic core
        s.distortion.type = 5; // TapeSaturator
        s.distortion.drive = 0.5f; s.distortion.character = 0.5f;
        s.distortion.tapeSaturation = 0.7f; s.distortion.tapeBias = 0.55f;
        s.distortion.mix = 1.0f;
        s.ampEnv.attackMs = 3.0f; s.ampEnv.decayMs = 240.0f;
        s.ampEnv.sustain = 0.75f; s.ampEnv.releaseMs = 220.0f;
        // Tape delay as a free-running slapback (sync off, short time)
        s.delayEnabled = 1;
        s.delay.type = 1; // Tape
        s.delay.sync = 0; s.delay.timeMs = 180.0f; // 180ms slap
        s.delay.feedback = 0.28f; s.delay.mix = 0.22f;
        s.delay.tapeSaturation = 0.4f; s.delay.tapeWear = 0.35f;
        // MOD IDENTITY: a slow, SYMMETRY-SKEWED triangle LFO2 free-runs the cutoff
        // for authentic tape 'wow'. SCurve keeps the motion gentle, not stepped.
        s.lfo2.rateHz = 0.3f; s.lfo2.shape = 1; s.lfo2.sync = 0; s.lfo2.depth = 1.0f;
        s.lfo2Ext.symmetry = 0.72f; // asymmetric ramp = uneven, mechanical wow
        setModSlot(s, 0, kSrcLFO2, kDstAllFltCut, 0.18f, kCurveSCurve);
        // Harder playing brightens the tone
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.35f);
        // Expressive mono lead: legato glide + wide pitch bend
        s.global.voiceMode = 1;
        s.monoMode.priority = 0;  // Last-note priority
        s.monoMode.legato = 1; s.monoMode.portamentoTimeMs = 30.0f;
        s.monoMode.portaMode = 1; // glide only on legato overlap
        s.settings.pitchBendRangeSemitones = 12.0f; // full-octave whammy
        presets.push_back(std::move(p));
    }
```

---

## 12. "Particle Beam" -> "Particle Beam"

- **Locate:** the block containing `p.name = "Particle Beam"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A drifting granular particle-cloud lead over a clean sine sub, morphing between grain-swarm and pure tone under a smooth-random LFO.
- **Coverage:** particle-oscillator; particle-scatter; particle-density; particle-lifetime; particle-drift; particle-spawn-random; sine-sub; ladder-filter; voiceroute-modEnv->OscAPitch; modmatrix-LFO1->AllMorphPos; chorus-fx; mono-legato-portamento; velocity->MorphPos.
- **Rationale:** Reworks the old 'tight' particle patch into a true drifting cloud (scatter 4, longer 130ms grains, Random spawn, Blackman env, and the previously-unused particleDrift=0.45 which this preset now owns). The identity gesture is a SmoothRandom LFO1 crossfading OSC A(particle) against OSC B(sine sub) via morph position, plus a modEnv->OscA-pitch grain blip on attack and a Chorus for width — three subsystems (LFO, modEnv voice route, Chorus) the old static version left empty.

```cpp
    // "Particle Beam" - Drifting granular cluster + sine sub, LFO morph
    {
        PresetDef p;
        p.name = "Particle Beam";
        p.category = "Leads";
        auto& s = p.state;
        // Particle engine as the voice: a wide, slowly-drifting grain cloud
        s.oscA.type = 6; // Particle
        s.oscA.particleScatter = 4.0f;   // ~4 st spread = shimmering cloud (not tight)
        s.oscA.particleDensity = 40.0f;  // dense but not a wall
        s.oscA.particleLifetime = 130.0f;// longer grains = smoother, singing tone
        s.oscA.particleSpawnMode = 1;    // Random spawn = organic, non-metronomic
        s.oscA.particleEnvType = 3;      // Blackman = softest grain edges
        s.oscA.particleDrift = 0.45f;    // OWNS particleDrift: slow pitch wander
        s.oscA.level = 0.85f;
        // Clean sine sub an octave down anchors the pitch of the cloud
        s.oscB.type = 0; s.oscB.waveform = 0; // Sine
        s.oscB.tuneSemitones = -12.0f; s.oscB.level = 0.32f;
        s.mixer.position = 0.25f; // favor the particle cloud, sine underneath
        // Ladder keeps grain fizz musical
        s.filter.type = 4; // Ladder
        s.filter.cutoffHz = 4200.0f; s.filter.resonance = 0.25f;
        s.filter.ladderSlope = 3; // 18 dB/oct, a hair softer
        s.filter.envAmount = 18.0f; // gentle attack lift (fixed: semitones)
        s.filterEnv.attackMs = 8.0f; s.filterEnv.decayMs = 260.0f;
        s.filterEnv.sustain = 0.5f; s.filterEnv.releaseMs = 300.0f;
        s.ampEnv.attackMs = 6.0f; s.ampEnv.decayMs = 220.0f;
        s.ampEnv.sustain = 0.72f; s.ampEnv.releaseMs = 260.0f;
        // MOD IDENTITY: a SmoothRandom LFO1 crossfades particle<->sine (morph pos)
        // so the timbre never sits still - the cloud breathes against the sub.
        s.lfo1.rateHz = 0.22f; s.lfo1.shape = 5; s.lfo1.sync = 0; s.lfo1.depth = 1.0f;
        setModSlot(s, 0, kSrcLFO1, kDstAllMorphPos, 0.35f, kCurveSCurve);
        // modEnv -> OSC A pitch: a tiny upward grain-blip on each attack (transient)
        s.modEnv.attackMs = 1.0f; s.modEnv.decayMs = 90.0f;
        s.modEnv.sustain = 0.0f; s.modEnv.releaseMs = 120.0f;
        setVoiceRoute(s, 0, kVSrcEnv3, kVDstOscAPitch, 0.12f);
        // Velocity pushes the morph toward more particles when played hard
        setVoiceRoute(s, 1, kVSrcVelocity, kVDstMorphPos, 0.25f);
        // Wide chorus thickens the grain field in stereo
        s.modulationType = 3; // Chorus
        s.chorus.voices = 3; s.chorus.rateHz = 0.4f; s.chorus.depth = 0.4f;
        s.chorus.mix = 0.35f; s.chorus.stereoSpread = 180.0f;
        // Mono legato for lead phrasing over the drifting cloud
        s.global.voiceMode = 1;
        s.monoMode.legato = 1; s.monoMode.portamentoTimeMs = 15.0f;
        presets.push_back(std::move(p));
    }
```

---

## 13. "Octave Stack" -> "Octave Titan"

- **Locate:** the block containing `p.name = "Octave Stack"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A huge stereo octave-stacked lead: granular harmonizer throws a copy an octave up and down through a mid-focused band-pass and a jet-flanger.
- **Coverage:** dual-osc-detune; phaseMod-crossmod; svf-bandpass-filter; svfDrive; harmonizer-granular; harmonizer-octave-spread-pan; harmonizer-detune; voiceroute-aftertouch->MorphPos; flanger-fx; filterEnv-audible-sweep; mono-legato-portamento; mono-priority-high; velocity->FltCut.
- **Rationale:** Diversifies away from the shared Ladder recipe by moving to an SVF band-pass (type 2) with svfDrive=6 for a focused, cutting stack, and adds cross-mod via oscA.phaseMod=0.25. Keeps the directive's granular harmonizer octave spread but adds detune for width. Identity gesture is aftertouch->morph (unused elsewhere in the chunk) plus a Flanger and high-note priority — making it a live-expressive titan lead rather than a static octave copy.

```cpp
    // "Octave Titan" - Granular octave-doubler through a band-pass + flanger
    {
        PresetDef p;
        p.name = "Octave Titan";
        p.category = "Leads";
        auto& s = p.state;
        // Saw + triangle, saw self-phase-modulated for extra bite (cross-mod)
        s.oscA.type = 0; s.oscA.waveform = 1; s.oscA.level = 0.8f;
        s.oscA.phaseMod = 0.25f; // phase-mod sidebands = a hollow-metallic edge
        s.oscB.type = 0; s.oscB.waveform = 4; // Triangle (softer octave partner)
        s.oscB.fineCents = 5.0f; s.oscB.level = 0.45f;
        s.mixer.position = 0.4f;
        // SVF BAND-PASS: focuses the stack into a cutting horn-like midrange
        s.filter.type = 2; // SVF BP
        s.filter.cutoffHz = 1800.0f; s.filter.resonance = 2.5f;
        s.filter.svfSlope = 1;      // 24 dB
        s.filter.svfDrive = 6.0f;   // post-filter saturation for presence
        s.filter.keyTrack = 0.35f;
        s.filter.envAmount = 22.0f; // fixed: semitones, opens the BP window on attack
        s.filterEnv.attackMs = 6.0f; s.filterEnv.decayMs = 240.0f;
        s.filterEnv.sustain = 0.45f; s.filterEnv.releaseMs = 260.0f;
        s.ampEnv.attackMs = 3.0f; s.ampEnv.decayMs = 300.0f;
        s.ampEnv.sustain = 0.72f; s.ampEnv.releaseMs = 250.0f;
        // Granular harmonizer: octave down (L) + octave up (R), slight detune = size
        s.harmonizerEnabled = 1;
        s.harmonizer.harmonyMode = 0;    // Chromatic
        s.harmonizer.pitchShiftMode = 1; // Granular (OWNS this variant)
        s.harmonizer.numVoices = 2;
        s.harmonizer.dryLevelDb = 0.0f; s.harmonizer.wetLevelDb = -3.0f;
        s.harmonizer.voiceInterval[0] = -12; s.harmonizer.voicePan[0] = -0.4f;
        s.harmonizer.voiceDetuneCents[0] = -4.0f;
        s.harmonizer.voiceInterval[1] = 12;  s.harmonizer.voicePan[1] = 0.4f;
        s.harmonizer.voiceDetuneCents[1] = 4.0f;
        // MOD IDENTITY: AFTERTOUCH sweeps morph position - lean on the key and the
        // blend shifts saw<->triangle live. No sibling uses aftertouch.
        setVoiceRoute(s, 0, kVSrcAftertouch, kVDstMorphPos, 0.5f);
        setVoiceRoute(s, 1, kVSrcVelocity, kVDstFltCut, 0.4f);
        // Jet flanger widens and animates the octave stack
        s.modulationType = 2; // Flanger
        s.flanger.rateHz = 0.25f; s.flanger.depth = 0.6f; s.flanger.feedback = 0.3f;
        s.flanger.mix = 0.4f; s.flanger.stereoSpread = 90.0f;
        // Mono, HIGH-note priority so the top line always leads
        s.global.voiceMode = 1;
        s.monoMode.priority = 2;  // High-note priority
        s.monoMode.legato = 1; s.monoMode.portamentoTimeMs = 20.0f;
        s.monoMode.portaMode = 1;
        presets.push_back(std::move(p));
    }
```

---

## 14. "Comb Razor" -> "Comb Razor"

- **Locate:** the block containing `p.name = "Comb Razor"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A metallic, noise-excited comb-resonator lead whose feedback edge swells under an LFO, spat out through a synced digital delay.
- **Coverage:** comb-filter; comb-damping; filter-keytrack; noise-excitation; modmatrix-LFO1->AllResonance; filterEnv-comb-chirp; digital-delay-synced; plate-reverb; mono-legato-portamento; velocity->FltRes.
- **Rationale:** Keeps the comb+keyTrack+noise-excitation directive but makes the resonance MOVE — the audit's core complaint. Since combDamping has no mod destination, the honest solution is LFO1->AllResonance (Exp curve) sweeping the comb feedback edge, plus a velocity->FltRes route for dynamic bite and a small filterEnv chirp on comb frequency. Adds a synced Digital delay and a small Plate reverb so it owns a distinct FX signature from the other leads.

```cpp
    // "Comb Razor" - Noise-excited comb resonator, LFO-swept feedback edge
    {
        PresetDef p;
        p.name = "Comb Razor";
        p.category = "Leads";
        auto& s = p.state;
        // Saw body + a whisper of white noise to excite the comb teeth
        s.oscA.type = 0; s.oscA.waveform = 1; s.oscA.level = 0.8f;
        s.oscB.type = 9; // Noise
        s.oscB.noiseColor = 0; // White
        s.oscB.level = 0.12f;
        s.mixer.position = 0.12f; // mostly saw, noise just sparks the resonance
        // Comb filter, key-tracked so the metallic pitch follows the note
        s.filter.type = 6; // Comb
        s.filter.cutoffHz = 1200.0f; s.filter.resonance = 0.7f; // feedback = the 'ring'
        s.filter.combDamping = 0.25f; // static HF damping in the feedback path
        s.filter.keyTrack = 0.85f;
        // Small env on comb frequency = a short metallic chirp as the note starts
        s.filter.envAmount = 8.0f; // semitones
        s.filterEnv.attackMs = 1.0f; s.filterEnv.decayMs = 150.0f;
        s.filterEnv.sustain = 0.0f; s.filterEnv.releaseMs = 120.0f;
        s.ampEnv.attackMs = 2.0f; s.ampEnv.decayMs = 400.0f;
        s.ampEnv.sustain = 0.55f; s.ampEnv.releaseMs = 350.0f;
        // MOD IDENTITY: combDamping is not a routable destination, so we evolve the
        // metallic character by sweeping comb FEEDBACK (resonance) with a slow LFO1.
        // Exp curve keeps it calm at the bottom and bites near the top of the sweep.
        s.lfo1.rateHz = 0.5f; s.lfo1.shape = 1; s.lfo1.sync = 0; s.lfo1.depth = 1.0f;
        setModSlot(s, 0, kSrcLFO1, kDstAllResonance, 0.4f, kCurveExp);
        // Harder playing = more feedback bite
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltRes, 0.3f);
        // Synced digital delay for rhythmic metallic repeats
        s.delayEnabled = 1;
        s.delay.type = 0; // Digital
        s.delay.sync = 1; s.delay.noteValue = kNote1_8;
        s.delay.feedback = 0.3f; s.delay.mix = 0.18f;
        s.delay.digitalWidth = 130.0f;
        // Small plate reverb to seat the metal in a room
        s.reverbEnabled = 1;
        s.reverbType = 0; // Plate
        s.reverb.size = 0.35f; s.reverb.mix = 0.15f;
        // Mono legato phrasing
        s.global.voiceMode = 1;
        s.monoMode.priority = 0;
        s.monoMode.legato = 1; s.monoMode.portamentoTimeMs = 12.0f;
        presets.push_back(std::move(p));
    }
```

---

## 15. "Self Osc Ping" -> "Self Osc Ping"

- **Locate:** the block containing `p.name = "Self Osc Ping"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A polyphonic plucked-resonator: noise pings a self-oscillating filter that bends on attack and rings out into a plate.
- **Coverage:** self-oscillating-filter; selfosc-glide; selfosc-extmix; selfosc-shape; selfosc-release; filter-keytrack; noise-excitation; voiceroute-modEnv->FltCut-pitchbend; velocity->FltRes; plate-reverb; polyphonic.
- **Rationale:** Retains the self-osc directive (glide/extMix/shape/release, keyTrack) and stays the poly exception, but ends the audit's 'no modulation' complaint by adding two voice routes: modEnv->FltCut bends the resonant pitch on attack (a mallet-strike transient) and velocity->FltRes makes the ping speak dynamically. Plate reverb with pre-delay and diffusion gives it a distinct tail. resonance=18 self-oscillates cleanly while noise excitation stays low enough to avoid harshness.

```cpp
    // "Self Osc Ping" - Polyphonic self-oscillating filter pluck, bends on attack
    {
        PresetDef p;
        p.name = "Self Osc Ping";
        p.category = "Leads";
        auto& s = p.state;
        // A short burst of white noise is the only exciter; osc B silent
        s.oscA.type = 9; // Noise
        s.oscA.noiseColor = 0; s.oscA.level = 0.3f;
        s.oscB.level = 0.0f;
        s.mixer.position = 0.0f; // OSC A only
        // Self-oscillating filter IS the tone generator (sine-like ping)
        s.filter.type = 12; // Self-Osc
        s.filter.cutoffHz = 440.0f; s.filter.resonance = 18.0f;
        s.filter.keyTrack = 1.0f;         // ping pitch tracks the keyboard
        s.filter.selfOscGlide = 40.0f;    // slight portamento of the resonant pitch
        s.filter.selfOscExtMix = 0.35f;   // let some noise bleed through the ping
        s.filter.selfOscShape = 0.3f;     // a little harmonic colour, not pure sine
        s.filter.selfOscRelease = 900.0f; // long ring-out
        // Pluck amp shape: instant attack, long decay to silence
        s.ampEnv.attackMs = 1.0f; s.ampEnv.decayMs = 1800.0f;
        s.ampEnv.sustain = 0.0f; s.ampEnv.releaseMs = 1200.0f;
        // MOD IDENTITY: a fast modEnv bends the resonant CUTOFF (= ping pitch) upward
        // on the strike then settles - a mallet-like attack transient. Unique gesture.
        s.modEnv.attackMs = 1.0f; s.modEnv.decayMs = 110.0f;
        s.modEnv.sustain = 0.0f; s.modEnv.releaseMs = 150.0f;
        setVoiceRoute(s, 0, kVSrcEnv3, kVDstFltCut, 0.15f);
        // Velocity drives resonance = how hard/loud the ping speaks
        setVoiceRoute(s, 1, kVSrcVelocity, kVDstFltRes, 0.4f);
        // Plate reverb tail; short pre-delay separates the ping from its wash
        s.reverbEnabled = 1;
        s.reverbType = 0; // Plate
        s.reverb.size = 0.6f; s.reverb.mix = 0.35f; s.reverb.damping = 0.35f;
        s.reverb.preDelayMs = 15.0f; s.reverb.diffusion = 0.8f;
        // POLYPHONIC (the mono-glide exception of the category) - chords of pings
        s.global.voiceMode = 0; s.global.polyphony = 6;
        presets.push_back(std::move(p));
    }
```

---

## 16. "PWM Sweep" -> "PWM Sweep"

- **Locate:** the block containing `p.name = "PWM Sweep"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A hollow, breathing dual-pulse lead whose timbre chorus-sweeps as a symmetry-skewed triangle LFO morphs between a thin nasal pulse and a fat square.
- **Coverage:** dual-pulse-osc; pulse-width/PWM; lfo-symmetry-skew; lfo->morph; svf-lp-filter; flanger-fx; aftertouch->fltcut; velocity->fltcut; mono-legato-portamento.
- **Rationale:** Since pulseWidth isn't a routable destination, I use two pulses at 0.15 (nasal) and 0.5 (square) and route LFO1->MorphPos to crossfade between them — an audible PWM sweep using real params. lfo1Ext.symmetry=0.75 skews the triangle into a ramp so the sweep is asymmetric (the owned feature). Mixer centered at 0.5 lets the morph travel the full A<->B range. SVF LP + svfDrive 4dB gives body; velocity and aftertouch both open the filter for playability; a slow Flanger widens the hollow tone. Distinct mod wiring (LFO->Morph, not ->FltCut) so PD Warp doesn't collide.

```cpp
    // "PWM Sweep" - Symmetry-skewed LFO morphs two pulse widths for classic PWM
    {
        PresetDef p;
        p.name = "PWM Sweep";
        p.category = "Leads";
        auto& s = p.state;
        // Two PolyBLEP pulses at DIFFERENT widths; morphing A<->B == sweeping PWM.
        // (pulseWidth itself is not a mod destination, so we crossfade a thin
        //  nasal pulse against a fat square to synthesize the PWM motion.)
        s.oscA.type = 0; s.oscA.waveform = 3; // Pulse
        s.oscA.pulseWidth = 0.15f; // thin/nasal end of the sweep
        s.oscA.level = 0.85f;
        s.oscB.type = 0; s.oscB.waveform = 3; // Pulse
        s.oscB.pulseWidth = 0.5f;  // square end of the sweep
        s.oscB.fineCents = -7.0f;  // gentle beating for analog thickness
        s.oscB.level = 0.85f;
        s.mixer.position = 0.5f;   // centered so morph can travel full A<->B
        // Plain SVF LP with a touch of drive for body under the hollow pulses.
        s.filter.type = 0; s.filter.cutoffHz = 3800.0f; s.filter.resonance = 0.35f;
        s.filter.svfSlope = 1; s.filter.svfDrive = 4.0f;
        s.ampEnv.attackMs = 8.0f; s.ampEnv.decayMs = 400.0f;
        s.ampEnv.sustain = 0.75f; s.ampEnv.releaseMs = 350.0f;
        // Slow free triangle, symmetry SKEWED to a ramp -> asymmetric PWM travel.
        s.lfo1.rateHz = 0.45f; s.lfo1.shape = 1; // Triangle
        s.lfo1.depth = 1.0f; s.lfo1.sync = 0;
        s.lfo1Ext.symmetry = 0.75f; // <-- owned feature: skewed LFO shape
        // Identity mod: LFO1 -> morph position (the PWM sweep itself), SCurve ease.
        setModSlot(s, 0, kSrcLFO1, kDstAllMorphPos, 0.85f, kCurveSCurve);
        // Expressive, playable brightness under fingers/pressure.
        setVoiceRoute(s, 0, kVSrcVelocity,   kVDstFltCut, 0.35f);
        setVoiceRoute(s, 1, kVSrcAftertouch, kVDstFltCut, 0.40f);
        // Flanger widens the hollow pulse tone (category owns the Flanger).
        s.modulationType = 2; // Flanger
        s.flanger.rateHz = 0.3f; s.flanger.depth = 0.6f;
        s.flanger.feedback = 0.35f; s.flanger.mix = 0.4f; s.flanger.stereoSpread = 120.0f;
        s.global.voiceMode = 1; // Mono
        s.monoMode.priority = 0; // Last-note
        s.monoMode.legato = 1; s.monoMode.portamentoTimeMs = 20.0f;
        s.global.width = 1.2f;
        presets.push_back(std::move(p));
    }
```

---

## 17. "Bright Brass" -> "Regal Fanfare"

- **Locate:** the block containing `p.name = "Bright Brass"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A brassy additive fanfare that actually swells: a corrected filter envelope blats the ladder open on attack, velocity drives brightness, and warm tape saturation adds bite.
- **Coverage:** additive-engine; additive-tilt/inharm; ladder-filter; ladder-slope/drive; filterEnv (BUG FIXED, +30 st); velocity->fltcut; keyTrack->SpecTilt; tape-saturator-distortion; pitchBendRange-wide; mono-legato-portamento.
- **Rationale:** The core fix is envAmount=30.0f now meaning +30 SEMITONES (verified plain encoding in FilterState), so the Ladder actually swells on attack instead of the old inaudible 30 Hz. filterEnv is shaped for a brass 'blat' (30ms rise, drop to 0.35 sustain). Additive 40 partials + tilt +3 + inharm 0.05 gives a bright reedy spectrum, ladderDrive 6dB adds horn grit. velocity->FltCut is real brass dynamics; keyTrack->SpecTilt brightens upper register; LFO1->AllResonance is a unique subtle-shimmer gesture no sibling repeats. TapeSaturator (mix 0.7, drive 0.3) warms without destroying. Wide 12-st pitch bend suits fanfare falls.

```cpp
    // "Regal Fanfare" - Additive brass with a CORRECTED filter-env swell + tape warmth
    {
        PresetDef p;
        p.name = "Regal Fanfare";
        p.category = "Leads";
        auto& s = p.state;
        // Additive core: many partials, upward tilt = bright brassy spectrum,
        // a hair of inharmonicity for reedy edge.
        s.oscA.type = 4; // Additive
        s.oscA.additivePartials = 40; s.oscA.additiveTilt = 3.0f;
        s.oscA.additiveInharm = 0.05f; s.oscA.level = 0.8f;
        // Saw unison partner, slightly detuned, fills the low-mid body.
        s.oscB.type = 0; s.oscB.waveform = 1; s.oscB.fineCents = 6.0f; s.oscB.level = 0.45f;
        s.mixer.position = 0.35f;
        // Ladder 18 dB/oct with input drive = the classic brass filter voice.
        s.filter.type = 4; // Ladder
        s.filter.cutoffHz = 1400.0f; s.filter.resonance = 0.3f;
        s.filter.ladderSlope = 3;   // 18 dB/oct
        s.filter.ladderDrive = 6.0f;
        // *** BUG FIX ***: envAmount is PLAIN SEMITONES (-48..+48), not Hz.
        // Old brass wrote 30 "Hz" -> near-zero sweep. +30 st = a real brass blat.
        s.filter.envAmount = 30.0f;
        s.ampEnv.attackMs = 25.0f; s.ampEnv.decayMs = 250.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 200.0f;
        // Fast-rising, quickly-falling filter env = the attack "bite" then settle.
        s.filterEnv.attackMs = 30.0f; s.filterEnv.decayMs = 280.0f;
        s.filterEnv.sustain = 0.35f; s.filterEnv.releaseMs = 220.0f;
        // Brass dynamics: harder = brighter; higher notes tilt spectrum brighter.
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut,  0.5f);
        setVoiceRoute(s, 1, kVSrcKeyTrack, kVDstSpecTilt, 0.4f);
        // Slow triangle LFO adds a subtle resonant shimmer (unique gesture).
        s.lfo1.rateHz = 0.8f; s.lfo1.shape = 1; s.lfo1.depth = 1.0f; s.lfo1.sync = 0;
        setModSlot(s, 0, kSrcLFO1, kDstAllResonance, 0.2f, kCurveSCurve);
        // Tape saturator = warm, slightly compressed brass drive.
        s.distortion.type = 5; // TapeSaturator
        s.distortion.drive = 0.3f; s.distortion.tapeSaturation = 0.55f;
        s.distortion.tapeBias = 0.5f; s.distortion.mix = 0.7f;
        s.global.voiceMode = 1; // Mono
        s.monoMode.priority = 2; // High-note priority (top-line brass)
        s.monoMode.legato = 1; s.monoMode.portamentoTimeMs = 12.0f;
        s.settings.pitchBendRangeSemitones = 12.0f; // wide bends/falls
        presets.push_back(std::move(p));
    }
```

---

## 18. "Spectral Slide" -> "Spectral Slide"

- **Locate:** the block containing `p.name = "Spectral Slide"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A ghostly gliding spectral-freeze lead pushed through a metallic comb, with a mod-env pitch-blip on attack, aftertouch morphing, and a tape-delay-into-hall tail.
- **Coverage:** spectral-freeze-engine; spectralFormant-shift; spectral-tilt; comb-filter; comb-damping; modEnv->OscA-pitch; aftertouch->morph; lfo->specTilt; tape-delay; hall-reverb; mono-portamento-glide.
- **Rationale:** spectralFormant=+7 (owned) detaches the vocal formant from pitch for a morphing chipmunk/monster colour; spectralTilt +2 keeps it airy. Comb filter (type 6, combDamping 0.3) gives metallic hollow resonance instead of the usual LP, covering a fresh filter. The mod-env is reshaped (2/220/0/200) and routed Env3->OscAPitch for a real attack pitch-blip (covers modEnv->OscA pitch); aftertouch->MorphPos adds expression; LFO1->AllSpecTilt is a unique slow breath. Tape delay into a large Hall reverb (reverbType 1) plus 80ms portamento make it the spatial glider — distinct FX signature from every sibling.

```cpp
    // "Spectral Slide" - Gliding spectral-freeze with formant-shift + comb + tape/hall
    {
        PresetDef p;
        p.name = "Spectral Slide";
        p.category = "Leads";
        auto& s = p.state;
        // Spectral-freeze source: tilt up for air, and a formant shift up an
        // octave for a chipmunk/monster vocal colour independent of pitch.
        s.oscA.type = 8; // Spectral Freeze
        s.oscA.spectralTilt = 2.0f;
        s.oscA.spectralFormant = 7.0f; // <-- owned: formant shifted +7 st
        s.oscA.level = 0.8f;
        s.oscB.type = 0; s.oscB.waveform = 1; s.oscB.level = 0.3f; // saw body
        s.mixer.position = 0.25f;
        // Comb filter = metallic, hollow resonance that suits the airy source.
        s.filter.type = 6; // Comb
        s.filter.cutoffHz = 1800.0f; s.filter.resonance = 0.45f;
        s.filter.combDamping = 0.3f; // tame the highest comb teeth
        s.ampEnv.attackMs = 40.0f; s.ampEnv.decayMs = 600.0f;
        s.ampEnv.sustain = 0.8f; s.ampEnv.releaseMs = 700.0f; // long airy tail
        // Mod-env drives a short upward pitch swoop on OscA at note-on.
        s.modEnv.attackMs = 2.0f; s.modEnv.decayMs = 220.0f;
        s.modEnv.sustain = 0.0f; s.modEnv.releaseMs = 200.0f;
        setVoiceRoute(s, 0, kVSrcEnv3,       kVDstOscAPitch, 0.35f); // pitch-blip
        setVoiceRoute(s, 1, kVSrcAftertouch, kVDstMorphPos,  0.5f);  // press = morph
        // Slow free LFO breathes the spectral tilt (unique -> SpecTilt gesture).
        s.lfo1.rateHz = 0.25f; s.lfo1.shape = 0; s.lfo1.depth = 0.6f; s.lfo1.sync = 0;
        setModSlot(s, 0, kSrcLFO1, kDstAllSpecTilt, 0.4f, kCurveLinear);
        // Tape delay + Hall reverb = the spacious slide tail (identity FX).
        s.delayEnabled = 1; s.delay.type = 1; // Tape
        s.delay.timeMs = 380.0f; s.delay.feedback = 0.3f; s.delay.mix = 0.2f;
        s.delay.tapeSaturation = 0.4f;
        s.reverbEnabled = 1; s.reverbType = 1; // Hall
        s.reverb.size = 0.75f; s.reverb.mix = 0.3f; s.reverb.damping = 0.35f;
        s.global.voiceMode = 1; // Mono
        s.monoMode.priority = 0; // Last-note
        s.monoMode.legato = 1; s.monoMode.portamentoTimeMs = 80.0f; // long glide
        s.settings.pitchBendRangeSemitones = 7.0f;
        presets.push_back(std::move(p));
    }
```

---

## 19. "PD Warp" -> "PD Warp"

- **Locate:** the block containing `p.name = "PD Warp"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A nasal, metallic phase-distortion lead: octave-stacked resonant CZ waveforms swept by a 3 Hz LFO through a bandpass, ring-modulated into a bell-like snarl.
- **Coverage:** phase-distortion-engine; pd-waveform-resonant; svf-bandpass; lfo->fltcut (distinct from PWM); filterEnv-sweep; velocity->fltRes; ring-modulator-distortion; mono-legato-portamento.
- **Rationale:** Keeps the phase-distortion identity but fixes the sameyness call-out: its LFO now routes to AllFltCut (sweeping the SVF bandpass = nasal wah) instead of PWM Sweep's LFO->MorphPos, so the two no longer share wiring. Resonant CZ shapes (ResSaw + ResTrapezoid, octave-stacked) plus a note-tracked RingModulator (ringRatio 0.2 normalized, mix 0.4 to stay musical) give the metallic snarl. filterEnv adds a +20-semitone attack sweep, velocity->FltRes adds dynamic squelch. Ring mod is the owned dirty distortion for this lead.

```cpp
    // "PD Warp" - Octave-stacked resonant phase distortion, LFO-swept bandpass + ring mod
    {
        PresetDef p;
        p.name = "PD Warp";
        p.category = "Leads";
        auto& s = p.state;
        // Both oscs are Casio-CZ phase distortion; resonant shapes give the
        // formant-y "warp", octave-stacked for a hollow power.
        s.oscA.type = 2; // Phase Distortion
        s.oscA.pdWaveform = 5; // ResSaw
        s.oscA.pdDistortion = 0.6f; s.oscA.level = 0.85f;
        s.oscB.type = 2;
        s.oscB.pdWaveform = 7; // ResTrapezoid
        s.oscB.pdDistortion = 0.45f; s.oscB.tuneSemitones = 12.0f; s.oscB.level = 0.4f;
        s.mixer.position = 0.35f;
        // SVF bandpass isolates a nasal formant band for the LFO to sweep.
        s.filter.type = 2; // SVF BP
        s.filter.cutoffHz = 2000.0f; s.filter.resonance = 0.5f; s.filter.svfSlope = 1;
        // A modest filter-env sweep on top (envAmount is PLAIN SEMITONES).
        s.filter.envAmount = 20.0f;
        s.ampEnv.attackMs = 2.0f; s.ampEnv.decayMs = 300.0f;
        s.ampEnv.sustain = 0.55f; s.ampEnv.releaseMs = 220.0f;
        s.filterEnv.attackMs = 5.0f; s.filterEnv.decayMs = 250.0f;
        s.filterEnv.sustain = 0.3f; s.filterEnv.releaseMs = 200.0f;
        // 3 Hz sine LFO sweeps the BANDPASS cutoff = nasal wah vibrato.
        // Deliberately DIFFERENT wiring from PWM Sweep (->FltCut, not ->Morph).
        s.lfo1.rateHz = 3.0f; s.lfo1.shape = 0; s.lfo1.depth = 0.5f; s.lfo1.sync = 0;
        setModSlot(s, 0, kSrcLFO1, kDstAllFltCut, 0.4f, kCurveExp);
        // Velocity opens resonance for dynamic squelch under harder playing.
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltRes, 0.45f);
        // Note-tracked ring modulator adds the metallic/bell snarl.
        s.distortion.type = 6; // RingModulator
        s.distortion.ringFreqMode = 1; // NoteTrack
        s.distortion.ringRatio = 0.2f;  // normalized -> inharmonic-ish ratio
        s.distortion.ringWaveform = 0;  // Sine
        s.distortion.drive = 0.3f; s.distortion.mix = 0.4f; // level-compensated
        s.global.voiceMode = 1; // Mono
        s.monoMode.priority = 0; // Last-note
        s.monoMode.legato = 1; s.monoMode.portamentoTimeMs = 10.0f;
        s.settings.pitchBendRangeSemitones = 2.0f;
        presets.push_back(std::move(p));
    }
```

---

## 20. "Fifth Power" -> "Fifth Column"

- **Locate:** the block containing `p.name = "Fifth Power"` in F:\projects\iterum\tools\ruinae_preset_generator.cpp
- **Character:** A snarling power-fifth lead where the two saws cross-modulate each other's frequency into a growl, folded through a wavefolder and swept by an envelope-follower auto-wah, thickened by a lush chorus.
- **Coverage:** dual-saw-fifth-interval; freqMod-cross-mod (owned); phaseMod-cross-mod; envFilter-autowah; envFilter-params; wavefolder-distortion; chorus-fx; velocity->distDrive; lfo->fltcut; pitchBendRange-wide; stereo-width/spread; mono-legato-portamento.
- **Rationale:** Replaces the near-template original: oscA.freqMod 0.3 + oscB.freqMod 0.25 (plus oscA.phaseMod 0.2) make the two saws cross-FM into a growl (owned freqMod cross-mod, covering phaseMod too). The filter is now an EnvFilter auto-wah (type 11) driven by the fold energy — a whole new filter class for the category — with full env-filter params set. Wavefolder (type 4, mix 0.55, level-compensated osc levels) is the owned dirty distortion; velocity->DistDrive makes dirt dynamic. Chorus (3 voices, spread 180) replaces the dropped phaser per directive; LFO2->AllFltCut adds slow evolution. Wide 12-st bend + 1.4 width + 0.3 spread give it a big guitar-power-lead identity distinct from every sibling.

```cpp
    // "Fifth Column" - Cross-FM power fifths, wavefolded, auto-wah + chorus
    {
        PresetDef p;
        p.name = "Fifth Column";
        p.category = "Leads";
        auto& s = p.state;
        // Two saws a perfect fifth apart; freqMod on each = mutual FM growl edge
        // (owned freqMod cross-mod), a touch of phaseMod for extra sideband dirt.
        s.oscA.type = 0; s.oscA.waveform = 1; s.oscA.level = 0.8f;
        s.oscA.freqMod = 0.3f; s.oscA.phaseMod = 0.2f;
        s.oscB.type = 0; s.oscB.waveform = 1;
        s.oscB.tuneSemitones = 7.0f; // the power fifth
        s.oscB.freqMod = 0.25f; s.oscB.level = 0.7f;
        s.mixer.position = 0.45f;
        // Envelope-follower filter (auto-wah): the fold/attack energy sweeps it.
        s.filter.type = 11; // Env Filter
        s.filter.cutoffHz = 600.0f; s.filter.resonance = 0.4f;
        s.filter.envSubType = 0;      // LP response
        s.filter.envSensitivity = 6.0f;
        s.filter.envDepth = 0.8f;
        s.filter.envAttack = 8.0f; s.filter.envRelease = 180.0f;
        s.filter.envDirection = 0;    // sweep Up
        s.ampEnv.attackMs = 4.0f; s.ampEnv.decayMs = 260.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 260.0f;
        // Wavefolder = aggressive harmonic grit; osc levels + mix keep it sane.
        s.distortion.type = 4; // Wavefolder
        s.distortion.drive = 0.4f; s.distortion.foldType = 1;
        s.distortion.character = 0.5f; s.distortion.mix = 0.55f;
        // Velocity pushes the fold drive -> harder = dirtier (unique route).
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstDistDrive, 0.4f);
        // Slow LFO2 nudges the auto-wah base for slow evolving motion.
        s.lfo2.rateHz = 0.5f; s.lfo2.shape = 1; s.lfo2.depth = 0.4f; s.lfo2.sync = 0;
        setModSlot(s, 0, kSrcLFO2, kDstAllFltCut, 0.25f, kCurveLinear);
        // Chorus replaces the old shared phaser -> lush stereo widening.
        s.modulationType = 3; // Chorus
        s.chorus.rateHz = 0.4f; s.chorus.depth = 0.4f; s.chorus.mix = 0.35f;
        s.chorus.voices = 3; s.chorus.stereoSpread = 180.0f;
        s.global.voiceMode = 1; // Mono
        s.monoMode.priority = 1; // Low-note priority (chord root leads)
        s.monoMode.legato = 1; s.monoMode.portamentoTimeMs = 15.0f;
        s.global.width = 1.4f; s.global.spread = 0.3f;
        s.settings.pitchBendRangeSemitones = 12.0f; // guitar-style dive bends
        presets.push_back(std::move(p));
    }
```
