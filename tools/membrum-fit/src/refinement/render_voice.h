#pragma once

#include "../types.h"
#include "dsp/drum_voice.h"

#include <span>
#include <vector>

namespace MembrumFit {

// RenderableMembrumVoice: minimal harness around Membrum::DrumVoice that takes
// a PadConfig, triggers a single hit, and fills a mono float buffer. Uses the
// production DSP path -- the analysis-by-synthesis loss surface at BOBYQA time
// is the same one the plugin produces at runtime.
//
// prepare(sr) MUST be called with the same sample rate used for modal
// extraction (specs/membrum-fit-tool.md §9 risk #10).
class RenderableMembrumVoice {
public:
    RenderableMembrumVoice();
    ~RenderableMembrumVoice();

    void prepare(double sampleRate, int maxBlockSize = 256);
    double sampleRate() const { return sampleRate_; }

    // Trigger a hit with the given PadConfig and render numSamples into `out`.
    // `out.size()` must equal numSamples.
    void render(const Membrum::PadConfig& config,
                float velocity01,
                std::span<float> out);

    // Convenience: allocate and return a buffer of length lenSec * sampleRate.
    std::vector<float> renderToVector(const Membrum::PadConfig& config,
                                      float velocity01,
                                      float lenSec);

private:
    double sampleRate_ = 44100.0;
    int    blockSize_  = 256;

    // Pimpl -- keeps DrumVoice's heavy template/instantiation machinery out of
    // this header for users that only need the public render API.
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace MembrumFit

#include <memory>
