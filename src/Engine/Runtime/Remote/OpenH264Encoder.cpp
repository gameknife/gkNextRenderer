#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Remote/OpenH264Encoder.hpp"

#include <cstring>

#if GK_WITH_REMOTE
#include <wels/codec_api.h>
#endif

#include <spdlog/spdlog.h>

namespace Runtime::Remote
{
#if GK_WITH_REMOTE
    namespace
    {
        void AppendLayerNalUnits(const SLayerBSInfo& layer, std::vector<std::byte>& outFrame)
        {
            const uint8_t* layerData = layer.pBsBuf;
            for (int nalIndex = 0; nalIndex < layer.iNalCount; ++nalIndex)
            {
                const int nalSize = layer.pNalLengthInByte[nalIndex];
                if (nalSize <= 0)
                {
                    continue;
                }
                const auto* bytes = reinterpret_cast<const std::byte*>(layerData);
                outFrame.insert(outFrame.end(), bytes, bytes + nalSize);
                layerData += nalSize;
            }
        }
    }
#endif

    FOpenH264Encoder::FOpenH264Encoder(FConfig config)
        : config_(config)
    {
    }

    FOpenH264Encoder::~FOpenH264Encoder()
    {
        Stop();
    }

    bool FOpenH264Encoder::Start()
    {
#if GK_WITH_REMOTE
        if (encoder_)
        {
            return true;
        }

        ISVCEncoder* encoder = nullptr;
        if (WelsCreateSVCEncoder(&encoder) != 0 || encoder == nullptr)
        {
            SPDLOG_ERROR("RemotePlay: failed to create OpenH264 encoder");
            return false;
        }

        SEncParamBase param;
        std::memset(&param, 0, sizeof(param));
        param.iUsageType = CAMERA_VIDEO_REAL_TIME;
        param.fMaxFrameRate = static_cast<float>(std::max(1u, config_.fps));
        param.iPicWidth = static_cast<int>(config_.width);
        param.iPicHeight = static_cast<int>(config_.height);
        param.iTargetBitrate = static_cast<int>(std::max(1u, config_.bitrateKbps) * 1000u);

        if (encoder->Initialize(&param) != cmResultSuccess)
        {
            SPDLOG_ERROR("RemotePlay: failed to initialize OpenH264 encoder {}x{}", config_.width, config_.height);
            WelsDestroySVCEncoder(encoder);
            return false;
        }

        int videoFormat = videoFormatI420;
        encoder->SetOption(ENCODER_OPTION_DATAFORMAT, &videoFormat);
        int idrInterval = static_cast<int>(std::max(1u, config_.fps) * 2u);
        encoder->SetOption(ENCODER_OPTION_IDR_INTERVAL, &idrInterval);

        encoder_ = encoder;
        forceKeyframe_ = true;
        SPDLOG_INFO("RemotePlay: OpenH264 encoder initialized {}x{} {}fps {}kbps", config_.width, config_.height,
                    config_.fps, config_.bitrateKbps);
        return true;
#else
        return false;
#endif
    }

    void FOpenH264Encoder::Stop()
    {
#if GK_WITH_REMOTE
        if (encoder_)
        {
            encoder_->Uninitialize();
            WelsDestroySVCEncoder(encoder_);
            encoder_ = nullptr;
        }
#endif
    }

    void FOpenH264Encoder::RequestKeyframe()
    {
        forceKeyframe_ = true;
    }

    void FOpenH264Encoder::SetBitrate(uint32_t bitrateKbps)
    {
        config_.bitrateKbps = std::max(1u, bitrateKbps);
#if GK_WITH_REMOTE
        if (encoder_)
        {
            int targetBitrate = static_cast<int>(config_.bitrateKbps * 1000u);
            if (encoder_->SetOption(ENCODER_OPTION_BITRATE, &targetBitrate) != cmResultSuccess)
            {
                SPDLOG_WARN("RemotePlay: failed to update OpenH264 bitrate to {}kbps", config_.bitrateKbps);
            }
        }
#endif
    }

    bool FOpenH264Encoder::Encode(const FI420Frame& frame, uint64_t timestampMs, std::vector<std::byte>& outFrame,
                                  bool& keyframe)
    {
        FI420View view;
        view.y = frame.y.data();
        view.u = frame.u.data();
        view.v = frame.v.data();
        view.width = frame.width;
        view.height = frame.height;
        view.strideY = frame.width;
        view.strideC = frame.width / 2u;
        return Encode(view, timestampMs, outFrame, keyframe);
    }

    bool FOpenH264Encoder::Encode(const FI420View& view, uint64_t timestampMs, std::vector<std::byte>& outFrame,
                                  bool& keyframe)
    {
        outFrame.clear();
        keyframe = false;

#if GK_WITH_REMOTE
        if (!encoder_ && !Start())
        {
            return false;
        }

        if (forceKeyframe_)
        {
            encoder_->ForceIntraFrame(true);
            forceKeyframe_ = false;
        }

        SSourcePicture picture;
        std::memset(&picture, 0, sizeof(picture));
        picture.iPicWidth = static_cast<int>(view.width);
        picture.iPicHeight = static_cast<int>(view.height);
        picture.iColorFormat = videoFormatI420;
        picture.iStride[0] = static_cast<int>(view.strideY);
        picture.iStride[1] = static_cast<int>(view.strideC);
        picture.iStride[2] = static_cast<int>(view.strideC);
        picture.pData[0] = const_cast<uint8_t*>(view.y);
        picture.pData[1] = const_cast<uint8_t*>(view.u);
        picture.pData[2] = const_cast<uint8_t*>(view.v);
        picture.uiTimeStamp = static_cast<long long>(timestampMs);

        SFrameBSInfo frameInfo;
        std::memset(&frameInfo, 0, sizeof(frameInfo));
        const int result = encoder_->EncodeFrame(&picture, &frameInfo);
        if (result != cmResultSuccess)
        {
            SPDLOG_WARN("RemotePlay: OpenH264 EncodeFrame failed with {}", result);
            return false;
        }

        if (frameInfo.eFrameType == videoFrameTypeSkip || frameInfo.iFrameSizeInBytes <= 0)
        {
            return false;
        }

        outFrame.reserve(static_cast<size_t>(frameInfo.iFrameSizeInBytes));
        for (int layerIndex = 0; layerIndex < frameInfo.iLayerNum; ++layerIndex)
        {
            AppendLayerNalUnits(frameInfo.sLayerInfo[layerIndex], outFrame);
        }

        keyframe = frameInfo.eFrameType == videoFrameTypeIDR || frameInfo.eFrameType == videoFrameTypeI;
        return !outFrame.empty();
#else
        return false;
#endif
    }
}
