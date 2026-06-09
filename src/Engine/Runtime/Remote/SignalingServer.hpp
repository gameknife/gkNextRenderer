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
    class FVideoPipeline;

    class FSignalingServer final
    {
    public:
        FSignalingServer(RemoteServer::FConfig config, FVideoPipeline* videoPipeline);
        ~FSignalingServer();

        bool Start();
        void Stop();

        bool IsRunning() const { return running_; }

    private:
        std::string LoadClientHtml() const;
        std::string BuildConfigJson() const;
        std::string PublicHostForClient() const;

        RemoteServer::FConfig config_;
        FVideoPipeline* videoPipeline_ = nullptr;
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
