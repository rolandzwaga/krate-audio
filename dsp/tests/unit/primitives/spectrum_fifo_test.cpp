// ==============================================================================
// Tests: SpectrumFIFO
// ==============================================================================
// Lock-free SPSC ring buffer tests for audio-UI spectrum data streaming.
// ==============================================================================

#include <krate/dsp/primitives/spectrum_fifo.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <array>
#include <cmath>
#include <numeric>
#include <thread>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;

TEST_CASE("SpectrumFIFO: default construction", "[spectrum][fifo][primitives]") {
    SpectrumFIFO<1024> fifo;

    SECTION("starts with zero written") {
        REQUIRE(fifo.totalWritten() == 0);
    }

    SECTION("readLatest returns 0 when empty") {
        float dest[256];
        REQUIRE(fifo.readLatest(dest, 256) == 0);
    }
}

TEST_CASE("SpectrumFIFO: push and readLatest basic", "[spectrum][fifo][primitives]") {
    SpectrumFIFO<1024> fifo;

    SECTION("push single block and read back") {
        std::array<float, 512> samples;
        std::iota(samples.begin(), samples.end(), 1.0f);  // 1, 2, 3, ..., 512

        fifo.push(samples.data(), samples.size());

        REQUIRE(fifo.totalWritten() == 512);

        // Read all 512 samples
        std::array<float, 512> dest{};
        REQUIRE(fifo.readLatest(dest.data(), 512) == 512);

        for (size_t i = 0; i < 512; ++i) {
            REQUIRE(dest[i] == Approx(static_cast<float>(i + 1)));
        }
    }

    SECTION("readLatest fails when requesting more than written") {
        std::array<float, 100> samples{};
        fifo.push(samples.data(), 100);

        float dest[200];
        REQUIRE(fifo.readLatest(dest, 200) == 0);
    }

    SECTION("readLatest returns exactly count or 0") {
        std::array<float, 512> samples{};
        fifo.push(samples.data(), 512);

        float dest[256];
        size_t read = fifo.readLatest(dest, 256);
        REQUIRE(read == 256);
    }
}

TEST_CASE("SpectrumFIFO: readLatest returns most recent samples", "[spectrum][fifo][primitives]") {
    SpectrumFIFO<1024> fifo;

    // Push 512 samples: values 0..511
    std::array<float, 512> block1;
    std::iota(block1.begin(), block1.end(), 0.0f);
    fifo.push(block1.data(), block1.size());

    // Push 512 more: values 512..1023
    std::array<float, 512> block2;
    std::iota(block2.begin(), block2.end(), 512.0f);
    fifo.push(block2.data(), block2.size());

    // Read latest 512 -- should get values 512..1023
    std::array<float, 512> dest{};
    REQUIRE(fifo.readLatest(dest.data(), 512) == 512);

    for (size_t i = 0; i < 512; ++i) {
        REQUIRE(dest[i] == Approx(static_cast<float>(i + 512)));
    }
}

TEST_CASE("SpectrumFIFO: wraparound behavior", "[spectrum][fifo][primitives]") {
    SpectrumFIFO<256> fifo;  // Small buffer to force wraparound

    // Push more than buffer size to force wraparound
    std::array<float, 128> block;

    // Push 4 blocks of 128 = 512 total (wraps around 256-sample buffer twice)
    for (int b = 0; b < 4; ++b) {
        for (size_t i = 0; i < 128; ++i) {
            block[i] = static_cast<float>(b * 128 + i);
        }
        fifo.push(block.data(), 128);
    }

    REQUIRE(fifo.totalWritten() == 512);

    // Read latest 128 -- should get values 384..511
    std::array<float, 128> dest{};
    REQUIRE(fifo.readLatest(dest.data(), 128) == 128);

    for (size_t i = 0; i < 128; ++i) {
        REQUIRE(dest[i] == Approx(static_cast<float>(384 + i)));
    }
}

TEST_CASE("SpectrumFIFO: full buffer read after wraparound", "[spectrum][fifo][primitives]") {
    SpectrumFIFO<256> fifo;

    // Write 512 samples (wraps around the 256-element buffer)
    std::array<float, 512> samples;
    std::iota(samples.begin(), samples.end(), 0.0f);

    // Push in two blocks
    fifo.push(samples.data(), 256);
    fifo.push(samples.data() + 256, 256);

    // Read latest 256 -- should get the second half (values 256..511)
    std::array<float, 256> dest{};
    REQUIRE(fifo.readLatest(dest.data(), 256) == 256);

    for (size_t i = 0; i < 256; ++i) {
        REQUIRE(dest[i] == Approx(static_cast<float>(256 + i)));
    }
}

TEST_CASE("SpectrumFIFO: null and edge cases", "[spectrum][fifo][primitives]") {
    SpectrumFIFO<1024> fifo;

    SECTION("push with null pointer does nothing") {
        fifo.push(nullptr, 100);
        REQUIRE(fifo.totalWritten() == 0);
    }

    SECTION("push with zero count does nothing") {
        float sample = 1.0f;
        fifo.push(&sample, 0);
        REQUIRE(fifo.totalWritten() == 0);
    }

    SECTION("readLatest with null dest returns 0") {
        float sample = 1.0f;
        fifo.push(&sample, 1);
        REQUIRE(fifo.readLatest(nullptr, 1) == 0);
    }

    SECTION("readLatest with zero count returns 0") {
        float sample = 1.0f;
        fifo.push(&sample, 1);
        float dest;
        REQUIRE(fifo.readLatest(&dest, 0) == 0);
    }

    SECTION("readLatest requesting more than buffer size returns 0") {
        // Buffer is 1024, requesting 2048
        std::array<float, 1024> samples{};
        fifo.push(samples.data(), 1024);
        std::array<float, 2048> dest{};
        REQUIRE(fifo.readLatest(dest.data(), 2048) == 0);
    }
}

TEST_CASE("SpectrumFIFO: clear resets state", "[spectrum][fifo][primitives]") {
    SpectrumFIFO<1024> fifo;

    std::array<float, 512> samples{};
    fifo.push(samples.data(), 512);
    REQUIRE(fifo.totalWritten() == 512);

    fifo.clear();
    REQUIRE(fifo.totalWritten() == 0);

    float dest[256];
    REQUIRE(fifo.readLatest(dest, 256) == 0);
}

TEST_CASE("SpectrumFIFO: concurrent push and readLatest", "[spectrum][fifo][primitives]") {
    SpectrumFIFO<8192> fifo;

    constexpr size_t kBlockSize = 512;
    constexpr size_t kNumBlocks = 100;
    constexpr size_t kReadSize = 2048;

    // Producer thread: push sequential blocks
    std::thread producer([&]() {
        std::array<float, kBlockSize> block;
        for (size_t b = 0; b < kNumBlocks; ++b) {
            for (size_t i = 0; i < kBlockSize; ++i) {
                block[i] = static_cast<float>(b * kBlockSize + i);
            }
            fifo.push(block.data(), kBlockSize);
        }
    });

    // Consumer thread: periodically read latest
    std::atomic<bool> consumerOk{true};
    std::thread consumer([&]() {
        std::array<float, kReadSize> dest{};
        int readCount = 0;

        while (readCount < 20) {
            size_t got = fifo.readLatest(dest.data(), kReadSize);
            if (got == kReadSize) {
                // Verify monotonically increasing (with possible gaps)
                for (size_t i = 1; i < kReadSize; ++i) {
                    if (dest[i] < dest[i - 1]) {
                        consumerOk.store(false);
                        return;
                    }
                }
                ++readCount;
            }
            // Brief sleep to avoid spinning
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });

    producer.join();
    consumer.join();

    REQUIRE(consumerOk.load());
    REQUIRE(fifo.totalWritten() == kBlockSize * kNumBlocks);
}
