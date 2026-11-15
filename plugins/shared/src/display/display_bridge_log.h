#pragma once

// ==============================================================================
// Diagnostic logging for SharedDisplayBridge / DataExchange fallback
// ==============================================================================
// Outputs to OutputDebugStringA (DebugView on Windows) or stderr (macOS/Linux).
// Define KRATE_DISPLAY_BRIDGE_LOG=0 to silence.
// ==============================================================================

#ifndef KRATE_DISPLAY_BRIDGE_LOG
#define KRATE_DISPLAY_BRIDGE_LOG 1
#endif

#if KRATE_DISPLAY_BRIDGE_LOG

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Krate::Plugins::Detail {

/// Prints "[KrateBridge] <message>\n". The prefix and newline are added here, not pasted in the macro: pasting
/// needs `, ##__VA_ARGS__` for argument-free calls, a GNU extension (-Wgnu-zero-variadic-macro-arguments, macOS).
inline void displayBridgeLog(const char* fmt, ...) // NOLINT(cert-dcl50-cpp)
{
    static constexpr char kPrefix[] = "[KrateBridge] ";
    char buf[512];
    std::snprintf(buf, sizeof(buf), "%s", kPrefix);
    constexpr std::size_t kPrefixLen = sizeof(kPrefix) - 1u;
    // Leave room for the newline and the terminator.
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf + kPrefixLen, sizeof(buf) - kPrefixLen - 1u, fmt, args);
    va_end(args);
    const std::size_t len = std::strlen(buf);
    buf[len] = '\n';
    buf[len + 1u] = '\0';
#ifdef _WIN32
    OutputDebugStringA(buf);
#else
    fprintf(stderr, "%s", buf);
#endif
}

} // namespace Krate::Plugins::Detail

#define KRATE_BRIDGE_LOG(...) Krate::Plugins::Detail::displayBridgeLog(__VA_ARGS__)

#else
#define KRATE_BRIDGE_LOG(...) ((void)0)
#endif
