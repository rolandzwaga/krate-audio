// ==============================================================================
// Vorago Phase 12 - bloom parameter pack contract (T027)
// ==============================================================================
// The Group 7 shared test shape (specs/vorago-phase12-parameters/tasks.md) for
// plugins/vorago/src/parameters/bloom_params.h: N = 2 parameters, B = 8 bytes.
//   1300 Bloom Depth       %   [0, 1]     0.60   n0 0.60         lin
//   1301 Bloom Spawn Rate  Hz  [0, 0.05]  1/240  n0 0.603772849  olog eps 1e-4 Hz
// Extra (T027): spawn rate n = 0 -> exactly 0 Hz.
//
// Built with -fno-fast-math (tests/CMakeLists.txt); NaN/Inf payloads are made
// from bit patterns through a volatile uint32 + memcpy.
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "parameters/bloom_params.h"
#include "parameters/param_mapping.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <krate/dsp/systems/bloom_engine.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

using Catch::Approx;
using Steinberg::Vst::ParamID;

constexpr int kN = 2;
constexpr Steinberg::int64 kB = 8;

constexpr std::array<ParamID, kN> kIds = {Vorago::kBloomDepthId, Vorago::kBloomSpawnRateId};

std::string toAscii(const Steinberg::Vst::TChar* s) {
    char buf[128] = {};
    Steinberg::UString(const_cast<Steinberg::Vst::TChar*>(s), 128)  // NOLINT(cppcoreguidelines-pro-type-const-cast)
        .toAscii(buf, 128);
    return {buf};
}

Steinberg::IPtr<Steinberg::MemoryStream> makeStream() {
    return Steinberg::owned(new Steinberg::MemoryStream());
}

void rewindStream(Steinberg::MemoryStream& s) {
    REQUIRE(s.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) == Steinberg::kResultOk);
}

float floatFromBits(std::uint32_t pattern) {
    volatile std::uint32_t v = pattern;
    const std::uint32_t b = v;
    float f = 0.0f;
    std::memcpy(&f, &b, sizeof(f));
    return f;
}

using Bits = std::array<std::uint32_t, kN>;

Bits snapshot(const Vorago::BloomParams& p) {
    return {std::bit_cast<std::uint32_t>(p.depth.load()),
            std::bit_cast<std::uint32_t>(p.spawnRateHz.load())};
}

void setFields(Vorago::BloomParams& p, float depth, float rate) {
    p.depth.store(depth);
    p.spawnRateHz.store(rate);
}

// Stream of two raw floats, in field order.
Steinberg::IPtr<Steinberg::MemoryStream> streamOf(float a, float b) {
    auto s = makeStream();
    Steinberg::IBStreamer w(s, kLittleEndian);
    w.writeFloat(a);
    w.writeFloat(b);
    rewindStream(*s);
    return s;
}

double spawnToNorm(double hz) {
    return Vorago::offsetLogToNormalized(hz, 0.0, 0.05, 1e-4);
}

}  // namespace

TEST_CASE("Vorago_BloomParamsContract", "[vorago][params]") {
    using namespace Vorago;

    // Non-default, in-range values used by SECTIONs 4, 5, 7.
    constexpr float kDepthY = 0.25f;
    constexpr float kRateY = 0.02f;

    SECTION("Registration") {
        Steinberg::Vst::ParameterContainer pc;
        registerBloomParams(pc);
        REQUIRE(pc.getParameterCount() == kN);

        struct Expect {
            ParamID id;
            const char* title;
            const char* units;
            double n0;
        };
        const std::array<Expect, kN> expect = {{
            {.id=kBloomDepthId, .title="Bloom Depth", .units="%", .n0=0.60},
            {.id=kBloomSpawnRateId, .title="Bloom Spawn Rate", .units="Hz", .n0=0.603772849},
        }};
        for (const auto& e : expect) {
            auto* p = pc.getParameter(e.id);
            REQUIRE(p != nullptr);
            const auto& info = p->getInfo();
            CHECK(toAscii(info.title) == e.title);
            CHECK(toAscii(info.units) == e.units);
            CHECK(info.stepCount == 0);
            CHECK(info.flags == Steinberg::Vst::ParameterInfo::kCanAutomate);
            CHECK(info.defaultNormalizedValue == Approx(e.n0).margin(1e-9));
        }
        // The registered spawn-rate default really is the inverse map of the engine default.
        CHECK(spawnToNorm(1.0 / 240.0) == Approx(0.603772849).margin(1e-9));
    }

    SECTION("DefaultsDenormalizeToPlain") {
        const BloomParams fresh;
        CHECK(fresh.depth.load() == 0.60f);
        CHECK(fresh.spawnRateHz.load() == Krate::DSP::BloomEngine::kDefaultSpawnRateHz);

        BloomParams p;
        setFields(p, 0.9f, 0.04f);
        handleBloomParamChange(p, kBloomDepthId, 0.60);
        handleBloomParamChange(p, kBloomSpawnRateId, 0.603772849);
        CHECK(p.depth.load() == Approx(fresh.depth.load()).epsilon(1e-6));
        CHECK(p.spawnRateHz.load() == Approx(fresh.spawnRateHz.load()).epsilon(1e-6));

        // n = 0 / n = 1 -> range ends.
        handleBloomParamChange(p, kBloomDepthId, 0.0);
        CHECK(p.depth.load() == 0.0f);
        handleBloomParamChange(p, kBloomDepthId, 1.0);
        CHECK(p.depth.load() == 1.0f);

        handleBloomParamChange(p, kBloomSpawnRateId, 1.0);
        CHECK(p.spawnRateHz.load() == Approx(0.05f).epsilon(1e-6));
        CHECK(p.spawnRateHz.load() <= Krate::DSP::BloomEngine::kMaxSpawnRateHz);

        // T027: n = 0 -> EXACTLY 0 Hz (internal clock off), and bit-exact +0.
        handleBloomParamChange(p, kBloomSpawnRateId, 0.0);
        CHECK(p.spawnRateHz.load() == 0.0f);
        CHECK(std::bit_cast<std::uint32_t>(p.spawnRateHz.load()) == 0u);

        // Midpoint (plan 3.3.1): 0.002138 Hz.
        handleBloomParamChange(p, kBloomSpawnRateId, 0.5);
        CHECK(static_cast<double>(p.spawnRateHz.load()) == Approx(0.002138).margin(1e-6));
    }

    SECTION("UnregisteredInBandIgnored") {
        BloomParams p;
        setFields(p, 0.33f, 0.011f);
        const Bits before = snapshot(p);
        for (ParamID id = kBloomSpawnRateId + 1; id < kBloomParamRangeEnd; ++id) {
            for (const double v : {0.0, 0.5, 1.0}) {
                handleBloomParamChange(p, id, v);
            }
        }
        CHECK(snapshot(p) == before);
    }

    SECTION("SaveLoadRoundTrip") {
        BloomParams src;
        setFields(src, kDepthY, kRateY);
        auto s = makeStream();
        {
            Steinberg::IBStreamer w(s, kLittleEndian);
            saveBloomParams(src, w);
        }
        REQUIRE(s->getSize() == kB);

        rewindStream(*s);
        BloomParams dst;
        Steinberg::IBStreamer r(s, kLittleEndian);
        REQUIRE(loadBloomParams(dst, r));
        CHECK(snapshot(dst) == snapshot(src));
    }

    SECTION("TruncationEveryOffset") {
        BloomParams src;
        setFields(src, kDepthY, kRateY);
        auto full = makeStream();
        {
            Steinberg::IBStreamer w(full, kLittleEndian);
            saveBloomParams(src, w);
        }
        REQUIRE(full->getSize() == kB);

        constexpr float kDepthX = 0.77f;
        constexpr float kRateX = 0.033f;
        for (Steinberg::int64 c = 0; c < kB; ++c) {
            auto cut = makeStream();
            if (c > 0) {
                Steinberg::int32 written = 0;
                REQUIRE(cut->write(full->getData(), static_cast<Steinberg::int32>(c), &written) ==
                        Steinberg::kResultOk);
                REQUIRE(written == static_cast<Steinberg::int32>(c));
            }
            rewindStream(*cut);

            BloomParams p;
            setFields(p, kDepthX, kRateX);
            Steinberg::IBStreamer r(cut, kLittleEndian);
            INFO("cut = " << c);
            CHECK_FALSE(loadBloomParams(p, r));
            // Field 0 occupies [0, 4), field 1 [4, 8).
            CHECK(p.depth.load() == (c >= 4 ? kDepthY : kDepthX));
            CHECK(p.spawnRateHz.load() == kRateX);
        }
    }

    SECTION("NonFiniteAndClamp") {
        const std::array<std::uint32_t, 3> patterns = {0x7FC00000u, 0x7F800000u, 0xFF800000u};
        constexpr float kDepthX = 0.41f;
        constexpr float kRateX = 0.007f;

        for (const auto pattern : patterns) {
            INFO("pattern = " << pattern);
            const float bad = floatFromBits(pattern);

            {   // non-finite depth: unchanged, rate still loaded
                BloomParams p;
                setFields(p, kDepthX, kRateX);
                auto s = streamOf(bad, kRateY);
                Steinberg::IBStreamer r(s, kLittleEndian);
                CHECK(loadBloomParams(p, r));
                CHECK(p.depth.load() == kDepthX);
                CHECK(p.spawnRateHz.load() == kRateY);
            }
            {   // non-finite rate: unchanged, depth loaded
                BloomParams p;
                setFields(p, kDepthX, kRateX);
                auto s = streamOf(kDepthY, bad);
                Steinberg::IBStreamer r(s, kLittleEndian);
                CHECK(loadBloomParams(p, r));
                CHECK(p.depth.load() == kDepthY);
                CHECK(p.spawnRateHz.load() == kRateX);
            }
            {   // the controller mirror skips the same field
                auto s = streamOf(bad, bad);
                Steinberg::IBStreamer r(s, kLittleEndian);
                int calls = 0;
                loadBloomParamsToController(r, [&](ParamID, double) { ++calls; });
                CHECK(calls == 0);
            }
        }

        // Finite out-of-range floats clamp to the plain range.
        {
            BloomParams p;
            auto s = streamOf(1.5f, 0.2f);
            Steinberg::IBStreamer r(s, kLittleEndian);
            CHECK(loadBloomParams(p, r));
            CHECK(p.depth.load() == 1.0f);
            CHECK(p.spawnRateHz.load() == 0.05f);
        }
        {
            BloomParams p;
            auto s = streamOf(-0.5f, -1.0f);
            Steinberg::IBStreamer r(s, kLittleEndian);
            CHECK(loadBloomParams(p, r));
            CHECK(p.depth.load() == 0.0f);
            CHECK(p.spawnRateHz.load() == 0.0f);
        }
        // (No index fields in this pack: the index -1 / n clamp clause does not apply.)
    }

    SECTION("ControllerMirror") {
        BloomParams src;
        setFields(src, kDepthY, kRateY);
        auto s = makeStream();
        {
            Steinberg::IBStreamer w(s, kLittleEndian);
            saveBloomParams(src, w);
        }
        rewindStream(*s);

        std::vector<std::pair<ParamID, double>> got;
        Steinberg::IBStreamer r(s, kLittleEndian);
        loadBloomParamsToController(r, [&](ParamID id, double n) { got.emplace_back(id, n); });

        REQUIRE(got.size() == static_cast<std::size_t>(kN));
        CHECK(got[0].first == kBloomDepthId);
        CHECK(got[0].second ==
              Approx(linearToNormalized(static_cast<double>(kDepthY), 0.0, 1.0)).margin(1e-9));
        CHECK(got[1].first == kBloomSpawnRateId);
        CHECK(got[1].second == Approx(spawnToNorm(static_cast<double>(kRateY))).margin(1e-9));

        // And the mirror maps back to the saved plain value.
        BloomParams back;
        handleBloomParamChange(back, got[1].first, got[1].second);
        CHECK(back.spawnRateHz.load() == Approx(kRateY).epsilon(1e-6));
    }

    SECTION("FormatNonEmpty") {
        for (const auto id : kIds) {
            for (const double v : {0.0, 0.5, 1.0}) {
                Steinberg::Vst::String128 str{};
                REQUIRE(formatBloomParam(id, v, str) == Steinberg::kResultOk);
                CHECK_FALSE(toAscii(str).empty());
            }
        }
        Steinberg::Vst::String128 str{};
        CHECK(formatBloomParam(kBloomParamRangeEnd - 1, 0.5, str) == Steinberg::kResultFalse);
    }
}
