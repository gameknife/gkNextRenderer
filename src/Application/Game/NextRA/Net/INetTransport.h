#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace NextRA::Net
{
    struct FNetPacket
    {
        uint8_t fromPlayer = 0;
        uint8_t toPlayer = 0;
        uint32_t execTick = 0;
        std::vector<uint8_t> payload;
    };

    class INetTransport
    {
    public:
        virtual ~INetTransport() = default;
        virtual void Send(uint8_t fromPlayer, uint8_t toPlayer, uint32_t execTick, std::span<const uint8_t> payload) = 0;
        virtual std::vector<FNetPacket> Drain(uint8_t toPlayer, uint32_t networkTick) = 0;
    };
}
