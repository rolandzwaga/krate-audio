// ==============================================================================
// Vorago Phase 12 - resonance parameter pack contract (T018)
// ==============================================================================
// The eight-SECTION pack contract of specs/vorago-phase12-parameters/tasks.md
// (Group 7 "Shared test shape") for the Resonance pack: IDs 400-403, N = 4,
// stream 3 x float + 1 x int32 = B = 16 bytes.
//
// Built with -fno-fast-math (tests/CMakeLists.txt): the NaN/Inf payloads below are
// produced from bit patterns through a volatile uint32 + memcpy.
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "parameters/resonance_params.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <utility>

namespace {

using Catch::Approx;
using Steinberg::Vst::ParamID;

constexpr std::size_t kStreamBytes = 16;  // B

std::string toAscii(const Steinberg::Vst::TChar* s) {
    char buf[128] = {};
    Steinberg::UString(const_cast<Steinberg::Vst::TChar*>(s), 128)  // NOLINT(cppcoreguidelines-pro-type-const-cast)
        .toAscii(buf, 128);
    return {buf};
}

float floatFromBits(std::uint32_t bits) {
    volatile std::uint32_t v = bits;
    const std::uint32_t b = v;
    float f = 0.0f;
    std::memcpy(&f, &b, sizeof(f));
    return f;
}

using Snapshot = std::array<std::uint32_t, 4>;

Snapshot snapshot(const ::Vorago::ResonanceParams& p) {
    return {std::bit_cast<std::uint32_t>(p.gravity.load()),
            std::bit_cast<std::uint32_t>(p.mix.load()),
            std::bit_cast<std::uint32_t>(p.wanderRateHz.load()),
            static_cast<std::uint32_t>(p.anchorMode.load())};
}

// Non-default, in-range values used by SECTIONs 4, 5 and 7.
constexpr float kSavedGravity = -0.35f;
constexpr float kSavedMix = 0.8f;
constexpr float kSavedWander = 0.25f;
constexpr int kSavedAnchor = 0;  // Free

void setSaved(::Vorago::ResonanceParams& p) {
    p.gravity.store(kSavedGravity);
    p.mix.store(kSavedMix);
    p.wanderRateHz.store(kSavedWander);
    p.anchorMode.store(kSavedAnchor);
}

// Distinct from both the saved values and the defaults.
void setDirty(::Vorago::ResonanceParams& p) {
    p.gravity.store(0.9f);
    p.mix.store(0.1f);
    p.wanderRateHz.store(0.5f);
    p.anchorMode.store(1);
}

std::vector<char> streamBytes(Steinberg::MemoryStream& s) {
    const auto size = static_cast<std::size_t>(s.getSize());
    std::vector<char> out(size);
    if (size > 0)
        std::memcpy(out.data(), s.getData(), size);
    return out;
}

std::vector<char> saveToBytes(const ::Vorago::ResonanceParams& p) {
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    {
        Steinberg::IBStreamer w(stream, kLittleEndian);
        ::Vorago::saveResonanceParams(p, w);
    }
    return streamBytes(*stream);
}

Steinberg::IPtr<Steinberg::MemoryStream> streamFrom(std::vector<char> bytes, std::size_t count) {
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    if (count > 0) {
        Steinberg::int32 written = 0;
        REQUIRE(stream->write(bytes.data(), static_cast<Steinberg::int32>(count), &written) ==
                Steinberg::kResultOk);
        REQUIRE(std::cmp_equal(written, count));
    }
    REQUIRE(stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) == Steinberg::kResultOk);
    return stream;
}

bool loadFromBytes(::Vorago::ResonanceParams& p, const std::vector<char>& bytes, std::size_t count) {
    auto stream = streamFrom(bytes, count);
    Steinberg::IBStreamer r(stream, kLittleEndian);
    return ::Vorago::loadResonanceParams(p, r);
}

// A hand-built stream: three floats then one int32 (ascending ID order).
std::vector<char> buildStream(float g, float m, float w, Steinberg::int32 a) {
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    {
        Steinberg::IBStreamer wr(stream, kLittleEndian);
        wr.writeFloat(g);
        wr.writeFloat(m);
        wr.writeFloat(w);
        wr.writeInt32(a);
    }
    return streamBytes(*stream);
}

double wanderToNormalized(double hz) { return std::log(hz / 0.002) / std::log(1.0 / 0.002); }

}  // namespace

TEST_CASE("Vorago_ResonanceParamsContract", "[vorago][params]") {
    using namespace ::Vorago;

    // --------------------------------------------------------------------------
    SECTION("Registration") {
        Steinberg::Vst::ParameterContainer pc;
        registerResonanceParams(pc);
        REQUIRE(pc.getParameterCount() == 4);

        struct Row {
            ParamID id;
            const char* title;
            const char* units;
            Steinberg::int32 stepCount;
            bool isList;
            double n0;
        };
        const std::array<Row, 4> rows = {{
            {.id=kResonanceGravityId, .title="Resonance Gravity", .units="", .stepCount=0, .isList=false, .n0=0.5},
            {.id=kResonanceMixId, .title="Resonance Mix", .units="%", .stepCount=0, .isList=false, .n0=0.45},
            {.id=kResonanceWanderRateId, .title="Resonance Wander Rate", .units="Hz", .stepCount=0, .isList=false, .n0=0.435755587},
            {.id=kResonanceAnchorModeId, .title="Resonance Anchor", .units="", .stepCount=2, .isList=true, .n0=1.0},
        }};
        for (const auto& row : rows) {
            INFO("id " << row.id);
            auto* p = pc.getParameter(row.id);
            REQUIRE(p != nullptr);
            const auto& info = p->getInfo();
            REQUIRE(toAscii(info.title) == row.title);
            REQUIRE(toAscii(info.units) == row.units);
            REQUIRE(info.stepCount == row.stepCount);
            REQUIRE((info.flags & Steinberg::Vst::ParameterInfo::kCanAutomate) != 0);
            REQUIRE(((info.flags & Steinberg::Vst::ParameterInfo::kIsList) != 0) == row.isList);
            REQUIRE(info.defaultNormalizedValue == Approx(row.n0).margin(1e-9));
        }

        // The anchor list labels, in AnchorMode order (resonance_drift_network.h:293).
        auto* anchor = pc.getParameter(kResonanceAnchorModeId);
        Steinberg::Vst::String128 label{};
        anchor->toString(0.0, label);
        REQUIRE(toAscii(label) == "Free");
        anchor->toString(0.5, label);
        REQUIRE(toAscii(label) == "Keyed");
        anchor->toString(1.0, label);
        REQUIRE(toAscii(label) == "Hybrid");
    }

    // --------------------------------------------------------------------------
    SECTION("DefaultsDenormalizeToPlain") {
        const ResonanceParams fresh;
        REQUIRE(fresh.gravity.load() == 0.0f);
        REQUIRE(fresh.mix.load() == 0.45f);
        REQUIRE(fresh.wanderRateHz.load() == 0.03f);
        REQUIRE(fresh.anchorMode.load() == 2);

        ResonanceParams p;
        setDirty(p);
        handleResonanceParamChange(p, kResonanceGravityId, 0.5);
        handleResonanceParamChange(p, kResonanceMixId, 0.45);
        handleResonanceParamChange(p, kResonanceWanderRateId, 0.435755587);
        handleResonanceParamChange(p, kResonanceAnchorModeId, 1.0);
        REQUIRE(p.gravity.load() == Approx(fresh.gravity.load()).margin(1e-6));
        REQUIRE(p.mix.load() == Approx(fresh.mix.load()).epsilon(1e-6));
        REQUIRE(p.wanderRateHz.load() == Approx(fresh.wanderRateHz.load()).epsilon(1e-6));
        REQUIRE(p.anchorMode.load() == fresh.anchorMode.load());

        // Taper midpoint (plan 3.3.1): log over [0.002, 1] at n = 0.5 -> 0.044721 Hz.
        handleResonanceParamChange(p, kResonanceWanderRateId, 0.5);
        REQUIRE(p.wanderRateHz.load() == Approx(0.044721).epsilon(1e-4));

        // n = 0 -> low range ends.
        handleResonanceParamChange(p, kResonanceGravityId, 0.0);
        handleResonanceParamChange(p, kResonanceMixId, 0.0);
        handleResonanceParamChange(p, kResonanceWanderRateId, 0.0);
        handleResonanceParamChange(p, kResonanceAnchorModeId, 0.0);
        REQUIRE(p.gravity.load() == -1.0f);
        REQUIRE(p.mix.load() == 0.0f);
        REQUIRE(p.wanderRateHz.load() == Approx(0.002).epsilon(1e-6));
        REQUIRE(p.anchorMode.load() == 0);

        // n = 1 -> high range ends.
        handleResonanceParamChange(p, kResonanceGravityId, 1.0);
        handleResonanceParamChange(p, kResonanceMixId, 1.0);
        handleResonanceParamChange(p, kResonanceWanderRateId, 1.0);
        handleResonanceParamChange(p, kResonanceAnchorModeId, 1.0);
        REQUIRE(p.gravity.load() == 1.0f);
        REQUIRE(p.mix.load() == 1.0f);
        REQUIRE(p.wanderRateHz.load() == Approx(1.0).epsilon(1e-6));
        REQUIRE(p.anchorMode.load() == 2);

        // Middle list entry.
        handleResonanceParamChange(p, kResonanceAnchorModeId, 0.5);
        REQUIRE(p.anchorMode.load() == 1);
    }

    // --------------------------------------------------------------------------
    SECTION("UnregisteredInBandIgnored") {
        ResonanceParams p;
        setSaved(p);
        const auto before = snapshot(p);
        for (const ParamID id : {ParamID{404}, ParamID{405}, ParamID{410}, ParamID{450},
                                 ParamID{kResonanceParamRangeEnd - 1}}) {
            for (const double v : {0.0, 0.5, 1.0}) {
                handleResonanceParamChange(p, id, v);
                INFO("id " << id << " v " << v);
                REQUIRE(snapshot(p) == before);
            }
        }
    }

    // --------------------------------------------------------------------------
    SECTION("SaveLoadRoundTrip") {
        ResonanceParams src;
        setSaved(src);
        const auto bytes = saveToBytes(src);
        REQUIRE(bytes.size() == kStreamBytes);

        ResonanceParams dst;
        REQUIRE(loadFromBytes(dst, bytes, bytes.size()));
        REQUIRE(snapshot(dst) == snapshot(src));
    }

    // --------------------------------------------------------------------------
    SECTION("TruncationEveryOffset") {
        ResonanceParams src;
        setSaved(src);
        const auto bytes = saveToBytes(src);
        REQUIRE(bytes.size() == kStreamBytes);

        const auto saved = snapshot(src);
        ResonanceParams dirtyRef;
        setDirty(dirtyRef);
        const auto dirty = snapshot(dirtyRef);

        for (std::size_t cut = 0; cut < kStreamBytes; ++cut) {
            INFO("cut " << cut);
            ResonanceParams p;
            setDirty(p);
            REQUIRE_FALSE(loadFromBytes(p, bytes, cut));
            const auto got = snapshot(p);
            for (std::size_t field = 0; field < 4; ++field) {
                const bool contained = 4u * (field + 1u) <= cut;
                INFO("field " << field);
                REQUIRE(got[field] == (contained ? saved[field] : dirty[field]));
            }
        }
    }

    // --------------------------------------------------------------------------
    SECTION("NonFiniteAndClamp") {
        const std::array<float, 3> nonFinite = {floatFromBits(0x7FC00000u),   // NaN
                                                floatFromBits(0x7F800000u),   // +Inf
                                                floatFromBits(0xFF800000u)};  // -Inf
        for (std::size_t field = 0; field < 3; ++field) {
            for (const float bad : nonFinite) {
                INFO("field " << field << " bits " << std::bit_cast<std::uint32_t>(bad));
                std::array<float, 3> vals = {kSavedGravity, kSavedMix, kSavedWander};
                vals[field] = bad;
                const auto bytes = buildStream(vals[0], vals[1], vals[2], kSavedAnchor);

                ResonanceParams p;
                setDirty(p);
                const auto dirty = snapshot(p);
                REQUIRE(loadFromBytes(p, bytes, bytes.size()));
                const auto got = snapshot(p);

                ResonanceParams expectRef;
                setSaved(expectRef);
                auto expected = snapshot(expectRef);
                expected[field] = dirty[field];  // the non-finite field is left unchanged
                REQUIRE(got == expected);
            }
        }

        // Finite out-of-range floats clamp to the plain range; indices clamp to [0, 2].
        {
            ResonanceParams p;
            REQUIRE(loadFromBytes(p, buildStream(5.0f, 2.0f, 10.0f, -1), kStreamBytes));
            REQUIRE(p.gravity.load() == 1.0f);
            REQUIRE(p.mix.load() == 1.0f);
            REQUIRE(p.wanderRateHz.load() == 1.0f);
            REQUIRE(p.anchorMode.load() == 0);
        }
        {
            ResonanceParams p;
            REQUIRE(loadFromBytes(p, buildStream(-5.0f, -1.0f, 0.0f, 3), kStreamBytes));
            REQUIRE(p.gravity.load() == -1.0f);
            REQUIRE(p.mix.load() == 0.0f);
            REQUIRE(p.wanderRateHz.load() == 0.002f);
            REQUIRE(p.anchorMode.load() == 2);
        }
    }

    // --------------------------------------------------------------------------
    SECTION("ControllerMirror") {
        ResonanceParams src;
        setSaved(src);
        const auto bytes = saveToBytes(src);

        std::map<ParamID, double> got;
        {
            auto stream = streamFrom(bytes, bytes.size());
            Steinberg::IBStreamer r(stream, kLittleEndian);
            loadResonanceParamsToController(r, [&got](ParamID id, double n) { got[id] = n; });
        }
        REQUIRE(got.size() == 4);
        REQUIRE(got.at(kResonanceGravityId) ==
                Approx((static_cast<double>(kSavedGravity) + 1.0) / 2.0).margin(1e-9));
        REQUIRE(got.at(kResonanceMixId) == Approx(static_cast<double>(kSavedMix)).margin(1e-9));
        REQUIRE(got.at(kResonanceWanderRateId) ==
                Approx(wanderToNormalized(static_cast<double>(kSavedWander))).margin(1e-9));
        REQUIRE(got.at(kResonanceAnchorModeId) == Approx(0.0).margin(1e-9));

        // Non-finite float in the stream: that ID is not set, later IDs still are.
        std::map<ParamID, double> got2;
        {
            auto stream =
                streamFrom(buildStream(kSavedGravity, floatFromBits(0x7FC00000u), kSavedWander, 1),
                           kStreamBytes);
            Steinberg::IBStreamer r(stream, kLittleEndian);
            loadResonanceParamsToController(r, [&got2](ParamID id, double n) { got2[id] = n; });
        }
        REQUIRE_FALSE(got2.contains(kResonanceMixId));
        REQUIRE(got2.at(kResonanceWanderRateId) ==
                Approx(wanderToNormalized(static_cast<double>(kSavedWander))).margin(1e-9));
        REQUIRE(got2.at(kResonanceAnchorModeId) == Approx(0.5).margin(1e-9));
    }

    // --------------------------------------------------------------------------
    SECTION("FormatNonEmpty") {
        for (const ParamID id : {ParamID{kResonanceGravityId}, ParamID{kResonanceMixId},
                                 ParamID{kResonanceWanderRateId}}) {
            for (const double v : {0.0, 0.5, 1.0}) {
                INFO("id " << id << " v " << v);
                Steinberg::Vst::String128 s{};
                REQUIRE(formatResonanceParam(id, v, s) == Steinberg::kResultOk);
                REQUIRE_FALSE(toAscii(s).empty());
            }
        }
        // The list formats itself; unknown IDs are refused.
        Steinberg::Vst::String128 s{};
        REQUIRE(formatResonanceParam(kResonanceAnchorModeId, 0.5, s) == Steinberg::kResultFalse);
        REQUIRE(formatResonanceParam(ParamID{450}, 0.5, s) == Steinberg::kResultFalse);
    }
}
