#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Remote/RemoteSession.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Remote/RemoteProtocol.hpp"

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

        uint32_t MakeEven(uint32_t value, uint32_t fallback)
        {
            const uint32_t selected = value == 0 ? fallback : value;
            return std::max(2u, selected & ~1u);
        }

        std::chrono::steady_clock::duration FrameInterval(uint32_t fps)
        {
            return std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(1.0 / static_cast<double>(std::max(1u, fps))));
        }
    }

    FRemoteSession::FRemoteSession(RemoteServer::FConfig config, std::string id,
                                   std::weak_ptr<rtc::WebSocket> signalingSocket)
        : config_(config)
        , id_(std::move(id))
        , signalingSocket_(std::move(signalingSocket))
        , frameSource_(MakeEven(config_.width, 640u), MakeEven(config_.height, 360u))
        , encoder_({MakeEven(config_.width, 640u), MakeEven(config_.height, 360u), std::max(1u, config_.fps),
                    std::max(1u, config_.bitrateKbps)})
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

        if (!encoder_.Start())
        {
            SendJson(R"({"type":"error","message":"OpenH264 encoder failed"})");
            return false;
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
        videoTrack_ = peerConnection_->addTrack(video);

        const auto cname = "gkNextRemoteVideo";
        auto rtpConfig =
            std::make_shared<rtc::RtpPacketizationConfig>(videoSsrc, cname, videoPayloadType,
                                                          rtc::H264RtpPacketizer::ClockRate);
        auto packetizer = std::make_shared<rtc::H264RtpPacketizer>(rtc::NalUnit::Separator::StartSequence, rtpConfig);
        packetizer->addToChain(std::make_shared<rtc::RtcpSrReporter>(rtpConfig));
        packetizer->addToChain(std::make_shared<rtc::RtcpNackResponder>());
        videoTrack_->setMediaHandler(packetizer);
        videoTrack_->onOpen(
            [weakSelf]()
            {
                if (auto self = weakSelf.lock())
                {
                    self->trackOpen_ = true;
                    self->encoder_.RequestKeyframe();
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
                            self->encoder_.RequestKeyframe();
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

        startedAt_ = std::chrono::steady_clock::now();
        nextFrameTime_ = startedAt_;
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
        std::lock_guard lock(peerMutex_);
        if (videoTrack_)
        {
            videoTrack_->close();
            videoTrack_.reset();
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
        encoder_.Stop();
        trackOpen_ = false;
    }

    void FRemoteSession::Tick()
    {
        if (!trackOpen_)
        {
            return;
        }
        SendVideoFrame(std::chrono::steady_clock::now());
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

    void FRemoteSession::SendVideoFrame(std::chrono::steady_clock::time_point now)
    {
#if GK_WITH_REMOTE
        if (now < nextFrameTime_)
        {
            return;
        }

        std::shared_ptr<rtc::Track> track;
        {
            std::lock_guard lock(peerMutex_);
            track = videoTrack_;
        }
        if (!track || !track->isOpen())
        {
            return;
        }

        const uint64_t timestampMs =
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now - startedAt_).count());
        const FI420Frame* frame = nullptr;
        if (NextEngine* engine = NextEngine::GetInstance())
        {
            if (frameSource_.CaptureSwapChainFrame(engine->GetRenderer()))
            {
                frame = &frameSource_.LatestFrame();
            }
        }
        if (!frame)
        {
            frame = &frameSource_.BuildTestPattern(frameIndex_);
        }
        ++frameIndex_;

        std::vector<std::byte> encodedFrame;
        bool keyframe = false;
        if (encoder_.Encode(*frame, timestampMs, encodedFrame, keyframe))
        {
            const auto timestamp = std::chrono::duration<double, std::milli>(static_cast<double>(timestampMs));
            track->sendFrame(encodedFrame.data(), encodedFrame.size(), rtc::FrameInfo(timestamp));
            ++sentFrameCount_;
            if (sentFrameCount_ == 1 || sentFrameCount_ % std::max(1u, config_.fps * 5u) == 0)
            {
                SPDLOG_INFO("RemotePlay: session {} sent video frame {} bytes keyframe={}", id_, encodedFrame.size(),
                            keyframe);
            }
        }

        nextFrameTime_ = now + FrameInterval(config_.fps);
#endif
    }
#endif
}
