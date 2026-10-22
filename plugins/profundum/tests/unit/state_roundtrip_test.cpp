// ==============================================================================
// Profundum - processor state round trip (Phase 10 T011)
// ==============================================================================
// SC-011.1 - .5, processor arm (FR-044, FR-045; plan 2.5.10, layout 3.4):
//   ByteIdentical36  getState -> fresh setState -> getState, 36 bytes, memcmp 0
//   DefaultDecode    a default processor's stream, field by field (little-endian)
//   FutureVersion    version kCurrentStateVersion + 1 -> kResultFalse, nothing moves
//   Truncated        a stream cut at byte 20 loads the globals, keeps the macros
//   Hostile          clamps, invalid voice modes, NaN / +Inf per float field
// T017 render arm (SC-011.5; FR-024, FR-028, FR-045):
//   HostileGainKeepsCeiling  gain 4.0 stored as 1.0; a Poly kChord8 at macros 1.0 stays under -1 dBTP
// T012 controller sections (SC-011.6; FR-045 controller side, FR-046):
//   ControllerMirror         ByteIdentical36 -> setComponentState, gain inverse points, v2 refused
//   ControllerMirrorHostile  the Hostile streams on a controller: exact clamps, invalid fields make no call
//
// Built with -fno-fast-math (tests/CMakeLists.txt); the non-finite payloads are
// bit patterns through a volatile (makeNaN / makePosInf, FR-062). Comparing two
// serialized STREAMS byte for byte is allowed: they are not float renders (FR-065).
// ==============================================================================

#include "profundum_test_fixture.h"

#include "controller/controller.h"
#include "parameters/global_params.h"
#include "parameters/macro_params.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

using namespace ProfundumPluginTest;

namespace {

using Steinberg::int32;
using Steinberg::Vst::ParamID;

constexpr std::array<ParamID, 4> kMacroIds{
    ::Profundum::kMacroWeightId, ::Profundum::kMacroDarkBrightId,
    ::Profundum::kMacroTightLooseId, ::Profundum::kMacroCleanViolentId,
};

Steinberg::IPtr<Steinberg::MemoryStream> makeStream() {
    return Steinberg::owned(new Steinberg::MemoryStream());
}

void rewindStream(Steinberg::MemoryStream& s) {
    REQUIRE(s.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) == Steinberg::kResultOk);
}

std::uint32_t bitsOf(float f) noexcept { return std::bit_cast<std::uint32_t>(f); }

// The eight latched atomics; floats compared by bit pattern.
struct Snapshot {
    std::uint32_t gainBits = 0;
    int voiceMode = 0;
    int polyphony = 0;
    std::uint32_t ceilingBits = 0;
    std::array<std::uint32_t, 4> macroBits{};

    bool operator==(const Snapshot&) const = default;
};

Snapshot snapshot(const ::Profundum::Processor& p) {
    const auto& g = p.globalParamsForTest();
    const auto& m = p.macroParamsForTest();
    Snapshot s;
    s.gainBits = bitsOf(g.outputGain.load(std::memory_order_relaxed));
    s.voiceMode = g.voiceMode.load(std::memory_order_relaxed);
    s.polyphony = g.polyphony.load(std::memory_order_relaxed);
    s.ceilingBits = bitsOf(g.outputCeilingDb.load(std::memory_order_relaxed));
    for (int i = 0; i < 4; ++i) {
        s.macroBits[static_cast<std::size_t>(i)] =
            bitsOf(::Profundum::macroField(m, i).load(std::memory_order_relaxed));
    }
    return s;
}

// The raw stream fields, written verbatim (no sanitising) in the plan 3.4 layout.
struct StreamFields {
    int32 version = ::Profundum::kCurrentStateVersion;
    float gain = 0.5f;
    int32 voiceMode = 0;
    int32 polyphony = 3;
    float ceilingDb = -6.0f;
    std::array<float, 4> macros{0.3f, 0.4f, 0.6f, 0.7f};
};

Steinberg::IPtr<Steinberg::MemoryStream> writeFields(const StreamFields& f) {
    auto stream = makeStream();
    Steinberg::IBStreamer s(stream, kLittleEndian);
    REQUIRE(s.writeInt32(f.version));
    REQUIRE(s.writeFloat(f.gain));
    REQUIRE(s.writeInt32(f.voiceMode));
    REQUIRE(s.writeInt32(f.polyphony));
    REQUIRE(s.writeFloat(f.ceilingDb));
    for (float m : f.macros) {
        REQUIRE(s.writeFloat(m));
    }
    REQUIRE(std::cmp_equal(stream->getSize(), ::Profundum::kStateV1Bytes));
    rewindStream(*stream);
    return stream;
}

// The snapshot a processor holds after loading @p f in full, assuming every
// field of @p f is already valid (in range, finite, voice mode 0 / 1).
Snapshot loadedSnapshot(const StreamFields& f) {
    Snapshot s;
    s.gainBits = bitsOf(f.gain);
    s.voiceMode = static_cast<int>(f.voiceMode);
    s.polyphony = static_cast<int>(f.polyphony);
    s.ceilingBits = bitsOf(f.ceilingDb);
    for (std::size_t i = 0; i < 4; ++i) {
        s.macroBits[i] = bitsOf(f.macros[i]);
    }
    return s;
}

// Known non-default values through the FR-043 path (a parameter-only process()):
// gain 0.25 normalized (-45 dB), Poly, polyphony 5/7 (-> 6), ceiling 0.75 (-> -3 dB),
// macros {0.1, 0.2, 0.8, 0.9}.
void setKnown(ProcessorFixture& fx) {
    Krate::Test::ParameterChanges pc;
    pc.addChange(::Profundum::kOutputGainId, 0.25);
    pc.addChange(::Profundum::kVoiceModeId, 1.0);
    pc.addChange(::Profundum::kPolyphonyId, 5.0 / 7.0);
    pc.addChange(::Profundum::kOutputCeilingId, 0.75);
    constexpr std::array<double, 4> kMacroValues{0.1, 0.2, 0.8, 0.9};
    for (std::size_t i = 0; i < 4; ++i) {
        pc.addChange(kMacroIds[i], kMacroValues[i]);
    }
    REQUIRE(fx.processNoOutputs(&pc) == Steinberg::kResultOk);

    const auto& g = fx.proc->globalParamsForTest();
    const auto& m = fx.proc->macroParamsForTest();
    REQUIRE(bitsOf(g.outputGain.load()) == bitsOf(::Profundum::outputGainFromNormalized(0.25)));
    REQUIRE(g.voiceMode.load() == 1);
    REQUIRE(g.polyphony.load() == 6);
    REQUIRE(g.outputCeilingDb.load() == -3.0f);
    REQUIRE(m.weight.load() == 0.1f);
    REQUIRE(m.darkBright.load() == 0.2f);
    REQUIRE(m.tightLoose.load() == 0.8f);
    REQUIRE(m.cleanViolent.load() == 0.9f);
}


// ---- Controller mirror (T012, SC-011.6; plan 4.3 SC-011 steps 8 - 9) ----

// The eight registered IDs, in stream order (globals, then macros).
constexpr std::array<ParamID, 8> kAllIds{
    ::Profundum::kOutputGainId,      ::Profundum::kVoiceModeId,
    ::Profundum::kPolyphonyId,       ::Profundum::kOutputCeilingId,
    ::Profundum::kMacroWeightId,     ::Profundum::kMacroDarkBrightId,
    ::Profundum::kMacroTightLooseId, ::Profundum::kMacroCleanViolentId,
};

using Normalized = std::array<double, 8>;

// Heap Controller (FR-064), initialised with a null context, terminated on scope exit.
struct MirrorController {
    Steinberg::IPtr<::Profundum::Controller> ctl =
        Steinberg::owned(new ::Profundum::Controller());

    MirrorController() { REQUIRE(ctl->initialize(nullptr) == Steinberg::kResultOk); }
    ~MirrorController() { ctl->terminate(); }
    MirrorController(const MirrorController&) = delete;
    MirrorController& operator=(const MirrorController&) = delete;
    MirrorController(MirrorController&&) = delete;
    MirrorController& operator=(MirrorController&&) = delete;

    [[nodiscard]] Normalized read() const {
        Normalized v{};
        for (std::size_t i = 0; i < kAllIds.size(); ++i) {
            v[i] = ctl->getParamNormalized(kAllIds[i]);
        }
        return v;
    }

    Steinberg::tresult load(Steinberg::MemoryStream& s) const {
        rewindStream(s);
        return ctl->setComponentState(&s);
    }
};

// The fields of the ByteIdentical36 stream (setKnown's values as the processor stores them).
StreamFields knownFields() {
    StreamFields f;
    f.gain = ::Profundum::outputGainFromNormalized(0.25);
    f.voiceMode = 1;
    f.polyphony = 6;
    f.ceilingDb = -3.0f;
    f.macros = {0.1f, 0.2f, 0.8f, 0.9f};
    return f;
}

// The normalized values a VALID stream maps to (plan 2.3 inverse mappings).
Normalized normalizedOf(const StreamFields& f) {
    Normalized v{};
    v[0] = ::Profundum::outputGainToNormalized(f.gain);
    v[1] = static_cast<double>(f.voiceMode);
    v[2] = static_cast<double>(f.polyphony - 1) / 7.0;
    v[3] = ::Profundum::ceilingToNormalized(f.ceilingDb);
    for (std::size_t i = 0; i < 4; ++i) {
        v[4 + i] = static_cast<double>(f.macros[i]);
    }
    return v;
}

void checkClose(const Normalized& actual, const Normalized& expected, double tol) {
    for (std::size_t i = 0; i < actual.size(); ++i) {
        CAPTURE(i, actual[i], expected[i]);
        CHECK(std::fabs(actual[i] - expected[i]) <= tol);
    }
}

}  // namespace

TEST_CASE("Profundum_StateRoundTrip", "[profundum][state]") {
    using namespace ::Profundum;

    SECTION("ByteIdentical36") {
        ProcessorFixture a;
        setKnown(a);
        auto first = makeStream();
        REQUIRE(a.proc->getState(first) == Steinberg::kResultOk);
        REQUIRE(std::cmp_equal(first->getSize(), kStateV1Bytes));

        ProcessorFixture b;
        rewindStream(*first);
        REQUIRE(b.proc->setState(first) == Steinberg::kResultOk);
        CHECK(snapshot(*b.proc) == snapshot(*a.proc));

        auto second = makeStream();
        REQUIRE(b.proc->getState(second) == Steinberg::kResultOk);
        REQUIRE(std::cmp_equal(second->getSize(), kStateV1Bytes));
        REQUIRE(std::memcmp(first->getData(), second->getData(), kStateV1Bytes) == 0);
    }

    SECTION("DefaultDecode") {
        ProcessorFixture fx;
        auto stream = makeStream();
        REQUIRE(fx.proc->getState(stream) == Steinberg::kResultOk);
        REQUIRE(std::cmp_equal(stream->getSize(), kStateV1Bytes));
        rewindStream(*stream);

        Steinberg::IBStreamer s(stream, kLittleEndian);
        int32 version = 0;
        REQUIRE(s.readInt32(version));
        CHECK(version == 1);
        float gain = 0.0f;
        REQUIRE(s.readFloat(gain));
        CHECK(gain == 1.0f);
        int32 mode = -7;
        REQUIRE(s.readInt32(mode));
        CHECK(mode == 0);
        int32 poly = 0;
        REQUIRE(s.readInt32(poly));
        CHECK(poly == 4);
        float ceilingDb = 0.0f;
        REQUIRE(s.readFloat(ceilingDb));
        CHECK(ceilingDb == -1.0f);
        for (int i = 0; i < 4; ++i) {
            float macro = 0.0f;
            REQUIRE(s.readFloat(macro));
            CAPTURE(i);
            CHECK(macro == 0.5f);
        }
        float extra = 0.0f;
        CHECK_FALSE(s.readFloat(extra));  // exactly 36 bytes
    }

    SECTION("FutureVersion") {
        ProcessorFixture fx;
        setKnown(fx);
        const Snapshot before = snapshot(*fx.proc);

        StreamFields f;  // valid fields, all distinct from the known values
        f.version = kCurrentStateVersion + 1;
        auto stream = writeFields(f);
        REQUIRE(fx.proc->setState(stream) == Steinberg::kResultFalse);
        CHECK(snapshot(*fx.proc) == before);
    }

    SECTION("Truncated") {
        ProcessorFixture fx;
        setKnown(fx);
        const Snapshot before = snapshot(*fx.proc);

        const StreamFields f;  // valid, every field distinct from the known values
        auto full = writeFields(f);
        constexpr std::size_t kCut = 20;  // after the global block (plan 3.4)
        auto cut = makeStream();
        Steinberg::int32 written = 0;
        REQUIRE(cut->write(full->getData(), static_cast<Steinberg::int32>(kCut), &written) ==
                Steinberg::kResultOk);
        REQUIRE(std::cmp_equal(written, kCut));
        rewindStream(*cut);

        REQUIRE(fx.proc->setState(cut) == Steinberg::kResultOk);

        Snapshot expected = loadedSnapshot(f);
        expected.macroBits = before.macroBits;  // the macros are unchanged
        CHECK(snapshot(*fx.proc) == expected);
    }

    SECTION("Hostile") {
        // Each case: start from the valid base fields, corrupt one, load it on a
        // processor holding the known values, and compare against the expected
        // snapshot (the base fields, with the corrupted field's rule applied).
        enum class Keep : std::uint8_t { None, Gain, VoiceMode, Ceiling, Macro };
        struct Case {
            const char* name;
            StreamFields fields;
            Keep keep;       // the field whose known value must survive
            int macroIndex;  // for Keep::Macro
            // The sanitised value of the corrupted field (when keep == None).
            int expectPoly;
            float expectGain;
            float expectCeiling;
            float expectMacro;
            int expectMacroIndex;
        };

        const StreamFields base;
        std::vector<Case> cases;
        const auto add = [&](const char* name, auto mutate, Keep keep, int macroIndex,
                             int poly, float gain, float ceiling, float macro, int mIdx) {
            Case c{.name = name, .fields = base, .keep = keep, .macroIndex = macroIndex, .expectPoly = poly,
                   .expectGain = gain, .expectCeiling = ceiling, .expectMacro = macro, .expectMacroIndex = mIdx};
            mutate(c.fields);
            cases.push_back(c);
        };
        const int bp = static_cast<int>(base.polyphony);
        const float bg = base.gain;
        const float bc = base.ceilingDb;

        add("polyphony 99 -> 8", [](StreamFields& f) { f.polyphony = 99; }, Keep::None, -1, 8, bg, bc, 0.0f, -1);
        add("polyphony 0 -> 1", [](StreamFields& f) { f.polyphony = 0; }, Keep::None, -1, 1, bg, bc, 0.0f, -1);
        add("ceiling +20 -> 0", [](StreamFields& f) { f.ceilingDb = 20.0f; }, Keep::None, -1, bp, bg, 0.0f, 0.0f, -1);
        add("ceiling -50 -> -12", [](StreamFields& f) { f.ceilingDb = -50.0f; }, Keep::None, -1, bp, bg, -12.0f, 0.0f, -1);
        add("gain 4 -> 1", [](StreamFields& f) { f.gain = 4.0f; }, Keep::None, -1, bp, 1.0f, bc, 0.0f, -1);
        add("gain -1 -> 0", [](StreamFields& f) { f.gain = -1.0f; }, Keep::None, -1, bp, 0.0f, bc, 0.0f, -1);
        add("macro 5 -> 1", [](StreamFields& f) { f.macros[2] = 5.0f; }, Keep::None, -1, bp, bg, bc, 1.0f, 2);
        add("voiceMode 7 kept", [](StreamFields& f) { f.voiceMode = 7; }, Keep::VoiceMode, -1, bp, bg, bc, 0.0f, -1);
        add("voiceMode -1 kept", [](StreamFields& f) { f.voiceMode = -1; }, Keep::VoiceMode, -1, bp, bg, bc, 0.0f, -1);

        const float nan = makeNaN();
        const float inf = makePosInf();
        for (int k = 0; k < 2; ++k) {
            const float bad = (k == 0) ? nan : inf;
            const bool isNan = (k == 0);
            add(isNan ? "gain NaN kept" : "gain +Inf kept",
                [bad](StreamFields& f) { f.gain = bad; }, Keep::Gain, -1, bp, bg, bc, 0.0f, -1);
            add(isNan ? "ceiling NaN kept" : "ceiling +Inf kept",
                [bad](StreamFields& f) { f.ceilingDb = bad; }, Keep::Ceiling, -1, bp, bg, bc, 0.0f, -1);
            for (int i = 0; i < 4; ++i) {
                add(isNan ? "macro NaN kept" : "macro +Inf kept",
                    [bad, i](StreamFields& f) { f.macros[static_cast<std::size_t>(i)] = bad; },
                    Keep::Macro, i, bp, bg, bc, 0.0f, -1);
            }
        }
        REQUIRE(cases.size() == 9u + 2u * 6u);

        for (const Case& c : cases) {
            CAPTURE(c.name, c.macroIndex);
            ProcessorFixture fx;
            setKnown(fx);
            const Snapshot known = snapshot(*fx.proc);

            auto stream = writeFields(c.fields);
            REQUIRE(fx.proc->setState(stream) == Steinberg::kResultOk);

            Snapshot expected = loadedSnapshot(base);
            expected.polyphony = c.expectPoly;
            expected.gainBits = bitsOf(c.expectGain);
            expected.ceilingBits = bitsOf(c.expectCeiling);
            if (c.expectMacroIndex >= 0) {
                expected.macroBits[static_cast<std::size_t>(c.expectMacroIndex)] =
                    bitsOf(c.expectMacro);
            }
            switch (c.keep) {
                case Keep::Gain: expected.gainBits = known.gainBits; break;
                case Keep::VoiceMode: expected.voiceMode = known.voiceMode; break;
                case Keep::Ceiling: expected.ceilingBits = known.ceilingBits; break;
                case Keep::Macro:
                    expected.macroBits[static_cast<std::size_t>(c.macroIndex)] =
                        known.macroBits[static_cast<std::size_t>(c.macroIndex)];
                    break;
                case Keep::None: break;
            }
            CHECK(snapshot(*fx.proc) == expected);

            // Every stored value is finite and inside its legal range.
            const auto& g = fx.proc->globalParamsForTest();
            CHECK(isFiniteBits(g.outputGain.load()));
            CHECK(g.outputGain.load() >= 0.0f);
            CHECK(g.outputGain.load() <= 1.0f);
            CHECK((g.voiceMode.load() == 0 || g.voiceMode.load() == 1));
            CHECK(g.polyphony.load() >= 1);
            CHECK(g.polyphony.load() <= 8);
            CHECK(isFiniteBits(g.outputCeilingDb.load()));
            CHECK(g.outputCeilingDb.load() >= -12.0f);
            CHECK(g.outputCeilingDb.load() <= 0.0f);
            for (int i = 0; i < 4; ++i) {
                const float m = macroField(fx.proc->macroParamsForTest(), i).load();
                CHECK(isFiniteBits(m));
                CHECK(m >= 0.0f);
                CHECK(m <= 1.0f);
            }
        }
    }

    SECTION("ControllerMirror") {
        // The ByteIdentical36 stream, byte for byte equal to writeFields(knownFields()).
        ProcessorFixture a;
        setKnown(a);
        auto procStream = makeStream();
        REQUIRE(a.proc->getState(procStream) == Steinberg::kResultOk);
        REQUIRE(std::cmp_equal(procStream->getSize(), kStateV1Bytes));
        const auto known = writeFields(knownFields());
        REQUIRE(std::memcmp(procStream->getData(), known->getData(), kStateV1Bytes) == 0);

        MirrorController c;
        REQUIRE(c.load(*procStream) == Steinberg::kResultOk);
        const Normalized v = c.read();
        CHECK(std::fabs(v[0] - 0.25) <= 1e-5);
        CHECK(std::fabs(v[1] - 1.0) <= 1e-6);
        CHECK(std::fabs(v[2] - 5.0 / 7.0) <= 1e-6);
        CHECK(std::fabs(v[3] - 0.75) <= 1e-6);
        constexpr std::array<double, 4> kMacros{0.1, 0.2, 0.8, 0.9};
        for (std::size_t i = 0; i < 4; ++i) {
            CAPTURE(i);
            CHECK(std::fabs(v[4 + i] - kMacros[i]) <= 1e-6);
        }

        // Inverse-taper points, each through a stream that changes only the gain.
        struct GainPoint {
            float gain;
            double expected;
            double tol;
        };
        const std::array<GainPoint, 3> points{{
            {.gain = 1.0f, .expected = 1.0, .tol = 1e-12},
            {.gain = 0.0f, .expected = 0.0, .tol = 1e-12},
            {.gain = static_cast<float>(std::pow(10.0, -30.0 / 20.0)), .expected = 0.5, .tol = 1e-5},
        }};
        for (const GainPoint& p : points) {
            CAPTURE(p.gain);
            StreamFields f = knownFields();
            f.gain = p.gain;
            auto stream = writeFields(f);
            REQUIRE(c.load(*stream) == Steinberg::kResultOk);
            Normalized got = c.read();
            CHECK(std::fabs(got[0] - p.expected) <= p.tol);
            got[0] = v[0];
            checkClose(got, v, 1e-12);  // the other seven did not move
        }

        // Version 2 -> kResultFalse and no parameter moves.
        const Normalized before = c.read();
        StreamFields future;  // valid fields, all distinct from the known values
        future.version = kCurrentStateVersion + 1;
        auto stream = writeFields(future);
        REQUIRE(c.load(*stream) == Steinberg::kResultFalse);
        CHECK(c.read() == before);
    }

    SECTION("ControllerMirrorHostile") {
        // Each case: the Hostile base stream with one field corrupted, loaded on a
        // fresh controller that first loaded the ByteIdentical36 stream.
        struct CCase {
            const char* name;
            StreamFields fields;
            std::size_t index;  // into kAllIds: the corrupted field
            bool keep;          // true: that ID must keep its known value
            double exact;       // otherwise: its exact normalized value
        };

        const StreamFields base;
        std::vector<CCase> cases;
        const auto add = [&](const char* name, auto mutate, std::size_t index, bool keep,
                             double exact) {
            CCase c{.name = name, .fields = base, .index = index, .keep = keep, .exact = exact};
            mutate(c.fields);
            cases.push_back(c);
        };

        add("polyphony 99 -> 1.0", [](StreamFields& f) { f.polyphony = 99; }, 2, false, 1.0);
        add("polyphony 0 -> 0.0", [](StreamFields& f) { f.polyphony = 0; }, 2, false, 0.0);
        add("ceiling +20 -> 1.0", [](StreamFields& f) { f.ceilingDb = 20.0f; }, 3, false, 1.0);
        add("ceiling -50 -> 0.0", [](StreamFields& f) { f.ceilingDb = -50.0f; }, 3, false, 0.0);
        add("gain 4 -> 1.0", [](StreamFields& f) { f.gain = 4.0f; }, 0, false, 1.0);
        add("gain -1 -> 0.0", [](StreamFields& f) { f.gain = -1.0f; }, 0, false, 0.0);
        add("macro 5 -> 1.0", [](StreamFields& f) { f.macros[2] = 5.0f; }, 6, false, 1.0);
        add("voiceMode 7 kept", [](StreamFields& f) { f.voiceMode = 7; }, 1, true, 0.0);
        add("voiceMode -1 kept", [](StreamFields& f) { f.voiceMode = -1; }, 1, true, 0.0);

        const float nan = makeNaN();
        const float inf = makePosInf();
        for (int k = 0; k < 2; ++k) {
            const float bad = (k == 0) ? nan : inf;
            const bool isNan = (k == 0);
            add(isNan ? "gain NaN kept" : "gain +Inf kept",
                [bad](StreamFields& f) { f.gain = bad; }, 0, true, 0.0);
            add(isNan ? "ceiling NaN kept" : "ceiling +Inf kept",
                [bad](StreamFields& f) { f.ceilingDb = bad; }, 3, true, 0.0);
            for (int i = 0; i < 4; ++i) {
                add(isNan ? "macro NaN kept" : "macro +Inf kept",
                    [bad, i](StreamFields& f) { f.macros[static_cast<std::size_t>(i)] = bad; },
                    4 + static_cast<std::size_t>(i), true, 0.0);
            }
        }
        REQUIRE(cases.size() == 9u + 2u * 6u);

        const Normalized baseNorm = normalizedOf(base);
        for (const CCase& cc : cases) {
            CAPTURE(cc.name, cc.index);
            MirrorController c;
            auto knownStream = writeFields(knownFields());
            REQUIRE(c.load(*knownStream) == Steinberg::kResultOk);
            const Normalized known = c.read();

            auto stream = writeFields(cc.fields);
            REQUIRE(c.load(*stream) == Steinberg::kResultOk);
            const Normalized got = c.read();

            for (std::size_t i = 0; i < kAllIds.size(); ++i) {
                CAPTURE(i, got[i]);
                if (i == cc.index) {
                    if (cc.keep) {
                        CHECK(std::fabs(got[i] - known[i]) <= 1e-12);
                    } else {
                        CHECK(got[i] == cc.exact);
                    }
                } else {
                    CHECK(std::fabs(got[i] - baseNorm[i]) <= 1e-12);
                }
                // Every value read back is in [0, 1] (a NaN fails both).
                CHECK(got[i] >= 0.0);
                CHECK(got[i] <= 1.0);
            }
        }
    }

    SECTION("HostileGainKeepsCeiling") {  // SC-011.5 render arm (FR-024, FR-028, FR-045)
        StreamFields f;
        f.gain = 4.0f;  // stored as 1.0
        f.voiceMode = 1;
        f.polyphony = 8;
        f.ceilingDb = -1.0f;
        f.macros = {1.0f, 1.0f, 1.0f, 1.0f};

        ProcessorFixture fx;
        auto stream = writeFields(f);
        REQUIRE(fx.proc->setState(stream) == Steinberg::kResultOk);
        REQUIRE(fx.prepare(48000.0) == Steinberg::kResultOk);

        constexpr std::size_t kTotal = 96000;  // 2 s at 48 kHz
        constexpr std::size_t kBlock = 512;
        std::vector<ProcessorFixture::ScriptedEvent> script;
        for (const Steinberg::int16 pitch : kChord8) {
            ProcessorFixture::ScriptedEvent e;
            e.sample = 0;
            e.kind = ProcessorFixture::ScriptedEvent::Kind::NoteOn;
            e.pitch = pitch;
            e.velocity = 100.0f / 127.0f;
            script.push_back(e);
        }
        fx.renderScript(script, kTotal, kBlock);
        REQUIRE(fx.capturedL.size() == kTotal);
        REQUIRE(fx.capturedR.size() == kTotal);

        // The limiter's own expression (true_peak_limiter.h:136): `<=` needs no tolerance.
        const float ceilMinus1Db = std::pow(10.0f, -1.0f * 0.05f);
        const float peakL = peakOf(fx.capturedL);
        const float peakR = peakOf(fx.capturedR);
        const float storedGain = fx.proc->globalParamsForTest().outputGain.load();
        WARN("SC-011.5 HostileGainKeepsCeiling: peak L = " << peakL << ", R = " << peakR << " (bar " << ceilMinus1Db
                                                         << "); stored gain = " << storedGain);
        CHECK(peakL <= ceilMinus1Db);
        CHECK(peakR <= ceilMinus1Db);
        CHECK(storedGain == 1.0f);
    }
}
