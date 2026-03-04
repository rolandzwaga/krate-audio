#pragma once

// ==============================================================================
// Ruinae Preset Format Header
// ==============================================================================
// Shared format definitions for the Ruinae preset generator and compatibility
// tests. These structs and their serialize() methods must produce byte-identical
// output to Processor::getState() in processor.cpp.
//
// When adding new fields to the processor state, update the corresponding
// struct here AND its serialize() method. The preset_format_compat_test will
// catch any drift.
// ==============================================================================

#include <vector>
#include <cstdint>
#include <array>

namespace RuinaeFormat {

// ==============================================================================
// Binary Writer (matches IBStreamer little-endian format)
// ==============================================================================

class BinaryWriter {
public:
    std::vector<uint8_t> data;

    void writeInt32(int32_t val) {
        auto bytes = reinterpret_cast<const uint8_t*>(&val);
        data.insert(data.end(), bytes, bytes + 4);
    }

    void writeFloat(float val) {
        auto bytes = reinterpret_cast<const uint8_t*>(&val);
        data.insert(data.end(), bytes, bytes + 4);
    }

    void writeInt8(int8_t val) {
        data.push_back(static_cast<uint8_t>(val));
    }
};

// ==============================================================================
// Constants
// ==============================================================================

static constexpr int32_t kStateVersion = 2;

// Trance gate state version marker (must match kTranceGateStateVersion in trance_gate_params.h)
static constexpr int32_t kTranceGateStateVersion = 3;

// Note value default index (1/8 note = index 10)
static constexpr int kNoteValueDefaultIndex = 10;

// Arp step flags
static constexpr int kStepActive = 0x01;
static constexpr int kStepTie    = 0x02;
static constexpr int kStepSlide  = 0x04;
static constexpr int kStepAccent = 0x08;

// ==============================================================================
// Preset State Sub-Structs (defaults match *Params struct constructors)
// ==============================================================================

struct GlobalState {
    float masterGain = 1.0f;
    int32_t voiceMode = 0;
    int32_t polyphony = 8;
    int32_t softLimit = 1; // true
    float width = 1.0f;
    float spread = 0.0f;

    void serialize(BinaryWriter& w) const {
        w.writeFloat(masterGain);
        w.writeInt32(voiceMode);
        w.writeInt32(polyphony);
        w.writeInt32(softLimit);
        w.writeFloat(width);
        w.writeFloat(spread);
    }
};

struct OscState {
    // Existing fields
    int32_t type = 0;
    float tuneSemitones = 0.0f;
    float fineCents = 0.0f;
    float level = 1.0f;
    float phase = 0.0f;
    // PolyBLEP / Wavetable
    int32_t waveform = 1; // Sawtooth
    float pulseWidth = 0.5f;
    float phaseMod = 0.0f;
    float freqMod = 0.0f;
    // Phase Distortion
    int32_t pdWaveform = 0;
    float pdDistortion = 0.0f;
    // Sync
    float syncRatio = 2.0f;
    int32_t syncWaveform = 1;
    int32_t syncMode = 0;
    float syncAmount = 1.0f;
    float syncPulseWidth = 0.5f;
    // Additive
    int32_t additivePartials = 16;
    float additiveTilt = 0.0f;
    float additiveInharm = 0.0f;
    // Chaos
    int32_t chaosAttractor = 0;
    float chaosAmount = 0.5f;
    float chaosCoupling = 0.0f;
    int32_t chaosOutput = 0;
    // Particle
    float particleScatter = 3.0f;
    float particleDensity = 16.0f;
    float particleLifetime = 200.0f;
    int32_t particleSpawnMode = 0;
    int32_t particleEnvType = 0;
    float particleDrift = 0.0f;
    // Formant
    int32_t formantVowel = 0;
    float formantMorph = 0.0f;
    // Spectral Freeze
    float spectralPitch = 0.0f;
    float spectralTilt = 0.0f;
    float spectralFormant = 0.0f;
    // Noise
    int32_t noiseColor = 0;

    void serialize(BinaryWriter& w) const {
        w.writeInt32(type);
        w.writeFloat(tuneSemitones);
        w.writeFloat(fineCents);
        w.writeFloat(level);
        w.writeFloat(phase);
        w.writeInt32(waveform);
        w.writeFloat(pulseWidth);
        w.writeFloat(phaseMod);
        w.writeFloat(freqMod);
        w.writeInt32(pdWaveform);
        w.writeFloat(pdDistortion);
        w.writeFloat(syncRatio);
        w.writeInt32(syncWaveform);
        w.writeInt32(syncMode);
        w.writeFloat(syncAmount);
        w.writeFloat(syncPulseWidth);
        w.writeInt32(additivePartials);
        w.writeFloat(additiveTilt);
        w.writeFloat(additiveInharm);
        w.writeInt32(chaosAttractor);
        w.writeFloat(chaosAmount);
        w.writeFloat(chaosCoupling);
        w.writeInt32(chaosOutput);
        w.writeFloat(particleScatter);
        w.writeFloat(particleDensity);
        w.writeFloat(particleLifetime);
        w.writeInt32(particleSpawnMode);
        w.writeInt32(particleEnvType);
        w.writeFloat(particleDrift);
        w.writeInt32(formantVowel);
        w.writeFloat(formantMorph);
        w.writeFloat(spectralPitch);
        w.writeFloat(spectralTilt);
        w.writeFloat(spectralFormant);
        w.writeInt32(noiseColor);
    }
};

struct MixerState {
    int32_t mode = 0;
    float position = 0.5f;
    float tilt = 0.0f;
    float shift = 0.0f;

    void serialize(BinaryWriter& w) const {
        w.writeInt32(mode);
        w.writeFloat(position);
        w.writeFloat(tilt);
        w.writeFloat(shift);
    }
};

struct FilterState {
    int32_t type = 0;
    float cutoffHz = 20000.0f;
    float resonance = 0.1f;
    float envAmount = 0.0f;
    float keyTrack = 0.0f;
    int32_t ladderSlope = 4;
    float ladderDrive = 0.0f;
    float formantMorph = 0.0f;
    float formantGender = 0.0f;
    float combDamping = 0.0f;
    int32_t svfSlope = 1;
    float svfDrive = 0.0f;
    float svfGain = 0.0f;
    int32_t envSubType = 0;
    float envSensitivity = 0.0f;
    float envDepth = 1.0f;
    float envAttack = 10.0f;
    float envRelease = 100.0f;
    int32_t envDirection = 0;
    float selfOscGlide = 0.0f;
    float selfOscExtMix = 0.5f;
    float selfOscShape = 0.0f;
    float selfOscRelease = 500.0f;

    void serialize(BinaryWriter& w) const {
        w.writeInt32(type);
        w.writeFloat(cutoffHz);
        w.writeFloat(resonance);
        w.writeFloat(envAmount);
        w.writeFloat(keyTrack);
        w.writeInt32(ladderSlope);
        w.writeFloat(ladderDrive);
        w.writeFloat(formantMorph);
        w.writeFloat(formantGender);
        w.writeFloat(combDamping);
        w.writeInt32(svfSlope);
        w.writeFloat(svfDrive);
        w.writeFloat(svfGain);
        w.writeInt32(envSubType);
        w.writeFloat(envSensitivity);
        w.writeFloat(envDepth);
        w.writeFloat(envAttack);
        w.writeFloat(envRelease);
        w.writeInt32(envDirection);
        w.writeFloat(selfOscGlide);
        w.writeFloat(selfOscExtMix);
        w.writeFloat(selfOscShape);
        w.writeFloat(selfOscRelease);
    }
};

struct DistortionState {
    int32_t type = 0;
    float drive = 0.0f;
    float character = 0.5f;
    float mix = 1.0f;
    int32_t chaosModel = 0;
    float chaosSpeed = 0.5f;
    float chaosCoupling = 0.0f;
    int32_t spectralMode = 0;
    int32_t spectralCurve = 0;
    float spectralBits = 1.0f;
    float grainSize = 0.47f;
    float grainDensity = 0.43f;
    float grainVariation = 0.0f;
    float grainJitter = 0.0f;
    int32_t foldType = 0;
    int32_t tapeModel = 0;
    float tapeSaturation = 0.5f;
    float tapeBias = 0.5f;
    // Ring Modulator
    float ringFreq = 0.6882f;         // normalized log-mapped to 440 Hz
    int32_t ringFreqMode = 1;         // 0=Free, 1=NoteTrack
    float ringRatio = 0.1111f;        // normalized linear-mapped to 2.0
    int32_t ringWaveform = 0;         // 0-4 (Sine/Tri/Saw/Sq/Noise)
    float ringStereoSpread = 0.0f;    // 0-1

    void serialize(BinaryWriter& w) const {
        w.writeInt32(type);
        w.writeFloat(drive);
        w.writeFloat(character);
        w.writeFloat(mix);
        w.writeInt32(chaosModel);
        w.writeFloat(chaosSpeed);
        w.writeFloat(chaosCoupling);
        w.writeInt32(spectralMode);
        w.writeInt32(spectralCurve);
        w.writeFloat(spectralBits);
        w.writeFloat(grainSize);
        w.writeFloat(grainDensity);
        w.writeFloat(grainVariation);
        w.writeFloat(grainJitter);
        w.writeInt32(foldType);
        w.writeInt32(tapeModel);
        w.writeFloat(tapeSaturation);
        w.writeFloat(tapeBias);
        // Ring Modulator
        w.writeFloat(ringFreq);
        w.writeInt32(ringFreqMode);
        w.writeFloat(ringRatio);
        w.writeInt32(ringWaveform);
        w.writeFloat(ringStereoSpread);
    }
};

struct TranceGateState {
    int32_t enabled = 0; // false
    int32_t numSteps = 16;
    float rateHz = 4.0f;
    float depth = 1.0f;
    float attackMs = 2.0f;
    float releaseMs = 10.0f;
    int32_t tempoSync = 1; // true
    int32_t noteValue = kNoteValueDefaultIndex;
    // v2 fields
    int32_t euclideanEnabled = 0;
    int32_t euclideanHits = 4;
    int32_t euclideanRotation = 0;
    float phaseOffset = 0.0f;
    // 32 step levels (default 1.0)
    float stepLevels[32]{};
    // Retrigger depth (v2 addition)
    float retriggerDepth = 0.0f;

    TranceGateState() {
        for (auto& level : stepLevels)
            level = 1.0f;
    }

    void serialize(BinaryWriter& w) const {
        // v1 fields
        w.writeInt32(enabled);
        w.writeInt32(numSteps);
        w.writeFloat(rateHz);
        w.writeFloat(depth);
        w.writeFloat(attackMs);
        w.writeFloat(releaseMs);
        w.writeInt32(tempoSync);
        w.writeInt32(noteValue);
        // v2 marker and fields
        w.writeInt32(kTranceGateStateVersion);
        w.writeInt32(euclideanEnabled);
        w.writeInt32(euclideanHits);
        w.writeInt32(euclideanRotation);
        w.writeFloat(phaseOffset);
        // 32 step levels
        for (int i = 0; i < 32; ++i)
            w.writeFloat(stepLevels[i]);
        // Retrigger depth
        w.writeFloat(retriggerDepth);
    }
};

struct EnvelopeState {
    float attackMs;
    float decayMs;
    float sustain;
    float releaseMs;
    float attackCurve = 0.0f;
    float decayCurve = 0.0f;
    float releaseCurve = 0.0f;
    float bezierEnabled = 0.0f;
    float bezierAttackCp1X = 0.33f;
    float bezierAttackCp1Y = 0.33f;
    float bezierAttackCp2X = 0.67f;
    float bezierAttackCp2Y = 0.67f;
    float bezierDecayCp1X = 0.33f;
    float bezierDecayCp1Y = 0.67f;
    float bezierDecayCp2X = 0.67f;
    float bezierDecayCp2Y = 0.33f;
    float bezierReleaseCp1X = 0.33f;
    float bezierReleaseCp1Y = 0.67f;
    float bezierReleaseCp2X = 0.67f;
    float bezierReleaseCp2Y = 0.33f;

    void serialize(BinaryWriter& w) const {
        w.writeFloat(attackMs);
        w.writeFloat(decayMs);
        w.writeFloat(sustain);
        w.writeFloat(releaseMs);
        w.writeFloat(attackCurve);
        w.writeFloat(decayCurve);
        w.writeFloat(releaseCurve);
        w.writeFloat(bezierEnabled);
        w.writeFloat(bezierAttackCp1X);
        w.writeFloat(bezierAttackCp1Y);
        w.writeFloat(bezierAttackCp2X);
        w.writeFloat(bezierAttackCp2Y);
        w.writeFloat(bezierDecayCp1X);
        w.writeFloat(bezierDecayCp1Y);
        w.writeFloat(bezierDecayCp2X);
        w.writeFloat(bezierDecayCp2Y);
        w.writeFloat(bezierReleaseCp1X);
        w.writeFloat(bezierReleaseCp1Y);
        w.writeFloat(bezierReleaseCp2X);
        w.writeFloat(bezierReleaseCp2Y);
    }
};

// AmpEnvParams: attack=10, decay=100, sustain=0.8, release=200
struct AmpEnvState : EnvelopeState {
    AmpEnvState() {
        attackMs = 10.0f;
        decayMs = 100.0f;
        sustain = 0.8f;
        releaseMs = 200.0f;
        // AmpEnv bezier defaults differ: attack cp1Y=0.33, cp2Y=0.67
        bezierAttackCp1Y = 0.33f;
        bezierAttackCp2Y = 0.67f;
    }
};

// FilterEnvParams: attack=10, decay=200, sustain=0.5, release=300
struct FilterEnvState : EnvelopeState {
    FilterEnvState() {
        attackMs = 10.0f;
        decayMs = 200.0f;
        sustain = 0.5f;
        releaseMs = 300.0f;
    }
};

// ModEnvParams: attack=10, decay=300, sustain=0.5, release=500
struct ModEnvState : EnvelopeState {
    ModEnvState() {
        attackMs = 10.0f;
        decayMs = 300.0f;
        sustain = 0.5f;
        releaseMs = 500.0f;
    }
};

struct LFOBaseState {
    float rateHz = 1.0f;
    int32_t shape = 0;
    float depth = 1.0f;
    int32_t sync = 1; // true

    void serialize(BinaryWriter& w) const {
        w.writeFloat(rateHz);
        w.writeInt32(shape);
        w.writeFloat(depth);
        w.writeInt32(sync);
    }
};

struct LFOExtState {
    float phaseOffset = 0.0f;
    int32_t retrigger = 1; // true
    int32_t noteValue = kNoteValueDefaultIndex;
    int32_t unipolar = 0; // false
    float fadeInMs = 0.0f;
    float symmetry = 0.5f;
    int32_t quantizeSteps = 0;

    void serialize(BinaryWriter& w) const {
        w.writeFloat(phaseOffset);
        w.writeInt32(retrigger);
        w.writeInt32(noteValue);
        w.writeInt32(unipolar);
        w.writeFloat(fadeInMs);
        w.writeFloat(symmetry);
        w.writeInt32(quantizeSteps);
    }
};

struct ChaosModState {
    float rateHz = 1.0f;
    int32_t type = 0;
    float depth = 0.0f;
    int32_t sync = 0; // false
    int32_t noteValue = kNoteValueDefaultIndex;

    void serialize(BinaryWriter& w) const {
        w.writeFloat(rateHz);
        w.writeInt32(type);
        w.writeFloat(depth);
        w.writeInt32(sync);
        w.writeInt32(noteValue);
    }
};

struct ModMatrixSlotState {
    int32_t source = 0;
    int32_t dest = 0;
    float amount = 0.0f;
    int32_t curve = 0;
    float smoothMs = 0.0f;
    int32_t scale = 2;
    int32_t bypass = 0;
};

struct ModMatrixState {
    std::array<ModMatrixSlotState, 8> slots;

    void serialize(BinaryWriter& w) const {
        for (const auto& slot : slots) {
            w.writeInt32(slot.source);
            w.writeInt32(slot.dest);
            w.writeFloat(slot.amount);
            w.writeInt32(slot.curve);
            w.writeFloat(slot.smoothMs);
            w.writeInt32(slot.scale);
            w.writeInt32(slot.bypass);
        }
    }
};

struct GlobalFilterState {
    int32_t enabled = 0; // false
    int32_t type = 0;
    float cutoffHz = 1000.0f;
    float resonance = 0.707f;

    void serialize(BinaryWriter& w) const {
        w.writeInt32(enabled);
        w.writeInt32(type);
        w.writeFloat(cutoffHz);
        w.writeFloat(resonance);
    }
};

struct DelayState {
    // Common
    int32_t type = 0;
    float timeMs = 500.0f;
    float feedback = 0.4f;
    float mix = 0.5f;
    int32_t sync = 1; // true
    int32_t noteValue = kNoteValueDefaultIndex;
    // Digital
    int32_t digitalEra = 0;
    float digitalAge = 0.0f;
    int32_t digitalLimiter = 0;
    float digitalModDepth = 0.0f;
    float digitalModRateHz = 1.0f;
    int32_t digitalModWaveform = 0;
    float digitalWidth = 100.0f;
    float digitalWavefoldAmt = 0.0f;
    int32_t digitalWavefoldModel = 0;
    float digitalWavefoldSym = 0.0f;
    // Tape
    float tapeInertiaMs = 300.0f;
    float tapeWear = 0.0f;
    float tapeSaturation = 0.5f;
    float tapeAge = 0.0f;
    int32_t tapeSpliceEnabled = 0; // false
    float tapeSpliceIntensity = 0.0f;
    int32_t tapeHead1Enabled = 1; // true
    float tapeHead1Level = 0.0f;
    float tapeHead1Pan = 0.0f;
    int32_t tapeHead2Enabled = 1; // true
    float tapeHead2Level = 0.0f;
    float tapeHead2Pan = 0.0f;
    int32_t tapeHead3Enabled = 1; // true
    float tapeHead3Level = 0.0f;
    float tapeHead3Pan = 0.0f;
    // Granular
    float granularSizeMs = 100.0f;
    float granularDensity = 10.0f;
    float granularPitch = 0.0f;
    float granularPitchSpray = 0.0f;
    int32_t granularPitchQuant = 0;
    float granularPosSpray = 0.0f;
    float granularReverseProb = 0.0f;
    float granularPanSpray = 0.0f;
    float granularJitter = 0.0f;
    float granularTexture = 0.0f;
    float granularWidth = 1.0f;
    int32_t granularEnvelope = 0;
    int32_t granularFreeze = 0; // false
    // Spectral
    int32_t spectralFFTSize = 1;
    float spectralSpreadMs = 0.0f;
    int32_t spectralDirection = 0;
    int32_t spectralCurve = 0;
    float spectralTilt = 0.0f;
    float spectralDiffusion = 0.0f;
    float spectralWidth = 0.0f;
    int32_t spectralFreeze = 0; // false
    // PingPong
    int32_t pingPongRatio = 0;
    float pingPongCrossFeed = 1.0f;
    float pingPongWidth = 100.0f;
    float pingPongModDepth = 0.0f;
    float pingPongModRateHz = 1.0f;

    void serialize(BinaryWriter& w) const {
        // Common
        w.writeInt32(type);
        w.writeFloat(timeMs);
        w.writeFloat(feedback);
        w.writeFloat(mix);
        w.writeInt32(sync);
        w.writeInt32(noteValue);
        // Digital
        w.writeInt32(digitalEra);
        w.writeFloat(digitalAge);
        w.writeInt32(digitalLimiter);
        w.writeFloat(digitalModDepth);
        w.writeFloat(digitalModRateHz);
        w.writeInt32(digitalModWaveform);
        w.writeFloat(digitalWidth);
        w.writeFloat(digitalWavefoldAmt);
        w.writeInt32(digitalWavefoldModel);
        w.writeFloat(digitalWavefoldSym);
        // Tape
        w.writeFloat(tapeInertiaMs);
        w.writeFloat(tapeWear);
        w.writeFloat(tapeSaturation);
        w.writeFloat(tapeAge);
        w.writeInt32(tapeSpliceEnabled);
        w.writeFloat(tapeSpliceIntensity);
        w.writeInt32(tapeHead1Enabled);
        w.writeFloat(tapeHead1Level);
        w.writeFloat(tapeHead1Pan);
        w.writeInt32(tapeHead2Enabled);
        w.writeFloat(tapeHead2Level);
        w.writeFloat(tapeHead2Pan);
        w.writeInt32(tapeHead3Enabled);
        w.writeFloat(tapeHead3Level);
        w.writeFloat(tapeHead3Pan);
        // Granular
        w.writeFloat(granularSizeMs);
        w.writeFloat(granularDensity);
        w.writeFloat(granularPitch);
        w.writeFloat(granularPitchSpray);
        w.writeInt32(granularPitchQuant);
        w.writeFloat(granularPosSpray);
        w.writeFloat(granularReverseProb);
        w.writeFloat(granularPanSpray);
        w.writeFloat(granularJitter);
        w.writeFloat(granularTexture);
        w.writeFloat(granularWidth);
        w.writeInt32(granularEnvelope);
        w.writeInt32(granularFreeze);
        // Spectral
        w.writeInt32(spectralFFTSize);
        w.writeFloat(spectralSpreadMs);
        w.writeInt32(spectralDirection);
        w.writeInt32(spectralCurve);
        w.writeFloat(spectralTilt);
        w.writeFloat(spectralDiffusion);
        w.writeFloat(spectralWidth);
        w.writeInt32(spectralFreeze);
        // PingPong
        w.writeInt32(pingPongRatio);
        w.writeFloat(pingPongCrossFeed);
        w.writeFloat(pingPongWidth);
        w.writeFloat(pingPongModDepth);
        w.writeFloat(pingPongModRateHz);
    }
};

struct ReverbState {
    float size = 0.5f;
    float damping = 0.5f;
    float width = 1.0f;
    float mix = 0.5f;
    float preDelayMs = 0.0f;
    float diffusion = 0.7f;
    int32_t freeze = 0; // false
    float modRateHz = 0.5f;
    float modDepth = 0.0f;

    void serialize(BinaryWriter& w) const {
        w.writeFloat(size);
        w.writeFloat(damping);
        w.writeFloat(width);
        w.writeFloat(mix);
        w.writeFloat(preDelayMs);
        w.writeFloat(diffusion);
        w.writeInt32(freeze);
        w.writeFloat(modRateHz);
        w.writeFloat(modDepth);
    }
};

struct MonoModeState {
    int32_t priority = 0;
    int32_t legato = 0; // false
    float portamentoTimeMs = 0.0f;
    int32_t portaMode = 0;

    void serialize(BinaryWriter& w) const {
        w.writeInt32(priority);
        w.writeInt32(legato);
        w.writeFloat(portamentoTimeMs);
        w.writeInt32(portaMode);
    }
};

struct VoiceRouteState {
    int8_t source = 0;
    int8_t destination = 0;
    float amount = 0.0f;
    int8_t curve = 0;
    float smoothMs = 0.0f;
    int8_t scale = 2;
    int8_t bypass = 0;
    int8_t active = 0;

    void serialize(BinaryWriter& w) const {
        w.writeInt8(source);
        w.writeInt8(destination);
        w.writeFloat(amount);
        w.writeInt8(curve);
        w.writeFloat(smoothMs);
        w.writeInt8(scale);
        w.writeInt8(bypass);
        w.writeInt8(active);
    }
};

struct PhaserState {
    float rateHz = 0.5f;
    float depth = 0.5f;
    float feedback = 0.5f;
    float mix = 0.5f;
    int32_t stages = 1;
    float centerFreqHz = 1000.0f;
    float stereoSpread = 0.0f;
    int32_t waveform = 0;
    int32_t sync = 0; // false
    int32_t noteValue = kNoteValueDefaultIndex;

    void serialize(BinaryWriter& w) const {
        w.writeFloat(rateHz);
        w.writeFloat(depth);
        w.writeFloat(feedback);
        w.writeFloat(mix);
        w.writeInt32(stages);
        w.writeFloat(centerFreqHz);
        w.writeFloat(stereoSpread);
        w.writeInt32(waveform);
        w.writeInt32(sync);
        w.writeInt32(noteValue);
    }
};

struct MacroState {
    float values[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    void serialize(BinaryWriter& w) const {
        for (int i = 0; i < 4; ++i)
            w.writeFloat(values[i]);
    }
};

struct RunglerState {
    float osc1FreqHz = 2.0f;
    float osc2FreqHz = 3.0f;
    float depth = 0.0f;
    float filter = 0.0f;
    int32_t bits = 8;
    int32_t loopMode = 0; // false

    void serialize(BinaryWriter& w) const {
        w.writeFloat(osc1FreqHz);
        w.writeFloat(osc2FreqHz);
        w.writeFloat(depth);
        w.writeFloat(filter);
        w.writeInt32(bits);
        w.writeInt32(loopMode);
    }
};

struct SettingsState {
    float pitchBendRangeSemitones = 2.0f;
    int32_t velocityCurve = 0;
    float tuningReferenceHz = 440.0f;
    int32_t voiceAllocMode = 1; // Oldest
    int32_t voiceStealMode = 0; // Hard
    int32_t gainCompensation = 1; // true

    void serialize(BinaryWriter& w) const {
        w.writeFloat(pitchBendRangeSemitones);
        w.writeInt32(velocityCurve);
        w.writeFloat(tuningReferenceHz);
        w.writeInt32(voiceAllocMode);
        w.writeInt32(voiceStealMode);
        w.writeInt32(gainCompensation);
    }
};

struct EnvFollowerState {
    float sensitivity = 0.5f;
    float attackMs = 10.0f;
    float releaseMs = 100.0f;

    void serialize(BinaryWriter& w) const {
        w.writeFloat(sensitivity);
        w.writeFloat(attackMs);
        w.writeFloat(releaseMs);
    }
};

struct SampleHoldState {
    float rateHz = 4.0f;
    int32_t sync = 0; // false
    int32_t noteValue = kNoteValueDefaultIndex;
    float slewMs = 0.0f;

    void serialize(BinaryWriter& w) const {
        w.writeFloat(rateHz);
        w.writeInt32(sync);
        w.writeInt32(noteValue);
        w.writeFloat(slewMs);
    }
};

struct RandomState {
    float rateHz = 4.0f;
    int32_t sync = 0; // false
    int32_t noteValue = kNoteValueDefaultIndex;
    float smoothness = 0.0f;

    void serialize(BinaryWriter& w) const {
        w.writeFloat(rateHz);
        w.writeInt32(sync);
        w.writeInt32(noteValue);
        w.writeFloat(smoothness);
    }
};

struct PitchFollowerState {
    float minHz = 80.0f;
    float maxHz = 2000.0f;
    float confidence = 0.5f;
    float speedMs = 50.0f;

    void serialize(BinaryWriter& w) const {
        w.writeFloat(minHz);
        w.writeFloat(maxHz);
        w.writeFloat(confidence);
        w.writeFloat(speedMs);
    }
};

struct TransientState {
    float sensitivity = 0.5f;
    float attackMs = 2.0f;
    float decayMs = 50.0f;

    void serialize(BinaryWriter& w) const {
        w.writeFloat(sensitivity);
        w.writeFloat(attackMs);
        w.writeFloat(decayMs);
    }
};

struct HarmonizerState {
    int32_t harmonyMode = 0;
    int32_t key = 0;
    int32_t scale = 0;
    int32_t pitchShiftMode = 0;
    int32_t formantPreserve = 0; // false
    int32_t numVoices = 4;
    float dryLevelDb = 0.0f;
    float wetLevelDb = -6.0f;
    // Per-voice (4 voices)
    int32_t voiceInterval[4] = {0, 0, 0, 0};
    float voiceLevelDb[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float voicePan[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float voiceDelayMs[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float voiceDetuneCents[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    void serialize(BinaryWriter& w) const {
        w.writeInt32(harmonyMode);
        w.writeInt32(key);
        w.writeInt32(scale);
        w.writeInt32(pitchShiftMode);
        w.writeInt32(formantPreserve);
        w.writeInt32(numVoices);
        w.writeFloat(dryLevelDb);
        w.writeFloat(wetLevelDb);
        for (int v = 0; v < 4; ++v) {
            w.writeInt32(voiceInterval[v]);
            w.writeFloat(voiceLevelDb[v]);
            w.writeFloat(voicePan[v]);
            w.writeFloat(voiceDelayMs[v]);
            w.writeFloat(voiceDetuneCents[v]);
        }
    }
};

struct ArpState {
    // Base params (11 values)
    int32_t enabled = 0; // false
    int32_t mode = 0; // Up
    int32_t octaveRange = 1;
    int32_t octaveMode = 0; // Sequential
    int32_t tempoSync = 1; // true
    int32_t noteValue = kNoteValueDefaultIndex; // 1/8 note
    float freeRate = 4.0f;
    float gateLength = 80.0f;
    float swing = 0.0f;
    int32_t latchMode = 0; // Off
    int32_t retrigger = 0; // Off

    // Velocity lane
    int32_t velocityLaneLength = 16;
    float velocityLaneSteps[32]{};

    // Gate lane
    int32_t gateLaneLength = 16;
    float gateLaneSteps[32]{};

    // Pitch lane
    int32_t pitchLaneLength = 16;
    int32_t pitchLaneSteps[32]{}; // all 0

    // Modifier lane
    int32_t modifierLaneLength = 16;
    int32_t modifierLaneSteps[32]{};
    int32_t accentVelocity = 30;
    float slideTime = 60.0f;

    // Ratchet lane
    int32_t ratchetLaneLength = 16;
    int32_t ratchetLaneSteps[32]{};

    // Euclidean
    int32_t euclideanEnabled = 0;
    int32_t euclideanHits = 4;
    int32_t euclideanSteps = 8;
    int32_t euclideanRotation = 0;

    // Condition lane
    int32_t conditionLaneLength = 16;
    int32_t conditionLaneSteps[32]{}; // all 0 = Always
    int32_t fillToggle = 0;

    // Spice/Humanize
    float spice = 0.0f;
    float humanize = 0.0f;

    // Ratchet swing
    float ratchetSwing = 50.0f;

    // Scale mode (ARP scale mode, commit 17ef2af)
    int32_t scaleType = 8;          // 0-15, default 8 = Chromatic
    int32_t rootNote = 0;           // 0=C, 1=C#, ..., 11=B
    int32_t scaleQuantizeInput = 0; // bool as int32

    // MIDI output
    int32_t midiOut = 0;            // bool as int32, default off

    ArpState() {
        // Velocity defaults to 1.0
        for (auto& step : velocityLaneSteps)
            step = 1.0f;
        // Gate defaults to 1.0
        for (auto& step : gateLaneSteps)
            step = 1.0f;
        // Pitch defaults to 0 (via zero-init)
        // Modifier defaults to kStepActive (0x01)
        for (auto& step : modifierLaneSteps)
            step = kStepActive;
        // Ratchet defaults to 1
        for (auto& step : ratchetLaneSteps)
            step = 1;
        // Condition defaults to 0 (Always) via zero-init
    }

    void serialize(BinaryWriter& w) const {
        // 11 base params
        w.writeInt32(enabled);
        w.writeInt32(mode);
        w.writeInt32(octaveRange);
        w.writeInt32(octaveMode);
        w.writeInt32(tempoSync);
        w.writeInt32(noteValue);
        w.writeFloat(freeRate);
        w.writeFloat(gateLength);
        w.writeFloat(swing);
        w.writeInt32(latchMode);
        w.writeInt32(retrigger);

        // Velocity lane
        w.writeInt32(velocityLaneLength);
        for (int i = 0; i < 32; ++i)
            w.writeFloat(velocityLaneSteps[i]);

        // Gate lane
        w.writeInt32(gateLaneLength);
        for (int i = 0; i < 32; ++i)
            w.writeFloat(gateLaneSteps[i]);

        // Pitch lane
        w.writeInt32(pitchLaneLength);
        for (int i = 0; i < 32; ++i)
            w.writeInt32(pitchLaneSteps[i]);

        // Modifier lane
        w.writeInt32(modifierLaneLength);
        for (int i = 0; i < 32; ++i)
            w.writeInt32(modifierLaneSteps[i]);
        w.writeInt32(accentVelocity);
        w.writeFloat(slideTime);

        // Ratchet lane
        w.writeInt32(ratchetLaneLength);
        for (int i = 0; i < 32; ++i)
            w.writeInt32(ratchetLaneSteps[i]);

        // Euclidean
        w.writeInt32(euclideanEnabled);
        w.writeInt32(euclideanHits);
        w.writeInt32(euclideanSteps);
        w.writeInt32(euclideanRotation);

        // Condition lane
        w.writeInt32(conditionLaneLength);
        for (int i = 0; i < 32; ++i)
            w.writeInt32(conditionLaneSteps[i]);
        w.writeInt32(fillToggle);

        // Spice/Humanize
        w.writeFloat(spice);
        w.writeFloat(humanize);

        // Ratchet swing
        w.writeFloat(ratchetSwing);

        // Scale mode (ARP scale mode, commit 17ef2af)
        w.writeInt32(scaleType);
        w.writeInt32(rootNote);
        w.writeInt32(scaleQuantizeInput);

        // MIDI output
        w.writeInt32(midiOut);
    }
};

// ==============================================================================
// Complete Ruinae Preset State
// ==============================================================================

struct RuinaePresetState {
    GlobalState global;
    OscState oscA;
    OscState oscB;
    MixerState mixer;
    FilterState filter;
    DistortionState distortion;
    TranceGateState tranceGate;
    AmpEnvState ampEnv;
    FilterEnvState filterEnv;
    ModEnvState modEnv;
    LFOBaseState lfo1;
    LFOBaseState lfo2;
    ChaosModState chaosMod;
    ModMatrixState modMatrix;
    GlobalFilterState globalFilter;
    DelayState delay;
    ReverbState reverb;
    MonoModeState monoMode;

    // Voice routes (16 slots, inline in processor.cpp)
    std::array<VoiceRouteState, 16> voiceRoutes;

    // FX enable flags (inline in processor.cpp)
    int8_t delayEnabled = 0; // false
    int8_t reverbEnabled = 0; // false

    // Phaser + enable
    PhaserState phaser;
    int8_t phaserEnabled = 0; // false

    // Extended LFO params
    LFOExtState lfo1Ext;
    LFOExtState lfo2Ext;

    // Macro and Rungler
    MacroState macros;
    RunglerState rungler;

    // Settings
    SettingsState settings;

    // Mod source params
    EnvFollowerState envFollower;
    SampleHoldState sampleHold;
    RandomState random;
    PitchFollowerState pitchFollower;
    TransientState transient;

    // Harmonizer + enable
    HarmonizerState harmonizer;
    int8_t harmonizerEnabled = 0; // false

    // Arpeggiator
    ArpState arp;

    std::vector<uint8_t> serialize() const {
        BinaryWriter w;

        // 1. State version
        w.writeInt32(kStateVersion);

        // 2-19. Synth parameter packs in order
        global.serialize(w);
        oscA.serialize(w);
        oscB.serialize(w);
        mixer.serialize(w);
        filter.serialize(w);
        distortion.serialize(w);
        tranceGate.serialize(w);
        ampEnv.serialize(w);
        filterEnv.serialize(w);
        modEnv.serialize(w);
        lfo1.serialize(w);
        lfo2.serialize(w);
        chaosMod.serialize(w);
        modMatrix.serialize(w);
        globalFilter.serialize(w);
        delay.serialize(w);
        reverb.serialize(w);
        monoMode.serialize(w);

        // 20. Voice routes (16 x {i8, i8, f32, i8, f32, i8, i8, i8})
        for (const auto& route : voiceRoutes)
            route.serialize(w);

        // 21. FX enable flags (2 x i8)
        w.writeInt8(delayEnabled);
        w.writeInt8(reverbEnabled);

        // 22. Phaser params + enable
        phaser.serialize(w);
        w.writeInt8(phaserEnabled);

        // 23-24. Extended LFO params
        lfo1Ext.serialize(w);
        lfo2Ext.serialize(w);

        // 25-26. Macro and Rungler
        macros.serialize(w);
        rungler.serialize(w);

        // 27. Settings
        settings.serialize(w);

        // 28-32. Mod source params
        envFollower.serialize(w);
        sampleHold.serialize(w);
        random.serialize(w);
        pitchFollower.serialize(w);
        transient.serialize(w);

        // 33. Harmonizer + enable
        harmonizer.serialize(w);
        w.writeInt8(harmonizerEnabled);

        // 34. Arpeggiator
        arp.serialize(w);

        return w.data;
    }
};

} // namespace RuinaeFormat
