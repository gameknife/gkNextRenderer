#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextRemote/SignalingServer.hpp"

#include "Modules/NextRemote/RemoteSession.hpp"
#include "Modules/NextRemote/VideoPipeline.hpp"
#include "Engine/Utilities/FileHelper.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <variant>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace Runtime::Remote
{
    FSignalingServer::FSignalingServer(RemoteServer::FConfig config, FVideoPipeline* videoPipeline)
        : config_(std::move(config))
        , videoPipeline_(videoPipeline)
    {
    }

    FSignalingServer::~FSignalingServer()
    {
        Stop();
    }

#if GK_WITH_REMOTE
    void FSignalingServer::RemoveClient(const std::shared_ptr<rtc::WebSocket>& ws)
    {
        std::lock_guard lock(clientsMutex_);
        clients_.erase(std::remove(clients_.begin(), clients_.end(), ws), clients_.end());
    }

    void FSignalingServer::RemoveSessionsForClient(const std::shared_ptr<rtc::WebSocket>& ws)
    {
        std::lock_guard lock(sessionsMutex_);
        for (auto it = sessions_.begin(); it != sessions_.end();)
        {
            if (it->second->UsesSignalingSocket(ws))
            {
                it = sessions_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void FSignalingServer::HandleTextMessage(const std::shared_ptr<rtc::WebSocket>& ws, const std::string& text)
    {
        nlohmann::json message;
        try
        {
            message = nlohmann::json::parse(text);
        }
        catch (const std::exception& error)
        {
            SPDLOG_WARN("RemotePlay: invalid signaling json: {}", error.what());
            ws->send(R"({"type":"error","message":"invalid json"})");
            return;
        }

        const std::string type = message.value("type", "");
        const std::string id = message.value("id", "default");
        if (type == "request")
        {
            const uint32_t h264ProfileMask = message.value("h264ProfileMask", h264ProfileBaselineBit);
            auto session = std::make_shared<FRemoteSession>(config_, id, ws, videoPipeline_, h264ProfileMask);
            {
                std::lock_guard lock(sessionsMutex_);
                sessions_[id] = session;
            }
            if (!session->Start())
            {
                std::lock_guard lock(sessionsMutex_);
                sessions_.erase(id);
            }
            return;
        }

        std::shared_ptr<FRemoteSession> session;
        {
            std::lock_guard lock(sessionsMutex_);
            if (auto it = sessions_.find(id); it != sessions_.end())
            {
                session = it->second;
            }
        }
        if (!session)
        {
            SPDLOG_WARN("RemotePlay: signaling message {} for missing session {}", type, id);
            return;
        }

        if (type == "answer")
        {
            session->ApplyAnswer(message.value("sdp", ""));
        }
        else if (type == "candidate")
        {
            session->AddRemoteCandidate(message.value("candidate", ""), message.value("mid", ""));
        }
        else
        {
            SPDLOG_INFO("RemotePlay: ignored signaling message type {}", type);
        }
    }
#endif

    std::string FSignalingServer::LoadClientHtml() const
    {
        const std::string path = Utilities::FileHelper::GetPlatformFilePath("assets/remote/index.html");
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            return R"(<!doctype html><meta charset="utf-8"><title>gkNext Remote Play</title>
<body style="margin:0;background:#101318;color:#e8edf2;font:14px system-ui;display:grid;place-items:center;height:100vh">
<main><h1>gkNext Remote Play</h1><p>Remote client asset was not found.</p></main></body>)";
        }

        std::ostringstream contents;
        contents << file.rdbuf();
        return contents.str();
    }

    std::string FSignalingServer::PublicHostForClient() const
    {
        if (config_.bindAddress.empty() || config_.bindAddress == "0.0.0.0")
        {
            return {};
        }
        return config_.bindAddress;
    }

    std::string FSignalingServer::BuildConfigJson() const
    {
        const std::string requestedEncoder = videoPipeline_ ? std::string(videoPipeline_->RequestedEncoderName())
                                                            : std::string(ToString(config_.encoderBackend));
        const std::string activeEncoder =
            videoPipeline_ ? std::string(videoPipeline_->ActiveEncoderName()) : std::string("openh264");
        const uint32_t activeBitrateKbps = videoPipeline_ ? videoPipeline_->BitrateKbps() : config_.bitrateKbps;
        return fmt::format(
            R"({{"signalingPort":{},"bindAddress":"{}","fps":{},"bitrateKbps":{},"width":{},"height":{},"requestedEncoder":"{}","activeEncoder":"{}","activeBitrateKbps":{}}})",
            config_.signalingPort, PublicHostForClient(), config_.fps, config_.bitrateKbps, config_.width,
            config_.height, requestedEncoder, activeEncoder, activeBitrateKbps);
    }

    bool FSignalingServer::Start()
    {
        if (running_)
        {
            return true;
        }

#if GK_WITH_REMOTE
        const std::string html = LoadClientHtml();
        httpServer_.Get("/",
                        [html](const httplib::Request&, httplib::Response& response)
                        {
                            response.set_content(html, "text/html; charset=utf-8");
                        });
        httpServer_.Get("/config",
                        [this](const httplib::Request&, httplib::Response& response)
                        {
                            response.set_content(BuildConfigJson(), "application/json; charset=utf-8");
                        });

        const std::string bindAddress = config_.bindAddress.empty() ? std::string("0.0.0.0") : config_.bindAddress;
        if (!httpServer_.bind_to_port(bindAddress, static_cast<int>(config_.httpPort)))
        {
            SPDLOG_ERROR("RemotePlay: failed to bind HTTP {}:{}", bindAddress, config_.httpPort);
            return false;
        }

        try
        {
            rtc::WebSocketServer::Configuration wsConfig;
            wsConfig.port = static_cast<uint16_t>(config_.signalingPort);
            if (!config_.bindAddress.empty() && config_.bindAddress != "0.0.0.0")
            {
                wsConfig.bindAddress = config_.bindAddress;
            }
            webSocketServer_ = std::make_shared<rtc::WebSocketServer>(wsConfig);
            webSocketServer_->onClient(
                [this](std::shared_ptr<rtc::WebSocket> ws)
                {
                    SPDLOG_INFO("RemotePlay: signaling websocket client connected");
                    {
                        std::lock_guard lock(clientsMutex_);
                        clients_.push_back(ws);
                    }
                    std::weak_ptr<rtc::WebSocket> weakWs = ws;
                    ws->onOpen(
                        [weakWs]()
                        {
                            if (auto lockedWs = weakWs.lock())
                            {
                                lockedWs->send(R"({"type":"hello","server":"gkNextRemote"})");
                            }
                        });
                    ws->onClosed(
                        [this, weakWs]()
                        {
                            SPDLOG_INFO("RemotePlay: signaling websocket client closed");
                            if (auto lockedWs = weakWs.lock())
                            {
                                RemoveSessionsForClient(lockedWs);
                                RemoveClient(lockedWs);
                            }
                        });
                    ws->onError(
                        [](const std::string& error)
                        {
                            SPDLOG_WARN("RemotePlay: signaling websocket error: {}", error);
                        });
                    ws->onMessage(
                        [this, weakWs](auto data)
                        {
                            if (std::holds_alternative<std::string>(data))
                            {
                                if (auto lockedWs = weakWs.lock())
                                {
                                    HandleTextMessage(lockedWs, std::get<std::string>(data));
                                }
                            }
                            else
                            {
                                SPDLOG_INFO("RemotePlay: signaling binary message received");
                            }
                        });
                });
        }
        catch (const std::exception& error)
        {
            SPDLOG_ERROR("RemotePlay: failed to start signaling websocket on port {}: {}", config_.signalingPort,
                         error.what());
            httpServer_.stop();
            return false;
        }

        running_ = true;
        httpThread_ = std::thread(
            [this]()
            {
                const bool ok = httpServer_.listen_after_bind();
                if (!ok)
                {
                    SPDLOG_WARN("RemotePlay: HTTP server stopped unexpectedly");
                }
            });

        SPDLOG_WARN("RemotePlay: LAN-only unauthenticated server; do not expose it to the public internet");
        SPDLOG_INFO("RemotePlay: client http://{}:{} signaling ws://{}:{}", bindAddress, config_.httpPort, bindAddress,
                    config_.signalingPort);
        return true;
#else
        SPDLOG_WARN("RemotePlay: --remote requested but this build was compiled without GK_WITH_REMOTE");
        return false;
#endif
    }

    void FSignalingServer::Stop()
    {
#if GK_WITH_REMOTE
        if (webSocketServer_)
        {
            webSocketServer_->stop();
            webSocketServer_.reset();
        }
        {
            std::lock_guard lock(sessionsMutex_);
            sessions_.clear();
        }
        {
            std::lock_guard lock(clientsMutex_);
            clients_.clear();
        }
        httpServer_.stop();
        if (httpThread_.joinable())
        {
            httpThread_.join();
        }
#endif
        running_ = false;
    }
}
