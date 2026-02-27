// ==============================================================================
// Factory Preset Generator for Ruinae
// ==============================================================================
// Generates .vstpreset files matching the Processor::getState() binary format.
// Run this tool once during development to create factory arp presets.
//
// Reference: plugins/ruinae/src/processor/processor.cpp getState() (lines 488-559)
// Reference: tools/disrumpo_preset_generator.cpp (established pattern)
// ==============================================================================

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <array>

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

// kProcessorUID(0xA3B7C1D5, 0x2E4F6A8B, 0x9C0D1E2F, 0x3A4B5C6D)
const char kClassIdAscii[33] = "A3B7C1D52E4F6A8B9C0D1E2F3A4B5C6D";

static constexpr int32_t kStateVersion = 1;

// Trance gate state version marker (must match kTranceGateStateVersion = 2)
static constexpr int32_t kTranceGateStateVersion = 2;

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
// bezier cp defaults: same as EnvelopeState except attack cp1Y/cp2Y
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

// ==============================================================================
// VST3 Preset File Writer
// ==============================================================================

void writeLE32(std::ofstream& f, uint32_t val) {
    f.write(reinterpret_cast<const char*>(&val), 4);
}

void writeLE64(std::ofstream& f, int64_t val) {
    f.write(reinterpret_cast<const char*>(&val), 8);
}

bool writeVstPreset(const std::filesystem::path& path,
                    const std::vector<uint8_t>& componentState) {
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        std::cerr << "Failed to create: " << path << std::endl;
        return false;
    }

    const int64_t kHeaderSize = 48;
    int64_t compDataOffset = kHeaderSize;
    int64_t compDataSize = static_cast<int64_t>(componentState.size());
    int64_t listOffset = compDataOffset + compDataSize;

    // Header
    f.write("VST3", 4);
    writeLE32(f, 1);
    f.write(kClassIdAscii, 32);
    writeLE64(f, listOffset);

    // Component State Data
    f.write(reinterpret_cast<const char*>(componentState.data()), compDataSize);

    // Chunk List
    f.write("List", 4);
    writeLE32(f, 1);
    f.write("Comp", 4);
    writeLE64(f, compDataOffset);
    writeLE64(f, compDataSize);

    f.close();
    return true;
}

// ==============================================================================
// Preset Definition
// ==============================================================================

struct PresetDef {
    std::string name;
    std::string category;
    RuinaePresetState state;
};

// ==============================================================================
// Arp Helper Functions (T020-T022)
// ==============================================================================

void setArpEnabled(RuinaePresetState& s, bool enabled) {
    s.arp.enabled = enabled ? 1 : 0;
}

void setArpMode(RuinaePresetState& s, int32_t mode) {
    s.arp.mode = mode;
}

void setArpRate(RuinaePresetState& s, int32_t noteValueIndex) {
    s.arp.noteValue = noteValueIndex;
}

void setArpGateLength(RuinaePresetState& s, float gateLength) {
    s.arp.gateLength = gateLength;
}

void setArpSwing(RuinaePresetState& s, float swing) {
    s.arp.swing = swing;
}

void setTempoSync(RuinaePresetState& s, bool tempoSync) {
    s.arp.tempoSync = tempoSync ? 1 : 0;
}

void setVelocityLane(RuinaePresetState& s, int32_t length,
                     const float* steps) {
    s.arp.velocityLaneLength = std::clamp(length, 1, 32);
    for (int i = 0; i < length && i < 32; ++i)
        s.arp.velocityLaneSteps[i] = steps[i];
}

void setGateLane(RuinaePresetState& s, int32_t length,
                 const float* steps) {
    s.arp.gateLaneLength = std::clamp(length, 1, 32);
    for (int i = 0; i < length && i < 32; ++i)
        s.arp.gateLaneSteps[i] = steps[i];
}

void setPitchLane(RuinaePresetState& s, int32_t length,
                  const int32_t* steps) {
    s.arp.pitchLaneLength = std::clamp(length, 1, 32);
    for (int i = 0; i < length && i < 32; ++i)
        s.arp.pitchLaneSteps[i] = steps[i];
}

void setModifierLane(RuinaePresetState& s, int32_t length,
                     const int32_t* steps, int32_t accentVelocity,
                     float slideTime) {
    s.arp.modifierLaneLength = std::clamp(length, 1, 32);
    for (int i = 0; i < length && i < 32; ++i)
        s.arp.modifierLaneSteps[i] = steps[i];
    s.arp.accentVelocity = accentVelocity;
    s.arp.slideTime = slideTime;
}

void setRatchetLane(RuinaePresetState& s, int32_t length,
                    const int32_t* steps) {
    s.arp.ratchetLaneLength = std::clamp(length, 1, 32);
    for (int i = 0; i < length && i < 32; ++i)
        s.arp.ratchetLaneSteps[i] = steps[i];
}

void setConditionLane(RuinaePresetState& s, int32_t length,
                      const int32_t* steps, int32_t fillToggle) {
    s.arp.conditionLaneLength = std::clamp(length, 1, 32);
    for (int i = 0; i < length && i < 32; ++i)
        s.arp.conditionLaneSteps[i] = steps[i];
    s.arp.fillToggle = fillToggle;
}

void setEuclidean(RuinaePresetState& s, bool enabled, int32_t hits,
                  int32_t steps, int32_t rotation) {
    s.arp.euclideanEnabled = enabled ? 1 : 0;
    s.arp.euclideanHits = hits;
    s.arp.euclideanSteps = steps;
    s.arp.euclideanRotation = rotation;
}

// ==============================================================================
// Synth Patch Helpers (T023)
// ==============================================================================

// Warm pad: saw wave, low cutoff, slow attack/release, reverb on
void setSynthPad(RuinaePresetState& s) {
    s.oscA.waveform = 1;  // Sawtooth
    s.oscA.level = 0.7f;
    s.oscB.type = 0;
    s.oscB.waveform = 1;  // Sawtooth
    s.oscB.fineCents = 8.0f;  // Slight detune for warmth
    s.oscB.level = 0.5f;
    s.mixer.position = 0.45f;  // Slightly favor Osc A
    s.filter.type = 0;  // Ladder LP
    s.filter.cutoffHz = 3000.0f;
    s.filter.resonance = 0.2f;
    s.ampEnv.attackMs = 200.0f;
    s.ampEnv.decayMs = 500.0f;
    s.ampEnv.sustain = 0.7f;
    s.ampEnv.releaseMs = 800.0f;
    s.reverbEnabled = 1;
    s.reverb.size = 0.6f;
    s.reverb.mix = 0.3f;
    s.reverb.damping = 0.4f;
}

// Punchy bass: sub oscillator, fast attack, mid-high filter
void setSynthBass(RuinaePresetState& s) {
    s.oscA.waveform = 1;  // Sawtooth
    s.oscA.level = 0.8f;
    s.oscA.tuneSemitones = -12.0f;  // One octave down
    s.oscB.type = 0;
    s.oscB.waveform = 3;  // Square
    s.oscB.level = 0.4f;
    s.oscB.tuneSemitones = -12.0f;
    s.mixer.position = 0.6f;
    s.filter.type = 0;  // Ladder LP
    s.filter.cutoffHz = 5000.0f;
    s.filter.resonance = 0.15f;
    s.ampEnv.attackMs = 2.0f;
    s.ampEnv.decayMs = 200.0f;
    s.ampEnv.sustain = 0.6f;
    s.ampEnv.releaseMs = 150.0f;
}

// Bright lead: saw + slight detune, high cutoff
void setSynthLead(RuinaePresetState& s) {
    s.oscA.waveform = 1;  // Sawtooth
    s.oscA.level = 0.8f;
    s.oscB.type = 0;
    s.oscB.waveform = 1;  // Sawtooth
    s.oscB.fineCents = 10.0f;  // Detune for thickness
    s.oscB.level = 0.6f;
    s.mixer.position = 0.5f;
    s.filter.type = 0;  // Ladder LP
    s.filter.cutoffHz = 8000.0f;
    s.filter.resonance = 0.2f;
    s.ampEnv.attackMs = 5.0f;
    s.ampEnv.decayMs = 300.0f;
    s.ampEnv.sustain = 0.7f;
    s.ampEnv.releaseMs = 200.0f;
}

// Squelchy acid: saw, filter with env amount, fast decay, resonance up
void setSynthAcid(RuinaePresetState& s) {
    s.oscA.waveform = 1;  // Sawtooth
    s.oscA.level = 0.9f;
    s.oscB.level = 0.0f;  // Single osc acid sound
    s.mixer.position = 0.0f;  // Osc A only
    s.filter.type = 0;  // Ladder LP
    s.filter.cutoffHz = 800.0f;  // Low cutoff for squelch
    s.filter.resonance = 0.7f;  // High resonance
    s.filter.envAmount = 4000.0f;  // Strong filter envelope
    s.ampEnv.attackMs = 1.0f;
    s.ampEnv.decayMs = 150.0f;
    s.ampEnv.sustain = 0.5f;
    s.ampEnv.releaseMs = 100.0f;
    s.filterEnv.attackMs = 1.0f;
    s.filterEnv.decayMs = 200.0f;
    s.filterEnv.sustain = 0.1f;
    s.filterEnv.releaseMs = 150.0f;
}

// ==============================================================================
// Factory Preset Definitions (T024-T037, T038)
// ==============================================================================

// Arp mode constants
static constexpr int32_t kModeUp       = 0;
static constexpr int32_t kModeDown     = 1;
static constexpr int32_t kModeUpDown   = 2;
static constexpr int32_t kModeRandom   = 6;
static constexpr int32_t kModeAsPlayed = 8;

// Note value index constants
static constexpr int32_t kNote1_16  = 7;
static constexpr int32_t kNote1_8   = 10;
static constexpr int32_t kNote1_8T  = 9;

// Condition constants
static constexpr int32_t kCondAlways = 0;
static constexpr int32_t kCondProb10 = 1;
static constexpr int32_t kCondProb25 = 2;
static constexpr int32_t kCondProb50 = 3;
static constexpr int32_t kCondProb75 = 4;
static constexpr int32_t kCondProb90 = 5;
static constexpr int32_t kCondFill   = 16;

std::vector<PresetDef> createAllPresets() {
    std::vector<PresetDef> presets;

    // ==================== Classic Category (3 presets) ====================

    // T024: "Basic Up 1/16"
    {
        PresetDef p;
        p.name = "Basic Up 1/16";
        p.category = "Arp Classic";
        setSynthLead(p.state);
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUp);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 80.0f);
        setArpSwing(p.state, 0.0f);
        p.state.arp.octaveRange = 2;
        float vel8[] = {0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f};
        setVelocityLane(p.state, 8, vel8);
        float gate8[] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
        setGateLane(p.state, 8, gate8);
        presets.push_back(std::move(p));
    }

    // T025: "Down 1/8"
    {
        PresetDef p;
        p.name = "Down 1/8";
        p.category = "Arp Classic";
        setSynthPad(p.state);
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeDown);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_8);
        setArpGateLength(p.state, 90.0f);
        setArpSwing(p.state, 0.0f);
        p.state.arp.octaveRange = 2;
        float vel8[] = {0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f};
        setVelocityLane(p.state, 8, vel8);
        float gate8[] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
        setGateLane(p.state, 8, gate8);
        presets.push_back(std::move(p));
    }

    // T026: "UpDown 1/8T"
    {
        PresetDef p;
        p.name = "UpDown 1/8T";
        p.category = "Arp Classic";
        setSynthLead(p.state);
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUpDown);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_8T);
        setArpGateLength(p.state, 75.0f);
        setArpSwing(p.state, 0.0f);
        p.state.arp.octaveRange = 2;
        // Accent pattern: alternating 0.6/1.0
        float velAccent8[] = {0.6f, 1.0f, 0.6f, 1.0f, 0.6f, 1.0f, 0.6f, 1.0f};
        setVelocityLane(p.state, 8, velAccent8);
        float gate8[] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
        setGateLane(p.state, 8, gate8);
        presets.push_back(std::move(p));
    }

    // ==================== Acid Category (2 presets) ====================

    // T027: "Acid Line 303"
    {
        PresetDef p;
        p.name = "Acid Line 303";
        p.category = "Arp Acid";
        setSynthAcid(p.state);
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUp);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 60.0f);
        float vel8[] = {0.75f, 0.75f, 0.75f, 0.75f,
                        0.75f, 0.75f, 0.75f, 0.75f};
        setVelocityLane(p.state, 8, vel8);
        int32_t pitch8[] = {0, 0, 3, 0, 5, 0, 3, 7};
        setPitchLane(p.state, 8, pitch8);
        // Modifier lane: slide on steps 3,7; accent on steps 5; both on step 7
        // (1-indexed in task, 0-indexed here)
        int32_t mod8[] = {
            kStepActive,                            // step 1
            kStepActive,                            // step 2
            kStepActive | kStepSlide,               // step 3 (slide)
            kStepActive,                            // step 4
            kStepActive | kStepAccent,              // step 5 (accent)
            kStepActive,                            // step 6
            kStepActive | kStepSlide | kStepAccent, // step 7 (slide+accent)
            kStepActive                             // step 8
        };
        setModifierLane(p.state, 8, mod8, 100, 50.0f);
        presets.push_back(std::move(p));
    }

    // T028: "Acid Stab"
    {
        PresetDef p;
        p.name = "Acid Stab";
        p.category = "Arp Acid";
        setSynthAcid(p.state);
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeAsPlayed);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 40.0f);
        float vel8[] = {0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f};
        setVelocityLane(p.state, 8, vel8);
        int32_t pitch8[] = {0, 0, 0, 0, 0, 0, 0, 0};
        setPitchLane(p.state, 8, pitch8);
        // All steps: active + accent
        int32_t mod8[] = {
            kStepActive | kStepAccent, kStepActive | kStepAccent,
            kStepActive | kStepAccent, kStepActive | kStepAccent,
            kStepActive | kStepAccent, kStepActive | kStepAccent,
            kStepActive | kStepAccent, kStepActive | kStepAccent
        };
        setModifierLane(p.state, 8, mod8, 110, 50.0f);
        presets.push_back(std::move(p));
    }

    // ==================== Euclidean World Category (3 presets) ====================

    // T029: "Tresillo E(3,8)"
    {
        PresetDef p;
        p.name = "Tresillo E(3,8)";
        p.category = "Arp Euclidean";
        setSynthPad(p.state);
        setArpEnabled(p.state, true);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 80.0f);
        p.state.arp.octaveRange = 1;
        setEuclidean(p.state, true, 3, 8, 0);
        float vel8[] = {0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f};
        setVelocityLane(p.state, 8, vel8);
        presets.push_back(std::move(p));
    }

    // T030: "Bossa E(5,16)"
    {
        PresetDef p;
        p.name = "Bossa E(5,16)";
        p.category = "Arp Euclidean";
        setSynthPad(p.state);
        setArpEnabled(p.state, true);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 75.0f);
        p.state.arp.octaveRange = 1;
        setEuclidean(p.state, true, 5, 16, 0);
        float vel16[16];
        for (int i = 0; i < 16; ++i) vel16[i] = 0.75f;
        setVelocityLane(p.state, 16, vel16);
        presets.push_back(std::move(p));
    }

    // T031: "Samba E(7,16)"
    {
        PresetDef p;
        p.name = "Samba E(7,16)";
        p.category = "Arp Euclidean";
        setSynthLead(p.state);
        setArpEnabled(p.state, true);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 70.0f);
        p.state.arp.octaveRange = 2;
        setEuclidean(p.state, true, 7, 16, 0);
        float vel16[16];
        for (int i = 0; i < 16; ++i) vel16[i] = 0.8f;
        setVelocityLane(p.state, 16, vel16);
        presets.push_back(std::move(p));
    }

    // ==================== Polymetric Category (2 presets) ====================

    // T032: "3x5x7 Evolving"
    {
        PresetDef p;
        p.name = "3x5x7 Evolving";
        p.category = "Arp Polymetric";
        setSynthPad(p.state);
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUp);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        // Velocity lane length=3
        float vel3[] = {0.5f, 0.8f, 1.0f};
        setVelocityLane(p.state, 3, vel3);
        // Gate lane length=5
        float gate5[] = {0.8f, 1.2f, 0.6f, 1.0f, 0.4f};
        setGateLane(p.state, 5, gate5);
        // Pitch lane length=7
        int32_t pitch7[] = {0, 3, 0, 7, 0, -2, 5};
        setPitchLane(p.state, 7, pitch7);
        // Ratchet lane length=4
        int32_t ratch4[] = {1, 1, 2, 1};
        setRatchetLane(p.state, 4, ratch4);
        presets.push_back(std::move(p));
    }

    // T033: "4x5 Shifting"
    {
        PresetDef p;
        p.name = "4x5 Shifting";
        p.category = "Arp Polymetric";
        setSynthBass(p.state);
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeAsPlayed);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        // Ratchet lane length=4
        int32_t ratch4[] = {1, 2, 1, 2};
        setRatchetLane(p.state, 4, ratch4);
        // Velocity lane length=5
        float vel5[] = {0.6f, 1.0f, 0.7f, 0.9f, 0.5f};
        setVelocityLane(p.state, 5, vel5);
        // Gate lane length=6
        float gate6[] = {0.8f, 1.1f, 0.7f, 1.0f, 0.6f, 0.9f};
        setGateLane(p.state, 6, gate6);
        presets.push_back(std::move(p));
    }

    // ==================== Generative Category (2 presets) ====================

    // T034: "Spice Evolver"
    {
        PresetDef p;
        p.name = "Spice Evolver";
        p.category = "Arp Generative";
        setSynthLead(p.state);
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUp);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        p.state.arp.octaveRange = 2;
        p.state.arp.spice = 0.7f;
        p.state.arp.humanize = 0.3f;
        // Condition lane length=8 with mixed conditions
        int32_t cond8[] = {
            kCondAlways, kCondProb50, kCondAlways, kCondProb75,
            kCondAlways, kCondProb25, kCondProb50, kCondAlways
        };
        setConditionLane(p.state, 8, cond8, 0);
        // Varied velocity lane
        float vel8[] = {0.7f, 0.9f, 0.5f, 1.0f, 0.6f, 0.8f, 0.4f, 0.95f};
        setVelocityLane(p.state, 8, vel8);
        presets.push_back(std::move(p));
    }

    // T035: "Chaos Garden"
    {
        PresetDef p;
        p.name = "Chaos Garden";
        p.category = "Arp Generative";
        setSynthPad(p.state);
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeRandom);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        p.state.arp.spice = 0.9f;
        p.state.arp.humanize = 0.5f;
        // Condition lane length=16 with cycling probabilities
        int32_t cond16[] = {
            kCondProb10, kCondProb25, kCondProb50, kCondProb75,
            kCondProb90, kCondProb10, kCondProb25, kCondProb50,
            kCondProb75, kCondProb90, kCondProb10, kCondProb25,
            kCondProb50, kCondProb75, kCondProb90, kCondProb10
        };
        setConditionLane(p.state, 16, cond16, 0);
        // Velocity lane 16 steps uniform
        float vel16[16];
        for (int i = 0; i < 16; ++i) vel16[i] = 0.8f;
        setVelocityLane(p.state, 16, vel16);
        // Pitch lane length=8
        int32_t pitch8[] = {0, 2, 4, 7, 9, 12, -5, 0};
        setPitchLane(p.state, 8, pitch8);
        presets.push_back(std::move(p));
    }

    // ==================== Performance Category (2 presets) ====================

    // T036: "Fill Cascade"
    {
        PresetDef p;
        p.name = "Fill Cascade";
        p.category = "Arp Performance";
        setSynthLead(p.state);
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUp);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        p.state.arp.octaveRange = 2;
        // Condition lane: Fill on steps 5-8 and 13-16 (0-indexed: 4-7, 12-15)
        int32_t cond16[] = {
            kCondAlways, kCondAlways, kCondAlways, kCondAlways,
            kCondFill,   kCondFill,   kCondFill,   kCondFill,
            kCondAlways, kCondAlways, kCondAlways, kCondAlways,
            kCondFill,   kCondFill,   kCondFill,   kCondFill
        };
        setConditionLane(p.state, 16, cond16, 0);
        // Velocity lane 16 steps uniform
        float vel16[16];
        for (int i = 0; i < 16; ++i) vel16[i] = 0.8f;
        setVelocityLane(p.state, 16, vel16);
        // Gate lane 16 steps uniform
        float gate16[16];
        for (int i = 0; i < 16; ++i) gate16[i] = 0.9f;
        setGateLane(p.state, 16, gate16);
        presets.push_back(std::move(p));
    }

    // T037: "Probability Waves"
    {
        PresetDef p;
        p.name = "Probability Waves";
        p.category = "Arp Performance";
        setSynthBass(p.state);
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUpDown);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        // Condition lane: Prob75 on even steps (1-indexed), Prob25 on odd
        // Even steps (2,4,6,8,...) are 0-indexed 1,3,5,7,...
        int32_t cond16[] = {
            kCondProb25, kCondProb75, kCondProb25, kCondProb75,
            kCondProb25, kCondProb75, kCondProb25, kCondProb75,
            kCondProb25, kCondProb75, kCondProb25, kCondProb75,
            kCondProb25, kCondProb75, kCondProb25, kCondProb75
        };
        setConditionLane(p.state, 16, cond16, 0);
        // Velocity lane 8 steps alternating
        float vel8[] = {0.6f, 1.0f, 0.6f, 1.0f, 0.6f, 1.0f, 0.6f, 1.0f};
        setVelocityLane(p.state, 8, vel8);
        // Modifier lane: accent on even steps (0-indexed 1,3,5,7)
        int32_t mod8[] = {
            kStepActive,                // step 1 (odd)
            kStepActive | kStepAccent,  // step 2 (even)
            kStepActive,                // step 3 (odd)
            kStepActive | kStepAccent,  // step 4 (even)
            kStepActive,                // step 5 (odd)
            kStepActive | kStepAccent,  // step 6 (even)
            kStepActive,                // step 7 (odd)
            kStepActive | kStepAccent   // step 8 (even)
        };
        setModifierLane(p.state, 8, mod8, 30, 60.0f);
        // Ratchet lane length=8 (values 1-4: 1,2,1,2,1,2,1,2)
        int32_t ratch8[] = {1, 2, 1, 2, 1, 2, 1, 2};
        setRatchetLane(p.state, 8, ratch8);
        presets.push_back(std::move(p));
    }

    return presets;
}

// ==============================================================================
// Main
// ==============================================================================

int main(int argc, char* argv[]) {
    std::filesystem::path outputDir = "plugins/ruinae/resources/presets";

    if (argc > 1) {
        outputDir = argv[1];
    }

    std::filesystem::create_directories(outputDir);

    auto presets = createAllPresets();
    int successCount = 0;

    std::cout << "Generating " << presets.size()
              << " Ruinae factory arp presets..." << std::endl;

    for (const auto& preset : presets) {
        auto stateData = preset.state.serialize();

        auto categoryDir = outputDir / preset.category;
        std::filesystem::create_directories(categoryDir);

        // Create filename: spaces -> underscores, keep alphanumeric and hyphens
        std::string filename;
        for (char c : preset.name) {
            if (c == ' ')
                filename += '_';
            else if (c == '/')
                filename += '-';
            else if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' ||
                     c == '(' || c == ')')
                filename += c;
        }
        filename += ".vstpreset";

        auto path = categoryDir / filename;

        if (writeVstPreset(path, stateData)) {
            std::cout << "  Created: " << preset.category << "/"
                      << filename << " (" << stateData.size() << " bytes)"
                      << std::endl;
            successCount++;
        }
    }

    std::cout << "\nGenerated " << successCount << " of " << presets.size()
              << " presets." << std::endl;
    std::cout << "Output directory: " << std::filesystem::absolute(outputDir)
              << std::endl;

    return (successCount == static_cast<int>(presets.size())) ? 0 : 1;
}
