// ==============================================================================
// Profundum Phase 12 (specs/profundum-phase12-ui) - T012: the engine / voice display read path
// ==============================================================================
// Profundum_EngineDisplayAccessorNeutral (SC-022 engine arm; FR-075, FR-095; plan S4.1, S5.1, S6.2).
//
// Two heap engines A and B, prepared identically (48 kHz, default config), the same event script, and the same
// recording observer type (ProfundumTest::VoiceEngineObserver) attached to both, so the observer's presence is
// symmetric. A renders host blocks of a pseudo-random size sequence (LCG seed 0x1234, sizes 1 ... 517), split at
// every event's sample (C-9). B renders the same slices, each cut into sub-calls of min(remaining,
// 32 - controlGridPhase()), and reads every accessor after each sub-call; after a sub-call that ends on a grid
// point it compares the voices' interval scratch with the observer's taps.
//
// Every equality is same-binary, same-process: two live renders (memcmp) or a live render against the observer's
// copy of the same voice's scratch (bit-pattern compare). No float golden.
//
// TU-local names carry the `eda` / `Eda` / `kEda` prefix inside an anonymous namespace (ODR sweep: 0 hits).
// ==============================================================================

#include <krate/dsp/systems/morph_state.h>
#include <krate/dsp/systems/bass_morph_engine.h>
#include <krate/dsp/systems/profundum_engine.h>
#include <krate/dsp/systems/profundum_engine_observer.h>
#include <krate/dsp/systems/profundum_voice.h>

#include "bass_morph_state_helpers.h"
#include "profundum_voice_engine_test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <span>
#include <vector>

namespace {

namespace edaPt = Krate::DSP::ProfundumTest;
namespace edaBmt = Krate::DSP::BassMorphTest;

using EdaEngine = Krate::DSP::ProfundumEngine;
using EdaVoice = Krate::DSP::ProfundumVoice;
using EdaKind = edaPt::VoiceEngineEventKind;
using EdaMode = Krate::DSP::ProfundumEngine::VoiceMode;

constexpr double kEdaFs = 48000.0;
constexpr std::size_t kEdaGrid = EdaEngine::kControlInterval;
constexpr std::uint64_t kEdaTotal = 48000;
constexpr std::uint32_t kEdaLcgSeed = 0x1234u;
constexpr std::size_t kEdaMaxBlock = 517;
constexpr std::size_t kEdaOffGridCut = 17;

// The MONO script's notes and times.
constexpr std::uint8_t kEdaMonoNote = edaPt::kVoiceEngineNoteE1;   // 28
constexpr std::uint64_t kEdaMonoOff = 24000;

// The POLY script's notes and times (every time but the polyphony change is a grid point; 21000 applies at 21024).
constexpr std::uint8_t kEdaPolyN1 = 28;
constexpr std::uint8_t kEdaPolyN2 = 33;
constexpr std::uint8_t kEdaPolyN3 = 40;
constexpr std::uint8_t kEdaPolyN4 = 45;
constexpr std::uint64_t kEdaPolyOn2 = 4800;
constexpr std::uint64_t kEdaPolyOn3 = 9600;
constexpr std::uint64_t kEdaPolyOff2 = 19200;
constexpr std::uint64_t kEdaPolyCount2 = 21000;
constexpr std::uint64_t kEdaPolyCount2Applied = 21024;   // the first grid point at or after 21000 (C-9)
constexpr std::uint64_t kEdaPolyOn4 = 24000;

enum class EdaCut : std::uint8_t { Grid, OffGrid };

/// Per-run tallies (counted rather than CHECKed per sample, so a failure reports one number, not thousands).
struct EdaRun {
    std::vector<float> aL, aR, bL, bR;
    float peak = 0.0f;
    std::size_t subCalls = 0;
    std::size_t clockFailures = 0;
    std::size_t modeFailures = 0;
    std::size_t noteFailures = 0;
    std::size_t boundsFailures = 0;
    std::size_t intervals = 0;
    std::size_t voiceTapChecks = 0;
    std::size_t tapLengthFailures = 0;
    std::size_t tapMismatches = 0;
    std::size_t maxVoicesRendered = 0;
    std::size_t offGridProbes = 0;
    std::size_t offGridFailures = 0;
};

[[nodiscard]] edaPt::VoiceEngineScript edaBaseScript() {
    edaPt::VoiceEngineScript s;
    s.fs = kEdaFs;
    s.hostBlock = kEdaGrid;   // only sizes the observer's record reservations (voiceEngineAllocateTaps)
    s.totalSamples = kEdaTotal;
    const auto pair = edaBmt::showcasePair();
    s.a = edaPt::voiceEngineAs(pair.first);
    s.b = pair.second;
    s.position = 0.5f;        // both endpoints contribute: every voice stage runs non-trivially
    s.tappedVoices = EdaEngine::kMaxVoices;
    return s;
}

[[nodiscard]] edaPt::VoiceEngineScript edaMonoScript() {
    edaPt::VoiceEngineScript s = edaBaseScript();
    s.mode = EdaMode::Mono;
    s.events.push_back(edaPt::VoiceEngineEvent{.sample = 0, .kind = EdaKind::NoteOn, .note = kEdaMonoNote});
    s.events.push_back(
        edaPt::VoiceEngineEvent{.sample = kEdaMonoOff, .kind = EdaKind::NoteOff, .note = kEdaMonoNote});
    return s;
}

[[nodiscard]] edaPt::VoiceEngineScript edaPolyScript() {
    edaPt::VoiceEngineScript s = edaBaseScript();
    s.mode = EdaMode::Poly;
    s.polyphony = 4;
    s.events.push_back(edaPt::VoiceEngineEvent{.sample = 0, .kind = EdaKind::NoteOn, .note = kEdaPolyN1});
    s.events.push_back(edaPt::VoiceEngineEvent{.sample = kEdaPolyOn2, .kind = EdaKind::NoteOn, .note = kEdaPolyN2});
    s.events.push_back(edaPt::VoiceEngineEvent{.sample = kEdaPolyOn3, .kind = EdaKind::NoteOn, .note = kEdaPolyN3});
    s.events.push_back(
        edaPt::VoiceEngineEvent{.sample = kEdaPolyOff2, .kind = EdaKind::NoteOff, .note = kEdaPolyN2});
    s.events.push_back(edaPt::VoiceEngineEvent{.sample = kEdaPolyCount2, .kind = EdaKind::Polyphony, .index = 2});
    s.events.push_back(edaPt::VoiceEngineEvent{.sample = kEdaPolyOn4, .kind = EdaKind::NoteOn, .note = kEdaPolyN4});
    return s;
}

/// voiceEngineRender's setup order (profundum_voice_engine_test_helpers.h), with the observer attached.
[[nodiscard]] std::unique_ptr<EdaEngine> edaMakeEngine(const edaPt::VoiceEngineScript& s,
                                                       Krate::DSP::ProfundumEngineObserver& observer) {
    auto e = std::make_unique<EdaEngine>();
    e->setSeed(s.seed);
    REQUIRE(e->setEndpoint(Krate::DSP::BassMorphEngine::Endpoint::A, s.a));
    REQUIRE(e->setEndpoint(Krate::DSP::BassMorphEngine::Endpoint::B, s.b));
    e->setMorphPosition(s.position);
    e->setMacros(s.macros);
    e->setVoiceMode(s.mode);
    e->setPolyphony(s.polyphony);
    e->setObserver(&observer);
    e->prepare(s.fs, s.config);
    return e;
}

[[nodiscard]] bool edaContains(const std::vector<int>& notes, int n) {
    return std::find(notes.begin(), notes.end(), n) != notes.end();
}

[[nodiscard]] bool edaSubsetOf(const std::vector<int>& notes, std::initializer_list<int> allowed) {
    for (const int n : notes)
        if (std::find(allowed.begin(), allowed.end(), n) == allowed.end())
            return false;
    return true;
}

/// The note checks after a sub-call that has rendered `done` samples in total.
void edaCheckNotes(const EdaEngine& b, const edaPt::VoiceEngineScript& s, std::uint64_t done, EdaRun& r) {
    // Out-of-range indices (plan S4.1): -1 / false / clamped to the last voice.
    if (b.voiceMidiNote(EdaEngine::kMaxVoices) != -1 || b.voiceRenderedThisInterval(EdaEngine::kMaxVoices)
        || &b.voiceForDisplay(99) != &b.voiceForDisplay(EdaEngine::kMaxVoices - 1))
        ++r.boundsFailures;

    if (s.mode == EdaMode::Mono) {
        // The note-on is applied at grid 0; the note-off latched before sample 24000 applies at the grid step that
        // starts the call rendering sample 24000, so after a call ending exactly at 24000 the note is still held.
        const int expected = done <= kEdaMonoOff ? static_cast<int>(kEdaMonoNote) : -1;
        if (b.voiceMidiNote(0) != expected)
            ++r.noteFailures;
        for (std::size_t v = 1; v < EdaEngine::kMaxVoices; ++v)
            if (b.voiceMidiNote(v) != -1)
                ++r.noteFailures;
        return;
    }

    std::vector<int> notes;
    for (std::size_t v = 0; v < EdaEngine::kMaxVoices; ++v) {
        const int n = b.voiceMidiNote(v);
        if (n >= 0)
            notes.push_back(n);
    }
    // Always: only scripted notes, none on two voices (the allocator retriggers a playing note in place).
    std::vector<int> sorted = notes;
    std::sort(sorted.begin(), sorted.end());
    if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end())
        ++r.noteFailures;
    if (!edaSubsetOf(notes, {kEdaPolyN1, kEdaPolyN2, kEdaPolyN3, kEdaPolyN4}))
        ++r.noteFailures;

    // Before any release the allocated set is exact; after a release, a releasing voice keeps its note until the
    // allocator is told it finished (envelope-dependent), so only the held notes are required.
    if (done <= kEdaPolyOn2) {
        if (sorted != std::vector<int>{kEdaPolyN1})
            ++r.noteFailures;
    } else if (done <= kEdaPolyOn3) {
        if (sorted != std::vector<int>{kEdaPolyN1, kEdaPolyN2})
            ++r.noteFailures;
    } else if (done <= kEdaPolyOff2) {
        if (sorted != std::vector<int>{kEdaPolyN1, kEdaPolyN2, kEdaPolyN3})
            ++r.noteFailures;
    } else if (done <= kEdaPolyCount2Applied) {
        if (!edaContains(notes, kEdaPolyN1) || !edaContains(notes, kEdaPolyN3)
            || !edaSubsetOf(notes, {kEdaPolyN1, kEdaPolyN2, kEdaPolyN3}))
            ++r.noteFailures;
    } else if (done <= kEdaPolyOn4) {
        if (!edaContains(notes, kEdaPolyN1) || !edaSubsetOf(notes, {kEdaPolyN1, kEdaPolyN2, kEdaPolyN3}))
            ++r.noteFailures;
    } else {
        if (!edaContains(notes, kEdaPolyN4))
            ++r.noteFailures;
    }
}

[[nodiscard]] bool edaSame(std::span<const float> tap, const std::vector<float>& observed, std::uint64_t start) {
    if (tap.size() != kEdaGrid || start + kEdaGrid > observed.size())
        return false;
    // Bit identity, sample by sample (no memcmp over floats).
    const auto bits = [](float x) noexcept { return std::bit_cast<std::uint32_t>(x); };
    const auto window = std::span<const float>(observed).subspan(static_cast<std::size_t>(start), kEdaGrid);
    return std::ranges::equal(tap, window, {}, bits, bits);
}

/// At a completed interval (controlGridPhase() == 0 after a call that rendered >= 1 sample).
void edaCheckInterval(const EdaEngine& b, const edaPt::VoiceEngineTaps& taps, EdaRun& r) {
    const std::uint64_t end = b.samplePosition();
    if (end < kEdaGrid)
        return;
    const std::uint64_t start = end - kEdaGrid;
    ++r.intervals;
    std::size_t rendered = 0;
    for (std::size_t v = 0; v < EdaEngine::kMaxVoices; ++v) {
        if (!b.voiceRenderedThisInterval(v))
            continue;
        ++rendered;
        const EdaVoice& voice = b.voiceForDisplay(v);
        if (voice.intervalSamplesRendered() != kEdaGrid) {
            ++r.tapLengthFailures;
            continue;
        }
        const edaPt::VoiceEngineVoiceTapSet& obs = taps.voices[v];
        ++r.voiceTapChecks;
        if (!edaSame(voice.characterTapL(), obs.charL, start) || !edaSame(voice.characterTapR(), obs.charR, start)
            || !edaSame(voice.anchorTapL(), obs.anchorL, start) || !edaSame(voice.anchorTapR(), obs.anchorR, start))
            ++r.tapMismatches;
    }
    r.maxVoicesRendered = std::max(r.maxVoicesRendered, rendered);
}

/// Renders the script on A (one call per event slice of each pseudo-random host block) and on B (each slice cut
/// at grid points, plus, for EdaCut::OffGrid, at sample 17 of every interval), reading B's accessors after every
/// sub-call.
[[nodiscard]] EdaRun edaRun(const edaPt::VoiceEngineScript& s, EdaCut cut) {
    std::vector<edaPt::VoiceEngineEvent> events = s.events;
    std::stable_sort(events.begin(), events.end(),
                     [](const edaPt::VoiceEngineEvent& x, const edaPt::VoiceEngineEvent& y) {
                         return x.sample < y.sample;
                     });

    edaPt::VoiceEngineScript sa = s;
    sa.tappedVoices = 0;   // A's observer records too (symmetry); only B's per-voice taps are compared
    edaPt::VoiceEngineTaps tapsA;
    edaPt::VoiceEngineTaps tapsB;
    edaPt::voiceEngineAllocateTaps(tapsA, sa);
    edaPt::voiceEngineAllocateTaps(tapsB, s);
    edaPt::VoiceEngineObserver observerA(tapsA);
    edaPt::VoiceEngineObserver observerB(tapsB);
    auto a = edaMakeEngine(s, observerA);
    auto b = edaMakeEngine(s, observerB);

    EdaRun r;
    const auto total = static_cast<std::size_t>(s.totalSamples);
    r.aL.assign(total, 0.0f);
    r.aR.assign(total, 0.0f);
    r.bL.assign(total, 0.0f);
    r.bR.assign(total, 0.0f);

    std::uint32_t lcg = kEdaLcgSeed;
    std::size_t pos = 0;
    std::size_t ei = 0;
    while (pos < total) {
        lcg = lcg * 1664525u + 1013904223u;
        const std::size_t size = 1 + static_cast<std::size_t>(lcg >> 8) % kEdaMaxBlock;
        const std::size_t blockEnd = std::min(pos + size, total);
        while (pos < blockEnd) {
            while (ei < events.size() && events[ei].sample <= pos) {
                edaPt::voiceEngineIssue(*a, events[ei], s);
                edaPt::voiceEngineIssue(*b, events[ei], s);
                ++ei;
            }
            std::size_t next = blockEnd;
            if (ei < events.size() && events[ei].sample < next)
                next = static_cast<std::size_t>(events[ei].sample);

            a->processBlock(r.aL.data() + pos, r.aR.data() + pos, next - pos);

            std::size_t done = pos;
            while (done < next) {
                const std::size_t phase = b->controlGridPhase();
                std::size_t m = std::min(next - done, kEdaGrid - phase);
                if (cut == EdaCut::OffGrid && phase < kEdaOffGridCut)
                    m = std::min(m, kEdaOffGridCut - phase);
                b->processBlock(r.bL.data() + done, r.bR.data() + done, m);
                done += m;
                ++r.subCalls;

                // ClockAndNotes: the clock after every sub-call.
                if (b->controlGridPhase() != done % kEdaGrid || b->samplePosition() != done)
                    ++r.clockFailures;
                if (b->appliedVoiceMode() != s.mode)
                    ++r.modeFailures;
                edaCheckNotes(*b, s, done, r);

                if (cut == EdaCut::OffGrid && done % kEdaGrid == kEdaOffGridCut) {
                    ++r.offGridProbes;
                    if (b->controlGridPhase() != kEdaOffGridCut)
                        ++r.offGridFailures;
                }
                if (b->controlGridPhase() == 0)
                    edaCheckInterval(*b, tapsB, r);
            }
            pos = next;
        }
    }
    a->setObserver(nullptr);
    b->setObserver(nullptr);

    for (std::size_t i = 0; i < total; ++i)
        r.peak = std::max({r.peak, std::abs(r.aL[i]), std::abs(r.aR[i])});
    return r;
}

[[nodiscard]] bool edaBitIdentical(const std::vector<float>& x, const std::vector<float>& y) {
    return x.size() == y.size() && std::memcmp(x.data(), y.data(), x.size() * sizeof(float)) == 0;
}

void edaRequireBitIdentical(const EdaRun& r) {
    INFO("sub-calls " << r.subCalls << ", peak " << r.peak);
    REQUIRE(r.peak > 1e-3f);   // non-vacuity: the render is not silence
    REQUIRE(r.subCalls > kEdaTotal / kEdaGrid);
    REQUIRE(edaBitIdentical(r.aL, r.bL));
    REQUIRE(edaBitIdentical(r.aR, r.bR));
}

void edaRequireTaps(const EdaRun& r, std::size_t minVoices) {
    INFO("intervals " << r.intervals << ", voice checks " << r.voiceTapChecks << ", length failures "
                      << r.tapLengthFailures << ", mismatches " << r.tapMismatches << ", max voices "
                      << r.maxVoicesRendered);
    REQUIRE(r.intervals == kEdaTotal / kEdaGrid);
    REQUIRE(r.voiceTapChecks >= kEdaMonoOff / kEdaGrid);   // a voice renders in every interval while held
    REQUIRE(r.maxVoicesRendered >= minVoices);
    REQUIRE(r.tapLengthFailures == 0);
    REQUIRE(r.tapMismatches == 0);
}

void edaRequireClock(const EdaRun& r) {
    INFO("sub-calls " << r.subCalls << ", clock " << r.clockFailures << ", mode " << r.modeFailures << ", notes "
                      << r.noteFailures << ", bounds " << r.boundsFailures);
    REQUIRE(r.subCalls > 0);
    REQUIRE(r.clockFailures == 0);
    REQUIRE(r.modeFailures == 0);
    REQUIRE(r.noteFailures == 0);
    REQUIRE(r.boundsFailures == 0);
}

}  // namespace

TEST_CASE("Profundum_EngineDisplayAccessorNeutral", "[profundum][systems][display]") {
    SECTION("MonoBitIdentical") {
        const EdaRun r = edaRun(edaMonoScript(), EdaCut::Grid);
        edaRequireBitIdentical(r);
    }

    SECTION("PolyBitIdentical") {
        const EdaRun r = edaRun(edaPolyScript(), EdaCut::Grid);
        edaRequireBitIdentical(r);
    }

    SECTION("TapsEqualObserver") {
        const EdaRun mono = edaRun(edaMonoScript(), EdaCut::Grid);
        edaRequireTaps(mono, 1);
        const EdaRun poly = edaRun(edaPolyScript(), EdaCut::Grid);
        edaRequireTaps(poly, 3);   // 28 / 33 / 40 sound together over (9600, 19200]
    }

    SECTION("ClockAndNotes") {
        const EdaRun mono = edaRun(edaMonoScript(), EdaCut::Grid);
        edaRequireClock(mono);
        const EdaRun poly = edaRun(edaPolyScript(), EdaCut::Grid);
        edaRequireClock(poly);
    }

    SECTION("OffGridCutIsDetected") {
        // Control arm: B is also cut at sample 17 of every interval; the accessors must report that position,
        // proving the clock read is live (a constant 0 would pass the grid-cut arms' phase checks vacuously).
        const EdaRun r = edaRun(edaMonoScript(), EdaCut::OffGrid);
        INFO("off-grid probes " << r.offGridProbes << ", failures " << r.offGridFailures);
        REQUIRE(r.offGridProbes >= kEdaTotal / kEdaGrid - 1);
        REQUIRE(r.offGridFailures == 0);
        REQUIRE(r.clockFailures == 0);
    }
}
