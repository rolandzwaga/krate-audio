// ==============================================================================
// Layer 3: System Tests - BassMorphEngine, non-finite TU (-fno-fast-math)
//                          (Profundum Phase 8, specs/profundum-phase8-bass-morph)
// ==============================================================================
// Holds: SC-012, FR-081 [nonfinite]; the non-finite rows of FR-005 / FR-006 / FR-013 / FR-021 / FR-062;
//        plan R-5, S1 C-7. Filled by T006 (T001 registered this TU in the -fno-fast-math block of
//        dsp/tests/CMakeLists.txt).
//
// SCOPE (tags [systems][profundum][bass-morph][nonfinite][sc-012]):
//   BassMorph_NonFinite  SECTIONs position, ramp, endpoints, validation, state bytes, runtime bytes.
//
// Every special value is built from its bit pattern through a volatile std::uint32_t and std::bit_cast<float>,
// never from std::numeric_limits or arithmetic; finiteness is read only through detail::isFinite; value
// preservation is checked bitwise (engine twins in one process, tasks.md rule 7). Engine-only: no render, no
// FTZ / DAZ (tasks.md rule 9, plan R-7). TU-local names carry a `bm` prefix (tasks.md rule 5).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/systems/morph_state.h>
#include <krate/dsp/systems/bass_morph_engine.h>

#include "bass_morph_state_helpers.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ios>
#include <type_traits>

// Qualified calls into the helper namespace (tasks.md rule 5); no using-directive.
namespace BassMorphTest = Krate::DSP::BassMorphTest;

namespace {

using Krate::DSP::BassMorphEngine;
using Krate::DSP::MorphState;
using Krate::DSP::detail::isFinite;
namespace io = Krate::DSP::detail::morph_state_io;

using BmEndpoint = BassMorphEngine::Endpoint;
using BmMode = BassMorphEngine::AnchorMorphMode;
using BmResolved = BassMorphEngine::Resolved;
using BmRuntimeBlob = std::array<std::byte, BassMorphEngine::kMorphRuntimeBytes>;
using BmStateBlob = std::array<std::byte, Krate::DSP::kMorphStateBytes>;

constexpr double bmFs = 48000.0;
constexpr int bmTwinIntervals = 64;
constexpr std::array<BmMode, 3> bmModes{BmMode::Lock, BmMode::Blend, BmMode::Punch};

// ---- special values (tasks.md T006) ----
constexpr std::uint32_t bmQuietNaN = 0x7FC00000u;
constexpr std::uint32_t bmSignallingNaN = 0x7FA00000u;
constexpr std::uint32_t bmNegNaN = 0xFFC00000u;
constexpr std::uint32_t bmPosInf = 0x7F800000u;
constexpr std::uint32_t bmNegInf = 0xFF800000u;
constexpr std::uint32_t bmPosDenormal = 0x00000001u;
constexpr std::uint32_t bmNegDenormal = 0x80000001u;
constexpr std::uint32_t bmNegZero = 0x80000000u;

/// NaN (quiet, signalling, negative) and +-Inf.
constexpr std::array<std::uint32_t, 5> bmNonFiniteBits{bmQuietNaN, bmSignallingNaN, bmNegNaN, bmPosInf, bmNegInf};
/// The values setPosition maps to +0 (plan S1 C-7).
constexpr std::array<std::uint32_t, 3> bmToZeroBits{bmPosDenormal, bmNegDenormal, bmNegZero};

/// A float from its bit pattern, through a volatile so no compiler folding can see it.
float bmFromBits(std::uint32_t bits) noexcept {
    volatile std::uint32_t v = bits;
    return std::bit_cast<float>(static_cast<std::uint32_t>(v));
}

std::uint32_t bmBits(float x) noexcept { return std::bit_cast<std::uint32_t>(x); }

/// std::nextafter(x, -inf) for finite x, computed on the bit pattern so a flush-to-zero mode can never turn the
/// step below +0 (0x80000001, the negative denormal) into -0.
float bmStepDown(float x) noexcept {
    const std::uint32_t u = bmBits(x);
    if (u == 0u) {
        return bmFromBits(bmNegDenormal);   // +0 -> -denormal
    }
    if ((u & 0x80000000u) != 0u) {
        return bmFromBits(u + 1u);          // negative (incl. -0): larger magnitude
    }
    return bmFromBits(u - 1u);              // positive: smaller magnitude
}

/// std::nextafter(x, +inf) for finite x, on the bit pattern.
float bmStepUp(float x) noexcept {
    const std::uint32_t u = bmBits(x);
    if (u == bmNegZero) {
        return bmFromBits(bmPosDenormal);   // -0 -> +denormal
    }
    if ((u & 0x80000000u) != 0u) {
        return bmFromBits(u - 1u);          // negative: smaller magnitude
    }
    return bmFromBits(u + 1u);              // positive (incl. +0): larger magnitude
}

/// @p s with F32 leaf @p index set to @p v (the caller passes an F32 row).
MorphState bmWithFloatLeaf(MorphState s, std::size_t index, float v) {
    io::forEachMorphLeaf(
        [&]([[maybe_unused]] auto leaf, auto& x) {
            using T = std::remove_cvref_t<decltype(x)>;
            if constexpr (std::is_same_v<T, float>) {
                if (decltype(leaf)::value == index) {
                    x = v;
                }
            }
        },
        s);
    return s;
}

/// @p s with Enum8 leaf @p index set to the raw byte @p byte (the caller passes an Enum8 row).
MorphState bmWithEnumByte(MorphState s, std::size_t index, std::uint8_t byte) {
    io::forEachMorphLeaf(
        [&]([[maybe_unused]] auto leaf, auto& x) {
            using T = std::remove_cvref_t<decltype(x)>;
            if constexpr (std::is_enum_v<T>) {
                if (decltype(leaf)::value == index) {
                    x = static_cast<T>(byte);
                }
            }
        },
        s);
    return s;
}

/// The invalid values of one F32 row (tasks.md T006 "endpoints"): NaN (three patterns), +Inf, -Inf,
/// nextafter(lo, -inf), nextafter(hi, +inf), and for the half-open phase row also hi itself.
struct BmBadFloats {
    std::array<float, 8> values{};
    std::size_t count = 0;
};

BmBadFloats bmBadFloatsFor(const MorphState::FieldInfo& row) {
    BmBadFloats bad{};
    for (const std::uint32_t bits : bmNonFiniteBits) {
        bad.values[bad.count++] = bmFromBits(bits);
    }
    bad.values[bad.count++] = bmStepDown(row.lo);
    bad.values[bad.count++] = bmStepUp(row.hi);
    if (row.hiOpen) {
        bad.values[bad.count++] = row.hi;   // [0, 360): 360 is outside
    }
    return bad;
}

/// SC-012 / FR-081: every float of resolved() finite, the resolved state valid.
void bmCheckResolvedSane(const BassMorphEngine& engine) {
    const BmResolved& r = engine.resolved();
    bool finite = true;
    io::forEachMorphLeaf(
        [&](auto, const auto& v) {
            using T = std::remove_cvref_t<decltype(v)>;
            if constexpr (std::is_same_v<T, float>) {
                finite = finite && isFinite(v);
            }
        },
        r.state);
    CHECK(finite);
    CHECK(isFinite(r.position));
    CHECK(isFinite(r.anchorPosition));
    CHECK(isFinite(r.anchorMass));
    CHECK(Krate::DSP::isValidMorphState(r.state));
}

/// Showcase pair, mode @p m, landed at @p p (prepare() ends with reset(), plan S1 C-5, which snaps p to its target).
BassMorphEngine bmLandedEngine(BmMode m, float p) {
    const auto [a, b] = BassMorphTest::showcasePair();
    BassMorphEngine engine;
    REQUIRE(engine.setEndpoint(BmEndpoint::A, a));
    REQUIRE(engine.setEndpoint(BmEndpoint::B, b));
    engine.setAnchorMorphMode(m);
    engine.setPosition(p);
    engine.prepare(bmFs);
    for (int i = 0; i < 4; ++i) {
        static_cast<void>(engine.advanceInterval());
    }
    REQUIRE(bmBits(engine.position()) == bmBits(p));
    return engine;
}

// Plan S4.8 runtime layout, version 1 (mirrors the private offsets at bass_morph_engine.h:339-346).
constexpr std::size_t bmRtTarget = 1;
constexpr std::size_t bmRtPosition = 5;
constexpr std::size_t bmRtAnchorPosition = 9;
constexpr std::size_t bmRtRamp = 14;

void bmPutBits(BmRuntimeBlob& blob, std::size_t offset, std::uint32_t bits) {
    std::memcpy(blob.data() + offset, &bits, sizeof(bits));
}

BmRuntimeBlob bmRuntimeBytes(const BassMorphEngine& engine) {
    BmRuntimeBlob blob{};
    REQUIRE(engine.serializeRuntime(blob.data(), blob.size()) == BassMorphEngine::kMorphRuntimeBytes);
    return blob;
}

}  // namespace

// SC-012 (FR-081; non-finite rows of FR-005, FR-006, FR-013, FR-021, FR-062; plan R-5, S1 C-7).
TEST_CASE("BassMorph_NonFinite", "[systems][profundum][bass-morph][nonfinite][sc-012]") {
    SECTION("position") {
        // FR-013: NaN / +-Inf keep the previous target; plan S1 C-7: +-denormal and -0 become +0.
        for (const BmMode m : bmModes) {
            const BassMorphEngine landed = bmLandedEngine(m, 0.4f);
            REQUIRE(bmBits(landed.targetPosition()) == bmBits(0.4f));

            for (const std::uint32_t bits : bmNonFiniteBits) {
                INFO("mode " << static_cast<int>(m) << " bits 0x" << std::hex << bits << std::dec);
                BassMorphEngine subject = landed;
                BassMorphEngine twin = landed;
                subject.setPosition(bmFromBits(bits));
                CHECK(bmBits(subject.targetPosition()) == bmBits(0.4f));
                bmCheckResolvedSane(subject);
                for (int i = 0; i < bmTwinIntervals; ++i) {
                    const BmResolved& rs = subject.advanceInterval();
                    const BmResolved& rt = twin.advanceInterval();
                    CHECK(BassMorphTest::bitEqual(rs, rt));
                    bmCheckResolvedSane(subject);
                }
                CHECK(bmBits(subject.targetPosition()) == bmBits(0.4f));
            }

            for (const std::uint32_t bits : bmToZeroBits) {
                INFO("mode " << static_cast<int>(m) << " bits 0x" << std::hex << bits << std::dec);
                BassMorphEngine subject = landed;
                BassMorphEngine twin = landed;
                subject.setPosition(bmFromBits(bits));
                twin.setPosition(0.0f);
                CHECK(bmBits(subject.targetPosition()) == 0u);   // +0 bitwise (plan S1 C-7)
                for (int i = 0; i < bmTwinIntervals; ++i) {
                    const BmResolved& rs = subject.advanceInterval();
                    const BmResolved& rt = twin.advanceInterval();
                    CHECK(BassMorphTest::bitEqual(rs, rt));
                    bmCheckResolvedSane(subject);
                }
            }
        }
    }

    SECTION("ramp") {
        // FR-021: a non-finite ramp time keeps the previous value; the ramp in flight is untouched.
        BassMorphEngine base = bmLandedEngine(BmMode::Blend, 0.0f);
        base.setPositionRampMs(333.0f);
        base.setPosition(1.0f);   // ramping 0 -> 1 while the bad value arrives
        static_cast<void>(base.advanceInterval());
        const std::uint32_t rampBits = bmBits(base.positionRampMs());
        REQUIRE(rampBits == bmBits(333.0f));

        for (const std::uint32_t bits : bmNonFiniteBits) {
            INFO("bits 0x" << std::hex << bits << std::dec);
            BassMorphEngine subject = base;
            BassMorphEngine twin = base;
            subject.setPositionRampMs(bmFromBits(bits));
            CHECK(bmBits(subject.positionRampMs()) == rampBits);
            for (int i = 0; i < bmTwinIntervals; ++i) {
                const BmResolved& rs = subject.advanceInterval();
                const BmResolved& rt = twin.advanceInterval();
                CHECK(BassMorphTest::bitEqual(rs, rt));
                bmCheckResolvedSane(subject);
            }
        }

        // Before prepare() as well (the rate is configured only when prepared).
        BassMorphEngine unprepared;
        unprepared.setPositionRampMs(333.0f);
        for (const std::uint32_t bits : bmNonFiniteBits) {
            INFO("unprepared bits 0x" << std::hex << bits << std::dec);
            unprepared.setPositionRampMs(bmFromBits(bits));
            CHECK(bmBits(unprepared.positionRampMs()) == rampBits);
        }
    }

    SECTION("endpoints") {
        // FR-012 / FR-005: an invalid endpoint is rejected and the stored one is unchanged.
        BassMorphEngine engine = bmLandedEngine(BmMode::Blend, 0.5f);
        BassMorphEngine twin = engine;
        std::size_t rejectedCalls = 0;
        for (const BmEndpoint e : {BmEndpoint::A, BmEndpoint::B}) {
            const MorphState before = engine.endpoint(e);
            for (std::size_t i = 0; i < Krate::DSP::kMorphLeafFieldCount; ++i) {
                const MorphState::FieldInfo& row = io::kFieldTable[i];
                if (row.kind == MorphState::Kind::F32) {
                    const BmBadFloats bad = bmBadFloatsFor(row);
                    for (std::size_t k = 0; k < bad.count; ++k) {
                        INFO("endpoint " << static_cast<int>(e) << " leaf " << row.name << " bits 0x" << std::hex
                                         << bmBits(bad.values[k]) << std::dec);
                        const MorphState candidate = bmWithFloatLeaf(before, i, bad.values[k]);
                        CHECK_FALSE(engine.setEndpoint(e, candidate));
                        CHECK(io::leafBitsEqual(engine.endpoint(e), before));
                        ++rejectedCalls;
                        CHECK(BassMorphTest::bitEqual(engine.advanceInterval(), twin.advanceInterval()));
                        bmCheckResolvedSane(engine);
                    }
                } else if (row.kind == MorphState::Kind::Enum8) {
                    INFO("endpoint " << static_cast<int>(e) << " leaf " << row.name << " byte maxEnum + 1");
                    const MorphState candidate =
                        bmWithEnumByte(before, i, static_cast<std::uint8_t>(row.maxEnum + 1u));
                    CHECK_FALSE(engine.setEndpoint(e, candidate));
                    CHECK(io::leafBitsEqual(engine.endpoint(e), before));
                    ++rejectedCalls;
                    CHECK(BassMorphTest::bitEqual(engine.advanceInterval(), twin.advanceInterval()));
                    bmCheckResolvedSane(engine);
                }
            }
        }
        CHECK(rejectedCalls > 0);
    }

    SECTION("validation") {
        // FR-005: every non-finite, out-of-box and undeclared value is invalid; denormals are finite values judged
        // by their box (orderedFloatKey, morph_state.h:502-506, so the verdict does not depend on DAZ).
        const MorphState base{};
        REQUIRE(Krate::DSP::isValidMorphState(base));
        for (std::size_t i = 0; i < Krate::DSP::kMorphLeafFieldCount; ++i) {
            const MorphState::FieldInfo& row = io::kFieldTable[i];
            if (row.kind == MorphState::Kind::F32) {
                const BmBadFloats bad = bmBadFloatsFor(row);
                for (std::size_t k = 0; k < bad.count; ++k) {
                    INFO("leaf " << row.name << " bits 0x" << std::hex << bmBits(bad.values[k]) << std::dec);
                    CHECK_FALSE(Krate::DSP::isValidMorphState(bmWithFloatLeaf(base, i, bad.values[k])));
                }
                // +denormal is inside iff lo <= 0 < hi; -denormal is inside iff lo < 0 <= hi (i.e. false in
                // [0, x] boxes, true in [-1, x] boxes).
                const bool posInside = row.lo <= 0.0f && row.hi > 0.0f;
                const bool negInside = row.lo < 0.0f && row.hi >= 0.0f;
                {
                    INFO("leaf " << row.name << " +denormal, lo " << row.lo << " hi " << row.hi);
                    CHECK(Krate::DSP::isValidMorphState(bmWithFloatLeaf(base, i, bmFromBits(bmPosDenormal)))
                          == posInside);
                }
                {
                    INFO("leaf " << row.name << " -denormal, lo " << row.lo << " hi " << row.hi);
                    CHECK(Krate::DSP::isValidMorphState(bmWithFloatLeaf(base, i, bmFromBits(bmNegDenormal)))
                          == negInside);
                }
            } else if (row.kind == MorphState::Kind::Enum8) {
                INFO("leaf " << row.name << " byte maxEnum + 1");
                CHECK_FALSE(Krate::DSP::isValidMorphState(
                    bmWithEnumByte(base, i, static_cast<std::uint8_t>(row.maxEnum + 1u))));
            }
        }
    }

    SECTION("state bytes") {
        // FR-006: a stream carrying a non-finite float is rejected and `out` is untouched; serialising an invalid
        // in-memory state returns 0 and writes nothing.
        const auto [a, b] = BassMorphTest::showcasePair();
        const BmStateBlob valid = BassMorphTest::serializedBytes(a);
        {
            MorphState control = b;
            REQUIRE(Krate::DSP::deserializeMorphState(valid.data(), valid.size(), control));   // positive control
            REQUIRE(io::leafBitsEqual(control, a));
        }

        for (std::size_t i = 0; i < Krate::DSP::kMorphLeafFieldCount; ++i) {
            const MorphState::FieldInfo& row = io::kFieldTable[i];
            if (row.kind != MorphState::Kind::F32) {
                continue;
            }
            for (const std::uint32_t bits : bmNonFiniteBits) {
                INFO("leaf " << row.name << " stream bits 0x" << std::hex << bits << std::dec);
                BmStateBlob stream = valid;
                std::memcpy(stream.data() + io::kLeafOffsets[i], &bits, sizeof(bits));
                MorphState out = b;
                CHECK_FALSE(Krate::DSP::deserializeMorphState(stream.data(), stream.size(), out));
                CHECK(io::leafBitsEqual(out, b));
            }

            const BmBadFloats bad = bmBadFloatsFor(row);
            for (std::size_t k = 0; k < bad.count; ++k) {
                INFO("leaf " << row.name << " in-memory bits 0x" << std::hex << bmBits(bad.values[k]) << std::dec);
                const MorphState invalid = bmWithFloatLeaf(a, i, bad.values[k]);
                BmStateBlob dest{};
                dest.fill(std::byte{0xA5});
                const BmStateBlob untouched = dest;
                CHECK(Krate::DSP::serializeMorphState(invalid, dest.data(), dest.size()) == 0u);
                CHECK(dest == untouched);
            }
        }
    }

    SECTION("runtime bytes") {
        // FR-062 / plan S1 C-7: a non-finite, sub-threshold or denormal position, or a non-finite ramp, is
        // rejected and the engine changes nothing.
        BassMorphEngine source = bmLandedEngine(BmMode::Blend, 0.6f);
        source.setPositionRampMs(333.0f);
        source.setPosition(0.2f);
        for (int i = 0; i < 5; ++i) {
            static_cast<void>(source.advanceInterval());   // mid-ramp: target, p and p_a all differ
        }
        const BmRuntimeBlob valid = bmRuntimeBytes(source);

        BassMorphEngine receiver = bmLandedEngine(BmMode::Punch, 0.9f);   // same endpoints, other runtime state
        receiver.setPositionRampMs(100.0f);
        const BmRuntimeBlob receiverBytes = bmRuntimeBytes(receiver);
        REQUIRE_FALSE(receiverBytes == valid);
        {
            BassMorphEngine control = receiver;
            REQUIRE(control.restoreRuntime(valid.data(), valid.size()));   // positive control
            REQUIRE(bmRuntimeBytes(control) == valid);
        }
        const BmResolved before = receiver.resolved();

        const std::array<std::uint32_t, 10> badPositions{
            bmQuietNaN, bmSignallingNaN, bmNegNaN, bmPosInf, bmNegInf,
            bmBits(1e-20f), bmBits(1e-16f), bmBits(std::nextafter(Krate::DSP::kDenormalThreshold, 0.0f)),
            bmPosDenormal, bmNegDenormal};
        for (const std::size_t offset : {bmRtTarget, bmRtPosition, bmRtAnchorPosition}) {
            for (const std::uint32_t bits : badPositions) {
                INFO("position offset " << offset << " bits 0x" << std::hex << bits << std::dec);
                BmRuntimeBlob blob = valid;
                bmPutBits(blob, offset, bits);
                CHECK_FALSE(receiver.restoreRuntime(blob.data(), blob.size()));
                CHECK(BassMorphTest::bitEqual(receiver.resolved(), before));
                CHECK(bmRuntimeBytes(receiver) == receiverBytes);
                bmCheckResolvedSane(receiver);
            }
        }
        for (const std::uint32_t bits : bmNonFiniteBits) {
            INFO("ramp bits 0x" << std::hex << bits << std::dec);
            BmRuntimeBlob blob = valid;
            bmPutBits(blob, bmRtRamp, bits);
            CHECK_FALSE(receiver.restoreRuntime(blob.data(), blob.size()));
            CHECK(BassMorphTest::bitEqual(receiver.resolved(), before));
            CHECK(bmRuntimeBytes(receiver) == receiverBytes);
            bmCheckResolvedSane(receiver);
        }
    }
}
