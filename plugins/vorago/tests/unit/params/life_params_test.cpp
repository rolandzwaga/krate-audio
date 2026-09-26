// ==============================================================================
// Vorago Phase 12 - life parameter pack contract (T029)
// ==============================================================================
// Shared Group 7 contract (specs/vorago-phase12-parameters/tasks.md): N = 3, B = 12.
//   1500 Breathing Depth        %  [0, 1]  0.30  lin
//   1501 Breathing Irregularity %  [0, 1]  0.30  lin
//   1502 Tidal Depth            %  [0, 1]  0.40  lin
// This TU is compiled with -fno-fast-math (tests/CMakeLists.txt): it injects
// NaN / +-Inf bit patterns into the loader.
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "parameters/life_params.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <array>
#include <atomic>
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

constexpr int kN = 3;
constexpr Steinberg::int64 kB = 12;

struct LifeRow {
    ParamID id;
    const char* title;
    float defaultPlain;
    double n0;
};

const std::array<LifeRow, kN> kRows = {{
    {.id=::Vorago::kLifeBreathingDepthId, .title="Breathing Depth", .defaultPlain=0.30f, .n0=0.30},
    {.id=::Vorago::kLifeBreathingIrregularityId, .title="Breathing Irregularity", .defaultPlain=0.30f, .n0=0.30},
    {.id=::Vorago::kLifeTidalDepthId, .title="Tidal Depth", .defaultPlain=0.40f, .n0=0.40},
}};

std::atomic<float>& field(::Vorago::LifeParams& p, int i) {
    switch (i) {
        case 0: return p.breathingDepth;
        case 1: return p.breathingIrregularity;
        default: return p.tidalDepth;
    }
}

std::array<std::uint32_t, kN> snapshotBits(::Vorago::LifeParams& p) {
    std::array<std::uint32_t, kN> bits{};
    for (int i = 0; i < kN; ++i) {
        bits[static_cast<std::size_t>(i)] = std::bit_cast<std::uint32_t>(field(p, i).load());
    }
    return bits;
}

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

/// A stream holding the given raw 32-bit words, little-endian via IBStreamer.
Steinberg::IPtr<Steinberg::MemoryStream> streamOfFloats(const std::array<float, kN>& values) {
    auto s = makeStream();
    Steinberg::IBStreamer w(s, kLittleEndian);  // kLittleEndian is a macro (fstreamer.h)
    for (const float v : values) { REQUIRE(w.writeFloat(v)); }
    rewindStream(*s);
    return s;
}

float floatFromBits(std::uint32_t bits) {
    volatile std::uint32_t vb = bits;  // defeat constant folding of the pattern
    const std::uint32_t b = vb;
    float f = 0.0f;
    std::memcpy(&f, &b, sizeof f);
    return f;
}

// Non-default, in-range values used by SECTIONs 4, 5 and 7.
constexpr std::array<float, kN> kSavedValues = {0.125f, 0.8125f, 0.0625f};
// Pre-dirt values for SECTION 5 (distinct from saved values and defaults).
constexpr std::array<float, kN> kDirtValues = {0.9375f, 0.0078125f, 0.5f};

void setAll(::Vorago::LifeParams& p, const std::array<float, kN>& v) {
    for (int i = 0; i < kN; ++i) { field(p, i).store(v[static_cast<std::size_t>(i)]); }
}

}  // namespace

TEST_CASE("Vorago_LifeParamsContract", "[vorago][params]") {
    using namespace Steinberg;
    using namespace Steinberg::Vst;

    SECTION("Registration") {
        ParameterContainer pc;
        ::Vorago::registerLifeParams(pc);
        REQUIRE(pc.getParameterCount() == kN);
        for (const auto& row : kRows) {
            auto* param = pc.getParameter(row.id);
            REQUIRE(param != nullptr);
            const auto& info = param->getInfo();
            CHECK(toAscii(info.title) == row.title);
            CHECK(toAscii(info.units) == "%");
            CHECK(info.stepCount == 0);
            CHECK(info.flags == ParameterInfo::kCanAutomate);
            CHECK(info.defaultNormalizedValue == Approx(row.n0).margin(1e-9));
        }
    }

    SECTION("DefaultsDenormalizeToPlain") {
        const ::Vorago::LifeParams fresh;
        CHECK(fresh.breathingDepth.load() == 0.30f);
        CHECK(fresh.breathingIrregularity.load() == 0.30f);
        CHECK(fresh.tidalDepth.load() == 0.40f);

        for (int i = 0; i < kN; ++i) {
            const auto& row = kRows[static_cast<std::size_t>(i)];
            ::Vorago::LifeParams p;
            ::Vorago::handleLifeParamChange(p, row.id, row.n0);
            CHECK(field(p, i).load() == Approx(row.defaultPlain).epsilon(1e-6));

            ::Vorago::handleLifeParamChange(p, row.id, 0.0);
            CHECK(field(p, i).load() == 0.0f);
            ::Vorago::handleLifeParamChange(p, row.id, 1.0);
            CHECK(field(p, i).load() == 1.0f);
        }
    }

    SECTION("UnregisteredInBandIgnored") {
        ::Vorago::LifeParams p;
        setAll(p, kSavedValues);
        const auto before = snapshotBits(p);
        for (const ParamID id : {ParamID{1503}, ParamID{1504}, ParamID{1550}, ParamID{1598},
                                 ParamID{1599}}) {
            for (const double v : {0.0, 0.5, 1.0}) {
                ::Vorago::handleLifeParamChange(p, id, v);
                REQUIRE(snapshotBits(p) == before);
            }
        }
    }

    SECTION("SaveLoadRoundTrip") {
        ::Vorago::LifeParams src;
        setAll(src, kSavedValues);
        auto s = makeStream();
        {
            IBStreamer w(s, kLittleEndian);
            ::Vorago::saveLifeParams(src, w);
        }
        REQUIRE(s->getSize() == kB);

        rewindStream(*s);
        ::Vorago::LifeParams dst;
        IBStreamer r(s, kLittleEndian);
        REQUIRE(::Vorago::loadLifeParams(dst, r));
        REQUIRE(snapshotBits(dst) == snapshotBits(src));
    }

    SECTION("TruncationEveryOffset") {
        ::Vorago::LifeParams src;
        setAll(src, kSavedValues);
        auto full = makeStream();
        {
            IBStreamer w(full, kLittleEndian);
            ::Vorago::saveLifeParams(src, w);
        }
        REQUIRE(full->getSize() == kB);
        const auto* bytes = reinterpret_cast<const unsigned char*>(full->getData());  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)

        for (Steinberg::int64 c = 0; c < kB; ++c) {
            auto cut = makeStream();
            if (c > 0) {
                Steinberg::int32 written = 0;
                REQUIRE(cut->write(const_cast<unsigned char*>(bytes),  // NOLINT(cppcoreguidelines-pro-type-const-cast)
                                   static_cast<Steinberg::int32>(c), &written) == kResultOk);
                REQUIRE(written == static_cast<Steinberg::int32>(c));
            }
            rewindStream(*cut);

            ::Vorago::LifeParams dst;
            setAll(dst, kDirtValues);
            IBStreamer r(cut, kLittleEndian);
            REQUIRE_FALSE(::Vorago::loadLifeParams(dst, r));

            const int complete = static_cast<int>(c / 4);  // fields fully inside [0, c)
            for (int i = 0; i < kN; ++i) {
                const auto idx = static_cast<std::size_t>(i);
                const float expected = (i < complete) ? kSavedValues[idx] : kDirtValues[idx];
                CHECK(std::bit_cast<std::uint32_t>(field(dst, i).load()) ==
                      std::bit_cast<std::uint32_t>(expected));
            }
        }
    }

    SECTION("NonFiniteAndClamp") {
        const std::array<std::uint32_t, 3> nonFinite = {0x7FC00000u, 0x7F800000u, 0xFF800000u};
        for (const std::uint32_t pattern : nonFinite) {
            for (int bad = 0; bad < kN; ++bad) {
                std::array<float, kN> values = kSavedValues;
                values[static_cast<std::size_t>(bad)] = floatFromBits(pattern);
                auto s = streamOfFloats(values);

                ::Vorago::LifeParams dst;
                setAll(dst, kDirtValues);
                IBStreamer r(s, kLittleEndian);
                REQUIRE(::Vorago::loadLifeParams(dst, r));
                for (int i = 0; i < kN; ++i) {
                    const auto idx = static_cast<std::size_t>(i);
                    const float expected = (i == bad) ? kDirtValues[idx] : kSavedValues[idx];
                    CHECK(std::bit_cast<std::uint32_t>(field(dst, i).load()) ==
                          std::bit_cast<std::uint32_t>(expected));
                }
            }
        }

        // Finite out-of-range floats clamp to the plain range [0, 1].
        {
            auto s = streamOfFloats({-0.5f, 1.5f, 1.0e30f});
            ::Vorago::LifeParams dst;
            IBStreamer r(s, kLittleEndian);
            REQUIRE(::Vorago::loadLifeParams(dst, r));
            CHECK(dst.breathingDepth.load() == 0.0f);
            CHECK(dst.breathingIrregularity.load() == 1.0f);
            CHECK(dst.tidalDepth.load() == 1.0f);
        }
        {
            auto s = streamOfFloats({2.0f, -1.0e30f, -0.0001f});
            ::Vorago::LifeParams dst;
            IBStreamer r(s, kLittleEndian);
            REQUIRE(::Vorago::loadLifeParams(dst, r));
            CHECK(dst.breathingDepth.load() == 1.0f);
            CHECK(dst.breathingIrregularity.load() == 0.0f);
            CHECK(dst.tidalDepth.load() == 0.0f);
        }
        // (No discrete fields in this pack: the index-clamp arm does not apply.)
    }

    SECTION("ControllerMirror") {
        ::Vorago::LifeParams src;
        setAll(src, kSavedValues);
        auto s = makeStream();
        {
            IBStreamer w(s, kLittleEndian);
            ::Vorago::saveLifeParams(src, w);
        }
        rewindStream(*s);

        std::vector<std::pair<ParamID, double>> calls;
        IBStreamer r(s, kLittleEndian);
        ::Vorago::loadLifeParamsToController(
            r, [&calls](ParamID id, double n) { calls.emplace_back(id, n); });

        REQUIRE(calls.size() == static_cast<std::size_t>(kN));
        for (int i = 0; i < kN; ++i) {
            const auto idx = static_cast<std::size_t>(i);
            CHECK(calls[idx].first == kRows[idx].id);
            const double expected =
                ::Vorago::linearToNormalized(static_cast<double>(kSavedValues[idx]), 0.0, 1.0);
            CHECK(calls[idx].second == Approx(expected).margin(1e-9));
        }
    }

    SECTION("FormatNonEmpty") {
        for (const auto& row : kRows) {
            for (const double v : {0.0, 0.5, 1.0}) {
                String128 str{};
                REQUIRE(::Vorago::formatLifeParam(row.id, v, str) == kResultOk);
                CHECK_FALSE(toAscii(str).empty());
            }
        }
        String128 str{};
        CHECK(::Vorago::formatLifeParam(1503, 0.5, str) == kResultFalse);
    }
}
