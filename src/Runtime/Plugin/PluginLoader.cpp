#include "Common/CoreMinimal.hpp"

#include "Runtime/Plugin/PluginLoader.hpp"

#include "Common/PluginExport.hpp"
#include "Runtime/Engine.hpp"
#include "Runtime/Platform/PlatformCommon.h"
#include "Utilities/FileHelper.hpp"

#include <spdlog/spdlog.h>

#if WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace
{
    std::string DebugPostfix()
    {
#if defined(GK_CMAKE_DEBUG_POSTFIX)
        return GK_CMAKE_DEBUG_POSTFIX;
#else
        return "";
#endif
    }

    std::vector<std::filesystem::path> CandidatePluginDirectories()
    {
        namespace fs = std::filesystem;

        std::vector<fs::path> directories;
        const fs::path executableDir = NextRenderer::GetExecutableDirectory();
        if (!executableDir.empty())
        {
            directories.push_back(executableDir);
        }
        directories.push_back(fs::current_path());
        directories.push_back(Utilities::FileHelper::GetRuntimeRoot() / "bin");
        return directories;
    }
}

PluginLoader::~PluginLoader()
{
    DiscardPreparedReload();
    Unload();
}

bool PluginLoader::Load(const std::filesystem::path& pluginPath)
{
    namespace fs = std::filesystem;

    std::error_code ec;
    fs::path resolvedPath = pluginPath;
    if (!resolvedPath.is_absolute())
    {
        resolvedPath = fs::absolute(resolvedPath, ec);
        if (ec)
        {
            resolvedPath = pluginPath;
            ec.clear();
        }
    }

    if (!fs::exists(resolvedPath, ec))
    {
        SPDLOG_WARN("[HotReload] Plugin not found: {}", resolvedPath.string());
        return false;
    }

    const fs::file_time_type timestamp = fs::last_write_time(resolvedPath, ec);
    if (ec)
    {
        SPDLOG_WARN("[HotReload] Failed to query plugin timestamp {}: {}", resolvedPath.string(), ec.message());
        return false;
    }

    FLoadedPlugin loadedPlugin;
    if (!LoadShadowCopy(resolvedPath, timestamp, loadedPlugin))
    {
        return false;
    }

    Unload();
    sourcePath_ = resolvedPath;
    activePlugin_ = loadedPlugin;
    SPDLOG_INFO("[HotReload] Plugin loaded: {}", sourcePath_.string());
    return true;
}

bool PluginLoader::PrepareReloadIfChanged(double deltaSeconds)
{
    namespace fs = std::filesystem;

    if (sourcePath_.empty() || activePlugin_.handle == nullptr)
    {
        return false;
    }

    const bool forceReload = manualReloadRequested_;
    manualReloadRequested_ = false;

    elapsedSeconds_ += deltaSeconds;
    if (!forceReload && elapsedSeconds_ < pollIntervalSeconds_)
    {
        return false;
    }
    elapsedSeconds_ = 0.0;

    std::error_code ec;
    if (!fs::exists(sourcePath_, ec))
    {
        return false;
    }

    const fs::file_time_type timestamp = fs::last_write_time(sourcePath_, ec);
    if (ec || (!forceReload && (timestamp <= activePlugin_.sourceTimestamp || timestamp == lastFailedTimestamp_)))
    {
        return false;
    }

    FLoadedPlugin loadedPlugin;
    if (!LoadShadowCopy(sourcePath_, timestamp, loadedPlugin))
    {
        lastFailedTimestamp_ = timestamp;
        return false;
    }

    DiscardPreparedReload();
    pendingPlugin_ = loadedPlugin;
    return true;
}

void PluginLoader::CommitPreparedReload()
{
    if (pendingPlugin_.handle == nullptr)
    {
        return;
    }

    CloseLoadedPlugin(activePlugin_);
    activePlugin_ = pendingPlugin_;
    pendingPlugin_ = {};
    lastFailedTimestamp_ = {};
    SPDLOG_INFO("[HotReload] Plugin reload committed: {}", sourcePath_.string());
}

void PluginLoader::DiscardPreparedReload()
{
    CloseLoadedPlugin(pendingPlugin_);
}

void PluginLoader::Unload()
{
    CloseLoadedPlugin(activePlugin_);
    sourcePath_.clear();
}

void PluginLoader::SetPollInterval(double seconds)
{
    pollIntervalSeconds_ = std::max(0.1, seconds);
}

PluginLoader::FStatus PluginLoader::GetStatus() const
{
    return {
        .loaded = activePlugin_.handle != nullptr,
        .pending = pendingPlugin_.handle != nullptr,
        .sourcePath = sourcePath_,
        .shadowPath = activePlugin_.shadowPath,
        .pollIntervalSeconds = pollIntervalSeconds_,
        .reloadCounter = reloadCounter_,
    };
}

NextGameInstanceBase* PluginLoader::Create(Vulkan::WindowConfig& config, Options& options, NextEngine* engine) const
{
    if (activePlugin_.create == nullptr)
    {
        return nullptr;
    }
    return activePlugin_.create(&config, &options, engine);
}

void PluginLoader::Destroy(NextGameInstanceBase* instance) const
{
    if (instance == nullptr)
    {
        return;
    }

    if (activePlugin_.destroy != nullptr)
    {
        activePlugin_.destroy(instance);
        return;
    }

    delete instance;
}

std::filesystem::path PluginLoader::ResolvePluginPath(std::string_view gameName)
{
    namespace fs = std::filesystem;

    const std::string baseName = fmt::format("{}Plugin", gameName);
    const std::string debugPostfix = DebugPostfix();
    const std::string extension = LibraryExtension();
    std::vector<std::string> candidateNames = {
        baseName + debugPostfix + extension,
        baseName + extension,
        "lib" + baseName + debugPostfix + extension,
        "lib" + baseName + extension,
    };

    std::error_code ec;
    for (const fs::path& directory : CandidatePluginDirectories())
    {
        for (const std::string& candidateName : candidateNames)
        {
            const fs::path candidatePath = (directory / candidateName).lexically_normal();
            if (fs::exists(candidatePath, ec) && fs::is_regular_file(candidatePath, ec))
            {
                const fs::path absolute = fs::absolute(candidatePath, ec);
                return ec ? candidatePath : absolute;
            }
            ec.clear();
        }
    }

    const fs::path fallbackDirectory = CandidatePluginDirectories().empty()
                                           ? fs::current_path()
                                           : CandidatePluginDirectories().front();
    return (fallbackDirectory / candidateNames.front()).lexically_normal();
}

std::string PluginLoader::LibraryExtension()
{
#if WIN32
    return ".dll";
#elif __APPLE__
    return ".dylib";
#else
    return ".so";
#endif
}

uint32_t PluginLoader::CurrentProcessId()
{
#if WIN32
    return static_cast<uint32_t>(::GetCurrentProcessId());
#else
    return static_cast<uint32_t>(::getpid());
#endif
}

void* PluginLoader::OpenLibrary(const std::filesystem::path& path)
{
#if WIN32
    return reinterpret_cast<void*>(::LoadLibraryW(path.wstring().c_str()));
#else
    return ::dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void PluginLoader::CloseLibrary(void* handle)
{
    if (handle == nullptr)
    {
        return;
    }

#if WIN32
    ::FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
    ::dlclose(handle);
#endif
}

void* PluginLoader::LoadSymbol(void* handle, const char* name)
{
    if (handle == nullptr)
    {
        return nullptr;
    }

#if WIN32
    return reinterpret_cast<void*>(::GetProcAddress(reinterpret_cast<HMODULE>(handle), name));
#else
    return ::dlsym(handle, name);
#endif
}

void PluginLoader::DeleteShadowFile(const std::filesystem::path& path)
{
    if (path.empty())
    {
        return;
    }

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

bool PluginLoader::LoadShadowCopy(const std::filesystem::path& sourcePath,
                                  std::filesystem::file_time_type sourceTimestamp,
                                  FLoadedPlugin& outPlugin)
{
    namespace fs = std::filesystem;

    std::error_code ec;
    const fs::path shadowDirectory = sourcePath.parent_path() / "_hot";
    fs::create_directories(shadowDirectory, ec);
    if (ec)
    {
        SPDLOG_WARN("[HotReload] Failed to create plugin shadow directory {}: {}",
                    shadowDirectory.string(),
                    ec.message());
        return false;
    }

    const fs::path shadowPath = shadowDirectory /
                                fmt::format("{}.{}.{}{}",
                                            sourcePath.stem().string(),
                                            CurrentProcessId(),
                                            ++reloadCounter_,
                                            sourcePath.extension().string());
    fs::copy_file(sourcePath, shadowPath, fs::copy_options::overwrite_existing, ec);
    if (ec)
    {
        SPDLOG_WARN("[HotReload] Failed to copy plugin {} -> {}: {}",
                    sourcePath.string(),
                    shadowPath.string(),
                    ec.message());
        return false;
    }

    FLoadedPlugin plugin;
#if WIN32
    // The shadow DLL lives in bin/_hot, while shared engine/runtime DLLs remain in bin.
    ::SetDllDirectoryW(sourcePath.parent_path().wstring().c_str());
#endif
    plugin.handle = OpenLibrary(shadowPath);
#if WIN32
    ::SetDllDirectoryW(nullptr);
#endif
    if (plugin.handle == nullptr)
    {
        SPDLOG_WARN("[HotReload] Failed to load plugin shadow copy: {}", shadowPath.string());
        DeleteShadowFile(shadowPath);
        return false;
    }

    plugin.create = reinterpret_cast<CreateFn>(LoadSymbol(plugin.handle, "gkCreateGameInstance"));
    plugin.destroy = reinterpret_cast<DestroyFn>(LoadSymbol(plugin.handle, "gkDestroyGameInstance"));
    plugin.abiVersion = reinterpret_cast<AbiVersionFn>(LoadSymbol(plugin.handle, "gkPluginAbiVersion"));
    plugin.shadowPath = shadowPath;
    plugin.sourceTimestamp = sourceTimestamp;

    if (plugin.create == nullptr || plugin.destroy == nullptr || plugin.abiVersion == nullptr)
    {
        SPDLOG_WARN("[HotReload] Plugin is missing required hot reload exports: {}", sourcePath.string());
        CloseLoadedPlugin(plugin);
        return false;
    }

    const uint32_t pluginAbiVersion = plugin.abiVersion();
    const uint32_t engineAbiVersion = GetEngineHotReloadAbiVersion();
    if (pluginAbiVersion != engineAbiVersion)
    {
        SPDLOG_WARN("[HotReload] Plugin ABI mismatch for {}. plugin={}, engine={}",
                    sourcePath.string(),
                    pluginAbiVersion,
                    engineAbiVersion);
        CloseLoadedPlugin(plugin);
        return false;
    }

    outPlugin = plugin;
    return true;
}

void PluginLoader::CloseLoadedPlugin(FLoadedPlugin& plugin)
{
    if (plugin.handle != nullptr)
    {
        CloseLibrary(plugin.handle);
    }
    DeleteShadowFile(plugin.shadowPath);
    plugin = {};
}
