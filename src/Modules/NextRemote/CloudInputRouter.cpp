#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextRemote/CloudInputRouter.hpp"

#include "Engine/Runtime/RemoteProtocol.hpp"

#include <cstring>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace Runtime::Remote
{
    namespace
    {
        template <typename T>
        bool ReadCloudInputValue(const std::byte* data, size_t size, size_t& offset, T& value)
        {
            if (offset + sizeof(T) > size)
            {
                return false;
            }
            std::memcpy(&value, data + offset, sizeof(T));
            offset += sizeof(T);
            return true;
        }
    }

    void FCloudInputRouter::EnqueueBinaryMessage(const std::string& sessionId,
                                                 const std::vector<std::byte>& message)
    {
        if (message.empty())
        {
            return;
        }

        const auto type = static_cast<ERemoteInputMessage>(message[0]);
        const std::byte* data = message.data() + 1;
        const size_t size = message.size() - 1;
        size_t offset = 0;
        FCloudInputEvent event{};

        switch (type)
        {
        case ERemoteInputMessage::Key:
            {
                uint8_t down = 0;
                uint8_t repeat = 0;
                if (ReadCloudInputValue(data, size, offset, down) &&
                    ReadCloudInputValue(data, size, offset, repeat) &&
                    ReadCloudInputValue(data, size, offset, event.scancode) &&
                    ReadCloudInputValue(data, size, offset, event.mod))
                {
                    event.type = FCloudInputEvent::EType::Key;
                    event.down = down != 0;
                    event.repeat = repeat != 0;
                    Enqueue(sessionId, event);
                }
                break;
            }
        case ERemoteInputMessage::MouseMove:
            if (ReadCloudInputValue(data, size, offset, event.mode) &&
                ReadCloudInputValue(data, size, offset, event.x) &&
                ReadCloudInputValue(data, size, offset, event.y))
            {
                event.type = FCloudInputEvent::EType::MouseMove;
                Enqueue(sessionId, event);
            }
            break;
        case ERemoteInputMessage::MouseButton:
            {
                uint8_t down = 0;
                if (ReadCloudInputValue(data, size, offset, down) &&
                    ReadCloudInputValue(data, size, offset, event.button) &&
                    ReadCloudInputValue(data, size, offset, event.x) &&
                    ReadCloudInputValue(data, size, offset, event.y))
                {
                    event.type = FCloudInputEvent::EType::MouseButton;
                    event.down = down != 0;
                    Enqueue(sessionId, event);
                }
                break;
            }
        case ERemoteInputMessage::Wheel:
            if (ReadCloudInputValue(data, size, offset, event.x) &&
                ReadCloudInputValue(data, size, offset, event.y))
            {
                event.type = FCloudInputEvent::EType::Wheel;
                Enqueue(sessionId, event);
            }
            break;
        case ERemoteInputMessage::Gamepad:
            for (int16_t& axis : event.axes)
            {
                if (!ReadCloudInputValue(data, size, offset, axis))
                {
                    return;
                }
            }
            if (ReadCloudInputValue(data, size, offset, event.buttonMask))
            {
                event.type = FCloudInputEvent::EType::Gamepad;
                Enqueue(sessionId, event);
            }
            break;
        default:
            break;
        }
    }

    void FCloudInputRouter::EnqueueTextMessage(const std::string& sessionId, const std::string& message)
    {
        try
        {
            const nlohmann::json json = nlohmann::json::parse(message);
            if (json.value("type", "") != "key")
            {
                return;
            }

            FCloudInputEvent event{};
            event.type = FCloudInputEvent::EType::Key;
            event.down = json.value("down", false);
            event.repeat = json.value("repeat", false);
            event.scancode = json.value("scancode", 0);
            event.mod = json.value("mod", 0);
            Enqueue(sessionId, event);
        }
        catch (const std::exception& error)
        {
            SPDLOG_WARN("RemotePlay: invalid cloud input json: {}", error.what());
        }
    }

    std::vector<FCloudInputEvent> FCloudInputRouter::Drain(const std::string& sessionId)
    {
        std::lock_guard lock(mutex_);
        auto it = pendingEvents_.find(sessionId);
        if (it == pendingEvents_.end())
        {
            return {};
        }

        std::vector<FCloudInputEvent> events;
        events.swap(it->second);
        return events;
    }

    void FCloudInputRouter::ClearSession(const std::string& sessionId)
    {
        std::lock_guard lock(mutex_);
        pendingEvents_.erase(sessionId);
    }

    void FCloudInputRouter::Clear()
    {
        std::lock_guard lock(mutex_);
        pendingEvents_.clear();
    }

    void FCloudInputRouter::Enqueue(const std::string& sessionId, FCloudInputEvent event)
    {
        std::lock_guard lock(mutex_);
        pendingEvents_[sessionId].push_back(event);
    }
}
