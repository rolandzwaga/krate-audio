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
#include <cstdio>

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

inline void displayBridgeLog(const char* fmt, ...) // NOLINT(cert-dcl50-cpp)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
#ifdef _WIN32
    OutputDebugStringA(buf);
#else
    fprintf(stderr, "%s", buf);
#endif
}

} // namespace Krate::Plugins::Detail

#define KRATE_BRIDGE_LOG(fmt, ...) \
    Krate::Plugins::Detail::displayBridgeLog("[KrateBridge] " fmt "\n", ##__VA_ARGS__)

#else
#define KRATE_BRIDGE_LOG(fmt, ...) ((void)0)
#endif
