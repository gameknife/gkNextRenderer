#pragma once

#include "Engine/Runtime/RenderFrameConsumer.hpp"

#include <memory>

namespace Runtime::Config
{
    class Options;
}

namespace Modules::NextRemote
{
    // Builds the WebRTC remote-play frame consumer from the engine options
    // (resolution clamping, ports, bitrate). Attach via
    // NextEngine::AddRenderFrameConsumer before NextEngine::Start.
    std::unique_ptr<Runtime::IRenderFrameConsumer> CreateRemoteServer(const Runtime::Config::Options& options);
}
