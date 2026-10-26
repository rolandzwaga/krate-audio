// Reproduce: clang++ -std=c++20 -O2 -ffast-math -march=x86-64-v2 -DNDEBUG -Idsp/include <this file> -o bench.exe
// then: pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe bench.exe   (plan S9.5)
// Microbenchmark: cost of the Phase 2 building blocks per base-rate sample at 48 kHz, stereo.
#include <krate/dsp/primitives/oversampler.h>
#include <krate/dsp/primitives/biquad.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>
#include <xmmintrin.h>
#include <pmmintrin.h>
using namespace Krate::DSP;
static constexpr size_t kRun = 16;      // the core renders in <= 16-sample runs
static constexpr size_t kSeconds = 20;  // audio seconds per measurement
volatile float sink;
template <size_t F> double benchOs(bool pre, int nl) {
    Oversampler<F, 2> os; os.prepare(48000.0, kRun);
    BiquadCascade<4> preL, preR; preL.setButterworth(FilterType::Lowpass, 0.40f * 48000.f, 48000.f * F);
    preR.setButterworth(FilterType::Lowpass, 0.40f * 48000.f, 48000.f * F);
    std::vector<float> inL(kRun), inR(kRun), osL(kRun * F), osR(kRun * F), outL(kRun), outR(kRun);
    double ph = 0; float v1 = 0, s1 = 1;
    const size_t runs = 48000 * kSeconds / kRun;
    auto t0 = std::chrono::steady_clock::now();
    for (size_t r = 0; r < runs; ++r) {
        for (size_t k = 0; k < kRun; ++k) { ph += 0.0108; inL[k] = inR[k] = 0.3f * float(std::sin(ph)); }
        os.upsample(inL.data(), osL.data(), kRun, 0); os.upsample(inR.data(), osR.data(), kRun, 1);
        for (float* b : {osL.data(), osR.data()}) {
            for (size_t i = 0; i < kRun * F; ++i) {
                float v = 8.0f * b[i] + 0.2f;
                if (nl == 1) { float s = std::sqrt(1.0f + v * v); b[i] = (v + v1) / (s + s1) - 0.95f * b[i]; v1 = v; s1 = s; }
                else if (nl == 2) { float u = v / std::sqrt(1.0f + v * v); b[i] = std::sin(2.5f * u) - 0.5f * b[i]; }
            }
        }
        if (pre) { preL.processBlock(osL.data(), kRun * F); preR.processBlock(osR.data(), kRun * F); }
        os.downsample(osL.data(), outL.data(), kRun, 0); os.downsample(osR.data(), outR.data(), kRun, 1);
        sink = outL[kRun - 1] + outR[kRun - 1];
    }
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return 100.0 * s / double(kSeconds);   // % of one core at 48 kHz
}
int main() {
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON); _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
    for (int rep = 0; rep < 3; ++rep) {
        std::printf("rep %d | 2x os only %.3f%% | 2x +pre %.3f%% | 2x +pre+adaa %.3f%% | 2x +pre+fold %.3f%%\n", rep,
            benchOs<2>(false, 0), benchOs<2>(true, 0), benchOs<2>(true, 1), benchOs<2>(true, 2));
        std::printf("rep %d | 4x os only %.3f%% | 4x +pre %.3f%% | 4x +pre+adaa %.3f%% | 4x +pre+fold %.3f%%\n", rep,
            benchOs<4>(false, 0), benchOs<4>(true, 0), benchOs<4>(true, 1), benchOs<4>(true, 2));
    }
}
