// ==============================================================================
// Profundum - state v3 and endpoint names (Phase 12; FR-047, FR-048, FR-096; SC-027)
// ==============================================================================
// Profundum_EndpointNames (T014; T019, T023 and T028 add sections):
//   Sanitise         plan 4.5 sanitiser: control bytes, invalid UTF-8, the 31-byte code-point cut; defaults A / B
//   BlockRoundTrip   the 32-byte name block: write, read back, and the three rejections
//   DescriptorTable  SC-027 (d): the six worked cases of design/advanced-page.md 7.3, determinism, one-field change
// T019 (state v3, kMsgEndpointName, setEndpointName; plan 4.5, 6.2 SC-027):
//   CurrentVersionIsThree        version 3, 574 bytes, the default "A" / "B" blocks at [510, 542) / [542, 574)
//   ApiCommitSendsOneMessage     SC-027 (a) API arm: one kMsgEndpointName, setDirty, the processor holds the name
//   NamesRoundTrip               SC-027 (a): a 31-byte ASCII name and a code-point-cut UTF-8 name, both sides
//   OlderStreamsLoad             SC-027 (b): a genuine v2 stream (v2StreamOf) and a v1 stream load; names A / B
//   V3PrefixEqualsV2Body         SC-027 (c): bytes [4, 510) of the v3 save equal the v2 reference; fixed offsets
//   DescriptorNotStored          SC-027 (e): a descriptor-relevant field moves only settings bytes
//   ShortNameBlockKeepsPriorName a v3 stream cut at 530 keeps the prior names, the rest loads
//   NotifyIsSanitised            R-13: the processor sanitises a notified name; an endpoint index of 2 is ignored
// T028 (built tree, HeadlessEditor on a ConnectedPair):
//   TextEditCommit               SC-027 (a) UI arm: a commit in the 9211 CTextEdit sends one kMsgEndpointName
//                                (endpoint 1) and the 9221 caption reads the T023 caption of B
// Descriptor words are compared as strings, never as float scores.
// ==============================================================================

#include "profundum_test_fixture.h"
#include "ui_tree_helpers.h"  // HeadlessEditor, collectViewsOfType (T028)

#include "parameters/endpoint_names.h"
#include "ui/endpoint_descriptor.h"
#include "ui/panel_sub_controller.h"  // kNameBTag, kCaptionBTag (T028)

#include <bass_morph_state_helpers.h>   // dsp/tests/unit/systems (read-only): BassMorphTest::showcasePair

#include <krate/dsp/systems/bass_morph_engine.h>
#include <krate/dsp/systems/morph_state.h>

#include <base/source/fstreamer.h>
#include <base/source/updatehandler.h>
#include <pluginterfaces/vst/ivstattributes.h>
#include <public.sdk/source/common/memorystream.h>
#include <public.sdk/source/vst/hosting/hostclasses.h>
#include <vstgui/lib/controls/ctextedit.h>
#include <vstgui/lib/controls/ctextlabel.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace ProfundumPluginTest;

namespace {

using Endpoint = Krate::DSP::BassMorphEngine::Endpoint;
using Krate::DSP::MorphState;

[[nodiscard]] std::string bytesOf(std::initializer_list<unsigned char> list) {
    std::string s;
    for (const unsigned char c : list) {
        s.push_back(static_cast<char>(c));
    }
    return s;
}

[[nodiscard]] std::string wordsOf(const MorphState& s) {
    const auto w = ::Profundum::UI::describeEndpoint(s);
    return std::string(w[0]) + " " + w[1] + " " + w[2];
}

/// A 32-byte name block with the given length byte and payload (zero-padded).
[[nodiscard]] std::array<std::byte, ::Profundum::kEndpointNameBlock> blockOf(std::uint8_t length,
                                                                            std::string_view payload) {
    std::array<std::byte, ::Profundum::kEndpointNameBlock> b{};
    b[0] = static_cast<std::byte>(length);
    for (std::size_t k = 0; k < payload.size() && k + 1u < b.size(); ++k) {
        b[k + 1u] = static_cast<std::byte>(static_cast<unsigned char>(payload[k]));
    }
    return b;
}

[[nodiscard]] std::string repeated(std::string_view unit, int count) {
    std::string s;
    for (int k = 0; k < count; ++k) {
        s += unit;
    }
    return s;
}

// ---------------------------------------------------------------------------
// T019 helpers (state v3, plan 4.5)
// ---------------------------------------------------------------------------

constexpr double kFs = 48000.0;
constexpr std::size_t kNameBlockA = ::Profundum::kStateNamesOffset;
constexpr std::size_t kNameBlockB = ::Profundum::kStateNamesOffset + ::Profundum::kEndpointNameBlockBytes;

/// A kMsgEndpointName message as the controller sends it: int "endpoint", binary "name" (the raw bytes as given).
[[nodiscard]] Steinberg::IPtr<Steinberg::Vst::HostMessage> makeNameMessage(Steinberg::int64 endpoint,
                                                                           std::string_view bytes) {
    auto msg = Steinberg::owned(new Steinberg::Vst::HostMessage());
    msg->setMessageID(::Profundum::kMsgEndpointName);
    Steinberg::Vst::IAttributeList* attrs = msg->getAttributes();
    REQUIRE(attrs != nullptr);
    REQUIRE(attrs->setInt("endpoint", endpoint) == Steinberg::kResultOk);
    REQUIRE(attrs->setBinary("name", bytes.data(), static_cast<Steinberg::uint32>(bytes.size())) ==
            Steinberg::kResultOk);
    return msg;
}

/// Names endpoint @p e of @p proc through its notify (the controller's commit path).
void notifyName(::Profundum::Processor& proc, Endpoint e, std::string_view name) {
    REQUIRE(proc.notify(makeNameMessage(static_cast<Steinberg::int64>(e), name)) == Steinberg::kResultOk);
}

/// True when the 32-byte name block at @p at of @p s holds exactly @p name: length byte, payload, zero padding.
[[nodiscard]] bool nameBlockIs(const StateBytes& s, std::size_t at, std::string_view name) {
    REQUIRE(s.size() >= at + ::Profundum::kEndpointNameBlockBytes);
    if (std::to_integer<std::size_t>(s[at]) != name.size()) {
        UNSCOPED_INFO("name block at " << at << ": length byte " << std::to_integer<int>(s[at]) << ", expected "
                                       << name.size());
        return false;
    }
    for (std::size_t k = 0; k < ::Profundum::kEndpointNameMaxBytes; ++k) {
        const auto want = k < name.size() ? static_cast<unsigned char>(name[k]) : static_cast<unsigned char>(0u);
        if (std::to_integer<unsigned char>(s[at + 1u + k]) != want) {
            UNSCOPED_INFO("name block at " << at << ": byte " << (k + 1u) << " differs");
            return false;
        }
    }
    return true;
}

/// setComponentState of @p bytes (all of them) on @p ctl.
Steinberg::tresult loadControllerBytes(::Profundum::Controller& ctl, std::span<const std::byte> bytes) {
    auto stream = memoryStreamOf(bytes);
    return ctl.setComponentState(stream);
}

/// The endpoint serialised at @p offset of a state stream.
[[nodiscard]] MorphState endpointAt(const StateBytes& b, std::size_t offset) {
    REQUIRE(b.size() >= offset + Krate::DSP::kMorphStateBytes);
    MorphState s{};
    REQUIRE(Krate::DSP::deserializeMorphState(b.data() + offset, Krate::DSP::kMorphStateBytes, s));
    return s;
}

[[nodiscard]] float f32At(const StateBytes& b, std::size_t at) { return std::bit_cast<float>(readU32(b, at)); }

[[nodiscard]] bool sameMorph(const MorphState& x, const MorphState& y) {
    return Krate::DSP::detail::morph_state_io::leafBitsEqual(x, y);
}

/// The v2 expectations of a v2 stream built from @p f, @p a, @p b, on a processor that loaded it (FR-044 keep):
/// globals, macros and both slots as stored, and bytes [4, 510) of its save equal to @p ref (the note, morph-control
/// and seed fields at their pre-phase offsets, and the pending runtime blob, plan 4.5.2 precedence 1).
void requireV2Restored(::Profundum::Processor& proc, const StateBytes& ref, const V2Fields& f, const MorphState& a,
                       const MorphState& b) {
    REQUIRE(ref.size() == ::Profundum::kStateV2Bytes);
    const auto& gp = proc.globalParamsForTest();
    CHECK(gp.outputGain.load() == f.globals.gain);
    CHECK(gp.voiceMode.load() == f.globals.voiceMode);
    CHECK(gp.polyphony.load() == f.globals.polyphony);
    CHECK(gp.outputCeilingDb.load() == f.globals.ceilingDb);
    const auto& mp = proc.macroParamsForTest();
    CHECK(mp.weight.load() == f.macros[0]);
    CHECK(mp.darkBright.load() == f.macros[1]);
    CHECK(mp.tightLoose.load() == f.macros[2]);
    CHECK(mp.cleanViolent.load() == f.macros[3]);
    CHECK(slotsOf(proc, Endpoint::A) == ::Profundum::leafBitsOf(a));
    CHECK(slotsOf(proc, Endpoint::B) == ::Profundum::leafBitsOf(b));
    const StateBytes out = writeV2Stream(proc);
    REQUIRE(out.size() == ::Profundum::kStateV3Bytes);
    CHECK(std::memcmp(out.data() + 4, ref.data() + 4, ::Profundum::kStateV2Bytes - 4u) == 0);
}

/// The v2 expectations on the controller side (FR-045): both mirrors, the Edit Target, the position and the macros.
void requireV2Displayed(::Profundum::Controller& ctl, const V2Fields& f, const MorphState& a, const MorphState& b) {
    CHECK(sameMorph(ctl.mirrorForTest(Endpoint::A), a));
    CHECK(sameMorph(ctl.mirrorForTest(Endpoint::B), b));
    CHECK(ctl.getParamNormalized(::Profundum::kMorphEditTargetId) == static_cast<double>(f.editTarget));
    CHECK(std::fabs(ctl.getParamNormalized(::Profundum::kMorphPositionId) - static_cast<double>(f.position)) <= 1e-9);
    for (std::size_t i = 0; i < f.macros.size(); ++i) {
        CAPTURE(i);
        CHECK(ctl.getParamNormalized(::Profundum::kMacroWeightId + static_cast<Steinberg::Vst::ParamID>(i)) ==
              static_cast<double>(f.macros[i]));
    }
}

}  // namespace

TEST_CASE("Profundum_EndpointNames", "[profundum][ui][morph][state]") {
    SECTION("Sanitise") {
        using ::Profundum::sanitizeEndpointName;

        const auto plain = sanitizeEndpointName("DEEP PRESSURE");
        CHECK(plain.length == 13u);
        CHECK(plain.view() == "DEEP PRESSURE");

        // Control bytes 0x01, 0x1F and 0x7F are removed; the printable bytes around them stay in order.
        const auto noControls = sanitizeEndpointName(bytesOf({'A', 0x01, 'B', 0x1F, 'C', 0x7F, 'D'}));
        CHECK(noControls.view() == "ABCD");
        CHECK(noControls.length == 4u);
        CHECK(sanitizeEndpointName(bytesOf({0x01, 0x1F, 0x7F})).length == 0u);

        // Invalid UTF-8: the lead 0xC3 is not followed by a continuation byte, so it is dropped.
        CHECK(sanitizeEndpointName(bytesOf({'X', 0xC3, '(', 'Y'})).view() == "X(Y");
        CHECK(sanitizeEndpointName(bytesOf({0xC3, '('})).view() == "(");
        // A stray continuation, overlong C0 / E0 forms, a surrogate and a code point above U+10FFFF are dropped too.
        CHECK(sanitizeEndpointName(bytesOf({0x80, 'a'})).view() == "a");
        CHECK(sanitizeEndpointName(bytesOf({0xC0, 0x80, 'b'})).view() == "b");
        CHECK(sanitizeEndpointName(bytesOf({0xE0, 0x80, 0x80, 'c'})).view() == "c");
        CHECK(sanitizeEndpointName(bytesOf({0xED, 0xA0, 0x80, 'd'})).view() == "d");
        CHECK(sanitizeEndpointName(bytesOf({0xF4, 0x90, 0x80, 0x80, 'e'})).view() == "e");
        // A valid two-byte code point survives (e-acute, C3 A9). Step 1 runs before step 2, so a control byte
        // inside it is removed and the sequence it split is still read as one code point.
        const std::string eAcute = bytesOf({0xC3, 0xA9});
        CHECK(sanitizeEndpointName(eAcute).view() == eAcute);
        CHECK(sanitizeEndpointName(bytesOf({0xC3, 0x01, 0xA9})).view() == eAcute);

        // 40 ASCII bytes -> 31.
        const auto cut = sanitizeEndpointName(std::string(40, 'x'));
        CHECK(cut.length == 31u);
        CHECK(cut.view() == std::string(31, 'x'));

        // Eleven 3-byte code points (EUR sign, 33 bytes) -> 10 code points, 30 bytes: cut at a code-point boundary.
        const std::string euros = repeated(bytesOf({0xE2, 0x82, 0xAC}), 11);
        REQUIRE(euros.size() == 33u);
        const auto euroCut = sanitizeEndpointName(euros);
        CHECK(euroCut.length == 30u);
        CHECK(euroCut.view() == std::string_view(euros).substr(0, 30));

        CHECK(sanitizeEndpointName("").length == 0u);

        CHECK(::Profundum::defaultEndpointName(Endpoint::A).view() == "A");
        CHECK(::Profundum::defaultEndpointName(Endpoint::B).view() == "B");
    }

    SECTION("BlockRoundTrip") {
        const auto name = ::Profundum::sanitizeEndpointName("DEEP PRESSURE");

        auto stream = Steinberg::owned(new Steinberg::MemoryStream());
        {
            Steinberg::IBStreamer writer(stream, kLittleEndian);
            CHECK(::Profundum::writeEndpointName(writer, name));
        }
        REQUIRE(stream->getSize() == static_cast<Steinberg::TSize>(::Profundum::kEndpointNameBlock));
        const auto* raw = reinterpret_cast<const unsigned char*>(stream->getData());
        CHECK(raw[0] == 13u);
        CHECK(std::string_view(reinterpret_cast<const char*>(raw + 1), 13u) == "DEEP PRESSURE");
        for (std::size_t k = 14u; k < ::Profundum::kEndpointNameBlock; ++k) {
            INFO("padding byte " << k);
            CHECK(raw[k] == 0u);
        }

        REQUIRE(stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) == Steinberg::kResultOk);
        {
            Steinberg::IBStreamer reader(stream, kLittleEndian);
            ::Profundum::EndpointName back{};
            CHECK(::Profundum::readEndpointName(reader, back));
            CHECK(back.length == 13u);
            CHECK(back.view() == "DEEP PRESSURE");
        }

        // A multi-byte name (ten EUR signs, 30 bytes) round-trips too.
        {
            const std::string euros = repeated(bytesOf({0xE2, 0x82, 0xAC}), 10);
            const auto euroName = ::Profundum::sanitizeEndpointName(euros);
            auto s2 = Steinberg::owned(new Steinberg::MemoryStream());
            Steinberg::IBStreamer w2(s2, kLittleEndian);
            CHECK(::Profundum::writeEndpointName(w2, euroName));
            REQUIRE(s2->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) == Steinberg::kResultOk);
            Steinberg::IBStreamer r2(s2, kLittleEndian);
            ::Profundum::EndpointName back{};
            CHECK(::Profundum::readEndpointName(r2, back));
            CHECK(back.view() == euros);
        }

        const auto prior = ::Profundum::sanitizeEndpointName("PRIOR");

        // Length byte 32 -> rejected, the target unchanged.
        {
            const auto block = blockOf(32u, std::string(31, 'x'));
            auto s = memoryStreamOf(block);
            Steinberg::IBStreamer r(s, kLittleEndian);
            auto target = prior;
            CHECK_FALSE(::Profundum::readEndpointName(r, target));
            CHECK(target.view() == "PRIOR");
        }
        // Invalid UTF-8 payload -> rejected.
        {
            const auto block = blockOf(2u, bytesOf({0xC3, '('}));
            auto s = memoryStreamOf(block);
            Steinberg::IBStreamer r(s, kLittleEndian);
            auto target = prior;
            CHECK_FALSE(::Profundum::readEndpointName(r, target));
            CHECK(target.view() == "PRIOR");
        }
        // Only 20 bytes left in the stream -> rejected.
        {
            const auto block = blockOf(5u, "SHORT");
            auto s = memoryStreamOf(std::span<const std::byte>(block.data(), 20u));
            Steinberg::IBStreamer r(s, kLittleEndian);
            auto target = prior;
            CHECK_FALSE(::Profundum::readEndpointName(r, target));
            CHECK(target.view() == "PRIOR");
        }
    }

    SECTION("DescriptorTable") {
        // design/advanced-page.md 7.3; fields not listed keep their MorphState{} defaults.
        // 1. MorphState{}
        CHECK(wordsOf(MorphState{}) == "Full Warm Clean");

        // 2. / 3. The Phase 8 showcase pair.
        const auto pair = Krate::DSP::BassMorphTest::showcasePair();
        CHECK(wordsOf(pair.first) == "Round Dark Clean");
        CHECK(wordsOf(pair.second) == "Full Warm Aggressive");

        // 4. Every score exactly 0.33: each threshold belongs to the upper band.
        MorphState c4{};
        c4.recipe.depth = 1.0f;
        c4.recipe.body = 0.66f;
        c4.recipe.edge = 0.66f;
        c4.recipe.shift = -1.0f;
        c4.characterAmount = 0.33f;
        c4.foldAmount = 0.0f;
        c4.excitation.mix = 0.0f;
        CHECK(wordsOf(c4) == "Full Warm Driven");

        // 5. Every score exactly 0.66 (Force through foldAmount 66).
        MorphState c5{};
        c5.recipe.depth = 0.4f;
        c5.recipe.body = 0.72f;
        c5.recipe.edge = 0.72f;
        c5.recipe.shift = 0.2f;
        c5.foldAmount = 66.0f;
        c5.characterAmount = 0.0f;
        c5.excitation.mix = 0.0f;
        CHECK(wordsOf(c5) == "Dense Bright Aggressive");

        // 6. Every score 0.6599, just below the upper threshold.
        MorphState c6{};
        c6.recipe.depth = 0.0f;
        c6.recipe.body = 0.3198f;
        c6.recipe.edge = 1.0f;
        c6.recipe.shift = -0.3604f;
        c6.characterAmount = 0.6599f;
        c6.foldAmount = 0.0f;
        c6.excitation.mix = 0.0f;
        CHECK(wordsOf(c6) == "Full Warm Driven");

        // Fold 33 alone reaches the middle Force band; 32.9 does not.
        MorphState fold{};
        fold.foldAmount = 33.0f;
        CHECK(wordsOf(fold) == "Full Warm Driven");
        fold.foldAmount = 32.9f;
        CHECK(wordsOf(fold) == "Full Warm Clean");

        // Identical states give the same words (names are not part of the state).
        const MorphState twinA = pair.second;
        const MorphState twinB = pair.second;
        CHECK(wordsOf(twinA) == wordsOf(twinB));

        // Changing a field the table reads changes the words as the table says: edge 1.0 -> Colour 0.75.
        MorphState brighter{};
        brighter.recipe.edge = 1.0f;
        CHECK(wordsOf(brighter) == "Full Bright Clean");

        // Raising body from the Round band into the Dense band (depth 0.5): word 1 changes, nothing else.
        MorphState low{};
        low.recipe.body = 0.0f;   // Shape 0.25
        MorphState high = low;
        high.recipe.body = 1.0f;  // Shape 0.75
        const auto wl = ::Profundum::UI::describeEndpoint(low);
        const auto wh = ::Profundum::UI::describeEndpoint(high);
        CHECK(std::string_view(wl[0]) == "Round");
        CHECK(std::string_view(wh[0]) == "Dense");
        CHECK(std::string_view(wl[1]) == std::string_view(wh[1]));
        CHECK(std::string_view(wl[2]) == std::string_view(wh[2]));
        CHECK(wordsOf(low) == "Round Warm Clean");
        CHECK(wordsOf(high) == "Dense Warm Clean");
    }

    // ---- T019: state v3 and the name message (plan 4.5, 6.2 SC-027) ----

    SECTION("CurrentVersionIsThree") {
        CHECK(::Profundum::kCurrentStateVersion == 3);
        CHECK(::Profundum::kStateV2Version == 2);
        CHECK(::Profundum::kStateV3Bytes == 574u);
        CHECK(::Profundum::kStateNamesOffset == 510u);

        ProcessorFixture fx;
        const StateBytes s = writeV2Stream(*fx.proc);
        REQUIRE(s.size() == 574u);
        CHECK(readU32(s, 0) == 3u);
        CHECK(nameBlockIs(s, 510u, "A"));
        CHECK(nameBlockIs(s, 542u, "B"));
        CHECK(fx.proc->endpointNameForTest(Endpoint::A).view() == "A");
        CHECK(fx.proc->endpointNameForTest(Endpoint::B).view() == "B");
    }

    SECTION("ApiCommitSendsOneMessage") {  // SC-027 (a), API arm
        ConnectedPair cp;
        cp.clearRecords();
        const int dirtyBefore = cp.cf.handler.dirtyCount;

        cp.cf.controller->setEndpointName(Endpoint::A, "DEEP PRESSURE");

        const auto recs = cp.toProcessor.records();
        REQUIRE(recs.size() == 1u);
        CHECK(recs[0].id == std::string(::Profundum::kMsgEndpointName));
        REQUIRE(recs[0].ints.count("endpoint") == 1u);
        CHECK(recs[0].ints.at("endpoint") == 0);
        REQUIRE(recs[0].hasName);
        REQUIRE(recs[0].name.size() == 13u);
        CHECK(std::memcmp(recs[0].name.data(), "DEEP PRESSURE", 13u) == 0);
        CHECK(cp.cf.handler.dirtyCount == dirtyBefore + 1);
        CHECK(cp.toController.records().empty());  // nothing is reported back (plan 4.5)

        CHECK(cp.fx.proc->endpointNameForTest(Endpoint::A).view() == "DEEP PRESSURE");
        CHECK(cp.fx.proc->endpointNameForTest(Endpoint::B).view() == "B");
        CHECK(cp.cf.controller->endpointNameForTest(Endpoint::A).view() == "DEEP PRESSURE");
        CHECK(cp.cf.controller->endpointName(Endpoint::B).view() == "B");

        // The save carries it.
        const StateBytes s = writeV2Stream(*cp.fx.proc);
        REQUIRE(s.size() == ::Profundum::kStateV3Bytes);
        CHECK(nameBlockIs(s, kNameBlockA, "DEEP PRESSURE"));
        CHECK(nameBlockIs(s, kNameBlockB, "B"));
    }

    SECTION("NamesRoundTrip") {  // SC-027 (a)
        const std::string longAscii = "ABCDEFGHIJKLMNOPQRSTUVWXYZ01234";
        REQUIRE(longAscii.size() == 31u);
        const std::string euros = repeated(bytesOf({0xE2, 0x82, 0xAC}), 11);  // 33 bytes
        REQUIRE(euros.size() == 33u);
        const ::Profundum::EndpointName wantA = ::Profundum::sanitizeEndpointName(longAscii);
        const ::Profundum::EndpointName wantB = ::Profundum::sanitizeEndpointName(euros);
        REQUIRE(wantA.length == 31u);
        REQUIRE(wantB.length == 30u);  // ten code points: cut at a code-point boundary

        ProcessorFixture src;
        notifyName(*src.proc, Endpoint::A, longAscii);
        notifyName(*src.proc, Endpoint::B, euros);
        const StateBytes saved = writeV2Stream(*src.proc);
        REQUIRE(saved.size() == ::Profundum::kStateV3Bytes);
        CHECK(nameBlockIs(saved, kNameBlockA, wantA.view()));
        CHECK(nameBlockIs(saved, kNameBlockB, wantB.view()));

        ProcessorFixture dst;  // a fresh heap processor
        REQUIRE(loadStateBytes(*dst.proc, saved) == Steinberg::kResultOk);
        CHECK(dst.proc->endpointNameForTest(Endpoint::A).view() == wantA.view());
        CHECK(dst.proc->endpointNameForTest(Endpoint::B).view() == wantB.view());

        ControllerFixture cf;  // a fresh controller
        REQUIRE(loadControllerBytes(*cf.controller, saved) == Steinberg::kResultOk);
        CHECK(cf.controller->endpointNameForTest(Endpoint::A).view() == wantA.view());
        CHECK(cf.controller->endpointNameForTest(Endpoint::B).view() == wantB.view());

        // Save -> load -> save is byte-identical.
        const StateBytes again = writeV2Stream(*dst.proc);
        REQUIRE(again.size() == saved.size());
        CHECK(std::memcmp(again.data(), saved.data(), saved.size()) == 0);
    }

    SECTION("OlderStreamsLoad") {  // SC-027 (b); guards processor.cpp:504 and controller.cpp:117 (R-11)
        const V2Fields f = priorFieldsP();
        const MorphState a = randomValidMorphState(0x41Au);
        const MorphState b = randomValidMorphState(0x41Bu);
        const StateBytes v2 = v2StreamOf(f, a, b);
        REQUIRE(v2.size() == ::Profundum::kStateV2Bytes);
        REQUIRE(readU32(v2, 0) == static_cast<std::uint32_t>(::Profundum::kStateV2Version));

        // v2 -> processor: every v2 value restored, the names become the defaults.
        {
            ProcessorFixture p;
            notifyName(*p.proc, Endpoint::A, "OLD A");  // teeth: names the load must replace
            notifyName(*p.proc, Endpoint::B, "OLD B");
            REQUIRE(loadStateBytes(*p.proc, v2) == Steinberg::kResultOk);
            requireV2Restored(*p.proc, v2, f, a, b);
            CHECK(p.proc->endpointNameForTest(Endpoint::A).view() == "A");
            CHECK(p.proc->endpointNameForTest(Endpoint::B).view() == "B");
        }
        // v2 -> controller.
        {
            ControllerFixture cf;
            cf.controller->setEndpointName(Endpoint::A, "OLD A");
            cf.controller->setEndpointName(Endpoint::B, "OLD B");
            REQUIRE(cf.controller->endpointNameForTest(Endpoint::A).view() == "OLD A");
            REQUIRE(loadControllerBytes(*cf.controller, v2) == Steinberg::kResultOk);
            requireV2Displayed(*cf.controller, f, a, b);
            CHECK(cf.controller->endpointNameForTest(Endpoint::A).view() == "A");
            CHECK(cf.controller->endpointNameForTest(Endpoint::B).view() == "B");
        }

        // v1 (FR-044 reset) on a non-default prior: globals and macros from the stream, every Phase 11 field reset.
        const V1Globals g{.gain = 0.3f, .voiceMode = 1, .polyphony = 3, .ceilingDb = -2.0f};
        const std::array<float, 4> macros{0.15f, 0.25f, 0.35f, 0.45f};
        const StateBytes v1 = v1Stream(g, macros);
        {
            ProcessorFixture p;
            REQUIRE(loadStateBytes(*p.proc, v2) == Steinberg::kResultOk);
            notifyName(*p.proc, Endpoint::A, "OLD A");
            notifyName(*p.proc, Endpoint::B, "OLD B");
            REQUIRE(slotsOf(*p.proc, Endpoint::A) != ::Profundum::kDefaultLeafBits);  // teeth

            REQUIRE(loadStateBytes(*p.proc, v1) == Steinberg::kResultOk);
            const auto& gp = p.proc->globalParamsForTest();
            CHECK(gp.outputGain.load() == g.gain);
            CHECK(gp.voiceMode.load() == g.voiceMode);
            CHECK(gp.polyphony.load() == g.polyphony);
            CHECK(gp.outputCeilingDb.load() == g.ceilingDb);
            const auto& mp = p.proc->macroParamsForTest();
            CHECK(mp.weight.load() == macros[0]);
            CHECK(mp.darkBright.load() == macros[1]);
            CHECK(mp.tightLoose.load() == macros[2]);
            CHECK(mp.cleanViolent.load() == macros[3]);
            CHECK(slotsOf(*p.proc, Endpoint::A) == ::Profundum::kDefaultLeafBits);
            CHECK(slotsOf(*p.proc, Endpoint::B) == ::Profundum::kDefaultLeafBits);
            const StateBytes out = writeV2Stream(*p.proc);
            REQUIRE(out.size() == ::Profundum::kStateV3Bytes);
            CHECK(f32At(out, kV2MorphAt) == ::Profundum::kDefaultMorphPosition);
            CHECK(readU32(out, kV2MorphAt + 12u) == static_cast<std::uint32_t>(::Profundum::kDefaultEditTarget));
            CHECK(readU32(out, kV2SeedAt) == Krate::DSP::ProfundumEngine::kDefaultSeed);
            CHECK(p.proc->endpointNameForTest(Endpoint::A).view() == "A");
            CHECK(p.proc->endpointNameForTest(Endpoint::B).view() == "B");
            CHECK(nameBlockIs(out, kNameBlockA, "A"));
            CHECK(nameBlockIs(out, kNameBlockB, "B"));
        }
        {
            ControllerFixture cf;
            REQUIRE(loadControllerBytes(*cf.controller, v2) == Steinberg::kResultOk);
            cf.controller->setEndpointName(Endpoint::A, "OLD A");
            REQUIRE_FALSE(sameMorph(cf.controller->mirrorForTest(Endpoint::A), MorphState{}));  // teeth

            REQUIRE(loadControllerBytes(*cf.controller, v1) == Steinberg::kResultOk);
            CHECK(sameMorph(cf.controller->mirrorForTest(Endpoint::A), MorphState{}));
            CHECK(sameMorph(cf.controller->mirrorForTest(Endpoint::B), MorphState{}));
            CHECK(cf.controller->getParamNormalized(::Profundum::kMorphEditTargetId) == 0.0);
            for (std::size_t i = 0; i < macros.size(); ++i) {
                CAPTURE(i);
                CHECK(cf.controller->getParamNormalized(::Profundum::kMacroWeightId +
                                                        static_cast<Steinberg::Vst::ParamID>(i)) ==
                      static_cast<double>(macros[i]));
            }
            CHECK(cf.controller->endpointNameForTest(Endpoint::A).view() == "A");
            CHECK(cf.controller->endpointNameForTest(Endpoint::B).view() == "B");
        }
    }

    SECTION("V3PrefixEqualsV2Body") {  // SC-027 (c)
        // Arm 1: the genuine v2 reference loaded, then saved as v3.
        {
            const V2Fields f = priorFieldsP();
            const StateBytes ref = v2StreamOf(f, randomValidMorphState(0x42Au), randomValidMorphState(0x42Bu));
            ProcessorFixture p;
            REQUIRE(loadStateBytes(*p.proc, ref) == Steinberg::kResultOk);
            const StateBytes out = writeV2Stream(*p.proc);
            REQUIRE(out.size() == ::Profundum::kStateV3Bytes);
            CHECK(readU32(out, 0) == 3u);
            CHECK(std::memcmp(out.data() + 4, ref.data() + 4, ::Profundum::kStateV2Bytes - 4u) == 0);
            CHECK(nameBlockIs(out, kNameBlockA, "A"));
            CHECK(nameBlockIs(out, kNameBlockB, "B"));
        }

        // Arm 2: values set through parameters, then decoded at their fixed pre-phase offsets in the v3 stream.
        {
            ProcessorFixture q;
            REQUIRE(q.prepare(kFs) == Steinberg::kResultOk);
            REQUIRE(q.setParamsInBlock({
                        {::Profundum::kMacroWeightId, 0.1},
                        {::Profundum::kMacroDarkBrightId, 0.2},
                        {::Profundum::kMacroTightLooseId, 0.8},
                        {::Profundum::kMacroCleanViolentId, 0.9},
                        {::Profundum::kVoiceModeId, 1.0},
                        {::Profundum::kNoteLegatoId, 1.0},
                        {::Profundum::kMorphPositionId, 0.25},
                        {::Profundum::kMorphEditTargetId, 1.0},
                    }) == Steinberg::kResultOk);
            REQUIRE(q.setParam(::Profundum::kCoreDepthId, 0.37) == Steinberg::kResultOk);  // lands in B
            REQUIRE(slotsOf(*q.proc, Endpoint::B) != ::Profundum::kDefaultLeafBits);      // teeth

            const StateBytes out = writeV2Stream(*q.proc);
            REQUIRE(out.size() == ::Profundum::kStateV3Bytes);
            CHECK(readU32(out, 0) == 3u);
            CHECK(f32At(out, kV2MacrosAt + 0u) == 0.1f);
            CHECK(f32At(out, kV2MacrosAt + 4u) == 0.2f);
            CHECK(f32At(out, kV2MacrosAt + 8u) == 0.8f);
            CHECK(f32At(out, kV2MacrosAt + 12u) == 0.9f);
            CHECK(readU32(out, kV2GlobalsAt + 4u) == 1u);  // voice mode
            CHECK(readU32(out, kV2NoteAt + 4u) == 1u);     // legato
            CHECK(f32At(out, kV2MorphAt + 0u) == 0.25f);   // position
            CHECK(readU32(out, kV2MorphAt + 12u) == 1u);   // Edit Target B
            CHECK(readU32(out, kV2SeedAt) == Krate::DSP::ProfundumEngine::kDefaultSeed);
            CHECK(::Profundum::leafBitsOf(endpointAt(out, ::Profundum::kStateEndpointAOffset)) ==
                  slotsOf(*q.proc, Endpoint::A));
            CHECK(::Profundum::leafBitsOf(endpointAt(out, ::Profundum::kStateEndpointBOffset)) ==
                  slotsOf(*q.proc, Endpoint::B));
        }
    }

    SECTION("DescriptorNotStored") {  // SC-027 (e)
        MorphState lowBody{};
        lowBody.recipe.body = 0.0f;
        MorphState highBody{};
        highBody.recipe.body = 1.0f;
        REQUIRE(Krate::DSP::isValidMorphState(lowBody));
        REQUIRE(Krate::DSP::isValidMorphState(highBody));
        // Teeth: body is a field the descriptor reads (word 1 changes).
        REQUIRE(std::string_view(::Profundum::UI::describeEndpoint(lowBody)[0]) !=
                std::string_view(::Profundum::UI::describeEndpoint(highBody)[0]));

        const V2Fields f = priorFieldsP();
        const MorphState b = randomValidMorphState(0x51Bu);
        ProcessorFixture p1;
        ProcessorFixture p2;
        REQUIRE(loadStateBytes(*p1.proc, v2StreamOf(f, lowBody, b)) == Steinberg::kResultOk);
        REQUIRE(loadStateBytes(*p2.proc, v2StreamOf(f, highBody, b)) == Steinberg::kResultOk);
        for (ProcessorFixture* p : {&p1, &p2}) {
            notifyName(*p->proc, Endpoint::A, "SAME A");
            notifyName(*p->proc, Endpoint::B, "SAME B");
        }
        const StateBytes s1 = writeV2Stream(*p1.proc);
        const StateBytes s2 = writeV2Stream(*p2.proc);
        REQUIRE(s1.size() == s2.size());
        REQUIRE(s1.size() == ::Profundum::kStateV3Bytes);
        std::size_t differing = 0;
        std::size_t outside = 0;
        for (std::size_t i = 0; i < s1.size(); ++i) {
            if (s1[i] != s2[i]) {
                ++differing;
                if (i < ::Profundum::kStateEndpointAOffset || i >= ::Profundum::kStateRuntimeOffset) {
                    UNSCOPED_INFO("byte " << i << " differs outside the settings");
                    ++outside;
                }
            }
        }
        CHECK(differing > 0u);
        CHECK(outside == 0u);
    }

    SECTION("ShortNameBlockKeepsPriorName") {  // the Phase 11 truncation rule, extended to the name blocks
        const MorphState a = randomValidMorphState(0x61Au);
        const MorphState b = randomValidMorphState(0x61Bu);
        ProcessorFixture src;
        REQUIRE(loadStateBytes(*src.proc, v2StreamOf(priorFieldsP(), a, b)) == Steinberg::kResultOk);
        notifyName(*src.proc, Endpoint::A, "NEW A");
        notifyName(*src.proc, Endpoint::B, "NEW B");
        const StateBytes full = writeV2Stream(*src.proc);
        REQUIRE(full.size() == ::Profundum::kStateV3Bytes);
        REQUIRE(nameBlockIs(full, kNameBlockA, "NEW A"));
        const StateBytes cut(full.begin(), full.begin() + 530);

        ProcessorFixture dst;
        notifyName(*dst.proc, Endpoint::A, "PRIOR A");
        notifyName(*dst.proc, Endpoint::B, "PRIOR B");
        REQUIRE(loadStateBytes(*dst.proc, cut) == Steinberg::kResultOk);
        CHECK(dst.proc->endpointNameForTest(Endpoint::A).view() == "PRIOR A");
        CHECK(dst.proc->endpointNameForTest(Endpoint::B).view() == "PRIOR B");
        // The rest loads: both slots, and the whole v2 body (blob pending) equals the source's.
        CHECK(slotsOf(*dst.proc, Endpoint::A) == ::Profundum::leafBitsOf(a));
        CHECK(slotsOf(*dst.proc, Endpoint::B) == ::Profundum::leafBitsOf(b));
        const StateBytes after = writeV2Stream(*dst.proc);
        REQUIRE(after.size() == ::Profundum::kStateV3Bytes);
        CHECK(std::memcmp(after.data() + 4, full.data() + 4, ::Profundum::kStateNamesOffset - 4u) == 0);
        CHECK(nameBlockIs(after, kNameBlockA, "PRIOR A"));
        CHECK(nameBlockIs(after, kNameBlockB, "PRIOR B"));

        // The controller follows the same rule.
        ControllerFixture cf;
        cf.controller->setEndpointName(Endpoint::A, "PRIOR A");
        cf.controller->setEndpointName(Endpoint::B, "PRIOR B");
        REQUIRE(loadControllerBytes(*cf.controller, cut) == Steinberg::kResultOk);
        CHECK(cf.controller->endpointNameForTest(Endpoint::A).view() == "PRIOR A");
        CHECK(cf.controller->endpointNameForTest(Endpoint::B).view() == "PRIOR B");
        CHECK(sameMorph(cf.controller->mirrorForTest(Endpoint::A), a));
        CHECK(sameMorph(cf.controller->mirrorForTest(Endpoint::B), b));
        // A full v3 stream replaces both.
        REQUIRE(loadControllerBytes(*cf.controller, full) == Steinberg::kResultOk);
        CHECK(cf.controller->endpointNameForTest(Endpoint::A).view() == "NEW A");
        CHECK(cf.controller->endpointNameForTest(Endpoint::B).view() == "NEW B");
    }

    SECTION("NotifyIsSanitised") {  // R-13
        ProcessorFixture p;
        const std::string raw = std::string(20, 'N') + bytesOf({0x01, 0x01, 0x01, 0x01, 0x01}) + std::string(15, 'M');
        REQUIRE(raw.size() == 40u);
        REQUIRE(p.proc->notify(makeNameMessage(0, raw)) == Steinberg::kResultOk);
        const ::Profundum::EndpointName got = p.proc->endpointNameForTest(Endpoint::A);
        CHECK(got.length == 31u);
        CHECK(got.view() == std::string(20, 'N') + std::string(11, 'M'));
        CHECK(got.view() == ::Profundum::sanitizeEndpointName(raw).view());

        // Endpoint index 2 (and -1) is ignored: neither name moves.
        static_cast<void>(p.proc->notify(makeNameMessage(2, "IGNORED")));
        static_cast<void>(p.proc->notify(makeNameMessage(-1, "IGNORED")));
        CHECK(p.proc->endpointNameForTest(Endpoint::A).view() == got.view());
        CHECK(p.proc->endpointNameForTest(Endpoint::B).view() == "B");

        const StateBytes s = writeV2Stream(*p.proc);
        REQUIRE(s.size() == ::Profundum::kStateV3Bytes);
        CHECK(nameBlockIs(s, kNameBlockA, got.view()));
        CHECK(nameBlockIs(s, kNameBlockB, "B"));
    }

    // ---- T023: the endpoint caption (plan 4.6.6, SC-027 (f)) ----

    SECTION("CaptionFormat") {  // SC-027 (f)
        ControllerFixture cf;
        auto& ctl = *cf.controller;
        // A non-default mirror for A (the processor's report), so the words are not the MorphState{} ones by luck.
        MorphState a{};
        a.recipe.body = 0.0f;  // "Round" (DescriptorTable)
        REQUIRE(ctl.notify(makeEndpointMessage(Endpoint::A, a)) == Steinberg::kResultOk);
        REQUIRE(sameMorph(ctl.mirrorForTest(Endpoint::A), a));

        // The em dash and the middle dot, spelled as UTF-8 bytes.
        const std::string dash = " \xE2\x80\x94 ";
        const std::string dot = " \xC2\xB7 ";
        // name + dash + w0 + dot + w1 + dot + w2, appended in place (no temporary strings).
        const auto caption = [&](std::string_view name, std::string_view w0, std::string_view w1, std::string_view w2) {
            std::string out(name);
            out += dash;
            out += w0;
            out += dot;
            out += w1;
            out += dot;
            out += w2;
            return out;
        };
        const auto expectedCaption = [&](std::string_view name, const MorphState& s) {
            const auto w = ::Profundum::UI::describeEndpoint(s);
            return caption(name, w[0], w[1], w[2]);
        };

        ctl.setEndpointName(Endpoint::A, "IRON FOLD");
        const std::string capA = ctl.endpointCaption(Endpoint::A);
        CHECK(capA == expectedCaption("IRON FOLD", ctl.mirrorForTest(Endpoint::A)));
        CHECK(capA == caption("IRON FOLD", "Round", "Warm", "Clean"));
        CHECK_FALSE(capA.empty());

        // Exactly three descriptor words after the dash, separated by exactly two dots.
        const std::size_t at = capA.find(dash);
        REQUIRE(at != std::string::npos);
        CHECK(capA.substr(0, at) == "IRON FOLD");
        const std::string tail = capA.substr(at + dash.size());
        std::vector<std::string> words;
        std::size_t from = 0;
        for (;;) {
            const std::size_t next = tail.find(dot, from);
            words.push_back(tail.substr(from, next == std::string::npos ? std::string::npos : next - from));
            if (next == std::string::npos) {
                break;
            }
            from = next + dot.size();
        }
        REQUIRE(words.size() == 3u);
        const auto w = ::Profundum::UI::describeEndpoint(ctl.mirrorForTest(Endpoint::A));
        for (std::size_t k = 0; k < 3u; ++k) {
            CAPTURE(k);
            CHECK_FALSE(words[k].empty());
            CHECK(words[k] == std::string(w[k]));
            CHECK(words[k].find(' ') == std::string::npos);
        }

        // B keeps its default name and the MorphState{} words.
        CHECK(ctl.endpointCaption(Endpoint::B) == expectedCaption("B", MorphState{}));
        CHECK(ctl.endpointCaption(Endpoint::B) == caption("B", "Full", "Warm", "Clean"));

        // An empty name yields the default name "A".
        ctl.setEndpointName(Endpoint::A, "");
        const std::string capEmpty = ctl.endpointCaption(Endpoint::A);
        CHECK(capEmpty.rfind("A" + dash, 0) == 0u);
        CHECK(capEmpty == expectedCaption("A", ctl.mirrorForTest(Endpoint::A)));

        // Recomputed on a mirror replacement: body 1.0 moves word 1 to "Dense", the name stays.
        ctl.setEndpointName(Endpoint::A, "IRON FOLD");
        MorphState dense = a;
        dense.recipe.body = 1.0f;
        REQUIRE(ctl.notify(makeEndpointMessage(Endpoint::A, dense)) == Steinberg::kResultOk);
        CHECK(ctl.endpointCaption(Endpoint::A) == caption("IRON FOLD", "Dense", "Warm", "Clean"));
        // ... and on a name change.
        ctl.setEndpointName(Endpoint::A, "DEEP PRESSURE");
        CHECK(ctl.endpointCaption(Endpoint::A) == expectedCaption("DEEP PRESSURE", dense));
        // Two reads are identical (the caption is computed, never stored).
        CHECK(ctl.endpointCaption(Endpoint::A) == ctl.endpointCaption(Endpoint::A));
    }

    // ---- T028: the name commit through the built tree (SC-027 (a) UI arm; FR-040, FR-047) ----

    SECTION("TextEditCommit") {
        ConnectedPair cp;
        auto& ctl = *cp.cf.controller;
        Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();
        HeadlessEditor ed(cp.cf.controller.get());
        REQUIRE(ed.frame() != nullptr);
        Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();

        // The panel gave the name field and the caption their session tags in verifyView. CTextEdit derives from
        // CTextLabel, so the caption is the 9221 label that is not an edit.
        VSTGUI::CTextEdit* nameB = nullptr;
        VSTGUI::CTextLabel* captionB = nullptr;
        int nameEdits = 0;
        int captionLabels = 0;
        for (VSTGUI::CTextLabel* label : collectViewsOfType<VSTGUI::CTextLabel>(ed.frame())) {
            auto* edit = dynamic_cast<VSTGUI::CTextEdit*>(label);
            if (label->getTag() == ::Profundum::UI::kNameBTag && edit != nullptr) {
                nameB = edit;
                ++nameEdits;
            } else if (label->getTag() == ::Profundum::UI::kCaptionBTag && edit == nullptr) {
                captionB = label;
                ++captionLabels;
            }
        }
        REQUIRE(nameEdits == 1);
        REQUIRE(captionLabels == 1);
        REQUIRE(nameB != nullptr);
        REQUIRE(captionB != nullptr);
        CHECK(captionB->getText().getString() == ctl.endpointCaption(Endpoint::B));  // "B ..." before the commit
        cp.clearRecords();
        const int dirtyBefore = cp.cf.handler.dirtyCount;

        // The CTextEdit commit path (ctextedit.cpp:406-418, updateText): beginEdit, setText, valueChanged, endEdit.
        nameB->beginEdit();
        nameB->setText("IRON FOLD");
        nameB->valueChanged();
        nameB->endEdit();

        const auto recs = cp.toProcessor.records();
        REQUIRE(recs.size() == 1u);
        CHECK(recs[0].id == std::string(::Profundum::kMsgEndpointName));
        REQUIRE(recs[0].ints.count("endpoint") == 1u);
        CHECK(recs[0].ints.at("endpoint") == 1);
        REQUIRE(recs[0].hasName);
        REQUIRE(recs[0].name.size() == 9u);
        CHECK(std::memcmp(recs[0].name.data(), "IRON FOLD", 9u) == 0);
        CHECK(cp.cf.handler.dirtyCount == dirtyBefore + 1);
        CHECK(cp.cf.handler.beginEdits.empty());  // a session control is never a host gesture
        CHECK(cp.cf.handler.performEdits.empty());
        CHECK(cp.cf.handler.endEdits.empty());

        CHECK(ctl.endpointName(Endpoint::B).view() == "IRON FOLD");
        CHECK(ctl.endpointName(Endpoint::A).view() == "A");
        CHECK(cp.fx.proc->endpointNameForTest(Endpoint::B).view() == "IRON FOLD");

        // The 9221 caption reads the T023 caption of B, which now carries the committed name.
        const std::string caption = ctl.endpointCaption(Endpoint::B);
        CHECK(caption.rfind("IRON FOLD \xE2\x80\x94 ", 0) == 0u);
        CHECK(captionB->getText().getString() == caption);
        CHECK(nameB->getText().getString() == "IRON FOLD");
    }
}
