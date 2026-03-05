#pragma once

// ==============================================================================
// Harmonic Snapshot JSON Serialization
// ==============================================================================
// Plugin-local JSON utilities for human-readable export/import of
// HarmonicSnapshot data. Uses manual string formatting (no external JSON
// library dependency).
//
// Feature: 119-harmonic-memory
// Requirements: FR-024, FR-025, FR-026, FR-027
// ==============================================================================

#include <krate/dsp/processors/harmonic_snapshot.h>
#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/processors/residual_types.h>

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <string>

namespace Innexus {

/// @brief Export a HarmonicSnapshot to a human-readable JSON string (FR-024, FR-027).
///
/// Writes only numPartials entries for per-partial arrays for readability.
/// Always includes "version": 1 for future format evolution.
///
/// @param snap The snapshot to export
/// @return JSON string representation
inline std::string snapshotToJson(const Krate::DSP::HarmonicSnapshot& snap)
{
    std::ostringstream os;
    os << std::setprecision(9);

    os << "{\n";
    os << "    \"version\": 1,\n";
    os << "    \"f0Reference\": " << snap.f0Reference << ",\n";
    os << "    \"numPartials\": " << snap.numPartials << ",\n";

    const int n = std::clamp(snap.numPartials, 0,
                             static_cast<int>(Krate::DSP::kMaxPartials));

    // Helper lambda for writing float arrays
    auto writeArray = [&](const char* name, const auto& arr, int count,
                          bool trailingComma) {
        os << "    \"" << name << "\": [";
        for (int i = 0; i < count; ++i)
        {
            if (i > 0) os << ", ";
            os << arr[static_cast<size_t>(i)];
        }
        os << "]";
        if (trailingComma) os << ",";
        os << "\n";
    };

    writeArray("relativeFreqs", snap.relativeFreqs, n, true);
    writeArray("normalizedAmps", snap.normalizedAmps, n, true);
    writeArray("phases", snap.phases, n, true);
    writeArray("inharmonicDeviation", snap.inharmonicDeviation, n, true);
    writeArray("residualBands", snap.residualBands,
               static_cast<int>(Krate::DSP::kResidualBands), true);

    os << "    \"residualEnergy\": " << snap.residualEnergy << ",\n";
    os << "    \"globalAmplitude\": " << snap.globalAmplitude << ",\n";
    os << "    \"spectralCentroid\": " << snap.spectralCentroid << ",\n";
    os << "    \"brightness\": " << snap.brightness << "\n";
    os << "}";

    return os.str();
}

// ==============================================================================
// JSON Parsing Helpers (simple, no external library)
// ==============================================================================
namespace detail {

/// Skip whitespace in json string starting at pos.
inline void skipWhitespace(const std::string& json, size_t& pos)
{
    while (pos < json.size() &&
           (json[pos] == ' ' || json[pos] == '\t' ||
            json[pos] == '\n' || json[pos] == '\r'))
    {
        ++pos;
    }
}

/// Try to match and consume a specific character.
inline bool expectChar(const std::string& json, size_t& pos, char c)
{
    skipWhitespace(json, pos);
    if (pos >= json.size() || json[pos] != c) return false;
    ++pos;
    return true;
}

/// Parse a JSON string key (between double quotes). Returns the key.
inline bool parseString(const std::string& json, size_t& pos, std::string& out)
{
    skipWhitespace(json, pos);
    if (pos >= json.size() || json[pos] != '"') return false;
    ++pos; // skip opening quote

    out.clear();
    while (pos < json.size() && json[pos] != '"')
    {
        if (json[pos] == '\\')
        {
            ++pos;
            if (pos >= json.size()) return false;
        }
        out += json[pos];
        ++pos;
    }
    if (pos >= json.size()) return false;
    ++pos; // skip closing quote
    return true;
}

/// Parse a number (int or float) from json at pos.
inline bool parseNumber(const std::string& json, size_t& pos, double& out)
{
    skipWhitespace(json, pos);
    if (pos >= json.size()) return false;

    size_t startPos = pos;

    // Handle negative
    if (json[pos] == '-') ++pos;

    // Must have at least one digit
    if (pos >= json.size() || (json[pos] < '0' || json[pos] > '9'))
        return false;

    // Integer part
    while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') ++pos;

    // Fractional part
    if (pos < json.size() && json[pos] == '.')
    {
        ++pos;
        while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9')
            ++pos;
    }

    // Exponent
    if (pos < json.size() && (json[pos] == 'e' || json[pos] == 'E'))
    {
        ++pos;
        if (pos < json.size() && (json[pos] == '+' || json[pos] == '-'))
            ++pos;
        while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9')
            ++pos;
    }

    std::string numStr = json.substr(startPos, pos - startPos);
    try
    {
        out = std::stod(numStr);
    }
    catch (...)
    {
        return false;
    }
    return true;
}

/// Parse a JSON array of numbers.
inline bool parseNumberArray(const std::string& json, size_t& pos,
                             std::vector<double>& out)
{
    skipWhitespace(json, pos);
    if (!expectChar(json, pos, '[')) return false;

    out.clear();
    skipWhitespace(json, pos);

    // Handle empty array
    if (pos < json.size() && json[pos] == ']')
    {
        ++pos;
        return true;
    }

    while (true)
    {
        double val = 0.0;
        if (!parseNumber(json, pos, val)) return false;
        out.push_back(val);

        skipWhitespace(json, pos);
        if (pos >= json.size()) return false;
        if (json[pos] == ']') { ++pos; return true; }
        if (json[pos] != ',') return false;
        ++pos; // skip comma
    }
}

/// Skip a JSON value (number, string, array, object, bool, null).
inline bool skipValue(const std::string& json, size_t& pos)
{
    skipWhitespace(json, pos);
    if (pos >= json.size()) return false;

    char c = json[pos];
    if (c == '"')
    {
        std::string dummy;
        return parseString(json, pos, dummy);
    }
    if (c == '[')
    {
        ++pos;
        int depth = 1;
        while (pos < json.size() && depth > 0)
        {
            if (json[pos] == '[') ++depth;
            else if (json[pos] == ']') --depth;
            else if (json[pos] == '"')
            {
                // Skip string contents
                ++pos;
                while (pos < json.size() && json[pos] != '"')
                {
                    if (json[pos] == '\\') ++pos;
                    ++pos;
                }
            }
            ++pos;
        }
        return depth == 0;
    }
    if (c == '{')
    {
        ++pos;
        int depth = 1;
        while (pos < json.size() && depth > 0)
        {
            if (json[pos] == '{') ++depth;
            else if (json[pos] == '}') --depth;
            else if (json[pos] == '"')
            {
                ++pos;
                while (pos < json.size() && json[pos] != '"')
                {
                    if (json[pos] == '\\') ++pos;
                    ++pos;
                }
            }
            ++pos;
        }
        return depth == 0;
    }
    // Number, bool, null
    while (pos < json.size() && json[pos] != ',' && json[pos] != '}' &&
           json[pos] != ']' && json[pos] != ' ' && json[pos] != '\n' &&
           json[pos] != '\r' && json[pos] != '\t')
    {
        ++pos;
    }
    return true;
}

} // namespace detail

/// @brief Import a HarmonicSnapshot from a JSON string (FR-025, FR-026, FR-027).
///
/// Validates: version == 1, numPartials in [0, 48], all required arrays present
/// with correct lengths, no negative amplitudes. On success, zero-pads per-partial
/// arrays to kMaxPartials. Returns false on any validation failure without
/// modifying `out`.
///
/// @param json The JSON string to parse
/// @param[out] out The snapshot to populate (unchanged on failure)
/// @return true if parse succeeded, false on any validation failure
inline bool jsonToSnapshot(const std::string& json,
                           Krate::DSP::HarmonicSnapshot& out)
{
    if (json.empty()) return false;

    size_t pos = 0;
    if (!detail::expectChar(json, pos, '{')) return false;

    // Temporary storage for parsed fields
    bool hasVersion = false;
    int version = 0;
    bool hasF0 = false;
    double f0Reference = 0.0;
    bool hasNumPartials = false;
    int numPartials = 0;
    bool hasRelativeFreqs = false;
    std::vector<double> relativeFreqs;
    bool hasNormalizedAmps = false;
    std::vector<double> normalizedAmps;
    bool hasPhases = false;
    std::vector<double> phases;
    bool hasInharmonicDeviation = false;
    std::vector<double> inharmonicDeviation;
    bool hasResidualBands = false;
    std::vector<double> residualBands;
    bool hasResidualEnergy = false;
    double residualEnergy = 0.0;
    bool hasGlobalAmplitude = false;
    double globalAmplitude = 0.0;
    bool hasSpectralCentroid = false;
    double spectralCentroid = 0.0;
    bool hasBrightness = false;
    double brightness = 0.0;

    // Parse key-value pairs
    while (true)
    {
        detail::skipWhitespace(json, pos);
        if (pos >= json.size()) return false;
        if (json[pos] == '}') break;

        // Parse key
        std::string key;
        if (!detail::parseString(json, pos, key)) return false;
        if (!detail::expectChar(json, pos, ':')) return false;

        // Dispatch by key
        if (key == "version")
        {
            double val = 0.0;
            if (!detail::parseNumber(json, pos, val)) return false;
            version = static_cast<int>(val);
            hasVersion = true;
        }
        else if (key == "f0Reference")
        {
            if (!detail::parseNumber(json, pos, f0Reference)) return false;
            hasF0 = true;
        }
        else if (key == "numPartials")
        {
            double val = 0.0;
            if (!detail::parseNumber(json, pos, val)) return false;
            numPartials = static_cast<int>(val);
            hasNumPartials = true;
        }
        else if (key == "relativeFreqs")
        {
            if (!detail::parseNumberArray(json, pos, relativeFreqs)) return false;
            hasRelativeFreqs = true;
        }
        else if (key == "normalizedAmps")
        {
            if (!detail::parseNumberArray(json, pos, normalizedAmps)) return false;
            hasNormalizedAmps = true;
        }
        else if (key == "phases")
        {
            if (!detail::parseNumberArray(json, pos, phases)) return false;
            hasPhases = true;
        }
        else if (key == "inharmonicDeviation")
        {
            if (!detail::parseNumberArray(json, pos, inharmonicDeviation))
                return false;
            hasInharmonicDeviation = true;
        }
        else if (key == "residualBands")
        {
            if (!detail::parseNumberArray(json, pos, residualBands)) return false;
            hasResidualBands = true;
        }
        else if (key == "residualEnergy")
        {
            if (!detail::parseNumber(json, pos, residualEnergy)) return false;
            hasResidualEnergy = true;
        }
        else if (key == "globalAmplitude")
        {
            if (!detail::parseNumber(json, pos, globalAmplitude)) return false;
            hasGlobalAmplitude = true;
        }
        else if (key == "spectralCentroid")
        {
            if (!detail::parseNumber(json, pos, spectralCentroid)) return false;
            hasSpectralCentroid = true;
        }
        else if (key == "brightness")
        {
            if (!detail::parseNumber(json, pos, brightness)) return false;
            hasBrightness = true;
        }
        else
        {
            // Unknown key -- skip its value
            if (!detail::skipValue(json, pos)) return false;
        }

        // Expect comma or end of object
        detail::skipWhitespace(json, pos);
        if (pos < json.size() && json[pos] == ',')
            ++pos;
    }

    // --- Validation ---

    // FR-027: version must be present and == 1
    if (!hasVersion || version != 1) return false;

    // Required fields
    if (!hasF0 || !hasNumPartials) return false;
    if (!hasRelativeFreqs || !hasNormalizedAmps) return false;
    if (!hasPhases || !hasInharmonicDeviation) return false;
    if (!hasResidualBands || !hasResidualEnergy) return false;
    if (!hasGlobalAmplitude || !hasSpectralCentroid || !hasBrightness)
        return false;

    // FR-026: numPartials must be in range [0, 48]
    if (numPartials < 0 ||
        numPartials > static_cast<int>(Krate::DSP::kMaxPartials))
        return false;

    // Validate array lengths match numPartials
    if (static_cast<int>(relativeFreqs.size()) != numPartials) return false;
    if (static_cast<int>(normalizedAmps.size()) != numPartials) return false;
    if (static_cast<int>(phases.size()) != numPartials) return false;
    if (static_cast<int>(inharmonicDeviation.size()) != numPartials)
        return false;

    // Residual bands must have exactly kResidualBands entries
    if (residualBands.size() != Krate::DSP::kResidualBands) return false;

    // FR-026: no negative amplitudes
    for (double amp : normalizedAmps)
    {
        if (amp < 0.0) return false;
    }

    // --- Populate output snapshot ---
    Krate::DSP::HarmonicSnapshot snap{};

    snap.f0Reference = static_cast<float>(f0Reference);
    snap.numPartials = numPartials;

    for (int i = 0; i < numPartials; ++i)
    {
        auto idx = static_cast<size_t>(i);
        snap.relativeFreqs[idx] = static_cast<float>(relativeFreqs[idx]);
        snap.normalizedAmps[idx] = static_cast<float>(normalizedAmps[idx]);
        snap.phases[idx] = static_cast<float>(phases[idx]);
        snap.inharmonicDeviation[idx] =
            static_cast<float>(inharmonicDeviation[idx]);
    }
    // Remaining entries are zero-initialized by default construction

    for (size_t i = 0; i < Krate::DSP::kResidualBands; ++i)
    {
        snap.residualBands[i] = static_cast<float>(residualBands[i]);
    }

    snap.residualEnergy = static_cast<float>(residualEnergy);
    snap.globalAmplitude = static_cast<float>(globalAmplitude);
    snap.spectralCentroid = static_cast<float>(spectralCentroid);
    snap.brightness = static_cast<float>(brightness);

    out = snap;
    return true;
}

} // namespace Innexus
