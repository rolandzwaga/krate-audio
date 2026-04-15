#include "loader.h"

#include <krate/dsp/primitives/sample_rate_converter.h>

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace MembrumFit {

namespace {

float computeCorrelation(const std::vector<float>& l, const std::vector<float>& r) {
    if (l.size() != r.size() || l.empty()) return 1.0f;
    double meanL = 0.0, meanR = 0.0;
    for (std::size_t i = 0; i < l.size(); ++i) { meanL += l[i]; meanR += r[i]; }
    meanL /= static_cast<double>(l.size());
    meanR /= static_cast<double>(r.size());
    double num = 0.0, denL = 0.0, denR = 0.0;
    for (std::size_t i = 0; i < l.size(); ++i) {
        const double dL = l[i] - meanL;
        const double dR = r[i] - meanR;
        num  += dL * dR;
        denL += dL * dL;
        denR += dR * dR;
    }
    const double den = std::sqrt(denL * denR);
    return (den > 0.0) ? static_cast<float>(num / den) : 1.0f;
}

// Simple linear resampler -- adequate for offline use. Lanczos would be better
// but we call this at most once per sample and quality is bounded by the
// downstream analysis resolution anyway.
std::vector<float> resampleLinear(const std::vector<float>& src, double srIn, double srOut) {
    if (std::abs(srIn - srOut) < 1e-6) return src;
    const double ratio = srOut / srIn;
    const std::size_t outLen = static_cast<std::size_t>(std::round(src.size() * ratio));
    std::vector<float> out(outLen);
    for (std::size_t i = 0; i < outLen; ++i) {
        const double srcPos = static_cast<double>(i) / ratio;
        const std::size_t i0 = static_cast<std::size_t>(std::floor(srcPos));
        const double frac = srcPos - static_cast<double>(i0);
        const std::size_t i1 = std::min(i0 + 1, src.size() - 1);
        out[i] = static_cast<float>(src[i0] * (1.0 - frac) + src[i1] * frac);
    }
    return out;
}

}  // namespace

std::optional<LoadedSample> loadSample(const std::filesystem::path& wav,
                                       double targetSampleRate) {
    unsigned channels = 0;
    unsigned sampleRate = 0;
    drwav_uint64 totalFrames = 0;
    float* interleaved = drwav_open_file_and_read_pcm_frames_f32(
        wav.string().c_str(), &channels, &sampleRate, &totalFrames, nullptr);
    if (!interleaved || totalFrames == 0 || channels == 0) {
        if (interleaved) drwav_free(interleaved, nullptr);
        return std::nullopt;
    }

    LoadedSample s;
    s.sampleRate = static_cast<double>(sampleRate);
    s.sourcePath = wav.string();

    std::vector<float> mono(totalFrames);
    if (channels == 1) {
        std::copy_n(interleaved, totalFrames, mono.begin());
        s.channelCorrelation = 1.0f;
    } else {
        std::vector<float> l(totalFrames), r(totalFrames);
        for (drwav_uint64 i = 0; i < totalFrames; ++i) {
            l[i] = interleaved[i * channels + 0];
            r[i] = interleaved[i * channels + 1];
            mono[i] = 0.5f * (l[i] + r[i]);
        }
        s.channelCorrelation = computeCorrelation(l, r);
    }
    drwav_free(interleaved, nullptr);

    if (std::abs(s.sampleRate - targetSampleRate) > 1e-3) {
        mono = resampleLinear(mono, s.sampleRate, targetSampleRate);
        s.sampleRate = targetSampleRate;
    }

    float peak = 0.0f;
    for (float x : mono) peak = std::max(peak, std::abs(x));
    s.originalPeakDbfs = (peak > 0.0f) ? 20.0f * std::log10(peak) : -120.0f;

    // Normalise to -1 dBFS.
    const float targetPeak = std::pow(10.0f, -1.0f / 20.0f);
    const float gain = (peak > 1e-9f) ? (targetPeak / peak) : 1.0f;
    for (float& x : mono) x *= gain;

    s.samples = std::move(mono);
    return s;
}

}  // namespace MembrumFit
