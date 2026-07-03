#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextRemote/NextRemoteModule.hpp"
#include "Modules/NextRemote/RemoteServer.hpp"
#include "Engine/Options.hpp"

#include <algorithm>

namespace Modules::NextRemote
{
    namespace
    {
        uint32_t AlignBitrateStep(uint32_t bitrateKbps)
        {
            const uint32_t step = 250;
            return std::max(step, ((bitrateKbps + step - 1) / step) * step);
        }

        uint32_t EstimateRemoteBitrateKbps(uint32_t width, uint32_t height, uint32_t fps)
        {
            const uint64_t pixelsPerSecond =
                static_cast<uint64_t>(std::max(1u, width)) * static_cast<uint64_t>(std::max(1u, height)) *
                static_cast<uint64_t>(std::max(1u, fps));

            if (pixelsPerSecond <= 1280ull * 720ull * 30ull)
            {
                return 4000;
            }
            if (pixelsPerSecond <= 1280ull * 720ull * 60ull)
            {
                return 6500;
            }
            if (pixelsPerSecond <= 1920ull * 1080ull * 30ull)
            {
                return 8000;
            }
            if (pixelsPerSecond <= 1920ull * 1080ull * 60ull)
            {
                return 12000;
            }

            const double scale = static_cast<double>(pixelsPerSecond) /
                                 static_cast<double>(1920ull * 1080ull * 60ull);
            return std::min(20000u, AlignBitrateStep(static_cast<uint32_t>(12000.0 * scale)));
        }

        Runtime::Remote::EVideoEncoderBackend ParseEncoderBackend(const std::string& encoder)
        {
            if (encoder == "vulkan")
            {
                return Runtime::Remote::EVideoEncoderBackend::Vulkan;
            }
            return Runtime::Remote::EVideoEncoderBackend::Auto;
        }
    }

    std::unique_ptr<Runtime::IRenderFrameConsumer> CreateRemoteServer(const Runtime::Config::Options& options)
    {
        uint32_t remoteWidth = options.RemoteWidth != 0 ? options.RemoteWidth : options.Width;
        uint32_t remoteHeight = options.RemoteHeight != 0 ? options.RemoteHeight : options.Height;
        if (options.RemoteWidth == 0 && options.RemoteHeight == 0 && remoteWidth > 0 && remoteHeight > 0)
        {
            constexpr uint32_t maxDefaultRemoteWidth = 1920;
            constexpr uint32_t maxDefaultRemoteHeight = 1080;
            const double scale = std::min(
                1.0, std::min(static_cast<double>(maxDefaultRemoteWidth) / static_cast<double>(remoteWidth),
                              static_cast<double>(maxDefaultRemoteHeight) / static_cast<double>(remoteHeight)));
            remoteWidth = std::max(2u, static_cast<uint32_t>(static_cast<double>(remoteWidth) * scale) & ~1u);
            remoteHeight = std::max(2u, static_cast<uint32_t>(static_cast<double>(remoteHeight) * scale) & ~1u);
        }

        Runtime::Remote::RemoteServer::FConfig remoteConfig;
        remoteConfig.enabled = true;
        remoteConfig.bindAddress = options.RemoteBind;
        remoteConfig.httpPort = options.RemoteHttpPort;
        remoteConfig.signalingPort = options.RemotePort;
        remoteConfig.fps = options.RemoteFps;
        remoteConfig.width = remoteWidth;
        remoteConfig.height = remoteHeight;
        remoteConfig.multiView = options.RemoteMultiView;
        remoteConfig.maxClients = options.RemoteMaxClients;
        remoteConfig.bitrateKbps = options.RemoteBitrateKbps != 0
                                       ? options.RemoteBitrateKbps
                                       : EstimateRemoteBitrateKbps(remoteConfig.width, remoteConfig.height,
                                                                   remoteConfig.fps);
        remoteConfig.encoderBackend = ParseEncoderBackend(options.RemoteEncoder);
        return std::make_unique<Runtime::Remote::RemoteServer>(std::move(remoteConfig));
    }
}
