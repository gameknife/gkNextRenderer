#pragma once

class NextAudio;
class NextEngine;
class NextGameInstanceBase;
class NextLocalization;
class NextPhysics;

namespace NextCVar
{
    class FCVarSystem;
}

namespace NextUI
{
    class IMultiViewportBackend;
    class UserInterface;
    struct Statistics;
}

namespace Runtime
{
    class FrameProfiler;
    class IDebugUiProvider;
    class IRenderFrameConsumer;
    class IScriptRuntime;
    class IUiOverlay;
}

namespace Runtime::Config
{
    class Options;
    struct ShowFlags;
    struct UserSettings;
}
