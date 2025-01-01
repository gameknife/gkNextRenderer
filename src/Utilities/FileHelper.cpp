#include "FileHelper.hpp"

namespace Utilities
{
    namespace Package
    {
        FPackageFileSystem* FPackageFileSystem::instance_ = nullptr;

        FPackageFileSystem::FPackageFileSystem(EPackageRunMode RunMode): runMode_(RunMode)
        {
            instance_ = this;
        }

        bool FPackageFileSystem::LoadFile(const std::string& entry, std::vector<uint8_t>& outData)
        {
            // pak mounted, read through offset and size
            if(runMode_ == EPM_OsFile || filemaps.find(entry) == filemaps.end())
            {
                std::filesystem::path path(entry);
                std::string absEntry = entry;
                if (!path.is_absolute())
                {
                    absEntry = FileHelper::GetPlatformFilePath(entry.c_str());
                }
                // read from os file
                std::ifstream reader(absEntry, std::ios::binary);
                if (!reader.is_open()) {
                    fmt::print("LoadFile: Failed to open file: {}\n", entry);
                    return false;
                }

                outData = std::vector<uint8_t>(std::istreambuf_iterator<char>(reader), {});
                reader.close();
                return true;
            }

            // from pak
            FPakEntry pakEntry = filemaps[entry];
            auto pakFile = mountedPaks[pakEntry.pkgIdx];
            
            std::ifstream reader(pakFile, std::ios::binary);
            if (!reader.is_open()) {
                fmt::print("LoadFile: Failed to open pak file: {}\n", "pakfile");
                return false;
            }

            void* comp_buf = malloc( pakEntry.size );
            reader.seekg(pakEntry.offset, std::ios::beg);
            reader.read(reinterpret_cast<char*>(comp_buf), pakEntry.size);
            reader.close();

            outData.resize(pakEntry.uncompressSize);
            int l = lzav_decompress( comp_buf, outData.data(), pakEntry.size, pakEntry.uncompressSize );
            free(comp_buf);

            return true;
        }

        void FPackageFileSystem::PakAll(const std::string& pakFile, const std::string& srcDir, const std::string& rootPath, const std::string& regex )
        {
            filemaps.clear();
            
            std::string absSrcPath = FileHelper::GetPlatformFilePath(srcDir.c_str());
            std::string absRootPath = FileHelper::GetPlatformFilePath(rootPath.c_str());

            for (const auto& entry : std::filesystem::recursive_directory_iterator(absSrcPath)) {
                if (entry.is_regular_file()) {
                    std::string entryPath = entry.path().string();
                    std::string entryRelativePath = entryPath.substr(absRootPath.size());
                    std::replace(entryRelativePath.begin(), entryRelativePath.end(), '\\', '/');

                    if (!regex.empty() && !std::regex_match(entryRelativePath, std::regex(regex))) {
                        continue;
                    }
                    
                    std::ifstream reader(entryPath, std::ios::binary);
                    if (!reader.is_open()) {
                        fmt::print("PakAll: Failed to open file: {}\n", entryPath);
                        continue;
                    }
                    reader.seekg(0, std::ios::end);
                    size_t fileSize = reader.tellg();
                    reader.close();
                                        
                    filemaps[entryRelativePath] = {entryRelativePath, 0, 0, static_cast<uint32_t>(fileSize), static_cast<uint32_t>(fileSize)};
                    fmt::print("entry: {} <- {}\n", entryRelativePath, entryPath);
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
            for (auto& [key, value] : filemaps) {
                std::ifstream reader(absRootPath + value.name, std::ios::binary);
                if (!reader.is_open()) {
                    fmt::print("PakAll: Failed to open file: {}\n", absRootPath + value.name);
                    continue;
                }

                std::vector<uint8_t> buffer(std::istreambuf_iterator<char>(reader), {});
                
                int max_len = lzav_compress_bound_hi( static_cast<int>(buffer.size()) );
                void* comp_buf = malloc( max_len );
                int comp_len = lzav_compress_hi( buffer.data(), comp_buf, static_cast<int>(buffer.size()), max_len );

                writer.write(reinterpret_cast<const char*>(comp_buf), comp_len);
                value.size = comp_len;

                free(comp_buf);
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

        void FPackageFileSystem::Reset()
        {
            filemaps.clear();
            mountedPaks.clear();
        }

        void FPackageFileSystem::MountPak(const std::string& pakFile)
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

            fmt::printf("Pak: mount %s with %d entries\n", pakFile.c_str(), entryCount);
        }
    }
}
