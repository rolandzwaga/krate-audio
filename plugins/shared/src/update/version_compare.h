#pragma once

// ==============================================================================
// SemVer - Semantic Version Parsing and Comparison
// ==============================================================================
// Header-only utility for parsing and comparing semantic version strings.
// Uses std::from_chars for zero-allocation parsing.
// ==============================================================================

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Krate::Plugins {

struct SemVer {
    uint32_t major = 0;
    uint32_t minor = 0;
    uint32_t patch = 0;

    /// Parse a version string like "1.2.3". Returns nullopt on failure.
    static std::optional<SemVer> parse(std::string_view str) {
        SemVer ver;

        // Parse major
        auto [p1, ec1] = std::from_chars(str.data(), str.data() + str.size(), ver.major);
        if (ec1 != std::errc{} || p1 == str.data() + str.size() || *p1 != '.')
            return std::nullopt;

        // Parse minor
        ++p1; // skip '.'
        auto [p2, ec2] = std::from_chars(p1, str.data() + str.size(), ver.minor);
        if (ec2 != std::errc{} || p2 == str.data() + str.size() || *p2 != '.')
            return std::nullopt;

        // Parse patch
        ++p2; // skip '.'
        auto [p3, ec3] = std::from_chars(p2, str.data() + str.size(), ver.patch);
        if (ec3 != std::errc{})
            return std::nullopt;

        // Reject trailing characters (except whitespace)
        for (auto* c = p3; c != str.data() + str.size(); ++c) {
            if (*c != ' ' && *c != '\t' && *c != '\r' && *c != '\n')
                return std::nullopt;
        }

        return ver;
    }

    std::string toString() const {
        return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
    }

    bool operator==(const SemVer& other) const {
        return major == other.major && minor == other.minor && patch == other.patch;
    }

    bool operator!=(const SemVer& other) const { return !(*this == other); }

    bool operator<(const SemVer& other) const {
        if (major != other.major) return major < other.major;
        if (minor != other.minor) return minor < other.minor;
        return patch < other.patch;
    }

    bool operator>(const SemVer& other) const { return other < *this; }
    bool operator<=(const SemVer& other) const { return !(other < *this); }
    bool operator>=(const SemVer& other) const { return !(*this < other); }
};

} // namespace Krate::Plugins
