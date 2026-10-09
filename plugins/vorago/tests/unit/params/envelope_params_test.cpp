// ==============================================================================
// Vorago Phase 12 - envelope parameter pack contract (T026)
// ==============================================================================
// The Group 7 shared test shape (specs/vorago-phase12-parameters/tasks.md) for
// the Envelope pack: 7 IDs 1200-1206, stream 28 bytes (int32 mode + 6 floats).
// Defaults are pinned to VoragoVoice's shipped constants (vorago_voice.h:322-333).
//
// Built with -fno-fast-math (tests/CMakeLists.txt): NaN/Inf payloads are produced
// from bit patterns through a volatile uint32 + memcpy.
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "parameters/envelope_params.h"
#include "parameters/param_mapping.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <krate/dsp/systems/vorago_voice.h>

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <utility>

namespace {

using Steinberg::Vst::ParamID;
using Voice = Krate::DSP::VoragoVoice;

constexpr std::size_t kNumIds = 7;
constexpr std::size_t kStreamBytes = 28;

// Continuous IDs in stream order (fields 1..6), with their table values.
constexpr std::array<ParamID, 6> kFloatIds = {
    Vorago::kEnvelopeStage0TimeId, Vorago::kEnvelopeStage1TimeId, Vorago::kEnvelopeStage2TimeId,
    Vorago::kEnvelopeStage3TimeId, Vorago::kEnvelopeReleaseId,    Vorago::kEnvelopeGrowthDurationId};

constexpr std::array<double, 6> kDefaultPlain = {20000.0, 30000.0, 45000.0,
                                                 60000.0, 45000.0, 120.0};
constexpr std::array<double, 6> kDefaultNorm = {0.809284413, 0.852434578, 0.895590654,
                                                0.926212855, 0.895590654, 1.0};

bool isGrowthId(ParamID id) { return id == Vorago::kEnvelopeGrowthDurationId; }

double plainMin(ParamID id) { return isGrowthId(id) ? 1.0 : 0.0; }
double plainMax(ParamID id) { return isGrowthId(id) ? 120.0 : 120000.0; }

// Independent inverse map (plan section 3.2 taper column).
double toNorm(ParamID id, double plain) {
    return isGrowthId(id) ? Krate::Plugins::logMapToNormalized(plain, 1.0, 120.0)
                          : Vorago::offsetLogToNormalized(plain, 0.0, 120000.0, 10.0);
}

std::string toAscii(const Steinberg::Vst::String128 s) {
    char buf[128] = {};
    Steinberg::UString(const_cast<Steinberg::Vst::TChar*>(s), 128)  // NOLINT(cppcoreguidelines-pro-type-const-cast)
        .toAscii(buf, 128);
    return {buf};
}

std::atomic<float>& floatField(Vorago::EnvelopeParams& p, std::size_t i) {
    switch (i) {
        case 0: return p.stage0TimeMs;
        case 1: return p.stage1TimeMs;
        case 2: return p.stage2TimeMs;
        case 3: return p.stage3TimeMs;
        case 4: return p.releaseMs;
        default: return p.growthDurationSeconds;
    }
}

// Stream-order snapshot: [0] mode (int bits), [1..6] float bits.
std::array<std::uint32_t, kNumIds> snapshot(Vorago::EnvelopeParams& p) {
    std::array<std::uint32_t, kNumIds> bits{};
    bits[0] = std::bit_cast<std::uint32_t>(p.mode.load());
    for (std::size_t i = 0; i < 6; ++i) {
        bits[i + 1] = std::bit_cast<std::uint32_t>(floatField(p, i).load());
    }
    return bits;
}

void setNonDefaults(Vorago::EnvelopeParams& p) {
    p.mode.store(1);
    p.stage0TimeMs.store(1000.0f);
    p.stage1TimeMs.store(2000.5f);
    p.stage2TimeMs.store(3000.25f);
    p.stage3TimeMs.store(4000.0f);
    p.releaseMs.store(5000.75f);
    p.growthDurationSeconds.store(30.5f);
}

// Values distinct from both the defaults and setNonDefaults().
void dirty(Vorago::EnvelopeParams& p) {
    p.mode.store(0);
    p.stage0TimeMs.store(7777.0f);
    p.stage1TimeMs.store(8888.0f);
    p.stage2TimeMs.store(9999.0f);
    p.stage3TimeMs.store(11111.0f);
    p.releaseMs.store(12121.0f);
    p.growthDurationSeconds.store(77.0f);
}

Steinberg::IPtr<Steinberg::MemoryStream> makeStream() {
    return Steinberg::owned(new Steinberg::MemoryStream());
}

void rewindStream(Steinberg::MemoryStream& s) {
    REQUIRE(s.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) == Steinberg::kResultOk);
}

Steinberg::IPtr<Steinberg::MemoryStream> saveToStream(const Vorago::EnvelopeParams& p) {
    auto s = makeStream();
    Steinberg::IBStreamer w(s, kLittleEndian);
    Vorago::saveEnvelopeParams(p, w);
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

}  // namespace

TEST_CASE("Vorago_EnvelopeParamsContract", "[vorago][params]") {
    using namespace Steinberg;
    using namespace Steinberg::Vst;

    SECTION("Registration") {
        ParameterContainer pc;
        Vorago::registerEnvelopeParams(pc);
        REQUIRE(pc.getParameterCount() == static_cast<int32>(kNumIds));

        struct Expect {
            ParamID id;
            const char* title;
            const char* units;
            int32 stepCount;
            bool isList;
            double n0;
        };
        const std::array<Expect, kNumIds> expect = {{
            {.id=Vorago::kEnvelopeModeId, .title="Envelope Mode", .units="", .stepCount=1, .isList=true, .n0=0.0},
            {.id=Vorago::kEnvelopeStage0TimeId, .title="Envelope Stage 1 Time", .units="ms", .stepCount=0, .isList=false, .n0=0.809284413},
            {.id=Vorago::kEnvelopeStage1TimeId, .title="Envelope Stage 2 Time", .units="ms", .stepCount=0, .isList=false, .n0=0.852434578},
            {.id=Vorago::kEnvelopeStage2TimeId, .title="Envelope Stage 3 Time", .units="ms", .stepCount=0, .isList=false, .n0=0.895590654},
            {.id=Vorago::kEnvelopeStage3TimeId, .title="Envelope Stage 4 Time", .units="ms", .stepCount=0, .isList=false, .n0=0.926212855},
            {.id=Vorago::kEnvelopeReleaseId, .title="Envelope Release", .units="ms", .stepCount=0, .isList=false, .n0=0.895590654},
            {.id=Vorago::kEnvelopeGrowthDurationId, .title="Envelope Growth Duration", .units="s", .stepCount=0, .isList=false, .n0=1.0},
        }};

        for (const auto& e : expect) {
            INFO("id " << e.id);
            auto* param = pc.getParameter(e.id);
            REQUIRE(param != nullptr);
            const auto& info = param->getInfo();
            CHECK(toAscii(info.title) == e.title);
            CHECK(toAscii(info.units) == e.units);
            CHECK(info.stepCount == e.stepCount);
            CHECK((info.flags & Steinberg::Vst::ParameterInfo::kCanAutomate) != 0);
            CHECK(((info.flags & Steinberg::Vst::ParameterInfo::kIsList) != 0) == e.isList);
            CHECK(info.defaultNormalizedValue == Catch::Approx(e.n0).margin(1e-9));
        }

        // The mode list labels, in index order.
        auto* mode = pc.getParameter(Vorago::kEnvelopeModeId);
        REQUIRE(mode != nullptr);
        String128 label{};
        mode->toString(0.0, label);
        CHECK(toAscii(label) == "Standard");
        mode->toString(1.0, label);
        CHECK(toAscii(label) == "Growth");
    }

    SECTION("DefaultsDenormalizeToPlain") {
        // Defaults pinned to the voice's shipped constants (vorago_voice.h:322-333).
        Vorago::EnvelopeParams fresh;
        CHECK(fresh.mode.load() == 0);
        CHECK(static_cast<int>(Voice::EnvelopeMode::Standard) == 0);
        CHECK(static_cast<int>(Voice::EnvelopeMode::Growth) == 1);
        for (std::size_t i = 0; i < 4; ++i) {
            CHECK(floatField(fresh, i).load() == Voice::kDefaultStageTimesMs[i]);
        }
        CHECK(fresh.releaseMs.load() == Voice::kDefaultReleaseMs);
        CHECK(fresh.growthDurationSeconds.load() == Voice::kDefaultGrowthDurationSeconds);
        for (std::size_t i = 0; i < 6; ++i) {
            CHECK(static_cast<double>(floatField(fresh, i).load()) == kDefaultPlain[i]);
        }

        // handle(n0) reproduces the default plain value.
        Vorago::EnvelopeParams p;
        dirty(p);
        Vorago::handleEnvelopeParamChange(p, Vorago::kEnvelopeModeId, 0.0);
        CHECK(p.mode.load() == 0);
        for (std::size_t i = 0; i < 6; ++i) {
            INFO("field " << i);
            Vorago::handleEnvelopeParamChange(p, kFloatIds[i], kDefaultNorm[i]);
            CHECK(static_cast<double>(floatField(p, i).load()) ==
                  Catch::Approx(kDefaultPlain[i]).epsilon(1e-6));
        }

        // n = 0 and n = 1 reach the range ends.
        Vorago::handleEnvelopeParamChange(p, Vorago::kEnvelopeModeId, 1.0);
        CHECK(p.mode.load() == 1);
        Vorago::handleEnvelopeParamChange(p, Vorago::kEnvelopeModeId, 0.0);
        CHECK(p.mode.load() == 0);
        for (std::size_t i = 0; i < 6; ++i) {
            INFO("field " << i);
            Vorago::handleEnvelopeParamChange(p, kFloatIds[i], 0.0);
            CHECK(static_cast<double>(floatField(p, i).load()) == plainMin(kFloatIds[i]));
            Vorago::handleEnvelopeParamChange(p, kFloatIds[i], 1.0);
            CHECK(static_cast<double>(floatField(p, i).load()) == plainMax(kFloatIds[i]));
        }

        // Offset-log midpoint (plan section 3.3.1): 1085.49 ms, no log(0) at n = 0.
        Vorago::handleEnvelopeParamChange(p, Vorago::kEnvelopeStage0TimeId, 0.5);
        CHECK(static_cast<double>(p.stage0TimeMs.load()) == Catch::Approx(1085.49).margin(0.01));
        Vorago::handleEnvelopeParamChange(p, Vorago::kEnvelopeGrowthDurationId, 0.5);
        CHECK(static_cast<double>(p.growthDurationSeconds.load()) ==
              Catch::Approx(10.9545).epsilon(1e-4));
    }

    SECTION("UnregisteredInBandIgnored") {
        Vorago::EnvelopeParams p;
        setNonDefaults(p);
        const auto before = snapshot(p);
        for (const ParamID id : {ParamID{1207}, ParamID{1208}, ParamID{1250}, ParamID{1299}}) {
            for (const double v : {0.0, 0.37, 1.0}) {
                Vorago::handleEnvelopeParamChange(p, id, v);
                CHECK(snapshot(p) == before);
            }
        }
    }

    SECTION("SaveLoadRoundTrip") {
        Vorago::EnvelopeParams src;
        setNonDefaults(src);
        auto s = saveToStream(src);
        CHECK(std::cmp_equal(s->getSize(), kStreamBytes));

        Vorago::EnvelopeParams dst;
        IBStreamer r(s, kLittleEndian);
        CHECK(Vorago::loadEnvelopeParams(dst, r));
        CHECK(snapshot(dst) == snapshot(src));
    }

    SECTION("TruncationEveryOffset") {
        Vorago::EnvelopeParams src;
        setNonDefaults(src);
        auto full = saveToStream(src);
        REQUIRE(std::cmp_equal(full->getSize(), kStreamBytes));
        const auto saved = snapshot(src);

        Vorago::EnvelopeParams dirtyRef;
        dirty(dirtyRef);
        const auto dirtyBits = snapshot(dirtyRef);

        for (std::size_t c = 0; c < kStreamBytes; ++c) {
            INFO("cut " << c);
            auto cut = makeStream();
            if (c > 0) {
                int32 written = 0;
                REQUIRE(cut->write(full->getData(), static_cast<int32>(c), &written) == kResultOk);
                REQUIRE(std::cmp_equal(written, c));
            }
            rewindStream(*cut);

            Vorago::EnvelopeParams p;
            dirty(p);
            IBStreamer r(cut, kLittleEndian);
            CHECK_FALSE(Vorago::loadEnvelopeParams(p, r));
            const auto got = snapshot(p);
            for (std::size_t f = 0; f < kNumIds; ++f) {
                INFO("field " << f);
                if ((f + 1) * 4 <= c) {
                    CHECK(got[f] == saved[f]);
                } else {
                    CHECK(got[f] == dirtyBits[f]);
                }
            }
        }
    }

    SECTION("NonFiniteAndClamp") {
        constexpr std::array<std::uint32_t, 3> kPatterns = {0x7FC00000u,   // quiet NaN
                                                            0x7F800000u,   // +Inf
                                                            0xFF800000u};  // -Inf
        Vorago::EnvelopeParams src;
        setNonDefaults(src);
        const auto saved = snapshot(src);

        Vorago::EnvelopeParams dirtyRef;
        dirty(dirtyRef);
        const auto dirtyBits = snapshot(dirtyRef);

        for (const std::uint32_t pattern : kPatterns) {
            for (std::size_t k = 0; k < 6; ++k) {
                INFO("pattern " << pattern << " field " << k);
                auto s = makeStream();
                {
                    IBStreamer w(s, kLittleEndian);
                    w.writeInt32(src.mode.load());
                    for (std::size_t i = 0; i < 6; ++i) {
                        w.writeFloat(i == k ? floatFromBits(pattern) : floatField(src, i).load());
                    }
                }
                rewindStream(*s);

                Vorago::EnvelopeParams p;
                dirty(p);
                IBStreamer r(s, kLittleEndian);
                CHECK(Vorago::loadEnvelopeParams(p, r));
                const auto got = snapshot(p);
                for (std::size_t f = 0; f < kNumIds; ++f) {
                    CHECK(got[f] == (f == k + 1 ? dirtyBits[f] : saved[f]));
                }
            }
        }

        // Finite out-of-range floats clamp; indices -1 / 2 clamp to 0 / 1.
        auto writeAll = [](IBStreamer& w, int32 mode, float t, float g) {
            w.writeInt32(mode);
            for (int i = 0; i < 5; ++i) {
                w.writeFloat(t);
            }
            w.writeFloat(g);
        };
        {
            auto s = makeStream();
            {
                IBStreamer w(s, kLittleEndian);
                writeAll(w, -1, -5.0f, 0.5f);
            }
            rewindStream(*s);
            Vorago::EnvelopeParams p;
            dirty(p);
            IBStreamer r(s, kLittleEndian);
            CHECK(Vorago::loadEnvelopeParams(p, r));
            CHECK(p.mode.load() == 0);
            for (std::size_t i = 0; i < 5; ++i) {
                CHECK(floatField(p, i).load() == 0.0f);
            }
            CHECK(p.growthDurationSeconds.load() == 1.0f);
        }
        {
            auto s = makeStream();
            {
                IBStreamer w(s, kLittleEndian);
                writeAll(w, 2, 1.0e6f, 500.0f);
            }
            rewindStream(*s);
            Vorago::EnvelopeParams p;
            IBStreamer r(s, kLittleEndian);
            CHECK(Vorago::loadEnvelopeParams(p, r));
            CHECK(p.mode.load() == 1);
            for (std::size_t i = 0; i < 5; ++i) {
                CHECK(floatField(p, i).load() == 120000.0f);
            }
            CHECK(p.growthDurationSeconds.load() == 120.0f);
        }
    }

    SECTION("ControllerMirror") {
        Vorago::EnvelopeParams src;
        setNonDefaults(src);
        auto s = saveToStream(src);

        std::map<ParamID, double> got;
        IBStreamer r(s, kLittleEndian);
        Vorago::loadEnvelopeParamsToController(
            r, [&got](ParamID id, double normalized) { got[id] = normalized; });

        REQUIRE(got.size() == kNumIds);
        CHECK(got[Vorago::kEnvelopeModeId] == Catch::Approx(1.0).margin(1e-9));
        for (std::size_t i = 0; i < 6; ++i) {
            INFO("field " << i);
            const double expected =
                toNorm(kFloatIds[i], static_cast<double>(floatField(src, i).load()));
            CHECK(got[kFloatIds[i]] == Catch::Approx(expected).margin(1e-9));
        }
    }

    SECTION("FormatNonEmpty") {
        for (const ParamID id : kFloatIds) {
            for (const double v : {0.0, 0.5, 1.0}) {
                INFO("id " << id << " value " << v);
                String128 text{};
                CHECK(Vorago::formatEnvelopeParam(id, v, text) == kResultOk);
                CHECK_FALSE(toAscii(text).empty());
            }
        }
        String128 text{};
        CHECK(Vorago::formatEnvelopeParam(Vorago::kEnvelopeModeId, 0.0, text) == kResultFalse);
        CHECK(Vorago::formatEnvelopeParam(ParamID{1207}, 0.0, text) == kResultFalse);
    }
}
