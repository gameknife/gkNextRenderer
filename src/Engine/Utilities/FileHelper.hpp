#pragma once
#include <random>
#include <filesystem>
#include <string>
#include <map>
#include <fstream>
#include <fmt/printf.h>
#include "ThirdParty/lzav/lzav.h"
#include <assert.h>
#include <regex>
#include <SDL3/SDL.h>
#include "Engine/Runtime/Platform/PlatformCommon.h"

namespace Utilities
{
    namespace FileHelper
    {
        static std::filesystem::path GetDesktopRuntimeRoot()
        {
            return (NextRenderer::GetExecutableDirectory() / "..").lexically_normal();
        }

        static std::filesystem::path GetRuntimeRoot()
        {
#if ANDROID
            return std::filesystem::path(SDL_GetAndroidExternalStoragePath());
#elif IOS
            return std::filesystem::path(SDL_GetBasePath());
#else
            return GetDesktopRuntimeRoot();
#endif
        }

        static std::filesystem::path GetWritableRuntimeRoot()
        {
#if ANDROID
            return std::filesystem::path(SDL_GetAndroidExternalStoragePath());
#elif IOS
            return std::filesystem::path(SDL_GetPrefPath("gknext", "renderer"));
#else
            return GetDesktopRuntimeRoot();
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
            std::filesystem::create_directories(path);
        }
        
        static std::filesystem::path GetAbsolutePath( const std::filesystem::path& srcPath )
        {
            return std::filesystem::absolute(srcPath);
        }
        
        static std::string GetPlatformFilePath( const char* srcPath )
        {
            return GetRuntimeRoot().append(srcPath).string();
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
            std::filesystem::create_directories(cookedDirectory);
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
        // Game instances use this to bail out gracefully when optional assets (fetched via scripts/fetch-paks)
        // are missing, instead of crashing later in scene/component code.
        static bool IsAssetAvailable(const std::string& relativePath)
        {
            std::error_code ec;
            if (std::filesystem::exists(GetPlatformFilePath(relativePath.c_str()), ec))
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
