#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/SplatLoader/SplatSettings.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Engine.hpp"

namespace Modules::Splat
{
    namespace { constexpr const char* serviceKey = "Modules.Splat.Settings"; }
    std::shared_ptr<FSplatSettings> GetSettings(NextEngine& engine)
    {
        return std::static_pointer_cast<FSplatSettings>(engine.GetExternalService(serviceKey));
    }
    std::shared_ptr<const FSplatSettings> GetSettings(const NextEngine& engine)
    {
        return std::static_pointer_cast<const FSplatSettings>(engine.GetExternalService(serviceKey));
    }
    void InstallSettings(NextEngine& engine)
    {
        if (GetSettings(engine)) return;
        auto s = std::make_shared<FSplatSettings>();
        auto& c = engine.GetCVarSystem();
        using NextCVar::ECVarFlags;
        c.RegisterUInt("r.splat.bucketCount", 4096, &s->bucketCount, ECVarFlags::Archive, "Minimum splat depth-sort bucket count");
        c.RegisterUInt("r.splat.maxCount", 0, &s->maxCount, ECVarFlags::Archive, "Maximum splats per frame (0=all)");
        c.RegisterBool("r.splat.sortCache", true, &s->sortCache, ECVarFlags::Archive, "Reuse splat sorting");
        c.RegisterFloat("r.splat.sigma", 2.5f, &s->sigma, ECVarFlags::Archive, "Splat billboard radius");
        c.RegisterBool("r.splat.forceAA", true, &s->forceAA, ECVarFlags::Archive, "Force splat antialiasing");
        c.RegisterFloat("r.splat.aaStrength", 0.5f, &s->aaStrength, ECVarFlags::Archive, "Splat antialias strength");
        c.RegisterBool("r.splat.proxy.enable", true, &s->proxyEnable, ECVarFlags::Archive, "Generate splat proxies");
        c.RegisterUInt("r.splat.proxy.gridMax", 64, &s->proxyGridMax, ECVarFlags::Archive, "Maximum proxy resolution");
        c.RegisterFloat("r.splat.proxy.sigma", 2.5f, &s->proxySigma, ECVarFlags::Archive, "Proxy influence radius");
        c.RegisterFloat("r.splat.proxy.isoThreshold", 0.35f, &s->proxyIsoThreshold, ECVarFlags::Archive, "Proxy alpha threshold");
        c.RegisterBool("r.splat.proxy.shadow.enable", true, &s->shadowEnable, ECVarFlags::Archive, "Splat proxy shadows");
        c.RegisterBool("r.splat.proxy.rayOcclusion.enable", true, &s->rayOcclusionEnable, ECVarFlags::Archive, "Splat proxy ray occlusion");
        c.RegisterBool("r.splat.proxy.debugVisible", false, &s->proxyDebugVisible, ECVarFlags::Archive, "Show splat proxies");
        c.RegisterBool("r.splat.receiveLighting", true, &s->receiveLighting, ECVarFlags::Archive, "Splat scene lighting");
        c.RegisterFloat("r.splat.lightingStrength", 0.35f, &s->lightingStrength, ECVarFlags::Archive, "Splat lighting strength");
        c.RegisterBool("show.gaussianSplats", true, &s->visible, ECVarFlags::None, "Show Gaussian splats");
        engine.SetExternalService(serviceKey, s);
        c.LoadUserFiles();
        for (const std::string& command : engine.GetOptions().CVarOverrides)
        {
            if (command.starts_with("r.splat.") || command.starts_with("show.gaussianSplats"))
            {
                (void)c.ExecuteCommand(command);
            }
        }
    }
}
