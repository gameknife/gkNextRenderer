#include "FileHelper.hpp"
#include <optional>
#include <system_error>
#include <cstring>

namespace Utilities
{
    namespace Package
    {
        namespace
        {
            bool LoadOsFileData(const std::string& entry, std::vector<uint8_t>& outData)
            {
                std::filesystem::path path(entry);
                std::string absEntry = entry;
                if (!path.is_absolute())
                {
                    absEntry = FileHelper::GetPlatformFilePath(entry.c_str());
                }

                std::ifstream reader(absEntry, std::ios::binary);
                if (!reader.is_open())
                {
                    return false;
                }

                reader.seekg(0, std::ios::end);
                size_t fileSize = reader.tellg();
                reader.seekg(0, std::ios::beg);

                outData.resize(fileSize);
                reader.read(reinterpret_cast<char*>(outData.data()), fileSize);
                reader.close();

                return true;
            }

            bool LoadMountedEntryData(
                const std::map<std::string, FPakEntry>& filemaps,
                const std::vector<std::string>& mountedPaks,
                const std::string& entry,
                std::vector<uint8_t>& outData)
            {
                outData.clear();

                auto pakEntryIt = filemaps.find(entry);
                if (pakEntryIt == filemaps.end())
                {
                    return false;
                }

                const FPakEntry& pakEntry = pakEntryIt->second;
                if (pakEntry.pkgIdx >= mountedPaks.size())
                {
                    SPDLOG_ERROR("LoadFile: Invalid pak index for entry: {}", entry);
                    return false;
                }

                const std::string& pakFile = mountedPaks[pakEntry.pkgIdx];
                std::ifstream reader(pakFile, std::ios::binary);
                if (!reader.is_open())
                {
                    SPDLOG_ERROR("LoadFile: Failed to open pak file: {}", pakFile);
                    return false;
                }

                void* compBuf = malloc(pakEntry.size);
                if (compBuf == nullptr)
                {
                    SPDLOG_ERROR("LoadFile: Failed to allocate buffer for entry: {}", entry);
                    return false;
                }

                reader.seekg(pakEntry.offset, std::ios::beg);
                reader.read(reinterpret_cast<char*>(compBuf), pakEntry.size);
                reader.close();

                outData.resize(pakEntry.uncompressSize);
                const int decompressedSize = lzav_decompress(
                    compBuf,
                    outData.data(),
                    pakEntry.size,
                    pakEntry.uncompressSize);
                if (decompressedSize < 0)
                {
                    if (pakEntry.size == pakEntry.uncompressSize)
                    {
                        memcpy(outData.data(), compBuf, pakEntry.size);
                        free(compBuf);
                        return true;
                    }

                    SPDLOG_ERROR("LoadFile: Failed to decompress entry: {}", entry);
                    free(compBuf);
                    return false;
                }

                free(compBuf);
                return true;
            }
        }

        FPackageFileSystem* FPackageFileSystem::instance_ = nullptr;

        FPackageFileSystem::FPackageFileSystem(EPackageRunMode runMode): runMode_(runMode)
        {
            instance_ = this;
        }

        bool FPackageFileSystem::LoadFile(const std::string& entry, std::vector<uint8_t>& outData)
        {
            outData.clear();
            const std::string normalizedEntry = FileHelper::NormalizePathString(entry);
            const bool hasMountedEntry = filemaps.find(normalizedEntry) != filemaps.end();

            if (runMode_ != EPM_OsFile && hasMountedEntry)
            {
                return LoadMountedEntryData(filemaps, mountedPaks, normalizedEntry, outData);
            }

            if (LoadOsFileData(normalizedEntry, outData))
            {
                return true;
            }

            if (hasMountedEntry)
            {
                return LoadMountedEntryData(filemaps, mountedPaks, normalizedEntry, outData);
            }

            SPDLOG_ERROR("LoadFile: Failed to open file: {}", normalizedEntry);
            return false;
        }

        bool FPackageFileSystem::LoadMountedFile(const std::string& entry, std::vector<uint8_t>& outData) const
        {
            return LoadMountedEntryData(filemaps, mountedPaks, FileHelper::NormalizePathString(entry), outData);
        }

        bool FPackageFileSystem::HasMountedEntry(const std::string& entry) const
        {
            return filemaps.find(FileHelper::NormalizePathString(entry)) != filemaps.end();
        }

        std::vector<std::string> FPackageFileSystem::ListMountedEntries(const std::string& prefix) const
        {
            std::vector<std::string> entries;
            entries.reserve(filemaps.size());
            const std::string normalizedPrefix = FileHelper::NormalizePathString(prefix);

            for (const auto& [name, pakEntry] : filemaps)
            {
                (void)pakEntry;
                if (normalizedPrefix.empty() || name.rfind(normalizedPrefix, 0) == 0)
                {
                    entries.push_back(name);
                }
            }

            return entries;
        }

        void FPackageFileSystem::PakAll(const std::string& pakFile, const std::string& srcDir, const std::string& rootPath, const std::string& regex, bool enableCompression, const std::string& manifestPath )
        {
            filemaps.clear();

            auto resolvePath = [](const std::string& path) {
                return std::filesystem::absolute(FileHelper::GetPlatformFilePath(path.c_str()));
            };

            std::filesystem::path absSrcPath = resolvePath(srcDir);
            std::filesystem::path absRootPath = resolvePath(rootPath);

            if (!std::filesystem::exists(absSrcPath))
            {
                SPDLOG_ERROR("PakAll: Source directory does not exist: {}", absSrcPath.string());
                return;
            }

            if (!std::filesystem::exists(absRootPath))
            {
                SPDLOG_ERROR("PakAll: Root directory does not exist: {}", absRootPath.string());
                return;
            }

            std::optional<std::regex> filterRegex;
            if (!regex.empty())
            {
                try
                {
                    filterRegex.emplace(regex);
                }
                catch (const std::regex_error& e)
                {
                    SPDLOG_ERROR("PakAll: Invalid regex '{}': {}", regex, e.what());
                    return;
                }
            }

            for (const auto& entry : std::filesystem::recursive_directory_iterator(absSrcPath)) {
                if (entry.is_regular_file()) {
                    std::filesystem::path entryPath = entry.path();
                    std::error_code relativeError;
                    std::filesystem::path entryRelativePath = std::filesystem::relative(entryPath, absRootPath, relativeError);
                    if (relativeError)
                    {
                        SPDLOG_WARN("PakAll: Failed to relativize {} against {}: {}", entryPath.string(), absRootPath.string(), relativeError.message());
                        continue;
                    }

                    std::string entryRelativePathString = FileHelper::NormalizePathString(entryRelativePath);

                    if (entryRelativePathString.empty())
                    {
                        SPDLOG_WARN("PakAll: Skipping empty relative path for {}", entryPath.string());
                        continue;
                    }

                    if (filterRegex.has_value() && !std::regex_match(entryRelativePathString, filterRegex.value())) {
                        continue;
                    }

                    if (!entryRelativePathString.empty() && entryRelativePathString.rfind("..", 0) == 0)
                    {
                        SPDLOG_WARN("PakAll: Skipping file outside root: {}", entryPath.string());
                        continue;
                    }

                    std::ifstream reader(entryPath, std::ios::binary);
                    if (!reader.is_open()) {
                        SPDLOG_ERROR("PakAll: Failed to open file: {}", entryPath.string());
                        continue;
                    }
                    reader.seekg(0, std::ios::end);
                    size_t fileSize = reader.tellg();
                    reader.close();

                    filemaps[entryRelativePathString] = {entryRelativePathString, 0, 0, static_cast<uint32_t>(fileSize), static_cast<uint32_t>(fileSize)};
                    SPDLOG_INFO("entry: {} <- {}", entryRelativePathString, entryPath.string());
                }
            }

            std::ofstream writer(pakFile, std::ios::binary);
            if (!writer.is_open()) {
                SPDLOG_ERROR("PakAll: Failed to open pak file: {}", pakFile);
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
                std::filesystem::path sourcePath = absRootPath / value.name;
                std::ifstream reader(sourcePath, std::ios::binary);
                if (!reader.is_open()) {
                    SPDLOG_ERROR("PakAll: Failed to open file: {}", sourcePath.string());
                    continue;
                }

                reader.seekg(0, std::ios::end);
                size_t fileSize = reader.tellg();
                reader.seekg(0, std::ios::beg);

                std::vector<uint8_t> buffer(fileSize);
                reader.read(reinterpret_cast<char*>(buffer.data()), fileSize);
                reader.close();

                if (enableCompression)
                {
                    int maxLen = lzav_compress_bound_hi( static_cast<int>(buffer.size()) );
                    void* compBuf = malloc( maxLen );
                    int compLen = lzav_compress_hi( buffer.data(), compBuf, static_cast<int>(buffer.size()), maxLen );

                    writer.write(reinterpret_cast<const char*>(compBuf), compLen);
                    value.size = compLen;

                    free(compBuf);
                }
                else
                {
                    writer.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
                    value.size = static_cast<uint32_t>(buffer.size());
                }
            }

            // rewrite offset and size
            writer.seekp(pos);
            for (auto& [key, value] : filemaps) {
                value.offset = offset;
                writer.write(reinterpret_cast<const char*>(&offset), sizeof(uint32_t));
                writer.write(reinterpret_cast<const char*>(&value.size), sizeof(uint32_t));
                writer.write(reinterpret_cast<const char*>(&value.uncompressSize), sizeof(uint32_t));
                offset += value.size;
            }

            writer.close();

            if (!manifestPath.empty())
            {
                std::filesystem::path manifest(manifestPath);
                if (!manifest.is_absolute())
                {
                    manifest = std::filesystem::absolute(manifest);
                }

                if (manifest.has_parent_path())
                {
                    FileHelper::EnsureDirectoryExists(manifest.parent_path());
                }

                std::ofstream manifestWriter(manifest, std::ios::binary);
                if (!manifestWriter.is_open())
                {
                    SPDLOG_ERROR("PakAll: Failed to open manifest for writing: {}", manifest.string());
                }
                else
                {
                    manifestWriter << "{\n  \"entries\": [\n";
                    bool first = true;
                    for (const auto& [key, value] : filemaps)
                    {
                        if (!first)
                        {
                            manifestWriter << ",\n";
                        }
                        first = false;
                        manifestWriter << "    {\n"
                                       << "      \"name\": \"" << value.name << "\",\n"
                                       << "      \"offset\": " << value.offset << ",\n"
                                       << "      \"size\": " << value.size << ",\n"
                                       << "      \"uncompressedSize\": " << value.uncompressSize << ",\n"
                                       << "      \"compressed\": " << (enableCompression ? "true" : "false") << "\n"
                                       << "    }";
                    }
                    manifestWriter << "\n  ]\n}\n";
                }
            }
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
                SPDLOG_ERROR("MountPak: Failed to open pak file: {}", pakFile);
                return;
            }

            reader.seekg(0, std::ios::end);
            size_t fileSize = reader.tellg();
            reader.seekg(0, std::ios::beg);

            char header[4];
            reader.read(header, 3);
            header[3] = '\0';
            if (std::string(header) != "GNP") {
                SPDLOG_ERROR("MountPak: Invalid pak file: {}", pakFile);
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
                entries[i].name = FileHelper::NormalizePathString(std::string(name));
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

            SPDLOG_INFO("Pak: mount {} with {} entries", pakFile.c_str(), entryCount);
        }
    }
}
