#pragma once
#include <random>
#include <filesystem>
#include <string>

namespace Utilities
{
    namespace Package
    {
        enum EPackageRunMode
        {
            EPM_OsFile,
            EPM_PakFile
        };
        
        struct FPakEntry
        {
            uint32_t hash;   
            uint32_t offset;
            uint32_t size;
        };
        
        // PackageFileSystem for Mostly User Oriented Resource, like Texture, Model, etc.
        // Package mass files to one pak
        class FPackageFileSystem
        {
            // Construct
            FPackageFileSystem(EPackageRunMode RunMode, const std::string& pakFile, const std::string& recordFile);
            
            // Loading
            void LoadFile(const std::string& entry, std::vector<uint8_t>& outData);

            // Recording
            void RecordUsage(const std::string& entry);
            void SaveRecord(const std::string& recordFile);

            // Paking
            void PakAll(const std::string& pakFile);
            void PakFromRecord(const std::string& pakFile, const std::string& recordFile);

            // UnPak
            void UnPak();

            // pak index
            std::vector<std::string> names;
            std::vector<FPakEntry> entries;
        };
    }

    
    namespace FileHelper
    {
        static std::string GetPlatformFilePath( const char* srcPath )
        {
#if ANDROID
            return std::string("/sdcard/Android/data/com.gknextrenderer/files/") + srcPath;
#else
            return std::string("../") + srcPath;
#endif
        }

        static std::string GetNormalizedFilePath( const char* srcPath )
        {
            std::string normlizedPath {};
#if ANDROID
            normlizedPath = std::string("/sdcard/Android/data/com.gknextrenderer/files/") + srcPath;
#else
            normlizedPath = std::string("../") + srcPath;
#endif
            std::filesystem::path fullPath(normlizedPath);
            std::filesystem::path directory = fullPath.parent_path();
            std::string pattern = fullPath.filename().string();

            for (const auto& entry : std::filesystem::directory_iterator(directory)) {
                if (entry.is_regular_file() && entry.path().filename().string() == pattern) {
                    normlizedPath =  std::filesystem::absolute(entry.path()).string();
                    break;
                }
            }

            return normlizedPath;
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
}
