// ==============================================================================
// midi_golden_compare.h -- portable "this arp MIDI stream is unchanged" pin
// ==============================================================================
// Comparing a captured arpeggiator MIDI dump to a golden text file with
// `REQUIRE(actual == golden)` looks like a byte comparison, but it is not: every
// sample offset in that text is the result of floating-point arithmetic that was
// truncated to an integer. Truncation turns a 1-ULP difference into a whole
// sample, so the assertion silently demands bit-identical FP math from every
// compiler that will ever build it.
//
// It cannot hold. The VST3 SDK enables -ffast-math globally on the GCC and
// Clang legs (see dsp/CLAUDE.md), so those builds are free to reassociate and
// use reciprocal multiplies where MSVC does not. ArpeggiatorCore's step and gate
// durations land exactly on integers by construction -- the single most fragile
// place for a truncating cast:
//
//     baseDuration = (size_t)(secondsPerBeat * beatsPerStep * sampleRate);
//     swung        = (size_t)((double)baseDuration * (1.0 + swing));
//
// e.g. 11025 * 1.36 is exactly 14994 in real arithmetic, but evaluates to
// 14993.999999999998 or 14994.000000000002 depending on how the product is
// formed -- truncating to 14993 or 14994. This is how Ruinae's SC-004b goldens,
// generated on Windows, went red on the Linux and macOS legs while passing
// every Windows gate.
//
// Measured spread. Compiling the ArpeggiatorCore duration math under
// g++ -O3 -fno-fast-math vs g++ -O3 -ffast-math and sweeping the plausible
// note-value / swing / gate-percent space produced differences on 570 of the
// swept rows, always of magnitude 1 sample. Replaying the full 60-second
// Ruinae Tape_Shuffle sequence gave:
//
//     events emitted                         504  (identical on both legs)
//     events differing in kind/pitch/order      0
//     events differing in timing              146
//     worst timing difference                   2 samples  (~45 us at 44.1 kHz)
//
// The drift is bounded, not cumulative: durations are recomputed per step from
// the tempo rather than accumulated, so a rounding flip perturbs one step and
// does not walk. kTimingToleranceSamples is set to 8 -- 4x the measured worst
// case, still under 0.2 ms, and under 0.25% of even the shortest step duration
// these presets use (3362 samples). It cannot mask a real regression: a leaking
// sequencer lane or a changed pattern adds, drops or reorders events, or moves
// them by a fraction of a step, all of which this comparison rejects outright.
//
// Everything that is not a timestamp is still compared exactly: event count,
// order, note-on vs note-off, pitch and velocity.
// ==============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace Krate {
namespace TestUtils {

/// Largest per-event timing difference treated as cross-toolchain noise.
/// See the header comment for the measurement this is derived from.
inline constexpr std::int64_t kTimingToleranceSamples = 8;

struct MidiGoldenEvent {
    std::int64_t sample = 0;
    bool         isNoteOn = false;
    int          pitch = 0;
    int          velocity = 0;  ///< 0 for note-off
};

struct MidiGoldenResult {
    bool        ok = false;
    std::string message;
};

/// @brief Parse the dump format written by the arp golden harnesses:
///        "[<sample>] noteOn  <pitch> <velocity>" / "[<sample>] noteOff <pitch>".
/// Unparseable lines are reported by returning false so a corrupt or
/// format-drifted golden fails loudly instead of comparing as empty.
inline bool parseMidiGolden(const std::string& text,
                            std::vector<MidiGoldenEvent>& out,
                            std::string& error)
{
    out.clear();
    std::istringstream in(text);
    std::string line;
    std::size_t lineNo = 0;

    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        MidiGoldenEvent e{};
        char kind[16] = {};
        int pitch = 0;
        int velocity = 0;
        long long sample = 0;

        const int fields = std::sscanf(line.c_str(), "[%lld] %15s %d %d",
                                       &sample, kind, &pitch, &velocity);
        if (fields < 3) {
            error = "unparseable line " + std::to_string(lineNo) + ": " + line;
            return false;
        }

        const std::string k(kind);
        if (k == "noteOn") {
            if (fields < 4) {
                error = "noteOn without velocity on line "
                      + std::to_string(lineNo) + ": " + line;
                return false;
            }
            e.isNoteOn = true;
            e.velocity = velocity;
        } else if (k == "noteOff") {
            e.isNoteOn = false;
            e.velocity = 0;
        } else {
            error = "unknown event kind '" + k + "' on line "
                  + std::to_string(lineNo);
            return false;
        }

        e.sample = static_cast<std::int64_t>(sample);
        e.pitch = pitch;
        out.push_back(e);
    }

    return true;
}

/// @brief Compare a captured MIDI dump against its golden, portably.
///
/// Exact on event count, order, kind, pitch and velocity; timestamps must agree
/// within kTimingToleranceSamples. Byte-identical input short-circuits to a
/// pass, so a same-toolchain run still proves exact reproduction.
inline MidiGoldenResult compareMidiGolden(const std::string& actual,
                                          const std::string& golden)
{
    MidiGoldenResult result{};

    if (actual == golden) {
        result.ok = true;
        result.message = "byte-identical";
        return result;
    }

    std::vector<MidiGoldenEvent> a;
    std::vector<MidiGoldenEvent> g;
    std::string parseError;

    if (!parseMidiGolden(actual, a, parseError)) {
        result.message = "captured MIDI " + parseError;
        return result;
    }
    if (!parseMidiGolden(golden, g, parseError)) {
        result.message = "golden MIDI " + parseError;
        return result;
    }

    if (a.size() != g.size()) {
        result.message = "event count differs: captured " + std::to_string(a.size())
                       + " vs golden " + std::to_string(g.size());
        return result;
    }

    std::int64_t worstDrift = 0;
    std::size_t  driftCount = 0;

    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto& ae = a[i];
        const auto& ge = g[i];

        if (ae.isNoteOn != ge.isNoteOn || ae.pitch != ge.pitch
            || ae.velocity != ge.velocity)
        {
            std::ostringstream ss;
            ss << "event " << i << " differs: captured ["
               << ae.sample << "] " << (ae.isNoteOn ? "noteOn" : "noteOff")
               << " pitch " << ae.pitch << " vel " << ae.velocity
               << "  vs golden [" << ge.sample << "] "
               << (ge.isNoteOn ? "noteOn" : "noteOff")
               << " pitch " << ge.pitch << " vel " << ge.velocity;
            result.message = ss.str();
            return result;
        }

        const std::int64_t drift =
            ae.sample > ge.sample ? ae.sample - ge.sample : ge.sample - ae.sample;
        if (drift > 0) {
            ++driftCount;
            if (drift > worstDrift) worstDrift = drift;
        }
        if (drift > kTimingToleranceSamples) {
            std::ostringstream ss;
            ss << "event " << i << " timing drift " << drift
               << " samples exceeds tolerance " << kTimingToleranceSamples
               << " (captured [" << ae.sample << "] vs golden ["
               << ge.sample << "], pitch " << ae.pitch << ")";
            result.message = ss.str();
            return result;
        }
    }

    std::ostringstream ss;
    ss << "structurally identical (" << a.size() << " events); "
       << driftCount << " timestamps differ, worst " << worstDrift
       << " samples, within tolerance " << kTimingToleranceSamples;
    result.ok = true;
    result.message = ss.str();
    return result;
}

}  // namespace TestUtils
}  // namespace Krate
