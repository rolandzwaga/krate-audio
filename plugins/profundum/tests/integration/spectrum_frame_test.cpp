// ==============================================================================
// Profundum Phase 12 - spectrum frame integration cases (T020 seam arms; T021 connected arms)
// ==============================================================================
// specs/profundum-phase12-ui/tasks.md T020, T021; plan 4.4, 4.6.1, 5.1 - 5.4, 6.2 "Spectrum"; spec SC-010, SC-011,
// SC-012, SC-023, SC-025, SC-026, SC-030.
//
// SEAM arms: Processor::setSpectrumProducerEnabledForTest(true) runs the producer with no DataExchange connection, on
// every OS. CONNECTED arms (T021): a ConnectedPair (real processor + real controller, one HostApplication, which is
// not an IDataExchangeHandler), so the SDK IMessage fallback carries the frame: "DataExchangeQueueOpened" /
// "...Closed" arrive synchronously, the blocks only from the fallback's 1 ms SDK Timer (dataexchange.cpp:79, :172-187).
// pumpUntilReceived() runs the platform loop that Timer needs (Win32 message pump / the main CFRunLoop, the
// plugins/innexus/tests/integration/test_data_exchange_pipeline.cpp:43-62 precedent) until the controller has the
// block. Linux has no Timer without an injected run loop, so the queue never opens in this harness: queue-dependent
// asserts WARN-skip there only (kQueueMayBeUnavailable / queueAvailable, copied from
// plugins/vorago/tests/integration/ecosystem_frame_test.cpp:108-131); a closed queue fails on Windows and macOS.
//
// The SC-010 reference is independent of the producer (plan 5.2): f0 and N come from midiNoteToFrequency(note), never
// from the frame, and the level at n * f0 is a direct-sum windowed DFT (Krate::DSP::ProfundumTest::goertzelPhasorWindowed)
// over the observer's post-Character tap of the focus voice.
//
// PORTABILITY: no <cmath> NaN / Inf classifiers; Krate::DSP::detail::isFinite and bit patterns only.
// Non-finite inputs come from the fixture's makeNaN / makePosInf (bit patterns through a volatile).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "profundum_test_fixture.h"

#include "controller/controller.h"
#include "plugin_ids.h"
#include "processor/spectrum_frame.h"
#include "processor/spectrum_producer.h"
#include "ui/harmonic_bar_view.h"

#include <allocation_detector.h>
#include <bass_morph_state_helpers.h>             // dsp/tests/unit/systems (read-only): BassMorphTest::showcasePair
#include <profundum_excitation_test_helpers.h>    // dsp/tests/unit/systems (read-only): goertzelPhasorWindowed
#include <vst_event_list.h>

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/midi_utils.h>
#include <krate/dsp/systems/morph_state.h>
#include <krate/dsp/systems/profundum_engine.h>
#include <krate/dsp/systems/profundum_engine_observer.h>

#include <pluginterfaces/base/fplatform.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivstdataexchange.h>
#include <public.sdk/source/vst/hosting/hostclasses.h>

#include "vstgui/lib/crect.h"
#include "vstgui/lib/vstguibase.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <numbers>
#include <span>
#include <thread>
#include <utility>
#include <vector>

// The platform loop the SDK fallback Timer needs (test-only; base/source/timer.cpp:62-180). Last, so the Windows
// macros reach no header.
#if SMTG_OS_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif SMTG_OS_MACOS
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace ProfundumPluginTest {
namespace {

namespace P = ::Profundum;
namespace Kd = Krate::DSP;
using Frame = P::ProfundumSpectrumFrame;
using Producer = P::ProfundumSpectrumProducer;
using Kd::MorphState;

// SC-030: the frame fits the 320-byte ceiling and the handler block is the frame rounded up to 32 bytes.
static_assert(sizeof(Frame) <= 320u);
static_assert(((sizeof(Frame) + 31u) / 32u) * 32u == 288u);

constexpr std::size_t kHostBlock = 512;
constexpr std::size_t kInterval = Kd::ProfundumEngine::kControlInterval;
constexpr Steinberg::int16 kNoteE1 = 28;
constexpr Steinberg::int16 kNoteC3 = 48;
constexpr Steinberg::int16 kNoteC6 = 84;  // 1046.5 Hz: the SC-010 Nyquist fixture
constexpr double kTwoPiD = 2.0 * std::numbers::pi;
constexpr double kMinus60DbPower = 1.0e-6;  // -60 dB as a power ratio
constexpr double kLevelToleranceDb = 0.5;
constexpr double kF0Tolerance = 1.0e-4;
constexpr std::uint32_t kPrfd = 0x50524644u;  // 'PRFD', SC-023
constexpr std::uint32_t kHandlerBlockBytes = 288u;  // sizeof(Frame) rounded up to 32 (SC-023)
static_assert(P::kProfundumFrameUserContextId == kPrfd);
constexpr int kDeliveryTimeoutMs = 2000;

// -----------------------------------------------------------------------------
// Connected-arm transport (T021). Copied from plugins/vorago/tests/integration/ecosystem_frame_test.cpp:108-131.
// -----------------------------------------------------------------------------
#if SMTG_OS_LINUX
constexpr bool kQueueMayBeUnavailable = true;
#else
constexpr bool kQueueMayBeUnavailable = false;
#endif

/// Queue-open precondition. Returns true when the queue-dependent asserts may run. Linux: WARN and skip; elsewhere a
/// closed queue fails.
bool queueAvailable(bool opened, const char* step) {
    if (opened) {
        return true;
    }
    if constexpr (kQueueMayBeUnavailable) {
        WARN("DataExchange fallback queue did not open at " << step
                                                             << " (Linux: no injected timer factory); "
                                                                "queue-dependent asserts skipped");
        return false;
    } else {
        INFO("DataExchange fallback queue did not open at " << step);
        REQUIRE(opened);
        return false;
    }
}

/// Runs the platform loop until @p done() holds, or @p timeoutMs passes.
/// The fallback sends its queued blocks from its SDK Timer (dataexchange.cpp:172-187), which fires only from the
/// loop: a Win32 timer with a TimerProc runs inside DispatchMessage, the macOS timer on the main CFRunLoop
/// (base/source/timer.cpp:137, :238). Linux never reaches this: its queue does not open (queueAvailable).
template <typename Done>
[[nodiscard]] bool pumpUntil(Done done, int timeoutMs = kDeliveryTimeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!done()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
#if SMTG_OS_WINDOWS
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE) != 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
#elif SMTG_OS_MACOS
        static_cast<void>(CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.002, true));
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
    }
    return true;
}

/// Pumps until the controller has received more than @p before blocks.
[[nodiscard]] bool pumpUntilReceived(const P::Controller& ctrl, std::uint64_t before) {
    return pumpUntil([&ctrl, before] { return ctrl.framesReceivedForTest() > before; });
}

/// Connected arm: frames published by earlier process() calls that ran without a controller check (a prepare's
/// parameter block publishes the silence frame, plan 4.3.4) are still queued in the fallback until the loop runs.
/// Pumps until the newest one has arrived, so the next renderDiscard counts only its own call's frames.
void drainPendingFrames(const ProcessorFixture& fx, const P::Controller& ctrl) {
    if (fx.proc->spectrumFramesPublishedForTest() == 0u || fx.proc->spectrumSkippedBlocksForTest() != 0u) {
        return;  // nothing sent in this activation, or the newest frame may be one that was skipped
    }
    const std::uint32_t newest = fx.proc->lastSpectrumFrameForTest().sequence;
    INFO("draining to frame sequence " << newest);
    REQUIRE(pumpUntil([&ctrl, newest] { return ctrl.cachedFrameForTest().sequence == newest; }));
}

// Byte identity, member by member (floats by bit pattern): the frame has no padding (spectrum_frame.h static_asserts).
[[nodiscard]] bool sameBytes(const Frame& a, const Frame& b) noexcept {
    const auto bits = [](float x) noexcept { return std::bit_cast<std::uint32_t>(x); };
    return a.sequence == b.sequence && bits(a.sampleRate) == bits(b.sampleRate) && bits(a.f0Hz) == bits(b.f0Hz) &&
           bits(a.anchorPower) == bits(b.anchorPower) && a.windowSamples == b.windowSamples &&
           a.numPartials == b.numPartials && a.flags == b.flags && a.focusVoice == b.focusVoice &&
           a.reserved == b.reserved && a.windowEndSample == b.windowEndSample &&
           std::ranges::equal(a.partialPower, b.partialPower, {}, bits, bits);
}

// -----------------------------------------------------------------------------
// Fixtures (spec SC-010): the pitch is held static (Motion 900 Off, Drop Amount 0, no bend).
// -----------------------------------------------------------------------------
enum class Patch : std::uint8_t { ShowcaseA, ShowcaseB, Default, Fold };

[[nodiscard]] MorphState patchOf(Patch p) {
    const auto pair = Kd::BassMorphTest::showcasePair();
    MorphState s{};
    switch (p) {
        case Patch::ShowcaseA:
            s = pair.first;
            break;
        case Patch::ShowcaseB:
            s = pair.second;
            break;
        case Patch::Default:
            break;
        case Patch::Fold:  // showcase B with Fold 500 = 100 and Character Amount 601 = 1
            s = pair.second;
            s.foldAmount = 100.0f;
            s.characterAmount = 1.0f;
            break;
    }
    s.motion.source = MorphState::MotionConfig::Source::Off;  // Motion 900 Off
    s.dropAmountSt = 0.0f;                                     // Drop Amount 0
    return s;
}

/// Both endpoints through setState (the current stream with its two endpoint blobs replaced), then a
/// deactivate / activate pair so the engine starts from reset with the loaded state.
void loadPatch(ProcessorFixture& fx, const MorphState& a, const MorphState& b) {
    const StateBytes bytes = withEndpoints(writeV2Stream(*fx.proc), a, b);
    REQUIRE(loadStateBytes(*fx.proc, bytes) == Steinberg::kResultOk);
    REQUIRE(fx.proc->setActive(0u) == Steinberg::kResultOk);
    REQUIRE(fx.proc->setActive(1u) == Steinberg::kResultOk);
}

/// N = clamp(round(8 fs / f0), 64, 2^20) (plan 5.2), computed from the independent f0.
[[nodiscard]] std::uint32_t windowSamplesFor(double fs, double f0) {
    const double n = std::round(8.0 * fs / f0);
    return static_cast<std::uint32_t>(std::clamp(n, 64.0, 1048576.0));
}

[[nodiscard]] double f0Of(Steinberg::int16 note) {
    return static_cast<double>(Kd::midiNoteToFrequency(static_cast<int>(note)));
}

[[nodiscard]] double powerDb(double p) { return 10.0 * std::log10(std::max(p, 1.0e-300)); }

/// One process() block of n samples; the capture is cleared first so a long render keeps bounded memory.
/// Returns the number of frames the processor published during the call.
/// Connected arm (@p ctrl non-null, T021): after a publish, pumps until the controller has the block, then requires
/// the controller's cached frame to equal the processor's published frame byte for byte.
std::uint32_t renderDiscard(ProcessorFixture& fx, std::size_t n, Steinberg::Vst::IEventList* ev = nullptr,
                            const P::Controller* ctrl = nullptr) {
    fx.capturedL.clear();
    fx.capturedR.clear();
    fx.reserveCapture(n);
    if (ctrl != nullptr) {
        drainPendingFrames(fx, *ctrl);
    }
    const std::uint32_t before = fx.proc->spectrumFramesPublishedForTest();
    const std::uint64_t receivedBefore = ctrl != nullptr ? ctrl->framesReceivedForTest() : 0u;
    REQUIRE(fx.processBlock(n, ev) == Steinberg::kResultOk);
    const std::uint32_t published = fx.proc->spectrumFramesPublishedForTest() - before;
    if (ctrl != nullptr && published > 0u) {
        INFO("frame sequence " << fx.proc->lastSpectrumFrameForTest().sequence << ", skipped blocks "
                               << fx.proc->spectrumSkippedBlocksForTest());
        REQUIRE(pumpUntilReceived(*ctrl, receivedBefore));
        CHECK(ctrl->framesReceivedForTest() == receivedBefore + published);
        REQUIRE(sameBytes(ctrl->cachedFrameForTest(), fx.proc->lastSpectrumFrameForTest()));
    }
    return published;
}

/// The frame under test: the controller's cached frame on a connected arm, else the processor's last publish.
[[nodiscard]] const Frame& frameOf(const ProcessorFixture& fx, const P::Controller* ctrl) noexcept {
    return ctrl != nullptr ? ctrl->cachedFrameForTest() : fx.proc->lastSpectrumFrameForTest();
}

[[nodiscard]] bool frameFloatsFinite(const Frame& f) noexcept {
    using Kd::detail::isFinite;
    if (!isFinite(f.sampleRate) || !isFinite(f.f0Hz) || !isFinite(f.anchorPower)) {
        return false;
    }
    return std::ranges::all_of(f.partialPower, [](float p) { return isFinite(p); });
}

/// Bit-pattern classification: exactly +-0, or a normal (exponent neither all-zero nor all-one).
[[nodiscard]] bool zeroOrNormalBits(float x) noexcept {
    const auto bits = std::bit_cast<std::uint32_t>(x);
    const std::uint32_t exponent = bits & 0x7F800000u;
    return (bits & 0x7FFFFFFFu) == 0u || (exponent != 0u && exponent != 0x7F800000u);
}

[[nodiscard]] bool isSilentFrame(const Frame& f) noexcept { return (f.flags & P::kFrameFlagSilent) != 0; }

// -----------------------------------------------------------------------------
// The observer (plan 6.2): the focus voice's charL / charR and anchorL / anchorR, indexed by the engine sample
// clock (ProfundumVoiceTaps::sample), plus the end of the voice's last rendered chunk.
// -----------------------------------------------------------------------------
struct TapRecorder final : public Kd::ProfundumEngineObserver {
    TapRecorder(std::size_t capacity, std::uint8_t voiceIndex)
        : voice(voiceIndex), cL(capacity, 0.0f), cR(capacity, 0.0f), aL(capacity, 0.0f), aR(capacity, 0.0f) {}

    void onVoiceChunk(const Kd::ProfundumVoiceTaps& t) noexcept override {
        if (t.voice != voice) {
            return;
        }
        ++chunks;
        lastChunkEnd = t.sample + t.n;
        const std::size_t n = std::min({t.n, t.charL.size(), t.charR.size(), t.anchorL.size(), t.anchorR.size()});
        for (std::size_t i = 0; i < n; ++i) {
            const std::uint64_t s = t.sample + i;
            if (s >= cL.size()) {
                break;
            }
            const auto k = static_cast<std::size_t>(s);
            cL[k] = t.charL[i];
            cR[k] = t.charR[i];
            aL[k] = t.anchorL[i];
            aR[k] = t.anchorR[i];
        }
    }

    std::uint8_t voice;
    std::vector<float> cL, cR, aL, aR;
    std::uint64_t lastChunkEnd = 0;
    std::uint64_t chunks = 0;
};

/// Plan 5.2 steps 1 - 6: the frame against the independent reference.
void checkFrameAgainstReference(const Frame& f, const TapRecorder& rec, double fs, Steinberg::int16 note) {
    const double f0Ref = f0Of(note);
    const std::uint32_t nRef = windowSamplesFor(fs, f0Ref);
    INFO("note " << note << ", fs " << fs << ", f0_ref " << f0Ref << ", N_ref " << nRef);

    REQUIRE_FALSE(isSilentFrame(f));
    // Step 2: rate, window length and f0 first, so a mislatched f0 or N fails before any level is compared.
    CHECK(f.sampleRate == static_cast<float>(fs));
    REQUIRE(f.windowSamples == nRef);
    INFO("frame f0 " << f.f0Hz);
    REQUIRE(std::abs(static_cast<double>(f.f0Hz) / f0Ref - 1.0) <= kF0Tolerance);

    // Count and Nyquist (plan 5.3).
    const double nyquist = 0.5 * fs;
    const auto floorCount = static_cast<std::size_t>(std::floor(nyquist / f0Ref));
    INFO("numPartials " << static_cast<int>(f.numPartials) << ", floor(Nyquist / f0) " << floorCount);
    CHECK(static_cast<std::size_t>(f.numPartials) <= floorCount);
    CHECK(static_cast<std::size_t>(f.numPartials) <= P::kSpectrumMaxPartials);
    CHECK(static_cast<double>(f.numPartials) * static_cast<double>(f.f0Hz) < nyquist);

    // Step 3: the observer's taps over [windowEndSample - N_ref, windowEndSample).
    REQUIRE(f.windowEndSample >= nRef);
    REQUIRE(f.windowEndSample <= rec.cL.size());
    const auto start = static_cast<std::size_t>(f.windowEndSample - nRef);
    std::vector<double> hann(nRef);
    for (std::size_t k = 0; k < nRef; ++k) {
        hann[k] = 0.5 * (1.0 - std::cos(kTwoPiD * static_cast<double>(k) / static_cast<double>(nRef)));
    }

    // Steps 4 - 5: P_ref,n = 8 (|G_L|^2 + |G_R|^2) / N_ref^2 for every n <= 64 below Nyquist.
    const double nSq = static_cast<double>(nRef) * static_cast<double>(nRef);
    std::vector<double> ref;
    for (std::size_t n = 1; n <= P::kSpectrumMaxPartials; ++n) {
        const double hz = static_cast<double>(n) * f0Ref;
        if (hz >= nyquist) {
            break;
        }
        const std::complex<double> gL =
            Kd::ProfundumTest::goertzelPhasorWindowed(rec.cL.data() + start, hann, nRef, fs, hz);
        const std::complex<double> gR =
            Kd::ProfundumTest::goertzelPhasorWindowed(rec.cR.data() + start, hann, nRef, fs, hz);
        ref.push_back(8.0 * (std::norm(gL) + std::norm(gR)) / nSq);
    }
    REQUIRE_FALSE(ref.empty());
    const double refMax = *std::max_element(ref.begin(), ref.end());
    REQUIRE(refMax > 0.0);

    const auto count = static_cast<std::size_t>(f.numPartials);
    double frameMax = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        frameMax = std::max(frameMax, static_cast<double>(f.partialPower[i]));
    }
    REQUIRE(frameMax > 0.0);

    // Step 6: every frame partial >= -60 dB re its strongest agrees within +-0.5 dB.
    for (std::size_t i = 0; i < count; ++i) {
        const auto p = static_cast<double>(f.partialPower[i]);
        if (p < frameMax * kMinus60DbPower) {
            continue;
        }
        INFO("partial n = " << (i + 1) << ": frame " << powerDb(p) << " dB");
        REQUIRE(i < ref.size());
        INFO("reference " << powerDb(ref[i]) << " dB");
        CHECK(std::abs(powerDb(p) - powerDb(ref[i])) <= kLevelToleranceDb);
    }
    // Every reference partial >= -60 dB re the strongest (n <= 64) is reported.
    for (std::size_t i = 0; i < ref.size(); ++i) {
        if (ref[i] >= refMax * kMinus60DbPower) {
            INFO("reference partial n = " << (i + 1) << " at " << powerDb(ref[i] / refMax) << " dB re strongest");
            CHECK(i < count);
        }
    }

    // Anchor: (sum aL^2 + sum aR^2) / N_ref over the same window, within +-0.5 dB.
    double anchorSq = 0.0;
    for (std::size_t k = start; k < start + nRef; ++k) {
        anchorSq += static_cast<double>(rec.aL[k]) * static_cast<double>(rec.aL[k]) +
                    static_cast<double>(rec.aR[k]) * static_cast<double>(rec.aR[k]);
    }
    const double anchorRef = anchorSq / static_cast<double>(nRef);
    INFO("anchor frame " << powerDb(static_cast<double>(f.anchorPower)) << " dB, reference " << powerDb(anchorRef)
                         << " dB");
    if (anchorRef > 1.0e-20) {
        CHECK(std::abs(powerDb(static_cast<double>(f.anchorPower)) - powerDb(anchorRef)) <= kLevelToleranceDb);
    } else {
        CHECK(static_cast<double>(f.anchorPower) <= 1.0e-20);
    }
}

/// One SC-010 fixture: MONO, 0.5 s settle, the first frame after settle against the reference, then note-off and
/// the first publish after the observer's last voice chunk. @p ctrl null = the seam arm on @p fx; non-null = the
/// connected arm (no seam), every check made on the controller's cached frame.
void runMatchOn(ProcessorFixture& fx, const P::Controller* ctrl, Patch patch, Steinberg::int16 note, double fs) {
    INFO("patch " << static_cast<int>(patch) << ", note " << note << ", fs " << fs << ", "
                  << (ctrl != nullptr ? "connected" : "seam"));
    REQUIRE(fx.prepare(fs, static_cast<Steinberg::int32>(kHostBlock)) == Steinberg::kResultOk);
    const MorphState s = patchOf(patch);
    loadPatch(fx, s, s);
    REQUIRE(fx.proc->globalParamsForTest().voiceMode.load() == 0);  // MONO
    if (ctrl == nullptr) {
        fx.proc->setSpectrumProducerEnabledForTest(true);
    } else if (!queueAvailable(ctrl->queueOpenCountForTest() > 0u, "SC-010 connected arm")) {
        return;
    }

    const auto settle = static_cast<std::size_t>(std::llround(0.5 * fs));
    const std::uint32_t nRef = windowSamplesFor(fs, f0Of(note));
    const std::size_t capacity = settle + 3u * static_cast<std::size_t>(nRef) + 4u * kHostBlock;
    TapRecorder rec(capacity, 0u);  // MONO: the focus is voice 0 (plan 4.3.2)
    fx.proc->setEngineObserverForTest(&rec);

    Krate::Test::EventList ev;
    ev.addNoteOn(note, 0.8f, 0);
    std::size_t pos = 0;
    static_cast<void>(renderDiscard(fx, kHostBlock, &ev, ctrl));
    if (ctrl != nullptr) {
        REQUIRE(fx.proc->spectrumQueueOpenForTest());  // the first process() probed an open queue
    }
    pos += kHostBlock;
    while (pos < settle) {
        static_cast<void>(renderDiscard(fx, kHostBlock, nullptr, ctrl));
        pos += kHostBlock;
    }

    // The first frame published after the settle point.
    bool found = false;
    Frame frame{};
    while (!found && pos + kHostBlock <= capacity) {
        const std::uint32_t published = renderDiscard(fx, kHostBlock, nullptr, ctrl);
        pos += kHostBlock;
        if (published > 0u) {
            frame = frameOf(fx, ctrl);
            found = frame.windowEndSample >= settle;
        }
    }
    REQUIRE(found);
    checkFrameAgainstReference(frame, rec, fs, note);

    // Note-off: "the frame reports silence within one publish after the voice ends".
    struct BlockLog {
        bool published;
        Frame frame;
        std::uint64_t chunksAfter;
    };
    std::vector<BlockLog> log;
    const auto maxBlocks = static_cast<std::size_t>(std::ceil(20.0 * fs / static_cast<double>(kHostBlock)));
    log.reserve(maxBlocks);
    ev.clear();
    ev.addNoteOff(note, 0);
    std::size_t quietBlocks = 0;
    for (std::size_t b = 0; b < maxBlocks && quietBlocks < 8u; ++b) {
        const std::uint64_t chunksBefore = rec.chunks;
        const std::uint32_t published = renderDiscard(fx, kHostBlock, b == 0u ? &ev : nullptr, ctrl);
        log.push_back({.published = published > 0u, .frame = frameOf(fx, ctrl), .chunksAfter = rec.chunks});
        quietBlocks = (rec.chunks == chunksBefore) ? quietBlocks + 1u : 0u;
    }
    fx.proc->setEngineObserverForTest(nullptr);
    REQUIRE(quietBlocks >= 8u);  // the voice ended inside the 20 s budget

    // The block that rendered the last chunk, and the first publish whose window ends after that chunk.
    std::size_t lastChunkBlock = 0;
    for (std::size_t b = 0; b < log.size(); ++b) {
        if (log[b].chunksAfter == rec.chunks) {
            lastChunkBlock = b;
            break;
        }
    }
    std::size_t firstAfter = log.size();
    for (std::size_t b = 0; b < log.size(); ++b) {
        if (log[b].published && log[b].frame.windowEndSample > rec.lastChunkEnd) {
            firstAfter = b;
            break;
        }
    }
    INFO("last voice chunk ends at " << rec.lastChunkEnd << " (block " << lastChunkBlock << ")");
    REQUIRE(firstAfter < log.size());
    CHECK(isSilentFrame(log[firstAfter].frame));
    CHECK(static_cast<int>(log[firstAfter].frame.numPartials) == 0);
    CHECK(firstAfter <= lastChunkBlock + 1u);
    if (ctrl != nullptr) {
        CHECK(fx.proc->spectrumSkippedBlocksForTest() == 0u);  // every publish was delivered
    }
}

/// The seam arm of one SC-010 fixture.
void runMatchFixture(Patch patch, Steinberg::int16 note, double fs) {
    ProcessorFixture fx;
    runMatchOn(fx, nullptr, patch, note, fs);
}

/// The connected arm of one SC-010 fixture.
void runMatchFixtureConnected(Patch patch, Steinberg::int16 note, double fs) {
    ConnectedPair cp;
    runMatchOn(cp.fx, cp.cf.controller.get(), patch, note, fs);
}

/// The voice that sounds @p note, or -1.
[[nodiscard]] int voiceOf(const Kd::ProfundumEngine& engine, Steinberg::int16 note) {
    for (std::size_t v = 0; v < Kd::ProfundumEngine::kMaxVoices; ++v) {
        if (engine.voiceMidiNote(v) == static_cast<int>(note)) {
            return static_cast<int>(v);
        }
    }
    return -1;
}

/// SC-025 step check: after the step's block, render two windows of @p note, then the producer's focus, the
/// frame's focus and its f0 all name the voice sounding @p note. Returns that voice. @p ctrl non-null: the
/// connected arm, the frame read is the controller's cached frame.
int checkFocusAfterStep(ProcessorFixture& fx, Krate::Test::EventList& step, Steinberg::int16 note,
                        const P::Controller* ctrl = nullptr) {
    INFO("focus note " << note);
    const std::uint64_t stepStart = fx.engine()->samplePosition();
    static_cast<void>(renderDiscard(fx, kHostBlock, &step, ctrl));
    const std::uint32_t nRef = windowSamplesFor(48000.0, f0Of(note));
    const std::size_t span = 2u * static_cast<std::size_t>(nRef) + 2u * kHostBlock;
    for (std::size_t done = 0; done < span; done += kHostBlock) {
        static_cast<void>(renderDiscard(fx, kHostBlock, nullptr, ctrl));
    }
    const int expected = voiceOf(*fx.engine(), note);
    REQUIRE(expected >= 0);
    CHECK(fx.proc->spectrumProducerForTest().focusVoice() == expected);
    const Frame& f = frameOf(fx, ctrl);
    REQUIRE_FALSE(isSilentFrame(f));
    CHECK(std::cmp_equal(f.focusVoice, expected));
    CHECK((f.flags & P::kFrameFlagPoly) != 0);
    CHECK(f.windowEndSample >= stepStart + f.windowSamples);
    INFO("frame f0 " << f.f0Hz << ", note f0 " << f0Of(note));
    CHECK(std::abs(static_cast<double>(f.f0Hz) / f0Of(note) - 1.0) <= kF0Tolerance);
    return expected;
}

/// POLY at @p polyphony, the default patch with a static pitch; the seam on unless @p seam is false (connected arm).
void preparePoly(ProcessorFixture& fx, int polyphony, bool seam = true) {
    REQUIRE(fx.prepare(48000.0, static_cast<Steinberg::int32>(kHostBlock)) == Steinberg::kResultOk);
    const MorphState s = patchOf(Patch::Default);
    loadPatch(fx, s, s);
    REQUIRE(fx.setParamsInBlock({{P::kVoiceModeId, 1.0},
                                 {P::kPolyphonyId, static_cast<double>(polyphony - 1) / 7.0}},
                                kHostBlock) == Steinberg::kResultOk);
    REQUIRE(fx.proc->globalParamsForTest().polyphony.load() == polyphony);
    fx.proc->setSpectrumProducerEnabledForTest(seam);
}

/// SC-025 steal arm: polyphony 2, two held notes, a third note steals. Returns the stealing note's voice.
int runStealArm() {
    constexpr Steinberg::int16 kA = 40;
    constexpr Steinberg::int16 kB = 45;
    constexpr Steinberg::int16 kC = 48;
    ProcessorFixture fx;
    preparePoly(fx, 2);
    Krate::Test::EventList ev;
    ev.addNoteOn(kA, 0.8f, 0);
    static_cast<void>(checkFocusAfterStep(fx, ev, kA));
    ev.clear();
    ev.addNoteOn(kB, 0.8f, 0);
    static_cast<void>(checkFocusAfterStep(fx, ev, kB));
    ev.clear();
    ev.addNoteOn(kC, 0.8f, 0);
    const int stealer = checkFocusAfterStep(fx, ev, kC);
    CHECK(voiceOf(*fx.engine(), kC) == stealer);
    return stealer;
}

/// SC-011 script: note on / off, a 105 sweep 0 -> 1 -> 0 over 1 s, then POLY and the chord 28 / 33 / 40.
[[nodiscard]] std::vector<ProcessorFixture::ScriptedEvent> doesNotChangeAudioScript() {
    using Ev = ProcessorFixture::ScriptedEvent;
    using Kind = Ev::Kind;
    std::vector<Ev> script;
    script.push_back({.sample = 0, .kind = Kind::NoteOn, .pitch = kNoteE1, .velocity = 0.8f});
    for (Steinberg::int64 s = 0; s < 48000; s += 512) {
        const double t = static_cast<double>(s) / 48000.0;  // [0, 1)
        const double v = t < 0.5 ? 2.0 * t : 2.0 * (1.0 - t);
        script.push_back({.sample = s, .kind = Kind::Param, .id = P::kMorphPositionId, .value = v});
    }
    script.push_back({.sample = 48000, .kind = Kind::Param, .id = P::kMorphPositionId, .value = 0.0});
    script.push_back({.sample = 30000, .kind = Kind::NoteOff, .pitch = kNoteE1});
    script.push_back({.sample = 48000, .kind = Kind::Param, .id = P::kVoiceModeId, .value = 1.0});
    for (const Steinberg::int16 n : {Steinberg::int16{28}, Steinberg::int16{33}, Steinberg::int16{40}}) {
        script.push_back({.sample = 48512, .kind = Kind::NoteOn, .pitch = n, .velocity = 0.8f});
        script.push_back({.sample = 84000, .kind = Kind::NoteOff, .pitch = n});
    }
    return script;
}

/// One block of the SC-012 run, built before the scope.
struct AllocBlockInput {
    MultiParamChanges changes;
    Krate::Test::EventList events;
};

/// SC-012 warm-up: a few rendered blocks with a note, so first-render work is not attributed to the scope.
void warmForAllocationScope(ProcessorFixture& fx, std::size_t block) {
    Krate::Test::EventList warm;
    warm.addNoteOn(kNoteE1, 0.8f, 0);
    static_cast<void>(renderDiscard(fx, block, &warm));
    for (int i = 0; i < 3; ++i) {
        static_cast<void>(renderDiscard(fx, block));
    }
    warm.clear();
    warm.addNoteOff(kNoteE1, 0);
    static_cast<void>(renderDiscard(fx, block, &warm));
}

/// The cycle's inputs, built before the scope: notes on / off, a voice-mode switch, and a 105 / 101 / 500 change in
/// every block.
void fillAllocationInputs(std::vector<AllocBlockInput>& inputs, std::size_t block) {
    const std::size_t cycle = inputs.size();
    for (std::size_t b = 0; b < cycle; ++b) {
        AllocBlockInput& in = inputs[b];
        in.changes.reserve(4);
        in.changes.clear();
        in.events.clear();
        if (b == 0u) {
            in.events.addNoteOn(kNoteE1, 0.8f, 10);
            in.events.addNoteOn(33, 0.7f, 120);
        } else if (b == 5u) {
            in.events.addNoteOff(33, 40);
        } else if (b == 8u) {
            in.events.addNoteOn(40, 0.9f, 0);
        } else if (b == 13u) {
            in.events.addNoteOff(kNoteE1, 7);
            in.events.addNoteOff(40, 200);
        }
        in.changes.addQueue(P::kMorphPositionId)
            .addTestPoint(static_cast<Steinberg::int32>((b * 17u) % block),
                          static_cast<double>(b) / static_cast<double>(cycle - 1u));
        in.changes.addQueue(P::kMacroDarkBrightId).addTestPoint(0, (b % 2u == 0u) ? 0.3 : 0.7);
        in.changes.addQueue(P::kFoldAmountId).addTestPoint(64, static_cast<double>(b % 4u) / 3.0);
        if (b == 3u || b == 11u) {
            in.changes.addQueue(P::kVoiceModeId).addTestPoint(0, b == 3u ? 1.0 : 0.0);
        }
    }
}

struct AllocationRun {
    int failures = 0;
    std::size_t allocations = 0;
};

/// @p calls process() calls of @p block samples under one AllocationScope, cycling through @p inputs. The
/// ProcessData is built before the scope; nothing inside allocates on the test's side.
AllocationRun runUnderAllocationScope(ProcessorFixture& fx, std::vector<AllocBlockInput>& inputs, std::size_t block,
                                      std::size_t calls) {
    std::array<float*, 2> channels{fx.audioL(), fx.audioR()};
    Steinberg::Vst::AudioBusBuffers bus{};
    bus.numChannels = 2;
    bus.silenceFlags = 0;
    bus.channelBuffers32 = channels.data();
    Steinberg::Vst::ProcessData data{};
    data.processMode = Steinberg::Vst::kRealtime;
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.numSamples = static_cast<Steinberg::int32>(block);
    data.numInputs = 0;
    data.inputs = nullptr;
    data.numOutputs = 1;
    data.outputs = &bus;
    data.inputParameterChanges = nullptr;
    data.outputParameterChanges = nullptr;
    data.inputEvents = nullptr;
    data.outputEvents = nullptr;
    data.processContext = nullptr;
    P::Processor& proc = *fx.proc;

    AllocationRun run;
    {
        TestHelpers::AllocationScope scope;
        for (std::size_t i = 0; i < calls; ++i) {
            AllocBlockInput& in = inputs[i % inputs.size()];
            data.inputParameterChanges = &in.changes;
            data.inputEvents = &in.events;
            if (proc.process(data) != Steinberg::kResultOk) {
                ++run.failures;
            }
        }
        run.allocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }
    return run;
}

/// SC-023 / FR-071 closed-queue readings: 200 process() calls with a held note leave the producer idle.
void checkClosedQueueIsNoOp(ProcessorFixture& fx) {
    Krate::Test::EventList ev;
    ev.addNoteOn(kNoteC3, 0.8f, 0);
    float peak = 0.0f;
    for (int c = 0; c < 200; ++c) {
        static_cast<void>(renderDiscard(fx, kHostBlock, c == 0 ? &ev : nullptr));
        peak = std::max(peak, peakOf(fx.capturedL));
    }
    CHECK(peak > 0.0f);  // non-vacuity: the held note sounded
    CHECK_FALSE(fx.proc->spectrumQueueOpenForTest());
    CHECK(fx.proc->spectrumProducerForTest().windowsCompleted() == 0u);
    CHECK(fx.proc->spectrumFramesPublishedForTest() == 0u);
    CHECK(fx.proc->spectrumSkippedBlocksForTest() == 0u);
}

/// A host context that is NOT an IHostApplication: the SDK fallback's openQueue fails (dataexchange.cpp:79-80).
class NotAHostContext final : public Steinberg::FUnknown {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID /*iid*/, void** obj) override {
        if (obj != nullptr) {
            *obj = nullptr;
        }
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
};

/// The largest drawn height of @p view, bars and Anchor lane.
[[nodiscard]] float peakHeight(const P::UI::HarmonicBarView& view) noexcept {
    float peak = view.anchorHeightForTest();
    for (const float h : view.barHeightsForTest()) {
        peak = std::max(peak, h);
    }
    return peak;
}

}  // namespace

// =============================================================================
// SC-010 (FR-070 - FR-075): the frame matches the signal.
// =============================================================================
TEST_CASE("Profundum_SpectrumFrameMatchesSignal", "[profundum][ui][spectrum]") {
    SECTION("Seam") {
        for (const Patch patch : {Patch::ShowcaseA, Patch::ShowcaseB, Patch::Default, Patch::Fold}) {
            for (const Steinberg::int16 note : {kNoteE1, kNoteC3}) {
                runMatchFixture(patch, note, 48000.0);
            }
        }
        // The Nyquist fixture: the default patch at C6, 44.1 kHz, where 64 core partials would cross Nyquist.
        runMatchFixture(Patch::Default, kNoteC6, 44100.0);
    }
    SECTION("Connected") {
        // T021: the same fixtures through the DataExchange fallback; every publish is compared byte for byte with
        // the controller's cached frame, and every T020 check runs on the cached frame.
        for (const Patch patch : {Patch::ShowcaseA, Patch::ShowcaseB, Patch::Default, Patch::Fold}) {
            for (const Steinberg::int16 note : {kNoteE1, kNoteC3}) {
                runMatchFixtureConnected(patch, note, 48000.0);
            }
        }
        runMatchFixtureConnected(Patch::Default, kNoteC6, 44100.0);
    }
}

// =============================================================================
// SC-011 (FR-072): the producer leaves the audio bit-identical.
// =============================================================================
TEST_CASE("Profundum_SpectrumFrameDoesNotChangeAudio", "[profundum][ui][spectrum]") {
    SECTION("Seam") {
        const std::vector<ProcessorFixture::ScriptedEvent> script = doesNotChangeAudioScript();
        constexpr std::size_t kTotal = 96000;  // 2 s at 48 kHz

        const auto renderArm = [&](bool seamOn, std::vector<float>& outL, std::vector<float>& outR,
                                   std::uint32_t& frames) {
            ProcessorFixture fx;
            REQUIRE(fx.prepare(48000.0, static_cast<Steinberg::int32>(kHostBlock)) == Steinberg::kResultOk);
            const auto pair = Kd::BassMorphTest::showcasePair();  // distinct endpoints, so the sweep is heard
            loadPatch(fx, pair.first, pair.second);
            fx.proc->setSpectrumProducerEnabledForTest(seamOn);
            fx.renderScript(script, kTotal, kHostBlock);
            outL = fx.capturedL;
            outR = fx.capturedR;
            frames = fx.proc->spectrumFramesPublishedForTest();
        };

        std::vector<float> onL;
        std::vector<float> onR;
        std::vector<float> offL;
        std::vector<float> offR;
        std::uint32_t framesOn = 0;
        std::uint32_t framesOff = 0;
        renderArm(true, onL, onR, framesOn);
        renderArm(false, offL, offR, framesOff);

        REQUIRE(onL.size() == kTotal);
        REQUIRE(offL.size() == kTotal);
        // Non-vacuity: the seam arm ran the producer, the other did not, and the render is not silent.
        CHECK(framesOn > 0u);
        CHECK(framesOff == 0u);
        CHECK(peakOf(offL) > 0.0f);
        CHECK(peakOf(offR) > 0.0f);

        INFO("max |dL| " << maxAbsDiff(onL, offL) << ", max |dR| " << maxAbsDiff(onR, offR));
        CHECK(mismatches(onL, offL) == 0u);
        CHECK(mismatches(onR, offR) == 0u);
        CHECK(maxAbsDiff(onL, offL) == 0.0f);
        CHECK(maxAbsDiff(onR, offR) == 0.0f);
    }
    SECTION("Connected") {
        // T021: queue asserted open vs no connection (same host context, no seam on either side).
        const std::vector<ProcessorFixture::ScriptedEvent> script = doesNotChangeAudioScript();
        constexpr std::size_t kTotal = 96000;  // 2 s at 48 kHz
        const auto pair = Kd::BassMorphTest::showcasePair();

        std::vector<float> conL;
        std::vector<float> conR;
        std::uint32_t framesCon = 0;
        bool opened = false;
        bool openAfter = false;
        {
            ConnectedPair cp;
            REQUIRE(cp.fx.prepare(48000.0, static_cast<Steinberg::int32>(kHostBlock)) == Steinberg::kResultOk);
            loadPatch(cp.fx, pair.first, pair.second);
            opened = cp.cf.controller->queueOpenCountForTest() > 0u;
            if (opened) {
                cp.fx.renderScript(script, kTotal, kHostBlock);
                conL = cp.fx.capturedL;
                conR = cp.fx.capturedR;
                framesCon = cp.fx.proc->spectrumFramesPublishedForTest();
                openAfter = cp.fx.proc->spectrumQueueOpenForTest();
            }
        }
        if (queueAvailable(opened, "SC-011 connected arm")) {
            Steinberg::Vst::HostApplication host;
            std::vector<float> offL;
            std::vector<float> offR;
            std::uint32_t framesOff = 0;
            {
                ProcessorFixture fx{&host};  // the same kind of host context, never connected
                REQUIRE(fx.prepare(48000.0, static_cast<Steinberg::int32>(kHostBlock)) == Steinberg::kResultOk);
                loadPatch(fx, pair.first, pair.second);
                fx.renderScript(script, kTotal, kHostBlock);
                offL = fx.capturedL;
                offR = fx.capturedR;
                framesOff = fx.proc->spectrumFramesPublishedForTest();
                CHECK_FALSE(fx.proc->spectrumQueueOpenForTest());
            }

            REQUIRE(conL.size() == kTotal);
            REQUIRE(offL.size() == kTotal);
            // Non-vacuity: the connected arm ran the producer against an open queue, the other did not.
            CHECK(openAfter);
            CHECK(framesCon > 0u);
            CHECK(framesOff == 0u);
            CHECK(peakOf(offL) > 0.0f);
            CHECK(peakOf(offR) > 0.0f);

            INFO("max |dL| " << maxAbsDiff(conL, offL) << ", max |dR| " << maxAbsDiff(conR, offR));
            CHECK(mismatches(conL, offL) == 0u);
            CHECK(mismatches(conR, offR) == 0u);
            CHECK(maxAbsDiff(conL, offL) == 0.0f);
            CHECK(maxAbsDiff(conR, offR) == 0.0f);
        }
    }
}

// =============================================================================
// SC-012 (FR-071): the producer path allocates nothing.
// =============================================================================
TEST_CASE("Profundum_SpectrumFrameAllocationFree", "[profundum][ui][rt]") {
    SECTION("Seam") {
        constexpr std::size_t kAllocBlock = 256;
        constexpr std::size_t kCalls = 2000;
        constexpr std::size_t kCycle = 16;

        ProcessorFixture fx;
        REQUIRE(fx.prepare(48000.0, static_cast<Steinberg::int32>(kAllocBlock)) == Steinberg::kResultOk);
        const auto pair = Kd::BassMorphTest::showcasePair();
        loadPatch(fx, pair.first, pair.second);
        fx.proc->setSpectrumProducerEnabledForTest(true);

        warmForAllocationScope(fx, kAllocBlock);
        std::vector<AllocBlockInput> inputs(kCycle);
        fillAllocationInputs(inputs, kAllocBlock);

        P::Processor& proc = *fx.proc;
        const std::uint32_t framesBefore = proc.spectrumFramesPublishedForTest();
        const AllocationRun run = runUnderAllocationScope(fx, inputs, kAllocBlock, kCalls);

        INFO("allocations over " << kCalls << " process() calls with the producer on: " << run.allocations);
        CHECK(run.failures == 0);
        // Non-vacuity: the producer published frames inside the scope.
        CHECK(proc.spectrumFramesPublishedForTest() > framesBefore);
        CHECK(proc.spectrumProducerForTest().windowsCompleted() > 0u);
        CHECK(run.allocations == 0u);
    }
    SECTION("Connected") {
        constexpr std::size_t kAllocBlock = 256;
        constexpr std::size_t kCalls = 2000;
        constexpr std::size_t kCycle = 16;

        ConnectedPair cp;
        P::Controller& ctrl = *cp.cf.controller;
        REQUIRE(cp.fx.prepare(48000.0, static_cast<Steinberg::int32>(kAllocBlock)) == Steinberg::kResultOk);
        const auto pair = Kd::BassMorphTest::showcasePair();
        loadPatch(cp.fx, pair.first, pair.second);
        warmForAllocationScope(cp.fx, kAllocBlock);
        std::vector<AllocBlockInput> inputs(kCycle);
        fillAllocationInputs(inputs, kAllocBlock);

        // Re-activate so the scope's FIRST process() is the queue probe (plan 4.4): the probe is measured too. The
        // queue's own allocations happen here, in setActive(true), on the host thread.
        const std::uint32_t opensBefore = ctrl.queueOpenCountForTest();
        REQUIRE(cp.fx.proc->setActive(0u) == Steinberg::kResultOk);
        REQUIRE(cp.fx.proc->setActive(1u) == Steinberg::kResultOk);
        if (queueAvailable(ctrl.queueOpenCountForTest() == opensBefore + 1u, "SC-012 connected arm")) {
            REQUIRE_FALSE(cp.fx.proc->spectrumQueueOpenForTest());  // not probed yet
            const AllocationRun run = runUnderAllocationScope(cp.fx, inputs, kAllocBlock, kCalls);

            INFO("allocations over " << kCalls << " connected process() calls: " << run.allocations);
            CHECK(run.failures == 0);
            // Non-vacuity: the probe ran inside the scope and found the queue open, and frames were published.
            CHECK(cp.fx.proc->spectrumQueueOpenForTest());
            CHECK(cp.fx.proc->spectrumFramesPublishedForTest() > 0u);
            CHECK(cp.fx.proc->spectrumProducerForTest().windowsCompleted() > 0u);
            CHECK(run.allocations == 0u);
        }
    }
}

// =============================================================================
// SC-025 (FR-074): the focus voice.
// =============================================================================
TEST_CASE("Profundum_SpectrumFrameFocusVoice", "[profundum][ui][spectrum]") {
    SECTION("Seam") {
        constexpr Steinberg::int16 kA = 40;
        constexpr Steinberg::int16 kB = 45;
        ProcessorFixture fx;
        preparePoly(fx, 4);
        Krate::Test::EventList ev;

        // 1. One note: the focus is its voice.
        ev.addNoteOn(kA, 0.8f, 0);
        const int voiceA = checkFocusAfterStep(fx, ev, kA);
        // 2. A second note: the focus moves to the later one.
        ev.clear();
        ev.addNoteOn(kB, 0.8f, 0);
        const int voiceB = checkFocusAfterStep(fx, ev, kB);
        CHECK(voiceB != voiceA);
        // 3. Release of the later note: the focus returns to the earlier one while it sounds.
        ev.clear();
        ev.addNoteOff(kB, 0);
        CHECK(checkFocusAfterStep(fx, ev, kA) == voiceA);

        // 4. A steal (polyphony 2, a third note): the stealing note's voice, identical across two runs.
        const int stealRun1 = runStealArm();
        const int stealRun2 = runStealArm();
        CHECK(stealRun1 == stealRun2);

        // 5. All released: the focus is retained through the tail, then the frame reports silence.
        ev.clear();
        ev.addNoteOff(kA, 0);
        static_cast<void>(renderDiscard(fx, kHostBlock, &ev));
        CHECK(fx.proc->spectrumProducerForTest().focusVoice() == voiceA);
        const auto maxBlocks = static_cast<std::size_t>(std::ceil(20.0 * 48000.0 / static_cast<double>(kHostBlock)));
        bool silent = false;
        std::size_t tailFrames = 0;
        for (std::size_t b = 0; b < maxBlocks && !silent; ++b) {
            if (renderDiscard(fx, kHostBlock) == 0u) {
                continue;
            }
            const Frame& f = fx.proc->lastSpectrumFrameForTest();
            if (isSilentFrame(f)) {
                silent = true;
            } else {
                ++tailFrames;
                CHECK(std::cmp_equal(f.focusVoice, voiceA));
                CHECK(std::abs(static_cast<double>(f.f0Hz) / f0Of(kA) - 1.0) <= kF0Tolerance);
            }
        }
        INFO("tail frames before silence: " << tailFrames);
        REQUIRE(silent);
        CHECK(fx.proc->spectrumProducerForTest().focusVoice() == -1);
        CHECK_FALSE(fx.engine()->voiceRenderedThisInterval(static_cast<std::size_t>(voiceA)));
    }
    SECTION("Connected") {
        // T021: one note, then a second; the controller's cached frame follows the focus.
        constexpr Steinberg::int16 kA = 40;
        constexpr Steinberg::int16 kB = 45;
        ConnectedPair cp;
        const P::Controller& ctrl = *cp.cf.controller;
        preparePoly(cp.fx, 4, false);
        if (queueAvailable(ctrl.queueOpenCountForTest() > 0u, "SC-025 connected arm")) {
            Krate::Test::EventList ev;
            ev.addNoteOn(kA, 0.8f, 0);
            const int voiceA = checkFocusAfterStep(cp.fx, ev, kA, &ctrl);
            ev.clear();
            ev.addNoteOn(kB, 0.8f, 0);
            const int voiceB = checkFocusAfterStep(cp.fx, ev, kB, &ctrl);
            CHECK(voiceB != voiceA);
            CHECK(std::cmp_equal(ctrl.cachedFrameForTest().focusVoice, voiceB));
            CHECK(ctrl.framesReceivedForTest() > 0u);
        }
    }
}

// =============================================================================
// SC-026 (FR-073) steps 1 - 4 (step 3, the view decay over the controller's cached frame, is T021), plus the
// FR-091 closed-editor cache (T021).
// =============================================================================
TEST_CASE("Profundum_SpectrumFrameSanitise", "[profundum][ui][spectrum]") {
    SECTION("NonFiniteInputNeverReachesFrame") {
        ProcessorFixture fx;
        REQUIRE(fx.prepare(48000.0, static_cast<Steinberg::int32>(kHostBlock)) == Steinberg::kResultOk);
        Producer& prod = fx.proc->mutableSpectrumProducerForTest();
        constexpr float kF0 = 440.0f;
        const std::uint32_t n = windowSamplesFor(48000.0, static_cast<double>(kF0));
        const std::uint64_t nanAt = 100;                     // inside window 1
        const std::uint64_t infAt = static_cast<std::uint64_t>(n) + 200u;  // inside window 2
        std::array<float, kInterval> cL{};
        std::array<float, kInterval> cR{};
        std::array<float, kInterval> aL{};
        std::array<float, kInterval> aR{};
        std::vector<Frame> frames;
        std::uint64_t pos = 0;
        while (pos < 7u * static_cast<std::uint64_t>(n)) {
            for (std::size_t i = 0; i < kInterval; ++i) {
                const double t = static_cast<double>(pos + i) / 48000.0;
                const auto x = static_cast<float>(0.5 * std::sin(kTwoPiD * static_cast<double>(kF0) * t));
                cL[i] = x;
                cR[i] = x;
                aL[i] = 0.5f * x;
                aR[i] = 0.5f * x;
                if (pos + i == nanAt) {
                    cL[i] = makeNaN();
                }
                if (pos + i == infAt) {
                    aR[i] = makePosInf();
                }
            }
            prod.feedInterval(std::span<const float>(cL), std::span<const float>(cR), std::span<const float>(aL),
                              std::span<const float>(aR), kF0, 0u, false, pos);
            Frame f{};
            if (prod.takeFrame(f)) {
                frames.push_back(f);
            }
            pos += kInterval;
        }
        CHECK(prod.invalidWindows() >= 2u);  // the NaN and the +Inf windows were dropped
        REQUIRE(frames.size() >= 3u);
        bool anyPower = false;
        for (const Frame& f : frames) {
            CHECK(frameFloatsFinite(f));
            anyPower = anyPower || f.partialPower[0] > 0.0f;
        }
        CHECK(anyPower);  // non-vacuity: the clean windows measured the sine
    }

    SECTION("RePrepareCarriesNewRate") {
        ProcessorFixture fx;
        REQUIRE(fx.prepare(48000.0, static_cast<Steinberg::int32>(kHostBlock)) == Steinberg::kResultOk);
        fx.proc->setSpectrumProducerEnabledForTest(true);
        Krate::Test::EventList ev;
        ev.addNoteOn(kNoteC3, 0.8f, 0);
        std::uint32_t published = renderDiscard(fx, kHostBlock, &ev);
        for (std::size_t done = kHostBlock; done < 48000u; done += kHostBlock) {
            published += renderDiscard(fx, kHostBlock);
        }
        REQUIRE(published > 0u);
        CHECK(fx.proc->lastSpectrumFrameForTest().sampleRate == 48000.0f);

        REQUIRE(fx.proc->setActive(0u) == Steinberg::kResultOk);
        Steinberg::Vst::ProcessSetup setup{};
        setup.processMode = Steinberg::Vst::kRealtime;
        setup.symbolicSampleSize = Steinberg::Vst::kSample32;
        setup.maxSamplesPerBlock = static_cast<Steinberg::int32>(kHostBlock);
        setup.sampleRate = 96000.0;
        REQUIRE(fx.proc->setupProcessing(setup) == Steinberg::kResultOk);
        REQUIRE(fx.proc->setActive(1u) == Steinberg::kResultOk);

        std::vector<float> rates;
        rates.reserve(512);
        for (std::size_t done = 0; done < 96000u; done += kHostBlock) {
            if (renderDiscard(fx, kHostBlock, done == 0u ? &ev : nullptr) > 0u) {
                rates.push_back(fx.proc->lastSpectrumFrameForTest().sampleRate);
            }
        }
        REQUIRE_FALSE(rates.empty());
        CHECK(rates.front() == 96000.0f);
        std::size_t stale = 0;
        for (const float r : rates) {
            if (r != 96000.0f) {
                ++stale;
            }
        }
        INFO(rates.size() << " frames after the re-prepare");
        CHECK(stale == 0u);
    }

    SECTION("TinyInputsStayNormal") {
        ProcessorFixture fx;
        REQUIRE(fx.prepare(48000.0, static_cast<Steinberg::int32>(kHostBlock)) == Steinberg::kResultOk);
        Producer& prod = fx.proc->mutableSpectrumProducerForTest();
        const auto f0 = static_cast<float>(f0Of(kNoteE1));
        const std::uint64_t n = windowSamplesFor(48000.0, static_cast<double>(f0));
        std::array<float, kInterval> c{};
        std::array<float, kInterval> a{};
        c.fill(1.0e-38f);  // character and Anchor taps at the same tiny level (plan 6.2 SC-026 step 4)
        a.fill(1.0e-38f);
        std::size_t framesSeen = 0;
        for (std::uint64_t pos = 0; pos < 5u * n + kInterval; pos += kInterval) {
            prod.feedInterval(std::span<const float>(c), std::span<const float>(c), std::span<const float>(a),
                              std::span<const float>(a), f0, 0u, false, pos);
            Frame f{};
            if (prod.takeFrame(f)) {
                ++framesSeen;
                CHECK(frameFloatsFinite(f));
                CHECK(zeroOrNormalBits(f.anchorPower));
                for (const float p : f.partialPower) {
                    INFO("power bits " << std::bit_cast<std::uint32_t>(p));
                    CHECK(zeroOrNormalBits(p));
                }
            }
        }
        CHECK(framesSeen == 5u);
    }

    SECTION("ViewDecaysAfterSilence") {
        // Step 3 (T021): HarmonicBarViews over the controller's cached frame. After a silent frame arrives, 33 ms
        // ticks never raise a height and lower each by exactly min(h, 0.033 / 0.300); a second view ticked over
        // exactly 0.300 s (two 150 ms ticks) is all 0, so the bars reach 0 by the D2 decay time.
        using P::UI::HarmonicBarView;
        REQUIRE(HarmonicBarView::kSilenceDecaySeconds == 0.300f);
        ConnectedPair cp;
        const P::Controller& ctrl = *cp.cf.controller;
        REQUIRE(cp.fx.prepare(48000.0, static_cast<Steinberg::int32>(kHostBlock)) == Steinberg::kResultOk);
        if (queueAvailable(ctrl.queueOpenCountForTest() > 0u, "SC-026 step 3")) {
            const VSTGUI::CRect rect(0.0, 0.0, 480.0, 160.0);
            auto ticked = VSTGUI::makeOwned<HarmonicBarView>(rect, &ctrl.cachedFrameForTest(),
                                                             HarmonicBarView::Variant::Main);
            auto exact = VSTGUI::makeOwned<HarmonicBarView>(rect, &ctrl.cachedFrameForTest(),
                                                            HarmonicBarView::Variant::Core);
            constexpr float kTick = 0.033f;

            // A held note until a sounding frame is cached, then its release until the cached frame is silent. Both
            // views are ticked after every delivered sounding frame, so they hold the last one when silence arrives.
            Krate::Test::EventList ev;
            ev.addNoteOn(kNoteC3, 0.8f, 0);
            bool sounding = false;
            for (std::size_t b = 0; b < 400u && !sounding; ++b) {
                if (renderDiscard(cp.fx, kHostBlock, b == 0u ? &ev : nullptr, &ctrl) > 0u) {
                    sounding = !isSilentFrame(ctrl.cachedFrameForTest());
                }
            }
            REQUIRE(sounding);
            ticked->onTickForTest(kTick);
            exact->onTickForTest(kTick);
            REQUIRE(peakHeight(*ticked.get()) > 0.0f);  // non-vacuity: bars are drawn before the release

            ev.clear();
            ev.addNoteOff(kNoteC3, 0);
            const auto maxBlocks = static_cast<std::size_t>(std::ceil(20.0 * 48000.0 / static_cast<double>(kHostBlock)));
            bool silent = false;
            for (std::size_t b = 0; b < maxBlocks && !silent; ++b) {
                if (renderDiscard(cp.fx, kHostBlock, b == 0u ? &ev : nullptr, &ctrl) == 0u) {
                    continue;
                }
                silent = isSilentFrame(ctrl.cachedFrameForTest());
                if (!silent) {
                    ticked->onTickForTest(kTick);
                    exact->onTickForTest(kTick);
                }
            }
            REQUIRE(silent);
            REQUIRE(peakHeight(*ticked.get()) > 0.0f);  // non-vacuity: something is left to decay

            const float step = kTick / HarmonicBarView::kSilenceDecaySeconds;  // the view's own expression
            HarmonicBarView::Heights prev = ticked->barHeightsForTest();
            float prevAnchor = ticked->anchorHeightForTest();
            int zeroAtTick = -1;
            for (int k = 1; k <= 12; ++k) {
                ticked->onTickForTest(kTick);
                const HarmonicBarView::Heights& cur = ticked->barHeightsForTest();
                const float curAnchor = ticked->anchorHeightForTest();
                INFO("tick " << k << " (" << static_cast<double>(k) * 0.033 << " s after the silent frame)");
                for (std::size_t n = 0; n < cur.size(); ++n) {
                    INFO("bar n = " << (n + 1) << ": " << prev[n] << " -> " << cur[n]);
                    CHECK(cur[n] <= prev[n]);
                    CHECK(std::abs((prev[n] - cur[n]) - std::min(prev[n], step)) <= 1.0e-6f);
                }
                CHECK(curAnchor <= prevAnchor);
                CHECK(std::abs((prevAnchor - curAnchor) - std::min(prevAnchor, step)) <= 1.0e-6f);
                if (zeroAtTick < 0 && peakHeight(*ticked.get()) == 0.0f) {
                    zeroAtTick = k;
                }
                prev = cur;
                prevAnchor = curAnchor;
            }
            INFO("all heights 0 at tick " << zeroAtTick);
            CHECK(zeroAtTick > 0);
            CHECK(zeroAtTick <= 10);  // the first 33 ms tick at or past 0.300 s
            for (const float h : ticked->barHeightsForTest()) {
                CHECK(h == 0.0f);
            }

            // Exactly 0.300 s: two 150 ms ticks (each step is exactly 0.5 in float).
            const float before = peakHeight(*exact.get());
            REQUIRE(before > 0.0f);
            REQUIRE(before <= 1.0f);
            exact->onTickForTest(0.150f);
            CHECK(peakHeight(*exact.get()) <= before);
            exact->onTickForTest(0.150f);
            for (const float h : exact->barHeightsForTest()) {
                CHECK(h == 0.0f);
            }
            CHECK(exact->anchorHeightForTest() == 0.0f);
        }
    }

    SECTION("ClosedEditorCaches") {
        // FR-091 (T021): no editor is ever opened here, so the controller has created no view. Delivered blocks
        // still update cachedFrameForTest() and are counted by framesReceivedForTest().
        ConnectedPair cp;
        P::Controller& ctrl = *cp.cf.controller;
        CHECK(sameBytes(ctrl.cachedFrameForTest(), P::Controller::silenceFrame()));  // before any block
        CHECK(ctrl.framesReceivedForTest() == 0u);
        REQUIRE(cp.fx.prepare(48000.0, static_cast<Steinberg::int32>(kHostBlock)) == Steinberg::kResultOk);

        if (queueAvailable(ctrl.queueOpenCountForTest() > 0u, "FR-091 closed-editor arm")) {
            Krate::Test::EventList ev;
            ev.addNoteOn(kNoteC3, 0.8f, 0);
            std::uint32_t published = 0;
            for (std::size_t done = 0; done < 48000u; done += kHostBlock) {
                published += renderDiscard(cp.fx, kHostBlock, done == 0u ? &ev : nullptr, &ctrl);  // byte-compares
            }
            REQUIRE(published > 0u);
            CHECK(ctrl.framesReceivedForTest() == published);
            CHECK_FALSE(isSilentFrame(ctrl.cachedFrameForTest()));
            CHECK(sameBytes(ctrl.cachedFrameForTest(), cp.fx.proc->lastSpectrumFrameForTest()));
        }

        // Direct delivery, every OS: the uid and size filters, the newest valid block wins, each one is counted.
        Frame older = P::Controller::silenceFrame();
        older.sequence = 7001u;
        older.flags = 0;
        older.numPartials = 2;
        older.partialPower[0] = 0.5f;
        older.partialPower[1] = 0.25f;
        Frame newer = older;
        newer.sequence = 7002u;
        newer.partialPower[1] = 0.125f;
        std::array<Steinberg::Vst::DataExchangeBlock, 2> blocks{};
        blocks[0].data = &older;
        blocks[0].size = static_cast<Steinberg::uint32>(sizeof(Frame));
        blocks[0].blockID = 0;
        blocks[1].data = &newer;
        blocks[1].size = static_cast<Steinberg::uint32>(sizeof(Frame));
        blocks[1].blockID = 1;
        const Frame cachedBefore = ctrl.cachedFrameForTest();
        const std::uint64_t receivedBefore = ctrl.framesReceivedForTest();

        ctrl.onDataExchangeBlocksReceived(0x5645434Fu /* 'VECO', Vorago's */, 2u, blocks.data(), 0);
        CHECK(sameBytes(ctrl.cachedFrameForTest(), cachedBefore));
        CHECK(ctrl.framesReceivedForTest() == receivedBefore);

        Steinberg::Vst::DataExchangeBlock shortBlock = blocks[1];
        shortBlock.size = static_cast<Steinberg::uint32>(sizeof(Frame) - 1u);
        ctrl.onDataExchangeBlocksReceived(kPrfd, 1u, &shortBlock, 0);
        CHECK(sameBytes(ctrl.cachedFrameForTest(), cachedBefore));
        CHECK(ctrl.framesReceivedForTest() == receivedBefore);

        ctrl.onDataExchangeBlocksReceived(kPrfd, 2u, blocks.data(), 0);
        CHECK(sameBytes(ctrl.cachedFrameForTest(), newer));
        CHECK(ctrl.framesReceivedForTest() == receivedBefore + 2u);

        // A re-opened queue starts from silence (plan 4.6.1) and asks for UI-thread dispatch; another uid is ignored.
        const std::uint32_t opensBefore = ctrl.queueOpenCountForTest();
        Steinberg::TBool background = 1;
        ctrl.queueOpened(0x5645434Fu, kHandlerBlockBytes, background);
        CHECK(ctrl.queueOpenCountForTest() == opensBefore);
        CHECK(sameBytes(ctrl.cachedFrameForTest(), newer));
        background = 1;
        ctrl.queueOpened(kPrfd, kHandlerBlockBytes, background);
        CHECK(background == 0);
        CHECK(ctrl.queueOpenCountForTest() == opensBefore + 1u);
        CHECK(ctrl.lastQueueBlockSizeForTest() == kHandlerBlockBytes);
        CHECK(sameBytes(ctrl.cachedFrameForTest(), P::Controller::silenceFrame()));
    }
}

// =============================================================================
// SC-023 (FR-071, FR-076): the DataExchange handler lifecycle, real processor and real controller, SDK fallback.
// =============================================================================
TEST_CASE("Profundum_SpectrumFrameHandlerLifecycle", "[profundum][ui][spectrum][lifecycle]") {
    SECTION("OpenCloseReprepare") {
        ConnectedPair cp;
        P::Controller& ctrl = *cp.cf.controller;
        REQUIRE(ctrl.queueOpenCountForTest() == 0u);
        REQUIRE(ctrl.queueCloseCountForTest() == 0u);

        REQUIRE(cp.fx.prepare(48000.0, static_cast<Steinberg::int32>(kHostBlock)) == Steinberg::kResultOk);
        const bool opened = ctrl.queueOpenCountForTest() > 0u;
        if (queueAvailable(opened, "setActive(true)")) {
            // setActive(true): exactly one queueOpened, uid 'PRFD', block 288 (counted only for the 'PRFD' uid).
            CHECK(ctrl.queueOpenCountForTest() == 1u);
            CHECK(ctrl.lastQueueBlockSizeForTest() == kHandlerBlockBytes);
            CHECK(ctrl.queueCloseCountForTest() == 0u);
            CHECK(cp.toController.count("DataExchangeQueueOpened") == 1u);
            static_cast<void>(renderDiscard(cp.fx, kHostBlock));
            CHECK(cp.fx.proc->spectrumQueueOpenForTest());  // the first process() probed an open queue

            // setActive(false): exactly one queueClosed.
            REQUIRE(cp.fx.proc->setActive(0u) == Steinberg::kResultOk);
            CHECK(ctrl.queueCloseCountForTest() == 1u);
            CHECK(ctrl.queueOpenCountForTest() == 1u);
            CHECK(cp.toController.count("DataExchangeQueueClosed") == 1u);
            CHECK_FALSE(cp.fx.proc->spectrumQueueOpenForTest());

            // Active again at 48 kHz, then deactivate, setupProcessing(96 kHz), activate: exactly one more close and
            // one more open, with the same uid and block size.
            REQUIRE(cp.fx.proc->setActive(1u) == Steinberg::kResultOk);
            REQUIRE(ctrl.queueOpenCountForTest() == 2u);
            const std::uint32_t opens = ctrl.queueOpenCountForTest();
            const std::uint32_t closes = ctrl.queueCloseCountForTest();
            REQUIRE(cp.fx.proc->setActive(0u) == Steinberg::kResultOk);
            Steinberg::Vst::ProcessSetup setup{};
            setup.processMode = Steinberg::Vst::kRealtime;
            setup.symbolicSampleSize = Steinberg::Vst::kSample32;
            setup.maxSamplesPerBlock = static_cast<Steinberg::int32>(kHostBlock);
            setup.sampleRate = 96000.0;
            REQUIRE(cp.fx.proc->setupProcessing(setup) == Steinberg::kResultOk);
            REQUIRE(cp.fx.proc->setActive(1u) == Steinberg::kResultOk);
            CHECK(ctrl.queueCloseCountForTest() == closes + 1u);
            CHECK(ctrl.queueOpenCountForTest() == opens + 1u);
            CHECK(ctrl.lastQueueBlockSizeForTest() == kHandlerBlockBytes);
            CHECK(sameBytes(ctrl.cachedFrameForTest(), P::Controller::silenceFrame()));  // reset on the open
            static_cast<void>(renderDiscard(cp.fx, kHostBlock));
            CHECK(cp.fx.proc->spectrumQueueOpenForTest());
        } else {
            // Linux only (no run loop in this harness): the plain ConnectedPair arm gives the closed-queue readings,
            // asserted, not skipped.
            checkClosedQueueIsNoOp(cp.fx);
        }
    }

    SECTION("ClosedQueueIsNoOp") {
        // Every OS (FR-071): a host context that is not an IHostApplication, so the fallback's openQueue fails.
        NotAHostContext context;
        CountingConnectionPoint peer;
        {
            ProcessorFixture fx{&context};
            REQUIRE(fx.proc->connect(&peer) == Steinberg::kResultTrue);
            REQUIRE(fx.prepare(48000.0, static_cast<Steinberg::int32>(kHostBlock)) == Steinberg::kResultOk);
            checkClosedQueueIsNoOp(fx);
            CHECK(peer.dataExchangeMessages == 0);  // no queueOpened was ever sent
            REQUIRE(fx.proc->disconnect(&peer) == Steinberg::kResultTrue);
        }
    }
}

// =============================================================================
// SC-030 (FR-075): frame cadence and capacity.
// =============================================================================
TEST_CASE("Profundum_SpectrumFrameCadence", "[profundum][ui][spectrum]") {
    SECTION("Seam") {
        constexpr double kFs = 48000.0;
        constexpr std::size_t kTotal = 96000;  // 2 s
        for (const Patch patch : {Patch::Default, Patch::Fold}) {
            for (const Steinberg::int16 note : {kNoteE1, kNoteC3}) {
                INFO("patch " << static_cast<int>(patch) << ", note " << note);
                ProcessorFixture fx;
                REQUIRE(fx.prepare(kFs, static_cast<Steinberg::int32>(kHostBlock)) == Steinberg::kResultOk);
                const MorphState s = patchOf(patch);
                loadPatch(fx, s, s);
                fx.proc->setSpectrumProducerEnabledForTest(true);

                Krate::Test::EventList ev;
                ev.addNoteOn(note, 0.8f, 0);
                std::size_t done = 0;
                while (done < kTotal) {
                    const std::size_t blk = std::min(kHostBlock, kTotal - done);
                    static_cast<void>(renderDiscard(fx, blk, done == 0u ? &ev : nullptr));
                    done += blk;
                }

                const std::uint32_t windows = fx.proc->spectrumProducerForTest().windowsCompleted();
                const std::uint32_t nRef = windowSamplesFor(kFs, f0Of(note));
                const auto expected = static_cast<std::int64_t>(kTotal / nRef);
                INFO("published " << fx.proc->spectrumFramesPublishedForTest() << ", windows " << windows
                                  << ", floor(samples / N) " << expected);
                CHECK(fx.proc->spectrumFramesPublishedForTest() == windows);
                CHECK(std::llabs(static_cast<std::int64_t>(windows) - expected) <= 1);
                CHECK(fx.proc->spectrumMaxFramesPerProcessForTest() <= 1u);
                CHECK(fx.proc->spectrumMaxFramesPerProcessForTest() == 1u);  // non-vacuity: frames were published

                if (patch == Patch::Fold) {
                    const Frame& f = fx.proc->lastSpectrumFrameForTest();
                    REQUIRE_FALSE(isSilentFrame(f));
                    const auto limit = std::min<std::size_t>(
                        P::kSpectrumMaxPartials, static_cast<std::size_t>(std::floor(0.5 * kFs / f0Of(note))));
                    CHECK(static_cast<std::size_t>(f.numPartials) == limit);
                    CHECK(static_cast<std::size_t>(f.numPartials) <= P::kSpectrumMaxPartials);
                }
            }
        }
    }
}

}  // namespace ProfundumPluginTest
