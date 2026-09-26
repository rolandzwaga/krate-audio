// ==============================================================================
// Vorago Phase 12 - sub parameter pack contract (T020)
// ==============================================================================
// The eight-SECTION pack contract of specs/vorago-phase12-parameters/tasks.md
// (Group 7 shared test shape) for plugins/vorago/src/parameters/sub_params.h:
// N = 5 parameters (IDs 600, 601, 610-612), B = 20 state bytes (5 floats).
// The pack has no list parameters, so SECTION 6's index clamps do not apply.
//
// Built with -fno-fast-math (tests/CMakeLists.txt): the NaN/Inf payloads below are
// produced from bit patterns through a volatile uint32 + memcpy.
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "parameters/sub_params.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/smartpointer.h"
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

using Steinberg::Vst::ParamID;
using Catch::Approx;

constexpr int kN = 5;
constexpr Steinberg::int64 kB = 20;

struct Row {
    ParamID id;
    const char* title;
    const char* units;
    double mn;
    double mx;
    float def;
    double n0;
};

// The T020 table, typed in from tasks.md (not derived from the header under test).
const std::array<Row, kN> kRows{{
    {.id=::Vorago::kSubLevelOffsetId, .title="Sub Level Offset", .units="dB", .mn=-24.0, .mx=24.0, .def=0.0f, .n0=0.5},
    {.id=::Vorago::kSubTrackingId, .title="Sub Tracking", .units="%", .mn=0.0, .mx=1.0, .def=1.0f, .n0=1.0},
    {.id=::Vorago::kSubDiv2LevelId, .title="Sub Div2 Level", .units="dB", .mn=-60.0, .mx=6.0, .def=-18.0f, .n0=0.636363636},
    {.id=::Vorago::kSubDiv4LevelId, .title="Sub Div4 Level", .units="dB", .mn=-60.0, .mx=6.0, .def=-24.0f, .n0=0.545454545},
    {.id=::Vorago::kSubFifthBelowLevelId, .title="Sub Fifth-Below Level", .units="dB", .mn=-60.0, .mx=6.0, .def=-30.0f,
     .n0=0.454545455},
}};

// Field i in ascending-ID (== stream) order.
std::atomic<float>& field(::Vorago::SubParams& p, int i) {
    switch (i) {
        case 0: return p.levelOffsetDb;
        case 1: return p.tracking;
        case 2: return p.div2LevelDb;
        case 3: return p.div4LevelDb;
        default: return p.fifthBelowLevelDb;
    }
}

std::array<std::uint32_t, kN> snapshotBits(::Vorago::SubParams& p) {
    std::array<std::uint32_t, kN> bits{};
    for (int i = 0; i < kN; ++i) {
        bits[static_cast<std::size_t>(i)] = std::bit_cast<std::uint32_t>(field(p, i).load());
    }
    return bits;
}

// Non-default, in-range values (SECTION 4) and a distinct "dirty" set (SECTION 5/6).
constexpr std::array<float, kN> kSaved{7.5f, 0.25f, -12.0f, -40.0f, -3.0f};
constexpr std::array<float, kN> kDirty{-11.0f, 0.6f, -50.0f, 2.0f, -45.0f};

void setAll(::Vorago::SubParams& p, const std::array<float, kN>& v) {
    for (int i = 0; i < kN; ++i) { field(p, i).store(v[static_cast<std::size_t>(i)]); }
}

std::string toAscii(const Steinberg::Vst::String128 s) {
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

Steinberg::IPtr<Steinberg::MemoryStream> streamOfFloats(const std::array<float, kN>& v) {
    auto s = makeStream();
    Steinberg::IBStreamer w(s, kLittleEndian);
    for (float f : v) { REQUIRE(w.writeFloat(f)); }
    rewindStream(*s);
    return s;
}

float floatFromBits(std::uint32_t bits) {
    volatile std::uint32_t vb = bits;
    const std::uint32_t b = vb;
    float f = 0.0f;
    std::memcpy(&f, &b, sizeof(f));
    return f;
}

}  // namespace

TEST_CASE("Vorago_SubParamsContract", "[vorago][params]") {
    using namespace Steinberg;
    using namespace Steinberg::Vst;

    SECTION("Registration") {
        ParameterContainer pc;
        ::Vorago::registerSubParams(pc);
        REQUIRE(pc.getParameterCount() == kN);
        for (const auto& r : kRows) {
            INFO("id " << r.id);
            auto* p = pc.getParameter(r.id);
            REQUIRE(p != nullptr);
            const ParameterInfo& info = p->getInfo();
            CHECK(toAscii(info.title) == r.title);
            CHECK(toAscii(info.units) == r.units);
            CHECK(info.stepCount == 0);
            CHECK(info.flags == ParameterInfo::kCanAutomate);
            CHECK(info.defaultNormalizedValue == Approx(r.n0).margin(1e-9));
        }
    }

    SECTION("DefaultsDenormalizeToPlain") {
        ::Vorago::SubParams fresh;
        for (int i = 0; i < kN; ++i) {
            const auto& r = kRows[static_cast<std::size_t>(i)];
            INFO("id " << r.id);
            CHECK(field(fresh, i).load() == r.def);  // explicit initializers == default column

            ::Vorago::SubParams p;
            ::Vorago::handleSubParamChange(p, r.id, r.n0);
            CHECK(field(p, i).load() == Approx(r.def).epsilon(1e-6).margin(1e-6));

            ::Vorago::handleSubParamChange(p, r.id, 0.0);
            CHECK(field(p, i).load() == Approx(r.mn).margin(1e-6));
            ::Vorago::handleSubParamChange(p, r.id, 1.0);
            CHECK(field(p, i).load() == Approx(r.mx).margin(1e-6));
        }
    }

    SECTION("UnregisteredInBandIgnored") {
        ::Vorago::SubParams p;
        setAll(p, kSaved);
        const auto before = snapshotBits(p);
        for (ParamID id = 600; id < ::Vorago::kSubParamRangeEnd; ++id) {
            bool registered = false;
            for (const auto& r : kRows) { registered = registered || (r.id == id); }
            if (registered) { continue; }
            for (double v : {0.0, 0.37, 1.0}) { ::Vorago::handleSubParamChange(p, id, v); }
        }
        CHECK(snapshotBits(p) == before);
    }

    // SECTION 4's stream is reused by SECTION 7.
    auto buildSavedStream = []() {
        ::Vorago::SubParams src;
        setAll(src, kSaved);
        auto s = makeStream();
        IBStreamer w(s, kLittleEndian);
        ::Vorago::saveSubParams(src, w);
        return s;
    };

    SECTION("SaveLoadRoundTrip") {
        auto s = buildSavedStream();
        REQUIRE(s->getSize() == kB);
        rewindStream(*s);
        ::Vorago::SubParams dst;
        IBStreamer r(s, kLittleEndian);
        REQUIRE(::Vorago::loadSubParams(dst, r));
        std::array<std::uint32_t, kN> expected{};
        for (int i = 0; i < kN; ++i) {
            expected[static_cast<std::size_t>(i)] =
                std::bit_cast<std::uint32_t>(kSaved[static_cast<std::size_t>(i)]);
        }
        CHECK(snapshotBits(dst) == expected);
    }

    SECTION("TruncationEveryOffset") {
        auto full = buildSavedStream();
        REQUIRE(full->getSize() == kB);
        for (Steinberg::int64 c = 0; c < kB; ++c) {
            INFO("cut " << c);
            auto cut = makeStream();
            if (c > 0) {
                int32 written = 0;
                REQUIRE(cut->write(full->getData(), static_cast<int32>(c), &written) == kResultOk);
                REQUIRE(written == static_cast<int32>(c));
            }
            rewindStream(*cut);
            ::Vorago::SubParams dst;
            setAll(dst, kDirty);
            IBStreamer r(cut, kLittleEndian);
            CHECK_FALSE(::Vorago::loadSubParams(dst, r));
            for (int i = 0; i < kN; ++i) {
                const bool contained = (4 * static_cast<Steinberg::int64>(i)) + 4 <= c;
                const float want = contained ? kSaved[static_cast<std::size_t>(i)]
                                             : kDirty[static_cast<std::size_t>(i)];
                CHECK(std::bit_cast<std::uint32_t>(field(dst, i).load()) ==
                      std::bit_cast<std::uint32_t>(want));
            }
        }
    }

    SECTION("NonFiniteAndClamp") {
        const std::array<std::uint32_t, 3> nonFinite{0x7FC00000u, 0x7F800000u, 0xFF800000u};
        for (int k = 0; k < kN; ++k) {
            for (std::uint32_t bits : nonFinite) {
                INFO("field " << k << " bits " << bits);
                auto v = kSaved;
                v[static_cast<std::size_t>(k)] = floatFromBits(bits);
                auto s = streamOfFloats(v);
                ::Vorago::SubParams dst;
                setAll(dst, kDirty);
                IBStreamer r(s, kLittleEndian);
                CHECK(::Vorago::loadSubParams(dst, r));
                for (int i = 0; i < kN; ++i) {
                    const float want = (i == k) ? kDirty[static_cast<std::size_t>(i)]
                                                : kSaved[static_cast<std::size_t>(i)];
                    CHECK(std::bit_cast<std::uint32_t>(field(dst, i).load()) ==
                          std::bit_cast<std::uint32_t>(want));
                }
            }
        }

        // Finite out-of-range floats clamp to the plain range ends.
        for (float out : {1000.0f, -1000.0f}) {
            INFO("out " << out);
            std::array<float, kN> v{};
            v.fill(out);
            auto s = streamOfFloats(v);
            ::Vorago::SubParams dst;
            IBStreamer r(s, kLittleEndian);
            CHECK(::Vorago::loadSubParams(dst, r));
            for (int i = 0; i < kN; ++i) {
                const auto& row = kRows[static_cast<std::size_t>(i)];
                CHECK(field(dst, i).load() ==
                      static_cast<float>(out > 0.0f ? row.mx : row.mn));
            }
        }
    }

    SECTION("ControllerMirror") {
        auto s = buildSavedStream();
        rewindStream(*s);
        std::vector<std::pair<ParamID, double>> calls;
        IBStreamer r(s, kLittleEndian);
        ::Vorago::loadSubParamsToController(
            r, [&calls](ParamID id, double n) { calls.emplace_back(id, n); });
        REQUIRE(calls.size() == static_cast<std::size_t>(kN));
        for (int i = 0; i < kN; ++i) {
            const auto& row = kRows[static_cast<std::size_t>(i)];
            const auto& call = calls[static_cast<std::size_t>(i)];
            INFO("id " << row.id);
            CHECK(call.first == row.id);
            const double plain = static_cast<double>(kSaved[static_cast<std::size_t>(i)]);
            const double inverse = (plain - row.mn) / (row.mx - row.mn);
            CHECK(call.second == Approx(inverse).margin(1e-9));
        }
    }

    SECTION("FormatNonEmpty") {
        for (const auto& row : kRows) {
            for (double v : {0.0, 0.5, 1.0}) {
                INFO("id " << row.id << " v " << v);
                String128 str{};
                CHECK(::Vorago::formatSubParam(row.id, v, str) == kResultOk);
                CHECK_FALSE(toAscii(str).empty());
            }
        }
        String128 str{};
        CHECK(::Vorago::formatSubParam(605, 0.5, str) == kResultFalse);
    }
}
