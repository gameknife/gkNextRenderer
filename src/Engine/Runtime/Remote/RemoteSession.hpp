#pragma once

#include "Engine/Runtime/Remote/FrameSource.hpp"
#include "Engine/Runtime/Remote/InputRouter.hpp"
#include "Engine/Runtime/Remote/OpenH264Encoder.hpp"
#include "Engine/Runtime/Remote/RemoteServer.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>

#if GK_WITH_REMOTE
#include <rtc/datachannel.hpp>
#include <rtc/peerconnection.hpp>
#include <rtc/track.hpp>
#include <rtc/websocket.hpp>
#endif

namespace Runtime::Remote
{
#if GK_WITH_REMOTE
    class FRemoteSession final : public std::enable_shared_from_this<FRemoteSession>
    {
    public:
        FRemoteSession(RemoteServer::FConfig config, std::string id,
                       std::weak_ptr<rtc::WebSocket> signalingSocket);
        ~FRemoteSession();

        bool Start();
        void Stop();
        void Tick();
        bool ApplyAnswer(const std::string& sdp);
        bool AddRemoteCandidate(const std::string& candidate, const std::string& mid);
        bool UsesSignalingSocket(const std::shared_ptr<rtc::WebSocket>& ws) const;

        const std::string& Id() const { return id_; }

    private:
        void SendJson(const std::string& message) const;
        void SendLocalDescriptionIfReady();
        void SendVideoFrame(std::chrono::steady_clock::time_point now);

        RemoteServer::FConfig config_;
        std::string id_;
        std::weak_ptr<rtc::WebSocket> signalingSocket_;
        FFrameSource frameSource_;
        FOpenH264Encoder encoder_;
        FInputRouter inputRouter_;
        std::chrono::steady_clock::time_point startedAt_;
        std::chrono::steady_clock::time_point nextFrameTime_;
        std::atomic_bool trackOpen_ = false;
        uint64_t frameIndex_ = 0;
        uint64_t sentFrameCount_ = 0;
        mutable std::mutex peerMutex_;

        std::shared_ptr<rtc::PeerConnection> peerConnection_;
        std::shared_ptr<rtc::Track> videoTrack_;
        std::shared_ptr<rtc::DataChannel> inputChannel_;
    };
#endif
}
