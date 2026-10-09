// ==============================================================================
// Vorago - controller DataExchange consumer (SC-013, FR-040)
// ==============================================================================
// Drives Vorago::Controller's IDataExchangeReceiver path and its IMessage
// fallback, and asserts the cached EcosystemFrame changes ONLY for a block
// carrying kEcosystemFrameUserContextId with size >= sizeof(EcosystemFrame).
//
// Fallback attribute names are the SDK's own constants
// (extern/vst3sdk/public.sdk/source/vst/utility/dataexchange.cpp:44-49):
//   MessageIDDataExchange = "DataExchange", MessageKeyData = "Data",
//   MessageKeyUserContextID = "UserContextID".
//
// Forwarding probe: ComponentBase::notify routes a "TextMessage" with a "Text"
// string attribute to receiveText, which returns kResultOk
// (vstcomponentbase.cpp:91-107, :153-156). DataExchangeReceiverHandler::onMessage
// returns false for that ID, so only the base class can produce the kResultOk;
// an override that swallowed non-DataExchange messages would not.
// ==============================================================================

#include "controller/controller.h"
#include "processor/ecosystem_frame.h"

#include "pluginterfaces/base/fstrdefs.h"
#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/vst/ivstdataexchange.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

namespace {

Vorago::EcosystemFrame makeFrame(std::uint32_t sequence) {
    Vorago::EcosystemFrame frame{};
    frame.sequence = sequence;
    return frame;
}

}  // namespace

TEST_CASE("Vorago_Controller_ConsumesEcosystemFrame", "[vorago][controller][ecosystem]") {
    using Steinberg::Vst::DataExchangeBlock;
    using Steinberg::Vst::DataExchangeUserContextID;

    constexpr auto kFrameSize = static_cast<Steinberg::uint32>(sizeof(Vorago::EcosystemFrame));
    static_assert(kFrameSize == 1072);
    constexpr auto kContext = static_cast<DataExchangeUserContextID>(Vorago::kEcosystemFrameUserContextId);

    auto controller = Steinberg::owned(new ::Vorago::Controller());
    REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);
    REQUIRE(controller->cachedEcosystemFrame().sequence == 0u);

    // queueOpened asks for UI-thread dispatch.
    Steinberg::TBool bg = 1;
    controller->queueOpened(kContext, kFrameSize, bg);
    REQUIRE_FALSE(bg);

    // Three valid blocks in one call -> the last one wins.
    std::array<Vorago::EcosystemFrame, 3> frames{makeFrame(1), makeFrame(2), makeFrame(3)};
    std::array<DataExchangeBlock, 3> blocks{};
    for (Steinberg::uint32 i = 0; i < 3; ++i) {
        blocks[i].data = &frames[i];
        blocks[i].size = kFrameSize;
        blocks[i].blockID = i;
    }
    controller->onDataExchangeBlocksReceived(kContext, 3, blocks.data(), 0);
    REQUIRE(controller->cachedEcosystemFrame().sequence == 3u);

    // Short block (sizeof - 1) -> ignored.
    auto shortFrame = makeFrame(9);
    DataExchangeBlock shortBlock{};
    shortBlock.data = &shortFrame;
    shortBlock.size = kFrameSize - 1;
    shortBlock.blockID = 0;
    controller->onDataExchangeBlocksReceived(kContext, 1, &shortBlock, 0);
    REQUIRE(controller->cachedEcosystemFrame().sequence == 3u);

    // Full block on a foreign context -> ignored.
    auto foreignFrame = makeFrame(10);
    DataExchangeBlock foreignBlock{};
    foreignBlock.data = &foreignFrame;
    foreignBlock.size = kFrameSize;
    foreignBlock.blockID = 0;
    controller->onDataExchangeBlocksReceived(static_cast<DataExchangeUserContextID>(0x12345678u), 1,
                                             &foreignBlock, 0);
    REQUIRE(controller->cachedEcosystemFrame().sequence == 3u);

    // IMessage fallback (dataexchange.cpp:411-432) reaches the cache via notify.
    {
        auto fallbackFrame = makeFrame(7);
        Steinberg::Vst::HostMessage msg;
        msg.setMessageID("DataExchange");
        auto* attrs = msg.getAttributes();
        REQUIRE(attrs != nullptr);
        attrs->setInt("UserContextID", static_cast<Steinberg::int64>(0x5645434F));
        attrs->setBinary("Data", &fallbackFrame, kFrameSize);
        REQUIRE(controller->notify(&msg) == Steinberg::kResultOk);
        REQUIRE(controller->cachedEcosystemFrame().sequence == 7u);
    }

    // Forwarding probe: a non-DataExchange message reaches ComponentBase::notify.
    {
        Steinberg::Vst::HostMessage msg;
        msg.setMessageID("TextMessage");
        auto* attrs = msg.getAttributes();
        REQUIRE(attrs != nullptr);
        attrs->setString("Text", STR16("forwarding probe"));
        REQUIRE(controller->notify(&msg) == Steinberg::kResultOk);
        REQUIRE(controller->cachedEcosystemFrame().sequence == 7u);
    }

    REQUIRE(controller->terminate() == Steinberg::kResultOk);
}
