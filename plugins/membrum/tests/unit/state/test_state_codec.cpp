// ==============================================================================
// Membrum state codec tests -- direct round-trip, version rejection, session
// fields, per-pad preset slice.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "state/state_codec.h"
#include "dsp/pad_config.h"
#include "unit/state/vstpreset_chunk.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/base/ibstream.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

using namespace Steinberg;
using Catch::Approx;
using Membrum::State::KitSnapshot;
using Membrum::State::PadSnapshot;
using Membrum::State::PadPresetSnapshot;
using Membrum::State::writeKitBlob;
using Membrum::State::readKitBlob;
using Membrum::State::writePadPresetBlob;
using Membrum::State::readPadPresetBlob;
using Membrum::State::toPadSnapshot;
using Membrum::State::applyPadSnapshot;
using Membrum::State::toPadPresetSnapshot;
using Membrum::State::applyPadPresetSnapshot;

namespace {

/// Build a KitSnapshot with distinct, deterministic, in-range values for
/// every field so round-trip mismatches surface as concrete deltas.
KitSnapshot makePopulatedKit()
{
    KitSnapshot kit;
    kit.maxPolyphony        = 12;
    kit.voiceStealingPolicy = 1;
    kit.selectedPadIndex    = 7;
    kit.globalCoupling      = 0.3;
    kit.snareBuzz           = 0.6;
    kit.tomResonance        = 0.45;
    kit.couplingDelayMs     = 1.25;

    for (std::size_t p = 0; p < kit.pads.size(); ++p)
    {
        auto& pad = kit.pads[p];
        const double base = static_cast<double>(p) / 32.0;
        pad.exciterType = static_cast<Membrum::ExciterType>(p % static_cast<std::size_t>(Membrum::ExciterType::kCount));
        pad.bodyModel   = static_cast<Membrum::BodyModelType>(p % static_cast<std::size_t>(Membrum::BodyModelType::kCount));
        for (std::size_t i = 0; i < pad.sound.size(); ++i)
        {
            pad.sound[i] = std::clamp(0.1 + base + static_cast<double>(i) * 0.013, 0.0, 1.0);
        }
        // Overwrite sound[28]/[29] with the float64 mirror of choke/bus.
        pad.chokeGroup      = static_cast<std::uint8_t>(p % 9);
        pad.outputBus       = static_cast<std::uint8_t>(p % 16);
        pad.sound[28]       = static_cast<double>(pad.chokeGroup);
        pad.sound[29]       = static_cast<double>(pad.outputBus);
        pad.couplingAmount  = std::clamp(0.05 + base, 0.0, 1.0);
        pad.macros[0]       = std::clamp(0.2 + base * 0.5, 0.0, 1.0);
        pad.macros[1]       = std::clamp(0.8 - base * 0.5, 0.0, 1.0);
        pad.macros[2]       = std::clamp(0.3 + base * 0.4, 0.0, 1.0);
        pad.macros[3]       = std::clamp(0.7 - base * 0.3, 0.0, 1.0);
        pad.macros[4]       = std::clamp(0.5 + (base - 0.5) * 0.25, 0.0, 1.0);
    }

    return kit;
}

bool padsEquivalent(const PadSnapshot& a, const PadSnapshot& b, double margin = 1e-12)
{
    if (a.exciterType != b.exciterType) return false;
    if (a.bodyModel   != b.bodyModel)   return false;
    for (std::size_t i = 0; i < a.sound.size(); ++i)
        if (std::abs(a.sound[i] - b.sound[i]) > margin) return false;
    if (a.chokeGroup  != b.chokeGroup)  return false;
    if (a.outputBus   != b.outputBus)   return false;
    if (std::abs(a.couplingAmount - b.couplingAmount) > margin) return false;
    for (std::size_t i = 0; i < a.macros.size(); ++i)
        if (std::abs(a.macros[i] - b.macros[i]) > margin) return false;
    return true;
}

bool kitsEquivalent(const KitSnapshot& a, const KitSnapshot& b, double margin = 0.0)
{
    if (a.maxPolyphony != b.maxPolyphony || a.voiceStealingPolicy != b.voiceStealingPolicy ||
        a.selectedPadIndex != b.selectedPadIndex || a.uiMode != b.uiMode ||
        a.hasSession != b.hasSession)
        return false;
    if (std::abs(a.globalCoupling - b.globalCoupling) > margin ||
        std::abs(a.snareBuzz - b.snareBuzz) > margin ||
        std::abs(a.tomResonance - b.tomResonance) > margin ||
        std::abs(a.couplingDelayMs - b.couplingDelayMs) > margin ||
        std::abs(a.masterGainNorm - b.masterGainNorm) > margin)
        return false;
    for (std::size_t p = 0; p < a.pads.size(); ++p)
        if (!padsEquivalent(a.pads[p], b.pads[p], margin)) return false;
    return true;
}

// ---- Released blob versions (fix plan T2.8) ---------------------------------
// Sound slots per pad that each released build wrote, read at the release tags
// (git show membrum/<tag>:plugins/membrum/src/state/state_codec.h):
//   version 1: membrum/v0.9.0, v0.9.1      kit 52 slots, pad preset 51
//   version 2: membrum/v0.10.1, v0.10.2    kit 56 slots, pad preset 56
//   version 5: membrum/v0.12.0 - v0.12.5   kit 59 slots, pad preset 59
// No release wrote 3 or 4 (v0.11.0 was never tagged).
std::size_t kitSlotsOf(int32 version) { return version == 1 ? 52U : version == 2 ? 56U : 59U; }
std::size_t padSlotsOf(int32 version) { return version == 1 ? 51U : version == 2 ? 56U : 59U; }

/// A kit blob as it sits on the wire, any version.
struct RawKit
{
    struct Pad
    {
        int32 exciter = 0;
        int32 body = 0;
        std::vector<double> sound;
        std::uint8_t choke = 0;
        std::uint8_t bus = 0;
    };
    int32 version = 0;
    int32 maxPoly = 8;
    int32 steal = 0;
    std::array<Pad, 32> pads{};
    int32 selected = 0;
    std::array<double, 4> globals{0.0, 0.0, 0.0, 1.0};  // gc, sb, tr, cd
    std::array<double, 32> couplingAmount{};
    std::array<std::array<double, 5>, 32> macros{};
    double masterGain = 0.5;
    bool hasUiMode = false;
    int32 uiMode = 0;
};

/// Little-endian reader over raw bytes, independent of the codec.
class LeReader
{
public:
    explicit LeReader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}

    std::uint64_t bits(std::size_t n)
    {
        REQUIRE(pos_ + n <= bytes_.size());
        std::uint64_t v = 0;
        for (std::size_t i = 0; i < n; ++i)
            v |= static_cast<std::uint64_t>(bytes_[pos_ + i]) << (8U * i);
        pos_ += n;
        return v;
    }
    int32 i32() { return static_cast<int32>(static_cast<std::uint32_t>(bits(4))); }
    std::uint8_t u8() { return static_cast<std::uint8_t>(bits(1)); }
    double f64()
    {
        const std::uint64_t b = bits(8);
        double d = 0.0;
        std::memcpy(&d, &b, sizeof(d));
        return d;
    }
    [[nodiscard]] std::size_t remaining() const { return bytes_.size() - pos_; }

private:
    const std::vector<std::uint8_t>& bytes_;
    std::size_t pos_ = 0;
};

/// Parses a kit blob of any released layout: header 12 bytes; per pad
/// 4 + 4 + n x 8 + 1 + 1 bytes; the selected pad, the four coupling globals,
/// 32 coupling amounts, 160 macros, the master gain, an optional uiMode.
RawKit parseRawKit(const std::vector<std::uint8_t>& bytes)
{
    LeReader r(bytes);
    RawKit kit;
    kit.version = r.i32();
    kit.maxPoly = r.i32();
    kit.steal = r.i32();
    const std::size_t n = kitSlotsOf(kit.version);
    for (auto& pad : kit.pads)
    {
        pad.exciter = r.i32();
        pad.body = r.i32();
        pad.sound.resize(n);
        for (double& v : pad.sound)
            v = r.f64();
        pad.choke = r.u8();
        pad.bus = r.u8();
    }
    kit.selected = r.i32();
    for (double& g : kit.globals)
        g = r.f64();
    for (double& a : kit.couplingAmount)
        a = r.f64();
    for (auto& m : kit.macros)
        for (double& v : m)
            v = r.f64();
    kit.masterGain = r.f64();
    if (r.remaining() >= 4)
    {
        kit.hasUiMode = true;
        kit.uiMode = r.i32();
    }
    CHECK(r.remaining() == 0U);
    return kit;
}

/// Writes `kit` in its own version's layout (little-endian, IBStreamer).
void writeRawKit(MemoryStream& stream, const RawKit& kit)
{
    IBStreamer s(&stream, kLittleEndian);
    s.writeInt32(kit.version);
    s.writeInt32(kit.maxPoly);
    s.writeInt32(kit.steal);
    for (const auto& pad : kit.pads)
    {
        s.writeInt32(pad.exciter);
        s.writeInt32(pad.body);
        for (double v : pad.sound)
            s.writeDouble(v);
        s.writeInt8u(pad.choke);
        s.writeInt8u(pad.bus);
    }
    s.writeInt32(kit.selected);
    for (double g : kit.globals)
        s.writeDouble(g);
    for (double a : kit.couplingAmount)
        s.writeDouble(a);
    for (const auto& m : kit.macros)
        for (double v : m)
            s.writeDouble(v);
    s.writeDouble(kit.masterGain);
    if (kit.hasUiMode)
        s.writeInt32(kit.uiMode);
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
}

/// A well-formed kit blob of `version`'s layout with distinct in-range values.
RawKit makeRawKit(int32 version)
{
    RawKit kit;
    kit.version = version;
    kit.maxPoly = 12;
    kit.steal = 1;
    kit.selected = 5;
    kit.globals = {0.25, 0.5, 0.75, 1.5};
    kit.masterGain = 0.6;
    kit.hasUiMode = true;
    kit.uiMode = 1;
    const std::size_t n = kitSlotsOf(version);
    for (std::size_t p = 0; p < kit.pads.size(); ++p)
    {
        auto& pad = kit.pads[p];
        pad.exciter = static_cast<int32>(p % 6U);
        pad.body = static_cast<int32>(p % 6U);
        pad.sound.resize(n);
        for (std::size_t i = 0; i < n; ++i)
            pad.sound[i] = std::clamp(0.05 + static_cast<double>(p) * 0.004
                                          + static_cast<double>(i) * 0.013, 0.0, 1.0);
        // Body damping: versions up to 5 stored absolute values (-1 = derive).
        pad.sound[42] = (version <= 5 && p % 2U == 0U) ? -1.0 : 0.3;
        pad.sound[43] = (version <= 5 && p % 2U == 0U) ? -1.0 : 0.2;
        pad.sound[28] = static_cast<double>(p % 9U);
        pad.sound[29] = static_cast<double>(p % 16U);
        pad.choke = static_cast<std::uint8_t>(p % 9U);
        pad.bus = static_cast<std::uint8_t>(p % 16U);
        kit.couplingAmount[p] = 0.5;
        kit.macros[p] = {0.5, 0.5, 0.5, 0.5, 0.5};
    }
    kit.macros[3] = {0.7, 0.3, 0.6, 0.4, 0.55};
    return kit;
}

std::vector<std::uint8_t> allBytes(IBStream& stream)
{
    int64 size = 0;
    stream.seek(0, IBStream::kIBSeekEnd, &size);
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    std::vector<std::uint8_t> out(static_cast<std::size_t>(size));
    int32 got = 0;
    if (size > 0)
        stream.read(out.data(), static_cast<int32>(size), &got);
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    return out;
}

/// The 59 sound slots a version-1 or version-2 pad must hold before the
/// version-5 migration runs (fix plan T2.8 step 2.1). Typed from the released
/// sources, not computed by the codec.
std::array<double, 59> legacyToV5(const std::vector<double>& raw, int32 version)
{
    std::array<double, 59> s{};
    // Slots the old version did not write: the value that makes today's
    // engine do what the old build did.
    s[51] = 1.0;  // enabled (a version-1 pad preset did not carry it)
    s[52] = 0.0;  // pitch-envelope knee off: one segment, as before Phase 10
    s[53] = 0.5;  // mid pitch (unused with the knee off)
    s[54] = 0.5;  // mid fraction (unused with the knee off)
    s[55] = 0.5;  // segment-2 curve: linear (unused with the knee off)
    s[56] = 0.5;  // pan centre: the old mono duplication
    s[57] = 1.0;  // noiseLayerGain: no multiplier existed
    s[58] = 0.0;  // wireCoupling 0: independent buzz (legacy)
    std::copy(raw.begin(), raw.end(), s.begin());
    if (version == 1)
    {
        // v0.9.x read the pitch-envelope curve as Exp (< 0.5) or Lin; 0.10.0
        // remapped its factory kits Exp -> 0.15, Lin -> 0.5 (CHANGELOG 0.10.0).
        s[14] = raw[14] < 0.5 ? 0.15 : 0.5;
        // v0.9.x decoded filter-envelope attack, decay and release linearly
        // (ms = n x max); 0.10.0 cubically (ms = n^3 x max).
        for (const std::size_t i : {std::size_t{15}, std::size_t{16}, std::size_t{18}})
            s[i] = std::cbrt(std::clamp(raw[i], 0.0, 1.0));
    }
    // v0.9.x and v0.10.x never forwarded FM Ratio, Feedback Amount or Friction
    // Pressure to the exciter: they played ratio 1.4 and velocity-only drive.
    s[30] = static_cast<double>((1.4f - 1.0f) / 3.0f);
    s[31] = 0.0;
    s[33] = 0.0;
    return s;
}

std::filesystem::path goldenDir()
{
    return std::filesystem::path(MEMBRUM_RESOURCES_DIR) / ".." / "tests" / "golden";
}

/// A released kit fixture loads with every stored field preserved: compared
/// against an independent parse of its bytes, and against the version-5 read
/// of the same values (which T1.22 pins).
void checkReleasedKitFixture(const char* fileName, int32 version)
{
    auto chunk = Membrum::TestHelpers::componentChunkFromFile(goldenDir() / fileName);
    REQUIRE(chunk != nullptr);
    const RawKit raw = parseRawKit(allBytes(*chunk));
    REQUIRE(raw.version == version);  // the fixture is what its name says
    const std::size_t n = kitSlotsOf(version);

    KitSnapshot got;
    REQUIRE(readKitBlob(chunk, got) == kResultOk);

    CHECK(got.maxPolyphony == std::clamp(static_cast<int>(raw.maxPoly), 4, 16));
    CHECK(got.voiceStealingPolicy == std::clamp(static_cast<int>(raw.steal), 0, 2));
    CHECK(got.selectedPadIndex == std::clamp(static_cast<int>(raw.selected), 0, 31));
    CHECK(got.globalCoupling == std::clamp(raw.globals[0], 0.0, 1.0));
    CHECK(got.snareBuzz == std::clamp(raw.globals[1], 0.0, 1.0));
    CHECK(got.tomResonance == std::clamp(raw.globals[2], 0.0, 1.0));
    CHECK(got.couplingDelayMs == std::clamp(raw.globals[3], 0.5, 2.0));
    CHECK(got.masterGainNorm == std::clamp(raw.masterGain, 0.0, 1.0));
    CHECK(got.hasSession == raw.hasUiMode);
    if (raw.hasUiMode)
        CHECK(got.uiMode == std::clamp(static_cast<int>(raw.uiMode), 0, 1));

    // Slots a migration rewrites: the converted ones (v1 / v2 -> v5), body
    // damping (v5 -> current) and, on a pad with a non-neutral macro, the
    // macro targets (v5 -> current). Every other stored slot reads as stored.
    const std::set<std::size_t> converted = version == 1
        ? std::set<std::size_t>{14, 15, 16, 18, 30, 31, 33}
        : std::set<std::size_t>{30, 31, 33};
    const std::set<std::size_t> macroTargets{0, 1, 2, 6, 11, 13, 19, 20, 21, 22, 32};

    for (std::size_t p = 0; p < raw.pads.size(); ++p)
    {
        INFO("pad " << p);
        const auto& rp = raw.pads[p];
        const auto& gp = got.pads[p];
        CHECK(static_cast<int32>(gp.exciterType) == rp.exciter);
        CHECK(static_cast<int32>(gp.bodyModel) == rp.body);
        CHECK(static_cast<unsigned>(gp.chokeGroup) == (rp.choke > 8U ? 0U : rp.choke));
        CHECK(static_cast<unsigned>(gp.outputBus) == (rp.bus > 15U ? 0U : rp.bus));
        CHECK(gp.couplingAmount == std::clamp(raw.couplingAmount[p], 0.0, 1.0));
        bool neutral = true;
        for (std::size_t j = 0; j < 5; ++j)
        {
            CHECK(gp.macros[j] == std::clamp(raw.macros[p][j], 0.0, 1.0));
            neutral = neutral && raw.macros[p][j] == 0.5;
        }

        const auto expected = legacyToV5(rp.sound, version);
        for (std::size_t i = 0; i < gp.sound.size(); ++i)
        {
            INFO("slot " << i);
            if (i == 42 || i == 43 || (!neutral && macroTargets.count(i) != 0))
                continue;
            if (i < n && converted.count(i) == 0)
                CHECK(gp.sound[i] == rp.sound[i]);
            else
                CHECK(gp.sound[i] == Approx(expected[i]).margin(1e-12));
        }
    }

    // The same values written as version 5 read to the same kit: body damping
    // and the macro bases go through the version-5 migration unchanged.
    RawKit asV5 = raw;
    asV5.version = 5;
    for (auto& pad : asV5.pads)
    {
        const auto s = legacyToV5(pad.sound, version);
        pad.sound.assign(s.begin(), s.end());
    }
    MemoryStream v5Stream;
    writeRawKit(v5Stream, asV5);
    KitSnapshot viaV5;
    REQUIRE(readKitBlob(&v5Stream, viaV5) == kResultOk);
    CHECK(kitsEquivalent(got, viaV5, 1e-12));
}

/// A pad-preset blob of `version`'s layout, written with IBStreamer
/// (little-endian): header, exciter, body, then the version's slots.
void writeRawPadBlob(MemoryStream& stream, int32 version, int32 exciter, int32 body,
                     const std::vector<double>& sound)
{
    IBStreamer s(&stream, kLittleEndian);
    s.writeInt32(version);
    s.writeInt32(exciter);
    s.writeInt32(body);
    for (double v : sound)
        s.writeDouble(v);
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
}

std::vector<double> makeRawPadSound(int32 version)
{
    std::vector<double> sound(padSlotsOf(version));
    for (std::size_t i = 0; i < sound.size(); ++i)
        sound[i] = std::clamp(0.02 + static_cast<double>(i) * 0.015, 0.0, 1.0);
    sound[14] = 0.0;   // version 1: "Exp"
    sound[42] = -1.0;  // absolute b1: derive
    sound[43] = 0.25;  // absolute b3
    return sound;
}

} // namespace

TEST_CASE("state_codec: kit blob round-trip preserves populated snapshot",
          "[state_codec][roundtrip]")
{
    const KitSnapshot src = makePopulatedKit();

    MemoryStream stream;
    REQUIRE(writeKitBlob(&stream, src) == kResultOk);
    stream.seek(0, IBStream::kIBSeekSet, nullptr);

    KitSnapshot dst;
    REQUIRE(readKitBlob(&stream, dst) == kResultOk);

    CHECK(dst.maxPolyphony        == src.maxPolyphony);
    CHECK(dst.voiceStealingPolicy == src.voiceStealingPolicy);
    CHECK(dst.selectedPadIndex    == src.selectedPadIndex);
    CHECK(dst.globalCoupling      == Approx(src.globalCoupling).margin(1e-12));
    CHECK(dst.snareBuzz           == Approx(src.snareBuzz).margin(1e-12));
    CHECK(dst.tomResonance        == Approx(src.tomResonance).margin(1e-12));
    CHECK(dst.couplingDelayMs     == Approx(src.couplingDelayMs).margin(1e-12));

    for (std::size_t p = 0; p < src.pads.size(); ++p)
    {
        INFO("pad " << p);
        CHECK(padsEquivalent(dst.pads[p], src.pads[p]));
    }

    // No session field was emitted by default.
    CHECK(!dst.hasSession);
    CHECK(dst.uiMode == 0);
}

TEST_CASE("state_codec: readKitBlob accepts the released versions 1, 2, 5 and the current one and rejects every other",
          "[state_codec][migration]")
{
    // Released builds wrote version 1 (membrum/v0.9.0, v0.9.1), 2
    // (membrum/v0.10.1, v0.10.2) and 5 (membrum/v0.12.0 - v0.12.5); the
    // current version is kBlobVersion. Each loads from a well-formed blob of
    // its own layout.
    for (const int32 version : {1, 2, 5, Membrum::State::kBlobVersion})
    {
        INFO("accepting version " << version);
        MemoryStream stream;
        writeRawKit(stream, makeRawKit(version));
        KitSnapshot dst;
        CHECK(readKitBlob(&stream, dst) == kResultOk);
    }

    // No release wrote 3 or 4, and nothing newer than the current version
    // exists: rejected, with the destination left as it was.
    for (const int32 version : {0, 3, 4, Membrum::State::kBlobVersion + 1, 99})
    {
        INFO("rejecting version " << version);
        RawKit raw = makeRawKit(Membrum::State::kBlobVersion);
        raw.version = version;
        MemoryStream stream;
        writeRawKit(stream, raw);
        const KitSnapshot before = makePopulatedKit();
        KitSnapshot dst = before;
        CHECK(readKitBlob(&stream, dst) == kResultFalse);
        CHECK(kitsEquivalent(dst, before));
    }
}

TEST_CASE("state_codec: a kit saved by v0.9.0 (blob version 1) loads with every stored field preserved",
          "[state_codec][migration]")
{
    // Acoustic Studio Kit as shipped in membrum/v0.9.0.
    checkReleasedKitFixture("kit_v1_v0.9.0_acoustic_studio.vstpreset", 1);
}

TEST_CASE("state_codec: a kit saved by v0.10.2 (blob version 2) loads with every stored field preserved",
          "[state_codec][migration]")
{
    // Acoustic Studio Kit as shipped in membrum/v0.10.2.
    checkReleasedKitFixture("kit_v2_v0.10.2_acoustic_studio.vstpreset", 2);
}

TEST_CASE("state_codec: pad presets of blob versions 1 and 2 load",
          "[state_codec][migration]")
{
    // No pad preset file was ever released; these blobs are written in the
    // layout v0.9.x (51 slots) and v0.10.x (56 slots) wrote them.
    for (const int32 version : {1, 2})
    {
        INFO("pad preset version " << version);
        const auto raw = makeRawPadSound(version);
        MemoryStream stream;
        writeRawPadBlob(stream, version, 4, 1, raw);
        CHECK(allBytes(stream).size() == 12U + raw.size() * 8U);

        PadPresetSnapshot got;
        REQUIRE(readPadPresetBlob(&stream, got) == kResultOk);
        CHECK(got.exciterType == Membrum::ExciterType::FMImpulse);
        CHECK(got.bodyModel == Membrum::BodyModelType::Plate);

        const auto expected = legacyToV5(raw, version);
        const std::set<std::size_t> converted = version == 1
            ? std::set<std::size_t>{14, 15, 16, 18, 30, 31, 33}
            : std::set<std::size_t>{30, 31, 33};
        for (std::size_t i = 0; i < got.sound.size(); ++i)
        {
            INFO("slot " << i);
            if (i == 42 || i == 43)
                continue;
            if (i < raw.size() && converted.count(i) == 0)
                CHECK(got.sound[i] == raw[i]);
            else
                CHECK(got.sound[i] == Approx(expected[i]).margin(1e-12));
        }

        // Body damping goes through the version-5 migration unchanged.
        MemoryStream v5;
        writeRawPadBlob(v5, 5, 4, 1, std::vector<double>(expected.begin(), expected.end()));
        PadPresetSnapshot viaV5;
        REQUIRE(readPadPresetBlob(&v5, viaV5) == kResultOk);
        CHECK(got.sound == viaV5.sound);
    }
}

// Audit finding 13: the codec must serialise multibyte values in an explicit,
// host-endianness-independent byte order (little-endian, via IBStreamer),
// matching the rest of the monorepo. This guards the on-wire layout so a
// big-endian host would read back the same values a little-endian host wrote.
TEST_CASE("state_codec: kit blob header is little-endian on the wire",
          "[state_codec][endian]")
{
    KitSnapshot src = makePopulatedKit();
    src.maxPolyphony = 12;  // 0x0000000C

    MemoryStream stream;
    REQUIRE(writeKitBlob(&stream, src) == kResultOk);

    const auto* bytes = reinterpret_cast<const std::uint8_t*>(stream.getData());
    REQUIRE(stream.getSize() >= 8);

    // Offset 0: int32 version == kBlobVersion, little-endian.
    CHECK(bytes[0] == static_cast<std::uint8_t>(Membrum::State::kBlobVersion));
    CHECK(bytes[1] == 0x00);
    CHECK(bytes[2] == 0x00);
    CHECK(bytes[3] == 0x00);

    // Offset 4: int32 maxPolyphony == 12, little-endian.
    CHECK(bytes[4] == 0x0C);
    CHECK(bytes[5] == 0x00);
    CHECK(bytes[6] == 0x00);
    CHECK(bytes[7] == 0x00);
}

TEST_CASE("state_codec: uiMode is optional on read, round-trips when written",
          "[state_codec][session]")
{
    SECTION("written -> read: hasSession=true preserves uiMode")
    {
        KitSnapshot src = makePopulatedKit();
        src.hasSession = true;
        src.uiMode     = 1;

        MemoryStream stream;
        REQUIRE(writeKitBlob(&stream, src) == kResultOk);
        stream.seek(0, IBStream::kIBSeekSet, nullptr);

        KitSnapshot dst;
        REQUIRE(readKitBlob(&stream, dst) == kResultOk);
        CHECK(dst.hasSession);
        CHECK(dst.uiMode == 1);
    }

    SECTION("absent on wire -> hasSession=false, uiMode unchanged")
    {
        KitSnapshot src = makePopulatedKit();
        src.hasSession = false;

        MemoryStream stream;
        REQUIRE(writeKitBlob(&stream, src) == kResultOk);
        stream.seek(0, IBStream::kIBSeekSet, nullptr);

        KitSnapshot dst;
        dst.uiMode = 0;
        REQUIRE(readKitBlob(&stream, dst) == kResultOk);
        CHECK(!dst.hasSession);
        CHECK(dst.uiMode == 0);
    }

    SECTION("hasSession adds exactly 4 bytes (one int32) to the blob")
    {
        KitSnapshot src = makePopulatedKit();

        MemoryStream streamNoSession;
        src.hasSession = false;
        REQUIRE(writeKitBlob(&streamNoSession, src) == kResultOk);
        Steinberg::int64 sizeNoSession = 0;
        streamNoSession.tell(&sizeNoSession);

        MemoryStream streamWithSession;
        src.hasSession = true;
        src.uiMode     = 1;
        REQUIRE(writeKitBlob(&streamWithSession, src) == kResultOk);
        Steinberg::int64 sizeWithSession = 0;
        streamWithSession.tell(&sizeWithSession);

        CHECK(sizeWithSession == sizeNoSession + 4);
    }
}

TEST_CASE("state_codec: per-pad preset round-trip",
          "[state_codec][pad_preset]")
{
    PadPresetSnapshot src;
    src.exciterType = Membrum::ExciterType::NoiseBurst;
    src.bodyModel   = Membrum::BodyModelType::Plate;
    for (std::size_t i = 0; i < src.sound.size(); ++i)
        src.sound[i] = 0.01 + static_cast<double>(i) * 0.015;

    MemoryStream stream;
    REQUIRE(writePadPresetBlob(&stream, src) == kResultOk);

    // Confirm the exact size: 4 (version) + 4 (exciter) + 4 (body) + 59*8 = 484.
    int64 bytes = 0;
    stream.seek(0, IBStream::kIBSeekEnd, &bytes);
    CHECK(bytes == 484);

    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    PadPresetSnapshot dst;
    REQUIRE(readPadPresetBlob(&stream, dst) == kResultOk);
    CHECK(dst.exciterType == src.exciterType);
    CHECK(dst.bodyModel   == src.bodyModel);
    for (std::size_t i = 0; i < src.sound.size(); ++i)
    {
        INFO("sound[" << i << "]");
        CHECK(dst.sound[i] == Approx(src.sound[i]).margin(1e-12));
    }
}

TEST_CASE("state_codec: readPadPresetBlob accepts the released versions 1, 2, 5 and the current one and rejects every other",
          "[state_codec][pad_preset][migration]")
{
    // Released builds wrote version 1 (membrum/v0.9.0, v0.9.1), 2
    // (membrum/v0.10.1, v0.10.2) and 5 (membrum/v0.12.0 - v0.12.5).
    for (const int32 version : {1, 2, 5, Membrum::State::kPadBlobVersion})
    {
        INFO("accepting pad-preset version " << version);
        MemoryStream stream;
        writeRawPadBlob(stream, version, 0, 0, makeRawPadSound(version));
        PadPresetSnapshot dst;
        CHECK(readPadPresetBlob(&stream, dst) == kResultOk);
    }

    for (const int32 version : {0, 3, 4, Membrum::State::kPadBlobVersion + 1, 99})
    {
        INFO("rejecting pad-preset version " << version);
        MemoryStream stream;
        writeRawPadBlob(stream, version, 0, 0, makeRawPadSound(Membrum::State::kPadBlobVersion));
        PadPresetSnapshot before;
        before.exciterType = Membrum::ExciterType::Clap;
        before.bodyModel = Membrum::BodyModelType::Bell;
        before.sound.fill(0.375);
        PadPresetSnapshot dst = before;
        CHECK(readPadPresetBlob(&stream, dst) == kResultFalse);
        CHECK(dst.exciterType == before.exciterType);
        CHECK(dst.bodyModel == before.bodyModel);
        CHECK(dst.sound == before.sound);
    }
}

TEST_CASE("state_codec: toPadSnapshot/applyPadSnapshot are inverses",
          "[state_codec][pad_snapshot]")
{
    Membrum::PadConfig src;
    src.exciterType        = Membrum::ExciterType::Friction;
    src.bodyModel          = Membrum::BodyModelType::Bell;
    src.material           = 0.123f;
    src.size               = 0.456f;
    src.decay              = 0.789f;
    src.strikePosition     = 0.321f;
    src.level              = 0.654f;
    src.tsFilterType       = 0.5f;
    src.tsFilterCutoff     = 0.8f;
    src.tsFilterResonance  = 0.2f;
    src.tsFilterEnvAmount  = 0.7f;
    src.tsDriveAmount      = 0.15f;
    src.tsFoldAmount       = 0.35f;
    src.tsPitchEnvStart    = 0.4f;
    src.tsPitchEnvEnd      = 0.6f;
    src.tsPitchEnvTime     = 0.25f;
    src.tsPitchEnvCurve    = 0.0f;
    src.tsFilterEnvAttack  = 0.1f;
    src.tsFilterEnvDecay   = 0.2f;
    src.tsFilterEnvSustain = 0.3f;
    src.tsFilterEnvRelease = 0.4f;
    src.modeStretch        = 0.5f;
    src.decaySkew          = 0.6f;
    src.modeInjectAmount   = 0.15f;
    src.nonlinearCoupling  = 0.25f;
    src.morphEnabled       = 1.0f;
    src.morphStart         = 0.2f;
    src.morphEnd           = 0.8f;
    src.morphDuration      = 0.33f;
    src.morphCurve         = 1.0f;
    src.chokeGroup         = 3;
    src.outputBus          = 5;
    src.fmRatio            = 0.4f;
    src.feedbackAmount     = 0.5f;
    src.noiseBurstDuration = 0.6f;
    src.frictionPressure   = 0.7f;
    src.couplingAmount     = 0.85f;
    src.macroTightness     = 0.11f;
    src.macroBrightness    = 0.22f;
    src.macroBodySize      = 0.33f;
    src.macroPunch         = 0.44f;
    src.macroComplexity    = 0.55f;

    const PadSnapshot snap = toPadSnapshot(src);
    Membrum::PadConfig dst;
    applyPadSnapshot(snap, dst);

    CHECK(dst.exciterType        == src.exciterType);
    CHECK(dst.bodyModel          == src.bodyModel);
    CHECK(dst.material           == Approx(src.material).margin(1e-6f));
    CHECK(dst.size               == Approx(src.size).margin(1e-6f));
    CHECK(dst.decay              == Approx(src.decay).margin(1e-6f));
    CHECK(dst.strikePosition     == Approx(src.strikePosition).margin(1e-6f));
    CHECK(dst.level              == Approx(src.level).margin(1e-6f));
    CHECK(dst.tsFilterCutoff     == Approx(src.tsFilterCutoff).margin(1e-6f));
    CHECK(dst.modeStretch        == Approx(src.modeStretch).margin(1e-6f));
    CHECK(dst.morphEnabled       == Approx(src.morphEnabled).margin(1e-6f));
    CHECK(dst.chokeGroup         == src.chokeGroup);
    CHECK(dst.outputBus          == src.outputBus);
    CHECK(dst.fmRatio            == Approx(src.fmRatio).margin(1e-6f));
    CHECK(dst.frictionPressure   == Approx(src.frictionPressure).margin(1e-6f));
    CHECK(dst.couplingAmount     == Approx(src.couplingAmount).margin(1e-6f));
    CHECK(dst.macroTightness     == Approx(src.macroTightness).margin(1e-6f));
    CHECK(dst.macroComplexity    == Approx(src.macroComplexity).margin(1e-6f));
}

TEST_CASE("state_codec: applyPadPresetSnapshot does NOT modify choke/bus/coupling/macros",
          "[state_codec][pad_preset][fr061]")
{
    Membrum::PadConfig existing;
    existing.chokeGroup      = 7;
    existing.outputBus       = 4;
    existing.couplingAmount  = 0.91f;
    existing.macroTightness  = 0.13f;
    existing.macroBrightness = 0.87f;

    PadPresetSnapshot snap;
    snap.exciterType = Membrum::ExciterType::Impulse;
    snap.bodyModel   = Membrum::BodyModelType::Membrane;
    // Deliberately populate sound[28]/[29] with values that, if applied, would
    // overwrite the existing choke/bus. They must be ignored.
    for (std::size_t i = 0; i < snap.sound.size(); ++i)
        snap.sound[i] = 0.5;
    snap.sound[28] = 0.0;  // would map to chokeGroup 0 if applied
    snap.sound[29] = 0.0;  // would map to outputBus 0 if applied

    applyPadPresetSnapshot(snap, existing);

    CHECK(existing.chokeGroup      == 7);
    CHECK(existing.outputBus       == 4);
    CHECK(existing.couplingAmount  == Approx(0.91f).margin(1e-6f));
    CHECK(existing.macroTightness  == Approx(0.13f).margin(1e-6f));
    CHECK(existing.macroBrightness == Approx(0.87f).margin(1e-6f));
    // Sound fields should have been overwritten.
    CHECK(existing.material == Approx(0.5f).margin(1e-6f));
    CHECK(existing.level    == Approx(0.5f).margin(1e-6f));
}
