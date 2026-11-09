// =============================================================================
// editor.uidesc control inventory (fix plan T2.22, D-069)
// =============================================================================
// Every automatable sweep parameter and the selected node's Bias must have a
// control in the editor: a view whose control-tag names it and that is not a
// readout (CTextLabel / CParamDisplay). Before T2.22 seven sweep parameters and
// Band.NodeBias had none, so only a host's generic parameter list reached them.
// =============================================================================

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string readUidesc() {
    std::ifstream in(std::string(DISRUMPO_RESOURCES_DIR) + "/editor.uidesc", std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/// Value of attribute `name` inside one element's text, "" when absent.
std::string attributeOf(const std::string& element, const std::string& name) {
    const std::string key = " " + name + "=\"";
    const auto pos = element.find(key);
    if (pos == std::string::npos) return {};
    const auto start = pos + key.size();
    const auto end = element.find('"', start);
    return end == std::string::npos ? std::string{} : element.substr(start, end - start);
}

/// control-tag name -> number of interactive <view> elements bound to it.
std::map<std::string, int> interactiveBindings(const std::string& xml) {
    std::map<std::string, int> bindings;
    size_t pos = 0;
    while ((pos = xml.find("<view ", pos)) != std::string::npos) {
        const auto end = xml.find('>', pos);
        if (end == std::string::npos) break;
        const std::string element = xml.substr(pos, end - pos);
        pos = end;
        const std::string tag = attributeOf(element, "control-tag");
        const std::string cls = attributeOf(element, "class");
        if (tag.empty() || cls == "CTextLabel" || cls == "CParamDisplay") continue;
        ++bindings[tag];
    }
    return bindings;
}

}  // namespace

TEST_CASE("Every automatable sweep parameter and Band.NodeBias has an editor control",
          "[disrumpo][ui][uidesc][D-069]") {
    const std::string xml = readUidesc();
    REQUIRE_FALSE(xml.empty());
    const auto bindings = interactiveBindings(xml);

    const std::vector<std::string> required = {
        "SweepEnable", "SweepFrequency", "SweepWidth", "SweepIntensity", "SweepFalloff", "SweepMorphLink",
        "SweepLFOEnable", "SweepLFORate", "SweepLFOWaveform", "SweepLFODepth", "SweepLFOSync",
        "SweepLFONoteValue", "SweepEnvEnable", "SweepEnvAttack", "SweepEnvRelease", "SweepEnvSensitivity",
        "Band.NodeBias",
    };
    std::vector<std::string> missing;
    for (const auto& name : required) {
        const auto it = bindings.find(name);
        if (it == bindings.end() || it->second == 0) missing.push_back(name);
    }
    std::string list;
    for (const auto& m : missing) list += m + " ";
    INFO("tags without an interactive control: " << list);
    REQUIRE(missing.empty());
}
