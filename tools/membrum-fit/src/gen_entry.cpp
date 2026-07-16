// ==============================================================================
// membrum-fit-gen -- companion tool that renders a JSON PadConfig description
// to a WAV file via the production Membrum voice, for building the
// golden-test corpus (spec §5.3).
//
// Usage:
//   membrum-fit-gen <input.json> <output.wav> [--sr 44100] [--sec 2.0] [--vel 1.0]
// ==============================================================================

#include "refinement/render_voice.h"
#include "dsp/default_kit.h"
#include "dsp/pad_config.h"

#include <nlohmann/json.hpp>
#include "dr_wav.h"  // implementation is provided by loader.cpp inside membrum_fit_core

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using json = nlohmann::json;

Membrum::PadConfig padConfigFromJson(const json& j) {
    Membrum::PadConfig cfg{};
    // Start from the Membrum default Kick so tests using partial JSON still
    // produce a sensible voice.
    Membrum::DefaultKit::applyTemplate(cfg, Membrum::DrumTemplate::Kick);

    auto getFloat = [&](const char* key, float& target) {
        if (j.contains(key) && j.at(key).is_number()) {
            target = j.at(key).get<float>();
        }
    };
    auto getInt = [&](const char* key, auto& target) {
        if (j.contains(key) && j.at(key).is_number_integer()) {
            target = static_cast<std::remove_reference_t<decltype(target)>>(j.at(key).get<int>());
        }
    };

    if (j.contains("exciterType") && j["exciterType"].is_number_integer()) {
        cfg.exciterType = static_cast<Membrum::ExciterType>(j["exciterType"].get<int>());
    }
    if (j.contains("bodyModel") && j["bodyModel"].is_number_integer()) {
        cfg.bodyModel = static_cast<Membrum::BodyModelType>(j["bodyModel"].get<int>());
    }

    getFloat("material",       cfg.material);
    getFloat("size",           cfg.size);
    getFloat("decay",          cfg.decay);
    getFloat("strikePosition", cfg.strikePosition);
    getFloat("level",          cfg.level);

    getFloat("tsFilterType",       cfg.tsFilterType);
    getFloat("tsFilterCutoff",     cfg.tsFilterCutoff);
    getFloat("tsFilterResonance",  cfg.tsFilterResonance);
    getFloat("tsFilterEnvAmount",  cfg.tsFilterEnvAmount);
    getFloat("tsDriveAmount",      cfg.tsDriveAmount);
    getFloat("tsFoldAmount",       cfg.tsFoldAmount);
    getFloat("tsPitchEnvStart",    cfg.tsPitchEnvStart);
    getFloat("tsPitchEnvEnd",      cfg.tsPitchEnvEnd);
    getFloat("tsPitchEnvTime",     cfg.tsPitchEnvTime);
    getFloat("tsPitchEnvCurve",    cfg.tsPitchEnvCurve);
    getFloat("tsFilterEnvAttack",  cfg.tsFilterEnvAttack);
    getFloat("tsFilterEnvDecay",   cfg.tsFilterEnvDecay);
    getFloat("tsFilterEnvSustain", cfg.tsFilterEnvSustain);
    getFloat("tsFilterEnvRelease", cfg.tsFilterEnvRelease);

    getFloat("modeStretch",       cfg.modeStretch);
    getFloat("decaySkew",         cfg.decaySkew);
    getFloat("modeInjectAmount",  cfg.modeInjectAmount);
    getFloat("nonlinearCoupling", cfg.nonlinearCoupling);

    getFloat("morphEnabled",   cfg.morphEnabled);
    getFloat("morphStart",     cfg.morphStart);
    getFloat("morphEnd",       cfg.morphEnd);
    getFloat("morphDuration",  cfg.morphDuration);
    getFloat("morphCurve",     cfg.morphCurve);

    getInt  ("chokeGroup",     cfg.chokeGroup);
    getInt  ("outputBus",      cfg.outputBus);

    getFloat("fmRatio",            cfg.fmRatio);
    getFloat("feedbackAmount",     cfg.feedbackAmount);
    getFloat("noiseBurstDuration", cfg.noiseBurstDuration);
    getFloat("frictionPressure",   cfg.frictionPressure);

    getFloat("couplingAmount",     cfg.couplingAmount);

    getFloat("macroTightness",  cfg.macroTightness);
    getFloat("macroBrightness", cfg.macroBrightness);
    getFloat("macroBodySize",   cfg.macroBodySize);
    getFloat("macroPunch",      cfg.macroPunch);
    getFloat("macroComplexity", cfg.macroComplexity);

    // D1b (06-orchestralKit-fix-plan.md): the loader stopped at the macro
    // block and silently dropped every field below -- so renders always used
    // the Kick-template noise/click layers, damping overrides, airLoading,
    // wireCoupling, secondary shell, tension mod, and pitch-env knee no
    // matter what the JSON said. Forward the FULL PadConfig surface; fields
    // absent from the JSON still inherit the Kick-template seed above.
    getFloat("noiseLayerMix",       cfg.noiseLayerMix);
    getFloat("noiseLayerCutoff",    cfg.noiseLayerCutoff);
    getFloat("noiseLayerResonance", cfg.noiseLayerResonance);
    getFloat("noiseLayerDecay",     cfg.noiseLayerDecay);
    getFloat("noiseLayerColor",     cfg.noiseLayerColor);
    getFloat("noiseLayerGain",      cfg.noiseLayerGain);
    getFloat("wireCoupling",        cfg.wireCoupling);

    getFloat("clickLayerMix",        cfg.clickLayerMix);
    getFloat("clickLayerContactMs",  cfg.clickLayerContactMs);
    getFloat("clickLayerBrightness", cfg.clickLayerBrightness);

    getFloat("bodyDampingB1", cfg.bodyDampingB1);
    getFloat("bodyDampingB3", cfg.bodyDampingB3);
    getFloat("airLoading",    cfg.airLoading);
    getFloat("modeScatter",   cfg.modeScatter);

    getFloat("couplingStrength",  cfg.couplingStrength);
    getFloat("secondaryEnabled",  cfg.secondaryEnabled);
    getFloat("secondarySize",     cfg.secondarySize);
    getFloat("secondaryMaterial", cfg.secondaryMaterial);
    getFloat("tensionModAmt",     cfg.tensionModAmt);

    getFloat("enabled", cfg.enabled);
    getFloat("tsPitchEnvKneeEnabled", cfg.tsPitchEnvKneeEnabled);
    getFloat("tsPitchEnvMidPitch",    cfg.tsPitchEnvMidPitch);
    getFloat("tsPitchEnvMidFraction", cfg.tsPitchEnvMidFraction);
    getFloat("tsPitchEnvCurve2",      cfg.tsPitchEnvCurve2);
    getFloat("pan", cfg.pan);
    return cfg;
}

bool writeWav(const std::filesystem::path& outPath,
              const std::vector<float>& samples,
              unsigned sampleRate) {
    drwav_data_format fmt{};
    fmt.container = drwav_container_riff;
    fmt.format    = DR_WAVE_FORMAT_IEEE_FLOAT;
    fmt.channels  = 1;
    fmt.sampleRate = sampleRate;
    fmt.bitsPerSample = 32;

    drwav w;
    if (!drwav_init_file_write(&w, outPath.string().c_str(), &fmt, nullptr)) {
        return false;
    }
    const drwav_uint64 frames = drwav_write_pcm_frames(&w, samples.size(), samples.data());
    drwav_uninit(&w);
    return frames == samples.size();
}

int usage() {
    std::fprintf(stderr,
                 "membrum-fit-gen <input.json> <output.wav> [--sr 44100] [--sec 2.0] [--vel 1.0]\n");
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    std::filesystem::path inPath  = argv[1];
    std::filesystem::path outPath = argv[2];
    double sampleRate = 44100.0;
    double lengthSec  = 2.0;
    double velocity   = 1.0;
    for (int i = 3; i + 1 < argc; i += 2) {
        std::string flag = argv[i];
        if (flag == "--sr")  sampleRate = std::stod(argv[i+1]);
        else if (flag == "--sec") lengthSec = std::stod(argv[i+1]);
        else if (flag == "--vel") velocity  = std::stod(argv[i+1]);
    }

    std::ifstream fin(inPath);
    if (!fin) {
        std::fprintf(stderr, "Cannot open %s\n", inPath.string().c_str());
        return 1;
    }
    json j;
    fin >> j;

    const Membrum::PadConfig cfg = padConfigFromJson(j);

    MembrumFit::RenderableMembrumVoice voice;
    voice.prepare(sampleRate);
    const auto samples = voice.renderToVector(cfg, static_cast<float>(velocity), static_cast<float>(lengthSec));

    if (!writeWav(outPath, samples, static_cast<unsigned>(sampleRate))) {
        std::fprintf(stderr, "Failed to write %s\n", outPath.string().c_str());
        return 1;
    }
    std::fprintf(stdout, "wrote %zu frames to %s\n", samples.size(), outPath.string().c_str());
    return 0;
}
