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
            break;

        case DrumTemplate::Snare:
            cfg.exciterType = ExciterType::NoiseBurst;
            cfg.bodyModel   = BodyModelType::Membrane;
            cfg.material       = 0.5f;
            cfg.size           = 0.5f;
            cfg.decay          = 0.4f;
            cfg.strikePosition = 0.3f;
            cfg.level          = 0.8f;
            // NoiseBurstDuration = 8ms -> (8-2)/13 = 0.461538...
            cfg.noiseBurstDuration = static_cast<float>((8.0 - 2.0) / 13.0);
            break;

        case DrumTemplate::Tom:
            cfg.exciterType = ExciterType::Mallet;
            cfg.bodyModel   = BodyModelType::Membrane;
            cfg.material       = 0.4f;
            cfg.size           = 0.5f;  // overridden by sizeOverride
            cfg.decay          = 0.5f;
            cfg.strikePosition = 0.3f;
            cfg.level          = 0.8f;
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
            break;

        case DrumTemplate::Cymbal:
            cfg.exciterType = ExciterType::NoiseBurst;
            cfg.bodyModel   = BodyModelType::NoiseBody;
            cfg.material       = 0.95f;
            cfg.size           = 0.3f;
            cfg.decay          = 0.8f;
            cfg.strikePosition = 0.3f;
            cfg.level          = 0.8f;
            // NoiseBurstDuration = 10ms -> (10-2)/13 = 0.615385...
            cfg.noiseBurstDuration = static_cast<float>((10.0 - 2.0) / 13.0);
            break;

        case DrumTemplate::Perc:
            cfg.exciterType = ExciterType::Mallet;
            cfg.bodyModel   = BodyModelType::Plate;
            cfg.material       = 0.7f;
            cfg.size           = 0.3f;
            cfg.decay          = 0.3f;
            cfg.strikePosition = 0.3f;
            cfg.level          = 0.8f;
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
        { DrumTemplate::Kick,   -1.0f },  //  0: MIDI 36 Bass Drum 1
        { DrumTemplate::Perc,   -1.0f },  //  1: MIDI 37 Side Stick
        { DrumTemplate::Snare,  -1.0f },  //  2: MIDI 38 Acoustic Snare
        { DrumTemplate::Perc,   -1.0f },  //  3: MIDI 39 Hand Clap
        { DrumTemplate::Snare,  -1.0f },  //  4: MIDI 40 Electric Snare
        { DrumTemplate::Tom,     0.8f },  //  5: MIDI 41 Low Floor Tom
        { DrumTemplate::Hat,    -1.0f },  //  6: MIDI 42 Closed Hi-Hat
        { DrumTemplate::Tom,     0.7f },  //  7: MIDI 43 High Floor Tom
        { DrumTemplate::Hat,    -1.0f },  //  8: MIDI 44 Pedal Hi-Hat
        { DrumTemplate::Tom,     0.6f },  //  9: MIDI 45 Low Tom
        { DrumTemplate::Hat,    -1.0f },  // 10: MIDI 46 Open Hi-Hat
        { DrumTemplate::Tom,     0.5f },  // 11: MIDI 47 Low-Mid Tom
        { DrumTemplate::Tom,    0.45f },  // 12: MIDI 48 Hi-Mid Tom
        { DrumTemplate::Cymbal, -1.0f },  // 13: MIDI 49 Crash Cymbal 1
        { DrumTemplate::Tom,     0.4f },  // 14: MIDI 50 High Tom
        { DrumTemplate::Cymbal, -1.0f },  // 15: MIDI 51 Ride Cymbal 1
        { DrumTemplate::Cymbal, -1.0f },  // 16: MIDI 52 Chinese Cymbal
        { DrumTemplate::Cymbal, -1.0f },  // 17: MIDI 53 Ride Bell
        { DrumTemplate::Perc,   -1.0f },  // 18: MIDI 54 Tambourine
        { DrumTemplate::Cymbal, -1.0f },  // 19: MIDI 55 Splash Cymbal
        { DrumTemplate::Perc,   -1.0f },  // 20: MIDI 56 Cowbell
        { DrumTemplate::Cymbal, -1.0f },  // 21: MIDI 57 Crash Cymbal 2
        { DrumTemplate::Perc,   -1.0f },  // 22: MIDI 58 Vibraslap
        { DrumTemplate::Cymbal, -1.0f },  // 23: MIDI 59 Ride Cymbal 2
        { DrumTemplate::Perc,   -1.0f },  // 24: MIDI 60 Hi Bongo
        { DrumTemplate::Perc,   -1.0f },  // 25: MIDI 61 Low Bongo
        { DrumTemplate::Perc,   -1.0f },  // 26: MIDI 62 Mute Hi Conga
        { DrumTemplate::Perc,   -1.0f },  // 27: MIDI 63 Open Hi Conga
        { DrumTemplate::Perc,   -1.0f },  // 28: MIDI 64 Low Conga
        { DrumTemplate::Perc,   -1.0f },  // 29: MIDI 65 High Timbale
        { DrumTemplate::Perc,   -1.0f },  // 30: MIDI 66 Low Timbale
        { DrumTemplate::Perc,   -1.0f },  // 31: MIDI 67 High Agogo
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
