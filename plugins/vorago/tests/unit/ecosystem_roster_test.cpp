// ==============================================================================
// Vorago Phase 14 - ecosystem rule-knob roster (syncRate, selfAffinity) reaches the engine; FR-071a, FR-076, SC-026a, SC-027
// ==============================================================================
// T012 (specs/vorago-phase14-presets-release/tasks.md). Access path per the
// plan-stage ruling of 2026-09-29 (T001 item 3, "drop the friend"): every engine
// read goes through the public const Processor::engineForTest()
// (processor.h:116-118); no probe struct is defined.
//
//   Vorago_EcosystemRosterReachesEngine      - 901 / 902 through IParameterChanges
//       land on EVERY voice's EcosystemEngine (VoragoEngine::getVoice(i),
//       VoragoVoice::ecosystem()): sync rate and the affinity DIAGONAL, the
//       off-diagonal untouched at +0.45; idle, held and returned-to-default.
//   Vorago_State_V2LoadsWithRosterDefaults_Engine - SC-027 engine half: a v2
//       stream (no rule-knob extension) loads the roster defaults 0.0 / -1.0 into
//       every voice's engine. Persisted half: state_v3_test.cpp (T013).
//   Vorago_VoiceParams_FieldCount             - FR-076: 33 fields and the two new
//       default members equal the EcosystemEngine member defaults.
//
// Exact float equality is valid throughout: 0.0 / 0.25 / 1.0 on the linear
// tapers [0, 0.5] and [-2, 2] give 0.0 / 0.5 and -2.0 / -1.0 / 2.0, all exactly
// representable.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "plugin_ids.h"
#include "vorago_test_fixture.h"

#include <vst_event_list.h>
#include <vst_param_changes.h>

#include <krate/dsp/systems/ecosystem_engine.h>
#include <krate/dsp/systems/vorago_engine.h>

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace {

using Krate::DSP::EcosystemEngine;
using Krate::DSP::VoragoEngine;

constexpr std::size_t kBlock = 512;

[[nodiscard]] const VoragoEngine& engineOf(const VoragoTest::ProcessorFixture& fx) {
    REQUIRE(fx.proc->engineForTest() != nullptr);
    return *fx.proc->engineForTest();
}

/// One processBlock() carrying the two rule knobs (normalized) and, optionally,
/// a NoteOn 36 at offset 0.
void driveRoster(VoragoTest::ProcessorFixture& fx, double syncNorm, double affinityNorm,
                 bool withNoteOn = false) {
    Krate::Test::ParameterChanges pc;
    pc.addChange(::Vorago::kEcosystemSyncRateId, syncNorm);
    pc.addChange(::Vorago::kEcosystemSelfAffinityId, affinityNorm);
    Krate::Test::EventList ev;
    if (withNoteOn) {
        ev.addNoteOn(36, 0.8f);
    }
    fx.reserveCapture(fx.capturedL.size() + kBlock);
    REQUIRE(fx.processBlock(kBlock, &ev, &pc) == Steinberg::kResultOk);
}

/// Checks every voice slot's ecosystem knobs; returns the number of slots checked.
[[nodiscard]] std::size_t checkAllVoices(const VoragoEngine& engine, float expectedSync,
                                         float expectedDiagonal) {
    std::size_t checked = 0;
    for (std::size_t v = 0; v < VoragoEngine::kMaxVoices; ++v) {
        INFO("voice " << v);
        const EcosystemEngine& eco = engine.getVoice(v).ecosystem();
        CHECK(eco.getSyncRate() == expectedSync);
        for (std::size_t a = 0; a < EcosystemEngine::kNumKinds; ++a) {
            for (std::size_t b = 0; b < EcosystemEngine::kNumKinds; ++b) {
                INFO("affinity[" << a << "][" << b << "]");
                const auto from = static_cast<EcosystemEngine::Kind>(a);
                const auto to = static_cast<EcosystemEngine::Kind>(b);
                if (a == b) {
                    CHECK(eco.getAffinity(from, to) == expectedDiagonal);
                } else {
                    CHECK(eco.getAffinity(from, to) == 0.45f);  // off-diagonal untouched
                }
            }
        }
        ++checked;
    }
    return checked;
}

[[nodiscard]] std::vector<char> stateBytes(::Vorago::Processor& proc) {
    auto s = Steinberg::owned(new Steinberg::MemoryStream());
    REQUIRE(proc.getState(s) == Steinberg::kResultOk);
    const auto size = static_cast<std::size_t>(s->getSize());
    const char* data = s->getData();
    return {data, data + size};
}

[[nodiscard]] Steinberg::IPtr<Steinberg::MemoryStream> streamOf(const std::vector<char>& bytes) {
    auto s = Steinberg::owned(new Steinberg::MemoryStream());
    if (!bytes.empty()) {
        Steinberg::int32 written = 0;
        REQUIRE(s->write(const_cast<char*>(bytes.data()),  // NOLINT(cppcoreguidelines-pro-type-const-cast)
                         static_cast<Steinberg::int32>(bytes.size()), &written) ==
                Steinberg::kResultOk);
        REQUIRE(std::cmp_equal(written, bytes.size()));
    }
    REQUIRE(s->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) == Steinberg::kResultOk);
    return s;
}

void putLeWord(std::vector<char>& bytes, std::size_t offset, std::uint32_t w) {
    REQUIRE(offset + 4u <= bytes.size());
    for (std::size_t b = 0; b < 4; ++b) {
        bytes[offset + b] = static_cast<char>((w >> (8u * b)) & 0xFFu);
    }
}

}  // namespace

TEST_CASE("Vorago_EcosystemRosterReachesEngine", "[vorago][ecosystem]") {
    STATIC_REQUIRE(VoragoEngine::kMaxVoices == 6u);

    VoragoTest::ProcessorFixture fx;
    fx.prepare(48000.0, static_cast<Steinberg::int32>(kBlock));

    // Pass 1: no note - 901 / 902 at 1.0 reach all six voices.
    driveRoster(fx, 1.0, 1.0);
    REQUIRE(checkAllVoices(engineOf(fx), 0.5f, 2.0f) == 6u);

    // Pass 2: the same with NoteOn 36 held. The knobs are first returned to the
    // defaults so the held-note pass has to move them again.
    driveRoster(fx, 0.0, 0.25);
    REQUIRE(checkAllVoices(engineOf(fx), 0.0f, -1.0f) == 6u);
    driveRoster(fx, 1.0, 1.0, /*withNoteOn=*/true);
    REQUIRE(engineOf(fx).getRenderingVoiceCount() >= 1u);  // the note is sounding
    REQUIRE(checkAllVoices(engineOf(fx), 0.5f, 2.0f) == 6u);

    // Pass 3: 901 -> 0.0, 902 -> 0.25 (the registered defaults), note still held.
    driveRoster(fx, 0.0, 0.25);
    REQUIRE(checkAllVoices(engineOf(fx), 0.0f, -1.0f) == 6u);
}

TEST_CASE("Vorago_State_V2LoadsWithRosterDefaults_Engine", "[vorago][state]") {
    VoragoTest::ProcessorFixture fx;
    fx.prepare(48000.0, static_cast<Steinberg::int32>(kBlock));

    // Non-default roster, confirmed on the engine before the load.
    driveRoster(fx, 1.0, 1.0);
    REQUIRE(checkAllVoices(engineOf(fx), 0.5f, 2.0f) == 6u);

    // v2 stream: this processor's own state, version int overwritten to 2 and
    // truncated to kStateV2Bytes (v2 is a strict prefix of v3).
    std::vector<char> v2 = stateBytes(*fx.proc);
    REQUIRE(v2.size() >= ::Vorago::kStateV2Bytes);
    v2.resize(::Vorago::kStateV2Bytes);
    putLeWord(v2, 0, 2u);

    REQUIRE(fx.proc->setState(streamOf(v2)) == Steinberg::kResultOk);

    Krate::Test::EventList noEvents;
    fx.reserveCapture(fx.capturedL.size() + kBlock);
    REQUIRE(fx.processBlock(kBlock, &noEvents) == Steinberg::kResultOk);

    REQUIRE(checkAllVoices(engineOf(fx), 0.0f, -1.0f) == 6u);
}

TEST_CASE("Vorago_VoiceParams_FieldCount", "[vorago][ecosystem]") {
    static_assert(Krate::DSP::VoragoVoiceParams::kFieldCount == 33,
                  "FR-076: 31 Phase-13 fields + ecosystemSyncRate + ecosystemSelfAffinity");
    STATIC_REQUIRE(Krate::DSP::VoragoVoiceParams::kFieldCount == 33u);

    const auto eco = std::make_unique<EcosystemEngine>();
    const Krate::DSP::VoragoVoiceParams defaults{};
    REQUIRE(defaults.ecosystemSyncRate == eco->getSyncRate());
    REQUIRE(defaults.ecosystemSelfAffinity ==
            eco->getAffinity(EcosystemEngine::Kind::Partial, EcosystemEngine::Kind::Partial));
}
