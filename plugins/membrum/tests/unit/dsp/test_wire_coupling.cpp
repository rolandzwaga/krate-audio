// ==============================================================================
// Wire Coupling -- modal-energy-coupled snare wire buzz
// ==============================================================================
// Feature: a per-pad `wireCoupling` [0,1] that modulates the parallel
// noise-layer ("wire buzz") amplitude by the body's modal-energy envelope, so
// the buzz tracks the head's vibration (Bilbao 2012: snare wires are driven by
// head motion). At 0 (default) the buzz keeps its independent fixed ADSR --
// bit-exact legacy behaviour. At 1 the buzz amplitude fully follows the modal
// energy: it dies with the head, chokes on note-off, and re-excites on flams.
//
// The modulation lives entirely in DrumVoice (three noise-mix sites) and reuses
// the gain-invariant getModalEnergy() follower that tension modulation already
// drives. These tests exercise DrumVoice directly so setWireCoupling() is the
// only variable between renders.
// ==============================================================================
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/default_kit.h"
#include "dsp/drum_voice.h"
#include "dsp/pad_config.h"

#include <algorithm>
#include <cmath>
#include <vector>

using Catch::Approx;
using Membrum::DrumVoice;
using Membrum::PadConfig;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr float  kPi         = 3.14159265358979323846f;

// Apply the sound-shaping subset of a PadConfig that the wire-coupling signal
// path depends on. Mirrors VoicePool::applyPadConfigToSlot for the fields that
// matter to the body + noise-layer + coupling chain (routing / macros / tone
// shaper filter are irrelevant to what we measure here).
void applyConfig(DrumVoice& v, const PadConfig& c)
{
    v.setExciterType(c.exciterType);
    v.setBodyModel(c.bodyModel);
    v.setMaterial(c.material);
    v.setSize(c.size);
    v.setDecay(c.decay);
    v.setStrikePosition(c.strikePosition);
    v.setLevel(c.level);
    v.setNoiseLayerMix(c.noiseLayerMix);
    v.setNoiseLayerCutoff(c.noiseLayerCutoff);
    v.setNoiseLayerResonance(c.noiseLayerResonance);
    v.setNoiseLayerDecay(c.noiseLayerDecay);
    v.setNoiseLayerColor(c.noiseLayerColor);
    v.setNoiseLayerGain(c.noiseLayerGain);
    v.setClickLayerMix(c.clickLayerMix);
    v.setClickLayerContactMs(c.clickLayerContactMs);
    v.setClickLayerBrightness(c.clickLayerBrightness);
    v.setBodyDampingB1(c.bodyDampingB1);
    v.setBodyDampingB3(c.bodyDampingB3);
    v.setAirLoading(c.airLoading);
    v.setModeScatter(c.modeScatter);
    v.setTensionModAmt(c.tensionModAmt);
    v.setWireCoupling(c.wireCoupling);
    // Secondary shell coupling left at defaults (off) so the measured signal is
    // the primary body + wire buzz only -- the feature under test.
}

PadConfig snareConfig()
{
    PadConfig c{};
    Membrum::DefaultKit::applyTemplate(c, Membrum::DrumTemplate::Snare);
    return c;
}

// Render `numSamples` of a freshly-triggered voice into a mono buffer using a
// fixed block size. If noteOffAt >= 0, a noteOff() is issued at that sample
// boundary (rounded down to the block edge).
std::vector<float> render(DrumVoice& v, int numSamples, int blockSize,
                          int noteOffAt = -1)
{
    std::vector<float> out;
    out.reserve(static_cast<size_t>(numSamples));
    std::vector<float> block(static_cast<size_t>(blockSize), 0.0f);
    int produced = 0;
    bool firedNoteOff = false;
    while (produced < numSamples)
    {
        if (noteOffAt >= 0 && !firedNoteOff && produced >= noteOffAt)
        {
            v.noteOff();
            firedNoteOff = true;
        }
        const int n = std::min(blockSize, numSamples - produced);
        v.processBlock(block.data(), n);
        for (int i = 0; i < n; ++i)
            out.push_back(block[static_cast<size_t>(i)]);
        produced += n;
    }
    return out;
}

// Single-bin Goertzel power over a sample window [lo, hi).
double goertzelPower(const std::vector<float>& x, int lo, int hi,
                     double sr, double freq)
{
    const double w = 2.0 * kPi * freq / sr;
    const double coeff = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (int i = lo; i < hi && i < static_cast<int>(x.size()); ++i)
    {
        const double s0 = static_cast<double>(x[static_cast<size_t>(i)])
                          + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - coeff * s1 * s2;
}

// Wire-band (4-10 kHz) power summed over a sample window.
double wireBandPower(const std::vector<float>& x, int lo, int hi, double sr)
{
    double e = 0.0;
    for (double f = 4000.0; f <= 10000.0; f += 50.0)
        e += goertzelPower(x, lo, hi, sr, f);
    return e;
}

double windowRms(const std::vector<float>& x, int lo, int hi)
{
    double sum = 0.0;
    int n = 0;
    for (int i = lo; i < hi && i < static_cast<int>(x.size()); ++i)
    {
        sum += static_cast<double>(x[static_cast<size_t>(i)])
               * static_cast<double>(x[static_cast<size_t>(i)]);
        ++n;
    }
    return n > 0 ? std::sqrt(sum / n) : 0.0;
}

double toDb(double ratio) { return 20.0 * std::log10(std::max(ratio, 1e-30)); }

} // namespace

TEST_CASE("Wire coupling: 0 is bit-exact to legacy (untouched) behaviour",
          "[membrum][dsp][wire-coupling]")
{
    // Two voices, identical snare config + same voiceId (so the noise PRNG seeds
    // match). One has wireCoupling explicitly set to 0; the other never touches
    // it (relies on the default). Both must produce identical samples.
    const PadConfig cfg = snareConfig();

    DrumVoice a;
    a.prepare(kSampleRate, /*voiceId*/ 7u);
    applyConfig(a, cfg);
    a.setWireCoupling(0.0f);     // force legacy: the snare template ships 0.45
    a.noteOn(0.9f);

    DrumVoice b;
    b.prepare(kSampleRate, /*voiceId*/ 7u);
    // Apply everything EXCEPT wireCoupling -- leave the field at its default.
    PadConfig noWire = cfg;
    b.setExciterType(noWire.exciterType);
    b.setBodyModel(noWire.bodyModel);
    b.setMaterial(noWire.material);
    b.setSize(noWire.size);
    b.setDecay(noWire.decay);
    b.setStrikePosition(noWire.strikePosition);
    b.setLevel(noWire.level);
    b.setNoiseLayerMix(noWire.noiseLayerMix);
    b.setNoiseLayerCutoff(noWire.noiseLayerCutoff);
    b.setNoiseLayerResonance(noWire.noiseLayerResonance);
    b.setNoiseLayerDecay(noWire.noiseLayerDecay);
    b.setNoiseLayerColor(noWire.noiseLayerColor);
    b.setNoiseLayerGain(noWire.noiseLayerGain);
    b.setClickLayerMix(noWire.clickLayerMix);
    b.setClickLayerContactMs(noWire.clickLayerContactMs);
    b.setClickLayerBrightness(noWire.clickLayerBrightness);
    b.setBodyDampingB1(noWire.bodyDampingB1);
    b.setBodyDampingB3(noWire.bodyDampingB3);
    b.setAirLoading(noWire.airLoading);
    b.setModeScatter(noWire.modeScatter);
    b.setTensionModAmt(noWire.tensionModAmt);
    // NB: setWireCoupling intentionally NOT called.
    b.noteOn(0.9f);

    const int kSamples = static_cast<int>(kSampleRate);  // 1 s
    const auto ra = render(a, kSamples, 256);
    const auto rb = render(b, kSamples, 256);

    REQUIRE(ra.size() == rb.size());
    bool identical = true;
    for (size_t i = 0; i < ra.size(); ++i)
        if (ra[i] != rb[i]) { identical = false; break; }
    CHECK(identical);
}

TEST_CASE("Wire coupling: coupling=1 makes the buzz die with the body",
          "[membrum][dsp][wire-coupling]")
{
    // Body b1 ~30 s^-1 (default snare) => amplitude t60 ~230 ms; the wire ADSR
    // decays over ~252 ms (decay norm 0.55). wireGainMod ~ sqrt(energyNorm) ~
    // the body's amplitude envelope, so coupling=1 buzz ~ coupling=0 buzz x
    // e^(-b1*t): by ~100 ms the buzz is already >20 dB down while coupling=0
    // still has a live tail. (A much later window is unusable -- BOTH decay to
    // silence by ~350 ms.)
    const PadConfig cfg = snareConfig();

    DrumVoice v0;
    v0.prepare(kSampleRate, 11u);
    applyConfig(v0, cfg);
    v0.setWireCoupling(0.0f);
    v0.noteOn(1.0f);

    DrumVoice v1;
    v1.prepare(kSampleRate, 11u);
    applyConfig(v1, cfg);
    v1.setWireCoupling(1.0f);
    v1.noteOn(1.0f);

    const int kSamples = static_cast<int>(kSampleRate);  // 1 s
    const auto r0 = render(v0, kSamples, 256);
    const auto r1 = render(v1, kSamples, 256);

    // Window 90-160 ms: coupling=0 buzz still alive, coupling=1 collapsed.
    const int lo = static_cast<int>(0.090 * kSampleRate);
    const int hi = static_cast<int>(0.160 * kSampleRate);
    const double w0 = wireBandPower(r0, lo, hi, kSampleRate);
    const double w1 = wireBandPower(r1, lo, hi, kSampleRate);

    const double dropDb = 10.0 * std::log10(std::max(w1, 1e-30)
                                            / std::max(w0, 1e-30));
    INFO("late-window wire power: coupling0=" << w0 << " coupling1=" << w1
         << " drop=" << dropDb << " dB");
    // coupling=1 late-window wire energy must be well below coupling=0.
    CHECK(dropDb < -12.0);
}

TEST_CASE("Wire coupling: coupling=1 does not attenuate the onset",
          "[membrum][dsp][wire-coupling]")
{
    // Running-peak normalisation => while modal energy is still rising the mod
    // factor is ~1, so the first ~20 ms must not be quieter than coupling=0.
    const PadConfig cfg = snareConfig();

    DrumVoice v0;
    v0.prepare(kSampleRate, 5u);
    applyConfig(v0, cfg);
    v0.setWireCoupling(0.0f);
    v0.noteOn(1.0f);

    DrumVoice v1;
    v1.prepare(kSampleRate, 5u);
    applyConfig(v1, cfg);
    v1.setWireCoupling(1.0f);
    v1.noteOn(1.0f);

    const int kSamples = static_cast<int>(0.050 * kSampleRate);
    const auto r0 = render(v0, kSamples, 256);
    const auto r1 = render(v1, kSamples, 256);

    const int onsetHi = static_cast<int>(0.020 * kSampleRate);
    const double rms0 = windowRms(r0, 0, onsetHi);
    const double rms1 = windowRms(r1, 0, onsetHi);
    const double deltaDb = toDb(rms1 / std::max(rms0, 1e-30));
    INFO("onset RMS: coupling0=" << rms0 << " coupling1=" << rms1
         << " delta=" << deltaDb << " dB");
    CHECK(deltaDb > -1.5);   // onset is essentially unchanged
}

TEST_CASE("Wire coupling: note-off chokes the buzz",
          "[membrum][dsp][wire-coupling]")
{
    // noteOff() damps the mode radii (x0.997) so modal energy collapses. With
    // coupling=1 the wire buzz must follow that collapse; with coupling=0 the
    // buzz keeps its own ADSR tail. Compare a post-note-off window.
    const PadConfig cfg = snareConfig();

    DrumVoice v0;
    v0.prepare(kSampleRate, 21u);
    applyConfig(v0, cfg);
    v0.setWireCoupling(0.0f);
    v0.noteOn(1.0f);

    DrumVoice v1;
    v1.prepare(kSampleRate, 21u);
    applyConfig(v1, cfg);
    v1.setWireCoupling(1.0f);
    v1.noteOn(1.0f);

    const int kSamples   = static_cast<int>(0.400 * kSampleRate);
    const int kNoteOffAt = static_cast<int>(0.060 * kSampleRate);
    const auto r0 = render(v0, kSamples, 256, kNoteOffAt);
    const auto r1 = render(v1, kSamples, 256, kNoteOffAt);

    // Post-note-off window: 100-160 ms (40-100 ms after the x0.997/sample damp
    // has crushed the modal energy). coupling=0 buzz keeps its own ADSR tail
    // (scaled by the amp release); coupling=1 buzz follows the dead body.
    const int lo = static_cast<int>(0.100 * kSampleRate);
    const int hi = static_cast<int>(0.160 * kSampleRate);
    const double w0 = wireBandPower(r0, lo, hi, kSampleRate);
    const double w1 = wireBandPower(r1, lo, hi, kSampleRate);
    const double dropDb = 10.0 * std::log10(std::max(w1, 1e-30)
                                            / std::max(w0, 1e-30));
    INFO("post-noteOff wire power: coupling0=" << w0 << " coupling1=" << w1
         << " drop=" << dropDb << " dB");
    CHECK(dropDb < -12.0);
}

TEST_CASE("Wire coupling: String body bypasses coupling (identical output)",
          "[membrum][dsp][wire-coupling]")
{
    // A String body runs a waveguide, not the modal bank, so getModalEnergy()
    // stays 0 => wireEnergyPeak_ never builds => computeWireGainMod() returns 1.
    // coupling=1 must be identical to coupling=0.
    PadConfig cfg = snareConfig();
    cfg.bodyModel = Membrum::BodyModelType::String;

    DrumVoice v0;
    v0.prepare(kSampleRate, 3u);
    applyConfig(v0, cfg);
    v0.setWireCoupling(0.0f);
    v0.noteOn(0.8f);

    DrumVoice v1;
    v1.prepare(kSampleRate, 3u);
    applyConfig(v1, cfg);
    v1.setWireCoupling(1.0f);
    v1.noteOn(0.8f);

    const int kSamples = static_cast<int>(0.500 * kSampleRate);
    const auto r0 = render(v0, kSamples, 256);
    const auto r1 = render(v1, kSamples, 256);

    REQUIRE(r0.size() == r1.size());
    bool identical = true;
    for (size_t i = 0; i < r0.size(); ++i)
        if (r0[i] != r1[i]) { identical = false; break; }
    CHECK(identical);
}

TEST_CASE("Wire coupling: block-size robustness (chunk-rate follower stable)",
          "[membrum][dsp][wire-coupling]")
{
    // The energy follower + gain mod update once per processBlock chunk. Two
    // renders with different block sizes must agree to within a small tolerance
    // in the late window -- a coarse guard that the chunk-rate math is stable
    // and does not, e.g., re-zero the peak or diverge with block size.
    const PadConfig cfg = snareConfig();

    DrumVoice va;
    va.prepare(kSampleRate, 9u);
    applyConfig(va, cfg);
    va.setWireCoupling(0.7f);
    va.noteOn(1.0f);

    DrumVoice vb;
    vb.prepare(kSampleRate, 9u);
    applyConfig(vb, cfg);
    vb.setWireCoupling(0.7f);
    vb.noteOn(1.0f);

    const int kSamples = static_cast<int>(0.500 * kSampleRate);
    const auto ra = render(va, kSamples, 64);
    const auto rb = render(vb, kSamples, 512);

    const int lo = static_cast<int>(0.200 * kSampleRate);
    const int hi = static_cast<int>(0.400 * kSampleRate);
    const double rmsA = windowRms(ra, lo, hi);
    const double rmsB = windowRms(rb, lo, hi);
    const double deltaDb = toDb(rmsA / std::max(rmsB, 1e-30));
    INFO("block64 vs block512 late RMS delta=" << deltaDb << " dB");
    CHECK(std::abs(deltaDb) < 3.0);
}
