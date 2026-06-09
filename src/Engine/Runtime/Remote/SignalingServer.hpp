#pragma once

#include "Engine/Runtime/Remote/RemoteServer.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#if GK_WITH_REMOTE
#include <httplib.h>
#include <rtc/websocket.hpp>
#include <rtc/websocketserver.hpp>
#endif

namespace Runtime::Remote
{
    class FRemoteSession;

    class FSignalingServer final
    {
    public:
        explicit FSignalingServer(RemoteServer::FConfig config);
        ~FSignalingServer();

        bool Start();
        void Stop();
        void Tick();

        bool IsRunning() const { return running_; }

    private:
        std::string LoadClientHtml() const;
        std::string BuildConfigJson() const;
        std::string PublicHostForClient() const;

        RemoteServer::FConfig config_;
        bool running_ = false;

#if GK_WITH_REMOTE
        void RemoveClient(const std::shared_ptr<rtc::WebSocket>& ws);
        void RemoveSessionsForClient(const std::shared_ptr<rtc::WebSocket>& ws);
        void HandleTextMessage(const std::shared_ptr<rtc::WebSocket>& ws, const std::string& text);

        httplib::Server httpServer_;
        std::thread httpThread_;
        std::shared_ptr<rtc::WebSocketServer> webSocketServer_;
        std::mutex clientsMutex_;
        std::vector<std::shared_ptr<rtc::WebSocket>> clients_;
        std::mutex sessionsMutex_;
        std::unordered_map<std::string, std::shared_ptr<FRemoteSession>> sessions_;
#endif
    };
}
