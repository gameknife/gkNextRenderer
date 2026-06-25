#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextRemote/NextRemoteModule.hpp"
#include "Modules/NextRemote/RemoteServer.hpp"
#include "Engine/Options.hpp"

#include <algorithm>

namespace Modules::NextRemote
{
    namespace
    {
        Runtime::Remote::EVideoEncoderBackend ParseEncoderBackend(const std::string& encoder)
        {
            if (encoder == "openh264")
            {
                return Runtime::Remote::EVideoEncoderBackend::OpenH264;
            }
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
            constexpr uint32_t maxDefaultRemoteWidth = 1280;
            constexpr uint32_t maxDefaultRemoteHeight = 720;
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
        remoteConfig.bitrateKbps = options.RemoteBitrateKbps;
        remoteConfig.fps = options.RemoteFps;
        remoteConfig.width = remoteWidth;
        remoteConfig.height = remoteHeight;
        remoteConfig.encoderBackend = ParseEncoderBackend(options.RemoteEncoder);
        return std::make_unique<Runtime::Remote::RemoteServer>(std::move(remoteConfig));
    }
}
