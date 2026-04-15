#include "sfz_ingest.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace MembrumFit::Ingestion {

namespace {

std::string trim(const std::string& s) {
    std::size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    std::size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// Convert an SFZ key field (either MIDI number or note name like "c3") to a
// MIDI note. Returns -1 on parse failure.
int parseKey(const std::string& v) {
    if (v.empty()) return -1;
    try {
        return std::stoi(v);
    } catch (...) {
        // Parse note name: e.g. "c3", "F#2", "Bb4".
        std::string s = v;
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        const std::string pcNames = "c d ef g a b";  // noop layout
        (void)pcNames;
        static const std::map<char, int> pc = {
            {'c', 0}, {'d', 2}, {'e', 4}, {'f', 5},
            {'g', 7}, {'a', 9}, {'b', 11}
        };
        if (s.empty()) return -1;
        const auto it = pc.find(s[0]);
        if (it == pc.end()) return -1;
        int semis = it->second;
        std::size_t i = 1;
        if (i < s.size() && s[i] == '#')      { semis += 1; ++i; }
        else if (i < s.size() && s[i] == 'b') { semis -= 1; ++i; }
        if (i >= s.size()) return -1;
        int octave = 0;
        try { octave = std::stoi(s.substr(i)); }
        catch (...) { return -1; }
        return 12 * (octave + 1) + semis;
    }
}

struct Region {
    std::string sample;
    int key = -1;
    int loKey = -1;
    int hiKey = -1;
    int pitchKeycenter = -1;
    int loVel = 0;
    int hiVel = 127;
};

// Parse key=value tokens out of a flat text stream. SFZ headers like <region>
// separate regions; anything between <region> and the next <...> is a region.
std::vector<Region> parseRegions(const std::string& text) {
    std::vector<Region> out;
    Region current;
    bool inRegion = false;
    std::size_t i = 0;
    auto skipSpaces = [&]() {
        while (i < text.size() && (std::isspace(static_cast<unsigned char>(text[i])) || text[i] == '\t'))
            ++i;
    };
    while (i < text.size()) {
        skipSpaces();
        if (i >= text.size()) break;
        // Comments
        if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            while (i < text.size() && text[i] != '\n') ++i;
            continue;
        }
        // Headers
        if (text[i] == '<') {
            const auto end = text.find('>', i);
            if (end == std::string::npos) break;
            const std::string header = text.substr(i + 1, end - i - 1);
            if (header == "region") {
                if (inRegion) out.push_back(current);
                current = Region{};
                inRegion = true;
            } else {
                // group/global/control/etc. -- close current region.
                if (inRegion) out.push_back(current);
                inRegion = false;
            }
            i = end + 1;
            continue;
        }
        // Opcode
        const auto eq = text.find('=', i);
        if (eq == std::string::npos) break;
        const std::string key = trim(text.substr(i, eq - i));
        // Value continues until whitespace that begins a new opcode or header.
        std::size_t j = eq + 1;
        // Skip leading spaces
        while (j < text.size() && (text[j] == ' ' || text[j] == '\t')) ++j;
        const std::size_t valStart = j;
        while (j < text.size() && text[j] != '\n' && text[j] != '<') {
            // Peek: if we hit ' word=' that's the next opcode.
            if (text[j] == ' ') {
                std::size_t k = j + 1;
                while (k < text.size() && text[k] == ' ') ++k;
                std::size_t eq2 = k;
                while (eq2 < text.size() && text[eq2] != '=' && text[eq2] != ' '
                       && text[eq2] != '\n' && text[eq2] != '<') ++eq2;
                if (eq2 < text.size() && text[eq2] == '=') break;
            }
            ++j;
        }
        const std::string val = trim(text.substr(valStart, j - valStart));
        if (inRegion) {
            if      (key == "sample")            current.sample = val;
            else if (key == "key")               current.key    = parseKey(val);
            else if (key == "lokey")             current.loKey  = parseKey(val);
            else if (key == "hikey")             current.hiKey  = parseKey(val);
            else if (key == "pitch_keycenter")   current.pitchKeycenter = parseKey(val);
            else if (key == "lovel")             current.loVel  = std::max(0, std::stoi(val));
            else if (key == "hivel")             current.hiVel  = std::min(127, std::stoi(val));
        }
        i = j;
    }
    if (inRegion) out.push_back(current);
    return out;
}

}  // namespace

KitSpec loadKitSFZ(const std::filesystem::path& sfzPath) {
    KitSpec spec;
    spec.rootDir = sfzPath.parent_path();

    std::ifstream in(sfzPath);
    if (!in) return spec;
    std::stringstream ss;
    ss << in.rdbuf();
    const auto regions = parseRegions(ss.str());

    // For each region, if it maps to a MIDI note in [36, 67], pick the HIGHEST
    // velocity layer as the v1 representative (spec §1.2 / §4.6: v1 picks the
    // loudest layer per note; multi-velocity is Phase 5 deferred).
    std::map<int, Region> picks;
    for (const auto& r : regions) {
        const int midiNote = (r.key >= 0) ? r.key
                           : (r.loKey >= 0 && r.hiKey >= r.loKey) ? r.loKey
                           : r.pitchKeycenter;
        if (midiNote < 36 || midiNote > 67) continue;
        if (r.sample.empty()) continue;
        const auto it = picks.find(midiNote);
        if (it == picks.end() || r.hiVel > it->second.hiVel) {
            picks[midiNote] = r;
        }
    }
    for (const auto& [note, r] : picks) {
        std::filesystem::path p = r.sample;
        if (p.is_relative()) p = spec.rootDir / p;
        spec.midiNoteToFile[note] = p;
    }
    return spec;
}

}  // namespace MembrumFit::Ingestion
