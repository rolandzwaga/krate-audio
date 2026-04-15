#include "json_writer.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace MembrumFit::PresetIO {

namespace {
nlohmann::json padToJson(int padIndex, const Membrum::PadConfig& p) {
    return {
        {"padIndex", padIndex},
        {"exciterType", static_cast<int>(p.exciterType)},
        {"bodyModel", static_cast<int>(p.bodyModel)},
        {"material", p.material},
        {"size", p.size},
        {"decay", p.decay},
        {"strikePosition", p.strikePosition},
        {"level", p.level},
        {"tsFilterType", p.tsFilterType},
        {"tsFilterCutoff", p.tsFilterCutoff},
        {"tsFilterResonance", p.tsFilterResonance},
        {"tsFilterEnvAmount", p.tsFilterEnvAmount},
        {"tsDriveAmount", p.tsDriveAmount},
        {"tsFoldAmount", p.tsFoldAmount},
        {"tsPitchEnvStart", p.tsPitchEnvStart},
        {"tsPitchEnvEnd", p.tsPitchEnvEnd},
        {"tsPitchEnvTime", p.tsPitchEnvTime},
        {"tsPitchEnvCurve", p.tsPitchEnvCurve},
        {"tsFilterEnvAttack", p.tsFilterEnvAttack},
        {"tsFilterEnvDecay", p.tsFilterEnvDecay},
        {"tsFilterEnvSustain", p.tsFilterEnvSustain},
        {"tsFilterEnvRelease", p.tsFilterEnvRelease},
        {"modeStretch", p.modeStretch},
        {"decaySkew", p.decaySkew},
        {"modeInjectAmount", p.modeInjectAmount},
        {"nonlinearCoupling", p.nonlinearCoupling},
        {"morphEnabled", p.morphEnabled},
        {"morphStart", p.morphStart},
        {"morphEnd", p.morphEnd},
        {"morphDuration", p.morphDuration},
        {"morphCurve", p.morphCurve},
        {"chokeGroup", p.chokeGroup},
        {"outputBus", p.outputBus},
        {"fmRatio", p.fmRatio},
        {"feedbackAmount", p.feedbackAmount},
        {"noiseBurstDuration", p.noiseBurstDuration},
        {"frictionPressure", p.frictionPressure},
        {"couplingAmount", p.couplingAmount},
        {"macros", {
            {"tightness", p.macroTightness},
            {"brightness", p.macroBrightness},
            {"bodySize", p.macroBodySize},
            {"punch", p.macroPunch},
            {"complexity", p.macroComplexity},
        }},
    };
}
}  // namespace

bool writeKitJson(const std::filesystem::path& outputPath,
                  const std::array<Membrum::PadConfig, 32>& pads,
                  const std::string& presetName) {
    nlohmann::json j;
    j["format_version"] = 6;
    j["name"] = presetName;
    j["pads"] = nlohmann::json::array();
    for (int i = 0; i < 32; ++i) j["pads"].push_back(padToJson(i, pads[static_cast<std::size_t>(i)]));
    j["couplingOverrides"] = nlohmann::json::array();
    std::ofstream f(outputPath);
    if (!f) return false;
    f << j.dump(2);
    return true;
}

bool writePadJson(const std::filesystem::path& outputPath,
                  const Membrum::PadConfig& pad,
                  const std::string& presetName) {
    nlohmann::json j = padToJson(0, pad);
    j["name"] = presetName;
    j["format_version"] = 1;
    std::ofstream f(outputPath);
    if (!f) return false;
    f << j.dump(2);
    return true;
}

}  // namespace MembrumFit::PresetIO
