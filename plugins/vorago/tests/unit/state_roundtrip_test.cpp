// ==============================================================================
// Vorago - state round-trip tests (SC-010)
// ==============================================================================
// T009: pack-level section "PackStreamsDirect" - saveGlobalParams /
// saveMacroParams byte layout, EOF-safe loaders, corrupt-stream clamps and the
// controller-sync inverses, exercised directly with no processor.
// T012: Processor::getState / setState (FR-045, FR-046; plan 2.5.10, layout 3.4):
// 60-byte byte round-trip, default-stream decode, future-version rejection,
// truncated-stream macro retention and null-stream rejection (SC-010.1-4).
// Phase 12 T042: the stream is now v2 (kStateV2Bytes, kCurrentStateVersion 2); the
// hand-built v1 decode arm lives in state_v2_test.cpp (SC-008 (2)).
// T016: "CorruptStreamConverges" - a +Inf gain / polyphony-99 stream loads
// clamped, and the next process() pushes polyphony exactly once (plan 2.3).
// T017: "ControllerLoadsComponentState" (SC-010.5) - Controller::setComponentState
// maps the processor stream back to normalized values; future/null streams rejected.
//
// Built with -fno-fast-math (tests/CMakeLists.txt): the +Inf payload below is
// produced from a bit pattern through a volatile uint32 + memcpy.
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "controller/controller.h"
#include "parameters/global_params.h"
#include "parameters/macro_params.h"
#include "plugin_ids.h"
#include "vorago_test_fixture.h"

#include <vst_param_changes.h>

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace {

Steinberg::IPtr<Steinberg::MemoryStream> makeStream() {
    return Steinberg::owned(new Steinberg::MemoryStream());
}

void rewindStream(Steinberg::MemoryStream& s) {
    REQUIRE(s.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) == Steinberg::kResultOk);
}

std::uint32_t bitsOf(float f) { return std::bit_cast<std::uint32_t>(f); }

// Little-endian decode, independent of the host byte order.
std::uint32_t leWordAt(const Steinberg::MemoryStream& s, std::size_t offset) {
    REQUIRE(offset + 4u <= static_cast<std::size_t>(s.getSize()));
    const auto* p = reinterpret_cast<const unsigned char*>(s.getData()) + offset;  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8u) |
           (static_cast<std::uint32_t>(p[2]) << 16u) | (static_cast<std::uint32_t>(p[3]) << 24u);
}

float leFloatAt(const Steinberg::MemoryStream& s, std::size_t offset) {
    return std::bit_cast<float>(leWordAt(s, offset));
}

Steinberg::int32 leInt32At(const Steinberg::MemoryStream& s, std::size_t offset) {
    return std::bit_cast<Steinberg::int32>(leWordAt(s, offset));
}

Steinberg::Vst::ParamID macroId(int i) {
    return static_cast<Steinberg::Vst::ParamID>(::Vorago::kMacroDarknessId) +
           static_cast<Steinberg::Vst::ParamID>(i);
}

// All fourteen parameters set to non-defaults through process() (FR-043 path).
// Gain 0.2 -> 0.4f linear, polyphony 0.4 -> 3 voices, macro i -> 0.05 + 0.07 i.
void setAllNonDefaultThroughProcess(VoragoTest::ProcessorFixture& fx, double gainNorm = 0.2,
                                    double polyNorm = 0.4, double macroBase = 0.05) {
    Krate::Test::ParameterChanges pc;
    pc.addChange(::Vorago::kMasterGainId, gainNorm);
    pc.addChange(::Vorago::kPolyphonyId, polyNorm);
    for (int i = 0; i < 12; ++i) {
        pc.addChange(macroId(i), macroBase + 0.07 * i);
    }
    REQUIRE(fx.processNoOutputs(&pc) == Steinberg::kResultOk);
}

struct ParamSnapshot {
    std::uint32_t gainBits = 0;
    int polyphony = 0;
    std::array<std::uint32_t, 12> macroBits{};

    bool operator==(const ParamSnapshot&) const = default;
};

ParamSnapshot snapshot(const ::Vorago::Processor& p) {
    ParamSnapshot out;
    out.gainBits = bitsOf(p.globalParamsForTest().masterGain.load());
    out.polyphony = p.globalParamsForTest().polyphony.load();
    for (int i = 0; i < 12; ++i) {
        out.macroBits[static_cast<std::size_t>(i)] =
            bitsOf(::Vorago::macroField(p.macroParamsForTest(), i).load());
    }
    return out;
}

}  // namespace

TEST_CASE("Vorago_StateRoundTrip", "[vorago][state]") {
    using Catch::Approx;
    using namespace ::Vorago;

    SECTION("PackStreamsDirect") {
        // ---- Byte round-trip of non-default values ---------------------------
        GlobalParams gOut;
        gOut.masterGain.store(1.37f);
        gOut.polyphony.store(2);
        MacroParams mOut;
        for (int i = 0; i < 12; ++i) {
            macroField(mOut, i).store(0.05f + 0.07f * static_cast<float>(i));
        }

        auto stream = makeStream();
        {
            Steinberg::IBStreamer w(stream, kLittleEndian);
            saveGlobalParams(gOut, w);
            saveMacroParams(mOut, w);
        }
        REQUIRE(stream->getSize() == 56);  // 8 (global) + 48 (macros)

        rewindStream(*stream);
        GlobalParams gIn;
        MacroParams mIn;
        {
            Steinberg::IBStreamer r(stream, kLittleEndian);
            REQUIRE(loadGlobalParams(gIn, r));
            REQUIRE(loadMacroParams(mIn, r));
        }
        REQUIRE(bitsOf(gIn.masterGain.load()) == bitsOf(gOut.masterGain.load()));
        REQUIRE(gIn.polyphony.load() == gOut.polyphony.load());
        for (int i = 0; i < 12; ++i) {
            REQUIRE(bitsOf(macroField(mIn, i).load()) == bitsOf(macroField(mOut, i).load()));
        }

        // ---- Corrupt global stream: +Inf gain and polyphony 99 ---------------
        {
            volatile std::uint32_t infBits = 0x7F800000u;
            const std::uint32_t infCopy = infBits;
            float inf = 0.0f;
            std::memcpy(&inf, &infCopy, sizeof(inf));

            auto bad = makeStream();
            {
                Steinberg::IBStreamer w(bad, kLittleEndian);
                w.writeFloat(inf);
                w.writeInt32(99);
            }
            rewindStream(*bad);
            GlobalParams g;
            Steinberg::IBStreamer r(bad, kLittleEndian);
            REQUIRE(loadGlobalParams(g, r));
            REQUIRE(bitsOf(g.masterGain.load()) == bitsOf(1.0f));  // non-finite rejected
            REQUIRE(g.polyphony.load() == 6);                      // clamped
        }

        // ---- Truncated global stream: only the 4-byte gain -------------------
        {
            auto shortStream = makeStream();
            {
                Steinberg::IBStreamer w(shortStream, kLittleEndian);
                w.writeFloat(0.8f);
            }
            REQUIRE(shortStream->getSize() == 4);
            rewindStream(*shortStream);
            GlobalParams g;
            g.polyphony.store(3);
            Steinberg::IBStreamer r(shortStream, kLittleEndian);
            REQUIRE_FALSE(loadGlobalParams(g, r));
            REQUIRE(g.polyphony.load() == 3);  // unread field unchanged
        }

        // ---- Truncated macro stream: three floats ----------------------------
        {
            auto shortStream = makeStream();
            {
                Steinberg::IBStreamer w(shortStream, kLittleEndian);
                w.writeFloat(0.11f);
                w.writeFloat(0.22f);
                w.writeFloat(0.33f);
            }
            rewindStream(*shortStream);
            MacroParams m;
            for (int i = 0; i < 12; ++i) {
                macroField(m, i).store(0.9f);
            }
            Steinberg::IBStreamer r(shortStream, kLittleEndian);
            REQUIRE_FALSE(loadMacroParams(m, r));
            REQUIRE(bitsOf(macroField(m, 0).load()) == bitsOf(0.11f));
            REQUIRE(bitsOf(macroField(m, 1).load()) == bitsOf(0.22f));
            REQUIRE(bitsOf(macroField(m, 2).load()) == bitsOf(0.33f));
            for (int i = 3; i < 12; ++i) {
                REQUIRE(bitsOf(macroField(m, i).load()) == bitsOf(0.9f));
            }
        }

        // ---- Controller sync inverses ----------------------------------------
        {
            rewindStream(*stream);
            std::vector<std::pair<Steinberg::Vst::ParamID, double>> received;
            received.reserve(14);
            auto capture = [&received](Steinberg::Vst::ParamID id, double v) {
                received.emplace_back(id, v);
            };
            Steinberg::IBStreamer r(stream, kLittleEndian);
            loadGlobalParamsToController(r, capture);
            loadMacroParamsToController(r, capture);

            REQUIRE(received.size() == 14);
            REQUIRE(received[0].first == static_cast<Steinberg::Vst::ParamID>(kMasterGainId));
            REQUIRE(received[0].second == Approx(1.37 / 2.0).margin(1e-6));
            REQUIRE(received[1].first == static_cast<Steinberg::Vst::ParamID>(kPolyphonyId));
            REQUIRE(received[1].second == Approx((2.0 - 1.0) / 5.0).margin(1e-6));
            for (int i = 0; i < 12; ++i) {
                const auto& [id, v] = received[static_cast<std::size_t>(i) + 2];
                REQUIRE(id == static_cast<Steinberg::Vst::ParamID>(kMacroDarknessId) +
                                  static_cast<Steinberg::Vst::ParamID>(i));
                REQUIRE(v == Approx(0.05 + 0.07 * i).margin(1e-6));
            }
        }
    }

    SECTION("ByteRoundTrip") {  // SC-010.1
        VoragoTest::ProcessorFixture src;
        setAllNonDefaultThroughProcess(src);
        // Non-vacuity: the source really holds non-defaults.
        REQUIRE(bitsOf(src.proc->globalParamsForTest().masterGain.load()) == bitsOf(0.4f));
        REQUIRE(src.proc->globalParamsForTest().polyphony.load() == 3);

        auto s1 = makeStream();
        REQUIRE(src.proc->getState(s1) == Steinberg::kResultOk);
        REQUIRE(s1->getSize() == static_cast<Steinberg::int64>(kStateV2Bytes));

        VoragoTest::ProcessorFixture dst;  // fresh processor
        rewindStream(*s1);
        REQUIRE(dst.proc->setState(s1) == Steinberg::kResultOk);
        auto s2 = makeStream();
        REQUIRE(dst.proc->getState(s2) == Steinberg::kResultOk);
        REQUIRE(s2->getSize() == static_cast<Steinberg::int64>(kStateV2Bytes));
        REQUIRE(std::memcmp(s1->getData(), s2->getData(), kStateV2Bytes) == 0);
        REQUIRE(snapshot(*dst.proc) == snapshot(*src.proc));
    }

    SECTION("DefaultStreamDecodes") {  // SC-010.2
        VoragoTest::ProcessorFixture fx;
        auto st = makeStream();
        REQUIRE(fx.proc->getState(st) == Steinberg::kResultOk);
        REQUIRE(st->getSize() == static_cast<Steinberg::int64>(kStateV2Bytes));
        REQUIRE(leInt32At(*st, 0) == kCurrentStateVersion);  // v1 prefix follows unchanged
        REQUIRE(bitsOf(leFloatAt(*st, 4)) == bitsOf(1.0f));
        REQUIRE(leInt32At(*st, 8) == 4);
        for (int i = 0; i < 12; ++i) {
            const std::size_t off = 12u + 4u * static_cast<std::size_t>(i);
            const float expected = (off == 28u) ? 0.5f : 0.0f;  // offset 28 == Gravity
            REQUIRE(bitsOf(leFloatAt(*st, off)) == bitsOf(expected));
        }
    }

    SECTION("FutureVersionRejected") {  // SC-010.3
        VoragoTest::ProcessorFixture fx;
        setAllNonDefaultThroughProcess(fx);
        const ParamSnapshot before = snapshot(*fx.proc);

        auto st = makeStream();
        {
            Steinberg::IBStreamer w(st, kLittleEndian);
            w.writeInt32(kCurrentStateVersion + 1);
            w.writeFloat(1.7f);
            w.writeInt32(6);
            for (int i = 0; i < 12; ++i) {
                w.writeFloat(0.9f);
            }
        }
        REQUIRE(st->getSize() == 60);
        rewindStream(*st);
        REQUIRE(fx.proc->setState(st) == Steinberg::kResultFalse);
        REQUIRE(snapshot(*fx.proc) == before);
    }

    SECTION("TruncatedStreamKeepsMacros") {  // SC-010.4
        // Source of the valid state: gain 0.4f, polyphony 3.
        VoragoTest::ProcessorFixture src;
        setAllNonDefaultThroughProcess(src, 0.2, 0.4, 0.05);
        auto full = makeStream();
        REQUIRE(src.proc->getState(full) == Steinberg::kResultOk);
        REQUIRE(full->getSize() == static_cast<Steinberg::int64>(kStateV2Bytes));

        // Target: macros at OTHER non-defaults, globals left at their defaults.
        VoragoTest::ProcessorFixture dst;
        {
            Krate::Test::ParameterChanges pc;
            for (int i = 0; i < 12; ++i) {
                pc.addChange(macroId(i), 0.9 - 0.03 * i);
            }
            REQUIRE(dst.processNoOutputs(&pc) == Steinberg::kResultOk);
        }
        const ParamSnapshot before = snapshot(*dst.proc);
        REQUIRE(bitsOf(dst.proc->globalParamsForTest().masterGain.load()) == bitsOf(1.0f));

        auto cut = makeStream();
        Steinberg::int32 written = 0;
        REQUIRE(cut->write(full->getData(), 12, &written) == Steinberg::kResultOk);
        REQUIRE(written == 12);
        REQUIRE(cut->getSize() == 12);
        rewindStream(*cut);

        REQUIRE(dst.proc->setState(cut) == Steinberg::kResultOk);
        REQUIRE(bitsOf(dst.proc->globalParamsForTest().masterGain.load()) == bitsOf(0.4f));
        REQUIRE(dst.proc->globalParamsForTest().polyphony.load() == 3);
        REQUIRE(snapshot(*dst.proc).macroBits == before.macroBits);
    }

    SECTION("NullStreamsRejected") {
        VoragoTest::ProcessorFixture fx;
        REQUIRE(fx.proc->setState(nullptr) == Steinberg::kResultFalse);
        REQUIRE(fx.proc->getState(nullptr) == Steinberg::kResultFalse);
    }

    SECTION("CorruptStreamConverges") {  // SC-010 corrupt arm, plan 2.3 convergence
        VoragoTest::ProcessorFixture fx;
        fx.prepare(48000.0, 2048);
        const std::uint32_t gainBefore = bitsOf(fx.proc->globalParamsForTest().masterGain.load());

        // +Inf built from its bit pattern through a volatile (fast-math immune).
        volatile std::uint32_t infBits = 0x7F800000u;
        const std::uint32_t infCopy = infBits;
        float inf = 0.0f;
        std::memcpy(&inf, &infCopy, sizeof(inf));

        auto st = makeStream();
        {
            Steinberg::IBStreamer w(st, kLittleEndian);
            w.writeInt32(1);    // valid version
            w.writeFloat(inf);  // corrupt gain
            w.writeInt32(99);   // corrupt polyphony
            for (int i = 0; i < 12; ++i) {
                w.writeFloat(0.1f + 0.05f * static_cast<float>(i));  // twelve valid macros
            }
        }
        REQUIRE(st->getSize() == 60);
        rewindStream(*st);

        REQUIRE(fx.proc->setState(st) == Steinberg::kResultOk);
        REQUIRE(fx.proc->globalParamsForTest().polyphony.load() == 6);
        REQUIRE(bitsOf(fx.proc->globalParamsForTest().masterGain.load()) == gainBefore);

        const std::uint32_t c0 = fx.proc->setPolyphonyCallCountForTest();
        fx.reserveCapture(std::size_t{2} * 512u);
        REQUIRE(fx.processBlock(512) == Steinberg::kResultOk);
        REQUIRE(fx.proc->setPolyphonyCallCountForTest() == c0 + 1u);  // pushed once
        REQUIRE(fx.processBlock(512) == Steinberg::kResultOk);
        REQUIRE(fx.proc->setPolyphonyCallCountForTest() == c0 + 1u);  // and converged
    }

    SECTION("ControllerLoadsComponentState") {  // SC-010.5
        // Stream 1 of ByteRoundTrip: all fourteen at non-defaults.
        VoragoTest::ProcessorFixture src;
        setAllNonDefaultThroughProcess(src);
        const float gain = src.proc->globalParamsForTest().masterGain.load();
        const int voices = src.proc->globalParamsForTest().polyphony.load();
        REQUIRE(bitsOf(gain) == bitsOf(0.4f));  // non-vacuity
        REQUIRE(voices == 3);

        auto s1 = makeStream();
        REQUIRE(src.proc->getState(s1) == Steinberg::kResultOk);
        REQUIRE(s1->getSize() == static_cast<Steinberg::int64>(kStateV2Bytes));
        rewindStream(*s1);

        auto controller = Steinberg::owned(new ::Vorago::Controller());
        REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);
        REQUIRE(controller->setComponentState(s1) == Steinberg::kResultOk);

        REQUIRE(controller->getParamNormalized(kMasterGainId) ==
                Approx(static_cast<double>(gain) / 2.0).margin(1e-6));
        REQUIRE(controller->getParamNormalized(kPolyphonyId) ==
                Approx((static_cast<double>(voices) - 1.0) / 5.0).margin(1e-6));
        for (int i = 0; i < 12; ++i) {
            const float stored = macroField(src.proc->macroParamsForTest(), i).load();
            INFO("macro " << i);
            REQUIRE(controller->getParamNormalized(macroId(i)) ==
                    Approx(static_cast<double>(stored)).margin(1e-6));
        }

        // Future version rejected.
        auto future = makeStream();
        {
            Steinberg::IBStreamer w(future, kLittleEndian);
            w.writeInt32(kCurrentStateVersion + 1);
            w.writeFloat(1.7f);
            w.writeInt32(6);
            for (int i = 0; i < 12; ++i) {
                w.writeFloat(0.9f);
            }
        }
        rewindStream(*future);
        REQUIRE(controller->setComponentState(future) == Steinberg::kResultFalse);

        // Null stream rejected.
        REQUIRE(controller->setComponentState(nullptr) == Steinberg::kResultFalse);

        REQUIRE(controller->terminate() == Steinberg::kResultOk);
    }
}
