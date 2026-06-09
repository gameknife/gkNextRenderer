#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Remote/RemoteServer.hpp"

#include "Engine/Runtime/Remote/SignalingServer.hpp"

#include <spdlog/spdlog.h>

namespace Runtime::Remote
{
    RemoteServer::RemoteServer(FConfig config)
        : config_(std::move(config))
    {
    }

    RemoteServer::~RemoteServer()
    {
        Stop();
    }

    bool RemoteServer::Start()
    {
        if (running_)
        {
            return true;
        }
        if (!config_.enabled)
        {
            return false;
        }

        signalingServer_ = std::make_unique<FSignalingServer>(config_);
        if (!signalingServer_->Start())
        {
            signalingServer_.reset();
            return false;
        }

        running_ = true;
        SPDLOG_INFO("RemotePlay: server started fps={} bitrate={}kbps target={}x{}", config_.fps,
                    config_.bitrateKbps, config_.width, config_.height);
        return true;
    }

    void RemoteServer::Stop()
    {
        if (signalingServer_)
        {
            signalingServer_->Stop();
            signalingServer_.reset();
        }
        if (running_)
        {
            SPDLOG_INFO("RemotePlay: server stopped");
        }
        running_ = false;
    }

    void RemoteServer::Tick()
    {
        if (signalingServer_)
        {
            signalingServer_->Tick();
        }
    }
}
