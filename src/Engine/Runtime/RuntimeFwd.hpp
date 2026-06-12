#pragma once

class NextAudio;
class NextEngine;
class NextGameInstanceBase;
class NextLocalization;
class NextPhysics;
class VulkanGpuTimer;

namespace NextCVar
{
    class FCVarSystem;
}

namespace NextUI
{
    class UserInterface;
    struct Statistics;
}

namespace Runtime
{
    class IDebugUiProvider;
    class IFrameStreamer;
    class IScriptRuntime;
    class IUiOverlay;
}

namespace Runtime::Config
{
    class Options;
    struct ShowFlags;
    struct UserSettings;
}
