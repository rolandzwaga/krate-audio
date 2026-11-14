#pragma once

// =============================================================================
// Preset description layout for the preset browser (FR-065, user ruling 2026-11-12 "wrap + hover")
// =============================================================================
// Pure function, no VSTGUI dependency: the caller supplies the width measure (PresetBrowserView passes the
// description label's font painter, so the layout is in the same units the label draws with on every platform).
// =============================================================================

#include <cstddef>
#include <string>
#include <vector>

namespace Krate::Plugins {

/// U+2026 HORIZONTAL ELLIPSIS, appended to the last line when the description does not fit.
inline constexpr const char* kDescriptionEllipsis = "\xE2\x80\xA6";

/// Lays @p text out in at most @p maxLines lines, each at most @p maxWidth wide as @p measure reports it, breaking at
/// whitespace (runs of it collapse to one space). When words remain after the last line, that line ends with
/// kDescriptionEllipsis: words are dropped from its end (characters only when a single word is too wide) until the
/// line plus the ellipsis fits. A single word wider than @p maxWidth on an earlier line is kept whole. Lines are
/// joined with '\n'; an empty or all-whitespace text gives "".
///
/// @param measure callable `double(const std::string&)` returning the drawn width of a UTF-8 string
template <typename MeasureFn>
[[nodiscard]] std::string wrapDescriptionLines(const std::string& text, double maxWidth, std::size_t maxLines,
                                               const MeasureFn& measure) {
    std::vector<std::string> words;
    {
        std::string word;
        for (const char c : text) {
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                if (!word.empty()) {
                    words.push_back(std::move(word));
                    word.clear();
                }
            } else {
                word += c;
            }
        }
        if (!word.empty()) {
            words.push_back(std::move(word));
        }
    }
    if (words.empty() || maxLines == 0) {
        return {};
    }

    std::vector<std::string> lines;
    std::string current;
    bool truncated = false;
    for (const auto& word : words) {
        if (current.empty()) {
            current = word;
            continue;
        }
        std::string candidate = current;
        candidate += ' ';
        candidate += word;
        if (measure(candidate) <= maxWidth) {
            current = std::move(candidate);
            continue;
        }
        if (lines.size() + 1 == maxLines) {  // `current` is the last line and words remain
            truncated = true;
            break;
        }
        lines.push_back(std::move(current));
        current = word;
    }

    if (truncated) {
        const std::string ellipsis(kDescriptionEllipsis);
        while (!current.empty() && measure(current + ellipsis) > maxWidth) {
            if (const auto space = current.rfind(' '); space != std::string::npos) {
                current.resize(space);
            } else {
                // One UTF-8 code point off the end: continuation bytes, then the lead byte.
                while (!current.empty() && (static_cast<unsigned char>(current.back()) & 0xC0u) == 0x80u) {
                    current.pop_back();
                }
                if (!current.empty()) {
                    current.pop_back();
                }
            }
        }
        current += ellipsis;
    }
    lines.push_back(std::move(current));

    std::string result;
    for (const auto& line : lines) {
        if (!result.empty()) {
            result += '\n';
        }
        result += line;
    }
    return result;
}

}  // namespace Krate::Plugins
