#pragma once

// ==============================================================================
// SharedDisplayBridge — Global registry for processor-to-controller display data
// ==============================================================================
// Tier 3 fallback for hosts that don't implement DataExchange or IConnectionPoint.
// Processor registers a pointer to its display data struct in initialize().
// Controller looks it up by instanceId in setComponentState().
//
// Thread safety: all registry operations are mutex-protected.
// Audio thread never touches the registry — only reads/writes to the data itself.
// ==============================================================================

#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace Krate::Plugins {

class SharedDisplayBridge {
public:
    /// Meyer's singleton — single global registry across all plugin instances
    static SharedDisplayBridge& instance();

    /// Register a processor's shared display data. Called from initialize().
    /// @param id Unique instance identifier (random uint64)
    /// @param data Pointer to processor-owned display data struct
    void registerInstance(uint64_t id, void* data);

    /// Unregister on terminate(). Safe to call with unknown id.
    void unregisterInstance(uint64_t id);

    /// Look up a registered instance. Returns nullptr if not found.
    [[nodiscard]] void* lookupInstance(uint64_t id) const;

private:
    SharedDisplayBridge() = default;
    ~SharedDisplayBridge() = default;
    SharedDisplayBridge(const SharedDisplayBridge&) = delete;
    SharedDisplayBridge& operator=(const SharedDisplayBridge&) = delete;

    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, void*> registry_;
};

} // namespace Krate::Plugins
