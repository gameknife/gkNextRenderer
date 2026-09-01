#pragma once
#include <random>
#include <filesystem>
#include <string>
#include <map>
#include <fstream>
#include <fmt/printf.h>
#include <spdlog/spdlog.h>
#include "ThirdParty/lzav/lzav.h"
#include <assert.h>
#include <regex>
#include <SDL3/SDL.h>
#include "Engine/Runtime/Platform/PlatformCommon.hpp"

namespace Utilities
{
    namespace FileHelper
    {
        // Runtime asset tracing is opt-in. The trace contains normalized project-root
        // relative paths, one per line, and is consumed by `gnb package --asset-trace`.
        // Writes and files outside assets/ are deliberately ignored.
        void SetAssetTracePath(const std::filesystem::path& tracePath);
        void RecordAssetReference(const std::filesystem::path& path, bool knownToExist = false);
        std::string ResolvePlatformFilePath(const char* srcPath);

        static std::filesystem::path GetDesktopRuntimeRoot()
        {
            return (NextRenderer::GetExecutableDirectory() / "..").lexically_normal();
        }

        static std::filesystem::path GetRuntimeRoot()
        {
#if ANDROID
            if (const char* externalStoragePath = SDL_GetAndroidExternalStoragePath();
                externalStoragePath != nullptr && externalStoragePath[0] != '\0')
            {
                return std::filesystem::path(externalStoragePath);
            }
            return std::filesystem::temp_directory_path();
#elif IOS
            if (const char* basePath = SDL_GetBasePath(); basePath != nullptr && basePath[0] != '\0')
            {
                return std::filesystem::path(basePath);
            }
            return std::filesystem::temp_directory_path();
#else
            return GetDesktopRuntimeRoot();
#endif
        }

        // Returns the loose-file location without consulting mounted paks or
        // materializing package entries. Use this for discovery/probes and for
        // callers that already have an in-memory fallback.
        static std::filesystem::path GetRuntimeFilePath(const std::filesystem::path& srcPath)
        {
            if (srcPath.is_absolute())
            {
                return srcPath.lexically_normal();
            }
            return (GetRuntimeRoot() / srcPath).lexically_normal();
        }

        // Portable mode keeps every writable artifact next to the executable. It is
        // opt-in through a `portable.txt` marker in the runtime root, because the usual
        // install location for a release build (Program Files, a read-only mount, a
        // network share) cannot be written to.
        inline bool IsPortableMode()
        {
            static const bool portable = []
            {
                std::error_code errorCode;
                return std::filesystem::exists(GetDesktopRuntimeRoot() / "portable.txt", errorCode);
            }();
            return portable;
        }

        inline std::filesystem::path GetDesktopWritableRoot()
        {
            if (IsPortableMode())
            {
                return GetDesktopRuntimeRoot();
            }

            static const std::filesystem::path preferencesRoot = []
            {
                char* preferences = SDL_GetPrefPath("gkNext", NextRenderer::GetApplicationIdentity().c_str());
                if (preferences == nullptr)
                {
                    // SDL could not resolve the platform preference directory. Keep writes
                    // functional even when the executable is installed read-only.
                    return std::filesystem::temp_directory_path() / "gkNextRenderer" /
                        NextRenderer::GetApplicationIdentity();
                }
                std::filesystem::path resolved = std::filesystem::path(preferences).lexically_normal();
                SDL_free(preferences);
                return resolved;
            }();
            return preferencesRoot;
        }

        static std::filesystem::path GetWritableRuntimeRoot()
        {
#if ANDROID
            const char* externalStoragePath = SDL_GetAndroidExternalStoragePath();
            if (externalStoragePath != nullptr && externalStoragePath[0] != '\0')
            {
                return std::filesystem::path(externalStoragePath);
            }
            return std::filesystem::temp_directory_path() / "gkNextRenderer" /
                NextRenderer::GetApplicationIdentity();
#elif IOS
            static const std::filesystem::path preferencesRoot = []
            {
                char* preferences = SDL_GetPrefPath("gkNextRenderer", NextRenderer::GetApplicationIdentity().c_str());
                if (preferences == nullptr || preferences[0] == '\0')
                {
                    SPDLOG_ERROR("Cannot resolve iOS writable application directory: {}", SDL_GetError());
                    if (preferences != nullptr)
                    {
                        SDL_free(preferences);
                    }
                    return std::filesystem::temp_directory_path() / "gkNextRenderer" /
                        NextRenderer::GetApplicationIdentity();
                }

                const std::filesystem::path resolved = std::filesystem::path(preferences).lexically_normal();
                SDL_free(preferences);
                return resolved;
            }();
            return preferencesRoot;
#else
            return GetDesktopWritableRoot();
#endif
        }

        static std::string NormalizePathString(const std::filesystem::path& srcPath)
        {
            std::string normalized = srcPath.lexically_normal().generic_string();
            if (normalized.rfind("./", 0) == 0)
            {
                normalized.erase(0, 2);
            }

            return normalized;
        }

        static std::string NormalizePathString(const std::string& srcPath)
        {
            return NormalizePathString(std::filesystem::path(srcPath));
        }

        static void EnsureDirectoryExists(const std::filesystem::path& path)
        {
            std::error_code errorCode;
            std::filesystem::create_directories(path, errorCode);
            if (errorCode)
            {
                SPDLOG_WARN("Failed to create directory {}: {}", path.string(), errorCode.message());
            }
        }
        
        static std::filesystem::path GetAbsolutePath( const std::filesystem::path& srcPath )
        {
            return std::filesystem::absolute(srcPath);
        }
        
        static std::string GetPlatformFilePath( const char* srcPath )
        {
            return ResolvePlatformFilePath(srcPath);
        }

        // Resolves application-generated data. Relative paths are always rooted in the
        // per-application writable directory; absolute paths are preserved for explicit
        // user-selected export destinations.
        static std::filesystem::path ResolveWritablePath(const std::filesystem::path& srcPath)
        {
            if (srcPath.is_absolute())
            {
                return srcPath.lexically_normal();
            }

            // Relative application paths must not be able to escape the per-app data root.
            // Normalize ordinary `a/../b` segments, and discard leading parent segments from
            // untrusted/user-provided relative output names.
            const std::filesystem::path normalized = srcPath.lexically_normal();
            std::filesystem::path safeRelative;
            bool escapedRoot = false;
            for (const std::filesystem::path& component : normalized)
            {
                if (component == "." || component.empty())
                {
                    continue;
                }
                if (component == "..")
                {
                    escapedRoot = true;
                    continue;
                }
                safeRelative /= component;
            }
            if (escapedRoot)
            {
                SPDLOG_WARN("Ignoring parent segments in writable path {}", srcPath.string());
            }
            return (GetWritableRuntimeRoot() / safeRelative).lexically_normal();
        }

        static std::filesystem::path GetWritableDirectoryPath(const std::filesystem::path& relativePath)
        {
            const std::filesystem::path directory = ResolveWritablePath(relativePath);
            std::error_code errorCode;
            std::filesystem::create_directories(directory, errorCode);
            if (errorCode)
            {
                SPDLOG_WARN("Failed to create writable directory {}: {}", directory.string(), errorCode.message());
            }
            return directory;
        }

        // Path for data the application produces (settings, logs, screenshots, layout).
        // Always use this instead of GetPlatformFilePath for writes: the runtime root is
        // read-only for an installed release.
        static std::string GetWritableFilePath( const char* srcPath )
        {
            const std::filesystem::path fullPath = ResolveWritablePath(std::filesystem::path(srcPath));
            std::error_code errorCode;
            std::filesystem::create_directories(fullPath.parent_path(), errorCode);
            if (errorCode)
            {
                SPDLOG_WARN("Failed to create writable parent directory for {}: {}",
                            fullPath.string(), errorCode.message());
            }
            return fullPath.string();
        }

        // Reads user data written by GetWritableFilePath, falling back to the runtime
        // root so existing portable installs and in-repo development keep working.
        static std::string ResolveWritableFileForRead( const char* srcPath )
        {
            const std::filesystem::path writablePath = ResolveWritablePath(std::filesystem::path(srcPath));
            std::error_code errorCode;
            if (std::filesystem::exists(writablePath, errorCode))
            {
                return writablePath.string();
            }
            return GetPlatformFilePath(srcPath);
        }

        static std::string GetNormalizedFilePath( const char* srcPath )
        {
            std::string normalizedPath = GetRuntimeRoot().append(srcPath).string();
            std::filesystem::path fullPath(normalizedPath);
            std::filesystem::path directory = fullPath.parent_path();
            std::string pattern = fullPath.filename().string();

            if ( std::filesystem::exists(directory) )
            {
                for (const auto& entry : std::filesystem::directory_iterator(directory)) {
                    if (entry.is_regular_file() && entry.path().filename().string() == pattern) {
                        normalizedPath =  std::filesystem::absolute(entry.path()).string();
                        RecordAssetReference(entry.path(), true);
                        break;
                    }
                }    
            }
            
            return normalizedPath;
        }
    }

    namespace NameHelper
    {
        static std::string RandomName(size_t length)
        {
            const std::string characters = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
            std::random_device rd;
            std::mt19937 generator(rd());
            std::uniform_int_distribution<> distribution(0, static_cast<int>(characters.size()) - 1);

            std::string randomName;
            for (size_t i = 0; i < length; ++i) {
                randomName += characters[distribution(generator)];
            }

            return randomName;
        }
    }

    namespace CookHelper
    {
        static std::string GetCookedFileName(const std::string& filehash, const std::string& cooktype)
        {
            const std::filesystem::path cookedDirectory = FileHelper::GetWritableRuntimeRoot() / "cooked";
            std::error_code errorCode;
            std::filesystem::create_directories(cookedDirectory, errorCode);
            if (errorCode)
            {
                SPDLOG_WARN("Failed to create cooked cache directory {}: {}",
                            cookedDirectory.string(), errorCode.message());
            }
            return (cookedDirectory / (cooktype + filehash + ".gncook")).string();
        }
    }
    
    namespace Package
    {
        enum EPackageRunMode
        {
            EPM_OsFile,
            EPM_PakFile
        };
        
        struct FPakEntry
        {
            std::string name;
            uint32_t pkgIdx;
            uint32_t offset;
            uint32_t size;
            uint32_t uncompressSize;
        };
        
        // PackageFileSystem for Mostly User Oriented Resource, like Texture, Model, etc.
        // Package mass files to one pak
        class FPackageFileSystem
        {
        public:
            // Construct
            FPackageFileSystem(EPackageRunMode RunMode);
            ~FPackageFileSystem();

            void SetRunMode(EPackageRunMode RunMode) { runMode_ = RunMode; }
            
            // Loading
            void Reset();
            void MountPak(const std::string& pakFile);
            bool LoadFile(const std::string& entry, std::vector<uint8_t>& outData);
            bool LoadMountedFile(const std::string& entry, std::vector<uint8_t>& outData) const;
            bool HasMountedEntry(const std::string& entry) const;
            std::vector<std::string> ListMountedEntries(const std::string& prefix = "") const;
            
            // Recording
            //void RecordUsage(const std::string& entry);
            //void SaveRecord(const std::string& recordFile);
            //void PakFromRecord(const std::string& pakFile, const std::string& recordFile);
            
            // Paking
            void PakAll(const std::string& pakFile, const std::string& srcDir, const std::string& rootPath, const std::string& regex = "", bool enableCompression = true, const std::string& manifestPath = "");
            bool PakFromList(const std::string& pakFile, const std::string& rootPath, const std::string& listPath, bool enableCompression = true, const std::string& manifestPath = "");

            static FPackageFileSystem& GetInstance()
            {
                return *instance_;
            }

            static FPackageFileSystem* TryGetInstance()
            {
                return instance_;
            }
        private:
            // pak index
            std::map<std::string, FPakEntry> filemaps;
            std::vector<std::string> mountedPaks;
            EPackageRunMode runMode_;

            static FPackageFileSystem* instance_;
        };
    }

    namespace FileHelper
    {
        // Returns true if a relative asset path can be resolved either from disk or from any mounted pak.
        // Game instances use this to bail out gracefully when optional assets (fetched via gnb paks fetch)
        // are missing, instead of crashing later in scene/component code.
        static bool IsAssetAvailable(const std::string& relativePath)
        {
            std::error_code ec;
            // Availability checks are probes, not reads. Avoid GetPlatformFilePath()
            // here because resolving a concrete disk file is intentionally traced.
            if (std::filesystem::exists(GetRuntimeRoot() / relativePath, ec))
            {
                return true;
            }
            if (auto* pkg = Package::FPackageFileSystem::TryGetInstance())
            {
                return pkg->HasMountedEntry(relativePath);
            }
            return false;
        }
    }
}
