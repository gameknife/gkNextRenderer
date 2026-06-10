#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/Loaders/LoaderRegistry.hpp"

#include <algorithm>
#include <cctype>

namespace Assets
{
    namespace
    {
        std::string ToLowerExtension(std::string_view extension)
        {
            std::string normalized(extension);
            std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return normalized;
        }
    }

    FLoaderRegistry& FLoaderRegistry::Get()
    {
        static FLoaderRegistry registry;
        return registry;
    }

    void FLoaderRegistry::RegisterSceneLoader(std::initializer_list<std::string_view> extensions, FSceneLoaderFn loadFn)
    {
        FLoaderEntry entry;
        for (std::string_view extension : extensions)
        {
            entry.extensions.push_back(ToLowerExtension(extension));
        }
        entry.load = std::move(loadFn);

        // Re-registration with the same extension set replaces the loader so
        // repeated app init (tests, scene reloads) stays idempotent.
        for (FLoaderEntry& existing : loaders_)
        {
            if (existing.extensions == entry.extensions)
            {
                existing.load = std::move(entry.load);
                return;
            }
        }
        loaders_.push_back(std::move(entry));
    }

    void FLoaderRegistry::RegisterProcScene(std::string_view name, FProcSceneFn buildFn)
    {
        for (FProcEntry& existing : procScenes_)
        {
            if (existing.name == name)
            {
                existing.build = std::move(buildFn);
                return;
            }
        }
        procScenes_.push_back({std::string(name), std::move(buildFn)});
    }

    bool FLoaderRegistry::SupportsExtension(std::string_view extension) const
    {
        return FindSceneLoader(extension) != nullptr;
    }

    int FLoaderRegistry::GetExtensionOrder(std::string_view extension) const
    {
        const std::string normalized = ToLowerExtension(extension);
        for (size_t i = 0; i < loaders_.size(); ++i)
        {
            const auto& extensions = loaders_[i].extensions;
            if (std::find(extensions.begin(), extensions.end(), normalized) != extensions.end())
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    std::vector<std::string> FLoaderRegistry::RegisteredExtensions() const
    {
        std::vector<std::string> result;
        for (const FLoaderEntry& entry : loaders_)
        {
            result.insert(result.end(), entry.extensions.begin(), entry.extensions.end());
        }
        return result;
    }

    std::vector<std::string> FLoaderRegistry::ProcSceneNames() const
    {
        std::vector<std::string> result;
        result.reserve(procScenes_.size());
        for (const FProcEntry& entry : procScenes_)
        {
            result.push_back(entry.name);
        }
        return result;
    }

    const FSceneLoaderFn* FLoaderRegistry::FindSceneLoader(std::string_view extension) const
    {
        const std::string normalized = ToLowerExtension(extension);
        for (const FLoaderEntry& entry : loaders_)
        {
            if (std::find(entry.extensions.begin(), entry.extensions.end(), normalized) != entry.extensions.end())
            {
                return &entry.load;
            }
        }
        return nullptr;
    }

    const FProcSceneFn* FLoaderRegistry::FindProcScene(std::string_view name) const
    {
        for (const FProcEntry& entry : procScenes_)
        {
            if (entry.name == name)
            {
                return &entry.build;
            }
        }
        return nullptr;
    }
}
