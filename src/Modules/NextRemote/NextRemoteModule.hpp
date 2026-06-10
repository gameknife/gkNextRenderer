#pragma once

#include "Engine/Runtime/FrameStreamer.hpp"

#include <memory>

namespace Runtime::Config
{
    class Options;
}

namespace Modules::NextRemote
{
    // Builds the WebRTC remote-play frame streamer from the engine options
    // (resolution clamping, ports, bitrate). Attach via
    // NextEngine::SetFrameStreamer before NextEngine::Start.
    std::unique_ptr<Runtime::IFrameStreamer> CreateRemoteServer(const Runtime::Config::Options& options);
}
