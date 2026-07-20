# Ruinae Preset Plan — Pads

The Pads category is the worst offender for the "boring / same-ish" complaint: nearly every preset was a detuned dual-saw through a lowpass into a big hall, built from the shared `setSynthPad` template with only a reverb swapped. This redesign gives each of the 20 pads a distinct engine, filter, modulation gesture, and space so they read as 20 different instruments while COLLECTIVELY exercising every functional area of the synth. Load-bearing decisions across the batch: the four additive pads take the full partial-count spread (12 / 40 / 80 / 128); the four spectral/chaos pads split into distinct filter+distortion+space combos (frozen comb-spectral vs SVF/Allpass vs clean chaos); the morph-motion axis is deliberately distributed (static / free-LFO / smoothed-random / keytrack / chaos); the filter-envelope semitone bug is corrected everywhere with audible +14…+30 st blooms; and every reverb+delay pairing is unique in the batch. Two pads are renamed to break signal-chain collisions with Leads siblings: "Analog Brass" → "Brass Regiment" and "Alien World" → "Xenoform".

Locate every block by searching for the `p.name = "<original name>"` string in `F:\projects\iterum\tools\ruinae_preset_generator.cpp` — do NOT rely on line numbers, they drift as edits land.

## 1. "Warm Analog" -> "Warm Analog"
- **Locate:** the block containing `p.name = "Warm Analog"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A lush detuned dual-saw analog pad that breathes — animated by a 3-voice chorus, an organic skewed SmoothRandom brightness drift, a non-linear Bezier swell, tape echo, and a sub-octave PitchSync harmonizer for weight.
- **Coverage:** engine: PolyBLEP; filter: Ladder (slope + drive); Bezier envelope mode; per-note mod identity: skewed SmoothRandom LFO -> SpecTilt (SCurve); LFO SmoothRandom shape + symmetry; Chorus FX; Tape delay; Reverb Plate + preDelay; harmonizer PitchSync pitchShiftMode; global width + spread; aftertouch/keyTrack + velocity voice routes.
- **Rationale:** Keeps the archetypal detuned dual-saw/ladder identity but obeys the ban on 'template + reverb': the Chorus (voices=3, stereoSpread=200) and skewed SmoothRandom LFO2->SpecTilt (symmetry=0.75, SCurve) give continuous non-repeating motion no sibling has; Bezier attack handles (cp low->rising) create a genuinely non-linear swell; ladderSlope=3+ladderDrive=4 warms the tone; envAmount=14 st fixes the semitone bug with an audible bloom; PitchSync -12 adds weight; Tape delay + Plate reverb are the warm-side wrapper (vs the huge halls elsewhere).
- **Replacement code:**
```cpp
    // "Warm Analog" - Reference analog pad, but no longer static: chorus width,
    // organic SmoothRandom tilt drift, a non-linear Bezier swell + tape echo.
    {
        PresetDef p;
        p.name = "Warm Analog";
        p.category = "Pads";
        auto& s = p.state;
        // Classic detuned dual-saw core (PolyBLEP) with symmetric beating
        s.oscA.type = 0; // PolyBLEP
        s.oscA.waveform = 1; // Saw
        s.oscA.fineCents = -4.0f; // symmetric detune vs OscB -> slow chorusing beat
        s.oscA.level = 0.7f;
        s.oscB.type = 0;
        s.oscB.waveform = 1; // Saw
        s.oscB.fineCents = 9.0f; // wider detune for analog warmth
        s.oscB.level = 0.62f;
        s.mixer.position = 0.5f;
        // Ladder LP at 18 dB/oct with a touch of drive = creamy analog color
        s.filter.type = 4; // Ladder LP
        s.filter.cutoffHz = 2600.0f;
        s.filter.resonance = 0.28f;
        s.filter.ladderSlope = 3; // 18 dB/oct (softer than full 24)
        s.filter.ladderDrive = 4.0f; // gentle input drive for saturation
        // Gentle filter-env bloom (CORRECTED: semitones, not the old 4000 Hz bug)
        s.filter.envAmount = 14.0f; // +14 st sweep opens the pad on attack
        s.filterEnv.attackMs = 500.0f;
        s.filterEnv.decayMs = 1200.0f;
        s.filterEnv.sustain = 0.6f;
        s.filterEnv.releaseMs = 1400.0f;
        // Amp env: slow non-linear swell via Bezier attack handles
        s.ampEnv.attackMs = 700.0f;
        s.ampEnv.decayMs = 900.0f;
        s.ampEnv.sustain = 0.78f;
        s.ampEnv.releaseMs = 1600.0f;
        s.ampEnv.bezierEnabled = 1.0f;
        s.ampEnv.bezierAttackCp1X = 0.25f; s.ampEnv.bezierAttackCp1Y = 0.05f; // stays low early
        s.ampEnv.bezierAttackCp2X = 0.55f; s.ampEnv.bezierAttackCp2Y = 0.25f; // then curves up -> slow exp swell
        // Chorus (never-used FX) gives the wide, moving analog stereo field
        s.modulationType = 3; // Chorus
        s.chorus.rateHz = 0.4f;
        s.chorus.depth = 0.5f;
        s.chorus.mix = 0.4f;
        s.chorus.voices = 3;
        s.chorus.feedback = 0.1f;
        s.chorus.stereoSpread = 200.0f; // wide animated width
        // Tape delay adds vintage repeats behind the pad
        s.delayEnabled = 1;
        s.delay.type = 1; // Tape
        s.delay.sync = 0;
        s.delay.timeMs = 380.0f;
        s.delay.feedback = 0.3f;
        s.delay.mix = 0.18f;
        s.delay.tapeSaturation = 0.4f;
        s.delay.tapeWear = 0.2f;
        s.delay.tapeInertiaMs = 400.0f;
        // Plate reverb with short pre-delay keeps transients defined
        s.reverbEnabled = 1;
        s.reverbType = 0; // Plate
        s.reverb.size = 0.6f;
        s.reverb.mix = 0.28f;
        s.reverb.damping = 0.45f;
        s.reverb.preDelayMs = 15.0f;
        s.reverb.diffusion = 0.8f;
        // Sub-octave PitchSync harmonizer thickens the low end
        s.harmonizerEnabled = 1;
        s.harmonizer.pitchShiftMode = 3; // PitchSync
        s.harmonizer.numVoices = 1;
        s.harmonizer.voiceInterval[0] = -12; // one octave down
        s.harmonizer.voiceLevelDb[0] = -10.0f;
        s.harmonizer.wetLevelDb = -12.0f;
        s.harmonizer.dryLevelDb = 0.0f;
        // Signature mod: skewed SmoothRandom LFO2 slowly drifts brightness (unique gesture)
        s.lfo2.rateHz = 0.07f; s.lfo2.shape = 5; // Smooth Random
        s.lfo2.depth = 0.5f; s.lfo2.sync = 0;
        s.lfo2Ext.symmetry = 0.75f; // skewed ramp -> asymmetric organic wander
        setModSlot(s, 0, kSrcLFO2, kDstAllSpecTilt, 0.3f, kCurveSCurve);
        // Playing dynamics: velocity opens filter, key-track brightens the top
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.35f);
        setVoiceRoute(s, 1, kVSrcKeyTrack, kVDstFltCut, 0.2f);
        s.global.width = 1.4f;
        s.global.spread = 0.3f;
        presets.push_back(std::move(p));
    }
```

## 2. "Glass Shimmer" -> "Glass Shimmer"
- **Locate:** the block containing `p.name = "Glass Shimmer"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A crystalline bell pad with a FIXED (static) morph: a 40-partial inharmonic additive body sitting against an octave-up wavetable layer, lifted by a hi-shelf air boost, sparkled by note-tracked ring-mod partials and smeared into an octave-up granular cloud through a chorused Plate — its motion comes from ring-mod, grains and tail modulation, never a morph sweep.
- **Coverage:** engine: Additive (40 partials, tilt, inharmonicity 0.12 = the batch's inharmonic member); engine: Wavetable (octave-up layer, phaseMod); filter: SVF Hi-Shelf (svfGain +6); RingModulator distortion (note-tracked); Granular delay; Reverb Plate + modRate/modDepth + preDelay; morph motion: STATIC (fixed mixer.position, no morph modulation); modEnv + per-stage decayCurve; Env3 -> SpecTilt voice route (bloom, not morph); global width/spread.
- **Rationale:** Assigned the batch's STATIC morph: mixer.position=0.42 is fixed and nothing routes to kVDstMorphPos (the keyTrack->MorphPos route that used to live here was moved to Solar Flare), so the A/B blend never sweeps - all motion is ring-mod + grains + plate mod. Keeps additivePartials=40 (the middle of the 12/40/80/128 spread) and is the designated inharmonic member (additiveInharm=0.12) so the four are no longer all clean-harmonic. Retains the Hi-Shelf air identity + Plate+Granular pairing; the other three diverge around it.
- **Replacement code:**
```cpp
    // "Glass Shimmer" - Additive bell (40 partials, inharmonic) morphed STATICALLY
    // against a wavetable octave layer; ring-mod sparkle into an octave-up granular
    // cloud and a chorused Plate. Morph position is FIXED - motion comes from the
    // ring-mod, the grains and the plate mod, not from any LFO/keyTrack sweep.
    {
        PresetDef p;
        p.name = "Glass Shimmer";
        p.category = "Pads";
        auto& s = p.state;
        // Additive bell body: MID partial count (40), bright tilt, clear inharmonicity
        s.oscA.type = 4; // Additive
        s.oscA.level = 0.68f;
        s.oscA.additivePartials = 40;   // mid of the 12/40/80/128 spread
        s.oscA.additiveTilt = 4.0f;     // airy top
        s.oscA.additiveInharm = 0.12f;  // the batch's inharmonic member -> bell metallicity
        // Octave-up wavetable layer (covers the never-used Wavetable engine)
        s.oscB.type = 1; // Wavetable
        s.oscB.waveform = 4; // Triangle base
        s.oscB.level = 0.35f;
        s.oscB.tuneSemitones = 12.0f; // one octave up = shimmer
        s.oscB.fineCents = 3.0f;
        s.oscB.phaseMod = 0.25f; // gentle wavetable FM sidebands
        s.mixer.position = 0.42f;      // FIXED morph -> favors the additive body, never swept
        // SVF Hi-Shelf lifts the air band (this preset keeps the air-boost identity)
        s.filter.type = 10; // SVF Hi-Shelf
        s.filter.cutoffHz = 4000.0f;
        s.filter.resonance = 0.2f;
        s.filter.svfGain = 6.0f; // +6 dB shelf boost for glassy top
        s.filter.svfSlope = 1;
        // Amp env with an exponential decay tail (per-stage curve)
        s.ampEnv.attackMs = 500.0f;
        s.ampEnv.decayMs = 1000.0f;
        s.ampEnv.sustain = 0.7f;
        s.ampEnv.releaseMs = 2200.0f;
        s.ampEnv.decayCurve = 0.4f; // slow-tailing exponential decay
        // Mod env blooms spectral tilt on attack (Env3) - NOT the morph axis
        s.modEnv.attackMs = 200.0f;
        s.modEnv.decayMs = 1600.0f;
        s.modEnv.sustain = 0.0f;
        s.modEnv.releaseMs = 1200.0f;
        // Note-tracked ring modulator adds inharmonic bell partials (subtle)
        s.distortion.type = 6; // Ring Modulator
        s.distortion.mix = 0.15f;
        s.distortion.drive = 0.2f;
        s.distortion.ringFreqMode = 1; // NoteTrack -> stays musical
        s.distortion.ringRatio = 0.175f; // normalized -> ~3.0 ratio (octave+fifth partial)
        s.distortion.ringWaveform = 0; // Sine
        s.distortion.ringStereoSpread = 0.3f;
        // Granular delay smears octave-up grains into a shimmering cloud (Plate+Granular pairing)
        s.delayEnabled = 1;
        s.delay.type = 3; // Granular
        s.delay.sync = 0;
        s.delay.timeMs = 300.0f;
        s.delay.feedback = 0.35f;
        s.delay.mix = 0.25f;
        s.delay.granularSizeMs = 120.0f;
        s.delay.granularDensity = 20.0f;
        s.delay.granularPitch = 12.0f; // octave-up grains
        s.delay.granularPitchSpray = 0.1f;
        s.delay.granularTexture = 0.3f;
        s.delay.granularWidth = 1.3f;
        // Modulated Plate reverb = chorused shimmer tail
        s.reverbEnabled = 1;
        s.reverbType = 0; // Plate
        s.reverb.size = 0.85f;
        s.reverb.mix = 0.4f;
        s.reverb.damping = 0.28f;
        s.reverb.preDelayMs = 10.0f;
        s.reverb.modRateHz = 0.35f; // slow chorusing of the tail
        s.reverb.modDepth = 0.25f;
        // MORPH MOTION = STATIC: nothing routes to MorphPos. The single voice route
        // blooms brightness (Env3 -> SpecTilt), leaving the A/B blend fixed.
        setVoiceRoute(s, 0, kVSrcEnv3, kVDstSpecTilt, 0.45f);
        s.global.width = 1.6f;
        s.global.spread = 0.2f;
        presets.push_back(std::move(p));
    }
```

## 3. "Spectral Drift" -> "Spectral Drift"
- **Locate:** the block containing `p.name = "Spectral Drift"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** An ever-evolving spectral-morph drone: a frozen-FFT oscillator morphed against a shifted octave sine in SpectralMorph mode, run through a metallic comb, dusted with spectral bit-erosion, and jittered by three unsynced mod sources into a frozen hall — the trio's reference comb-spectral wash.
- **Coverage:** engine: SpectralFreeze; SpectralMorph mixer (tilt + shift active); filter: Comb (damping); Spectral distortion (bit-erosion); per-stage attack curve; 3-slot mod matrix incl. S&H -> Resonance (Stepped) + mod-matrix scale axis (x2); Spectral delay; Reverb Hall + preDelay + freeze; global width + spread.
- **Rationale:** Kept unchanged as the trio's anchor timbre because the directive differentiates the OTHER two AWAY from this recipe (Frozen Spectral -> SVF/Allpass+Plate; Nebula Rise -> non-spectral clean SVF-LP). Spectral Drift is the sole preset retaining Comb (type 6) + Spectral distortion + Spectral delay + FROZEN Hall, so it remains distinct from both. All fields verified against ruinae_preset_format.h: MixerState.shift is a plain float serialized verbatim and maps to the normalized [0,1] Spectral Shift param, so 0.25f (not 25.0f) is correct and distinct from tilt (denormalized dB/oct, -3.0f). The Stepped S&H->AllResonance and modMatrix scale=3 (x2) are unique-in-bank coverage.
- **Replacement code:**
```cpp
    // "Spectral Drift" - Frozen-FFT osc morphed in SpectralMorph mode, comb-
    // resonated, spectrally eroded, and drifted by three unsynced sources. This is
    // the trio's anchor: the ONLY one that keeps Comb + Spectral dist + Spectral
    // delay + frozen Hall (Frozen Spectral moves to SVF/Allpass+Plate, Nebula Rise
    // to a non-spectral clean SVF-LP path) so the three read as three timbres.
    {
        PresetDef p;
        p.name = "Spectral Drift";
        p.category = "Pads";
        auto& s = p.state;
        // Spectral-freeze body with a slight formant shift for character
        s.oscA.type = 8; // Spectral Freeze
        s.oscA.level = 0.8f;
        s.oscA.spectralTilt = 2.0f;
        s.oscA.spectralFormant = 2.0f; // shifts formants up for a vocal-ish sheen
        // Octave-up sine partner
        s.oscB.type = 0; // PolyBLEP
        s.oscB.waveform = 0; // Sine
        s.oscB.level = 0.32f;
        s.oscB.tuneSemitones = 12.0f;
        // SpectralMorph mixer with tilt + shift ACTIVE (FFT interpolation A<->B)
        s.mixer.mode = 1; // Spectral Morph
        s.mixer.position = 0.5f;
        s.mixer.tilt = -3.0f; // darken the morphed spectrum
        s.mixer.shift = 0.25f; // normalized [0,1] -> 25% inharmonic frequency shift for drift
        // Comb filter adds a metallic resonant body with damped feedback
        s.filter.type = 6; // Comb
        s.filter.cutoffHz = 3000.0f; // comb tuning
        s.filter.resonance = 0.4f;
        s.filter.combDamping = 0.35f; // tames the high comb peaks
        // Mild spectral bit-erosion for grit under the drone
        s.distortion.type = 2; // Spectral
        s.distortion.mix = 0.22f;
        s.distortion.drive = 0.3f;
        s.distortion.spectralMode = 1;
        s.distortion.spectralCurve = 3;
        s.distortion.spectralBits = 0.5f; // ~8-bit spectral crush (subtle at mix 0.22)
        // Very slow swell (per-stage exponential attack curve)
        s.ampEnv.attackMs = 800.0f;
        s.ampEnv.decayMs = 600.0f;
        s.ampEnv.sustain = 0.8f;
        s.ampEnv.releaseMs = 2800.0f;
        s.ampEnv.attackCurve = 0.5f; // slow-start exp swell
        // Three unsynced mod sources = never-repeating motion
        s.lfo1.rateHz = 0.15f; s.lfo1.shape = 0; // Sine
        s.lfo1.depth = 0.6f; s.lfo1.sync = 0;
        setModSlot(s, 0, kSrcLFO1, kDstAllMorphPos, 0.5f, kCurveLinear);
        s.modMatrix.slots[0].scale = 3; // x2 depth on the morph (exercises the scale axis)
        s.lfo2.rateHz = 0.05f; s.lfo2.shape = 1; // Triangle
        s.lfo2.depth = 0.6f; s.lfo2.sync = 0;
        setModSlot(s, 1, kSrcLFO2, kDstAllSpecTilt, 0.4f, kCurveSCurve);
        // Signature gesture: stepped S&H jitters resonance (unique to this pad)
        s.sampleHold.rateHz = 1.5f; s.sampleHold.slewMs = 100.0f;
        setModSlot(s, 2, kSrcSampleHold, kDstAllResonance, 0.25f, kCurveStepped);
        // Spectral delay smears the tail across the FFT
        s.delayEnabled = 1;
        s.delay.type = 4; // Spectral
        s.delay.sync = 0;
        s.delay.timeMs = 600.0f;
        s.delay.feedback = 0.4f;
        s.delay.mix = 0.3f;
        s.delay.spectralFFTSize = 2;
        s.delay.spectralSpreadMs = 400.0f;
        s.delay.spectralDirection = 1;
        s.delay.spectralTilt = -0.3f;
        s.delay.spectralDiffusion = 0.5f;
        s.delay.spectralWidth = 0.6f;
        // Frozen hall = an infinite, evolving spectral bed (the pad's identity)
        s.reverbEnabled = 1;
        s.reverbType = 1; // Hall
        s.reverb.size = 0.8f;
        s.reverb.mix = 0.3f; // moderate so the frozen bed sits under, not over, the dry
        s.reverb.damping = 0.4f;
        s.reverb.preDelayMs = 40.0f;
        s.reverb.diffusion = 0.85f;
        s.reverb.freeze = 1; // frozen tail -> continuous ambient bed
        s.global.width = 1.5f;
        s.global.spread = 0.35f;
        presets.push_back(std::move(p));
    }
```

## 4. "Choir" -> "Choir"
- **Locate:** the block containing `p.name = "Choir"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** An expressive breathing choir tuned to 432 Hz: a formant vowel voice with pink-noise breath, sung through a formant filter, harmonized into a scalic octave+fifth stack via the phase-vocoder, and alive under aftertouch vibrato.
- **Coverage:** engine: Formant; engine: Noise (pink); filter: Formant (morph + gender); filterEnv corrected + per-stage attack curve; per-stage release curve; aftertouch -> OscA pitch voice route; LFO -> morph + smoothed Random -> cutoff (breath); harmonizer PhaseVocoder pitchShiftMode; Reverb Hall + preDelay; tuningReferenceHz; global spread + width.
- **Rationale:** Its unique gestures are aftertouch->OscAPitch vibrato and smoothed Random->FltCut breath — neither appears elsewhere. Stacks a Formant osc INTO a Formant filter (morph+gender) for a doubly vocal timbre, then a scalic PhaseVocoder harmonizer (fifth+octave, formant-preserved, panned) builds an actual choir. filter.envAmount=18 st with filterEnv.attackCurve fixes the semitone bug audibly; ampEnv.releaseCurve gives a human fade; tuningReferenceHz=432 is a per-preset identity lever no sibling uses. Hall + preDelay is the space, no delay/heavy distortion to keep the voice clean.
- **Replacement code:**
```cpp
    // "Choir" - Formant voice + pink breath, formant-filtered, phase-vocoder
    // harmonized into a choir, tuned to 432 Hz, expressive under aftertouch.
    {
        PresetDef p;
        p.name = "Choir";
        p.category = "Pads";
        auto& s = p.state;
        // Formant oscillator = the vocal body
        s.oscA.type = 7; // Formant
        s.oscA.level = 0.8f;
        s.oscA.formantVowel = 0; // A
        s.oscA.formantMorph = 0.0f;
        // Pink noise breath layer sits just under the voice
        s.oscB.type = 9; // Noise
        s.oscB.noiseColor = 1; // Pink
        s.oscB.level = 0.09f;
        s.mixer.position = 0.14f; // mostly voice, a whisper of breath
        // Formant FILTER on top of the formant osc = doubly vocal
        s.filter.type = 5; // Formant filter
        s.filter.cutoffHz = 1200.0f;
        s.filter.resonance = 0.3f;
        s.filter.formantMorph = 1.0f; // shift the filter vowel toward 'E'
        s.filter.formantGender = -0.2f; // slightly higher/lighter formants
        // Filter env gives a vowel-brightening swell (CORRECTED semitone amount)
        s.filter.envAmount = 18.0f; // +18 st sweep (not the old 4000 bug)
        s.filterEnv.attackMs = 500.0f;
        s.filterEnv.decayMs = 900.0f;
        s.filterEnv.sustain = 0.55f;
        s.filterEnv.releaseMs = 1200.0f;
        s.filterEnv.attackCurve = 0.3f; // eased-in brightening
        // Amp env: soft swell, long expressive release tail (per-stage curve)
        s.ampEnv.attackMs = 450.0f;
        s.ampEnv.decayMs = 600.0f;
        s.ampEnv.sustain = 0.82f;
        s.ampEnv.releaseMs = 1400.0f;
        s.ampEnv.releaseCurve = 0.4f; // slow exponential fade
        // LFO slowly morphs the voice/breath balance (breath swell)
        s.lfo1.rateHz = 0.09f; s.lfo1.shape = 1; // Triangle
        s.lfo1.depth = 0.8f; s.lfo1.sync = 0;
        setModSlot(s, 0, kSrcLFO1, kDstAllMorphPos, 0.4f, kCurveLinear);
        // Smoothed Random gently animates the filter -> breathy, human air
        s.random.rateHz = 3.0f; s.random.smoothness = 0.7f;
        setModSlot(s, 1, kSrcRandom, kDstAllFltCut, 0.15f, kCurveLinear, 50.0f);
        // Playing dynamics + the signature aftertouch vibrato (owns AT->pitch)
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.35f);
        setVoiceRoute(s, 1, kVSrcAftertouch, kVDstOscAPitch, 0.12f); // press = vibrato/swell
        // Phase-vocoder harmonizer builds a scalic octave+fifth choir stack
        s.harmonizerEnabled = 1;
        s.harmonizer.harmonyMode = 1; // Scalic
        s.harmonizer.pitchShiftMode = 2; // Phase Vocoder
        s.harmonizer.formantPreserve = 1; // keep the voices natural
        s.harmonizer.numVoices = 2;
        s.harmonizer.voiceInterval[0] = 7;  // fifth
        s.harmonizer.voiceInterval[1] = 12; // octave
        s.harmonizer.voiceLevelDb[0] = -6.0f;
        s.harmonizer.voiceLevelDb[1] = -8.0f;
        s.harmonizer.voicePan[0] = -0.4f;
        s.harmonizer.voicePan[1] = 0.4f;
        s.harmonizer.wetLevelDb = -4.0f;
        s.harmonizer.dryLevelDb = 0.0f;
        // Cathedral hall
        s.reverbEnabled = 1;
        s.reverbType = 1; // Hall
        s.reverb.size = 0.8f;
        s.reverb.mix = 0.35f;
        s.reverb.damping = 0.5f;
        s.reverb.preDelayMs = 25.0f;
        s.reverb.diffusion = 0.8f;
        // Warm 432 Hz tuning for the choir
        s.settings.tuningReferenceHz = 432.0f;
        s.global.polyphony = 8;
        s.global.spread = 0.45f;
        s.global.width = 1.3f;
        presets.push_back(std::move(p));
    }
```

## 5. "Dark Matter" -> "Dark Matter"
- **Locate:** the block containing `p.name = "Dark Matter"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A dense, collapsing dark pad: a Rossler-attractor oscillator (Y-axis) over a brown-noise floor, driven through a low overdriven 24 dB ladder, torn by a wavefolder, and animated by fast Rossler chaosMod + Rungler + a SmoothRandom LFO with a corrected +30-semitone filter bloom — sealed inside an intimate, tight, heavily-damped small Plate that contrasts hard against Abyssal's cavernous Hall.
- **Coverage:** engine: Chaos (Rossler, Y-axis output); engine: Noise (brown); filter: Ladder (slope + drive); filterEnv corrected (+30 st) + per-stage curve; Wavefolder distortion (foldType); exotic mod sources: chaosMod (Rossler type) + Rungler; 3-slot mod matrix (Chaos->Cut Exp, LFO->Morph, Rungler->Tilt); per-stage decay curve; Reverb Plate SMALL + short preDelay (hard space contrast vs Abyssal); global width + spread.
- **Rationale:** Breaks the Dark Matter / Abyssal duplication by moving four load-bearing axes off the shared core while keeping Dark Matter's identity as the chaos-drone pad. Attractor Lorenz->Rossler with chaosOutput on the Y axis gives a spiralling, quasi-periodic body audibly unlike Abyssal's Lorenz-X noise; chaosMod switched to Rossler at rateHz=1.1 (vs the slow 0.3) makes the motion agitated and fast. Distortion moves TapeSaturator->Wavefolder for metallic harmonic tearing — its owned dirt. Space is inverted to a SMALL Plate, the hardest possible contrast to a cavernous Hall+granular void. Retains the 700 Hz 24 dB ladder dark spine, the corrected +30 st bloom, decayCurve log shape, and the unrepeatable three-slot Chaos/LFO/Rungler matrix.
- **Replacement code:**
```cpp
    // "Dark Matter" - Rossler chaos over brown noise, low overdriven ladder,
    // wavefolder grit, torn open by fast Rossler chaosMod + rungler + SmoothRandom,
    // sealed in a tight, damped small Plate (hard contrast vs Abyssal's Hall).
    {
        PresetDef p;
        p.name = "Dark Matter";
        p.category = "Pads";
        auto& s = p.state;
        // Chaotic Rossler body on the Y axis - spiralling, quasi-pitched, distinct
        // from Abyssal's Lorenz-X core.
        s.oscA.type = 5; // Chaos
        s.oscA.chaosAttractor = 1; // Rossler
        s.oscA.chaosAmount = 0.5f; // quasi-pitched spiral, not full noise
        s.oscA.chaosCoupling = 0.3f;
        s.oscA.chaosOutput = 1; // Y axis
        s.oscA.level = 0.7f;
        // Brown noise floor for subterranean rumble
        s.oscB.type = 9; // Noise
        s.oscB.noiseColor = 2; // Brown
        s.oscB.level = 0.16f;
        s.mixer.position = 0.22f;
        // Low overdriven 24 dB ladder = the dark core
        s.filter.type = 4; // Ladder LP
        s.filter.cutoffHz = 700.0f;
        s.filter.resonance = 0.42f;
        s.filter.ladderSlope = 4; // 24 dB/oct
        s.filter.ladderDrive = 6.0f; // pushes the ladder into saturation
        // Big CORRECTED filter-env bloom: +30 st slow sweep (was the 4000 Hz bug)
        s.filter.envAmount = 30.0f;
        s.filterEnv.attackMs = 900.0f; // slow cavernous opening
        s.filterEnv.decayMs = 1400.0f;
        s.filterEnv.sustain = 0.5f;
        s.filterEnv.releaseMs = 2000.0f;
        s.filterEnv.attackCurve = 0.4f; // eased-in bloom
        // Wavefolder grit - metallic harmonic tearing (owned dirt vs Abyssal's tape)
        s.distortion.type = 4; // Wavefolder
        s.distortion.foldType = 1;
        s.distortion.drive = 0.4f;
        s.distortion.character = 0.55f;
        s.distortion.mix = 0.6f;
        // Slow amp swell with a log-ish decay
        s.ampEnv.attackMs = 800.0f;
        s.ampEnv.decayMs = 1000.0f;
        s.ampEnv.sustain = 0.6f;
        s.ampEnv.releaseMs = 2200.0f;
        s.ampEnv.decayCurve = -0.3f; // fast-start log decay
        // Signature exotic mod trio - FAST Rossler chaos-motion tempo
        s.chaosMod.rateHz = 1.1f; s.chaosMod.type = 1; // Rossler
        s.chaosMod.depth = 0.6f;
        setModSlot(s, 0, kSrcChaos, kDstAllFltCut, 0.5f, kCurveExp, 100.0f);
        s.lfo1.rateHz = 0.08f; s.lfo1.shape = 5; // Smooth Random
        s.lfo1.depth = 0.4f; s.lfo1.sync = 0;
        setModSlot(s, 1, kSrcLFO1, kDstAllMorphPos, 0.3f, kCurveLinear);
        s.rungler.osc1FreqHz = 1.5f; s.rungler.osc2FreqHz = 2.3f;
        s.rungler.depth = 0.35f; s.rungler.bits = 5; s.rungler.filter = 0.3f;
        setModSlot(s, 2, kSrcRungler, kDstAllSpecTilt, 0.28f);
        // SMALL, tight, damped plate = intimate, close, menacing (hard contrast
        // against Abyssal's cavernous Hall + granular void).
        s.reverbEnabled = 1;
        s.reverbType = 0; // Plate
        s.reverb.size = 0.38f;
        s.reverb.mix = 0.3f;
        s.reverb.damping = 0.7f;
        s.reverb.preDelayMs = 8.0f;
        s.reverb.diffusion = 0.8f;
        s.global.width = 1.3f;
        s.global.spread = 0.25f;
        s.global.polyphony = 6;
        presets.push_back(std::move(p));
    }
```

## 6. "Harmonic Heaven" -> "Harmonic Heaven"
- **Locate:** the block containing `p.name = "Harmonic Heaven"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A shimmering DUAL-WAVETABLE choir harmonized to diatonic thirds and fifths, breathing through a lush chorus and plate — the bank's flagship wavetable pad.
- **Coverage:** engine: Wavetable (both oscillators); wavetable phaseMod; filter: Ladder (slope + drive); filterEnv corrected (semitone envAmount); per-stage attack curve; harmonizer PhaseVocoder; Chorus FX; Reverb Plate; keyTrack voice route; LFO SCurve -> EffectMix + LFO SCurve -> MorphPos.
- **Rationale:** Moves both oscillators onto the untouched Wavetable engine and exploits its phaseMod for shimmer, killing the dual-saw sameness. Filter is a real Ladder with 4 dB drive and 24 dB slope. The filterEnv bug is fixed to a musical +20 semitones so the bloom is audible. Two slow free-running LFOs on SCurve give a dual mod gesture (wet-mix breath + wavetable-blend morph) no sibling repeats; Chorus + Plate keep it distinct from the hall-drenched siblings.
- **Replacement code:**
```cpp
    // "Harmonic Heaven" - Dual-wavetable choir, diatonic harmonizer, chorus + plate
    {
        PresetDef p;
        p.name = "Harmonic Heaven";
        p.category = "Pads";
        auto& s = p.state;
        // IDENTITY: the never-used WAVETABLE engine on BOTH oscillators (was dual saw).
        s.oscA.type = 1;             // Wavetable
        s.oscA.waveform = 1;         // saw table = bright fundamental
        s.oscA.phaseMod = 0.3f;      // wavetable phase-mod for FM-ish upper shimmer
        s.oscA.level = 0.7f;
        s.oscB.type = 1;             // Wavetable
        s.oscB.waveform = 2;         // square table = hollow partner voice
        s.oscB.fineCents = 7.0f;     // gentle beating against A for width
        s.oscB.phaseMod = 0.15f;
        s.oscB.level = 0.55f;
        s.mixer.position = 0.45f;    // slightly favour A
        // Ladder filter exercised with real drive/slope (not the stock SVF LP).
        s.filter.type = 4;           // Ladder
        s.filter.cutoffHz = 3200.0f;
        s.filter.resonance = 0.25f;
        s.filter.ladderSlope = 4;    // 24 dB/oct
        s.filter.ladderDrive = 4.0f; // warm valve-ish saturation into the ladder
        s.filter.keyTrack = 0.3f;    // filter opens with pitch for natural top
        // FILTER-ENV BUG FIX: envAmount is SEMITONES (was 4000 in acid template).
        s.filter.envAmount = 20.0f;  // +20 st bloom as each note swells in
        s.filterEnv.attackMs = 220.0f; s.filterEnv.decayMs = 900.0f;
        s.filterEnv.sustain = 0.5f;  s.filterEnv.releaseMs = 1000.0f;
        // Slow-swell amp env with a genuine per-stage attack curve.
        s.ampEnv.attackMs = 350.0f;  s.ampEnv.decayMs = 600.0f;
        s.ampEnv.sustain = 0.8f;     s.ampEnv.releaseMs = 1600.0f;
        s.ampEnv.attackCurve = 0.4f; // slow-start (exp) swell, not linear
        // Diatonic harmony stack via PhaseVocoder (high-quality formant-neutral).
        s.harmonizerEnabled = 1;
        s.harmonizer.harmonyMode = 1;    // Scalic
        s.harmonizer.key = 0; s.harmonizer.scale = 0; // C Major
        s.harmonizer.pitchShiftMode = 2; // PhaseVocoder
        s.harmonizer.numVoices = 2;
        s.harmonizer.dryLevelDb = 0.0f; s.harmonizer.wetLevelDb = -4.0f;
        s.harmonizer.voiceInterval[0] = 2; s.harmonizer.voicePan[0] = -0.4f; // 3rd L
        s.harmonizer.voiceInterval[1] = 4; s.harmonizer.voicePan[1] = 0.4f;  // 5th R
        // Lush chorus is this pad's modulation-FX signature (owns Chorus for the cat).
        s.modulationType = 3;        // Chorus
        s.chorus.rateHz = 0.35f; s.chorus.depth = 0.5f;
        s.chorus.voices = 3; s.chorus.mix = 0.35f; s.chorus.stereoSpread = 200.0f;
        // PLATE reverb (not the reflexive giant hall) keeps the choir intimate.
        s.reverbType = 0;            // Plate
        s.reverbEnabled = 1;
        s.reverb.size = 0.7f; s.reverb.mix = 0.3f;
        s.reverb.damping = 0.4f; s.reverb.diffusion = 0.8f;
        s.global.width = 1.5f;
        // MOD IDENTITY: two slow free LFOs on non-default SCurve. One breathes the
        // wet mix, the other morphs the A/B wavetable blend - unique to this preset.
        s.lfo1.rateHz = 0.1f;  s.lfo1.shape = 0; s.lfo1.depth = 0.5f; s.lfo1.sync = 0;
        setModSlot(s, 0, kSrcLFO1, kDstEffectMix, 0.25f, kCurveSCurve);
        s.lfo2.rateHz = 0.07f; s.lfo2.shape = 0; s.lfo2.depth = 0.4f; s.lfo2.sync = 0;
        setModSlot(s, 1, kSrcLFO2, kDstAllMorphPos, 0.3f, kCurveSCurve, 80.0f);
        // Velocity + key-track shape brightness per note.
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.4f);
        setVoiceRoute(s, 1, kVSrcKeyTrack, kVDstFltCut, 0.35f);
        presets.push_back(std::move(p));
    }
```

## 7. "Arctic Frost" -> "Arctic Frost"
- **Locate:** the block containing `p.name = "Arctic Frost"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A dense 128-partial additive ice pad: low end carved by a global high-pass, top lifted by a voice hi-shelf air boost, its morph driven by a slow FREE-RUNNING triangle LFO, with resonance drifting on a smoothed-random LFO through the x2 scale axis into a pre-delayed modulated Hall smeared by a spectral delay.
- **Coverage:** engine: Additive (128 partials = densest of the batch, tilt); filter: SVF HP (global filter low-cut); filter: SVF Hi-Shelf svfGain (voice air boost); global filter enabled; morph motion: FREE LFO (un-synced triangle -> MorphPos); LFO SmoothRandom shape + symmetry -> Resonance; mod-matrix scale axis x2; Spectral delay (FFT smear); Reverb Hall + preDelay + modRate/modDepth; global width + spread.
- **Rationale:** Assigned the batch's FREE-LFO morph: an un-synced (sync=0) triangle LFO1 at 0.08 Hz drives kDstAllMorphPos - previously this preset drove morph from Random, which is now Dreamscape's job. Takes the densest additivePartials=128 (top of the 12/40/80/128 spread). Keeps the global-HP-plus-voice-hi-shelf two-filter trick and the untouched scale=x2 axis, and swaps its space to Hall+Spectral-delay so no sibling shares its reverb+delay combo.
- **Replacement code:**
```cpp
    // "Arctic Frost" - DENSE 128-partial additive ice pad. Global HP low-cut + a
    // voice SVF Hi-Shelf air boost. Morph driven by a FREE-RUNNING LFO (the batch's
    // free-LFO member). Modulated Hall + a Spectral delay smear.
    {
        PresetDef p;
        p.name = "Arctic Frost";
        p.category = "Pads";
        auto& s = p.state;
        s.oscA.type = 4;              // Additive
        s.oscA.additivePartials = 128; // densest spectrum of the batch
        s.oscA.additiveTilt = 4.0f;   // +tilt = bright, thin, cold top
        s.oscA.level = 0.6f;
        s.oscB.type = 0; s.oscB.waveform = 4; // Triangle, soft body
        s.oscB.tuneSemitones = 12.0f; s.oscB.level = 0.35f; // octave-up sparkle
        s.mixer.position = 0.35f;
        // GLOBAL filter as the HP low-cut...
        s.globalFilter.enabled = 1;
        s.globalFilter.type = 1;     // HP
        s.globalFilter.cutoffHz = 300.0f; s.globalFilter.resonance = 0.6f;
        // ...and the VOICE filter as an SVF Hi-Shelf air boost (owns svfGain).
        s.filter.type = 10;          // SVF Hi Shelf
        s.filter.cutoffHz = 4500.0f; // shelf corner
        s.filter.svfGain = 9.0f;     // +9 dB of icy air above 4.5 kHz
        s.filter.resonance = 0.2f; s.filter.svfSlope = 1;
        s.ampEnv.attackMs = 600.0f; s.ampEnv.decayMs = 800.0f;
        s.ampEnv.sustain = 0.65f;   s.ampEnv.releaseMs = 2500.0f;
        // Big MODULATED HALL with pre-delay = the frozen cathedral of ice.
        s.reverbType = 1;            // Hall
        s.reverbEnabled = 1;
        s.reverb.size = 0.95f; s.reverb.mix = 0.5f;
        s.reverb.damping = 0.15f; s.reverb.diffusion = 0.9f;
        s.reverb.preDelayMs = 20.0f;
        s.reverb.modRateHz = 0.2f; s.reverb.modDepth = 0.15f; // shimmering tail
        // SPECTRAL delay: an icy FFT smear (Hall+Spectral pairing, unique in the batch)
        s.delayEnabled = 1;
        s.delay.type = 4;            // Spectral
        s.delay.sync = 0;
        s.delay.timeMs = 450.0f;
        s.delay.feedback = 0.4f;
        s.delay.mix = 0.28f;
        s.delay.spectralFFTSize = 2; // 2048
        s.delay.spectralSpreadMs = 400.0f;
        s.delay.spectralDirection = 0;
        s.delay.spectralTilt = 0.2f; // bright smear
        s.delay.spectralDiffusion = 0.5f;
        s.delay.spectralWidth = 0.6f;
        s.global.width = 1.8f; s.global.spread = 0.4f;
        // MORPH MOTION = FREE LFO: a slow un-synced triangle sweeps the additive morph.
        s.lfo1.rateHz = 0.08f; s.lfo1.shape = 1; // Triangle
        s.lfo1.depth = 0.6f; s.lfo1.sync = 0;    // free-running (not tempo-synced)
        setModSlot(s, 0, kSrcLFO1, kDstAllMorphPos, 0.35f, kCurveSCurve);
        // Secondary coverage: a SmoothRandom LFO (skewed via symmetry) drifts resonance,
        // pushed through the scale=x2 axis for a deeper-than-unity sweep no other pad uses.
        s.lfo2.rateHz = 0.04f; s.lfo2.shape = 5; // Smooth Random
        s.lfo2.depth = 0.3f; s.lfo2.sync = 0;
        s.lfo2Ext.symmetry = 0.7f;   // asymmetric wander
        setModSlot(s, 1, kSrcLFO2, kDstAllResonance, 0.2f, kCurveLinear, 100.0f);
        s.modMatrix.slots[1].scale = 3; // x2 depth (the untouched scale axis)
        presets.push_back(std::move(p));
    }
```

## 8. "Velvet Strings" -> "Velvet Strings"
- **Locate:** the block containing `p.name = "Velvet Strings"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A vintage octave-stacked saw string ensemble with a slow filter-env bloom, tape-saturated warmth, a 2-stage phaser sweep, and a subtle tape echo into a plate.
- **Coverage:** engine: PolyBLEP dual saw (octave stack); filter: SVF LP with svfDrive; filterEnv corrected + modEnv -> cutoff bloom; per-stage decay curve (modEnv); TapeSaturator distortion; Phaser FX (2-stage); Tape delay; Reverb Plate.
- **Rationale:** Fixes the plainest-of-the-batch problem: the once-static dual saw now has internal motion from a slow curved modEnv-driven cutoff bloom plus a corrected +15 semitone filterEnv, so the timbre evolves after note-on. TapeSaturator distortion and svfDrive add vintage grit that separates its tone from Harmonic Heaven's clean wavetables; Phaser + Tape delay + small Plate give it a self-contained vintage-string FX signature.
- **Replacement code:**
```cpp
    // "Velvet Strings" - Vintage saw ensemble: tape-warm, phaser sweep, modEnv bloom
    {
        PresetDef p;
        p.name = "Velvet Strings";
        p.category = "Pads";
        auto& s = p.state;
        s.oscA.type = 0; s.oscA.waveform = 1; s.oscA.level = 0.7f; // saw
        s.oscB.type = 0; s.oscB.waveform = 1;
        s.oscB.tuneSemitones = -12.0f; s.oscB.fineCents = 5.0f;    // octave-down body + beat
        s.oscB.level = 0.5f;
        s.mixer.position = 0.4f;
        // SVF LP with post-filter drive = analog grit that distinguishes it from HH's Ladder.
        s.filter.type = 0;           // SVF LP
        s.filter.cutoffHz = 3200.0f; s.filter.resonance = 0.15f;
        s.filter.svfDrive = 3.0f;    // gentle saturation in the filter
        // NOT STATIC: a corrected filter-env AND a slow modEnv both bloom the cutoff.
        s.filter.envAmount = 15.0f;  // +15 st (fixed semitone value, was garbage 4000)
        s.filterEnv.attackMs = 400.0f; s.filterEnv.decayMs = 1200.0f;
        s.filterEnv.sustain = 0.4f;    s.filterEnv.releaseMs = 900.0f;
        s.ampEnv.attackMs = 250.0f; s.ampEnv.decayMs = 700.0f;
        s.ampEnv.sustain = 0.75f;   s.ampEnv.releaseMs = 1200.0f;
        // Mod env drives a long, curved cutoff bloom (per-stage decay curve).
        s.modEnv.attackMs = 800.0f; s.modEnv.decayMs = 2500.0f;
        s.modEnv.sustain = 0.0f;    s.modEnv.releaseMs = 1500.0f;
        s.modEnv.decayCurve = 0.5f; // slow-tail exponential decay for a natural swell
        setVoiceRoute(s, 0, kVSrcEnv3, kVDstFltCut, 0.5f);      // modEnv -> cutoff bloom
        setVoiceRoute(s, 1, kVSrcVelocity, kVDstFltCut, 0.3f);  // dynamics
        // CHARACTER: TapeSaturator distortion for warm vintage-console glue.
        s.distortion.type = 5;       // TapeSaturator
        s.distortion.drive = 0.3f; s.distortion.character = 0.5f;
        s.distortion.tapeModel = 0; s.distortion.tapeSaturation = 0.4f;
        s.distortion.tapeBias = 0.5f; s.distortion.mix = 0.6f; // level-compensated blend
        // Slow 2-stage phaser sweep.
        s.modulationType = 1;        // Phaser
        s.phaser.rateHz = 0.2f; s.phaser.depth = 0.35f;
        s.phaser.feedback = 0.4f; s.phaser.mix = 0.3f; s.phaser.stages = 2;
        // Warm TAPE delay whisper.
        s.delayEnabled = 1;
        s.delay.type = 1;            // Tape
        s.delay.mix = 0.15f; s.delay.feedback = 0.25f;
        s.delay.tapeSaturation = 0.3f; s.delay.tapeWear = 0.2f;
        // PLATE, small and tight - a rehearsal-room, not a cathedral.
        s.reverbType = 0;            // Plate
        s.reverbEnabled = 1;
        s.reverb.size = 0.6f; s.reverb.mix = 0.25f;
        s.global.width = 1.5f; s.global.spread = 0.3f;
        presets.push_back(std::move(p));
    }
```

## 9. "Quantum Field" -> "Quantum Field"
- **Locate:** the block containing `p.name = "Quantum Field"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A shimmering particle-swarm morphing spectrally into an inharmonic additive body, jittered by sample-and-hold spectral tilt, pitch-bending under aftertouch, granular-delayed into a hall.
- **Coverage:** engine: Particle (Blackman grains, drift); engine: Additive (inharmonic, B); SpectralMorph mixer + tilt; S&H source -> SpecTilt (SCurve); LFO -> MorphPos; modEnv -> cutoff bloom; Bezier envelope mode (amp); Spectral distortion; Granular delay; aftertouch -> OscB pitch voice route; Reverb Hall; global width + spread.
- **Rationale:** The batch's coverage powerhouse: keeps the unique Particle+Additive SpectralMorph core but now also exercises the SpectralMorph tilt, a hand-built Bezier amp curve, an aftertouch->OscB-pitch performance route, subtle Spectral distortion, and a Granular delay throwing octave-up shards. Three complementary mod gestures (LFO morph sweep, stepped S&H tilt, aftertouch pitch) keep it in constant motion. spectralBits 8 and distortion mix 0.3 keep the character subtle and level-safe.
- **Replacement code:**
```cpp
    // "Quantum Field" - Particle<->additive spectral morph, S&H tilt, aftertouch, granular hall
    {
        PresetDef p;
        p.name = "Quantum Field";
        p.category = "Pads";
        auto& s = p.state;
        s.oscA.type = 6;             // Particle
        s.oscA.particleScatter = 4.0f; s.oscA.particleDensity = 24.0f;
        s.oscA.particleLifetime = 600.0f; // long grains = smooth cloud
        s.oscA.particleSpawnMode = 1;     // Random spawn
        s.oscA.particleEnvType = 3;       // Blackman grain window (smooth)
        s.oscA.particleDrift = 0.2f;      // slow pitch wander
        s.oscA.level = 0.7f;
        s.oscB.type = 4;             // Additive
        s.oscB.additivePartials = 32; s.oscB.additiveInharm = 0.2f; // bell-like
        s.oscB.additiveTilt = -2.0f; s.oscB.level = 0.5f;
        // SPECTRAL MORPH mixer - FFT interpolation between the two spectra, with tilt.
        s.mixer.mode = 1;            // SpectralMorph
        s.mixer.position = 0.5f; s.mixer.tilt = 3.0f; // brighten the morphed spectrum
        s.filter.type = 0; s.filter.cutoffHz = 6000.0f; s.filter.resonance = 0.15f;
        // BEZIER amp envelope: a hand-shaped slow-then-fast swell (never used elsewhere).
        s.ampEnv.attackMs = 700.0f; s.ampEnv.decayMs = 500.0f;
        s.ampEnv.sustain = 0.85f;   s.ampEnv.releaseMs = 2000.0f;
        s.ampEnv.bezierEnabled = 1.0f;
        s.ampEnv.bezierAttackCp1X = 0.25f; s.ampEnv.bezierAttackCp1Y = 0.08f;
        s.ampEnv.bezierAttackCp2X = 0.60f; s.ampEnv.bezierAttackCp2Y = 0.35f;
        // MOD IDENTITY: LFO sweeps the spectral morph; S&H steps the tilt (SCurve).
        s.lfo1.rateHz = 0.1f; s.lfo1.shape = 0; s.lfo1.depth = 0.7f; s.lfo1.sync = 0;
        setModSlot(s, 0, kSrcLFO1, kDstAllMorphPos, 0.5f);
        s.sampleHold.rateHz = 1.5f; s.sampleHold.slewMs = 150.0f;
        setModSlot(s, 1, kSrcSampleHold, kDstAllSpecTilt, 0.35f, kCurveSCurve);
        // Velocity nudges morph; mod env blooms cutoff; AFTERTOUCH bends the additive layer.
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstMorphPos, 0.3f);
        setVoiceRoute(s, 1, kVSrcEnv3, kVDstFltCut, 0.35f);
        setVoiceRoute(s, 2, kVSrcAftertouch, kVDstOscBPitch, 0.3f); // press = detune B
        s.modEnv.attackMs = 500.0f; s.modEnv.decayMs = 2000.0f;
        s.modEnv.sustain = 0.0f;    s.modEnv.releaseMs = 1500.0f;
        // Subtle SPECTRAL distortion adds crystalline bit/phase character.
        s.distortion.type = 2;       // Spectral
        s.distortion.drive = 0.25f; s.distortion.spectralMode = 0;
        s.distortion.spectralBits = 8.0f; s.distortion.mix = 0.3f;
        // GRANULAR delay throws octave-up shards behind the cloud.
        s.delayEnabled = 1;
        s.delay.type = 3;            // Granular
        s.delay.mix = 0.3f; s.delay.feedback = 0.3f;
        s.delay.granularSizeMs = 120.0f; s.delay.granularDensity = 20.0f;
        s.delay.granularPitch = 12.0f; s.delay.granularPitchSpray = 0.2f;
        s.delay.granularTexture = 0.3f;
        // HALL for the vast quantum space.
        s.reverbType = 1;            // Hall
        s.reverbEnabled = 1;
        s.reverb.size = 0.85f; s.reverb.mix = 0.4f; s.reverb.diffusion = 0.85f;
        s.global.width = 1.6f; s.global.spread = 0.4f;
        presets.push_back(std::move(p));
    }
```

## 10. "Cathedral Organ" -> "Cathedral Organ"
- **Locate:** the block containing `p.name = "Cathedral Organ"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A dual-square pipe organ stacked octave-down/fifth/octave-up via a clean PitchSync harmonizer, tuned to A=432, tremulant-wobbling into a huge pre-delayed hall.
- **Coverage:** engine: PolyBLEP square (organ); harmonizer PitchSync pitchShiftMode; tuningReferenceHz (432); settings velocityCurve = Fixed (organ ranks ignore velocity); tremulant: LFO -> MasterVol; Reverb Hall + preDelay; global width.
- **Rationale:** Differentiates from Harmonic Heaven (the other harmonizer pad) by using the PitchSync shift mode instead of PhaseVocoder, and gives it a real per-preset mod gesture — a 5.5 Hz LFO->MasterVol tremulant — so it is no longer a static, mod-less voice. Owns the tuningReferenceHz settings axis (A=432) and the pre-delayed Hall depth. Tremulant depth 0.18 x LFO depth 0.5 keeps the amplitude wobble musical, not seasick.
- **Replacement code:**
```cpp
    // "Cathedral Organ" - Dual-square pipe stack, PitchSync harmonizer, 432 Hz, tremulant hall
    {
        PresetDef p;
        p.name = "Cathedral Organ";
        p.category = "Pads";
        auto& s = p.state;
        s.oscA.type = 0; s.oscA.waveform = 2; s.oscA.level = 0.7f; // Square = organ reed
        s.oscB.type = 0; s.oscB.waveform = 2;
        s.oscB.tuneSemitones = 12.0f; s.oscB.level = 0.4f;         // octave-up rank
        s.mixer.position = 0.35f;
        s.filter.type = 0; s.filter.cutoffHz = 5500.0f; s.filter.resonance = 0.1f;
        // Quick-on, high-sustain organ amp shape.
        s.ampEnv.attackMs = 120.0f; s.ampEnv.decayMs = 400.0f;
        s.ampEnv.sustain = 0.9f;    s.ampEnv.releaseMs = 900.0f;
        // PITCH-SYNC harmonizer draws the pipe ranks (owns PitchSync mode for the category).
        s.harmonizerEnabled = 1;
        s.harmonizer.harmonyMode = 0;    // Chromatic (fixed intervals = drawbars)
        s.harmonizer.pitchShiftMode = 3; // PitchSync (clean, phase-locked octaves)
        s.harmonizer.numVoices = 3;
        s.harmonizer.dryLevelDb = 0.0f; s.harmonizer.wetLevelDb = -3.0f;
        s.harmonizer.voiceInterval[0] = -12; // 16' sub rank
        s.harmonizer.voiceInterval[1] = 7;   s.harmonizer.voicePan[1] = -0.3f; // fifth
        s.harmonizer.voiceInterval[2] = 12;  s.harmonizer.voicePan[2] = 0.3f;  // octave
        // SETTINGS axis: tune the whole instrument to A=432 for a period pipe feel.
        s.settings.tuningReferenceHz = 432.0f;
        s.settings.velocityCurve = 3; // Fixed - pipe ranks speak at a constant level, ignoring key velocity
        // MOD IDENTITY: a fast free LFO on Master Volume = the organ TREMULANT (not static).
        s.lfo1.rateHz = 5.5f; s.lfo1.shape = 0; s.lfo1.depth = 0.5f; s.lfo1.sync = 0;
        setModSlot(s, 0, kSrcLFO1, kDstMasterVol, 0.18f, kCurveSCurve);
        // Enormous PRE-DELAYED HALL = the nave.
        s.reverbType = 1;            // Hall
        s.reverbEnabled = 1;
        s.reverb.size = 0.9f; s.reverb.mix = 0.4f;
        s.reverb.damping = 0.35f; s.reverb.diffusion = 0.85f;
        s.reverb.preDelayMs = 40.0f; // distance to the far wall
        s.global.width = 1.3f;
        presets.push_back(std::move(p));
    }
```

## 11. "Nebula Rise" -> "Nebula Rise"
- **Locate:** the block containing `p.name = "Nebula Rise"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A vast, ever-drifting cloud steered by a Rossler chaos body and slow smooth-random motion, shaped by a NON-spectral SVF low-pass with a long filter-env sweep and left tonally CLEAN (no spectral distortion) so it blooms into a modulated hall as its own chaos-driven timbre.
- **Coverage:** engine-spectralfreeze; engine-chaos (Rossler, Y axis); spectralmorph-mixer; filter: SVF LP (non-spectral) with corrected filterEnv sweep; modEnv; per-stage attack + release curves; lfo-smoothrandom-shape; lfo-symmetry; chaos-mod-source; 3-slot mod matrix (SCurve/Exp/Linear); aftertouch-to-pitch-voice-route; reverb-hall + predelay + modrate/moddepth (modulated, non-frozen); global-width-spread.
- **Rationale:** Directive compliance: (1) NON-spectral filter — SVF LP (type 0); the +22 st envAmount against a 2500 Hz cutoff with a multi-second filterEnv gives the audible slow sweep a spectral/comb filter could not. (2) DROPPED the spectral distortion so distortion stays at its Clean (type 0) default; spectral-distortion coverage for the suite is preserved by Spectral Drift. Identity now rests on the Rossler chaos B osc, SmoothRandom LFO1 + lfo1Ext.symmetry=0.7, chaos-mod source, and a MODULATED non-frozen Hall (reverb.freeze left 0) — versus Spectral Drift's frozen Hall. This makes the three siblings three distinct timbres and three distinct spaces.
- **Replacement code:**
```cpp
    // "Nebula Rise" - Generative wash steered by chaos + smooth-random. Uses a
    // NON-spectral SVF-LP filter and is left tonally CLEAN (spectral distortion
    // dropped) so it reads as its own chaos-driven timbre, distinct from Spectral
    // Drift's frozen comb-spectral wash and Frozen Spectral's SVF/Allpass+Plate.
    {
        PresetDef p;
        p.name = "Nebula Rise";
        p.category = "Pads";
        auto& s = p.state;
        // OSC A: Spectral Freeze drone, slightly tilted + formant-shifted for shimmer
        s.oscA.type = 8; // Spectral Freeze
        s.oscA.spectralTilt = 3.0f; s.oscA.spectralPitch = 0.0f;
        s.oscA.spectralFormant = 2.0f; // detached formant adds airy sheen
        s.oscA.level = 0.7f;
        // OSC B: Rossler chaos attractor - quasi-pitched noisy motion under the drone
        s.oscB.type = 5; // Chaos
        s.oscB.chaosAttractor = 1; // Rossler
        s.oscB.chaosAmount = 0.3f; s.oscB.chaosCoupling = 0.25f;
        s.oscB.chaosOutput = 1; // Y axis - different timbre than default X
        s.oscB.level = 0.28f;
        // MIXER: SpectralMorph (FFT interpolation A<->B) so morph-pos sweeps are spectral, not a plain crossfade
        s.mixer.mode = 1; // SpectralMorph
        s.mixer.position = 0.35f;
        s.mixer.tilt = -4.0f; // darken the morphed spectrum
        s.mixer.shift = 12.0f; // small inharmonic freq shift for otherworldliness
        // FILTER: SVF LP (NON-spectral) kept below max so the corrected filter-env sweep is audible
        s.filter.type = 0; // SVF LP
        s.filter.cutoffHz = 2500.0f; s.filter.resonance = 0.3f;
        s.filter.envAmount = 22.0f; // +22 semitones = big, SLOW audible sweep (bug-fixed range)
        s.filterEnv.attackMs = 2000.0f; s.filterEnv.decayMs = 3500.0f;
        s.filterEnv.sustain = 0.35f; s.filterEnv.releaseMs = 4000.0f;
        // AMP: very long swell + 3s tail, eased curves for a gentle fade in/out
        s.ampEnv.attackMs = 1200.0f; s.ampEnv.decayMs = 800.0f;
        s.ampEnv.sustain = 0.85f; s.ampEnv.releaseMs = 3000.0f;
        s.ampEnv.attackCurve = 0.4f;  // slow-start swell
        s.ampEnv.releaseCurve = 0.3f; // lingering exponential tail
        // LFO1: Smooth-Random - the signature unrepeatable wander (skewed by symmetry)
        s.lfo1.rateHz = 0.08f; s.lfo1.shape = 5; // SmoothRandom
        s.lfo1.depth = 0.8f; s.lfo1.sync = 0;
        s.lfo1Ext.symmetry = 0.7f; // asymmetric ramp -> uneven, organic motion
        setModSlot(s, 0, kSrcLFO1, kDstAllFltCut, 0.4f, kCurveSCurve);
        // Chaos-mod source (Rossler) slowly warps the spectral morph position
        s.chaosMod.rateHz = 0.15f; s.chaosMod.type = 1; // Rossler
        s.chaosMod.depth = 0.4f;
        setModSlot(s, 1, kSrcChaos, kDstAllMorphPos, 0.35f, kCurveExp, 150.0f);
        // LFO2 (triangle) drifts spectral tilt for slow brightness breathing
        s.lfo2.rateHz = 0.04f; s.lfo2.shape = 1; // Triangle
        s.lfo2.depth = 0.5f; s.lfo2.sync = 0;
        setModSlot(s, 2, kSrcLFO2, kDstAllSpecTilt, 0.3f);
        // ModEnv -> spectral tilt: a slow onset bloom of brightness per note
        s.modEnv.attackMs = 800.0f; s.modEnv.decayMs = 2000.0f;
        s.modEnv.sustain = 0.4f; s.modEnv.releaseMs = 3000.0f;
        setVoiceRoute(s, 0, kVSrcEnv3, kVDstSpecTilt, 0.4f);
        // Aftertouch -> OSC A pitch: press for a subtle rising detune, keeps it alive under the hand
        setVoiceRoute(s, 1, kVSrcAftertouch, kVDstOscAPitch, 0.12f);
        // Distortion intentionally CLEAN (type 0 default): spectral distortion dropped
        // so Nebula Rise's timbre is the chaos+freeze body itself, not a spectrally-
        // crushed wash like Spectral Drift.
        // Reverb: modulated HALL with pre-delay for a huge, gently-swirling space (NOT frozen)
        s.reverbEnabled = 1; s.reverbType = 1; // Hall
        s.reverb.size = 0.9f; s.reverb.mix = 0.5f;
        s.reverb.damping = 0.25f; s.reverb.diffusion = 0.9f;
        s.reverb.preDelayMs = 40.0f;
        s.reverb.modRateHz = 0.3f; s.reverb.modDepth = 0.25f; // chorused tail
        s.global.width = 1.5f; s.global.spread = 0.4f;
        presets.push_back(std::move(p));
    }
```

## 12. "Warm Tape" -> "Warm Tape"
- **Locate:** the block containing `p.name = "Warm Tape"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A mellow, saturated analog pad — a resonant phase-distortion core and a wavetable triangle poured through a driven 24 dB ladder, tape-saturated, echoing off a vintage tape delay into a plate.
- **Coverage:** engine-phasedistortion; engine-wavetable; filter-ladder-slope-drive; filterEnv-corrected; tapesaturator-distortion; bezier-envelope; per-stage-curves; tape-delay; reverb-plate; reverb-predelay; keytrack-voice-route; lfo-symmetry; mod-matrix; tuningReferenceHz; global-width.
- **Rationale:** The only preset in the batch that pairs the PhaseDistortion engine (ResSaw + DCW) with the genuine Wavetable engine on B (type=1, not the usual PolyBLEP) - two under-used engines in one voice. The 24 dB Ladder is driven (ladderDrive=6) to add its own saturation ahead of a TapeSaturator distortion, and the filter-env bug is fixed (envAmount=20 semitones) for an audible ladder swell. Its unique envelope identity is a Bezier attack (concave ease-in) that no ADSR curve reproduces. Character wrapper is deliberately NOT a big hall: a multi-head Tape delay into a tight Plate, tuned to A=432. Mod identity is a single symmetry-skewed triangle LFO->MorphPos plus keyTrack->FltCut, distinct from every sibling.
- **Replacement code:**
```cpp
    // "Warm Tape" - Phase-distortion + wavetable pad, ladder-driven, tape everywhere
    {
        PresetDef p;
        p.name = "Warm Tape";
        p.category = "Pads";
        auto& s = p.state;
        // OSC A: Casio-CZ style Phase Distortion, resonant-saw shape for a vocal buzz
        s.oscA.type = 2; // Phase Distortion
        s.oscA.pdWaveform = 5; // ResSaw
        s.oscA.pdDistortion = 0.4f; s.oscA.level = 0.75f;
        // OSC B: genuine WAVETABLE engine (not PolyBLEP), triangle, +6c for analog beating
        s.oscB.type = 1; // Wavetable
        s.oscB.waveform = 4; // Triangle
        s.oscB.fineCents = 6.0f; s.oscB.level = 0.5f;
        s.mixer.position = 0.42f; // favour the PD core slightly
        // FILTER: 24 dB Ladder WITH drive - the tape-warmth engine's front end
        s.filter.type = 4; // Ladder
        s.filter.cutoffHz = 2600.0f; s.filter.resonance = 0.3f;
        s.filter.ladderSlope = 4; // 24 dB/oct
        s.filter.ladderDrive = 6.0f; // pushes the ladder into gentle saturation
        s.filter.envAmount = 20.0f; // corrected: +20 semitones for an audible opening swell
        s.filterEnv.attackMs = 400.0f; s.filterEnv.decayMs = 1500.0f;
        s.filterEnv.sustain = 0.35f; s.filterEnv.releaseMs = 1500.0f;
        // AMP: BEZIER attack for a soft concave (ease-in) swell no ADSR curve can match
        s.ampEnv.attackMs = 350.0f; s.ampEnv.decayMs = 700.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 1300.0f;
        s.ampEnv.attackCurve = 0.3f;
        s.ampEnv.bezierEnabled = 1.0f;
        s.ampEnv.bezierAttackCp1X = 0.2f; s.ampEnv.bezierAttackCp1Y = 0.05f; // hold low...
        s.ampEnv.bezierAttackCp2X = 0.6f; s.ampEnv.bezierAttackCp2Y = 0.4f;  // ...then rush up
        // DISTORTION: TapeSaturator - hiss, bias and gentle compression warmth
        s.distortion.type = 5; // TapeSaturator
        s.distortion.tapeModel = 0; s.distortion.drive = 0.3f;
        s.distortion.tapeSaturation = 0.45f; s.distortion.tapeBias = 0.55f;
        s.distortion.character = 0.5f; s.distortion.mix = 1.0f;
        // Mod identity: one slow, symmetry-skewed triangle LFO breathing the morph position
        s.lfo1.rateHz = 0.12f; s.lfo1.shape = 1; // Triangle
        s.lfo1.depth = 0.5f; s.lfo1.sync = 0;
        s.lfo1Ext.symmetry = 0.3f; // ramp-down bias -> lazy backwards sway
        setModSlot(s, 0, kSrcLFO1, kDstAllMorphPos, 0.3f);
        // Key-track -> filter cutoff so high notes stay present, low notes stay warm
        setVoiceRoute(s, 0, kVSrcKeyTrack, kVDstFltCut, 0.3f);
        // TAPE DELAY: multi-head wow/flutter echo folded back into the pad
        s.delayEnabled = 1;
        s.delay.type = 1; // Tape
        s.delay.sync = 1; s.delay.feedback = 0.35f; s.delay.mix = 0.28f;
        s.delay.tapeInertiaMs = 350.0f; s.delay.tapeWear = 0.25f;
        s.delay.tapeSaturation = 0.4f; s.delay.tapeAge = 0.3f;
        s.delay.tapeHead1Level = 0.0f;  s.delay.tapeHead1Pan = 0.0f;
        s.delay.tapeHead2Level = -4.0f; s.delay.tapeHead2Pan = -0.3f;
        s.delay.tapeHead3Level = -8.0f; s.delay.tapeHead3Pan = 0.3f;
        // Reverb: tight PLATE with pre-delay (contrast to the big halls of its siblings)
        s.reverbEnabled = 1; s.reverbType = 0; // Plate
        s.reverb.size = 0.55f; s.reverb.mix = 0.28f;
        s.reverb.damping = 0.5f; s.reverb.preDelayMs = 20.0f;
        // Tuned to A=432 for a mellower, slightly-flat vintage feel
        s.settings.tuningReferenceHz = 432.0f;
        s.global.width = 1.3f;
        presets.push_back(std::move(p));
    }
```

## 13. "Dreamscape" -> "Dreamscape"
- **Locate:** the block containing `p.name = "Dreamscape"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A warm, round low-tilt dream pad — the batch's SVF LO-SHELF member: only 12 additive partials with a dark tilt, its LOW band lifted (not the air), morph wandering on a heavily-smoothed Random source, through a warm wowing Tape delay and 3-voice chorus into a soft Plate.
- **Coverage:** engine: Additive (12 partials = fewest of the batch, negative tilt); engine: PolyBLEP (unison triangle body); filter: SVF LO-SHELF svfGain (type 9 - previously used by NO preset); morph motion: SMOOTHED RANDOM (Random source, smoothness 0.9 -> MorphPos); Tape delay (warm repeats); Chorus FX (3 voices); Reverb Plate; modEnv + per-stage attackCurve; Env3 -> SpecTilt voice route; global width + spread.
- **Rationale:** This is the reassigned LO-SHELF preset: filter.type=9 (SVF Lo Shelf, instantiated by no prior preset) with svfGain +6 at a 400 Hz corner LIFTS the low band, and additiveTilt=-3 + only 12 partials + a unison triangle make it genuinely warm/dark instead of another bright shimmer. Owns the SMOOTHED-RANDOM morph (random.smoothness=0.9 -> MorphPos), and its space is Plate+Tape delay - a warm combo no sibling reuses (Glass Shimmer's Plate is paired with Granular, so the reverb+delay pairings stay distinct).
- **Replacement code:**
```cpp
    // "Dreamscape" - WARM low-tilt dream pad. The batch's SVF LO-SHELF member: instead
    // of an air boost it lifts the LOW band for a soft, round body. Only 12 additive
    // partials (mellow/organ-like). Morph driven by a heavily-SMOOTHED Random source.
    // Warm Tape delay + 3-voice chorus into a soft Plate.
    {
        PresetDef p;
        p.name = "Dreamscape";
        p.category = "Pads";
        auto& s = p.state;
        // OSC A: Additive, FEW partials (12), gently dark tilt = warm and mellow
        s.oscA.type = 4; // Additive
        s.oscA.additivePartials = 12;  // fewest of the batch -> soft, hollow warmth
        s.oscA.additiveTilt = -3.0f;   // -tilt = darker, rounder body
        s.oscA.level = 0.7f;
        // OSC B: PolyBLEP triangle in UNISON for a thick low body (no octave sparkle here)
        s.oscB.type = 0; s.oscB.waveform = 4; // Triangle
        s.oscB.fineCents = 6.0f; s.oscB.level = 0.4f;
        s.mixer.position = 0.35f;
        // FILTER: SVF LO-SHELF boosting the LOW band with svfGain (warm low-tilt) -
        // the divergent filter (type 9) that breaks the 4-way hi-shelf collision.
        s.filter.type = 9; // SVF Lo Shelf
        s.filter.cutoffHz = 400.0f;  // shelf corner - everything below is lifted
        s.filter.resonance = 0.2f;
        s.filter.svfGain = 6.0f;     // +6 dB low shelf = warm, round bottom
        s.filter.svfSlope = 1;
        // AMP: warm swell, exponential slow-start
        s.ampEnv.attackMs = 500.0f; s.ampEnv.decayMs = 900.0f;
        s.ampEnv.sustain = 0.75f; s.ampEnv.releaseMs = 2200.0f;
        s.ampEnv.attackCurve = 0.5f;
        // MORPH MOTION = SMOOTHED RANDOM: a slow, heavily-interpolated Random source
        // wanders the additive<->triangle morph (the batch's smoothed-Random member).
        s.random.rateHz = 0.3f; s.random.smoothness = 0.9f; // slow, interpolated wander
        setModSlot(s, 0, kSrcRandom, kDstAllMorphPos, 0.4f, kCurveLinear, 150.0f);
        // ModEnv slowly ramps the spectrum open across the note's life (not the morph)
        s.modEnv.attackMs = 1500.0f; s.modEnv.decayMs = 2000.0f;
        s.modEnv.sustain = 0.6f; s.modEnv.releaseMs = 2500.0f;
        setVoiceRoute(s, 0, kVSrcEnv3, kVDstSpecTilt, 0.3f);
        // TAPE delay: warm, wowing repeats (Plate+Tape pairing, distinct from siblings)
        s.delayEnabled = 1;
        s.delay.type = 1; // Tape
        s.delay.sync = 0;
        s.delay.timeMs = 400.0f;
        s.delay.feedback = 0.32f;
        s.delay.mix = 0.25f;
        s.delay.tapeInertiaMs = 300.0f;
        s.delay.tapeWear = 0.15f;
        s.delay.tapeSaturation = 0.45f; // gentle tape warmth
        s.delay.tapeAge = 0.2f;
        // CHORUS: 3-voice wide ensemble for lush width
        s.modulationType = 3; // Chorus
        s.chorus.rateHz = 0.4f; s.chorus.depth = 0.5f;
        s.chorus.voices = 3; s.chorus.stereoSpread = 200.0f;
        s.chorus.mix = 0.4f; s.chorus.feedback = 0.1f;
        // Soft PLATE reverb finishes the warm wash (distinct combo vs Glass Shimmer's Plate)
        s.reverbEnabled = 1; s.reverbType = 0; // Plate
        s.reverb.size = 0.8f; s.reverb.mix = 0.35f; s.reverb.damping = 0.5f;
        s.global.width = 1.7f; s.global.spread = 0.5f;
        presets.push_back(std::move(p));
    }
```

## 14. "Formant Sea" -> "Formant Sea"
- **Locate:** the block containing `p.name = "Formant Sea"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A slowly-talking dual-vowel choir: two formant oscillators on opposite vowels crossfade through a formant filter, ring-edged and smeared across a spectral delay.
- **Coverage:** engine-formant; formant-filter; ringmodulator-distortion; spectral-delay; lfo-symmetry; mod-matrix; reverb-hall; global-width-spread.
- **Rationale:** Two Formant oscillators on opposite vowels (A and U) with mixer.position=0.5 turn an LFO->MorphPos sweep into a genuine vowel crossfade - the 'talking' motion - while a second Formant FILTER adds a modulated vocal tract (LFO2->FltCut shifts its centre). lfo1Ext.symmetry=0.35 makes the vowel sweep uneven and more speech-like. It owns the RingModulator distortion in NoteTrack mode at a low 0.18 mix so it adds a metallic edge without harshness, and the character wrapper is a Spectral delay (FFT-smeared) rather than the usual echo/hall - covering spectral delay. Two-slot mod story is unique to this preset.
- **Replacement code:**
```cpp
    // "Formant Sea" - Dual-vowel formant choir, ring-edged, through a spectral delay
    {
        PresetDef p;
        p.name = "Formant Sea";
        p.category = "Pads";
        auto& s = p.state;
        // OSC A: Formant on vowel A (open), OSC B: Formant on vowel U (dark/rounded)
        s.oscA.type = 7; // Formant
        s.oscA.formantVowel = 0; s.oscA.formantMorph = 0.3f; s.oscA.level = 0.7f;
        s.oscB.type = 7; // Formant
        s.oscB.formantVowel = 4; s.oscB.formantMorph = 3.5f; // toward U
        s.oscB.fineCents = 5.0f; s.oscB.level = 0.55f;
        // Mixer sits at 0.5 so the LFO->MorphPos sweep crossfades A<->B = vowel morph ("talking")
        s.mixer.position = 0.5f;
        // FILTER: Formant filter shapes a second vocal-tract layer, gender shifted feminine
        s.filter.type = 5; // Formant filter
        s.filter.formantMorph = 1.0f; s.filter.formantGender = -0.3f;
        s.filter.cutoffHz = 4000.0f; s.filter.resonance = 0.4f;
        // AMP: choir swell
        s.ampEnv.attackMs = 450.0f; s.ampEnv.decayMs = 700.0f;
        s.ampEnv.sustain = 0.8f; s.ampEnv.releaseMs = 1600.0f;
        // Mod identity: slow sine sweeps the vowel crossfade; a faster triangle nudges the filter formant
        s.lfo1.rateHz = 0.06f; s.lfo1.shape = 0; // Sine
        s.lfo1.depth = 1.0f; s.lfo1.sync = 0;
        s.lfo1Ext.symmetry = 0.35f; // uneven vowel sweep -> more speech-like
        setModSlot(s, 0, kSrcLFO1, kDstAllMorphPos, 0.6f, kCurveSCurve);
        s.lfo2.rateHz = 0.09f; s.lfo2.shape = 1; // Triangle
        s.lfo2.depth = 0.5f; s.lfo2.sync = 0;
        setModSlot(s, 1, kSrcLFO2, kDstAllFltCut, 0.3f); // shifts the formant-filter centre
        // DISTORTION: note-tracked Ring Modulator for a subtle metallic vocal edge
        s.distortion.type = 6; // RingModulator
        s.distortion.ringFreqMode = 1; // NoteTrack - stays musical across the keyboard
        s.distortion.ringRatio = 0.1111f; // ~2.0 ratio (an octave partial)
        s.distortion.ringWaveform = 0; // Sine carrier
        s.distortion.ringStereoSpread = 0.3f;
        s.distortion.drive = 0.25f; s.distortion.character = 0.5f; s.distortion.mix = 0.18f;
        // SPECTRAL DELAY: smears the vowels into a frequency-blurred sea
        s.delayEnabled = 1;
        s.delay.type = 4; // Spectral
        s.delay.mix = 0.3f; s.delay.feedback = 0.3f;
        s.delay.spectralFFTSize = 2; // 2048
        s.delay.spectralSpreadMs = 400.0f; s.delay.spectralTilt = 0.2f;
        s.delay.spectralDiffusion = 0.5f; s.delay.spectralWidth = 0.6f;
        // Moderate Hall
        s.reverbEnabled = 1; s.reverbType = 1; // Hall
        s.reverb.size = 0.7f; s.reverb.mix = 0.32f; s.reverb.damping = 0.45f;
        s.global.width = 1.3f; s.global.spread = 0.4f;
        presets.push_back(std::move(p));
    }
```

## 15. "Crystal Choir" -> "Crystal Choir"
- **Locate:** the block containing `p.name = "Crystal Choir"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A frozen crystalline cathedral: a formant voice pitch-sync-harmonised into a wide, formant-preserved diatonic stack, high-passed to glass and suspended in an infinite hall.
- **Coverage:** engine-formant; engine-polyblep; svf-hp-filter; harmonizer-pitchsync; harmonizer-formantpreserve; reverb-hall; reverb-freeze; reverb-predelay; modEnv; per-stage-curves; lfo-triangle; mod-matrix; global-width-spread.
- **Rationale:** Owns the PitchSync harmonizer variant (pitchShiftMode=3) as the directive demands, building a 4-voice formant-preserved C-major stack (3rd/5th/oct/10th) panned wide with small detune for chorusing. An SVF HIGH-PASS at 220 Hz thins the low end so the choir stays glassy - covering the SVF HP filter. Its unique mod identity pairs a gentle triangle LFO->FltCut shimmer with a very fast modEnv->OscAPitch pitch-scoop (5 ms attack, 250 ms decay to 0) that gives each note a vocal onset scoop. The wrapper is a FROZEN Hall (reverb.freeze=1) with pre-delay - a suspended infinite cathedral that no sibling uses - and it is very wide (1.6) with spread 0.35.
- **Replacement code:**
```cpp
    // "Crystal Choir" - Pitch-sync harmonised formant choir in a frozen cathedral
    {
        PresetDef p;
        p.name = "Crystal Choir";
        p.category = "Pads";
        auto& s = p.state;
        // OSC A: Formant on vowel E (bright, forward), OSC B: PolyBLEP triangle body
        s.oscA.type = 7; // Formant
        s.oscA.formantVowel = 1; s.oscA.formantMorph = 1.0f; s.oscA.level = 0.75f;
        s.oscB.type = 0; s.oscB.waveform = 4; // Triangle
        s.oscB.level = 0.3f;
        s.mixer.position = 0.25f; // mostly the formant voice
        // FILTER: SVF HIGH-PASS to thin the lows -> airy, glassy choir with no mud
        s.filter.type = 1; // SVF HP
        s.filter.cutoffHz = 220.0f; s.filter.resonance = 0.4f;
        s.filter.svfSlope = 1;
        // AMP: eased swell, both attack and release curved for a breathing choir
        s.ampEnv.attackMs = 400.0f; s.ampEnv.decayMs = 600.0f;
        s.ampEnv.sustain = 0.8f; s.ampEnv.releaseMs = 2000.0f;
        s.ampEnv.attackCurve = 0.5f; s.ampEnv.releaseCurve = 0.3f;
        // HARMONIZER: PITCH-SYNC mode (owns this variant), scalic C-major, formant preserved
        s.harmonizerEnabled = 1;
        s.harmonizer.harmonyMode = 1; // Scalic
        s.harmonizer.key = 0; s.harmonizer.scale = 0; // C Major
        s.harmonizer.pitchShiftMode = 3; // PitchSync
        s.harmonizer.formantPreserve = 1; // keeps the vowel intact under transposition
        s.harmonizer.numVoices = 4;
        s.harmonizer.dryLevelDb = 0.0f; s.harmonizer.wetLevelDb = -3.0f;
        s.harmonizer.voiceInterval[0] = 2; s.harmonizer.voicePan[0] = -0.6f; // 3rd
        s.harmonizer.voiceInterval[1] = 4; s.harmonizer.voicePan[1] = 0.6f;  // 5th
        s.harmonizer.voiceInterval[2] = 7; s.harmonizer.voicePan[2] = -0.3f; // octave
        s.harmonizer.voiceInterval[3] = 9; s.harmonizer.voicePan[3] = 0.3f;  // 10th (3rd+oct)
        s.harmonizer.voiceDetuneCents[2] = 4.0f; s.harmonizer.voiceDetuneCents[3] = -4.0f;
        // Mod identity: a gentle triangle shimmer on cutoff + a fast modEnv pitch-scoop into each note
        s.lfo2.rateHz = 0.07f; s.lfo2.shape = 1; // Triangle
        s.lfo2.depth = 0.4f; s.lfo2.sync = 0;
        setModSlot(s, 0, kSrcLFO2, kDstAllFltCut, 0.25f, kCurveSCurve);
        s.modEnv.attackMs = 5.0f; s.modEnv.decayMs = 250.0f;
        s.modEnv.sustain = 0.0f; s.modEnv.releaseMs = 100.0f; // quick blip
        setVoiceRoute(s, 0, kVSrcEnv3, kVDstOscAPitch, 0.1f); // vocal pitch-scoop onset
        // Reverb: FROZEN Hall - the infinite crystalline cathedral tail
        s.reverbEnabled = 1; s.reverbType = 1; // Hall
        s.reverb.size = 0.8f; s.reverb.mix = 0.38f;
        s.reverb.damping = 0.3f; s.reverb.diffusion = 0.88f;
        s.reverb.preDelayMs = 30.0f;
        s.reverb.freeze = 1; // holds the wash indefinitely for a suspended cathedral
        s.global.width = 1.6f; s.global.spread = 0.35f;
        presets.push_back(std::move(p));
    }
```

## 16. "Deep Cavern" -> "Deep Cavern"
- **Locate:** the block containing `p.name = "Deep Cavern"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A subterranean sub-saw colored by a key-tracked metallic comb resonance that slowly drifts under a smooth-random LFO, echoed by grainy cave reflections into a huge diffuse Hall.
- **Coverage:** engines: PolyBLEP, Noise; filters: Comb(damping); Granular delay; Reverb Hall + preDelay; LFO SmoothRandom shape + symmetry; keyTrack voice route; mod-matrix scale axis; per-stage release curve; global width + spread.
- **Rationale:** Comb filter at 300 Hz with keyTrack=1 makes the metallic resonance a musical, note-following element instead of a fixed drone. Since combDamping is NOT a routable mod destination, movement comes from an LFO on AllFltCut (which IS the comb frequency) using shape 5 (SmoothRandom) + symmetry 0.65 for an organic wander, with slot scale=3 (x2) to exercise the untouched depth axis and widen the sweep. releaseCurve +0.5 gives an exponential fade. Granular delay adds grainy cave reflections ahead of a big Hall (reverbType=1) with 40 ms preDelay for distance.
- **Replacement code:**
```cpp
    // "Deep Cavern" - Key-tracked comb-resonance drone drifting under a smooth-random LFO,
    //                 grainy cave echoes into a huge diffuse Hall
    {
        PresetDef p;
        p.name = "Deep Cavern";
        p.category = "Pads";
        auto& s = p.state;
        // --- Oscillators: low saw body + a whisper of brown noise for air-rumble
        s.oscA.type = 0; s.oscA.waveform = 1;      // PolyBLEP saw
        s.oscA.tuneSemitones = -12.0f;             // one octave down = cavernous sub
        s.oscA.level = 0.85f;
        s.oscB.type = 9;                           // Noise
        s.oscB.noiseColor = 2;                     // Brown = deep rumble, no hiss
        s.oscB.level = 0.14f;
        s.mixer.position = 0.12f;                  // mostly saw, noise just seasons it
        // --- Comb filter = the metallic cavern resonance; low tuning, moderate damping
        s.filter.type = 6;                         // Comb
        s.filter.cutoffHz = 300.0f;                // low comb pitch for a big hollow space
        s.filter.resonance = 0.5f;
        s.filter.combDamping = 0.55f;              // rounds off the harsh metallic top
        s.filter.keyTrack = 1.0f;                  // comb resonance follows the played note
        // --- Slow, generous amp shape with an exponential (slow) release tail
        s.ampEnv.attackMs = 400.0f; s.ampEnv.decayMs = 1200.0f;
        s.ampEnv.sustain = 0.75f; s.ampEnv.releaseMs = 2500.0f;
        s.ampEnv.releaseCurve = 0.5f;              // slow-fading tail, not a linear cut
        // --- MOD IDENTITY: a free smooth-random LFO gently sweeps the comb pitch so the
        //     metallic resonance is alive, never static. Scale x2 widens the drift.
        s.lfo1.rateHz = 0.12f;                      // glacial
        s.lfo1.shape = 5;                          // SmoothRandom (organic wander)
        s.lfo1.depth = 1.0f;
        s.lfo1.sync = 0;                           // free-running, not tempo-locked
        s.lfo1Ext.symmetry = 0.65f;                // skew the random contour
        setModSlot(s, 0, kSrcLFO1, kDstAllFltCut, 0.5f, kCurveSCurve);
        s.modMatrix.slots[0].scale = 3;            // x2 depth = wide comb-pitch drift (scale axis)
        // key-track voice route reinforces the comb tracking the keyboard
        setVoiceRoute(s, 0, kVSrcKeyTrack, kVDstFltCut, 0.6f);
        // --- Granular delay = grainy cave reflections feeding the reverb
        s.delayEnabled = 1;
        s.delay.type = 3;                          // Granular
        s.delay.timeMs = 450.0f; s.delay.feedback = 0.3f; s.delay.mix = 0.22f;
        s.delay.sync = 0;
        s.delay.granularSizeMs = 140.0f; s.delay.granularDensity = 14.0f;
        s.delay.granularTexture = 0.3f; s.delay.granularWidth = 1.2f;
        // --- Huge diffuse HALL with pre-delay for the sense of distance
        s.reverbEnabled = 1; s.reverbType = 1;     // Hall
        s.reverb.size = 0.95f; s.reverb.mix = 0.45f;
        s.reverb.damping = 0.7f; s.reverb.diffusion = 0.85f;
        s.reverb.preDelayMs = 40.0f;               // reflections arrive before the tail
        s.global.width = 1.4f; s.global.spread = 0.2f;
        presets.push_back(std::move(p));
    }
```

## 17. "Solar Flare" -> "Solar Flare"
- **Locate:** the block containing `p.name = "Solar Flare"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A brilliant 80-partial additive shimmer spectrally morphed against a saw and boosted by a hi-shelf, whose A/B morph is KEY-TRACKED so high notes lean into the saw; brightness breathes under a slow free LFO, widened by a jet-sweep Flanger into a synced PingPong delay and a modulated Hall.
- **Coverage:** engines: Additive (80 partials), PolyBLEP saw; SpectralMorph mixer (tilt/shift); filter: SVF Hi-Shelf (svfGain); Spectral distortion; morph motion: KEYTRACK (keyTrack voice route -> MorphPos); LFO -> SpecTilt (brightness breathing, not morph); Flanger FX (distinct from sibling chorus); PingPong delay (synced); Reverb Hall + preDelay + modRate/modDepth; per-stage attackCurve, global width + spread.
- **Rationale:** Assigned the batch's KEYTRACK morph: setVoiceRoute(kVSrcKeyTrack -> kVDstMorphPos, 0.5) makes the SpectralMorph A/B blend track the keyboard, so the LFO->MorphPos it previously shared with Dreamscape is gone (LFO1 now only breathes SpecTilt). additivePartials=80 sits between the 12/40/128 of the siblings. Swaps chorus for a Flanger and moves its space to Hall+PingPong so its reverb+delay combo is unique in the batch, while keeping the SpectralMorph mixer + Hi-Shelf + spectral-distortion character.
- **Replacement code:**
```cpp
    // "Solar Flare" - BRILLIANT 80-partial additive shimmer, spectral-morphed against
    // a saw, SVF Hi-Shelf lift. Its morph is KEY-TRACKED (the batch's keyTrack member):
    // higher notes lean further into the saw. Modulated Hall + a synced PingPong delay,
    // widened by a Flanger (not the sibling chorus).
    {
        PresetDef p;
        p.name = "Solar Flare";
        p.category = "Pads";
        auto& s = p.state;
        // --- Additive A vs saw B, blended in the SPECTRAL-MORPH mixer (FFT interpolation)
        s.oscA.type = 4;                           // Additive
        s.oscA.additivePartials = 80;              // dense, between the batch extremes (12/40/128)
        s.oscA.additiveTilt = 5.0f;                // tilt up = bright, airy top
        s.oscA.level = 0.6f;
        s.oscB.type = 0; s.oscB.waveform = 1;      // PolyBLEP saw for body under the partials
        s.oscB.fineCents = 6.0f;                   // faint beating = analog thickness
        s.oscB.level = 0.45f;
        s.mixer.mode = 1;                          // SpectralMorph (not plain crossfade)
        s.mixer.position = 0.4f;                   // base blend (keyTrack sweeps it)
        s.mixer.tilt = 3.0f;                       // extra spectral brightness in the morph
        s.mixer.shift = 0.15f;                     // subtle inharmonic freq shift
        // --- SVF Hi-Shelf filter lifts the very top for that solar sparkle
        s.filter.type = 10;                        // SVF Hi Shelf
        s.filter.cutoffHz = 3500.0f; s.filter.resonance = 0.2f;
        s.filter.svfGain = 6.0f;                   // +6 dB shelf boost above 3.5 kHz
        s.filter.svfSlope = 1;
        // --- Per-bin SPECTRAL distortion adds gentle harmonic glint
        s.distortion.type = 2;                     // Spectral
        s.distortion.drive = 0.3f; s.distortion.spectralMode = 0; // PerBinSaturate
        s.distortion.spectralCurve = 0;            // Tanh
        s.distortion.mix = 0.6f;
        // --- Amp: slow bloom with a slow-start attack curve
        s.ampEnv.attackMs = 350.0f; s.ampEnv.decayMs = 700.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 1600.0f;
        s.ampEnv.attackCurve = 0.3f;               // eases into the swell
        // --- MORPH MOTION = KEYTRACK: the mixer morph follows the keyboard - high notes
        //     lean into the saw, low notes stay additive. No LFO on the morph axis.
        setVoiceRoute(s, 0, kVSrcKeyTrack, kVDstMorphPos, 0.5f);
        // A slow free LFO breathes only the brightness (SpecTilt), leaving morph to keyTrack.
        s.lfo1.rateHz = 0.2f; s.lfo1.shape = 0; s.lfo1.sync = 0;   // slow sine
        setModSlot(s, 0, kSrcLFO1, kDstAllSpecTilt, 0.4f, kCurveSCurve);
        // --- FLANGER widens it with a jet-sweep sheen (distinct from the chorus siblings)
        s.modulationType = 2;                      // Flanger
        s.flanger.rateHz = 0.25f; s.flanger.depth = 0.5f;
        s.flanger.feedback = 0.3f; s.flanger.mix = 0.35f;
        s.flanger.stereoSpread = 120.0f;
        // --- HALL reverb + synced PINGPONG delay (Hall+PingPong pairing, unique to this preset)
        s.delayEnabled = 1;
        s.delay.type = 2; // PingPong
        s.delay.sync = 1; s.delay.noteValue = kNote1_8;
        s.delay.feedback = 0.35f; s.delay.mix = 0.25f;
        s.delay.pingPongRatio = 0;
        s.delay.pingPongCrossFeed = 0.8f;
        s.delay.pingPongWidth = 140.0f;
        s.delay.pingPongModDepth = 0.15f;
        s.delay.pingPongModRateHz = 0.3f;
        s.reverbEnabled = 1; s.reverbType = 1;     // Hall
        s.reverb.size = 0.85f; s.reverb.mix = 0.3f; s.reverb.damping = 0.35f;
        s.reverb.preDelayMs = 20.0f;
        s.reverb.modRateHz = 0.3f; s.reverb.modDepth = 0.15f; // chorused tail
        s.global.width = 1.5f; s.global.spread = 0.3f;
        presets.push_back(std::move(p));
    }
```

## 18. "Analog Brass" -> "Brass Regiment"
- **Locate:** the block containing `p.name = "Analog Brass"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A punchy detuned saw+square brass through a driven 18 dB ladder with a real +30-semitone filter-envelope swell, tape-saturated, expressive under velocity/key/aftertouch, with a tape-delay slap into a tight plate.
- **Coverage:** engines: PolyBLEP; filters: Ladder(slope/drive); filterEnv corrected (+30 st); modEnv + per-stage decay curves; TapeSaturator distortion; Tape delay; aftertouch->pitch voice route; keyTrack voice route; velocity voice route; Reverb Plate.
- **Rationale:** Headline fix: envAmount is written as +30 PLAIN SEMITONES (valid -48..+48) over a low 900 Hz base cutoff so the filter-env swell is genuinely audible. Ladder type 4 with ladderSlope 3 (18 dB) + ladderDrive 4.0 give the brass grind; filterEnv.decayCurve +0.4 shapes the 'blat' transient. This preset owns the classic dual-VA performance story - the full velocity/keyTrack/aftertouch->pitch/modEnv->morph routing set - so it reads as an expressive played brass section rather than a static pad. As the Pads half of the flagged pair it stays PolyBLEP + Ladder + Tape delay + Plate; the additive/high-partial/steep-tilt push and a different delay+reverb belong to Regal Fanfare (Leads), so once that moves the two no longer share a signal chain.
- **Replacement code:**
```cpp
    // "Brass Regiment" - Punchy analog brass: driven ladder with the CORRECT +30-semitone
    //                    filter-env swell, tape saturation, and full performance routing
    {
        PresetDef p;
        p.name = "Brass Regiment";
        p.category = "Pads";
        auto& s = p.state;
        // --- Classic detuned saw + square brass stack
        s.oscA.type = 0; s.oscA.waveform = 1; s.oscA.level = 0.8f;  // saw
        s.oscB.type = 0; s.oscB.waveform = 2;                       // square
        s.oscB.level = 0.55f; s.oscB.fineCents = 5.0f;              // slight detune = ensemble
        s.mixer.position = 0.42f;
        // --- Driven LADDER filter, 18 dB/oct. Starts nearly closed so the env does the work.
        s.filter.type = 4;                         // Ladder LP
        s.filter.cutoffHz = 900.0f;                // low base -> big audible sweep on top
        s.filter.resonance = 0.35f;
        s.filter.envAmount = 30.0f;                // CORRECTED: +30 SEMITONES (not the bugged Hz value)
        s.filter.ladderSlope = 3;                  // 18 dB/oct
        s.filter.ladderDrive = 4.0f;               // input drive = brassy grind
        // --- Filter env = the brass 'blat'. Fast attack, shaped punchy decay.
        s.filterEnv.attackMs = 8.0f; s.filterEnv.decayMs = 300.0f;
        s.filterEnv.sustain = 0.35f; s.filterEnv.releaseMs = 250.0f;
        s.filterEnv.decayCurve = 0.4f;             // exp decay = snappy attack transient
        // --- Amp env: quick attack, moderate release for a section pad
        s.ampEnv.attackMs = 25.0f; s.ampEnv.decayMs = 350.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 350.0f;
        s.ampEnv.decayCurve = 0.2f;
        // --- Mod env drives a slow timbral shift via the osc morph position
        s.modEnv.attackMs = 5.0f; s.modEnv.decayMs = 500.0f;
        s.modEnv.sustain = 0.2f; s.modEnv.releaseMs = 400.0f;
        // --- MOD IDENTITY: a fully-played performance patch - velocity opens the filter,
        //     key-track keeps highs bright, aftertouch adds pressure vibrato, modEnv morphs osc.
        setVoiceRoute(s, 0, kVSrcVelocity,   kVDstFltCut,    0.5f);  // dynamics -> brightness
        setVoiceRoute(s, 1, kVSrcKeyTrack,   kVDstFltCut,    0.4f);  // pitch tracking
        setVoiceRoute(s, 2, kVSrcAftertouch, kVDstOscAPitch, 0.15f); // press = subtle bend/vibrato
        setVoiceRoute(s, 3, kVSrcEnv3,       kVDstMorphPos,  0.4f);  // modEnv sweeps saw<->square
        // --- Tape-saturator distortion warms the brass edge
        s.distortion.type = 5;                     // TapeSaturator
        s.distortion.drive = 0.35f; s.distortion.character = 0.5f; s.distortion.mix = 0.7f;
        s.distortion.tapeModel = 0; s.distortion.tapeSaturation = 0.5f; s.distortion.tapeBias = 0.5f;
        // --- Short tape-delay slap for section depth (not a wash)
        s.delayEnabled = 1; s.delay.type = 1;      // Tape
        s.delay.timeMs = 260.0f; s.delay.feedback = 0.22f; s.delay.mix = 0.15f;
        s.delay.sync = 0; s.delay.tapeSaturation = 0.4f;
        // --- Tight plate for glue, deliberately small (brass stays upfront)
        s.reverbEnabled = 1; s.reverbType = 0;     // Plate
        s.reverb.size = 0.35f; s.reverb.mix = 0.15f; s.reverb.damping = 0.5f;
        s.global.polyphony = 6; s.global.width = 1.25f;
        presets.push_back(std::move(p));
    }
```

## 19. "Alien World" -> "Xenoform"
- **Locate:** the block containing `p.name = "Alien World"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** An unstable Duffing-chaos drone ring-modulated at a note-tracked ratio through a bandpass that the chaos LFO wanders, smeared by spectral delay into a big diffuse hall.
- **Coverage:** engines: Chaos, Noise; filters: SVF BandPass; RingModulator distortion; chaosMod exotic source; Random source + mod-matrix stepped curve; Spectral delay; Reverb Hall; mod -> FltCut/Resonance identity; global width + spread.
- **Rationale:** Keeps the strong chaos+ring-mod+bandpass identity but fixes the original's total lack of movement. Raises chaosMod.depth to 0.6 (default 0 = silent source) and routes kSrcChaos->AllFltCut with the STEPPED curve for an eerie quantized bandpass lurch, plus kSrcRandom->AllResonance for Q jitter - a mod story no sibling repeats. Ring mod stays NoteTrack so it stays musical across the keyboard. Swaps the plain delay for a Spectral delay (FFT smear) to cover that must-cover area and match the inharmonic sci-fi character, feeding a wide diffuse Hall.
- **Replacement code:**
```cpp
    // "Xenoform" - Unstable Duffing-chaos drone, note-tracked ring mod, chaos-swept bandpass,
    //              spectral-delay smear into a big hall
    {
        PresetDef p;
        p.name = "Xenoform";
        p.category = "Pads";
        auto& s = p.state;
        // --- Chaos oscillator (Duffing) + grey noise grit
        s.oscA.type = 5;                           // Chaos
        s.oscA.chaosAttractor = 3;                 // Duffing
        s.oscA.chaosAmount = 0.5f; s.oscA.chaosCoupling = 0.25f;
        s.oscA.chaosOutput = 0;                    // X axis
        s.oscA.level = 0.65f;
        s.oscB.type = 9; s.oscB.noiseColor = 5;    // Grey noise
        s.oscB.level = 0.1f;
        s.mixer.position = 0.15f;
        // --- SVF bandpass frames the chaos into a formant-like window
        s.filter.type = 2;                         // SVF BP
        s.filter.cutoffHz = 1800.0f; s.filter.resonance = 0.4f;
        s.filter.svfSlope = 1;
        // --- Ring modulator at a note-tracked ratio = the metallic alien voice
        s.distortion.type = 6;                     // RingModulator
        s.distortion.drive = 0.4f;
        s.distortion.ringFreqMode = 1;             // NoteTrack (follows the key)
        s.distortion.ringRatio = 0.22f;            // normalized -> ~3x ratio
        s.distortion.ringWaveform = 0;             // Sine
        s.distortion.character = 0.5f; s.distortion.mix = 0.7f;
        s.distortion.ringStereoSpread = 0.3f;      // stereo shimmer on the sidebands
        // --- Slow amp swell/long tail
        s.ampEnv.attackMs = 500.0f; s.ampEnv.decayMs = 900.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 2200.0f;
        // --- MOD IDENTITY: the dedicated chaos LFO wanders the bandpass, a smoothed random
        //     source jitters resonance. Stepped curve gives an eerie quantized lurch.
        s.chaosMod.rateHz = 0.3f; s.chaosMod.type = 1; // Rossler attractor
        s.chaosMod.depth = 0.6f; s.chaosMod.sync = 0;  // raised from default 0 so it's audible
        s.random.rateHz = 0.5f; s.random.smoothness = 0.6f;
        setModSlot(s, 0, kSrcChaos,  kDstAllFltCut,    0.5f, kCurveStepped); // lurching sweep
        setModSlot(s, 1, kSrcRandom, kDstAllResonance, 0.3f, kCurveLinear);  // Q jitter
        // --- Spectral delay smears the drone into an inharmonic cloud
        s.delayEnabled = 1; s.delay.type = 4;      // Spectral
        s.delay.timeMs = 500.0f; s.delay.feedback = 0.4f; s.delay.mix = 0.3f;
        s.delay.sync = 0;
        s.delay.spectralFFTSize = 2; s.delay.spectralSpreadMs = 400.0f;
        s.delay.spectralDiffusion = 0.5f; s.delay.spectralWidth = 0.6f;
        // --- Big diffuse HALL
        s.reverbEnabled = 1; s.reverbType = 1;     // Hall
        s.reverb.size = 0.85f; s.reverb.mix = 0.4f;
        s.reverb.damping = 0.5f; s.reverb.diffusion = 0.8f;
        s.global.width = 1.3f; s.global.spread = 0.4f;
        presets.push_back(std::move(p));
    }
```

## 20. "Frozen Time" -> "Frozen Time"
- **Locate:** the block containing `p.name = "Frozen Time"` in `F:\projects\iterum\tools\ruinae_preset_generator.cpp`
- **Character:** A frozen spectral pad plus an octave sine, harmonized into a phase-vocoded fifth-up/fourth-down chord, tuned to 432 Hz, swelling on a Bezier attack, suspended forever inside a frozen plate.
- **Coverage:** engines: SpectralFreeze, PolyBLEP; harmonizer PhaseVocoder; Bezier envelope mode; Reverb Plate + freeze + preDelay + modRate/modDepth; LFO SmoothRandom -> SpecTilt mod identity; tuningReferenceHz (432); per-stage release curve; global width + spread.
- **Rationale:** Adds the movement the original lacked without losing the glassy identity: a Bezier attack (bezierEnabled + late-blooming handles) shapes an ease-in swell no ADSR can, and a glacial SmoothRandom LFO->AllSpecTilt drifts the otherwise-static freeze. reverb.freeze=1 delivers the directive's frozen plate (reverbType 0) with modRate/modDepth for a shimmering suspended tail. tuningReferenceHz=432 gives an otherworldly detune and covers that must-cover item; harmonizer PhaseVocoder (mode 2) supplies the panned suspended 5th/-4th chord.
- **Replacement code:**
```cpp
    // "Frozen Time" - Frozen spectral pad + octave sine, phase-vocoded harmony, Bezier swell,
    //                 tuned to 432 Hz, suspended in a FROZEN plate
    {
        PresetDef p;
        p.name = "Frozen Time";
        p.category = "Pads";
        auto& s = p.state;
        // --- Spectral-freeze pad + a soft octave sine to anchor the pitch
        s.oscA.type = 8;                           // Spectral Freeze
        s.oscA.spectralPitch = 0.0f; s.oscA.spectralTilt = -2.0f;  // slightly dark freeze
        s.oscA.level = 0.8f;
        s.oscB.type = 0; s.oscB.waveform = 0;      // pure sine
        s.oscB.tuneSemitones = 12.0f; s.oscB.level = 0.2f;         // octave up glassy anchor
        s.mixer.position = 0.18f;
        // --- Gentle LP keeps it glassy, not harsh
        s.filter.type = 0; s.filter.cutoffHz = 5500.0f; s.filter.resonance = 0.15f;
        // --- BEZIER amp envelope: a hand-shaped slow ease-in swell + long tail
        s.ampEnv.attackMs = 900.0f; s.ampEnv.decayMs = 700.0f;
        s.ampEnv.sustain = 0.85f; s.ampEnv.releaseMs = 3200.0f;
        s.ampEnv.releaseCurve = 0.4f;
        s.ampEnv.bezierEnabled = 1.0f;             // enable Bezier attack shaping
        s.ampEnv.bezierAttackCp1X = 0.2f; s.ampEnv.bezierAttackCp1Y = 0.0f;   // flat start
        s.ampEnv.bezierAttackCp2X = 0.8f; s.ampEnv.bezierAttackCp2Y = 0.35f;  // late bloom
        // --- Harmonizer (PhaseVocoder): fifth up + fourth down = suspended chord
        s.harmonizerEnabled = 1;
        s.harmonizer.harmonyMode = 0;              // Chromatic
        s.harmonizer.pitchShiftMode = 2;           // PhaseVocoder (smooth, formant-neutral)
        s.harmonizer.numVoices = 2;
        s.harmonizer.dryLevelDb = 0.0f; s.harmonizer.wetLevelDb = -4.0f;
        s.harmonizer.voiceInterval[0] = 7;  s.harmonizer.voicePan[0] = -0.5f;  // +5th, left
        s.harmonizer.voiceInterval[1] = -5; s.harmonizer.voicePan[1] = 0.5f;   // -4th, right
        // --- 432 Hz tuning for an otherworldly, suspended feel
        s.settings.tuningReferenceHz = 432.0f;
        // --- MOD IDENTITY: a glacial smooth-random LFO drifts the spectral tilt so the
        //     'frozen' timbre subtly evolves rather than sitting perfectly still.
        s.lfo2.rateHz = 0.1f; s.lfo2.shape = 5;    // SmoothRandom
        s.lfo2.sync = 0; s.lfo2Ext.symmetry = 0.5f;
        setModSlot(s, 0, kSrcLFO2, kDstAllSpecTilt, 0.35f, kCurveSCurve);
        // --- FROZEN plate: infinite glassy tail, modulated and pre-delayed
        s.reverbEnabled = 1; s.reverbType = 0;     // Plate
        s.reverb.size = 0.9f; s.reverb.mix = 0.5f;
        s.reverb.damping = 0.2f; s.reverb.diffusion = 0.9f;
        s.reverb.freeze = 1;                       // suspended, self-sustaining wash
        s.reverb.preDelayMs = 30.0f;
        s.reverb.modRateHz = 0.3f; s.reverb.modDepth = 0.12f; // shimmering frozen tail
        s.global.width = 1.6f; s.global.spread = 0.4f;
        presets.push_back(std::move(p));
    }
```
