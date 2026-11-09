#pragma once

// =============================================================================
// Disrumpo processor render rig (test support, header-only)
// =============================================================================
// Moved from the audit probe TU specs/disrumpo-audit/probes/audit_probes.cpp.txt
// (fix plan T1.00). A Rig owns an initialized Processor that has been through
// setupProcessing + setActive(true), pushes parameter changes with apply(), and
// renders a stereo signal block by block with render().
// =============================================================================

#include "processor/processor.h"

#include "artifact_detection.h"
#include "vst_param_changes.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/common/memorystream.h"

#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace Disrumpo::TestSupport {

using Steinberg::int32;
using Steinberg::Vst::ParamID;
using Steinberg::Vst::ParamValue;
using PChanges = Krate::Test::ParameterChanges;
using PList = std::vector<std::pair<ParamID, double>>;

// -----------------------------------------------------------------------------
// Metrics (non-finite samples are skipped / counted as 0)
// -----------------------------------------------------------------------------

inline double toDb(double lin) { return lin > 1e-12 ? 20.0 * std::log10(lin) : -240.0; }

inline bool fin(float v) { return Krate::DSP::detail::isFinite(v); }

inline double rmsOf(const float* x, size_t n) {
    if (n == 0) return 0.0;
    double s = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double v = fin(x[i]) ? x[i] : 0.0;
        s += v * v;
    }
    return std::sqrt(s / static_cast<double>(n));
}

inline double peakOf(const float* x, size_t n) {
    double p = 0.0;
    for (size_t i = 0; i < n; ++i) if (fin(x[i])) p = std::max(p, std::fabs(static_cast<double>(x[i])));
    return p;
}

inline double meanOf(const float* x, size_t n) {
    if (n == 0) return 0.0;
    double s = 0.0;
    for (size_t i = 0; i < n; ++i) s += fin(x[i]) ? x[i] : 0.0;
    return s / static_cast<double>(n);
}

/// Largest sample-to-sample step.
inline double maxJump(const float* x, size_t n) {
    double m = 0.0;
    for (size_t i = 1; i < n; ++i) {
        if (!fin(x[i]) || !fin(x[i - 1])) continue;
        m = std::max(m, std::fabs(static_cast<double>(x[i]) - static_cast<double>(x[i - 1])));
    }
    return m;
}

/// Clicks the shared ClickDetector finds (512/256 frames, 5 sigma, -60 dB energy floor).
inline size_t clickCount(const float* x, size_t n, double sr) {
    Krate::DSP::TestUtils::ClickDetectorConfig cfg;
    cfg.sampleRate = static_cast<float>(sr);
    cfg.frameSize = 512;
    cfg.hopSize = 256;
    cfg.detectionThreshold = 5.0f;
    cfg.energyThresholdDb = -60.0f;
    Krate::DSP::TestUtils::ClickDetector det(cfg);
    det.prepare();
    return det.detect(x, n).size();
}

struct ContResult {
    double ratio = 0.0;      ///< max step in the event window / max step in the steady windows
    size_t clicksEvent = 0;  ///< clicks in the event window
    size_t clicksBase = 0;   ///< clicks in a steady window of the same length before it
    size_t nonFinite = 0;
};

/// Continuity of `y` around an event (moved from the audit probe TU): the largest
/// step in [evBeg, evEnd) against the larger of the steady pre and post windows.
inline ContResult continuityAround(const std::vector<float>& y, size_t evBeg, size_t evEnd, size_t preBeg,
                                   size_t preEnd, size_t postBeg, size_t postEnd, double sr) {
    ContResult c;
    const double base = std::max(maxJump(y.data() + preBeg, preEnd - preBeg),
                                 maxJump(y.data() + postBeg, postEnd - postBeg));
    const double ev = maxJump(y.data() + evBeg, evEnd - evBeg);
    c.ratio = base > 1e-12 ? ev / base : (ev > 1e-9 ? 1e9 : 1.0);
    c.clicksEvent = clickCount(y.data() + evBeg, evEnd - evBeg, sr);
    const size_t w = std::min(evEnd - evBeg, preEnd - preBeg);
    c.clicksBase = clickCount(y.data() + preEnd - w, w, sr);
    for (float v : y) {
        if (!fin(v)) ++c.nonFinite;
    }
    return c;
}

// -----------------------------------------------------------------------------
// Output-parameter capture (records the processor's addPoint() writes)
// -----------------------------------------------------------------------------

class CaptureQueue : public Steinberg::Vst::IParamValueQueue {
public:
    explicit CaptureQueue(ParamID id) : id_(id) {}
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID /*iid*/, void** /*obj*/) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
    ParamID PLUGIN_API getParameterId() override { return id_; }
    int32 PLUGIN_API getPointCount() override { return static_cast<int32>(pts_.size()); }
    Steinberg::tresult PLUGIN_API getPoint(int32 index, int32& off, ParamValue& v) override {
        if (index < 0 || index >= static_cast<int32>(pts_.size())) return Steinberg::kResultFalse;
        off = pts_[static_cast<size_t>(index)].first;
        v = pts_[static_cast<size_t>(index)].second;
        return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API addPoint(int32 off, ParamValue v, int32& index) override {
        pts_.emplace_back(off, v);
        index = static_cast<int32>(pts_.size()) - 1;
        return Steinberg::kResultTrue;
    }
    ParamID id_;
    std::vector<std::pair<int32, double>> pts_;
};

class CaptureChanges : public Steinberg::Vst::IParameterChanges {
public:
    CaptureChanges() { qs_.reserve(16); }
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID /*iid*/, void** /*obj*/) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
    int32 PLUGIN_API getParameterCount() override { return static_cast<int32>(qs_.size()); }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(int32 index) override {
        if (index < 0 || index >= static_cast<int32>(qs_.size())) return nullptr;
        return &qs_[static_cast<size_t>(index)];
    }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(const ParamID& id, int32& index) override {
        for (size_t i = 0; i < qs_.size(); ++i) {
            if (qs_[i].id_ == id) { index = static_cast<int32>(i); return &qs_[i]; }
        }
        if (qs_.size() >= 16) return nullptr;
        qs_.emplace_back(id);
        index = static_cast<int32>(qs_.size()) - 1;
        return &qs_.back();
    }
    bool last(ParamID id, double& v) const {
        for (const auto& q : qs_) {
            if (q.id_ == id && !q.pts_.empty()) { v = q.pts_.back().second; return true; }
        }
        return false;
    }
    void clear() { qs_.clear(); }

private:
    std::vector<CaptureQueue> qs_;
};

// -----------------------------------------------------------------------------
// Processor rig
// -----------------------------------------------------------------------------

using BlockHook = std::function<void(size_t blockIdx, size_t start, PChanges& pc)>;
using OutHook = std::function<void(size_t blockIdx, CaptureChanges& out)>;

struct Rig {
    std::unique_ptr<Processor> p;
    double sr;
    int32 maxBlock;
    bool active = false;
    /// Host transport context handed to every process() call (nullptr = the host sends none).
    Steinberg::Vst::ProcessContext* ctx = nullptr;

    explicit Rig(double sampleRate = 48000.0, int32 mb = 512, bool doSetup = true)
        : sr(sampleRate), maxBlock(mb) {
        p = std::make_unique<Processor>();
        p->initialize(nullptr);
        if (doSetup) setup();
    }
    ~Rig() {
        deactivate();
        p->terminate();
    }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    void setup() {
        Steinberg::Vst::ProcessSetup ps{};
        ps.processMode = Steinberg::Vst::kRealtime;
        ps.symbolicSampleSize = Steinberg::Vst::kSample32;
        ps.sampleRate = sr;
        ps.maxSamplesPerBlock = maxBlock;
        p->setupProcessing(ps);
        p->setActive(true);
        active = true;
    }
    void deactivate() {
        if (active) { p->setActive(false); active = false; }
    }
    void reactivate() {
        p->setActive(false);
        p->setActive(true);
        active = true;
    }

    void processBlock(float* iL, float* iR, float* oL, float* oR, int32 n,
                      Steinberg::Vst::IParameterChanges* in, Steinberg::Vst::IParameterChanges* out = nullptr) {
        float* ic[2] = {iL, iR};
        float* oc[2] = {oL, oR};
        Steinberg::Vst::AudioBusBuffers ib{};
        ib.numChannels = 2;
        ib.channelBuffers32 = ic;
        Steinberg::Vst::AudioBusBuffers ob{};
        ob.numChannels = 2;
        ob.channelBuffers32 = oc;
        Steinberg::Vst::ProcessData d{};
        d.processMode = Steinberg::Vst::kRealtime;
        d.symbolicSampleSize = Steinberg::Vst::kSample32;
        d.numSamples = n;
        d.numInputs = 1;
        d.numOutputs = 1;
        d.inputs = &ib;
        d.outputs = &ob;
        d.inputParameterChanges = in;
        d.outputParameterChanges = out;
        d.processContext = ctx;
        p->process(d);
    }

    void apply(const PList& l) {
        PChanges pc;
        for (const auto& c : l) pc.addChange(c.first, c.second);
        float z[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        processBlock(z, z + 1, z + 2, z + 3, 0, &pc);
    }

    void render(const std::vector<float>& inL, const std::vector<float>& inR,
                std::vector<float>& outL, std::vector<float>& outR, int32 block = 512,
                const BlockHook& hook = {}, const OutHook& outHook = {}) {
        const size_t n = std::min(inL.size(), inR.size());
        outL.assign(n, 0.0f);
        outR.assign(n, 0.0f);
        std::vector<float> bL(static_cast<size_t>(block)), bR(static_cast<size_t>(block));
        std::vector<float> oL(static_cast<size_t>(block)), oR(static_cast<size_t>(block));
        PChanges pc;
        CaptureChanges cap;
        size_t blk = 0;
        for (size_t s = 0; s < n; s += static_cast<size_t>(block), ++blk) {
            const auto len = static_cast<int32>(std::min(static_cast<size_t>(block), n - s));
            std::copy(inL.begin() + static_cast<std::ptrdiff_t>(s), inL.begin() + static_cast<std::ptrdiff_t>(s) + len, bL.begin());
            std::copy(inR.begin() + static_cast<std::ptrdiff_t>(s), inR.begin() + static_cast<std::ptrdiff_t>(s) + len, bR.begin());
            pc.clear();
            if (hook) hook(blk, s, pc);
            cap.clear();
            processBlock(bL.data(), bR.data(), oL.data(), oR.data(), len, &pc, outHook ? &cap : nullptr);
            if (outHook) outHook(blk, cap);
            std::copy(oL.begin(), oL.begin() + len, outL.begin() + static_cast<std::ptrdiff_t>(s));
            std::copy(oR.begin(), oR.begin() + len, outR.begin() + static_cast<std::ptrdiff_t>(s));
        }
    }
};

// -----------------------------------------------------------------------------
// State round trip through a MemoryStream
// -----------------------------------------------------------------------------

inline bool setStateBytes(Processor& p, const std::vector<uint8_t>& bytes) {
    auto* s = new Steinberg::MemoryStream(const_cast<uint8_t*>(bytes.data()), static_cast<Steinberg::TSize>(bytes.size()));
    const auto res = p.setState(s);
    s->release();
    return res == Steinberg::kResultOk || res == Steinberg::kResultTrue;
}

inline std::vector<uint8_t> getStateBytes(Processor& p) {
    auto* s = new Steinberg::MemoryStream();
    p.getState(s);
    Steinberg::int64 sz = 0;
    s->seek(0, Steinberg::IBStream::kIBSeekEnd, &sz);
    s->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    std::vector<uint8_t> out(static_cast<size_t>(sz));
    Steinberg::int32 got = 0;
    if (sz > 0) s->read(out.data(), static_cast<Steinberg::int32>(sz), &got);
    s->release();
    return out;
}

}  // namespace Disrumpo::TestSupport
