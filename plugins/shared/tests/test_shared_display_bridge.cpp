// ==============================================================================
// SharedDisplayBridge Unit Tests
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include "display/shared_display_bridge.h"

using namespace Krate::Plugins;

// ==============================================================================
// Register + Lookup
// ==============================================================================

TEST_CASE("SharedDisplayBridge register and lookup returns correct pointer",
          "[shared_display_bridge]")
{
    auto& bridge = SharedDisplayBridge::instance();

    int data = 42;
    uint64_t id = 0xDEAD'BEEF'0001;
    bridge.registerInstance(id, &data);

    void* result = bridge.lookupInstance(id);
    REQUIRE(result == &data);
    REQUIRE(*static_cast<int*>(result) == 42);

    bridge.unregisterInstance(id);
}

// ==============================================================================
// Unregister + Lookup returns nullptr
// ==============================================================================

TEST_CASE("SharedDisplayBridge unregister then lookup returns nullptr",
          "[shared_display_bridge]")
{
    auto& bridge = SharedDisplayBridge::instance();

    float data = 3.14f;
    uint64_t id = 0xDEAD'BEEF'0002;
    bridge.registerInstance(id, &data);
    bridge.unregisterInstance(id);

    REQUIRE(bridge.lookupInstance(id) == nullptr);
}

// ==============================================================================
// Multiple instances coexist independently
// ==============================================================================

TEST_CASE("SharedDisplayBridge multiple instances coexist independently",
          "[shared_display_bridge]")
{
    auto& bridge = SharedDisplayBridge::instance();

    int dataA = 1;
    int dataB = 2;
    int dataC = 3;
    uint64_t idA = 0xAAAA'0001;
    uint64_t idB = 0xBBBB'0002;
    uint64_t idC = 0xCCCC'0003;

    bridge.registerInstance(idA, &dataA);
    bridge.registerInstance(idB, &dataB);
    bridge.registerInstance(idC, &dataC);

    REQUIRE(bridge.lookupInstance(idA) == &dataA);
    REQUIRE(bridge.lookupInstance(idB) == &dataB);
    REQUIRE(bridge.lookupInstance(idC) == &dataC);

    // Unregister one — others remain
    bridge.unregisterInstance(idB);
    REQUIRE(bridge.lookupInstance(idA) == &dataA);
    REQUIRE(bridge.lookupInstance(idB) == nullptr);
    REQUIRE(bridge.lookupInstance(idC) == &dataC);

    bridge.unregisterInstance(idA);
    bridge.unregisterInstance(idC);
}

// ==============================================================================
// Double-register overwrites safely
// ==============================================================================

TEST_CASE("SharedDisplayBridge double register overwrites safely",
          "[shared_display_bridge]")
{
    auto& bridge = SharedDisplayBridge::instance();

    int oldData = 10;
    int newData = 20;
    uint64_t id = 0xDEAD'BEEF'0003;

    bridge.registerInstance(id, &oldData);
    REQUIRE(bridge.lookupInstance(id) == &oldData);

    bridge.registerInstance(id, &newData);
    REQUIRE(bridge.lookupInstance(id) == &newData);

    bridge.unregisterInstance(id);
}

// ==============================================================================
// Unregister non-existent ID is harmless
// ==============================================================================

TEST_CASE("SharedDisplayBridge unregister non-existent ID is harmless",
          "[shared_display_bridge]")
{
    auto& bridge = SharedDisplayBridge::instance();

    // Should not throw or crash
    bridge.unregisterInstance(0xFFFF'FFFF'FFFF'FFFF);
    bridge.unregisterInstance(0);
    bridge.unregisterInstance(999999);

    REQUIRE(bridge.lookupInstance(0xFFFF'FFFF'FFFF'FFFF) == nullptr);
}

// ==============================================================================
// Lookup of unknown ID returns nullptr
// ==============================================================================

TEST_CASE("SharedDisplayBridge lookup unknown ID returns nullptr",
          "[shared_display_bridge]")
{
    auto& bridge = SharedDisplayBridge::instance();
    REQUIRE(bridge.lookupInstance(0x1234'5678'9ABC'DEF0) == nullptr);
}
