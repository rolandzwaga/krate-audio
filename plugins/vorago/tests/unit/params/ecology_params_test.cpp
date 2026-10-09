// ==============================================================================
// Vorago Phase 12 - ecology parameter pack contract (T019)
// ==============================================================================
// Shared pack contract, specs/vorago-phase12-parameters/tasks.md Group 7:
// 8 parameters (500 Mix, 501 Loop Gain, 510-515 Loop 1..6 Filter), 32-byte
// stream (2 float + 6 int32, ascending ID order).
//
// Built with -fno-fast-math (tests/CMakeLists.txt): NaN/Inf payloads are built
// from bit patterns through a volatile uint32 + memcpy.
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "parameters/ecology_params.h"
#include "parameters/param_mapping.h"
#include "plugin_ids.h"

#include <krate/dsp/systems/feedback_ecology.h>

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace {

namespace Vst = Steinberg::Vst;

constexpr std::size_t kN = 8;
constexpr Steinberg::int64 kB = 32;

constexpr std::array<Vst::ParamID, 6> kFilterIds = {
    Vorago::kEcologyLoop0FilterModeId, Vorago::kEcologyLoop1FilterModeId,
    Vorago::kEcologyLoop2FilterModeId, Vorago::kEcologyLoop3FilterModeId,
    Vorago::kEcologyLoop4FilterModeId, Vorago::kEcologyLoop5FilterModeId};

Steinberg::IPtr<Steinberg::MemoryStream> makeStream() {
    return Steinberg::owned(new Steinberg::MemoryStream());
}

void rewindStream(Steinberg::MemoryStream& s) {
    REQUIRE(s.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) == Steinberg::kResultOk);
}

std::uint32_t bitsOf(float f) { return std::bit_cast<std::uint32_t>(f); }

float floatFromBits(std::uint32_t bits) {
    volatile std::uint32_t v = bits;
    const std::uint32_t copy = v;
    float f = 0.0f;
    std::memcpy(&f, &copy, sizeof(f));
    return f;
}

bool tcharEqualsAscii(const Vst::TChar* s, const char* ascii) {
    while (*s != 0 && *ascii != 0) {
        if (*s != static_cast<Vst::TChar>(*ascii)) {
            return false;
        }
        ++s;
        ++ascii;
    }
    return (*s == 0) && (*ascii == 0);
}

bool isEmpty(const Vst::String128 s) { return s[0] == 0; }

/// One snapshot of every atomic, bit-level (floats) / exact (indices).
struct Snapshot {
    std::uint32_t mixBits = 0;
    std::uint32_t loopGainBits = 0;
    std::array<int, 6> filter{};

    bool operator==(const Snapshot&) const = default;
};

Snapshot snapshot(const Vorago::EcologyParams& p) {
    Snapshot s;
    s.mixBits = bitsOf(p.mix.load());
    s.loopGainBits = bitsOf(p.loopGain.load());
    for (std::size_t l = 0; l < 6; ++l) {
        s.filter[l] = p.loopFilterMode[l].load();
    }
    return s;
}

/// Plain values used as "saved" (SECTION 4) and "dirty" (SECTION 5) states.
/// Every field differs from the default AND between the two sets.
struct PlainSet {
    float mix;
    float loopGain;
    std::array<int, 6> filter;
};

constexpr PlainSet kSaved{.mix = 0.37f, .loopGain = 0.41f, .filter = {1, 2, 1, 2, 1, 2}};
constexpr PlainSet kDirty{.mix = 0.83f, .loopGain = 0.13f, .filter = {2, 1, 2, 1, 2, 1}};

void applySet(Vorago::EcologyParams& p, const PlainSet& v) {
    p.mix.store(v.mix);
    p.loopGain.store(v.loopGain);
    for (std::size_t l = 0; l < 6; ++l) {
        p.loopFilterMode[l].store(v.filter[l]);
    }
}

Snapshot snapshotOf(const PlainSet& v) {
    Vorago::EcologyParams p;
    applySet(p, v);
    return snapshot(p);
}

/// Writes a raw stream in the pack's field order (float, float, 6 x int32).
void writeRaw(Steinberg::MemoryStream& stream, float mix, float loopGain,
              const std::array<Steinberg::int32, 6>& filter) {
    Steinberg::IBStreamer w(&stream, kLittleEndian);
    w.writeFloat(mix);
    w.writeFloat(loopGain);
    for (auto f : filter) {
        w.writeInt32(f);
    }
}

}  // namespace

TEST_CASE("Vorago_EcologyParamsContract", "[vorago][params]") {
    using Catch::Approx;
    using namespace ::Vorago;

    SECTION("Registration") {
        Vst::ParameterContainer pc;
        registerEcologyParams(pc);
        REQUIRE(pc.getParameterCount() == static_cast<Steinberg::int32>(kN));

        struct Row {
            Vst::ParamID id;
            const char* title;
            const char* units;
            Steinberg::int32 stepCount;
            Steinberg::int32 flags;
            double n0;
        };
        constexpr Steinberg::int32 kList =
            Vst::ParameterInfo::kCanAutomate | Vst::ParameterInfo::kIsList;
        const std::array<Row, kN> rows = {{
            {.id=kEcologyMixId, .title="Ecology Mix", .units="%", .stepCount=0, .flags=Vst::ParameterInfo::kCanAutomate, .n0=0.15},
            {.id=kEcologyLoopGainId, .title="Ecology Loop Gain", .units="%", .stepCount=0, .flags=Vst::ParameterInfo::kCanAutomate,
             .n0=0.8},
            {.id=kEcologyLoop0FilterModeId, .title="Ecology Loop 1 Filter", .units="", .stepCount=2, .flags=kList, .n0=0.0},
            {.id=kEcologyLoop1FilterModeId, .title="Ecology Loop 2 Filter", .units="", .stepCount=2, .flags=kList, .n0=0.0},
            {.id=kEcologyLoop2FilterModeId, .title="Ecology Loop 3 Filter", .units="", .stepCount=2, .flags=kList, .n0=0.0},
            {.id=kEcologyLoop3FilterModeId, .title="Ecology Loop 4 Filter", .units="", .stepCount=2, .flags=kList, .n0=0.0},
            {.id=kEcologyLoop4FilterModeId, .title="Ecology Loop 5 Filter", .units="", .stepCount=2, .flags=kList, .n0=0.0},
            {.id=kEcologyLoop5FilterModeId, .title="Ecology Loop 6 Filter", .units="", .stepCount=2, .flags=kList, .n0=0.0},
        }};

        for (const auto& r : rows) {
            INFO("id " << r.id);
            auto* p = pc.getParameter(r.id);
            REQUIRE(p != nullptr);
            const auto& info = p->getInfo();
            CHECK(tcharEqualsAscii(info.title, r.title));
            CHECK(tcharEqualsAscii(info.units, r.units));
            CHECK(info.stepCount == r.stepCount);
            CHECK(info.flags == r.flags);
            CHECK(info.defaultNormalizedValue == Approx(r.n0).margin(1e-9));
        }

        // List labels in FilterMode order (index == enum value).
        const std::array<const char*, 3> labels = {"Lowpass", "Bandpass", "Highpass"};
        for (auto id : kFilterIds) {
            auto* p = pc.getParameter(id);
            REQUIRE(p != nullptr);
            for (int i = 0; i < 3; ++i) {
                Vst::String128 s{};
                p->toString(indexToNormalized(i, 3), s);
                CHECK(tcharEqualsAscii(s, labels[static_cast<std::size_t>(i)]));
            }
        }
    }

    SECTION("DefaultsDenormalizeToPlain") {
        const EcologyParams fresh;
        EcologyParams p;
        handleEcologyParamChange(p, kEcologyMixId, 0.15);
        handleEcologyParamChange(p, kEcologyLoopGainId, 0.8);
        for (auto id : kFilterIds) {
            handleEcologyParamChange(p, id, 0.0);
        }
        CHECK(fresh.mix.load() == 0.15f);
        CHECK(fresh.loopGain.load() == 0.72f);
        CHECK(p.mix.load() == Approx(fresh.mix.load()).epsilon(1e-6));
        CHECK(p.loopGain.load() == Approx(fresh.loopGain.load()).epsilon(1e-6));
        CHECK(fresh.mix.load() == Krate::DSP::FeedbackEcology::kDefaultMix);
        CHECK(fresh.loopGain.load() == Krate::DSP::FeedbackEcology::kDefaultLoopGain);
        for (std::size_t l = 0; l < 6; ++l) {
            CHECK(fresh.loopFilterMode[l].load() == 0);
            CHECK(p.loopFilterMode[l].load() == fresh.loopFilterMode[l].load());
        }

        // Range ends.
        handleEcologyParamChange(p, kEcologyMixId, 0.0);
        handleEcologyParamChange(p, kEcologyLoopGainId, 0.0);
        CHECK(p.mix.load() == 0.0f);
        CHECK(p.loopGain.load() == 0.0f);
        handleEcologyParamChange(p, kEcologyMixId, 1.0);
        handleEcologyParamChange(p, kEcologyLoopGainId, 1.0);
        CHECK(p.mix.load() == 1.0f);
        CHECK(p.loopGain.load() == Approx(0.9f).epsilon(1e-6));
        CHECK(p.loopGain.load() <= Krate::DSP::FeedbackEcology::kMaxLoopGain);
        for (std::size_t l = 0; l < 6; ++l) {
            handleEcologyParamChange(p, kFilterIds[l], 0.0);
            CHECK(p.loopFilterMode[l].load() == 0);
            handleEcologyParamChange(p, kFilterIds[l], 1.0);
            CHECK(p.loopFilterMode[l].load() == 2);
            handleEcologyParamChange(p, kFilterIds[l], 0.5);
            CHECK(p.loopFilterMode[l].load() == 1);
        }
    }

    SECTION("UnregisteredInBandIgnored") {
        EcologyParams p;
        applySet(p, kSaved);
        const auto before = snapshot(p);
        for (Vst::ParamID id = 500; id < kEcologyParamRangeEnd; ++id) {
            const bool used = (id == kEcologyMixId) || (id == kEcologyLoopGainId) ||
                              (id >= kEcologyLoop0FilterModeId && id <= kEcologyLoop5FilterModeId);
            if (used) {
                continue;
            }
            for (double v : {0.0, 0.5, 1.0}) {
                handleEcologyParamChange(p, id, v);
            }
        }
        REQUIRE(snapshot(p) == before);
    }

    SECTION("SaveLoadRoundTrip") {
        EcologyParams out;
        applySet(out, kSaved);
        REQUIRE_FALSE(snapshot(out) == snapshot(EcologyParams{}));

        auto stream = makeStream();
        {
            Steinberg::IBStreamer w(stream, kLittleEndian);
            saveEcologyParams(out, w);
        }
        REQUIRE(stream->getSize() == kB);

        rewindStream(*stream);
        EcologyParams in;
        {
            Steinberg::IBStreamer r(stream, kLittleEndian);
            REQUIRE(loadEcologyParams(in, r));
        }
        REQUIRE(snapshot(in) == snapshot(out));
    }

    SECTION("TruncationEveryOffset") {
        EcologyParams out;
        applySet(out, kSaved);
        auto full = makeStream();
        {
            Steinberg::IBStreamer w(full, kLittleEndian);
            saveEcologyParams(out, w);
        }
        REQUIRE(full->getSize() == kB);
        char* bytes = full->getData();

        const Snapshot saved = snapshotOf(kSaved);
        const Snapshot dirty = snapshotOf(kDirty);

        for (Steinberg::int64 c = 0; c < kB; ++c) {
            INFO("cut " << c);
            auto part = makeStream();
            if (c > 0) {
                Steinberg::int32 written = 0;
                REQUIRE(part->write(bytes, static_cast<Steinberg::int32>(c), &written) ==
                        Steinberg::kResultOk);
                REQUIRE(written == static_cast<Steinberg::int32>(c));
            }
            rewindStream(*part);

            EcologyParams p;
            applySet(p, kDirty);
            {
                Steinberg::IBStreamer r(part, kLittleEndian);
                REQUIRE_FALSE(loadEcologyParams(p, r));
            }

            // Field k occupies [4k, 4k + 4): loaded iff 4k + 4 <= c.
            Snapshot expected = dirty;
            if (c >= 4) expected.mixBits = saved.mixBits;
            if (c >= 8) expected.loopGainBits = saved.loopGainBits;
            for (std::size_t l = 0; l < 6; ++l) {
                if (std::cmp_greater_equal(c, 12 + (4 * l))) {
                    expected.filter[l] = saved.filter[l];
                }
            }
            REQUIRE(snapshot(p) == expected);
        }
    }

    SECTION("NonFiniteAndClamp") {
        const std::array<std::uint32_t, 3> nonFinite = {0x7FC00000u, 0x7F800000u, 0xFF800000u};
        const std::array<Steinberg::int32, 6> filters = {1, 2, 1, 2, 1, 2};

        for (std::uint32_t bits : nonFinite) {
            const float bad = floatFromBits(bits);
            INFO("bits " << bits);

            // Field 0 (mix) non-finite.
            {
                auto s = makeStream();
                writeRaw(*s, bad, 0.41f, filters);
                rewindStream(*s);
                EcologyParams p;
                applySet(p, kDirty);
                Steinberg::IBStreamer r(s, kLittleEndian);
                REQUIRE(loadEcologyParams(p, r));
                CHECK(bitsOf(p.mix.load()) == bitsOf(kDirty.mix));
                CHECK(bitsOf(p.loopGain.load()) == bitsOf(0.41f));
                for (std::size_t l = 0; l < 6; ++l) {
                    CHECK(p.loopFilterMode[l].load() == filters[l]);
                }
            }
            // Field 1 (loop gain) non-finite.
            {
                auto s = makeStream();
                writeRaw(*s, 0.37f, bad, filters);
                rewindStream(*s);
                EcologyParams p;
                applySet(p, kDirty);
                Steinberg::IBStreamer r(s, kLittleEndian);
                REQUIRE(loadEcologyParams(p, r));
                CHECK(bitsOf(p.mix.load()) == bitsOf(0.37f));
                CHECK(bitsOf(p.loopGain.load()) == bitsOf(kDirty.loopGain));
                for (std::size_t l = 0; l < 6; ++l) {
                    CHECK(p.loopFilterMode[l].load() == filters[l]);
                }
            }
        }

        // Finite out-of-range floats clamp; indices -1 / 3 clamp to 0 / 2.
        {
            auto s = makeStream();
            writeRaw(*s, 1.5f, 0.95f, {-1, 3, -1, 3, -1, 3});
            rewindStream(*s);
            EcologyParams p;
            Steinberg::IBStreamer r(s, kLittleEndian);
            REQUIRE(loadEcologyParams(p, r));
            CHECK(p.mix.load() == 1.0f);
            CHECK(p.loopGain.load() == Krate::DSP::FeedbackEcology::kMaxLoopGain);
            for (std::size_t l = 0; l < 6; ++l) {
                CHECK(p.loopFilterMode[l].load() == ((l % 2 == 0) ? 0 : 2));
            }
        }
        {
            auto s = makeStream();
            writeRaw(*s, -0.5f, -1.0f, {3, -1, 3, -1, 3, -1});
            rewindStream(*s);
            EcologyParams p;
            Steinberg::IBStreamer r(s, kLittleEndian);
            REQUIRE(loadEcologyParams(p, r));
            CHECK(p.mix.load() == 0.0f);
            CHECK(p.loopGain.load() == 0.0f);
            for (std::size_t l = 0; l < 6; ++l) {
                CHECK(p.loopFilterMode[l].load() == ((l % 2 == 0) ? 2 : 0));
            }
        }
    }

    SECTION("ControllerMirror") {
        EcologyParams out;
        applySet(out, kSaved);
        auto stream = makeStream();
        {
            Steinberg::IBStreamer w(stream, kLittleEndian);
            saveEcologyParams(out, w);
        }
        rewindStream(*stream);

        std::vector<std::pair<Vst::ParamID, double>> got;
        {
            Steinberg::IBStreamer r(stream, kLittleEndian);
            loadEcologyParamsToController(
                r, [&got](Vst::ParamID id, double n) { got.emplace_back(id, n); });
        }
        REQUIRE(got.size() == kN);

        CHECK(got[0].first == kEcologyMixId);
        CHECK(got[0].second ==
              Approx(linearToNormalized(static_cast<double>(kSaved.mix), 0.0, 1.0)).margin(1e-9));
        CHECK(got[1].first == kEcologyLoopGainId);
        CHECK(got[1].second ==
              Approx(linearToNormalized(static_cast<double>(kSaved.loopGain), 0.0, 0.9))
                  .margin(1e-9));
        for (std::size_t l = 0; l < 6; ++l) {
            CHECK(got[2 + l].first == kFilterIds[l]);
            CHECK(got[2 + l].second == Approx(indexToNormalized(kSaved.filter[l], 3)).margin(1e-9));
        }
    }

    SECTION("FormatNonEmpty") {
        for (Vst::ParamID id : {static_cast<Vst::ParamID>(kEcologyMixId),
                                static_cast<Vst::ParamID>(kEcologyLoopGainId)}) {
            for (double v : {0.0, 0.5, 1.0}) {
                INFO("id " << id << " v " << v);
                Vst::String128 s{};
                REQUIRE(formatEcologyParam(id, v, s) == Steinberg::kResultOk);
                CHECK_FALSE(isEmpty(s));
            }
        }
        // List parameters format themselves; unknown ids are rejected.
        Vst::String128 s{};
        CHECK(formatEcologyParam(kEcologyLoop0FilterModeId, 0.5, s) == Steinberg::kResultFalse);
        CHECK(formatEcologyParam(502, 0.5, s) == Steinberg::kResultFalse);
    }
}
