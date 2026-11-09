// ==============================================================================
// SharedDisplayBridge — Implementation
// ==============================================================================

#include "display/shared_display_bridge.h"

namespace Krate::Plugins {

SharedDisplayBridge& SharedDisplayBridge::instance()
{
    static SharedDisplayBridge bridge;
    return bridge;
}

void SharedDisplayBridge::registerInstance(uint64_t id, void* data)
{
    std::scoped_lock lock(mutex_);
    registry_[id] = data;
}

bool SharedDisplayBridge::registerInstanceIfFree(uint64_t id, void* data)
{
    std::scoped_lock lock(mutex_);
    const auto [it, inserted] = registry_.try_emplace(id, data);
    return inserted || it->second == data;
}

void SharedDisplayBridge::unregisterInstance(uint64_t id)
{
    std::scoped_lock lock(mutex_);
    registry_.erase(id);
}

void SharedDisplayBridge::unregisterInstance(uint64_t id, const void* data)
{
    std::scoped_lock lock(mutex_);
    auto it = registry_.find(id);
    if (it != registry_.end() && it->second == data)
        registry_.erase(it);
}

void* SharedDisplayBridge::lookupInstance(uint64_t id) const
{
    std::scoped_lock lock(mutex_);
    auto it = registry_.find(id);
    if (it != registry_.end())
        return it->second;
    return nullptr;
}

} // namespace Krate::Plugins
