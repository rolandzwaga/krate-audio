// ==============================================================================
// Vorago Phase 12 - events parameter pack contract (T022)
// ==============================================================================
// Shared Group 7 contract (specs/vorago-phase12-parameters/tasks.md, T016-T029),
// N = 1, B = 4:
//   800 Event Rate, x, [0.1, 10], default 1.0, n0 0.5 (exact log midpoint), log.
//
// Built with -fno-fast-math (tests/CMakeLists.txt): NaN/Inf payloads are
// produced from bit patterns through a volatile uint32 + memcpy.
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "parameters/events_params.h"
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

using Steinberg::Vst::ParamID;

constexpr std::size_t kEventsStreamBytes = 4;  // B

Steinberg::IPtr<Steinberg::MemoryStream> makeStream() {
    return Steinberg::owned(new Steinberg::MemoryStream());
}

void rewindStream(Steinberg::MemoryStream& s) {
    REQUIRE(s.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) == Steinberg::kResultOk);
}

std::uint32_t bitsOf(float f) { return std::bit_cast<std::uint32_t>(f); }

float floatFromBits(std::uint32_t bits) {
    volatile std::uint32_t v = bits;  // defeat constant folding of the payload
    const std::uint32_t b = v;
    float f = 0.0f;
    std::memcpy(&f, &b, sizeof(f));
    return f;
}

std::string toAscii(const Steinberg::Vst::String128 s) {
    char buf[128] = {};
    Steinberg::UString(const_cast<Steinberg::Vst::TChar*>(s), 128)  // NOLINT(cppcoreguidelines-pro-type-const-cast)
        .toAscii(buf, 128);
    return {buf};
}

/// A stream holding exactly `bytes` (read position 0).
Steinberg::IPtr<Steinberg::MemoryStream> streamFromBytes(const std::vector<char>& bytes) {
    auto s = makeStream();
    if (!bytes.empty()) {
        Steinberg::int32 written = 0;
        REQUIRE(s->write(const_cast<char*>(bytes.data()),  // NOLINT(cppcoreguidelines-pro-type-const-cast)
                         static_cast<Steinberg::int32>(bytes.size()), &written) ==
                Steinberg::kResultOk);
        REQUIRE(std::cmp_equal(written, bytes.size()));
    }
    rewindStream(*s);
    return s;
}

std::vector<char> bytesOf(const Steinberg::MemoryStream& s) {
    const auto* p = s.getData();
    return {p, p + s.getSize()};
}

/// One float field written little-endian through the SDK streamer.
std::vector<char> streamWithFloat(float f) {
    auto s = makeStream();
    Steinberg::IBStreamer w(s, kLittleEndian);
    REQUIRE(w.writeFloat(f));
    return bytesOf(*s);
}

double expectedNormalized(float plain) {
    return Krate::Plugins::logMapToNormalized(static_cast<double>(plain), 0.1, 10.0);
}

}  // namespace

TEST_CASE("Vorago_EventsParamsContract", "[vorago][params]") {
    using namespace Steinberg;
    using namespace Steinberg::Vst;

    SECTION("Registration") {
        ParameterContainer pc;
        Vorago::registerEventsParams(pc);
        REQUIRE(pc.getParameterCount() == 1);

        auto* p = pc.getParameter(Vorago::kEventsRateScaleId);
        REQUIRE(p != nullptr);
        const auto& info = p->getInfo();
        CHECK(toAscii(info.title) == "Event Rate");
        CHECK(toAscii(info.units) == "x");
        CHECK(info.stepCount == 0);
        CHECK((info.flags & ParameterInfo::kCanAutomate) != 0);
        CHECK((info.flags & ParameterInfo::kIsList) == 0);
        CHECK(info.defaultNormalizedValue == Catch::Approx(0.5).margin(1e-9));
    }

    SECTION("DefaultsDenormalizeToPlain") {
        Vorago::EventsParams fresh;
        CHECK(fresh.eventRateScale.load() == 1.0f);

        Vorago::EventsParams p;
        p.eventRateScale.store(7.0f);
        Vorago::handleEventsParamChange(p, Vorago::kEventsRateScaleId, 0.5);
        CHECK(p.eventRateScale.load() == Catch::Approx(1.0).epsilon(1e-6));
        CHECK(p.eventRateScale.load() == Catch::Approx(fresh.eventRateScale.load()).epsilon(1e-6));

        Vorago::handleEventsParamChange(p, Vorago::kEventsRateScaleId, 0.0);
        CHECK(p.eventRateScale.load() == Catch::Approx(0.1).epsilon(1e-6));
        Vorago::handleEventsParamChange(p, Vorago::kEventsRateScaleId, 1.0);
        CHECK(p.eventRateScale.load() == Catch::Approx(10.0).epsilon(1e-6));

        // Log taper: quarter points are the geometric quarter points.
        Vorago::handleEventsParamChange(p, Vorago::kEventsRateScaleId, 0.25);
        CHECK(p.eventRateScale.load() == Catch::Approx(0.316227766).epsilon(1e-6));
    }

    SECTION("UnregisteredInBandIgnored") {
        Vorago::EventsParams p;
        p.eventRateScale.store(3.25f);
        const auto before = bitsOf(p.eventRateScale.load());
        const std::array<ParamID, 5> unused = {801u, 802u, 850u, 898u,
                                               Vorago::kEventsParamRangeEnd - 1u};
        for (const ParamID id : unused) {
            for (const double n : {0.0, 0.5, 1.0}) {
                Vorago::handleEventsParamChange(p, id, n);
                CHECK(bitsOf(p.eventRateScale.load()) == before);
            }
        }
    }

    SECTION("SaveLoadRoundTrip") {
        Vorago::EventsParams src;
        src.eventRateScale.store(3.5f);

        auto s = makeStream();
        {
            IBStreamer w(s, kLittleEndian);
            Vorago::saveEventsParams(src, w);
        }
        REQUIRE(std::cmp_equal(s->getSize(), kEventsStreamBytes));

        rewindStream(*s);
        Vorago::EventsParams dst;
        IBStreamer r(s, kLittleEndian);
        REQUIRE(Vorago::loadEventsParams(dst, r));
        CHECK(bitsOf(dst.eventRateScale.load()) == bitsOf(src.eventRateScale.load()));
    }

    SECTION("TruncationEveryOffset") {
        Vorago::EventsParams src;
        src.eventRateScale.store(3.5f);
        auto full = makeStream();
        {
            IBStreamer w(full, kLittleEndian);
            Vorago::saveEventsParams(src, w);
        }
        const auto bytes = bytesOf(*full);
        REQUIRE(bytes.size() == kEventsStreamBytes);

        for (std::size_t c = 0; c < kEventsStreamBytes; ++c) {
            INFO("cut at byte " << c);
            auto s = streamFromBytes(
                std::vector<char>(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(c)));
            Vorago::EventsParams dirty;
            dirty.eventRateScale.store(7.25f);
            const auto before = bitsOf(dirty.eventRateScale.load());
            IBStreamer r(s, kLittleEndian);
            CHECK_FALSE(Vorago::loadEventsParams(dirty, r));
            // The single field is never fully contained in [0, c) for c < 4.
            CHECK(bitsOf(dirty.eventRateScale.load()) == before);
        }
    }

    SECTION("NonFiniteAndClamp") {
        const std::array<std::uint32_t, 3> payloads = {0x7FC00000u,   // quiet NaN
                                                       0x7F800000u,   // +Inf
                                                       0xFF800000u};  // -Inf
        for (const auto bits : payloads) {
            INFO("payload bits " << bits);
            auto s = streamFromBytes(streamWithFloat(floatFromBits(bits)));
            Vorago::EventsParams p;
            p.eventRateScale.store(2.5f);
            IBStreamer r(s, kLittleEndian);
            CHECK(Vorago::loadEventsParams(p, r));  // the read succeeded
            CHECK(bitsOf(p.eventRateScale.load()) == bitsOf(2.5f));

            // The controller mirror skips the same field.
            auto s2 = streamFromBytes(streamWithFloat(floatFromBits(bits)));
            IBStreamer r2(s2, kLittleEndian);
            int calls = 0;
            Vorago::loadEventsParamsToController(r2, [&](ParamID, double) { ++calls; });
            CHECK(calls == 0);
        }

        const std::array<std::pair<float, float>, 4> clamps = {
            std::pair{100.0f, 10.0f}, std::pair{10.5f, 10.0f}, std::pair{0.01f, 0.1f},
            std::pair{-5.0f, 0.1f}};
        for (const auto& [raw, expected] : clamps) {
            INFO("raw " << raw);
            auto s = streamFromBytes(streamWithFloat(raw));
            Vorago::EventsParams p;
            IBStreamer r(s, kLittleEndian);
            CHECK(Vorago::loadEventsParams(p, r));
            CHECK(p.eventRateScale.load() == expected);
        }
    }

    SECTION("ControllerMirror") {
        Vorago::EventsParams src;
        src.eventRateScale.store(3.5f);
        auto s = makeStream();
        {
            IBStreamer w(s, kLittleEndian);
            Vorago::saveEventsParams(src, w);
        }
        rewindStream(*s);

        std::vector<std::pair<ParamID, double>> seen;
        IBStreamer r(s, kLittleEndian);
        Vorago::loadEventsParamsToController(
            r, [&](ParamID id, double n) { seen.emplace_back(id, n); });
        REQUIRE(seen.size() == 1);
        CHECK(seen[0].first == Vorago::kEventsRateScaleId);
        CHECK(seen[0].second == Catch::Approx(expectedNormalized(3.5f)).margin(1e-9));

        // Round trip back through the processor handler lands on the saved value.
        Vorago::EventsParams back;
        Vorago::handleEventsParamChange(back, seen[0].first, seen[0].second);
        CHECK(back.eventRateScale.load() == Catch::Approx(3.5).epsilon(1e-6));

        // Empty stream: no call, no crash.
        auto empty = makeStream();
        IBStreamer re(empty, kLittleEndian);
        int calls = 0;
        Vorago::loadEventsParamsToController(re, [&](ParamID, double) { ++calls; });
        CHECK(calls == 0);
    }

    SECTION("FormatNonEmpty") {
        for (const double n : {0.0, 0.5, 1.0}) {
            INFO("n " << n);
            String128 text = {};
            CHECK(Vorago::formatEventsParam(Vorago::kEventsRateScaleId, n, text) == kResultOk);
            CHECK_FALSE(toAscii(text).empty());
        }
        String128 text = {};
        CHECK(Vorago::formatEventsParam(801u, 0.5, text) == kResultFalse);
    }
}
