// ==============================================================================
// Vorago Phase 12 - noise parameter pack contract (T017)
// ==============================================================================
// The eight-section pack contract of specs/vorago-phase12-parameters/tasks.md
// (Group 7) for the Noise pack: 23 IDs in 300-399, 92-byte stream.
// Stream field order = ascending ID: 3F (level, wake, wander), 4I model,
// 4I type, 4F comb fundamental, 4F comb spread, 4F comb feedback.
//
// Built with -fno-fast-math (tests/CMakeLists.txt): the NaN / +-Inf payloads
// below are produced from bit patterns through a volatile uint32 + memcpy.
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "parameters/noise_params.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <algorithm>
#include <array>
#include <atomic>
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

using Steinberg::Vst::ParamID;

enum class Kind : std::uint8_t { Lin, Log, List };

struct Desc {
    ParamID id;
    const char* title;
    const char* units;
    Kind kind;
    double mn;       // plain min (list: 0)
    double mx;       // plain max (list: count - 1)
    int count;       // list entries (continuous: 0)
    double def;      // plain default (list: index)
    double n0;       // registered default normalized
};

// The T017 table, transcribed literally (ascending ID == stream order).
const std::array<Desc, 23> kTable{{
    {.id=300, .title="Noise Level", .units="dB", .kind=Kind::Lin, .mn=-96.0, .mx=12.0, .count=0, .def=-18.0, .n0=0.722222222},
    {.id=301, .title="Noise Wake", .units="%", .kind=Kind::Lin, .mn=0.0, .mx=1.0, .count=0, .def=0.35, .n0=0.35},
    {.id=302, .title="Noise Wander Rate", .units="Hz", .kind=Kind::Log, .mn=0.01, .mx=100.0, .count=0, .def=0.03, .n0=0.119280314},
    {.id=310, .title="Noise Slot 1 Model", .units="", .kind=Kind::List, .mn=0.0, .mx=3.0, .count=4, .def=1.0, .n0=1.0 / 3.0},
    {.id=311, .title="Noise Slot 2 Model", .units="", .kind=Kind::List, .mn=0.0, .mx=3.0, .count=4, .def=2.0, .n0=2.0 / 3.0},
    {.id=312, .title="Noise Slot 3 Model", .units="", .kind=Kind::List, .mn=0.0, .mx=3.0, .count=4, .def=0.0, .n0=0.0},
    {.id=313, .title="Noise Slot 4 Model", .units="", .kind=Kind::List, .mn=0.0, .mx=3.0, .count=4, .def=3.0, .n0=1.0},
    {.id=320, .title="Noise Slot 1 Type", .units="", .kind=Kind::List, .mn=0.0, .mx=11.0, .count=12, .def=5.0, .n0=5.0 / 11.0},
    {.id=321, .title="Noise Slot 2 Type", .units="", .kind=Kind::List, .mn=0.0, .mx=11.0, .count=12, .def=5.0, .n0=5.0 / 11.0},
    {.id=322, .title="Noise Slot 3 Type", .units="", .kind=Kind::List, .mn=0.0, .mx=11.0, .count=12, .def=5.0, .n0=5.0 / 11.0},
    {.id=323, .title="Noise Slot 4 Type", .units="", .kind=Kind::List, .mn=0.0, .mx=11.0, .count=12, .def=5.0, .n0=5.0 / 11.0},
    {.id=330, .title="Noise Slot 1 Comb Freq", .units="Hz", .kind=Kind::Log, .mn=20.0, .mx=19845.0, .count=0, .def=60.0, .n0=0.159219747},
    {.id=331, .title="Noise Slot 2 Comb Freq", .units="Hz", .kind=Kind::Log, .mn=20.0, .mx=19845.0, .count=0, .def=60.0, .n0=0.159219747},
    {.id=332, .title="Noise Slot 3 Comb Freq", .units="Hz", .kind=Kind::Log, .mn=20.0, .mx=19845.0, .count=0, .def=60.0, .n0=0.159219747},
    {.id=333, .title="Noise Slot 4 Comb Freq", .units="Hz", .kind=Kind::Log, .mn=20.0, .mx=19845.0, .count=0, .def=60.0, .n0=0.159219747},
    {.id=340, .title="Noise Slot 1 Comb Spread", .units="%", .kind=Kind::Lin, .mn=0.0, .mx=1.0, .count=0, .def=0.35, .n0=0.35},
    {.id=341, .title="Noise Slot 2 Comb Spread", .units="%", .kind=Kind::Lin, .mn=0.0, .mx=1.0, .count=0, .def=0.35, .n0=0.35},
    {.id=342, .title="Noise Slot 3 Comb Spread", .units="%", .kind=Kind::Lin, .mn=0.0, .mx=1.0, .count=0, .def=0.35, .n0=0.35},
    {.id=343, .title="Noise Slot 4 Comb Spread", .units="%", .kind=Kind::Lin, .mn=0.0, .mx=1.0, .count=0, .def=0.35, .n0=0.35},
    {.id=350, .title="Noise Slot 1 Comb Feedback", .units="%", .kind=Kind::Lin, .mn=0.0, .mx=0.9, .count=0, .def=0.55, .n0=0.611111111},
    {.id=351, .title="Noise Slot 2 Comb Feedback", .units="%", .kind=Kind::Lin, .mn=0.0, .mx=0.9, .count=0, .def=0.55, .n0=0.611111111},
    {.id=352, .title="Noise Slot 3 Comb Feedback", .units="%", .kind=Kind::Lin, .mn=0.0, .mx=0.9, .count=0, .def=0.55, .n0=0.611111111},
    {.id=353, .title="Noise Slot 4 Comb Feedback", .units="%", .kind=Kind::Lin, .mn=0.0, .mx=0.9, .count=0, .def=0.75, .n0=0.833333333},
}};

constexpr std::size_t kN = 23;
constexpr std::size_t kB = 92;

// Test-local field accessors (independent of any header helper).
std::atomic<float>* floatField(::Vorago::NoiseParams& p, ParamID id) {
    switch (id) {
        case 300: return &p.levelDb;
        case 301: return &p.wake;
        case 302: return &p.wanderRateHz;
        default: break;
    }
    if (id >= 330 && id <= 333) return &p.combFundamentalHz[id - 330];
    if (id >= 340 && id <= 343) return &p.combSpread[id - 340];
    if (id >= 350 && id <= 353) return &p.combFeedback[id - 350];
    return nullptr;
}

std::atomic<int>* intField(::Vorago::NoiseParams& p, ParamID id) {
    if (id >= 310 && id <= 313) return &p.model[id - 310];
    if (id >= 320 && id <= 323) return &p.type[id - 320];
    return nullptr;
}

// Bit image of field k (float bits, or the int32 bits of an index).
std::uint32_t fieldBits(::Vorago::NoiseParams& p, std::size_t k) {
    const Desc& d = kTable[k];
    if (d.kind == Kind::List)
        return static_cast<std::uint32_t>(intField(p, d.id)->load());
    return std::bit_cast<std::uint32_t>(floatField(p, d.id)->load());
}

std::array<std::uint32_t, kN> snapshot(::Vorago::NoiseParams& p) {
    std::array<std::uint32_t, kN> s{};
    for (std::size_t k = 0; k < kN; ++k) s[k] = fieldBits(p, k);
    return s;
}

// Field k's value as a word; frac in (0,1) picks an in-range non-default plain value.
std::uint32_t valueWord(std::size_t k, double frac, int indexShift) {
    const Desc& d = kTable[k];
    if (d.kind == Kind::List) {
        const int idx = (static_cast<int>(d.def) + indexShift) % d.count;
        return static_cast<std::uint32_t>(idx);
    }
    const float v = static_cast<float>(d.mn + frac * (d.mx - d.mn));
    return std::bit_cast<std::uint32_t>(v);
}

void setFromWords(::Vorago::NoiseParams& p, const std::array<std::uint32_t, kN>& w) {
    for (std::size_t k = 0; k < kN; ++k) {
        const Desc& d = kTable[k];
        if (d.kind == Kind::List)
            intField(p, d.id)->store(static_cast<int>(w[k]));
        else
            floatField(p, d.id)->store(std::bit_cast<float>(w[k]));
    }
}

// Two distinct in-range, non-default value sets.
std::array<std::uint32_t, kN> savedWords() {
    std::array<std::uint32_t, kN> w{};
    for (std::size_t k = 0; k < kN; ++k) w[k] = valueWord(k, 0.2 + 0.03 * static_cast<double>(k), 1);
    return w;
}

std::array<std::uint32_t, kN> dirtyWords() {
    std::array<std::uint32_t, kN> w{};
    for (std::size_t k = 0; k < kN; ++k) w[k] = valueWord(k, 0.9 - 0.025 * static_cast<double>(k), 2);
    return w;
}

Steinberg::IPtr<Steinberg::MemoryStream> makeStream() {
    return Steinberg::owned(new Steinberg::MemoryStream());
}

void rewindStream(Steinberg::MemoryStream& s) {
    REQUIRE(s.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) == Steinberg::kResultOk);
}

// Raw little-endian words -> stream (via writeInt32, byte-exact for any payload).
Steinberg::IPtr<Steinberg::MemoryStream> streamFromWords(const std::vector<std::uint32_t>& w) {
    auto s = makeStream();
    Steinberg::IBStreamer ws(s, kLittleEndian);
    for (auto word : w) REQUIRE(ws.writeInt32(std::bit_cast<Steinberg::int32>(word)));
    rewindStream(*s);
    return s;
}

std::uint32_t leWordAt(const Steinberg::MemoryStream& s, std::size_t offset) {
    REQUIRE(offset + 4u <= static_cast<std::size_t>(s.getSize()));
    const auto* p = reinterpret_cast<const unsigned char*>(s.getData()) + offset;  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8u) |
           (static_cast<std::uint32_t>(p[2]) << 16u) | (static_cast<std::uint32_t>(p[3]) << 24u);
}

std::uint32_t patternBits(std::uint32_t bits) {
    volatile std::uint32_t v = bits;  // defeats constant folding of the payload
    const std::uint32_t b = v;
    float f = 0.0f;
    std::memcpy(&f, &b, sizeof f);
    std::uint32_t out = 0;
    std::memcpy(&out, &f, sizeof out);
    return out;
}

std::string toAsciiString(const Steinberg::Vst::String128 s) {
    char buf[128] = {};
    Steinberg::UString(const_cast<Steinberg::Vst::TChar*>(s), 128)  // NOLINT(cppcoreguidelines-pro-type-const-cast)
        .toAscii(buf, 128);
    return {buf};
}

double inverseMap(const Desc& d, double plain) {
    switch (d.kind) {
        case Kind::Lin: return (plain - d.mn) / (d.mx - d.mn);
        case Kind::Log: return std::log(plain / d.mn) / std::log(d.mx / d.mn);
        case Kind::List: return plain / static_cast<double>(d.count - 1);
    }
    return -1.0;
}

bool inTable(ParamID id) {
    return std::ranges::any_of(kTable, [id](const auto& d) { return d.id == id; });
}

}  // namespace

TEST_CASE("Vorago_NoiseParamsContract", "[vorago][params]") {
    using Catch::Approx;
    using namespace ::Vorago;
    using namespace Steinberg;
    using namespace Steinberg::Vst;

    SECTION("Registration") {
        ParameterContainer pc;
        registerNoiseParams(pc);
        REQUIRE(pc.getParameterCount() == static_cast<int32>(kN));
        for (const auto& d : kTable) {
            INFO("id " << d.id);
            auto* p = pc.getParameter(d.id);
            REQUIRE(p != nullptr);
            const ParameterInfo& info = p->getInfo();
            REQUIRE(toAsciiString(info.title) == d.title);
            REQUIRE(toAsciiString(info.units) == d.units);
            if (d.kind == Kind::List) {
                REQUIRE(info.stepCount == d.count - 1);
                REQUIRE(info.flags == (ParameterInfo::kCanAutomate | ParameterInfo::kIsList));
                // P-1: the current value must also sit on the default.
                REQUIRE(p->getNormalized() == Approx(d.n0).margin(1e-9));
            } else {
                REQUIRE(info.stepCount == 0);
                REQUIRE(info.flags == ParameterInfo::kCanAutomate);
            }
            REQUIRE(info.defaultNormalizedValue == Approx(d.n0).margin(1e-9));
        }
    }

    SECTION("DefaultsDenormalizeToPlain") {
        NoiseParams fresh;
        for (const auto& d : kTable) {
            INFO("id " << d.id);
            NoiseParams p;
            handleNoiseParamChange(p, d.id, d.n0);
            if (d.kind == Kind::List) {
                REQUIRE(intField(fresh, d.id)->load() == static_cast<int>(d.def));
                REQUIRE(intField(p, d.id)->load() == static_cast<int>(d.def));
                handleNoiseParamChange(p, d.id, 0.0);
                REQUIRE(intField(p, d.id)->load() == 0);
                handleNoiseParamChange(p, d.id, 1.0);
                REQUIRE(intField(p, d.id)->load() == d.count - 1);
            } else {
                REQUIRE(static_cast<double>(floatField(fresh, d.id)->load()) ==
                        Approx(d.def).epsilon(1e-6));
                REQUIRE(static_cast<double>(floatField(p, d.id)->load()) ==
                        Approx(d.def).epsilon(1e-6));
                handleNoiseParamChange(p, d.id, 0.0);
                REQUIRE(static_cast<double>(floatField(p, d.id)->load()) ==
                        Approx(d.mn).epsilon(1e-6).margin(1e-9));
                handleNoiseParamChange(p, d.id, 1.0);
                REQUIRE(static_cast<double>(floatField(p, d.id)->load()) ==
                        Approx(d.mx).epsilon(1e-6).margin(1e-9));
            }
        }
    }

    SECTION("UnregisteredInBandIgnored") {
        NoiseParams p;
        setFromWords(p, savedWords());
        const auto before = snapshot(p);
        int probed = 0;
        for (ParamID id = 300; id < kNoiseParamRangeEnd; ++id) {
            if (inTable(id)) continue;
            for (double v : {0.0, 0.37, 1.0}) {
                handleNoiseParamChange(p, id, v);
                REQUIRE(snapshot(p) == before);
            }
            ++probed;
        }
        REQUIRE(probed == 100 - static_cast<int>(kN));
    }

    const auto saved = savedWords();

    SECTION("SaveLoadRoundTrip") {
        NoiseParams src;
        setFromWords(src, saved);
        auto s = makeStream();
        {
            IBStreamer w(s, kLittleEndian);
            saveNoiseParams(src, w);
        }
        REQUIRE(std::cmp_equal(s->getSize(), kB));
        // Field order = ascending ID, 4 bytes each.
        for (std::size_t k = 0; k < kN; ++k) {
            INFO("field " << k << " id " << kTable[k].id);
            REQUIRE(leWordAt(*s, 4 * k) == saved[k]);
        }
        rewindStream(*s);
        NoiseParams dst;
        IBStreamer r(s, kLittleEndian);
        REQUIRE(loadNoiseParams(dst, r));
        REQUIRE(snapshot(dst) == saved);
    }

    SECTION("TruncationEveryOffset") {
        const std::vector<std::uint32_t> full(saved.begin(), saved.end());
        auto fullStream = streamFromWords(full);
        const auto dirty = dirtyWords();
        for (std::size_t k = 0; k < kN; ++k) REQUIRE(dirty[k] != saved[k]);

        for (std::size_t c = 0; c < kB; ++c) {
            INFO("cut " << c);
            auto s = makeStream();
            int32 written = 0;
            REQUIRE(s->write(fullStream->getData(), static_cast<int32>(c), &written) ==
                    kResultOk);
            REQUIRE(std::cmp_equal(written, c));
            rewindStream(*s);

            NoiseParams p;
            setFromWords(p, dirty);
            IBStreamer r(s, kLittleEndian);
            REQUIRE_FALSE(loadNoiseParams(p, r));
            const auto got = snapshot(p);
            for (std::size_t k = 0; k < kN; ++k) {
                INFO("field " << k);
                if (4 * (k + 1) <= c)
                    REQUIRE(got[k] == saved[k]);
                else
                    REQUIRE(got[k] == dirty[k]);
            }
        }
    }

    SECTION("NonFiniteAndClamp") {
        const auto dirty = dirtyWords();
        const std::array<std::uint32_t, 3> bad{patternBits(0x7FC00000u),   // NaN
                                               patternBits(0x7F800000u),   // +Inf
                                               patternBits(0xFF800000u)};  // -Inf
        for (std::size_t k = 0; k < kN; ++k) {
            const Desc& d = kTable[k];
            if (d.kind == Kind::List) continue;
            for (auto b : bad) {
                INFO("field " << k << " bits " << b);
                std::vector<std::uint32_t> w(saved.begin(), saved.end());
                w[k] = b;
                auto s = streamFromWords(w);
                NoiseParams p;
                setFromWords(p, dirty);
                IBStreamer r(s, kLittleEndian);
                REQUIRE(loadNoiseParams(p, r));
                const auto got = snapshot(p);
                for (std::size_t j = 0; j < kN; ++j) {
                    INFO("field " << j);
                    REQUIRE(got[j] == (j == k ? dirty[j] : saved[j]));
                }
            }
        }

        // Finite out-of-range floats clamp; indices -1 / n clamp to 0 / n-1.
        std::vector<std::uint32_t> lo(saved.begin(), saved.end());
        std::vector<std::uint32_t> hi(saved.begin(), saved.end());
        for (std::size_t k = 0; k < kN; ++k) {
            const Desc& d = kTable[k];
            if (d.kind == Kind::List) {
                lo[k] = static_cast<std::uint32_t>(-1);
                hi[k] = static_cast<std::uint32_t>(d.count);
            } else {
                const double span = d.mx - d.mn;
                lo[k] = std::bit_cast<std::uint32_t>(static_cast<float>(d.mn - span - 1.0));
                hi[k] = std::bit_cast<std::uint32_t>(static_cast<float>(d.mx + span + 1.0));
            }
        }
        NoiseParams pl;
        NoiseParams ph;
        {
            auto s = streamFromWords(lo);
            IBStreamer r(s, kLittleEndian);
            REQUIRE(loadNoiseParams(pl, r));
        }
        {
            auto s = streamFromWords(hi);
            IBStreamer r(s, kLittleEndian);
            REQUIRE(loadNoiseParams(ph, r));
        }
        for (const auto& d : kTable) {
            INFO("id " << d.id);
            if (d.kind == Kind::List) {
                REQUIRE(intField(pl, d.id)->load() == 0);
                REQUIRE(intField(ph, d.id)->load() == d.count - 1);
            } else {
                REQUIRE(floatField(pl, d.id)->load() == static_cast<float>(d.mn));
                REQUIRE(floatField(ph, d.id)->load() == static_cast<float>(d.mx));
            }
        }
    }

    SECTION("ControllerMirror") {
        const std::vector<std::uint32_t> full(saved.begin(), saved.end());
        auto s = streamFromWords(full);
        std::map<ParamID, double> got;
        std::vector<ParamID> order;
        {
            IBStreamer r(s, kLittleEndian);
            loadNoiseParamsToController(r, [&](ParamID id, double n) {
                got[id] = n;
                order.push_back(id);
            });
        }
        REQUIRE(order.size() == kN);
        for (std::size_t k = 0; k < kN; ++k) {
            const Desc& d = kTable[k];
            INFO("id " << d.id);
            REQUIRE(order[k] == d.id);
            const double plain = (d.kind == Kind::List)
                                     ? static_cast<double>(static_cast<int>(saved[k]))
                                     : static_cast<double>(std::bit_cast<float>(saved[k]));
            REQUIRE(got[d.id] == Approx(inverseMap(d, plain)).margin(1e-9));
        }
    }

    SECTION("FormatNonEmpty") {
        for (const auto& d : kTable) {
            INFO("id " << d.id);
            if (d.kind == Kind::List) {
                String128 str{};
                REQUIRE(formatNoiseParam(d.id, 0.5, str) == kResultFalse);
                continue;
            }
            for (double v : {0.0, 0.5, 1.0}) {
                String128 out{};
                REQUIRE(formatNoiseParam(d.id, v, out) == kResultOk);
                REQUIRE_FALSE(toAsciiString(out).empty());
            }
        }
        String128 str{};
        REQUIRE(formatNoiseParam(303, 0.5, str) == kResultFalse);
    }
}
