// ==============================================================================
// Vorago Phase 14 - state v3 stream (436 bytes) round-trip and v2 migration; FR-072, FR-074, SC-027; filled by T013
// ==============================================================================
// T013 (specs/vorago-phase14-presets-release/tasks.md; plan 5.2). The v3 stream is
// the v2 stream (kStateV2Bytes = 428) followed by the ecosystem rule-knob extension:
// float syncRate (ID 901) at byte offset 428 and float selfAffinity (ID 902) at 432.
//   - Vorago_StateRoundTripV3: 436 bytes, version 3, knob floats, byte-exact reload.
//   - Vorago_State_V2LoadsWithRosterDefaults: a v2 stream (SC-027 persisted half)
//     resets both knobs to their registered defaults 0.0 / -1.0, even from a
//     non-default state.
//   - Vorago_State_V3TruncatedKeepsPrefix: a v3 stream cut at 428 loads the v2
//     prefix and leaves the knobs unchanged.
//   - Vorago_State_V3NonFiniteKnobRejected: a NaN bit pattern at 428 leaves
//     syncRate unchanged; selfAffinity still loads.
//   - Vorago_State_V4Rejected: version 4 -> kResultFalse, nothing changed.
//   - Vorago_ControllerState_V3AndV2: the controller's applyStateStream mirror.
//
// Built with -fno-fast-math (tests/CMakeLists.txt). The NaN payload is written as
// raw little-endian bytes, so no NaN float value is ever materialized here.
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "controller/controller.h"
#include "plugin_ids.h"
#include "vorago_test_fixture.h"

#include <vst_param_changes.h>

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kSyncRateOffset = 428;      // == kStateV2Bytes
constexpr std::size_t kSelfAffinityOffset = 432;  // == kStateV2Bytes + 4
constexpr std::size_t kBlock = 512;

static_assert(kSyncRateOffset == ::Vorago::kStateV2Bytes);

// ------------------------------------------------------------------------------
// Streams
// ------------------------------------------------------------------------------

[[nodiscard]] std::vector<char> stateBytes(::Vorago::Processor& proc) {
    auto s = Steinberg::owned(new Steinberg::MemoryStream());
    REQUIRE(proc.getState(s) == Steinberg::kResultOk);
    const auto size = static_cast<std::size_t>(s->getSize());
    const char* data = s->getData();
    return {data, data + size};
}

[[nodiscard]] Steinberg::IPtr<Steinberg::MemoryStream> streamOf(const std::vector<char>& bytes,
                                                                std::size_t count) {
    REQUIRE(count <= bytes.size());
    auto s = Steinberg::owned(new Steinberg::MemoryStream());
    if (count > 0) {
        Steinberg::int32 written = 0;
        REQUIRE(s->write(const_cast<char*>(bytes.data()),  // NOLINT(cppcoreguidelines-pro-type-const-cast)
                         static_cast<Steinberg::int32>(count), &written) == Steinberg::kResultOk);
        REQUIRE(std::cmp_equal(written, count));
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

[[nodiscard]] std::uint32_t getLeWord(const std::vector<char>& bytes, std::size_t offset) {
    REQUIRE(offset + 4u <= bytes.size());
    std::uint32_t w = 0;
    for (std::size_t b = 0; b < 4; ++b) {
        w |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + b])) << (8u * b);
    }
    return w;
}

[[nodiscard]] std::int32_t versionOf(const std::vector<char>& bytes) {
    return static_cast<std::int32_t>(getLeWord(bytes, 0));
}

/// The float at `offset`. Only called on offsets known to hold a finite value.
[[nodiscard]] float floatAt(const std::vector<char>& bytes, std::size_t offset) {
    return std::bit_cast<float>(getLeWord(bytes, offset));
}

/// `bytes` rewritten as a v2 stream: version 2, truncated to kStateV2Bytes (exact:
/// v2 is a strict prefix of v3).
[[nodiscard]] std::vector<char> asV2Stream(std::vector<char> bytes) {
    REQUIRE(bytes.size() >= ::Vorago::kStateV2Bytes);
    putLeWord(bytes, 0, 2u);
    bytes.resize(::Vorago::kStateV2Bytes);
    return bytes;
}

// ------------------------------------------------------------------------------
// Processor helpers
// ------------------------------------------------------------------------------

[[nodiscard]] float storedSyncRate(const ::Vorago::Processor& proc) {
    return proc.packsForTest().ecosystem.syncRate.load();
}

[[nodiscard]] float storedSelfAffinity(const ::Vorago::Processor& proc) {
    return proc.packsForTest().ecosystem.selfAffinity.load();
}

[[nodiscard]] std::uint32_t bitsOf(float v) noexcept {
    return std::bit_cast<std::uint32_t>(v);
}

/// Prepared at 48 kHz / 512 with capture room for `blocks` process() calls.
void prepareFixture(VoragoTest::ProcessorFixture& fx, std::size_t blocks = 8) {
    fx.prepare(48000.0, static_cast<Steinberg::int32>(kBlock));
    fx.reserveCapture(blocks * kBlock);
}

/// Drives 901 / 902 to the given normalized values in ONE process() call.
void driveKnobs(VoragoTest::ProcessorFixture& fx, double syncNorm, double affinityNorm) {
    Krate::Test::ParameterChanges pc;
    pc.addChange(::Vorago::kEcosystemSyncRateId, syncNorm);
    pc.addChange(::Vorago::kEcosystemSelfAffinityId, affinityNorm);
    REQUIRE(fx.processBlock(kBlock, nullptr, &pc) == Steinberg::kResultOk);
}

void processOnce(VoragoTest::ProcessorFixture& fx) {
    REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
}

}  // namespace

// ==============================================================================

TEST_CASE("Vorago_StateRoundTripV3", "[vorago][state]") {
    namespace Vg = ::Vorago;

    STATIC_REQUIRE(Vg::kCurrentStateVersion == 3);
    STATIC_REQUIRE(Vg::kStateV2Bytes == 428u);
    STATIC_REQUIRE(Vg::kStateV3Bytes == 436u);

    VoragoTest::ProcessorFixture src;
    prepareFixture(src);
    driveKnobs(src, 0.8, 0.8);  // plain 0.4 (sync [0, 0.5]) / 1.2 (affinity [-2, 2])

    REQUIRE(storedSyncRate(*src.proc) == Catch::Approx(0.4).margin(1e-6));
    REQUIRE(storedSelfAffinity(*src.proc) == Catch::Approx(1.2).margin(1e-6));

    const std::vector<char> bytes = stateBytes(*src.proc);
    REQUIRE(bytes.size() == Vg::kStateV3Bytes);
    REQUIRE(versionOf(bytes) == 3);
    REQUIRE(floatAt(bytes, kSyncRateOffset) == Catch::Approx(0.4).margin(1e-6));
    REQUIRE(floatAt(bytes, kSelfAffinityOffset) == Catch::Approx(1.2).margin(1e-6));

    VoragoTest::ProcessorFixture dst;
    prepareFixture(dst);
    REQUIRE(dst.proc->setState(streamOf(bytes, bytes.size())) == Steinberg::kResultOk);
    REQUIRE(bitsOf(storedSyncRate(*dst.proc)) == bitsOf(storedSyncRate(*src.proc)));
    REQUIRE(bitsOf(storedSelfAffinity(*dst.proc)) == bitsOf(storedSelfAffinity(*src.proc)));

    const std::vector<char> reloaded = stateBytes(*dst.proc);
    REQUIRE(reloaded.size() == bytes.size());
    REQUIRE(std::memcmp(reloaded.data(), bytes.data(), bytes.size()) == 0);
}

TEST_CASE("Vorago_State_V2LoadsWithRosterDefaults", "[vorago][state]") {
    namespace Vg = ::Vorago;

    // From a NON-default state: both knobs at the top of their ranges.
    VoragoTest::ProcessorFixture fx;
    prepareFixture(fx);
    driveKnobs(fx, 1.0, 1.0);
    REQUIRE(storedSyncRate(*fx.proc) == 0.5f);
    REQUIRE(storedSelfAffinity(*fx.proc) == 2.0f);

    const std::vector<char> v2 = asV2Stream(stateBytes(*fx.proc));
    REQUIRE(v2.size() == Vg::kStateV2Bytes);
    REQUIRE(versionOf(v2) == 2);

    REQUIRE(fx.proc->setState(streamOf(v2, v2.size())) == Steinberg::kResultOk);
    processOnce(fx);

    // Registered defaults == the engine defaults (syncRate_ 0, diagonal -1).
    REQUIRE(storedSyncRate(*fx.proc) == 0.0f);
    REQUIRE(storedSelfAffinity(*fx.proc) == -1.0f);

    const std::vector<char> after = stateBytes(*fx.proc);
    REQUIRE(after.size() == Vg::kStateV3Bytes);
    REQUIRE(versionOf(after) == 3);
    REQUIRE(floatAt(after, kSyncRateOffset) == 0.0f);
    REQUIRE(floatAt(after, kSelfAffinityOffset) == -1.0f);
    // Every v2 field survives the migration unchanged.
    REQUIRE(std::memcmp(after.data() + 4, v2.data() + 4, Vg::kStateV2Bytes - 4u) == 0);
}

TEST_CASE("Vorago_State_V3TruncatedKeepsPrefix", "[vorago][state]") {
    namespace Vg = ::Vorago;

    // X: knobs at plain 0.4 / 1.2, everything else at defaults.
    VoragoTest::ProcessorFixture x;
    prepareFixture(x);
    driveKnobs(x, 0.8, 0.8);
    const float xSync = storedSyncRate(*x.proc);
    const float xAffinity = storedSelfAffinity(*x.proc);
    REQUIRE(xSync == Catch::Approx(0.4).margin(1e-6));
    REQUIRE(xAffinity == Catch::Approx(1.2).margin(1e-6));

    // Y: knobs at plain 0.1 / 0.4 (normalized 0.2 / 0.6) and a few v2 fields moved
    // off their defaults so the prefix load is observable.
    VoragoTest::ProcessorFixture y;
    prepareFixture(y);
    {
        Krate::Test::ParameterChanges pc;
        pc.addChange(Vg::kEcosystemSyncRateId, 0.2);
        pc.addChange(Vg::kEcosystemSelfAffinityId, 0.6);
        pc.addChange(Vg::kEcosystemDepthId, 0.73);
        pc.addChange(Vg::kMasterGainId, 0.31);
        pc.addChange(Vg::kCloudRichnessId, 0.62);
        pc.addChange(Vg::kSpaceMixId, 0.17);
        REQUIRE(y.processBlock(kBlock, nullptr, &pc) == Steinberg::kResultOk);
    }
    const std::vector<char> yBytes = stateBytes(*y.proc);
    REQUIRE(yBytes.size() == Vg::kStateV3Bytes);
    REQUIRE(floatAt(yBytes, kSyncRateOffset) == Catch::Approx(0.1).margin(1e-6));
    REQUIRE(floatAt(yBytes, kSelfAffinityOffset) == Catch::Approx(0.4).margin(1e-6));

    const std::vector<char> xBefore = stateBytes(*x.proc);
    REQUIRE(xBefore.size() == Vg::kStateV3Bytes);
    REQUIRE(std::memcmp(xBefore.data() + 4, yBytes.data() + 4, Vg::kStateV2Bytes - 4u) != 0);

    // Y's v3 stream cut at the end of the v2 prefix (version field stays 3).
    REQUIRE(x.proc->setState(streamOf(yBytes, Vg::kStateV2Bytes)) == Steinberg::kResultOk);
    processOnce(x);

    // The knobs are untouched...
    REQUIRE(bitsOf(storedSyncRate(*x.proc)) == bitsOf(xSync));
    REQUIRE(bitsOf(storedSelfAffinity(*x.proc)) == bitsOf(xAffinity));

    // ...and every v2 field equals Y's.
    const std::vector<char> xAfter = stateBytes(*x.proc);
    REQUIRE(xAfter.size() == Vg::kStateV3Bytes);
    REQUIRE(std::memcmp(xAfter.data(), yBytes.data(), Vg::kStateV2Bytes) == 0);
    REQUIRE(floatAt(xAfter, kSyncRateOffset) == Catch::Approx(0.4).margin(1e-6));
    REQUIRE(floatAt(xAfter, kSelfAffinityOffset) == Catch::Approx(1.2).margin(1e-6));
}

TEST_CASE("Vorago_State_V3NonFiniteKnobRejected", "[vorago][state]") {
    namespace Vg = ::Vorago;

    // Source stream: knobs at plain 0.4 / 1.2.
    VoragoTest::ProcessorFixture src;
    prepareFixture(src);
    driveKnobs(src, 0.8, 0.8);
    std::vector<char> bytes = stateBytes(*src.proc);
    REQUIRE(bytes.size() == Vg::kStateV3Bytes);
    putLeWord(bytes, kSyncRateOffset, 0x7FC00000u);  // quiet NaN in syncRate

    // Target: syncRate at plain 0.1 (distinct from the source), affinity default.
    VoragoTest::ProcessorFixture dst;
    prepareFixture(dst);
    {
        Krate::Test::ParameterChanges pc;
        pc.addChange(Vg::kEcosystemSyncRateId, 0.2);
        REQUIRE(dst.processBlock(kBlock, nullptr, &pc) == Steinberg::kResultOk);
    }
    const float dstSync = storedSyncRate(*dst.proc);
    REQUIRE(dstSync == Catch::Approx(0.1).margin(1e-6));
    REQUIRE(storedSelfAffinity(*dst.proc) == -1.0f);

    REQUIRE(dst.proc->setState(streamOf(bytes, bytes.size())) == Steinberg::kResultOk);
    processOnce(dst);

    const float gotSync = storedSyncRate(*dst.proc);
    REQUIRE(Krate::DSP::detail::isFinite(gotSync));
    REQUIRE(bitsOf(gotSync) == bitsOf(dstSync));
    REQUIRE(bitsOf(storedSelfAffinity(*dst.proc)) == bitsOf(storedSelfAffinity(*src.proc)));
    REQUIRE(storedSelfAffinity(*dst.proc) == Catch::Approx(1.2).margin(1e-6));
}

TEST_CASE("Vorago_State_V4Rejected", "[vorago][state]") {
    namespace Vg = ::Vorago;

    VoragoTest::ProcessorFixture src;
    prepareFixture(src);
    driveKnobs(src, 1.0, 1.0);
    std::vector<char> future = stateBytes(*src.proc);
    REQUIRE(future.size() == Vg::kStateV3Bytes);
    putLeWord(future, 0, 4u);

    VoragoTest::ProcessorFixture dst;
    prepareFixture(dst);
    const std::vector<char> before = stateBytes(*dst.proc);
    REQUIRE(dst.proc->setState(streamOf(future, future.size())) == Steinberg::kResultFalse);
    REQUIRE(stateBytes(*dst.proc) == before);
    REQUIRE(storedSyncRate(*dst.proc) == 0.0f);
    REQUIRE(storedSelfAffinity(*dst.proc) == -1.0f);

    auto controller = Steinberg::owned(new Vg::Controller());
    REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);
    REQUIRE(controller->setComponentState(streamOf(future, future.size())) ==
            Steinberg::kResultFalse);
    REQUIRE(controller->terminate() == Steinberg::kResultOk);
}

TEST_CASE("Vorago_ControllerState_V3AndV2", "[vorago][state]") {
    namespace Vg = ::Vorago;

    // A v3 stream with both knobs at the top of their ranges.
    VoragoTest::ProcessorFixture src;
    prepareFixture(src);
    driveKnobs(src, 1.0, 1.0);
    const std::vector<char> v3 = stateBytes(*src.proc);
    REQUIRE(v3.size() == Vg::kStateV3Bytes);
    const std::vector<char> v2 = asV2Stream(v3);

    auto controller = Steinberg::owned(new Vg::Controller());
    REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);

    REQUIRE(controller->setComponentState(streamOf(v3, v3.size())) == Steinberg::kResultOk);
    REQUIRE(std::fabs(controller->getParamNormalized(Vg::kEcosystemSyncRateId) - 1.0) <= 1.0e-9);
    REQUIRE(std::fabs(controller->getParamNormalized(Vg::kEcosystemSelfAffinityId) - 1.0) <=
            1.0e-9);

    // The v2 stream, loaded over that non-default controller state, mirrors the
    // registered defaults: normalized 0.0 (sync 0.0) and 0.25 (affinity -1.0).
    REQUIRE(controller->setComponentState(streamOf(v2, v2.size())) == Steinberg::kResultOk);
    REQUIRE(std::fabs(controller->getParamNormalized(Vg::kEcosystemSyncRateId) - 0.0) <= 1.0e-9);
    REQUIRE(std::fabs(controller->getParamNormalized(Vg::kEcosystemSelfAffinityId) - 0.25) <=
            1.0e-9);

    REQUIRE(controller->terminate() == Steinberg::kResultOk);
}
