// ==============================================================================
// MemorySlotStatusView Unit Tests
// ==============================================================================
// T041: Construction, updateData, slot state tracking
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "controller/views/memory_slot_status_view.h"
#include "controller/display_data.h"

using Innexus::MemorySlotStatusView;
using Innexus::DisplayData;

TEST_CASE("MemorySlotStatusView construction does not crash", "[innexus][ui][memory-slot]")
{
    VSTGUI::CRect rect(0, 0, 120, 20);
    MemorySlotStatusView view(rect);
    // All slots default to unoccupied
    for (int i = 0; i < 8; ++i)
        REQUIRE(view.isSlotOccupied(i) == false);
}

TEST_CASE("MemorySlotStatusView updateData all slots unoccupied", "[innexus][ui][memory-slot]")
{
    VSTGUI::CRect rect(0, 0, 120, 20);
    MemorySlotStatusView view(rect);

    DisplayData data{};
    // All slotOccupied default to 0
    view.updateData(data);

    for (int i = 0; i < 8; ++i)
        REQUIRE(view.isSlotOccupied(i) == false);
}

TEST_CASE("MemorySlotStatusView updateData single slot occupied", "[innexus][ui][memory-slot]")
{
    VSTGUI::CRect rect(0, 0, 120, 20);
    MemorySlotStatusView view(rect);

    DisplayData data{};
    data.slotOccupied[3] = 1;
    view.updateData(data);

    REQUIRE(view.isSlotOccupied(3) == true);
    // All others remain false
    REQUIRE(view.isSlotOccupied(0) == false);
    REQUIRE(view.isSlotOccupied(1) == false);
    REQUIRE(view.isSlotOccupied(2) == false);
    REQUIRE(view.isSlotOccupied(4) == false);
    REQUIRE(view.isSlotOccupied(5) == false);
    REQUIRE(view.isSlotOccupied(6) == false);
    REQUIRE(view.isSlotOccupied(7) == false);
}

TEST_CASE("MemorySlotStatusView updateData replaces previous state", "[innexus][ui][memory-slot]")
{
    VSTGUI::CRect rect(0, 0, 120, 20);
    MemorySlotStatusView view(rect);

    // First update: slots 0 and 5 occupied
    DisplayData data1{};
    data1.slotOccupied[0] = 1;
    data1.slotOccupied[5] = 1;
    view.updateData(data1);

    REQUIRE(view.isSlotOccupied(0) == true);
    REQUIRE(view.isSlotOccupied(5) == true);

    // Second update: only slot 2 occupied (previous state replaced)
    DisplayData data2{};
    data2.slotOccupied[2] = 1;
    view.updateData(data2);

    REQUIRE(view.isSlotOccupied(0) == false);
    REQUIRE(view.isSlotOccupied(2) == true);
    REQUIRE(view.isSlotOccupied(5) == false);
}

TEST_CASE("MemorySlotStatusView isSlotOccupied out-of-range returns false", "[innexus][ui][memory-slot]")
{
    VSTGUI::CRect rect(0, 0, 120, 20);
    MemorySlotStatusView view(rect);

    DisplayData data{};
    data.slotOccupied[0] = 1;
    view.updateData(data);

    REQUIRE(view.isSlotOccupied(-1) == false);
    REQUIRE(view.isSlotOccupied(8) == false);
    REQUIRE(view.isSlotOccupied(100) == false);
}
