// ==============================================================================
// Vorago Phase 12 - ecosystem parameter pack contract (T023)
// ==============================================================================
// The eight-SECTION pack contract of specs/vorago-phase12-parameters/tasks.md
// (Group 7 shared test shape) for the Ecosystem pack: N = 1, B = 4.
//   900 Ecosystem Depth, %, [0, 1], default 0.85, n0 0.85, linear.
// Fast-math-off TU (plugins/vorago/tests/CMakeLists.txt): injects NaN / +-Inf.
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "parameters/ecosystem_params.h"
#include "parameters/param_mapping.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr int kN = 1;             // registered IDs
constexpr Steinberg::int64 kB = 4;  // stream bytes

Steinberg::IPtr<Steinberg::MemoryStream> makeStream() {
    return Steinberg::owned(new Steinberg::MemoryStream());
}

void rewindStream(Steinberg::MemoryStream& s) {
    REQUIRE(s.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) == Steinberg::kResultOk);
}

std::uint32_t bitsOf(float f) { return std::bit_cast<std::uint32_t>(f); }

std::string toAsciiString(const Steinberg::Vst::TChar* s) {
    char buf[128] = {};
    Steinberg::UString(const_cast<Steinberg::Vst::TChar*>(s), 128)  // NOLINT(cppcoreguidelines-pro-type-const-cast)
        .toAscii(buf, 128);
    return {buf};
}

float bitsToFloat(std::uint32_t bits) {
    volatile std::uint32_t v = bits;  // defeat constant folding under any float model
    const std::uint32_t copy = v;
    float f = 0.0f;
    std::memcpy(&f, &copy, sizeof(f));
    return f;
}

void setNonDefaults(::Vorago::EcosystemParams& p) { p.depth.store(0.3f); }

std::vector<char> saveBytes(const ::Vorago::EcosystemParams& p) {
    auto s = makeStream();
    {
        Steinberg::IBStreamer w(s, kLittleEndian);
        ::Vorago::saveEcosystemParams(p, w);
    }
    const auto size = static_cast<std::size_t>(s->getSize());
    return {s->getData(), s->getData() + size};
}

Steinberg::IPtr<Steinberg::MemoryStream> streamFrom(const std::vector<char>& bytes,
                                                    std::size_t count) {
    auto s = makeStream();
    if (count > 0) {
        Steinberg::int32 written = 0;
        REQUIRE(s->write(const_cast<char*>(bytes.data()),  // NOLINT(cppcoreguidelines-pro-type-const-cast)
                         static_cast<Steinberg::int32>(count), &written) == Steinberg::kResultOk);
        REQUIRE(std::cmp_equal(written, count));
    }
    rewindStream(*s);
    return s;
}

}  // namespace

TEST_CASE("Vorago_EcosystemParamsContract", "[vorago][params]") {
    using Catch::Approx;
    using namespace ::Vorago;
    using namespace Steinberg::Vst;

    SECTION("Registration") {
        ParameterContainer pc;
        registerEcosystemParams(pc);
        REQUIRE(pc.getParameterCount() == kN);

        Parameter* p = pc.getParameter(kEcosystemDepthId);
        REQUIRE(p != nullptr);
        const ParameterInfo& info = p->getInfo();
        REQUIRE(toAsciiString(info.title) == "Ecosystem Depth");
        REQUIRE(toAsciiString(info.units) == "%");
        REQUIRE(info.stepCount == 0);
        REQUIRE((info.flags & ParameterInfo::kCanAutomate) != 0);
        REQUIRE((info.flags & ParameterInfo::kIsList) == 0);
        REQUIRE(info.defaultNormalizedValue == Approx(0.85).margin(1e-9));
    }

    SECTION("DefaultsDenormalizeToPlain") {
        const EcosystemParams fresh;
        REQUIRE(fresh.depth.load() == Approx(0.85f).epsilon(1e-6));

        EcosystemParams p;
        p.depth.store(0.1f);
        handleEcosystemParamChange(p, kEcosystemDepthId, 0.85);
        REQUIRE(p.depth.load() == Approx(fresh.depth.load()).epsilon(1e-6));

        handleEcosystemParamChange(p, kEcosystemDepthId, 0.0);
        REQUIRE(p.depth.load() == 0.0f);
        handleEcosystemParamChange(p, kEcosystemDepthId, 1.0);
        REQUIRE(p.depth.load() == 1.0f);
    }

    SECTION("UnregisteredInBandIgnored") {
        constexpr std::array<ParamID, 4> kUnused{901, 902, 950, kEcosystemParamRangeEnd - 1};
        EcosystemParams p;
        p.depth.store(0.42f);
        const std::uint32_t before = bitsOf(p.depth.load());
        for (const ParamID id : kUnused) {
            for (const double v : {0.0, 0.5, 1.0}) {
                handleEcosystemParamChange(p, id, v);
                REQUIRE(bitsOf(p.depth.load()) == before);
            }
        }
    }

    SECTION("SaveLoadRoundTrip") {
        EcosystemParams src;
        setNonDefaults(src);
        const auto bytes = saveBytes(src);
        REQUIRE(static_cast<Steinberg::int64>(bytes.size()) == kB);

        auto s = streamFrom(bytes, bytes.size());
        EcosystemParams dst;
        Steinberg::IBStreamer r(s, kLittleEndian);
        REQUIRE(loadEcosystemParams(dst, r));
        REQUIRE(bitsOf(dst.depth.load()) == bitsOf(src.depth.load()));
    }

    SECTION("TruncationEveryOffset") {
        EcosystemParams src;
        setNonDefaults(src);
        const auto bytes = saveBytes(src);
        REQUIRE(static_cast<Steinberg::int64>(bytes.size()) == kB);

        for (std::size_t c = 0; c < bytes.size(); ++c) {
            auto s = streamFrom(bytes, c);
            EcosystemParams dst;
            dst.depth.store(0.77f);  // pre-dirtied
            Steinberg::IBStreamer r(s, kLittleEndian);
            REQUIRE_FALSE(loadEcosystemParams(dst, r));
            // The only field spans [0, 4), never fully contained in [0, c): unchanged.
            REQUIRE(bitsOf(dst.depth.load()) == bitsOf(0.77f));
        }
    }

    SECTION("NonFiniteAndClamp") {
        const std::array<std::uint32_t, 3> kBadBits{0x7FC00000u, 0x7F800000u, 0xFF800000u};
        for (const std::uint32_t bad : kBadBits) {
            auto s = makeStream();
            {
                Steinberg::IBStreamer w(s, kLittleEndian);
                w.writeFloat(bitsToFloat(bad));
            }
            rewindStream(*s);
            EcosystemParams dst;
            dst.depth.store(0.61f);
            Steinberg::IBStreamer r(s, kLittleEndian);
            REQUIRE(loadEcosystemParams(dst, r));  // read succeeded; value rejected
            REQUIRE(bitsOf(dst.depth.load()) == bitsOf(0.61f));
        }

        const std::array<std::pair<float, float>, 2> kClamp{{{1.5f, 1.0f}, {-0.5f, 0.0f}}};
        for (const auto& [in, expected] : kClamp) {
            auto s = makeStream();
            {
                Steinberg::IBStreamer w(s, kLittleEndian);
                w.writeFloat(in);
            }
            rewindStream(*s);
            EcosystemParams dst;
            Steinberg::IBStreamer r(s, kLittleEndian);
            REQUIRE(loadEcosystemParams(dst, r));
            REQUIRE(bitsOf(dst.depth.load()) == bitsOf(expected));
        }
    }

    SECTION("ControllerMirror") {
        EcosystemParams src;
        setNonDefaults(src);
        const auto bytes = saveBytes(src);
        auto s = streamFrom(bytes, bytes.size());

        std::vector<std::pair<ParamID, double>> calls;
        Steinberg::IBStreamer r(s, kLittleEndian);
        loadEcosystemParamsToController(
            r, [&calls](ParamID id, ParamValue v) { calls.emplace_back(id, v); });

        REQUIRE(calls.size() == static_cast<std::size_t>(kN));
        REQUIRE(calls[0].first == kEcosystemDepthId);
        REQUIRE(calls[0].second ==
                Approx(linearToNormalized(static_cast<double>(src.depth.load()), 0.0, 1.0))
                    .margin(1e-9));
    }

    SECTION("FormatNonEmpty") {
        for (const double v : {0.0, 0.5, 1.0}) {
            String128 str{};
            REQUIRE(formatEcosystemParam(kEcosystemDepthId, v, str) == Steinberg::kResultOk);
            REQUIRE_FALSE(toAsciiString(str).empty());
        }
        String128 str{};
        REQUIRE(formatEcosystemParam(kEcosystemDepthId + 1, 0.5, str) == Steinberg::kResultFalse);
    }
}
