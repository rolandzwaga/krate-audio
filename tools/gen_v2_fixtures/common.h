#pragma once

// ==============================================================================
// gen_v2_fixtures common types (spec 142, Phase 1)
// ==============================================================================
// Plugin-agnostic types shared between main.cpp and the per-plugin generator
// translation units. NOTHING in this header may include plugin-specific headers
// (Gradus and Ruinae each have their own plugin_ids.h with conflicting enum
// values, so Gradus and Ruinae generator code MUST live in separate .cpp files).
// ==============================================================================

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace KrateFixtures {

// 60-second deterministic test sequence parameters.
inline constexpr double  kSampleRate = 44100.0;
inline constexpr int32_t kBlockSize  = 512;
inline constexpr int64_t kTotalSamples =
    static_cast<int64_t>(60.0 * kSampleRate);
inline constexpr int kNumBlocks =
    static_cast<int>(kTotalSamples / kBlockSize);

struct ScheduledEvent {
    int64_t sampleTime;
    bool    isNoteOn;
    int16_t pitch;
    float   velocity;  // ignored for note-off
};

inline std::vector<ScheduledEvent> makeStandardMidiSequence() {
    // notes 60/64/67 held 5s each + chord 60+64+67 held for 30s,
    // then 15s of silence (no input MIDI). Matches contracts/state-stream-v3.md.
    std::vector<ScheduledEvent> seq;
    const int64_t s = static_cast<int64_t>(kSampleRate);
    seq.push_back({0LL,         true,  60, 0.8f});
    seq.push_back({5LL  * s,    false, 60, 0.0f});
    seq.push_back({5LL  * s,    true,  64, 0.8f});
    seq.push_back({10LL * s,    false, 64, 0.0f});
    seq.push_back({10LL * s,    true,  67, 0.8f});
    seq.push_back({15LL * s,    false, 67, 0.0f});
    seq.push_back({15LL * s,    true,  60, 0.8f});
    seq.push_back({15LL * s,    true,  64, 0.8f});
    seq.push_back({15LL * s,    true,  67, 0.8f});
    seq.push_back({45LL * s,    false, 60, 0.0f});
    seq.push_back({45LL * s,    false, 64, 0.0f});
    seq.push_back({45LL * s,    false, 67, 0.0f});
    return seq;
}

struct CapturedMidi {
    int64_t absoluteSample;
    bool    isNoteOn;
    int16_t pitch;
    int     velocity;  // 0..127
};

void writeGoldenMidi(const std::filesystem::path& outPath,
                     const std::vector<CapturedMidi>& events);

std::string sanitizeForFilename(const std::string& in);

// Per-plugin entry points (each implemented in its own translation unit so
// that the two plugin namespaces' colliding plugin_ids.h enums don't share
// the same TU).
void generateGradusArtifacts(const std::filesystem::path& fixturesDir);
void generateRuinaeArtifacts(const std::filesystem::path& presetsDir,
                             const std::filesystem::path& fixturesDir);

}  // namespace KrateFixtures
