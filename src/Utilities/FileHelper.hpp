#pragma once
#include <random>
#include <filesystem>
#include <string>
#include <map>
#include <fstream>
#include <fmt/printf.h>
#include "ThirdParty/lzav/lzav.h"
#include <assert.h>

namespace Utilities
{
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
            
            // Loading
            void LoadFile(const std::string& entry, std::vector<uint8_t>& outData);

            // Recording
            //void RecordUsage(const std::string& entry);
            //void SaveRecord(const std::string& recordFile);

            // Paking
            void PakAll(const std::string& pakFile, const std::string& srcDir, const std::string& rootPath);
            //void PakFromRecord(const std::string& pakFile, const std::string& recordFile);

            void Reset();
            // UnPak
            void MountPak(const std::string& pakFile);
        private:
            // pak index
            std::map<std::string, FPakEntry> filemaps;
            std::vector<std::string> mountedPaks;
        };

        inline FPackageFileSystem::FPackageFileSystem(EPackageRunMode RunMode)
        {
        }

        inline void FPackageFileSystem::LoadFile(const std::string& entry, std::vector<uint8_t>& outData)
        {
            // pak mounted, read through offset and size
            if (filemaps.find(entry) != filemaps.end()) {
                FPakEntry pakEntry = filemaps[entry];

                auto pakFile = mountedPaks[pakEntry.pkgIdx];
                
                std::ifstream reader(pakFile, std::ios::binary);
                if (!reader.is_open()) {
                    fmt::print("LoadFile: Failed to open pak file: {}\n", "pakfile");
                    return;
                }

                reader.seekg(pakEntry.offset, std::ios::beg);
                outData.resize(pakEntry.size);
                reader.read(reinterpret_cast<char*>(outData.data()), pakEntry.size);
                reader.close();
            }
            else {
                // read from os file
                std::string absEntry = FileHelper::GetPlatformFilePath(entry.c_str());
                std::ifstream reader(absEntry, std::ios::binary);
                if (!reader.is_open()) {
                    fmt::print("LoadFile: Failed to open file: {}\n", entry);
                    return;
                }

                outData = std::vector<uint8_t>(std::istreambuf_iterator<char>(reader), {});
                reader.close();
            }
        }

        inline void FPackageFileSystem::PakAll(const std::string& pakFile, const std::string& srcDir, const std::string& rootPath)
        {
            filemaps.clear();
            
            std::string absSrcPath = FileHelper::GetPlatformFilePath(srcDir.c_str());
            std::string absRootPath = FileHelper::GetPlatformFilePath(rootPath.c_str());

            for (const auto& entry : std::filesystem::recursive_directory_iterator(absSrcPath)) {
                if (entry.is_regular_file()) {
                    std::string entryPath = entry.path().string();
                    std::string entryRelativePath = entryPath.substr(absRootPath.size());
                    std::ranges::replace(entryRelativePath.begin(), entryRelativePath.end(), '\\', '/');

                    std::ifstream reader(entryPath, std::ios::binary);
                    if (!reader.is_open()) {
                        fmt::print("PakAll: Failed to open file: {}\n", entryPath);
                        continue;
                    }
                    reader.seekg(0, std::ios::end);
                    size_t fileSize = reader.tellg();
                    reader.close();
                                        
                    filemaps[entryRelativePath] = {entryRelativePath, 0, 0, static_cast<uint32_t>(fileSize), static_cast<uint32_t>(fileSize)};
                    fmt::print("PakAll: {} -> {}\n", entryRelativePath, entryPath);
                }
            }

            std::ofstream writer(pakFile, std::ios::binary);
            if (!writer.is_open()) {
                fmt::print("PakAll: Failed to open pak file: {}\n", pakFile);
                return;
            }

            writer.write("GNP", 3);
            uint32_t entryCount = static_cast<uint32_t>(filemaps.size());
            writer.write(reinterpret_cast<const char*>(&entryCount), sizeof(uint32_t));

            for (const auto& [key, value] : filemaps) {
                writer.write(value.name.c_str(), value.name.size());
                writer.write("\0", 1);
            }

            auto pos = writer.tellp();

            // pre-write offset and size
            uint32_t offset = static_cast<uint32_t>(pos) + entryCount * 4 * 3;

            writer.seekp(offset);
            // compress and write data
            for (const auto& [key, value] : filemaps) {
                std::ifstream reader(absRootPath + value.name, std::ios::binary);
                if (!reader.is_open()) {
                    fmt::print("PakAll: Failed to open file: {}\n", absRootPath + value.name);
                    continue;
                }

                std::vector<uint8_t> buffer(std::istreambuf_iterator<char>(reader), {});
                writer.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
            }

            // rewrite offset and size
            writer.seekp(pos);
            for (const auto& [key, value] : filemaps) {
                writer.write(reinterpret_cast<const char*>(&offset), sizeof(uint32_t));
                writer.write(reinterpret_cast<const char*>(&value.size), sizeof(uint32_t));
                writer.write(reinterpret_cast<const char*>(&value.uncompressSize), sizeof(uint32_t));
                offset += value.size;
            }
            
            writer.close();
        }

        inline void FPackageFileSystem::Reset()
        {
            filemaps.clear();
            mountedPaks.clear();
        }

        inline void FPackageFileSystem::MountPak(const std::string& pakFile)
        {
            std::ifstream reader(pakFile, std::ios::binary);
            if (!reader.is_open()) {
                fmt::print("MountPak: Failed to open pak file: {}\n", pakFile);
                return;
            }

            reader.seekg(0, std::ios::end);
            size_t fileSize = reader.tellg();
            reader.seekg(0, std::ios::beg);

            char header[4];
            reader.read(header, 3);
            header[3] = '\0';
            if (std::string(header) != "GNP") {
                fmt::print("MountPak: Invalid pak file: {}\n", pakFile);
                return;
            }

            mountedPaks.push_back(pakFile);
            uint32_t pakIdx = static_cast<uint32_t>(mountedPaks.size()) - 1;

            uint32_t entryCount;
            reader.read(reinterpret_cast<char*>(&entryCount), sizeof(uint32_t));

            std::vector<FPakEntry> entries(entryCount); 
            for (uint32_t i = 0; i < entryCount; ++i) {
                char name[256];
                reader.getline(name, 256, '\0');
                entries[i].name = name;
                entries[i].pkgIdx = pakIdx;
            }

            for (auto& entry : entries) {
                reader.read(reinterpret_cast<char*>(&entry.offset), sizeof(uint32_t));
                reader.read(reinterpret_cast<char*>(&entry.size), sizeof(uint32_t));
                reader.read(reinterpret_cast<char*>(&entry.uncompressSize), sizeof(uint32_t));
            }

            // add to maps
            for (auto entry : entries) {
                filemaps[entry.name] = entry;
            }

            reader.close();
        }
    }
}
