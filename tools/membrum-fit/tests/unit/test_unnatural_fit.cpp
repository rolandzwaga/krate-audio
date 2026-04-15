// Unnatural fit smoke: injected non-harmonic modes raise modeInjectAmount.
#include "src/unnatural_fit.h"

#include "dsp/pad_config.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

TEST_CASE("Unnatural fit: natural harmonics => defaults preserved") {
    MembrumFit::ModalDecomposition md;
    for (int k = 1; k <= 6; ++k) {
        MembrumFit::Mode m;
        m.freqHz    = 200.0f * k;
        m.amplitude = 1.0f / k;
        m.decayRate = 5.0f;
        md.modes.push_back(m);
    }
    Membrum::PadConfig cfg{};
    MembrumFit::SegmentedSample seg{};
    MembrumFit::fitUnnaturalZone({}, seg, 44100.0, md, cfg, cfg);
    REQUIRE(cfg.modeInjectAmount == Catch::Approx(0.0f));
}

TEST_CASE("Unnatural fit: non-harmonic modes raise modeInjectAmount") {
    MembrumFit::ModalDecomposition md;
    // Three harmonic modes, three very non-harmonic ones.
    const float freqs[] = { 200.0f, 400.0f, 600.0f, 237.0f, 473.0f, 718.0f };
    for (float f : freqs) {
        MembrumFit::Mode m;
        m.freqHz    = f;
        m.amplitude = 1.0f;
        m.decayRate = 5.0f;
        md.modes.push_back(m);
    }
    Membrum::PadConfig cfg{};
    MembrumFit::SegmentedSample seg{};
    MembrumFit::fitUnnaturalZone({}, seg, 44100.0, md, cfg, cfg);
    REQUIRE(cfg.modeInjectAmount > 0.5f);
}
