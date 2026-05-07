#include "Common/CoreMinimal.hpp"

#include "Common/PluginExport.hpp"
#include "Runtime/Engine.hpp"

extern std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config,
                                                                Options& options,
                                                                NextEngine* engine);

extern "C" GK_PLUGIN_EXPORT NextGameInstanceBase* gkCreateGameInstance(Vulkan::WindowConfig* config,
                                                                       Options* options,
                                                                       NextEngine* engine)
{
    if (config == nullptr || options == nullptr)
    {
        return nullptr;
    }

    return CreateGameInstance(*config, *options, engine).release();
}

extern "C" GK_PLUGIN_EXPORT void gkDestroyGameInstance(NextGameInstanceBase* instance)
{
    delete instance;
}

extern "C" GK_PLUGIN_EXPORT uint32_t gkPluginAbiVersion()
{
    return GetEngineHotReloadAbiVersion();
}
