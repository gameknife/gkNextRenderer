#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextRemote/RemoteSession.hpp"

#include "Engine/Runtime/RemoteProtocol.hpp"
#include "Modules/NextRemote/VideoPipeline.hpp"

#include <algorithm>
#include <sstream>

#if GK_WITH_REMOTE
#include <rtc/datachannel.hpp>
#include <rtc/h264rtppacketizer.hpp>
#include <rtc/rtcpnackresponder.hpp>
#include <rtc/rtcpsrreporter.hpp>
#include <rtc/rtppacketizationconfig.hpp>
#endif

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace Runtime::Remote
{
#if GK_WITH_REMOTE
    namespace
    {
        constexpr uint8_t videoPayloadType = 102;
        constexpr uint32_t videoSsrc = 42;
    }

    FRemoteSession::FRemoteSession(RemoteServer::FConfig config, std::string id,
                                   std::weak_ptr<rtc::WebSocket> signalingSocket, FVideoPipeline* videoPipeline)
        : config_(config)
        , id_(std::move(id))
        , signalingSocket_(std::move(signalingSocket))
        , videoPipeline_(videoPipeline)
    {
    }

    FRemoteSession::~FRemoteSession()
    {
        Stop();
    }

    bool FRemoteSession::Start()
    {
#if GK_WITH_REMOTE
        std::lock_guard lock(peerMutex_);
        if (peerConnection_)
        {
            return true;
        }

        rtc::Configuration rtcConfig;
        rtcConfig.disableAutoNegotiation = true;
        if (!config_.bindAddress.empty() && config_.bindAddress != "0.0.0.0")
        {
            rtcConfig.bindAddress = config_.bindAddress;
        }

        peerConnection_ = std::make_shared<rtc::PeerConnection>(rtcConfig);
        auto weakSelf = weak_from_this();

        peerConnection_->onStateChange(
            [weakSelf](rtc::PeerConnection::State state)
            {
                if (auto self = weakSelf.lock())
                {
                    SPDLOG_INFO("RemotePlay: session {} peer state {}", self->id_, static_cast<int>(state));
                }
            });
        peerConnection_->onGatheringStateChange(
            [weakSelf](rtc::PeerConnection::GatheringState state)
            {
                if (auto self = weakSelf.lock())
                {
                    SPDLOG_INFO("RemotePlay: session {} gathering state {}", self->id_, static_cast<int>(state));
                    if (state == rtc::PeerConnection::GatheringState::Complete)
                    {
                        self->SendLocalDescriptionIfReady();
                    }
                }
            });
        peerConnection_->onLocalDescription(
            [weakSelf](rtc::Description description)
            {
                if (auto self = weakSelf.lock())
                {
                    SPDLOG_INFO("RemotePlay: session {} local description {}", self->id_, description.typeString());
                }
            });
        peerConnection_->onLocalCandidate(
            [weakSelf](rtc::Candidate candidate)
            {
                if (auto self = weakSelf.lock())
                {
                    nlohmann::json message;
                    message["type"] = "candidate";
                    message["id"] = self->id_;
                    message["candidate"] = static_cast<std::string>(candidate);
                    message["mid"] = candidate.mid();
                    self->SendJson(message.dump());
                }
            });

        rtc::Description::Video video("video", rtc::Description::Direction::SendOnly);
        video.addH264Codec(videoPayloadType);
        video.addSSRC(videoSsrc, "gkNextRemoteVideo", id_, "video");
        auto track = peerConnection_->addTrack(video);
        {
            std::lock_guard trackLock(trackMutex_);
            videoTrack_ = track;
        }

        const auto cname = "gkNextRemoteVideo";
        auto rtpConfig =
            std::make_shared<rtc::RtpPacketizationConfig>(videoSsrc, cname, videoPayloadType,
                                                          rtc::H264RtpPacketizer::ClockRate);
        auto packetizer = std::make_shared<rtc::H264RtpPacketizer>(rtc::NalUnit::Separator::StartSequence, rtpConfig);
        packetizer->addToChain(std::make_shared<rtc::RtcpSrReporter>(rtpConfig));
        packetizer->addToChain(std::make_shared<rtc::RtcpNackResponder>());
        track->setMediaHandler(packetizer);
        track->onOpen(
            [weakSelf]()
            {
                if (auto self = weakSelf.lock())
                {
                    self->trackOpen_ = true;
                    if (self->videoPipeline_ && self->sinkId_.load() == 0)
                    {
                        std::weak_ptr<FRemoteSession> weakSession = weakSelf;
                        const uint64_t sinkId = self->videoPipeline_->AddSink(
                            [weakSession](const FEncodedPacket& packet)
                            {
                                if (auto session = weakSession.lock())
                                {
                                    session->OnEncodedPacket(packet);
                                }
                            });
                        self->sinkId_.store(sinkId);
                    }
                    SPDLOG_INFO("RemotePlay: session {} video track open", self->id_);
                }
            });

        rtc::DataChannelInit inputInit;
        inputInit.reliability.unordered = true;
        inputInit.reliability.maxRetransmits = 0u;
        inputChannel_ = peerConnection_->createDataChannel("input", inputInit);
        inputChannel_->onOpen(
            [weakSelf]()
            {
                if (auto self = weakSelf.lock())
                {
                    SPDLOG_INFO("RemotePlay: session {} input channel open", self->id_);
                }
            });
        inputChannel_->onMessage(
            [weakSelf](rtc::message_variant data)
            {
                if (auto self = weakSelf.lock())
                {
                    if (std::holds_alternative<rtc::binary>(data))
                    {
                        const rtc::binary& bytes = std::get<rtc::binary>(data);
                        if (!bytes.empty() &&
                            static_cast<ERemoteInputMessage>(bytes[0]) == ERemoteInputMessage::RequestKeyframe)
                        {
                            if (self->videoPipeline_)
                            {
                                self->videoPipeline_->RequestKeyframe();
                            }
                        }
                        else
                        {
                            self->inputRouter_.HandleBinaryMessage(bytes);
                        }
                    }
                    else
                    {
                        self->inputRouter_.HandleTextMessage(std::get<std::string>(data));
                    }
                }
            });

        peerConnection_->setLocalDescription(rtc::Description::Type::Offer);
        SPDLOG_INFO("RemotePlay: session {} offer started", id_);
        return true;
#else
        return false;
#endif
    }

    void FRemoteSession::Stop()
    {
#if GK_WITH_REMOTE
        // Unsubscribe first (outside of peerMutex_): the encoder thread may be inside
        // OnEncodedPacket and we must never hold peerMutex_ while waiting for the sinks lock.
        if (videoPipeline_)
        {
            if (const uint64_t sinkId = sinkId_.exchange(0); sinkId != 0)
            {
                videoPipeline_->RemoveSink(sinkId);
            }
        }
        trackOpen_ = false;

        std::lock_guard lock(peerMutex_);
        {
            std::lock_guard trackLock(trackMutex_);
            if (videoTrack_)
            {
                videoTrack_->close();
                videoTrack_.reset();
            }
        }
        if (inputChannel_)
        {
            inputChannel_->close();
            inputChannel_.reset();
        }
        if (peerConnection_)
        {
            peerConnection_->close();
            peerConnection_.reset();
        }
#endif
        inputRouter_.Stop();
    }

    bool FRemoteSession::ApplyAnswer(const std::string& sdp)
    {
#if GK_WITH_REMOTE
        std::lock_guard lock(peerMutex_);
        if (!peerConnection_)
        {
            return false;
        }
        try
        {
            peerConnection_->setRemoteDescription(rtc::Description(sdp, "answer"));
            SPDLOG_INFO("RemotePlay: session {} answer applied", id_);
            return true;
        }
        catch (const std::exception& error)
        {
            SPDLOG_WARN("RemotePlay: session {} failed to apply answer: {}", id_, error.what());
            return false;
        }
#else
        return false;
#endif
    }

    bool FRemoteSession::AddRemoteCandidate(const std::string& candidate, const std::string& mid)
    {
#if GK_WITH_REMOTE
        std::lock_guard lock(peerMutex_);
        if (!peerConnection_)
        {
            return false;
        }
        try
        {
            peerConnection_->addRemoteCandidate(rtc::Candidate(candidate, mid));
            return true;
        }
        catch (const std::exception& error)
        {
            SPDLOG_WARN("RemotePlay: session {} failed to add remote candidate: {}", id_, error.what());
            return false;
        }
#else
        return false;
#endif
    }

    bool FRemoteSession::UsesSignalingSocket(const std::shared_ptr<rtc::WebSocket>& ws) const
    {
        return signalingSocket_.lock() == ws;
    }

    void FRemoteSession::SendJson(const std::string& message) const
    {
#if GK_WITH_REMOTE
        if (auto ws = signalingSocket_.lock())
        {
            try
            {
                ws->send(message);
            }
            catch (const std::exception& error)
            {
                SPDLOG_WARN("RemotePlay: session {} failed to send signaling message: {}", id_, error.what());
            }
        }
#endif
    }

    void FRemoteSession::SendLocalDescriptionIfReady()
    {
#if GK_WITH_REMOTE
        std::lock_guard lock(peerMutex_);
        if (!peerConnection_)
        {
            return;
        }
        auto description = peerConnection_->localDescription();
        if (!description)
        {
            return;
        }

        nlohmann::json message;
        message["type"] = description->typeString();
        message["id"] = id_;
        message["sdp"] = static_cast<std::string>(*description);
        SendJson(message.dump());
        SPDLOG_INFO("RemotePlay: session {} sent {}", id_, description->typeString());
#endif
    }

    void FRemoteSession::OnEncodedPacket(const FEncodedPacket& packet)
    {
#if GK_WITH_REMOTE
        if (!trackOpen_ || !packet.annexb || packet.annexb->empty())
        {
            return;
        }

        std::shared_ptr<rtc::Track> track;
        {
            std::lock_guard lock(trackMutex_);
            track = videoTrack_;
        }
        if (!track || !track->isOpen())
        {
            return;
        }

        try
        {
            const auto timestamp =
                std::chrono::duration<double, std::milli>(static_cast<double>(packet.timestampUs) / 1000.0);
            track->sendFrame(packet.annexb->data(), packet.annexb->size(), rtc::FrameInfo(timestamp));
            ++sentFrameCount_;
            if (sentFrameCount_ == 1 || sentFrameCount_ % std::max<uint64_t>(1u, config_.fps * 5u) == 0)
            {
                SPDLOG_INFO("RemotePlay: session {} sent video frame {} bytes keyframe={}", id_,
                            packet.annexb->size(), packet.keyframe);
            }
        }
        catch (const std::exception& error)
        {
            SPDLOG_WARN("RemotePlay: session {} failed to send video frame: {}", id_, error.what());
        }
#endif
    }
#endif
}
