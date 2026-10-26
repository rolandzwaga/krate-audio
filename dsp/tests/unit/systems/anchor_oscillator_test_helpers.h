// ==============================================================================
// Test helpers: AnchorOscillator render and measurement helpers
//                                    (Profundum Phase 3, specs/profundum-phase3-anchor)
// ==============================================================================
// Plan S8.2; tasks.md T007 (base helpers), T008 (appends the analytic toolkit).
// Test-side only: allocation is fine here.
//
// Every helper is an `inline` function in namespace Krate::DSP::ProfundumTest
// with the `anchor` prefix (plan S0 ODR sweep), never an anonymous namespace in
// a header (unused-function warnings would break the zero-warning rule).
// ==============================================================================

#pragma once

#include "profundum_core_test_helpers.h"

#include <krate/dsp/systems/anchor_oscillator.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Krate::DSP::ProfundumTest {

struct AnchorRender {
    std::vector<float> L, R;
    std::vector<double> g, phase, inc;
};

/// Renders n samples in `block`-sized blocks (the last may be shorter), passing the matching
/// slices of f0Traj / rTraj (each must hold the whole render) when non-null. With perSample the
/// blocks are forced to 1 sample and modeGain(), phase() and phaseIncrement() are recorded after
/// each sample. Always requires L == R bit-identically.
inline AnchorRender renderAnchor(AnchorOscillator& a, std::size_t n, std::size_t block,
                                 const float* f0Traj = nullptr, const float* rTraj = nullptr,
                                 bool perSample = false) {
    REQUIRE(block > 0u);
    const std::size_t blk = perSample ? std::size_t{1} : block;
    AnchorRender r;
    r.L.assign(n, 0.0f);
    r.R.assign(n, 0.0f);
    if (perSample) {
        r.g.reserve(n);
        r.phase.reserve(n);
        r.inc.reserve(n);
    }
    for (std::size_t pos = 0; pos < n; pos += blk) {
        const std::size_t len = std::min(blk, n - pos);
        a.processBlock(r.L.data() + pos, r.R.data() + pos, len, f0Traj != nullptr ? f0Traj + pos : nullptr,
                       rTraj != nullptr ? rTraj + pos : nullptr);
        if (perSample) {
            r.g.push_back(a.modeGain());
            r.phase.push_back(a.phase());
            r.inc.push_back(a.phaseIncrement());
        }
    }
    requireLREqual(r.L.data(), r.R.data(), n);
    return r;
}

/// Instantaneous frequency between recorded phases n - 1 and n: wrap(phase[n] - phase[n-1]) · fs,
/// with the difference wrapped into [-0.5, 0.5).
inline double anchorInstFreqHz(const std::vector<double>& phase, std::size_t n, double fs) {
    REQUIRE(n >= 1u);
    REQUIRE(n < phase.size());
    double d = phase[n] - phase[n - 1];
    d -= std::floor(d + 0.5);   // [-0.5, 0.5)
    return d * fs;
}

/// Σ x² in double over [from, to).
inline double anchorWindowEnergy(std::span<const float> x, std::size_t from, std::size_t to) {
    REQUIRE(from <= to);
    REQUIRE(to <= x.size());
    double e = 0.0;
    for (std::size_t i = from; i < to; ++i)
        e += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    return e;
}

/// Equal-tempered MIDI note frequency (A4 = 440 Hz), rounded to float.
inline float anchorMidiHz(int m) {
    return static_cast<float>(440.0 * std::exp2((static_cast<double>(m) - 69.0) / 12.0));
}

// =============================================================================
// T008 analytic toolkit (plan S8.2)
// =============================================================================

inline constexpr double kAnchorPiD = 3.14159265358979323846;

/// D-4 closed form in double: g0 + a·½(1 − cos(π·n/len)).
inline double anchorRaisedCosine(double g0, double a, std::size_t n, std::size_t len) {
    REQUIRE(len > 0u);
    return g0 + a * 0.5 * (1.0 - std::cos(kAnchorPiD * static_cast<double>(n) / static_cast<double>(len)));
}

/// FR-041 cubic Hermite in double: value g0 and slope gp0 (per sample) at n = 0, value g1 and slope 0
/// at n = len. Basis h00/h10/h01 with t = n/len and m0 = len·gp0. n is not clamped: n > len evaluates
/// the cubic's extension (used by central differences at n = len).
inline double anchorHermite(double g0, double gp0, double g1, std::size_t n, std::size_t len) {
    REQUIRE(len > 0u);
    const double t = static_cast<double>(n) / static_cast<double>(len);
    const double t2 = t * t;
    const double t3 = t2 * t;
    const double m0 = static_cast<double>(len) * gp0;
    const double h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
    const double h10 = t3 - 2.0 * t2 + t;
    const double h01 = -2.0 * t3 + 3.0 * t2;
    return h00 * g0 + h10 * m0 + h01 * g1;
}

/// Peak |second difference| per sample² of anchorHermite: H'' is linear in t, so the peak is at an
/// end: max(|6Δ − 4m0|, |6Δ − 2m0|)/len², Δ = g1 − g0, m0 = len·gp0.
inline double anchorHermitePeakD2(double g0, double gp0, double g1, std::size_t len) {
    REQUIRE(len > 0u);
    const double L = static_cast<double>(len);
    const double delta = g1 - g0;
    const double m0 = L * gp0;
    return std::max(std::abs(6.0 * delta - 4.0 * m0), std::abs(6.0 * delta - 2.0 * m0)) / (L * L);
}

/// One analytic segment the C¹ checker expects. The segment spans samples [start, start + len]
/// inclusive (its end sample is the next segment's start sample).
struct AnchorSegmentSpec {
    enum class Kind { Hold, Cosine, Hermite } kind;
    double g0, a, gp0, g1;
    std::size_t start, len;
};

struct C1Report {
    double worstRatio;          // max in-segment |Δ²g| / that segment's analytic bound
    double worstBoundaryJump;   // max |Δg[b+1] − Δg[b]| at a segment boundary b
    bool inRange;               // every sample in [lo, hi]
    bool pass;                  // inRange, worstRatio ≤ 1.05, every boundary jump ≤ 1.05 × its bound
};

/// Analytic per-sample² bound of one segment: Hold 0, raised cosine |A|·π²/(2N²), Hermite
/// anchorHermitePeakD2.
inline double anchorSegmentD2Bound(const AnchorSegmentSpec& s) {
    switch (s.kind) {
        case AnchorSegmentSpec::Kind::Hold:
            return 0.0;
        case AnchorSegmentSpec::Kind::Cosine: {
            const double N = static_cast<double>(s.len);
            return std::abs(s.a) * kAnchorPiD * kAnchorPiD / (2.0 * N * N);
        }
        case AnchorSegmentSpec::Kind::Hermite:
            return anchorHermitePeakD2(s.g0, s.gp0, s.g1, s.len);
    }
    return 0.0;
}

/// SC-006(a) C¹ checker. Per segment, the max |Δ²g| over its interior (both neighbours inside the
/// segment) against 1.05 × its analytic bound; at each boundary b (a segment's start, b ≥ 1),
/// |g[b+1] − 2g[b] + g[b−1]| against 1.05 × the larger adjacent in-segment bound. A Hold's bound is
/// 0, so the neighbour's bound applies (also to the Hold's own interior). A zero bound with a
/// non-zero Δ² reports a ratio of 1e300.
inline C1Report anchorCheckC1(std::span<const double> g, std::span<const AnchorSegmentSpec> segs, double lo,
                              double hi) {
    constexpr double kTolerance = 1.05;
    constexpr double kUnbounded = 1e300;
    C1Report rep{0.0, 0.0, true, false};
    for (const double v : g) {
        if (v < lo || v > hi)
            rep.inRange = false;
    }

    const std::size_t nSeg = segs.size();
    std::vector<double> bound(nSeg, 0.0);
    for (std::size_t i = 0; i < nSeg; ++i) {
        REQUIRE(segs[i].start + segs[i].len < g.size());
        bound[i] = anchorSegmentD2Bound(segs[i]);
    }
    const auto ratioOf = [&](double d2, double b) {
        if (b > 0.0)
            return std::abs(d2) / b;
        return d2 == 0.0 ? 0.0 : kUnbounded;
    };
    const auto d2At = [&](std::size_t n) { return g[n + 1] - 2.0 * g[n] + g[n - 1]; };

    for (std::size_t i = 0; i < nSeg; ++i) {
        double eff = bound[i];
        if (segs[i].kind == AnchorSegmentSpec::Kind::Hold) {
            if (i > 0)
                eff = std::max(eff, bound[i - 1]);
            if (i + 1 < nSeg)
                eff = std::max(eff, bound[i + 1]);
        }
        for (std::size_t n = segs[i].start + 1; n + 1 <= segs[i].start + segs[i].len; ++n)
            rep.worstRatio = std::max(rep.worstRatio, ratioOf(d2At(n), eff));
    }

    bool boundariesOk = true;
    for (std::size_t i = 1; i < nSeg; ++i) {
        const std::size_t b = segs[i].start;
        if (b < 1 || b + 1 >= g.size())
            continue;
        const double jump = std::abs(d2At(b));
        rep.worstBoundaryJump = std::max(rep.worstBoundaryJump, jump);
        if (jump > kTolerance * std::max(bound[i - 1], bound[i]))
            boundariesOk = false;
    }

    rep.pass = rep.inRange && rep.worstRatio <= kTolerance && boundariesOk;
    return rep;
}

/// SC-008(r) fundamental estimator (offline, double). x must hold TestUtils::kLowFrequencyFftSize
/// (262144) samples.
///   Stage 1 (plan S8.2, Hermes 1988 subharmonic summation): Hann magnitude spectrum of the frame
///   (lowFreq::magnitudeSpectrum), S(f) = Σ_{h=1}^{15} 0.84^{h−1}·M(h·f) with M linearly interpolated
///   between bins, searched on a 0.05-cent log grid over [f0Hint/2.2, 2.2·f0Hint], parabolic
///   refinement through the best three points.
///   Stage 2 (T008 deviation, surfaced): with M linearly interpolated, S(f) is piecewise linear and
///   peaks on a bin vertex of one harmonic, so stage 1 alone is quantised (measured in a Node model
///   of this exact estimator on the T008 control signals: −0.19 cent at E0, E1 and E2, which fails
///   the 0.1-cent control). Stage 2 maximises the same 15-term, 0.84-weighted sum on the exact
///   Hann-windowed DTFT magnitude |X(h·f)| by golden-section search over ±1.2 bins (re h1) around
///   the stage-1 estimate, to 1e-4 cent. Stage 1 still chooses the octave; stage 2 only refines.
inline double anchorShsF0(const float* x, double fs, double f0Hint) {
    namespace lf = TestUtils::lowFreq;
    constexpr std::size_t kFrame = TestUtils::kLowFrequencyFftSize;
    constexpr int kHarmonics = 15;
    constexpr double kCompression = 0.84;
    constexpr double kGridStep = 0.05 / 1200.0;   // octaves
    constexpr double kRefineTol = 1e-4 / 1200.0;  // octaves
    constexpr double kInvPhi = 0.6180339887498949;
    REQUIRE(x != nullptr);
    REQUIRE(fs > 0.0);
    REQUIRE(f0Hint > 0.0);

    std::vector<float> mags;
    REQUIRE(lf::magnitudeSpectrum(x, kFrame, mags));
    const double binHz = fs / static_cast<double>(kFrame);
    const double nyquist = 0.5 * fs;

    // ---- Stage 1: plan S8.2 ----
    const auto interpMag = [&](double hz) {
        const double p = hz / binHz;
        const auto k = static_cast<std::size_t>(p);
        if (k + 1 >= mags.size())
            return 0.0;
        const double fr = p - static_cast<double>(k);
        return static_cast<double>(mags[k]) * (1.0 - fr) + static_cast<double>(mags[k + 1]) * fr;
    };
    const auto gridScore = [&](double f) {
        double s = 0.0;
        double w = 1.0;
        for (int h = 1; h <= kHarmonics; ++h) {
            s += w * interpMag(static_cast<double>(h) * f);
            w *= kCompression;
        }
        return s;
    };
    const double lo = std::log2(f0Hint / 2.2);
    const double hi = std::log2(f0Hint * 2.2);
    const auto count = static_cast<std::size_t>((hi - lo) / kGridStep) + 1;
    std::vector<double> scores(count, 0.0);
    std::size_t best = 0;
    for (std::size_t i = 0; i < count; ++i) {
        scores[i] = gridScore(std::exp2(lo + static_cast<double>(i) * kGridStep));
        if (scores[i] > scores[best])
            best = i;
    }
    double offset = 0.0;
    if (best > 0 && best + 1 < count) {
        const double curv = scores[best - 1] - 2.0 * scores[best] + scores[best + 1];
        if (curv < 0.0)
            offset = 0.5 * (scores[best - 1] - scores[best + 1]) / curv;
    }
    const double fCoarse = std::exp2(lo + (static_cast<double>(best) + offset) * kGridStep);

    // ---- Stage 2: exact Hann-windowed DTFT refinement ----
    std::vector<double> xw(kFrame, 0.0);
    for (std::size_t i = 0; i < kFrame; ++i) {
        const double w =
            0.5 - 0.5 * std::cos(2.0 * kAnchorPiD * static_cast<double>(i) / static_cast<double>(kFrame));
        xw[i] = static_cast<double>(x[i]) * w;
    }
    const auto exactScore = [&](double f) {
        double s = 0.0;
        double w = 1.0;
        for (int h = 1; h <= kHarmonics; ++h) {
            const double hz = static_cast<double>(h) * f;
            if (hz >= nyquist)
                break;
            const double om = 2.0 * kAnchorPiD * hz / fs;
            const double cr = std::cos(om);
            const double ci = -std::sin(om);
            double pr = 1.0;
            double pi = 0.0;
            double re = 0.0;
            double im = 0.0;
            for (std::size_t i = 0; i < kFrame; ++i) {
                re += xw[i] * pr;
                im += xw[i] * pi;
                if (((i + 1) & 1023u) == 0) {   // re-anchor the rotating phasor every 1024 samples
                    const double ang = -om * static_cast<double>(i + 1);
                    pr = std::cos(ang);
                    pi = std::sin(ang);
                } else {
                    const double t = pr * cr - pi * ci;
                    pi = pr * ci + pi * cr;
                    pr = t;
                }
            }
            s += w * std::sqrt(re * re + im * im);
            w *= kCompression;
        }
        return s;
    };
    const double centre = std::log2(fCoarse);
    const double half = std::log2(1.0 + 1.2 * binHz / fCoarse);
    double a = centre - half;
    double b = centre + half;
    double c = b - kInvPhi * (b - a);
    double d = a + kInvPhi * (b - a);
    double sc = exactScore(std::exp2(c));
    double sd = exactScore(std::exp2(d));
    while (b - a > kRefineTol) {
        if (sc > sd) {
            b = d;
            d = c;
            sd = sc;
            c = b - kInvPhi * (b - a);
            sc = exactScore(std::exp2(c));
        } else {
            a = c;
            c = d;
            sc = sd;
            d = a + kInvPhi * (b - a);
            sd = exactScore(std::exp2(d));
        }
    }
    return std::exp2(0.5 * (a + b));
}

/// Identifier character for the SC-013 scanner (ASCII; no locale).
inline bool anchorIsIdentChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/// Replaces // and /* */ comments with a space and string / character literals (including raw
/// strings) with empty literals. A ' preceded by an identifier character is a digit separator.
inline std::string anchorStripCommentsAndLiterals(std::string_view src) {
    std::string out;
    out.reserve(src.size());
    const std::size_t n = src.size();
    std::size_t i = 0;
    while (i < n) {
        const char c = src[i];
        if (c == '/' && i + 1 < n && src[i + 1] == '/') {
            while (i < n && src[i] != '\n')
                ++i;
            out.push_back(' ');
            continue;
        }
        if (c == '/' && i + 1 < n && src[i + 1] == '*') {
            i += 2;
            while (i + 1 < n && !(src[i] == '*' && src[i + 1] == '/'))
                ++i;
            i = std::min(n, i + 2);
            out.push_back(' ');
            continue;
        }
        if (c == '"' && i > 0 && src[i - 1] == 'R') {   // raw string R"delim( ... )delim"
            const std::size_t open = src.find('(', i + 1);
            if (open == std::string_view::npos) {
                i = n;
                break;
            }
            std::string close = ")";
            close.append(src.substr(i + 1, open - (i + 1)));
            close.push_back('"');
            const std::size_t end = src.find(close, open + 1);
            i = end == std::string_view::npos ? n : end + close.size();
            out.append("\"\"");
            continue;
        }
        if (c == '"' || (c == '\'' && !(i > 0 && anchorIsIdentChar(src[i - 1])))) {
            ++i;
            while (i < n && src[i] != c) {
                if (src[i] == '\\')
                    ++i;
                ++i;
            }
            i = std::min(n, i + 1);
            out.push_back(c);
            out.push_back(c);
            continue;
        }
        out.push_back(c);
        ++i;
    }
    return out;
}

/// SC-013 scanner (plan S8.3): after stripping comments and literals, every declaration containing
/// the token `static` (whole word, so static_cast / static_assert never match) is returned unless it
/// is `static constexpr` / `static const` (the specifier after `static`, skipping `inline`) or a
/// function declaration (a `(` before the first `=`, `[`, `{` or `;`). Each hit is the text from
/// `static` to the next `;`.
inline std::vector<std::string> anchorFindMutableStatics(std::string_view source) {
    const std::string s = anchorStripCommentsAndLiterals(source);
    const std::string_view kw = "static";
    std::vector<std::string> hits;
    const auto nextWord = [&](std::size_t& p) {
        while (p < s.size() && !anchorIsIdentChar(s[p]))
            ++p;
        const std::size_t from = p;
        while (p < s.size() && anchorIsIdentChar(s[p]))
            ++p;
        return std::string_view(s).substr(from, p - from);
    };
    for (std::size_t pos = s.find(kw); pos != std::string::npos; pos = s.find(kw, pos + kw.size())) {
        if (pos > 0 && anchorIsIdentChar(s[pos - 1]))
            continue;
        const std::size_t after = pos + kw.size();
        if (after < s.size() && anchorIsIdentChar(s[after]))
            continue;   // static_cast, static_assert, ...
        std::size_t p = after;
        std::string_view word = nextWord(p);
        while (word == "inline")
            word = nextWord(p);
        if (word == "constexpr" || word == "const")
            continue;
        const std::size_t term = s.find_first_of("(=[{;", after);
        if (term != std::string::npos && s[term] == '(')
            continue;   // function declaration
        const std::size_t semi = s.find(';', pos);
        const std::size_t end = semi == std::string::npos ? s.size() : semi + 1;
        hits.emplace_back(s.substr(pos, end - pos));
    }
    return hits;
}

/// SC-012(a) separation rule with the plan S1 C-14 floor: entry n of each power vector (index 0 =
/// w1) becomes max(10·log10(wn/w1), −90 dB); the pair is separated iff some n ≥ 2 has
/// |dB_A − dB_B| ≥ 6 dB with at least one of the two above the −90 dB floor.
inline bool anchorWaveformsSeparated(std::span<const double> wA, std::span<const double> wB) {
    constexpr double kFloorDb = -90.0;
    constexpr double kSeparationDb = 6.0;
    REQUIRE(wA.size() == wB.size());
    REQUIRE(!wA.empty());
    REQUIRE(wA[0] > 0.0);
    REQUIRE(wB[0] > 0.0);
    for (std::size_t i = 1; i < wA.size(); ++i) {
        const double dbA = std::max(safeDb10(wA[i] / wA[0]), kFloorDb);
        const double dbB = std::max(safeDb10(wB[i] / wB[0]), kFloorDb);
        if ((dbA > kFloorDb || dbB > kFloorDb) && std::abs(dbA - dbB) >= kSeparationDb)
            return true;
    }
    return false;
}

}  // namespace Krate::DSP::ProfundumTest
