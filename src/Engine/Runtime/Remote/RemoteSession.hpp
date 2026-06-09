#pragma once

#include "Engine/Runtime/Remote/InputRouter.hpp"
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
    struct FEncodedPacket;
    class FVideoPipeline;

#if GK_WITH_REMOTE
    class FRemoteSession final : public std::enable_shared_from_this<FRemoteSession>
    {
    public:
        FRemoteSession(RemoteServer::FConfig config, std::string id,
                       std::weak_ptr<rtc::WebSocket> signalingSocket, FVideoPipeline* videoPipeline);
        ~FRemoteSession();

        bool Start();
        void Stop();
        bool ApplyAnswer(const std::string& sdp);
        bool AddRemoteCandidate(const std::string& candidate, const std::string& mid);
        bool UsesSignalingSocket(const std::shared_ptr<rtc::WebSocket>& ws) const;

        const std::string& Id() const { return id_; }

    private:
        void SendJson(const std::string& message) const;
        void SendLocalDescriptionIfReady();
        // Called on the video pipeline's encoder thread.
        void OnEncodedPacket(const FEncodedPacket& packet);

        RemoteServer::FConfig config_;
        std::string id_;
        std::weak_ptr<rtc::WebSocket> signalingSocket_;
        FVideoPipeline* videoPipeline_ = nullptr;
        FInputRouter inputRouter_;
        std::atomic_bool trackOpen_ = false;
        std::atomic<uint64_t> sinkId_ = 0;
        uint64_t sentFrameCount_ = 0;
        mutable std::mutex peerMutex_;
        mutable std::mutex trackMutex_;

        std::shared_ptr<rtc::PeerConnection> peerConnection_;
        std::shared_ptr<rtc::Track> videoTrack_;
        std::shared_ptr<rtc::DataChannel> inputChannel_;
    };
#endif
}
