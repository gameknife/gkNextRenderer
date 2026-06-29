#include "Net/LoopbackTransport.h"

namespace NextRA::Net
{
    void FLoopbackTransport::Send(uint8_t fromPlayer, uint8_t toPlayer, uint32_t execTick, std::span<const uint8_t> payload)
    {
        ++sendCounter_;
        if (dropEveryNthPacket_ > 0 && sendCounter_ % dropEveryNthPacket_ == 0)
        {
            ++droppedPacketCount_;
            return;
        }

        const bool reorder = reorderEveryNthPacket_ > 0 && sendCounter_ % reorderEveryNthPacket_ == 0;
        FQueuedPacket queued;
        queued.deliverTick = execTick + delayTicks_ + (reorder ? reorderExtraDelayTicks_ : 0);
        queued.packet.fromPlayer = fromPlayer;
        queued.packet.toPlayer = toPlayer;
        queued.packet.execTick = execTick;
        queued.packet.payload.assign(payload.begin(), payload.end());
        packets_.push_back(std::move(queued));
    }

    std::vector<FNetPacket> FLoopbackTransport::Drain(uint8_t toPlayer, uint32_t networkTick)
    {
        std::vector<FNetPacket> out;
        for (auto it = packets_.begin(); it != packets_.end();)
        {
            if (it->deliverTick <= networkTick && it->packet.toPlayer == toPlayer)
            {
                out.push_back(std::move(it->packet));
                it = packets_.erase(it);
            }
            else
            {
                ++it;
            }
        }
        return out;
    }
}
