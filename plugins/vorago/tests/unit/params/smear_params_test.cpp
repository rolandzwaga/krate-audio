// ==============================================================================
// Vorago Phase 12 - smear parameter pack contract (T021, FR-011/012/013, SC-018)
// ==============================================================================
// Pack: 700 Smear Amount (%, [0,1], 0.20), 701 Smear Decoherence (%, [0,1], 0.20),
// 702 Smear Tilt ("", [-1,1], 0.0 -> n0 0.5). All continuous, linear taper.
// Stream: 3 x float32 = 12 bytes, ascending ID order.
//
// Built with -fno-fast-math (tests/CMakeLists.txt): the NaN/Inf payloads below are
// produced from bit patterns through a volatile uint32 + memcpy.
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "parameters/param_mapping.h"
#include "parameters/smear_params.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace {

using Steinberg::Vst::ParamID;

constexpr int kSmearN = 3;
constexpr std::size_t kSmearBytes = 12;

struct SmearRow {
    ParamID id;
    const char* title;
    const char* units;
    double mn;
    double mx;
    double def;
    double n0;
};

const std::array<SmearRow, kSmearN>& smearRows() {
    static const std::array<SmearRow, kSmearN> rows = {{
        {.id=::Vorago::kSmearAmountId, .title="Smear Amount", .units="%", .mn=0.0, .mx=1.0, .def=0.20, .n0=0.20},
        {.id=::Vorago::kSmearDecoherenceId, .title="Smear Decoherence", .units="%", .mn=0.0, .mx=1.0, .def=0.20, .n0=0.20},
        {.id=::Vorago::kSmearTiltId, .title="Smear Tilt", .units="", .mn=-1.0, .mx=1.0, .def=0.0, .n0=0.5},
    }};
    return rows;
}

std::atomic<float>& fieldOf(::Vorago::SmearParams& p, int i) {
    switch (i) {
        case 0: return p.amount;
        case 1: return p.decoherence;
        default: return p.tilt;
    }
}

float loadField(const ::Vorago::SmearParams& p, int i) {
    switch (i) {
        case 0: return p.amount.load();
        case 1: return p.decoherence.load();
        default: return p.tilt.load();
    }
}

std::array<std::uint32_t, kSmearN> snapshot(const ::Vorago::SmearParams& p) {
    return {std::bit_cast<std::uint32_t>(p.amount.load()),
            std::bit_cast<std::uint32_t>(p.decoherence.load()),
            std::bit_cast<std::uint32_t>(p.tilt.load())};
}

std::string toAscii(const Steinberg::Vst::String128 s) {
    std::string out;
    for (int i = 0; i < 128 && s[i] != 0; ++i)
        out.push_back(static_cast<char>(s[i]));
    return out;
}

Steinberg::IPtr<Steinberg::MemoryStream> makeStream() {
    return Steinberg::owned(new Steinberg::MemoryStream());
}

void rewindStream(Steinberg::MemoryStream& s) {
    REQUIRE(s.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) == Steinberg::kResultOk);
}

Steinberg::IPtr<Steinberg::MemoryStream> streamFromFloats(const std::vector<float>& values) {
    auto s = makeStream();
    {
        Steinberg::IBStreamer w(s, kLittleEndian);
        for (float v : values)
            REQUIRE(w.writeFloat(v));
    }
    rewindStream(*s);
    return s;
}

Steinberg::IPtr<Steinberg::MemoryStream> prefixOf(const Steinberg::MemoryStream& src,
                                                  std::size_t count) {
    auto s = makeStream();
    if (count > 0) {
        Steinberg::int32 written = 0;
        REQUIRE(s->write(src.getData(), static_cast<Steinberg::int32>(count), &written) ==
                Steinberg::kResultOk);
        REQUIRE(std::cmp_equal(written, count));
    }
    rewindStream(*s);
    return s;
}

float floatFromBits(std::uint32_t pattern) {
    volatile std::uint32_t bits = pattern;
    const std::uint32_t copy = bits;
    float f = 0.0f;
    std::memcpy(&f, &copy, sizeof(f));
    return f;
}

// Non-default, in-range values per field.
constexpr std::array<float, kSmearN> kNonDefault = {0.65f, 0.83f, -0.4f};
// Pre-dirty values (distinct from both defaults and kNonDefault).
constexpr std::array<float, kSmearN> kDirty = {0.9f, 0.05f, 0.75f};

void setAll(::Vorago::SmearParams& p, const std::array<float, kSmearN>& v) {
    for (int i = 0; i < kSmearN; ++i)
        fieldOf(p, i).store(v[static_cast<std::size_t>(i)]);
}

}  // namespace

TEST_CASE("Vorago_SmearParamsContract", "[vorago][params]") {
    using Catch::Approx;
    using namespace ::Vorago;
    using namespace Steinberg::Vst;

    SECTION("Registration") {
        ParameterContainer pc;
        registerSmearParams(pc);
        REQUIRE(pc.getParameterCount() == kSmearN);
        for (const auto& row : smearRows()) {
            INFO("id " << row.id);
            Parameter* param = pc.getParameter(row.id);
            REQUIRE(param != nullptr);
            const ParameterInfo& info = param->getInfo();
            CHECK(toAscii(info.title) == row.title);
            CHECK(toAscii(info.units) == row.units);
            CHECK(info.stepCount == 0);
            CHECK((info.flags & ParameterInfo::kCanAutomate) != 0);
            CHECK((info.flags & ParameterInfo::kIsList) == 0);
            CHECK(info.defaultNormalizedValue == Approx(row.n0).margin(1e-9));
        }
    }

    SECTION("DefaultsDenormalizeToPlain") {
        const SmearParams fresh;
        for (int i = 0; i < kSmearN; ++i) {
            const auto& row = smearRows()[static_cast<std::size_t>(i)];
            INFO("id " << row.id);
            // Fresh struct holds the default plain value.
            CHECK(static_cast<double>(loadField(fresh, i)) ==
                  Approx(row.def).epsilon(1e-6).margin(1e-12));

            SmearParams p;
            setAll(p, kDirty);
            handleSmearParamChange(p, row.id, row.n0);
            CHECK(static_cast<double>(fieldOf(p, i).load()) ==
                  Approx(row.def).epsilon(1e-6).margin(1e-12));
            CHECK(fieldOf(p, i).load() ==
                  loadField(fresh, i));

            handleSmearParamChange(p, row.id, 0.0);
            CHECK(static_cast<double>(fieldOf(p, i).load()) ==
                  Approx(row.mn).epsilon(1e-6).margin(1e-12));
            handleSmearParamChange(p, row.id, 1.0);
            CHECK(static_cast<double>(fieldOf(p, i).load()) ==
                  Approx(row.mx).epsilon(1e-6).margin(1e-12));
        }
    }

    SECTION("UnregisteredInBandIgnored") {
        SmearParams p;
        setAll(p, kNonDefault);
        const auto before = snapshot(p);
        for (ParamID id = kSmearTiltId + 1; id < kSmearParamRangeEnd; ++id) {
            for (double n : {0.0, 0.5, 1.0}) {
                handleSmearParamChange(p, id, n);
            }
        }
        // Neighbouring bands are not this pack's either.
        handleSmearParamChange(p, kSmearAmountId - 1, 0.0);
        handleSmearParamChange(p, kSmearParamRangeEnd, 1.0);
        CHECK(snapshot(p) == before);
    }

    // Stream shared by SECTIONs 4, 5 and 7.
    SmearParams saved;
    setAll(saved, kNonDefault);
    auto stream = makeStream();
    {
        Steinberg::IBStreamer w(stream, kLittleEndian);
        saveSmearParams(saved, w);
    }

    SECTION("SaveLoadRoundTrip") {
        REQUIRE(std::cmp_equal(stream->getSize(), kSmearBytes));
        rewindStream(*stream);
        SmearParams loaded;
        Steinberg::IBStreamer r(stream, kLittleEndian);
        REQUIRE(loadSmearParams(loaded, r));
        CHECK(snapshot(loaded) == snapshot(saved));
    }

    SECTION("TruncationEveryOffset") {
        REQUIRE(std::cmp_equal(stream->getSize(), kSmearBytes));
        for (std::size_t cut = 0; cut < kSmearBytes; ++cut) {
            INFO("cut " << cut);
            auto shortStream = prefixOf(*stream, cut);
            SmearParams p;
            setAll(p, kDirty);
            Steinberg::IBStreamer r(shortStream, kLittleEndian);
            CHECK_FALSE(loadSmearParams(p, r));
            for (int i = 0; i < kSmearN; ++i) {
                const auto k = static_cast<std::size_t>(i);
                const bool fullyContained = (k + 1u) * 4u <= cut;
                const float expected = fullyContained ? kNonDefault[k] : kDirty[k];
                CHECK(std::bit_cast<std::uint32_t>(fieldOf(p, i).load()) ==
                      std::bit_cast<std::uint32_t>(expected));
            }
        }
    }

    SECTION("NonFiniteAndClamp") {
        const std::array<float, 3> nonFinite = {floatFromBits(0x7FC00000u),   // NaN
                                                floatFromBits(0x7F800000u),   // +Inf
                                                floatFromBits(0xFF800000u)};  // -Inf
        for (int bad = 0; bad < kSmearN; ++bad) {
            for (float nf : nonFinite) {
                INFO("field " << bad);
                std::vector<float> values(kNonDefault.begin(), kNonDefault.end());
                values[static_cast<std::size_t>(bad)] = nf;
                auto s = streamFromFloats(values);
                SmearParams p;
                setAll(p, kDirty);
                Steinberg::IBStreamer r(s, kLittleEndian);
                CHECK(loadSmearParams(p, r));
                for (int i = 0; i < kSmearN; ++i) {
                    const auto k = static_cast<std::size_t>(i);
                    const float expected = (i == bad) ? kDirty[k] : kNonDefault[k];
                    CHECK(std::bit_cast<std::uint32_t>(fieldOf(p, i).load()) ==
                          std::bit_cast<std::uint32_t>(expected));
                }
            }
        }

        // Finite out-of-range floats clamp to the plain range (no index fields in this pack).
        {
            auto s = streamFromFloats({2.5f, -3.0f, 7.0f});
            SmearParams p;
            Steinberg::IBStreamer r(s, kLittleEndian);
            REQUIRE(loadSmearParams(p, r));
            CHECK(p.amount.load() == 1.0f);
            CHECK(p.decoherence.load() == 0.0f);
            CHECK(p.tilt.load() == 1.0f);
        }
        {
            auto s = streamFromFloats({-0.5f, 1.5f, -7.0f});
            SmearParams p;
            Steinberg::IBStreamer r(s, kLittleEndian);
            REQUIRE(loadSmearParams(p, r));
            CHECK(p.amount.load() == 0.0f);
            CHECK(p.decoherence.load() == 1.0f);
            CHECK(p.tilt.load() == -1.0f);
        }
    }

    SECTION("ControllerMirror") {
        rewindStream(*stream);
        std::vector<std::pair<ParamID, double>> calls;
        Steinberg::IBStreamer r(stream, kLittleEndian);
        loadSmearParamsToController(
            r, [&calls](ParamID id, double norm) { calls.emplace_back(id, norm); });
        REQUIRE(calls.size() == static_cast<std::size_t>(kSmearN));
        for (int i = 0; i < kSmearN; ++i) {
            const auto k = static_cast<std::size_t>(i);
            const auto& row = smearRows()[k];
            INFO("id " << row.id);
            CHECK(calls[k].first == row.id);
            const double expected =
                linearToNormalized(static_cast<double>(kNonDefault[k]), row.mn, row.mx);
            CHECK(calls[k].second == Approx(expected).margin(1e-9));
        }
    }

    SECTION("FormatNonEmpty") {
        for (const auto& row : smearRows()) {
            for (double n : {0.0, 0.5, 1.0}) {
                INFO("id " << row.id << " n " << n);
                String128 text{};
                REQUIRE(formatSmearParam(row.id, n, text) == Steinberg::kResultOk);
                CHECK_FALSE(toAscii(text).empty());
            }
        }
        String128 text{};
        CHECK(formatSmearParam(kSmearTiltId + 1, 0.5, text) == Steinberg::kResultFalse);
    }
}
