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

    SharedDisplayBridge(const SharedDisplayBridge&) = delete;
    SharedDisplayBridge& operator=(const SharedDisplayBridge&) = delete;

    /// Register a processor's shared display data. Called from initialize().
    /// @param id Unique instance identifier (random uint64)
    /// @param data Pointer to processor-owned display data struct
    void registerInstance(uint64_t id, void* data);

    /// Register only if no other data holds the id (atomic check-and-insert), e.g.
    /// when adopting an id read from a state stream another live instance may own.
    /// @return true if `id` now maps to `data` (newly, or already)
    [[nodiscard]] bool registerInstanceIfFree(uint64_t id, void* data);

    /// Unregister on terminate(). Safe to call with unknown id.
    void unregisterInstance(uint64_t id);

    /// Unregister only if `id` still maps to `data`: never removes an entry
    /// another instance registered under the same id. Safe with unknown id.
    void unregisterInstance(uint64_t id, const void* data);

    /// Look up a registered instance. Returns nullptr if not found.
    [[nodiscard]] void* lookupInstance(uint64_t id) const;

private:
    SharedDisplayBridge() = default;
    ~SharedDisplayBridge() = default;

    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, void*> registry_;
};

} // namespace Krate::Plugins
