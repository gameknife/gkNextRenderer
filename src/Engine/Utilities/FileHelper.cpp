#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include <optional>
#include <system_error>
#include <cstring>
#include <mutex>
#include <set>

namespace Utilities
{
    namespace FileHelper
    {
        namespace
        {
            std::mutex assetTraceMutex;
            std::filesystem::path assetTracePath;
            std::set<std::string> tracedAssets;

            bool IsTraceableAsset(const std::string& relativePath)
            {
                if (relativePath.rfind("assets/", 0) != 0)
                {
                    return false;
                }
                // Runtime assets live below a named asset directory. Build-system
                // stamps and cmake_install.cmake sit directly under assets/ and must
                // never leak into a coverage-derived package.
                if (relativePath.find('/', std::string("assets/").size()) == std::string::npos)
                {
                    return false;
                }
                const std::filesystem::path assetPath(relativePath);
                return assetPath.extension() != ".pak" && assetPath.extension() != ".stamp";
            }
        }

        void SetAssetTracePath(const std::filesystem::path& tracePath)
        {
            std::scoped_lock lock(assetTraceMutex);
            assetTracePath = tracePath;
            tracedAssets.clear();
            if (!assetTracePath.empty() && assetTracePath.has_parent_path())
            {
                std::error_code errorCode;
                std::filesystem::create_directories(assetTracePath.parent_path(), errorCode);
            }
        }

        void RecordAssetReference(const std::filesystem::path& path, bool knownToExist)
        {
            std::scoped_lock lock(assetTraceMutex);
            if (assetTracePath.empty())
            {
                return;
            }

            std::filesystem::path absolutePath = path;
            if (!absolutePath.is_absolute())
            {
                absolutePath = GetRuntimeRoot() / absolutePath;
            }
            absolutePath = absolutePath.lexically_normal();

            std::error_code errorCode;
            const bool isFile = std::filesystem::is_regular_file(absolutePath, errorCode);
            if (!knownToExist && !isFile)
            {
                return;
            }

            std::filesystem::path relativePath = std::filesystem::relative(absolutePath, GetRuntimeRoot(), errorCode);
            if (errorCode)
            {
                return;
            }
            const std::string normalized = NormalizePathString(relativePath);
            if (!IsTraceableAsset(normalized) || normalized.rfind("..", 0) == 0 || !tracedAssets.insert(normalized).second)
            {
                return;
            }

            std::ofstream writer(assetTracePath, std::ios::binary | std::ios::app);
            if (writer.is_open())
            {
                writer << normalized << '\n';
                writer.flush();
            }
        }
    }

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
                const bool loaded = LoadMountedEntryData(filemaps, mountedPaks, normalizedEntry, outData);
                if (loaded)
                {
                    FileHelper::RecordAssetReference(normalizedEntry, true);
                }
                return loaded;
            }

            if (LoadOsFileData(normalizedEntry, outData))
            {
                FileHelper::RecordAssetReference(normalizedEntry, true);
                return true;
            }

            if (hasMountedEntry)
            {
                const bool loaded = LoadMountedEntryData(filemaps, mountedPaks, normalizedEntry, outData);
                if (loaded)
                {
                    FileHelper::RecordAssetReference(normalizedEntry, true);
                }
                return loaded;
            }

            SPDLOG_ERROR("LoadFile: Failed to open file: {}", normalizedEntry);
            return false;
        }

        bool FPackageFileSystem::LoadMountedFile(const std::string& entry, std::vector<uint8_t>& outData) const
        {
            const std::string normalizedEntry = FileHelper::NormalizePathString(entry);
            const bool loaded = LoadMountedEntryData(filemaps, mountedPaks, normalizedEntry, outData);
            if (loaded)
            {
                FileHelper::RecordAssetReference(normalizedEntry, true);
            }
            return loaded;
        }

        bool FPackageFileSystem::HasMountedEntry(const std::string& entry) const
        {
            const std::string normalizedEntry = FileHelper::NormalizePathString(entry);
            return filemaps.find(normalizedEntry) != filemaps.end();
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

        bool FPackageFileSystem::PakFromList(const std::string& pakFile, const std::string& rootPath, const std::string& listPath, bool enableCompression, const std::string& manifestPath)
        {
            const std::filesystem::path absoluteRoot = std::filesystem::absolute(FileHelper::GetPlatformFilePath(rootPath.c_str()));
            std::ifstream listReader(listPath);
            if (!listReader.is_open())
            {
                SPDLOG_ERROR("PakFromList: Failed to open asset list: {}", listPath);
                return false;
            }

            std::set<std::string> requestedEntries;
            std::string line;
            while (std::getline(listReader, line))
            {
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                const std::string normalized = FileHelper::NormalizePathString(line);
                if (!normalized.empty() && normalized.rfind("assets/", 0) == 0 && normalized.rfind("..", 0) != 0 && std::filesystem::path(normalized).extension() != ".pak")
                {
                    requestedEntries.insert(normalized);
                }
            }
            if (requestedEntries.empty())
            {
                SPDLOG_ERROR("PakFromList: Asset list contains no usable entries: {}", listPath);
                return false;
            }

            // Mount source paks so a trace produced while reading optional assets can
            // be flattened into the new standalone pak instead of nesting whole paks.
            const std::filesystem::path sourcePakDirectory = absoluteRoot / "assets" / "paks";
            const std::filesystem::path absoluteOutputPak = std::filesystem::absolute(pakFile).lexically_normal();
            std::error_code iterationError;
            if (std::filesystem::exists(sourcePakDirectory, iterationError))
            {
                for (const auto& item : std::filesystem::directory_iterator(sourcePakDirectory, iterationError))
                {
                    if (item.is_regular_file() && item.path().extension() == ".pak" &&
                        std::filesystem::absolute(item.path()).lexically_normal() != absoluteOutputPak)
                    {
                        MountPak(item.path().string());
                    }
                }
            }

            std::map<std::string, std::vector<uint8_t>> payloads;
            bool complete = true;
            for (const std::string& name : requestedEntries)
            {
                std::vector<uint8_t> data;
                const std::filesystem::path source = absoluteRoot / name;
                std::ifstream reader(source, std::ios::binary);
                if (reader.is_open())
                {
                    reader.seekg(0, std::ios::end);
                    const size_t size = static_cast<size_t>(reader.tellg());
                    reader.seekg(0, std::ios::beg);
                    data.resize(size);
                    reader.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
                }
                else if (!LoadMountedEntryData(filemaps, mountedPaks, name, data))
                {
                    SPDLOG_ERROR("PakFromList: Traced asset is unavailable: {}", name);
                    complete = false;
                    continue;
                }
                payloads.emplace(name, std::move(data));
            }
            if (!complete)
            {
                return false;
            }

            filemaps.clear();
            mountedPaks.clear();
            for (const auto& [name, data] : payloads)
            {
                filemaps[name] = {name, 0, 0, static_cast<uint32_t>(data.size()), static_cast<uint32_t>(data.size())};
            }

            std::ofstream writer(pakFile, std::ios::binary);
            if (!writer.is_open())
            {
                SPDLOG_ERROR("PakFromList: Failed to open output pak: {}", pakFile);
                return false;
            }
            writer.write("GNP", 3);
            const uint32_t entryCount = static_cast<uint32_t>(filemaps.size());
            writer.write(reinterpret_cast<const char*>(&entryCount), sizeof(uint32_t));
            for (const auto& [key, value] : filemaps)
            {
                writer.write(value.name.c_str(), static_cast<std::streamsize>(value.name.size()));
                writer.write("\0", 1);
            }

            const auto indexPosition = writer.tellp();
            uint32_t offset = static_cast<uint32_t>(indexPosition) + entryCount * 4 * 3;
            writer.seekp(offset);
            for (auto& [name, value] : filemaps)
            {
                const std::vector<uint8_t>& data = payloads.at(name);
                if (enableCompression && !data.empty())
                {
                    const int maximumSize = lzav_compress_bound_hi(static_cast<int>(data.size()));
                    std::vector<uint8_t> compressed(static_cast<size_t>(maximumSize));
                    const int compressedSize = lzav_compress_hi(data.data(), compressed.data(), static_cast<int>(data.size()), maximumSize);
                    writer.write(reinterpret_cast<const char*>(compressed.data()), compressedSize);
                    value.size = static_cast<uint32_t>(compressedSize);
                }
                else
                {
                    writer.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
                    value.size = static_cast<uint32_t>(data.size());
                }
            }
            writer.seekp(indexPosition);
            for (auto& [key, value] : filemaps)
            {
                value.offset = offset;
                writer.write(reinterpret_cast<const char*>(&value.offset), sizeof(uint32_t));
                writer.write(reinterpret_cast<const char*>(&value.size), sizeof(uint32_t));
                writer.write(reinterpret_cast<const char*>(&value.uncompressSize), sizeof(uint32_t));
                offset += value.size;
            }
            writer.close();

            if (!manifestPath.empty())
            {
                std::ofstream manifestWriter(manifestPath, std::ios::binary);
                manifestWriter << "{\n  \"entries\": [\n";
                bool first = true;
                for (const auto& [key, value] : filemaps)
                {
                    if (!first) manifestWriter << ",\n";
                    first = false;
                    manifestWriter << "    {\"name\": \"" << value.name << "\", \"size\": " << value.size
                                   << ", \"uncompressedSize\": " << value.uncompressSize << "}";
                }
                manifestWriter << "\n  ]\n}\n";
            }
            SPDLOG_INFO("PakFromList: wrote {} entries to {}", entryCount, pakFile);
            return true;
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

    namespace FileHelper
    {
        std::string ResolvePlatformFilePath(const char* srcPath)
        {
            const std::filesystem::path relativePath = std::filesystem::path(srcPath).lexically_normal();
            const std::filesystem::path diskPath = (GetRuntimeRoot() / relativePath).lexically_normal();
            std::error_code errorCode;
            if (std::filesystem::is_regular_file(diskPath, errorCode))
            {
                RecordAssetReference(diskPath, true);
                return diskPath.string();
            }
            if (std::filesystem::is_directory(diskPath, errorCode))
            {
                // Directory discovery is not asset consumption. The caller may only
                // be building a scene/content-browser list, so do not trace children.
                return diskPath.string();
            }

            auto* packageSystem = Package::FPackageFileSystem::TryGetInstance();
            if (packageSystem == nullptr || relativePath.is_absolute())
            {
                return diskPath.string();
            }

            const std::string normalized = NormalizePathString(relativePath);
            std::vector<std::string> entries;
            if (packageSystem->HasMountedEntry(normalized))
            {
                entries.push_back(normalized);
            }
            else
            {
                std::string prefix = normalized;
                if (!prefix.empty() && prefix.back() != '/')
                {
                    prefix.push_back('/');
                }
                entries = packageSystem->ListMountedEntries(prefix);
            }
            if (entries.empty())
            {
                return diskPath.string();
            }

            const std::filesystem::path cacheRoot = GetWritableRuntimeRoot() / "asset-cache";
            for (const std::string& entry : entries)
            {
                if (!IsTraceableAsset(entry) || entry.rfind("..", 0) == 0)
                {
                    SPDLOG_WARN("Pak: refusing to materialize unsafe entry '{}'", entry);
                    continue;
                }
                std::vector<uint8_t> data;
                if (!packageSystem->LoadMountedFile(entry, data))
                {
                    continue;
                }
                const std::filesystem::path destination = (cacheRoot / entry).lexically_normal();
                std::filesystem::create_directories(destination.parent_path(), errorCode);
                std::ofstream writer(destination, std::ios::binary | std::ios::trunc);
                writer.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
            }
            return (cacheRoot / relativePath).lexically_normal().string();
        }
    }
}
