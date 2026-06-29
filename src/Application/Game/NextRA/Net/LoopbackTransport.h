#pragma once

#include "Net/INetTransport.h"

#include <deque>

namespace NextRA::Net
{
    class FLoopbackTransport final : public INetTransport
    {
    public:
        void SetDelayTicks(uint32_t delayTicks) { delayTicks_ = delayTicks; }
        void SetDropEveryNthPacket(uint32_t dropEveryNthPacket) { dropEveryNthPacket_ = dropEveryNthPacket; }
        void SetReorderEveryNthPacket(uint32_t reorderEveryNthPacket, uint32_t extraDelayTicks)
        {
            reorderEveryNthPacket_ = reorderEveryNthPacket;
            reorderExtraDelayTicks_ = extraDelayTicks;
        }
        uint32_t DroppedPacketCount() const { return droppedPacketCount_; }

        void Send(uint8_t fromPlayer, uint8_t toPlayer, uint32_t execTick, std::span<const uint8_t> payload) override;
        std::vector<FNetPacket> Drain(uint8_t toPlayer, uint32_t networkTick) override;

    private:
        struct FQueuedPacket
        {
            uint32_t deliverTick = 0;
            FNetPacket packet;
        };

        std::deque<FQueuedPacket> packets_;
        uint32_t delayTicks_ = 0;
        uint32_t dropEveryNthPacket_ = 0;
        uint32_t reorderEveryNthPacket_ = 0;
        uint32_t reorderExtraDelayTicks_ = 0;
        uint32_t sendCounter_ = 0;
        uint32_t droppedPacketCount_ = 0;
    };
}
