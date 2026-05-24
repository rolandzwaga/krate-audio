// ==============================================================================
// gen_v2_fixtures shared utilities (spec 142, Phase 1)
// ==============================================================================

#include "common.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <iostream>

namespace KrateFixtures {

void writeGoldenMidi(const std::filesystem::path& outPath,
                     const std::vector<CapturedMidi>& events) {
    // Binary mode: prevents Windows CRLF translation so byte-identical golden
    // files compare equal across platforms.
    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::cerr << "ERROR: cannot open " << outPath.string() << " for write\n";
        return;
    }
    for (const auto& e : events) {
        char buf[128];
        if (e.isNoteOn) {
            std::snprintf(buf, sizeof(buf), "[%lld] noteOn  %d %d\n",
                static_cast<long long>(e.absoluteSample),
                static_cast<int>(e.pitch),
                e.velocity);
        } else {
            std::snprintf(buf, sizeof(buf), "[%lld] noteOff %d\n",
                static_cast<long long>(e.absoluteSample),
                static_cast<int>(e.pitch));
        }
        out << buf;
    }
}

std::string sanitizeForFilename(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') {
            out += c;
        } else if (c == ' ') {
            out += '_';
        } else {
            out += '_';
        }
    }
    return out;
}

}  // namespace KrateFixtures
