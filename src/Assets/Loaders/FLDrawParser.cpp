#include "Assets/Loaders/FLDrawParser.h"
#include "Utilities/FileHelper.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>

namespace Assets
{
    namespace
    {
        constexpr uint32_t kLDrawPartCacheMagic = 0x4C445043; // 'LDPC'
        constexpr uint32_t kLDrawPartCacheVersion = 1;

        struct LDrawPartCacheHeader
        {
            uint32_t magic = kLDrawPartCacheMagic;
            uint32_t version = kLDrawPartCacheVersion;
            uint64_t originalSize = 0;
            uint64_t compressedSize = 0;
            uint64_t dataHash = 0;
            uint32_t dependencyCount = 0;
            uint32_t reserved = 0;
        };

        struct LDrawPartCacheDependency
        {
            int64_t writeTime = 0;
            uint64_t fileSize = 0;
            uint32_t pathSize = 0;
            uint32_t reserved = 0;
        };

        std::string ToLowerKey(const std::string& s)
        {
            std::string result = s;
            std::transform(result.begin(), result.end(), result.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return result;
        }

        uint64_t HashBuffer(const uint8_t* data, size_t size)
        {
            constexpr uint64_t fnvOffset = 1469598103934665603ull;
            constexpr uint64_t fnvPrime = 1099511628211ull;

            uint64_t hash = fnvOffset;
            for (size_t i = 0; i < size; ++i)
            {
                hash ^= static_cast<uint64_t>(data[i]);
                hash *= fnvPrime;
            }

            return hash;
        }

        uint64_t HashString(const std::string& text)
        {
            return HashBuffer(reinterpret_cast<const uint8_t*>(text.data()), text.size());
        }

        std::string NormalizeCachePath(const std::string& filepath)
        {
            std::error_code ec;
            std::filesystem::path path = std::filesystem::absolute(filepath, ec);
            if (ec)
            {
                path = std::filesystem::path(filepath);
            }

            path = path.lexically_normal();
            return path.generic_string();
        }

        std::string BuildCacheFileName(const std::string& normalizedPath, bool expandSubfiles)
        {
            const std::string cookType = expandSubfiles ? "ldpart" : "ldpartd";
            return Utilities::CookHelper::GetCookedFileName(
                fmt::format("{:016x}", HashString(normalizedPath)),
                cookType);
        }

        bool AppendUniqueDependency(std::vector<std::string>& dependencies, const std::string& path)
        {
            if (path.empty())
            {
                return false;
            }

            if (std::find(dependencies.begin(), dependencies.end(), path) != dependencies.end())
            {
                return false;
            }

            dependencies.push_back(path);
            return true;
        }

        bool QueryFileSignature(const std::string& path, uint64_t& fileSize, int64_t& writeTime)
        {
            std::error_code ec;
            if (!std::filesystem::exists(path, ec) || ec)
            {
                return false;
            }

            fileSize = std::filesystem::file_size(path, ec);
            if (ec)
            {
                return false;
            }

            const auto lastWriteTime = std::filesystem::last_write_time(path, ec);
            if (ec)
            {
                return false;
            }

            writeTime = static_cast<int64_t>(lastWriteTime.time_since_epoch().count());
            return true;
        }

        bool HasFilesystemSignature(const std::string& path)
        {
            uint64_t fileSize = 0;
            int64_t writeTime = 0;
            return QueryFileSignature(path, fileSize, writeTime);
        }

        void SerializeTemplate(const LDrawPartTemplate& tmpl, std::vector<uint8_t>& outData)
        {
            outData.clear();

            auto writeBytes = [&outData](const void* src, size_t size)
            {
                const uint8_t* bytes = static_cast<const uint8_t*>(src);
                outData.insert(outData.end(), bytes, bytes + size);
            };

            const uint32_t filenameSize = static_cast<uint32_t>(tmpl.filename.size());
            writeBytes(&filenameSize, sizeof(filenameSize));
            if (filenameSize > 0)
            {
                writeBytes(tmpl.filename.data(), filenameSize);
            }

            const uint64_t faceCount = static_cast<uint64_t>(tmpl.faces.size());
            writeBytes(&faceCount, sizeof(faceCount));

            for (const LDrawFace& face : tmpl.faces)
            {
                writeBytes(&face.vertexCount, sizeof(face.vertexCount));
                writeBytes(&face.colorCode, sizeof(face.colorCode));

                for (int vertexIndex = 0; vertexIndex < 4; ++vertexIndex)
                {
                    const glm::vec3 vertex =
                        vertexIndex < face.vertexCount ? face.vertices[vertexIndex] : glm::vec3(0.0f);
                    writeBytes(&vertex.x, sizeof(vertex.x));
                    writeBytes(&vertex.y, sizeof(vertex.y));
                    writeBytes(&vertex.z, sizeof(vertex.z));
                }
            }
        }

        bool DeserializeTemplate(const std::vector<uint8_t>& data, LDrawPartTemplate& tmpl)
        {
            size_t offset = 0;
            auto readBytes = [&data, &offset](void* dst, size_t size) -> bool
            {
                if (offset + size > data.size())
                {
                    return false;
                }

                std::memcpy(dst, data.data() + offset, size);
                offset += size;
                return true;
            };

            uint32_t filenameSize = 0;
            if (!readBytes(&filenameSize, sizeof(filenameSize)))
            {
                return false;
            }

            tmpl.filename.clear();
            if (filenameSize > 0)
            {
                tmpl.filename.resize(filenameSize);
                if (!readBytes(tmpl.filename.data(), filenameSize))
                {
                    return false;
                }
            }

            uint64_t faceCount = 0;
            if (!readBytes(&faceCount, sizeof(faceCount)))
            {
                return false;
            }

            if (faceCount > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
            {
                return false;
            }

            tmpl.faces.clear();
            tmpl.faces.resize(static_cast<size_t>(faceCount));

            for (LDrawFace& face : tmpl.faces)
            {
                if (!readBytes(&face.vertexCount, sizeof(face.vertexCount))
                    || !readBytes(&face.colorCode, sizeof(face.colorCode)))
                {
                    return false;
                }

                if (face.vertexCount < 0 || face.vertexCount > 4)
                {
                    return false;
                }

                for (int vertexIndex = 0; vertexIndex < 4; ++vertexIndex)
                {
                    if (!readBytes(&face.vertices[vertexIndex].x, sizeof(face.vertices[vertexIndex].x))
                        || !readBytes(&face.vertices[vertexIndex].y, sizeof(face.vertices[vertexIndex].y))
                        || !readBytes(&face.vertices[vertexIndex].z, sizeof(face.vertices[vertexIndex].z)))
                    {
                        return false;
                    }
                }
            }

            return offset == data.size();
        }

        bool LoadTemplateCache(
            const std::string& cacheFileName,
            LDrawPartTemplate& tmpl,
            std::vector<std::string>& dependencies)
        {
            std::filesystem::path cacheFilePath(cacheFileName);
            if (!std::filesystem::exists(cacheFilePath))
            {
                return false;
            }

            bool cacheLoaded = false;
            std::ifstream cacheFile(cacheFileName, std::ios::binary);
            if (cacheFile.is_open())
            {
                LDrawPartCacheHeader header{};
                cacheFile.read(reinterpret_cast<char*>(&header), sizeof(header));

                bool validCache = cacheFile.gcount() == sizeof(header)
                    && header.magic == kLDrawPartCacheMagic
                    && header.version == kLDrawPartCacheVersion
                    && header.originalSize > 0
                    && header.compressedSize > 0
                    && header.originalSize <= static_cast<uint64_t>(std::numeric_limits<size_t>::max())
                    && header.compressedSize <= static_cast<uint64_t>(std::numeric_limits<size_t>::max())
                    && header.originalSize <= static_cast<uint64_t>(std::numeric_limits<int>::max())
                    && header.compressedSize <= static_cast<uint64_t>(std::numeric_limits<int>::max());

                dependencies.clear();

                for (uint32_t dependencyIndex = 0; validCache && dependencyIndex < header.dependencyCount; ++dependencyIndex)
                {
                    LDrawPartCacheDependency dependencyHeader{};
                    cacheFile.read(reinterpret_cast<char*>(&dependencyHeader), sizeof(dependencyHeader));
                    validCache = cacheFile.gcount() == sizeof(dependencyHeader)
                        && dependencyHeader.pathSize > 0
                        && dependencyHeader.pathSize <= 32768;
                    if (!validCache)
                    {
                        break;
                    }

                    std::string dependencyPath;
                    dependencyPath.resize(dependencyHeader.pathSize);
                    cacheFile.read(dependencyPath.data(), dependencyHeader.pathSize);
                    if (!cacheFile)
                    {
                        validCache = false;
                        break;
                    }

                    uint64_t fileSize = 0;
                    int64_t writeTime = 0;
                    if (!QueryFileSignature(dependencyPath, fileSize, writeTime)
                        || fileSize != dependencyHeader.fileSize
                        || writeTime != dependencyHeader.writeTime)
                    {
                        validCache = false;
                        break;
                    }

                    AppendUniqueDependency(dependencies, dependencyPath);
                }

                if (validCache)
                {
                    const size_t compressedSize = static_cast<size_t>(header.compressedSize);
                    const size_t originalSize = static_cast<size_t>(header.originalSize);

                    std::vector<uint8_t> compressedData(compressedSize);
                    cacheFile.read(reinterpret_cast<char*>(compressedData.data()), compressedSize);
                    if (!cacheFile)
                    {
                        validCache = false;
                    }
                    else
                    {
                        std::vector<uint8_t> uncompressedData(originalSize);
                        const size_t decompressedSize = lzav_decompress(
                            compressedData.data(),
                            uncompressedData.data(),
                            static_cast<int>(compressedSize),
                            static_cast<int>(originalSize));

                        validCache = decompressedSize == originalSize
                            && HashBuffer(uncompressedData.data(), uncompressedData.size()) == header.dataHash
                            && DeserializeTemplate(uncompressedData, tmpl);
                    }
                }

                cacheFile.close();
                cacheLoaded = validCache;
            }

            if (!cacheLoaded)
            {
                std::error_code removeError;
                std::filesystem::remove(cacheFilePath, removeError);
                dependencies.clear();
            }

            return cacheLoaded;
        }

        void SaveTemplateCache(
            const std::string& cacheFileName,
            const LDrawPartTemplate& tmpl,
            const std::vector<std::string>& dependencies)
        {
            std::vector<uint8_t> uncompressedData;
            SerializeTemplate(tmpl, uncompressedData);
            if (uncompressedData.empty()
                || uncompressedData.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
            {
                return;
            }

            const size_t compressedBound = lzav_compress_bound_hi(static_cast<int32_t>(uncompressedData.size()));
            std::vector<uint8_t> compressedData(compressedBound);
            const size_t actualCompressedSize = lzav_compress_hi(
                uncompressedData.data(),
                compressedData.data(),
                static_cast<int32_t>(uncompressedData.size()),
                static_cast<int32_t>(compressedBound));

            if (actualCompressedSize == 0
                || actualCompressedSize > static_cast<size_t>(std::numeric_limits<int>::max()))
            {
                return;
            }

            std::vector<LDrawPartCacheDependency> dependencyHeaders;
            dependencyHeaders.reserve(dependencies.size());
            for (const std::string& dependencyPath : dependencies)
            {
                uint64_t fileSize = 0;
                int64_t writeTime = 0;
                if (!QueryFileSignature(dependencyPath, fileSize, writeTime))
                {
                    return;
                }

                if (dependencyPath.size() > static_cast<size_t>(std::numeric_limits<uint32_t>::max()))
                {
                    return;
                }

                LDrawPartCacheDependency dependencyHeader{};
                dependencyHeader.fileSize = fileSize;
                dependencyHeader.writeTime = writeTime;
                dependencyHeader.pathSize = static_cast<uint32_t>(dependencyPath.size());
                dependencyHeaders.push_back(dependencyHeader);
            }

            LDrawPartCacheHeader header{};
            header.originalSize = static_cast<uint64_t>(uncompressedData.size());
            header.compressedSize = static_cast<uint64_t>(actualCompressedSize);
            header.dataHash = HashBuffer(uncompressedData.data(), uncompressedData.size());
            header.dependencyCount = static_cast<uint32_t>(dependencyHeaders.size());

            std::filesystem::path cacheFilePath(cacheFileName);
            std::filesystem::path tempCachePath = cacheFilePath;
            tempCachePath += ".tmp";

            std::ofstream cacheFile(tempCachePath, std::ios::binary | std::ios::trunc);
            if (!cacheFile.is_open())
            {
                return;
            }

            cacheFile.write(reinterpret_cast<const char*>(&header), sizeof(header));
            for (size_t dependencyIndex = 0; dependencyIndex < dependencies.size(); ++dependencyIndex)
            {
                cacheFile.write(reinterpret_cast<const char*>(&dependencyHeaders[dependencyIndex]), sizeof(LDrawPartCacheDependency));
                cacheFile.write(dependencies[dependencyIndex].data(), dependencyHeaders[dependencyIndex].pathSize);
            }
            cacheFile.write(reinterpret_cast<const char*>(compressedData.data()), static_cast<std::streamsize>(actualCompressedSize));
            cacheFile.flush();
            cacheFile.close();

            std::error_code removeError;
            std::filesystem::remove(cacheFilePath, removeError);

            std::error_code renameError;
            std::filesystem::rename(tempCachePath, cacheFilePath, renameError);
            if (renameError)
            {
                std::error_code cleanupError;
                std::filesystem::remove(tempCachePath, cleanupError);
            }
        }
    }

    LDrawParser::LDrawParser(LDrawColorTable& colors, LDrawFileResolver& resolver)
        : colors_(colors), resolver_(resolver)
    {
    }

    int LDrawParser::ResolveColor(int faceColor, int parentColor) const
    {
        if (faceColor == 16)
            return (parentColor == 16) ? kLDrawColorInherit : parentColor;
        if (faceColor == 24)
            return kLDrawColorInherit;
        return faceColor;
    }

    void LDrawParser::SetMPDSubfiles(std::unordered_map<std::string, std::string> subfiles)
    {
        mpdSubfiles_ = std::move(subfiles);
    }

    void LDrawParser::ClearMPDSubfiles()
    {
        mpdSubfiles_.clear();
    }

    bool LDrawParser::HasMPDSubfile(const std::string& name) const
    {
        return mpdSubfiles_.count(ToLowerKey(name)) > 0;
    }

    LDrawPartTemplate LDrawParser::ParseFile(const std::string& filepath)
    {
        return ParseFileImpl(filepath, true);
    }

    LDrawPartTemplate LDrawParser::ParseFileDirectGeometry(const std::string& filepath)
    {
        return ParseFileImpl(filepath, false);
    }

    LDrawPartTemplate LDrawParser::ParseFileImpl(const std::string& filepath, bool expandSubfiles)
    {
        std::string key = ToLowerKey(std::filesystem::path(filepath).filename().string());

        auto& templateCache = expandSubfiles ? templateCache_ : directTemplateCache_;
        auto& dependencyCache = expandSubfiles ? templateDependencies_ : directTemplateDependencies_;

        auto cacheIt = templateCache.find(key);
        if (cacheIt != templateCache.end())
        {
            return cacheIt->second;
        }

        LDrawPartTemplate tmpl;
        tmpl.filename = key;

        BFCState bfc;

        auto mpdIt = mpdSubfiles_.find(key);
        if (mpdIt != mpdSubfiles_.end())
        {
            std::istringstream stream(mpdIt->second);
            ParseStream(stream, 16, glm::mat4(1.0f), bfc, expandSubfiles, tmpl.faces, nullptr);
            templateCache[key] = tmpl;
            dependencyCache[key].clear();
            SPDLOG_DEBUG("LDraw: parsed {} {} -> {} faces",
                        expandSubfiles ? "part" : "direct geometry",
                        key,
                        tmpl.faces.size());
            return tmpl;
        }

        std::vector<std::string> dependencies;
        const bool allowDiskCache = HasFilesystemSignature(filepath);
        const std::string normalizedPath = allowDiskCache ? NormalizeCachePath(filepath) : std::string();
        const std::string cacheFileName = allowDiskCache ? BuildCacheFileName(normalizedPath, expandSubfiles) : std::string();

        if (allowDiskCache && LoadTemplateCache(cacheFileName, tmpl, dependencies))
        {
            templateCache[key] = tmpl;
            dependencyCache[key] = std::move(dependencies);
            SPDLOG_DEBUG("LDraw: loaded cached {} {} -> {} faces",
                        expandSubfiles ? "part" : "direct geometry",
                        key,
                        tmpl.faces.size());
            return tmpl;
        }

        std::string fileContent;
        if (!LoadLDrawTextResource(filepath, fileContent))
        {
            SPDLOG_WARN("LDraw: cannot open file '{}'", filepath);
            return tmpl;
        }

        std::istringstream file(fileContent);
        std::vector<std::string>* dependencyFiles = nullptr;
        if (allowDiskCache)
        {
            AppendUniqueDependency(dependencies, normalizedPath);
            dependencyFiles = &dependencies;
        }

        ParseStream(file, 16, glm::mat4(1.0f), bfc, expandSubfiles, tmpl.faces, dependencyFiles);

        templateCache[key] = tmpl;
        dependencyCache[key] = dependencies;
        if (allowDiskCache)
        {
            SaveTemplateCache(cacheFileName, tmpl, dependencies);
        }

        SPDLOG_DEBUG("LDraw: parsed {} {} -> {} faces",
                    expandSubfiles ? "part" : "direct geometry",
                    key,
                    tmpl.faces.size());
        return tmpl;
    }

    void LDrawParser::AppendTemplateInstance(
        const LDrawPartTemplate& tmpl,
        int parentColor,
        const glm::mat4& transform,
        bool invertWinding,
        std::vector<LDrawFace>& outFaces) const
    {
        for (const LDrawFace& sourceFace : tmpl.faces)
        {
            LDrawFace face{};
            face.vertexCount = sourceFace.vertexCount;
            face.colorCode = (sourceFace.colorCode == kLDrawColorInherit)
                ? ResolveColor(16, parentColor)
                : sourceFace.colorCode;

            for (int vertexIndex = 0; vertexIndex < sourceFace.vertexCount; ++vertexIndex)
            {
                face.vertices[vertexIndex] = glm::vec3(
                    transform * glm::vec4(sourceFace.vertices[vertexIndex], 1.0f));
            }

            if (invertWinding && face.vertexCount >= 3)
            {
                std::swap(face.vertices[1], face.vertices[2]);
            }

            outFaces.push_back(face);
        }
    }

    void LDrawParser::ParseStream(std::istream& input, int parentColor,
                                  const glm::mat4& transform, BFCState bfc, bool expandSubfiles,
                                  std::vector<LDrawFace>& outFaces,
                                  std::vector<std::string>* dependencyFiles)
    {
        std::string line;
        while (std::getline(input, line))
        {
            size_t start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos)
                continue;
            line = line.substr(start);

            if (line.empty())
                continue;

            int lineType = line[0] - '0';
            if (lineType < 0 || lineType > 5)
                continue;

            std::istringstream iss(line);
            int type;
            iss >> type;

            switch (type)
            {
            case 0: // Meta command
            {
                std::string rest;
                std::getline(iss, rest);
                size_t p = rest.find_first_not_of(" \t");
                if (p != std::string::npos)
                    rest = rest.substr(p);
                else
                    break;

                if (rest.find("BFC") == 0)
                {
                    std::string bfcCmd = rest.substr(3);
                    p = bfcCmd.find_first_not_of(" \t");
                    if (p != std::string::npos)
                        bfcCmd = bfcCmd.substr(p);
                    p = bfcCmd.find_last_not_of(" \t\r\n");
                    if (p != std::string::npos)
                        bfcCmd = bfcCmd.substr(0, p + 1);
                    else
                        bfcCmd.clear();

                    if (bfcCmd.find("CERTIFY") == 0)
                    {
                        bfc.certified = true;
                        bfc.localCull = true;
                        if (bfcCmd.find("CW") != std::string::npos && bfcCmd.find("CCW") == std::string::npos)
                            bfc.fileWindingCCW = false;
                        else
                            bfc.fileWindingCCW = true;
                    }
                    else if (bfcCmd == "NOCERTIFY")
                    {
                        bfc.certified = false;
                    }
                    else if (bfcCmd == "CW")
                    {
                        bfc.fileWindingCCW = false;
                    }
                    else if (bfcCmd == "CCW")
                    {
                        bfc.fileWindingCCW = true;
                    }
                    else if (bfcCmd == "INVERTNEXT")
                    {
                        bfc.invertNext = true;
                    }
                    else if (bfcCmd.find("CLIP") == 0)
                    {
                        bfc.localCull = true;
                        if (bfcCmd.find("CW") != std::string::npos && bfcCmd.find("CCW") == std::string::npos)
                            bfc.fileWindingCCW = false;
                        else if (bfcCmd.find("CCW") != std::string::npos)
                            bfc.fileWindingCCW = true;
                    }
                    else if (bfcCmd == "NOCLIP")
                    {
                        bfc.localCull = false;
                    }
                }
                break;
            }

            case 1: // Sub-file reference
            {
                int color;
                float x, y, z, a, b, c, d, e, f, g, h, i;
                std::string subfile;

                iss >> color >> x >> y >> z >> a >> b >> c >> d >> e >> f >> g >> h >> i;
                std::getline(iss, subfile);
                {
                    size_t sp = subfile.find_first_not_of(" \t");
                    if (sp != std::string::npos)
                        subfile = subfile.substr(sp);
                    size_t ep = subfile.find_last_not_of(" \t\r\n");
                    if (ep != std::string::npos)
                        subfile = subfile.substr(0, ep + 1);
                }

                if (subfile.empty())
                {
                    bfc.invertNext = false;
                    break;
                }

                glm::mat4 subTransform(
                    a, d, g, 0.0f,
                    b, e, h, 0.0f,
                    c, f, i, 0.0f,
                    x, y, z, 1.0f
                );

                glm::mat4 combinedTransform = transform * subTransform;
                int subColor = (color == 16) ? parentColor : color;

                BFCState childBfc = bfc;
                childBfc.invertNext = false;

                bool invert = bfc.orientationInverted;
                if (bfc.invertNext)
                    invert = !invert;

                glm::mat3 rotPart(subTransform);
                if (glm::determinant(rotPart) < 0.0f)
                    invert = !invert;

                childBfc.orientationInverted = invert;

                if (expandSubfiles)
                {
                    std::string subKey = ToLowerKey(subfile);
                    auto mpdIt = mpdSubfiles_.find(subKey);
                    if (mpdIt != mpdSubfiles_.end())
                    {
                        std::istringstream subStream(mpdIt->second);
                        ParseStream(subStream, subColor, combinedTransform,
                                    childBfc, true, outFaces, dependencyFiles);
                    }
                    else
                    {
                        std::string resolvedPath = resolver_.Resolve(subfile);
                        if (!resolvedPath.empty())
                        {
                            LDrawPartTemplate childTemplate = ParseFile(resolvedPath);
                            AppendTemplateInstance(
                                childTemplate,
                                subColor,
                                combinedTransform,
                                childBfc.orientationInverted,
                                outFaces);

                            if (dependencyFiles != nullptr)
                            {
                                auto dependencyIt = templateDependencies_.find(
                                    ToLowerKey(std::filesystem::path(resolvedPath).filename().string()));
                                if (dependencyIt != templateDependencies_.end())
                                {
                                    for (const std::string& dependencyPath : dependencyIt->second)
                                    {
                                        AppendUniqueDependency(*dependencyFiles, dependencyPath);
                                    }
                                }
                            }
                        }
                    }
                }

                bfc.invertNext = false;
                break;
            }

            case 2: // Line - skip
            {
                bfc.invertNext = false;
                break;
            }

            case 3: // Triangle
            {
                int color;
                glm::vec3 v0, v1, v2;
                iss >> color >> v0.x >> v0.y >> v0.z >> v1.x >> v1.y >> v1.z >> v2.x >> v2.y >> v2.z;

                v0 = glm::vec3(transform * glm::vec4(v0, 1.0f));
                v1 = glm::vec3(transform * glm::vec4(v1, 1.0f));
                v2 = glm::vec3(transform * glm::vec4(v2, 1.0f));

                bool faceWindingCCW = (bfc.fileWindingCCW != bfc.orientationInverted);

                LDrawFace face{};
                face.vertexCount = 3;
                if (faceWindingCCW)
                {
                    face.vertices[0] = v0;
                    face.vertices[1] = v1;
                    face.vertices[2] = v2;
                }
                else
                {
                    face.vertices[0] = v0;
                    face.vertices[1] = v2;
                    face.vertices[2] = v1;
                }
                face.colorCode = ResolveColor(color, parentColor);

                outFaces.push_back(face);
                bfc.invertNext = false;
                break;
            }

            case 4: // Quadrilateral
            {
                int color;
                glm::vec3 v0, v1, v2, v3;
                iss >> color >> v0.x >> v0.y >> v0.z >> v1.x >> v1.y >> v1.z
                    >> v2.x >> v2.y >> v2.z >> v3.x >> v3.y >> v3.z;

                v0 = glm::vec3(transform * glm::vec4(v0, 1.0f));
                v1 = glm::vec3(transform * glm::vec4(v1, 1.0f));
                v2 = glm::vec3(transform * glm::vec4(v2, 1.0f));
                v3 = glm::vec3(transform * glm::vec4(v3, 1.0f));

                glm::vec3 c0 = glm::cross(v1 - v0, v2 - v0);
                glm::vec3 c1 = glm::cross(v2 - v0, v3 - v0);
                if (glm::dot(c0, c1) < 0.0f)
                    std::swap(v1, v3);

                bool faceWindingCCW = (bfc.fileWindingCCW != bfc.orientationInverted);
                if (!faceWindingCCW)
                    std::swap(v1, v3);

                int resolved = ResolveColor(color, parentColor);

                LDrawFace face1{};
                face1.vertexCount = 3;
                face1.vertices[0] = v0;
                face1.vertices[1] = v1;
                face1.vertices[2] = v2;
                face1.colorCode = resolved;
                outFaces.push_back(face1);

                LDrawFace face2{};
                face2.vertexCount = 3;
                face2.vertices[0] = v0;
                face2.vertices[1] = v2;
                face2.vertices[2] = v3;
                face2.colorCode = resolved;
                outFaces.push_back(face2);

                bfc.invertNext = false;
                break;
            }

            case 5: // Conditional line - skip
            {
                bfc.invertNext = false;
                break;
            }

            default:
                break;
            }
        }
    }

    void LDrawParser::ClearCache()
    {
        templateCache_.clear();
        directTemplateCache_.clear();
        templateDependencies_.clear();
        directTemplateDependencies_.clear();
    }
}
