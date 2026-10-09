// ==============================================================================
// Vorago Phase 12 - cloud parameter pack contract (T016)
// ==============================================================================
// The shared Group 7 contract (specs/vorago-phase12-parameters/tasks.md) for the
// Cloud pack: 7 continuous linear IDs 200-206, stream 7 floats = 28 bytes.
//
// The expected table below is written independently of cloud_params.h (plan
// section 3.2 Cloud rows) so a wrong range/default in the header is caught.
//
// ODR: Vorago::CloudParams is a near-name of Seraphis::CloudParams
// (plugins/seraphis/src/parameters/cloud_params.h:82). This TU only ever names
// ::Vorago - it never `using namespace`s either.
//
// Built with -fno-fast-math (tests/CMakeLists.txt): NaN/Inf payloads are produced
// from bit patterns through a volatile uint32 + memcpy.
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "parameters/cloud_params.h"
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
#include <map>
#include <string>
#include <vector>

namespace {

using Catch::Approx;
namespace V = ::Vorago;

constexpr int kN = 7;
constexpr int kBytes = 28;

struct Expected {
    Steinberg::Vst::ParamID id;
    const char* title;
    const char* units;
    double minPlain;
    double maxPlain;
    double defaultPlain;
    double n0;
    std::atomic<float> V::CloudParams::*field;
};

const std::array<Expected, kN> kExpected = {{
    {.id=200, .title="Cloud Richness", .units="%", .minPlain=0.0, .maxPlain=1.0, .defaultPlain=0.70, .n0=0.70, .field=&V::CloudParams::richness},
    {.id=201, .title="Cloud Tilt", .units="dB/oct", .minPlain=-12.0, .maxPlain=12.0, .defaultPlain=-4.0, .n0=0.333333333, .field=&V::CloudParams::tiltDb},
    {.id=202, .title="Cloud Mutation", .units="%", .minPlain=0.0, .maxPlain=1.0, .defaultPlain=0.15, .n0=0.15, .field=&V::CloudParams::mutation},
    {.id=203, .title="Cloud Inharmonicity", .units="", .minPlain=0.0, .maxPlain=0.1, .defaultPlain=0.015, .n0=0.15, .field=&V::CloudParams::inharmonicity},
    {.id=204, .title="Cloud Drift Depth", .units="ct", .minPlain=0.0, .maxPlain=50.0, .defaultPlain=8.0, .n0=0.16, .field=&V::CloudParams::driftCents},
    {.id=205, .title="Cloud Stereo Spread", .units="%", .minPlain=0.0, .maxPlain=1.0, .defaultPlain=0.45, .n0=0.45, .field=&V::CloudParams::stereoSpread},
    {.id=206, .title="Cloud Spectral Gravity", .units="", .minPlain=-1.0, .maxPlain=1.0, .defaultPlain=0.10, .n0=0.55,
     .field=&V::CloudParams::spectralGravity},
}};

// Non-default in-range values (S) and a second, distinct in-range set (D) used to
// pre-dirty load destinations.
constexpr std::array<float, kN> kSetS = {0.33f, 7.5f, 0.61f, 0.072f, 31.25f, 0.9f, -0.4f};
constexpr std::array<float, kN> kSetD = {0.05f, -9.0f, 0.95f, 0.004f, 2.5f, 0.1f, 0.8f};

std::string toAsciiString(const Steinberg::Vst::String128 s) {
    char buf[128] = {};
    Steinberg::UString(const_cast<Steinberg::Vst::TChar*>(s), 128)  // NOLINT(cppcoreguidelines-pro-type-const-cast)
        .toAscii(buf, 128);
    return {buf};
}

std::atomic<float>& fieldOf(V::CloudParams& p, int i) {
    return p.*(kExpected[static_cast<std::size_t>(i)].field);
}

std::array<std::uint32_t, kN> snapshotBits(V::CloudParams& p) {
    std::array<std::uint32_t, kN> bits{};
    for (int i = 0; i < kN; ++i) {
        bits[static_cast<std::size_t>(i)] = std::bit_cast<std::uint32_t>(fieldOf(p, i).load());
    }
    return bits;
}

void fill(V::CloudParams& p, const std::array<float, kN>& values) {
    for (int i = 0; i < kN; ++i) {
        fieldOf(p, i).store(values[static_cast<std::size_t>(i)]);
    }
}

Steinberg::IPtr<Steinberg::MemoryStream> makeStream() {
    return Steinberg::owned(new Steinberg::MemoryStream());
}

void rewindStream(Steinberg::MemoryStream& s) {
    REQUIRE(s.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) == Steinberg::kResultOk);
}

// A stream holding exactly `count` bytes copied from `src`, rewound for reading.
Steinberg::IPtr<Steinberg::MemoryStream> prefixStream(const Steinberg::MemoryStream& src,
                                                      int count) {
    auto out = makeStream();
    if (count > 0) {
        Steinberg::int32 written = 0;
        REQUIRE(out->write(src.getData(), count, &written) == Steinberg::kResultOk);
        REQUIRE(written == count);
    }
    rewindStream(*out);
    return out;
}

// Float from a raw bit pattern, immune to -ffast-math constant folding.
float floatFromBits(std::uint32_t bits) {
    volatile std::uint32_t vb = bits;
    const std::uint32_t b = vb;
    float f = 0.0f;
    std::memcpy(&f, &b, sizeof(f));
    return f;
}

Steinberg::IPtr<Steinberg::MemoryStream> streamOf(const std::array<float, kN>& values) {
    auto s = makeStream();
    {
        Steinberg::IBStreamer w(s, kLittleEndian);
        for (float v : values) {
            REQUIRE(w.writeFloat(v));
        }
    }
    rewindStream(*s);
    return s;
}

}  // namespace

TEST_CASE("Vorago_CloudParamsContract", "[vorago][params]") {
    SECTION("Registration") {
        Steinberg::Vst::ParameterContainer pc;
        V::registerCloudParams(pc);
        REQUIRE(pc.getParameterCount() == kN);

        for (const auto& e : kExpected) {
            INFO("id " << e.id);
            auto* p = pc.getParameter(e.id);
            REQUIRE(p != nullptr);
            const auto& info = p->getInfo();
            REQUIRE(toAsciiString(info.title) == e.title);
            REQUIRE(toAsciiString(info.units) == e.units);
            REQUIRE(info.stepCount == 0);
            REQUIRE((info.flags & Steinberg::Vst::ParameterInfo::kCanAutomate) != 0);
            REQUIRE((info.flags & Steinberg::Vst::ParameterInfo::kIsList) == 0);
            REQUIRE(info.defaultNormalizedValue == Approx(e.n0).margin(1e-9));
        }
    }

    SECTION("DefaultsDenormalizeToPlain") {
        for (int i = 0; i < kN; ++i) {
            const auto& e = kExpected[static_cast<std::size_t>(i)];
            INFO("id " << e.id);

            V::CloudParams fresh;
            REQUIRE(static_cast<double>(fieldOf(fresh, i).load()) ==
                    Approx(e.defaultPlain).epsilon(1e-6));

            V::CloudParams p;
            fill(p, kSetD);  // make sure the handler really writes
            V::handleCloudParamChange(p, e.id, e.n0);
            REQUIRE(static_cast<double>(fieldOf(p, i).load()) ==
                    Approx(e.defaultPlain).epsilon(1e-6));
            REQUIRE(fieldOf(p, i).load() == fieldOf(fresh, i).load());

            V::handleCloudParamChange(p, e.id, 0.0);
            REQUIRE(static_cast<double>(fieldOf(p, i).load()) ==
                    Approx(e.minPlain).margin(1e-9));
            V::handleCloudParamChange(p, e.id, 1.0);
            REQUIRE(static_cast<double>(fieldOf(p, i).load()) ==
                    Approx(e.maxPlain).epsilon(1e-6));

            // Only field i moved.
            for (int k = 0; k < kN; ++k) {
                if (k == i) { continue; }
                REQUIRE(fieldOf(p, k).load() == kSetD[static_cast<std::size_t>(k)]);
            }
        }
    }

    SECTION("UnregisteredInBandIgnored") {
        V::CloudParams p;
        fill(p, kSetS);
        const auto before = snapshotBits(p);
        for (Steinberg::Vst::ParamID id = 207; id <= 299; ++id) {
            V::handleCloudParamChange(p, id, 0.0);
            V::handleCloudParamChange(p, id, 0.9);
            V::handleCloudParamChange(p, id, 1.0);
        }
        REQUIRE(snapshotBits(p) == before);
    }

    SECTION("SaveLoadRoundTrip") {
        V::CloudParams out;
        fill(out, kSetS);
        auto stream = makeStream();
        {
            Steinberg::IBStreamer w(stream, kLittleEndian);
            V::saveCloudParams(out, w);
        }
        REQUIRE(stream->getSize() == kBytes);

        rewindStream(*stream);
        V::CloudParams in;
        {
            Steinberg::IBStreamer r(stream, kLittleEndian);
            REQUIRE(V::loadCloudParams(in, r));
        }
        REQUIRE(snapshotBits(in) == snapshotBits(out));
    }

    SECTION("TruncationEveryOffset") {
        V::CloudParams out;
        fill(out, kSetS);
        auto full = makeStream();
        {
            Steinberg::IBStreamer w(full, kLittleEndian);
            V::saveCloudParams(out, w);
        }
        REQUIRE(full->getSize() == kBytes);

        V::CloudParams dirtyRef;
        fill(dirtyRef, kSetD);
        const auto sBits = snapshotBits(out);
        const auto dBits = snapshotBits(dirtyRef);

        for (int c = 0; c < kBytes; ++c) {
            INFO("cut " << c);
            auto cut = prefixStream(*full, c);
            V::CloudParams in;
            fill(in, kSetD);
            {
                Steinberg::IBStreamer r(cut, kLittleEndian);
                REQUIRE_FALSE(V::loadCloudParams(in, r));
            }
            const auto got = snapshotBits(in);
            for (int k = 0; k < kN; ++k) {
                const auto ks = static_cast<std::size_t>(k);
                const bool contained = (k + 1) * 4 <= c;
                REQUIRE(got[ks] == (contained ? sBits[ks] : dBits[ks]));
            }
        }
    }

    SECTION("NonFiniteAndClamp") {
        const std::array<std::uint32_t, 3> nonFinite = {0x7FC00000u, 0x7F800000u, 0xFF800000u};
        V::CloudParams sRef;
        fill(sRef, kSetS);
        V::CloudParams dRef;
        fill(dRef, kSetD);
        const auto sBits = snapshotBits(sRef);
        const auto dBits = snapshotBits(dRef);

        for (const std::uint32_t bits : nonFinite) {
            for (int k = 0; k < kN; ++k) {
                INFO("field " << k << " bits " << bits);
                auto values = kSetS;
                values[static_cast<std::size_t>(k)] = floatFromBits(bits);
                auto s = streamOf(values);

                V::CloudParams in;
                fill(in, kSetD);
                {
                    Steinberg::IBStreamer r(s, kLittleEndian);
                    REQUIRE(V::loadCloudParams(in, r));
                }
                const auto got = snapshotBits(in);
                for (int j = 0; j < kN; ++j) {
                    const auto js = static_cast<std::size_t>(j);
                    REQUIRE(got[js] == (j == k ? dBits[js] : sBits[js]));
                }
            }
        }

        // Finite out-of-range floats clamp to the plain range.
        std::array<float, kN> high{};
        std::array<float, kN> low{};
        for (int i = 0; i < kN; ++i) {
            const auto& e = kExpected[static_cast<std::size_t>(i)];
            high[static_cast<std::size_t>(i)] = static_cast<float>(e.maxPlain + 10.0);
            low[static_cast<std::size_t>(i)] = static_cast<float>(e.minPlain - 10.0);
        }
        {
            auto s = streamOf(high);
            V::CloudParams in;
            Steinberg::IBStreamer r(s, kLittleEndian);
            REQUIRE(V::loadCloudParams(in, r));
            for (int i = 0; i < kN; ++i) {
                const auto& e = kExpected[static_cast<std::size_t>(i)];
                REQUIRE(fieldOf(in, i).load() == static_cast<float>(e.maxPlain));
            }
        }
        {
            auto s = streamOf(low);
            V::CloudParams in;
            Steinberg::IBStreamer r(s, kLittleEndian);
            REQUIRE(V::loadCloudParams(in, r));
            for (int i = 0; i < kN; ++i) {
                const auto& e = kExpected[static_cast<std::size_t>(i)];
                REQUIRE(fieldOf(in, i).load() == static_cast<float>(e.minPlain));
            }
        }
        // No discrete (index) fields in the Cloud pack: the index -1 / n clamp is n/a.
    }

    SECTION("ControllerMirror") {
        V::CloudParams out;
        fill(out, kSetS);
        auto stream = makeStream();
        {
            Steinberg::IBStreamer w(stream, kLittleEndian);
            V::saveCloudParams(out, w);
        }
        rewindStream(*stream);

        std::map<Steinberg::Vst::ParamID, double> got;
        std::vector<Steinberg::Vst::ParamID> order;
        {
            Steinberg::IBStreamer r(stream, kLittleEndian);
            V::loadCloudParamsToController(
                r, [&got, &order](Steinberg::Vst::ParamID id, double normalized) {
                    got[id] = normalized;
                    order.push_back(id);
                });
        }
        REQUIRE(got.size() == static_cast<std::size_t>(kN));
        REQUIRE(order.size() == static_cast<std::size_t>(kN));
        for (int i = 0; i < kN; ++i) {
            const auto& e = kExpected[static_cast<std::size_t>(i)];
            INFO("id " << e.id);
            REQUIRE(order[static_cast<std::size_t>(i)] == e.id);  // ascending-ID read order
            const double plain = static_cast<double>(kSetS[static_cast<std::size_t>(i)]);
            const double expected = (plain - e.minPlain) / (e.maxPlain - e.minPlain);
            REQUIRE(got.at(e.id) == Approx(expected).margin(1e-9));
        }
    }

    SECTION("FormatNonEmpty") {
        for (const auto& e : kExpected) {
            for (const double n : {0.0, 0.5, 1.0}) {
                INFO("id " << e.id << " n " << n);
                Steinberg::Vst::String128 text = {};
                REQUIRE(V::formatCloudParam(e.id, n, text) == Steinberg::kResultOk);
                REQUIRE_FALSE(toAsciiString(text).empty());
            }
        }
        Steinberg::Vst::String128 text = {};
        REQUIRE(V::formatCloudParam(207, 0.5, text) == Steinberg::kResultFalse);
        REQUIRE(V::formatCloudParam(299, 0.5, text) == Steinberg::kResultFalse);
    }
}
