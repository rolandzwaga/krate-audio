// ==============================================================================
// uidesc_reachability.h -- assert every registered param is bound in the editor
// ==============================================================================
// A registered VST3 parameter that no control in editor.uidesc binds is invisible
// to the user (silently unautomatable from the UI). This helper parses the uidesc
// XML directly (NOT the live view tree -- a UIViewSwitchContainer only realizes the
// active template, so a walk misses controls on inactive templates) and reports
// registered parameter IDs that are not reachable from any control on any template.
//
// It resolves each control's control-tag="NAME" reference through the
// <control-tag name="NAME" tag="ID"/> map, so a param is reachable if some control
// references a name that maps to its ID. Per-plugin intentionally-hidden params
// (e.g. a UI-mode selector) are passed in as an allowlist.
//
// APPLICABILITY / LIMITATION: this only sees params bound through a control-tag.
// Params driven by a CUSTOM VIEW that edits them programmatically (a step-sequencer
// lane, an XY/morph pad, a mod matrix, Membrum's pad grid) have no control-tag and
// will be reported as "unreachable" even though the UI does expose them. Each
// per-plugin test must pass those params in via the allowlist. For a plugin that is
// mostly standard knobs/menus the allowlist is small; for a custom-view-heavy plugin
// (e.g. the Gradus step sequencer, or Membrum) it is large — for those, a tag-based
// check is the wrong tool and a bespoke test (like Membrum's proxy-aware scanner) is
// warranted instead. Do not point Membrum at this helper.
//
// Usage per plugin:
//   auto ids = enumerate Controller::getParameterInfo(...).id;
//   auto xml = read <PLUGIN>_RESOURCES_DIR "/editor.uidesc";
//   auto missing = Krate::Test::unreachableParams(xml, ids, allowlistOfCustomViewParams);
//   CHECK(missing.empty());
// ==============================================================================
#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace Krate::Test {

// name -> numeric tag (== ParamID) from every <control-tag name="..." tag="NNN"/>.
inline std::map<std::string, int> extractControlTagMap(const std::string& xml) {
    std::map<std::string, int> map;
    const std::string marker = "<control-tag";
    std::size_t pos = 0;
    while ((pos = xml.find(marker, pos)) != std::string::npos) {
        const std::size_t tagEnd = xml.find('>', pos);
        if (tagEnd == std::string::npos) break;
        const std::string el = xml.substr(pos, tagEnd - pos);
        const auto namePos = el.find("name=\"");
        const auto tagPos = el.find("tag=\"");
        if (namePos != std::string::npos && tagPos != std::string::npos) {
            const auto ns = namePos + 6;
            const auto ne = el.find('"', ns);
            const auto ts = tagPos + 5;
            const auto te = el.find('"', ts);
            if (ne != std::string::npos && te != std::string::npos) {
                const std::string name = el.substr(ns, ne - ns);
                try {
                    map[name] = std::stoi(el.substr(ts, te - ts));
                } catch (...) {
                    // non-numeric tag (e.g. a symbolic constant) -> skip
                }
            }
        }
        pos = tagEnd + 1;
    }
    return map;
}

// Every control-tag="NAME" reference anywhere in the description (all templates).
inline std::set<std::string> extractReferencedTagNames(const std::string& xml) {
    std::set<std::string> names;
    const std::string marker = "control-tag=\"";
    std::size_t pos = 0;
    while ((pos = xml.find(marker, pos)) != std::string::npos) {
        const auto ns = pos + marker.size();
        const auto ne = xml.find('"', ns);
        if (ne == std::string::npos) break;
        names.insert(xml.substr(ns, ne - ns));
        pos = ne + 1;
    }
    return names;
}

// Registered param IDs that no control binds (excluding the allowlist), sorted.
inline std::vector<int> unreachableParams(const std::string& uidescXml,
                                          const std::vector<int>& registeredIds,
                                          const std::set<int>& allowlist = {}) {
    const auto tagMap = extractControlTagMap(uidescXml);
    const auto refNames = extractReferencedTagNames(uidescXml);

    std::set<int> boundIds;
    for (const auto& name : refNames) {
        auto it = tagMap.find(name);
        if (it != tagMap.end()) boundIds.insert(it->second);
    }

    std::vector<int> unreachable;
    for (int id : registeredIds) {
        if (boundIds.count(id) == 0 && allowlist.count(id) == 0) unreachable.push_back(id);
    }
    std::sort(unreachable.begin(), unreachable.end());
    return unreachable;
}

}  // namespace Krate::Test
