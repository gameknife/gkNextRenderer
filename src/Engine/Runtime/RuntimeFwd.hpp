#pragma once

class NextAudio;
class NextEngine;
class NextGameInstanceBase;
class NextLocalization;
class NextPhysics;
class NextRig;

namespace NextCVar
{
    class FCVarSystem;
}

namespace NextUI
{
    class IUserInterface;
    class IMultiViewportBackend;
    class UserInterface;
    struct Statistics;
}

namespace Runtime
{
    class IDebugUiProvider;
    class IRenderFrameConsumer;
    class IScreenShotService;
    class IScriptRuntime;
    class IUiOverlay;
}

namespace Runtime::Config
{
    class Options;
    struct ShowFlags;
    struct UserSettings;
}
