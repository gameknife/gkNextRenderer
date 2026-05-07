#pragma once

#include "Common/CoreMinimal.hpp"

class NextEngine;
class NextGameInstanceBase;
class Options;

namespace Vulkan
{
    struct WindowConfig;
}

class PluginLoader final
{
public:
    struct FStatus
    {
        bool loaded = false;
        bool pending = false;
        std::filesystem::path sourcePath;
        std::filesystem::path shadowPath;
        double pollIntervalSeconds = 0.5;
        uint64_t reloadCounter = 0;
    };

    PluginLoader(const PluginLoader&) = delete;
    PluginLoader& operator=(const PluginLoader&) = delete;

    PluginLoader() = default;
    ~PluginLoader();

    bool Load(const std::filesystem::path& pluginPath);
    bool PrepareReloadIfChanged(double deltaSeconds);
    void CommitPreparedReload();
    void DiscardPreparedReload();
    void Unload();
    void SetPollInterval(double seconds);
    void RequestReload() { manualReloadRequested_ = true; }

    NextGameInstanceBase* Create(Vulkan::WindowConfig& config, Options& options, NextEngine* engine) const;
    void Destroy(NextGameInstanceBase* instance) const;

    bool IsLoaded() const { return activePlugin_.handle != nullptr; }
    const std::filesystem::path& SourcePath() const { return sourcePath_; }
    FStatus GetStatus() const;

    static std::filesystem::path ResolvePluginPath(std::string_view gameName);

private:
    using CreateFn = NextGameInstanceBase* (*)(Vulkan::WindowConfig*, Options*, NextEngine*);
    using DestroyFn = void (*)(NextGameInstanceBase*);
    using AbiVersionFn = uint32_t (*)();

    struct FLoadedPlugin
    {
        void* handle = nullptr;
        CreateFn create = nullptr;
        DestroyFn destroy = nullptr;
        AbiVersionFn abiVersion = nullptr;
        std::filesystem::path shadowPath;
        std::filesystem::file_time_type sourceTimestamp{};
    };

    static std::string LibraryExtension();
    static uint32_t CurrentProcessId();
    static void* OpenLibrary(const std::filesystem::path& path);
    static void CloseLibrary(void* handle);
    static void* LoadSymbol(void* handle, const char* name);
    static void DeleteShadowFile(const std::filesystem::path& path);

    bool LoadShadowCopy(const std::filesystem::path& sourcePath,
                        std::filesystem::file_time_type sourceTimestamp,
                        FLoadedPlugin& outPlugin);
    void CloseLoadedPlugin(FLoadedPlugin& plugin);

    std::filesystem::path sourcePath_;
    FLoadedPlugin activePlugin_{};
    FLoadedPlugin pendingPlugin_{};
    double elapsedSeconds_ = 0.0;
    double pollIntervalSeconds_ = 0.5;
    uint64_t reloadCounter_ = 0;
    bool manualReloadRequested_ = false;
    std::filesystem::file_time_type lastFailedTimestamp_{};
};
