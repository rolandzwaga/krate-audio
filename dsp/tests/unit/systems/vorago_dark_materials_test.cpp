// ==============================================================================
// Layer 3: System Tests - the six dark ContinuousBody materials
//                                    (specs/vorago-phase10-voice-engine)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase10-voice-engine/spec.md
//            specs/vorago-phase10-voice-engine/plan.md
//            specs/vorago-phase10-voice-engine/tasks.md  (T006 creates and wires
//                                                         this TU; T008 lands the
//                                                         append-only enum and
//                                                         table cases; T026 lands
//                                                         SC-015)
//
// SCOPE OF THIS TU: the AR-4 material append to ContinuousBody - FR-039's
//   append-only enumerator and profile-table assertions (written as
//   static_assert, so a regression is a build break rather than a run-time
//   failure), SC-016 clauses 3 and 4, and SC-015's spectral separation, which
//   is the one [long] case here.
//
// ALLOCATION DETECTION: this TU includes neither <allocation_detector.h> nor
//   <allocation_operator_overrides.h>. The single owner of the global
//   operator new/delete replacements in dsp_systems_tests is
//   unit/systems/selectable_oscillator_test.cpp:388; a second include of
//   <allocation_operator_overrides.h> is a duplicate-symbol link error.
//
// PORTABILITY: no std::isnan / std::isinf / std::isfinite anywhere in this TU,
//   so it stays correct under -ffast-math.
// ==============================================================================

#include <catch2/catch_all.hpp>

#include <krate/dsp/core/random.h>
#include <krate/dsp/primitives/pink_noise_filter.h>
#include <krate/dsp/systems/continuous_body.h>

// tests/test_helpers is on the include path for every dsp_* target
// (tests/test_helpers/CMakeLists.txt: an INTERFACE library whose include
// directory is inherited through `target_link_libraries(... test_helpers)`).
// extractAudioFeatures is the ONE spectral-centroid implementation in this
// repo, so SC-015 and continuous_body_spectral_test.cpp's SC-003(b) cannot
// disagree about what "the centroid" is.
#include <audio_features.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <vector>

namespace {

using CB = Krate::DSP::ContinuousBody;
using BM = CB::BodyMaterial;

/// The underlying value of an enumerator, as a std::size_t, for index maths.
[[nodiscard]] constexpr std::size_t idxOf(BM m) noexcept
{
    return static_cast<std::size_t>(m);
}

/// FR-039 clause: every one of the six APPENDED materials is `Engine::Modal`
/// with `defaultModeCount <= kModeCountCeiling`.
///
/// The all-Modal ruling is load-bearing, not incidental (plan S4.0):
/// `configureNonModalSlot` reaches only referenceHz / t60 / hfDampingParam, so
/// two non-modal dark materials could differ in three fields only and could not
/// satisfy SC-015's 5 % separation; and modal bank *i* is bound to slot *i*, so
/// only modal/modal pairs crossfade without taking FR-024a's engine collapse.
[[nodiscard]] constexpr bool newMaterialsAreModalWithinCeiling() noexcept
{
    for (std::size_t i = CB::kNumSeraphisMaterials; i < CB::kNumMaterials; ++i) {
        const CB::MaterialProfile& p = CB::kMaterialProfiles[i];
        if (p.engine != CB::Engine::Modal) {
            return false;
        }
        if (p.defaultModeCount > CB::kModeCountCeiling) {
            return false;
        }
    }
    return true;
}

/// SC-016 clause 4, as a constexpr predicate so the pairing cannot silently
/// widen: the crossfade partner of material *i* is its cyclic successor
/// WITHIN ITS OWN BLOCK - the five Seraphis materials cycle among themselves,
/// the six Vorago materials cycle among themselves.
///
/// This is the mapping `continuous_body_perf_test.cpp:373-379` must carry after
/// FR-038 site 4. A plain `(i + 1) % kNumMaterials` widening would re-point
/// Ice's partner from Glass to StoneChamber and thereby change a
/// Seraphis-MEASURED crossfade pairing, which SC-016 forbids.
[[nodiscard]] constexpr BM crossfadePartnerOf(BM m) noexcept
{
    const std::size_t i = idxOf(m);
    if (i < CB::kNumSeraphisMaterials) {
        return static_cast<BM>((i + 1u) % CB::kNumSeraphisMaterials);
    }
    const std::size_t numDark = CB::kNumMaterials - CB::kNumSeraphisMaterials;
    return static_cast<BM>(CB::kNumSeraphisMaterials
                           + ((i - CB::kNumSeraphisMaterials + 1u) % numDark));
}

/// SC-016 clause 4, stated once for the whole table rather than row by row, so
/// a future append to either block cannot leak a partner across the boundary
/// unnoticed: every material's partner sits in the same block it does.
[[nodiscard]] constexpr bool crossfadeBlocksAreClosed() noexcept
{
    for (std::size_t i = 0; i < CB::kNumMaterials; ++i) {
        const std::size_t partner = idxOf(crossfadePartnerOf(static_cast<BM>(i)));
        const bool srcIsSeraphis = i < CB::kNumSeraphisMaterials;
        const bool dstIsSeraphis = partner < CB::kNumSeraphisMaterials;
        if (srcIsSeraphis != dstIsSeraphis) {
            return false;
        }
    }
    return true;
}

}  // namespace

// ==============================================================================
// FR-039 - the append is an append: no stored material index moved
// ==============================================================================

TEST_CASE("ContinuousBody_AppendOnlyEnumerators", "[systems][vorago]")
{
    // --- clause 1: the five Seraphis enumerators keep their values ------------
    // A BodyMaterial is persisted by underlying value; moving any of these five
    // would silently re-point every stored Seraphis state.
    static_assert(idxOf(BM::Glass) == 0u, "FR-039: Glass must stay 0");
    static_assert(idxOf(BM::Strings) == 1u, "FR-039: Strings must stay 1");
    static_assert(idxOf(BM::MetalPlate) == 2u, "FR-039: MetalPlate must stay 2");
    static_assert(idxOf(BM::Chamber) == 3u, "FR-039: Chamber must stay 3");
    static_assert(idxOf(BM::Ice) == 4u, "FR-039: Ice must stay 4");

    // --- clause 2: the six new enumerators are 5..10, in this exact order -----
    static_assert(idxOf(BM::StoneChamber) == 5u, "FR-039: StoneChamber is the first append");
    static_assert(idxOf(BM::SteelTank) == 6u, "FR-039: SteelTank follows StoneChamber");
    static_assert(idxOf(BM::WoodenHull) == 7u, "FR-039: WoodenHull follows SteelTank");
    static_assert(idxOf(BM::CathedralColumn) == 8u, "FR-039: CathedralColumn follows WoodenHull");
    static_assert(idxOf(BM::CavernWall) == 9u, "FR-039: CavernWall follows CathedralColumn");
    static_assert(idxOf(BM::GlassSphere) == 10u, "FR-039: GlassSphere is the last append");

    // --- clause 3: the counts and the profile table agree --------------------
    static_assert(CB::kNumMaterials == 11u, "FR-039: five shipped + six appended");
    static_assert(CB::kNumSeraphisMaterials == 5u,
                  "FR-038a: Seraphis's measured subject is the five-material prefix");
    static_assert(CB::kNumSeraphisMaterials < CB::kNumMaterials,
                  "FR-038a: the Seraphis prefix is a strict prefix");
    static_assert(CB::kMaterialProfiles.size() == CB::kNumMaterials,
                  "FR-011a: one profile per material - a short initialiser list would "
                  "zero-fill rather than fail to compile");

    // --- clause 4: every appended material is Modal and within the ceiling ----
    static_assert(newMaterialsAreModalWithinCeiling(),
                  "plan S4.0: all six dark materials are Engine::Modal with "
                  "defaultModeCount <= kModeCountCeiling");

    SUCCEED("all clauses are static_assert - a regression is a build break");
}

// ==============================================================================
// SC-016 clause 4 - Seraphis's crossfade pairings are unchanged
// ==============================================================================

TEST_CASE("ContinuousBody_MaterialTableIsAppendOnly", "[systems][vorago]")
{
    // --- the Seraphis five-cycle, unchanged ----------------------------------
    static_assert(crossfadePartnerOf(BM::Glass) == BM::Strings, "SC-016(4): Glass -> Strings");
    static_assert(crossfadePartnerOf(BM::Strings) == BM::MetalPlate,
                  "SC-016(4): Strings -> MetalPlate");
    static_assert(crossfadePartnerOf(BM::MetalPlate) == BM::Chamber,
                  "SC-016(4): MetalPlate -> Chamber");
    static_assert(crossfadePartnerOf(BM::Chamber) == BM::Ice, "SC-016(4): Chamber -> Ice");
    static_assert(crossfadePartnerOf(BM::Ice) == BM::Glass,
                  "SC-016(4): Ice wraps to Glass, NOT to StoneChamber - a plain "
                  "(i + 1) % kNumMaterials widening would change a Seraphis-measured pairing");

    // --- the six new materials cycle among themselves ------------------------
    static_assert(crossfadePartnerOf(BM::StoneChamber) == BM::SteelTank,
                  "SC-016(4): StoneChamber -> SteelTank");
    static_assert(crossfadePartnerOf(BM::SteelTank) == BM::WoodenHull,
                  "SC-016(4): SteelTank -> WoodenHull");
    static_assert(crossfadePartnerOf(BM::WoodenHull) == BM::CathedralColumn,
                  "SC-016(4): WoodenHull -> CathedralColumn");
    static_assert(crossfadePartnerOf(BM::CathedralColumn) == BM::CavernWall,
                  "SC-016(4): CathedralColumn -> CavernWall");
    static_assert(crossfadePartnerOf(BM::CavernWall) == BM::GlassSphere,
                  "SC-016(4): CavernWall -> GlassSphere");
    static_assert(crossfadePartnerOf(BM::GlassSphere) == BM::StoneChamber,
                  "SC-016(4): GlassSphere wraps to StoneChamber, staying inside the "
                  "Vorago block");

    // --- the two blocks are closed under the mapping -------------------------
    static_assert(crossfadeBlocksAreClosed(),
                  "SC-016(4): the Seraphis block and the Vorago block are each closed "
                  "under crossfadePartner");

    SUCCEED("all clauses are static_assert - a regression is a build break");
}

// ==============================================================================
// SC-015 - the six dark materials are DARKER than every shipped one, and
//          DISTINCT from each other
// ==============================================================================
// Timbre only. CPU is SC-025's job (ContinuousBody_DarkMaterialBudgets in
// continuous_body_perf_test.cpp, T009/T031) and is deliberately not measured
// here: this case renders 550 s of audio and would be a hopeless timer.
//
// If a material misses, the lever is that material's alpha, then its b1
// damping, then its mode count, IN THAT ORDER - re-voice the row in
// continuous_body.h and re-measure. NEVER relax this case. Terminal step: drop
// the material from the six and surface the drop to the user (FR-038b).
// ==============================================================================

namespace {

// --- the measurement configuration, fixed for all eleven ---------------------

constexpr double kSampleRate = 48000.0;
constexpr std::size_t kSampleRateHz = 48000;
static_assert(kSampleRate == static_cast<double>(kSampleRateHz),
              "the second-to-sample conversions below assume one rate");

/// 8 x kControlChunkSamples, so every full block lands exactly on the control
/// grid and the one short trailing block (256 samples) still does.
constexpr std::size_t kBlockSize = 512;
static_assert(kBlockSize % CB::kControlChunkSamples == 0,
              "a block must be a whole number of control chunks");

/// f_body, identical for all eleven. keyTracking stays at its FR-009 default of
/// 1.0, so f_body == kNoteHz for every material INDEPENDENTLY of the profile's
/// referenceHz (which spans 55 Hz to 880 Hz across the eleven rows) - otherwise
/// this case would be measuring the reference pitches, not the timbres.
constexpr float kNoteHz = 110.0f;

/// Resonance and damping at their FR-009 defaults, identical for all eleven.
/// Damping is the frequency-DEPENDENT control (continuous_body_spectral_test's
/// SC-003(b)); holding it at 0 keeps the measured centroid a property of the
/// material profile alone.
constexpr float kResonance = CB::kDefaultResonance;
constexpr float kDamping = CB::kDefaultDamping;

/// The excitation seed, fixed by value so a failure is reproducible.
constexpr std::uint32_t kExcitationSeed = 0x5EEDDA12u;

/// Pink noise is already normalised to [-1, 1] by PinkNoiseFilter's 0.2 output
/// factor (pink_noise_filter.h:96-100), so the excitation runs at unity. Level
/// is not a free parameter of this criterion - a spectral centroid is
/// scale-invariant - but it must stay high enough that the modal bank's own
/// energy cull never fires on a quiet upper mode.
constexpr float kExcitationAmplitude = 1.0f;

/// 30 s of settle, then the 20 s analysis window. The settle is >= 3 x the
/// longest engine T60 at kResonance: resonanceScale(0.7) is 2^(log2(40)*0.3) =
/// 3.02, so the longest shipped row (MetalPlate, t60AtMaxResonanceSec = 23.0)
/// rings for 23.0 / 3.02 = 7.6 s and the longest new row (CathedralColumn,
/// 20.0) for 6.6 s. The 500 ms material crossfade (kMaterialCrossfadeMs) that
/// assignBody's detour starts is over sixty times over before the window opens.
///
/// The settle is a FIXED number of samples rather than a per-material multiple
/// of that material's own T60, so the 20 s window sees the SAME pink-noise
/// samples for every one of the eleven - which is what "identical excitation"
/// means here.
constexpr std::size_t kSettleSeconds = 30;
constexpr std::size_t kWindowSeconds = 20;
constexpr std::size_t kSettleSamples = kSettleSeconds * kSampleRateHz;
constexpr std::size_t kWindowSamples = kWindowSeconds * kSampleRateHz;

/// SC-015's separation floor: every pair of NEW materials differs by >= 5 % of
/// the LARGER of the two centroids. The larger is the stricter of the two
/// symmetric forms - a pair that clears it also clears the |a-b| / min(a,b)
/// form - and SC-015 is a criterion that is never relaxed.
constexpr double kMinSeparation = 0.05;

/// Enumerator order, so the index into this array IS the BodyMaterial value.
constexpr std::array<const char*, CB::kNumMaterials> kMaterialNames = {{
    "Glass",            // 0  shipped
    "Strings",          // 1  shipped
    "MetalPlate",       // 2  shipped
    "Chamber",          // 3  shipped
    "Ice",              // 4  shipped
    "StoneChamber",     // 5  new
    "SteelTank",        // 6  new
    "WoodenHull",       // 7  new
    "CathedralColumn",  // 8  new
    "CavernWall",       // 9  new
    "GlassSphere",      // 10 new
}};
static_assert(kMaterialNames.size() == CB::kNumMaterials,
              "one name per material - a short initialiser list would zero-fill with "
              "null const char* rather than fail to compile (FR-038)");

// --- the excitation ----------------------------------------------------------

/// @brief Deterministic pink noise: Xorshift32 -> Paul Kellet's filter.
///
/// One generator drives BOTH input channels with the same sample, so the
/// component's mono sum 0.5 * (L + R) is exactly this signal and the excitation
/// the engines see is the one written here.
class PinkExcitation {
public:
    void prepare(double sampleRate, std::uint32_t seed) noexcept
    {
        rng_.seed(seed);
        pink_.prepare(static_cast<float>(sampleRate));
        pink_.reset();
    }

    [[nodiscard]] float next() noexcept { return pink_.process(rng_.nextFloat()); }

private:
    Krate::DSP::Xorshift32 rng_{1u};
    Krate::DSP::PinkNoiseFilter pink_;
};

// --- render helpers ----------------------------------------------------------

/// @brief Advance the control grid with a silent input.
void settleSilent(CB& body, std::size_t numBlocks)
{
    std::array<float, kBlockSize> zeros{};
    std::array<float, kBlockSize> outLeft{};
    std::array<float, kBlockSize> outRight{};
    for (std::size_t b = 0; b < numBlocks; ++b) {
        body.processStereoBlock(zeros.data(), zeros.data(), outLeft.data(), outRight.data(),
                                zeros.size());
    }
}

/// @brief Settle `body` at the fixed pitch / resonance / damping above and force
///        a FRESH material assignment, so the mode set, G-hat and the snapped
///        drive are all derived at exactly the configuration under test.
///
/// The detour is the shape continuous_body_spectral_test.cpp:150-168 uses, for
/// the same reason: FR-014 makes `setMaterial(current)` a no-op, so a detour
/// material is needed to force a full assignment even when the body already
/// carries the material under test (every body here starts on the
/// kDefaultMaterial Glass). Chamber is the detour for every material except
/// Chamber itself, where it is Glass.
///
/// AGC off (FR-034a): rmsGain is then exactly 1, so getDriveGain() is CONSTANT
/// across the whole render and no follower dynamics sit inside the measurement
/// window. cloudMix = 0: this criterion measures the ENGINE, never the decay
/// cloud, which is identically configured for every material and would drag all
/// eleven centroids toward one another.
void assignBody(CB& body, CB::BodyMaterial material)
{
    body.prepare(kSampleRate);
    body.setCloudMix(0.0f);
    body.setInputAgcEnabled(false);
    body.setMix(CB::kMaxMix);
    body.setKeyTracking(CB::kMaxKeyTracking);
    body.setDrive(CB::kDefaultUserDrive);
    body.setResonance(kResonance);
    body.setDamping(kDamping);
    body.setNoteFrequencyHz(kNoteHz);
    // 32 x 512 = 341 ms at 48 kHz, > 17 x kPitchSmoothMs, and
    // OnePoleSmoother::advanceSamples snaps exactly to target once it is inside
    // its completion threshold - so f_body is the target BIT-EXACTLY before the
    // material (and therefore the mode count) is assigned.
    settleSilent(body, 32u);
    const CB::BodyMaterial detour = (material == CB::BodyMaterial::Chamber)
                                        ? CB::BodyMaterial::Glass
                                        : CB::BodyMaterial::Chamber;
    body.setMaterial(detour);
    body.setMaterial(material);
}

/// @brief Render settle + window through `body` and return the window's LEFT
///        output.
///
/// The resonator core is mono and the decay cloud is held at cloudMix = 0 by
/// assignBody, so left and right are identical and measuring one channel is
/// sufficient.
[[nodiscard]] std::vector<float> renderWindow(CB& body)
{
    PinkExcitation excitation;
    excitation.prepare(kSampleRate, kExcitationSeed);

    std::vector<float> window;
    window.reserve(kWindowSamples);

    std::array<float, kBlockSize> in{};
    std::array<float, kBlockSize> outLeft{};
    std::array<float, kBlockSize> outRight{};

    const std::size_t total = kSettleSamples + kWindowSamples;
    std::size_t done = 0;
    while (done < total) {
        const std::size_t n = std::min(kBlockSize, total - done);
        for (std::size_t i = 0; i < n; ++i) {
            in[i] = kExcitationAmplitude * excitation.next();
        }
        body.processStereoBlock(in.data(), in.data(), outLeft.data(), outRight.data(), n);
        for (std::size_t i = 0; i < n; ++i) {
            if (done + i >= kSettleSamples) {
                window.push_back(outLeft[i]);
            }
        }
        done += n;
    }
    return window;
}

}  // namespace

TEST_CASE("ContinuousBody_DarkMaterialsSpectral", "[systems][vorago][long]")
{
    // -------------------------------------------------------------------------
    // 1. Measure all eleven - no REQUIRE inside this loop
    // -------------------------------------------------------------------------
    // A REQUIRE aborts the case, so gating a material where it is measured would
    // let the first miss hide the other ten and the whole separation clause.
    // Every figure goes on the record before anything can fail - the discipline
    // continuous_body_perf_test.cpp:909-917 sets.
    std::array<double, CB::kNumMaterials> centroid{};
    std::array<bool, CB::kNumMaterials> measured{};

    for (std::size_t i = 0; i < CB::kNumMaterials; ++i) {
        CB body;
        assignBody(body, static_cast<BM>(i));
        const std::vector<float> window = renderWindow(body);
        centroid[i] = Krate::Test::extractAudioFeatures(window, kSampleRate).centroidHz;
        measured[i] = body.stateFinite() && (window.size() == kWindowSamples);
    }

    constexpr std::size_t kFirstNew = CB::kNumSeraphisMaterials;
    const auto shippedMinIt = std::min_element(
        centroid.begin(), centroid.begin() + static_cast<std::ptrdiff_t>(kFirstNew));
    const double shippedMin = *shippedMinIt;
    const auto shippedMinIndex =
        static_cast<std::size_t>(std::distance(centroid.begin(), shippedMinIt));

    // -------------------------------------------------------------------------
    // 2. The report - all eleven centroids, printed before any gate
    // -------------------------------------------------------------------------
    {
        std::ostringstream os;
        os << "SC-015 steady-state spectral centroid, all eleven materials.\n"
           << "  f_body = " << kNoteHz << " Hz (keyTracking 1), resonance = " << kResonance
           << ", damping = " << kDamping << ", cloud off, AGC off;\n"
           << "  identical pink-noise excitation from seed 0x" << std::hex << kExcitationSeed
           << std::dec << ", measured over the last " << kWindowSeconds << " s of a "
           << (kSettleSeconds + kWindowSeconds) << " s render.\n";
        os << std::fixed << std::setprecision(2);
        for (std::size_t i = 0; i < CB::kNumMaterials; ++i) {
            const CB::MaterialProfile& p = CB::kMaterialProfiles[i];
            os << (i < kFirstNew ? "  [shipped] " : "  [new]     ") << kMaterialNames[i]
               << " : centroid " << centroid[i] << " Hz  (alpha = " << p.amplitudeExponent
               << ", b1 = " << p.damping.b1 << ", modes = " << p.defaultModeCount
               << ", finite = " << (measured[i] ? "yes" : "NO") << ")\n";
        }
        os << "  shipped minimum = " << shippedMin << " Hz ("
           << kMaterialNames[shippedMinIndex]
           << ") - every [new] centroid must sit below it.\n";
        os << "  pairwise separation of the six new materials, |a-b| / max(a,b) "
              "(required: >= "
           << kMinSeparation << "):\n";
        for (std::size_t i = kFirstNew; i < CB::kNumMaterials; ++i) {
            for (std::size_t j = i + 1; j < CB::kNumMaterials; ++j) {
                const double larger = std::fmax(centroid[i], centroid[j]);
                const double separation =
                    (larger > 0.0) ? (std::fabs(centroid[i] - centroid[j]) / larger) : 0.0;
                os << "    " << kMaterialNames[i] << " vs " << kMaterialNames[j] << " : "
                   << separation << "\n";
            }
        }
        WARN(os.str());
    }

    // -------------------------------------------------------------------------
    // 3. THE GATES - deliberately after every measurement and the whole report
    // -------------------------------------------------------------------------
    for (std::size_t i = 0; i < CB::kNumMaterials; ++i) {
        INFO("material " << kMaterialNames[i]
                         << ": the render must be finite and the window complete");
        REQUIRE(measured[i]);
        INFO("material " << kMaterialNames[i] << ": centroid " << centroid[i] << " Hz");
        REQUIRE(centroid[i] > 0.0);
    }

    // FR-035: each new material is darker than EVERY shipped one, i.e. below the
    // minimum of the five.
    for (std::size_t i = kFirstNew; i < CB::kNumMaterials; ++i) {
        INFO("FR-035 / SC-015: " << kMaterialNames[i] << " centroid " << centroid[i]
                                 << " Hz must be below the shipped minimum " << shippedMin
                                 << " Hz (" << kMaterialNames[shippedMinIndex]
                                 << "). The lever is this material's alpha, then its b1, "
                                    "then its mode count - never this threshold.");
        REQUIRE(centroid[i] < shippedMin);
    }

    // SC-015 clause 2: six materials, not one repeated six times.
    for (std::size_t i = kFirstNew; i < CB::kNumMaterials; ++i) {
        for (std::size_t j = i + 1; j < CB::kNumMaterials; ++j) {
            const double larger = std::fmax(centroid[i], centroid[j]);
            const double separation =
                (larger > 0.0) ? (std::fabs(centroid[i] - centroid[j]) / larger) : 0.0;
            INFO("SC-015: " << kMaterialNames[i] << " (" << centroid[i] << " Hz) vs "
                            << kMaterialNames[j] << " (" << centroid[j] << " Hz) - separation "
                            << separation << ", required >= " << kMinSeparation);
            REQUIRE(separation >= kMinSeparation);
        }
    }
}
