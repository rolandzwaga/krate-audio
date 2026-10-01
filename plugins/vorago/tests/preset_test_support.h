#pragma once

// ==============================================================================
// Vorago - factory-preset test support (Phase 14, plan 4.8)
// ==============================================================================
// Catch2-free: this header must never include a Catch2 header, because the
// probe TU (T002), the sweep harness and the preset generator all share it and
// the generator is not a Catch2 target.
//
// PART 0 (T001b, ruling R-8): the C-7.2 sound-space descriptor and the C-7.3
// distance (plan 5.4 / 5.5). ONE implementation, used by the audibility probe
// and by the sweep. Phase 14 second-pass tasks T006, T025, T026, T033-T036,
// T040 extend this header.
//
// Every metric is an EXISTING helper, reused, not re-implemented:
//   - bandEnergyDb, crestFactorDb, blockRmsDb, perBandTotalVariation,
//     perBinMagnitudeFlux: Krate::DSP::TestUtils::Vorago
//     (tests/test_helpers/vorago_fixtures.h)
//   - calculateCorrelation: Krate::DSP::TestUtils
//     (tests/test_helpers/low_frequency_metrics.h)
//
// NAMESPACE HAZARD (as vorago_test_fixture.h): Krate::DSP::TestUtils::Vorago
// exists, so the helpers are always fully qualified and this header never
// writes `using namespace Krate::DSP::TestUtils;`.
// ==============================================================================

#include <low_frequency_metrics.h>
#include <vorago_fixtures.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace VoragoTest {

/// Number of relative band-energy components (b0 ... b8).
inline constexpr std::size_t kDescriptorBands = 9u;

/// Total scaled components: 9 bands + motion, flux, corr, energySpread, crest.
inline constexpr std::size_t kDescriptorComponents = kDescriptorBands + 5u;

/// @brief The C-7.2 sound-space descriptor, every component already scaled so
///        that 1.0 is one audible step (plan 5.4).
struct PresetDescriptor {
    std::array<double, kDescriptorBands> band{};
    double motion = 0.0;
    double flux = 0.0;
    double corr = 0.0;
    double energySpread = 0.0;
    double crest = 0.0;
};

namespace detail {

/// Stereo band power: the power mean of the two per-channel bandEnergyDb values.
[[nodiscard]] inline double stereoBandDb(std::span<const float> L, std::span<const float> R,
                                         double sr, double lo, double hi) {
    const double eL = Krate::DSP::TestUtils::Vorago::bandEnergyDb(L, sr, lo, hi);
    const double eR = Krate::DSP::TestUtils::Vorago::bandEnergyDb(R, sr, lo, hi);
    const double p = (std::pow(10.0, eL / 10.0) + std::pow(10.0, eR / 10.0)) / 2.0;
    return 10.0 * std::log10(std::max(p, 1e-30));
}

/// Band edges (plan 5.4): k = 0 is [20, 80]; k = 1..7 is [80*2^(k-1), 80*2^k];
/// k = 8 is [10240, 20000].
[[nodiscard]] inline double bandLoHz(std::size_t k) {
    if (k == 0u) {
        return 20.0;
    }
    return 80.0 * std::pow(2.0, static_cast<double>(k - 1u));
}

[[nodiscard]] inline double bandHiHz(std::size_t k) {
    if (k == 0u) {
        return 80.0;
    }
    if (k >= kDescriptorBands - 1u) {
        return 20000.0;
    }
    return 80.0 * std::pow(2.0, static_cast<double>(k));
}

/// log2 with the plan's 1e-6 floor (a static render returns 0.0 from both
/// movement helpers). Written as a positive test so a NaN takes the floor.
[[nodiscard]] inline double log2Floored(double v) {
    const double floored = (v > 1e-6) ? v : 1e-6;
    return std::log2(floored);
}

/// The body of `describe` (moved verbatim, T026 / plan 6.4). When
/// `energyFloorDb` holds a value, each one-second stereo dB value is clamped
/// below at it before the spread is taken (C-7.4 D_att). std::optional, never a
/// -inf sentinel: the generator and the macOS leg build with -ffast-math.
[[nodiscard]] inline PresetDescriptor describeImpl(std::span<const float> L,
                                                   std::span<const float> R, double sr,
                                                   std::optional<double> energyFloorDb) {
    namespace VM = Krate::DSP::TestUtils::Vorago;

    PresetDescriptor d;
    const std::size_t n = std::min(L.size(), R.size());
    const std::span<const float> l = L.first(n);
    const std::span<const float> r = R.first(n);

    // Bands, relative to the sub-free E_hi = E(80, 20000), clamped at -60 dB, /3.
    const double eHi = detail::stereoBandDb(l, r, sr, 80.0, 20000.0);
    for (std::size_t k = 0; k < kDescriptorBands; ++k) {
        const double rel =
            detail::stereoBandDb(l, r, sr, detail::bandLoHz(k), detail::bandHiHz(k)) - eHi;
        d.band[k] = std::max(rel, -60.0) / 3.0;
    }

    // Motion and flux: log2 of the channel mean, 1e-6 floor, unit 1.
    d.motion = detail::log2Floored(
        (VM::perBandTotalVariation(l, sr) + VM::perBandTotalVariation(r, sr)) / 2.0);
    d.flux = detail::log2Floored(
        (VM::perBinMagnitudeFlux(l, sr) + VM::perBinMagnitudeFlux(r, sr)) / 2.0);

    // Inter-channel correlation, unit 0.25.
    d.corr = static_cast<double>(Krate::DSP::TestUtils::calculateCorrelation(l.data(), r.data(), n))
             / 0.25;

    // Energy spread: population stddev of the one-second stereo dB values, unit 2 dB.
    const auto blockLen = static_cast<std::size_t>(sr);
    const std::vector<double> dbL = VM::blockRmsDb(l, blockLen);
    const std::vector<double> dbR = VM::blockRmsDb(r, blockLen);
    const std::size_t blocks = std::min(dbL.size(), dbR.size());
    if (blocks > 0u) {
        std::vector<double> stereoDb(blocks);
        double sum = 0.0;
        for (std::size_t b = 0; b < blocks; ++b) {
            const double p = (std::pow(10.0, dbL[b] / 10.0) + std::pow(10.0, dbR[b] / 10.0)) / 2.0;
            stereoDb[b] = 10.0 * std::log10(std::max(p, 1e-30));
            if (energyFloorDb.has_value()) {
                stereoDb[b] = std::max(stereoDb[b], *energyFloorDb);
            }
            sum += stereoDb[b];
        }
        const double mean = sum / static_cast<double>(blocks);
        double var = 0.0;
        for (const double v : stereoDb) {
            var += (v - mean) * (v - mean);
        }
        d.energySpread = std::sqrt(var / static_cast<double>(blocks)) / 2.0;
    }

    // Crest factor, channel mean, unit 3 dB.
    d.crest = ((VM::crestFactorDb(l) + VM::crestFactorDb(r)) / 2.0) / 3.0;

    return d;
}

}  // namespace detail

/// @brief C-7.2 descriptor of one stereo minute (plan 5.4).
[[nodiscard]] inline PresetDescriptor describe(std::span<const float> L, std::span<const float> R,
                                               double sr) {
    return detail::describeImpl(L, R, sr, std::nullopt);
}

/// @brief C-7.4 D_att (plan 6.4 / 6.8): the C-7.2 descriptor with each
///        one-second stereo dB value feeding the energy spread clamped below at
///        `floorDb` (= RMS(Sus_P) - 60 dB) before the spread is taken.
[[nodiscard]] inline PresetDescriptor describeWithEnergyFloor(std::span<const float> L,
                                                              std::span<const float> R, double sr,
                                                              double floorDb) {
    return detail::describeImpl(L, R, sr, floorDb);
}

/// @brief C-7.3 distance: Euclidean over the 14 scaled components (plan 5.5).
[[nodiscard]] inline double descriptorDistance(const PresetDescriptor& a, const PresetDescriptor& b) {
    double sum = 0.0;
    for (std::size_t k = 0; k < kDescriptorBands; ++k) {
        const double diff = a.band[k] - b.band[k];
        sum += diff * diff;
    }
    const std::array<double, 5> da{a.motion - b.motion, a.flux - b.flux, a.corr - b.corr,
                                   a.energySpread - b.energySpread, a.crest - b.crest};
    for (const double diff : da) {
        sum += diff * diff;
    }
    return std::sqrt(sum);
}

/// @brief Component-wise mean (D(P) is the mean of the three minutes, plan 5.4).
///        An empty span yields the all-zero descriptor.
[[nodiscard]] inline PresetDescriptor meanOf(std::span<const PresetDescriptor> ds) {
    PresetDescriptor m;
    if (ds.empty()) {
        return m;
    }
    for (const PresetDescriptor& d : ds) {
        for (std::size_t k = 0; k < kDescriptorBands; ++k) {
            m.band[k] += d.band[k];
        }
        m.motion += d.motion;
        m.flux += d.flux;
        m.corr += d.corr;
        m.energySpread += d.energySpread;
        m.crest += d.crest;
    }
    const double inv = 1.0 / static_cast<double>(ds.size());
    for (double& b : m.band) {
        b *= inv;
    }
    m.motion *= inv;
    m.flux *= inv;
    m.corr *= inv;
    m.energySpread *= inv;
    m.crest *= inv;
    return m;
}

}  // namespace VoragoTest

// ==============================================================================
// C1 (T006, plan 5.8 C1 rows, P2-2): Constants, Take sets, Render, Env, Pool.
// Everything E0 needs, and none of it references the v3 surface or the defs
// header. Still Catch2-free: jobs run under runJobs never call Catch2 macros;
// results are asserted on the test thread.
// ==============================================================================

#include "parameters/param_mapping.h"
#include "plugin_ids.h"
#include "vorago_preset_host.h"

#include <vst_event_list.h>
#include <vst_param_changes.h>

#include <krate/dsp/core/db_utils.h>

#include <pluginterfaces/vst/vsttypes.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace VoragoTest {

// ---- Constants (every threshold named once) ---------------------------------
inline constexpr double kFloorF = 4.0;
inline constexpr double kSecondaryBar = 1.5;
inline constexpr double kSeedTwinMargin = kFloorF / 2;
inline constexpr double kLevelTwinBound = 0.05;
inline constexpr double kFreezeFloorDb = 20.0;
inline constexpr double kTailDropDb = 40.0;
inline constexpr int kNumSeedIndices = 16;
inline constexpr int kMaxTakes = 8;

static_assert(2 * kMaxTakes == kNumSeedIndices, "the A and B take sets together span every seed");

/// The ruled take count K (FR-017a, gate G2; T040). Frozen from pilot run 3:
/// specs/vorago-phase14-presets-release/artifacts/pilot_calibrate_run3.log:43
/// "G2: PROCEED with K = 4", recorded in compliance.md "## FR-017a pilot / G2"
/// ("Pilot run 3 - gate G2"; run 1 = artifacts/pilot_calibrate.log stopped at G2).
/// The pilot TU still loops K itself; every later sweep uses this value.
inline constexpr int kRuledTakes = 4;
static_assert(kRuledTakes >= 1 && kRuledTakes <= kMaxTakes, "K is a take count in [1, kMaxTakes]");

// ---- Take sets (P2-2) --------------------------------------------------------
/// A_K = {(s + j) mod 16 : j < K} (set 0, for D(P)); B_K = {(s + K + j) mod 16 :
/// j < K} (set 1, for t_K). `stored` is the preset's stored seed index.
[[nodiscard]] inline int takeSeedIndex(int stored, int set, int j, int K) noexcept {
    const int raw = (stored + set * K + j) % kNumSeedIndices;
    return (raw < 0) ? raw + kNumSeedIndices : raw;
}

/// kSeedId normalized value of seed index `index` (16-entry StringListParameter;
/// inverse of ::Vorago::indexFromNormalized, param_mapping.h:55-58).
[[nodiscard]] inline double seedNormalized(int index) {
    return static_cast<double>(index) / static_cast<double>(kNumSeedIndices - 1);
}

// ---- Render ------------------------------------------------------------------
/// One streaming render on a fresh PresetHost. Times are seconds.
struct RenderSpec {
    std::span<const std::uint8_t> comp;  ///< empty: skip setState (default surface)
    std::vector<std::pair<Steinberg::Vst::ParamID, double>> block0;  ///< (ID, normalized) at block 0
    int seedIndex = -1;                ///< >= 0: kSeedId -> seedNormalized(seedIndex) at block 0
    double sr = 48000;
    std::vector<std::int16_t> notes{36};
    std::int32_t forcePolyIndex = -1;  ///< >= 0: kPolyphonyId -> indexToNormalized(i, 6) at block 0
    double noteOffAt = -1;             ///< >= 0: NoteOff (every note) at this time
    double freezeAt = -1;              ///< >= 0: kSpaceFreezeId -> 1.0, first block starting at/after
    double end = 0.0;                  ///< render length
    std::vector<std::pair<double, double>> capture;  ///< [start, end) windows copied out
};

struct SweepCapture {
    bool finite = true;  ///< every sample finite (bit pattern); false also on a host failure
    float peak = 0.0f;   ///< stereo absolute peak
    std::vector<double> blockPowerL, blockPowerR;  ///< one sum of squares per 512-sample block
    std::vector<std::vector<float>> capL, capR;    ///< one entry per capture window
};

namespace detail {

inline constexpr Steinberg::int32 kRenderBlock = 512;
inline constexpr float kRenderVelocity100 = 100.0f / 127.0f;  // quantises to 100
inline constexpr int kPolyphonyChoices = 6;                   // kPolyphonyId: 1..6 voices

}  // namespace detail

/// Streams one render: per block it updates bit-pattern finiteness and the
/// stereo peak and appends one power sum per channel; samples are copied only
/// inside `capture` windows (capacity reserved before the loop). Block 0 carries
/// every parameter point at offset 0 plus the NoteOns at offset 0 (parameter
/// changes latch before events, processor.cpp:298).
[[nodiscard]] inline SweepCapture renderPreset(const RenderSpec& spec) {
    SweepCapture out;

    const long long total = std::llround(spec.end * spec.sr);
    if (total <= 0) {
        return out;
    }
    const long long block = detail::kRenderBlock;
    const auto numBlocks = static_cast<std::size_t>((total + block - 1) / block);
    out.blockPowerL.reserve(numBlocks);
    out.blockPowerR.reserve(numBlocks);

    // Capture windows in samples, clamped to [0, total].
    std::vector<std::pair<long long, long long>> windows;
    windows.reserve(spec.capture.size());
    out.capL.reserve(spec.capture.size());
    out.capR.reserve(spec.capture.size());
    for (const auto& [t0, t1] : spec.capture) {
        const long long a = std::clamp(std::llround(t0 * spec.sr), 0LL, total);
        const long long b = std::clamp(std::llround(t1 * spec.sr), a, total);
        windows.emplace_back(a, b);
        out.capL.emplace_back();
        out.capR.emplace_back();
        out.capL.back().reserve(static_cast<std::size_t>(b - a));
        out.capR.back().reserve(static_cast<std::size_t>(b - a));
    }

    PresetHost host;
    if (host.prepare(spec.sr, detail::kRenderBlock) != Steinberg::kResultOk) {
        out.finite = false;
        return out;
    }
    if (!spec.comp.empty() && host.loadState(spec.comp) != Steinberg::kResultOk) {
        out.finite = false;
        return out;
    }

    const long long noteOffSample =
        (spec.noteOffAt >= 0.0) ? std::llround(spec.noteOffAt * spec.sr) : -1LL;
    const long long freezeSample =
        (spec.freezeAt >= 0.0) ? std::llround(spec.freezeAt * spec.sr) : -1LL;
    bool freezeSent = false;

    Krate::Test::EventList ev;
    Krate::Test::ParameterChanges pc;

    for (long long start = 0; start < total; start += block) {
        const long long n = std::min(block, total - start);
        ev.clear();
        pc.clear();

        if (start == 0) {
            for (const auto& [id, value] : spec.block0) {
                pc.addChange(id, value);
            }
            if (spec.seedIndex >= 0) {
                pc.addChange(::Vorago::kSeedId, seedNormalized(spec.seedIndex));
            }
            if (spec.forcePolyIndex >= 0) {
                pc.addChange(::Vorago::kPolyphonyId,
                             ::Vorago::indexToNormalized(static_cast<int>(spec.forcePolyIndex),
                                                         detail::kPolyphonyChoices));
            }
            for (const std::int16_t note : spec.notes) {
                ev.addNoteOn(note, detail::kRenderVelocity100, 0);
            }
        }
        if (freezeSample >= 0 && !freezeSent && start >= freezeSample) {
            pc.addChange(::Vorago::kSpaceFreezeId, 1.0);
            freezeSent = true;
        }
        if (noteOffSample >= start && noteOffSample < start + n) {
            const auto offset = static_cast<Steinberg::int32>(noteOffSample - start);
            for (const std::int16_t note : spec.notes) {
                ev.addNoteOff(note, offset);
            }
        }

        const Steinberg::tresult r =
            host.process(static_cast<std::size_t>(n), (ev.getEventCount() > 0) ? &ev : nullptr,
                         (pc.getParameterCount() > 0) ? &pc : nullptr);
        if (r != Steinberg::kResultOk) {
            out.finite = false;
            return out;
        }

        const std::span<const float> L = host.outL();
        const std::span<const float> R = host.outR();
        double powL = 0.0;
        double powR = 0.0;
        for (std::size_t i = 0; i < L.size(); ++i) {
            const float l = L[i];
            const float rr = R[i];
            if (!Krate::DSP::detail::isFinite(l) || !Krate::DSP::detail::isFinite(rr)) {
                out.finite = false;
            }
            out.peak = std::max(out.peak, std::max(std::fabs(l), std::fabs(rr)));
            powL += static_cast<double>(l) * static_cast<double>(l);
            powR += static_cast<double>(rr) * static_cast<double>(rr);
        }
        out.blockPowerL.push_back(powL);
        out.blockPowerR.push_back(powR);

        for (std::size_t w = 0; w < windows.size(); ++w) {
            const long long a = std::max(windows[w].first, start);
            const long long b = std::min(windows[w].second, start + n);
            if (a >= b) {
                continue;
            }
            const auto first = static_cast<std::size_t>(a - start);
            const auto count = static_cast<std::size_t>(b - a);
            const std::span<const float> segL = L.subspan(first, count);
            const std::span<const float> segR = R.subspan(first, count);
            out.capL[w].insert(out.capL[w].end(), segL.begin(), segL.end());
            out.capR[w].insert(out.capR[w].end(), segR.begin(), segR.end());
        }
    }
    return out;
}

// ---- Env -----------------------------------------------------------------------
/// The variable's value, or std::nullopt when unset. getenv_s under MSVC (the
/// probe's pattern, ecosystem_rule_probe_test.cpp:211; its own readEnv is left
/// untouched), std::getenv elsewhere.
[[nodiscard]] inline std::optional<std::string> sweepEnv(const char* name) {
#ifdef _MSC_VER
    std::size_t len = 0;
    if (getenv_s(&len, nullptr, 0, name) != 0 || len == 0u) {
        return std::nullopt;
    }
    std::string value(len, '\0');  // len counts the terminator
    if (getenv_s(&len, value.data(), value.size(), name) != 0) {
        return std::nullopt;
    }
    value.resize((len > 0u) ? len - 1u : 0u);
    return value;
#else
    const char* v = std::getenv(name);  // NOLINT(concurrency-mt-unsafe) test thread only
    if (v == nullptr) {
        return std::nullopt;
    }
    return std::string(v);
#endif
}

// ---- Pool ----------------------------------------------------------------------
/// Runs every job exactly once on `threads` plain std::threads (clamped to
/// [1, jobs.size()]), an atomic next-index handing out the work; joins all.
/// Jobs never call Catch2 macros; results are asserted on the test thread.
inline void runJobs(std::vector<std::function<void()>>& jobs, unsigned threads) {
    if (jobs.empty()) {
        return;
    }
    const std::size_t count =
        std::clamp<std::size_t>(static_cast<std::size_t>(threads), 1u, jobs.size());
    std::atomic<std::size_t> next{0};
    const auto worker = [&jobs, &next] {
        for (;;) {
            const std::size_t i = next.fetch_add(1u, std::memory_order_relaxed);
            if (i >= jobs.size()) {
                return;
            }
            jobs[i]();
        }
    };
    std::vector<std::thread> pool;
    pool.reserve(count);
    for (std::size_t t = 0; t < count; ++t) {
        pool.emplace_back(worker);
    }
    for (std::thread& t : pool) {
        t.join();
    }
}

}  // namespace VoragoTest

// ==============================================================================
// C2 (T025, plan 5.8 rows, 6.1): Container, Info, Typed decode, Timeline.
// Still Catch2-free. The typed decode reuses the SHIPPED load*Params in the
// processor's getState() order (processor.cpp:643-659) - it never re-parses a
// field by hand.
// ==============================================================================

#include "processor/tail_estimate.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"

#include <krate/dsp/systems/vorago_macro_matrix.h>
#include <krate/dsp/systems/vorago_voice.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string_view>

namespace VoragoTest {

// ---- Container (FR-028) --------------------------------------------------------

/// One parsed `.vstpreset`. `ok` is true only when every structural check held;
/// otherwise `why` names the first failure.
struct PresetFile {
    std::string classId;             ///< the 32 header chars, verbatim
    std::vector<std::uint8_t> comp;  ///< the `Comp` chunk payload
    std::string info;                ///< the `Info` chunk payload (XML text)
    bool ok = false;
    std::string why;
};

namespace detail {

inline constexpr std::size_t kVstPresetHeaderBytes = 48u;  // magic, version, 32-char id, list offset
inline constexpr std::size_t kVstPresetClassIdChars = 32u;
inline constexpr std::size_t kVstPresetChunkEntryBytes = 20u;  // tag + int64 offset + int64 size

[[nodiscard]] inline bool presetReadLE32(const std::vector<std::uint8_t>& bytes, std::size_t offset,
                                         std::uint32_t& out) {
    if (offset > bytes.size() || bytes.size() - offset < 4u) {
        return false;
    }
    out = static_cast<std::uint32_t>(bytes[offset]) |
          (static_cast<std::uint32_t>(bytes[offset + 1u]) << 8u) |
          (static_cast<std::uint32_t>(bytes[offset + 2u]) << 16u) |
          (static_cast<std::uint32_t>(bytes[offset + 3u]) << 24u);
    return true;
}

[[nodiscard]] inline bool presetReadLE64(const std::vector<std::uint8_t>& bytes, std::size_t offset,
                                         std::int64_t& out) {
    if (offset > bytes.size() || bytes.size() - offset < 8u) {
        return false;
    }
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < 8u; ++i) {
        v |= static_cast<std::uint64_t>(bytes[offset + i]) << (8u * i);
    }
    out = static_cast<std::int64_t>(v);
    return true;
}

[[nodiscard]] inline bool presetTagAt(const std::vector<std::uint8_t>& bytes, std::size_t offset,
                                      const char* tag) {
    if (offset > bytes.size() || bytes.size() - offset < 4u) {
        return false;
    }
    return std::memcmp(bytes.data() + offset, tag, 4u) == 0;
}

/// [offset, offset + size) lies inside a file of `fileSize` bytes (overflow-safe).
[[nodiscard]] inline bool presetRangeInBounds(std::int64_t offset, std::int64_t size,
                                              std::size_t fileSize) {
    if (offset < 0 || size < 0) {
        return false;
    }
    const auto o = static_cast<std::uint64_t>(offset);
    const auto s = static_cast<std::uint64_t>(size);
    const auto f = static_cast<std::uint64_t>(fileSize);
    return o <= f && s <= f - o;
}

}  // namespace detail

/// Read and structurally validate one `.vstpreset` (FR-028): magic `VST3`,
/// container version 1, a 32-character class id, the list offset in bounds with a
/// `List` tag there, every chunk entry's offset + size in bounds, and both `Comp`
/// and `Info` present.
[[nodiscard]] inline PresetFile parseVstPreset(const std::filesystem::path& path) {
    PresetFile pf;

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        pf.why = "cannot open " + path.string();
        return pf;
    }
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                          std::istreambuf_iterator<char>());

    if (bytes.size() < detail::kVstPresetHeaderBytes) {
        pf.why = "file is " + std::to_string(bytes.size()) + " bytes, shorter than the " +
                 std::to_string(detail::kVstPresetHeaderBytes) + "-byte header";
        return pf;
    }
    if (!detail::presetTagAt(bytes, 0u, "VST3")) {
        pf.why = "missing \"VST3\" magic at offset 0";
        return pf;
    }
    std::uint32_t formatVersion = 0;
    if (!detail::presetReadLE32(bytes, 4u, formatVersion) || formatVersion != 1u) {
        pf.why = "container format version is " + std::to_string(formatVersion) + ", expected 1";
        return pf;
    }

    pf.classId.assign(reinterpret_cast<const char*>(bytes.data()) + 8,
                      detail::kVstPresetClassIdChars);
    if (pf.classId.find('\0') != std::string::npos) {
        pf.why = "class id is not 32 characters";
        return pf;
    }

    std::int64_t listOffset = 0;
    if (!detail::presetReadLE64(bytes, 40u, listOffset) ||
        !detail::presetRangeInBounds(listOffset, 8, bytes.size())) {
        pf.why = "list offset " + std::to_string(listOffset) + " is outside the " +
                 std::to_string(bytes.size()) + "-byte file";
        return pf;
    }
    const auto listAt = static_cast<std::size_t>(listOffset);
    if (!detail::presetTagAt(bytes, listAt, "List")) {
        pf.why = "no \"List\" tag at list offset " + std::to_string(listOffset);
        return pf;
    }
    std::uint32_t entryCount = 0;
    if (!detail::presetReadLE32(bytes, listAt + 4u, entryCount)) {
        pf.why = "truncated chunk-list entry count";
        return pf;
    }
    const std::uint64_t entriesBytes =
        static_cast<std::uint64_t>(entryCount) * detail::kVstPresetChunkEntryBytes;
    if (entriesBytes > static_cast<std::uint64_t>(bytes.size() - (listAt + 8u))) {
        pf.why = "chunk list declares " + std::to_string(entryCount) +
                 " entries, which runs past the end of the file";
        return pf;
    }

    std::optional<std::pair<std::size_t, std::size_t>> compSpan;
    std::optional<std::pair<std::size_t, std::size_t>> infoSpan;
    for (std::uint32_t i = 0; i < entryCount; ++i) {
        const std::size_t entryAt =
            listAt + 8u + (static_cast<std::size_t>(i) * detail::kVstPresetChunkEntryBytes);
        std::int64_t chunkOffset = 0;
        std::int64_t chunkSize = 0;
        if (!detail::presetReadLE64(bytes, entryAt + 4u, chunkOffset) ||
            !detail::presetReadLE64(bytes, entryAt + 12u, chunkSize)) {
            pf.why = "truncated chunk-list entry " + std::to_string(i);
            return pf;
        }
        if (!detail::presetRangeInBounds(chunkOffset, chunkSize, bytes.size())) {
            pf.why = "chunk-list entry " + std::to_string(i) + " (offset " +
                     std::to_string(chunkOffset) + ", size " + std::to_string(chunkSize) +
                     ") is outside the " + std::to_string(bytes.size()) + "-byte file";
            return pf;
        }
        const auto span = std::make_pair(static_cast<std::size_t>(chunkOffset),
                                         static_cast<std::size_t>(chunkSize));
        if (detail::presetTagAt(bytes, entryAt, "Comp")) {
            compSpan = span;
        } else if (detail::presetTagAt(bytes, entryAt, "Info")) {
            infoSpan = span;
        }
    }
    if (!compSpan.has_value()) {
        pf.why = "chunk list carries no \"Comp\" entry";
        return pf;
    }
    if (!infoSpan.has_value()) {
        pf.why = "chunk list carries no \"Info\" entry";
        return pf;
    }

    pf.comp.assign(bytes.begin() + static_cast<std::ptrdiff_t>(compSpan->first),
                   bytes.begin() + static_cast<std::ptrdiff_t>(compSpan->first + compSpan->second));
    pf.info.assign(reinterpret_cast<const char*>(bytes.data()) + infoSpan->first,
                   infoSpan->second);
    pf.ok = true;
    return pf;
}

// ---- Info ------------------------------------------------------------------------

/// `<Attr id="X" value="Y" type="string"/>` -> { "X" -> "Y" }. A missing
/// attribute is ABSENT from the map (never a default empty string), so a missing
/// `Comment` or `MusicalInstrument` reads as the structural fault it is. Each
/// element is isolated first, so one element's id never pairs with the next
/// element's value.
[[nodiscard]] inline std::map<std::string, std::string> parseInfoAttributes(std::string_view xml) {
    std::map<std::string, std::string> out;
    std::size_t pos = 0;
    while ((pos = xml.find("<Attr", pos)) != std::string_view::npos) {
        const std::size_t end = xml.find('>', pos);
        if (end == std::string_view::npos) {
            break;
        }
        const std::string_view element = xml.substr(pos, end - pos);
        const auto attribute = [element](std::string_view key) -> std::optional<std::string> {
            const std::string needle = std::string(key) + "=\"";
            const std::size_t at = element.find(needle);
            if (at == std::string_view::npos) {
                return std::nullopt;
            }
            const std::size_t valueBegin = at + needle.size();
            const std::size_t valueEnd = element.find('"', valueBegin);
            if (valueEnd == std::string_view::npos) {
                return std::nullopt;
            }
            return std::string(element.substr(valueBegin, valueEnd - valueBegin));
        };
        const std::optional<std::string> id = attribute("id");
        const std::optional<std::string> value = attribute("value");
        if (id.has_value() && value.has_value()) {
            out.emplace(*id, *value);
        }
        pos = end + 1u;
    }
    return out;
}

// ---- Typed decode (FR-031) -------------------------------------------------------

/// One member per shipped pack, as the processor holds them. The packs hold
/// atomics, so this is non-copyable and filled through decodePresetState's
/// out-param. `version` is the stream's int32 header; `bytesConsumed` is the
/// stream position after the last loader.
struct DecodedPresetState {
    ::Vorago::GlobalParams global;
    ::Vorago::MacroParams macros;
    ::Vorago::CloudParams cloud;
    ::Vorago::NoiseParams noise;
    ::Vorago::ResonanceParams resonance;
    ::Vorago::EcologyParams ecology;
    ::Vorago::SubParams sub;
    ::Vorago::SmearParams smear;
    ::Vorago::EventsParams events;
    ::Vorago::EcosystemParams ecosystem;
    ::Vorago::BodyParams body;
    ::Vorago::SpaceParams space;
    ::Vorago::EnvelopeParams envelope;
    ::Vorago::BloomParams bloom;
    ::Vorago::GhostParams ghost;
    ::Vorago::LifeParams life;
    std::int32_t version = 0;
    std::size_t bytesConsumed = 0;

    DecodedPresetState() = default;
    DecodedPresetState(const DecodedPresetState&) = delete;
    DecodedPresetState& operator=(const DecodedPresetState&) = delete;
    DecodedPresetState(DecodedPresetState&&) = delete;
    DecodedPresetState& operator=(DecodedPresetState&&) = delete;
    ~DecodedPresetState() = default;
};

/// Decodes a component state through the shipped loaders in getState() order
/// (processor.cpp:643-659): version, global, macro, global v2 ext, the 14 packs
/// in band order, then loadEcosystemParamsV3Ext. True only when every loader
/// returned true and exactly kStateV3Bytes (436) bytes were consumed.
[[nodiscard]] inline bool decodePresetState(std::span<const std::uint8_t> comp,
                                            DecodedPresetState& out) {
    out.version = 0;
    out.bytesConsumed = 0;
    if (comp.empty()) {
        return false;
    }
    // A private copy: MemoryStream's non-owning constructor takes a non-const
    // pointer, and the loaders only read.
    std::vector<std::uint8_t> buf(comp.begin(), comp.end());
    Steinberg::MemoryStream stream(buf.data(), static_cast<Steinberg::TSize>(buf.size()));
    Steinberg::IBStreamer s(&stream, kLittleEndian);

    Steinberg::int32 version = 0;
    if (!s.readInt32(version)) {
        return false;
    }
    out.version = static_cast<std::int32_t>(version);

    const bool loaded =
        ::Vorago::loadGlobalParams(out.global, s) && ::Vorago::loadMacroParams(out.macros, s) &&
        ::Vorago::loadGlobalParamsV2Ext(out.global, s) && ::Vorago::loadCloudParams(out.cloud, s) &&
        ::Vorago::loadNoiseParams(out.noise, s) &&
        ::Vorago::loadResonanceParams(out.resonance, s) &&
        ::Vorago::loadEcologyParams(out.ecology, s) && ::Vorago::loadSubParams(out.sub, s) &&
        ::Vorago::loadSmearParams(out.smear, s) && ::Vorago::loadEventsParams(out.events, s) &&
        ::Vorago::loadEcosystemParams(out.ecosystem, s) && ::Vorago::loadBodyParams(out.body, s) &&
        ::Vorago::loadSpaceParams(out.space, s) &&
        ::Vorago::loadEnvelopeParams(out.envelope, s) && ::Vorago::loadBloomParams(out.bloom, s) &&
        ::Vorago::loadGhostParams(out.ghost, s) && ::Vorago::loadLifeParams(out.life, s) &&
        ::Vorago::loadEcosystemParamsV3Ext(out.ecosystem, s);

    Steinberg::int64 pos = 0;
    if (stream.tell(&pos) != Steinberg::kResultOk || pos < 0) {
        return false;
    }
    out.bytesConsumed = static_cast<std::size_t>(pos);
    return loaded && out.bytesConsumed == ::Vorago::kStateV3Bytes;
}

// ---- Timeline (C-6, plan 6.1) ------------------------------------------------------

/// Seconds from note-on. Sus == M1; m[k] = {start, end} of minute k.
struct SweepTimeline {
    double A = 0.0;
    double rel = 0.0;
    double rt60 = 0.0;
    double sus0 = 0.0;
    double sus1 = 0.0;
    double m[3][2] = {};  // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays) plan 5.8 shape
    double H = 0.0;
    double tail0 = 0.0;
    double tail1 = 0.0;
    double total = 0.0;
    bool freezeOnTail = false;
};

/// The attack span A in seconds (plan 6.1): Growth -> the growth duration;
/// Standard -> the stage-time sum. One definition, shared by makeTimeline and
/// the D9 state predicate (plan 6.10).
[[nodiscard]] inline double attackSpanSeconds(const DecodedPresetState& st) {
    constexpr auto kR = std::memory_order_relaxed;
    const auto& env = st.envelope;
    if (env.mode.load(kR) == static_cast<int>(Krate::DSP::VoragoVoice::EnvelopeMode::Growth)) {
        return static_cast<double>(env.growthDurationSeconds.load(kR));
    }
    return (static_cast<double>(env.stage0TimeMs.load(kR)) +
            static_cast<double>(env.stage1TimeMs.load(kR)) +
            static_cast<double>(env.stage2TimeMs.load(kR)) +
            static_cast<double>(env.stage3TimeMs.load(kR))) /
           1000.0;
}

/// The AUDIBLE attack in seconds (sweep-2 ruling S-9, 2026-09-30; plan 6.8 as
/// amended): Growth -> the growth duration (the level rises over all of it);
/// Standard -> stage 0's time, the ramp to the first stage level. The C-6 sum
/// stays the TIMELINE's A (Sus placement, D9's state predicate): the registered
/// envelope (20 / 30 / 45 / 60 s, levels 1.0 / 0.8 / 0.92 / 0.85) is at full
/// level after its 20 s stage 0, so a 155 s "span" hides a 20 s attack, and an
/// attack window sized by the sum compared 150 s of near-identical sustain
/// (Sudden Chasm d_att 1.25 against a 4.83 attributability floor).
[[nodiscard]] inline double audibleAttackSeconds(const DecodedPresetState& st) {
    constexpr auto kR = std::memory_order_relaxed;
    const auto& env = st.envelope;
    if (env.mode.load(kR) == static_cast<int>(Krate::DSP::VoragoVoice::EnvelopeMode::Growth)) {
        return static_cast<double>(env.growthDurationSeconds.load(kR));
    }
    return static_cast<double>(env.stage0TimeMs.load(kR)) / 1000.0;
}

/// Plan 6.1: A (Growth: growth duration; Standard: the stage-time sum), Rel,
/// RT60 = effectiveCavernDecaySeconds over the decoded KNOB macros (the one
/// definition getTailSamples uses, tail_estimate.h), Sus = M1 = [A+5, A+65],
/// M2 = [A+65, A+125], M3 = [A+125, A+185], H = A+185; the tail is
/// [H+Rel+10, H+Rel+70] when the decoded freeze is On or `freezeGesture`, else
/// [H+Rel+RT60+5, H+Rel+RT60+15]; total = tail end.
[[nodiscard]] inline SweepTimeline makeTimeline(const DecodedPresetState& st, bool freezeGesture) {
    constexpr auto kR = std::memory_order_relaxed;
    SweepTimeline tl;

    const auto& env = st.envelope;
    tl.A = attackSpanSeconds(st);
    tl.rel = static_cast<double>(env.releaseMs.load(kR)) / 1000.0;

    Krate::DSP::VoragoMacroValues knobs{};
    knobs.darkness = st.macros.darkness.load(kR);
    knobs.age = st.macros.age.load(kR);
    knobs.density = st.macros.density.load(kR);
    knobs.movement = st.macros.movement.load(kR);
    knobs.gravity = st.macros.gravity.load(kR);
    knobs.entropy = st.macros.entropy.load(kR);
    knobs.pressure = st.macros.pressure.load(kR);
    knobs.weight = st.macros.weight.load(kR);
    knobs.fog = st.macros.fog.load(kR);
    knobs.life = st.macros.life.load(kR);
    knobs.depth = st.macros.depth.load(kR);
    knobs.mass = st.macros.mass.load(kR);
    tl.rt60 = static_cast<double>(
        ::Vorago::effectiveCavernDecaySeconds(knobs, st.space.decaySeconds.load(kR)));

    tl.sus0 = tl.A + 5.0;
    tl.sus1 = tl.A + 65.0;
    for (int k = 0; k < 3; ++k) {
        tl.m[k][0] = tl.A + 5.0 + (60.0 * static_cast<double>(k));
        tl.m[k][1] = tl.A + 65.0 + (60.0 * static_cast<double>(k));
    }
    tl.H = tl.A + 185.0;

    tl.freezeOnTail = freezeGesture || st.space.freeze.load(kR) != 0;
    if (tl.freezeOnTail) {
        tl.tail0 = tl.H + tl.rel + 10.0;
        tl.tail1 = tl.H + tl.rel + 70.0;
    } else {
        tl.tail0 = tl.H + tl.rel + tl.rt60 + 5.0;
        tl.tail1 = tl.H + tl.rel + tl.rt60 + 15.0;
    }
    tl.total = tl.tail1;
    return tl;
}

}  // namespace VoragoTest

// ==============================================================================
// C2 part 2b (T026, plan 5.8 Descriptor / Outcomes / Vector rows, 6.4, 6.10,
// 6.12; C-7.2, C-7.4, FR-011a, FR-075): the synthetic descriptor signal, the
// verification outcome types, verifiedAt, findWitness, and every decoded-state
// predicate the verification vector needs. Still Catch2-free.
// ==============================================================================

#include "vorago_preset_defs.h"

namespace VoragoTest {

// ---- Synthetic descriptor signal (T026; T036's level-twin case reuses it) -------

/// A fixed, deterministic stereo signal: 65.4 Hz + 440 Hz sines under a slow
/// linear amplitude ramp (0.1 -> 0.5 across the whole signal); R carries the same
/// sines phase-shifted by pi/3.
inline void makeDescriptorTestSignal(std::size_t numSamples, double sr, std::vector<float>& L,
                                     std::vector<float>& R) {
    constexpr double kTwoPi = 6.283185307179586;
    constexpr double kShift = 1.0471975511965976;  // pi / 3
    L.assign(numSamples, 0.0f);
    R.assign(numSamples, 0.0f);
    const double rampLen = (numSamples > 1u) ? static_cast<double>(numSamples - 1u) : 1.0;
    for (std::size_t i = 0; i < numSamples; ++i) {
        const double t = static_cast<double>(i) / sr;
        const double amp = 0.1 + (0.4 * (static_cast<double>(i) / rampLen));
        L[i] = static_cast<float>(amp * ((0.5 * std::sin(kTwoPi * 65.4 * t)) +
                                         (0.3 * std::sin(kTwoPi * 440.0 * t))));
        R[i] = static_cast<float>(amp * ((0.5 * std::sin((kTwoPi * 65.4 * t) + kShift)) +
                                         (0.3 * std::sin((kTwoPi * 440.0 * t) + kShift))));
    }
}

// ---- Outcomes (plan 5.8, 6.12) ----------------------------------------------------

/// The attributability margin of plan 6.8 / 6.9 (`d >= attribBase + 1.5`).
inline constexpr double kAttribMargin = 1.5;

enum class ClaimRole : std::uint8_t { Secondary, Primary };

/// One cell's raw verification terms. Render-scored kinds read d / twoS /
/// attribBase; a state-only kind records rendered = false, d = 0, twoS = 0 and
/// skip = "state-only kind", and never reads them (plan 6.12).
struct CellOutcome {
    bool stateOk = false;
    bool conjunctOk = false;
    bool rendered = false;
    double d = 0.0;
    double twoS = 0.0;
    double attribBase = -1.0;  ///< < 0: no attributability term
    std::string skip;
};

namespace detail {

/// R(bar) of plan 6.12 as amended at gate G2 (user ruling 2026-09-29): stateOk
/// && conjunctOk && rendered && d >= bar && (attribBase < 0 || d >= attribBase
/// + 1.5). The 2 s(P) term is RECORDED in twoS but no longer gated: a twin is a
/// same-seed, same-window render, so the preset's own minute-to-minute
/// evolution (s(P)) and its reseed distance (t_1) are common to both sides and
/// are not this comparison's noise; the pilot measured real effects (S5 6.9,
/// D1.1 3.5) failing bars of 8.7-9.9 set by s(P) alone. Positive comparisons,
/// so a NaN term fails.
[[nodiscard]] inline bool renderScored(const CellOutcome& o, double bar) noexcept {
    if (!(o.stateOk && o.conjunctOk && o.rendered)) {
        return false;
    }
    if (!(o.d >= bar)) {
        return false;
    }
    return o.attribBase < 0.0 || o.d >= o.attribBase + kAttribMargin;
}

}  // namespace detail

/// Plan 6.12 "`verifiedAt` by kind". Secondary bar 1.5, primary bar F = 4.0.
/// For `StateWithS` at Primary this is the D1.x per-material-reversion rule and,
/// since the ruling of 2026-09-30, the D3.x rule on the S1 conjunct's copied
/// render terms; the Capability overload below rejects every other `StateWithS`
/// primary.
[[nodiscard]] inline bool verifiedAt(const CellOutcome& o, ::Vorago::PresetDefs::Verification v,
                                     ClaimRole role) noexcept {
    using V = ::Vorago::PresetDefs::Verification;
    const bool primary = (role == ClaimRole::Primary);
    const double bar = primary ? kFloorF : kSecondaryBar;
    switch (v) {
        case V::Ablation:
        case V::RouteIsolated:
        case V::ExtReversion:
        case V::StateWithReversion:
            return detail::renderScored(o, bar);
        case V::StateWithS:
            if (!primary) {
                return o.stateOk && o.conjunctOk;  // no d term
            }
            return o.stateOk && o.conjunctOk && o.rendered && o.d >= kFloorF;  // G2: bar only
        case V::AttackWindow:
            return primary ? detail::renderScored(o, kFloorF) : o.stateOk;  // secondary: no d term
        case V::FreezeGesture:
            return !primary && o.stateOk && o.conjunctOk;  // never primary (FR-011b)
        case V::StateOnly:
            return !primary && o.stateOk;  // default-state by construction
    }
    return false;
}

/// verifiedAt dispatched on the cell's kind (`cellSpecs()[c].verification`).
/// At Primary: a `StateWithS` cell other than D1.x or D3.x is false (plan 6.12;
/// D3.x admitted by the ruling of 2026-09-30, scored on its S1 conjunct's render
/// terms copied in by computeVerificationVector's pass 3), and an
/// `AttackWindow` cell other than D8.2 / D9.1 is false (only those two have the
/// attack-window reversion, plan 6.8).
[[nodiscard]] inline bool verifiedAt(const CellOutcome& o, ::Vorago::PresetDefs::Capability c,
                                     ClaimRole role) noexcept {
    using C = ::Vorago::PresetDefs::Capability;
    using V = ::Vorago::PresetDefs::Verification;
    const auto i = static_cast<std::size_t>(c);
    if (i >= ::Vorago::PresetDefs::kNumCapabilities) {
        return false;
    }
    const V v = ::Vorago::PresetDefs::cellSpecs()[i].verification;
    if (role == ClaimRole::Primary) {
        const bool d1 = i >= static_cast<std::size_t>(C::D1Glass) &&
                        i <= static_cast<std::size_t>(C::D1GlassSphere);
        const bool d3 = i >= static_cast<std::size_t>(C::D3Direct) &&
                        i <= static_cast<std::size_t>(C::D3MetallicHiss);
        if (v == V::StateWithS && !d1 && !d3) {
            return false;
        }
        if (v == V::AttackWindow && c != C::D8Growth && c != C::D9FastAttack) {
            return false;
        }
    }
    return verifiedAt(o, v, role);
}

// ---- Vector (plan 5.8, 6.12) --------------------------------------------------------

/// One CellOutcome per Capability, indexed by the enum value.
struct VerificationVector {
    std::array<CellOutcome, ::Vorago::PresetDefs::kNumCapabilities> cells{};
};

/// FR-011a witness search (plan 6.12): scan P's claims - the primary first, then
/// the secondaries in definition order - and return the first one Q does not
/// verify. Q verifies c iff verifiedAt(Q.vec[c], c, Secondary), and, when c is
/// Q's own primary, also verifiedAt(Q.vec[c], c, Primary). std::nullopt means
/// SUBSET (a failure). `qPrimary` is Capability::Count for the default-surface
/// pseudo-preset.
[[nodiscard]] inline std::optional<::Vorago::PresetDefs::Capability> findWitness(
    const ::Vorago::PresetDefs::VoragoPresetDef& p, const VerificationVector& q,
    ::Vorago::PresetDefs::Capability qPrimary) {
    using C = ::Vorago::PresetDefs::Capability;
    const auto verifies = [&q, qPrimary](C c) {
        const auto i = static_cast<std::size_t>(c);
        if (i >= q.cells.size()) {
            return false;
        }
        const CellOutcome& o = q.cells[i];
        if (!verifiedAt(o, c, ClaimRole::Secondary)) {
            return false;
        }
        return c != qPrimary || verifiedAt(o, c, ClaimRole::Primary);
    };
    if (!verifies(p.primary)) {
        return p.primary;
    }
    for (const C c : p.secondaries) {
        if (!verifies(c)) {
            return c;
        }
    }
    return std::nullopt;
}

// ---- State predicates (plan 6.7, 6.10; FR-031, FR-075) ------------------------------
// Stored floats are compared in float against float thresholds, the precision
// the state holds (0.35f as a double is below 0.35; 0.3f is above 0.3).

namespace detail {

inline constexpr float kMacroDisplacement = 0.5f;     // M: |v - 0| >= 0.5 (default 0)
inline constexpr float kGravityDisplacement = 0.35f;  // M5: |g - 0.5| >= 0.35
inline constexpr float kGravityDefault = 0.5f;
inline constexpr float kBlendAHeardMax = 0.65f;  // D1 via A, D2 upper
inline constexpr float kBlendBHeardMin = 0.35f;  // D1 via B, D2 lower
inline constexpr double kFastAttackMaxS = 10.0;  // D9.1
inline constexpr double kSlowAttackMinS = 90.0;  // D9.2
inline constexpr float kGhostReverseMin = 0.5f;  // D11
inline constexpr float kSlowEventsMax = 0.3f;    // D13.1
inline constexpr float kFastEventsMin = 3.0f;    // D13.2
inline constexpr float kLifeDepthMin = 0.7f;     // D14.1 / D14.2
inline constexpr double kExtSideFraction = 0.5;  // FR-075: n >= n0 + 0.5 (1 - n0)

[[nodiscard]] inline bool capInRange(::Vorago::PresetDefs::Capability c,
                                     ::Vorago::PresetDefs::Capability first,
                                     ::Vorago::PresetDefs::Capability last) noexcept {
    const auto v = static_cast<std::size_t>(c);
    return v >= static_cast<std::size_t>(first) && v <= static_cast<std::size_t>(last);
}

/// Offset of c within the family starting at `first` (the caller checked the range).
[[nodiscard]] inline int capOffset(::Vorago::PresetDefs::Capability c,
                                   ::Vorago::PresetDefs::Capability first) noexcept {
    return static_cast<int>(c) - static_cast<int>(first);
}

}  // namespace detail

/// The M displacement conjunct (plan 6.7): the stored macro is displaced from its
/// registered default by >= 0.5 (Gravity, default 0.5: |g - 0.5| >= 0.35). False
/// for a non-M cell.
[[nodiscard]] inline bool macroDisplaced(::Vorago::PresetDefs::Capability c,
                                         const DecodedPresetState& st) {
    using C = ::Vorago::PresetDefs::Capability;
    if (!detail::capInRange(c, C::M1Darkness, C::M12Mass)) {
        return false;
    }
    const float v = ::Vorago::macroField(st.macros, detail::capOffset(c, C::M1Darkness))
                        .load(std::memory_order_relaxed);
    if (c == C::M5Gravity) {
        return std::fabs(v - detail::kGravityDefault) >= detail::kGravityDisplacement;
    }
    return std::fabs(v) >= detail::kMacroDisplacement;
}

/// The FR-075 side predicate (plan 6.7, Q7): the decoded NORMALIZED value n of
/// 901 (E6.hi) / 902 (E7.hi) satisfies n >= n0 + 0.5 (1 - n0), n0 the registered
/// default (901: n >= 0.5, plain 0.25; 902: n >= 0.625, plain +0.5). False for any
/// other cell.
[[nodiscard]] inline bool extSidePredicate(::Vorago::PresetDefs::Capability c,
                                           const DecodedPresetState& st) {
    using C = ::Vorago::PresetDefs::Capability;
    constexpr auto kR = std::memory_order_relaxed;
    const auto passes = [](double plain, double def, double mn, double mx) {
        const double n = ::Vorago::linearToNormalized(plain, mn, mx);
        const double n0 = ::Vorago::linearToNormalized(def, mn, mx);
        return n >= n0 + (detail::kExtSideFraction * (1.0 - n0));
    };
    if (c == C::E6SyncRateHi) {
        return passes(static_cast<double>(st.ecosystem.syncRate.load(kR)),
                      ::Vorago::kEcosystemSyncRateDefault, ::Vorago::kEcosystemSyncRateMin,
                      ::Vorago::kEcosystemSyncRateMax);
    }
    if (c == C::E7SelfAffinityHi) {
        return passes(static_cast<double>(st.ecosystem.selfAffinity.load(kR)),
                      ::Vorago::kEcosystemSelfAffinityDefault, ::Vorago::kEcosystemSelfAffinityMin,
                      ::Vorago::kEcosystemSelfAffinityMax);
    }
    return false;
}

/// The `stateOk` term of every cell from the decoded state (plan 6.10, 6.12):
/// S and E1-E5 -> true; M -> macroDisplaced; E6.hi / E7.hi -> extSidePredicate;
/// every D row of the plan 6.10 table. D10.1's predicate ("P is the S8-primary
/// preset") is a property of the definition, not of the state: this returns
/// false for it, and the harness sets that entry's stateOk from the def (T036).
[[nodiscard]] inline bool statePredicate(::Vorago::PresetDefs::Capability c,
                                         const DecodedPresetState& st) {
    using C = ::Vorago::PresetDefs::Capability;
    using detail::capInRange;
    using detail::capOffset;
    constexpr auto kR = std::memory_order_relaxed;

    if (capInRange(c, C::S1Noise, C::S10Body) ||
        capInRange(c, C::E1PartialBloom, C::E5GhostBursts)) {
        return true;
    }
    if (capInRange(c, C::M1Darkness, C::M12Mass)) {
        return macroDisplaced(c, st);
    }
    if (c == C::E6SyncRateHi || c == C::E7SelfAffinityHi) {
        return extSidePredicate(c, st);
    }

    const float blend = st.body.blend.load(kR);
    if (capInRange(c, C::D1Glass, C::D1GlassSphere)) {
        const int x = capOffset(c, C::D1Glass);  // == BodyMaterial index
        return (st.body.materialA.load(kR) == x && blend <= detail::kBlendAHeardMax) ||
               (st.body.materialB.load(kR) == x && blend >= detail::kBlendBHeardMin);
    }
    if (capInRange(c, C::D3Direct, C::D3MetallicHiss)) {
        const int m = capOffset(c, C::D3Direct);  // == NoiseOrganismModel index
        return std::ranges::any_of(st.noise.model,
                                   [m](const std::atomic<int>& a) {
                                       return a.load(std::memory_order_relaxed) == m;
                                   });
    }
    if (capInRange(c, C::D4Type1, C::D4Type12)) {
        const int t = capOffset(c, C::D4Type1);  // == kNoiseTypeByIndex index (label t - 1)
        constexpr int kDirect = static_cast<int>(Krate::DSP::NoiseOrganismModel::Direct);
        for (std::size_t s = 0; s < st.noise.model.size(); ++s) {
            if (st.noise.model[s].load(kR) == kDirect && st.noise.type[s].load(kR) == t) {
                return true;
            }
        }
        return false;
    }
    if (capInRange(c, C::D5Free, C::D5Hybrid)) {
        return st.resonance.anchorMode.load(kR) == capOffset(c, C::D5Free);  // AnchorMode index
    }
    if (capInRange(c, C::D6Lowpass, C::D6Highpass)) {
        const int f = capOffset(c, C::D6Lowpass);  // == FeedbackEcology::FilterMode index
        return std::ranges::any_of(st.ecology.loopFilterMode,
                                   [f](const std::atomic<int>& a) {
                                       return a.load(std::memory_order_relaxed) == f;
                                   });
    }
    if (capInRange(c, C::D7Div2, C::D7FifthBelow)) {
        const std::array<float, 3> lv{st.sub.div2LevelDb.load(kR), st.sub.div4LevelDb.load(kR),
                                      st.sub.fifthBelowLevelDb.load(kR)};
        const auto k = static_cast<std::size_t>(capOffset(c, C::D7Div2));
        for (std::size_t j = 0; j < lv.size(); ++j) {
            if (j != k && !(lv[k] > lv[j])) {
                return false;  // strictly the loudest: ties fail
            }
        }
        return true;
    }

    switch (c) {
        case C::D2BlendBoth:
            return blend >= detail::kBlendBHeardMin && blend <= detail::kBlendAHeardMax;
        case C::D8Standard:
            return st.envelope.mode.load(kR) ==
                   static_cast<int>(Krate::DSP::VoragoVoice::EnvelopeMode::Standard);
        case C::D8Growth:
            return st.envelope.mode.load(kR) ==
                   static_cast<int>(Krate::DSP::VoragoVoice::EnvelopeMode::Growth);
        case C::D9FastAttack:
            return attackSpanSeconds(st) <= detail::kFastAttackMaxS;
        case C::D9SlowAttack:
            return attackSpanSeconds(st) >= detail::kSlowAttackMinS;
        case C::D10FreezeHolds:
            return false;  // a property of the definition (see above)
        case C::D10FreezeOff:
            return st.space.freeze.load(kR) == 0;
        case C::D11GhostReverse:
            return st.ghost.reverseProbability.load(kR) >= detail::kGhostReverseMin;
        case C::D12TriggersOn:
            return st.ghost.eventTriggers.load(kR) == 1;
        case C::D12TriggersOff:
            return st.ghost.eventTriggers.load(kR) == 0;
        case C::D13SlowEvents:
            return st.events.eventRateScale.load(kR) <= detail::kSlowEventsMax;
        case C::D13FastEvents:
            return st.events.eventRateScale.load(kR) >= detail::kFastEventsMin;
        case C::D14Breathing:
            return st.life.breathingDepth.load(kR) >= detail::kLifeDepthMin;
        case C::D14Tidal:
            return st.life.tidalDepth.load(kR) >= detail::kLifeDepthMin;
        default:
            return false;
    }
}

}  // namespace VoragoTest

// ==============================================================================
// D (T033, plan 5.8 rows Record / Sharding): the per-preset sweep record, its
// text round-trip, and CI sharding. Still Catch2-free. A record is a transient
// CI artifact: never committed and never compared across toolchains, so it is
// not a golden (C-8). Doubles are written %.17g so the text round-trip inside
// one binary is exact.
// ==============================================================================

#include <charconv>
#include <cstdio>
#include <sstream>
#include <system_error>

namespace VoragoTest {

// ---- Record (plan 5.8) ----------------------------------------------------------

/// One take of the K-take main render (plan 5.8, 6.3): its seed, arm figures, the
/// arm 1-4 verdicts (T034; the Freeze-On arm 4 reads window figures the record
/// does not keep, so the verdicts are recorded) and the three minute descriptors
/// M1..M3. `attack` is the plan 6.8 D_att of [0, W_end], present only when the
/// take was rendered with an attack window; it is consumed in-process (T035) and
/// is NOT written to the record.
struct TakeRecord {
    int seedIndex = -1;
    bool finite = false;
    float peak = 0.0f;
    double worstHiDb = 0.0;
    double worstLoDb = 0.0;
    double lateVsSusDb = 0.0;
    double tailDb = 0.0;
    std::array<bool, 4> armPass{};  ///< arms 1..4 (plan 6.3)
    std::array<PresetDescriptor, 3> minutes{};
    std::optional<PresetDescriptor> attack;
    // In-process only (T035), NOT written to the record: RMS(Sus) of this take
    // (the plan 6.8 floorDb = susDb - 60 for P_rev too) and, with an attack
    // window, the printed time to first reach RMS(Sus) - 6 dB (plan 6.8).
    double susDb = 0.0;
    std::optional<double> attackReachSeconds;
    // In-process only (T036), NOT written per take: the plan 6.13 (a) level twin
    // of this take's M1..M3 buffers (levelTwinD); the record keeps the stored-seed
    // take's value as SweepRecord::levelTwinD.
    double levelTwinD = 0.0;
};

/// The freeze gesture G and its dry-residue twin G0 (plan 6.3, T036).
struct GestureResult {
    double loudestDb = 0.0;
    double lastDb = 0.0;
    double floorDb = 0.0;
    double dryLoudestDb = 0.0;
    bool pass = false;
};

/// The FR-033a alternate-rate renders, arm 1 only (T036).
struct RateResult {
    bool finite441 = false;
    float peak441 = 0.0f;
    double worstHi441 = 0.0;
    bool finite96 = false;
    float peak96 = 0.0f;
    double worstHi96 = 0.0;
};

/// One preset's (or the default surface's) complete sweep result. `takeCount`
/// is the K the record was computed with (the plan's `int takes`, renamed
/// because the take vector already holds that name).
struct SweepRecord {
    std::string name;  ///< single line (preset names carry no newline)
    int takeCount = 0;
    SweepTimeline tl;
    std::vector<TakeRecord> takes;
    PresetDescriptor mean;
    double selfDistance = 0.0;
    double levelTwinD = 0.0;
    // T036: the stored Pressure (106) and Weight (107) macro plain values, which
    // the plan 6.13 control-set rule reads in the aggregate job (records only).
    double storedPressure = 0.0;
    double storedWeight = 0.0;
    VerificationVector vec;
    GestureResult gesture;
    RateResult rates;
    bool reproducible = false;
};

namespace detail {

inline constexpr const char* kRecordMagic = "vorago-sweep-record";
inline constexpr const char* kRecordVersion = "2";  // 2: T036 storedMacros line
inline constexpr int kRecordMaxTakes = 4096;  // sanity bound on a parsed take count

[[nodiscard]] inline std::string recordNum(double v) {
    std::array<char, 48> buf{};
    const int n = std::snprintf(buf.data(), buf.size(), "%.17g", v);
    return std::string(buf.data(), (n > 0) ? static_cast<std::size_t>(n) : 0u);
}

inline void writeRecordNums(std::ostream& out, const char* key, std::span<const double> vals) {
    out << key;
    for (const double v : vals) {
        out << ' ' << recordNum(v);
    }
    out << '\n';
}

[[nodiscard]] inline std::array<double, kDescriptorComponents> descriptorToArray(
    const PresetDescriptor& d) {
    std::array<double, kDescriptorComponents> a{};
    for (std::size_t k = 0; k < kDescriptorBands; ++k) {
        a[k] = d.band[k];
    }
    a[kDescriptorBands + 0u] = d.motion;
    a[kDescriptorBands + 1u] = d.flux;
    a[kDescriptorBands + 2u] = d.corr;
    a[kDescriptorBands + 3u] = d.energySpread;
    a[kDescriptorBands + 4u] = d.crest;
    return a;
}

[[nodiscard]] inline PresetDescriptor descriptorFromArray(
    const std::array<double, kDescriptorComponents>& a) {
    PresetDescriptor d;
    for (std::size_t k = 0; k < kDescriptorBands; ++k) {
        d.band[k] = a[k];
    }
    d.motion = a[kDescriptorBands + 0u];
    d.flux = a[kDescriptorBands + 1u];
    d.corr = a[kDescriptorBands + 2u];
    d.energySpread = a[kDescriptorBands + 3u];
    d.crest = a[kDescriptorBands + 4u];
    return d;
}

[[nodiscard]] inline double recordBool(bool b) noexcept { return b ? 1.0 : 0.0; }

/// The record's lines and a read position. Every read consumes exactly one line
/// whose key matches, so a truncated or reordered file fails.
struct RecordCursor {
    std::vector<std::string> lines;
    std::size_t pos = 0;
};

/// Consumes the next line if it is `key` or `key <rest>`; `rest` is the text after
/// the single separating space (possibly empty).
[[nodiscard]] inline bool takeRecordLine(RecordCursor& c, std::string_view key, std::string& rest) {
    if (c.pos >= c.lines.size()) {
        return false;
    }
    const std::string& line = c.lines[c.pos];
    if (line.size() < key.size() || std::string_view(line).substr(0, key.size()) != key) {
        return false;
    }
    if (line.size() == key.size()) {
        rest.clear();
    } else if (line[key.size()] == ' ') {
        rest = line.substr(key.size() + 1u);
    } else {
        return false;
    }
    ++c.pos;
    return true;
}

/// Consumes the next line `key v0 v1 ...` with exactly out.size() numbers.
[[nodiscard]] inline bool readRecordNumbers(RecordCursor& c, std::string_view key,
                                            std::span<double> out) {
    std::string rest;
    if (!takeRecordLine(c, key, rest)) {
        return false;
    }
    std::istringstream iss(rest);
    for (double& v : out) {
        std::string tok;
        if (!(iss >> tok)) {
            return false;
        }
        char* end = nullptr;
        v = std::strtod(tok.c_str(), &end);
        if (end != tok.c_str() + tok.size()) {
            return false;
        }
    }
    std::string extra;
    return !(iss >> extra);
}

[[nodiscard]] inline bool readRecordNumber(RecordCursor& c, std::string_view key, double& v) {
    return readRecordNumbers(c, key, std::span<double>(&v, 1u));
}

[[nodiscard]] inline bool readRecordDescriptor(RecordCursor& c, std::string_view key,
                                               PresetDescriptor& d) {
    std::array<double, kDescriptorComponents> a{};
    if (!readRecordNumbers(c, key, a)) {
        return false;
    }
    d = descriptorFromArray(a);
    return true;
}

}  // namespace detail

/// Writes `rec` as text `key value...` lines (doubles %.17g), ending with `end`.
inline void writeRecord(const SweepRecord& rec, const std::filesystem::path& path) {
    using detail::recordBool;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << detail::kRecordMagic << ' ' << detail::kRecordVersion << '\n';
    out << "name " << rec.name << '\n';
    out << "takeCount " << rec.takeCount << '\n';

    const SweepTimeline& tl = rec.tl;
    const std::array<double, 16> timeline{tl.A,       tl.rel,     tl.rt60,    tl.sus0,
                                          tl.sus1,    tl.m[0][0], tl.m[0][1], tl.m[1][0],
                                          tl.m[1][1], tl.m[2][0], tl.m[2][1], tl.H,
                                          tl.tail0,   tl.tail1,   tl.total,   recordBool(tl.freezeOnTail)};
    detail::writeRecordNums(out, "timeline", timeline);

    out << "takes " << rec.takes.size() << '\n';
    for (const TakeRecord& t : rec.takes) {
        const std::array<double, 11> take{static_cast<double>(t.seedIndex),
                                          recordBool(t.finite),
                                          static_cast<double>(t.peak),
                                          t.worstHiDb,
                                          t.worstLoDb,
                                          t.lateVsSusDb,
                                          t.tailDb,
                                          recordBool(t.armPass[0]),
                                          recordBool(t.armPass[1]),
                                          recordBool(t.armPass[2]),
                                          recordBool(t.armPass[3])};
        detail::writeRecordNums(out, "take", take);
        for (const PresetDescriptor& m : t.minutes) {
            detail::writeRecordNums(out, "minute", detail::descriptorToArray(m));
        }
    }
    detail::writeRecordNums(out, "mean", detail::descriptorToArray(rec.mean));
    out << "selfDistance " << detail::recordNum(rec.selfDistance) << '\n';
    out << "levelTwinD " << detail::recordNum(rec.levelTwinD) << '\n';
    const std::array<double, 2> storedMacros{rec.storedPressure, rec.storedWeight};
    detail::writeRecordNums(out, "storedMacros", storedMacros);

    out << "cells " << rec.vec.cells.size() << '\n';
    for (const CellOutcome& o : rec.vec.cells) {
        const std::array<double, 6> cell{recordBool(o.stateOk), recordBool(o.conjunctOk),
                                         recordBool(o.rendered), o.d,
                                         o.twoS,                 o.attribBase};
        detail::writeRecordNums(out, "cell", cell);
        out << "skip " << o.skip << '\n';
    }

    const GestureResult& g = rec.gesture;
    const std::array<double, 5> gesture{g.loudestDb, g.lastDb, g.floorDb, g.dryLoudestDb,
                                        recordBool(g.pass)};
    detail::writeRecordNums(out, "gesture", gesture);

    const RateResult& r = rec.rates;
    const std::array<double, 6> rates{recordBool(r.finite441), static_cast<double>(r.peak441),
                                      r.worstHi441,            recordBool(r.finite96),
                                      static_cast<double>(r.peak96), r.worstHi96};
    detail::writeRecordNums(out, "rates", rates);

    out << "reproducible " << (rec.reproducible ? 1 : 0) << '\n';
    out << "end\n";
}

/// Reads a record written by writeRecord. False (and `out` untouched) on a missing,
/// truncated or malformed file.
[[nodiscard]] inline bool readRecord(const std::filesystem::path& path, SweepRecord& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in.good()) {
        return false;
    }
    detail::RecordCursor c;
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        c.lines.push_back(std::move(line));
        line.clear();
    }

    SweepRecord rec;
    std::string rest;
    if (!detail::takeRecordLine(c, detail::kRecordMagic, rest) || rest != detail::kRecordVersion) {
        return false;
    }
    if (!detail::takeRecordLine(c, "name", rec.name)) {
        return false;
    }
    double num = 0.0;
    if (!detail::readRecordNumber(c, "takeCount", num)) {
        return false;
    }
    rec.takeCount = static_cast<int>(num);

    std::array<double, 16> timeline{};
    if (!detail::readRecordNumbers(c, "timeline", timeline)) {
        return false;
    }
    SweepTimeline& tl = rec.tl;
    tl.A = timeline[0];
    tl.rel = timeline[1];
    tl.rt60 = timeline[2];
    tl.sus0 = timeline[3];
    tl.sus1 = timeline[4];
    tl.m[0][0] = timeline[5];
    tl.m[0][1] = timeline[6];
    tl.m[1][0] = timeline[7];
    tl.m[1][1] = timeline[8];
    tl.m[2][0] = timeline[9];
    tl.m[2][1] = timeline[10];
    tl.H = timeline[11];
    tl.tail0 = timeline[12];
    tl.tail1 = timeline[13];
    tl.total = timeline[14];
    tl.freezeOnTail = timeline[15] != 0.0;

    if (!detail::readRecordNumber(c, "takes", num) || !(num >= 0.0) ||
        num > static_cast<double>(detail::kRecordMaxTakes)) {
        return false;
    }
    rec.takes.resize(static_cast<std::size_t>(num));
    for (TakeRecord& t : rec.takes) {
        std::array<double, 11> take{};
        if (!detail::readRecordNumbers(c, "take", take)) {
            return false;
        }
        t.seedIndex = static_cast<int>(take[0]);
        t.finite = take[1] != 0.0;
        t.peak = static_cast<float>(take[2]);
        t.worstHiDb = take[3];
        t.worstLoDb = take[4];
        t.lateVsSusDb = take[5];
        t.tailDb = take[6];
        for (std::size_t a = 0; a < t.armPass.size(); ++a) {
            t.armPass[a] = take[7u + a] != 0.0;
        }
        for (PresetDescriptor& m : t.minutes) {
            if (!detail::readRecordDescriptor(c, "minute", m)) {
                return false;
            }
        }
    }
    if (!detail::readRecordDescriptor(c, "mean", rec.mean) ||
        !detail::readRecordNumber(c, "selfDistance", rec.selfDistance) ||
        !detail::readRecordNumber(c, "levelTwinD", rec.levelTwinD)) {
        return false;
    }
    std::array<double, 2> storedMacros{};
    if (!detail::readRecordNumbers(c, "storedMacros", storedMacros)) {
        return false;
    }
    rec.storedPressure = storedMacros[0];
    rec.storedWeight = storedMacros[1];

    if (!detail::readRecordNumber(c, "cells", num) ||
        num != static_cast<double>(rec.vec.cells.size())) {
        return false;
    }
    for (CellOutcome& o : rec.vec.cells) {
        std::array<double, 6> cell{};
        if (!detail::readRecordNumbers(c, "cell", cell)) {
            return false;
        }
        o.stateOk = cell[0] != 0.0;
        o.conjunctOk = cell[1] != 0.0;
        o.rendered = cell[2] != 0.0;
        o.d = cell[3];
        o.twoS = cell[4];
        o.attribBase = cell[5];
        if (!detail::takeRecordLine(c, "skip", o.skip)) {
            return false;
        }
    }

    std::array<double, 5> gesture{};
    if (!detail::readRecordNumbers(c, "gesture", gesture)) {
        return false;
    }
    rec.gesture.loudestDb = gesture[0];
    rec.gesture.lastDb = gesture[1];
    rec.gesture.floorDb = gesture[2];
    rec.gesture.dryLoudestDb = gesture[3];
    rec.gesture.pass = gesture[4] != 0.0;

    std::array<double, 6> rates{};
    if (!detail::readRecordNumbers(c, "rates", rates)) {
        return false;
    }
    rec.rates.finite441 = rates[0] != 0.0;
    rec.rates.peak441 = static_cast<float>(rates[1]);
    rec.rates.worstHi441 = rates[2];
    rec.rates.finite96 = rates[3] != 0.0;
    rec.rates.peak96 = static_cast<float>(rates[4]);
    rec.rates.worstHi96 = rates[5];

    if (!detail::readRecordNumber(c, "reproducible", num)) {
        return false;
    }
    rec.reproducible = num != 0.0;

    if (!detail::takeRecordLine(c, "end", rest) || !rest.empty()) {
        return false;
    }
    out = std::move(rec);
    return true;
}

// ---- Sharding (plan 5.8) ----------------------------------------------------------

/// A CI shard `index/count`. Definition index N (= allPresets().size()) is the
/// default-surface pseudo-preset. {0, 0} marks a malformed value; the calling
/// TEST_CASE REQUIREs count > 0.
struct Shard {
    std::size_t index = 0;
    std::size_t count = 0;
    friend bool operator==(const Shard&, const Shard&) = default;
};

/// Pure parse of "i/n" (decimal digits only, 0 <= i < n); anything else -> {0, 0}.
[[nodiscard]] inline Shard parseShard(std::string_view s) {
    const std::size_t slash = s.find('/');
    if (slash == std::string_view::npos) {
        return {};
    }
    const auto parsePart = [](std::string_view p, std::size_t& v) {
        if (p.empty()) {
            return false;
        }
        const char* last = p.data() + p.size();
        const auto [ptr, ec] = std::from_chars(p.data(), last, v);
        return ec == std::errc{} && ptr == last;
    };
    std::size_t index = 0;
    std::size_t count = 0;
    if (!parsePart(s.substr(0, slash), index) || !parsePart(s.substr(slash + 1u), count) ||
        count == 0u || index >= count) {
        return {};
    }
    return Shard{index, count};
}

/// VORAGO_SWEEP_SHARD, default "0/1" when unset.
[[nodiscard]] inline Shard shardFromEnv() {
    return parseShard(sweepEnv("VORAGO_SWEEP_SHARD").value_or("0/1"));
}

/// True iff `defIndex` belongs to shard `s` (defIndex mod count == index).
[[nodiscard]] inline bool inShard(std::size_t defIndex, Shard s) noexcept {
    return s.count > 0u && (defIndex % s.count) == s.index;
}

}  // namespace VoragoTest

// ==============================================================================
// D (T034, plan 6.2 / 6.3, C-6, FR-033): 10 s windows, the four per-take arms,
// and the K-take main render. Still Catch2-free. Windows and spans are measured
// from SweepCapture's per-512-block power sums, snapped inward to block edges
// (<= 511 samples of slack per edge).
// ==============================================================================

namespace VoragoTest {

// ---- Arm constants (plan 6.3; kFreezeFloorDb / kTailDropDb are above) -----------
inline constexpr double kSweepSampleRate = 48000.0;    ///< every main-take render (C-6)
inline constexpr float kSweepPeakCeiling = 0.9661f;    ///< 10^(-0.3/20), arm 1
inline constexpr double kArmWindowSeconds = 10.0;      ///< "every 10 s window"
inline constexpr double kRunawayDb = -6.0;             ///< arm 1: every window <=
inline constexpr double kSilenceDb = -60.0;            ///< arm 2: every window >=
inline constexpr double kLateSeconds = 60.0;           ///< arm 3: [H - 60, H]
inline constexpr double kLateLoDb = -18.0;             ///< arm 3 lower bound
inline constexpr double kLateHiDb = 12.0;              ///< arm 3 upper bound
inline constexpr double kFreezeGrowthDb = 1.0;         ///< arm 4 On: non-growing
inline constexpr double kFreezeHoldDb = 6.0;           ///< arm 4 On: held, not dying
inline constexpr double kAttackFloorBelowSusDb = 60.0; ///< plan 6.8 floorDb = RMS(Sus) - 60
inline constexpr double kAttackReachBelowSusDb = 6.0;  ///< plan 6.8 printed: first reach RMS(Sus) - 6
inline constexpr double kPowerFloor = 1e-24;           ///< 10*log10(max(p, 1e-24)): -240 dB

// ---- Windows (plan 6.2) ------------------------------------------------------------
/// One window in seconds and its inward block snap: blocks [firstBlock, endBlock)
/// of 512 samples, firstBlock * 512 >= llround(t0 * sr), endBlock * 512 <=
/// llround(t1 * sr).
struct SweepWindow {
    double t0 = 0.0;
    double t1 = 0.0;
    std::size_t firstBlock = 0;
    std::size_t endBlock = 0;
};

namespace detail {

/// Tolerance for the floor((b - a) / 10) count, so a span that is a multiple of
/// 10 s up to rounding (e.g. the 60 s Freeze-On tail) gets no remainder window.
inline constexpr double kWindowEpsilonSeconds = 1e-9;

[[nodiscard]] inline SweepWindow snapWindowInward(double t0, double t1, double sr) {
    const long long block = kRenderBlock;
    const long long s0 = std::max(0LL, std::llround(t0 * sr));
    const long long s1 = std::max(s0, std::llround(t1 * sr));
    const long long first = (s0 + block - 1) / block;
    const long long end = std::max(first, s1 / block);
    return SweepWindow{t0, t1, static_cast<std::size_t>(first), static_cast<std::size_t>(end)};
}

/// Stereo power (sumL + sumR) / (2n) over blocks [first, end) in dB,
/// 10*log10(max(p, 1e-24)); blocks past the capture count as silent.
[[nodiscard]] inline double spanPowerDb(const SweepCapture& cap, std::size_t first,
                                        std::size_t end) {
    const std::size_t avail = std::min(cap.blockPowerL.size(), cap.blockPowerR.size());
    const std::size_t stop = std::min(end, avail);
    const double n = static_cast<double>(end > first ? end - first : 0u) *
                     static_cast<double>(kRenderBlock);
    double sum = 0.0;
    for (std::size_t i = first; i < stop; ++i) {
        sum += cap.blockPowerL[i] + cap.blockPowerR[i];
    }
    const double p = (n > 0.0) ? sum / (2.0 * n) : 0.0;
    return 10.0 * std::log10(std::max(p, kPowerFloor));
}

[[nodiscard]] inline double windowDb(const SweepCapture& cap, const SweepWindow& w) {
    return spanPowerDb(cap, w.firstBlock, w.endBlock);
}

/// RMS in dB of the single span [t0, t1], snapped inward like a window.
[[nodiscard]] inline double spanDb(const SweepCapture& cap, double t0, double t1, double sr) {
    return windowDb(cap, snapWindowInward(t0, t1, sr));
}

/// Plan 6.8 (printed only): the start time in seconds of the first one-second
/// window of the captured span whose stereo dB value (the C-7.2 one-second
/// values: blockRmsDb per channel at sr, combined as (pL + pR) / 2) is at or
/// above targetDb; std::nullopt when no window of the span reaches it. The span
/// starts at t = 0 (the [0, W_end] capture).
[[nodiscard]] inline std::optional<double> firstSecondAtOrAbove(std::span<const float> L,
                                                                std::span<const float> R,
                                                                double sr, double targetDb) {
    namespace VM = Krate::DSP::TestUtils::Vorago;
    const auto blockLen = static_cast<std::size_t>(sr);
    const std::size_t n = std::min(L.size(), R.size());
    const std::vector<double> dbL = VM::blockRmsDb(L.first(n), blockLen);
    const std::vector<double> dbR = VM::blockRmsDb(R.first(n), blockLen);
    const std::size_t blocks = std::min(dbL.size(), dbR.size());
    for (std::size_t b = 0; b < blocks; ++b) {
        const double p = (std::pow(10.0, dbL[b] / 10.0) + std::pow(10.0, dbR[b] / 10.0)) / 2.0;
        if (10.0 * std::log10(std::max(p, 1e-30)) >= targetDb) {
            return static_cast<double>(b);
        }
    }
    return std::nullopt;
}

}  // namespace detail

/// Plan 6.2: "every 10 s window over [a, b]" = k = floor((b - a) / 10) full
/// windows from a, plus one right-aligned at b when there is a remainder; every
/// sample is covered and no window is shorter than 10 s. (A span shorter than
/// 10 s, which no C-6 timeline produces, yields the single window [a, b].)
[[nodiscard]] inline std::vector<SweepWindow> tenSecondWindows(double a, double b, double sr) {
    std::vector<SweepWindow> out;
    const double span = b - a;
    if (!(span > 0.0)) {
        return out;
    }
    const auto k = static_cast<std::size_t>(
        std::floor((span + detail::kWindowEpsilonSeconds) / kArmWindowSeconds));
    out.reserve(k + 1u);
    for (std::size_t i = 0; i < k; ++i) {
        const double di = static_cast<double>(i);
        out.push_back(detail::snapWindowInward(a + (kArmWindowSeconds * di),
                                               a + (kArmWindowSeconds * (di + 1.0)), sr));
    }
    const double covered = kArmWindowSeconds * static_cast<double>(k);
    if (span - covered > detail::kWindowEpsilonSeconds) {
        const double start = (k == 0u) ? a : b - kArmWindowSeconds;
        out.push_back(detail::snapWindowInward(start, b, sr));
    }
    return out;
}

namespace detail {

/// The Freeze-On tail figures over the 10 s windows of [tail0, tail1] (plan 6.3
/// arm 4 On): the loudest window, the last window, and the loudest window
/// BEFORE the last (the non-growing reference; == last when there are fewer
/// than two windows). One definition, shared by evaluateArms and the T036
/// freeze-gesture score.
struct FreezeTailFigures {
    double loudestDb = 0.0;
    double lastDb = 0.0;
    double priorLoudestDb = 0.0;
    std::size_t windows = 0;
};

[[nodiscard]] inline FreezeTailFigures freezeTailFigures(const SweepCapture& cap,
                                                         const SweepTimeline& tl, double sr) {
    FreezeTailFigures f;
    const std::vector<SweepWindow> tail = tenSecondWindows(tl.tail0, tl.tail1, sr);
    f.windows = tail.size();
    for (std::size_t i = 0; i < tail.size(); ++i) {
        const double db = windowDb(cap, tail[i]);
        f.loudestDb = (i == 0u) ? db : std::max(f.loudestDb, db);
        if (i + 1u < tail.size()) {
            f.priorLoudestDb = (i == 0u) ? db : std::max(f.priorLoudestDb, db);
        }
        f.lastDb = db;
    }
    if (tail.size() < 2u) {
        f.priorLoudestDb = f.lastDb;
    }
    return f;
}

/// Arm 4 Freeze On (plan 6.3): last <= prior loudest + 1.0, last >= loudest - 6.0,
/// loudest >= susDb - 20. `susDb` is RMS(Sus) of the take the criteria are
/// scored against (the ungestured stored-seed take for the gesture render).
[[nodiscard]] inline bool freezeOnTailPasses(const FreezeTailFigures& f, double susDb) noexcept {
    return f.windows > 0u && f.lastDb <= f.priorLoudestDb + kFreezeGrowthDb &&
           f.lastDb >= f.loudestDb - kFreezeHoldDb && f.loudestDb >= susDb - kFreezeFloorDb;
}

}  // namespace detail

// ---- Level twin (plan 6.13 control (a), T036) ------------------------------------------
/// 10^(-6/20): the post-render -6 dB level scaling of control (a).
inline constexpr double kLevelTwinGain = 0.501187233627272285;

/// Plan 6.13 (a): d(meanOf(describe(each buffer pair)), meanOf(describe(the same
/// buffers x 10^(-6/20)))) - descriptor math on already-rendered buffers, no
/// re-render. Empty buffers are ignored; 0 when none remain.
[[nodiscard]] inline double levelTwinD(std::span<const std::vector<float>> L,
                                       std::span<const std::vector<float>> R, double sr) {
    const std::size_t n = std::min(L.size(), R.size());
    std::vector<PresetDescriptor> dry;
    std::vector<PresetDescriptor> scaled;
    dry.reserve(n);
    scaled.reserve(n);
    const auto gain = static_cast<float>(kLevelTwinGain);
    std::vector<float> sL;
    std::vector<float> sR;
    for (std::size_t i = 0; i < n; ++i) {
        if (L[i].empty() || R[i].empty()) {
            continue;
        }
        dry.push_back(describe(L[i], R[i], sr));
        sL.assign(L[i].begin(), L[i].end());
        sR.assign(R[i].begin(), R[i].end());
        for (float& x : sL) {
            x *= gain;
        }
        for (float& x : sR) {
            x *= gain;
        }
        scaled.push_back(describe(sL, sR, sr));
    }
    if (dry.empty()) {
        return 0.0;
    }
    return descriptorDistance(meanOf(dry), meanOf(scaled));
}

// ---- Arms (plan 6.3) -----------------------------------------------------------------
/// One take's arm figures and verdicts. dB figures are stereo-power dB (plan 6.2).
///   worstHiDb     loudest 10 s window over [0, Total]           (arm 1)
///   worstLoDb     quietest 10 s window over [A, H]               (arm 2)
///   susDb         RMS(Sus)                                       (arms 3, 4)
///   lateVsSusDb   RMS([H - 60, H]) - RMS(Sus)                    (arm 3)
///   tailDb        RMS(Tail) over the whole Tail span             (arm 4 Off)
///   tailLastDb    the last 10 s window of Tail                   (arm 4 On)
///   tailLoudestDb the loudest 10 s window of Tail                (arm 4 On)
struct ArmResult {
    bool finite = false;
    float peak = 0.0f;
    double worstHiDb = 0.0;
    double worstLoDb = 0.0;
    double susDb = 0.0;
    double lateVsSusDb = 0.0;
    double tailDb = 0.0;
    double tailLastDb = 0.0;
    double tailLoudestDb = 0.0;
    bool pass1 = false;
    bool pass2 = false;
    bool pass3 = false;
    bool pass4 = false;
};

/// Plan 6.3 arms 1-4 over one take's capture on timeline `tl` at `sr`:
///  1. finite (bit pattern), peak <= 0.9661f, every 10 s window of [0, Total] <= -6 dB;
///  2. every 10 s window of [A, H] >= -60 dB;
///  3. RMS([H - 60, H]) - RMS(Sus) in [-18, +12] dB;
///  4. Freeze Off (tl.freezeOnTail false): RMS(Tail) <= RMS(Sus) - 40;
///     Freeze On, over the 10 s windows of Tail: last <= (loudest window BEFORE
///     the last) + 1.0 (non-growing), last >= loudest - 6.0 (held), and
///     loudest >= RMS(Sus) - 20 (the absolute floor).
/// Non-growing reads the loudest window before the last: against the loudest
/// of all six (which includes the last) `last <= loudest + 1.0` would hold for
/// every signal, so the clause could never fail.
[[nodiscard]] inline ArmResult evaluateArms(const SweepCapture& cap, const SweepTimeline& tl,
                                            double sr) {
    ArmResult r;
    r.finite = cap.finite;
    r.peak = cap.peak;

    // Arm 1: bounded over [0, Total].
    const std::vector<SweepWindow> all = tenSecondWindows(0.0, tl.total, sr);
    bool anyHi = false;
    for (const SweepWindow& w : all) {
        const double db = detail::windowDb(cap, w);
        r.worstHiDb = anyHi ? std::max(r.worstHiDb, db) : db;
        anyHi = true;
    }
    r.pass1 = r.finite && r.peak <= kSweepPeakCeiling && anyHi && r.worstHiDb <= kRunawayDb;

    // Arm 2: non-silence over [A, H].
    const std::vector<SweepWindow> hold = tenSecondWindows(tl.A, tl.H, sr);
    bool anyLo = false;
    for (const SweepWindow& w : hold) {
        const double db = detail::windowDb(cap, w);
        r.worstLoDb = anyLo ? std::min(r.worstLoDb, db) : db;
        anyLo = true;
    }
    r.pass2 = anyLo && r.worstLoDb >= kSilenceDb;

    // Arm 3: late vs early.
    r.susDb = detail::spanDb(cap, tl.sus0, tl.sus1, sr);
    const double lateDb = detail::spanDb(cap, tl.H - kLateSeconds, tl.H, sr);
    r.lateVsSusDb = lateDb - r.susDb;
    r.pass3 = r.lateVsSusDb >= kLateLoDb && r.lateVsSusDb <= kLateHiDb;

    // Arm 4: tail.
    r.tailDb = detail::spanDb(cap, tl.tail0, tl.tail1, sr);
    const detail::FreezeTailFigures tail = detail::freezeTailFigures(cap, tl, sr);
    r.tailLoudestDb = tail.loudestDb;
    r.tailLastDb = tail.lastDb;
    if (tl.freezeOnTail) {
        r.pass4 = detail::freezeOnTailPasses(tail, r.susDb);
    } else {
        r.pass4 = r.tailDb <= r.susDb - kTailDropDb;
    }
    return r;
}

// ---- The K-take main render (plan 6.1, 6.5, FR-033) ------------------------------------
/// One ungestured take of `comp` on `tl` at 48 kHz: NoteOn 36 at 0 (seed index
/// `seedIndex` at block 0), NoteOff at H, rendered to Total; captures M1..M3 and,
/// when `attackWindowEnd` is set, [0, W_end] (plan 6.8). Fills the arm figures and
/// verdicts, the three minute descriptors and, with an attack window, D_att with
/// floorDb = RMS(Sus) - 60 dB of this take.
[[nodiscard]] inline TakeRecord renderTake(const std::vector<std::uint8_t>& comp,
                                           const SweepTimeline& tl, int seedIndex,
                                           std::optional<double> attackWindowEnd) {
    RenderSpec spec;
    spec.comp = std::span<const std::uint8_t>(comp);
    spec.seedIndex = seedIndex;
    spec.sr = kSweepSampleRate;
    spec.noteOffAt = tl.H;
    spec.end = tl.total;
    spec.capture.reserve(4u);
    for (int k = 0; k < 3; ++k) {
        spec.capture.emplace_back(tl.m[k][0], tl.m[k][1]);
    }
    if (attackWindowEnd.has_value()) {
        spec.capture.emplace_back(0.0, *attackWindowEnd);
    }

    const SweepCapture cap = renderPreset(spec);
    const ArmResult arms = evaluateArms(cap, tl, spec.sr);

    TakeRecord t;
    t.seedIndex = seedIndex;
    t.finite = arms.finite;
    t.peak = arms.peak;
    t.worstHiDb = arms.worstHiDb;
    t.worstLoDb = arms.worstLoDb;
    t.lateVsSusDb = arms.lateVsSusDb;
    t.tailDb = arms.tailDb;
    t.armPass = {arms.pass1, arms.pass2, arms.pass3, arms.pass4};
    for (std::size_t k = 0; k < t.minutes.size(); ++k) {
        if (k < cap.capL.size() && !cap.capL[k].empty()) {
            t.minutes[k] = describe(cap.capL[k], cap.capR[k], spec.sr);
        }
    }
    const std::size_t minuteCaps = std::min<std::size_t>(t.minutes.size(), cap.capL.size());
    t.levelTwinD = levelTwinD(std::span<const std::vector<float>>(cap.capL.data(), minuteCaps),
                              std::span<const std::vector<float>>(cap.capR.data(), minuteCaps),
                              spec.sr);
    t.susDb = arms.susDb;
    if (attackWindowEnd.has_value() && cap.capL.size() > 3u && !cap.capL[3].empty()) {
        t.attack = describeWithEnergyFloor(cap.capL[3], cap.capR[3], spec.sr,
                                           arms.susDb - kAttackFloorBelowSusDb);
        t.attackReachSeconds = detail::firstSecondAtOrAbove(
            cap.capL[3], cap.capR[3], spec.sr, arms.susDb - kAttackReachBelowSusDb);
    }
    return t;
}

/// The K takes of A_K (plan 6.5): take j at seed index takeSeedIndex(storedSeed,
/// 0, j, K), rendered through runJobs on `threads`. Take 0 is the stored seed;
/// `attackWindowEnd` (plan 6.8, D8.2 / D9.1 primaries only) is added to that
/// take's capture alone. Jobs never touch Catch2.
[[nodiscard]] inline std::vector<TakeRecord> computeTakes(
    const std::vector<std::uint8_t>& comp, const SweepTimeline& tl, int storedSeed, int K,
    unsigned threads, std::optional<double> attackWindowEnd = std::nullopt) {
    std::vector<TakeRecord> takes(static_cast<std::size_t>(std::max(K, 0)));
    std::vector<std::function<void()>> jobs;
    jobs.reserve(takes.size());
    for (int j = 0; j < K; ++j) {
        jobs.emplace_back([&comp, &tl, &takes, &attackWindowEnd, storedSeed, j, K] {
            const int seed = takeSeedIndex(storedSeed, 0, j, K);
            takes[static_cast<std::size_t>(j)] =
                renderTake(comp, tl, seed, (j == 0) ? attackWindowEnd : std::nullopt);
        });
    }
    runJobs(jobs, threads);
    return takes;
}

}  // namespace VoragoTest

// ==============================================================================
// D (T035, plan 6.7-6.11; C-7.4, FR-012, FR-037): ablation twins, the D8.2 /
// D9.1 attack-window reversion, the route-isolated E arms, the exact skip
// rules and the verification-vector fill. Still Catch2-free: every render runs
// through runJobs; nothing here asserts.
// ==============================================================================

namespace VoragoTest {

/// (ID, normalized) points delivered at offset 0 in block 0 (RenderSpec::block0).
using ParamOverrides = std::vector<std::pair<Steinberg::Vst::ParamID, double>>;

// ---- Skip reasons (plan 6.11, 6.12) ----------------------------------------------
inline constexpr std::string_view kSkipOverrideEqualsStored = "override equals stored";
inline constexpr std::string_view kSkipMDisplacement = "M displacement";
inline constexpr std::string_view kSkipEcosystemDepth0 = "ecosystem depth 0";
inline constexpr std::string_view kSkipStateFalse = "state false";
inline constexpr std::string_view kSkipSidePredicate = "side predicate";
inline constexpr std::string_view kSkipStateOnly = "state-only kind";
// Not plan skips: a verdict that cannot be scored is recorded, never dropped.
inline constexpr std::string_view kSkipDecodeFailed = "decode failed";
inline constexpr std::string_view kSkipRenderFailed = "render failed";
inline constexpr std::string_view kSkipNoAttackCapture = "no attack-window capture";

/// Two normalized values are "equal" for the override-equals-stored skip when
/// they differ by at most this: the stored side is a float plain value mapped
/// back to normalized (the ...ToController inverse), so an override equal to
/// the stored value comes back within float storage precision, never exactly.
inline constexpr double kOverrideEqualTolerance = 1e-6;

/// Plan 6.9 route destinations: E1<->S6, E2<->S2, E3<->S1, E4<->S4, E5<->S9.
inline constexpr std::array<std::pair<::Vorago::PresetDefs::Capability,
                                      ::Vorago::PresetDefs::Capability>,
                            5>
    kRouteDestinations{{
        {::Vorago::PresetDefs::Capability::E1PartialBloom, ::Vorago::PresetDefs::Capability::S6Bloom},
        {::Vorago::PresetDefs::Capability::E2ResonatorPeaks,
         ::Vorago::PresetDefs::Capability::S2Resonance},
        {::Vorago::PresetDefs::Capability::E3NoiseWake, ::Vorago::PresetDefs::Capability::S1Noise},
        {::Vorago::PresetDefs::Capability::E4FeedbackLoopWake,
         ::Vorago::PresetDefs::Capability::S4Ecology},
        {::Vorago::PresetDefs::Capability::E5GhostBursts, ::Vorago::PresetDefs::Capability::S9Ghost},
    }};

// ---- Stored values ------------------------------------------------------------------

/// The default surface's component state: a fresh PresetHost's getState().
/// False (and `out` empty) if the host fails.
[[nodiscard]] inline bool defaultSurfaceState(std::vector<std::uint8_t>& out) {
    out.clear();
    PresetHost host;
    if (host.prepare(kSweepSampleRate, detail::kRenderBlock) != Steinberg::kResultOk) {
        return false;
    }
    return host.saveState(out);
}

/// Every persisted parameter's stored NORMALIZED value, through the shipped
/// ...ToController inverse mappings in the controller's order
/// (controller.cpp:188-241, a v3 stream): global, macro, the v2 tail, then the
/// v3 ecosystem extension. Empty for an empty or unreadable stream.
[[nodiscard]] inline std::map<Steinberg::Vst::ParamID, double> storedNormalizedValues(
    std::span<const std::uint8_t> comp) {
    std::map<Steinberg::Vst::ParamID, double> out;
    if (comp.empty()) {
        return out;
    }
    std::vector<std::uint8_t> buf(comp.begin(), comp.end());
    Steinberg::MemoryStream stream(buf.data(), static_cast<Steinberg::TSize>(buf.size()));
    Steinberg::IBStreamer s(&stream, kLittleEndian);
    Steinberg::int32 version = 0;
    if (!s.readInt32(version)) {
        return out;
    }
    const auto set = [&out](Steinberg::Vst::ParamID id, double v) { out[id] = v; };
    ::Vorago::loadGlobalParamsToController(s, set);
    ::Vorago::loadMacroParamsToController(s, set);
    ::Vorago::loadGlobalParamsV2ExtToController(s, set);
    ::Vorago::loadCloudParamsToController(s, set);
    ::Vorago::loadNoiseParamsToController(s, set);
    ::Vorago::loadResonanceParamsToController(s, set);
    ::Vorago::loadEcologyParamsToController(s, set);
    ::Vorago::loadSubParamsToController(s, set);
    ::Vorago::loadSmearParamsToController(s, set);
    ::Vorago::loadEventsParamsToController(s, set);
    ::Vorago::loadEcosystemParamsToController(s, set);
    ::Vorago::loadBodyParamsToController(s, set);
    ::Vorago::loadSpaceParamsToController(s, set);
    ::Vorago::loadEnvelopeParamsToController(s, set);
    ::Vorago::loadBloomParamsToController(s, set);
    ::Vorago::loadGhostParamsToController(s, set);
    ::Vorago::loadLifeParamsToController(s, set);
    ::Vorago::loadEcosystemParamsV3ExtToController(s, set);
    return out;
}

// ---- Twin overrides (plan 6.7, 6.8, 6.9) ------------------------------------------------

/// The C-7.4 twin of cell `c` for the decoded preset `st`, in normalized units:
///  - S, M, E6.hi, E7.hi, D13, D14: the static `cellSpecs()[c].ablation` list;
///  - D1.x (the per-material reversion): each of 1004 / 1005 holding material x
///    -> its registered default index (5 / 6 of 11);
///  - D8.2: 1200 -> 0.0 (Standard);
///  - D9.1: 1201-1204 -> their registered defaults, plus 1200 -> 0.0 when Growth
///    is stored;
///  - every other cell (E1-E5 use routeOverrides): empty.
[[nodiscard]] inline ParamOverrides twinOverrides(::Vorago::PresetDefs::Capability c,
                                                  const DecodedPresetState& st) {
    using C = ::Vorago::PresetDefs::Capability;
    constexpr auto kR = std::memory_order_relaxed;
    ParamOverrides out;
    const auto i = static_cast<std::size_t>(c);
    if (i >= ::Vorago::PresetDefs::kNumCapabilities) {
        return out;
    }
    const ::Vorago::PresetDefs::CellSpec& spec = ::Vorago::PresetDefs::cellSpecs()[i];
    for (std::size_t k = 0; k < spec.ablationCount; ++k) {
        out.emplace_back(spec.ablation[k].id, spec.ablation[k].normalized);
    }
    if (!out.empty()) {
        return out;
    }

    if (detail::capInRange(c, C::D1Glass, C::D1GlassSphere)) {
        const int x = detail::capOffset(c, C::D1Glass);  // == BodyMaterial index
        if (st.body.materialA.load(kR) == x) {
            out.emplace_back(::Vorago::kBodyMaterialAId,
                             ::Vorago::indexToNormalized(::Vorago::kBodyMaterialADefault,
                                                         ::Vorago::kNumBodyMaterialChoices));
        }
        if (st.body.materialB.load(kR) == x) {
            out.emplace_back(::Vorago::kBodyMaterialBId,
                             ::Vorago::indexToNormalized(::Vorago::kBodyMaterialBDefault,
                                                         ::Vorago::kNumBodyMaterialChoices));
        }
        return out;
    }

    constexpr double kStandardNormalized = 0.0;  // indexToNormalized(Standard = 0, 2)
    if (c == C::D8Growth) {
        out.emplace_back(::Vorago::kEnvelopeModeId, kStandardNormalized);
        return out;
    }
    if (c == C::D9FastAttack) {
        constexpr std::array<Steinberg::Vst::ParamID, 4> kStageIds{
            ::Vorago::kEnvelopeStage0TimeId, ::Vorago::kEnvelopeStage1TimeId,
            ::Vorago::kEnvelopeStage2TimeId, ::Vorago::kEnvelopeStage3TimeId};
        for (std::size_t k = 0; k < kStageIds.size(); ++k) {
            out.emplace_back(kStageIds[k],
                             ::Vorago::detail::envelopeTimeToNormalized(static_cast<double>(
                                 Krate::DSP::VoragoVoice::kDefaultStageTimesMs[k])));
        }
        if (st.envelope.mode.load(kR) ==
            static_cast<int>(Krate::DSP::VoragoVoice::EnvelopeMode::Growth)) {
            out.emplace_back(::Vorago::kEnvelopeModeId, kStandardNormalized);
        }
    }
    return out;
}

/// Plan 6.9: R_k = P + the S-overrides of the OTHER four route destinations of
/// `kept` (E1-E5); `kept == Capability::Count` (or any non-route cell) gives
/// R_0 = all five destinations. `depthZero` adds 900 -> 0.0 (the R_k0 / R_00
/// variants).
[[nodiscard]] inline ParamOverrides routeOverrides(::Vorago::PresetDefs::Capability kept,
                                                   bool depthZero = false) {
    ParamOverrides out;
    for (const auto& [e, s] : kRouteDestinations) {
        if (e == kept) {
            continue;
        }
        const ::Vorago::PresetDefs::CellSpec& spec =
            ::Vorago::PresetDefs::cellSpecs()[static_cast<std::size_t>(s)];
        for (std::size_t k = 0; k < spec.ablationCount; ++k) {
            out.emplace_back(spec.ablation[k].id, spec.ablation[k].normalized);
        }
    }
    if (depthZero) {
        out.emplace_back(::Vorago::kEcosystemDepthId, 0.0);
    }
    return out;
}

// ---- Skip rules (plan 6.11) ---------------------------------------------------------------

/// Plan 6.11 (T001 item 5: skips acknowledged): why the render of the
/// render-scored cell `c` is skipped, or empty when it must be rendered. A skip
/// happens only where the verdict is known exactly:
///  - M whose stored displacement is below the threshold -> "M displacement";
///  - E1-E5 with 900 stored at 0.0 (R_k == R_k0)          -> "ecosystem depth 0";
///  - E6.hi / E7.hi with a false side predicate           -> "side predicate";
///  - a D cell with a false state predicate               -> "state false";
///  - every override equal to the stored value (twin == P) -> "override equals stored".
/// `overrides` is the twin's list (routeOverrides for E1-E5, which never take
/// the override-equality skip: R_k and R_k0 differ in 900 alone).
[[nodiscard]] inline std::string skipReason(
    ::Vorago::PresetDefs::Capability c, const DecodedPresetState& st,
    const std::map<Steinberg::Vst::ParamID, double>& stored, const ParamOverrides& overrides) {
    using C = ::Vorago::PresetDefs::Capability;
    if (detail::capInRange(c, C::M1Darkness, C::M12Mass) && !macroDisplaced(c, st)) {
        return std::string(kSkipMDisplacement);
    }
    if (detail::capInRange(c, C::E1PartialBloom, C::E5GhostBursts)) {
        return (st.ecosystem.depth.load(std::memory_order_relaxed) == 0.0f)
                   ? std::string(kSkipEcosystemDepth0)
                   : std::string();
    }
    if ((c == C::E6SyncRateHi || c == C::E7SelfAffinityHi) && !extSidePredicate(c, st)) {
        return std::string(kSkipSidePredicate);
    }
    if (detail::capInRange(c, C::D1Glass, C::D14Tidal) && !statePredicate(c, st)) {
        return std::string(kSkipStateFalse);
    }
    if (!overrides.empty()) {
        const bool allEqual =
            std::ranges::all_of(overrides, [&stored](const auto& ov) {
                const auto it = stored.find(ov.first);
                return it != stored.end() &&
                       std::fabs(it->second - ov.second) <= kOverrideEqualTolerance;
            });
        if (allEqual) {
            return std::string(kSkipOverrideEqualsStored);
        }
    }
    return {};
}

/// Records a skipped (unrendered) entry: rendered = false, d = 0, the reason.
/// stateOk / conjunctOk / twoS are left as the caller set them.
inline void markSkipped(CellOutcome& o, std::string reason) {
    o.rendered = false;
    o.d = 0.0;
    o.skip = std::move(reason);
}

/// True when cell `c` is scored from a render in the vector of a preset whose
/// primary is `primary` (Capability::Count: the default-surface pseudo-preset):
/// the Ablation, RouteIsolated, ExtReversion and StateWithReversion kinds always;
/// D1.x only as that preset's primary (the per-material reversion) and D8.2 /
/// D9.1 only as its primary (the attack-window reversion). Every other entry
/// is scored by a state-only rule (plan 6.12).
[[nodiscard]] inline bool isRenderScored(::Vorago::PresetDefs::Capability c,
                                         ::Vorago::PresetDefs::Capability primary) {
    using C = ::Vorago::PresetDefs::Capability;
    using V = ::Vorago::PresetDefs::Verification;
    const auto i = static_cast<std::size_t>(c);
    if (i >= ::Vorago::PresetDefs::kNumCapabilities) {
        return false;
    }
    switch (::Vorago::PresetDefs::cellSpecs()[i].verification) {
        case V::Ablation:
        case V::RouteIsolated:
        case V::ExtReversion:
        case V::StateWithReversion:
            return true;
        case V::StateWithS:
            return c == primary && detail::capInRange(c, C::D1Glass, C::D1GlassSphere);
        case V::AttackWindow:
            return c == primary && (c == C::D8Growth || c == C::D9FastAttack);
        case V::FreezeGesture:
        case V::StateOnly:
            return false;
    }
    return false;
}

// ---- Attack window (plan 6.8) ------------------------------------------------------------

namespace detail {

/// The component state to decode: `comp`, or the default surface's getState()
/// when `comp` is empty (held in `scratch`).
[[nodiscard]] inline std::span<const std::uint8_t> stateBytesOrDefault(
    std::span<const std::uint8_t> comp, std::vector<std::uint8_t>& scratch) {
    if (!comp.empty()) {
        return comp;
    }
    if (!defaultSurfaceState(scratch)) {
        scratch.clear();
    }
    return {scratch.data(), scratch.size()};
}

}  // namespace detail

/// A_rev of plan 6.8: the attack span of P_rev's decode, i.e. `comp` decoded
/// with cell `c`'s envelope reversion (twinOverrides) applied through the
/// shipped envelope handler. std::nullopt when the state does not decode.
[[nodiscard]] inline std::optional<double> revertedAttackSpanSeconds(
    ::Vorago::PresetDefs::Capability c, std::span<const std::uint8_t> comp) {
    std::vector<std::uint8_t> scratch;
    DecodedPresetState rev;
    if (!decodePresetState(detail::stateBytesOrDefault(comp, scratch), rev)) {
        return std::nullopt;
    }
    for (const auto& [id, value] : twinOverrides(c, rev)) {
        ::Vorago::handleEnvelopeParamChange(rev.envelope, id, value);
    }
    return audibleAttackSeconds(rev);  // ruling S-9: the audible attack, not the C-6 sum
}

/// W_end = max(A_P, A_rev) + 5 s (plan 6.8) when `def`'s primary is D8.2 or
/// D9.1, else std::nullopt, with A the AUDIBLE attack (audibleAttackSeconds,
/// ruling S-9). The main take of such a preset captures [0, W_end] on its
/// stored-seed take (computeTakes' attackWindowEnd).
[[nodiscard]] inline std::optional<double> attackWindowEndSeconds(
    const ::Vorago::PresetDefs::VoragoPresetDef* def, std::span<const std::uint8_t> comp) {
    using C = ::Vorago::PresetDefs::Capability;
    if (def == nullptr || (def->primary != C::D8Growth && def->primary != C::D9FastAttack)) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> scratch;
    DecodedPresetState st;
    if (!decodePresetState(detail::stateBytesOrDefault(comp, scratch), st)) {
        return std::nullopt;
    }
    const std::optional<double> aRev = revertedAttackSpanSeconds(def->primary, comp);
    if (!aRev.has_value()) {
        return std::nullopt;
    }
    return std::max(audibleAttackSeconds(st), *aRev) + 5.0;  // ruling S-9
}

// ---- Twin renders and descriptors ------------------------------------------------------------

/// Plan 6.5 "Twin self-distance": d(describe(first half of the Sus capture),
/// describe(second half)) - for the 60 s Sus, the first and last 30 s. Reads
/// the capture's first window. 0 for an empty capture.
[[nodiscard]] inline double susHalfSelfDistance(const SweepCapture& cap, double sr) {
    if (cap.capL.empty() || cap.capR.empty()) {
        return 0.0;
    }
    const std::size_t n = std::min(cap.capL[0].size(), cap.capR[0].size());
    const std::size_t half = n / 2u;
    if (half == 0u) {
        return 0.0;
    }
    const std::span<const float> L(cap.capL[0].data(), n);
    const std::span<const float> R(cap.capR[0].data(), n);
    return descriptorDistance(describe(L.first(half), R.first(half), sr),
                              describe(L.subspan(half, half), R.subspan(half, half), sr));
}

namespace detail {

/// A single-take twin at the stored seed (plan 6.7 / 6.9): setState(P) plus the
/// overrides as offset-0 points in block 0, rendered on P's timeline to
/// Sus.end, capturing Sus. The NoteOff at H lies past the end (no release).
[[nodiscard]] inline RenderSpec twinSusSpec(std::span<const std::uint8_t> comp,
                                            const SweepTimeline& tl, int storedSeed,
                                            ParamOverrides overrides) {
    RenderSpec spec;
    spec.comp = comp;
    spec.block0 = std::move(overrides);
    spec.seedIndex = storedSeed;
    spec.sr = kSweepSampleRate;
    spec.noteOffAt = tl.H;
    // Sweep ruling 2026-09-30: a twin is scored on the mean of its three
    // sustain-minute descriptors (M1..M3), like D(P)'s minutes, not on M1 alone -
    // slow capabilities (macros, routes, breathing) read 2-5x larger over three
    // minutes (E6.hi 0.15 -> 0.85 on the same twin). The NoteOff at H is the end
    // of M3, so the third minute is still sustain.
    spec.end = tl.m[2][1];
    spec.capture = {{tl.m[0][0], tl.m[0][1]}, {tl.m[1][0], tl.m[1][1]}, {tl.m[2][0], tl.m[2][1]}};
    return spec;
}

/// describe(capture window `w`) when the render finished finite with a
/// non-empty capture there; std::nullopt otherwise (a failed render is recorded
/// as such, never scored).
[[nodiscard]] inline std::optional<PresetDescriptor> captureDescriptor(const SweepCapture& cap,
                                                                       std::size_t w, double sr) {
    if (!cap.finite || w >= cap.capL.size() || w >= cap.capR.size() || cap.capL[w].empty()) {
        return std::nullopt;
    }
    return describe(cap.capL[w], cap.capR[w], sr);
}

/// Sweep ruling 2026-09-30: the twin's Sus descriptor = meanOf(describe(M1),
/// describe(M2), describe(M3)) over the three windows starting at `first` -
/// the same construction as the stored take's minutes, so d(P_Sus, twin) compares
/// like with like. std::nullopt when any window is missing.
[[nodiscard]] inline std::optional<PresetDescriptor> twinSusDescriptor(const SweepCapture& cap,
                                                                       std::size_t first,
                                                                       double sr) {
    std::array<PresetDescriptor, 3> m{};
    for (std::size_t k = 0; k < 3u; ++k) {
        const std::optional<PresetDescriptor> d = captureDescriptor(cap, first + k, sr);
        if (!d.has_value()) {
            return std::nullopt;
        }
        m[k] = *d;
    }
    return meanOf(std::span<const PresetDescriptor>(m));
}

inline void printReach(const char* what, std::optional<double> seconds) {
    if (seconds.has_value()) {
        std::printf("    %s: first reaches RMS(Sus) - 6 dB at %.0f s\n", what, *seconds);
    } else {
        std::printf("    %s: never reaches RMS(Sus) - 6 dB inside [0, W_end]\n", what);
    }
}

}  // namespace detail

// ---- The verification vector (plan 6.11, 6.12; FR-012, FR-037) ----------------------------

/// Fills every one of the 79 CellOutcomes for `def` (nullptr = the default-surface
/// pseudo-preset, whose `comp` may be empty) on P's timeline `tl`:
///  - state terms from the decoded state (plan 6.10): stateOk per kind, and the
///    StateWithS / D13 conjuncts from this same vector's S entry at the
///    secondary bar;
///  - render-scored entries (isRenderScored): single-take twins at the stored
///    seed to Sus.end, d = d(pSus, describe(twin Sus)), twoS = 2 selfDistance
///    (ablation, E-ext, D13 / D14, the D1.x primary reversion); the route arms
///    (12 renders: R_k, R_k0, R_0, R_00) with d = d(R_k, R_k0), twoS = 2 s(R_k),
///    attribBase = d(R_0, R_00); the D8.2 / D9.1 primary attack-window reversion
///    with d = d_att, attribBase = d_Sus - it needs `storedTake`, P's stored-seed
///    main take rendered with the [0, W_end] capture (attackWindowEndSeconds);
///  - a skipped render is recorded with its plan 6.11 reason and d = 0;
///  - state-only entries: rendered = false, d = 0, twoS = 0, "state-only kind".
/// D10.1's gesture conjunct is left false for T036 to fill. `selfDistance` is
/// s(P) (the K-take figure), `pSus` = describe(M1) of the stored-seed take.
/// Renders run through runJobs on `threads`; this function prints the plan 6.8
/// time-to-level lines and asserts nothing.
[[nodiscard]] inline VerificationVector computeVerificationVector(
    const ::Vorago::PresetDefs::VoragoPresetDef* def, const std::vector<std::uint8_t>& comp,
    const SweepTimeline& tl, double selfDistance, const PresetDescriptor& pSus,
    unsigned threads, const TakeRecord* storedTake = nullptr) {
    using C = ::Vorago::PresetDefs::Capability;
    using V = ::Vorago::PresetDefs::Verification;
    constexpr std::size_t kCells = ::Vorago::PresetDefs::kNumCapabilities;
    constexpr double kSr = kSweepSampleRate;

    VerificationVector vec;
    const auto at = [&vec](C c) -> CellOutcome& { return vec.cells[static_cast<std::size_t>(c)]; };

    std::vector<std::uint8_t> scratch;
    const std::span<const std::uint8_t> bytes = detail::stateBytesOrDefault(comp, scratch);
    DecodedPresetState st;
    if (!decodePresetState(bytes, st)) {
        for (CellOutcome& o : vec.cells) {
            markSkipped(o, std::string(kSkipDecodeFailed));
        }
        return vec;
    }
    const std::map<Steinberg::Vst::ParamID, double> stored = storedNormalizedValues(bytes);
    const int storedSeed = st.global.seedIndex.load(std::memory_order_relaxed);
    const C primary = (def != nullptr) ? def->primary : C::Count;
    const double twoS = 2.0 * selfDistance;
    // Renders use `comp` itself: an empty comp is the true default surface (no setState).
    const std::span<const std::uint8_t> renderComp(comp);

    std::vector<RenderSpec> specs;
    specs.reserve(kCells + 12u);
    std::array<int, kCells> twinIndex{};
    twinIndex.fill(-1);

    // ---- Pass 1: state terms, skip rules, and the render list --------------------------
    int attackIndex = -1;
    double attackWEnd = 0.0;
    for (std::size_t i = 0; i < kCells; ++i) {
        const auto c = static_cast<C>(i);
        CellOutcome& o = vec.cells[i];
        const V kind = ::Vorago::PresetDefs::cellSpecs()[i].verification;

        if (!isRenderScored(c, primary)) {
            // D10.1: "P is the S8-primary preset", a property of the definition.
            o.stateOk =
                (c == C::D10FreezeHolds) ? (primary == C::S8Cavern) : statePredicate(c, st);
            o.conjunctOk = (kind == V::AttackWindow || kind == V::StateOnly);  // StateWithS: pass 3
            o.twoS = 0.0;
            o.attribBase = -1.0;
            markSkipped(o, std::string(kSkipStateOnly));
            continue;
        }

        o.stateOk = statePredicate(c, st);  // S, E1-E5: true; M: displacement; E-ext: side
        o.conjunctOk = true;                // StateWithS / D13: pass 3
        o.twoS = twoS;
        o.attribBase = -1.0;
        if (kind == V::RouteIsolated) {
            continue;  // the route arms below
        }

        const ParamOverrides ov = twinOverrides(c, st);
        const std::string why = skipReason(c, st, stored, ov);
        if (!why.empty()) {
            markSkipped(o, why);
            continue;
        }

        if (kind == V::AttackWindow) {
            // Plan 6.8: P_rev to max(W_end, A_rev + 65), capturing [0, W_end] and
            // Sus_rev; its own timeline's H lies past the end (no release).
            const std::optional<double> wEnd = attackWindowEndSeconds(def, comp);
            const std::optional<double> aRev = revertedAttackSpanSeconds(c, comp);
            if (storedTake == nullptr || !storedTake->attack.has_value() || !wEnd.has_value() ||
                !aRev.has_value()) {
                markSkipped(o, std::string(kSkipNoAttackCapture));
                continue;
            }
            RenderSpec spec;
            spec.comp = renderComp;
            spec.block0 = ov;
            spec.seedIndex = storedSeed;
            spec.sr = kSr;
            spec.noteOffAt = *aRev + 185.0;
            spec.end = std::max(*wEnd, *aRev + 185.0);
            spec.capture = {{0.0, *wEnd},
                            {*aRev + 5.0, *aRev + 65.0},
                            {*aRev + 65.0, *aRev + 125.0},
                            {*aRev + 125.0, *aRev + 185.0}};  // ruling 2026-09-30: M1..M3
            attackIndex = static_cast<int>(specs.size());
            attackWEnd = *wEnd;
            specs.push_back(std::move(spec));
            continue;
        }

        twinIndex[i] = static_cast<int>(specs.size());
        specs.push_back(detail::twinSusSpec(renderComp, tl, storedSeed, ov));
    }

    // Route arms (plan 6.9): 12 renders unless 900 is stored at 0.0.
    std::array<int, 5> rk{};
    std::array<int, 5> rk0{};
    int rNull = -1;
    int rNull0 = -1;
    const std::string routeWhy =
        skipReason(C::E1PartialBloom, st, stored, routeOverrides(C::E1PartialBloom));
    if (routeWhy.empty()) {
        for (std::size_t k = 0; k < kRouteDestinations.size(); ++k) {
            const C e = kRouteDestinations[k].first;
            rk[k] = static_cast<int>(specs.size());
            specs.push_back(detail::twinSusSpec(renderComp, tl, storedSeed, routeOverrides(e)));
            rk0[k] = static_cast<int>(specs.size());
            specs.push_back(
                detail::twinSusSpec(renderComp, tl, storedSeed, routeOverrides(e, true)));
        }
        rNull = static_cast<int>(specs.size());
        specs.push_back(detail::twinSusSpec(renderComp, tl, storedSeed, routeOverrides(C::Count)));
        rNull0 = static_cast<int>(specs.size());
        specs.push_back(
            detail::twinSusSpec(renderComp, tl, storedSeed, routeOverrides(C::Count, true)));
    } else {
        for (const auto& route : kRouteDestinations) {
            markSkipped(at(route.first), routeWhy);
        }
    }

    // ---- Render ------------------------------------------------------------------------
    std::vector<SweepCapture> caps(specs.size());
    std::vector<std::function<void()>> jobs;
    jobs.reserve(specs.size());
    for (std::size_t j = 0; j < specs.size(); ++j) {
        jobs.emplace_back([&specs, &caps, j] { caps[j] = renderPreset(specs[j]); });
    }
    runJobs(jobs, threads);

    // ---- Pass 2: distances ---------------------------------------------------------------
    for (std::size_t i = 0; i < kCells; ++i) {
        if (twinIndex[i] < 0) {
            continue;
        }
        CellOutcome& o = vec.cells[i];
        const std::optional<PresetDescriptor> twin =
            detail::twinSusDescriptor(caps[static_cast<std::size_t>(twinIndex[i])], 0u, kSr);
        if (!twin.has_value()) {
            markSkipped(o, std::string(kSkipRenderFailed));
            continue;
        }
        o.rendered = true;
        o.d = descriptorDistance(pSus, *twin);
    }

    if (rNull >= 0) {
        const std::optional<PresetDescriptor> dNull =
            detail::twinSusDescriptor(caps[static_cast<std::size_t>(rNull)], 0u, kSr);
        const std::optional<PresetDescriptor> dNull0 =
            detail::twinSusDescriptor(caps[static_cast<std::size_t>(rNull0)], 0u, kSr);
        for (std::size_t k = 0; k < kRouteDestinations.size(); ++k) {
            CellOutcome& o = at(kRouteDestinations[k].first);
            const SweepCapture& capK = caps[static_cast<std::size_t>(rk[k])];
            const std::optional<PresetDescriptor> dK = detail::twinSusDescriptor(capK, 0u, kSr);
            const std::optional<PresetDescriptor> dK0 =
                detail::twinSusDescriptor(caps[static_cast<std::size_t>(rk0[k])], 0u, kSr);
            if (!dNull.has_value() || !dNull0.has_value() || !dK.has_value() ||
                !dK0.has_value()) {
                markSkipped(o, std::string(kSkipRenderFailed));
                continue;
            }
            o.rendered = true;
            o.d = descriptorDistance(*dK, *dK0);
            o.twoS = 2.0 * susHalfSelfDistance(capK, kSr);
            o.attribBase = descriptorDistance(*dNull, *dNull0);
        }
    }

    if (attackIndex >= 0) {
        // Plan 6.8: floorDb = RMS(Sus_P) - 60; d_att = d(D_att(P), D_att(P_rev));
        // d_Sus = d(describe(Sus_P), describe(Sus_rev)).
        CellOutcome& o = at(primary);
        const SweepCapture& rev = caps[static_cast<std::size_t>(attackIndex)];
        const std::optional<PresetDescriptor> susRev = detail::twinSusDescriptor(rev, 1u, kSr);
        if (!rev.finite || rev.capL.size() < 4u || rev.capL[0].empty() || !susRev.has_value()) {
            markSkipped(o, std::string(kSkipRenderFailed));
        } else {
            const double floorDb = storedTake->susDb - kAttackFloorBelowSusDb;
            const PresetDescriptor attRev =
                describeWithEnergyFloor(rev.capL[0], rev.capR[0], kSr, floorDb);
            o.rendered = true;
            o.d = descriptorDistance(*storedTake->attack, attRev);
            o.attribBase = descriptorDistance(pSus, *susRev);
            const RenderSpec& revSpec = specs[static_cast<std::size_t>(attackIndex)];
            const double susRevDb = detail::spanDb(rev, revSpec.capture[1].first,
                                                   revSpec.capture[1].second, kSr);
            const std::string label(
                ::Vorago::PresetDefs::cellSpecs()[static_cast<std::size_t>(primary)].label);
            std::printf("  attack window (%s primary, W_end %.1f s): d_att %.4f, d_Sus %.4f\n",
                        label.c_str(), attackWEnd, o.d, o.attribBase);
            detail::printReach("P", storedTake->attackReachSeconds);
            detail::printReach("P_rev", detail::firstSecondAtOrAbove(
                                            rev.capL[0], rev.capR[0], kSr,
                                            susRevDb - kAttackReachBelowSusDb));
        }
    }

    // ---- Pass 3: conjuncts from this same vector's S entries (secondary bar) ------------
    for (std::size_t i = 0; i < kCells; ++i) {
        const ::Vorago::PresetDefs::CellSpec& spec = ::Vorago::PresetDefs::cellSpecs()[i];
        if (spec.sConjunct == C::Count) {
            continue;  // only StateWithS and D13 name an S conjunct
        }
        vec.cells[i].conjunctOk =
            verifiedAt(at(spec.sConjunct), V::Ablation, ClaimRole::Secondary);
        // D3 primary rule (ruling 2026-09-30, the plan 6.12 "Required primaries"
        // stop): a noise-model cell carries its S1 conjunct's render terms, so at
        // Primary it reads stateOk && conjunctOk && rendered && d >= F - the
        // showcase IS the audible noise organism. Its secondary verdict is
        // unchanged (no d term). The copy is what the aggregate prints as the
        // cell's d.
        if (detail::capInRange(static_cast<C>(i), C::D3Direct, C::D3MetallicHiss)) {
            const CellOutcome& s1 = at(spec.sConjunct);
            CellOutcome& o = vec.cells[i];
            o.rendered = s1.rendered;
            o.d = s1.d;
            o.attribBase = -1.0;
            o.skip = s1.rendered ? std::string() : s1.skip;
        }
    }
    return vec;
}

}  // namespace VoragoTest

// ==============================================================================
// D (T036, plan 6.1 / 6.3 / 6.5 / 6.13; C-6, C-7.3, FR-033, FR-033a, FR-038):
// the freeze-gesture block rule, the gesture render G and its dry-residue twin
// G0, the 44.1 / 96 kHz sustain renders, reproducibility, the control helpers
// and computeSweepRecord. Still Catch2-free: every render runs through runJobs
// (or on the calling test thread); nothing here asserts.
// ==============================================================================

#include <render_fingerprint.h>

namespace VoragoTest {

// ---- Constants ------------------------------------------------------------------------
inline constexpr double kGestureAfterAttackSeconds = 65.0;  ///< freeze at A + 65 = Sus.end
inline constexpr double kRate441 = 44100.0;                 ///< FR-033a
inline constexpr double kRate96 = 96000.0;                  ///< FR-033a
inline constexpr double kGainTwinScale = 0.5;               ///< (a2) 0 -> 0.5 x stored normalized
inline constexpr double kSubTwinStep = 0.125;               ///< (c) 600 at stored +/- 0.125 (6 dB)
inline constexpr std::size_t kMinControlSet = 3u;           ///< C-7.3: >= 3 distinct presets
inline constexpr std::string_view kDefaultSurfaceName = "<default surface>";

// ---- Freeze gesture (plan 6.1) ------------------------------------------------------------
/// The block that carries kSpaceFreezeId -> 1.0: the first block whose start
/// sample is >= llround((A + 65) * sr) (the rule renderPreset applies to
/// RenderSpec::freezeAt). `latenessSamples` = startSample - that target.
struct GestureBlock {
    long long block = 0;
    long long startSample = 0;
    long long latenessSamples = 0;
};

[[nodiscard]] inline GestureBlock freezeGestureBlock(double attackSeconds, double sr,
                                                     int blockSize) {
    const long long b = std::max(1LL, static_cast<long long>(blockSize));
    const long long target =
        std::max(0LL, std::llround((attackSeconds + kGestureAfterAttackSeconds) * sr));
    const long long block = (target + b - 1) / b;
    return GestureBlock{block, block * b, (block * b) - target};
}

/// Plan 6.3 G / G0 score. `g` is the gesture render on `tlG` (makeTimeline(st,
/// true)), `g0` the same render with kSpaceMixId -> 0.0; `susDb` is RMS(Sus) of
/// the UNGESTURED stored-seed take. pass = G's arm 1 over its whole length, G's
/// Freeze-On arm 4 (floor included) against `susDb`, and G0 finite with its
/// loudest 10 s Tail window <= susDb - 40. floorDb is the arm 4 floor susDb - 20.
[[nodiscard]] inline GestureResult scoreFreezeGesture(const SweepCapture& g,
                                                      const SweepCapture& g0,
                                                      const SweepTimeline& tlG, double susDb,
                                                      double sr) {
    const ArmResult armsG = evaluateArms(g, tlG, sr);
    const detail::FreezeTailFigures tailG = detail::freezeTailFigures(g, tlG, sr);
    const detail::FreezeTailFigures tailG0 = detail::freezeTailFigures(g0, tlG, sr);
    GestureResult r;
    r.loudestDb = tailG.loudestDb;
    r.lastDb = tailG.lastDb;
    r.floorDb = susDb - kFreezeFloorDb;
    r.dryLoudestDb = tailG0.loudestDb;
    r.pass = armsG.pass1 && detail::freezeOnTailPasses(tailG, susDb) && g0.finite &&
             tailG0.windows > 0u && r.dryLoudestDb <= susDb - kTailDropDb;
    return r;
}

namespace detail {

/// The stored-seed render of the plan 6.3 gesture protocol on `tlG`: NoteOn 36,
/// kSpaceFreezeId -> 1.0 at A + 65 (freezeGestureBlock's block), NoteOff at H,
/// rendered to H + Rel + 70. `dryResidue` adds kSpaceMixId -> 0.0 at block 0 (G0).
[[nodiscard]] inline RenderSpec gestureSpec(std::span<const std::uint8_t> comp,
                                            const SweepTimeline& tlG, int storedSeed,
                                            bool dryResidue) {
    RenderSpec spec;
    spec.comp = comp;
    if (dryResidue) {
        spec.block0.emplace_back(::Vorago::kSpaceMixId, 0.0);
    }
    spec.seedIndex = storedSeed;
    spec.sr = kSweepSampleRate;
    spec.noteOffAt = tlG.H;
    spec.freezeAt = tlG.A + kGestureAfterAttackSeconds;
    spec.end = tlG.total;
    return spec;
}

/// A stored-seed render over [0, A + 65] at `sr` (FR-033a rates, FR-038
/// reproducibility); `captureAll` copies the whole span out.
[[nodiscard]] inline RenderSpec sustainSpec(std::span<const std::uint8_t> comp,
                                            const SweepTimeline& tl, int storedSeed, double sr,
                                            bool captureAll) {
    RenderSpec spec;
    spec.comp = comp;
    spec.seedIndex = storedSeed;
    spec.sr = sr;
    spec.noteOffAt = tl.H;  // past the end: no release inside [0, A + 65]
    spec.end = tl.A + kGestureAfterAttackSeconds;
    if (captureAll) {
        spec.capture = {{0.0, spec.end}};
    }
    return spec;
}

/// Arm 1 only over [0, end] of a sustain render: finite, peak, loudest 10 s window.
inline void scoreRateArm1(const SweepCapture& cap, double end, double sr, bool& finite,
                          float& peak, double& worstHi) {
    finite = cap.finite && !cap.blockPowerL.empty();
    peak = cap.peak;
    worstHi = 0.0;
    const std::vector<SweepWindow> windows = tenSecondWindows(0.0, end, sr);
    for (std::size_t i = 0; i < windows.size(); ++i) {
        const double db = windowDb(cap, windows[i]);
        worstHi = (i == 0u) ? db : std::max(worstHi, db);
    }
}

/// A stored-seed twin rendered to M3.end with `overrides` at block 0, capturing
/// M1..M3; the component-wise mean of the three minute descriptors, or
/// std::nullopt when the render is not finite or a minute is empty.
[[nodiscard]] inline std::optional<PresetDescriptor> minutesMeanOfTwin(
    std::span<const std::uint8_t> comp, const SweepTimeline& tl, int storedSeed,
    ParamOverrides overrides) {
    RenderSpec spec;
    spec.comp = comp;
    spec.block0 = std::move(overrides);
    spec.seedIndex = storedSeed;
    spec.sr = kSweepSampleRate;
    spec.noteOffAt = tl.H;
    spec.end = tl.m[2][1];
    for (int k = 0; k < 3; ++k) {
        spec.capture.emplace_back(tl.m[k][0], tl.m[k][1]);
    }
    const SweepCapture cap = renderPreset(spec);
    std::array<PresetDescriptor, 3> minutes{};
    for (std::size_t k = 0; k < minutes.size(); ++k) {
        const std::optional<PresetDescriptor> d = captureDescriptor(cap, k, spec.sr);
        if (!d.has_value()) {
            return std::nullopt;
        }
        minutes[k] = *d;
    }
    return meanOf(minutes);
}

/// The stored normalized value of `id` in `comp` (the default surface's
/// getState() when `comp` is empty), or std::nullopt.
[[nodiscard]] inline std::optional<double> storedNormalized(std::span<const std::uint8_t> comp,
                                                            Steinberg::Vst::ParamID id) {
    std::vector<std::uint8_t> scratch;
    const std::map<Steinberg::Vst::ParamID, double> stored =
        storedNormalizedValues(stateBytesOrDefault(comp, scratch));
    const auto it = stored.find(id);
    if (it == stored.end()) {
        return std::nullopt;
    }
    return it->second;
}

}  // namespace detail

// ---- Reproducibility (FR-038) ---------------------------------------------------------------
/// Two renders agree: both finite with equal, non-empty whole-span captures, and
/// compareFingerprints(...).withinTolerance() per channel at the shared
/// render_fingerprint.h tolerances.
[[nodiscard]] inline bool reproducibleCaptures(const SweepCapture& a, const SweepCapture& b) {
    namespace TU = Krate::DSP::TestUtils;
    if (!a.finite || !b.finite || a.capL.empty() || b.capL.empty() || a.capR.empty() ||
        b.capR.empty() || a.capL[0].empty() || a.capL[0].size() != b.capL[0].size() ||
        a.capR[0].size() != b.capR[0].size()) {
        return false;
    }
    const bool left = TU::compareFingerprints(TU::fingerprintRender(a.capL[0]),
                                              TU::fingerprintRender(b.capL[0]))
                          .withinTolerance();
    const bool right = TU::compareFingerprints(TU::fingerprintRender(a.capR[0]),
                                               TU::fingerprintRender(b.capR[0]))
                           .withinTolerance();
    return left && right;
}

// ---- Take aggregates (plan 6.5) -------------------------------------------------------------
/// D(P): the component-wise mean of every take's three minute descriptors (3K).
[[nodiscard]] inline PresetDescriptor takesMean(const std::vector<TakeRecord>& takes) {
    std::vector<PresetDescriptor> all;
    all.reserve(3u * takes.size());
    for (const TakeRecord& t : takes) {
        all.insert(all.end(), t.minutes.begin(), t.minutes.end());
    }
    return meanOf(all);
}

/// s(P) = max pairwise d among the K-take-averaged minutes m1, m2, m3 (Q4).
[[nodiscard]] inline double takesSelfDistance(const std::vector<TakeRecord>& takes) {
    std::array<PresetDescriptor, 3> bar{};
    std::vector<PresetDescriptor> perMinute;
    perMinute.reserve(takes.size());
    for (std::size_t k = 0; k < bar.size(); ++k) {
        perMinute.clear();
        for (const TakeRecord& t : takes) {
            perMinute.push_back(t.minutes[k]);
        }
        bar[k] = meanOf(perMinute);
    }
    return std::max({descriptorDistance(bar[0], bar[1]), descriptorDistance(bar[0], bar[2]),
                     descriptorDistance(bar[1], bar[2])});
}

// ---- Controls (plan 6.13; T001 item 2: same seed, single take, except (b)) ------------------

/// The control set C: indices into `records`, in rule order with duplicates
/// removed (argmax s(P), argmin s(P), the S8 preset, highest stored Pressure,
/// highest stored Weight; ties -> the first record). The S8 preset is the
/// record whose D10.1 entry has stateOk (computeVerificationVector sets it iff
/// the preset's primary is S8). ok == false (an FR-017 stop for the caller)
/// when fewer than 3 distinct presets result.
struct ControlSet {
    std::vector<std::size_t> members;
    bool ok = false;
};

[[nodiscard]] inline ControlSet controlSet(std::span<const SweepRecord> records) {
    ControlSet cs;
    if (records.empty()) {
        return cs;
    }
    const auto argBest = [&records](auto better) {
        std::size_t best = 0;
        for (std::size_t i = 1; i < records.size(); ++i) {
            if (better(records[i], records[best])) {
                best = i;
            }
        }
        return best;
    };
    const auto add = [&cs](std::size_t i) {
        if (std::find(cs.members.begin(), cs.members.end(), i) == cs.members.end()) {
            cs.members.push_back(i);
        }
    };
    add(argBest([](const SweepRecord& a, const SweepRecord& b) {
        return a.selfDistance > b.selfDistance;
    }));
    add(argBest([](const SweepRecord& a, const SweepRecord& b) {
        return a.selfDistance < b.selfDistance;
    }));
    constexpr auto kD10 =
        static_cast<std::size_t>(::Vorago::PresetDefs::Capability::D10FreezeHolds);
    for (std::size_t i = 0; i < records.size(); ++i) {
        if (records[i].vec.cells[kD10].stateOk) {
            add(i);
            break;
        }
    }
    add(argBest([](const SweepRecord& a, const SweepRecord& b) {
        return a.storedPressure > b.storedPressure;
    }));
    add(argBest([](const SweepRecord& a, const SweepRecord& b) {
        return a.storedWeight > b.storedWeight;
    }));
    cs.ok = cs.members.size() >= kMinControlSet;
    return cs;
}

/// Control (a2), the gain twin: the stored-seed render with kMasterGainId (0) at
/// 0.5 x its stored normalized value, M1..M3 mean vs `storedMinutesMean` (the
/// stored-seed take's M1..M3 mean). std::nullopt when the render or the lookup
/// fails.
[[nodiscard]] inline std::optional<double> gainTwinD(const std::vector<std::uint8_t>& comp,
                                                     const SweepTimeline& tl, int storedSeed,
                                                     const PresetDescriptor& storedMinutesMean) {
    const std::optional<double> gain = detail::storedNormalized(comp, ::Vorago::kMasterGainId);
    if (!gain.has_value()) {
        return std::nullopt;
    }
    const std::optional<PresetDescriptor> twin = detail::minutesMeanOfTwin(
        comp, tl, storedSeed, ParamOverrides{{::Vorago::kMasterGainId, kGainTwinScale * *gain}});
    if (!twin.has_value()) {
        return std::nullopt;
    }
    return descriptorDistance(*twin, storedMinutesMean);
}

/// Control (c), the sub twin: kSubLevelOffsetId (600) at stored - 0.125 and
/// stored + 0.125; a side leaving [0, 1] is not rendered. The sides run are
/// recorded; ok iff at least one side ran and every run side rendered and scored
/// d < 4.0.
struct SubTwinResult {
    bool minusRun = false;
    bool plusRun = false;
    double dMinus = 0.0;
    double dPlus = 0.0;
    bool ok = false;
};

[[nodiscard]] inline SubTwinResult subTwinD(const std::vector<std::uint8_t>& comp,
                                            const SweepTimeline& tl, int storedSeed,
                                            const PresetDescriptor& storedMinutesMean,
                                            unsigned threads) {
    SubTwinResult r;
    const std::optional<double> sub = detail::storedNormalized(comp, ::Vorago::kSubLevelOffsetId);
    if (!sub.has_value()) {
        return r;
    }
    const double lo = *sub - kSubTwinStep;
    const double hi = *sub + kSubTwinStep;
    r.minusRun = lo >= 0.0;
    r.plusRun = hi <= 1.0;
    std::optional<PresetDescriptor> minus;
    std::optional<PresetDescriptor> plus;
    std::vector<std::function<void()>> jobs;
    if (r.minusRun) {
        jobs.emplace_back([&comp, &tl, &minus, storedSeed, lo] {
            minus = detail::minutesMeanOfTwin(comp, tl, storedSeed,
                                              ParamOverrides{{::Vorago::kSubLevelOffsetId, lo}});
        });
    }
    if (r.plusRun) {
        jobs.emplace_back([&comp, &tl, &plus, storedSeed, hi] {
            plus = detail::minutesMeanOfTwin(comp, tl, storedSeed,
                                             ParamOverrides{{::Vorago::kSubLevelOffsetId, hi}});
        });
    }
    runJobs(jobs, threads);
    bool ok = r.minusRun || r.plusRun;
    if (r.minusRun) {
        r.dMinus = minus.has_value() ? descriptorDistance(*minus, storedMinutesMean) : 0.0;
        ok = ok && minus.has_value() && r.dMinus < kFloorF;
    }
    if (r.plusRun) {
        r.dPlus = plus.has_value() ? descriptorDistance(*plus, storedMinutesMean) : 0.0;
        ok = ok && plus.has_value() && r.dPlus < kFloorF;
    }
    r.ok = ok;
    return r;
}

/// Control (b), the seed twin: render the K takes of B_K (takeSeedIndex(stored,
/// 1, j, K)) as ordinary main takes and return t_K = d(D_A, D_B), D_B =
/// takesMean(B takes). std::nullopt when K <= 0 or any B take is not finite.
[[nodiscard]] inline std::optional<double> seedTwinTK(const std::vector<std::uint8_t>& comp,
                                                      const SweepTimeline& tl, int storedSeed,
                                                      int K, const PresetDescriptor& dA,
                                                      unsigned threads) {
    if (K <= 0) {
        return std::nullopt;
    }
    std::vector<TakeRecord> takes(static_cast<std::size_t>(K));
    std::vector<std::function<void()>> jobs;
    jobs.reserve(takes.size());
    for (int j = 0; j < K; ++j) {
        jobs.emplace_back([&comp, &tl, &takes, storedSeed, j, K] {
            takes[static_cast<std::size_t>(j)] =
                renderTake(comp, tl, takeSeedIndex(storedSeed, 1, j, K), std::nullopt);
        });
    }
    runJobs(jobs, threads);
    for (const TakeRecord& t : takes) {
        if (!t.finite) {
            return std::nullopt;
        }
    }
    return descriptorDistance(dA, takesMean(takes));
}

// ---- The record (plan 5.8) ---------------------------------------------------------------------
/// One preset's complete sweep record. `defIndex` < N is allPresets()[defIndex]
/// (Comp built through buildPresetComponentState); defIndex == N is the
/// default-surface pseudo-preset (empty comp, no setState). Assembles:
///  - the timeline (makeTimeline(decode, false)) and the stored Pressure / Weight;
///  - the K takes of A_K (arms 1-4 on every take; the D8.2 / D9.1 primary's
///    stored-seed take also captures [0, W_end]);
///  - D(P) = mean of the 3K minutes, s(P) from the K-take-averaged minutes;
///  - the level twin (a) of the stored-seed take;
///  - the verification vector (P_Sus = describe(M1) of the stored-seed take);
///  - the S8 preset only: the gesture G and dry-residue G0 (RMS(Sus) from the
///    ungestured stored-seed take), filling GestureResult and the D10.1 entry;
///  - FR-033a: stored-seed renders over [0, A + 65] at 44.1 and 96 kHz, arm 1;
///  - FR-038: two fresh hosts on two threads over [0, A + 65], fingerprints.
/// A definition that does not build or decode, an index past N, or K <= 0
/// returns the record with no takes (the caller REQUIREs takes.size() == K);
/// a build / decode failure is appended to the name.
[[nodiscard]] inline SweepRecord computeSweepRecord(std::size_t defIndex, int takes,
                                                    unsigned threads) {
    using C = ::Vorago::PresetDefs::Capability;
    constexpr auto kR = std::memory_order_relaxed;
    SweepRecord rec;
    rec.takeCount = takes;

    const std::vector<::Vorago::PresetDefs::VoragoPresetDef>& defs =
        ::Vorago::PresetDefs::allPresets();
    if (defIndex > defs.size()) {
        rec.name = "<invalid definition index " + std::to_string(defIndex) + ">";
        return rec;
    }
    const ::Vorago::PresetDefs::VoragoPresetDef* def =
        (defIndex < defs.size()) ? &defs[defIndex] : nullptr;
    rec.name = (def != nullptr) ? std::string(def->name) : std::string(kDefaultSurfaceName);

    std::vector<std::uint8_t> comp;  // empty: the default surface
    if (def != nullptr) {
        std::string why;
        if (!buildPresetComponentState(*def, comp, why)) {
            rec.name += " <build failed: " + why + ">";
            return rec;
        }
    }
    std::vector<std::uint8_t> scratch;
    DecodedPresetState st;
    if (!decodePresetState(detail::stateBytesOrDefault(comp, scratch), st)) {
        rec.name += " <decode failed>";
        return rec;
    }
    const SweepTimeline tl = makeTimeline(st, false);
    rec.tl = tl;
    rec.storedPressure = static_cast<double>(st.macros.pressure.load(kR));
    rec.storedWeight = static_cast<double>(st.macros.weight.load(kR));
    const int storedSeed = st.global.seedIndex.load(kR);
    if (takes <= 0) {
        return rec;
    }

    // The K-take main render (take 0 is the stored seed).
    rec.takes = computeTakes(comp, tl, storedSeed, takes, threads,
                             attackWindowEndSeconds(def, comp));
    const TakeRecord& storedTake = rec.takes.front();
    rec.mean = takesMean(rec.takes);
    rec.selfDistance = takesSelfDistance(rec.takes);
    rec.levelTwinD = storedTake.levelTwinD;

    rec.vec = computeVerificationVector(def, comp, tl, rec.selfDistance,
                                        meanOf(std::span<const PresetDescriptor>(storedTake.minutes)),  // ruling 2026-09-30: M1..M3
                                        threads, &storedTake);

    // Rates, reproducibility and (S8 preset only) the gesture pair in one batch.
    const std::span<const std::uint8_t> renderComp(comp);
    const bool gesture = def != nullptr && def->primary == C::S8Cavern;
    const SweepTimeline tlG = makeTimeline(st, true);
    std::vector<RenderSpec> specs;
    specs.reserve(6u);
    specs.push_back(detail::sustainSpec(renderComp, tl, storedSeed, kRate441, false));
    specs.push_back(detail::sustainSpec(renderComp, tl, storedSeed, kRate96, false));
    specs.push_back(detail::sustainSpec(renderComp, tl, storedSeed, kSweepSampleRate, true));
    specs.push_back(detail::sustainSpec(renderComp, tl, storedSeed, kSweepSampleRate, true));
    if (gesture) {
        specs.push_back(detail::gestureSpec(renderComp, tlG, storedSeed, false));  // G
        specs.push_back(detail::gestureSpec(renderComp, tlG, storedSeed, true));   // G0
    }
    std::vector<SweepCapture> caps(specs.size());
    std::vector<std::function<void()>> jobs;
    jobs.reserve(specs.size());
    for (std::size_t j = 0; j < specs.size(); ++j) {
        jobs.emplace_back([&specs, &caps, j] { caps[j] = renderPreset(specs[j]); });
    }
    runJobs(jobs, threads);

    const double sustainEnd = tl.A + kGestureAfterAttackSeconds;
    detail::scoreRateArm1(caps[0], sustainEnd, kRate441, rec.rates.finite441, rec.rates.peak441,
                          rec.rates.worstHi441);
    detail::scoreRateArm1(caps[1], sustainEnd, kRate96, rec.rates.finite96, rec.rates.peak96,
                          rec.rates.worstHi96);
    rec.reproducible = reproducibleCaptures(caps[2], caps[3]);

    if (gesture) {
        rec.gesture =
            scoreFreezeGesture(caps[4], caps[5], tlG, storedTake.susDb, kSweepSampleRate);
        // D10.1 (FR-011b, SC-024): stateOk = "this def's primary is S8", its
        // conjunct scored on the gesture render.
        CellOutcome& d10 = rec.vec.cells[static_cast<std::size_t>(C::D10FreezeHolds)];
        d10.stateOk = true;
        d10.conjunctOk = rec.gesture.pass;
        d10.rendered = true;
        d10.d = 0.0;
        d10.skip.clear();
    }
    return rec;
}

}  // namespace VoragoTest

// ==============================================================================
// Memo (T040, plan 5.8 "Memo"): one SweepRecord per definition index, computed
// (or loaded) once per process and shared by every TU that asks (the sweep and
// matrix TUs share this inline function's single static cache).
// ==============================================================================

#include <mutex>

namespace VoragoTest {

/// The sweep pool width (plan 5.8 Pool): VORAGO_SWEEP_THREADS when a positive
/// integer, else min(hardware_concurrency, 4) (at least 1).
[[nodiscard]] inline unsigned sweepPoolWidth() {
    if (const std::optional<std::string> env = sweepEnv("VORAGO_SWEEP_THREADS")) {
        const unsigned long v = std::strtoul(env->c_str(), nullptr, 10);
        if (v > 0ul) {
            return static_cast<unsigned>(v);
        }
    }
    return std::clamp(std::thread::hardware_concurrency(), 1u, 4u);
}

/// `<dir>/record_<defIndex>.txt`, the per-index record file of VORAGO_SWEEP_IN / _OUT.
[[nodiscard]] inline std::filesystem::path sweepRecordPath(const std::string& dir,
                                                           std::size_t defIndex) {
    return std::filesystem::path(dir) / ("record_" + std::to_string(defIndex) + ".txt");
}

/// The record of definition index `defIndex` (N = the default-surface pseudo-preset).
///  - VORAGO_SWEEP_IN set: loads `<dir>/record_<defIndex>.txt` (the aggregate job);
///    a missing or malformed file yields a record with NO takes and the failure in
///    its name, which the caller's REQUIRE(takes.size() == kRuledTakes) reports.
///  - otherwise: computeSweepRecord(defIndex, kRuledTakes, sweepPoolWidth()); with
///    VORAGO_SWEEP_OUT set each computed record is also written to
///    `<dir>/record_<defIndex>.txt` (directory created as needed).
/// Function-local static cache; the mutex is held only for lookup / insert, never
/// while rendering or loading (test threads only). std::map nodes are stable, so
/// the returned reference lives for the process.
[[nodiscard]] inline const SweepRecord& sweepRecordFor(std::size_t defIndex) {
    static std::mutex mutex;
    static std::map<std::size_t, SweepRecord> cache;
    {
        const std::lock_guard<std::mutex> lock(mutex);
        const auto it = cache.find(defIndex);
        if (it != cache.end()) {
            return it->second;
        }
    }

    SweepRecord rec;
    if (const std::optional<std::string> inDir = sweepEnv("VORAGO_SWEEP_IN")) {
        const std::filesystem::path path = sweepRecordPath(*inDir, defIndex);
        if (!readRecord(path, rec)) {
            rec.name = "<missing or malformed record " + path.string() + ">";
        }
    } else {
        rec = computeSweepRecord(defIndex, kRuledTakes, sweepPoolWidth());
        if (const std::optional<std::string> outDir = sweepEnv("VORAGO_SWEEP_OUT")) {
            std::error_code ec;
            std::filesystem::create_directories(std::filesystem::path(*outDir), ec);
            writeRecord(rec, sweepRecordPath(*outDir, defIndex));
        }
    }

    const std::lock_guard<std::mutex> lock(mutex);
    return cache.try_emplace(defIndex, std::move(rec)).first->second;
}

}  // namespace VoragoTest
