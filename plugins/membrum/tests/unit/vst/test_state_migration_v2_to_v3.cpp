// ==============================================================================
// State migration test: v2 -> v3 (T3.4.2 / FR-142 / FR-183 / SC-025)
// ==============================================================================
// Loads the committed Phase 2 fixture `tests/golden/phase2_state_v2.bin` (268
// bytes, version==2) into a Phase 3 processor and verifies:
//   (a) setState() returns kResultOk
//   (b) all Phase 2 params are preserved bit-exactly (the committed fixture's
//       field-level content is re-verified against the originating byte blob)
//   (c) Phase 3 fields take documented defaults:
//         maxPolyphony          == 8
//         voiceStealingPolicy   == 0 (Oldest)
//         chokeGroupAssignments == all zero
//   (d) a subsequent getState() produces exactly 302 bytes, with the v2 body
//       intact and the Phase 3 defaults appended.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"

#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

namespace {

constexpr int kPhase2FloatCount = 27;
constexpr int64 kV2BodyBytes    = 268;
constexpr int64 kV3BlobBytes    = 302;

ProcessSetup makeSetup(double sampleRate = 44100.0)
{
    ProcessSetup setup{};
    setup.processMode         = kRealtime;
    setup.symbolicSampleSize  = kSample32;
    setup.maxSamplesPerBlock  = 512;
    setup.sampleRate          = sampleRate;
    return setup;
}

// Candidate paths for the committed v2 fixture. Test runner cwd varies by
// generator; try a short list of plausible repo-relative spots before giving
// up.
std::vector<std::filesystem::path> candidateFixturePaths()
{
    namespace fs = std::filesystem;
    const char* rel = "plugins/membrum/tests/golden/phase2_state_v2.bin";
    return {
        fs::path("F:/projects/iterum") / rel,
        fs::current_path() / rel,
        fs::current_path().parent_path() / rel,
        fs::current_path().parent_path().parent_path() / rel,
        fs::current_path().parent_path().parent_path().parent_path() / rel,
        fs::current_path().parent_path().parent_path().parent_path().parent_path() / rel,
    };
}

std::vector<std::uint8_t> loadFixture()
{
    for (const auto& p : candidateFixturePaths())
    {
        std::error_code ec;
        if (!std::filesystem::exists(p, ec))
            continue;
        std::ifstream in(p, std::ios::binary);
        if (!in)
            continue;
        std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)),
                                        std::istreambuf_iterator<char>());
        return data;
    }
    return {};
}

} // namespace

TEST_CASE("State v2 StateMigration v2->v3: fixture loads into Phase 3 processor with defaults",
          "[membrum][vst][state_v2_v3][StateMigration]")
{
    // Acceptance evidence for FR-183 -- fixture must exist and be 268 bytes.
    const std::vector<std::uint8_t> fixture = loadFixture();
    REQUIRE_FALSE(fixture.empty());
    REQUIRE(fixture.size() == static_cast<std::size_t>(kV2BodyBytes));

    // Sanity-check the fixture's version header is 2.
    int32 fixtureVersion = 0;
    std::memcpy(&fixtureVersion, fixture.data(), sizeof(fixtureVersion));
    REQUIRE(fixtureVersion == 2);

    Membrum::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);

    auto setup = makeSetup();
    REQUIRE(processor.setupProcessing(setup) == kResultOk);
    REQUIRE(processor.setActive(true) == kResultOk);

    // Feed the 268-byte fixture into setState.
    auto* inStream = new MemoryStream();
    inStream->write(const_cast<std::uint8_t*>(fixture.data()),
                    static_cast<int32>(fixture.size()), nullptr);
    inStream->seek(0, IBStream::kIBSeekSet, nullptr);

    REQUIRE(processor.setState(inStream) == kResultOk);
    inStream->release();

    // getState() must now produce a 302-byte v3 blob.
    auto* outStream = new MemoryStream();
    REQUIRE(processor.getState(outStream) == kResultOk);

    int64 endPos = 0;
    outStream->seek(0, IBStream::kIBSeekEnd, &endPos);
    CHECK(endPos == kV3BlobBytes);

    outStream->seek(0, IBStream::kIBSeekSet, nullptr);

    // (a) Version header is 3.
    int32 readVersion = 0;
    outStream->read(&readVersion, sizeof(readVersion), nullptr);
    CHECK(readVersion == 3);

    // (b) Compare the v2 body byte-for-byte (starting at offset 4 of both
    //     streams -- the fixture was written with version==2, but all of the
    //     param payload bytes after the version int32 must match verbatim).
    std::vector<std::uint8_t> emittedV2Body(kV2BodyBytes - 4, 0);
    outStream->read(emittedV2Body.data(),
                    static_cast<int32>(emittedV2Body.size()), nullptr);
    for (std::size_t i = 0; i < emittedV2Body.size(); ++i)
    {
        INFO("v2 body byte offset=" << (4 + i));
        CHECK(emittedV2Body[i] == fixture[4 + i]);
    }

    // (c) The Phase 3 tail defaults: maxPoly=8, policy=0, all chokes=0.
    std::uint8_t readMaxPoly = 0xFF;
    std::uint8_t readPolicy  = 0xFF;
    outStream->read(&readMaxPoly, sizeof(readMaxPoly), nullptr);
    outStream->read(&readPolicy,  sizeof(readPolicy),  nullptr);
    CHECK(static_cast<int>(readMaxPoly) == 8);
    CHECK(static_cast<int>(readPolicy)  == 0);

    for (int i = 0; i < 32; ++i)
    {
        std::uint8_t b = 0xFF;
        outStream->read(&b, sizeof(b), nullptr);
        INFO("choke default index=" << i);
        CHECK(static_cast<int>(b) == 0);
    }

    outStream->release();
    REQUIRE(processor.setActive(false) == kResultOk);
    REQUIRE(processor.terminate() == kResultOk);
}
