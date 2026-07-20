// ==============================================================================
// Factory Preset Generator for Ruinae
// ==============================================================================
// Generates .vstpreset files matching the Processor::getState() binary format.
// Run this tool once during development to create factory presets.
//
// Reference: plugins/ruinae/src/processor/processor.cpp getState() (lines 488-559)
// Reference: tools/disrumpo_preset_generator.cpp (established pattern)
// ==============================================================================

#include "ruinae_preset_format.h"

#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>

using namespace RuinaeFormat;

// ==============================================================================
// Constants (preset-generator-specific)
// ==============================================================================

// kProcessorUID(0xA3B7C1D5, 0x2E4F6A8B, 0x9C0D1E2F, 0x3A4B5C6D)
const char kClassIdAscii[33] = "A3B7C1D52E4F6A8B9C0D1E2F3A4B5C6D";

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
    s.arp.operatingMode = enabled ? 1 : 0;
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

// Chord type constants
static constexpr int32_t kChordNone  = 0;
static constexpr int32_t kChordDyad  = 1;
static constexpr int32_t kChordTriad = 2;
static constexpr int32_t kChord7th   = 3;
static constexpr int32_t kChord9th   = 4;

// Inversion constants
static constexpr int32_t kInvRoot = 0;
static constexpr int32_t kInv1st  = 1;
static constexpr int32_t kInv2nd  = 2;
static constexpr int32_t kInv3rd  = 3;

void setChordLane(RuinaePresetState& s, int32_t length,
                  const int32_t* steps) {
    s.arp.chordLaneLength = std::clamp(length, 1, 32);
    for (int i = 0; i < length && i < 32; ++i)
        s.arp.chordLaneSteps[i] = steps[i];
}

void setInversionLane(RuinaePresetState& s, int32_t length,
                      const int32_t* steps) {
    s.arp.inversionLaneLength = std::clamp(length, 1, 32);
    for (int i = 0; i < length && i < 32; ++i)
        s.arp.inversionLaneSteps[i] = steps[i];
}

void setVoicingMode(RuinaePresetState& s, int32_t mode) {
    s.arp.voicingMode = mode;
}

void setEuclidean(RuinaePresetState& s, bool enabled, int32_t hits,
                  int32_t steps, int32_t rotation) {
    s.arp.euclideanEnabled = enabled ? 1 : 0;
    s.arp.euclideanHits = hits;
    s.arp.euclideanSteps = steps;
    s.arp.euclideanRotation = rotation;
}

// ==============================================================================
// Mod Matrix & Voice Route Helpers
// ==============================================================================

// Global mod sources (indices into mod matrix source dropdown)
static constexpr int kSrcNone         = 0;
static constexpr int kSrcLFO1         = 1;
static constexpr int kSrcLFO2         = 2;
static constexpr int kSrcEnvFollower  = 3;
static constexpr int kSrcRandom       = 4;
static constexpr int kSrcMacro1       = 5;
static constexpr int kSrcMacro2       = 6;
static constexpr int kSrcMacro3       = 7;
static constexpr int kSrcMacro4       = 8;
static constexpr int kSrcChaos        = 9;
static constexpr int kSrcRungler      = 10;
static constexpr int kSrcSampleHold   = 11;
static constexpr int kSrcPitchFollow  = 12;
static constexpr int kSrcTransient    = 13;
static constexpr int kSrcArpPitch     = 14;

// Global mod destinations
static constexpr int kDstGlobalFltCut   = 0;
static constexpr int kDstGlobalFltRes   = 1;
static constexpr int kDstMasterVol      = 2;
static constexpr int kDstEffectMix      = 3;
static constexpr int kDstAllFltCut      = 4;
static constexpr int kDstAllMorphPos    = 5;
static constexpr int kDstAllGateRate    = 6;
static constexpr int kDstAllSpecTilt    = 7;
static constexpr int kDstAllResonance   = 8;
static constexpr int kDstAllFltEnvAmt   = 9;
static constexpr int kDstArpRate        = 10;
static constexpr int kDstArpGateLen     = 11;
static constexpr int kDstArpOctRange    = 12;
static constexpr int kDstArpSwing       = 13;
static constexpr int kDstArpSpice       = 14;

// Per-voice mod sources
static constexpr int kVSrcEnv1       = 0;  // Amp Env
static constexpr int kVSrcEnv2       = 1;  // Filter Env
static constexpr int kVSrcEnv3       = 2;  // Mod Env
static constexpr int kVSrcVoiceLFO   = 3;
static constexpr int kVSrcGate       = 4;  // TranceGate output
static constexpr int kVSrcVelocity   = 5;
static constexpr int kVSrcKeyTrack   = 6;
static constexpr int kVSrcAftertouch = 7;

// Per-voice mod destinations
static constexpr int kVDstFltCut     = 0;
static constexpr int kVDstFltRes     = 1;
static constexpr int kVDstMorphPos   = 2;
static constexpr int kVDstDistDrive  = 3;
static constexpr int kVDstGateDepth  = 4;
static constexpr int kVDstOscAPitch  = 5;
static constexpr int kVDstOscBPitch  = 6;
static constexpr int kVDstSpecTilt   = 7;

// Mod matrix curve types
static constexpr int kCurveLinear = 0;
static constexpr int kCurveExp    = 1;
static constexpr int kCurveSCurve = 2;
static constexpr int kCurveStepped = 3;

void setModSlot(RuinaePresetState& s, int slot, int source, int dest,
                float amount, int curve = 0, float smoothMs = 0.0f) {
    s.modMatrix.slots[slot].source = source;
    s.modMatrix.slots[slot].dest = dest;
    s.modMatrix.slots[slot].amount = amount;
    s.modMatrix.slots[slot].curve = curve;
    s.modMatrix.slots[slot].smoothMs = smoothMs;
}

void setVoiceRoute(RuinaePresetState& s, int slot, int source, int dest,
                   float amount) {
    s.voiceRoutes[slot].source = static_cast<int8_t>(source);
    s.voiceRoutes[slot].destination = static_cast<int8_t>(dest);
    s.voiceRoutes[slot].amount = amount;
    s.voiceRoutes[slot].active = 1;
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
    s.filter.resonance = 2.96f;
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
    s.filter.resonance = 2.40f;
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
    s.filter.resonance = 2.96f;
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
    s.filter.resonance = 8.61f;  // High resonance
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
static constexpr int32_t kNote1_8D  = 11;
static constexpr int32_t kNote1_4   = 13;
static constexpr int32_t kNote1_4T  = 12;
static constexpr int32_t kNote1_16D = 8;

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
        p.state.filter.resonance = 6.35f;
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

    // ==================== Acid Category (2 presets) ====================

    // T027: "Acid Line 303" - analog ladder acid (single saw, mono glide)
    {
        PresetDef p;
        p.name = "Acid Line 303";
        p.category = "Arp Acid";
        auto& s = p.state;

        // --- Voice: one raw saw, Osc B silent (classic 303 topology) ---
        s.oscA.type = 0;            // PolyBLEP
        s.oscA.waveform = 1;       // Sawtooth
        s.oscA.level = 0.9f;
        s.oscB.level = 0.0f;       // single-oscillator acid
        s.mixer.position = 0.0f;   // Osc A only

        // --- Ladder LP: the analog half of the pair (NOT SVF) ---
        s.filter.type = 4;         // Ladder LP (type 4, not the template's SVF)
        s.filter.cutoffHz = 600.0f;   // low base so the env-sweep is the timbre
        s.filter.resonance = 9.18f;   // squelchy but pre-self-osc
        s.filter.ladderSlope = 4;     // 24 dB/oct - fat 303 slope
        s.filter.ladderDrive = 6.0f;  // transistor grit that thickens the saw
        s.filter.keyTrack = 0.3f;     // higher notes sit a touch brighter
        // FIX the setSynthAcid bug: envAmount is PLAIN SEMITONES (-48..+48),
        // not Hz. +32 st = a strong, musically-scaled sweep (was 4000 = garbage).
        s.filter.envAmount = 32.0f;

        // --- Amp env: plucky, some sustain so slides ring between steps ---
        s.ampEnv.attackMs = 1.0f;
        s.ampEnv.decayMs = 180.0f;
        s.ampEnv.sustain = 0.4f;
        s.ampEnv.releaseMs = 90.0f;

        // --- Filter env: fast snap = the 'wow' on every note ---
        s.filterEnv.attackMs = 1.0f;
        s.filterEnv.decayMs = 140.0f;
        s.filterEnv.sustain = 0.05f;
        s.filterEnv.releaseMs = 80.0f;
        s.filterEnv.decayCurve = 0.5f; // exp-ish snap, not linear

        // --- Mono + legato glide: slides in the modifier lane actually bend ---
        s.global.voiceMode = 1;    // Mono
        s.global.polyphony = 1;
        s.monoMode.legato = 1;
        s.monoMode.portamentoTimeMs = 60.0f;
        s.monoMode.portaMode = 1;  // glide only on legato/slide steps

        // --- Arp: 1/16 up-run with a moving pitch line ---
        setArpEnabled(s, true);
        setArpMode(s, kModeUp);
        setTempoSync(s, true);
        setArpRate(s, kNote1_16);
        setArpGateLength(s, 60.0f);
        float vel8[] = {0.75f, 0.75f, 0.75f, 0.75f,
                        0.75f, 0.75f, 0.75f, 0.75f};
        setVelocityLane(s, 8, vel8);
        int32_t pitch8[] = {0, 0, 3, 0, 5, 0, 3, 7}; // riff that opens on the highs
        setPitchLane(s, 8, pitch8);
        // Slides on 3 & 7 (glide), accent on 5, slide+accent on 7
        int32_t mod8[] = {
            kStepActive,
            kStepActive,
            kStepActive | kStepSlide,               // glide up to the 3rd
            kStepActive,
            kStepActive | kStepAccent,              // punch the 5th
            kStepActive,
            kStepActive | kStepSlide | kStepAccent, // slide+accent into the octave
            kStepActive
        };
        setModifierLane(s, 8, mod8, 100, 60.0f);   // accentVel=100, slideTime=60ms

        // --- Mod identity: arp pitch opens the ladder (higher note = brighter) ---
        setModSlot(s, 0, kSrcArpPitch, kDstAllFltCut, 0.5f, kCurveExp);
        // Playable squelch: velocity AND aftertouch push resonance
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltRes, 0.4f);
        setVoiceRoute(s, 1, kVSrcAftertouch, kVDstFltRes, 0.3f);
        presets.push_back(std::move(p));
    }

    // T028: "Digital Acid Stab" - phase-distortion acid, the digital counterpart
    {
        PresetDef p;
        p.name = "Digital Acid Stab";
        p.category = "Arp Acid";
        auto& s = p.state;

        // --- Voice: Casio-CZ phase distortion (the DIGITAL half of the pair) ---
        s.oscA.type = 2;           // PhaseDistortion engine
        s.oscA.pdWaveform = 5;     // ResSaw - resonant formant character
        s.oscA.pdDistortion = 0.7f;// strong DCW = metallic, hollow bite
        s.oscA.level = 0.9f;
        s.oscB.level = 0.0f;       // single osc
        s.mixer.position = 0.0f;   // Osc A only

        // --- SVF LP (distinct from Acid Line's ladder) with post-filter drive ---
        s.filter.type = 0;         // SVF LP
        s.filter.cutoffHz = 900.0f;
        s.filter.resonance = 3.0f; // SVF Q: resonant edge without whistling
        s.filter.svfSlope = 1;     // 24 dB cascaded
        s.filter.svfDrive = 4.0f;  // grit that hardens the stab
        // FIX the 4000 bug here too: +24 semitones of real sweep on the SVF.
        s.filter.envAmount = 24.0f;

        // --- Amp env: tight percussive chop (fully decays each step) ---
        s.ampEnv.attackMs = 1.0f;
        s.ampEnv.decayMs = 90.0f;
        s.ampEnv.sustain = 0.0f;
        s.ampEnv.releaseMs = 50.0f;

        // --- Filter env: sharp attack transient on each stab ---
        s.filterEnv.attackMs = 1.0f;
        s.filterEnv.decayMs = 110.0f;
        s.filterEnv.sustain = 0.0f;
        s.filterEnv.releaseMs = 60.0f;
        s.filterEnv.decayCurve = 0.4f;

        // --- Arp: static single-pitch 1/16 stab, tight gate, 10% swing ---
        setArpEnabled(s, true);
        setArpMode(s, kModeAsPlayed);
        setTempoSync(s, true);
        setArpRate(s, kNote1_16);
        setArpGateLength(s, 40.0f);
        setArpSwing(s, 10.0f);     // subtle groove push (mustCover: arp swing)
        float vel8[] = {0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f};
        setVelocityLane(s, 8, vel8);
        int32_t pitch8[] = {0, 0, 0, 0, 0, 0, 0, 0}; // no melody - identity is timbre
        setPitchLane(s, 8, pitch8);
        // Every step accented (hard chop); slideTime unused but set sanely
        int32_t mod8[] = {
            kStepActive | kStepAccent, kStepActive | kStepAccent,
            kStepActive | kStepAccent, kStepActive | kStepAccent,
            kStepActive | kStepAccent, kStepActive | kStepAccent,
            kStepActive | kStepAccent, kStepActive | kStepAccent
        };
        setModifierLane(s, 8, mod8, 110, 40.0f);
        // Elektron trig-conditions carve the rhythm instead of a pitch line:
        // Ratio_1_2 (6) = fire on 1st of every 2 loops; Ratio_3_4 (13) = 3rd of 4.
        int32_t cond8[] = {kCondAlways, 6, kCondAlways, 13,
                           kCondAlways, 6, kCondAlways, 13};
        setConditionLane(s, 8, cond8, 0);

        // --- Mod identity (must NOT repeat Acid Line's ArpPitch->cutoff): ---
        // tempo-synced S&H steps the filter for a digital, glitchy stutter.
        s.sampleHold.sync = 1;         // lock steps to tempo
        s.sampleHold.noteValue = kNote1_8;
        s.sampleHold.slewMs = 0.0f;    // hard steps = digital character
        setModSlot(s, 0, kSrcSampleHold, kDstAllFltCut, 0.3f, kCurveStepped);
        // Velocity opens the SVF for accent dynamics
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.35f);

        // --- FX signature: wet synced ping-pong (analog sibling stays dry) ---
        s.delayEnabled = 1;
        s.delay.type = 2;          // PingPong
        s.delay.sync = 1;
        s.delay.noteValue = kNote1_8;
        s.delay.feedback = 0.35f;
        s.delay.mix = 0.3f;
        s.delay.pingPongCrossFeed = 0.8f;
        s.delay.pingPongWidth = 140.0f;
        presets.push_back(std::move(p));
    }

    // ==================== Euclidean World Category (3 presets) ====================

    // T029: "Obsidian Tresillo"
    {
        PresetDef p;
        p.name = "Obsidian Tresillo";
        p.category = "Arp Euclidean";
        RuinaePresetState& s = p.state;

        // --- Voice: Wavetable pluck (gives the never-used Wavetable engine a home) ---
        s.oscA.type = 1;            // Wavetable
        s.oscA.waveform = 1;        // Saw table
        s.oscA.phaseMod = 0.35f;    // wavetable PM -> metallic FM sheen; unmistakably not a plain saw
        s.oscA.level = 0.85f;
        s.oscB.type = 1;            // Wavetable
        s.oscB.waveform = 2;        // Square table
        s.oscB.tuneSemitones = 12.0f;   // octave-up bite on the attack transient
        s.oscB.fineCents = 4.0f;    // hair of detune so the two tables shimmer
        s.oscB.level = 0.4f;
        s.mixer.mode = 0;           // Crossfade
        s.mixer.position = 0.4f;    // favor OscA body

        // --- Filter: 24 dB SVF LP with a REAL (bug-free) filter-env pluck sweep ---
        s.filter.type = 0;          // SVF LP
        s.filter.svfSlope = 1;      // 24 dB/oct for a tight edge
        s.filter.cutoffHz = 1100.0f;
        s.filter.resonance = 3.5f;  // singing, not whistling
        s.filter.envAmount = 32.0f; // +32 semitones sweep (FIXED: plain semitones, not the old 4000 Hz bug)
        s.filter.keyTrack = 0.5f;   // keep plucks even across the keyboard

        // --- Envelopes: short percussive body, zero wash ---
        s.ampEnv.attackMs = 2.0f;
        s.ampEnv.decayMs = 170.0f;
        s.ampEnv.sustain = 0.12f;
        s.ampEnv.releaseMs = 120.0f;
        s.ampEnv.decayCurve = 0.4f; // exp-ish decay = snappier pluck tail
        s.filterEnv.attackMs = 1.0f;
        s.filterEnv.decayMs = 150.0f;
        s.filterEnv.sustain = 0.0f; // sweep fully closes -> percussive "tick"
        s.filterEnv.releaseMs = 110.0f;

        // --- Mod identity (unique to this preset): S&H steps the A/B wavetable blend
        //     every 16th with a Stepped curve, so each tresillo hit picks a new table mix. ---
        s.sampleHold.sync = 1;
        s.sampleHold.noteValue = kNote1_16;
        setModSlot(s, 0, kSrcSampleHold, kDstAllMorphPos, 0.35f, kCurveStepped);
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.4f); // velocity opens the tick brighter

        // --- Scale color: E Phrygian (dark, Spanish half-step above the root) ---
        s.arp.scaleType = 6;        // Phrygian (DSP enum order)
        s.arp.rootNote = 4;         // E

        // --- Arp: E(3,8) tresillo, tight & dry ---
        setArpEnabled(s, true);
        setArpMode(s, kModeUp);
        setTempoSync(s, true);
        setArpRate(s, kNote1_16);
        setArpGateLength(s, 55.0f);  // tight staccato so the tresillo speaks
        s.arp.octaveRange = 1;
        setEuclidean(s, true, 3, 8, 0);
        float vel8[] = {1.0f, 0.7f, 0.72f, 0.9f, 0.7f, 0.72f, 0.85f, 0.7f}; // accent the 3 struck positions
        setVelocityLane(s, 8, vel8);

        // Dry percussive member: NO reverb - the rhythm carries it.
        presets.push_back(std::move(p));
    }

    // T030: "Marimba Bossa"
    {
        PresetDef p;
        p.name = "Marimba Bossa";
        p.category = "Arp Euclidean";
        RuinaePresetState& s = p.state;

        // --- Voice: Phase-Distortion "DoubleSine" = FM-marimba mallet tone ---
        s.oscA.type = 2;            // Phase Distortion (Casio-CZ style)
        s.oscA.pdWaveform = 3;      // DoubleSine (the two-lobe FM-ish shape)
        s.oscA.pdDistortion = 0.55f;// DCW depth -> bell/marimba overtone bloom on the attack
        s.oscA.level = 0.85f;
        s.oscB.type = 0;            // PolyBLEP sine sub for weight + a pitch anchor
        s.oscB.waveform = 0;        // Sine
        s.oscB.tuneSemitones = -12.0f;
        s.oscB.level = 0.35f;
        s.mixer.position = 0.35f;   // favor the PD mallet

        // --- Filter: gentle SVF LP with a short env pop for the mallet strike ---
        s.filter.type = 0;          // SVF LP
        s.filter.svfSlope = 1;      // 24 dB
        s.filter.cutoffHz = 3200.0f;
        s.filter.resonance = 1.2f;
        s.filter.envAmount = 20.0f; // +20 st strike sweep (FIXED semitones)

        // --- Envelopes: mallet pluck - fast attack, low sustain, medium tail ---
        s.ampEnv.attackMs = 3.0f;
        s.ampEnv.decayMs = 260.0f;
        s.ampEnv.sustain = 0.1f;
        s.ampEnv.releaseMs = 220.0f;
        s.ampEnv.decayCurve = 0.35f; // wooden mallet decay
        s.filterEnv.attackMs = 1.0f;
        s.filterEnv.decayMs = 200.0f;
        s.filterEnv.sustain = 0.05f;
        s.filterEnv.releaseMs = 180.0f;

        // --- Mod identity (unique): a slow FREE LFO gently morphs the mallet timbre
        //     across the bar via an S-curve, so the pattern breathes without any metric pulse. ---
        s.lfo1.rateHz = 0.28f;      // very slow
        s.lfo1.sync = 0;            // free-running (default is synced) -> non-metric drift
        s.lfo1.shape = 0;           // Sine
        setModSlot(s, 0, kSrcLFO1, kDstAllMorphPos, 0.25f, kCurveSCurve, 20.0f);
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.35f); // dynamics under the fingers

        // --- Scale color: D Dorian (warm minor with the bright major 6th) ---
        s.arp.scaleType = 4;        // Dorian
        s.arp.rootNote = 2;         // D

        // --- Arp: E(5,16) rotated + swung, medium detache gate ---
        setArpEnabled(s, true);
        setArpMode(s, kModeUp);
        setTempoSync(s, true);
        setArpRate(s, kNote1_16);
        setArpGateLength(s, 68.0f); // medium detache
        setArpSwing(s, 12.0f);      // 12% swing = bossa lilt
        s.arp.octaveRange = 1;
        setEuclidean(s, true, 5, 16, 2); // rotation=2 shifts the syncopation off the downbeat

        // --- Light tape delay for warmth (identity wrapper, NOT a big diffuse hall) ---
        s.delayEnabled = 1;
        s.delay.type = 1;           // Tape
        s.delay.sync = 1;
        s.delay.noteValue = kNote1_8;
        s.delay.feedback = 0.25f;
        s.delay.mix = 0.2f;
        s.delay.tapeSaturation = 0.5f;
        s.delay.tapeInertiaMs = 250.0f;

        presets.push_back(std::move(p));
    }

    // T031: "Granular Samba"
    {
        PresetDef p;
        p.name = "Granular Samba";
        p.category = "Arp Euclidean";
        RuinaePresetState& s = p.state;

        // --- Voice: dense Particle swarm (the granular engine's showcase) ---
        s.oscA.type = 6;            // Particle
        s.oscA.particleScatter = 3.0f;   // moderate freq scatter
        s.oscA.particleDensity = 24.0f;  // dense cloud
        s.oscA.particleLifetime = 60.0f; // short grains -> percussive fizz
        s.oscA.particleSpawnMode = 1;    // Random spawn -> lively, non-mechanical
        s.oscA.particleEnvType = 0;      // Hann (smooth grain windows)
        s.oscA.particleDrift = 0.15f;    // slight pitch wander for organic motion
        s.oscA.level = 0.85f;
        s.oscB.type = 0;            // triangle anchors the pitch under the cloud
        s.oscB.waveform = 4;        // Triangle
        s.oscB.level = 0.3f;
        s.mixer.position = 0.3f;    // favor the particle cloud

        // --- Filter: Ladder LP with drive for grit on the busy pattern ---
        s.filter.type = 4;          // Ladder
        s.filter.ladderSlope = 4;   // 24 dB
        s.filter.ladderDrive = 6.0f;// input drive -> harmonic thickening/warmth
        s.filter.cutoffHz = 3800.0f;
        s.filter.resonance = 2.0f;
        s.filter.envAmount = 24.0f; // +24 st sweep (FIXED semitones)

        // --- Envelopes: percussive with a little tail so the grains bloom ---
        s.ampEnv.attackMs = 2.0f;
        s.ampEnv.decayMs = 200.0f;
        s.ampEnv.sustain = 0.2f;
        s.ampEnv.releaseMs = 170.0f;
        s.filterEnv.attackMs = 1.0f;
        s.filterEnv.decayMs = 170.0f;
        s.filterEnv.sustain = 0.1f;
        s.filterEnv.releaseMs = 150.0f;

        // --- Mod identity (unique): per-STEP Random resonance sparkle, hard-stepped and
        //     synced to the 16th grid, so each of the 7 hits gets a different resonant peak. ---
        s.random.sync = 1;
        s.random.noteValue = kNote1_16;
        s.random.smoothness = 0.0f; // hard steps, no glide
        setModSlot(s, 0, kSrcRandom, kDstAllResonance, 0.35f, kCurveExp);
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.4f); // velocity opens the cloud

        // --- Scale color: A natural minor ---
        s.arp.scaleType = 1;        // NaturalMinor
        s.arp.rootNote = 9;         // A

        // --- Arp: dense E(7,16) across 2 octaves ---
        setArpEnabled(s, true);
        setArpMode(s, kModeUp);
        setTempoSync(s, true);
        setArpRate(s, kNote1_16);
        setArpGateLength(s, 70.0f);
        s.arp.octaveRange = 2;      // wider melodic range for the busy 7-hit pattern
        setEuclidean(s, true, 7, 16, 0);

        // --- REAL accent GATE lane: dynamic contour over the 7 hits ---
        // >1.0 gates overlap into the next step (legato accents on strong beats 0/4/8/12);
        // <1.0 gates cut short elsewhere. This gives the busy pattern a breathing groove.
        float gate16[] = {1.4f, 0.6f, 0.8f, 0.6f, 1.3f, 0.6f, 0.8f, 0.7f,
                          1.4f, 0.6f, 0.8f, 0.6f, 1.2f, 0.7f, 0.9f, 0.6f};
        setGateLane(s, 16, gate16);
        // velocity lane reinforces the same accent contour
        float vel16[] = {1.0f, 0.6f, 0.72f, 0.6f, 0.95f, 0.6f, 0.75f, 0.65f,
                         1.0f, 0.6f, 0.72f, 0.6f, 0.9f, 0.65f, 0.8f, 0.6f};
        setVelocityLane(s, 16, vel16);

        presets.push_back(std::move(p));
    }

    // ==================== Polymetric Category (2 presets) ====================

    // T032: "3x5x7 Evolving" — integer coprime lanes (3/5/7) over a dual-Additive
    // morph pad; a glacial free LFO sweeps the A<->B blend so the harmonic body
    // breathes beneath a grid that won't repeat for 3*5*7 = 105 steps. NO template.
    {
        PresetDef p;
        p.name = "3x5x7 Evolving";
        p.category = "Arp Polymetric";

        // --- Voice: two Additive oscillators at the SAME pitch, so the mixer morph
        //     is PURE timbre. A = dark/hollow (few partials, falling tilt);
        //     B = bright/rich (many partials, faint inharmonic shimmer).
        p.state.oscA.type = 4;                 // Additive
        p.state.oscA.additivePartials = 8;     // organ-ish, hollow body
        p.state.oscA.additiveTilt = -6.0f;     // dark: HF rolled off
        p.state.oscA.additiveInharm = 0.0f;    // pure harmonic
        p.state.oscA.level = 0.85f;
        p.state.oscB.type = 4;                 // Additive
        p.state.oscB.additivePartials = 48;    // bright, spectrally rich
        p.state.oscB.additiveTilt = 5.0f;      // tilted up: airy top end
        p.state.oscB.additiveInharm = 0.15f;   // faint bell/glass shimmer
        p.state.oscB.level = 0.85f;
        p.state.mixer.mode = 0;                // Crossfade
        p.state.mixer.position = 0.5f;         // start mid-morph; LFO sweeps it

        // --- Filter: 24 dB SVF LP with a SLOW filter-env "inhale". Fixes the classic
        //     bug: envAmount is SEMITONES (-48..+48), so +18 is a real, audible sweep.
        p.state.filter.type = 0;               // SVF LP
        p.state.filter.cutoffHz = 3800.0f;
        p.state.filter.resonance = 4.66f;
        p.state.filter.svfSlope = 1;           // 24 dB
        p.state.filter.envAmount = 18.0f;      // +18 st slow filter breath (was buggy 4000)
        p.state.filter.keyTrack = 0.3f;        // brighter up the keyboard
        p.state.filterEnv.attackMs = 900.0f;   // very slow open
        p.state.filterEnv.decayMs = 2500.0f;
        p.state.filterEnv.sustain = 0.6f;
        p.state.filterEnv.releaseMs = 1800.0f;

        // --- Amp env: classic pad swell/tail.
        p.state.ampEnv.attackMs = 400.0f;
        p.state.ampEnv.decayMs = 1500.0f;
        p.state.ampEnv.sustain = 0.75f;
        p.state.ampEnv.releaseMs = 1200.0f;

        // --- Wide poly so overlapping arp tails spread across the field.
        p.state.global.width = 1.4f;           // stereo widen (0..2)
        p.state.global.spread = 0.4f;          // pan spread across voices
        p.state.global.polyphony = 12;         // room for long tails to stack

        // --- Mod identity: THREE independent motions, none shared with the sibling.
        // (0) Glacial FREE LFO1 -> morph position: the A<->B spectrum drifts open and
        //     shut over ~20 s beneath the grid. This is the "evolving" core.
        p.state.lfo1.rateHz = 0.05f;           // ~20 s cycle
        p.state.lfo1.shape = 0;                // Sine
        p.state.lfo1.depth = 1.0f;
        p.state.lfo1.sync = 0;                 // FREE-running (not tempo-locked)
        setModSlot(p.state, 0, kSrcLFO1, kDstAllMorphPos, 0.9f, kCurveSCurve);
        // (1) A second, unrelated slow triangle LFO2 -> spectral tilt, so the two
        //     brightness motions phase against each other.
        p.state.lfo2.rateHz = 0.08f;
        p.state.lfo2.shape = 1;                // Triangle
        p.state.lfo2.depth = 0.6f;
        p.state.lfo2.sync = 0;
        setModSlot(p.state, 1, kSrcLFO2, kDstAllSpecTilt, 0.3f);
        // (2) Arp pitch -> filter cutoff: every polymetric step is a different pitch,
        //     so each nudges the cutoff — the never-repeating grid becomes timbral.
        setModSlot(p.state, 2, kSrcArpPitch, kDstAllFltCut, 0.35f, kCurveExp);
        p.state.modMatrix.slots[2].scale = 3;  // x2 depth (exercises the dormant scale axis)

        // --- Wrapper: a PLATE (not the default diffuse hall) keeps the top shimmery.
        p.state.reverbEnabled = 1;
        p.state.reverbType = 0;                // Plate
        p.state.reverb.size = 0.55f;
        p.state.reverb.mix = 0.28f;
        p.state.reverb.damping = 0.35f;

        // --- Arp: integer-coprime lanes 3/5/7, ALL at default speed 1.0. The lengths
        //     alone guarantee non-repetition; the sibling owns fractional speeds.
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUp);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 90.0f);      // slight overlap for a legato pad grid
        p.state.arp.octaveRange = 2;
        p.state.arp.octaveMode = 1;            // Interleaved octaves (exercises octaveMode)
        float vel3[] = {0.6f, 0.85f, 1.0f};            // length 3
        setVelocityLane(p.state, 3, vel3);
        int32_t pitch5[] = {0, 5, 7, 3, 10};           // length 5
        setPitchLane(p.state, 5, pitch5);
        int32_t ratch7[] = {1, 1, 2, 1, 1, 3, 1};      // length 7, occasional rolls
        setRatchetLane(p.state, 7, ratch7);

        presets.push_back(std::move(p));
    }

    // T033: "Reich Drift" (was "4x5 Shifting") — hard-sync bass whose PITCH and
    // RATCHET lanes run at FRACTIONAL speeds (1.5 / 0.75) against 1.0 lanes, so the
    // riff phases out and slowly re-aligns against the beat. NO template.
    {
        PresetDef p;
        p.name = "Reich Drift";
        p.category = "Arp Polymetric";

        // --- Voice: OSC A = hard-Sync engine (buzzy, formant-rich slave) an octave
        //     down; OSC B = a square sub two octaves down for low-end weight.
        p.state.oscA.type = 3;                 // Sync
        p.state.oscA.tuneSemitones = -12.0f;   // octave-down bass
        p.state.oscA.syncRatio = 2.5f;         // bright classic sync formant
        p.state.oscA.syncWaveform = 1;         // Saw slave (aggressive)
        p.state.oscA.syncMode = 0;             // Hard sync
        p.state.oscA.syncAmount = 1.0f;
        p.state.oscA.level = 0.9f;
        p.state.oscB.type = 0;                 // PolyBLEP
        p.state.oscB.waveform = 3;             // Square
        p.state.oscB.tuneSemitones = -24.0f;   // deep sub
        p.state.oscB.level = 0.45f;
        p.state.mixer.position = 0.4f;         // favour the sync bite, keep the sub

        // --- Filter: driven Ladder with a fast, deep env sweep. Bug fixed: envAmount
        //     is SEMITONES, +30 = a punchy acid sweep (the old 4000 was inaudible).
        p.state.filter.type = 4;               // Ladder LP
        p.state.filter.cutoffHz = 380.0f;      // low base so the env sweep is obvious
        p.state.filter.resonance = 6.35f;
        p.state.filter.ladderSlope = 4;        // 24 dB/oct
        p.state.filter.ladderDrive = 6.0f;     // dB of drive for bass grit
        p.state.filter.envAmount = 30.0f;      // +30 st acid sweep (was buggy 4000)
        p.state.filter.keyTrack = 0.4f;
        p.state.filterEnv.attackMs = 1.0f;     // instant snap
        p.state.filterEnv.decayMs = 180.0f;
        p.state.filterEnv.sustain = 0.15f;
        p.state.filterEnv.releaseMs = 120.0f;

        // --- Amp env: tight plucky bass.
        p.state.ampEnv.attackMs = 2.0f;
        p.state.ampEnv.decayMs = 220.0f;
        p.state.ampEnv.sustain = 0.5f;
        p.state.ampEnv.releaseMs = 140.0f;

        // --- Mono + glide: 303-style legato so held riff notes slide into each other.
        p.state.global.voiceMode = 1;          // Mono
        p.state.monoMode.legato = 1;
        p.state.monoMode.portamentoTimeMs = 35.0f;
        p.state.monoMode.portaMode = 1;        // glide on legato only

        // --- Grit: Tape-Saturator distortion (this preset OWNS that dirty type). A
        //     per-note mod-env transient pushes the drive so each hit spits.
        p.state.distortion.type = 5;           // TapeSaturator
        p.state.distortion.drive = 0.35f;
        p.state.distortion.character = 0.5f;
        p.state.distortion.mix = 0.8f;
        p.state.modEnv.attackMs = 1.0f;
        p.state.modEnv.decayMs = 120.0f;
        p.state.modEnv.sustain = 0.0f;
        p.state.modEnv.releaseMs = 80.0f;

        // --- Mod identity (distinct from the sibling's smooth 3-LFO drift):
        // (voice 0) Velocity -> cutoff: dynamics open the filter per step.
        setVoiceRoute(p.state, 0, kVSrcVelocity, kVDstFltCut, 0.45f);
        // (voice 1) Mod-env (ENV3) -> distortion drive: a spitting grit transient per note.
        setVoiceRoute(p.state, 1, kVSrcEnv3, kVDstDistDrive, 0.5f);
        // (global 0) Tempo-synced SmoothRandom LFO1 -> all-voice cutoff: a stepped
        //     filter wander locked to the grid, so the phasing riff keeps re-colouring.
        p.state.lfo1.rateHz = 4.0f;
        p.state.lfo1.shape = 5;                // SmoothRandom (rarely-used shape)
        p.state.lfo1.depth = 1.0f;
        p.state.lfo1.sync = 1;                 // tempo-locked
        p.state.lfo1Ext.noteValue = kNote1_16; // 1/16 wander
        setModSlot(p.state, 0, kSrcLFO1, kDstAllFltCut, 0.25f, kCurveLinear);

        // --- Wrapper: PingPong delay (NO reverb) throws the shuffling ratchets across
        //     the stereo field — reinforces the "shifting" motion, diversifies the FX.
        p.state.delayEnabled = 1;
        p.state.delay.type = 2;                // PingPong
        p.state.delay.sync = 1;
        p.state.delay.noteValue = kNote1_8;
        p.state.delay.feedback = 0.35f;
        p.state.delay.mix = 0.22f;
        p.state.delay.pingPongCrossFeed = 0.8f;
        p.state.delay.pingPongWidth = 130.0f;

        // --- Arp: lanes 4/5/6, with PITCH @1.5x and RATCHET @0.75x — the fractional
        //     speeds phase the riff against the beat and each other (the whole point).
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeAsPlayed);    // hold one note; the lanes build the riff
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 75.0f);
        p.state.arp.octaveRange = 1;           // stay in the bass register
        float vel4[] = {0.7f, 1.0f, 0.6f, 0.9f};             // length 4 @ 1.0
        setVelocityLane(p.state, 4, vel4);
        float gate5[] = {0.9f, 0.6f, 1.0f, 0.7f, 0.5f};      // length 5 @ 1.0
        setGateLane(p.state, 5, gate5);
        int32_t pitch5[] = {0, 12, 7, 0, -5};                // length 5 octave/fifth riff
        setPitchLane(p.state, 5, pitch5);
        p.state.arp.pitchLaneSpeed = 1.5f;     // pitch lane runs 1.5x — phases AHEAD
        int32_t ratch6[] = {1, 2, 1, 2, 1, 2};               // length 6, alternating double-hits
        setRatchetLane(p.state, 6, ratch6);
        p.state.arp.ratchetLaneSpeed = 0.75f;  // ratchet lane runs 0.75x — drags BEHIND

        presets.push_back(std::move(p));
    }

    // ==================== Generative Category (2 presets) ====================

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

    // ==================== Performance Category (2 presets) ====================

    // T036: "Fill Cascade"
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

    // T037: "Ratio Stutter" (was "Probability Waves")
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

    // ==================== Arp Chords Category (5 presets) ====================

    // "Cathedral Triads" - Wavetable choir-organ, Chord-mode block triads (Plate)
    {
        PresetDef p;
        p.name = "Cathedral Triads";
        p.category = "Arp Chords";
        auto& s = p.state;
        // --- Voice: Wavetable core (OSC A) morphed against a soft PolyBLEP triangle
        //     body (OSC B) so the mixer position = a gentle choir 'aah' motion.
        s.oscA.type = 1;                 // Wavetable engine (never used elsewhere)
        s.oscA.waveform = 2;             // Square base table -> hollow organ core
        s.oscA.level = 0.85f;
        s.oscA.phaseMod = 0.22f;         // faint inharmonic shimmer = choral 'air'
        s.oscB.type = 0;                 // PolyBLEP body
        s.oscB.waveform = 4;             // Triangle -> smooth, pure fundament
        s.oscB.tuneSemitones = 12.0f;    // octave up = 8'+4' organ registration
        s.oscB.level = 0.5f;
        s.mixer.mode = 0;                // Crossfade keeps the tone CLEAN (not FFT-washy)
        s.mixer.position = 0.42f;        // favour the wavetable core
        // --- Filter: Formant (type 5) gives the choir its vowel colour
        s.filter.type = 5;               // Formant filter
        s.filter.cutoffHz = 2600.0f;
        s.filter.resonance = 4.09f;
        s.filter.formantMorph = 0.8f;    // sit between A and E vowels
        s.filter.formantGender = 0.15f;  // nudge toward a female/choir formant set
        // --- Amp env: organ-like, slow-ish swell with high sustain
        s.ampEnv.attackMs = 60.0f;
        s.ampEnv.decayMs = 400.0f;
        s.ampEnv.sustain = 0.85f;
        s.ampEnv.releaseMs = 900.0f;
        // --- Mod identity: slow free-run triangle LFO drifts the A/B morph (choir sway)
        s.lfo1.rateHz = 0.30f;           // very slow
        s.lfo1.shape = 1;                // Triangle
        s.lfo1.sync = 0;                 // free-running, unrelated to tempo
        setModSlot(s, 0, kSrcLFO1, kDstAllMorphPos, 0.45f, kCurveSCurve);
        // Second, slower LFO breathes the formant cutoff a touch
        s.lfo2.rateHz = 0.17f;
        s.lfo2.shape = 0;                // Sine
        s.lfo2.sync = 0;
        setModSlot(s, 1, kSrcLFO2, kDstAllFltCut, 0.18f, kCurveLinear);
        // Mod envelope adds a gentle per-note filter bloom
        s.modEnv.attackMs = 250.0f;
        s.modEnv.decayMs = 600.0f;
        s.modEnv.sustain = 0.6f;
        setVoiceRoute(s, 0, kVSrcEnv3, kVDstFltCut, 0.30f);
        // --- Chorus widens the choir
        s.modulationType = 3;            // Chorus
        s.chorus.rateHz = 0.4f;
        s.chorus.depth = 0.35f;
        s.chorus.voices = 3;
        s.chorus.mix = 0.4f;
        // --- FX: bright PLATE reverb (this preset's reverb identity)
        s.reverbEnabled = 1;
        s.reverbType = 0;                // Plate
        s.reverb.size = 0.62f;
        s.reverb.mix = 0.32f;
        s.reverb.damping = 0.35f;
        s.reverb.diffusion = 0.7f;
        // --- Arp: Chord mode fires all held notes together each step
        setArpEnabled(s, true);
        setArpMode(s, 9);                // Chord (all tones together) - literal 9
        setTempoSync(s, true);
        setArpRate(s, kNote1_8);
        setArpGateLength(s, 85.0f);
        s.arp.octaveRange = 1;           // chord mode: keep register tight
        s.arp.scaleType = 0;             // Major
        s.arp.rootNote = 0;              // C
        s.arp.scaleQuantizeInput = 1;    // snap held notes into C major
        setVoicingMode(s, 0);            // Close voicing = block triads
        // Chord lane: constant diatonic triads
        int32_t chords4[] = {kChordTriad, kChordTriad, kChordTriad, kChordTriad};
        setChordLane(s, 4, chords4);
        float vel4[] = {0.9f, 0.75f, 0.85f, 0.72f};
        setVelocityLane(s, 4, vel4);
        presets.push_back(std::move(p));
    }

    // "Minor 7th Pulse" - Dark octave-down Ladder-bass 7th stabs, stepped-res jitter
    {
        PresetDef p;
        p.name = "Minor 7th Pulse";
        p.category = "Arp Chords";
        auto& s = p.state;
        // --- Voice: sub-octave saw (A) + square (B), classic reese-ish bass
        s.oscA.type = 0;                 // PolyBLEP
        s.oscA.waveform = 1;             // Saw
        s.oscA.tuneSemitones = -12.0f;   // one octave down
        s.oscA.level = 0.8f;
        s.oscB.type = 0;                 // PolyBLEP
        s.oscB.waveform = 2;             // Square (real square = 2, not 3)
        s.oscB.tuneSemitones = -12.0f;
        s.oscB.fineCents = 4.0f;         // slight beat for thickness
        s.oscB.level = 0.45f;
        s.mixer.position = 0.5f;
        // --- Filter: a REAL Moog ladder (type 4), 24 dB, driven for growl
        s.filter.type = 4;               // Ladder (audit: templates wrongly used SVF here)
        s.filter.ladderSlope = 4;        // 24 dB/oct
        s.filter.ladderDrive = 6.0f;     // dB of input drive -> harmonic grit
        s.filter.cutoffHz = 900.0f;      // low, so the env sweep is audible
        s.filter.resonance = 3.0f;
        s.filter.envAmount = 22.0f;      // BUG FIX: +22 SEMITONES (was 4000 Hz nonsense)
        // --- Envelopes: tight plucked bass
        s.ampEnv.attackMs = 2.0f;
        s.ampEnv.decayMs = 180.0f;
        s.ampEnv.sustain = 0.35f;
        s.ampEnv.releaseMs = 140.0f;
        s.filterEnv.attackMs = 1.0f;
        s.filterEnv.decayMs = 130.0f;    // fast decay = snappy sweep per stab
        s.filterEnv.sustain = 0.0f;
        s.filterEnv.releaseMs = 120.0f;
        s.filterEnv.decayCurve = 0.4f;   // exp-ish decay = punchier pluck
        s.global.masterGain = 0.85f;     // headroom for drive + resonance
        // --- Mod identity: synced S/H-style Random -> resonance in stepped jumps,
        //     so every stab has a slightly different squelch.
        s.random.sync = 1;
        s.random.noteValue = kNote1_8;
        s.random.smoothness = 0.0f;      // hard jumps
        setModSlot(s, 0, kSrcRandom, kDstAllResonance, 0.25f, kCurveStepped);
        // Velocity opens the ladder for dynamics
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.35f);
        // --- FX: Digital delay, dotted-1/8 (rhythmic depth), NO reverb (stays tight)
        s.delayEnabled = 1;
        s.delay.type = 0;                // Digital
        s.delay.sync = 1;
        s.delay.noteValue = kNote1_8D;
        s.delay.mix = 0.22f;
        s.delay.feedback = 0.32f;
        // --- Arp: as-played 1/16 staccato 7th chords
        setArpEnabled(s, true);
        setArpMode(s, kModeAsPlayed);
        setTempoSync(s, true);
        setArpRate(s, kNote1_16);
        setArpGateLength(s, 55.0f);
        s.arp.scaleType = 1;             // NaturalMinor
        s.arp.rootNote = 9;              // A
        s.arp.scaleQuantizeInput = 1;    // snap input into A minor
        // Chord lane: 7th chords on the beat, dyads on the off
        int32_t chords8[] = {
            kChord7th, kChordNone, kChordDyad, kChordNone,
            kChord7th, kChordNone, kChordDyad, kChordNone
        };
        setChordLane(s, 8, chords8);
        // Inversion lane: emphasise FIRST inversion for the dark stab voicing
        int32_t inv8[] = {
            kInv1st, kInvRoot, kInv1st, kInvRoot,
            kInv1st, kInvRoot, kInv1st, kInvRoot
        };
        setInversionLane(s, 8, inv8);
        // Staccato gate carving
        float gate8[] = {0.6f, 0.3f, 0.45f, 0.3f, 0.6f, 0.3f, 0.5f, 0.3f};
        setGateLane(s, 8, gate8);
        float vel8[] = {1.0f, 0.5f, 0.8f, 0.45f, 0.95f, 0.5f, 0.75f, 0.45f};
        setVelocityLane(s, 8, vel8);
        presets.push_back(std::move(p));
    }

    // "Dorian Cascade" - Additive polymetric harmony that phases out of sync
    {
        PresetDef p;
        p.name = "Dorian Cascade";
        p.category = "Arp Chords";
        auto& s = p.state;
        // --- Voice: two Additive oscillators, one octave apart, for a rich
        //     spectral pad whose tilt/inharmonicity we can modulate live.
        s.oscA.type = 4;                 // Additive engine
        s.oscA.additivePartials = 24;    // rich but not harsh
        s.oscA.additiveTilt = -3.0f;     // gently darker top
        s.oscA.additiveInharm = 0.08f;   // faint bell shimmer
        s.oscA.level = 0.8f;
        s.oscB.type = 4;                 // Additive shimmer octave
        s.oscB.additivePartials = 8;     // sparse = airy overtone layer
        s.oscB.additiveTilt = 2.0f;
        s.oscB.tuneSemitones = 12.0f;
        s.oscB.level = 0.4f;
        s.mixer.position = 0.45f;
        // --- Filter: SVF bandpass keeps the cascade mid-focused and clear
        s.filter.type = 2;               // SVF_BP
        s.filter.svfSlope = 1;           // 24 dB
        s.filter.cutoffHz = 1200.0f;
        s.filter.resonance = 1.2f;
        s.filter.envAmount = 18.0f;      // slow filter opening (semitones)
        // --- Envelopes: slow swelling pad
        s.ampEnv.attackMs = 300.0f;
        s.ampEnv.decayMs = 600.0f;
        s.ampEnv.sustain = 0.7f;
        s.ampEnv.releaseMs = 1200.0f;
        s.filterEnv.attackMs = 400.0f;
        s.filterEnv.decayMs = 800.0f;
        s.filterEnv.sustain = 0.5f;
        s.filterEnv.releaseMs = 1000.0f;
        // --- Mod identity: LFO on the additive spectral tilt + a HEAVILY smoothed
        //     random on morph position = two independent slow drifts.
        s.lfo1.rateHz = 0.22f;
        s.lfo1.shape = 1;                // Triangle
        s.lfo1.sync = 0;                 // free-run
        setModSlot(s, 0, kSrcLFO1, kDstAllSpecTilt, 0.4f, kCurveSCurve);
        s.random.sync = 0;
        s.random.rateHz = 0.4f;
        s.random.smoothness = 0.8f;      // slow, wandering (not stepped)
        setModSlot(s, 1, kSrcRandom, kDstAllMorphPos, 0.3f, kCurveSCurve);
        setVoiceRoute(s, 0, kVSrcEnv3, kVDstFltCut, 0.35f);
        // --- FX: HALL reverb + SPECTRAL delay (distinct from the plate/digital siblings)
        s.reverbEnabled = 1;
        s.reverbType = 1;                // Hall
        s.reverb.size = 0.85f;
        s.reverb.mix = 0.4f;
        s.reverb.damping = 0.3f;
        s.reverb.diffusion = 0.8f;
        s.delayEnabled = 1;
        s.delay.type = 4;                // Spectral delay
        s.delay.sync = 1;
        s.delay.noteValue = kNote1_8;
        s.delay.mix = 0.28f;
        s.delay.feedback = 0.45f;
        s.delay.spectralFFTSize = 2;     // 2048
        s.delay.spectralSpreadMs = 400.0f;
        s.delay.spectralDiffusion = 0.5f;
        s.delay.spectralTilt = 0.2f;
        // --- Arp: Converge motion, polymetric harmony lanes
        setArpEnabled(s, true);
        setArpMode(s, 4);                // Converge (outside-in) - literal 4
        setTempoSync(s, true);
        setArpRate(s, kNote1_16);
        setArpGateLength(s, 88.0f);
        s.arp.octaveRange = 2;
        s.arp.scaleType = 4;             // Dorian
        s.arp.rootNote = 2;              // D
        s.arp.scaleQuantizeInput = 1;
        setVoicingMode(s, 3);            // Random octave displacement adds to the cascade
        // Chord lane length 5, running at 1.5x speed
        int32_t chords5[] = {kChordTriad, kChordDyad, kChord7th, kChordTriad, kChordNone};
        setChordLane(s, 5, chords5);
        s.arp.chordLaneSpeed = 1.5f;     // harmony cycles faster than the clock
        // Inversion lane length 7, running at 0.5x speed -> phases against chords
        int32_t inv7[] = {kInvRoot, kInv1st, kInv2nd, kInvRoot, kInv1st, kInv2nd, kInv3rd};
        setInversionLane(s, 7, inv7);
        s.arp.inversionLaneSpeed = 0.5f; // inversions crawl -> long non-repeating harmony
        // Pitch lane length 3 for extra polymetry
        int32_t pitch3[] = {0, 7, -5};
        setPitchLane(s, 3, pitch3);
        float vel4[] = {0.9f, 0.6f, 0.8f, 0.55f};
        setVelocityLane(s, 4, vel4);
        presets.push_back(std::move(p));
    }

    // "Lydian Aurora" - Formant-vocal spread 9th pad, aftertouch-morphed, huge hall
    {
        PresetDef p;
        p.name = "Lydian Aurora";
        p.category = "Arp Chords";
        auto& s = p.state;
        // --- Voice: Formant (vocal) oscillator A over an airy saw bed B; the mixer
        //     morphs vocal<->saw, and we hand that morph to AFTERTOUCH.
        s.oscA.type = 7;                 // Formant engine
        s.oscA.formantVowel = 0;         // A
        s.oscA.formantMorph = 1.5f;      // drift toward E/I -> brighter 'aah/eee'
        s.oscA.level = 0.8f;
        s.oscB.type = 0;                 // PolyBLEP saw bed
        s.oscB.waveform = 1;             // Saw
        s.oscB.fineCents = 7.0f;         // shimmer detune
        s.oscB.level = 0.4f;
        s.mixer.position = 0.5f;
        // --- Filter: SVF High-Shelf boosts the air above cutoff for the 'aurora' sheen
        s.filter.type = 10;              // SVF_HighShelf
        s.filter.cutoffHz = 3000.0f;
        s.filter.resonance = 5.22f;
        s.filter.svfSlope = 1;
        s.filter.svfGain = 6.0f;         // +6 dB of air
        // --- Amp env: long, slow, ambient swell
        s.ampEnv.attackMs = 500.0f;
        s.ampEnv.decayMs = 800.0f;
        s.ampEnv.sustain = 0.75f;
        s.ampEnv.releaseMs = 1500.0f;
        // --- Mod identity: expressive AFTERTOUCH - pressure morphs vocal->saw AND
        //     blooms resonance (an almost-unused source across the whole bank).
        setVoiceRoute(s, 0, kVSrcAftertouch, kVDstMorphPos, 0.5f);
        setVoiceRoute(s, 1, kVSrcAftertouch, kVDstFltRes, 0.3f);
        // A slow shimmer LFO on cutoff keeps it alive when no pressure is applied
        s.lfo1.rateHz = 0.15f;
        s.lfo1.shape = 0;                // Sine
        s.lfo1.sync = 0;
        setModSlot(s, 0, kSrcLFO1, kDstAllFltCut, 0.2f, kCurveLinear);
        // --- Wide stereo image for the 'aurora'
        s.global.width = 1.6f;           // plain 0-2 multiplier
        s.global.spread = 0.5f;          // voice pan spread
        // --- FX: enormous, diffuse, MODULATED hall (its unmistakable reverb identity)
        s.reverbEnabled = 1;
        s.reverbType = 1;                // Hall
        s.reverb.size = 0.92f;
        s.reverb.mix = 0.5f;
        s.reverb.damping = 0.25f;
        s.reverb.diffusion = 0.88f;
        s.reverb.preDelayMs = 25.0f;
        s.reverb.modRateHz = 0.3f;
        s.reverb.modDepth = 0.15f;       // lush chorused tail
        // --- Arp: slow descending Diverge 1/4, wide spread 9th chords
        setArpEnabled(s, true);
        setArpMode(s, 5);                // Diverge (inside-out) - literal 5
        setTempoSync(s, true);
        setArpRate(s, kNote1_4);
        setArpGateLength(s, 100.0f);
        s.arp.scaleType = 7;             // Lydian
        s.arp.rootNote = 5;              // F
        s.arp.scaleQuantizeInput = 1;
        setVoicingMode(s, 2);            // Spread -> alternate notes up an octave
        int32_t chords4[] = {kChord9th, kChord9th, kChord7th, kChord9th};
        setChordLane(s, 4, chords4);
        // Inversion lane emphasises SECOND inversion for the wide, rootless-ish top
        int32_t inv4[] = {kInv2nd, kInvRoot, kInv2nd, kInv1st};
        setInversionLane(s, 4, inv4);
        // Velocity swell into the pattern
        float vel4[] = {0.5f, 0.65f, 0.82f, 1.0f};
        setVelocityLane(s, 4, vel4);
        presets.push_back(std::move(p));
    }

    // "Stab Machine" - Hard-sync funk stabs, Drop2, velocity-driven tape grit
    {
        PresetDef p;
        p.name = "Stab Machine";
        p.category = "Arp Chords";
        auto& s = p.state;
        // --- Voice: hard-sync oscillator (A) for the aggressive stab formant, plus
        //     a sub square (B) for weight underneath the funk.
        s.oscA.type = 3;                 // Sync engine
        s.oscA.syncRatio = 2.5f;         // sits in the classic sync sweet spot
        s.oscA.syncWaveform = 1;         // Saw slave -> bright, cutting
        s.oscA.syncMode = 0;             // Hard sync
        s.oscA.syncAmount = 1.0f;
        s.oscA.level = 0.85f;
        s.oscB.type = 0;                 // PolyBLEP sub
        s.oscB.waveform = 2;             // Square
        s.oscB.tuneSemitones = -12.0f;
        s.oscB.level = 0.4f;
        s.mixer.position = 0.4f;
        // --- Filter: SVF LP, 24 dB, driven, with a STRONG env pluck sweep
        s.filter.type = 0;               // SVF_LP
        s.filter.svfSlope = 1;           // 24 dB
        s.filter.svfDrive = 6.0f;        // dB post-filter saturation -> bite
        s.filter.cutoffHz = 700.0f;      // low so the pluck sweep is dramatic
        s.filter.resonance = 2.5f;
        s.filter.envAmount = 32.0f;      // BUG-FIX-CLASS value: +32 SEMITONES pluck
        // --- Envelopes: extremely tight stab
        s.ampEnv.attackMs = 1.0f;
        s.ampEnv.decayMs = 140.0f;
        s.ampEnv.sustain = 0.25f;
        s.ampEnv.releaseMs = 90.0f;
        s.filterEnv.attackMs = 1.0f;
        s.filterEnv.decayMs = 120.0f;
        s.filterEnv.sustain = 0.0f;
        s.filterEnv.releaseMs = 90.0f;
        s.filterEnv.decayCurve = 0.5f;   // snappy exponential pluck
        s.global.masterGain = 0.85f;     // headroom for drive + distortion
        // --- Distortion: Tape saturator gives the stabs their punch
        s.distortion.type = 5;           // TapeSaturator
        s.distortion.drive = 0.35f;
        s.distortion.character = 0.5f;
        s.distortion.mix = 0.5f;
        s.distortion.tapeSaturation = 0.5f;
        // --- Mod identity: velocity opens the filter AND pushes distortion drive,
        //     so hard hits are brighter AND dirtier - the funk 'accent' response.
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.4f);
        setVoiceRoute(s, 1, kVSrcVelocity, kVDstDistDrive, 0.5f);
        // --- FX: Digital delay, dotted-1/16 groove, widened
        s.delayEnabled = 1;
        s.delay.type = 0;                // Digital
        s.delay.sync = 1;
        s.delay.noteValue = kNote1_16D;
        s.delay.mix = 0.18f;
        s.delay.feedback = 0.28f;
        s.delay.digitalWidth = 130.0f;
        // --- Arp: funky as-played 1/16 with swing
        setArpEnabled(s, true);
        setArpMode(s, kModeAsPlayed);
        setTempoSync(s, true);
        setArpRate(s, kNote1_16);
        setArpGateLength(s, 50.0f);
        setArpSwing(s, 15.0f);
        s.arp.scaleType = 5;             // Mixolydian
        s.arp.rootNote = 7;              // G
        s.arp.scaleQuantizeInput = 1;
        setVoicingMode(s, 1);            // Drop2 (2nd-from-top note down an octave)
        s.arp.spice = 0.2f;              // per-step randomisation ~= 'one step random' feel
        // Chord lane: sparse Triad/7th stabs with rests
        int32_t chords16[] = {
            kChordTriad, kChordNone, kChordNone, kChord7th,
            kChordNone, kChordTriad, kChordNone, kChordNone,
            kChordDyad,  kChordNone, kChordNone, kChord7th,
            kChordNone, kChordTriad, kChordNone, kChordNone
        };
        setChordLane(s, 16, chords16);
        // Inversion lane: mix First/Second for tighter Drop2 voicings
        int32_t inv16[] = {
            kInvRoot, kInvRoot, kInvRoot, kInv1st,
            kInvRoot, kInv2nd, kInvRoot, kInvRoot,
            kInv1st, kInvRoot, kInvRoot, kInv2nd,
            kInvRoot, kInv1st, kInvRoot, kInvRoot
        };
        setInversionLane(s, 16, inv16);
        // Ratchet lane length 8 -> polymetric against the L16 chord lane
        int32_t ratch8[] = {1, 1, 2, 1, 1, 1, 3, 1};
        setRatchetLane(s, 8, ratch8);
        // Velocity accents drive the filter/dist routes
        float vel16[] = {
            1.0f, 0.4f, 0.3f, 0.85f,
            0.3f, 0.95f, 0.3f, 0.3f,
            0.75f, 0.3f, 0.3f, 0.9f,
            0.3f, 0.7f, 0.3f, 0.3f
        };
        setVelocityLane(s, 16, vel16);
        presets.push_back(std::move(p));
    }

    // ==================== PAD Category (5 presets) ====================

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
        s.filter.resonance = 3.86f;
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
        s.filter.resonance = 2.96f;
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
        s.filter.resonance = 5.22f;
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
        s.filter.resonance = 4.09f;
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
        s.filter.resonance = 5.45f;
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

    // ==================== LEAD Category (5 presets) ====================

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
        s.filter.resonance = 2.40f;
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
        s.filter.resonance = 4.09f;
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
        s.filter.resonance = 8.61f;
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
        s.filter.resonance = 1.83f;
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
        s.filter.resonance = 6.92f;
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

    // ==================== BASS Category (5 presets) ====================

    // "Sub Bass" - Clean foundational sub; owns osc start-phase + tape warmth + PitchSync sub-double
    {
        PresetDef p;
        p.name = "Sub Bass";
        p.category = "Bass";
        auto& s = p.state;
        // Pure sine fundamental one octave down, whisper of triangle harmonic
        s.oscA.type = 0;            // PolyBLEP
        s.oscA.waveform = 0;       // Sine
        s.oscA.tuneSemitones = -12.0f;
        s.oscA.level = 0.9f;
        s.oscA.phase = 0.0f;       // start at zero-crossing => click-free attack (owns start-phase axis)
        s.oscB.type = 0;
        s.oscB.waveform = 4;       // Triangle
        s.oscB.tuneSemitones = -12.0f;
        s.oscB.level = 0.2f;
        s.oscB.phase = 0.0f;       // both oscs phase-locked for a consistent, thump-free onset
        s.mixer.position = 0.15f;  // mostly sine
        // SVF LP with full key-tracking so the sub stays even across the keyboard
        s.filter.type = 0;         // SVF LP
        s.filter.cutoffHz = 500.0f;
        s.filter.resonance = 1.83f;
        s.filter.keyTrack = 1.0f;
        s.ampEnv.attackMs = 2.0f;
        s.ampEnv.decayMs = 150.0f;
        s.ampEnv.sustain = 0.85f;
        s.ampEnv.releaseMs = 120.0f;
        s.global.voiceMode = 1;    // Mono
        // Tape saturator: the one distinctive trait of the "reference" sub - analog weight
        s.distortion.type = 5;     // TapeSaturator
        s.distortion.tapeModel = 0;
        s.distortion.tapeSaturation = 0.4f;
        s.distortion.tapeBias = 0.5f;
        s.distortion.drive = 0.2f;
        s.distortion.mix = 0.6f;   // parallel warmth, keeps the clean fundamental intact
        // Mod-env slowly blooms the tape drive after the attack (distinctive gesture, unique to Sub)
        s.modEnv.attackMs = 400.0f;
        s.modEnv.decayMs = 600.0f;
        s.modEnv.sustain = 1.0f;
        s.modEnv.releaseMs = 300.0f;
        setVoiceRoute(s, 0, kVSrcEnv3, kVDstDistDrive, 0.3f);
        // A very slow, free, smoothstep LFO breathes the cutoff so held notes aren't dead-static
        s.lfo1.rateHz = 0.1f; s.lfo1.shape = 0; s.lfo1.depth = 1.0f; s.lfo1.sync = 0;
        setModSlot(s, 0, kSrcLFO1, kDstAllFltCut, 0.15f, kCurveSCurve);
        // Harmonizer PitchSync doubles an octave below for earth-shaking sub reinforcement
        s.harmonizerEnabled = 1;
        s.harmonizer.harmonyMode = 0;      // Chromatic (literal semitone interval)
        s.harmonizer.pitchShiftMode = 3;   // PitchSync (low-latency, tight for bass)
        s.harmonizer.numVoices = 1;
        s.harmonizer.voiceInterval[0] = -12; // one octave down
        s.harmonizer.voiceLevelDb[0] = -4.0f;
        s.harmonizer.dryLevelDb = 0.0f;
        s.harmonizer.wetLevelDb = -3.0f;
        presets.push_back(std::move(p));
    }

    // "Reese" - Moving detuned-saw Reese: phaser + slow morph LFO + chaos filter drift
    {
        PresetDef p;
        p.name = "Reese";
        p.category = "Bass";
        auto& s = p.state;
        s.oscA.type = 0; s.oscA.waveform = 1; // Saw
        s.oscA.tuneSemitones = -12.0f; s.oscA.level = 0.8f;
        s.oscA.phase = 0.0f;
        s.oscB.type = 0; s.oscB.waveform = 1; // Saw
        s.oscB.tuneSemitones = -12.0f;
        s.oscB.fineCents = 12.0f;   // wider detune than before for a fatter, faster Reese beat
        s.oscB.level = 0.8f;
        s.mixer.position = 0.5f;
        s.filter.type = 4;          // Ladder LP
        s.filter.cutoffHz = 2400.0f;
        s.filter.resonance = 3.53f;
        s.filter.ladderSlope = 4;   // 24 dB/oct
        s.filter.ladderDrive = 2.0f;
        s.ampEnv.attackMs = 8.0f; s.ampEnv.decayMs = 300.0f;
        s.ampEnv.sustain = 0.85f; s.ampEnv.releaseMs = 250.0f;
        s.global.voiceMode = 1; s.monoMode.legato = 1; s.monoMode.portamentoTimeMs = 25.0f;
        // Phaser gives the classic swirling Reese motion
        s.modulationType = 1;       // Phaser
        s.phaser.rateHz = 0.25f; s.phaser.depth = 0.5f;
        s.phaser.feedback = 0.45f; s.phaser.mix = 0.35f;
        s.phaser.stages = 3;        // 8-stage
        // Slow free sine LFO drifts the A/B morph => the detune "breathes"
        s.lfo2.rateHz = 0.15f; s.lfo2.shape = 0; s.lfo2.depth = 0.5f; s.lfo2.sync = 0;
        setModSlot(s, 0, kSrcLFO2, kDstAllMorphPos, 0.4f, kCurveSCurve);
        // Chaos (Rossler) drifts the ladder cutoff for organic, never-repeating movement
        s.chaosMod.rateHz = 0.4f; s.chaosMod.type = 1; s.chaosMod.depth = 0.5f; s.chaosMod.sync = 0;
        setModSlot(s, 1, kSrcChaos, kDstAllFltCut, 0.25f, kCurveSCurve);
        // Velocity opens the filter for dynamic playing
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.45f);
        // Analog tape echo for width/depth without washing out the low end
        s.delayEnabled = 1;
        s.delay.type = 1;           // Tape
        s.delay.sync = 1; s.delay.noteValue = kNote1_8;
        s.delay.feedback = 0.3f; s.delay.mix = 0.18f;
        s.delay.tapeSaturation = 0.5f;
        presets.push_back(std::move(p));
    }

    // "Acid Bass" - Expressive 303: ladder drive, proper filter-env sweep, touch-controlled
    {
        PresetDef p;
        p.name = "Acid Bass";
        p.category = "Bass";
        auto& s = p.state;
        s.oscA.type = 0; s.oscA.waveform = 1; // Saw
        s.oscA.tuneSemitones = -12.0f; s.oscA.level = 0.9f;
        s.oscB.level = 0.0f;        // single-osc 303
        s.mixer.position = 0.0f;    // Osc A only
        s.filter.type = 4;          // Ladder LP
        s.filter.cutoffHz = 500.0f;
        s.filter.resonance = 9.74f;  // squelchy
        s.filter.envAmount = 30.0f; // PROPER semitone sweep (was the 4000 Hz-value bug in the acid template)
        s.filter.ladderSlope = 4;
        s.filter.ladderDrive = 4.0f; // ladder overdrive = the 303 grit
        s.filter.keyTrack = 0.3f;
        s.ampEnv.attackMs = 1.0f; s.ampEnv.decayMs = 200.0f;
        s.ampEnv.sustain = 0.4f; s.ampEnv.releaseMs = 90.0f;
        // Fast, deep filter env = the classic acid pluck-sweep
        s.filterEnv.attackMs = 1.0f; s.filterEnv.decayMs = 260.0f;
        s.filterEnv.sustain = 0.05f; s.filterEnv.releaseMs = 140.0f;
        s.filterEnv.decayCurve = 0.4f; // exp-ish decay for a snappier sweep
        s.global.voiceMode = 1; s.monoMode.legato = 1; s.monoMode.portamentoTimeMs = 35.0f;
        // Velocity scans cutoff with an exponential response (very 303)
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.6f);
        s.voiceRoutes[0].curve = static_cast<int8_t>(kCurveExp);
        // Aftertouch pushes resonance for live squelch control
        setVoiceRoute(s, 1, kVSrcAftertouch, kVDstFltRes, 0.35f);
        // Tight dotted digital delay for that classic acid ping
        s.delayEnabled = 1;
        s.delay.type = 0;           // Digital
        s.delay.sync = 1; s.delay.noteValue = kNote1_8D;
        s.delay.feedback = 0.35f; s.delay.mix = 0.22f;
        s.delay.digitalWidth = 140.0f;
        presets.push_back(std::move(p));
    }

    // "FM Bass" - Digital phase-distortion FM bass whose timbre evolves per note
    {
        PresetDef p;
        p.name = "FM Bass";
        p.category = "Bass";
        auto& s = p.state;
        s.oscA.type = 2;            // Phase Distortion
        s.oscA.pdWaveform = 3;      // DoubleSine (metallic FM bite)
        s.oscA.pdDistortion = 0.55f;
        s.oscA.tuneSemitones = -12.0f; s.oscA.level = 0.85f;
        s.oscA.phaseMod = 0.2f;     // static PM sidebands (owns phaseMod)
        s.oscA.freqMod = 0.35f;     // static FM index (owns freqMod on bass)
        s.oscB.type = 0; s.oscB.waveform = 2; // Square sub
        s.oscB.tuneSemitones = -12.0f; s.oscB.level = 0.5f;
        s.mixer.position = 0.25f;   // start FM-heavy (favor Osc A)...
        s.filter.type = 0;          // SVF LP
        s.filter.cutoffHz = 3800.0f; s.filter.resonance = 2.40f;
        s.ampEnv.attackMs = 1.0f; s.ampEnv.decayMs = 320.0f;
        s.ampEnv.sustain = 0.45f; s.ampEnv.releaseMs = 110.0f;
        s.global.voiceMode = 1;
        // Mod-env sweeps the A/B morph so the bright FM tone settles into the square sub
        // over the note - the "FM character" audibly evolves each key-press.
        s.modEnv.attackMs = 4.0f; s.modEnv.decayMs = 380.0f;
        s.modEnv.sustain = 0.25f; s.modEnv.releaseMs = 150.0f;
        setVoiceRoute(s, 0, kVSrcEnv3, kVDstMorphPos, 0.5f);
        s.voiceRoutes[0].curve = static_cast<int8_t>(kCurveExp);
        // Wavefolder adds extra digital harmonics for a harder metallic edge
        s.distortion.type = 4;      // Wavefolder
        s.distortion.foldType = 2;  // Lockhart
        s.distortion.drive = 0.35f;
        s.distortion.character = 0.5f;
        s.distortion.mix = 0.45f;   // parallel fold, keeps sub weight
        presets.push_back(std::move(p));
    }

    // "Wobble" - Dubstep modulation exemplar: 3 synced/chaos mod slots + ring growl
    {
        PresetDef p;
        p.name = "Wobble";
        p.category = "Bass";
        auto& s = p.state;
        s.oscA.type = 0; s.oscA.waveform = 1; // Saw
        s.oscA.tuneSemitones = -12.0f; s.oscA.level = 0.8f;
        s.oscB.type = 0; s.oscB.waveform = 2; // Square
        s.oscB.tuneSemitones = -12.0f; s.oscB.level = 0.6f;
        s.mixer.position = 0.45f;
        s.filter.type = 4;          // Ladder LP
        s.filter.cutoffHz = 1600.0f; s.filter.resonance = 6.92f;
        s.filter.ladderSlope = 4; s.filter.ladderDrive = 4.0f;
        s.ampEnv.attackMs = 2.0f; s.ampEnv.decayMs = 200.0f;
        s.ampEnv.sustain = 0.85f; s.ampEnv.releaseMs = 150.0f;
        s.global.voiceMode = 1; s.monoMode.legato = 1;
        // Slot 0: tempo-synced sine LFO sweeps cutoff; scale x2 pushes a DEEP wobble
        s.lfo1.rateHz = 4.0f; s.lfo1.shape = 0; s.lfo1.depth = 1.0f; s.lfo1.sync = 1;
        s.lfo1Ext.noteValue = 16; // 1/2 note = slow half-bar wobble
        setModSlot(s, 0, kSrcLFO1, kDstAllFltCut, 0.5f);
        s.modMatrix.slots[0].scale = 3; // x2 => effective ~1.0 deep sweep (owns the scale axis)
        // Slot 1: synced saw LFO saws the morph at a different rate for grit complexity
        s.lfo2.rateHz = 2.0f; s.lfo2.shape = 2; s.lfo2.depth = 0.6f; s.lfo2.sync = 1;
        s.lfo2Ext.noteValue = kNote1_4;
        setModSlot(s, 1, kSrcLFO2, kDstAllMorphPos, 0.5f, kCurveStepped); // stepped = rhythmic morph jumps
        // Slot 2: chaos jitters resonance for organic, unpredictable growl
        s.chaosMod.rateHz = 1.5f; s.chaosMod.type = 0; s.chaosMod.depth = 0.3f;
        setModSlot(s, 2, kSrcChaos, kDstAllResonance, 0.2f);
        // Velocity sets the wobble intensity via cutoff
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.4f);
        // Note-tracked ring modulator adds a metallic growl at the wobble peaks
        s.distortion.type = 6;      // RingModulator
        s.distortion.ringFreqMode = 1; // NoteTrack (stays musical)
        s.distortion.ringWaveform = 0; // Sine
        s.distortion.mix = 0.3f;    // subtle - preserves the low end
        presets.push_back(std::move(p));
    }

    // ==================== TEXTURE Category (5 presets) ====================

    // "Particle Cloud" - animated grain swarm that breathes between grains and noise
    {
        PresetDef p;
        p.name = "Particle Cloud";
        p.category = "Textures";
        auto& s = p.state;
        // --- Voice: sparse grain swarm over a warm pink-noise bed ---
        s.oscA.type = 6;                  // Particle
        s.oscA.particleScatter = 7.0f;    // wide freq scatter -> airy cloud
        s.oscA.particleDensity = 28.0f;   // sparse-to-medium grain count
        s.oscA.particleLifetime = 900.0f; // long grains bloom smoothly
        s.oscA.particleSpawnMode = 1;     // Random spawn -> no rhythmic pulse
        s.oscA.particleEnvType = 3;       // Blackman grains = softest edges (3=Blackman)
        s.oscA.particleDrift = 0.35f;     // slow pitch wander inside the cloud
        s.oscA.level = 0.7f;
        s.oscB.type = 9;                  // Noise
        s.oscB.noiseColor = 1;            // Pink -> warm hiss bed under the grains
        s.oscB.level = 0.12f;
        s.mixer.position = 0.2f;          // mostly grains, hint of noise
        // --- Filter: SVF Band-Pass gives the cloud a focused, vowel-ish body ---
        s.filter.type = 2;                // SVF BP (owns this filter type)
        s.filter.cutoffHz = 1400.0f;
        s.filter.resonance = 1.2f;        // gentle emphasis, not whistling
        s.filter.svfSlope = 1;            // 24 dB
        // --- Motion 1: a glacial SmoothRandom LFO cross-fades grains<->noise ---
        s.lfo1.rateHz = 0.08f;            // ~12 s cycle
        s.lfo1.shape = 5;                 // SmoothRandom
        s.lfo1.depth = 0.8f;
        s.lfo1.sync = 0;                  // free-running, not tempo-locked
        s.lfo1Ext.fadeInMs = 4000.0f;     // motion blooms in over 4 s (owns fadeInMs)
        s.lfo1Ext.phaseOffset = 90.0f;    // start off-centre (owns phaseOffset)
        setModSlot(s, 0, kSrcLFO1, kDstAllMorphPos, 0.55f, kCurveSCurve, 40.0f);
        // --- Motion 2: Sample&Hold steps the resonance -> shimmering "grains of Q" ---
        s.sampleHold.rateHz = 1.5f;       // new value ~every 0.66 s
        s.sampleHold.slewMs = 120.0f;     // soft steps, no clicks
        setModSlot(s, 1, kSrcSampleHold, kDstAllResonance, 0.35f, kCurveStepped, 0.0f);
        // --- Amp env: slow swell, very long tail ---
        s.ampEnv.attackMs = 1200.0f;
        s.ampEnv.decayMs = 600.0f;
        s.ampEnv.sustain = 0.85f;
        s.ampEnv.releaseMs = 3200.0f;
        // --- Gentle tape warmth so the noise bed has body ---
        s.distortion.type = 5;            // TapeSaturator (owns this dirt type)
        s.distortion.drive = 0.25f;
        s.distortion.tapeModel = 0;
        s.distortion.tapeSaturation = 0.4f;
        s.distortion.tapeBias = 0.5f;
        s.distortion.mix = 0.3f;          // subtle, stays a texture
        // --- PingPong delay throws grains across the stereo field ---
        s.delayEnabled = 1;
        s.delay.type = 2;                 // PingPong (owns this delay type)
        s.delay.mix = 0.28f;
        s.delay.feedback = 0.4f;
        s.delay.pingPongCrossFeed = 0.6f;
        s.delay.pingPongWidth = 150.0f;
        // --- Plate reverb: high diffusion + pre-delay for depth ---
        s.reverbEnabled = 1;
        s.reverbType = 0;                 // Plate
        s.reverb.size = 0.85f;
        s.reverb.mix = 0.45f;
        s.reverb.damping = 0.4f;
        s.reverb.diffusion = 0.92f;       // very smeared
        s.reverb.preDelayMs = 45.0f;      // gap before the wash (owns preDelay)
        s.global.width = 1.7f;
        s.global.spread = 0.5f;
        presets.push_back(std::move(p));
    }

    // "Chaos Wind" - unstable four-source drone (the category exemplar)
    {
        PresetDef p;
        p.name = "Chaos Wind";
        p.category = "Textures";
        auto& s = p.state;
        // --- Voice: Rossler attractor + brown-noise gusts ---
        s.oscA.type = 5;                  // Chaos
        s.oscA.chaosAttractor = 1;        // Rossler
        s.oscA.chaosAmount = 0.7f;
        s.oscA.chaosCoupling = 0.45f;     // cross-axis instability
        s.oscA.chaosOutput = 1;           // Y axis
        s.oscA.level = 0.6f;
        s.oscB.type = 9;                  // Noise
        s.oscB.noiseColor = 2;            // Brown -> low rumble bed
        s.oscB.level = 0.22f;
        s.mixer.position = 0.28f;
        // --- Filter: SVF Notch scoops the mids -> hollow, windy timbre ---
        s.filter.type = 3;                // SVF Notch (owns this filter type)
        s.filter.cutoffHz = 2200.0f;
        s.filter.resonance = 1.5f;
        // --- Motion: three independent wanderers on three destinations ---
        s.lfo1.rateHz = 0.1f;
        s.lfo1.shape = 5;                 // SmoothRandom
        s.lfo1.depth = 0.7f;
        s.lfo1.sync = 0;
        setModSlot(s, 0, kSrcLFO1, kDstAllFltCut, 0.5f, kCurveLinear, 30.0f);
        s.chaosMod.rateHz = 0.5f; s.chaosMod.type = 1; s.chaosMod.depth = 0.5f; // Rossler LFO
        setModSlot(s, 1, kSrcChaos, kDstAllSpecTilt, 0.4f, kCurveExp, 80.0f);
        s.random.rateHz = 0.8f; s.random.smoothness = 0.85f;
        setModSlot(s, 2, kSrcRandom, kDstAllMorphPos, 0.3f, kCurveLinear, 60.0f);
        // --- Rungler: bit-crushed stepped voltage into resonance, boosted x2 ---
        s.rungler.osc1FreqHz = 1.7f; s.rungler.osc2FreqHz = 2.9f;
        s.rungler.depth = 0.6f; s.rungler.bits = 8;
        setModSlot(s, 3, kSrcRungler, kDstAllResonance, 0.4f, kCurveSCurve, 20.0f);
        s.modMatrix.slots[3].scale = 3;   // x2 depth (owns the mod-matrix scale axis)
        // --- Amp env: slow gusting swell ---
        s.ampEnv.attackMs = 800.0f;
        s.ampEnv.decayMs = 500.0f;
        s.ampEnv.sustain = 0.9f;
        s.ampEnv.releaseMs = 2600.0f;
        // --- Wavefolder adds jagged harmonics as the chaos peaks ---
        s.distortion.type = 4;            // Wavefolder (owns this dirt type)
        s.distortion.drive = 0.35f;
        s.distortion.foldType = 1;
        s.distortion.mix = 0.3f;
        // --- Tape delay: wow/flutter echoes reinforce the drift ---
        s.delayEnabled = 1;
        s.delay.type = 1;                 // Tape (owns this delay type)
        s.delay.mix = 0.25f;
        s.delay.feedback = 0.42f;
        s.delay.timeMs = 750.0f;
        s.delay.tapeSaturation = 0.5f;
        s.delay.tapeAge = 0.3f;
        // --- Hall reverb with slow modulation = big moving air ---
        s.reverbEnabled = 1;
        s.reverbType = 1;                 // Hall (owns Hall)
        s.reverb.size = 0.85f;
        s.reverb.mix = 0.4f;
        s.reverb.damping = 0.5f;
        s.reverb.modRateHz = 0.3f;
        s.reverb.modDepth = 0.12f;
        presets.push_back(std::move(p));
    }

    // "Spectral Ghost" - cold quantized-morph spectral drone (owns the smooth PhaseVocoder freeze wash)
    {
        PresetDef p;
        p.name = "Spectral Ghost";
        p.category = "Textures";
        auto& s = p.state;
        // --- Voice: shifted freeze morphing against a dark inharmonic additive tone ---
        s.oscA.type = 8;                  // Spectral Freeze
        s.oscA.spectralPitch = 5.0f;      // a fourth above the played note
        s.oscA.spectralTilt = -4.0f;      // darker
        s.oscA.spectralFormant = -3.0f;   // hollow, ghostly formant shift
        s.oscA.level = 0.7f;
        s.oscB.type = 4;                  // Additive
        s.oscB.additivePartials = 48;
        s.oscB.additiveTilt = -6.0f;      // very dark
        s.oscB.additiveInharm = 0.3f;     // bell-like detune
        s.oscB.level = 0.45f;
        s.mixer.mode = 1;                 // SpectralMorph (FFT interpolation A<->B)
        s.mixer.position = 0.5f;
        // --- Filter: SVF High-Pass hollows the low end -> cold air ---
        s.filter.type = 1;                // SVF HP
        s.filter.cutoffHz = 320.0f;
        s.filter.resonance = 4.09f;
        // --- Motion 1: a QUANTIZED LFO steps the spectral morph -> the ghost drifts in stages ---
        s.lfo1.rateHz = 0.12f;
        s.lfo1.shape = 1;                 // Triangle
        s.lfo1.depth = 0.7f;
        s.lfo1.sync = 0;
        s.lfo1Ext.quantizeSteps = 6;      // 6 discrete morph positions (owns quantizeSteps)
        setModSlot(s, 0, kSrcLFO1, kDstAllMorphPos, 0.6f, kCurveSCurve, 50.0f);
        // --- Motion 2: PitchFollower tilts the spectrum with played pitch ---
        s.pitchFollower.minHz = 60.0f; s.pitchFollower.maxHz = 1500.0f;
        s.pitchFollower.confidence = 0.5f; s.pitchFollower.speedMs = 120.0f;
        setModSlot(s, 1, kSrcPitchFollow, kDstAllSpecTilt, 0.4f, kCurveLinear, 60.0f);
        // --- Amp env: extremely slow, ghostly ---
        s.ampEnv.attackMs = 1600.0f;
        s.ampEnv.decayMs = 800.0f;
        s.ampEnv.sustain = 0.7f;
        s.ampEnv.releaseMs = 3200.0f;
        // --- Spectral distortion smears the harmonics further (thematic, spectral-domain dirt) ---
        s.distortion.type = 2;            // Spectral (owns this dirt type)
        s.distortion.drive = 0.3f;
        s.distortion.spectralMode = 1;
        s.distortion.spectralCurve = 3;
        s.distortion.spectralBits = 0.5f; // ~mid bit reduction
        s.distortion.mix = 0.28f;
        // --- Harmonizer (PhaseVocoder): a cold, SMOOTH spectral fifth + octave shimmer ---
        s.harmonizerEnabled = 1;
        s.harmonizer.pitchShiftMode = 2;  // PhaseVocoder (owns this mode) -> smooth freeze wash
        s.harmonizer.numVoices = 2;
        s.harmonizer.wetLevelDb = -10.0f;
        s.harmonizer.voiceInterval[0] = 7;  // +5th
        s.harmonizer.voiceInterval[1] = 12; // +octave
        s.harmonizer.voicePan[0] = -0.4f; s.harmonizer.voicePan[1] = 0.4f;
        // --- Spectral delay: frequency-blurred echo trails ---
        s.delayEnabled = 1;
        s.delay.type = 4;                 // Spectral (owns this delay type)
        s.delay.mix = 0.3f;
        s.delay.feedback = 0.5f;
        s.delay.timeMs = 600.0f;
        s.delay.spectralSpreadMs = 400.0f;
        s.delay.spectralTilt = -0.3f;
        // --- Modulated Hall reverb -> shimmering tail ---
        s.reverbEnabled = 1;
        s.reverbType = 1;                 // Hall
        s.reverb.size = 0.95f;
        s.reverb.mix = 0.5f;
        s.reverb.damping = 0.25f;
        s.reverb.modRateHz = 0.2f;
        s.reverb.modDepth = 0.15f;        // owns reverb modulation
        presets.push_back(std::move(p));
    }

    // "Granular Fog" - dense dual-particle fog through a breathing allpass
    {
        PresetDef p;
        p.name = "Granular Fog";
        p.category = "Textures";
        auto& s = p.state;
        // --- Voice: two particle layers -> a dense, wide fog ---
        s.oscA.type = 6;                  // Particle (long-grain bed)
        s.oscA.particleScatter = 4.0f;
        s.oscA.particleDensity = 52.0f;   // dense
        s.oscA.particleLifetime = 1600.0f;
        s.oscA.particleSpawnMode = 0;     // Regular -> smooth continuous bed
        s.oscA.particleEnvType = 3;       // Blackman (softest, 3=Blackman)
        s.oscA.particleDrift = 0.5f;
        s.oscA.level = 0.7f;
        s.oscB.type = 6;                  // Particle (short clustered sparkle)
        s.oscB.particleScatter = 9.0f;
        s.oscB.particleDensity = 18.0f;
        s.oscB.particleLifetime = 450.0f;
        s.oscB.particleSpawnMode = 2;     // Burst -> clustered grains on top (2=Burst)
        s.oscB.particleEnvType = 0;       // Hann
        s.oscB.particleDrift = 0.7f;
        s.oscB.level = 0.5f;
        s.mixer.position = 0.45f;
        // --- Filter: SVF ALLPASS -> phasey, diffuse smear with no tonal notch ---
        s.filter.type = 7;                // SVF Allpass (owns this rare filter)
        s.filter.cutoffHz = 900.0f;
        s.filter.resonance = 1.8f;        // allpass "swoosh" around 900 Hz
        // --- Motion 1: heavily-smoothed LFO morphs the two grain layers ---
        s.lfo1.rateHz = 0.06f;
        s.lfo1.shape = 5;                 // SmoothRandom
        s.lfo1.depth = 0.75f;
        s.lfo1.sync = 0;
        setModSlot(s, 0, kSrcLFO1, kDstAllMorphPos, 0.5f, kCurveSCurve, 90.0f); // big smoothMs
        // --- Motion 2: Envelope Follower opens the allpass with dynamics, scaled x4 ---
        s.envFollower.sensitivity = 0.7f; s.envFollower.attackMs = 60.0f; s.envFollower.releaseMs = 400.0f;
        setModSlot(s, 1, kSrcEnvFollower, kDstAllFltCut, 0.3f, kCurveExp, 40.0f);
        s.modMatrix.slots[1].scale = 4;   // x4 -> dramatic breathing sweep (owns scale x4)
        // --- Amp env: very slow fog roll-in ---
        s.ampEnv.attackMs = 1400.0f;
        s.ampEnv.decayMs = 500.0f;
        s.ampEnv.sustain = 0.9f;
        s.ampEnv.releaseMs = 3600.0f;
        // --- Granular distortion: grain-cloud saturation thickens the fog (thematic) ---
        s.distortion.type = 3;            // Granular (owns this dirt type)
        s.distortion.drive = 0.3f;
        s.distortion.grainSize = 0.4f;
        s.distortion.grainDensity = 0.5f;
        s.distortion.grainVariation = 0.4f;
        s.distortion.mix = 0.25f;
        // --- Granular delay scatters pitch-sprayed echoes ---
        s.delayEnabled = 1;
        s.delay.type = 3;                 // Granular (owns this delay type)
        s.delay.mix = 0.3f;
        s.delay.feedback = 0.4f;
        s.delay.timeMs = 500.0f;
        s.delay.granularSizeMs = 120.0f;
        s.delay.granularPitchSpray = 0.3f;
        s.delay.granularPosSpray = 0.4f;
        // --- Enormous diffuse Hall (distinct from Particle Cloud's plate) ---
        s.reverbEnabled = 1;
        s.reverbType = 1;                 // Hall
        s.reverb.size = 0.95f;
        s.reverb.mix = 0.5f;
        s.reverb.damping = 0.3f;
        s.reverb.diffusion = 0.92f;
        s.global.width = 2.0f;
        s.global.spread = 0.6f;
        presets.push_back(std::move(p));
    }

    // "Metal Resonance" - struck inharmonic bell through a swept comb
    {
        PresetDef p;
        p.name = "Metal Resonance";
        p.category = "Textures";
        auto& s = p.state;
        // --- Voice: inharmonic 128-partial additive tone, pinged by white noise ---
        s.oscA.type = 4;                  // Additive
        s.oscA.additivePartials = 128;    // full harmonic stack
        s.oscA.additiveInharm = 0.55f;    // strong bell/metal detune
        s.oscA.additiveTilt = -2.0f;
        s.oscA.level = 0.6f;
        s.oscB.type = 9;                  // Noise
        s.oscB.noiseColor = 0;            // White -> strike excitation
        s.oscB.level = 0.12f;
        s.mixer.position = 0.14f;         // mostly additive, whisper of noise
        // --- Filter: resonant Comb -> metallic ringing body ---
        s.filter.type = 6;                // Comb
        s.filter.cutoffHz = 700.0f;       // comb tuning
        s.filter.resonance = 8.84f;       // long ring, still stable
        s.filter.combDamping = 0.25f;
        // --- Percussive filter env sweeps the comb on each strike ---
        // (envAmount is PLAIN semitones, range -48..+48 -- NOT a Hz value)
        s.filter.envAmount = 30.0f;       // +30 st sweep = strong metallic "ping"
        s.filterEnv.attackMs = 2.0f;
        s.filterEnv.decayMs = 400.0f;
        s.filterEnv.sustain = 0.0f;
        s.filterEnv.releaseMs = 300.0f;
        s.filterEnv.decayCurve = 0.5f;    // snappy exp-ish fall
        // --- Transient source: each attack briefly punches the comb up (the "struck" lever) ---
        s.transient.sensitivity = 0.8f; s.transient.attackMs = 2.0f; s.transient.decayMs = 80.0f;
        setModSlot(s, 0, kSrcTransient, kDstAllFltCut, 0.6f, kCurveExp, 10.0f);
        // --- Percussive amp env: fast strike, long metallic ring-out ---
        s.ampEnv.attackMs = 3.0f;
        s.ampEnv.decayMs = 2800.0f;
        s.ampEnv.sustain = 0.28f;
        s.ampEnv.releaseMs = 2200.0f;
        s.ampEnv.decayCurve = 0.4f;
        // --- Ring modulator: note-tracked sidebands = clangorous metal ---
        s.distortion.type = 6;            // RingModulator (owns this dirt type)
        s.distortion.drive = 0.2f;
        s.distortion.ringFreqMode = 1;    // NoteTrack -> stays musical across the keyboard
        s.distortion.ringRatio = 0.28f;   // normalized -> ratio ~4.7 (slightly inharmonic)
        s.distortion.ringWaveform = 0;    // Sine sidebands
        s.distortion.mix = 0.4f;          // clear metallic edge, still tonal
        // --- Short digital delay = tight metallic slapback ---
        s.delayEnabled = 1;
        s.delay.type = 0;                 // Digital
        s.delay.mix = 0.2f;
        s.delay.feedback = 0.35f;
        s.delay.timeMs = 280.0f;
        s.delay.sync = 0;                 // free time for the slap
        // --- Small plate reverb keeps it a struck object, not a wash ---
        s.reverbEnabled = 1;
        s.reverbType = 0;                 // Plate
        s.reverb.size = 0.55f;
        s.reverb.mix = 0.3f;
        s.reverb.damping = 0.45f;
        presets.push_back(std::move(p));
    }

    // ==================== RHYTHMIC Category (5 presets) ====================

    // "Trance Gate Pad" - lush gated pad where the gate also morphs the timbre
    {
        PresetDef p;
        p.name = "Trance Gate Pad";
        p.category = "Rhythmic";
        auto& s = p.state;
        // --- Saw vs SQUARE blended through the (bank-unique) SpectralMorph mixer, so
        //     morph position is a real FFT timbre morph, not just a crossfade.
        s.oscA.type = 0;            // PolyBLEP
        s.oscA.waveform = 1;       // Saw
        s.oscA.level = 0.7f;
        s.oscB.type = 0;
        s.oscB.waveform = 2;       // Square -> gives the morph something to travel to
        s.oscB.fineCents = 9.0f;   // analog beating
        s.oscB.level = 0.65f;
        s.mixer.mode = 1;          // SpectralMorph (unused elsewhere in the bank)
        s.mixer.position = 0.4f;
        // --- Ladder LP with a CORRECTED audible filter-env swell (semitones, not Hz)
        s.filter.type = 4;         // Ladder LP
        s.filter.cutoffHz = 2600.0f;
        s.filter.resonance = 4.32f;
        s.filter.ladderSlope = 3;  // 18 dB/oct, softer than full 24
        s.filter.envAmount = 18.0f; // +18 st bloom (was the broken 4000 idiom)
        s.filterEnv.attackMs = 350.0f;
        s.filterEnv.decayMs = 1400.0f;
        s.filterEnv.sustain = 0.35f;
        s.filterEnv.releaseMs = 900.0f;
        // Amp: slow pad swell
        s.ampEnv.attackMs = 120.0f;
        s.ampEnv.decayMs = 500.0f;
        s.ampEnv.sustain = 0.85f;
        s.ampEnv.releaseMs = 900.0f;
        // --- 16-step trance gate, soft edges so it pulses rather than clicks
        s.tranceGate.enabled = 1;
        s.tranceGate.numSteps = 16;
        s.tranceGate.tempoSync = 1;
        s.tranceGate.noteValue = kNote1_16;
        s.tranceGate.depth = 0.9f;
        s.tranceGate.attackMs = 6.0f;
        s.tranceGate.releaseMs = 35.0f;
        float tg[32]{};
        tg[0]=1.0f; tg[1]=0.6f; tg[2]=0.0f; tg[3]=1.0f;
        tg[4]=0.8f; tg[5]=0.0f; tg[6]=1.0f; tg[7]=0.4f;
        tg[8]=1.0f; tg[9]=0.7f; tg[10]=0.0f; tg[11]=1.0f;
        tg[12]=0.9f; tg[13]=0.0f; tg[14]=0.5f; tg[15]=1.0f;
        for (int i = 0; i < 32; ++i) s.tranceGate.stepLevels[i] = tg[i];
        // --- Dual TEMPO-SYNCED LFOs: fast synced saw chops cutoff with the gate,
        //     slow synced sine drifts the spectral morph.
        s.lfo1.rateHz = 4.0f; s.lfo1.shape = 2; // Saw
        s.lfo1.depth = 0.6f;  s.lfo1.sync = 1;
        s.lfo1Ext.noteValue = kNote1_8;
        setModSlot(s, 0, kSrcLFO1, kDstAllFltCut, 0.35f, kCurveSCurve);
        s.lfo2.rateHz = 0.5f; s.lfo2.shape = 0; // Sine
        s.lfo2.depth = 0.5f;  s.lfo2.sync = 1;
        s.lfo2Ext.noteValue = 19; // 1/1 whole-note, very slow drift
        setModSlot(s, 1, kSrcLFO2, kDstAllMorphPos, 0.30f, kCurveSCurve);
        // --- Signature move: the GATE itself drives the spectral morph, so every
        //     chop also lands on a different timbre (Gate voice-source -> MorphPos).
        setVoiceRoute(s, 0, kVSrcGate, kVDstMorphPos, 0.6f);
        setVoiceRoute(s, 1, kVSrcVelocity, kVDstFltCut, 0.25f);
        // --- Lush wrapper: gentle chorus + wide HALL (contrasts the strings' Plate)
        s.modulationType = 3;      // Chorus
        s.chorus.rateHz = 0.4f; s.chorus.depth = 0.4f; s.chorus.voices = 3;
        s.chorus.mix = 0.35f;
        s.reverbEnabled = 1;
        s.reverbType = 1;          // Hall
        s.reverb.size = 0.7f; s.reverb.mix = 0.28f; s.reverb.damping = 0.4f;
        s.global.width = 1.5f;
        s.global.spread = 0.35f;
        s.global.polyphony = 12;
        presets.push_back(std::move(p));
    }

    // "Sidechain Sync" - mono hard-sync lead ducked by its own transient detector
    {
        PresetDef p;
        p.name = "Sidechain Sync";
        p.category = "Rhythmic";
        auto& s = p.state;
        // --- Hard-sync engine on OSC A, quiet sub saw on OSC B for weight
        s.oscA.type = 3;           // Sync
        s.oscA.waveform = 1;
        s.oscA.syncRatio = 2.5f;   // classic sync-sweep zone
        s.oscA.syncWaveform = 1;   // Saw slave = aggressive
        s.oscA.syncMode = 0;       // Hard
        s.oscA.syncAmount = 1.0f;
        s.oscA.level = 0.9f;
        s.oscB.type = 0;
        s.oscB.waveform = 1;
        s.oscB.tuneSemitones = -12.0f; // sub for body
        s.oscB.level = 0.35f;
        s.mixer.position = 0.3f;   // favour the sync osc
        // --- SVF LP, bright, with a CORRECTED filter-env snap on each note
        s.filter.type = 0;         // SVF LP
        s.filter.cutoffHz = 3500.0f;
        s.filter.resonance = 4.09f;
        s.filter.svfDrive = 4.0f;  // a little edge
        s.filter.envAmount = 28.0f; // +28 st zap (corrected semitone amount)
        s.filterEnv.attackMs = 2.0f;
        s.filterEnv.decayMs = 180.0f;
        s.filterEnv.sustain = 0.25f;
        s.filterEnv.releaseMs = 120.0f;
        s.ampEnv.attackMs = 4.0f;
        s.ampEnv.decayMs = 250.0f;
        s.ampEnv.sustain = 0.8f;
        s.ampEnv.releaseMs = 160.0f;
        // --- MONO with legato glide: portamento slurs the sync sweeps
        s.global.voiceMode = 1;    // Mono
        s.monoMode.priority = 0;   // Last
        s.monoMode.legato = 1;
        s.monoMode.portamentoTimeMs = 55.0f;
        s.monoMode.portaMode = 1;  // glide on legato only
        // --- 4-step sidechain-shaped pump gate (slow rise = the pumped swell)
        s.tranceGate.enabled = 1;
        s.tranceGate.numSteps = 4;
        s.tranceGate.tempoSync = 1;
        s.tranceGate.noteValue = kNote1_16;
        s.tranceGate.depth = 1.0f;
        s.tranceGate.attackMs = 20.0f;
        s.tranceGate.releaseMs = 60.0f;
        s.tranceGate.stepLevels[0] = 0.0f; s.tranceGate.stepLevels[1] = 0.6f;
        s.tranceGate.stepLevels[2] = 1.0f; s.tranceGate.stepLevels[3] = 0.85f;
        // --- The REAL sidechain: transient detector ducks master volume on every
        //     attack (negative amount = duck), so it pumps like a keyed compressor.
        s.transient.sensitivity = 0.7f;
        s.transient.attackMs = 1.0f;
        s.transient.decayMs = 140.0f;
        setModSlot(s, 0, kSrcTransient, kDstMasterVol, -0.55f, kCurveExp);
        // Harder hits duck the gate deeper for playing dynamics (-> GateDepth)
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstGateDepth, 0.4f);
        // --- Metallic bite: note-tracked ring modulator, blended so pitch survives
        s.distortion.type = 6;     // RingModulator
        s.distortion.drive = 0.4f;
        s.distortion.mix = 0.3f;
        s.distortion.ringFreqMode = 1;   // NoteTrack (stays musical)
        s.distortion.ringRatio = 0.1111f; // ~2.0 ratio (normalized encoding)
        s.distortion.ringWaveform = 0;   // Sine
        // --- Phaser sweep + synced PingPong echo
        s.modulationType = 1;      // Phaser
        s.phaser.rateHz = 0.3f; s.phaser.depth = 0.6f; s.phaser.stages = 2; // 6-stage
        s.phaser.feedback = 0.4f; s.phaser.mix = 0.4f;
        s.delayEnabled = 1;
        s.delay.type = 2;          // PingPong
        s.delay.sync = 1; s.delay.noteValue = kNote1_8D;
        s.delay.feedback = 0.35f; s.delay.mix = 0.22f;
        s.delay.pingPongWidth = 140.0f;
        s.reverbEnabled = 1;
        s.reverbType = 0;          // Plate
        s.reverb.size = 0.4f; s.reverb.mix = 0.15f;
        presets.push_back(std::move(p));
    }

    // "Stutter Bass" - octave-down mono bass, machine-gun retrigger stutter
    {
        PresetDef p;
        p.name = "Stutter Bass";
        p.category = "Rhythmic";
        auto& s = p.state;
        // --- Octave-down saw + detuned square, mono, through a driven ladder
        s.oscA.type = 0;
        s.oscA.waveform = 1;       // Saw
        s.oscA.tuneSemitones = -12.0f;
        s.oscA.level = 0.85f;
        s.oscB.type = 0;
        s.oscB.waveform = 2;       // Square
        s.oscB.tuneSemitones = -12.0f;
        s.oscB.fineCents = -4.0f;  // slight beat for thickness
        s.oscB.level = 0.5f;
        s.mixer.position = 0.4f;
        s.filter.type = 4;         // Ladder LP
        s.filter.cutoffHz = 900.0f;
        s.filter.resonance = 5.79f;
        s.filter.ladderSlope = 4;  // 24 dB/oct
        s.filter.ladderDrive = 7.0f; // grit (osc levels moderated to compensate)
        // CORRECTED filter-env: a hard +30 st thump on every hit
        s.filter.envAmount = 30.0f;
        s.filterEnv.attackMs = 1.0f;
        s.filterEnv.decayMs = 90.0f;
        s.filterEnv.sustain = 0.15f;
        s.filterEnv.releaseMs = 70.0f;
        s.ampEnv.attackMs = 1.0f;
        s.ampEnv.decayMs = 140.0f;
        s.ampEnv.sustain = 0.7f;
        s.ampEnv.releaseMs = 70.0f;
        s.global.voiceMode = 1;    // Mono
        // --- Machine-gun stutter: fast gate + retriggerDepth re-fires the envelope
        //     inside each step for sub-hit rolls (this preset owns retriggerDepth).
        s.tranceGate.enabled = 1;
        s.tranceGate.numSteps = 16;
        s.tranceGate.tempoSync = 1;
        s.tranceGate.noteValue = kNote1_16;
        s.tranceGate.depth = 1.0f;
        s.tranceGate.attackMs = 0.5f;
        s.tranceGate.releaseMs = 4.0f;
        s.tranceGate.retriggerDepth = 0.85f; // the machine-gun character
        float bg[32]{};
        bg[0]=1;bg[1]=1;bg[2]=0;bg[3]=1; bg[4]=0;bg[5]=1;bg[6]=1;bg[7]=0;
        bg[8]=1;bg[9]=0;bg[10]=1;bg[11]=1; bg[12]=0;bg[13]=1;bg[14]=0;bg[15]=1;
        for (int i = 0; i < 32; ++i) s.tranceGate.stepLevels[i] = bg[i];
        // --- Tape saturation for warmth + bite (this preset owns TapeSaturator)
        s.distortion.type = 5;     // TapeSaturator
        s.distortion.drive = 0.5f;
        s.distortion.mix = 0.7f;
        s.distortion.tapeModel = 1;
        s.distortion.tapeSaturation = 0.6f;
        s.distortion.tapeBias = 0.55f;
        // --- The gate output pumps distortion drive so stutters spit harder
        //     (Gate voice-source -> DistDrive).
        setVoiceRoute(s, 0, kVSrcGate, kVDstDistDrive, 0.4f);
        setVoiceRoute(s, 1, kVSrcVelocity, kVDstFltCut, 0.3f);
        // --- Subtle synced DIGITAL delay for a dub tail (kept low = tight low end)
        s.delayEnabled = 1;
        s.delay.type = 0;          // Digital
        s.delay.sync = 1; s.delay.noteValue = kNote1_16;
        s.delay.feedback = 0.3f; s.delay.mix = 0.15f;
        s.delay.digitalWidth = 130.0f;
        presets.push_back(std::move(p));
    }

    // "Gate Strings" - additive-shimmer string section, gated, flanged, harmonized
    {
        PresetDef p;
        p.name = "Gate Strings";
        p.category = "Rhythmic";
        auto& s = p.state;
        // --- Bowed saw OSC A + an ADDITIVE shimmer OSC B for airy upper harmonics
        s.oscA.type = 0;
        s.oscA.waveform = 1;       // Saw
        s.oscA.level = 0.6f;
        s.oscB.type = 4;           // Additive
        s.oscB.additivePartials = 24;   // rich but not buzzy
        s.oscB.additiveTilt = -6.0f;    // roll off the top = silky
        s.oscB.additiveInharm = 0.04f;  // faint string inharmonicity
        s.oscB.fineCents = 6.0f;
        s.oscB.level = 0.5f;
        s.mixer.position = 0.45f;
        s.filter.type = 0;         // SVF LP
        s.filter.cutoffHz = 3200.0f;
        s.filter.resonance = 2.73f;
        // CORRECTED filter-env: a gentle bow-swell opening as the note sustains
        s.filter.envAmount = 14.0f;
        s.filterEnv.attackMs = 400.0f;
        s.filterEnv.decayMs = 900.0f;
        s.filterEnv.sustain = 0.6f;
        s.filterEnv.releaseMs = 800.0f;
        s.ampEnv.attackMs = 220.0f; // slow bow attack
        s.ampEnv.decayMs = 600.0f;
        s.ampEnv.sustain = 0.8f;
        s.ampEnv.releaseMs = 900.0f;
        // --- 8-step gentle rhythmic gate (1/8) with alternating dynamics
        s.tranceGate.enabled = 1;
        s.tranceGate.numSteps = 8;
        s.tranceGate.tempoSync = 1;
        s.tranceGate.noteValue = kNote1_8;
        s.tranceGate.depth = 0.75f;
        s.tranceGate.attackMs = 12.0f;
        s.tranceGate.releaseMs = 55.0f;
        s.tranceGate.stepLevels[0] = 1.0f; s.tranceGate.stepLevels[1] = 0.5f;
        s.tranceGate.stepLevels[2] = 0.85f; s.tranceGate.stepLevels[3] = 0.35f;
        s.tranceGate.stepLevels[4] = 1.0f; s.tranceGate.stepLevels[5] = 0.45f;
        s.tranceGate.stepLevels[6] = 0.9f; s.tranceGate.stepLevels[7] = 0.3f;
        // --- Own mod identity: slow free-run sine crossfades saw<->additive shimmer
        s.lfo1.rateHz = 0.2f; s.lfo1.shape = 0; // Sine
        s.lfo1.depth = 0.5f;  s.lfo1.sync = 0;  // free-run breathing
        setModSlot(s, 0, kSrcLFO1, kDstAllMorphPos, 0.30f, kCurveSCurve);
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstMorphPos, 0.30f); // harder bow = more shimmer
        // --- Ensemble: a Flanger widens it into a string-machine (owns Flanger)
        s.modulationType = 2;      // Flanger
        s.flanger.rateHz = 0.25f; s.flanger.depth = 0.5f; s.flanger.feedback = 0.2f;
        s.flanger.mix = 0.35f; s.flanger.stereoSpread = 120.0f;
        // --- Harmonizer stacks a diatonic 5th + octave for a full section
        s.harmonizerEnabled = 1;
        s.harmonizer.harmonyMode = 1;   // Scalic
        s.harmonizer.numVoices = 2;
        s.harmonizer.wetLevelDb = -8.0f;
        s.harmonizer.voiceInterval[0] = 4; // +5th (scale degrees)
        s.harmonizer.voiceInterval[1] = 7; // +octave
        s.harmonizer.voicePan[0] = -0.4f;
        s.harmonizer.voicePan[1] = 0.4f;
        // --- Synced TAPE delay + PLATE reverb (contrast to the Pad's Hall)
        s.delayEnabled = 1;
        s.delay.type = 1;          // Tape
        s.delay.sync = 1; s.delay.noteValue = kNote1_8;
        s.delay.feedback = 0.28f; s.delay.mix = 0.2f;
        s.delay.tapeSaturation = 0.4f;
        s.reverbEnabled = 1;
        s.reverbType = 0;          // Plate
        s.reverb.size = 0.55f; s.reverb.mix = 0.25f;
        s.global.width = 1.4f;
        s.global.spread = 0.25f;
        s.global.polyphony = 12;
        presets.push_back(std::move(p));
    }

    // "Choppy Texture" - granular particle + noise cloud, Euclidean-gated, chaos-driven
    {
        PresetDef p;
        p.name = "Choppy Texture";
        p.category = "Rhythmic";
        auto& s = p.state;
        // --- Granular particle cloud + pink noise
        s.oscA.type = 6;           // Particle
        s.oscA.particleScatter = 5.0f;
        s.oscA.particleDensity = 28.0f;
        s.oscA.particleLifetime = 220.0f;
        s.oscA.particleSpawnMode = 1; // Random
        s.oscA.particleEnvType = 3;   // Blackman grains = smoother cloud
        s.oscA.particleDrift = 0.3f;  // slow pitch wander
        s.oscA.level = 0.7f;
        s.oscB.type = 9;           // Noise
        s.oscB.noiseColor = 1;     // Pink
        s.oscB.level = 0.18f;
        s.mixer.position = 0.25f;
        s.filter.type = 0;         // SVF LP
        s.filter.cutoffHz = 4500.0f;
        s.filter.resonance = 3.53f;
        s.ampEnv.attackMs = 25.0f;
        s.ampEnv.decayMs = 400.0f;
        s.ampEnv.sustain = 0.7f;
        s.ampEnv.releaseMs = 600.0f;
        // --- EUCLIDEAN gate E(5,8): 5 hits spread over 8 sixteenths
        s.tranceGate.enabled = 1;
        s.tranceGate.numSteps = 8;
        s.tranceGate.tempoSync = 1;
        s.tranceGate.noteValue = kNote1_16;
        s.tranceGate.depth = 0.9f;
        s.tranceGate.attackMs = 3.0f;
        s.tranceGate.releaseMs = 18.0f;
        s.tranceGate.euclideanEnabled = 1;
        s.tranceGate.euclideanHits = 5;   // E(5,8)
        s.tranceGate.euclideanRotation = 1;
        // --- chaosMod gurgles the resonance for an ever-shifting texture
        s.chaosMod.rateHz = 0.8f;
        s.chaosMod.type = 1;       // Rossler
        s.chaosMod.depth = 0.6f;   // must raise depth or the source is silent
        s.chaosMod.sync = 0;
        setModSlot(s, 0, kSrcChaos, kDstAllResonance, 0.4f, kCurveLinear);
        // A slow smoothed random nudges the filter for extra life
        s.random.rateHz = 0.5f; s.random.smoothness = 0.8f;
        setModSlot(s, 1, kSrcRandom, kDstAllFltCut, 0.25f, kCurveSCurve);
        // --- Spectral bit-crush smears the grains into a metallic haze (owns Spectral)
        //     spectralBits is NORMALIZED 0-1 (maps to 1-16 bits in the voice); use
        //     0.333f = (6-1)/15 for ~6 bits, and SpectralBitcrush mode (3) for a true crush.
        s.distortion.type = 2;     // Spectral
        s.distortion.drive = 0.4f;
        s.distortion.mix = 0.35f;
        s.distortion.spectralMode = 3;    // SpectralBitcrush (true bit-crush)
        s.distortion.spectralBits = 0.333f; // ~6 bits (normalized: (6-1)/15)
        // --- Granular delay + big diffuse HALL wash
        s.delayEnabled = 1;
        s.delay.type = 3;          // Granular
        s.delay.sync = 1; s.delay.noteValue = kNote1_8;
        s.delay.feedback = 0.4f; s.delay.mix = 0.3f;
        s.delay.granularSizeMs = 120.0f;
        s.delay.granularDensity = 18.0f;
        s.delay.granularPitchSpray = 0.3f;
        s.delay.granularPosSpray = 0.4f;
        s.reverbEnabled = 1;
        s.reverbType = 1;          // Hall
        s.reverb.size = 0.8f; s.reverb.mix = 0.4f; s.reverb.diffusion = 0.9f;
        s.global.width = 1.4f;
        presets.push_back(std::move(p));
    }

    // ==================== EXPERIMENTAL Category (5 presets) ====================

    // "Chaos Machine" - Dual strange-attractor drone, chaos-waveshaped, four-source mod web
    {
        PresetDef p;
        p.name = "Chaos Machine";
        p.category = "Experimental";
        auto& s = p.state;
        // --- Dual chaos engines: Lorenz X (bright, ordered) vs Henon Z (grainy, folded) ---
        s.oscA.type = 5; // Chaos
        s.oscA.chaosAttractor = 0; // Lorenz
        s.oscA.chaosAmount = 0.8f;
        s.oscA.chaosCoupling = 0.6f; // strong cross-axis coupling = unstable timbre
        s.oscA.chaosOutput = 0; // X axis
        s.oscA.level = 0.7f;
        s.oscB.type = 5; // Chaos
        s.oscB.chaosAttractor = 3; // Henon (index 3)
        s.oscB.chaosAmount = 0.5f;
        s.oscB.chaosCoupling = 0.3f;
        s.oscB.chaosOutput = 2; // Z axis - different spectral fingerprint than A
        s.oscB.tuneSemitones = -12.0f; // drop B an octave for low turbulent weight
        s.oscB.level = 0.5f;
        s.mixer.position = 0.5f; // equal blend of the two attractors
        // --- SVF band-pass keeps only the resonant core of the noise ---
        s.filter.type = 2; // SVF BP
        s.filter.cutoffHz = 1400.0f; // low base so the upward chaos/env sweep is audible
        s.filter.resonance = 5.79f;
        s.filter.envAmount = 18.0f; // FIXED semitone value (was the class of 4000-Hz bug); +18 st = clear sweep
        s.filterEnv.attackMs = 4.0f; s.filterEnv.decayMs = 700.0f;
        s.filterEnv.sustain = 0.2f; s.filterEnv.releaseMs = 500.0f;
        // --- Chaos waveshaper: adds a SECOND layer of nonlinearity on top of the osc chaos ---
        s.distortion.type = 1; // Chaos Waveshaper
        s.distortion.drive = 0.5f;
        s.distortion.chaosModel = 0; // Lorenz
        s.distortion.chaosSpeed = 0.7f;
        s.distortion.chaosCoupling = 0.4f;
        s.distortion.mix = 0.85f;
        s.ampEnv.attackMs = 12.0f; s.ampEnv.decayMs = 500.0f;
        s.ampEnv.sustain = 0.6f; s.ampEnv.releaseMs = 900.0f;
        // === UNIQUE MOD WEB: chaos->cutoff (x2 scaled), rungler->morph, S&H->res (stepped), random->tilt ===
        s.chaosMod.rateHz = 2.0f; s.chaosMod.type = 1; s.chaosMod.depth = 0.7f; // Rossler modulator
        setModSlot(s, 0, kSrcChaos, kDstAllFltCut, 0.5f, kCurveExp);
        s.modMatrix.slots[0].scale = 3; // SCALE AXIS x2: pushes the cutoff sweep beyond +-1 for wild motion
        s.rungler.osc1FreqHz = 4.0f; s.rungler.osc2FreqHz = 7.0f;
        s.rungler.depth = 0.5f; s.rungler.bits = 4; s.rungler.filter = 0.25f;
        setModSlot(s, 1, kSrcRungler, kDstAllMorphPos, 0.5f, kCurveSCurve); // smoothstep morph steps
        s.sampleHold.rateHz = 3.0f; s.sampleHold.slewMs = 30.0f;
        setModSlot(s, 2, kSrcSampleHold, kDstAllResonance, 0.3f, kCurveStepped, 15.0f); // smoothMs de-zippers the S&H
        s.random.rateHz = 1.5f; s.random.smoothness = 0.3f;
        setModSlot(s, 3, kSrcRandom, kDstAllSpecTilt, 0.4f);
        // BYPASS AXIS + LFO square shape: an alternate square-wave gate the user can toggle on
        s.lfo1.rateHz = 4.0f; s.lfo1.shape = 3; s.lfo1.depth = 0.6f; s.lfo1.sync = 0; // Square
        setModSlot(s, 4, kSrcLFO1, kDstAllFltCut, 0.4f);
        s.modMatrix.slots[4].bypass = 1; // wired but BYPASSED by default
        // Per-voice: velocity bites the waveshaper, mod-env pushes morph on each attack
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstDistDrive, 0.5f);
        setVoiceRoute(s, 1, kVSrcEnv3, kVDstMorphPos, 0.4f);
        s.modEnv.attackMs = 5.0f; s.modEnv.decayMs = 800.0f;
        s.modEnv.sustain = 0.0f; s.modEnv.releaseMs = 400.0f;
        // === PERFORMANCE SETTINGS AXES (rarely touched anywhere in the bank) ===
        s.settings.velocityCurve = 2; // Hard - rewards hard playing on the chaos drive
        s.settings.voiceAllocMode = 3; // HighNote priority
        s.settings.voiceStealMode = 1; // Soft steal avoids clicks on this sustaining drone
        s.settings.pitchBendRangeSemitones = 12.0f; // whole-octave bends for dive-bomb chaos
        s.global.polyphony = 6; // dense enough, CPU-sane for chaos engines
        s.global.masterGain = 0.9f; // headroom for the waveshaper
        // --- Tape echo (+splice) then hall: analog smear, not the default digital slap ---
        s.delayEnabled = 1;
        s.delay.type = 1; // Tape
        s.delay.timeMs = 375.0f; s.delay.feedback = 0.45f; s.delay.mix = 0.3f;
        s.delay.tapeSaturation = 0.6f; s.delay.tapeWear = 0.3f; s.delay.tapeAge = 0.4f;
        s.delay.tapeSpliceEnabled = 1; s.delay.tapeSpliceIntensity = 0.4f; // dropouts add instability
        s.reverbEnabled = 1;
        s.reverb.size = 0.6f; s.reverb.mix = 0.3f;
        presets.push_back(std::move(p));
    }

    // "Vox Automata" - Ring-modulated vowel choir, macro-morphed, aftertouch-expressive
    {
        PresetDef p;
        p.name = "Vox Automata";
        p.category = "Experimental";
        auto& s = p.state;
        // --- Two formant oscillators voiced on different vowels for a chord-of-vowels blend ---
        s.oscA.type = 7; // Formant
        s.oscA.formantVowel = 0; // A (open, bright)
        s.oscA.formantMorph = 0.0f;
        s.oscA.level = 0.8f;
        s.oscB.type = 7; // Formant
        s.oscB.formantVowel = 3; // O (rounded, dark)
        s.oscB.formantMorph = 2.0f; // starts mid-morph toward I/U
        s.oscB.fineCents = 6.0f; // slight beat between the two throats
        s.oscB.level = 0.6f;
        s.mixer.position = 0.5f;
        // --- Formant FILTER on top with a shifted gender for a deeper 'chest' voice ---
        s.filter.type = 5; // Formant
        s.filter.formantMorph = 0.5f;
        s.filter.formantGender = -0.4f; // FORMANTGENDER coverage: shift toward male/monster
        s.filter.cutoffHz = 5000.0f;
        s.filter.resonance = 0.71f;     // inert for Formant, but keeps the bank off the Q floor
        // --- Ring modulator, NOTE-TRACKED so the metallic edge stays musical (classic robot voice) ---
        s.distortion.type = 6; // Ring Modulator
        s.distortion.drive = 0.3f;
        s.distortion.character = 0.5f;
        s.distortion.mix = 0.35f; // subtle - vowels stay intelligible
        s.distortion.ringFreqMode = 1; // NoteTrack - carrier follows the played pitch
        s.distortion.ringRatio = 0.1111f; // normalized ~2.0 ratio
        s.distortion.ringWaveform = 0; // Sine carrier
        s.distortion.ringStereoSpread = 0.4f;
        s.ampEnv.attackMs = 50.0f; s.ampEnv.decayMs = 400.0f;
        s.ampEnv.sustain = 0.75f; s.ampEnv.releaseMs = 600.0f;
        // === MOD WEB ===
        // LFO1 (triangle, free) slowly sweeps the vowel morph = the 'wow' talking motion
        s.lfo1.rateHz = 0.2f; s.lfo1.shape = 1; s.lfo1.depth = 1.0f; s.lfo1.sync = 0; // Triangle
        setModSlot(s, 0, kSrcLFO1, kDstAllMorphPos, 0.6f, kCurveSCurve); // smoothstep = natural glide
        // LFO2 (sine, very slow) breathes the filter cutoff
        s.lfo2.rateHz = 0.08f; s.lfo2.shape = 0; s.lfo2.depth = 0.5f; s.lfo2.sync = 0; // Sine
        setModSlot(s, 1, kSrcLFO2, kDstAllFltCut, 0.3f);
        // PITCH FOLLOWER -> spectral tilt: higher notes get brighter formants automatically
        s.pitchFollower.minHz = 80.0f; s.pitchFollower.maxHz = 1500.0f;
        s.pitchFollower.confidence = 0.5f; s.pitchFollower.speedMs = 40.0f;
        setModSlot(s, 2, kSrcPitchFollow, kDstAllSpecTilt, 0.35f);
        // Random adds gentle spectral wander (smoothed so it is a drift, not a jitter)
        s.random.rateHz = 2.0f; s.random.smoothness = 0.6f;
        setModSlot(s, 3, kSrcRandom, kDstAllSpecTilt, 0.2f, kCurveLinear, 60.0f); // smoothMs axis
        // === ONE MACRO, THREE DESTINATIONS: a single 'vowel-open' knob morphs cutoff+vowel+res ===
        s.macros.values[0] = 0.4f; // resting position
        setModSlot(s, 4, kSrcMacro1, kDstGlobalFltCut, 0.5f);
        setModSlot(s, 5, kSrcMacro1, kDstAllMorphPos, 0.4f);
        setModSlot(s, 6, kSrcMacro1, kDstAllResonance, 0.3f); // multi-routed one-knob morph
        // Per-voice EXPRESSION routes (nearly unused across the whole bank)
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.3f);
        setVoiceRoute(s, 1, kVSrcAftertouch, kVDstMorphPos, 0.5f); // lean on the key = push the vowel
        setVoiceRoute(s, 2, kVSrcKeyTrack, kVDstSpecTilt, 0.3f);   // keyboard brightens the top
        // === SCALIC PHASE-VOCODER HARMONIZER: turns the robot into a 3-part choir ===
        s.harmonizerEnabled = 1;
        s.harmonizer.harmonyMode = 1; // Scalic
        s.harmonizer.key = 0; s.harmonizer.scale = 0; // C Major
        s.harmonizer.pitchShiftMode = 2; // PhaseVocoder (formant-smearing, choir-like)
        s.harmonizer.formantPreserve = 1;
        s.harmonizer.numVoices = 3;
        s.harmonizer.dryLevelDb = 0.0f; s.harmonizer.wetLevelDb = -9.0f;
        s.harmonizer.voiceInterval[0] = 2; s.harmonizer.voicePan[0] = -0.5f; s.harmonizer.voiceDetuneCents[0] = -4.0f; // 3rd
        s.harmonizer.voiceInterval[1] = 4; s.harmonizer.voicePan[1] = 0.5f;  s.harmonizer.voiceDetuneCents[1] = 5.0f;  // 5th
        s.harmonizer.voiceInterval[2] = 7; s.harmonizer.voicePan[2] = 0.0f;  s.harmonizer.voiceDelayMs[2] = 12.0f;    // octave, delayed
        // --- Chorus (not the reflexive reverb-only wrapper) then a wide Hall ---
        s.modulationType = 3; // Chorus
        s.chorus.rateHz = 0.4f; s.chorus.depth = 0.5f; s.chorus.voices = 3; s.chorus.mix = 0.35f;
        s.reverbEnabled = 1;
        s.reverbType = 1; // Hall (not Plate)
        s.reverb.size = 0.7f; s.reverb.mix = 0.28f;
        s.global.masterGain = 0.85f; // headroom for ring-mod + harmonizer stack
        presets.push_back(std::move(p));
    }

    // "Rusted Circuit" - Dynamic bitcrush + stepped auto-wah, transient-snapped
    {
        PresetDef p;
        p.name = "Rusted Circuit";
        p.category = "Experimental";
        auto& s = p.state;
        // Saw + square a fifth up = a fat, hollow lo-fi source
        s.oscA.type = 0; // PolyBLEP
        s.oscA.waveform = 1; // Saw
        s.oscA.level = 0.8f;
        s.oscB.type = 0;
        s.oscB.waveform = 2; // Square
        s.oscB.tuneSemitones = 7.0f; // fifth
        s.oscB.pulseWidth = 0.35f; // thinner square = more nasal grit
        s.oscB.level = 0.5f;
        s.mixer.position = 0.4f;
        // --- ENV-FILTER (auto-wah): the filter itself follows dynamics, base freq stepped by S&H ---
        s.filter.type = 11; // Env Filter
        s.filter.cutoffHz = 800.0f; // low base so the wah has room to open
        s.filter.resonance = 6.35f;
        s.filter.envSubType = 1; // BP response = vocal wah
        s.filter.envSensitivity = 8.0f; // dB - reacts to input level
        s.filter.envDepth = 0.8f;
        s.filter.envAttack = 5.0f; s.filter.envRelease = 130.0f;
        s.filter.envDirection = 0; // Up (opens on attack)
        // --- SPECTRAL BITCRUSH: the core lo-fi destruction ---
        s.distortion.type = 2; // Spectral
        s.distortion.drive = 0.6f;
        s.distortion.spectralMode = 3; // SpectralBitcrush
        s.distortion.spectralCurve = 8; // BitReduce
        s.distortion.spectralBits = 0.3f; // ~5 bits
        s.distortion.mix = 0.9f;
        s.ampEnv.attackMs = 3.0f; s.ampEnv.decayMs = 320.0f;
        s.ampEnv.sustain = 0.6f; s.ampEnv.releaseMs = 220.0f;
        // === ANIMATION (the fix for its former static-ness) ===
        // S&H steps the auto-wah base frequency in hard stair-steps = rhythmic timbral clank
        s.sampleHold.rateHz = 7.0f; s.sampleHold.slewMs = 0.0f; // zero slew = hard steps
        setModSlot(s, 0, kSrcSampleHold, kDstAllFltCut, 0.5f, kCurveStepped);
        // TRANSIENT detector snaps resonance up on each note attack = percussive spit
        s.transient.sensitivity = 0.6f; s.transient.attackMs = 2.0f; s.transient.decayMs = 60.0f;
        setModSlot(s, 1, kSrcTransient, kDstAllResonance, 0.4f, kCurveExp);
        // Square LFO gates the spectral tilt on/off = a lo-fi tremolo of brightness
        s.lfo1.rateHz = 6.0f; s.lfo1.shape = 3; s.lfo1.depth = 0.6f; s.lfo1.sync = 0; // Square
        setModSlot(s, 2, kSrcLFO1, kDstAllSpecTilt, 0.35f);
        // Velocity drives crush intensity: play harder = more destruction
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstDistDrive, 0.5f);
        // Filter env also swept for extra movement on the wah base (FIXED semitone amount)
        s.filter.envAmount = 24.0f; // +24 st sweep - audible, not the 4000-Hz nonsense
        s.filterEnv.attackMs = 3.0f; s.filterEnv.decayMs = 200.0f;
        s.filterEnv.sustain = 0.3f; s.filterEnv.releaseMs = 180.0f;
        // --- Short Digital slap keeps it tight and lo-fi (era/limiter engaged) ---
        s.delayEnabled = 1;
        s.delay.type = 0; // Digital
        s.delay.timeMs = 220.0f; s.delay.feedback = 0.32f; s.delay.mix = 0.22f;
        s.delay.digitalEra = 1; s.delay.digitalAge = 0.4f; s.delay.digitalLimiter = 1;
        s.settings.velocityCurve = 2; // Hard - makes the velocity->crush route bite
        s.global.masterGain = 0.9f;
        presets.push_back(std::move(p));
    }

    // "Folded Scream" - Hard-sync into a Lockhart wavefolder, envelope-tracked BP, granular shimmer
    {
        PresetDef p;
        p.name = "Folded Scream";
        p.category = "Experimental";
        auto& s = p.state;
        // --- Hard-sync saw slave at a high ratio = dense, tearing harmonic spectrum ---
        s.oscA.type = 3; // Sync
        s.oscA.syncRatio = 5.0f;
        s.oscA.syncWaveform = 1; // Saw slave
        s.oscA.syncMode = 0; // Hard
        s.oscA.syncAmount = 0.85f;
        s.oscA.level = 0.8f;
        s.oscB.type = 0; // PolyBLEP triangle underneath for body
        s.oscB.waveform = 4; // Triangle
        s.oscB.tuneSemitones = -12.0f; // sub octave to anchor the scream
        s.oscB.level = 0.45f;
        s.mixer.position = 0.35f; // favor the sync osc
        // --- SVF band-pass isolates the folded formant peak; fast filter-env pluck ---
        s.filter.type = 2; // SVF BP
        s.filter.cutoffHz = 1200.0f; // low base; env + envFollower sweep it up
        s.filter.resonance = 5.79f;
        s.filter.svfDrive = 6.0f; // a little post-filter grit
        s.filter.envAmount = 30.0f; // +30 st snappy pluck sweep (FIXED semitone value)
        s.filterEnv.attackMs = 2.0f; s.filterEnv.decayMs = 260.0f;
        s.filterEnv.sustain = 0.15f; s.filterEnv.releaseMs = 200.0f;
        // --- Lockhart wavefolder: the screaming nonlinearity ---
        s.distortion.type = 4; // Wavefolder
        s.distortion.drive = 0.7f;
        s.distortion.foldType = 2; // Lockhart
        s.distortion.mix = 0.9f;
        s.ampEnv.attackMs = 5.0f; s.ampEnv.decayMs = 400.0f;
        s.ampEnv.sustain = 0.55f; s.ampEnv.releaseMs = 320.0f;
        // === MOD WEB: envFollower + two LFOs + modEnv all sculpt the fold/filter ===
        // ENVELOPE FOLLOWER tracks the voice loudness and opens the band-pass with it
        s.envFollower.sensitivity = 0.6f; s.envFollower.attackMs = 5.0f; s.envFollower.releaseMs = 80.0f;
        setModSlot(s, 0, kSrcEnvFollower, kDstAllFltCut, 0.4f, kCurveLinear, 20.0f); // smoothMs
        // LFO1 triangle sweeps spectral tilt (slow timbral breathing)
        s.lfo1.rateHz = 0.5f; s.lfo1.shape = 1; s.lfo1.depth = 0.7f; s.lfo1.sync = 0; // Triangle
        setModSlot(s, 1, kSrcLFO1, kDstAllSpecTilt, 0.4f);
        // LFO2 saw ramps resonance for rising 'screaming peak' motion
        s.lfo2.rateHz = 0.3f; s.lfo2.shape = 2; s.lfo2.depth = 0.6f; s.lfo2.sync = 0; // Saw
        setModSlot(s, 2, kSrcLFO2, kDstAllResonance, 0.35f);
        // Per-voice: mod-env pushes morph on each note, velocity drives the fold intensity
        setVoiceRoute(s, 0, kVSrcEnv3, kVDstMorphPos, 0.5f);
        s.modEnv.attackMs = 2.0f; s.modEnv.decayMs = 600.0f;
        s.modEnv.sustain = 0.1f; s.modEnv.releaseMs = 300.0f;
        setVoiceRoute(s, 1, kVSrcVelocity, kVDstDistDrive, 0.6f); // harder = more folding
        // --- Granular delay pitched up an octave = shimmering, smeared fold tails ---
        s.delayEnabled = 1;
        s.delay.type = 3; // Granular
        s.delay.timeMs = 300.0f; s.delay.feedback = 0.4f; s.delay.mix = 0.28f;
        s.delay.granularSizeMs = 80.0f; s.delay.granularDensity = 15.0f;
        s.delay.granularPitch = 12.0f; // octave-up grains = shimmer
        s.delay.granularTexture = 0.35f; s.delay.granularPosSpray = 0.3f;
        s.reverbEnabled = 1;
        s.reverb.size = 0.45f; s.reverb.mix = 0.2f; // small plate keeps it aggressive, not washy
        s.global.masterGain = 0.85f; // compensate wavefolder gain
        presets.push_back(std::move(p));
    }

    // "Benjolin Drift" - Blue-noise + Chua through a comb, full-rungler mod web, frozen reverb
    {
        PresetDef p;
        p.name = "Benjolin Drift";
        p.category = "Experimental";
        auto& s = p.state;
        // --- Blue noise (hissy, high-tilted) blended with a Chua chaotic oscillator ---
        s.oscA.type = 9; // Noise
        s.oscA.noiseColor = 3; // Blue
        s.oscA.level = 0.5f;
        s.oscB.type = 5; // Chaos
        s.oscB.chaosAttractor = 2; // Chua
        s.oscB.chaosAmount = 0.6f;
        s.oscB.chaosCoupling = 0.5f;
        s.oscB.chaosOutput = 1; // Y axis
        s.oscB.level = 0.6f;
        s.mixer.position = 0.5f;
        // --- COMB filter: metallic, resonant, tuneable ringing = the Benjolin voice ---
        s.filter.type = 6; // Comb
        s.filter.cutoffHz = 1200.0f; // comb tuning
        s.filter.resonance = 7.48f; // feedback = strong metallic ring
        s.filter.combDamping = 0.3f; // tame the extreme highs in the feedback
        // --- Granular waveshaper distortion: chops the noise into grains ---
        s.distortion.type = 3; // Granular
        s.distortion.drive = 0.4f;
        s.distortion.grainSize = 0.4f; s.distortion.grainDensity = 0.5f;
        s.distortion.grainVariation = 0.4f; s.distortion.grainJitter = 0.3f;
        s.distortion.mix = 0.6f;
        s.ampEnv.attackMs = 20.0f; s.ampEnv.decayMs = 500.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 700.0f;
        // === FULL FOUR-SLOT RUNGLER-CENTRIC MOD WEB ===
        // RUNGLER (looping shift-register) sweeps the comb tuning = classic stepped Benjolin melody
        s.rungler.osc1FreqHz = 3.0f; s.rungler.osc2FreqHz = 5.0f;
        s.rungler.depth = 0.6f; s.rungler.bits = 6; s.rungler.filter = 0.3f;
        s.rungler.loopMode = 1; // LOOP mode = repeating pseudo-melodic pattern (not free chaos)
        setModSlot(s, 0, kSrcRungler, kDstAllFltCut, 0.55f);
        // S&H steps the morph between noise and Chua in hard stairs
        s.sampleHold.rateHz = 5.0f; s.sampleHold.slewMs = 20.0f;
        setModSlot(s, 1, kSrcSampleHold, kDstAllMorphPos, 0.4f, kCurveStepped);
        // Smooth-random LFO drifts the spectral tilt (the slow 'drift' of the name)
        s.lfo1.rateHz = 0.2f; s.lfo1.shape = 5; s.lfo1.depth = 0.6f; s.lfo1.sync = 0; // SmoothRandom
        setModSlot(s, 2, kSrcLFO1, kDstAllSpecTilt, 0.35f);
        // Chaos modulator wobbles the comb resonance for unstable ringing
        s.chaosMod.rateHz = 1.0f; s.chaosMod.type = 0; s.chaosMod.depth = 0.4f; // Lorenz
        setModSlot(s, 3, kSrcChaos, kDstAllResonance, 0.25f, kCurveSCurve);
        // --- Granular delay then a FROZEN reverb hold the drifting cloud ---
        s.delayEnabled = 1;
        s.delay.type = 3; // Granular
        s.delay.timeMs = 333.0f; s.delay.feedback = 0.5f; s.delay.mix = 0.3f;
        s.delay.granularSizeMs = 120.0f; s.delay.granularDensity = 12.0f;
        s.delay.granularReverseProb = 0.3f; s.delay.granularTexture = 0.4f;
        s.reverbEnabled = 1;
        s.reverb.size = 0.75f; s.reverb.mix = 0.3f;
        s.reverb.freeze = 1; // REVERB FREEZE - infinite drone bed under the drift
        s.settings.voiceAllocMode = 1; // Oldest (stable drone), covers alloc-mode axis
        s.global.polyphony = 4; // this is a drone, few voices needed
        s.global.masterGain = 0.85f; // freeze + granular feedback need headroom
        presets.push_back(std::move(p));
    }

    // ==================== PADS Category (15 new presets) ====================

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
        s.filter.resonance = 3.53f;
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
        s.filter.resonance = 2.96f; s.filter.svfSlope = 1;
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
        s.filter.cutoffHz = 3200.0f; s.filter.resonance = 2.40f;
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
        s.filter.type = 0; s.filter.cutoffHz = 6000.0f; s.filter.resonance = 2.40f;
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
        s.filter.type = 0; s.filter.cutoffHz = 5500.0f; s.filter.resonance = 1.83f;
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
        s.filter.cutoffHz = 2500.0f; s.filter.resonance = 4.09f;
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
        s.filter.cutoffHz = 2600.0f; s.filter.resonance = 4.09f;
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
        s.filter.resonance = 2.96f;
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
        s.filter.cutoffHz = 4000.0f; s.filter.resonance = 5.22f;
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
        s.filter.cutoffHz = 220.0f; s.filter.resonance = 5.22f;
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
        s.filter.resonance = 6.35f;
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
        s.filter.cutoffHz = 3500.0f; s.filter.resonance = 2.96f;
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
        s.filter.resonance = 4.66f;
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
        s.oscA.chaosAmount = 0.65f; s.oscA.chaosCoupling = 0.25f;
        s.oscA.chaosOutput = 0;                    // X axis
        s.oscA.level = 1.0f;
        s.oscB.type = 9; s.oscB.noiseColor = 5;    // Grey noise
        s.oscB.level = 0.2f;
        s.mixer.position = 0.15f;
        // --- Driven ladder lowpass. A bandpass framed the chaos more tightly but
        //     threw away almost all of its energy: the Duffing X output is a slow,
        //     low-level excursion and there is no tonal oscillator to carry the
        //     patch, so the band-limited result was inaudible. The alien
        //     "formant" character now comes from the ring modulator instead.
        s.filter.type = 4;                         // Ladder LP
        s.filter.cutoffHz = 1400.0f; s.filter.resonance = 4.66f;
        s.filter.ladderSlope = 4;                  // 24 dB/oct
        s.filter.ladderDrive = 15.0f;              // dB of input drive: makeup + grit
        // --- Ring modulator at a note-tracked ratio = the metallic alien voice
        s.distortion.type = 6;                     // RingModulator
        s.distortion.drive = 0.4f;
        s.distortion.ringFreqMode = 1;             // NoteTrack (follows the key)
        s.distortion.ringRatio = 0.22f;            // normalized -> ~3x ratio
        s.distortion.ringWaveform = 0;             // Sine
        s.distortion.character = 0.5f; s.distortion.mix = 0.45f;
        s.distortion.ringStereoSpread = 0.3f;      // stereo shimmer on the sidebands
        // --- Slow amp swell/long tail
        s.ampEnv.attackMs = 500.0f; s.ampEnv.decayMs = 900.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 2200.0f;
        // --- MOD IDENTITY: the dedicated chaos LFO wanders the bandpass, a smoothed random
        //     source jitters resonance. Stepped curve gives an eerie quantized lurch.
        s.chaosMod.rateHz = 0.3f; s.chaosMod.type = 1; // Rossler attractor
        s.chaosMod.depth = 0.6f; s.chaosMod.sync = 0;  // raised from default 0 so it's audible
        s.random.rateHz = 0.5f; s.random.smoothness = 0.6f;
        // Both amounts kept moderate: a wide stepped cutoff sweep over a narrow
        // resonant band lands on chaotic energy only intermittently, which reads
        // as a mostly-silent patch punctuated by resonant level spikes.
        setModSlot(s, 0, kSrcChaos,  kDstAllFltCut,    0.3f,  kCurveStepped); // lurching sweep
        setModSlot(s, 1, kSrcRandom, kDstAllResonance, 0.12f, kCurveLinear);  // Q jitter
        // --- Spectral delay smears the drone into an inharmonic cloud
        s.delayEnabled = 1; s.delay.type = 4;      // Spectral
        s.delay.timeMs = 500.0f; s.delay.feedback = 0.4f; s.delay.mix = 0.22f;
        s.delay.sync = 0;
        s.delay.spectralFFTSize = 2; s.delay.spectralSpreadMs = 400.0f;
        s.delay.spectralDiffusion = 0.5f; s.delay.spectralWidth = 0.6f;
        // --- Big diffuse HALL. Wet kept below the original 0.4 so the dry chaos
        //     body still carries the patch rather than dissolving into the tail.
        s.reverbEnabled = 1; s.reverbType = 1;     // Hall
        s.reverb.size = 0.85f; s.reverb.mix = 0.3f;
        s.reverb.damping = 0.5f; s.reverb.diffusion = 0.8f;
        s.global.width = 1.3f; s.global.spread = 0.4f;
        s.global.masterGain = 2.0f;                // chaos+ringmod chain still loses level
        presets.push_back(std::move(p));
    }

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
        s.filter.type = 0; s.filter.cutoffHz = 5500.0f; s.filter.resonance = 2.40f;
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

    // ==================== LEADS Category (15 new presets) ====================

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
        s.filter.resonance = 8.61f;    // vocal peak
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
        s.filter.resonance = 3.53f;
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
        s.filter.resonance = 4.66f;
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
        s.filter.resonance = 6.35f;
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
        s.filter.resonance = 6.35f;    // feedback depth
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
        s.filter.cutoffHz = 3800.0f; s.filter.resonance = 4.09f;
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
        s.filter.cutoffHz = 4200.0f; s.filter.resonance = 3.53f;
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
        s.filter.cutoffHz = 1200.0f; s.filter.resonance = 8.61f; // feedback = the 'ring'
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
        s.filter.type = 0; s.filter.cutoffHz = 3800.0f; s.filter.resonance = 4.66f;
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
        s.filter.cutoffHz = 1400.0f; s.filter.resonance = 4.09f;
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
        s.filter.cutoffHz = 1800.0f; s.filter.resonance = 5.79f;
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
        s.filter.cutoffHz = 2000.0f; s.filter.resonance = 6.35f; s.filter.svfSlope = 1;
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
        s.filter.cutoffHz = 600.0f; s.filter.resonance = 5.22f;
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

    // ==================== BASSES Category (15 new presets) ====================

        // "Lorenz Maw" - Unstable Lorenz-chaos sub reinforced by a pure sine, ground
        // down by tape saturation and animated by the chaosMod source into cutoff.
        {
            PresetDef p;
            p.name = "Lorenz Maw";
            p.category = "Bass";
            auto& s = p.state;

            // OSC A: Chaos engine (Lorenz) as the fundamental — pushed hard enough to grind
            s.oscA.type = 5;              // Chaos
            s.oscA.chaosAttractor = 0;    // Lorenz
            s.oscA.chaosAmount = 0.55f;   // grind: enough to smear the fundamental (was 0.3, too tame)
            s.oscA.chaosCoupling = 0.2f;  // cross-axis instability => a living low end
            s.oscA.chaosOutput = 0;       // X axis
            s.oscA.tuneSemitones = -12.0f; s.oscA.level = 0.8f;
            // OSC B: pure sine one octave down — the anchor that keeps pitch legible
            s.oscB.type = 0; s.oscB.waveform = 0; // Sine
            s.oscB.tuneSemitones = -12.0f; s.oscB.level = 0.6f;
            s.mixer.position = 0.35f;     // mostly chaos, sine underpins

            // Ladder LP with input drive — the tube-y grind stage (Ladder slope/drive)
            s.filter.type = 4;            // Ladder
            s.filter.cutoffHz = 1800.0f; s.filter.resonance = 4.09f;
            s.filter.ladderSlope = 4;     // 24 dB/oct
            s.filter.ladderDrive = 6.0f;  // dB of input drive for extra grit

            // Tape saturation warms and compresses the chaotic smear
            s.distortion.type = 5;        // TapeSaturator
            s.distortion.drive = 0.5f; s.distortion.tapeSaturation = 0.6f; s.distortion.mix = 1.0f;

            s.ampEnv.attackMs = 3.0f; s.ampEnv.decayMs = 220.0f;
            s.ampEnv.sustain = 0.65f; s.ampEnv.releaseMs = 140.0f;

            // chaosMod source: an independent Lorenz LFO, raised from its silent default
            s.chaosMod.type = 0;          // Lorenz
            s.chaosMod.rateHz = 0.3f;     // slow drift
            s.chaosMod.depth = 0.6f;      // MUST raise — default 0 = silent source
            s.chaosMod.sync = 0;

            // Synced LFO1 for a tempo-locked resonance shimmer under the chaos
            s.lfo1.shape = 0;             // Sine
            s.lfo1.sync = 1;              // tempo-synced
            s.lfo1Ext.noteValue = 16;     // 1/2 note — slow pulse

            // Mod matrix: chaos -> cutoff (SCurve) is this preset's unique gesture;
            // synced LFO1 -> resonance adds tempo-locked movement.
            setModSlot(s, 0, kSrcChaos, kDstAllFltCut, 0.4f, kCurveSCurve);
            setModSlot(s, 1, kSrcLFO1, kDstAllResonance, 0.3f, kCurveLinear);

            // Subtle tape delay for depth without washing out the sub
            s.delayEnabled = 1;
            s.delay.type = 1;             // Tape
            s.delay.sync = 1; s.delay.noteValue = 10; // 1/8
            s.delay.feedback = 0.25f; s.delay.mix = 0.14f; s.delay.tapeSaturation = 0.5f;

            s.global.voiceMode = 1;       // Mono
            s.global.masterGain = 0.9f;   // headroom for tape + drive
            presets.push_back(std::move(p));
        }

        // "Anvil Ring" - Note-tracked ring modulator on a single saw, focused by an
        // SVF Peak in the low-mids; a free LFO sweeps the peak across the ring's
        // inharmonics (ringRatio is not a mod destination, so we move the peak instead).
        {
            PresetDef p;
            p.name = "Anvil Ring";
            p.category = "Bass";
            auto& s = p.state;

            s.oscA.type = 0; s.oscA.waveform = 1; // Saw
            s.oscA.tuneSemitones = -12.0f; s.oscA.level = 0.82f;
            s.oscB.level = 0.0f;           // single osc
            s.mixer.position = 0.0f;       // OSC A only

            // SVF Peak: a low-mid bump gives the clang body/punch (covers svfGain)
            s.filter.type = 8;             // SVF Peak
            s.filter.cutoffHz = 320.0f;    // low-mid punch band (LFO sweeps it upward)
            s.filter.resonance = 1.5f;
            s.filter.svfGain = 12.0f;      // +12 dB peak boost
            s.filter.svfSlope = 1;

            // Ring modulator, carrier tracking the note ~1.5x for an inharmonic bell edge
            s.distortion.type = 6;         // RingModulator
            s.distortion.drive = 0.45f;
            s.distortion.ringFreqMode = 1; // NoteTrack
            s.distortion.ringRatio = 0.079f; // ~1.5x  (norm: (1.5-0.25)/15.75)
            s.distortion.ringWaveform = 0; // Sine carrier
            s.distortion.ringStereoSpread = 0.3f;
            s.distortion.mix = 0.5f;

            s.ampEnv.attackMs = 1.0f; s.ampEnv.decayMs = 220.0f;
            s.ampEnv.sustain = 0.6f; s.ampEnv.releaseMs = 110.0f;

            // Free-running LFO2 sweeps the peak filter across the ring's inharmonics so
            // the "bell overtone moves" — unique gesture (Exp curve, x2 scale axis).
            s.lfo2.shape = 0;              // Sine
            s.lfo2.sync = 0;               // free-run
            s.lfo2.rateHz = 0.8f;
            setModSlot(s, 0, kSrcLFO2, kDstAllFltCut, 0.5f, kCurveExp);
            s.modMatrix.slots[0].scale = 3; // x2 — exercise the untouched scale axis for a wide sweep

            // Velocity drives ring intensity so harder hits clang harder
            setVoiceRoute(s, 0, kVSrcVelocity, kVDstDistDrive, 0.4f);

            // Phaser adds an evolving metallic shimmer on top
            s.modulationType = 1;          // Phaser
            s.phaser.rateHz = 0.4f; s.phaser.depth = 0.5f;
            s.phaser.feedback = 0.4f; s.phaser.mix = 0.3f;
            s.phaser.stages = 1;           // dropdown idx 1 => 4 stages (moderate)
            s.phaser.centerFreqHz = 800.0f;

            s.global.voiceMode = 1;        // Mono
            s.global.masterGain = 0.9f;
            presets.push_back(std::move(p));
        }

        // "Gravel Pit" - Saw+triangle sub with FM growl, crumbled by granular
        // distortion (internal jitter keeps it alive), an auto-wah EnvFilter, a
        // digital delay, and a stepped LFO morph between the two waves.
        {
            PresetDef p;
            p.name = "Gravel Pit";
            p.category = "Bass";
            auto& s = p.state;

            s.oscA.type = 0; s.oscA.waveform = 1; // Saw
            s.oscA.tuneSemitones = -12.0f; s.oscA.level = 0.85f;
            s.oscA.phaseMod = 0.3f;        // FM-ish sidebands => growl
            s.oscA.freqMod = 0.2f;         // slight freq-mod grit
            s.oscB.type = 0; s.oscB.waveform = 2; // Triangle
            s.oscB.tuneSemitones = -12.0f; s.oscB.level = 0.45f;
            s.mixer.position = 0.35f;

            // EnvFilter auto-wah: input envelope opens the filter per note
            s.filter.type = 11;            // Env Filter
            s.filter.cutoffHz = 350.0f;    // base
            s.filter.resonance = 6.35f;
            s.filter.envSubType = 0;       // LP response
            s.filter.envSensitivity = 6.0f;
            s.filter.envDepth = 0.85f;
            s.filter.envAttack = 12.0f;
            s.filter.envRelease = 160.0f;
            s.filter.envDirection = 0;     // sweep up

            // Granular distortion with real variation + jitter so the crumble evolves
            s.distortion.type = 3;         // GranularDistortion
            s.distortion.drive = 0.5f;
            s.distortion.grainSize = 0.25f;
            s.distortion.grainDensity = 0.55f;
            s.distortion.grainVariation = 0.5f; // up from 0.2 — audible texture spread
            s.distortion.grainJitter = 0.35f;   // non-static crumble (no jitter mod dest, so bake it in)
            s.distortion.mix = 0.85f;

            s.ampEnv.attackMs = 1.0f; s.ampEnv.decayMs = 190.0f;
            s.ampEnv.sustain = 0.55f; s.ampEnv.releaseMs = 110.0f;

            // Stepped LFO morph flips between saw and triangle for a rhythmic timbre
            s.lfo1.shape = 0; s.lfo1.sync = 1; s.lfo1Ext.noteValue = 10; // 1/8
            setModSlot(s, 0, kSrcLFO1, kDstAllMorphPos, 0.3f, kCurveStepped);

            // Digital delay for grit-space
            s.delayEnabled = 1;
            s.delay.type = 0;              // Digital
            s.delay.sync = 1; s.delay.noteValue = 11; // 1/8 dotted groove
            s.delay.feedback = 0.2f; s.delay.mix = 0.16f;

            s.global.voiceMode = 1;
            s.global.masterGain = 0.9f;
            presets.push_back(std::move(p));
        }

        // "Sync Fist" - Hard-sync saw with a REAL downward filter sweep (envAmount
        // fixed to +28 semitones, not the bogus 30 "Hz"), Lockhart wavefolder bite,
        // consistent start phase for punch, and velocity/aftertouch performance routes.
        {
            PresetDef p;
            p.name = "Sync Fist";
            p.category = "Bass";
            auto& s = p.state;

            s.oscA.type = 3;               // Sync
            s.oscA.syncRatio = 2.5f;
            s.oscA.syncWaveform = 1;       // Saw slave
            s.oscA.syncMode = 0;           // Hard
            s.oscA.tuneSemitones = -12.0f; s.oscA.level = 0.9f;
            s.oscA.phase = 0.15f;          // fixed start phase => consistent punchy transient
            s.oscB.level = 0.0f;
            s.mixer.position = 0.0f;

            // SVF LP (24 dB) with a touch of drive; the filter env sweeps it down hard
            s.filter.type = 0;             // SVF LP
            s.filter.cutoffHz = 500.0f;
            s.filter.resonance = 6.35f;
            s.filter.svfSlope = 1;         // 24 dB
            s.filter.svfDrive = 4.0f;
            s.filter.envAmount = 28.0f;    // FIX: +28 semitones of sweep (was 30 => near-zero)

            // Lockhart wavefolder adds harmonic bite on the attack
            s.distortion.type = 4;         // Wavefolder
            s.distortion.foldType = 2;     // Lockhart
            s.distortion.drive = 0.3f;     // moderate — folding gets loud fast
            s.distortion.mix = 0.7f;

            s.ampEnv.attackMs = 1.0f; s.ampEnv.decayMs = 200.0f;
            s.ampEnv.sustain = 0.5f; s.ampEnv.releaseMs = 100.0f;
            // Fast filter env => snappy downward sweep each note
            s.filterEnv.attackMs = 1.0f; s.filterEnv.decayMs = 170.0f;
            s.filterEnv.sustain = 0.1f; s.filterEnv.releaseMs = 90.0f;
            s.filterEnv.decayCurve = 0.4f; // exp-ish snap

            // Performance routes: velocity opens cutoff, aftertouch pushes resonance
            setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.5f);
            setVoiceRoute(s, 1, kVSrcAftertouch, kVDstFltRes, 0.4f);

            s.global.voiceMode = 1;        // Mono
            s.monoMode.legato = 1;
            s.monoMode.portamentoTimeMs = 40.0f; // subtle glide for stabs
            s.monoMode.portaMode = 1;      // Legato-only glide
            s.global.masterGain = 0.9f;
            presets.push_back(std::move(p));
        }

        // "Fifth Cellar" - Saw+triangle sub thickened by a PitchSync harmonizer voice a
        // FIFTH up (adds new content instead of doubling the existing -12 sub), voiced
        // through a key-tracked comb, with an Env3 pitch drift and legato glide.
        {
            PresetDef p;
            p.name = "Fifth Cellar";
            p.category = "Bass";
            auto& s = p.state;

            s.oscA.type = 0; s.oscA.waveform = 1; // Saw
            s.oscA.tuneSemitones = -12.0f; s.oscA.level = 0.8f;
            s.oscB.type = 0; s.oscB.waveform = 2; // Triangle
            s.oscB.tuneSemitones = -12.0f; s.oscB.level = 0.45f;
            s.mixer.position = 0.35f;

            // Key-tracked comb filter => a hollow, resonant body that follows pitch
            s.filter.type = 6;             // Comb
            s.filter.cutoffHz = 1600.0f;
            s.filter.resonance = 4.66f;
            s.filter.combDamping = 0.35f;  // tame the HF in the comb feedback
            s.filter.keyTrack = 1.0f;      // comb resonance tracks the note

            s.ampEnv.attackMs = 2.0f; s.ampEnv.decayMs = 220.0f;
            s.ampEnv.sustain = 0.75f; s.ampEnv.releaseMs = 130.0f;

            // Harmonizer: PitchSync, a FIFTH up so it adds harmonic content, not more sub
            s.harmonizerEnabled = 1;
            s.harmonizer.harmonyMode = 0;    // Chromatic
            s.harmonizer.pitchShiftMode = 3; // PitchSync (glitch-free tracked shift)
            s.harmonizer.numVoices = 1;
            s.harmonizer.dryLevelDb = 0.0f;
            s.harmonizer.wetLevelDb = -8.0f;
            s.harmonizer.voiceInterval[0] = 7;   // +7 st = perfect fifth
            s.harmonizer.voiceLevelDb[0] = 0.0f;

            // Env3 (ModEnv) nudges OSC B pitch for slow beating/thickening — unique gesture
            s.modEnv.attackMs = 400.0f; s.modEnv.decayMs = 800.0f;
            s.modEnv.sustain = 0.6f; s.modEnv.releaseMs = 600.0f;
            setVoiceRoute(s, 0, kVSrcEnv3, kVDstOscBPitch, 0.15f);

            // Slow free LFO detunes the comb resonance slightly for a living body
            s.lfo2.shape = 0; s.lfo2.sync = 0; s.lfo2.rateHz = 0.2f;
            setModSlot(s, 0, kSrcLFO2, kDstAllFltCut, 0.15f, kCurveLinear);

            s.global.voiceMode = 1;        // Mono
            s.monoMode.legato = 1;
            s.monoMode.portamentoTimeMs = 60.0f;
            s.monoMode.portaMode = 0;      // always glide
            presets.push_back(std::move(p));
        }

    // "Vocal Bass" - Formant-osc + formant-FILTER vowel bass, PitchSync sub, aftertouch growl
    {
        PresetDef p;
        p.name = "Vocal Bass";
        p.category = "Bass";
        auto& s = p.state;
        // OSC A: Formant engine on vowel 'O' — the nasal vowel body of the bass
        s.oscA.type = 7;              // Formant
        s.oscA.formantVowel = 3;      // O
        s.oscA.formantMorph = 0.6f;   // sit just past O toward U for a rounder low vowel
        s.oscA.tuneSemitones = -12.0f;
        s.oscA.level = 0.9f;
        s.oscA.phase = 0.1f;          // slight start-phase offset = consistent soft attack transient
        // OSC B: pure sine two octaves down for sub weight
        s.oscB.type = 0; s.oscB.waveform = 0; // Sine
        s.oscB.tuneSemitones = -24.0f; s.oscB.level = 0.35f;
        s.mixer.position = 0.35f;     // favour the vowel, keep the sub present underneath
        // FORMANT FILTER (type 5) — a SECOND vowel stage; morph+gender for a masculine 'aw'
        s.filter.type = 5;
        s.filter.cutoffHz = 800.0f;
        s.filter.resonance = 5.22f;
        s.filter.formantMorph = 1.2f;   // filter vowel toward E — combs the formant osc for a talking timbre
        s.filter.formantGender = -0.3f; // shift formants down = chest-voice / male bass
        // Corrected filter-env: PLAIN semitones (+18 = audible vowel-brightening on attack)
        s.filter.envAmount = 18.0f;
        s.filterEnv.attackMs = 8.0f; s.filterEnv.decayMs = 220.0f;
        s.filterEnv.sustain = 0.5f; s.filterEnv.releaseMs = 180.0f;
        // Amp env — legato bass, moderate body
        s.ampEnv.attackMs = 5.0f; s.ampEnv.decayMs = 250.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 200.0f;
        // Mono + legato glide = expressive vocal phrasing
        s.global.voiceMode = 1;
        s.monoMode.legato = 1; s.monoMode.portamentoTimeMs = 40.0f;
        // Harmonizer PitchSync sub-doubler: one voice an octave down, formant-locked (chromatic)
        s.harmonizerEnabled = 1;
        s.harmonizer.harmonyMode = 0;    // Chromatic -> interval is in semitones
        s.harmonizer.pitchShiftMode = 3; // PitchSync
        s.harmonizer.numVoices = 1;
        s.harmonizer.voiceInterval[0] = -12; // octave-down doubling for reinforced sub
        s.harmonizer.voiceLevelDb[0] = -2.0f;
        s.harmonizer.dryLevelDb = 0.0f; s.harmonizer.wetLevelDb = -3.0f;
        // Mod identity #1: synced slow LFO1 sways the A/B morph = rhythmic vowel<->sub pump
        s.lfo1Ext.noteValue = 16;        // 1/2 note = lazy half-bar sway
        setModSlot(s, 0, kSrcLFO1, kDstAllMorphPos, 0.3f, kCurveSCurve);
        // Mod identity #2: aftertouch opens filter resonance = press for vocal growl
        setVoiceRoute(s, 0, kVSrcAftertouch, kVDstFltRes, 0.4f);
        presets.push_back(std::move(p));
    }

    // "Gut String" - deep upright-bass Karplus pluck: pink-noise burst -> LOW key-tracked comb,
    // triangle sub an octave under, STATIC tape-colored resonator + vintage tape slap.
    // Deliberately motion-free (no LFO/random) to contrast Sinew's modulated string.
    {
        PresetDef p;
        p.name = "Gut String";
        p.category = "Bass";
        auto& s = p.state;
        // OSC A: pink noise = the excitation 'pluck' feeding the comb (warmer/darker than white)
        s.oscA.type = 9;             // Noise
        s.oscA.noiseColor = 1;       // Pink - softer, gut-string excitation
        s.oscA.level = 0.55f;
        // OSC B: triangle sub a full OCTAVE below the comb pitch -> deep upright-bass body
        s.oscB.type = 0; s.oscB.waveform = 4; // Triangle
        s.oscB.tuneSemitones = -24.0f; s.oscB.level = 0.5f;
        s.oscB.phase = 0.25f;        // defined start phase = consistent, clicky pluck attack
        s.mixer.position = 0.4f;     // excitation slightly favoured so the comb 'speaks'
        // COMB FILTER (type 6): LOW base freq + keyTrack=1 -> a deep tuned gut string,
        // a register below Sinew. Higher combDamping = a woody, damped, STATIC resonator
        // (no ring-out motion) vs Sinew's live, moving string.
        s.filter.type = 6;
        s.filter.cutoffHz = 220.0f;  // low comb reference (keytrack scales it per-note)
        s.filter.resonance = 8.84f;  // rings, but tamed by damping below
        s.filter.combDamping = 0.45f;// strong HF loss in feedback = warm, dead, static string
        s.filter.keyTrack = 1.0f;    // comb follows pitch - essential for a tuned pluck
        // Corrected filter-env: PLAIN +28 st fast decay = bright pluck attack that dulls quickly
        s.filter.envAmount = 28.0f;
        s.filterEnv.attackMs = 1.0f; s.filterEnv.decayMs = 120.0f;
        s.filterEnv.sustain = 0.0f; s.filterEnv.releaseMs = 100.0f;
        // Amp env: instant attack, long decay to low sustain = a decaying pluck, not a pad
        s.ampEnv.attackMs = 1.0f; s.ampEnv.decayMs = 450.0f;
        s.ampEnv.sustain = 0.15f; s.ampEnv.releaseMs = 250.0f;
        s.global.voiceMode = 1;
        // Vintage TAPE slap-back: short, low feedback, and genuine tape-resonator color via
        // saturation + wear + age (static tape character, not the moving verb Sinew would use)
        s.delayEnabled = 1;
        s.delay.type = 1;            // Tape
        s.delay.sync = 0;            // free-run slap
        s.delay.timeMs = 180.0f;
        s.delay.feedback = 0.25f;
        s.delay.mix = 0.18f;
        s.delay.tapeSaturation = 0.6f; // warm the repeats
        s.delay.tapeWear = 0.3f;       // worn-tape HF loss = tape-resonator color
        s.delay.tapeAge = 0.4f;        // aged tape character on the slap
        // Mod identity: harder playing opens the comb = dynamic pluck brightness (velocity, NOT LFO)
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.4f);
        presets.push_back(std::move(p));
    }

    // "Foldback" - Lockhart-folded triangle, SVF Peak punch, chaos wobble, velocity->fold
    {
        PresetDef p;
        p.name = "Foldback";
        p.category = "Bass";
        auto& s = p.state;
        // OSC A: triangle — clean fundamental that folds into rich buzz
        s.oscA.type = 0; s.oscA.waveform = 4; // Triangle
        s.oscA.tuneSemitones = -12.0f; s.oscA.level = 0.85f;
        // OSC B: sub sine two octaves down for weight the folder can't erase
        s.oscB.type = 0; s.oscB.waveform = 0; // Sine
        s.oscB.tuneSemitones = -24.0f; s.oscB.level = 0.3f;
        s.mixer.position = 0.3f;
        // WAVEFOLDER (Lockhart) at real drive so the fold is the identity, not a subtle tint
        s.distortion.type = 4;       // Wavefolder
        s.distortion.foldType = 2;   // Lockhart (0=Triangle,1=Sine,2=Lockhart)
        s.distortion.drive = 0.65f;  // ~6.5x fold — buzzy but the sub keeps it controlled
        s.distortion.mix = 0.9f;     // leave a sliver of dry sub for stability
        // SVF PEAK filter (type 8): a low-mid band BOOST = the bass 'punch' knob for this category
        s.filter.type = 8;
        s.filter.cutoffHz = 250.0f;  // low-mid punch centre
        s.filter.resonance = 1.5f;
        s.filter.svfGain = 8.0f;     // +8 dB peak boost — chesty punch under the fold
        s.filter.svfDrive = 3.0f;    // gentle post saturation glues the boosted band
        // Corrected filter-env: PLAIN +20 st = the peak band sweeps up on attack
        s.filter.envAmount = 20.0f;
        s.filterEnv.attackMs = 1.0f; s.filterEnv.decayMs = 180.0f;
        s.filterEnv.sustain = 0.2f; s.filterEnv.releaseMs = 150.0f;
        s.ampEnv.attackMs = 1.0f; s.ampEnv.decayMs = 220.0f;
        s.ampEnv.sustain = 0.65f; s.ampEnv.releaseMs = 120.0f;
        s.global.voiceMode = 1;
        // Mod identity #1: Chaos LFO wobbles the peak centre = unstable analog 'growl'
        s.chaosMod.depth = 0.5f;     // raise it — chaosMod is silent by default
        s.chaosMod.rateHz = 3.0f;    // slow-ish organic wobble
        setModSlot(s, 0, kSrcChaos, kDstAllFltCut, 0.35f, kCurveExp, 15.0f);
        // Mod identity #2: velocity drives fold depth = play harder, fold harder
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstDistDrive, 0.5f);
        presets.push_back(std::move(p));
    }

    // "Bitrot" - FM-grit saw/square -> spectral bitcrush (magnitude quantize), SVF Notch, stepped-LFO glitch
    {
        PresetDef p;
        p.name = "Bitrot";
        p.category = "Bass";
        auto& s = p.state;
        // OSC A: saw with phase-mod = extra inharmonic sidebands feeding the crusher
        s.oscA.type = 0; s.oscA.waveform = 1; // Saw
        s.oscA.tuneSemitones = -12.0f; s.oscA.level = 0.8f;
        s.oscA.phaseMod = 0.35f;     // FM-ish grit before the spectral stage
        // OSC B: square with freq-mod = growl/detune motion in the low mids
        s.oscB.type = 0; s.oscB.waveform = 2; // Square/Pulse
        s.oscB.tuneSemitones = -12.0f; s.oscB.level = 0.35f;
        s.oscB.freqMod = 0.25f;
        s.mixer.position = 0.3f;
        // SVF NOTCH (type 3): scoops a mid band so it doesn't sit on its siblings' LP curve
        s.filter.type = 3;
        s.filter.cutoffHz = 700.0f;  // notch centre in the low-mids
        s.filter.resonance = 9.74f;
        // Corrected filter-env: PLAIN +22 st sweeps the notch on attack = moving hollow
        s.filter.envAmount = 22.0f;
        s.filterEnv.attackMs = 2.0f; s.filterEnv.decayMs = 200.0f;
        s.filterEnv.sustain = 0.2f; s.filterEnv.releaseMs = 150.0f;
        // SPECTRAL distortion: SpectralBitcrush mode — per-frame magnitude quantization = metallic FFT crunch
        s.distortion.type = 2;       // Spectral
        s.distortion.drive = 0.5f;
        s.distortion.spectralMode = 3;  // SpectralBitcrush — spectralBits now actually quantizes magnitudes
        s.distortion.spectralBits = 0.35f; // ~6.25 bits (0-1 -> [1,16], bits=1+x*15) = audible bitcrush
        s.distortion.mix = 0.8f;
        s.ampEnv.attackMs = 1.0f; s.ampEnv.decayMs = 190.0f;
        s.ampEnv.sustain = 0.55f; s.ampEnv.releaseMs = 110.0f;
        s.global.voiceMode = 1;
        // Lo-fi DIGITAL delay: aged, wide = the crunch smears into a broken-machine echo
        s.delayEnabled = 1;
        s.delay.type = 0;            // Digital
        s.delay.sync = 0;
        s.delay.timeMs = 250.0f;
        s.delay.feedback = 0.35f;
        s.delay.mix = 0.2f;
        s.delay.digitalAge = 0.4f;  // degrade the repeats
        s.delay.digitalWidth = 120.0f;
        // Mod identity #1: 1/16 STEPPED LFO jolts the spectral tilt = glitchy, quantized crunch shifts
        s.lfo1Ext.noteValue = 7;    // 1/16
        setModSlot(s, 0, kSrcLFO1, kDstAllSpecTilt, 0.4f, kCurveStepped);
        // Mod identity #2: velocity opens the notch region = dynamics move the hollow
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.4f);
        presets.push_back(std::move(p));
    }

    // "Swarm Sub" - burst-particle cloud + sine sub, EnvFilter auto-wah, phaser swirl
    {
        PresetDef p;
        p.name = "Swarm Sub";
        p.category = "Bass";
        auto& s = p.state;
        // OSC A: Particle engine in BURST spawn mode = rhythmic grain clusters, not a smear
        s.oscA.type = 6;                 // Particle
        s.oscA.particleSpawnMode = 2;    // Burst (own this: 0=Regular,1=Random,2=Burst)
        s.oscA.particleEnvType = 3;      // Blackman grain window = smooth, low-click clusters
        s.oscA.particleScatter = 1.5f;   // gentle detune spread — stays bass-like
        s.oscA.particleDensity = 40.0f;  // dense but not a wall
        s.oscA.particleLifetime = 60.0f; // short grains = textured fizz on the low end
        s.oscA.particleDrift = 0.15f;    // slow pitch wander = living cloud
        s.oscA.tuneSemitones = -12.0f; s.oscA.level = 0.8f;
        // OSC B: sine sub two octaves down anchors the cloud
        s.oscB.type = 0; s.oscB.waveform = 0; // Sine
        s.oscB.tuneSemitones = -24.0f; s.oscB.level = 0.45f;
        s.mixer.position = 0.3f;
        // ENV FILTER (type 11) auto-wah: the cloud squelches under its own envelope
        s.filter.type = 11;
        s.filter.cutoffHz = 350.0f;      // base — the auto-wah sweeps up from here
        s.filter.resonance = 12.00f;
        s.filter.envSubType = 0;         // LP response
        s.filter.envSensitivity = 6.0f;  // +6 dB input drive into the follower
        s.filter.envDepth = 0.8f;        // strong sweep
        s.filter.envAttack = 15.0f;
        s.filter.envRelease = 180.0f;
        s.filter.envDirection = 0;       // Up — opens on transient, classic wah
        s.filter.envAmount = 0.0f;       // let the EnvFilter own the motion (no filterEnv stacking)
        s.ampEnv.attackMs = 3.0f; s.ampEnv.decayMs = 250.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 150.0f;
        s.global.voiceMode = 1;
        // Phaser FX = slow swirl across the grain texture (unique FX in this chunk)
        s.modulationType = 1;            // Phaser
        s.phaser.rateHz = 0.4f;
        s.phaser.depth = 0.5f;
        s.phaser.feedback = 0.4f;
        s.phaser.mix = 0.3f;
        s.phaser.stages = 2;             // 6-stage
        s.phaser.centerFreqHz = 600.0f;
        // Mod identity: 1/4-note synced LFO2 breathes the grain<->sub blend
        s.lfo2Ext.noteValue = 13;        // 1/4
        setModSlot(s, 0, kSrcLFO2, kDstAllMorphPos, 0.3f, kCurveSCurve);
        presets.push_back(std::move(p));
    }

    // "Echo Bass" - all-tape dub bass: tape saturation + synced tape delay,
    // per-note SVF filter pluck, and an LFO pumping the echo swells.
    {
        PresetDef p;
        p.name = "Echo Bass";
        p.category = "Bass";
        auto& s = p.state;
        // Saw + triangle sub an octave down; triangle fills the body under the saw
        s.oscA.type = 0; s.oscA.waveform = 1;            // Saw
        s.oscA.tuneSemitones = -12.0f; s.oscA.level = 0.8f;
        s.oscA.phase = 0.0f;                             // reset to zero-cross => tight, repeatable attack
        s.oscB.type = 0; s.oscB.waveform = 4;            // Triangle
        s.oscB.tuneSemitones = -12.0f; s.oscB.level = 0.45f;
        s.mixer.position = 0.32f;                        // favour the saw
        // SVF LP with a CORRECTED filter-env sweep (semitones, not Hz) => plucky per note
        s.filter.type = 0;                               // SVF LP
        s.filter.cutoffHz = 420.0f; s.filter.resonance = 10.87f;
        s.filter.envAmount = 22.0f;                      // +22 st sweep (fixes the bogus-Hz envAmount bug class)
        s.filterEnv.attackMs = 2.0f; s.filterEnv.decayMs = 180.0f;
        s.filterEnv.sustain = 0.18f; s.filterEnv.releaseMs = 200.0f;
        s.filterEnv.decayCurve = 0.4f;                   // exp-ish snap on the pluck tail
        s.ampEnv.attackMs = 2.0f; s.ampEnv.decayMs = 200.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 130.0f;
        // Tape SATURATOR in the voice for analog grit before the tape ECHO
        s.distortion.type = 5;                           // TapeSaturator
        s.distortion.drive = 0.3f; s.distortion.character = 0.5f; s.distortion.mix = 0.6f;
        s.distortion.tapeModel = 0; s.distortion.tapeSaturation = 0.6f; s.distortion.tapeBias = 0.5f;
        // Synced tape delay slapback with wow/wear => dub echo character
        s.delayEnabled = 1;
        s.delay.type = 1;                                // Tape
        s.delay.sync = 1; s.delay.noteValue = kNote1_8D;
        s.delay.mix = 0.28f; s.delay.feedback = 0.42f;
        s.delay.tapeSaturation = 0.6f; s.delay.tapeWear = 0.3f; s.delay.tapeAge = 0.2f;
        // IDENTITY gesture: a half-note synced LFO pumps the effect (echo) mix - no sibling touches EffectMix
        s.lfo1.rateHz = 1.0f; s.lfo1.shape = 1;          // Triangle
        s.lfo1.sync = 1; s.lfo1Ext.noteValue = 16;       // 1/2 note
        setModSlot(s, 0, kSrcLFO1, kDstEffectMix, 0.35f, kCurveSCurve);
        // Harder hits open the filter
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.4f);
        s.global.voiceMode = 1;                          // Mono
        s.monoMode.legato = 1; s.monoMode.portamentoTimeMs = 12.0f;
        presets.push_back(std::move(p));
    }

    // "Phase Bass" - phase-thickened bass: inter-osc phaseMod + a key-tracked comb
    // filter + a deep phaser, with an LFO morphing the A/B blend. Owns phaseMod on bass.
    {
        PresetDef p;
        p.name = "Phase Bass";
        p.category = "Bass";
        auto& s = p.state;
        s.oscA.type = 0; s.oscA.waveform = 1;            // Saw
        s.oscA.tuneSemitones = -12.0f; s.oscA.level = 0.8f;
        s.oscA.phaseMod = 0.45f;                         // FM-ish sidebands thicken the tone...
        s.oscB.type = 0; s.oscB.waveform = 1;            // Saw
        s.oscB.tuneSemitones = -12.0f; s.oscB.fineCents = 7.0f; s.oscB.level = 0.7f;
        s.oscB.phaseMod = 0.45f;                         // ...applied to both => a dense cross-modded pair
        s.mixer.position = 0.5f;
        // Comb filter, key-tracked => hollow phasey resonances that follow the note
        s.filter.type = 6;                               // Comb
        s.filter.cutoffHz = 200.0f; s.filter.resonance = 4.66f;
        s.filter.combDamping = 0.5f;                     // tame the metallic top so it stays a bass
        s.filter.keyTrack = 1.0f;                        // comb tunes with pitch
        s.ampEnv.attackMs = 3.0f; s.ampEnv.decayMs = 200.0f;
        s.ampEnv.sustain = 0.75f; s.ampEnv.releaseMs = 150.0f;
        // Deep slow phaser layered on top of the phaseMod thickness
        s.modulationType = 1;                            // Phaser
        s.phaser.rateHz = 0.35f; s.phaser.depth = 0.6f;
        s.phaser.feedback = 0.55f; s.phaser.mix = 0.4f; s.phaser.stages = 3; // 8 stages
        s.phaser.centerFreqHz = 450.0f;
        // IDENTITY gesture: a 1/4 synced LFO morphs the A/B mix position (no sibling uses MorphPos)
        s.lfo2.rateHz = 1.0f; s.lfo2.shape = 1;          // Triangle
        s.lfo2.sync = 1; s.lfo2Ext.noteValue = kNote1_4; // 1/4
        setModSlot(s, 0, kSrcLFO2, kDstAllMorphPos, 0.5f, kCurveSCurve);
        // Aftertouch drives comb feedback for expressive growl
        setVoiceRoute(s, 0, kVSrcAftertouch, kVDstFltRes, 0.5f);
        s.global.voiceMode = 1;
        s.monoMode.legato = 1; s.monoMode.portamentoTimeMs = 15.0f;
        presets.push_back(std::move(p));
    }

    // "Additive Sub" - pure additive sub-spectrum on a ladder, sub-doubled by a
    // PitchSync harmonizer, with a slow mod-env breathing the spectral tilt.
    {
        PresetDef p;
        p.name = "Additive Sub";
        p.category = "Bass";
        auto& s = p.state;
        s.oscA.type = 4;                                 // Additive
        s.oscA.additivePartials = 8; s.oscA.additiveTilt = -6.0f; // dark, rolled-off partials
        s.oscA.tuneSemitones = -12.0f; s.oscA.level = 0.85f;
        s.oscB.type = 0; s.oscB.waveform = 0;            // Sine reinforcement
        s.oscB.tuneSemitones = -12.0f; s.oscB.level = 0.5f;
        s.mixer.position = 0.35f;
        // A TRUE ladder (24 dB) with drive + keytrack - the audit's mislabelled "ladder" made real
        s.filter.type = 4;                               // Ladder
        s.filter.cutoffHz = 900.0f; s.filter.resonance = 4.09f;
        s.filter.ladderSlope = 4;                        // 24 dB/oct
        s.filter.ladderDrive = 3.0f;                     // gentle valve warmth
        s.filter.keyTrack = 0.5f;                        // follows pitch so high notes stay open
        s.ampEnv.attackMs = 4.0f; s.ampEnv.decayMs = 250.0f;
        s.ampEnv.sustain = 0.8f; s.ampEnv.releaseMs = 140.0f;
        // Slow mod-env (ENV3) breathes spectral tilt so the dark sub is never fully static
        s.modEnv.attackMs = 800.0f; s.modEnv.decayMs = 1500.0f;
        s.modEnv.sustain = 0.55f; s.modEnv.releaseMs = 1200.0f;
        setVoiceRoute(s, 0, kVSrcEnv3, kVDstSpecTilt, 0.5f);
        // PitchSync harmonizer locks a sub-octave onto the note for extra weight
        s.harmonizerEnabled = 1;
        s.harmonizer.pitchShiftMode = 3;                 // PitchSync
        s.harmonizer.numVoices = 1;
        s.harmonizer.voiceInterval[0] = -12;             // one octave down
        s.harmonizer.voiceLevelDb[0] = -2.0f;
        s.harmonizer.dryLevelDb = 0.0f; s.harmonizer.wetLevelDb = -3.0f;
        s.global.voiceMode = 1;
        s.monoMode.legato = 1; s.monoMode.portamentoTimeMs = 20.0f;
        presets.push_back(std::move(p));
    }

    // "Auto Wah" - funky envelope-filter bass with a chaotic LFO wobbling the
    // resonance for a living, squelchy wah. Owns EnvFilter + chaosMod on bass.
    {
        PresetDef p;
        p.name = "Auto Wah";
        p.category = "Bass";
        auto& s = p.state;
        s.oscA.type = 0; s.oscA.waveform = 1;            // Saw
        s.oscA.tuneSemitones = -12.0f; s.oscA.level = 0.85f;
        s.oscA.phase = 0.25f;                            // fixed start phase => consistent wah attack
        s.oscB.type = 0; s.oscB.waveform = 4;            // Triangle body
        s.oscB.tuneSemitones = -12.0f; s.oscB.level = 0.35f;
        s.oscB.freqMod = 0.2f;                           // slight FM growl sitting under the wah
        s.mixer.position = 0.25f;
        // Envelope FILTER (auto-wah) - opens upward on each note attack
        s.filter.type = 11;                              // EnvFilter
        s.filter.cutoffHz = 1400.0f; s.filter.resonance = 6.92f;
        s.filter.envSubType = 0;                         // LP response
        s.filter.envSensitivity = 8.0f; s.filter.envDepth = 0.8f;
        s.filter.envAttack = 3.0f; s.filter.envRelease = 60.0f;
        s.filter.envDirection = 0;                       // Up
        s.ampEnv.attackMs = 1.0f; s.ampEnv.decayMs = 200.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 100.0f;
        // IDENTITY gesture: a chaotic Lorenz LFO wobbles resonance (scale x2) => squelch that never repeats
        s.chaosMod.rateHz = 0.8f; s.chaosMod.type = 0;   // Lorenz
        s.chaosMod.depth = 0.6f;                         // must raise from 0 or the source is silent
        setModSlot(s, 0, kSrcChaos, kDstAllResonance, 0.4f, kCurveSCurve);
        s.modMatrix.slots[0].scale = 3;                  // x2 => exercise the untouched depth-scale axis
        // Aftertouch tips the wah's morph/centre for performable expression
        setVoiceRoute(s, 0, kVSrcAftertouch, kVDstMorphPos, 0.4f);
        s.global.voiceMode = 1;
        s.monoMode.legato = 1; s.monoMode.portamentoTimeMs = 15.0f;
        presets.push_back(std::move(p));
    }

    // "Massive" - Huge detuned bass with wavefolder and delay
    {
        PresetDef p;
        p.name = "Massive";
        p.category = "Bass";
        auto& s = p.state;
        s.oscA.type = 0; s.oscA.waveform = 1;            // Saw
        s.oscA.tuneSemitones = -12.0f; s.oscA.level = 0.85f;
        s.oscA.phase = 0.0f;
        s.oscB.type = 0; s.oscB.waveform = 1;            // Saw
        s.oscB.tuneSemitones = -12.0f; s.oscB.fineCents = 15.0f; s.oscB.level = 0.72f;
        s.oscB.phase = 0.5f;                             // half-cycle offset => fatter, hollow transient
        s.oscB.freqMod = 0.3f;                           // growl
        s.mixer.position = 0.5f;
        // SVF PEAK: a resonant low-mid bell boost for parallel-style punch (owns svfGain)
        s.filter.type = 8;                               // SVF Peak
        s.filter.cutoffHz = 180.0f; s.filter.resonance = 2.0f;
        s.filter.svfGain = 9.0f;                         // +9 dB low-mid punch
        s.filter.svfDrive = 8.0f;                        // post-filter saturation grit
        s.filter.envAmount = 14.0f;                      // small CORRECTED env sweep => attack punch on the peak
        s.filterEnv.attackMs = 2.0f; s.filterEnv.decayMs = 160.0f;
        s.filterEnv.sustain = 0.3f; s.filterEnv.releaseMs = 180.0f;
        // Lockhart wavefolder for harmonic aggression (mix<1 keeps the clean lows underneath)
        s.distortion.type = 4;                           // Wavefolder
        s.distortion.drive = 0.35f; s.distortion.foldType = 2; // Lockhart
        s.distortion.character = 0.5f; s.distortion.mix = 0.7f;
        // Global LP tames the wavefolder's bright top so it stays a BASS
        s.globalFilter.enabled = 1; s.globalFilter.type = 0; // LP
        s.globalFilter.cutoffHz = 4000.0f; s.globalFilter.resonance = 0.707f;
        s.ampEnv.attackMs = 2.0f; s.ampEnv.decayMs = 250.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 120.0f;
        // Free-time (un-synced) digital slap for width without tempo lock
        s.delayEnabled = 1;
        s.delay.type = 0;                                // Digital
        s.delay.sync = 0; s.delay.timeMs = 170.0f;
        s.delay.mix = 0.12f; s.delay.feedback = 0.2f; s.delay.digitalWidth = 130.0f;
        // IDENTITY gesture: a whole-note synced LFO sweeps the GLOBAL LP cutoff => slow tonal movement on top
        s.lfo2.rateHz = 1.0f; s.lfo2.shape = 0;          // Sine
        s.lfo2.sync = 1; s.lfo2Ext.noteValue = 19;       // 1/1 note
        setModSlot(s, 0, kSrcLFO2, kDstGlobalFltCut, 0.4f, kCurveSCurve);
        // Velocity drives the wavefolder amount for dynamic dirt
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstDistDrive, 0.4f);
        s.global.voiceMode = 1;
        s.monoMode.legato = 1; s.monoMode.portamentoTimeMs = 10.0f;
        presets.push_back(std::move(p));
    }

    // ==================== TEXTURES Category (15 new presets) ====================

    // "Shimmer Verb" - Frozen modulated-Hall octave shimmer that breathes
    {
        PresetDef p;
        p.name = "Shimmer Verb";
        p.category = "Textures";
        auto& s = p.state;
        // OSC: PolyBLEP saw body + a glassy triangle an octave up for air
        s.oscA.type = 0; s.oscA.waveform = 1; s.oscA.level = 0.6f;   // saw fundamental
        s.oscB.type = 0; s.oscB.waveform = 4;                        // triangle = pure top
        s.oscB.tuneSemitones = 12.0f; s.oscB.level = 0.3f;
        s.mixer.mode = 0; s.mixer.position = 0.35f;                 // favour the saw
        // FILTER: SVF Allpass (category owns it) - flat magnitude, phase-only.
        // Sweeping its cutoff via ModEnv creates a phaser-like notch once summed
        // through the widened/harmonised/reverberated stereo field.
        s.filter.type = 7; s.filter.cutoffHz = 6000.0f; s.filter.resonance = 4.09f;
        s.filter.svfSlope = 1;
        s.filter.envAmount = 20.0f;                                 // +20 st allpass sweep (bug-fixed range)
        s.filterEnv.attackMs = 1500.0f; s.filterEnv.decayMs = 3000.0f;
        s.filterEnv.sustain = 0.4f; s.filterEnv.releaseMs = 2000.0f;
        // AMP: long glassy swell, distinct from the old 600/800/0.7/2500 wrapper
        s.ampEnv.attackMs = 800.0f; s.ampEnv.decayMs = 1200.0f;
        s.ampEnv.sustain = 0.75f; s.ampEnv.releaseMs = 3000.0f;
        s.ampEnv.attackCurve = 0.4f;                                // slow-start swell
        // ModEnv drives the allpass sweep (voice route below)
        s.modEnv.attackMs = 40.0f; s.modEnv.decayMs = 4000.0f;
        s.modEnv.sustain = 0.6f; s.modEnv.releaseMs = 2000.0f;
        // HARMONIZER: octave + octave-fifth PhaseVocoder shimmer
        s.harmonizerEnabled = 1;
        s.harmonizer.harmonyMode = 0;      // Chromatic
        s.harmonizer.pitchShiftMode = 2;   // PhaseVocoder (clean shimmer)
        s.harmonizer.numVoices = 2;
        s.harmonizer.dryLevelDb = 0.0f; s.harmonizer.wetLevelDb = -4.0f;
        s.harmonizer.voiceInterval[0] = 12; s.harmonizer.voicePan[0] = -0.45f;
        s.harmonizer.voiceInterval[1] = 19; s.harmonizer.voicePan[1] = 0.45f;
        s.harmonizer.voiceLevelDb[1] = -6.0f;
        // LFO1: very slow, free-running, long fade-in -> Effect Mix so the whole
        // wet bed (shimmer + reverb) swells in and breathes. SCurve for smoothness.
        s.lfo1.rateHz = 0.1f; s.lfo1.shape = 0; s.lfo1.depth = 0.8f; s.lfo1.sync = 0;
        s.lfo1Ext.fadeInMs = 3000.0f;      // shimmer fades up over 3 s
        s.modMatrix.slots[0].source = 1;   // LFO1
        s.modMatrix.slots[0].dest   = 3;   // Effect Mix (breathing wet)
        s.modMatrix.slots[0].amount = 0.5f;
        s.modMatrix.slots[0].curve  = 2;   // SCurve
        s.modMatrix.slots[0].smoothMs = 20.0f;
        s.modMatrix.slots[0].scale  = 2;   // x1
        // LFO2: independent slow drift of the A/B morph so timbre never sits still
        s.lfo2.rateHz = 0.13f; s.lfo2.shape = 0; s.lfo2.depth = 0.7f; s.lfo2.sync = 0;
        s.modMatrix.slots[1].source = 2;   // LFO2
        s.modMatrix.slots[1].dest   = 5;   // All-voice Morph Position
        s.modMatrix.slots[1].amount = 0.3f;
        s.modMatrix.slots[1].curve  = 0;   // Linear
        // Voice routes: ModEnv sweeps the allpass; Aftertouch adds live morph motion
        s.voiceRoutes[0].source = 2; s.voiceRoutes[0].destination = 0; // Env3 -> FltCut
        s.voiceRoutes[0].amount = 0.4f; s.voiceRoutes[0].active = 1;
        s.voiceRoutes[1].source = 7; s.voiceRoutes[1].destination = 2; // Aftertouch -> MorphPos
        s.voiceRoutes[1].amount = 0.45f; s.voiceRoutes[1].active = 1;
        // REVERB: big FROZEN, MODULATED Hall - the signature. Dry voice (mix 0.45)
        // stays fully audible even if the freeze holds the tail.
        s.reverbEnabled = 1; s.reverbType = 1;    // Hall
        s.reverb.size = 0.9f; s.reverb.mix = 0.45f;
        s.reverb.damping = 0.2f; s.reverb.diffusion = 0.9f;
        s.reverb.preDelayMs = 40.0f;
        s.reverb.modRateHz = 0.2f; s.reverb.modDepth = 0.25f;
        s.reverb.freeze = 1;                       // frozen shimmer bed
        // Wide stereo image with unison spread
        s.global.width = 1.7f; s.global.spread = 0.4f;
        presets.push_back(std::move(p));
    }

    // "Ferric Storm" (was Modular Noise) - note-tracked ring-mod chaos noise
    {
        PresetDef p;
        p.name = "Ferric Storm";
        p.category = "Textures";
        auto& s = p.state;
        // OSC A: pink noise bed
        s.oscA.type = 9; s.oscA.noiseColor = 1; s.oscA.level = 0.55f;   // pink
        // OSC B: Chua chaos attractor, Y-axis output with cross-coupling for grit
        s.oscB.type = 5; s.oscB.chaosAttractor = 2;                    // Chua
        s.oscB.chaosAmount = 0.5f; s.oscB.chaosCoupling = 0.4f;        // instability
        s.oscB.chaosOutput = 1;                                        // Y axis timbre
        s.oscB.level = 0.45f;
        s.mixer.mode = 0; s.mixer.position = 0.45f;
        // FILTER: resonant SVF Band-Pass carves a formant band out of the noise
        // Bandpass on pink noise: keep Q moderate so the band still passes level.
        s.filter.type = 2; s.filter.cutoffHz = 1500.0f; s.filter.resonance = 2.0f;
        s.global.masterGain = 1.8f;   // BP + 50% ring mod on a noise bed costs a lot of level
        s.filter.svfSlope = 1;
        s.filter.envAmount = 18.0f;                                    // slow BP sweep on note (bug-fixed)
        s.filterEnv.attackMs = 400.0f; s.filterEnv.decayMs = 1500.0f;
        s.filterEnv.sustain = 0.35f; s.filterEnv.releaseMs = 800.0f;
        // DISTORTION: note-tracked ring modulator -> metallic, pitch-following clang
        s.distortion.type = 6;                     // Ring Modulator
        s.distortion.drive = 0.3f; s.distortion.mix = 0.5f;
        s.distortion.ringFreqMode = 1;             // NoteTrack (follows played pitch)
        s.distortion.ringRatio = 0.35f;            // normalized -> clangorous ratio
        s.distortion.ringWaveform = 2;             // saw carrier
        s.distortion.ringStereoSpread = 0.5f;      // stereo shimmer on the ring
        // AMP: medium swell
        s.ampEnv.attackMs = 300.0f; s.ampEnv.decayMs = 1000.0f;
        s.ampEnv.sustain = 0.8f; s.ampEnv.releaseMs = 1800.0f;
        // LFO1: Sample & Hold, free, quantized -> steps the band-pass cutoff.
        // scale x2 widens the sweep (mod-matrix scale axis, otherwise unused).
        s.lfo1.rateHz = 3.0f; s.lfo1.shape = 4; s.lfo1.depth = 0.7f; s.lfo1.sync = 0;
        s.lfo1Ext.quantizeSteps = 6;               // stair-stepped motion
        s.modMatrix.slots[0].source = 1;           // LFO1 (S&H)
        s.modMatrix.slots[0].dest   = 4;           // All-voice Filter Cutoff
        s.modMatrix.slots[0].amount = 0.45f;
        s.modMatrix.slots[0].curve  = 0;           // Linear
        s.modMatrix.slots[0].scale  = 3;           // x2 depth
        // Transient detector -> resonance: the storm spikes on its own attacks
        s.transient.sensitivity = 0.7f; s.transient.attackMs = 2.0f; s.transient.decayMs = 60.0f;
        s.modMatrix.slots[1].source = 13;          // Transient
        s.modMatrix.slots[1].dest   = 8;           // All-voice Resonance
        s.modMatrix.slots[1].amount = 0.4f;
        s.modMatrix.slots[1].curve  = 1;           // Exponential
        // Smoothed Random -> spectral tilt for slow underlying wander
        s.random.rateHz = 0.4f; s.random.smoothness = 0.8f; s.random.sync = 0;
        s.modMatrix.slots[2].source = 4;           // Random
        s.modMatrix.slots[2].dest   = 7;           // All-voice Spectral Tilt
        s.modMatrix.slots[2].amount = 0.3f;
        s.modMatrix.slots[2].smoothMs = 40.0f;
        // DELAY: PingPong for wide rhythmic scatter
        s.delayEnabled = 1; s.delay.type = 2;      // PingPong
        s.delay.timeMs = 375.0f; s.delay.feedback = 0.4f; s.delay.mix = 0.28f;
        s.delay.sync = 0;
        s.delay.pingPongRatio = 2; s.delay.pingPongCrossFeed = 0.8f;
        s.delay.pingPongWidth = 140.0f; s.delay.pingPongModDepth = 0.2f;
        // REVERB: small Plate (contrast to the big halls elsewhere)
        s.reverbEnabled = 1; s.reverbType = 0;     // Plate
        s.reverb.size = 0.55f; s.reverb.mix = 0.3f; s.reverb.damping = 0.4f;
        s.reverb.diffusion = 0.6f;
        s.global.width = 1.5f; s.global.spread = 0.5f;
        presets.push_back(std::move(p));
    }

    // "Digital Decay" - rungler-glitched additive drone through a tuned comb, rhythmic aged digital echoes
    {
        PresetDef p;
        p.name = "Digital Decay";
        p.category = "Textures";
        auto& s = p.state;
        // OSC A: full 128-partial additive with inharmonicity -> metallic digital body
        s.oscA.type = 4; s.oscA.additivePartials = 128;
        s.oscA.additiveTilt = -3.0f; s.oscA.additiveInharm = 0.25f;   // slightly clangorous
        s.oscA.level = 0.7f;
        // OSC B: sub saw for weight the rungler crossfades against
        s.oscB.type = 0; s.oscB.waveform = 1; s.oscB.tuneSemitones = -12.0f;
        s.oscB.level = 0.35f;
        // MIXER: plain CROSSFADE (not SpectralMorph) -> the rungler's Morph-Position glitch
        // (slot 0) hard-cuts between the additive body and the sub-saw. Morph Position is
        // audible in Crossfade mode, so no spectral-domain dependency is needed here.
        s.mixer.mode = 0; s.mixer.position = 0.35f;
        // FILTER: tuned COMB -> metallic, key-tracked resonant body (replaces the shared SVF HP)
        s.filter.type = 6;                // Comb
        s.filter.cutoffHz = 220.0f;
        s.filter.resonance = 7.48f;
        s.filter.combDamping = 0.3f;
        s.filter.keyTrack = 0.5f;
        // DISTORTION: WAVEFOLDER -> harsh digital fold (replaces the shared spectral bitcrush)
        s.distortion.type = 4;            // Wavefolder
        s.distortion.drive = 0.5f;
        s.distortion.foldType = 1;
        s.distortion.mix = 0.6f;
        // AMP: slow swell so the stepped rungler rhythm emerges under a sustained bed
        s.ampEnv.attackMs = 1200.0f; s.ampEnv.decayMs = 700.0f;
        s.ampEnv.sustain = 0.75f; s.ampEnv.releaseMs = 2200.0f;
        s.ampEnv.attackCurve = 0.5f;
        // ModEnv: long rise that pushes the wavefolder drive deeper over the note
        s.modEnv.attackMs = 3000.0f; s.modEnv.decayMs = 4000.0f;
        s.modEnv.sustain = 0.8f; s.modEnv.releaseMs = 1500.0f;
        // --- RUNGLER is the identity: a Benjolin shift-register driving stepped glitch motion ---
        s.rungler.osc1FreqHz = 2.5f; s.rungler.osc2FreqHz = 3.7f;
        s.rungler.depth = 0.7f; s.rungler.bits = 6; s.rungler.loopMode = 0; // 6-bit chaos pattern
        // Rungler -> Morph Position: hard stepped crossfade jumps between the two oscillators
        s.modMatrix.slots[0].source = kSrcRungler;      // 10
        s.modMatrix.slots[0].dest   = kDstAllMorphPos;  // 5
        s.modMatrix.slots[0].amount = 0.55f;
        s.modMatrix.slots[0].curve  = kCurveStepped;    // 3
        // Rungler -> Comb cutoff: pitch-glitches the metallic comb in lockstep
        s.modMatrix.slots[1].source = kSrcRungler;      // 10
        s.modMatrix.slots[1].dest   = kDstAllFltCut;    // 4
        s.modMatrix.slots[1].amount = 0.45f;
        s.modMatrix.slots[1].curve  = kCurveStepped;    // 3
        // Secondary stepped motion: glacial SmoothRandom LFO, quantized -> All-voice resonance,
        // Stepped curve, x2 scale so the comb feedback pulses in discrete stages.
        s.lfo1.rateHz = 0.08f; s.lfo1.shape = 5; s.lfo1.depth = 0.6f; s.lfo1.sync = 0;
        s.lfo1Ext.quantizeSteps = 8;
        s.modMatrix.slots[2].source = kSrcLFO1;         // 1 (SmoothRandom)
        s.modMatrix.slots[2].dest   = kDstAllResonance; // 8
        s.modMatrix.slots[2].amount = 0.5f;
        s.modMatrix.slots[2].curve  = kCurveStepped;    // 3 (owns Stepped curve)
        s.modMatrix.slots[2].scale  = 3;                // x2 (owns the depth-scale axis)
        // Voice route: ModEnv drives wavefolder drive so the fold deepens across the note
        s.voiceRoutes[0].source = kVSrcEnv3; s.voiceRoutes[0].destination = kVDstDistDrive; // Env3 -> DistDrive
        s.voiceRoutes[0].amount = 0.6f; s.voiceRoutes[0].active = 1;
        // GLOBAL FILTER: a static NOTCH scoops the low-mids out of the clangorous 128-partial
        // body -> a hollow, bell-like spectral scoop that tames the boxy inharmonic buildup.
        s.globalFilter.enabled = 1; s.globalFilter.type = 3;   // Notch (spectral scoop)
        s.globalFilter.cutoffHz = 700.0f; s.globalFilter.resonance = 1.2f;
        // DELAY: NON-spectral DIGITAL, synced -> rhythmic, degrading digital echoes
        s.delayEnabled = 1; s.delay.type = 0;           // Digital
        s.delay.sync = 1; s.delay.noteValue = 7;        // 1/16 rhythmic glitch trails
        s.delay.feedback = 0.5f; s.delay.mix = 0.32f;
        s.delay.digitalAge = 0.6f;                      // bit/sample degradation on the trail
        s.delay.digitalModDepth = 0.3f; s.delay.digitalModRateHz = 0.7f;
        s.delay.digitalWavefoldAmt = 0.25f;
        s.delay.digitalWidth = 140.0f;
        // REVERB: small Plate (Spectral Ghost owns the big modulated Hall)
        s.reverbEnabled = 1; s.reverbType = 0;          // Plate
        s.reverb.size = 0.5f; s.reverb.mix = 0.24f; s.reverb.damping = 0.4f;
        s.reverb.diffusion = 0.7f;
        s.global.width = 1.3f; s.global.spread = 0.3f;
        presets.push_back(std::move(p));
    }

    // "Degauss Drift" (was Tape Artifact) - wobbling-notch worn-tape hiss bed
    {
        PresetDef p;
        p.name = "Degauss Drift";
        p.category = "Textures";
        auto& s = p.state;
        // OSC A: brown noise = dark tape rumble
        s.oscA.type = 9; s.oscA.noiseColor = 2; s.oscA.level = 0.5f;   // brown
        // OSC B: sub-octave saw for pitched weight under the hiss
        s.oscB.type = 0; s.oscB.waveform = 1; s.oscB.tuneSemitones = -12.0f;
        s.oscB.level = 0.3f;
        s.mixer.mode = 0; s.mixer.position = 0.35f;
        // FILTER: SVF Notch (category owns it) - carves a moving gap out of the noise
        s.filter.type = 3; s.filter.cutoffHz = 1200.0f; s.filter.resonance = 5.22f;
        s.filter.svfSlope = 1;
        s.filter.envAmount = 15.0f;                                    // slow notch rise (bug-fixed)
        s.filterEnv.attackMs = 600.0f; s.filterEnv.decayMs = 2000.0f;
        s.filterEnv.sustain = 0.4f; s.filterEnv.releaseMs = 1000.0f;
        // DISTORTION: hysteresis TapeSaturator -> warm, worn magnetic grit
        s.distortion.type = 5; s.distortion.drive = 0.6f;
        s.distortion.tapeModel = 1;                // Hysteresis
        s.distortion.tapeSaturation = 0.7f; s.distortion.tapeBias = 0.4f;
        s.distortion.mix = 0.8f;
        // AMP: medium bed
        s.ampEnv.attackMs = 250.0f; s.ampEnv.decayMs = 700.0f;
        s.ampEnv.sustain = 0.65f; s.ampEnv.releaseMs = 1400.0f;
        // LFO1: slow triangle with a 90-deg phase offset -> notch cutoff wobble
        s.lfo1.rateHz = 0.25f; s.lfo1.shape = 1; s.lfo1.depth = 0.6f; s.lfo1.sync = 0;
        s.lfo1Ext.phaseOffset = 90.0f;
        s.modMatrix.slots[0].source = 1;           // LFO1
        s.modMatrix.slots[0].dest   = 4;           // All-voice Filter Cutoff (notch freq)
        s.modMatrix.slots[0].amount = 0.4f;
        s.modMatrix.slots[0].curve  = 0;
        s.modMatrix.slots[0].scale  = 3;           // x2 for a wide wobble
        // Chaos LFO (Rossler) -> resonance: organic pitch-of-wow instability
        s.chaosMod.rateHz = 0.3f; s.chaosMod.type = 1;  // Rossler
        s.chaosMod.depth = 0.4f; s.chaosMod.sync = 0;
        s.modMatrix.slots[1].source = 9;           // Chaos
        s.modMatrix.slots[1].dest   = 8;           // All-voice Resonance
        s.modMatrix.slots[1].amount = 0.3f;
        s.modMatrix.slots[1].curve  = 0;
        // Slewed Sample & Hold -> morph position: slow random head-balance drift
        s.sampleHold.rateHz = 0.5f; s.sampleHold.slewMs = 120.0f; s.sampleHold.sync = 0;
        s.modMatrix.slots[2].source = 11;          // Sample & Hold
        s.modMatrix.slots[2].dest   = 5;           // All-voice Morph Position
        s.modMatrix.slots[2].amount = 0.25f;
        s.modMatrix.slots[2].smoothMs = 30.0f;
        // DELAY: Tape with heavy wear -> the wobble/dropout artifacts
        s.delayEnabled = 1; s.delay.type = 1;      // Tape
        s.delay.timeMs = 450.0f; s.delay.feedback = 0.35f; s.delay.mix = 0.22f;
        s.delay.tapeSaturation = 0.5f; s.delay.tapeWear = 0.5f; s.delay.tapeAge = 0.3f;
        // REVERB: small dark Plate
        s.reverbEnabled = 1; s.reverbType = 0;     // Plate
        s.reverb.size = 0.45f; s.reverb.mix = 0.24f; s.reverb.damping = 0.5f;
        s.global.width = 1.2f; s.global.spread = 0.2f;
        presets.push_back(std::move(p));
    }

    // "Vowel Ghost" - dual Formant (I vs U), unipolar morph, frozen granular cloud
    {
        PresetDef p;
        p.name = "Vowel Ghost";
        p.category = "Textures";
        auto& s = p.state;
        // OSC A + B: two Formant engines on OPPOSITE CLOSE vowels. This trio's bright
        // member: 'I' (ee) against 'U' (oo) - distinct from Formant Sea (A/O) and
        // Vowel Cloud (E/O). Morphing the mixer position makes the vowel genuinely shift.
        s.oscA.type = 7; s.oscA.formantVowel = 2; s.oscA.formantMorph = 2.0f; // 'I'
        s.oscA.level = 0.65f;
        s.oscB.type = 7; s.oscB.formantVowel = 4; s.oscB.formantMorph = 3.8f; // 'U'
        s.oscB.level = 0.65f;
        // SpectralMorph mixer = FFT interpolation A<->B = a real vowel morph
        s.mixer.mode = 1;                          // SpectralMorph
        s.mixer.position = 0.5f; s.mixer.tilt = 0.0f;
        // FILTER: resonant Comb adds a metallic, hollow throat body
        s.filter.type = 6; s.filter.cutoffHz = 600.0f; s.filter.resonance = 6.35f;
        s.filter.combDamping = 0.4f;
        // AMP: breathy slow swell, high sustain
        s.ampEnv.attackMs = 900.0f; s.ampEnv.decayMs = 600.0f;
        s.ampEnv.sustain = 0.85f; s.ampEnv.releaseMs = 2200.0f;
        s.ampEnv.attackCurve = 0.4f;
        // HARMONIZER: Scalic minor, GRANULAR pitch mode (covers the granular variant),
        // formant-preserved so the harmonised voices stay vocal
        s.harmonizerEnabled = 1;
        s.harmonizer.harmonyMode = 1;      // Scalic
        s.harmonizer.key = 0; s.harmonizer.scale = 1;  // Natural Minor
        s.harmonizer.pitchShiftMode = 1;   // Granular
        s.harmonizer.formantPreserve = 1;
        s.harmonizer.numVoices = 2;
        s.harmonizer.dryLevelDb = 0.0f; s.harmonizer.wetLevelDb = -5.0f;
        s.harmonizer.voiceInterval[0] = 3;  s.harmonizer.voicePan[0] = -0.5f;
        s.harmonizer.voiceInterval[1] = -5; s.harmonizer.voicePan[1] = 0.5f;
        // LFO1: slow, free, UNIPOLAR (full 0..1 sweep), long fade-in -> Morph Position.
        // Unipolar makes it sweep the whole I->U vowel range in one direction.
        s.lfo1.rateHz = 0.12f; s.lfo1.shape = 0; s.lfo1.depth = 0.9f; s.lfo1.sync = 0;
        s.lfo1Ext.unipolar = 1; s.lfo1Ext.fadeInMs = 2000.0f;
        s.modMatrix.slots[0].source = 1;           // LFO1
        s.modMatrix.slots[0].dest   = 5;           // All-voice Morph Position (vowel morph)
        s.modMatrix.slots[0].amount = 0.6f;
        s.modMatrix.slots[0].curve  = 2;           // SCurve
        s.modMatrix.slots[0].smoothMs = 15.0f;
        // Envelope follower -> comb cutoff: the ghost's throat opens with dynamics
        s.envFollower.sensitivity = 0.7f; s.envFollower.attackMs = 20.0f; s.envFollower.releaseMs = 200.0f;
        s.modMatrix.slots[1].source = 3;           // Env Follower
        s.modMatrix.slots[1].dest   = 4;           // All-voice Filter Cutoff
        s.modMatrix.slots[1].amount = 0.3f;
        s.modMatrix.slots[1].curve  = 0;
        // Voice route: velocity brightens the comb for playing expression
        s.voiceRoutes[0].source = 5; s.voiceRoutes[0].destination = 0; // Velocity -> FltCut
        s.voiceRoutes[0].amount = 0.4f; s.voiceRoutes[0].active = 1;
        // DELAY: Granular, FROZEN -> a held vocal grain-cloud (this patch's spatial identity)
        s.delayEnabled = 1; s.delay.type = 3;      // Granular
        s.delay.timeMs = 500.0f; s.delay.feedback = 0.3f; s.delay.mix = 0.3f;
        s.delay.granularSizeMs = 140.0f; s.delay.granularDensity = 14.0f;
        s.delay.granularPitchSpray = 0.2f; s.delay.granularTexture = 0.3f;
        s.delay.granularFreeze = 1;                // frozen grain cloud
        // REVERB: large modulated Hall
        s.reverbEnabled = 1; s.reverbType = 1;     // Hall
        s.reverb.size = 0.85f; s.reverb.mix = 0.4f; s.reverb.damping = 0.25f;
        s.reverb.diffusion = 0.85f; s.reverb.preDelayMs = 30.0f;
        s.reverb.modRateHz = 0.3f; s.reverb.modDepth = 0.15f;
        s.global.width = 1.6f; s.global.spread = 0.45f;
        presets.push_back(std::move(p));
    }

    // "Rosin Halo" - Noise+Additive bowed into a resonant comb; pitch-follower tunes the resonance
    {
        PresetDef p;
        p.name = "Rosin Halo";
        p.category = "Textures";
        auto& s = p.state;
        // --- Excitation: pink-noise bow-scrape + inharmonic additive string body ---
        s.oscA.type = 9;              // Noise
        s.oscA.noiseColor = 1;        // Pink - soft bow-hair air, not white hiss
        s.oscA.level = 0.30f;
        s.oscB.type = 4;              // Additive
        s.oscB.additivePartials = 24; // rich body without glassy top
        s.oscB.additiveTilt = -6.0f;  // roll off highs -> warm rosin timbre
        s.oscB.additiveInharm = 0.08f;// slight string stretch (metallic edge)
        s.oscB.level = 0.42f;
        s.mixer.position = 0.55f;     // favor the tuned additive body over the noise
        // --- Resonant comb = the "string" the excitation drives ---
        s.filter.type = 6;            // Comb
        s.filter.cutoffHz = 440.0f;   // comb tuned near A4
        s.filter.resonance = 10.30f;   // long ringing bow tail
        s.filter.combDamping = 0.30f; // bow damping in the feedback path
        s.filter.envAmount = 18.0f;   // FIX: plain semitones (audible swell) - not a Hz value
        // Filter env = the bow drawing across the string (slow rise, no re-attack)
        s.filterEnv.attackMs = 400.0f;
        s.filterEnv.decayMs = 1200.0f;
        s.filterEnv.sustain = 0.6f;
        s.filterEnv.releaseMs = 1800.0f;
        s.filterEnv.attackCurve = 0.5f; // slow-start swell
        // --- Amp: bowed onset, long sustaining tail ---
        s.ampEnv.attackMs = 300.0f;
        s.ampEnv.decayMs = 1500.0f;
        s.ampEnv.sustain = 0.55f;
        s.ampEnv.releaseMs = 2600.0f;
        // === MOD IDENTITY: the comb resonance tracks the pitch you play ===
        s.pitchFollower.minHz = 80.0f;
        s.pitchFollower.maxHz = 1200.0f;
        s.pitchFollower.confidence = 0.6f;
        s.pitchFollower.speedMs = 25.0f;   // fast, so the comb re-tunes as you play
        setModSlot(s, 0, 12, 4, 0.55f, 2, 20.0f); // PitchFollow -> AllFltCut, SCurve, gentle smooth
        s.modMatrix.slots[0].scale = 3;    // x2 depth -> wider comb re-tuning range (scale axis)
        // Second gesture: transient detector adds a bright bow-scrape on each attack
        s.transient.sensitivity = 0.6f;
        s.transient.attackMs = 2.0f;
        s.transient.decayMs = 80.0f;
        setModSlot(s, 1, 13, 8, 0.35f, 1); // Transient -> AllResonance, Exp - scrape bite
        // --- Space: big diffuse Hall with pre-delay (no delay line) ---
        s.reverbEnabled = 1;
        s.reverbType = 1;             // Hall
        s.reverb.size = 0.75f;
        s.reverb.mix = 0.32f;
        s.reverb.preDelayMs = 40.0f;  // air before the wash
        s.reverb.diffusion = 0.85f;
        s.global.width = 1.5f;
        presets.push_back(std::move(p));
    }

    // "Ping Garden" - Particle grains ping a self-osc filter; stepped-random tuning, ping-pong echoes
    {
        PresetDef p;
        p.name = "Ping Garden";
        p.category = "Textures";
        auto& s = p.state;
        // --- Excitation: airy noise + sparse particle grains ---
        s.oscA.type = 9;              // Noise
        s.oscA.noiseColor = 3;        // Blue - bright airy sparkle for glassy pings
        s.oscA.level = 0.15f;
        s.oscB.type = 6;              // Particle
        s.oscB.particleScatter = 8.0f;
        s.oscB.particleDensity = 6.0f;   // sparse - individual droplets
        s.oscB.particleLifetime = 80.0f; // short percussive grains
        s.oscB.particleSpawnMode = 1;    // Random spawn (irregular rain)
        s.oscB.particleEnvType = 0;      // Hann - clean pings
        s.oscB.particleDrift = 0.2f;
        s.oscB.level = 0.32f;
        s.mixer.position = 0.55f;
        // --- Self-oscillating filter = the resonant bell being struck ---
        s.filter.type = 12;           // Self-Oscillating
        s.filter.cutoffHz = 800.0f;
        s.filter.resonance = 20.0f;   // rings on its own
        s.filter.selfOscGlide = 120.0f;
        s.filter.selfOscExtMix = 0.4f;
        s.filter.selfOscShape = 0.3f;
        s.filter.selfOscRelease = 1400.0f;
        // --- Amp: pluck onset, low sustain, long ring ---
        s.ampEnv.attackMs = 15.0f;
        s.ampEnv.decayMs = 900.0f;
        s.ampEnv.sustain = 0.2f;
        s.ampEnv.releaseMs = 2200.0f;
        // === MOD IDENTITY: stepped-random LFO re-tunes the ping pitch (arpeggiated droplets) ===
        s.lfo1.rateHz = 0.9f;          // slow wander
        s.lfo1.shape = 5;              // SmoothRandom
        s.lfo1.depth = 0.7f;
        s.lfo1.sync = 0;               // free-running
        s.lfo1Ext.quantizeSteps = 5;   // snap to 5 discrete pitches -> pentatonic-ish pings
        s.lfo1Ext.phaseOffset = 90.0f; // stagger so L/R droplets feel offset
        s.lfo1Ext.fadeInMs = 2000.0f;  // pings drift in over 2s
        setModSlot(s, 0, 1, 4, 0.6f, 3); // LFO1 -> AllFltCut, Stepped curve (hard pitch steps)
        // Second gesture: sample&hold jitters the ring intensity
        s.sampleHold.rateHz = 2.5f;
        s.sampleHold.slewMs = 40.0f;
        setModSlot(s, 1, 11, 8, 0.3f);   // S&H -> AllResonance - sparkle variation
        // --- FX: ping-pong echoes into a small bright plate ---
        s.delayEnabled = 1;
        s.delay.type = 2;              // PingPong
        s.delay.mix = 0.3f;
        s.delay.feedback = 0.45f;
        s.delay.pingPongWidth = 180.0f;
        s.reverbEnabled = 1;
        s.reverbType = 0;             // Plate - tight and bright, contrasts the Halls
        s.reverb.size = 0.45f;
        s.reverb.mix = 0.3f;
        s.global.width = 1.6f;
        presets.push_back(std::move(p));
    }

    // "Casio Nebula" - Twin phase-distortion waves spectrally morphed; dual slow LFOs evolve morph & tilt
    {
        PresetDef p;
        p.name = "Casio Nebula";
        p.category = "Textures";
        auto& s = p.state;
        // --- Two resonant CZ phase-distortion oscillators ---
        s.oscA.type = 2;              // Phase Distortion
        s.oscA.pdWaveform = 5;        // ResSaw - formant-y resonant sweep
        s.oscA.pdDistortion = 0.5f;
        s.oscA.level = 0.7f;
        s.oscB.type = 2;
        s.oscB.pdWaveform = 6;        // ResTri
        s.oscB.pdDistortion = 0.65f;
        s.oscB.tuneSemitones = 12.0f; // octave up for shimmer
        s.oscB.fineCents = 4.0f;      // slow beating
        s.oscB.level = 0.55f;
        // --- SpectralMorph mixer: FFT interpolation A<->B, tilted ---
        s.mixer.mode = 1;             // Spectral Morph
        s.mixer.position = 0.5f;
        s.mixer.tilt = 3.0f;          // +dB/oct spectral tilt (morph-only param) -> airy top
        // --- Resonant allpass for phasey smear (no LP dulling) ---
        s.filter.type = 7;            // SVF Allpass
        s.filter.cutoffHz = 1500.0f;
        s.filter.resonance = 2.0f;    // resonant allpass coloration
        s.filter.svfDrive = 3.0f;     // gentle saturation
        // --- Slow pad envelope ---
        s.ampEnv.attackMs = 800.0f;
        s.ampEnv.decayMs = 1000.0f;
        s.ampEnv.sustain = 0.75f;
        s.ampEnv.releaseMs = 2400.0f;
        s.ampEnv.attackCurve = 0.4f;
        // === MOD IDENTITY: two out-of-phase slow LFOs evolve morph position and spectral tilt ===
        s.lfo1.rateHz = 0.08f;         // ultra-slow morph drift
        s.lfo1.shape = 0;              // Sine
        s.lfo1.depth = 0.8f;
        s.lfo1.sync = 0;
        s.lfo1Ext.symmetry = 0.7f;     // skew the sine -> asymmetric evolution
        setModSlot(s, 0, 1, 5, 0.6f, 2); // LFO1 -> AllMorphPos, SCurve
        s.lfo2.rateHz = 0.05f;         // even slower, different period (never re-syncs)
        s.lfo2.shape = 1;              // Triangle
        s.lfo2.depth = 0.6f;
        s.lfo2.sync = 0;
        setModSlot(s, 1, 2, 7, 0.5f, 2); // LFO2 -> AllSpecTilt, SCurve
        // --- Spectral delay smear + modulated Hall ---
        s.delayEnabled = 1;
        s.delay.type = 4;             // Spectral
        s.delay.timeMs = 600.0f;
        s.delay.feedback = 0.35f;
        s.delay.mix = 0.28f;
        s.delay.spectralSpreadMs = 400.0f; // smear frequencies across time
        s.delay.spectralTilt = 0.3f;
        s.reverbEnabled = 1;
        s.reverbType = 1;             // Hall
        s.reverb.size = 0.7f;
        s.reverb.mix = 0.3f;
        s.reverb.modDepth = 0.3f;     // chorused reverb tail -> shimmer
        s.reverb.modRateHz = 0.4f;
        presets.push_back(std::move(p));
    }

    // "Attractor Storm" - Twin chaos attractors swept by an x4-scaled LFO through a howling bandpass
    {
        PresetDef p;
        p.name = "Attractor Storm";
        p.category = "Textures";
        auto& s = p.state;
        // --- Dual chaotic oscillators (different attractors, different output axes) ---
        s.oscA.type = 5;              // Chaos
        s.oscA.chaosAttractor = 0;    // Lorenz
        s.oscA.chaosAmount = 0.6f;
        s.oscA.chaosCoupling = 0.3f;
        s.oscA.chaosOutput = 0;       // X axis
        s.oscA.level = 0.6f;
        s.oscB.type = 5;
        s.oscB.chaosAttractor = 1;    // Rossler
        s.oscB.chaosAmount = 0.5f;
        s.oscB.chaosCoupling = 0.45f;
        s.oscB.chaosOutput = 1;       // Y axis - different timbre than A
        s.oscB.level = 0.5f;
        s.mixer.position = 0.5f;
        // --- Resonant bandpass = the storm's shrieking window ---
        s.filter.type = 2;            // SVF BP
        s.filter.cutoffHz = 1200.0f;
        s.filter.resonance = 3.0f;    // base howl; the LFO pushes it far higher
        s.filter.svfSlope = 1;        // 24 dB steep band
        // --- Amp: slow surge ---
        s.ampEnv.attackMs = 500.0f;
        s.ampEnv.decayMs = 700.0f;
        s.ampEnv.sustain = 0.75f;
        s.ampEnv.releaseMs = 1500.0f;
        // === MOD IDENTITY: LFO with SCALE=x4 drives EXTREME resonance sweeps ===
        s.lfo1.rateHz = 0.2f;
        s.lfo1.shape = 1;              // Triangle
        s.lfo1.depth = 0.8f;
        s.lfo1.sync = 0;
        setModSlot(s, 0, 1, 8, 0.7f, 2); // LFO1 -> AllResonance, SCurve
        s.modMatrix.slots[0].scale = 4;  // x4 - pushes amount past +/-1 -> screaming resonance
        // Second LFO sweeps the band center so the shriek moves in pitch
        s.lfo2.rateHz = 0.13f;
        s.lfo2.shape = 0;              // Sine
        s.lfo2.depth = 0.7f;
        s.lfo2.sync = 0;
        setModSlot(s, 1, 2, 4, 0.5f);    // LFO2 -> AllFltCut
        // Third gesture: the dedicated chaos-mod LFO warps the morph blend
        s.chaosMod.rateHz = 0.6f;
        s.chaosMod.type = 0;           // Lorenz
        s.chaosMod.depth = 0.5f;       // raise from default 0 so it is audible as a source
        setModSlot(s, 2, 9, 5, 0.4f);  // Chaos -> AllMorphPos
        // --- FX: slow phaser + tape-wow delay into a plate ---
        s.modulationType = 1;          // Phaser
        s.phaser.rateHz = 0.3f;
        s.phaser.depth = 0.5f;
        s.phaser.feedback = 0.4f;
        s.phaser.mix = 0.35f;
        s.phaser.stages = 3;
        s.delayEnabled = 1;
        s.delay.type = 1;              // Tape
        s.delay.timeMs = 450.0f;
        s.delay.feedback = 0.4f;
        s.delay.mix = 0.25f;
        s.delay.tapeWear = 0.4f;       // wow/flutter grit fits the chaos
        s.delay.tapeSaturation = 0.6f;
        s.reverbEnabled = 1;
        s.reverbType = 0;             // Plate
        s.reverb.size = 0.6f;
        s.reverb.mix = 0.3f;
        s.global.width = 1.5f;
        // Master trim: high Q + x4 sweep is hot - pull back and let the soft limiter guard
        s.global.masterGain = 0.85f;
        presets.push_back(std::move(p));
    }

    // "Origami Cloud" - Dense particle cloud folded by a wavefolder whose drive breathes via ModEnv
    {
        PresetDef p;
        p.name = "Origami Cloud";
        p.category = "Textures";
        auto& s = p.state;
        // --- Dense particle swarm ---
        s.oscA.type = 6;              // Particle
        s.oscA.particleScatter = 6.0f;
        s.oscA.particleDensity = 32.0f;  // thick cloud
        s.oscA.particleLifetime = 400.0f;
        s.oscA.particleSpawnMode = 1;    // Random
        s.oscA.particleEnvType = 3;      // Blackman - smoothest grains
        s.oscA.particleDrift = 0.4f;
        s.oscA.level = 0.7f;
        s.oscB.level = 0.0f;             // single-source cloud
        s.mixer.position = 0.0f;
        // --- Notch filter carves a moving hollow through the folded cloud ---
        s.filter.type = 3;            // SVF Notch
        s.filter.cutoffHz = 1200.0f;
        s.filter.resonance = 1.5f;
        // --- Wavefolder: base drive modest, ANIMATED by ModEnv below (not the old static 0.4) ---
        s.distortion.type = 4;        // Wavefolder
        s.distortion.drive = 0.25f;   // starting fold
        s.distortion.foldType = 1;    // Sine fold
        s.distortion.mix = 0.9f;
        // --- Slow swell ---
        s.ampEnv.attackMs = 800.0f;
        s.ampEnv.decayMs = 600.0f;
        s.ampEnv.sustain = 0.8f;
        s.ampEnv.releaseMs = 2500.0f;
        // === MOD IDENTITY: ModEnv opens the fold over ~2s so harmonics bloom per note ===
        s.modEnv.attackMs = 1800.0f;  // slow rise -> fold intensifies as the note sustains
        s.modEnv.decayMs = 1500.0f;
        s.modEnv.sustain = 0.7f;
        s.modEnv.releaseMs = 2000.0f;
        s.modEnv.attackCurve = 0.5f;
        setVoiceRoute(s, 0, 2, 3, 0.6f); // Env3(ModEnv) -> DistDrive - the fold breathes open
        // Second gesture: smoothed random drifts the notch so the hollow wanders
        s.random.rateHz = 0.3f;
        s.random.smoothness = 0.8f;      // slow morph, not steppy
        setModSlot(s, 0, 4, 4, 0.35f, 2, 30.0f); // Random -> AllFltCut, SCurve, smoothed
        // --- FX: granular delay grain-cloud into a big diffuse Hall ---
        s.delayEnabled = 1;
        s.delay.type = 3;             // Granular
        s.delay.timeMs = 500.0f;
        s.delay.feedback = 0.3f;
        s.delay.mix = 0.3f;
        s.delay.granularSizeMs = 120.0f;
        s.delay.granularDensity = 20.0f;
        s.delay.granularPitch = 12.0f;   // octave-up shimmer grains
        s.reverbEnabled = 1;
        s.reverbType = 1;             // Hall
        s.reverb.size = 0.85f;
        s.reverb.mix = 0.4f;
        s.reverb.diffusion = 0.9f;
        s.global.width = 1.7f;
        s.global.spread = 0.5f;
        presets.push_back(std::move(p));
    }

    // "Spectral Wash" - Evolving spectral wash: saw + SpectralFreeze, spectral delay
    {
        PresetDef p;
        p.name = "Spectral Wash";
        p.category = "Textures";
        auto& s = p.state;
        // A PolyBLEP saw fused with a SpectralFreeze partner so the timbre already
        // carries spectral motion before any FX; oscB.spectralTilt is a live mod
        // target (LFO->AllSpecTilt below) so the wash keeps evolving.
        s.oscA.type = 0; s.oscA.waveform = 1;          // saw body
        s.oscA.fineCents = -6.0f; s.oscA.level = 0.6f;  // slight flat detune for width
        s.oscB.type = 8;                                // SpectralFreeze engine
        s.oscB.spectralPitch = 0.0f;                    // in tune with oscA
        s.oscB.spectralTilt = 2.0f;                     // start slightly bright
        s.oscB.spectralFormant = -3.0f;                 // hollow the partner voice
        s.oscB.level = 0.5f;
        s.mixer.position = 0.5f;                        // equal blend of the two
        // SVF Allpass: no amplitude notch, just frequency-dependent phase smear -
        // the diffuse "wash" character this category owns.
        s.filter.type = 7;                              // SVF Allpass
        s.filter.cutoffHz = 2200.0f; s.filter.resonance = 7.48f;
        s.ampEnv.attackMs = 700.0f; s.ampEnv.decayMs = 600.0f;
        s.ampEnv.sustain = 0.8f; s.ampEnv.releaseMs = 2600.0f;
        s.ampEnv.attackCurve = 0.4f;                    // gentle slow-start swell
        // Slow free LFO -> spectral tilt, SCurve so the sweep eases at the extremes.
        // This is the "evolves after the tail" signature gesture.
        s.lfo1.rateHz = 0.09f; s.lfo1.shape = 0; s.lfo1.depth = 0.8f; s.lfo1.sync = 0;
        setModSlot(s, 0, 1 /*LFO1*/, 7 /*AllSpecTilt*/, 0.7f, 2 /*SCurve*/);
        // Smoothed Random -> morph position: organic, never-repeating shimmer drift.
        s.random.rateHz = 0.5f; s.random.smoothness = 0.9f;
        setModSlot(s, 1, 4 /*Random*/, 5 /*AllMorphPos*/, 0.3f, 0 /*Linear*/, 40.0f);
        // Pitch follower gently opens the allpass region on higher notes.
        s.pitchFollower.speedMs = 120.0f;
        setModSlot(s, 2, 12 /*PitchFollow*/, 4 /*AllFltCut*/, 0.4f, 1 /*Exp*/);
        // Spectral delay is this preset's signature FX member.
        s.delayEnabled = 1;
        s.delay.type = 4;                               // Spectral
        s.delay.mix = 0.35f; s.delay.feedback = 0.35f;
        s.delay.spectralSpreadMs = 600.0f; s.delay.spectralDiffusion = 0.7f;
        s.delay.spectralWidth = 0.9f; s.delay.spectralTilt = 0.25f;
        // Plate reverb, medium, for air without swallowing the spectral motion.
        s.reverbEnabled = 1; s.reverbType = 0;         // Plate
        s.reverb.size = 0.75f; s.reverb.mix = 0.32f;
        s.reverb.diffusion = 0.8f; s.reverb.preDelayMs = 25.0f;
        s.global.width = 1.7f;                          // very wide stereo image
        presets.push_back(std::move(p));
    }

    // "Grain Destroy" - Granular-distortion glitch cloud through a sliding bandpass
    {
        PresetDef p;
        p.name = "Grain Destroy";
        p.category = "Textures";
        auto& s = p.state;
        // Saw bed + white noise layer, per the glitch brief.
        s.oscA.type = 0; s.oscA.waveform = 1; s.oscA.level = 0.7f;
        s.oscB.type = 9; s.oscB.noiseColor = 0;        // white noise
        s.oscB.level = 0.25f;
        s.mixer.position = 0.25f;                       // mostly saw, noise as fringe
        // SVF BandPass windows the grain cloud (per directive).
        s.filter.type = 2;                              // SVF BandPass
        s.filter.cutoffHz = 2600.0f; s.filter.resonance = 6.35f;
        s.filter.svfDrive = 4.0f;                       // slight post-filter grit
        // Granular distortion = the destroyer. All four grain params engaged.
        s.distortion.type = 3;                          // Granular
        s.distortion.drive = 0.65f;
        s.distortion.grainSize = 0.15f; s.distortion.grainDensity = 0.7f;
        s.distortion.grainVariation = 0.6f; s.distortion.grainJitter = 0.45f;
        s.distortion.mix = 0.8f;
        s.ampEnv.attackMs = 120.0f; s.ampEnv.decayMs = 500.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 1400.0f;
        // Global LFO slides the bandpass window so the grain cloud appears to churn
        // (the "LFO on the grains" gesture - AllFltCut is the reachable dest).
        s.lfo1.rateHz = 0.35f; s.lfo1.shape = 1; s.lfo1.depth = 0.8f; s.lfo1.sync = 0;
        setModSlot(s, 0, 1 /*LFO1*/, 4 /*AllFltCut*/, 0.6f, 0 /*Linear*/);
        // Transient detector zaps resonance on each attack -> percussive grain stabs.
        s.transient.sensitivity = 0.7f;
        setModSlot(s, 1, 13 /*Transient*/, 8 /*AllResonance*/, 0.5f, 1 /*Exp*/);
        // Envelope follower pushes wet FX mix when you dig in (dynamic grit).
        s.envFollower.sensitivity = 0.7f;
        setModSlot(s, 2, 3 /*EnvFollower*/, 3 /*EffectMix*/, 0.4f, 0 /*Linear*/);
        // ModEnv (Env3) opens distortion drive over the note for evolving grit.
        s.modEnv.attackMs = 200.0f; s.modEnv.decayMs = 1200.0f; s.modEnv.sustain = 0.6f;
        setVoiceRoute(s, 0, 2 /*Env3/ModEnv*/, 3 /*DistDrive*/, 0.4f);
        // Plain digital delay (per directive) + modest plate.
        s.delayEnabled = 1;
        s.delay.type = 0;                               // Digital
        s.delay.timeMs = 375.0f; s.delay.mix = 0.22f; s.delay.feedback = 0.4f;
        s.delay.digitalWidth = 150.0f;
        s.reverbEnabled = 1; s.reverbType = 0;         // Plate
        s.reverb.size = 0.55f; s.reverb.mix = 0.28f;
        presets.push_back(std::move(p));
    }

    // "Abyssal" - Sub-octave chaos drone, tape-saturated, slow-blooming cavern
    {
        PresetDef p;
        p.name = "Abyssal";
        p.category = "Textures";
        auto& s = p.state;
        // Sub-octave Lorenz chaos + brown-noise rumble = an ever-churning drone.
        s.oscA.type = 5;                                // Chaos
        s.oscA.chaosAttractor = 0;                      // Lorenz
        s.oscA.chaosAmount = 0.42f; s.oscA.chaosCoupling = 0.2f;
        s.oscA.chaosOutput = 1;                         // Y axis - rounder core than X
        s.oscA.tuneSemitones = -24.0f; s.oscA.level = 0.8f;
        s.oscB.type = 9; s.oscB.noiseColor = 2;        // brown-noise floor
        s.oscB.level = 0.45f;
        s.mixer.position = 0.35f;
        // 24 dB ladder, low and resonant with input drive, to shape the rumble.
        s.filter.type = 4;                              // Ladder
        s.filter.cutoffHz = 550.0f; s.filter.resonance = 5.79f;
        s.filter.ladderSlope = 4; s.filter.ladderDrive = 6.0f;
        // Tape saturation glues the low end.
        s.distortion.type = 5;                          // TapeSaturator
        s.distortion.drive = 0.5f; s.distortion.tapeSaturation = 0.6f;
        s.distortion.tapeBias = 0.55f; s.distortion.mix = 0.9f;
        s.ampEnv.attackMs = 1800.0f; s.ampEnv.decayMs = 900.0f;
        s.ampEnv.sustain = 0.85f; s.ampEnv.releaseMs = 3500.0f;
        // The bloom: a very slow LFO with a long fade-in so the filter only begins
        // to breathe ~4 s in. This preset OWNS lfo fadeInMs.
        s.lfo1.rateHz = 0.06f; s.lfo1.shape = 0; s.lfo1.depth = 0.9f; s.lfo1.sync = 0;
        s.lfo1Ext.fadeInMs = 4000.0f;
        setModSlot(s, 0, 1 /*LFO1*/, 4 /*AllFltCut*/, 0.6f, 2 /*SCurve*/);
        s.modMatrix.slots[0].scale = 4;                 // x4 depth -> huge slow bloom
        // ChaosMod source stirs resonance for unstable, living depth.
        s.chaosMod.rateHz = 0.15f; s.chaosMod.type = 0; s.chaosMod.depth = 0.5f;
        setModSlot(s, 1, 9 /*Chaos*/, 8 /*AllResonance*/, 0.35f, 0 /*Linear*/);
        // Sparse, pitched-down granular delay smears the drone into a cavern.
        s.delayEnabled = 1;
        s.delay.type = 3;                               // Granular
        s.delay.timeMs = 600.0f; s.delay.mix = 0.3f; s.delay.feedback = 0.3f;
        s.delay.granularSizeMs = 180.0f; s.delay.granularDensity = 6.0f;
        s.delay.granularPitch = -12.0f; s.delay.granularPosSpray = 0.5f;
        s.delay.granularTexture = 0.6f; s.delay.granularWidth = 1.0f;
        // Near-max dark Hall.
        s.reverbEnabled = 1; s.reverbType = 1;         // Hall
        s.reverb.size = 0.95f; s.reverb.mix = 0.45f;
        s.reverb.damping = 0.8f; s.reverb.diffusion = 0.88f; s.reverb.preDelayMs = 40.0f;
        s.global.width = 1.3f;
        presets.push_back(std::move(p));
    }

    // "Binary Rain" - Dual phase-distortion + ring mod, S&H-quantized digital rain
    {
        PresetDef p;
        p.name = "Binary Rain";
        p.category = "Textures";
        auto& s = p.state;
        // Dual phase-distortion (CZ-style) oscillators at a fifth = brittle digital core.
        s.oscA.type = 2;                                // PhaseDistortion
        s.oscA.pdWaveform = 1;                          // Square
        s.oscA.pdDistortion = 0.6f; s.oscA.level = 0.6f;
        s.oscB.type = 2;                                // PhaseDistortion
        s.oscB.pdWaveform = 2;                          // Pulse
        s.oscB.pdDistortion = 0.45f;
        s.oscB.tuneSemitones = 7.0f;                    // a fifth up
        s.oscB.level = 0.45f;
        s.mixer.position = 0.42f;
        // SVF Notch hollows the mid -> glassy "rain through a window" timbre.
        s.filter.type = 3;                              // SVF Notch
        s.filter.cutoffHz = 1500.0f; s.filter.resonance = 9.74f;
        // Ring modulator sprinkles inharmonic droplets (note-tracked carrier).
        s.distortion.type = 6;                          // RingModulator
        s.distortion.ringFreq = 0.6882f;               // ~440 Hz carrier (normalized)
        s.distortion.ringFreqMode = 1;                  // note-track
        s.distortion.ringRatio = 0.1111f;              // ratio ~2.0 (normalized)
        s.distortion.ringWaveform = 0; s.distortion.mix = 0.35f;
        s.ampEnv.attackMs = 60.0f; s.ampEnv.decayMs = 450.0f;
        s.ampEnv.sustain = 0.6f; s.ampEnv.releaseMs = 1200.0f;
        // S&H LFO, free-run, QUANTIZED to a few steps + phase-offset start entry ->
        // the stuttering "binary rain". This preset OWNS S&H quantizeSteps + phaseOffset.
        s.lfo1.rateHz = 9.0f; s.lfo1.shape = 4; s.lfo1.depth = 0.7f; s.lfo1.sync = 0;
        s.lfo1Ext.quantizeSteps = 6;                    // gridded, stepped jumps
        s.lfo1Ext.phaseOffset = 90.0f;                  // offset entry point
        // Stepped curve makes the routing itself quantize -> extra digital jitter.
        setModSlot(s, 0, 1 /*LFO1*/, 4 /*AllFltCut*/, 0.5f, 3 /*Stepped*/);
        // Rungler (Benjolin shift register) -> morph position for evolving bit patterns.
        s.rungler.osc1FreqHz = 5.0f; s.rungler.osc2FreqHz = 7.0f;
        s.rungler.depth = 0.5f; s.rungler.bits = 8;
        setModSlot(s, 1, 10 /*Rungler*/, 5 /*AllMorphPos*/, 0.4f, 0 /*Linear*/);
        // Ping-pong delay throws the droplets across the field.
        s.delayEnabled = 1;
        s.delay.type = 2;                               // PingPong
        s.delay.timeMs = 300.0f; s.delay.mix = 0.28f; s.delay.feedback = 0.4f;
        s.delay.pingPongWidth = 170.0f; s.delay.pingPongCrossFeed = 0.8f;
        s.reverbEnabled = 1; s.reverbType = 0;         // Plate
        s.reverb.size = 0.5f; s.reverb.mix = 0.24f;
        presets.push_back(std::move(p));
    }

    // "Vowel Cloud" - dual Formant (E vs O), dual phase-offset LFOs, wide tape choral cloud
    {
        PresetDef p;
        p.name = "Vowel Cloud";
        p.category = "Textures";
        auto& s = p.state;
        // Two formant oscillators, E vs O, cross-morphed in the spectral-morph mixer.
        // This trio's WARM member: mid vowels E(1)<->O(3), distinct from Formant Sea
        // (A/O) and Vowel Ghost (I/U).
        s.oscA.type = 7;                                // Formant
        s.oscA.formantVowel = 1; s.oscA.formantMorph = 1.0f;   // E
        s.oscA.level = 0.7f;
        s.oscB.type = 7;                                // Formant
        s.oscB.formantVowel = 3; s.oscB.formantMorph = 2.8f;   // O
        s.oscB.level = 0.65f;
        s.mixer.mode = 1;                               // Spectral Morph
        s.mixer.position = 0.5f; s.mixer.tilt = 1.0f;   // centered, gentle bright tilt
        // SVF High-pass thins the low end so the choir floats.
        s.filter.type = 1;                              // SVF HP
        s.filter.cutoffHz = 180.0f; s.filter.resonance = 4.09f;
        s.ampEnv.attackMs = 900.0f; s.ampEnv.decayMs = 700.0f;
        s.ampEnv.sustain = 0.85f; s.ampEnv.releaseMs = 2800.0f;
        s.ampEnv.attackCurve = 0.5f;                    // breathy slow-start swell
        // Sub-0.1 Hz LFO cross-fades the vowels via morph position; x2 scale so it
        // travels the full E<->O space (mod-matrix scale axis).
        s.lfo1.rateHz = 0.06f; s.lfo1.shape = 1; s.lfo1.depth = 0.95f; s.lfo1.sync = 0;
        setModSlot(s, 0, 1 /*LFO1*/, 5 /*AllMorphPos*/, 0.6f, 2 /*SCurve*/);
        s.modMatrix.slots[0].scale = 3;                 // x2 -> full vowel travel
        // A second, phase-offset LFO nudges spectral tilt so the "mouth" opens/closes
        // out of phase with the vowel morph - dual-LFO motion distinct from Ghost's
        // single unipolar sweep.
        s.lfo2.rateHz = 0.11f; s.lfo2.shape = 0; s.lfo2.depth = 0.8f; s.lfo2.sync = 0;
        s.lfo2Ext.phaseOffset = 120.0f;
        setModSlot(s, 1, 2 /*LFO2*/, 7 /*AllSpecTilt*/, 0.35f, 2 /*SCurve*/, 30.0f);
        // Warm tape delay doubles the choir just behind the beat (this patch's spatial identity).
        s.delayEnabled = 1;
        s.delay.type = 1;                               // Tape
        s.delay.timeMs = 480.0f; s.delay.mix = 0.24f; s.delay.feedback = 0.3f;
        s.delay.tapeSaturation = 0.5f; s.delay.tapeWear = 0.25f;
        s.delay.tapeHead1Level = 0.0f;                  // 0 dB main head
        // Modulated Hall with pre-delay = a wide breathing cathedral.
        s.reverbEnabled = 1; s.reverbType = 1;         // Hall
        s.reverb.size = 0.9f; s.reverb.mix = 0.4f;
        s.reverb.damping = 0.35f; s.reverb.diffusion = 0.88f;
        s.reverb.preDelayMs = 45.0f;
        s.reverb.modRateHz = 0.4f; s.reverb.modDepth = 0.3f;
        // Wide stereo + voice spread = the choral cloud fans across the field.
        s.global.width = 1.8f; s.global.spread = 0.6f;
        s.global.polyphony = 12;                        // lush poly for held chords
        presets.push_back(std::move(p));
    }

    // ==================== RHYTHMIC Category (15 new presets) ====================

    // "Harmony Gate" - detuned dual-saw + scalic harmonizer, twin-LFO motion
    {
        PresetDef p;
        p.name = "Harmony Gate";
        p.category = "Rhythmic";
        auto& s = p.state;
        // --- Voice: classic warm virtual-analog dual saw, wide unison feel ---
        s.global.width = 1.5f;      // wide stereo bed for the harmony voices
        s.global.spread = 0.3f;     // pan-spread the poly stack
        s.global.polyphony = 8;
        s.oscA.type = 0; s.oscA.waveform = 1; s.oscA.level = 0.75f;      // saw A
        s.oscB.type = 0; s.oscB.waveform = 1;                            // saw B
        s.oscB.fineCents = 9.0f; s.oscB.level = 0.6f;                    // +9c beating = analog thickness
        s.mixer.position = 0.45f;                                       // slight A bias
        // --- Ladder LP with a REAL filter-env sweep (env is +semitones, not Hz) ---
        s.filter.type = 4;                        // Ladder LP for warmth
        s.filter.cutoffHz = 2200.0f;              // start dark so the sweep is audible
        s.filter.resonance = 4.66f;
        s.filter.ladderSlope = 4;                 // 24 dB/oct
        s.filter.ladderDrive = 4.0f;              // gentle input push
        s.filter.envAmount = 22.0f;               // +22 st sweep (bug-fixed: NOT a Hz value)
        s.filter.keyTrack = 0.3f;                 // track pitch a little
        s.filterEnv.attackMs = 5.0f; s.filterEnv.decayMs = 260.0f;
        s.filterEnv.sustain = 0.35f; s.filterEnv.releaseMs = 400.0f;
        // --- Amp: soft pad body, long enough to hear the gate chop the sustain ---
        s.ampEnv.attackMs = 40.0f; s.ampEnv.decayMs = 400.0f;
        s.ampEnv.sustain = 0.75f; s.ampEnv.releaseMs = 550.0f;
        // --- 1/16 gate rhythm ---
        s.tranceGate.enabled = 1; s.tranceGate.numSteps = 16;
        s.tranceGate.tempoSync = 1; s.tranceGate.noteValue = kNote1_16;
        s.tranceGate.depth = 0.85f;
        s.tranceGate.attackMs = 4.0f; s.tranceGate.releaseMs = 25.0f;
        float tg[] = {1,1,0,1, 1,0,1,0, 1,1,0,1, 0,1,1,0};
        for (int i = 0; i < 16; ++i) s.tranceGate.stepLevels[i] = tg[i];
        // --- Scalic 3rd + 5th harmony, panned wide (the identity of this preset) ---
        s.harmonizerEnabled = 1;
        s.harmonizer.harmonyMode = 1;             // Scalic
        s.harmonizer.key = 0; s.harmonizer.scale = 0; // C Major
        s.harmonizer.pitchShiftMode = 1;          // Granular
        s.harmonizer.numVoices = 2;
        s.harmonizer.dryLevelDb = 0.0f; s.harmonizer.wetLevelDb = -4.0f;
        s.harmonizer.voiceInterval[0] = 2; s.harmonizer.voicePan[0] = -0.45f; // 3rd left
        s.harmonizer.voiceInterval[1] = 4; s.harmonizer.voicePan[1] = 0.45f;  // 5th right
        // --- Dual tempo-synced LFOs give the sustain constant movement ---
        s.lfo1.shape = 0; s.lfo1.depth = 1.0f; s.lfo1.sync = 1;   // sine, synced
        s.lfo1Ext.noteValue = 19;                                 // 1/1 = very slow morph drift
        s.lfo2.shape = 1; s.lfo2.depth = 1.0f; s.lfo2.sync = 1;   // triangle, synced
        s.lfo2Ext.noteValue = 13;                                 // 1/4 = mid filter shimmer
        setModSlot(s, 0, kSrcLFO1, kDstAllMorphPos, 0.4f, kCurveSCurve); // smooth morph wander
        setModSlot(s, 1, kSrcLFO2, kDstAllFltCut,  0.3f, kCurveExp);     // filter breathing
        // --- Macro 1 = one-knob timbral morph over cutoff (performance handle) ---
        s.macros.values[0] = 0.4f;
        setModSlot(s, 2, kSrcMacro1, kDstAllFltCut, 0.5f, kCurveLinear);
        // --- Gate output nudges the A/B morph so each hit has a tiny timbral flick ---
        setVoiceRoute(s, 0, kVSrcGate, kVDstMorphPos, 0.3f);
        setVoiceRoute(s, 1, kVSrcVelocity, kVDstFltCut, 0.4f);           // dynamics -> brightness
        // --- Slow phaser + a real HALL (not the reflexive plate) ---
        s.modulationType = 1;                      // Phaser
        s.phaser.rateHz = 0.25f; s.phaser.depth = 0.5f; s.phaser.feedback = 0.4f;
        s.phaser.mix = 0.35f; s.phaser.stages = 2; s.phaser.centerFreqHz = 900.0f;
        s.reverbEnabled = 1; s.reverbType = 1;     // Hall
        s.reverb.size = 0.6f; s.reverb.mix = 0.25f; s.reverb.damping = 0.4f;
        presets.push_back(std::move(p));
    }

    // "Ring Pulse" - mono hard-sync square + ring modulator, gate-pumped clang
    {
        PresetDef p;
        p.name = "Ring Pulse";
        p.category = "Rhythmic";
        auto& s = p.state;
        // --- Mono with glide: a single aggressive metallic voice ---
        s.global.voiceMode = 1;      // Mono
        s.global.polyphony = 1;
        s.global.width = 1.1f;
        s.monoMode.priority = 0;     // Last-note
        s.monoMode.legato = 1;
        s.monoMode.portamentoTimeMs = 40.0f;
        s.monoMode.portaMode = 1;    // glide only on legato
        // --- Hard-sync engine on OSC A: square master, square slave for a biting edge ---
        s.oscA.type = 3;             // Sync engine
        s.oscA.waveform = 2;         // square base tone
        s.oscA.syncRatio = 2.5f;     // sweetspot sync formant
        s.oscA.syncWaveform = 3;     // square slave = hollow/hard
        s.oscA.syncMode = 0;         // Hard sync
        s.oscA.syncAmount = 1.0f;
        s.oscA.level = 0.85f;
        s.oscB.level = 0.0f;         // single oscillator
        s.mixer.position = 0.0f;     // OSC A only
        // --- Comb filter tunes the metallic resonance instead of a plain LP ---
        s.filter.type = 6;           // Comb
        s.filter.cutoffHz = 3000.0f; // comb tuning
        s.filter.combDamping = 0.3f;
        s.filter.resonance = 6.35f;
        // --- Ring modulator: note-tracked so the clang stays musical ---
        s.distortion.type = 6;                 // Ring Modulator
        s.distortion.drive = 0.45f;
        s.distortion.ringFreqMode = 1;         // NoteTrack
        s.distortion.ringRatio = 0.2f;         // normalized -> ~3.4x ratio, inharmonic bite
        s.distortion.ringWaveform = 0;         // sine carrier
        s.distortion.ringStereoSpread = 0.4f;  // widen the metallic image
        s.distortion.character = 0.5f;
        s.distortion.mix = 0.55f;
        // --- Percussive amp, short so gate stabs read as discrete hits ---
        s.ampEnv.attackMs = 1.0f; s.ampEnv.decayMs = 180.0f;
        s.ampEnv.sustain = 0.55f; s.ampEnv.releaseMs = 90.0f;
        // --- Full-depth 1/16 gate with retrigger stutter ---
        s.tranceGate.enabled = 1; s.tranceGate.numSteps = 8;
        s.tranceGate.tempoSync = 1; s.tranceGate.noteValue = kNote1_16;
        s.tranceGate.depth = 1.0f;
        s.tranceGate.attackMs = 1.0f; s.tranceGate.releaseMs = 8.0f;
        s.tranceGate.retriggerDepth = 0.3f;    // subtle intra-step stutter
        float tg[] = {1,0,1,0, 1,1,0,1};
        for (int i = 0; i < 8; ++i) s.tranceGate.stepLevels[i] = tg[i];
        // --- Synced LFO opens the comb between hits so the timbre never sits still ---
        s.lfo1.shape = 0; s.lfo1.depth = 1.0f; s.lfo1.sync = 1;
        s.lfo1Ext.noteValue = kNote1_8;
        setModSlot(s, 0, kSrcLFO1, kDstAllFltCut, 0.5f, kCurveExp);
        // --- The GATE output drives distortion drive: clang re-strikes on every hit ---
        setVoiceRoute(s, 0, kVSrcGate, kVDstDistDrive, 0.6f);
        // --- Ping-pong echo throws the stabs across the field ---
        s.delayEnabled = 1;
        s.delay.type = 2;            // Ping-Pong
        s.delay.sync = 1; s.delay.noteValue = kNote1_8;
        s.delay.mix = 0.22f; s.delay.feedback = 0.32f;
        s.delay.pingPongWidth = 150.0f;
        presets.push_back(std::move(p));
    }

    // "Tape Groove" - wavetable saw + square sub, tape saturation, transient-pumped filter
    {
        PresetDef p;
        p.name = "Tape Groove";
        p.category = "Rhythmic";
        auto& s = p.state;
        // --- OSC A: Wavetable engine (an engine nothing else in the bank uses) ---
        s.oscA.type = 1;             // Wavetable
        s.oscA.waveform = 1;         // saw table
        s.oscA.level = 0.75f;
        s.oscA.phaseMod = 0.15f;     // gentle self-PM = subtle wavetable movement
        s.oscB.type = 0; s.oscB.waveform = 2;   // square sub
        s.oscB.tuneSemitones = -12.0f; s.oscB.level = 0.45f;
        s.mixer.position = 0.35f;
        // --- SVF LP with post-drive + a corrected filter-env sweep ---
        s.filter.type = 0;           // SVF LP
        s.filter.cutoffHz = 3200.0f;
        s.filter.resonance = 4.09f;
        s.filter.svfSlope = 1;       // 24 dB
        s.filter.svfDrive = 6.0f;    // saturated LP for warmth
        s.filter.envAmount = 18.0f;  // +18 st sweep (bug-fixed semitone value)
        s.filter.keyTrack = 0.2f;
        s.filterEnv.attackMs = 8.0f; s.filterEnv.decayMs = 320.0f;
        s.filterEnv.sustain = 0.4f; s.filterEnv.releaseMs = 300.0f;
        // --- Tape-saturator distortion in the voice path for analog grit ---
        s.distortion.type = 5;       // Tape Saturator
        s.distortion.drive = 0.3f;
        s.distortion.tapeModel = 0;
        s.distortion.tapeSaturation = 0.5f;
        s.distortion.tapeBias = 0.5f;
        s.distortion.character = 0.5f;
        s.distortion.mix = 0.6f;
        // --- Laid-back amp ---
        s.ampEnv.attackMs = 3.0f; s.ampEnv.decayMs = 300.0f;
        s.ampEnv.sustain = 0.6f; s.ampEnv.releaseMs = 220.0f;
        // --- Softer 1/8 gate with a velocity-graded groove ---
        s.tranceGate.enabled = 1; s.tranceGate.numSteps = 8;
        s.tranceGate.tempoSync = 1; s.tranceGate.noteValue = kNote1_8;
        s.tranceGate.depth = 0.85f;
        s.tranceGate.attackMs = 8.0f; s.tranceGate.releaseMs = 40.0f;
        float tg[] = {1,0.7f,0.4f,1, 0.6f,0.3f,0.9f,0.5f};
        for (int i = 0; i < 8; ++i) s.tranceGate.stepLevels[i] = tg[i];
        // --- Transient detector = sidechain feel: each note-onset ducks/opens the filter ---
        s.transient.sensitivity = 0.6f; s.transient.attackMs = 2.0f; s.transient.decayMs = 60.0f;
        setModSlot(s, 0, kSrcTransient, kDstAllFltCut, 0.4f, kCurveExp);
        // --- Slow mod-env slowly evolves the gate depth over a phrase ---
        s.modEnv.attackMs = 200.0f; s.modEnv.decayMs = 800.0f;
        s.modEnv.sustain = 0.5f; s.modEnv.releaseMs = 600.0f;
        setVoiceRoute(s, 0, kVSrcEnv3, kVDstGateDepth, 0.3f);   // evolving gate depth
        setVoiceRoute(s, 1, kVSrcVelocity, kVDstFltCut, 0.3f);  // dynamics -> brightness
        // --- Warm dotted-8th tape delay, small plate room ---
        s.delayEnabled = 1;
        s.delay.type = 1;            // Tape
        s.delay.sync = 1; s.delay.noteValue = kNote1_8D;
        s.delay.mix = 0.26f; s.delay.feedback = 0.36f;
        s.delay.tapeInertiaMs = 300.0f;
        s.delay.tapeSaturation = 0.4f; s.delay.tapeWear = 0.3f;
        s.reverbEnabled = 1; s.reverbType = 0;   // Plate
        s.reverb.size = 0.4f; s.reverb.mix = 0.15f;
        presets.push_back(std::move(p));
    }

    // "Grain Scatter" - particle-swarm osc, auto-wah, chaos + flanger + granular delay
    {
        PresetDef p;
        p.name = "Grain Scatter";
        p.category = "Rhythmic";
        auto& s = p.state;
        s.global.width = 1.4f; s.global.spread = 0.4f;
        // --- OSC A: Particle engine - a literal grain swarm (on-theme) ---
        s.oscA.type = 6;                    // Particle
        s.oscA.particleScatter = 6.0f;      // wide freq scatter = detuned cloud
        s.oscA.particleDensity = 24.0f;     // dense swarm
        s.oscA.particleLifetime = 120.0f;   // medium grains
        s.oscA.particleSpawnMode = 1;       // Random spawn = organic
        s.oscA.particleEnvType = 0;         // Hann grains
        s.oscA.particleDrift = 0.3f;        // slow pitch wander
        s.oscA.level = 0.7f;
        s.oscB.type = 0; s.oscB.waveform = 1;   // saw glue underneath
        s.oscB.fineCents = 6.0f; s.oscB.level = 0.45f;
        s.mixer.position = 0.4f;
        // --- Env Filter (auto-wah): the swarm self-sweeps on its own envelope ---
        s.filter.type = 11;                 // Env Filter
        s.filter.cutoffHz = 800.0f;         // base of the wah
        s.filter.resonance = 2.0f;
        s.filter.envSubType = 1;            // BP wah
        s.filter.envSensitivity = 6.0f;
        s.filter.envDepth = 0.8f;
        s.filter.envAttack = 8.0f; s.filter.envRelease = 180.0f;
        s.filter.envDirection = 0;          // sweep up
        // --- Amp ---
        s.ampEnv.attackMs = 3.0f; s.ampEnv.decayMs = 300.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 300.0f;
        // --- Busy 1/16 gate ---
        s.tranceGate.enabled = 1; s.tranceGate.numSteps = 16;
        s.tranceGate.tempoSync = 1; s.tranceGate.noteValue = kNote1_16;
        s.tranceGate.depth = 0.9f;
        s.tranceGate.attackMs = 2.0f; s.tranceGate.releaseMs = 15.0f;
        float tg[] = {1,0,0.5f,1, 0,1,0,0.7f, 1,0.3f,0,1, 0.8f,0,1,0};
        for (int i = 0; i < 16; ++i) s.tranceGate.stepLevels[i] = tg[i];
        // --- Chaos modulator (Rossler) evolves spectral tilt + resonance unpredictably ---
        s.chaosMod.type = 1;                // Rossler
        s.chaosMod.rateHz = 0.8f;
        s.chaosMod.depth = 0.6f;            // MUST be > 0 or the source is silent
        s.chaosMod.sync = 0;
        setModSlot(s, 0, kSrcChaos, kDstAllSpecTilt,  0.4f, kCurveLinear);
        setModSlot(s, 1, kSrcChaos, kDstAllResonance, 0.3f, kCurveLinear);
        // --- Gate flicks the A/B morph so grains re-color per hit ---
        setVoiceRoute(s, 0, kVSrcGate, kVDstMorphPos, 0.35f);
        // --- Light spectral distortion smears the cloud further ---
        s.distortion.type = 2;              // Spectral
        s.distortion.drive = 0.25f;
        s.distortion.spectralMode = 0;
        s.distortion.mix = 0.3f;
        // --- Flanger jet-swirl (an FX no other preset here uses) ---
        s.modulationType = 2;               // Flanger
        s.flanger.rateHz = 0.3f; s.flanger.depth = 0.6f; s.flanger.feedback = 0.2f;
        s.flanger.mix = 0.4f; s.flanger.stereoSpread = 120.0f;
        // --- Pitch/pan-sprayed granular delay = the diffuse scatter tail ---
        s.delayEnabled = 1;
        s.delay.type = 3;                   // Granular
        s.delay.mix = 0.32f; s.delay.feedback = 0.3f;
        s.delay.granularSizeMs = 60.0f; s.delay.granularDensity = 20.0f;
        s.delay.granularPitchSpray = 0.3f; s.delay.granularPanSpray = 0.6f;
        s.reverbEnabled = 1; s.reverbType = 1;  // Hall
        s.reverb.size = 0.55f; s.reverb.mix = 0.22f;
        presets.push_back(std::move(p));
    }

    // "Euclidean Bells" - inharmonic additive bell on a Euclidean gate, long ringing tail
    {
        PresetDef p;
        p.name = "Euclidean Bells";
        p.category = "Rhythmic";
        auto& s = p.state;
        // --- OSC A: Additive engine, inharmonic partials = struck-metal timbre ---
        s.oscA.type = 4;                    // Additive
        s.oscA.additivePartials = 48;       // rich partial stack
        s.oscA.additiveInharm = 0.35f;      // stretched/inharmonic = bell
        s.oscA.additiveTilt = -2.0f;        // gently darker top
        s.oscA.level = 0.7f;
        s.oscB.type = 0; s.oscB.waveform = 0;   // pure sine
        s.oscB.tuneSemitones = 12.0f;       // octave-up shimmer
        s.oscB.level = 0.28f;
        s.mixer.position = 0.2f;            // mostly the additive bell
        // --- SVF Peak filter accents the strike band; env gives a bright transient ping ---
        s.filter.type = 8;                  // SVF Peak
        s.filter.cutoffHz = 2500.0f;
        s.filter.resonance = 3.0f;
        s.filter.svfGain = 6.0f;            // +6 dB peak on the bell band
        s.filter.svfSlope = 1;
        s.filter.envAmount = 30.0f;         // +30 st bright ping on attack (bug-fixed value)
        s.filterEnv.attackMs = 1.0f; s.filterEnv.decayMs = 500.0f;
        s.filterEnv.sustain = 0.1f; s.filterEnv.releaseMs = 800.0f;
        // --- Bell amp: instant strike, long ringing decay, near-zero sustain ---
        s.ampEnv.attackMs = 1.0f; s.ampEnv.decayMs = 1600.0f;
        s.ampEnv.sustain = 0.12f; s.ampEnv.releaseMs = 1200.0f;
        // --- Euclidean 7-in-16 gate = the rhythmic signature ---
        s.tranceGate.enabled = 1; s.tranceGate.numSteps = 16;
        s.tranceGate.tempoSync = 1; s.tranceGate.noteValue = kNote1_16;
        s.tranceGate.depth = 1.0f;
        s.tranceGate.attackMs = 1.0f; s.tranceGate.releaseMs = 5.0f;
        s.tranceGate.euclideanEnabled = 1;
        s.tranceGate.euclideanHits = 7;     // E(7,16)
        s.tranceGate.euclideanRotation = 0;
        // --- Slow synced LFO drifts spectral tilt so the ringing tail shimmers ---
        s.lfo1.shape = 0; s.lfo1.depth = 1.0f; s.lfo1.sync = 1;
        s.lfo1Ext.noteValue = 16;           // 1/2 note = slow shimmer
        setModSlot(s, 0, kSrcLFO1, kDstAllSpecTilt, 0.3f, kCurveSCurve);
        // --- Harder strikes ring brighter ---
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.5f);
        // --- Ping-pong echo + plate keep the bells ringing across the stereo field ---
        s.delayEnabled = 1;
        s.delay.type = 2;                   // Ping-Pong
        s.delay.sync = 1; s.delay.noteValue = kNote1_8;
        s.delay.mix = 0.28f; s.delay.feedback = 0.32f;
        s.delay.pingPongWidth = 160.0f;
        s.reverbEnabled = 1; s.reverbType = 0;  // Plate
        s.reverb.size = 0.7f; s.reverb.mix = 0.32f; s.reverb.damping = 0.35f;
        presets.push_back(std::move(p));
    }

    // "Wobble Gate" - Sub-octave dubstep growl; the mod-matrix exemplar
    {
        PresetDef p;
        p.name = "Wobble Gate";
        p.category = "Rhythmic";
        auto& s = p.state;
        // Two VA oscillators an octave down; saw + square for a fat sub-growl
        s.oscA.type = 0; s.oscA.waveform = 1; // Sawtooth
        s.oscA.tuneSemitones = -12.0f; s.oscA.level = 0.85f;
        s.oscB.type = 0; s.oscB.waveform = 3; // Pulse/Square adds hollow body
        s.oscB.tuneSemitones = -12.0f; s.oscB.pulseWidth = 0.35f; // thinner = more edge
        s.oscB.fineCents = -7.0f; s.oscB.level = 0.55f; // slight beat for analog thickness
        s.mixer.position = 0.45f;
        // Driven resonant ladder is the wobble's voice
        s.filter.type = 4; // Ladder
        s.filter.cutoffHz = 700.0f; // starts low so the LFO opens it dramatically
        s.filter.resonance = 6.92f;
        s.filter.ladderSlope = 4; s.filter.ladderDrive = 3.0f;
        // FIXED filter-env: plain semitones (was the category's 4000 bug elsewhere).
        // +26 st per-note snap gives each gate hit a plucky attack under the wobble.
        s.filter.envAmount = 26.0f;
        s.filterEnv.attackMs = 4.0f; s.filterEnv.decayMs = 180.0f;
        s.filterEnv.sustain = 0.2f; s.filterEnv.releaseMs = 120.0f;
        s.filterEnv.decayCurve = 0.4f; // punchy exponential drop
        s.ampEnv.attackMs = 2.0f; s.ampEnv.decayMs = 200.0f;
        s.ampEnv.sustain = 0.85f; s.ampEnv.releaseMs = 120.0f;
        // Dual tempo-synced LFOs, different shapes/rates => movement that never lines up
        s.lfo1.rateHz = 2.0f; s.lfo1.shape = 1; s.lfo1.depth = 1.0f; s.lfo1.sync = 1; // Triangle
        s.lfo1Ext.noteValue = kNote1_4;   // the main wobble tempo
        setModSlot(s, 0, kSrcLFO1, kDstAllFltCut, 0.65f, kCurveSCurve);
        s.lfo2.rateHz = 1.0f; s.lfo2.shape = 0; s.lfo2.depth = 0.5f; s.lfo2.sync = 1; // Sine
        s.lfo2Ext.noteValue = kNote1_8;   // faster timbral shimmer on the morph
        setModSlot(s, 1, kSrcLFO2, kDstAllMorphPos, 0.4f, kCurveExp);
        // Sidechain feel: transient detector ducks master on each note attack
        s.transient.sensitivity = 0.7f; s.transient.attackMs = 2.0f; s.transient.decayMs = 90.0f;
        setModSlot(s, 2, kSrcTransient, kDstMasterVol, -0.18f);
        // Gate output drives BOTH distortion crunch and morph => rhythmically alive
        setVoiceRoute(s, 0, kVSrcGate, kVDstDistDrive, 0.35f);
        setVoiceRoute(s, 1, kVSrcGate, kVDstMorphPos, 0.25f);
        s.distortion.type = 5; // Tape Saturator
        s.distortion.drive = 0.25f; s.distortion.tapeSaturation = 0.45f; s.distortion.tapeBias = 0.55f;
        s.tranceGate.enabled = 1;
        s.tranceGate.numSteps = 4;
        s.tranceGate.tempoSync = 1; s.tranceGate.noteValue = kNote1_16;
        s.tranceGate.depth = 0.75f;
        s.tranceGate.attackMs = 2.0f; s.tranceGate.releaseMs = 20.0f;
        s.tranceGate.stepLevels[0] = 1.0f;  s.tranceGate.stepLevels[1] = 0.35f;
        s.tranceGate.stepLevels[2] = 0.85f; s.tranceGate.stepLevels[3] = 0.5f;
        s.global.voiceMode = 1; // mono for a focused bass
        s.global.masterGain = 0.9f; // headroom for the drive + resonance
        s.monoMode.legato = 1; s.monoMode.portamentoTimeMs = 30.0f; // slides between wobbles
        presets.push_back(std::move(p));
    }

    // "Spectral Chop" - Frozen-spectrum stutter with evolving spectral tilt
    {
        PresetDef p;
        p.name = "Spectral Chop";
        p.category = "Rhythmic";
        auto& s = p.state;
        // Spectral-freeze drone is the timbral core
        s.oscA.type = 8; // Spectral Freeze
        s.oscA.spectralTilt = 2.0f;  // bright, glassy top
        s.oscA.spectralPitch = 0.0f;
        s.oscA.spectralFormant = 3.0f; // shift formants up for a vocal-ish sheen
        s.oscA.level = 0.8f;
        // Dark additive under-layer (few partials, downward tilt) fills the low-mid
        s.oscB.type = 4; // Additive
        s.oscB.additivePartials = 8; s.oscB.additiveTilt = -8.0f; // mellow, organ-like
        s.oscB.additiveInharm = 0.05f; // faint bell edge
        s.oscB.tuneSemitones = -12.0f; s.oscB.level = 0.3f;
        s.mixer.position = 0.3f; // favour the spectral engine
        // SVF LP with a real filter-env so each chop MOVES (fixes the inert original)
        s.filter.type = 0; // SVF LP
        s.filter.cutoffHz = 3000.0f; s.filter.resonance = 4.09f;
        s.filter.envAmount = 30.0f; // +30 st sweep per note
        s.filterEnv.attackMs = 5.0f; s.filterEnv.decayMs = 260.0f;
        s.filterEnv.sustain = 0.3f; s.filterEnv.releaseMs = 300.0f;
        s.ampEnv.attackMs = 5.0f; s.ampEnv.decayMs = 300.0f;
        s.ampEnv.sustain = 0.75f; s.ampEnv.releaseMs = 250.0f;
        // Slow FREE-RUNNING LFO tilts the spectrum -> the drone evolves across bars
        s.lfo1.rateHz = 0.12f; s.lfo1.shape = 0; s.lfo1.depth = 1.0f; s.lfo1.sync = 0; // Sine, free
        setModSlot(s, 0, kSrcLFO1, kDstAllSpecTilt, 0.5f, kCurveSCurve);
        // Hard 16-step chop, full depth, near-instant edges = stutter
        s.tranceGate.enabled = 1;
        s.tranceGate.numSteps = 16;
        s.tranceGate.tempoSync = 1; s.tranceGate.noteValue = kNote1_16;
        s.tranceGate.depth = 1.0f;
        s.tranceGate.attackMs = 1.0f; s.tranceGate.releaseMs = 5.0f;
        {
            float tg[16] = {1,1,0,0, 1,0,1,1, 0,1,1,0, 1,1,0,1};
            for (int i = 0; i < 16; ++i) s.tranceGate.stepLevels[i] = tg[i];
        }
        // Hall reverb (not the default plate) smears fragments into a cathedral
        s.reverbEnabled = 1;
        s.reverbType = 1; // Hall
        s.reverb.size = 0.65f; s.reverb.mix = 0.28f; s.reverb.damping = 0.35f;
        s.reverb.preDelayMs = 20.0f;
        presets.push_back(std::move(p));
    }

    // "Bounce Lead" (was Ping Pong Lead) - flanged dual-saw lead bouncing in stereo
    {
        PresetDef p;
        p.name = "Bounce Lead";
        p.category = "Rhythmic";
        auto& s = p.state;
        // Classic detuned saw pair, but earns its identity from FX + motion
        s.oscA.type = 0; s.oscA.waveform = 1; s.oscA.level = 0.8f;
        s.oscB.type = 0; s.oscB.waveform = 1;
        s.oscB.fineCents = 11.0f; s.oscB.level = 0.55f; // wider detune than a plain pad
        s.mixer.position = 0.4f;
        s.filter.type = 0; // SVF LP
        s.filter.cutoffHz = 4200.0f; s.filter.resonance = 3.86f;
        s.filter.svfDrive = 3.0f; // a little grit on the lead
        // FIXED filter-env: +18 st gives the lead a plucky opening on each note
        s.filter.envAmount = 18.0f;
        s.filterEnv.attackMs = 4.0f; s.filterEnv.decayMs = 200.0f;
        s.filterEnv.sustain = 0.4f; s.filterEnv.releaseMs = 180.0f;
        s.ampEnv.attackMs = 3.0f; s.ampEnv.decayMs = 220.0f;
        s.ampEnv.sustain = 0.6f; s.ampEnv.releaseMs = 200.0f;
        // Synced LFO gently breathes the cutoff so sustained notes never sit still
        s.lfo1.rateHz = 1.0f; s.lfo1.shape = 1; s.lfo1.depth = 0.5f; s.lfo1.sync = 1; // Triangle
        s.lfo1Ext.noteValue = kNote1_8;
        setModSlot(s, 0, kSrcLFO1, kDstAllFltCut, 0.25f);
        // FLANGER (the category's never-used modulation FX) = deep swirling body
        s.modulationType = 2; // Flanger
        s.flanger.rateHz = 0.3f; s.flanger.depth = 0.65f; s.flanger.feedback = 0.45f;
        s.flanger.mix = 0.4f; s.flanger.stereoSpread = 120.0f; s.flanger.waveform = 1; // Triangle
        // Dotted-1/8 ping-pong delay bounces the lead across the field
        s.delayEnabled = 1;
        s.delay.type = 2; // PingPong
        s.delay.sync = 1; s.delay.noteValue = kNote1_8D;
        s.delay.mix = 0.35f; s.delay.feedback = 0.42f;
        s.delay.pingPongWidth = 180.0f;
        s.delay.pingPongModDepth = 0.15f; s.delay.pingPongModRateHz = 0.5f;
        // Light plate keeps it from sounding dry between echoes
        s.reverbEnabled = 1; s.reverbType = 0; // Plate
        s.reverb.size = 0.3f; s.reverb.mix = 0.15f;
        s.global.voiceMode = 1;
        s.monoMode.legato = 1; s.monoMode.portamentoTimeMs = 25.0f; // expressive glides
        presets.push_back(std::move(p));
    }

    // "Rossler Riot" (was Chaos Beat) - chaos-driven Euclidean stutter
    {
        PresetDef p;
        p.name = "Rossler Riot";
        p.category = "Rhythmic";
        auto& s = p.state;
        // Chaos oscillator (Rossler attractor) = the unstable, ever-shifting core
        s.oscA.type = 5; s.oscA.chaosAttractor = 1; // Rossler
        s.oscA.chaosAmount = 0.45f; s.oscA.chaosCoupling = 0.2f; // extra cross-axis instability
        s.oscA.chaosOutput = 0; s.oscA.level = 0.7f;
        s.oscB.type = 0; s.oscB.waveform = 4; // Triangle anchors pitch under the chaos
        s.oscB.level = 0.45f;
        s.mixer.position = 0.35f;
        s.filter.type = 0; // SVF LP
        s.filter.cutoffHz = 2500.0f; s.filter.resonance = 4.66f;
        // FIXED filter-env: +24 st fast decay makes each Euclidean hit snap
        s.filter.envAmount = 24.0f;
        s.filterEnv.attackMs = 3.0f; s.filterEnv.decayMs = 160.0f;
        s.filterEnv.sustain = 0.25f; s.filterEnv.releaseMs = 150.0f;
        s.filterEnv.decayCurve = 0.3f;
        s.ampEnv.attackMs = 2.0f; s.ampEnv.decayMs = 200.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 160.0f;
        // Dedicated chaos modulator feeds the matrix as kSrcChaos
        s.chaosMod.rateHz = 3.0f; s.chaosMod.type = 0; s.chaosMod.depth = 0.6f; s.chaosMod.sync = 0; // Lorenz
        // Chaos -> cutoff in STEPPED jumps (its signature gesture) + chaos -> resonance
        setModSlot(s, 0, kSrcChaos, kDstAllFltCut, 0.5f, kCurveStepped);
        setModSlot(s, 1, kSrcChaos, kDstAllResonance, 0.3f, kCurveLinear);
        // A slow synced LFO adds a second, orderly layer of cutoff motion for contrast
        s.lfo1.rateHz = 0.5f; s.lfo1.shape = 1; s.lfo1.depth = 0.4f; s.lfo1.sync = 1; // Triangle
        s.lfo1Ext.noteValue = kNote1_4;
        setModSlot(s, 2, kSrcLFO1, kDstAllMorphPos, 0.3f);
        // Euclidean E(5,8) gate = the world-rhythm signature (only Euclidean member)
        s.tranceGate.enabled = 1;
        s.tranceGate.numSteps = 8;
        s.tranceGate.tempoSync = 1; s.tranceGate.noteValue = kNote1_16;
        s.tranceGate.depth = 0.85f;
        s.tranceGate.attackMs = 2.0f; s.tranceGate.releaseMs = 15.0f;
        s.tranceGate.euclideanEnabled = 1; s.tranceGate.euclideanHits = 5; s.tranceGate.euclideanRotation = 0;
        // Granular delay smears the chaotic hits into a texture cloud
        s.delayEnabled = 1;
        s.delay.type = 3; // Granular
        s.delay.sync = 1; s.delay.noteValue = kNote1_8;
        s.delay.mix = 0.3f; s.delay.feedback = 0.35f;
        s.delay.granularSizeMs = 80.0f; s.delay.granularDensity = 20.0f;
        s.delay.granularPitch = 0.0f; s.delay.granularTexture = 0.3f; s.delay.granularWidth = 1.0f;
        s.global.masterGain = 0.85f; // chaos can spike; keep headroom
        presets.push_back(std::move(p));
    }

    // "Vowel Sequencer" (was Vocal Sequence) - talking formant sequence
    {
        PresetDef p;
        p.name = "Vowel Sequencer";
        p.category = "Rhythmic";
        auto& s = p.state;
        s.oscA.type = 0; s.oscA.waveform = 1; s.oscA.level = 0.75f; // Saw = rich source for formants
        s.oscB.type = 0; s.oscB.waveform = 4; s.oscB.level = 0.4f;  // Triangle softens the top
        s.mixer.position = 0.35f;
        // Formant filter: pick a bright vowel and shade it a little female
        s.filter.type = 5; // Formant
        s.filter.formantMorph = 1.6f;   // between E and I -> present, articulate
        s.filter.formantGender = 0.3f;  // slightly higher formants
        s.filter.resonance = 0.71f;     // inert for Formant, but keeps the bank off the Q floor
        // NOTE: for the Formant filter, 'cutoff' maps to FORMANT SHIFT (semitones).
        // 1000 Hz = 0 semitone shift baseline; the LFO/env below move it => vowel motion.
        s.filter.cutoffHz = 1000.0f;
        // FIXED filter-env in real semitones: +18 st shift gives a 'wow' on each attack
        s.filter.envAmount = 18.0f;
        s.filterEnv.attackMs = 8.0f; s.filterEnv.decayMs = 220.0f;
        s.filterEnv.sustain = 0.4f; s.filterEnv.releaseMs = 200.0f;
        s.ampEnv.attackMs = 4.0f; s.ampEnv.decayMs = 280.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 220.0f;
        // Synced saw LFO -> AllFltCut = continuous formant-shift sweep = 'talking'.
        // (The old preset routed LFO->MorphPos, which only crossfades A/B and does NOT
        //  move the vowel; AllFltCut on a Formant filter is the correct lever.)
        s.lfo1.rateHz = 1.0f; s.lfo1.shape = 2; s.lfo1.depth = 1.0f; s.lfo1.sync = 1; // Saw
        s.lfo1Ext.noteValue = kNote1_4;
        setModSlot(s, 0, kSrcLFO1, kDstAllFltCut, 0.5f, kCurveSCurve);
        // 8-step gate carves the vowel phrase into a rhythmic sequence
        s.tranceGate.enabled = 1;
        s.tranceGate.numSteps = 8;
        s.tranceGate.tempoSync = 1; s.tranceGate.noteValue = kNote1_16;
        s.tranceGate.depth = 0.9f;
        s.tranceGate.attackMs = 3.0f; s.tranceGate.releaseMs = 20.0f;
        {
            float tg[8] = {1.0f, 0.5f, 1.0f, 0.2f, 0.85f, 0.4f, 1.0f, 0.3f};
            for (int i = 0; i < 8; ++i) s.tranceGate.stepLevels[i] = tg[i];
        }
        // Small plate adds a hint of room without washing out the diction
        s.reverbEnabled = 1; s.reverbType = 0; // Plate
        s.reverb.size = 0.35f; s.reverb.mix = 0.2f; s.reverb.damping = 0.5f;
        presets.push_back(std::move(p));
    }

    // "Sidechain Wash" - Transient-ducked tape pad pumped by a rising gate ramp
    {
        PresetDef p;
        p.name = "Sidechain Wash";
        p.category = "Rhythmic";
        auto& s = p.state;
        // Dual detuned saws, splayed in cents for a wide analog bed
        s.oscA.type = 0; s.oscA.waveform = 1; s.oscA.fineCents = -6.0f; s.oscA.level = 0.70f;
        s.oscB.type = 0; s.oscB.waveform = 1; s.oscB.fineCents = 7.0f;  s.oscB.level = 0.60f;
        s.mixer.mode = 0; s.mixer.position = 0.5f;        // centre crossfade; LFO1 sweeps it
        // SVF LP with a slow, AUDIBLE filter-env swell (envAmount is in SEMITONES)
        s.filter.type = 0; s.filter.cutoffHz = 2400.0f; s.filter.resonance = 4.66f;
        s.filter.envAmount = 16.0f; s.filter.keyTrack = 0.2f; s.filter.svfDrive = 3.0f;
        // Slow pad amp + a long gradual filter swell for the wash
        s.ampEnv.attackMs = 180.0f; s.ampEnv.decayMs = 600.0f;
        s.ampEnv.sustain = 0.85f; s.ampEnv.releaseMs = 1200.0f; s.ampEnv.attackCurve = 0.4f;
        s.filterEnv.attackMs = 500.0f; s.filterEnv.decayMs = 900.0f;
        s.filterEnv.sustain = 0.45f; s.filterEnv.releaseMs = 900.0f;
        // Rising 4-step 1/4 gate ramp = the pump shape
        s.tranceGate.enabled = 1; s.tranceGate.numSteps = 4;
        s.tranceGate.tempoSync = 1; s.tranceGate.noteValue = kNote1_4;
        s.tranceGate.depth = 1.0f; s.tranceGate.attackMs = 25.0f; s.tranceGate.releaseMs = 140.0f;
        s.tranceGate.stepLevels[0] = 0.0f; s.tranceGate.stepLevels[1] = 0.45f;
        s.tranceGate.stepLevels[2] = 0.75f; s.tranceGate.stepLevels[3] = 1.0f;
        // TapeSaturator (type 5) for the warm, glued tape colour
        s.distortion.type = 5; s.distortion.drive = 0.35f; s.distortion.character = 0.6f;
        s.distortion.mix = 0.8f; s.distortion.tapeModel = 0;
        s.distortion.tapeSaturation = 0.55f; s.distortion.tapeBias = 0.45f;
        // IDENTITY: transient detector ducks the master -> sidechain-pump feel
        // (transient is unipolar; negative amount dips level on each attack then recovers)
        s.transient.sensitivity = 0.8f; s.transient.attackMs = 2.0f; s.transient.decayMs = 180.0f;
        setModSlot(s, 0, kSrcTransient, kDstMasterVol, -0.7f, kCurveExp);
        // Dual TEMPO-SYNCED LFOs keep the timbre in motion under sustain
        s.lfo1.rateHz = 0.5f; s.lfo1.shape = 0; s.lfo1.depth = 0.8f; s.lfo1.sync = 1;
        s.lfo1Ext.noteValue = 16; // 1/2 note slow morph drift
        s.lfo2.rateHz = 0.75f; s.lfo2.shape = 1; s.lfo2.depth = 0.5f; s.lfo2.sync = 1;
        s.lfo2Ext.noteValue = 13; // 1/4 note cutoff shimmer
        setModSlot(s, 1, kSrcLFO1, kDstAllMorphPos, 0.6f, kCurveSCurve);
        setModSlot(s, 2, kSrcLFO2, kDstAllFltCut, 0.25f, kCurveLinear);
        // Gate output also opens the A/B morph per step for extra rhythmic motion
        setVoiceRoute(s, 0, kVSrcGate, kVDstMorphPos, 0.4f);
        // Harmonizer: a fifth + octave shimmer thickens the wash
        s.harmonizerEnabled = 1; s.harmonizer.harmonyMode = 1; // scalic
        s.harmonizer.numVoices = 2; s.harmonizer.wetLevelDb = -10.0f;
        s.harmonizer.voiceInterval[0] = 7;  s.harmonizer.voiceLevelDb[0] = -6.0f; s.harmonizer.voicePan[0] = -0.4f;
        s.harmonizer.voiceInterval[1] = 12; s.harmonizer.voiceLevelDb[1] = -9.0f; s.harmonizer.voicePan[1] = 0.4f;
        // Light TAPE delay, then a big Hall reverb
        s.delayEnabled = 1; s.delay.type = 1; // Tape
        s.delay.sync = 1; s.delay.noteValue = kNote1_8; s.delay.feedback = 0.3f; s.delay.mix = 0.18f;
        s.delay.tapeSaturation = 0.4f; s.delay.tapeWear = 0.2f;
        s.reverbEnabled = 1; s.reverbType = 1; // Hall
        s.reverb.size = 0.8f; s.reverb.mix = 0.32f; s.reverb.damping = 0.35f; s.reverb.preDelayMs = 20.0f;
        s.global.width = 1.5f; s.global.spread = 0.4f; s.global.polyphony = 8;
        presets.push_back(std::move(p));
    }

    // "Phase Pulse" - Formant-vowel pulse, phaser-swept, chaos-wandered
    {
        PresetDef p;
        p.name = "Phase Pulse";
        p.category = "Rhythmic";
        auto& s = p.state;
        // Saw + a triangle sub-voice for body
        s.oscA.type = 0; s.oscA.waveform = 1; s.oscA.level = 0.78f;
        s.oscB.type = 0; s.oscB.waveform = 4; s.oscB.fineCents = 5.0f; s.oscB.level = 0.5f; // Triangle
        s.mixer.position = 0.4f;
        // FORMANT filter (type 5) => vocal aah/ooh pulses instead of the usual LP monoculture
        s.filter.type = 5; s.filter.cutoffHz = 1200.0f; s.filter.resonance = 7.48f;
        s.filter.formantMorph = 1.5f;    // between E and I
        s.filter.formantGender = -0.3f;  // slightly larger throat
        s.filter.envAmount = 20.0f;      // filterEnv sweeps the vowel (semitones, corrected)
        s.ampEnv.attackMs = 40.0f; s.ampEnv.decayMs = 400.0f;
        s.ampEnv.sustain = 0.75f; s.ampEnv.releaseMs = 450.0f;
        s.filterEnv.attackMs = 8.0f; s.filterEnv.decayMs = 350.0f;
        s.filterEnv.sustain = 0.3f; s.filterEnv.releaseMs = 250.0f;
        // Syncing PHASER (modulation slot) = the swept-pulse identity
        s.modulationType = 1;
        s.phaser.rateHz = 2.0f; s.phaser.depth = 0.75f; s.phaser.feedback = 0.6f;
        s.phaser.mix = 0.55f; s.phaser.stages = 3; s.phaser.centerFreqHz = 900.0f;
        s.phaser.stereoSpread = 120.0f; s.phaser.sync = 1; s.phaser.noteValue = kNote1_4;
        // 8-step 1/8 gate
        s.tranceGate.enabled = 1; s.tranceGate.numSteps = 8;
        s.tranceGate.tempoSync = 1; s.tranceGate.noteValue = kNote1_8;
        s.tranceGate.depth = 0.8f; s.tranceGate.attackMs = 10.0f; s.tranceGate.releaseMs = 45.0f;
        float tg[] = {1.0f,0.5f,0.85f,0.3f, 1.0f,0.6f,0.9f,0.25f};
        for (int i = 0; i < 8; ++i) s.tranceGate.stepLevels[i] = tg[i];
        // chaosMod source (Rossler, free-run) wanders the vowel brightness -> never repeats
        s.chaosMod.rateHz = 0.4f; s.chaosMod.type = 1; s.chaosMod.depth = 0.7f; s.chaosMod.sync = 0;
        setModSlot(s, 0, kSrcChaos, kDstAllFltCut, 0.4f, kCurveSCurve);
        // A slow synced LFO wobbles resonance on the opposite curve for a talking motion
        s.lfo1.rateHz = 0.5f; s.lfo1.shape = 2; s.lfo1.depth = 0.4f; s.lfo1.sync = 1;
        s.lfo1Ext.noteValue = 13; // 1/4 note
        setModSlot(s, 1, kSrcLFO1, kDstAllResonance, 0.3f, kCurveExp);
        // Gate output opens its own depth for a breathing pulse envelope
        setVoiceRoute(s, 0, kVSrcGate, kVDstGateDepth, 0.5f);
        s.global.width = 1.4f;
        presets.push_back(std::move(p));
    }

    // "Fractal Grid" - Ring-modulated pluck on a dense Euclidean 32-step grid
    {
        PresetDef p;
        p.name = "Fractal Grid";
        p.category = "Rhythmic";
        auto& s = p.state;
        // Saw + sub-triangle an octave down = punchy pluck body with weight
        s.oscA.type = 0; s.oscA.waveform = 1; s.oscA.level = 0.72f;
        s.oscB.type = 0; s.oscB.waveform = 4; s.oscB.tuneSemitones = -12.0f; s.oscB.level = 0.55f;
        s.mixer.position = 0.38f;
        // Ladder LP (type 4) with a STRONG corrected filter env (semitones, +22)
        s.filter.type = 4; s.filter.cutoffHz = 900.0f; s.filter.resonance = 6.35f;
        s.filter.envAmount = 22.0f; s.filter.ladderSlope = 4; s.filter.ladderDrive = 4.0f;
        // Punchy pluck amp; fast snappy filter env
        s.ampEnv.attackMs = 2.0f; s.ampEnv.decayMs = 220.0f;
        s.ampEnv.sustain = 0.55f; s.ampEnv.releaseMs = 160.0f; s.ampEnv.decayCurve = 0.4f;
        s.filterEnv.attackMs = 1.0f; s.filterEnv.decayMs = 170.0f;
        s.filterEnv.sustain = 0.15f; s.filterEnv.releaseMs = 120.0f; s.filterEnv.decayCurve = 0.5f;
        // RingModulator (type 6), note-tracked octave => metallic bell edge on each pluck
        s.distortion.type = 6; s.distortion.drive = 0.3f; s.distortion.mix = 0.4f;
        s.distortion.ringFreqMode = 1;      // NoteTrack
        s.distortion.ringRatio = 0.1111f;   // normalized -> ~2.0 (octave sidebands)
        s.distortion.ringWaveform = 1;      // Triangle carrier (softer than square)
        s.distortion.ringStereoSpread = 0.3f;
        // Dense 32-step 1/16 gate, further carved by a EUCLIDEAN mask E(23,32)
        s.tranceGate.enabled = 1; s.tranceGate.numSteps = 32;
        s.tranceGate.tempoSync = 1; s.tranceGate.noteValue = kNote1_16;
        s.tranceGate.depth = 0.95f; s.tranceGate.attackMs = 1.0f; s.tranceGate.releaseMs = 8.0f;
        s.tranceGate.euclideanEnabled = 1; s.tranceGate.euclideanHits = 23; s.tranceGate.euclideanRotation = 3;
        float tg32[] = {
            1,0.6f,0.8f,0.4f, 1,0.7f,0.5f,0.9f, 1,0.5f,0.85f,0.6f, 0.7f,1,0.4f,0.8f,
            1,0.6f,0.9f,0.5f, 0.8f,1,0.6f,0.7f, 1,0.5f,0.8f,0.9f, 0.6f,1,0.5f,0.85f
        };
        for (int i = 0; i < 32; ++i) s.tranceGate.stepLevels[i] = tg32[i];
        // Gate output pumps the ring-mod drive -> the metallic edge tracks the rhythm
        setVoiceRoute(s, 0, kVSrcGate, kVDstDistDrive, 0.5f);
        // Synced DIGITAL delay with vintage age + a wider image
        s.delayEnabled = 1; s.delay.type = 0; // Digital
        s.delay.sync = 1; s.delay.noteValue = kNote1_8D; s.delay.mix = 0.2f; s.delay.feedback = 0.35f;
        s.delay.digitalEra = 1; s.delay.digitalAge = 0.3f; s.delay.digitalWidth = 140.0f;
        s.global.width = 1.3f;
        presets.push_back(std::move(p));
    }

    // "Ratchet Fury" - Phase-distortion lead machine-gunned by a 4x ratchet arp
    {
        PresetDef p;
        p.name = "Ratchet Fury";
        p.category = "Rhythmic";
        auto& s = p.state;
        // OFF the saw-lead template: PHASE DISTORTION engine (resonant CZ voices)
        s.oscA.type = 2; s.oscA.pdWaveform = 5; s.oscA.pdDistortion = 0.7f; s.oscA.level = 0.85f; // ResSaw
        s.oscB.type = 2; s.oscB.pdWaveform = 2; s.oscB.pdDistortion = 0.55f; // Pulse PD
        s.oscB.tuneSemitones = -12.0f; s.oscB.fineCents = 4.0f; s.oscB.level = 0.5f;
        s.mixer.position = 0.42f;
        // Ladder LP with an audible env sweep so each ratchet burst chirps
        s.filter.type = 4; s.filter.cutoffHz = 1500.0f; s.filter.resonance = 5.79f;
        s.filter.envAmount = 26.0f; s.filter.ladderDrive = 5.0f; s.filter.keyTrack = 0.4f;
        s.ampEnv.attackMs = 3.0f; s.ampEnv.decayMs = 180.0f;
        s.ampEnv.sustain = 0.55f; s.ampEnv.releaseMs = 140.0f;
        s.filterEnv.attackMs = 2.0f; s.filterEnv.decayMs = 120.0f;
        s.filterEnv.sustain = 0.25f; s.filterEnv.releaseMs = 100.0f; s.filterEnv.decayCurve = 0.4f;
        // MONO with legato glide => 303-style slides between arp notes
        s.global.voiceMode = 1; s.global.polyphony = 1;
        s.monoMode.priority = 0; s.monoMode.legato = 1;
        s.monoMode.portamentoTimeMs = 45.0f; s.monoMode.portaMode = 1; // legato-only glide
        // ARP with the heavy RATCHET lane (up to 4x) - the identity of this preset
        setArpEnabled(s, true); setArpMode(s, kModeUp); setTempoSync(s, true);
        setArpRate(s, kNote1_16); setArpGateLength(s, 65.0f); s.arp.octaveRange = 2;
        float vel8[] = {1.0f, 0.55f, 0.8f, 0.5f, 1.0f, 0.65f, 0.9f, 0.45f};
        setVelocityLane(s, 8, vel8);
        int32_t ratch8[] = {1, 2, 1, 4, 1, 3, 4, 2};
        setRatchetLane(s, 8, ratch8);
        s.arp.ratchetSwing = 66.0f;
        // FLANGER (modulation slot) gives the ratchet rolls a jet-sweep tail
        s.modulationType = 2;
        s.flanger.rateHz = 0.3f; s.flanger.depth = 0.7f; s.flanger.feedback = 0.5f;
        s.flanger.mix = 0.45f; s.flanger.stereoSpread = 110.0f;
        // Velocity drives cutoff so accented arp steps open up
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.5f);
        // A slow synced LFO breathes the resonance across bars
        s.lfo1.rateHz = 0.25f; s.lfo1.shape = 0; s.lfo1.depth = 0.4f; s.lfo1.sync = 1;
        s.lfo1Ext.noteValue = 19; // whole note
        setModSlot(s, 0, kSrcLFO1, kDstAllResonance, 0.3f, kCurveSCurve);
        // PINGPONG delay bounces the ratchet bursts across the stereo field
        s.delayEnabled = 1; s.delay.type = 2; // PingPong
        s.delay.sync = 1; s.delay.noteValue = kNote1_8; s.delay.mix = 0.22f; s.delay.feedback = 0.35f;
        s.delay.pingPongWidth = 150.0f; s.delay.pingPongCrossFeed = 0.8f;
        presets.push_back(std::move(p));
    }

    // "Glitch Step" - Bit-crushed noise stabs, S&H-scrambled under a hard gate
    {
        PresetDef p;
        p.name = "Glitch Step";
        p.category = "Rhythmic";
        auto& s = p.state;
        // Triangle body + a layer of BLUE noise for digital hiss
        s.oscA.type = 0; s.oscA.waveform = 4; s.oscA.level = 0.7f;   // Triangle
        s.oscB.type = 9; s.oscB.noiseColor = 3; s.oscB.level = 0.18f; // Blue noise
        s.mixer.position = 0.18f;
        s.filter.type = 0; s.filter.cutoffHz = 3800.0f; s.filter.resonance = 4.09f;
        s.filter.envAmount = 18.0f; // corrected semitone sweep
        // SPECTRAL distortion as a bitcrusher/decimator
        s.distortion.type = 2; s.distortion.drive = 0.4f; s.distortion.mix = 0.85f;
        s.distortion.spectralMode = 3; s.distortion.spectralCurve = 4; s.distortion.spectralBits = 0.3f;
        s.ampEnv.attackMs = 1.0f; s.ampEnv.decayMs = 140.0f;
        s.ampEnv.sustain = 0.55f; s.ampEnv.releaseMs = 70.0f;
        s.filterEnv.attackMs = 1.0f; s.filterEnv.decayMs = 120.0f; s.filterEnv.sustain = 0.2f;
        // Hard 16-step 1/16 gate with RETRIGGER DEPTH for stutter accents
        s.tranceGate.enabled = 1; s.tranceGate.numSteps = 16;
        s.tranceGate.tempoSync = 1; s.tranceGate.noteValue = kNote1_16;
        s.tranceGate.depth = 1.0f; s.tranceGate.attackMs = 0.5f; s.tranceGate.releaseMs = 3.0f;
        s.tranceGate.retriggerDepth = 0.6f;
        float tg[] = {1,0,0,1, 0,1,1,0, 1,1,0,0, 0,1,1,1};
        for (int i = 0; i < 16; ++i) s.tranceGate.stepLevels[i] = tg[i];
        // DUAL LFOs: lfo1 free S&H scrambles cutoff, lfo2 synced pushes the morph
        s.lfo1.rateHz = 8.0f; s.lfo1.shape = 4; s.lfo1.depth = 0.6f; s.lfo1.sync = 0; // free S&H
        s.lfo2.rateHz = 2.0f; s.lfo2.shape = 1; s.lfo2.depth = 0.5f; s.lfo2.sync = 1; // synced tri
        s.lfo2Ext.noteValue = 10; // 1/8
        setModSlot(s, 0, kSrcLFO1, kDstAllFltCut, 0.45f, kCurveStepped);  // stepped => hard glitch jumps
        setModSlot(s, 1, kSrcLFO2, kDstAllMorphPos, 0.4f, kCurveSCurve);
        // Sample & Hold source scrambles the spectral tilt for extra digital chatter
        s.sampleHold.rateHz = 6.0f; s.sampleHold.sync = 0; s.sampleHold.slewMs = 2.0f;
        setModSlot(s, 2, kSrcSampleHold, kDstAllSpecTilt, 0.5f, kCurveStepped);
        // Gate output pumps the bitcrush drive on every hit
        setVoiceRoute(s, 0, kVSrcGate, kVDstDistDrive, 0.5f);
        // GRANULAR delay smears the stutters into a glitch cloud
        s.delayEnabled = 1; s.delay.type = 3; // Granular
        s.delay.sync = 1; s.delay.noteValue = kNote1_16; s.delay.mix = 0.22f; s.delay.feedback = 0.3f;
        s.delay.granularSizeMs = 80.0f; s.delay.granularDensity = 20.0f;
        s.delay.granularPitchSpray = 0.2f; s.delay.granularReverseProb = 0.25f;
        s.global.width = 1.3f;
        presets.push_back(std::move(p));
    }

    // ==================== EXPERIMENTAL Category (15 new presets) ====================

    // "Lorenz Choir" - chaotic attractor sung through an SVF-peak formant into a diatonic choir
    {
        PresetDef p;
        p.name = "Lorenz Choir";
        p.category = "Experimental";
        auto& s = p.state;

        // --- Voice: chaotic attractor thickened by a sub-sine anchor ---
        s.oscA.type = 5;              // Chaos
        s.oscA.chaosAttractor = 0;    // Lorenz - warm quasi-pitched turbulence
        s.oscA.chaosAmount = 0.42f;   // enough motion, still tonal (not noise)
        s.oscA.chaosCoupling = 0.35f; // cross-axis wander for organic drift
        s.oscA.chaosOutput = 2;       // Z axis - the brighter Lorenz lobe
        s.oscA.level = 0.7f;
        s.oscB.type = 0; s.oscB.waveform = 0;  // pure sine
        s.oscB.tuneSemitones = -12.0f;         // sub octave anchors the chaos to pitch
        s.oscB.level = 0.5f;
        s.mixer.position = 0.4f;      // favor chaos, sine grounds it

        // --- Filter: SVF PEAK sculpts a resonant vocal band in the chaos ---
        s.filter.type = 8;            // SVF Peak
        s.filter.cutoffHz = 900.0f;   // vocal-ish peak center
        s.filter.resonance = 3.5f;
        s.filter.svfGain = 12.0f;     // +12 dB boost = the peak that sings
        s.filter.svfSlope = 1;        // 24 dB skirts
        s.filter.svfDrive = 3.0f;     // gentle saturation on the peak
        s.filter.envAmount = 24.0f;   // FIX: real semitone sweep (avoids the old -48..48 bug value)
        s.filterEnv.attackMs = 80.0f; s.filterEnv.decayMs = 700.0f;
        s.filterEnv.sustain = 0.4f; s.filterEnv.releaseMs = 900.0f;
        s.filterEnv.decayCurve = 0.4f; // slow-start decay = gliding peak fall

        // --- ChaosWaveshaper distortion: chaos folded by chaos (thematic) ---
        s.distortion.type = 1;        // ChaosWaveshaper
        s.distortion.drive = 0.3f;    // subtle grit, level-safe
        s.distortion.chaosModel = 0;  // Lorenz waveshaper to match the osc
        s.distortion.chaosSpeed = 0.3f;
        s.distortion.chaosCoupling = 0.2f;
        s.distortion.mix = 0.35f;

        // --- Amp env: choir-like swell ---
        s.ampEnv.attackMs = 60.0f; s.ampEnv.decayMs = 500.0f;
        s.ampEnv.sustain = 0.75f; s.ampEnv.releaseMs = 900.0f;

        // --- Dedicated chaos LFO adds a second, slower turbulence layer ---
        s.chaosMod.rateHz = 0.25f; s.chaosMod.type = 0; // Lorenz
        s.chaosMod.depth = 0.6f; s.chaosMod.sync = 0;

        // --- Mod web (unique): chaos + envelope-follower drive morph & sweep depth ---
        s.modMatrix.slots[0].source = 9;  // Chaos (the dedicated chaosMod)
        s.modMatrix.slots[0].dest = 5;    // All Morph Position - glides the chaos/sine blend
        s.modMatrix.slots[0].amount = 0.5f;
        s.modMatrix.slots[0].curve = 2;   // SCurve
        s.modMatrix.slots[0].smoothMs = 40.0f; // tame chaos steps
        s.modMatrix.slots[1].source = 1;  // LFO1
        s.modMatrix.slots[1].dest = 8;    // All Resonance - breathing formant width
        s.modMatrix.slots[1].amount = 0.35f;
        s.modMatrix.slots[1].curve = 1;   // Exp
        s.modMatrix.slots[2].source = 3;  // EnvFollower
        s.modMatrix.slots[2].dest = 9;    // All Filter Env Amount - louder input = deeper sweep
        s.modMatrix.slots[2].amount = 0.4f;
        s.envFollower.sensitivity = 0.7f; s.envFollower.attackMs = 20.0f; s.envFollower.releaseMs = 250.0f;

        s.lfo1.rateHz = 0.4f; s.lfo1.shape = 0; s.lfo1.depth = 0.5f; s.lfo1.sync = 0; // free sine
        s.lfo2.rateHz = 0.13f; s.lfo2.shape = 2; s.lfo2.depth = 0.4f; s.lfo2.sync = 0; // slow saw ramp

        // --- Mod envelope opens the peak on each note (modEnv used) ---
        s.modEnv.attackMs = 5.0f; s.modEnv.decayMs = 600.0f; s.modEnv.sustain = 0.2f; s.modEnv.releaseMs = 500.0f;
        s.voiceRoutes[0].source = 2;      // Env3 (mod env)
        s.voiceRoutes[0].destination = 0; // Filter Cutoff
        s.voiceRoutes[0].amount = 0.35f;
        s.voiceRoutes[0].active = 1;

        // --- Harmonizer: SCALIC, PhaseVocoder, formant-preserved choir ---
        s.harmonizerEnabled = 1;
        s.harmonizer.harmonyMode = 1;     // Scalic (diatonic)
        s.harmonizer.pitchShiftMode = 2;  // PhaseVocoder (smooth, formant-safe)
        s.harmonizer.formantPreserve = 1;
        s.harmonizer.numVoices = 4;
        s.harmonizer.key = 0; s.harmonizer.scale = 0; // C major
        s.harmonizer.dryLevelDb = -1.0f; s.harmonizer.wetLevelDb = -4.0f;
        s.harmonizer.voiceInterval[0] = 2;  s.harmonizer.voicePan[0] = -0.6f; s.harmonizer.voiceDetuneCents[0] = -6.0f;
        s.harmonizer.voiceInterval[1] = 4;  s.harmonizer.voicePan[1] = 0.6f;  s.harmonizer.voiceDetuneCents[1] = 6.0f;
        s.harmonizer.voiceInterval[2] = 7;  s.harmonizer.voicePan[2] = -0.3f;
        s.harmonizer.voiceInterval[3] = 11; s.harmonizer.voicePan[3] = 0.3f;  s.harmonizer.voiceLevelDb[3] = -3.0f;

        // --- Tape delay (+splice) for a warbly analog tail (unique FX signature) ---
        s.delayEnabled = 1;
        s.delay.type = 1;             // Tape
        s.delay.sync = 1; s.delay.noteValue = kNote1_8D;
        s.delay.mix = 0.22f; s.delay.feedback = 0.4f;
        s.delay.tapeSaturation = 0.5f; s.delay.tapeWear = 0.3f; s.delay.tapeAge = 0.4f;
        s.delay.tapeSpliceEnabled = 1; s.delay.tapeSpliceIntensity = 0.35f;

        // --- Reverb: Plate, medium - keeps the choir intimate (not a giant hall) ---
        s.reverbEnabled = 1;
        s.reverbType = 0;             // Plate
        s.reverb.size = 0.55f; s.reverb.mix = 0.28f; s.reverb.damping = 0.4f;

        presets.push_back(std::move(p));
    }

    // "Tidal Lock" - self-oscillating drone that tracks played pitch, frozen in an infinite hall
    {
        PresetDef p;
        p.name = "Tidal Lock";
        p.category = "Experimental";
        auto& s = p.state;

        // --- Source: brown-noise wind + deep sine feeding a screaming resonator ---
        s.oscA.type = 9; s.oscA.noiseColor = 2; // Brown noise (rumble/wind)
        s.oscA.level = 0.28f;
        s.oscB.type = 0; s.oscB.waveform = 0;    // sine
        s.oscB.tuneSemitones = -24.0f; s.oscB.level = 0.5f; // 2-oct sub anchor
        s.mixer.position = 0.5f;

        // --- Self-oscillating filter IS the drone's voice (res 25) ---
        s.filter.type = 12;          // Self-Oscillating
        s.filter.cutoffHz = 160.0f;  // low fundamental sine-tone
        s.filter.resonance = 25.0f;
        s.filter.selfOscGlide = 2500.0f; // slow portamento between pitches
        s.filter.selfOscExtMix = 0.45f;  // let noise+sub bleed through the resonator
        s.filter.selfOscShape = 0.3f;    // slightly non-sine timbre
        s.filter.selfOscRelease = 2500.0f;
        s.filter.keyTrack = 1.0f;    // resonator follows the keyboard

        // --- Spectral distortion: faint shimmer dusted onto the drone ---
        s.distortion.type = 2;       // Spectral
        s.distortion.drive = 0.25f;
        s.distortion.spectralMode = 1;
        s.distortion.spectralCurve = 4;
        s.distortion.spectralBits = 0.7f;
        s.distortion.mix = 0.2f;     // subtle, keeps the drone clean-ish

        // --- Glacial amp env ---
        s.ampEnv.attackMs = 2500.0f; s.ampEnv.decayMs = 800.0f;
        s.ampEnv.sustain = 0.9f; s.ampEnv.releaseMs = 4000.0f;

        // --- Mod web (unique): PITCH FOLLOWER makes the drone track played pitch ---
        s.pitchFollower.minHz = 40.0f; s.pitchFollower.maxHz = 800.0f;
        s.pitchFollower.confidence = 0.4f; s.pitchFollower.speedMs = 300.0f; // slow glide-track
        s.modMatrix.slots[0].source = 12; // PitchFollow
        s.modMatrix.slots[0].dest = 4;    // All Filter Cutoff
        s.modMatrix.slots[0].amount = 0.8f;
        s.modMatrix.slots[0].curve = 0;   // Linear tracking
        s.modMatrix.slots[0].scale = 4;   // x4 - extreme range so it truly tracks octaves
        s.modMatrix.slots[0].smoothMs = 80.0f;
        // Smooth-random LFO wobbles the resonance for a living, breathing tone
        s.lfo1.rateHz = 0.05f; s.lfo1.shape = 5; s.lfo1.depth = 0.5f; s.lfo1.sync = 0; // SmoothRandom
        s.modMatrix.slots[1].source = 1;  // LFO1
        s.modMatrix.slots[1].dest = 8;    // All Resonance
        s.modMatrix.slots[1].amount = 0.25f;
        s.modMatrix.slots[1].curve = 2;   // SCurve

        // --- Reverb FREEZE: infinite cathedral wash under the drone ---
        s.reverbEnabled = 1;
        s.reverbType = 1;            // Hall
        s.reverb.size = 0.95f; s.reverb.mix = 0.42f; s.reverb.damping = 0.55f;
        s.reverb.freeze = 1;         // frozen tail = endless sustain bed
        s.reverb.modRateHz = 0.15f; s.reverb.modDepth = 0.3f; // slow chorus on the wash

        presets.push_back(std::move(p));
    }

    // "Fission Cloud" - sync/particle spectral morph, exotic-source hub, full mod matrix
    {
        PresetDef p;
        p.name = "Fission Cloud";
        p.category = "Experimental";
        auto& s = p.state;

        // --- Two engines spectrally interpolated: sync edge vs granular haze ---
        s.oscA.type = 3;             // Sync
        s.oscA.syncRatio = 3.0f; s.oscA.syncWaveform = 1;
        s.oscA.syncMode = 2; // PhaseAdvance: smooth phase-shift sync (not hard tearing) suits the spectral morph
        s.oscA.syncAmount = 0.85f;
        s.oscA.level = 0.7f;
        s.oscB.type = 6;             // Particle
        s.oscB.particleScatter = 6.0f; s.oscB.particleDensity = 28.0f;
        s.oscB.particleLifetime = 160.0f; s.oscB.particleSpawnMode = 1; // Random spawn
        s.oscB.particleEnvType = 3;  // Blackman grains = smooth cloud
        s.oscB.level = 0.6f;
        s.mixer.mode = 1;            // Spectral Morph (FFT interpolation A<->B)
        s.mixer.position = 0.5f;
        s.mixer.tilt = 3.0f;         // brighten the morph
        s.mixer.shift = 40.0f;       // inharmonic frequency shift on the morph

        // --- Comb filter: metallic resonant body distinct from every sibling ---
        s.filter.type = 6;          // Comb
        s.filter.cutoffHz = 440.0f; // comb tuned near A
        s.filter.resonance = 6.0f;  // strong feedback = ringing tine
        s.filter.combDamping = 0.4f;// tame the top of the feedback

        // --- Wavefolder: folds the morph into extra harmonics ---
        s.distortion.type = 4;      // Wavefolder
        s.distortion.drive = 0.4f;
        s.distortion.foldType = 1;
        s.distortion.character = 0.6f;
        s.distortion.mix = 0.5f;

        s.ampEnv.attackMs = 120.0f; s.ampEnv.decayMs = 600.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 850.0f;

        // --- Exotic-source HUB: rungler + S&H + random all drive the morph web ---
        s.rungler.osc1FreqHz = 2.5f; s.rungler.osc2FreqHz = 3.7f;
        s.rungler.depth = 0.7f; s.rungler.filter = 0.4f; s.rungler.bits = 6; s.rungler.loopMode = 0; // chaos
        s.modMatrix.slots[0].source = 10; // Rungler
        s.modMatrix.slots[0].dest = 7;    // Spectral Tilt - Benjolin moves the morph spectrum
        s.modMatrix.slots[0].amount = 0.5f;
        s.modMatrix.slots[0].curve = 3;   // Stepped - stair-stepped Benjolin motion
        s.sampleHold.rateHz = 6.0f; s.sampleHold.sync = 0; s.sampleHold.slewMs = 15.0f;
        s.modMatrix.slots[1].source = 11; // SampleHold
        s.modMatrix.slots[1].dest = 8;    // All Resonance
        s.modMatrix.slots[1].amount = 0.3f;
        s.modMatrix.slots[1].curve = 3;   // Stepped
        s.random.rateHz = 0.7f; s.random.smoothness = 0.8f;
        s.modMatrix.slots[2].source = 4;  // Random
        s.modMatrix.slots[2].dest = 4;    // All Filter Cutoff
        s.modMatrix.slots[2].amount = 0.35f;
        s.modMatrix.slots[2].curve = 2;   // SCurve

        // --- MACRO 1 multi-routed: one knob morphs the whole cloud ---
        s.macros.values[0] = 0.5f;        // park at center
        s.modMatrix.slots[3].source = 5;  // Macro1
        s.modMatrix.slots[3].dest = 5;    // Morph Position
        s.modMatrix.slots[3].amount = 0.8f;
        s.modMatrix.slots[4].source = 5;  // Macro1
        s.modMatrix.slots[4].dest = 3;    // Effect Mix
        s.modMatrix.slots[4].amount = 0.5f;
        s.modMatrix.slots[5].source = 5;  // Macro1
        s.modMatrix.slots[5].dest = 0;    // Global Filter Cutoff
        s.modMatrix.slots[5].amount = 0.6f; s.modMatrix.slots[5].scale = 3; // x2 wide sweep

        // --- Dual LFO: triangle + S&H on the morph tilt region ---
        s.lfo1.rateHz = 0.2f; s.lfo1.shape = 1; s.lfo1.depth = 0.7f; s.lfo1.sync = 0; // triangle
        s.lfo2.rateHz = 3.0f; s.lfo2.shape = 4; s.lfo2.depth = 0.4f; s.lfo2.sync = 0; // S&H
        s.modMatrix.slots[6].source = 2;  // LFO2
        s.modMatrix.slots[6].dest = 7;    // Spectral Tilt
        s.modMatrix.slots[6].amount = 0.3f;
        s.modMatrix.slots[6].smoothMs = 25.0f;
        // slot7 prepared but BYPASSED: an alternate LFO1->morph route the user can enable
        s.modMatrix.slots[7].source = 1;  // LFO1
        s.modMatrix.slots[7].dest = 5;    // Morph Position
        s.modMatrix.slots[7].amount = 0.4f;
        s.modMatrix.slots[7].bypass = 1;  // off by default (demonstrates the bypass axis)

        // --- Global filter engaged (macro sweeps it) ---
        s.globalFilter.enabled = 1; s.globalFilter.type = 0; // LP
        s.globalFilter.cutoffHz = 3000.0f; s.globalFilter.resonance = 1.2f;

        // --- Performance settings: shape how the cloud plays ---
        s.settings.velocityCurve = 2;   // Hard - expressive dynamics
        s.settings.voiceAllocMode = 3;  // HighNote priority
        s.settings.voiceStealMode = 1;  // Soft steal (no clicks on this dense patch)
        s.settings.pitchBendRangeSemitones = 12.0f; // octave dive-bombs
        s.global.polyphony = 6;

        // --- Digital delay: clean, wide, modulated ---
        s.delayEnabled = 1;
        s.delay.type = 0;           // Digital
        s.delay.sync = 1; s.delay.noteValue = kNote1_8;
        s.delay.mix = 0.25f; s.delay.feedback = 0.45f;
        s.delay.digitalModDepth = 0.3f; s.delay.digitalModRateHz = 0.4f; s.delay.digitalWidth = 150.0f;

        // --- Reverb: Plate, moderate (delay carries the space, not a huge hall) ---
        s.reverbEnabled = 1;
        s.reverbType = 0;           // Plate
        s.reverb.size = 0.5f; s.reverb.mix = 0.22f;

        presets.push_back(std::move(p));
    }

    // "Glossolalia" - formant osc + formant filter + note-tracked ring mod, expressive under touch
    {
        PresetDef p;
        p.name = "Glossolalia";
        p.category = "Experimental";
        auto& s = p.state;

        // --- Formant oscillator: a synthetic voice between two vowels ---
        s.oscA.type = 7;            // Formant
        s.oscA.formantVowel = 0; s.oscA.formantMorph = 1.5f; // between A and I
        s.oscA.level = 0.85f;
        s.oscB.type = 0; s.oscB.waveform = 0; s.oscB.tuneSemitones = -12.0f; s.oscB.level = 0.25f; // sub body
        s.mixer.position = 0.15f;

        // --- FORMANT filter (not the inert ladder): a SECOND vocal tract on the tone ---
        s.filter.type = 5;         // Formant
        s.filter.formantMorph = 2.5f;   // filter vowel offset from osc vowel = talking timbre
        s.filter.formantGender = -0.4f; // shift formants down = deeper/masc voice
        s.filter.cutoffHz = 1200.0f;
        s.filter.resonance = 4.0f;
        s.filter.keyTrack = 0.5f;  // vowels track pitch partway

        // --- Ring modulator clangs the vowel against a note-tracked carrier ---
        s.distortion.type = 6;     // Ring Modulator
        s.distortion.drive = 0.4f;
        s.distortion.ringFreqMode = 1;  // NoteTrack - carrier follows pitch = musical clang
        s.distortion.ringFreq = 0.5f;
        s.distortion.ringRatio = 0.35f; // non-integer ratio for inharmonic bell edge
        s.distortion.ringWaveform = 1;  // Triangle carrier
        s.distortion.ringStereoSpread = 0.4f;
        s.distortion.mix = 0.55f;

        s.ampEnv.attackMs = 30.0f; s.ampEnv.decayMs = 450.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 500.0f;

        // --- Mod web (unique - NOT the shared LFO->morphPos): animate ring & vowel ---
        // Voice LFO -> distortion drive: ringRatio isn't a routable dest, so we wobble the
        // ring's intensity for the same living, talking motion (vibrato-rate vocal shimmer).
        s.lfo1.rateHz = 5.5f; s.lfo1.shape = 0; s.lfo1.depth = 0.6f; s.lfo1.sync = 0; // vibrato rate sine
        s.voiceRoutes[0].source = 3;      // Voice LFO
        s.voiceRoutes[0].destination = 3; // Distortion Drive
        s.voiceRoutes[0].amount = 0.5f;
        s.voiceRoutes[0].active = 1;
        // Aftertouch -> filter resonance: lean on the key to sharpen the vowel
        s.voiceRoutes[1].source = 7;      // Aftertouch
        s.voiceRoutes[1].destination = 1; // Filter Resonance
        s.voiceRoutes[1].amount = 0.5f;
        s.voiceRoutes[1].active = 1;
        // Velocity -> morph position: harder = brighter vowel blend
        s.voiceRoutes[2].source = 5;      // Velocity
        s.voiceRoutes[2].destination = 2; // Morph Position
        s.voiceRoutes[2].amount = 0.4f;
        s.voiceRoutes[2].active = 1;
        // Global: synced square LFO2 chops the wet ring level rhythmically
        s.lfo2.rateHz = 2.0f; s.lfo2.shape = 3; s.lfo2.depth = 0.4f; s.lfo2.sync = 1; s.lfo2Ext.noteValue = kNote1_8;
        s.modMatrix.slots[0].source = 2;  // LFO2
        s.modMatrix.slots[0].dest = 3;    // Effect Mix
        s.modMatrix.slots[0].amount = 0.3f;
        s.modMatrix.slots[0].curve = 1;   // Exp

        // --- Chorus widens the choir-of-one ---
        s.modulationType = 3;      // Chorus
        s.chorus.rateHz = 0.6f; s.chorus.depth = 0.4f; s.chorus.mix = 0.4f; s.chorus.voices = 3;

        // --- Reverb: Hall, medium-small - a room for the voice ---
        s.reverbEnabled = 1;
        s.reverbType = 1;          // Hall
        s.reverb.size = 0.5f; s.reverb.mix = 0.24f; s.reverb.preDelayMs = 20.0f;

        presets.push_back(std::move(p));
    }

    // "Grain Reactor" - granular dist + granular delay, transient- and auto-wah-reactive
    {
        PresetDef p;
        p.name = "Grain Reactor";
        p.category = "Experimental";
        auto& s = p.state;

        // --- Two detuned saws = raw fuel for the grain engines ---
        s.oscA.type = 0; s.oscA.waveform = 1; s.oscA.level = 0.7f;  // saw
        s.oscA.fineCents = -8.0f;
        s.oscB.type = 0; s.oscB.waveform = 1; s.oscB.level = 0.55f; // saw
        s.oscB.fineCents = 9.0f;
        s.mixer.position = 0.5f;

        // --- ENV FILTER (auto-wah): the filter itself reacts to the note's attack ---
        s.filter.type = 11;        // Env Filter
        s.filter.cutoffHz = 500.0f;
        s.filter.resonance = 5.0f;
        s.filter.envSubType = 1;   // BP - vocal-ish auto-wah
        s.filter.envSensitivity = 6.0f;
        s.filter.envDepth = 0.9f;
        s.filter.envAttack = 8.0f; s.filter.envRelease = 200.0f;
        s.filter.envDirection = 0; // Up sweep on transients

        // --- Granular distortion shreds the saws into a grain cloud ---
        s.distortion.type = 3;     // Granular
        s.distortion.drive = 0.5f;
        s.distortion.grainSize = 0.22f; s.distortion.grainDensity = 0.55f;
        s.distortion.grainVariation = 0.5f; s.distortion.grainJitter = 0.35f;
        s.distortion.mix = 0.7f;

        s.ampEnv.attackMs = 15.0f; s.ampEnv.decayMs = 450.0f;
        s.ampEnv.sustain = 0.65f; s.ampEnv.releaseMs = 550.0f;

        // --- Mod web (unique): TRANSIENT detector reacts to attacks ---
        // grain-density isn't a routable dest, so the transient drives grain wetness/drive
        // (= grain intensity) so each attack bursts more grains - the directive's intent.
        s.transient.sensitivity = 0.7f; s.transient.attackMs = 1.5f; s.transient.decayMs = 80.0f;
        s.modMatrix.slots[0].source = 13; // Transient
        s.modMatrix.slots[0].dest = 3;    // Effect Mix - grain wetness bursts on attack
        s.modMatrix.slots[0].amount = 0.5f;
        s.modMatrix.slots[0].curve = 1;   // Exp - snappy transient response
        // Fast mod-envelope also punches distortion drive per note (per-note grain burst)
        s.modEnv.attackMs = 1.0f; s.modEnv.decayMs = 120.0f; s.modEnv.sustain = 0.0f; s.modEnv.releaseMs = 100.0f;
        s.voiceRoutes[0].source = 2;      // Env3 (mod env - fast blip)
        s.voiceRoutes[0].destination = 3; // Distortion Drive
        s.voiceRoutes[0].amount = 0.5f;
        s.voiceRoutes[0].active = 1;
        // LFO1 saw slowly sweeps the grain field via spectral tilt
        s.lfo1.rateHz = 0.3f; s.lfo1.shape = 2; s.lfo1.depth = 0.5f; s.lfo1.sync = 0; // saw ramp
        s.modMatrix.slots[1].source = 1;  // LFO1
        s.modMatrix.slots[1].dest = 7;    // Spectral Tilt
        s.modMatrix.slots[1].amount = 0.3f;
        s.modMatrix.slots[1].curve = 2;   // SCurve

        // --- Granular DELAY smears the grains into a stereo cloud ---
        s.delayEnabled = 1;
        s.delay.type = 3;          // Granular
        s.delay.sync = 1; s.delay.noteValue = kNote1_8D;
        s.delay.mix = 0.32f; s.delay.feedback = 0.4f;
        s.delay.granularSizeMs = 60.0f; s.delay.granularDensity = 28.0f;
        s.delay.granularPitchSpray = 0.35f; s.delay.granularPanSpray = 0.6f;
        s.delay.granularReverseProb = 0.25f; s.delay.granularJitter = 0.3f;

        // --- Reverb: Plate, short - keeps the grains articulate, not washed out ---
        s.reverbEnabled = 1;
        s.reverbType = 0;          // Plate
        s.reverb.size = 0.45f; s.reverb.mix = 0.2f; s.reverb.damping = 0.4f;

        presets.push_back(std::move(p));
    }

    // "Dub Chamber" - Mono gliding saw into a self-oscillating spliced-tape echo
    {
        PresetDef p;
        p.name = "Dub Chamber";
        p.category = "Experimental";
        auto& s = p.state;
        // Single saw source, OSC B silent (A-only blend)
        s.oscA.type = 0; s.oscA.waveform = 1; s.oscA.level = 0.68f;
        s.oscB.level = 0.0f;
        s.mixer.position = 0.0f;
        // Ladder with drive so the source has grit before it hits the tape
        s.filter.type = 4;                 // Ladder LP
        s.filter.cutoffHz = 2200.0f; s.filter.resonance = 3.86f;
        s.filter.ladderSlope = 4;          // 24 dB/oct
        s.filter.ladderDrive = 6.0f;       // pushes the ladder into warm saturation
        s.filter.envAmount = 24.0f;        // +24 st sweep (FIX: was the 4000 'Hz' bug elsewhere)
        // Plucked filter env = each stab opens then closes
        s.filterEnv.attackMs = 2.0f; s.filterEnv.decayMs = 260.0f;
        s.filterEnv.sustain = 0.15f; s.filterEnv.releaseMs = 300.0f;
        // Dub-stab amp shape
        s.ampEnv.attackMs = 3.0f; s.ampEnv.decayMs = 240.0f;
        s.ampEnv.sustain = 0.4f; s.ampEnv.releaseMs = 420.0f;
        // Mono + glide turns it into a riff instrument (unused area)
        s.global.voiceMode = 1;            // Mono
        s.monoMode.legato = 1;
        s.monoMode.portamentoTimeMs = 40.0f;
        s.monoMode.portaMode = 1;          // Legato-only glide
        // Self-oscillating spliced tape delay = the star
        s.delayEnabled = 1;
        s.delay.type = 1;                  // Tape
        constexpr int32_t kNote1_4D = 14;  // dotted quarter (note-value dropdown index 14 = "1/4D")
        s.delay.sync = 1; s.delay.noteValue = kNote1_4D; // dotted quarter dub feel
        s.delay.mix = 0.45f; s.delay.feedback = 0.8f;    // high but self-limited by tape
        s.delay.tapeSaturation = 0.7f; s.delay.tapeWear = 0.5f;
        s.delay.tapeAge = 0.6f; s.delay.tapeInertiaMs = 260.0f;
        s.delay.tapeSpliceEnabled = 1; s.delay.tapeSpliceIntensity = 0.4f; // owns splice
        // Mod identity: no delay-feedback dest exists, so swell the WET level instead
        s.lfo1.rateHz = 0.1f; s.lfo1.shape = 0; s.lfo1.depth = 0.85f; s.lfo1.sync = 0;
        setModSlot(s, 0, 1, 3, 0.5f, kCurveSCurve, 20.0f); // LFO1 -> EffectMix, smoothstep swell
        setModSlot(s, 1, 1, 4, 0.25f, kCurveExp);          // LFO1 -> AllFltCut, source drifts too
        // Small PLATE, not the shared big hall
        s.reverbEnabled = 1; s.reverbType = 0;  // Plate
        s.reverb.size = 0.4f; s.reverb.mix = 0.18f; s.reverb.damping = 0.55f;
        presets.push_back(std::move(p));
    }

    // "Tidal Breath" - Dual-noise double auto-wah drifting through a granular cloud
    {
        PresetDef p;
        p.name = "Tidal Breath";
        p.category = "Experimental";
        auto& s = p.state;
        s.oscA.type = 9; s.oscA.noiseColor = 1; s.oscA.level = 0.65f; // Pink body
        s.oscB.type = 9; s.oscB.noiseColor = 4; s.oscB.level = 0.35f; // Violet air
        s.mixer.position = 0.35f;
        // Internal bandpass envelope filter (auto-wah #1)
        s.filter.type = 11;                // Envelope Filter
        // Bandpass: Q narrows the band rather than adding a resonant boost, so
        // it stays moderate here -- a high Q starves a noise source of level.
        s.filter.cutoffHz = 1400.0f; s.filter.resonance = 4.0f;
        s.filter.envSubType = 1;           // BP
        s.filter.envSensitivity = 12.0f; s.filter.envDepth = 0.9f;
        s.filter.envAttack = 4.0f; s.filter.envRelease = 120.0f;
        s.filter.envDirection = 0;         // sweep up
        // Slow surf swell
        s.ampEnv.attackMs = 40.0f; s.ampEnv.decayMs = 600.0f;
        s.ampEnv.sustain = 0.55f; s.ampEnv.releaseMs = 700.0f;
        // Enable the EnvFollower MODULE (auto-wah #2, routed below)
        s.envFollower.sensitivity = 0.7f; s.envFollower.attackMs = 8.0f; s.envFollower.releaseMs = 180.0f;
        // Dual free LFOs make the wah gesture never repeat (envDepth has no dest, so vary res+cut)
        s.lfo1.rateHz = 0.2f; s.lfo1.shape = 0; s.lfo1.depth = 0.7f; s.lfo1.sync = 0; // sine
        s.lfo2.rateHz = 0.5f; s.lfo2.shape = 1; s.lfo2.depth = 0.6f; s.lfo2.sync = 0; // triangle
        setModSlot(s, 0, 1, 8, 0.4f, kCurveSCurve);       // LFO1 -> AllResonance (wah bite varies)
        setModSlot(s, 1, 2, 4, 0.3f, kCurveExp);          // LFO2 -> AllFltCut (center drifts)
        setModSlot(s, 2, 3, 4, 0.5f, kCurveLinear, 30.0f);// EnvFollower -> AllFltCut, smoothed breathing
        // Granular delay cloud instead of the shared 350 ms slap
        s.delayEnabled = 1;
        s.delay.type = 3;                  // Granular
        s.delay.sync = 0; s.delay.timeMs = 300.0f;
        s.delay.mix = 0.25f; s.delay.feedback = 0.35f;
        s.delay.granularSizeMs = 80.0f; s.delay.granularDensity = 20.0f;
        s.delay.granularPitchSpray = 0.15f; s.delay.granularPanSpray = 0.6f;
        s.delay.granularJitter = 0.2f;
        // Airy HALL
        s.reverbEnabled = 1; s.reverbType = 1;  // Hall
        s.reverb.size = 0.7f; s.reverb.mix = 0.32f; s.reverb.damping = 0.5f;
        presets.push_back(std::move(p));
    }

    // "Glass Automaton" - PD morphed against reverse-sync, ring-modded, stepped-random evolution
    {
        PresetDef p;
        p.name = "Glass Automaton";
        p.category = "Experimental";
        auto& s = p.state;
        s.oscA.type = 2;                   // Phase Distortion
        s.oscA.pdWaveform = 5;             // ResSaw (formant-resonant CZ shape)
        s.oscA.pdDistortion = 0.7f; s.oscA.level = 0.7f;
        s.oscB.type = 3;                   // Sync slave
        s.oscB.syncRatio = 2.5f; s.oscB.syncWaveform = 2; // square = hollow sync
        s.oscB.syncMode = 1;              // Reverse (softer, unusual)
        s.oscB.syncAmount = 0.75f; s.oscB.level = 0.6f;
        s.mixer.mode = 1;                 // Spectral Morph
        s.mixer.position = 0.5f; s.mixer.tilt = 2.0f; s.mixer.shift = 0.1f;
        // SVF Peak = resonant formant bump (a must-cover filter)
        s.filter.type = 8;                // SVF Peak
        s.filter.cutoffHz = 2200.0f; s.filter.resonance = 2.0f; s.filter.svfGain = 9.0f;
        s.filter.envAmount = 18.0f;       // pluck sweep on the peak
        s.filterEnv.attackMs = 6.0f; s.filterEnv.decayMs = 300.0f;
        s.filterEnv.sustain = 0.3f; s.filterEnv.releaseMs = 260.0f;
        s.ampEnv.attackMs = 8.0f; s.ampEnv.decayMs = 400.0f;
        s.ampEnv.sustain = 0.6f; s.ampEnv.releaseMs = 350.0f;
        // Ring modulator for metallic sheen (owns this dirty type)
        s.distortion.type = 6;            // RingModulator
        s.distortion.drive = 0.3f; s.distortion.mix = 0.3f;
        s.distortion.ringFreqMode = 1;    // NoteTrack (musical)
        s.distortion.ringRatio = 0.17f;   // normalized -> ratio ~3.0 (metallic)
        s.distortion.ringWaveform = 0;    // Sine
        // Evolving mod web (multi-slot, not the single-LFO skeleton)
        s.lfo1.rateHz = 0.25f; s.lfo1.shape = 0; s.lfo1.depth = 0.6f; s.lfo1.sync = 0; // slow sine
        s.lfo2.rateHz = 6.0f;  s.lfo2.shape = 3; s.lfo2.depth = 0.3f; s.lfo2.sync = 0; // fast square
        s.random.rateHz = 1.5f; s.random.smoothness = 0.0f; // hard steps
        setModSlot(s, 0, 1, 5, 0.5f, kCurveSCurve);  // LFO1 -> AllMorphPos (smooth glide)
        setModSlot(s, 1, 4, 7, 0.4f, kCurveStepped); // Random -> AllSpecTilt (stepped 'shift' glitch)
        setModSlot(s, 2, 2, 4, 0.2f, kCurveLinear);  // LFO2 -> AllFltCut (robotic chatter)
        // Digital bounce
        s.delayEnabled = 1;
        s.delay.type = 2;                 // PingPong
        s.delay.sync = 1; s.delay.noteValue = kNote1_8;
        s.delay.mix = 0.28f; s.delay.feedback = 0.4f;
        s.delay.pingPongRatio = 2; s.delay.pingPongWidth = 140.0f;
        // Small plate + wide pitch bend for whammy performance
        s.reverbEnabled = 1; s.reverbType = 0; // Plate
        s.reverb.size = 0.45f; s.reverb.mix = 0.22f;
        s.settings.pitchBendRangeSemitones = 12.0f;
        presets.push_back(std::move(p));
    }

    // "Living Harmonics" - Inharmonic additive choir destabilized by Lorenz chaos + a macro
    {
        PresetDef p;
        p.name = "Living Harmonics";
        p.category = "Experimental";
        auto& s = p.state;
        s.oscA.type = 4;                   // Additive
        s.oscA.additivePartials = 24;
        s.oscA.additiveTilt = -2.0f;       // slightly dark (was bright +1)
        s.oscA.additiveInharm = 0.12f;     // bell/metallic shimmer (never-touched param)
        s.oscA.level = 0.7f;
        s.oscB.type = 0; s.oscB.waveform = 1; s.oscB.tuneSemitones = -12.0f;
        s.oscB.fineCents = -6.0f; s.oscB.level = 0.3f; // sub weight
        s.mixer.position = 0.32f;
        // Filter that actually moves (fixes the inert 6k LP)
        s.filter.type = 0;                 // SVF LP
        s.filter.cutoffHz = 3500.0f; s.filter.resonance = 5.22f; s.filter.keyTrack = 0.4f;
        s.filter.envAmount = 20.0f;        // +20 st opening sweep
        s.filterEnv.attackMs = 80.0f; s.filterEnv.decayMs = 900.0f;
        s.filterEnv.sustain = 0.5f; s.filterEnv.releaseMs = 1000.0f;
        // Orchestral swell
        s.ampEnv.attackMs = 120.0f; s.ampEnv.decayMs = 700.0f;
        s.ampEnv.sustain = 0.75f; s.ampEnv.releaseMs = 1200.0f;
        // Full 4-voice Lydian PhaseVocoder choir
        s.harmonizerEnabled = 1;
        s.harmonizer.harmonyMode = 1;      // Scalic
        s.harmonizer.key = 2;              // D
        s.harmonizer.scale = 7;            // Lydian (ethereal)
        s.harmonizer.pitchShiftMode = 2;   // PhaseVocoder
        s.harmonizer.formantPreserve = 1;
        s.harmonizer.numVoices = 4;
        s.harmonizer.dryLevelDb = 0.0f; s.harmonizer.wetLevelDb = -3.0f;
        s.harmonizer.voiceInterval[0] = 2; s.harmonizer.voicePan[0] = -0.5f; s.harmonizer.voiceDetuneCents[0] = 6.0f;
        s.harmonizer.voiceInterval[1] = 4; s.harmonizer.voicePan[1] = 0.5f;  s.harmonizer.voiceDetuneCents[1] = -6.0f;
        s.harmonizer.voiceInterval[2] = 6; s.harmonizer.voicePan[2] = -0.25f; s.harmonizer.voiceDetuneCents[2] = 10.0f;
        s.harmonizer.voiceInterval[3] = -3; s.harmonizer.voicePan[3] = 0.25f; s.harmonizer.voiceDelayMs[3] = 20.0f; s.harmonizer.voiceDetuneCents[3] = -8.0f;
        // Lorenz chaos as the destabilizer
        s.chaosMod.rateHz = 0.7f; s.chaosMod.type = 0; s.chaosMod.depth = 0.5f; // Lorenz
        s.lfo1.rateHz = 0.12f; s.lfo1.shape = 0; s.lfo1.depth = 0.5f; s.lfo1.sync = 0;
        s.macros.values[0] = 0.3f;         // 'instability' knob start value
        setModSlot(s, 0, 9, 7, 0.35f, kCurveSCurve); // Chaos -> AllSpecTilt
        setModSlot(s, 1, 9, 5, 0.3f,  kCurveExp);    // Chaos -> AllMorphPos
        s.modMatrix.slots[1].scale = 3;              // SCALE AXIS x2: push chaos past +-1 (unused dim)
        setModSlot(s, 2, 1, 4, 0.3f,  kCurveLinear); // LFO1 -> AllFltCut (slow breathing)
        setModSlot(s, 3, 5, 9, 0.4f,  kCurveExp);    // Macro1 -> AllFltEnvAmt (one-knob instability)
        // Spectral delay cloud into a modulated hall
        s.delayEnabled = 1;
        s.delay.type = 4;                  // Spectral
        s.delay.sync = 0; s.delay.timeMs = 500.0f; s.delay.mix = 0.18f; s.delay.feedback = 0.3f;
        s.delay.spectralDiffusion = 0.4f; s.delay.spectralTilt = 0.2f; s.delay.spectralSpreadMs = 300.0f;
        s.reverbEnabled = 1; s.reverbType = 1; // Hall
        s.reverb.size = 0.85f; s.reverb.mix = 0.35f;
        s.reverb.modRateHz = 0.3f; s.reverb.modDepth = 0.2f; // shimmering reverb mod (unused)
        presets.push_back(std::move(p));
    }

    // "Sinew" - Velocity-alive key-tracked comb-string with pluck-to-pluck variation
    {
        PresetDef p;
        p.name = "Sinew";
        p.category = "Experimental";
        auto& s = p.state;
        s.oscA.type = 9; s.oscA.noiseColor = 0; s.oscA.level = 0.5f;  // White exciter burst
        s.oscB.type = 0; s.oscB.waveform = 3; s.oscB.pulseWidth = 0.15f; // thin pulse
        s.oscB.level = 0.35f;
        s.mixer.position = 0.4f;
        // Key-tracked comb resonator (the string body)
        s.filter.type = 6;                 // Comb
        s.filter.cutoffHz = 220.0f; s.filter.resonance = 10.30f;
        s.filter.combDamping = 0.2f; s.filter.keyTrack = 1.0f;
        s.filter.envAmount = 15.0f;        // +15 st brightness 'ping' at attack
        s.filterEnv.attackMs = 0.0f; s.filterEnv.decayMs = 400.0f;
        s.filterEnv.sustain = 0.0f; s.filterEnv.releaseMs = 300.0f;
        // Long ringing pluck
        s.ampEnv.attackMs = 1.0f; s.ampEnv.decayMs = 2500.0f;
        s.ampEnv.sustain = 0.1f; s.ampEnv.releaseMs = 1800.0f;
        // Expressive velocity (owns a settings axis)
        s.settings.velocityCurve = 2;      // Hard
        // Per-voice velocity dynamics (voice-dest table: 0=FltCut,1=FltRes; src 5=Velocity,1=Env2)
        setVoiceRoute(s, 0, 5, 0, 0.5f);   // Velocity -> FltCut (harder = brighter)
        setVoiceRoute(s, 1, 5, 1, 0.3f);   // Velocity -> FltRes (harder = longer ring)
        setVoiceRoute(s, 2, 1, 0, 0.3f);   // Env2(filterEnv) -> FltCut (attack brightness)
        // Global motion so no two plucks ring alike (combDamping has no dest -> vary cut+res)
        s.lfo1.rateHz = 0.3f; s.lfo1.shape = 1; s.lfo1.depth = 0.4f; s.lfo1.sync = 0; // slow triangle
        s.random.rateHz = 2.0f; s.random.smoothness = 0.1f;
        setModSlot(s, 0, 1, 4, 0.15f, kCurveSCurve); // LFO1 -> AllFltCut (subtle comb shimmer)
        setModSlot(s, 1, 4, 8, 0.25f, kCurveLinear); // Random -> AllResonance (pluck-to-pluck ring)
        // Tight PLATE with pre-delay, NOT the shared 0.6/0.3 hall tail
        s.reverbEnabled = 1; s.reverbType = 0; // Plate
        s.reverb.size = 0.45f; s.reverb.mix = 0.2f; s.reverb.damping = 0.6f; s.reverb.preDelayMs = 20.0f;
        presets.push_back(std::move(p));
    }

    // "Frozen Spectral" - Glacial dual-spectral drone morphed through a resonant comb
    {
        PresetDef p;
        p.name = "Frozen Spectral";
        p.category = "Experimental";
        auto& s = p.state;
        // Two SpectralFreeze engines detuned in pitch/formant, blended in the
        // FFT-domain SpectralMorph mixer (mode 1). tilt/shift only act in this
        // mode, so they are part of this preset's spectral identity.
        s.oscA.type = 8; // Spectral Freeze
        s.oscA.spectralPitch = 0.0f; s.oscA.spectralTilt = -2.0f;
        s.oscA.spectralFormant = 4.0f; s.oscA.level = 0.7f;
        s.oscB.type = 8; // Spectral Freeze
        s.oscB.spectralPitch = -7.0f;  // a fifth below = hollow interval
        s.oscB.spectralTilt = 3.0f; s.oscB.spectralFormant = -3.0f;
        s.oscB.level = 0.55f;
        s.mixer.mode = 1;              // Spectral Morph (FFT interpolation A<->B)
        s.mixer.position = 0.5f;
        s.mixer.tilt = -3.0f;          // darken the morphed spectrum (SpectralMorph only)
        s.mixer.shift = 40.0f;         // small inharmonic freq shift for shimmer
        // Comb filter: tuned resonant teeth turn the drone metallic/glassy.
        s.filter.type = 6;             // Comb
        s.filter.cutoffHz = 320.0f;    // comb fundamental
        s.filter.resonance = 7.48f;
        s.filter.combDamping = 0.45f;  // soften the high feedback teeth
        // Very slow swell; long release so the frozen tail rings on.
        s.ampEnv.attackMs = 1200.0f; s.ampEnv.decayMs = 800.0f;
        s.ampEnv.sustain = 0.9f; s.ampEnv.releaseMs = 4000.0f;
        s.ampEnv.attackCurve = 0.5f;   // slow-start exponential swell
        // ModEnv shapes spectral tilt per note (routed via voice route below).
        s.modEnv.attackMs = 2500.0f; s.modEnv.decayMs = 3000.0f;
        s.modEnv.sustain = 0.6f; s.modEnv.releaseMs = 4000.0f;
        // LFO1: glacial 0.05 Hz sine -> morph position (SCurve for smooth turns).
        s.lfo1.rateHz = 0.05f; s.lfo1.shape = 0; s.lfo1.depth = 0.6f; s.lfo1.sync = 0;
        // LFO2: 0.03 Hz triangle -> spectral tilt for a slow timbral tide.
        s.lfo2.rateHz = 0.03f; s.lfo2.shape = 1; s.lfo2.depth = 0.5f; s.lfo2.sync = 0;
        // Sample & Hold gives stepped comb-pitch jumps, glide-smoothed to stay glacial.
        s.sampleHold.rateHz = 0.08f; s.sampleHold.sync = 0; s.sampleHold.slewMs = 400.0f;
        // --- Mod web ---
        setModSlot(s, 0, kSrcLFO1, kDstAllMorphPos, 0.5f, kCurveSCurve);
        setModSlot(s, 1, kSrcLFO2, kDstAllSpecTilt, 0.4f, kCurveLinear);
        setModSlot(s, 2, kSrcSampleHold, kDstAllFltCut, 0.35f, kCurveStepped, 300.0f);
        // Slot 7: a parked Random->morph route left BYPASSED to demonstrate the
        // bypass axis (flip bypass to 0 live for a busier second morph layer).
        setModSlot(s, 7, kSrcRandom, kDstAllMorphPos, 0.4f, kCurveLinear);
        s.modMatrix.slots[7].bypass = 1;
        // Per-note spectral-tilt evolution from ModEnv (Env3).
        setVoiceRoute(s, 0, kVSrcEnv3, kVDstSpecTilt, 0.5f);
        // Frozen Hall reverb: the tail literally freezes and holds.
        s.reverbEnabled = 1;
        s.reverbType = 1;              // Hall (bigger/darker than Plate)
        s.reverb.size = 0.95f; s.reverb.mix = 0.5f;
        s.reverb.damping = 0.15f; s.reverb.freeze = 1;
        presets.push_back(std::move(p));
    }

    // "Particle Storm" - Roiling dual-granular swarm; one macro is the storm dial
    {
        PresetDef p;
        p.name = "Particle Storm";
        p.category = "Experimental";
        auto& s = p.state;
        // Osc A: dense, short-lived, wide-scatter grains, Burst spawn = gusts.
        s.oscA.type = 6; // Particle
        s.oscA.particleScatter = 11.0f; s.oscA.particleDensity = 64.0f;
        s.oscA.particleLifetime = 40.0f; s.oscA.particleSpawnMode = 2; // Burst
        s.oscA.particleEnvType = 3; // Blackman (smooth grains)
        s.oscA.particleDrift = 0.85f; s.oscA.level = 0.7f;
        // Osc B: sparse, long-lived, tight grains, Random spawn = a calmer bed.
        s.oscB.type = 6; // Particle
        s.oscB.particleScatter = 2.0f; s.oscB.particleDensity = 8.0f;
        s.oscB.particleLifetime = 700.0f; s.oscB.particleSpawnMode = 1; // Random
        s.oscB.particleEnvType = 0; // Hann
        s.oscB.particleDrift = 0.4f; s.oscB.level = 0.55f;
        s.mixer.position = 0.5f;       // morph = perceived density (A dense <-> B sparse)
        // Envelope-following auto-wah: the swarm's own amplitude opens the filter.
        s.filter.type = 11;            // Env Filter (auto-wah)
        s.filter.cutoffHz = 700.0f; s.filter.resonance = 2.5f;
        s.filter.envSubType = 1;       // BP response
        s.filter.envDepth = 0.9f; s.filter.envSensitivity = 6.0f;
        s.filter.envAttack = 20.0f; s.filter.envRelease = 250.0f;
        s.filter.envDirection = 0;     // sweep up on transients
        s.ampEnv.attackMs = 150.0f; s.ampEnv.decayMs = 600.0f;
        s.ampEnv.sustain = 0.8f; s.ampEnv.releaseMs = 1800.0f;
        // LFO1 SmoothRandom drives spectral tilt (scaled x2) - unpredictable weather.
        s.lfo1.rateHz = 0.25f; s.lfo1.shape = 5; s.lfo1.depth = 0.7f; s.lfo1.sync = 0;
        // LFO2 slow triangle nudges filter cutoff underneath.
        s.lfo2.rateHz = 0.12f; s.lfo2.shape = 1; s.lfo2.depth = 0.5f; s.lfo2.sync = 0;
        // Random + EnvFollower + Macro sources feed the storm.
        s.random.rateHz = 3.0f; s.random.sync = 0; s.random.smoothness = 0.7f;
        s.envFollower.sensitivity = 0.7f; s.envFollower.attackMs = 15.0f;
        s.envFollower.releaseMs = 200.0f;
        s.macros.values[0] = 0.5f;     // "Storm" macro parked at half
        // --- Mod web: Macro1 is a one-knob storm-intensity morph (multi-routed) ---
        setModSlot(s, 0, kSrcMacro1, kDstAllMorphPos, 0.7f, kCurveSCurve);
        setModSlot(s, 1, kSrcMacro1, kDstAllFltCut, 0.6f, kCurveExp);
        setModSlot(s, 2, kSrcMacro1, kDstEffectMix, 0.5f, kCurveLinear);
        // Weather modulation on top of the macro:
        setModSlot(s, 3, kSrcLFO1, kDstAllSpecTilt, 0.5f, kCurveLinear);
        s.modMatrix.slots[3].scale = 3;    // x2 - push tilt beyond +/-1
        setModSlot(s, 4, kSrcRandom, kDstAllResonance, 0.35f, kCurveLinear, 120.0f);
        setModSlot(s, 5, kSrcEnvFollower, kDstAllMorphPos, 0.3f, kCurveLinear);
        setModSlot(s, 6, kSrcLFO2, kDstAllFltCut, 0.25f, kCurveLinear);
        // Own the voice-management settings axes for a chaotic dense swarm.
        s.global.voiceMode = 0;        // Poly
        s.global.polyphony = 16;       // maximum voices for a thick cloud
        s.global.width = 2.0f; s.global.spread = 0.7f;
        s.settings.voiceAllocMode = 0; // Round-Robin (spreads grains across voices)
        s.settings.voiceStealMode = 1; // Soft steal (no clicks when the cloud saturates)
        // Granular delay thickens the cloud further.
        s.delayEnabled = 1;
        s.delay.type = 3;              // Granular
        s.delay.mix = 0.35f; s.delay.feedback = 0.5f; s.delay.timeMs = 220.0f;
        s.delay.granularSizeMs = 120.0f; s.delay.granularDensity = 20.0f;
        s.delay.granularPitchSpray = 0.3f; s.delay.granularPanSpray = 0.6f;
        s.delay.granularTexture = 0.4f;
        // Diffuse plate wash on top.
        s.reverbEnabled = 1;
        s.reverbType = 0;              // Plate
        s.reverb.size = 0.85f; s.reverb.mix = 0.4f; s.reverb.diffusion = 0.9f;
        presets.push_back(std::move(p));
    }

    // "Double Fold" - Hard-sync metal folded and pushed through a talking formant filter
    {
        PresetDef p;
        p.name = "Double Fold";
        p.category = "Experimental";
        auto& s = p.state;
        // Osc A: hard-sync, aggressive high slave ratio = screaming sync sweep.
        s.oscA.type = 3; // Sync
        s.oscA.syncRatio = 4.5f; s.oscA.syncWaveform = 1; // saw slave
        s.oscA.syncMode = 0; s.oscA.syncAmount = 1.0f; s.oscA.level = 0.8f;
        // Osc B: bare triangle under it for a fold-able fundamental.
        s.oscB.type = 0; s.oscB.waveform = 4; // Triangle
        s.oscB.level = 0.45f;
        s.mixer.position = 0.35f;      // favour the sync osc
        // Wavefolder (Lockhart) adds the metallic upper harmonics.
        s.distortion.type = 4;         // Wavefolder
        s.distortion.drive = 0.6f; s.distortion.foldType = 2; // Lockhart
        s.distortion.mix = 0.85f;
        // Formant filter makes the folded metal "speak" a vowel; gender + morph
        // give it a distinct throat.
        s.filter.type = 5;             // Formant
        s.filter.cutoffHz = 1200.0f; s.filter.resonance = 9.74f;
        s.filter.formantMorph = 1.6f;  // between E and I
        s.filter.formantGender = -0.5f; // deeper/darker throat
        s.filter.envAmount = 18.0f;    // filter env sweeps the vowel (semitones, bug-fixed)
        s.filterEnv.attackMs = 4.0f; s.filterEnv.decayMs = 350.0f;
        s.filterEnv.sustain = 0.3f; s.filterEnv.releaseMs = 300.0f;
        s.ampEnv.attackMs = 3.0f; s.ampEnv.decayMs = 450.0f;
        s.ampEnv.sustain = 0.6f; s.ampEnv.releaseMs = 350.0f;
        // LFO1 saw ramps the fold; LFO2 square jumps resonance for gated bite.
        s.lfo1.rateHz = 0.4f; s.lfo1.shape = 2; s.lfo1.depth = 0.6f; s.lfo1.sync = 0;
        s.lfo2.rateHz = 1.0f; s.lfo2.shape = 3; s.lfo2.depth = 0.4f; s.lfo2.sync = 0;
        // Rungler (Benjolin shift-register) is the chaos engine: stepped,
        // pitched-noise modulation of the vowel morph. Depth MUST be raised.
        s.rungler.osc1FreqHz = 3.5f; s.rungler.osc2FreqHz = 5.5f;
        s.rungler.depth = 0.7f; s.rungler.filter = 0.4f;
        s.rungler.bits = 6; s.rungler.loopMode = 0; // free chaos
        // --- Mod web ---
        setModSlot(s, 0, kSrcRungler, kDstAllMorphPos, 0.5f, kCurveExp);
        // Scaled (x2) LFO2 -> resonance: a distinct destination from Frozen Spectral.
        setModSlot(s, 1, kSrcLFO2, kDstAllResonance, 0.45f, kCurveLinear);
        s.modMatrix.slots[1].scale = 3; // x2
        setModSlot(s, 2, kSrcLFO1, kDstAllFltCut, 0.4f, kCurveExp);
        // Velocity drives the fold harder per hit for dynamics.
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstDistDrive, 0.5f);
        // Short Digital delay (with a touch of wavefold) = metallic slap.
        s.delayEnabled = 1;
        s.delay.type = 0;              // Digital
        s.delay.mix = 0.2f; s.delay.feedback = 0.3f; s.delay.timeMs = 280.0f;
        s.delay.digitalWavefoldAmt = 0.3f; s.delay.digitalWidth = 140.0f;
        presets.push_back(std::move(p));
    }

    // "Inharmonic Bells" - Clangorous additive bells, harmonized and pitch-aware
    {
        PresetDef p;
        p.name = "Inharmonic Bells";
        p.category = "Experimental";
        auto& s = p.state;
        // Osc A: 128 partials, high inharmonicity = struck-metal clang.
        s.oscA.type = 4; // Additive
        s.oscA.additivePartials = 128; s.oscA.additiveInharm = 0.85f;
        s.oscA.additiveTilt = -4.5f; s.oscA.level = 0.65f;
        // Osc B: fewer partials, tuned a fifth up, milder inharm = shimmer layer.
        s.oscB.type = 4;
        s.oscB.additivePartials = 48; s.oscB.additiveInharm = 0.45f;
        s.oscB.additiveTilt = -2.0f; s.oscB.tuneSemitones = 7.0f; s.oscB.level = 0.4f;
        s.mixer.position = 0.4f;
        // SVF High-Pass thins the low end so only the metallic ring survives;
        // keyTrack makes the HP corner follow pitch so low notes stay clear.
        s.filter.type = 1;             // SVF HP
        s.filter.cutoffHz = 300.0f; s.filter.resonance = 5.22f;
        s.filter.keyTrack = 1.0f; s.filter.svfSlope = 1; // 24 dB
        // Near-percussive strike: instant attack, long decay/release ring.
        s.ampEnv.attackMs = 1.0f; s.ampEnv.decayMs = 3500.0f;
        s.ampEnv.sustain = 0.08f; s.ampEnv.releaseMs = 3000.0f;
        s.ampEnv.decayCurve = 0.5f;    // natural exponential decay
        // Transient detector -> master volume sharpens each strike's attack.
        s.transient.sensitivity = 0.7f; s.transient.attackMs = 1.5f;
        s.transient.decayMs = 60.0f;
        // Pitch follower tracks the played bell and brightens higher strikes.
        s.pitchFollower.minHz = 80.0f; s.pitchFollower.maxHz = 3000.0f;
        s.pitchFollower.confidence = 0.5f; s.pitchFollower.speedMs = 40.0f;
        // --- Mod web ---
        setModSlot(s, 0, kSrcTransient, kDstMasterVol, 0.5f, kCurveExp);
        setModSlot(s, 1, kSrcPitchFollow, kDstAllFltCut, 0.4f, kCurveLinear);
        // Aftertouch adds spectral-tilt shimmer for expressive rings.
        setVoiceRoute(s, 0, kVSrcAftertouch, kVDstSpecTilt, 0.4f);
        // Wide pitch bend for dramatic bell dive-bombs.
        s.settings.pitchBendRangeSemitones = 12.0f;
        // Scalic PhaseVocoder harmonizer stacks diatonic bell overtones.
        s.harmonizerEnabled = 1;
        s.harmonizer.harmonyMode = 1;    // Scalic
        s.harmonizer.pitchShiftMode = 2; // PhaseVocoder
        s.harmonizer.formantPreserve = 1;
        s.harmonizer.numVoices = 3;
        s.harmonizer.key = 0; s.harmonizer.scale = 0;
        s.harmonizer.dryLevelDb = 0.0f; s.harmonizer.wetLevelDb = -9.0f;
        s.harmonizer.voiceInterval[0] = 2; s.harmonizer.voicePan[0] = -0.5f;
        s.harmonizer.voiceDetuneCents[0] = 4.0f;
        s.harmonizer.voiceInterval[1] = 4; s.harmonizer.voicePan[1] = 0.5f;
        s.harmonizer.voiceDetuneCents[1] = -4.0f;
        s.harmonizer.voiceInterval[2] = 7; s.harmonizer.voicePan[2] = 0.0f;
        s.harmonizer.voiceDelayMs[2] = 25.0f;
        // Plate reverb for a bright metallic tail.
        s.reverbEnabled = 1;
        s.reverbType = 0;              // Plate
        s.reverb.size = 0.7f; s.reverb.mix = 0.4f; s.reverb.damping = 0.2f;
        presets.push_back(std::move(p));
    }

    // "FM Grunge" - Phase-distortion + noise mangled by a chaos waveshaper onto tape
    {
        PresetDef p;
        p.name = "FM Grunge";
        p.category = "Experimental";
        auto& s = p.state;
        // Osc A: phase distortion, half-sine w/ heavy DCW = hollow FM-ish growl.
        s.oscA.type = 2; // Phase Distortion
        s.oscA.pdWaveform = 4; // HalfSine
        s.oscA.pdDistortion = 0.75f; s.oscA.level = 0.7f;
        // Osc B: white noise seasons the top end.
        s.oscB.type = 9; // Noise
        s.oscB.noiseColor = 0; s.oscB.level = 0.18f;
        s.mixer.position = 0.18f;
        // Driven 4-pole ladder: real analog dirt before the waveshaper.
        s.filter.type = 4;             // Ladder
        s.filter.cutoffHz = 1400.0f; s.filter.resonance = 6.35f;
        s.filter.ladderSlope = 4; s.filter.ladderDrive = 8.0f;
        s.filter.envAmount = 30.0f;    // strong acid-style sweep (semitones, bug-fixed)
        s.filter.keyTrack = 0.3f;
        s.filterEnv.attackMs = 2.0f; s.filterEnv.decayMs = 250.0f;
        s.filterEnv.sustain = 0.2f; s.filterEnv.releaseMs = 200.0f;
        // Chaos Waveshaper (Henon map) = the signature unstable grunge.
        s.distortion.type = 1;         // Chaos Waveshaper
        s.distortion.drive = 0.4f;
        s.distortion.chaosModel = 3;   // Henon
        s.distortion.chaosSpeed = 0.6f; s.distortion.chaosCoupling = 0.35f;
        s.distortion.mix = 0.8f;
        s.ampEnv.attackMs = 5.0f; s.ampEnv.decayMs = 400.0f;
        s.ampEnv.sustain = 0.55f; s.ampEnv.releaseMs = 300.0f;
        // ChaosMod (Rossler) wobbles the ladder cutoff unpredictably.
        s.chaosMod.rateHz = 0.6f; s.chaosMod.type = 1; // Rossler
        s.chaosMod.depth = 0.6f; s.chaosMod.sync = 0;
        // LFO1 square gates a bit of morph flutter.
        s.lfo1.rateHz = 0.5f; s.lfo1.shape = 3; s.lfo1.depth = 0.3f; s.lfo1.sync = 0;
        // --- Mod web ---
        setModSlot(s, 0, kSrcChaos, kDstAllFltCut, 0.5f, kCurveLinear);
        setModSlot(s, 1, kSrcLFO1, kDstAllMorphPos, 0.3f, kCurveLinear);
        // Aftertouch presses more drive into the waveshaper (per-voice).
        setVoiceRoute(s, 0, kVSrcAftertouch, kVDstDistDrive, 0.6f);
        setVoiceRoute(s, 1, kVSrcVelocity, kVDstFltCut, 0.4f);
        // Soft velocity curve so light playing stays gritty but controllable.
        s.settings.velocityCurve = 1;  // Soft
        // Tape delay with splice artefacts = degraded, warbling echoes.
        s.delayEnabled = 1;
        s.delay.type = 1;              // Tape
        s.delay.mix = 0.25f; s.delay.feedback = 0.35f; s.delay.timeMs = 260.0f;
        s.delay.tapeSaturation = 0.6f; s.delay.tapeWear = 0.4f;
        s.delay.tapeSpliceEnabled = 1; s.delay.tapeSpliceIntensity = 0.5f;
        presets.push_back(std::move(p));
    }

    // ==================== ARP MODULATION Category ====================
    // Presets showcasing the Arp Pitch as a global modulation source

    // "Arp Filter Sweep" - every arp step re-opens the ladder; a mono acid runner
    // whose cutoff literally TRACKS melodic pitch (higher note = brighter).
    {
        PresetDef p;
        p.name = "Arp Filter Sweep";
        p.category = "Arp Modulation";
        auto& s = p.state;
        // --- Voice: PolyBLEP saw over a WAVETABLE square (gives the category a
        //     Wavetable home; the thin square adds a hollow reed edge under the saw) ---
        s.oscA.type = 0; s.oscA.waveform = 1;          // PolyBLEP Saw
        s.oscA.level = 0.85f;
        s.oscB.type = 1; s.oscB.waveform = 2;          // Wavetable Square
        s.oscB.pulseWidth = 0.35f;                     // thin/nasal square for bite
        s.oscB.phaseMod = 0.2f;                        // tiny PM grit on the table
        s.oscB.fineCents = 7.0f; s.oscB.level = 0.55f; // detune beat vs OSC A
        s.mixer.position = 0.42f;                      // favour the saw
        // --- Ladder LP, 24 dB, driven: the classic acid growl ---
        s.filter.type = 4;                             // Ladder
        s.filter.cutoffHz = 900.0f;                    // low resting cutoff to sweep UP from
        s.filter.resonance = 6.35f;                     // squelch, not self-osc
        s.filter.ladderSlope = 4;                      // 24 dB/oct
        s.filter.ladderDrive = 4.0f;                   // growl (tamed by moderate res + soft limit)
        s.filter.keyTrack = 0.3f;                      // filter follows the keyboard a touch
        s.filter.envAmount = 28.0f;                    // +28 SEMITONES of filter-env stab (audible!)
        // Punchy pluck envelopes
        s.ampEnv.attackMs = 2.0f;  s.ampEnv.decayMs = 180.0f;
        s.ampEnv.sustain = 0.55f;  s.ampEnv.releaseMs = 140.0f;
        s.ampEnv.decayCurve = 0.4f;                    // exp-ish decay = extra punch
        s.filterEnv.attackMs = 1.0f; s.filterEnv.decayMs = 220.0f;
        s.filterEnv.sustain = 0.08f; s.filterEnv.releaseMs = 120.0f;
        // --- Arp: MIDI+Mod so it both PLAYS and feeds the mod system ---
        s.arp.operatingMode = 3;                       // MIDI+Mod (covers operatingMode=3)
        s.arp.mode = 0;                                // Up
        s.arp.octaveRange = 2;
        s.arp.tempoSync = 1; s.arp.noteValue = kNote1_16;
        s.arp.gateLength = 68.0f;
        int32_t pitch8[] = {0, 2, 4, 7, 12, 7, 4, 2};  // rising/falling contour drives the sweep
        setPitchLane(s, 8, pitch8);
        // Slide + accent bits on the peak steps for a 303 glide feel
        // (0x01 active, 0x09 active+accent, 0x0D active+slide+accent)
        int32_t modF[] = {0x01, 0x01, 0x09, 0x01, 0x0D, 0x01, 0x09, 0x01};
        setModifierLane(s, 8, modF, 45, 55.0f);
        // --- THE gesture: arp pitch -> ladder cutoff (Exp so highs really pop) ---
        setModSlot(s, 0, kSrcArpPitch, kDstAllFltCut, 0.75f, kCurveExp);
        // Self-mod: higher notes nudge the arp a hair faster (covers ArpPitch->ArpRate)
        setModSlot(s, 1, kSrcArpPitch, kDstArpRate, 0.15f, kCurveLinear);
        // Free LFO drizzles morph movement so sustained steps still breathe
        s.lfo1.rateHz = 0.22f; s.lfo1.shape = 1; s.lfo1.depth = 0.4f; s.lfo1.sync = 0;
        setModSlot(s, 2, kSrcLFO1, kDstAllMorphPos, 0.2f);
        // Expression: velocity opens the filter, aftertouch adds squelch resonance
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.4f);
        setVoiceRoute(s, 1, kVSrcAftertouch, kVDstFltRes, 0.35f);
        // --- FX: dotted ping-pong tail, deliberately NO reverb (stays dry/rhythmic) ---
        s.delayEnabled = 1;
        s.delay.type = 2;                              // PingPong
        s.delay.mix = 0.22f; s.delay.feedback = 0.33f;
        s.delay.sync = 1; s.delay.noteValue = kNote1_8D;
        s.delay.pingPongWidth = 150.0f;
        // Mono legato acid voice with a short glide
        s.global.voiceMode = 1;                        // Mono
        s.monoMode.legato = 1; s.monoMode.portamentoTimeMs = 20.0f;
        s.monoMode.portaMode = 1;                      // glide only on legato overlaps
        presets.push_back(std::move(p));
    }

    // "Arp Morph Sequence" - a TALKING arp: pitch crossfades a vowel formant into a
    // bright additive spectrum (Spectral-Morph mixer) and tilts brightness with the melody.
    {
        PresetDef p;
        p.name = "Arp Morph Sequence";
        p.category = "Arp Modulation";
        auto& s = p.state;
        // OSC A vowel formant, OSC B rich additive - blended in the FFT morph domain
        s.oscA.type = 7;                               // Formant
        s.oscA.formantVowel = 1;                       // 'E' base vowel
        s.oscA.formantMorph = 1.2f;                    // sit between E and I for a nasal 'eee'
        s.oscA.level = 0.8f;
        s.oscB.type = 4;                               // Additive
        s.oscB.additivePartials = 28; s.oscB.additiveTilt = 3.0f; // bright partial stack
        s.oscB.additiveInharm = 0.08f;                 // faint bell shimmer on top
        s.oscB.level = 0.6f;
        s.mixer.mode = 1;                              // SpectralMorph (FFT interp A<->B) - covers the feature
        s.mixer.position = 0.3f;                       // start near the vowel
        s.mixer.tilt = 2.0f;                           // morph-domain brightness lift
        // Open, gentle filter - the timbre motion lives in the MORPH, not the filter
        s.filter.type = 0; s.filter.cutoffHz = 6000.0f; s.filter.resonance = 2.73f;
        s.ampEnv.attackMs = 8.0f;  s.ampEnv.decayMs = 320.0f;
        s.ampEnv.sustain = 0.7f;   s.ampEnv.releaseMs = 260.0f;
        s.ampEnv.attackCurve = 0.3f;                   // soft vocal onset
        // Arp: UpDown with swing for a lilting spoken phrase
        s.arp.operatingMode = 1;                       // MIDI
        s.arp.mode = 2;                                // UpDown
        s.arp.octaveRange = 2;
        s.arp.tempoSync = 1; s.arp.noteValue = kNote1_16;
        s.arp.gateLength = 82.0f; s.arp.swing = 18.0f;
        s.arp.midiOut = 1;                             // emit arp notes as MIDI (covers midiOut)
        int32_t pitchM[] = {0, 3, 7, 10, 12, 10, 7, 3}; // minor-ish rise/fall
        setPitchLane(s, 8, pitchM);
        // pitch -> morph position (SCurve: smooth vowel<->additive glide)
        setModSlot(s, 0, kSrcArpPitch, kDstAllMorphPos, 0.6f, kCurveSCurve);
        // pitch -> spectral tilt (Exp: higher notes noticeably brighter)
        setModSlot(s, 1, kSrcArpPitch, kDstAllSpecTilt, 0.4f, kCurveExp);
        // Slow FREE LFO breathes the cutoff underneath so held tails move
        s.lfo2.rateHz = 0.11f; s.lfo2.shape = 0; s.lfo2.depth = 0.5f; s.lfo2.sync = 0;
        setModSlot(s, 2, kSrcLFO2, kDstAllFltCut, 0.28f);
        // Expression: velocity pushes the morph, keytrack pushes tilt (formant 'gender' feel)
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstMorphPos, 0.3f);
        setVoiceRoute(s, 1, kVSrcKeyTrack, kVDstSpecTilt, 0.25f);
        // FX: lush HALL only - vocal pad space, no delay clutter to muddy the words
        s.reverbEnabled = 1;
        s.reverbType = 1;                              // Hall
        s.reverb.size = 0.72f; s.reverb.mix = 0.3f; s.reverb.damping = 0.3f;
        s.reverb.preDelayMs = 25.0f;
        presets.push_back(std::move(p));
    }

    // "Arp Tilt Cascade" - the arp becomes a pure MODULATOR: notes are held as a
    // dark additive PAD while a FREE-RUNNING internal arp cascades spectral tilt and
    // reverb send from its stepping pitch. No note gating - just blooming motion.
    {
        PresetDef p;
        p.name = "Arp Tilt Cascade";
        p.category = "Arp Modulation";
        auto& s = p.state;
        s.oscA.type = 4;                               // Additive
        s.oscA.additivePartials = 56; s.oscA.additiveTilt = -3.0f; // dark, dense base
        s.oscA.level = 0.7f;
        s.oscB.type = 0; s.oscB.waveform = 1;          // PolyBLEP Saw body underneath
        s.oscB.level = 0.45f; s.oscB.fineCents = 5.0f;
        s.mixer.position = 0.4f;
        s.filter.type = 0; s.filter.cutoffHz = 9000.0f; s.filter.resonance = 2.06f;
        // PAD-shaped amp env so held notes ring as a sustained wash (audible in Mod-only mode)
        s.ampEnv.attackMs = 120.0f; s.ampEnv.decayMs = 500.0f;
        s.ampEnv.sustain = 0.8f;    s.ampEnv.releaseMs = 900.0f;
        s.ampEnv.attackCurve = 0.4f;
        // Arp in MOD-ONLY mode, FREE-RUNNING (not tempo-synced)
        s.arp.operatingMode = 2;                       // Mod (covers operatingMode=2)
        s.arp.mode = 5;                                // Diverge / inside-out (a real mode value)
        s.arp.octaveRange = 3;
        s.arp.tempoSync = 0;                           // free-run
        s.arp.freeRate = 6.5f;                         // 6.5 Hz cascade (covers freeRate)
        s.arp.gateLength = 60.0f;
        int32_t pitchC[] = {0, 5, -3, 7, -7, 12, 0, -12}; // wide leaps = big tilt swings
        setPitchLane(s, 8, pitchC);
        float velC[] = {1.0f, 0.6f, 0.85f, 0.5f, 1.0f, 0.7f, 0.9f, 0.45f};
        setVelocityLane(s, 8, velC);
        // pitch -> spectral tilt (Exp: dark->brilliant bloom)
        setModSlot(s, 0, kSrcArpPitch, kDstAllSpecTilt, 0.65f, kCurveExp);
        // pitch -> effect mix (SCurve), scaled x2 so top notes really drench in reverb
        setModSlot(s, 1, kSrcArpPitch, kDstEffectMix, 0.4f, kCurveSCurve);
        s.modMatrix.slots[1].scale = 3;                // x2 depth - uses the dormant scale axis
        // Slow free LFO wanders the cutoff so the pad never sits still
        s.lfo1.rateHz = 0.13f; s.lfo1.shape = 1; s.lfo1.depth = 0.5f; s.lfo1.sync = 0;
        setModSlot(s, 2, kSrcLFO1, kDstAllFltCut, 0.25f);
        // Mod-env blooms the filter on each held note
        setVoiceRoute(s, 0, kVSrcEnv3, kVDstFltCut, 0.35f);
        s.modEnv.attackMs = 40.0f; s.modEnv.decayMs = 600.0f;
        s.modEnv.sustain = 0.15f;  s.modEnv.releaseMs = 500.0f;
        // FX: Tape delay + PLATE reverb = spacious, slightly wobbly bloom
        s.delayEnabled = 1;
        s.delay.type = 1;                              // Tape
        s.delay.mix = 0.18f; s.delay.feedback = 0.28f;
        s.delay.sync = 1; s.delay.noteValue = kNote1_8;
        s.delay.tapeSaturation = 0.4f; s.delay.tapeAge = 0.2f;
        s.reverbEnabled = 1;
        s.reverbType = 0;                              // Plate
        s.reverb.size = 0.7f; s.reverb.mix = 0.3f; s.reverb.damping = 0.35f;
        presets.push_back(std::move(p));
    }

    // "Arp Chaos Matrix" - the antidote to same-ish: hard-sync + Lorenz chaos through a
    // wavefolder, driven by SIX mod slots (arp pitch, chaos, LFO, rungler + TWO arp
    // SELF-mod routes) with swung ratchets. Gnarly, unpredictable, ever-shifting mono lead.
    {
        PresetDef p;
        p.name = "Arp Chaos Matrix";
        p.category = "Arp Modulation";
        auto& s = p.state;
        s.oscA.type = 3;                               // Sync
        s.oscA.syncRatio = 3.0f; s.oscA.syncWaveform = 1; // saw slave
        s.oscA.syncMode = 0; s.oscA.syncAmount = 0.85f;
        s.oscA.level = 0.8f;
        s.oscB.type = 5;                               // Chaos
        s.oscB.chaosAttractor = 0;                     // Lorenz
        s.oscB.chaosAmount = 0.45f; s.oscB.chaosCoupling = 0.2f;
        s.oscB.chaosOutput = 1;                        // Y axis
        s.oscB.level = 0.4f;
        s.mixer.mode = 1;                              // SpectralMorph (chaos<->sync FFT blend)
        s.mixer.position = 0.4f;
        s.filter.type = 4; s.filter.cutoffHz = 1800.0f; s.filter.resonance = 5.45f;
        s.filter.ladderSlope = 4; s.filter.envAmount = 30.0f; // +30 SEMITONES stab (audible!)
        s.ampEnv.attackMs = 3.0f; s.ampEnv.decayMs = 300.0f;
        s.ampEnv.sustain = 0.6f;  s.ampEnv.releaseMs = 200.0f;
        s.filterEnv.attackMs = 1.0f; s.filterEnv.decayMs = 240.0f;
        s.filterEnv.sustain = 0.15f; s.filterEnv.releaseMs = 150.0f;
        // Arp: drunk walk with ratchet rolls, spice and humanize
        s.arp.operatingMode = 1;
        s.arp.mode = 7;                                // Walk (drunk / +-1 random walk)
        s.arp.octaveRange = 2;
        s.arp.tempoSync = 1; s.arp.noteValue = kNote1_16;
        s.arp.gateLength = 65.0f; s.arp.swing = 20.0f;
        s.arp.spice = 0.4f; s.arp.humanize = 0.25f;
        int32_t pitchX[] = {0, 3, -2, 5, 7, -4, 12, -7};
        setPitchLane(s, 8, pitchX);
        int32_t ratchX[] = {1, 1, 2, 1, 1, 3, 1, 2};  // rolls for drum-like bursts
        setRatchetLane(s, 8, ratchX);
        s.arp.ratchetSwing = 62.0f;                    // swung sub-steps inside the rolls
        // ---- SIX mod slots ----
        // 0: arp pitch -> cutoff (Exp)
        setModSlot(s, 0, kSrcArpPitch, kDstAllFltCut, 0.5f, kCurveExp);
        // 1: chaos LFO -> morph position (raise chaosMod.depth first!)
        s.chaosMod.rateHz = 2.2f; s.chaosMod.type = 0; s.chaosMod.depth = 0.6f;
        setModSlot(s, 1, kSrcChaos, kDstAllMorphPos, 0.45f);
        // 2: synced saw LFO -> spectral tilt
        s.lfo1.rateHz = 0.3f; s.lfo1.shape = 2; s.lfo1.depth = 0.7f; s.lfo1.sync = 1;
        s.lfo1Ext.noteValue = kNote1_4;
        setModSlot(s, 2, kSrcLFO1, kDstAllSpecTilt, 0.4f);
        // 3: rungler -> resonance (Stepped = quantized crunch; raise rungler.depth first!)
        s.rungler.osc1FreqHz = 3.0f; s.rungler.osc2FreqHz = 5.5f;
        s.rungler.depth = 0.45f; s.rungler.bits = 5;
        setModSlot(s, 3, kSrcRungler, kDstAllResonance, 0.3f, kCurveStepped);
        // 4: SELF-MOD arp pitch -> arp gate length (covers ArpPitch->ArpGateLen)
        setModSlot(s, 4, kSrcArpPitch, kDstArpGateLen, 0.25f);
        // 5: SELF-MOD arp pitch -> arp swing (covers ArpPitch->ArpSwing)
        setModSlot(s, 5, kSrcArpPitch, kDstArpSwing, 0.2f, kCurveSCurve);
        // Voice routes: velocity drives the fold, mod-env sweeps the morph
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstDistDrive, 0.4f);
        setVoiceRoute(s, 1, kVSrcEnv3, kVDstMorphPos, 0.3f);
        s.modEnv.attackMs = 2.0f; s.modEnv.decayMs = 400.0f;
        s.modEnv.sustain = 0.0f;  s.modEnv.releaseMs = 200.0f;
        // Wavefolder = the dirty character owned by THIS preset
        s.distortion.type = 4;                         // Wavefolder
        s.distortion.drive = 0.35f; s.distortion.foldType = 1; s.distortion.mix = 0.8f;
        // Mono glide + GRANULAR delay (a delay TYPE none of the ping-pong siblings use)
        s.global.voiceMode = 1;
        s.monoMode.legato = 1; s.monoMode.portamentoTimeMs = 15.0f;
        s.delayEnabled = 1;
        s.delay.type = 3;                              // Granular
        s.delay.mix = 0.22f; s.delay.feedback = 0.3f;
        s.delay.sync = 1; s.delay.noteValue = kNote1_8;
        s.delay.granularSizeMs = 120.0f; s.delay.granularDensity = 14.0f;
        s.delay.granularPitchSpray = 0.15f; s.delay.granularTexture = 0.3f;
        presets.push_back(std::move(p));
    }

    // "Arp FX Depth" - bouncy euclidean saw/triangle where arp pitch pushes the wet send
    // AND the spice, plus a MACRO hand-sweeps the spectral morph. A one-knob performance
    // arp drenched in chorus + delay + hall.
    {
        PresetDef p;
        p.name = "Arp FX Depth";
        p.category = "Arp Modulation";
        auto& s = p.state;
        s.oscA.type = 0; s.oscA.waveform = 1; s.oscA.level = 0.7f; // PolyBLEP Saw
        s.oscB.type = 1; s.oscB.waveform = 4;          // Wavetable Triangle
        s.oscB.tuneSemitones = 12.0f;                  // octave-up sparkle
        s.oscB.phaseMod = 0.15f;                       // subtle table motion
        s.oscB.level = 0.4f;
        s.mixer.mode = 1;                              // SpectralMorph so Macro->Morph is AUDIBLE
        s.mixer.position = 0.35f;
        s.filter.type = 4; s.filter.cutoffHz = 4200.0f; s.filter.resonance = 3.86f;
        s.filter.ladderSlope = 4;
        s.ampEnv.attackMs = 12.0f; s.ampEnv.decayMs = 460.0f;
        s.ampEnv.sustain = 0.7f;   s.ampEnv.releaseMs = 380.0f;
        // Arp: as-played, euclidean 5/8 with heavy swing
        s.arp.operatingMode = 1;
        s.arp.mode = 8;                                // AsPlayed
        s.arp.octaveRange = 1;
        s.arp.tempoSync = 1; s.arp.noteValue = kNote1_8;
        s.arp.gateLength = 88.0f; s.arp.swing = 30.0f;
        setEuclidean(s, true, 5, 8, 1);                // E(5,8) rot 1 - cinquillo bounce
        int32_t pitchF[] = {0, 4, 7, 12, 0, -3, 5, 10};
        setPitchLane(s, 8, pitchF);
        float gateF[] = {1.0f, 0.5f, 1.5f, 0.3f, 1.0f, 0.7f, 1.2f, 0.4f}; // staccato/legato mix
        setGateLane(s, 8, gateF);
        // pitch -> effect mix (SCurve): higher notes = wetter
        setModSlot(s, 0, kSrcArpPitch, kDstEffectMix, 0.5f, kCurveSCurve);
        // pitch -> arp spice (covers ArpPitch->ArpSpice): highs get more variation
        setModSlot(s, 1, kSrcArpPitch, kDstArpSpice, 0.35f);
        // MACRO 1 = one-knob performance morph over the spectral mixer (covers macro-in-arp)
        s.macros.values[0] = 0.35f;                    // parked mid so morph is live from load
        setModSlot(s, 2, kSrcMacro1, kDstAllMorphPos, 0.7f, kCurveLinear);
        // Free LFO for gentle cutoff drift
        s.lfo1.rateHz = 0.28f; s.lfo1.shape = 0; s.lfo1.depth = 0.5f; s.lfo1.sync = 0;
        setModSlot(s, 3, kSrcLFO1, kDstAllFltCut, 0.3f);
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.45f);
        setVoiceRoute(s, 1, kVSrcKeyTrack, kVDstSpecTilt, 0.25f);
        // FX chain: CHORUS + Digital delay + HALL = deep wet bed
        s.modulationType = 3;                          // Chorus (an under-used effect)
        s.chorus.rateHz = 0.4f; s.chorus.depth = 0.5f; s.chorus.mix = 0.35f;
        s.chorus.voices = 3; s.chorus.stereoSpread = 180.0f;
        s.delayEnabled = 1;
        s.delay.type = 0;                              // Digital (distinct delay type)
        s.delay.mix = 0.26f; s.delay.feedback = 0.35f;
        s.delay.sync = 1; s.delay.noteValue = kNote1_8D;
        s.delay.digitalModDepth = 0.2f; s.delay.digitalWidth = 130.0f;
        s.reverbEnabled = 1;
        s.reverbType = 1;                              // Hall
        s.reverb.size = 0.62f; s.reverb.mix = 0.3f;
        presets.push_back(std::move(p));
    }

    // "Arp Self Modulator" - the arp's own pitch is its only modulator:
    //   higher notes -> faster, swung-er, spicier, brighter, wetter.
    //   This preset OWNS operatingMode=MIDI+Mod and the free-running (non-synced)
    //   arp clock, so it is the self-animating hub of the Arp Modulation set.
    {
        PresetDef p;
        p.name = "Arp Self Modulator";
        p.category = "Arp Modulation";
        auto& s = p.state;
        // OSC A: Phase-Distortion ResSaw = a resonant CZ-buzz that sweeps with note pitch
        s.oscA.type = 2;          // Phase Distortion
        s.oscA.pdWaveform = 5;    // ResSaw (formant-y resonant sweep)
        s.oscA.pdDistortion = 0.5f; // half DCW = strong but not screaming
        s.oscA.level = 0.8f;
        // OSC B: square sub an octave down for weight under the buzz
        s.oscB.type = 0; s.oscB.waveform = 2; // PolyBLEP Square
        s.oscB.tuneSemitones = -12.0f; s.oscB.level = 0.4f;
        s.mixer.position = 0.3f;  // favour the ResSaw, keep sub as a bed
        // Ladder LP with envelope movement; cutoff is ALSO an ArpPitch target below
        s.filter.type = 4;        // Ladder
        s.filter.cutoffHz = 2600.0f; s.filter.resonance = 5.22f;
        s.filter.ladderSlope = 4; s.filter.ladderDrive = 2.0f;
        s.filter.envAmount = 26.0f; // +26 st = an audible per-note pluck sweep (was a sane 20)
        // Amp: pluck-ish so each step has a transient the self-mod can articulate
        s.ampEnv.attackMs = 2.0f; s.ampEnv.decayMs = 250.0f;
        s.ampEnv.sustain = 0.5f; s.ampEnv.releaseMs = 150.0f;
        s.filterEnv.attackMs = 1.0f; s.filterEnv.decayMs = 200.0f;
        s.filterEnv.sustain = 0.1f; s.filterEnv.releaseMs = 100.0f;
        // --- ARP: free-running MIDI+Mod, self-modulated ---
        s.arp.operatingMode = 3;  // MIDI+Mod: plays the pattern AND emits mod
        s.arp.mode = 2;           // UpDown
        s.arp.octaveRange = 2;
        s.arp.tempoSync = 0;      // FREE-RUN: this preset owns freeRate
        s.arp.freeRate = 8.0f;    // ~1/16 @120bpm baseline; ArpPitch pushes it faster
        s.arp.gateLength = 70.0f;
        int32_t pitchS[] = {0, 2, 4, 5, 7, 9, 11, 12}; // diatonic climb over an octave
        setPitchLane(s, 8, pitchS);
        // Modifier lane: accents on the downbeats, slides into the octave leaps
        int32_t modS[] = {
            kStepActive | kStepAccent, kStepActive, kStepActive,
            kStepActive | kStepSlide, kStepActive | kStepAccent,
            kStepActive, kStepActive, kStepActive | kStepSlide
        };
        setModifierLane(s, 8, modS, 35, 80.0f);
        // --- The self-modulation matrix: ArpPitch -> everything ---
        // higher notes = faster clock (exp so the top of the range really rips)
        setModSlot(s, 0, kSrcArpPitch, kDstArpRate,    0.30f, kCurveExp);
        // higher notes = shorter gate (staccato at the top, legato at the bottom)
        setModSlot(s, 1, kSrcArpPitch, kDstArpGateLen, -0.25f, kCurveLinear);
        // higher notes = more swing/groove push
        setModSlot(s, 2, kSrcArpPitch, kDstArpSwing,   0.25f);
        // higher notes = brighter (smoothstep so the sweep feels vocal, not linear)
        setModSlot(s, 3, kSrcArpPitch, kDstAllFltCut,  0.40f, kCurveSCurve);
        // higher notes = more probabilistic variation (spice)
        setModSlot(s, 4, kSrcArpPitch, kDstArpSpice,   0.30f);
        // higher notes = wetter tail (pitch pushes the delay/reverb send up)
        setModSlot(s, 5, kSrcArpPitch, kDstEffectMix,  0.20f, kCurveExp);
        // Slow free LFO drifts the PD morph so the buzz breathes between notes
        s.lfo1.rateHz = 0.2f; s.lfo1.shape = 1; s.lfo1.depth = 0.5f; s.lfo1.sync = 0;
        setModSlot(s, 6, kSrcLFO1, kDstAllMorphPos, 0.30f);
        // Performance voice routes: velocity opens cutoff, pressure adds resonance
        setVoiceRoute(s, 0, kVSrcVelocity,   kVDstFltCut, 0.5f);
        setVoiceRoute(s, 1, kVSrcAftertouch, kVDstFltRes, 0.3f);
        // Mono legato with a short glide so the slide steps actually portamento
        s.global.voiceMode = 1;
        s.monoMode.legato = 1; s.monoMode.portamentoTimeMs = 25.0f;
        // Tempo-synced dotted-8th delay tail (delay stays synced even though arp free-runs)
        s.delayEnabled = 1;
        s.delay.mix = 0.22f; s.delay.feedback = 0.32f;
        s.delay.sync = 1; s.delay.noteValue = kNote1_8;
        presets.push_back(std::move(p));
    }

    // ==================== MODULATION SHOWCASE Category ====================
    // Presets demonstrating deep modulation routing

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
        s.filter.resonance = 4.66f;
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
        s.filter.type = 4; s.filter.cutoffHz = 1600.0f; s.filter.resonance = 4.09f;
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

        s.filter.type = 4; s.filter.cutoffHz = 2600.0f; s.filter.resonance = 3.53f;
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

    // ==================== Rhythmic Arp Batch (20 presets) ====================
    //
    // The bank already carries 25 arp presets across the dedicated Arp* categories,
    // and those run the arpeggiator BARE: each one exists to demonstrate a lane, a
    // mode, or a scale system. These twenty are deliberately different in kind --
    // every one pairs the arp with a SECOND rhythmic element (trance gate,
    // transient duck, Euclidean gate, ratchet lane, or a synced FX rate) whose
    // cycle length does not divide the arp's. The two grids drift against each
    // other, so the groove is polyrhythmic rather than a plain note pattern. That
    // is what earns them a place in Rhythmic instead of Arp*, and it is why they
    // do not collapse into the existing 25 on an A/B listen.

    // "Gate Ladder" - 16-step arp against a 12-step gate: the two grids realign
    // only every 3 bars, so a single held chord keeps re-accenting itself
    {
        PresetDef p;
        p.name = "Gate Ladder";
        p.category = "Rhythmic";
        auto& s = p.state;
        // Classic warm dual saw - deliberately plain, so the GRID is the interest
        s.oscA.type = 0; s.oscA.waveform = 1; s.oscA.level = 0.78f;
        s.oscB.type = 0; s.oscB.waveform = 1;
        s.oscB.fineCents = 7.0f; s.oscB.level = 0.6f;
        s.mixer.position = 0.45f;
        s.global.polyphony = 8; s.global.width = 1.3f;
        // Ladder LP with a per-note sweep so every arp step re-opens
        s.filter.type = 4;                  // Ladder LP
        s.filter.cutoffHz = 1600.0f; s.filter.resonance = 5.20f;
        s.filter.ladderSlope = 4; s.filter.ladderDrive = 3.5f;
        s.filter.envAmount = 24.0f; s.filter.keyTrack = 0.35f;
        s.filterEnv.attackMs = 2.0f; s.filterEnv.decayMs = 140.0f;
        s.filterEnv.sustain = 0.3f; s.filterEnv.releaseMs = 120.0f;
        s.ampEnv.attackMs = 3.0f; s.ampEnv.decayMs = 200.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 180.0f;
        // ARP: straight 16ths, two octaves
        setArpEnabled(s, true); setArpMode(s, kModeUp); setTempoSync(s, true);
        setArpRate(s, kNote1_16); setArpGateLength(s, 70.0f);
        s.arp.octaveRange = 2;
        // THE HOOK: gate runs TWELVE steps at the same 1/16 clock. 12 vs 16 means
        // the accent pattern lands on a different arp note every bar and only
        // repeats after three -- a free-running polymeter from two static lanes.
        s.tranceGate.enabled = 1; s.tranceGate.numSteps = 12;
        s.tranceGate.tempoSync = 1; s.tranceGate.noteValue = kNote1_16;
        s.tranceGate.depth = 0.9f;
        s.tranceGate.attackMs = 2.0f; s.tranceGate.releaseMs = 18.0f;
        float tg[] = {1,0,1,1, 0,1,0,1, 1,0,1,0};
        for (int i = 0; i < 12; ++i) s.tranceGate.stepLevels[i] = tg[i];
        // Gate output flicks the morph so the accents differ in timbre, not just level
        setVoiceRoute(s, 0, kVSrcGate, kVDstMorphPos, 0.3f);
        setVoiceRoute(s, 1, kVSrcVelocity, kVDstFltCut, 0.4f);
        // Synced digital delay at 1/8D adds a third, non-dividing period
        s.delayEnabled = 1; s.delay.type = 0;   // Digital
        s.delay.sync = 1; s.delay.noteValue = kNote1_8D;
        s.delay.mix = 0.24f; s.delay.feedback = 0.36f;
        s.reverbEnabled = 1; s.reverbType = 0;  // Plate
        s.reverb.size = 0.42f; s.reverb.mix = 0.18f;
        presets.push_back(std::move(p));
    }

    // "Duck Runner" - phase-distortion runner that ducks itself: the transient
    // detector pumps master volume on every arp attack, keyed-compressor style
    {
        PresetDef p;
        p.name = "Duck Runner";
        p.category = "Rhythmic";
        auto& s = p.state;
        s.oscA.type = 2;                    // Phase Distortion
        s.oscA.pdWaveform = 1; s.oscA.pdDistortion = 0.55f; s.oscA.level = 0.85f;
        s.oscB.type = 2; s.oscB.pdWaveform = 4; s.oscB.pdDistortion = 0.35f;
        s.oscB.tuneSemitones = -12.0f; s.oscB.level = 0.45f;
        s.mixer.position = 0.4f;
        s.filter.type = 0;                  // SVF LP
        s.filter.cutoffHz = 2800.0f; s.filter.resonance = 3.60f;
        s.filter.svfDrive = 3.0f; s.filter.envAmount = 20.0f;
        s.filterEnv.attackMs = 2.0f; s.filterEnv.decayMs = 160.0f;
        s.filterEnv.sustain = 0.3f; s.filterEnv.releaseMs = 130.0f;
        s.ampEnv.attackMs = 2.0f; s.ampEnv.decayMs = 220.0f;
        s.ampEnv.sustain = 0.75f; s.ampEnv.releaseMs = 170.0f;
        // ARP: 1/8 UpDown -- a slower grid than the duck's release, on purpose
        setArpEnabled(s, true); setArpMode(s, kModeUpDown); setTempoSync(s, true);
        setArpRate(s, kNote1_8); setArpGateLength(s, 78.0f);
        s.arp.octaveRange = 2;
        // THE HOOK: each arp note triggers the transient detector, which ducks
        // master volume (negative amount). The 200 ms recovery is LONGER than the
        // 1/8 step at most tempos, so ducks overlap and the line breathes in waves
        // instead of pumping uniformly -- a rhythm the arp alone cannot produce.
        s.transient.sensitivity = 0.75f;
        s.transient.attackMs = 1.0f; s.transient.decayMs = 200.0f;
        setModSlot(s, 0, kSrcTransient, kDstMasterVol, -0.5f, kCurveExp);
        // The same detector opens the filter as it ducks: quieter but brighter
        setModSlot(s, 1, kSrcTransient, kDstAllFltCut, 0.35f, kCurveExp);
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.45f);
        s.modulationType = 3;               // Chorus
        s.chorus.rateHz = 0.5f; s.chorus.depth = 0.4f; s.chorus.mix = 0.3f;
        s.delayEnabled = 1; s.delay.type = 2;   // PingPong
        s.delay.sync = 1; s.delay.noteValue = kNote1_16;
        s.delay.mix = 0.2f; s.delay.feedback = 0.3f;
        s.delay.pingPongWidth = 130.0f;
        s.reverbEnabled = 1; s.reverbType = 0;
        s.reverb.size = 0.38f; s.reverb.mix = 0.16f;
        presets.push_back(std::move(p));
    }

    // "Euclid Pluck" - comb-resonator pluck on a 5-in-8 Euclidean arp, with a
    // ratchet lane of a different length rolling across the gaps
    {
        PresetDef p;
        p.name = "Euclid Pluck";
        p.category = "Rhythmic";
        auto& s = p.state;
        // Noise burst into a key-tracked comb = Karplus-ish plucked string
        s.oscA.type = 9; s.oscA.noiseColor = 1; s.oscA.level = 0.5f;  // pink exciter
        s.oscB.type = 0; s.oscB.waveform = 4; s.oscB.level = 0.35f;   // triangle body
        s.mixer.position = 0.4f;
        s.filter.type = 6;                  // Comb
        s.filter.cutoffHz = 440.0f; s.filter.resonance = 7.50f;
        s.filter.combDamping = 0.35f; s.filter.keyTrack = 1.0f; // tracks the played note
        s.ampEnv.attackMs = 1.0f; s.ampEnv.decayMs = 320.0f;
        s.ampEnv.sustain = 0.15f; s.ampEnv.releaseMs = 260.0f;
        // ARP with EUCLIDEAN gating: 5 hits spread over 8 steps
        setArpEnabled(s, true); setArpMode(s, kModeUp); setTempoSync(s, true);
        setArpRate(s, kNote1_16); setArpGateLength(s, 45.0f);  // short = plucky
        s.arp.octaveRange = 2;
        setEuclidean(s, true, 5, 8, 2);     // rotated so the pattern starts off-beat
        // THE HOOK: ratchet lane is SEVEN long against the 8-step Euclidean cycle,
        // so which Euclidean hit gets rolled advances by one every bar.
        int32_t ratch7[] = {1, 1, 3, 1, 2, 1, 4};
        setRatchetLane(s, 7, ratch7);
        s.arp.ratchetSwing = 56.0f;
        float vel7[] = {1.0f, 0.6f, 0.85f, 0.55f, 0.95f, 0.6f, 0.75f};
        setVelocityLane(s, 7, vel7);
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltRes, 0.4f);  // harder = more ring
        // Tape echo softens the pluck tails without washing the transients
        s.delayEnabled = 1; s.delay.type = 1;   // Tape
        s.delay.sync = 1; s.delay.noteValue = kNote1_8;
        s.delay.mix = 0.26f; s.delay.feedback = 0.34f;
        s.delay.tapeSaturation = 0.45f; s.delay.tapeWear = 0.3f;
        s.reverbEnabled = 1; s.reverbType = 1;  // Hall
        s.reverb.size = 0.5f; s.reverb.mix = 0.22f; s.reverb.damping = 0.45f;
        s.global.masterGain = 1.4f;             // noise-excited comb is a quiet source
        presets.push_back(std::move(p));
    }

    // "Half Time Crush" - dotted-8th arp crushed under a straight 1/16 gate; the
    // dotted grid slides against the gate so the crush lands somewhere new each bar
    {
        PresetDef p;
        p.name = "Half Time Crush";
        p.category = "Rhythmic";
        auto& s = p.state;
        s.oscA.type = 1;                    // Wavetable
        s.oscA.waveform = 2; s.oscA.level = 0.8f;
        s.oscB.type = 0; s.oscB.waveform = 3; s.oscB.pulseWidth = 0.35f;
        s.oscB.tuneSemitones = -12.0f; s.oscB.level = 0.5f;
        s.mixer.position = 0.45f;
        s.filter.type = 0;                  // SVF LP
        s.filter.cutoffHz = 2200.0f; s.filter.resonance = 4.40f;
        s.filter.envAmount = 22.0f;
        s.filterEnv.attackMs = 1.0f; s.filterEnv.decayMs = 130.0f;
        s.filterEnv.sustain = 0.25f; s.filterEnv.releaseMs = 110.0f;
        s.ampEnv.attackMs = 2.0f; s.ampEnv.decayMs = 240.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 150.0f;
        // Spectral bit-reduction: the "crush"
        s.distortion.type = 2;              // Spectral
        s.distortion.drive = 0.45f; s.distortion.mix = 0.7f;
        s.distortion.spectralMode = 3; s.distortion.spectralBits = 0.35f;
        s.distortion.spectralCurve = 3;
        // ARP: DOTTED 8ths -- 3/16 per step, which never aligns with a 16-step gate
        setArpEnabled(s, true); setArpMode(s, kModeUp); setTempoSync(s, true);
        setArpRate(s, kNote1_8D); setArpGateLength(s, 85.0f);
        s.arp.octaveRange = 2;
        // THE HOOK: gate is straight 1/16 over 16 steps. Arp steps every 3/16,
        // gate cycle is 16/16: they realign only every 3 bars.
        s.tranceGate.enabled = 1; s.tranceGate.numSteps = 16;
        s.tranceGate.tempoSync = 1; s.tranceGate.noteValue = kNote1_16;
        s.tranceGate.depth = 0.8f;
        s.tranceGate.attackMs = 1.0f; s.tranceGate.releaseMs = 12.0f;
        float tg[] = {1,1,0,0, 1,0,1,1, 0,1,1,0, 1,0,0,1};
        for (int i = 0; i < 16; ++i) s.tranceGate.stepLevels[i] = tg[i];
        // Gate drives distortion drive: the crush intensity is itself rhythmic
        setVoiceRoute(s, 0, kVSrcGate, kVDstDistDrive, 0.45f);
        s.lfo1.rateHz = 0.2f; s.lfo1.shape = 1; s.lfo1.depth = 0.5f; s.lfo1.sync = 1;
        s.lfo1Ext.noteValue = 19;           // whole-note tilt drift
        setModSlot(s, 0, kSrcLFO1, kDstAllSpecTilt, 0.35f, kCurveSCurve);
        s.delayEnabled = 1; s.delay.type = 0;   // Digital
        s.delay.sync = 1; s.delay.noteValue = kNote1_16;
        s.delay.mix = 0.18f; s.delay.feedback = 0.28f;
        presets.push_back(std::move(p));
    }

    // "Swing Vox" - heavily swung formant arp over a sparse gate lane; the vowel
    // moves per step so the pattern reads as syllables, not notes
    {
        PresetDef p;
        p.name = "Swing Vox";
        p.category = "Rhythmic";
        auto& s = p.state;
        s.oscA.type = 7;                    // Formant
        s.oscA.formantVowel = 0; s.oscA.formantMorph = 0.3f; s.oscA.level = 0.85f;
        s.oscB.type = 0; s.oscB.waveform = 1; s.oscB.level = 0.3f;
        s.mixer.position = 0.3f;
        s.filter.type = 5;                  // Formant filter on a formant osc
        s.filter.cutoffHz = 900.0f; s.filter.resonance = 3.20f;
        s.filter.formantMorph = 0.4f; s.filter.formantGender = 0.3f;
        s.ampEnv.attackMs = 8.0f; s.ampEnv.decayMs = 200.0f;
        s.ampEnv.sustain = 0.65f; s.ampEnv.releaseMs = 220.0f;
        // ARP: 1/16 with heavy swing -- the shuffle IS the rhythmic partner here
        setArpEnabled(s, true); setArpMode(s, kModeUpDown); setTempoSync(s, true);
        setArpRate(s, kNote1_16); setArpGateLength(s, 62.0f);
        setArpSwing(s, 62.0f);              // deep shuffle, near triplet feel
        s.arp.octaveRange = 2;
        // THE HOOK: a 5-step gate lane against the swung 16ths. Because the gate
        // lane is odd-length AND the clock is shuffled, the clipped steps fall on
        // alternating sides of the swing pair -- the phrase never repeats a bar.
        // The short values are 0.15, not 0.0: the arp clamps gate duration to a
        // 1-sample minimum (FR-014 always fires a NoteOff), so a 0.0 lane value
        // is a click rather than a rest. 0.15 is a real staccato syllable.
        float gate5[] = {1.0f, 0.15f, 0.85f, 0.6f, 0.18f};
        setGateLane(s, 5, gate5);
        float vel5[] = {1.0f, 0.5f, 0.8f, 0.65f, 0.9f};
        setVelocityLane(s, 5, vel5);
        // Vowel follows the arp's own pitch: higher notes open toward "ee"
        setModSlot(s, 0, kSrcArpPitch, kDstAllMorphPos, 0.55f, kCurveLinear);
        s.lfo1.rateHz = 0.35f; s.lfo1.shape = 0; s.lfo1.depth = 0.5f; s.lfo1.sync = 0;
        setModSlot(s, 1, kSrcLFO1, kDstAllFltCut, 0.25f, kCurveSCurve);
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstMorphPos, 0.35f);
        s.modulationType = 3;               // Chorus widens the choir
        s.chorus.rateHz = 0.45f; s.chorus.depth = 0.5f; s.chorus.mix = 0.35f;
        s.reverbEnabled = 1; s.reverbType = 1;  // Hall
        s.reverb.size = 0.55f; s.reverb.mix = 0.26f; s.reverb.damping = 0.4f;
        presets.push_back(std::move(p));
    }

    // "Sub Pulse Grid" - mono sub-bass arp descending under an 8-step gate;
    // built to sit below a kick, so the gate carves the space rather than adding
    {
        PresetDef p;
        p.name = "Sub Pulse Grid";
        p.category = "Rhythmic";
        auto& s = p.state;
        s.oscA.type = 0; s.oscA.waveform = 1; s.oscA.level = 0.85f;
        s.oscA.tuneSemitones = -12.0f;
        s.oscB.type = 0; s.oscB.waveform = 0; s.oscB.level = 0.5f;   // sine sub
        s.oscB.tuneSemitones = -24.0f;
        s.mixer.position = 0.5f;
        s.filter.type = 4;                  // Ladder LP
        s.filter.cutoffHz = 700.0f; s.filter.resonance = 3.80f;
        s.filter.ladderSlope = 4; s.filter.ladderDrive = 7.0f;  // drive = weight
        s.filter.envAmount = 20.0f;
        s.filterEnv.attackMs = 1.0f; s.filterEnv.decayMs = 110.0f;
        s.filterEnv.sustain = 0.2f; s.filterEnv.releaseMs = 90.0f;
        s.ampEnv.attackMs = 1.0f; s.ampEnv.decayMs = 180.0f;
        s.ampEnv.sustain = 0.8f; s.ampEnv.releaseMs = 100.0f;
        // Mono, no glide: each arp step must be a separate, tight sub hit
        s.global.voiceMode = 1; s.global.polyphony = 1;
        s.monoMode.priority = 0; s.monoMode.legato = 0;
        // ARP: descending 16ths, ONE octave -- stays in the sub register
        setArpEnabled(s, true); setArpMode(s, kModeDown); setTempoSync(s, true);
        setArpRate(s, kNote1_16); setArpGateLength(s, 55.0f);
        s.arp.octaveRange = 1;
        // THE HOOK: 8-step gate at 1/8 -- HALF the arp's clock. Every other arp
        // note is silenced, and because the gate pattern is asymmetric the surviving
        // notes trace a slower melody out of the fast run.
        s.tranceGate.enabled = 1; s.tranceGate.numSteps = 8;
        s.tranceGate.tempoSync = 1; s.tranceGate.noteValue = kNote1_8;
        s.tranceGate.depth = 1.0f;
        s.tranceGate.attackMs = 1.0f; s.tranceGate.releaseMs = 30.0f;
        float tg[] = {1,0,1,1, 0,1,0,1};
        for (int i = 0; i < 8; ++i) s.tranceGate.stepLevels[i] = tg[i];
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.35f);
        setVoiceRoute(s, 1, kVSrcGate, kVDstDistDrive, 0.3f);
        s.distortion.type = 5;              // Tape Saturator for sub harmonics
        s.distortion.drive = 0.4f; s.distortion.mix = 0.5f;
        s.distortion.tapeSaturation = 0.55f;
        s.global.masterGain = 0.9f;         // sub energy needs headroom
        presets.push_back(std::move(p));
    }

    // "Ratchet Bell" - inharmonic additive bell on a 7-in-16 Euclidean arp, each
    // hit subdivided by a ratchet lane of a different length
    {
        PresetDef p;
        p.name = "Ratchet Bell";
        p.category = "Rhythmic";
        auto& s = p.state;
        s.oscA.type = 4;                    // Additive
        s.oscA.additivePartials = 24; s.oscA.additiveInharm = 0.35f;
        s.oscA.additiveTilt = -0.3f; s.oscA.level = 0.8f;
        s.oscB.type = 0; s.oscB.waveform = 0; s.oscB.level = 0.25f;
        s.mixer.position = 0.25f;
        s.filter.type = 8;                  // SVF Peak - emphasises one partial band
        s.filter.cutoffHz = 2400.0f; s.filter.resonance = 4.20f;
        s.filter.svfGain = 6.0f;
        s.ampEnv.attackMs = 1.0f; s.ampEnv.decayMs = 600.0f;
        s.ampEnv.sustain = 0.1f; s.ampEnv.releaseMs = 700.0f;  // long ring
        // ARP: Euclidean 7-in-16 -- a dense but uneven bell pattern
        setArpEnabled(s, true); setArpMode(s, kModeUp); setTempoSync(s, true);
        setArpRate(s, kNote1_16); setArpGateLength(s, 40.0f);
        s.arp.octaveRange = 3;              // bells span wide
        setEuclidean(s, true, 7, 16, 3);
        // THE HOOK: 5-long ratchet lane against the 16-step Euclidean cycle. Which
        // of the seven hits gets a roll advances every bar (5 and 16 are coprime),
        // so the bell figure reshuffles continuously from static data.
        int32_t ratch5[] = {1, 2, 1, 3, 1};
        setRatchetLane(s, 5, ratch5);
        s.arp.ratchetSwing = 50.0f;
        setModSlot(s, 0, kSrcArpPitch, kDstAllSpecTilt, 0.4f, kCurveLinear);
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstSpecTilt, 0.35f);
        s.delayEnabled = 1; s.delay.type = 2;   // PingPong scatters the bells
        s.delay.sync = 1; s.delay.noteValue = kNote1_8T;
        s.delay.mix = 0.28f; s.delay.feedback = 0.4f;
        s.delay.pingPongWidth = 160.0f; s.delay.pingPongCrossFeed = 0.7f;
        s.reverbEnabled = 1; s.reverbType = 1;  // Hall
        s.reverb.size = 0.7f; s.reverb.mix = 0.3f; s.reverb.damping = 0.35f;
        presets.push_back(std::move(p));
    }

    // "Chaos Step" - Lorenz-driven timbre on a Random-order arp whose steps fire
    // probabilistically; the note order and the note COUNT both vary each bar
    {
        PresetDef p;
        p.name = "Chaos Step";
        p.category = "Rhythmic";
        auto& s = p.state;
        s.oscA.type = 5;                    // Chaos
        s.oscA.chaosAttractor = 0;          // Lorenz
        s.oscA.chaosAmount = 0.45f; s.oscA.chaosCoupling = 0.3f;
        s.oscA.level = 0.7f;
        s.oscB.type = 0; s.oscB.waveform = 3; s.oscB.level = 0.5f;
        s.mixer.position = 0.55f;           // keep a solid pitched anchor
        s.filter.type = 2;                  // SVF BP
        s.filter.cutoffHz = 1200.0f; s.filter.resonance = 5.60f;
        s.filter.envAmount = 26.0f;
        s.filterEnv.attackMs = 1.0f; s.filterEnv.decayMs = 150.0f;
        s.filterEnv.sustain = 0.2f; s.filterEnv.releaseMs = 120.0f;
        s.ampEnv.attackMs = 2.0f; s.ampEnv.decayMs = 200.0f;
        s.ampEnv.sustain = 0.6f; s.ampEnv.releaseMs = 180.0f;
        // ARP: Random order at 1/16
        setArpEnabled(s, true); setArpMode(s, kModeRandom); setTempoSync(s, true);
        setArpRate(s, kNote1_16); setArpGateLength(s, 65.0f);
        s.arp.octaveRange = 2;
        // THE HOOK: the CONDITION lane makes steps probabilistic, so the pattern
        // is rhythmically sparse in a way that changes every pass. Random order
        // alone reorders a fixed note count; this varies the count too.
        int32_t cond8[] = {kCondAlways, kCondProb50, kCondProb75, kCondProb25,
                           kCondAlways, kCondProb90, kCondProb50, kCondProb10};
        setConditionLane(s, 8, cond8, 0);
        float vel8[] = {1.0f, 0.6f, 0.85f, 0.5f, 0.95f, 0.7f, 0.8f, 0.55f};
        setVelocityLane(s, 8, vel8);
        s.arp.humanize = 0.25f;             // micro-timing keeps it from feeling gridded
        // Chaos source also wanders the filter, so timbre drifts under the rhythm
        setModSlot(s, 0, kSrcChaos, kDstAllFltCut, 0.4f, kCurveSCurve, 30.0f);
        setModSlot(s, 1, kSrcRandom, kDstAllResonance, 0.25f, kCurveStepped);
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.4f);
        s.delayEnabled = 1; s.delay.type = 0;   // Digital
        s.delay.sync = 1; s.delay.noteValue = kNote1_16;
        s.delay.mix = 0.22f; s.delay.feedback = 0.35f;
        s.reverbEnabled = 1; s.reverbType = 0;
        s.reverb.size = 0.45f; s.reverb.mix = 0.2f;
        s.global.masterGain = 1.1f;
        presets.push_back(std::move(p));
    }

    // "Tape Shuffle" - triplet arp on genuinely worn tape; the wow drift is slow
    // and unsynced, so the triplet grid never sits perfectly still
    {
        PresetDef p;
        p.name = "Tape Shuffle";
        p.category = "Rhythmic";
        auto& s = p.state;
        s.oscA.type = 1;                    // Wavetable
        s.oscA.waveform = 1; s.oscA.level = 0.75f;
        s.oscB.type = 0; s.oscB.waveform = 1; s.oscB.fineCents = -6.0f;
        s.oscB.level = 0.55f;
        s.mixer.position = 0.45f;
        s.filter.type = 4;                  // Ladder LP
        s.filter.cutoffHz = 2600.0f; s.filter.resonance = 3.40f;
        s.filter.ladderDrive = 5.0f; s.filter.envAmount = 18.0f;
        s.filterEnv.attackMs = 4.0f; s.filterEnv.decayMs = 200.0f;
        s.filterEnv.sustain = 0.35f; s.filterEnv.releaseMs = 160.0f;
        s.ampEnv.attackMs = 5.0f; s.ampEnv.decayMs = 260.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 200.0f;
        // ARP: 1/8 TRIPLETS with swing on top of the triplet grid
        setArpEnabled(s, true); setArpMode(s, kModeUpDown); setTempoSync(s, true);
        setArpRate(s, kNote1_8T); setArpGateLength(s, 72.0f);
        setArpSwing(s, 22.0f);
        s.arp.octaveRange = 2;
        // THE HOOK: heavy tape saturation + wear in the DELAY, fed at a dotted
        // rate. Against a triplet arp a dotted delay is maximally non-aligning:
        // 3-against-2-against-3, and the tape wear smears the collisions.
        s.delayEnabled = 1; s.delay.type = 1;   // Tape
        s.delay.sync = 1; s.delay.noteValue = kNote1_8D;
        s.delay.mix = 0.32f; s.delay.feedback = 0.45f;
        s.delay.tapeSaturation = 0.6f; s.delay.tapeWear = 0.5f;
        s.delay.tapeAge = 0.4f; s.delay.tapeInertiaMs = 420.0f;
        // Tape saturator in the voice too, so the source is warm before the echo
        s.distortion.type = 5;              // Tape Saturator
        s.distortion.drive = 0.35f; s.distortion.mix = 0.45f;
        s.distortion.tapeSaturation = 0.5f; s.distortion.tapeBias = 0.55f;
        s.lfo1.rateHz = 0.18f; s.lfo1.shape = 0; s.lfo1.depth = 0.4f; s.lfo1.sync = 0;
        setModSlot(s, 0, kSrcLFO1, kDstAllFltCut, 0.3f, kCurveSCurve);
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.4f);
        s.modulationType = 3;               // Chorus
        s.chorus.rateHz = 0.3f; s.chorus.depth = 0.35f; s.chorus.mix = 0.25f;
        s.reverbEnabled = 1; s.reverbType = 0;
        s.reverb.size = 0.45f; s.reverb.mix = 0.18f;
        presets.push_back(std::move(p));
    }

    // "Spectral Ratchet" - frozen-spectrum arp: note-tracked freeze (the spectral
    // oscillator now follows pitch) machine-gunned by ratchets into a spectral delay
    {
        PresetDef p;
        p.name = "Spectral Ratchet";
        p.category = "Rhythmic";
        auto& s = p.state;
        s.oscA.type = 8;                    // Spectral Freeze
        s.oscA.spectralTilt = 0.2f; s.oscA.spectralFormant = 0.15f;
        s.oscA.level = 0.8f;
        s.oscB.type = 0; s.oscB.waveform = 0; s.oscB.level = 0.3f;  // sine anchor
        s.mixer.position = 0.3f;
        s.filter.type = 0;                  // SVF LP
        s.filter.cutoffHz = 3200.0f; s.filter.resonance = 2.80f;
        s.ampEnv.attackMs = 3.0f; s.ampEnv.decayMs = 260.0f;
        s.ampEnv.sustain = 0.55f; s.ampEnv.releaseMs = 300.0f;
        // ARP at 1/16
        setArpEnabled(s, true); setArpMode(s, kModeUp); setTempoSync(s, true);
        setArpRate(s, kNote1_16); setArpGateLength(s, 58.0f);
        s.arp.octaveRange = 2;
        // THE HOOK: a 6-long ratchet lane and a 4-long gate lane run at once. Six
        // against four resolves every 12 steps, and neither divides the 16-step
        // bar, so the stutter figure walks around the bar continuously.
        int32_t ratch6[] = {1, 3, 1, 2, 4, 1};
        setRatchetLane(s, 6, ratch6);
        float gate4[] = {1.0f, 0.7f, 0.16f, 0.9f};   // 0.16 not 0.0: see FR-014
        setGateLane(s, 4, gate4);
        s.arp.ratchetSwing = 60.0f;
        // Arp pitch pushes spectral tilt: high notes are brighter frozen spectra
        setModSlot(s, 0, kSrcArpPitch, kDstAllSpecTilt, 0.5f, kCurveLinear);
        s.lfo1.rateHz = 0.25f; s.lfo1.shape = 1; s.lfo1.depth = 0.5f; s.lfo1.sync = 1;
        s.lfo1Ext.noteValue = 19;
        setModSlot(s, 1, kSrcLFO1, kDstAllMorphPos, 0.35f, kCurveSCurve);
        s.delayEnabled = 1; s.delay.type = 4;   // Spectral
        s.delay.sync = 1; s.delay.noteValue = kNote1_8;
        s.delay.mix = 0.3f; s.delay.feedback = 0.42f;
        s.reverbEnabled = 1; s.reverbType = 1;  // Hall
        s.reverb.size = 0.6f; s.reverb.mix = 0.25f; s.reverb.damping = 0.4f;
        s.global.masterGain = 1.2f;
        presets.push_back(std::move(p));
    }

    // "Granular Cascade" - three-octave particle arp poured into a granular delay;
    // the delay's grain rate is a second, unsynced rhythm under the note grid
    {
        PresetDef p;
        p.name = "Granular Cascade";
        p.category = "Rhythmic";
        auto& s = p.state;
        s.oscA.type = 6;                    // Particle
        s.oscA.particleDensity = 22.0f; s.oscA.particleScatter = 4.0f;
        s.oscA.particleLifetime = 140.0f; s.oscA.particleDrift = 0.3f;
        s.oscA.particleEnvType = 1; s.oscA.level = 0.8f;
        s.oscB.type = 0; s.oscB.waveform = 4; s.oscB.level = 0.35f;
        s.mixer.position = 0.32f;
        s.filter.type = 0;                  // SVF LP
        s.filter.cutoffHz = 2800.0f; s.filter.resonance = 3.20f;
        s.filter.envAmount = 16.0f;
        s.filterEnv.attackMs = 3.0f; s.filterEnv.decayMs = 180.0f;
        s.filterEnv.sustain = 0.35f; s.filterEnv.releaseMs = 200.0f;
        s.ampEnv.attackMs = 4.0f; s.ampEnv.decayMs = 240.0f;
        s.ampEnv.sustain = 0.6f; s.ampEnv.releaseMs = 260.0f;
        // ARP: 1/16 over THREE octaves, interleaved so octaves weave
        setArpEnabled(s, true); setArpMode(s, kModeUp); setTempoSync(s, true);
        setArpRate(s, kNote1_16); setArpGateLength(s, 68.0f);
        s.arp.octaveRange = 3; s.arp.octaveMode = 1;   // Interleaved
        // THE HOOK: the granular delay runs ~30 grains/sec, which is unrelated to
        // the note clock. Each arp note is shattered into a grain burst that keeps
        // sounding across the following steps -- a second rhythm at audio-adjacent
        // rate layered under the arp's musical one.
        s.delayEnabled = 1; s.delay.type = 3;   // Granular
        s.delay.sync = 1; s.delay.noteValue = kNote1_8D;
        s.delay.mix = 0.35f; s.delay.feedback = 0.42f;
        s.delay.granularSizeMs = 55.0f; s.delay.granularDensity = 30.0f;
        s.delay.granularPitchSpray = 0.3f; s.delay.granularPanSpray = 0.7f;
        s.delay.granularJitter = 0.35f; s.delay.granularReverseProb = 0.2f;
        float vel6[] = {1.0f, 0.65f, 0.8f, 0.55f, 0.9f, 0.7f};
        setVelocityLane(s, 6, vel6);        // 6 against a 16-step bar
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.4f);
        s.lfo1.rateHz = 0.22f; s.lfo1.shape = 2; s.lfo1.depth = 0.5f; s.lfo1.sync = 0;
        setModSlot(s, 0, kSrcLFO1, kDstEffectMix, 0.3f, kCurveSCurve);
        s.reverbEnabled = 1; s.reverbType = 1;  // Hall
        s.reverb.size = 0.65f; s.reverb.mix = 0.28f; s.reverb.damping = 0.4f;
        s.global.masterGain = 1.25f;
        presets.push_back(std::move(p));
    }

    // "Acid Gate" - a 303 line with a trance gate over it. The acid presets in the
    // Arp categories run the line bare; here the gate chops the slides mid-glide,
    // which turns legato squelch into stabs without changing a single arp note
    {
        PresetDef p;
        p.name = "Acid Gate";
        p.category = "Rhythmic";
        auto& s = p.state;
        s.oscA.type = 0; s.oscA.waveform = 1; s.oscA.level = 0.9f;   // single saw
        s.oscB.level = 0.0f;
        s.mixer.position = 0.0f;
        s.filter.type = 4;                  // Ladder LP - the 303 filter
        s.filter.cutoffHz = 620.0f; s.filter.resonance = 9.50f;      // squelch
        s.filter.ladderSlope = 4; s.filter.ladderDrive = 8.0f;
        s.filter.envAmount = 34.0f;
        s.filterEnv.attackMs = 1.0f; s.filterEnv.decayMs = 190.0f;
        s.filterEnv.sustain = 0.12f; s.filterEnv.releaseMs = 140.0f;
        s.filterEnv.decayCurve = 0.35f;
        s.ampEnv.attackMs = 1.0f; s.ampEnv.decayMs = 200.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 90.0f;
        // Mono + legato glide = the slide
        s.global.voiceMode = 1; s.global.polyphony = 1;
        s.monoMode.priority = 0; s.monoMode.legato = 1;
        s.monoMode.portamentoTimeMs = 60.0f; s.monoMode.portaMode = 1;
        // ARP: 1/16 as-played, with the SLIDE/ACCENT modifier lane doing 303 duty
        setArpEnabled(s, true); setArpMode(s, kModeAsPlayed); setTempoSync(s, true);
        setArpRate(s, kNote1_16); setArpGateLength(s, 88.0f);   // long = notes join
        s.arp.octaveRange = 2;
        int32_t mod8[] = {kStepActive | kStepAccent, kStepActive,
                          kStepActive | kStepSlide,  kStepActive,
                          kStepActive | kStepAccent, kStepActive | kStepSlide,
                          kStepActive,               kStepActive | kStepAccent};
        setModifierLane(s, 8, mod8, 40, 55.0f);
        // THE HOOK: a 6-step gate at 1/16 over the 8-step modifier lane. The gate
        // cuts the line mid-slide, and because 6 and 8 resolve only every 24 steps
        // a different slide gets truncated each time round.
        s.tranceGate.enabled = 1; s.tranceGate.numSteps = 6;
        s.tranceGate.tempoSync = 1; s.tranceGate.noteValue = kNote1_16;
        s.tranceGate.depth = 0.95f;
        s.tranceGate.attackMs = 1.0f; s.tranceGate.releaseMs = 8.0f;
        float tg[] = {1,1,0,1, 0,1};
        for (int i = 0; i < 6; ++i) s.tranceGate.stepLevels[i] = tg[i];
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.55f);  // accents open up
        s.distortion.type = 5;              // Tape Saturator for 303 grit
        s.distortion.drive = 0.45f; s.distortion.mix = 0.4f;
        s.delayEnabled = 1; s.delay.type = 0;
        s.delay.sync = 1; s.delay.noteValue = kNote1_8D;
        s.delay.mix = 0.2f; s.delay.feedback = 0.35f;
        presets.push_back(std::move(p));
    }

    // "Polymeter Pulse" - the deliberate extreme: velocity 5, gate 7, ratchet 3,
    // all running at once on one clock. Full cycle is 105 steps
    {
        PresetDef p;
        p.name = "Polymeter Pulse";
        p.category = "Rhythmic";
        auto& s = p.state;
        // Plain square + saw: the patch is simple so the RHYTHM is unmistakable
        s.oscA.type = 0; s.oscA.waveform = 3; s.oscA.pulseWidth = 0.4f;
        s.oscA.level = 0.75f;
        s.oscB.type = 0; s.oscB.waveform = 1; s.oscB.fineCents = 5.0f;
        s.oscB.level = 0.5f;
        s.mixer.position = 0.45f;
        s.filter.type = 0;                  // SVF LP
        s.filter.cutoffHz = 2400.0f; s.filter.resonance = 4.80f;
        s.filter.envAmount = 22.0f;
        s.filterEnv.attackMs = 1.0f; s.filterEnv.decayMs = 130.0f;
        s.filterEnv.sustain = 0.25f; s.filterEnv.releaseMs = 110.0f;
        s.ampEnv.attackMs = 2.0f; s.ampEnv.decayMs = 190.0f;
        s.ampEnv.sustain = 0.65f; s.ampEnv.releaseMs = 140.0f;
        setArpEnabled(s, true); setArpMode(s, kModeUp); setTempoSync(s, true);
        setArpRate(s, kNote1_16); setArpGateLength(s, 70.0f);
        s.arp.octaveRange = 2;
        // THE HOOK: three coprime lane lengths. 5 x 7 x 3 = 105 steps before the
        // combination repeats -- over six bars of 16ths from entirely static data.
        float vel5[] = {1.0f, 0.55f, 0.85f, 0.6f, 0.95f};
        setVelocityLane(s, 5, vel5);
        float gate7[] = {1.0f, 0.8f, 0.0f, 1.0f, 0.6f, 0.0f, 0.9f};
        setGateLane(s, 7, gate7);
        int32_t ratch3[] = {1, 1, 2};
        setRatchetLane(s, 3, ratch3);
        // A fourth period: the condition lane is 4 long, coprime with 5 and 7
        int32_t cond4[] = {kCondAlways, kCondAlways, kCondProb75, kCondAlways};
        setConditionLane(s, 4, cond4, 0);
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.5f);
        s.lfo1.rateHz = 0.15f; s.lfo1.shape = 1; s.lfo1.depth = 0.45f; s.lfo1.sync = 1;
        s.lfo1Ext.noteValue = 19;
        setModSlot(s, 0, kSrcLFO1, kDstAllFltCut, 0.3f, kCurveSCurve);
        s.modulationType = 1;               // Phaser
        s.phaser.rateHz = 0.2f; s.phaser.depth = 0.5f; s.phaser.feedback = 0.35f;
        s.phaser.mix = 0.3f; s.phaser.stages = 2;
        s.delayEnabled = 1; s.delay.type = 2;
        s.delay.sync = 1; s.delay.noteValue = kNote1_8;
        s.delay.mix = 0.22f; s.delay.feedback = 0.32f;
        s.delay.pingPongWidth = 140.0f;
        presets.push_back(std::move(p));
    }

    // "Noise Tick" - pitched noise ticks through a high-pass: an arp used as a
    // percussion sequencer rather than a melodic one
    {
        PresetDef p;
        p.name = "Noise Tick";
        p.category = "Rhythmic";
        auto& s = p.state;
        s.oscA.type = 9; s.oscA.noiseColor = 3; s.oscA.level = 0.6f;  // blue noise
        s.oscB.type = 0; s.oscB.waveform = 0; s.oscB.level = 0.4f;    // sine pitch anchor
        s.mixer.position = 0.5f;
        // High-pass with a strong resonant peak: the noise becomes a pitched tick
        s.filter.type = 1;                  // SVF HP
        s.filter.cutoffHz = 1800.0f; s.filter.resonance = 8.20f;
        s.filter.envAmount = -18.0f;        // sweeps DOWN, so each tick chirps
        s.filter.keyTrack = 0.6f;
        s.filterEnv.attackMs = 0.5f; s.filterEnv.decayMs = 60.0f;
        s.filterEnv.sustain = 0.0f; s.filterEnv.releaseMs = 50.0f;
        // Very short amp env = a tick, not a note
        s.ampEnv.attackMs = 0.5f; s.ampEnv.decayMs = 70.0f;
        s.ampEnv.sustain = 0.0f; s.ampEnv.releaseMs = 60.0f;
        // ARP: fast 16ths, tiny gate
        setArpEnabled(s, true); setArpMode(s, kModeUp); setTempoSync(s, true);
        setArpRate(s, kNote1_16); setArpGateLength(s, 18.0f);   // staccato ticks
        s.arp.octaveRange = 3;
        // THE HOOK: Euclidean 9-in-16 gives a dense, uneven hi-hat-like pattern,
        // and a 5-long velocity lane makes the accent walk through it.
        setEuclidean(s, true, 9, 16, 0);
        float vel5[] = {1.0f, 0.45f, 0.7f, 0.4f, 0.85f};
        setVelocityLane(s, 5, vel5);
        s.arp.humanize = 0.2f;
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.5f);
        s.delayEnabled = 1; s.delay.type = 2;   // PingPong scatters the ticks wide
        s.delay.sync = 1; s.delay.noteValue = kNote1_16D;
        s.delay.mix = 0.26f; s.delay.feedback = 0.38f;
        s.delay.pingPongWidth = 170.0f; s.delay.pingPongCrossFeed = 0.75f;
        s.reverbEnabled = 1; s.reverbType = 0;  // Plate
        s.reverb.size = 0.35f; s.reverb.mix = 0.18f;
        s.global.masterGain = 1.5f;             // short ticks measure quiet
        presets.push_back(std::move(p));
    }

    // "Fold Groove" - wavefolder whose fold depth is driven by a synced LFO at a
    // rate the arp does not divide: the timbre pulses on its own meter
    {
        PresetDef p;
        p.name = "Fold Groove";
        p.category = "Rhythmic";
        auto& s = p.state;
        s.oscA.type = 0; s.oscA.waveform = 4; s.oscA.level = 0.8f;   // triangle folds best
        s.oscB.type = 0; s.oscB.waveform = 0; s.oscB.tuneSemitones = -12.0f;
        s.oscB.level = 0.45f;
        s.mixer.position = 0.4f;
        s.filter.type = 0;                  // SVF LP
        s.filter.cutoffHz = 3000.0f; s.filter.resonance = 3.10f;
        s.filter.envAmount = 14.0f;
        s.filterEnv.attackMs = 3.0f; s.filterEnv.decayMs = 170.0f;
        s.filterEnv.sustain = 0.4f; s.filterEnv.releaseMs = 150.0f;
        s.ampEnv.attackMs = 3.0f; s.ampEnv.decayMs = 230.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 190.0f;
        // Wavefolder: harmonics appear and vanish as drive moves
        s.distortion.type = 4;              // Wavefolder
        s.distortion.drive = 0.45f; s.distortion.mix = 0.65f;
        s.distortion.foldType = 1;          // Sine fold - smooth harmonic bloom
        // ARP: straight 1/8
        setArpEnabled(s, true); setArpMode(s, kModeUpDown); setTempoSync(s, true);
        setArpRate(s, kNote1_8); setArpGateLength(s, 80.0f);
        s.arp.octaveRange = 2;
        // THE HOOK: the fold depth is driven by a synced LFO at a DOTTED QUARTER,
        // which is 3/8 -- against 1/8 arp steps the timbre peak lands on a
        // different note of the run every cycle. Level stays put; harmonics move.
        s.lfo1.shape = 0; s.lfo1.depth = 1.0f; s.lfo1.sync = 1;
        s.lfo1Ext.noteValue = 14;           // dotted quarter
        setModSlot(s, 0, kSrcLFO1, kDstEffectMix, 0.5f, kCurveSCurve);
        setVoiceRoute(s, 0, kVSrcVoiceLFO, kVDstDistDrive, 0.4f);
        setVoiceRoute(s, 1, kVSrcVelocity, kVDstDistDrive, 0.35f);
        // A second synced LFO at 1/2 gives a slower, also-non-dividing swell
        s.lfo2.shape = 1; s.lfo2.depth = 0.8f; s.lfo2.sync = 1;
        s.lfo2Ext.noteValue = 16;           // half note
        setModSlot(s, 1, kSrcLFO2, kDstAllFltCut, 0.35f, kCurveSCurve);
        s.modulationType = 2;               // Flanger emphasises the fold harmonics
        s.flanger.rateHz = 0.25f; s.flanger.depth = 0.55f; s.flanger.feedback = 0.4f;
        s.flanger.mix = 0.35f; s.flanger.stereoSpread = 100.0f;
        s.reverbEnabled = 1; s.reverbType = 0;
        s.reverb.size = 0.45f; s.reverb.mix = 0.2f;
        presets.push_back(std::move(p));
    }

    // "Ring Sequence" - the PITCH lane writes a melody the player does not: hold
    // one note and the arp transposes it through a fixed interval sequence
    {
        PresetDef p;
        p.name = "Ring Sequence";
        p.category = "Rhythmic";
        auto& s = p.state;
        s.oscA.type = 0; s.oscA.waveform = 3; s.oscA.pulseWidth = 0.45f;
        s.oscA.level = 0.8f;
        s.oscB.type = 0; s.oscB.waveform = 0; s.oscB.level = 0.4f;
        s.mixer.position = 0.45f;
        s.filter.type = 0;                  // SVF LP
        s.filter.cutoffHz = 2600.0f; s.filter.resonance = 4.10f;
        s.filter.envAmount = 20.0f;
        s.filterEnv.attackMs = 1.0f; s.filterEnv.decayMs = 140.0f;
        s.filterEnv.sustain = 0.3f; s.filterEnv.releaseMs = 110.0f;
        s.ampEnv.attackMs = 2.0f; s.ampEnv.decayMs = 200.0f;
        s.ampEnv.sustain = 0.65f; s.ampEnv.releaseMs = 150.0f;
        // Note-tracked ring mod: metallic, but stays in key
        s.distortion.type = 6;              // RingModulator
        s.distortion.drive = 0.35f; s.distortion.mix = 0.4f;
        s.distortion.ringFreqMode = 1;      // NoteTrack
        s.distortion.ringRatio = 0.1667f;   // ~3:1
        s.distortion.ringWaveform = 0;
        // ARP: as-played at 1/16 -- one held note is enough
        setArpEnabled(s, true); setArpMode(s, kModeAsPlayed); setTempoSync(s, true);
        setArpRate(s, kNote1_16); setArpGateLength(s, 62.0f);
        s.arp.octaveRange = 1;
        // THE HOOK: a 9-step PITCH lane over a 16-step bar. The melody it writes
        // is 9 long, so it rotates through the bar and takes 9 bars to come home.
        // Intervals are a minor-pentatonic shape, so it stays musical while moving.
        int32_t pitch9[] = {0, 3, 5, 7, 10, 7, 5, 3, 12};
        setPitchLane(s, 9, pitch9);
        // 4 against 9: the clipped step rotates through the melody. 0.15 rather
        // than 0.0 -- gate duration clamps to 1 sample, so 0.0 clicks (FR-014).
        float gate4[] = {1.0f, 0.75f, 0.9f, 0.15f};
        setGateLane(s, 4, gate4);
        setModSlot(s, 0, kSrcArpPitch, kDstEffectMix, 0.4f, kCurveLinear);
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.4f);
        s.delayEnabled = 1; s.delay.type = 0;
        s.delay.sync = 1; s.delay.noteValue = kNote1_8;
        s.delay.mix = 0.24f; s.delay.feedback = 0.36f;
        s.reverbEnabled = 1; s.reverbType = 0;
        s.reverb.size = 0.42f; s.reverb.mix = 0.2f;
        presets.push_back(std::move(p));
    }

    // "Comb Runner" - key-tracked comb resonance turns every arp step into a
    // struck tube; the flanger sweeps at a rate unrelated to the note grid
    {
        PresetDef p;
        p.name = "Comb Runner";
        p.category = "Rhythmic";
        auto& s = p.state;
        s.oscA.type = 0; s.oscA.waveform = 1; s.oscA.level = 0.7f;
        s.oscB.type = 9; s.oscB.noiseColor = 0; s.oscB.level = 0.2f;  // white edge
        s.mixer.position = 0.35f;
        s.filter.type = 6;                  // Comb
        s.filter.cutoffHz = 330.0f; s.filter.resonance = 6.80f;
        s.filter.combDamping = 0.28f; s.filter.keyTrack = 1.0f;
        s.ampEnv.attackMs = 2.0f; s.ampEnv.decayMs = 280.0f;
        s.ampEnv.sustain = 0.45f; s.ampEnv.releaseMs = 240.0f;
        setArpEnabled(s, true); setArpMode(s, kModeUp); setTempoSync(s, true);
        setArpRate(s, kNote1_16); setArpGateLength(s, 52.0f);
        s.arp.octaveRange = 2;
        // THE HOOK: an 11-step velocity lane. Eleven is coprime with every common
        // bar length, so the comb's excitation strength -- and therefore how hard
        // the tube rings -- never lands the same way twice inside a phrase.
        float vel11[] = {1.0f, 0.5f, 0.8f, 0.6f, 0.95f, 0.45f,
                         0.85f, 0.7f, 0.55f, 0.9f, 0.65f};
        setVelocityLane(s, 11, vel11);
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltRes, 0.45f);
        setVoiceRoute(s, 1, kVSrcKeyTrack, kVDstFltCut, 0.3f);
        // Flanger at a free (unsynced) rate: a third period against the grid
        s.modulationType = 2;               // Flanger
        s.flanger.rateHz = 0.17f; s.flanger.depth = 0.65f;
        s.flanger.feedback = 0.55f; s.flanger.mix = 0.4f;
        s.flanger.stereoSpread = 120.0f;
        s.lfo1.rateHz = 0.28f; s.lfo1.shape = 1; s.lfo1.depth = 0.45f; s.lfo1.sync = 0;
        setModSlot(s, 0, kSrcLFO1, kDstAllResonance, 0.3f, kCurveSCurve);
        s.delayEnabled = 1; s.delay.type = 1;   // Tape
        s.delay.sync = 1; s.delay.noteValue = kNote1_8T;
        s.delay.mix = 0.24f; s.delay.feedback = 0.38f;
        s.delay.tapeSaturation = 0.4f;
        s.reverbEnabled = 1; s.reverbType = 1;
        s.reverb.size = 0.55f; s.reverb.mix = 0.24f; s.reverb.damping = 0.45f;
        s.global.masterGain = 1.3f;
        presets.push_back(std::move(p));
    }

    // "PWM Strut" - dotted-8th arp where pulse width is swept by a synced LFO on
    // a different note value; the hollow/full timbre cycle walks across the strut
    {
        PresetDef p;
        p.name = "PWM Strut";
        p.category = "Rhythmic";
        auto& s = p.state;
        s.oscA.type = 0; s.oscA.waveform = 3; s.oscA.pulseWidth = 0.5f;
        s.oscA.level = 0.8f;
        s.oscB.type = 0; s.oscB.waveform = 3; s.oscB.pulseWidth = 0.3f;
        s.oscB.fineCents = 8.0f; s.oscB.level = 0.6f;
        s.mixer.position = 0.5f;
        s.filter.type = 4;                  // Ladder LP
        s.filter.cutoffHz = 2900.0f; s.filter.resonance = 3.90f;
        s.filter.ladderDrive = 4.0f; s.filter.envAmount = 18.0f;
        s.filterEnv.attackMs = 3.0f; s.filterEnv.decayMs = 180.0f;
        s.filterEnv.sustain = 0.35f; s.filterEnv.releaseMs = 140.0f;
        s.ampEnv.attackMs = 3.0f; s.ampEnv.decayMs = 220.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 170.0f;
        // ARP: dotted 8ths = the strut
        setArpEnabled(s, true); setArpMode(s, kModeUp); setTempoSync(s, true);
        setArpRate(s, kNote1_8D); setArpGateLength(s, 76.0f);
        setArpSwing(s, 15.0f);
        s.arp.octaveRange = 2;
        // THE HOOK: PWM is driven by a synced LFO at 1/4. Arp steps are 3/16 and
        // the LFO cycle is 4/16, so the point in the PWM sweep where each note is
        // struck advances every step -- classic PWM, but phase-locked to nothing.
        s.lfo1.shape = 0; s.lfo1.depth = 1.0f; s.lfo1.sync = 1;
        s.lfo1Ext.noteValue = 13;           // 1/4
        setModSlot(s, 0, kSrcLFO1, kDstAllMorphPos, 0.6f, kCurveSCurve);
        setVoiceRoute(s, 0, kVSrcVoiceLFO, kVDstMorphPos, 0.45f);
        setVoiceRoute(s, 1, kVSrcVelocity, kVDstFltCut, 0.4f);
        // Synced phaser at yet another value keeps the stereo image moving
        s.modulationType = 1;               // Phaser
        s.phaser.rateHz = 0.22f; s.phaser.depth = 0.55f;
        s.phaser.feedback = 0.45f; s.phaser.mix = 0.35f; s.phaser.stages = 3;
        s.phaser.centerFreqHz = 800.0f;
        s.delayEnabled = 1; s.delay.type = 2;
        s.delay.sync = 1; s.delay.noteValue = kNote1_4;
        s.delay.mix = 0.22f; s.delay.feedback = 0.34f;
        s.delay.pingPongWidth = 145.0f;
        s.reverbEnabled = 1; s.reverbType = 0;
        s.reverb.size = 0.45f; s.reverb.mix = 0.2f;
        presets.push_back(std::move(p));
    }

    // "Sync Ratchet" - hard-sync screams cut into bursts; the accent lane and the
    // ratchet lane are different lengths so emphasis and subdivision drift apart
    {
        PresetDef p;
        p.name = "Sync Ratchet";
        p.category = "Rhythmic";
        auto& s = p.state;
        s.oscA.type = 3;                    // Sync
        s.oscA.waveform = 1; s.oscA.syncRatio = 3.0f;
        s.oscA.syncWaveform = 1; s.oscA.syncMode = 0; s.oscA.syncAmount = 1.0f;
        s.oscA.level = 0.85f;
        s.oscB.type = 0; s.oscB.waveform = 1; s.oscB.tuneSemitones = -12.0f;
        s.oscB.level = 0.4f;
        s.mixer.position = 0.35f;
        s.filter.type = 4;                  // Ladder LP
        s.filter.cutoffHz = 1900.0f; s.filter.resonance = 5.40f;
        s.filter.ladderDrive = 6.0f; s.filter.envAmount = 28.0f;
        s.filter.keyTrack = 0.4f;
        s.filterEnv.attackMs = 1.0f; s.filterEnv.decayMs = 120.0f;
        s.filterEnv.sustain = 0.22f; s.filterEnv.releaseMs = 100.0f;
        s.ampEnv.attackMs = 1.0f; s.ampEnv.decayMs = 170.0f;
        s.ampEnv.sustain = 0.6f; s.ampEnv.releaseMs = 120.0f;
        setArpEnabled(s, true); setArpMode(s, kModeUp); setTempoSync(s, true);
        setArpRate(s, kNote1_16); setArpGateLength(s, 58.0f);
        s.arp.octaveRange = 2;
        // THE HOOK: 4-long ratchet lane vs 7-long modifier (accent) lane. The
        // rolled step and the accented step coincide only every 28 steps, so the
        // pattern alternates between "loud roll" and "quiet roll" over 7 bars.
        int32_t ratch4[] = {1, 2, 4, 1};
        setRatchetLane(s, 4, ratch4);
        int32_t mod7[] = {kStepActive | kStepAccent, kStepActive, kStepActive,
                          kStepActive | kStepAccent, kStepActive,
                          kStepActive | kStepAccent, kStepActive};
        setModifierLane(s, 7, mod7, 45, 40.0f);
        s.arp.ratchetSwing = 62.0f;
        // Sync sweep follows the arp's pitch: higher notes scream harder
        setModSlot(s, 0, kSrcArpPitch, kDstAllFltCut, 0.4f, kCurveExp);
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.5f);
        setVoiceRoute(s, 1, kVSrcEnv3, kVDstOscAPitch, 0.3f);   // per-note sync zap
        s.modEnv.attackMs = 1.0f; s.modEnv.decayMs = 90.0f;
        s.modEnv.sustain = 0.0f; s.modEnv.releaseMs = 80.0f;
        s.distortion.type = 5;              // Tape Saturator tames the sync edge
        s.distortion.drive = 0.4f; s.distortion.mix = 0.4f;
        s.delayEnabled = 1; s.delay.type = 2;
        s.delay.sync = 1; s.delay.noteValue = kNote1_8D;
        s.delay.mix = 0.24f; s.delay.feedback = 0.36f;
        s.delay.pingPongWidth = 150.0f;
        s.reverbEnabled = 1; s.reverbType = 0;
        s.reverb.size = 0.4f; s.reverb.mix = 0.16f;
        s.global.masterGain = 0.9f;         // sync + drive is a hot source
        presets.push_back(std::move(p));
    }

    // "Vapor Arp" - the slow one. Quarter-note arp, long gate, shimmer hall: the
    // batch needed a preset that proves an arp does not have to be busy
    {
        PresetDef p;
        p.name = "Vapor Arp";
        p.category = "Rhythmic";
        auto& s = p.state;
        s.oscA.type = 1;                    // Wavetable
        s.oscA.waveform = 2; s.oscA.level = 0.7f;
        s.oscB.type = 4;                    // Additive shimmer partner
        s.oscB.additivePartials = 16; s.oscB.additiveTilt = -0.5f;
        s.oscB.level = 0.5f;
        s.mixer.position = 0.5f;
        s.global.polyphony = 8; s.global.width = 1.4f; s.global.spread = 0.35f;
        s.filter.type = 0;                  // SVF LP
        s.filter.cutoffHz = 3400.0f; s.filter.resonance = 2.20f;
        s.filter.envAmount = 12.0f;
        s.filterEnv.attackMs = 120.0f; s.filterEnv.decayMs = 600.0f;
        s.filterEnv.sustain = 0.5f; s.filterEnv.releaseMs = 800.0f;
        // Slow attack: notes bloom rather than strike
        s.ampEnv.attackMs = 180.0f; s.ampEnv.decayMs = 700.0f;
        s.ampEnv.sustain = 0.7f; s.ampEnv.releaseMs = 1200.0f;
        // ARP: QUARTER notes, near-legato, three octaves
        setArpEnabled(s, true); setArpMode(s, kModeUpDown); setTempoSync(s, true);
        setArpRate(s, kNote1_4); setArpGateLength(s, 115.0f);   // >100% = overlap
        s.arp.octaveRange = 3; s.arp.octaveMode = 1;
        // THE HOOK: a 3-step gate lane against the quarter-note arp. With 3/4
        // against a 4/4 bar the soft dip walks around the bar, so a slow pad
        // acquires a drifting internal pulse it would never have on its own.
        float gate3[] = {1.0f, 0.55f, 0.8f};
        setGateLane(s, 3, gate3);
        float vel5[] = {0.9f, 0.65f, 1.0f, 0.7f, 0.8f};
        setVelocityLane(s, 5, vel5);        // 5 vs 3: 15-step combined cycle
        s.lfo1.shape = 0; s.lfo1.depth = 0.6f; s.lfo1.sync = 1;
        s.lfo1Ext.noteValue = 19;           // whole-note morph drift
        setModSlot(s, 0, kSrcLFO1, kDstAllMorphPos, 0.4f, kCurveSCurve);
        setModSlot(s, 1, kSrcArpPitch, kDstAllSpecTilt, 0.3f, kCurveLinear);
        setVoiceRoute(s, 0, kVSrcVelocity, kVDstFltCut, 0.3f);
        s.modulationType = 3;               // Chorus
        s.chorus.rateHz = 0.25f; s.chorus.depth = 0.5f; s.chorus.mix = 0.35f;
        constexpr int32_t kNote1_4D = 14;   // dotted quarter (dropdown index 14)
        s.delayEnabled = 1; s.delay.type = 4;   // Spectral
        s.delay.sync = 1; s.delay.noteValue = kNote1_4D;
        s.delay.mix = 0.3f; s.delay.feedback = 0.45f;
        s.reverbEnabled = 1; s.reverbType = 1;  // Hall
        s.reverb.size = 0.8f; s.reverb.mix = 0.38f; s.reverb.damping = 0.3f;
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
              << " Ruinae factory presets..." << std::endl;

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
