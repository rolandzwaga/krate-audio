#pragma once

// ==============================================================================
// DefaultKit -- GM-inspired default kit templates for Membrum Phase 4
// ==============================================================================
// FR-030: All 32 pads initialized with GM-inspired defaults on first load
// FR-031: 6 template archetypes (Kick, Snare, Tom, Hat, Cymbal, Perc)
// FR-032: Choke group 1 for hats (MIDI 42, 44, 46)
// FR-033: Tom pads have progressively increasing Size values
// ==============================================================================

#include "pad_config.h"
#include "exciter_type.h"
#include "body_model_type.h"

#include <array>
#include <cmath>

namespace Membrum {

/// GM drum map template archetypes.
enum class DrumTemplate
{
    Kick,
    Snare,
    Tom,
    Hat,
    Cymbal,
    Perc,
};

namespace DefaultKit {

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Internal: Apply a template archetype to a PadConfig
// ---------------------------------------------------------------------------
inline void applyTemplate(PadConfig& cfg, DrumTemplate tmpl, float sizeOverride = -1.0f)
{
    // Start from PadConfig defaults (already initialized by aggregate init)
    cfg = PadConfig{};

    switch (tmpl) {
        case DrumTemplate::Kick:
            cfg.exciterType = ExciterType::Impulse;
            cfg.bodyModel   = BodyModelType::Membrane;
            cfg.material       = 0.3f;
            cfg.size           = 0.8f;
            cfg.decay          = 0.3f;
            cfg.strikePosition = 0.3f;
            cfg.level          = 0.8f;
            // Pitch envelope: 160->50Hz, 20ms
            // norm = log(Hz/20)/log(100)
            // 160 Hz: log(8)/log(100) = 0.45154...
            // 50 Hz:  log(2.5)/log(100) = 0.19897...
            // 20 ms / 500 ms = 0.04
            cfg.tsPitchEnvStart = static_cast<float>(std::log(160.0 / 20.0) / std::log(100.0));
            cfg.tsPitchEnvEnd   = static_cast<float>(std::log(50.0 / 20.0) / std::log(100.0));
            cfg.tsPitchEnvTime  = 0.04f;  // 20ms / 500ms
            // Phase 7.1: air-thump + beater thwack. Phase 8C rebalance:
            // the noise + click layers were drowning the modal body's
            // air-loading / damping character. Scaled 85% -> 50% and
            // 75% -> 40% so the Phase 8 body knobs stay audible.
            cfg.noiseLayerMix        = 0.50f;
            cfg.noiseLayerCutoff     = 0.08f;  // ~55 Hz low rumble
            cfg.noiseLayerResonance  = 0.15f;
            cfg.noiseLayerDecay      = 0.55f;  // ~280 ms tail
            cfg.noiseLayerColor      = 0.0f;   // brown (darkest, -6 dB/oct)
            cfg.clickLayerMix        = 0.40f;
            cfg.clickLayerContactMs  = 0.15f;  // ~2.4 ms short beater strike
            cfg.clickLayerBrightness = 0.4f;   // darker thwack (mallet felt)
            // Phase 8C: strong air-loading for a deep kick. Small scatter
            // breaks the tuned-bar symmetry of the pure Bessel lattice.
            cfg.airLoading  = 0.7f;
            cfg.modeScatter = 0.15f;
            // Phase 8D: coupling is opt-in (secondaryEnabled = 0 by
            // default). Sensible per-template seed values populated for
            // when the user flips the toggle on.
            cfg.secondarySize     = 0.55f;   // ~0.6 x head f0
            cfg.secondaryMaterial = 0.3f;    // wood-ish shell
            // Phase 8E: kicks respond strongly to velocity-driven tension.
            cfg.tensionModAmt     = 0.8f;
            break;

        case DrumTemplate::Snare:
            // Snare-body investigation (INVESTIGATION-snare-body-2026-07-01):
            // a snare = a SHORT body "tat" + a bright broadband WIRE buzz on top.
            // The old NoiseBurst recipe was all wires / no body (hi-hat); the
            // first fix pass over-corrected to a long tonal body with no wires
            // (hollow woodblock). This is the tuned balance (audition wire_high):
            //   * Impulse strike gives a real ~200 Hz body.
            //   * Short body (low decay + high b1) so the head is a quick tat
            //     that gets out of the way -- the wire buzz carries the tail.
            //   * Bright, loud wires via noiseLayerGain (the mix knob + the
            //     -18 dBFS accent ceiling alone can't reach snare-wire level).
            cfg.exciterType = ExciterType::Impulse;
            cfg.bodyModel   = BodyModelType::Membrane;
            cfg.material       = 0.5f;
            // Size 0.4 -> f0 ~199 Hz, on the measured 14" (0,1) mode (Rossing &
            // Bork 1992). Was 0.5 -> ~158 Hz, a whole tone flat.
            cfg.size           = 0.4f;
            // Short "tat": fast decay + b1 override (~30 s^-1) so the body is a
            // snares-on thwack, not a sustained tom-like tone (the woodblock).
            cfg.decay          = 0.13f;
            cfg.strikePosition = 0.3f;
            cfg.level          = 0.8f;
            // Impulse exciter ignores noiseBurstDuration; kept as a seed value
            // for users who switch the exciter back to NoiseBurst.
            cfg.noiseBurstDuration = static_cast<float>((8.0 - 2.0) / 13.0);
            // WIRE buzz -- the snare's identity. Bright band (~5 kHz), white/
            // violet, decays a touch past the body. noiseLayerGain lifts it to
            // near-body level (mix + global ceiling cap it as a quiet accent).
            cfg.noiseLayerMix        = 0.65f;
            cfg.noiseLayerCutoff     = 0.80f;  // ~5 kHz wire sizzle band
            cfg.noiseLayerResonance  = 0.25f;
            cfg.noiseLayerDecay      = 0.55f;  // wire tail outlasts the body tat
            cfg.noiseLayerColor      = 0.75f;  // white->violet (bright rattle)
            cfg.noiseLayerGain       = 6.2f;   // wire reaches snare level (calibrated)
            // Wire coupling: buzz partially tracks the head so it dies with the
            // body and chokes on note-off, instead of running its full fixed
            // ~600 ms ADSR tail (Bilbao 2012: wires are driven by head motion).
            cfg.wireCoupling         = 0.45f;
            cfg.clickLayerMix        = 0.55f;  // stick crack
            cfg.clickLayerContactMs  = 0.2f;
            cfg.clickLayerBrightness = 0.7f;
            // Short-tat damping: b1 ~30 s^-1 (fast head decay), moderate b3 so
            // the body still has some crack (not over-damped into a dull thud).
            cfg.bodyDampingB1 = 0.60f;
            cfg.bodyDampingB3 = 0.40f;
            // Phase 8C: moderate air-loading; scatter 0.28 reproduces the
            // (0,1)/(1,1) mode splitting of a real snare head.
            cfg.airLoading  = 0.5f;
            cfg.modeScatter = 0.28f;
            // Phase 8D coupling: off by default, seed values for snare shell.
            cfg.secondarySize     = 0.6f;
            cfg.secondaryMaterial = 0.5f;
            // Phase 8E: modest tension mod on snare (wires limit pitch glide).
            cfg.tensionModAmt     = 0.3f;
            break;

        case DrumTemplate::Tom:
            cfg.exciterType = ExciterType::Mallet;
            cfg.bodyModel   = BodyModelType::Membrane;
            cfg.material       = 0.4f;
            cfg.size           = 0.5f;  // overridden by sizeOverride
            cfg.decay          = 0.5f;
            cfg.strikePosition = 0.3f;
            cfg.level          = 0.8f;
            // Phase 7.1: head resonance + felt-mallet click.
            cfg.noiseLayerMix        = 0.55f;
            cfg.noiseLayerCutoff     = 0.3f;
            cfg.noiseLayerResonance  = 0.25f;
            cfg.noiseLayerDecay      = 0.4f;
            cfg.noiseLayerColor      = 0.3f;  // pink-leaning
            cfg.clickLayerMix        = 0.7f;
            cfg.clickLayerContactMs  = 0.3f;
            cfg.clickLayerBrightness = 0.45f;
            // Phase 8C: tom-leaning air-loading + light scatter.
            cfg.airLoading  = 0.6f;
            cfg.modeScatter = 0.15f;
            // Phase 8D coupling: off by default, seed values for tom shell.
            cfg.secondarySize     = 0.5f;
            cfg.secondaryMaterial = 0.4f;
            // Phase 8E: toms are the canonical "kerthump" case -- strong
            // velocity-dependent pitch glide (JASA 2021 Kirby & Sandler).
            cfg.tensionModAmt     = 1.0f;
            break;

        case DrumTemplate::Hat:
            cfg.exciterType = ExciterType::NoiseBurst;
            cfg.bodyModel   = BodyModelType::NoiseBody;
            cfg.material       = 0.9f;
            cfg.size           = 0.15f;
            cfg.decay          = 0.1f;
            cfg.strikePosition = 0.3f;
            cfg.level          = 0.8f;
            // NoiseBurstDuration = 3ms -> (3-2)/13 = 0.076923...
            cfg.noiseBurstDuration = static_cast<float>((3.0 - 2.0) / 13.0);
            // Phase 7.1: noise-dominated; click provides stick attack.
            cfg.noiseLayerMix        = 0.95f;
            cfg.noiseLayerCutoff     = 0.88f;
            cfg.noiseLayerResonance  = 0.2f;
            cfg.noiseLayerDecay      = 0.15f;
            cfg.noiseLayerColor      = 0.95f;  // very bright violet
            cfg.clickLayerMix        = 0.55f;
            cfg.clickLayerContactMs  = 0.05f;  // very short tick
            cfg.clickLayerBrightness = 0.9f;
            // Phase 8C: hi-hat is open-air -- no air-loading, modest scatter.
            cfg.airLoading  = 0.0f;
            cfg.modeScatter = 0.10f;
            break;

        case DrumTemplate::Cymbal:
            // Crash-cymbal redesign (CRASH-REDESIGN-PLAN.md). A crash is a thin
            // free-edge plate driven into a nonlinear (wave-turbulence) regime:
            //   * a dense inharmonic mode cloud (stretch + heavy scatter break
            //     the tuned-plate symmetry so the tail reads as wash, not pitch),
            //   * frequency-dependent decay -- the lowest modes ring for seconds
            //     while the top octave dies in ~0.5 s (explicit b1/b3 law), and
            //   * a delayed-HF "bloom" (energy cascades up after the strike),
            //     driven downstream by NonlinearCoupling (the bloom is gated on
            //     coupling > 0, so it activates here and stays off for hats/toms).
            cfg.exciterType = ExciterType::NoiseBurst;
            cfg.bodyModel   = BodyModelType::NoiseBody;
            cfg.material       = 0.95f;
            cfg.size           = 0.3f;
            cfg.decay          = 0.8f;
            cfg.strikePosition = 0.32f;  // near-edge but keeps a strong fundamental
            cfg.level          = 0.8f;
            // NoiseBurstDuration = 10ms -> (10-2)/13 = 0.615385...
            cfg.noiseBurstDuration = static_cast<float>((10.0 - 2.0) / 13.0);
            // Metallic inharmonic cloud: 1.4x stretch + heavy scatter so the 32+
            // plate modes fuse into a dense wash instead of a resolvable chime.
            cfg.modeStretch = 0.6f;   // norm of [0.5,2.0] -> ~1.4x
            cfg.decaySkew   = 0.55f;  // whisper of low-mode emphasis (~+0.1)
            cfg.modeInjectAmount  = 0.0f;   // NO harmonic drone (a crash has no pitch)
            cfg.nonlinearCoupling = 0.35f;  // crash bloom (velocity-sensitive)
            // Frequency-dependent damping (the crash's dark-through-tail decay).
            // The lowest modes ring for SECONDS -- that long low ring IS the
            // wash (Rossing; FEM crash studies measure LF T60 up to ~8 s). The
            // top octave dies in ~0.5 s via the f^2 term. This long low ring is
            // also what keeps the voice above the pool's -60 dBFS auto-release
            // floor for the full tail instead of being cut at ~1.5 s.
            //   b1 = 0.2 + 0.020*49.8 ~ 1.2 s^-1  -> lowest-mode T60 ~ 4.6 s
            //   b3 = 8.0e-5 * 1e-3 s              -> 15 kHz decayRate ~ 19 s^-1
            //                                        -> top-octave T60 ~ 0.36 s
            // b3 is tuned up (vs the naive 6e-5) because a slightly faster HF
            // roll-off makes the tail DENSER-sounding / less tonal: a small set
            // of surviving mid modes reinforces a pitch, whereas rolling them
            // off sooner leaves the broadband wash in charge (lower tail pitch
            // salience -- AC-4).
            cfg.bodyDampingB1 = 0.020f;
            cfg.bodyDampingB3 = 0.00008f;
            // Phase 7.1: sustained shimmer noise, short stick click.
            cfg.noiseLayerMix        = 0.95f;
            cfg.noiseLayerCutoff     = 0.82f;
            cfg.noiseLayerResonance  = 0.3f;
            cfg.noiseLayerDecay      = 0.95f;  // long sizzle wash (~1.6 s)
            cfg.noiseLayerColor      = 0.9f;
            // Make the bloomed wash the dominant tail so its dark->bright cutoff
            // sweep shapes the COMPOSITE brightness (a crash IS mostly wash --
            // SOS "HP-noise tail"): darkens the onset, brings HF up over the
            // first ~50 ms, then darkens through the tail. Like the snare wire
            // gain, mix + the -18 dBFS ceiling alone can't reach wash level.
            cfg.noiseLayerGain       = 3.5f;
            // Darker, quieter stick click so the onset is not a bright tick that
            // pins the HF maximum at t=0 (defeats the bloom).
            cfg.clickLayerMix        = 0.30f;
            cfg.clickLayerContactMs  = 0.15f;
            cfg.clickLayerBrightness = 0.45f;
            // Phase 8C: open-air cymbal; heavy scatter for a dense inharmonic
            // cloud (breaks the tuned-plate pitch salience -> wash, not tone).
            cfg.airLoading  = 0.0f;
            cfg.modeScatter = 0.35f;
            break;

        case DrumTemplate::Perc:
            cfg.exciterType = ExciterType::Mallet;
            cfg.bodyModel   = BodyModelType::Plate;
            cfg.material       = 0.7f;
            cfg.size           = 0.3f;
            cfg.decay          = 0.3f;
            cfg.strikePosition = 0.3f;
            cfg.level          = 0.8f;
            // Phase 7.1: prominent click + moderate noise body.
            cfg.noiseLayerMix        = 0.55f;
            cfg.noiseLayerCutoff     = 0.55f;
            cfg.noiseLayerResonance  = 0.25f;
            cfg.noiseLayerDecay      = 0.2f;
            cfg.noiseLayerColor      = 0.6f;
            cfg.clickLayerMix        = 0.75f;
            cfg.clickLayerContactMs  = 0.2f;
            cfg.clickLayerBrightness = 0.65f;
            // Phase 8C: wood-block style perc, light loading.
            cfg.airLoading  = 0.4f;
            cfg.modeScatter = 0.15f;
            break;
    }

    // Apply size override for toms (FR-033)
    if (sizeOverride >= 0.0f) {
        cfg.size = sizeOverride;
    }
}

// ---------------------------------------------------------------------------
// DefaultKit::apply -- Initialize all 32 pads with GM-inspired templates
// ---------------------------------------------------------------------------
// GM Drum Map (MIDI 36-67) -> Template assignment:
//
// Pad  MIDI  GM Name              Template   Size Override
//  0    36   Bass Drum 1          Kick       -
//  1    37   Side Stick           Perc       -
//  2    38   Acoustic Snare       Snare      -
//  3    39   Hand Clap            Perc       -
//  4    40   Electric Snare       Snare      -
//  5    41   Low Floor Tom        Tom        0.8
//  6    42   Closed Hi-Hat        Hat        -  (choke 1)
//  7    43   High Floor Tom       Tom        0.7
//  8    44   Pedal Hi-Hat         Hat        -  (choke 1)
//  9    45   Low Tom              Tom        0.6
// 10    46   Open Hi-Hat          Hat        -  (choke 1)
// 11    47   Low-Mid Tom          Tom        0.5
// 12    48   Hi-Mid Tom           Tom        0.45
// 13    49   Crash Cymbal 1       Cymbal     -
// 14    50   High Tom             Tom        0.4
// 15    51   Ride Cymbal 1        Cymbal     -
// 16    52   Chinese Cymbal       Cymbal     -
// 17    53   Ride Bell            Cymbal     -
// 18    54   Tambourine           Perc       -
// 19    55   Splash Cymbal        Cymbal     -
// 20    56   Cowbell              Perc       -
// 21    57   Crash Cymbal 2       Cymbal     -
// 22    58   Vibraslap            Perc       -
// 23    59   Ride Cymbal 2        Cymbal     -
// 24    60   Hi Bongo             Perc       -
// 25    61   Low Bongo            Perc       -
// 26    62   Mute Hi Conga        Perc       -
// 27    63   Open Hi Conga        Perc       -
// 28    64   Low Conga            Perc       -
// 29    65   High Timbale         Perc       -
// 30    66   Low Timbale          Perc       -
// 31    67   High Agogo           Perc       -
// ---------------------------------------------------------------------------
inline void apply(std::array<PadConfig, kNumPads>& pads)
{
    // Per-pad template and size override table.
    // Index = pad index (0-31), corresponding to MIDI notes 36-67.
    struct PadSpec {
        DrumTemplate tmpl;
        float sizeOverride;  // -1.0 = use template default
    };

    static constexpr PadSpec kSpecs[kNumPads] = {
        {.tmpl = DrumTemplate::Kick,   .sizeOverride = -1.0f },  //  0: MIDI 36 Bass Drum 1
        {.tmpl = DrumTemplate::Perc,   .sizeOverride = -1.0f },  //  1: MIDI 37 Side Stick
        {.tmpl = DrumTemplate::Snare,  .sizeOverride = -1.0f },  //  2: MIDI 38 Acoustic Snare
        {.tmpl = DrumTemplate::Perc,   .sizeOverride = -1.0f },  //  3: MIDI 39 Hand Clap
        {.tmpl = DrumTemplate::Snare,  .sizeOverride = -1.0f },  //  4: MIDI 40 Electric Snare
        {.tmpl = DrumTemplate::Tom,    .sizeOverride =  0.8f },  //  5: MIDI 41 Low Floor Tom
        {.tmpl = DrumTemplate::Hat,    .sizeOverride = -1.0f },  //  6: MIDI 42 Closed Hi-Hat
        {.tmpl = DrumTemplate::Tom,    .sizeOverride =  0.7f },  //  7: MIDI 43 High Floor Tom
        {.tmpl = DrumTemplate::Hat,    .sizeOverride = -1.0f },  //  8: MIDI 44 Pedal Hi-Hat
        {.tmpl = DrumTemplate::Tom,    .sizeOverride =  0.6f },  //  9: MIDI 45 Low Tom
        {.tmpl = DrumTemplate::Hat,    .sizeOverride = -1.0f },  // 10: MIDI 46 Open Hi-Hat
        {.tmpl = DrumTemplate::Tom,    .sizeOverride =  0.5f },  // 11: MIDI 47 Low-Mid Tom
        {.tmpl = DrumTemplate::Tom,    .sizeOverride = 0.45f },  // 12: MIDI 48 Hi-Mid Tom
        {.tmpl = DrumTemplate::Cymbal, .sizeOverride = -1.0f },  // 13: MIDI 49 Crash Cymbal 1
        {.tmpl = DrumTemplate::Tom,    .sizeOverride =  0.4f },  // 14: MIDI 50 High Tom
        {.tmpl = DrumTemplate::Cymbal, .sizeOverride = -1.0f },  // 15: MIDI 51 Ride Cymbal 1
        {.tmpl = DrumTemplate::Cymbal, .sizeOverride = -1.0f },  // 16: MIDI 52 Chinese Cymbal
        {.tmpl = DrumTemplate::Cymbal, .sizeOverride = -1.0f },  // 17: MIDI 53 Ride Bell
        {.tmpl = DrumTemplate::Perc,   .sizeOverride = -1.0f },  // 18: MIDI 54 Tambourine
        {.tmpl = DrumTemplate::Cymbal, .sizeOverride = -1.0f },  // 19: MIDI 55 Splash Cymbal
        {.tmpl = DrumTemplate::Perc,   .sizeOverride = -1.0f },  // 20: MIDI 56 Cowbell
        {.tmpl = DrumTemplate::Cymbal, .sizeOverride = -1.0f },  // 21: MIDI 57 Crash Cymbal 2
        {.tmpl = DrumTemplate::Perc,   .sizeOverride = -1.0f },  // 22: MIDI 58 Vibraslap
        {.tmpl = DrumTemplate::Cymbal, .sizeOverride = -1.0f },  // 23: MIDI 59 Ride Cymbal 2
        {.tmpl = DrumTemplate::Perc,   .sizeOverride = -1.0f },  // 24: MIDI 60 Hi Bongo
        {.tmpl = DrumTemplate::Perc,   .sizeOverride = -1.0f },  // 25: MIDI 61 Low Bongo
        {.tmpl = DrumTemplate::Perc,   .sizeOverride = -1.0f },  // 26: MIDI 62 Mute Hi Conga
        {.tmpl = DrumTemplate::Perc,   .sizeOverride = -1.0f },  // 27: MIDI 63 Open Hi Conga
        {.tmpl = DrumTemplate::Perc,   .sizeOverride = -1.0f },  // 28: MIDI 64 Low Conga
        {.tmpl = DrumTemplate::Perc,   .sizeOverride = -1.0f },  // 29: MIDI 65 High Timbale
        {.tmpl = DrumTemplate::Perc,   .sizeOverride = -1.0f },  // 30: MIDI 66 Low Timbale
        {.tmpl = DrumTemplate::Perc,   .sizeOverride = -1.0f },  // 31: MIDI 67 High Agogo
    };

    for (int i = 0; i < kNumPads; ++i) {
        applyTemplate(pads[i], kSpecs[i].tmpl, kSpecs[i].sizeOverride);
    }

    // FR-032: Hat pads in choke group 1
    // Pad 6 = MIDI 42 (Closed Hi-Hat)
    // Pad 8 = MIDI 44 (Pedal Hi-Hat)
    // Pad 10 = MIDI 46 (Open Hi-Hat)
    pads[6].chokeGroup  = 1;
    pads[8].chokeGroup  = 1;
    pads[10].chokeGroup = 1;
}

} // namespace DefaultKit
} // namespace Membrum
