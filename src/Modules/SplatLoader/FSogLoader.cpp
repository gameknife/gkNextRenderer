#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/SplatLoader/FSogLoader.hpp"
#include "Modules/SplatLoader/FSplatQuant.hpp"

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Modules/SplatLoader/GaussianSplatComponent.h"
#include "Modules/SplatLoader/SplatProxyBuilder.hpp"
#include "Engine/Utilities/FileHelper.hpp"

#include <nlohmann/json.hpp>
#include <webp/decode.h>
#include <zlib.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <unordered_map>

namespace Assets
{
    namespace
    {
        using ByteArray = std::vector<uint8_t>;

        uint16_t ReadU16(const ByteArray& bytes, size_t offset)
        {
            if (offset + 2 > bytes.size()) throw std::runtime_error("truncated ZIP header");
            return static_cast<uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
        }

        uint32_t ReadU32(const ByteArray& bytes, size_t offset)
        {
            if (offset + 4 > bytes.size()) throw std::runtime_error("truncated ZIP header");
            return static_cast<uint32_t>(bytes[offset]) |
                   (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
                   (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
                   (static_cast<uint32_t>(bytes[offset + 3]) << 24);
        }

        ByteArray ReadAssetFile(const std::filesystem::path& path)
        {
            ByteArray result;
            if (auto* package = Utilities::Package::FPackageFileSystem::TryGetInstance())
            {
                if (package->LoadFile(path.generic_string(), result))
                {
                    return result;
                }
            }

            const std::filesystem::path loosePath = path.is_absolute()
                ? path
                : Utilities::FileHelper::GetRuntimeFilePath(path);
            std::ifstream stream(loosePath, std::ios::binary | std::ios::ate);
            if (!stream) throw std::runtime_error(fmt::format("cannot open {}", path.string()));
            const auto length = stream.tellg();
            if (length < 0) throw std::runtime_error(fmt::format("cannot size {}", path.string()));
            result.resize(static_cast<size_t>(length));
            stream.seekg(0);
            stream.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(result.size()));
            if (!stream) throw std::runtime_error(fmt::format("cannot read {}", path.string()));
            return result;
        }

        ByteArray InflateZipMember(const uint8_t* compressed, size_t compressedSize, size_t uncompressedSize)
        {
            ByteArray result(uncompressedSize);
            z_stream stream{};
            stream.next_in = const_cast<Bytef*>(compressed);
            stream.avail_in = static_cast<uInt>(compressedSize);
            stream.next_out = result.data();
            stream.avail_out = static_cast<uInt>(result.size());
            if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
                throw std::runtime_error("failed to initialize ZIP inflater");
            const int status = inflate(&stream, Z_FINISH);
            inflateEnd(&stream);
            if (status != Z_STREAM_END || stream.total_out != uncompressedSize)
                throw std::runtime_error("failed to inflate ZIP member");
            return result;
        }

        std::unordered_map<std::string, ByteArray> ReadStoredZip(const std::filesystem::path& path)
        {
            const ByteArray zip = ReadAssetFile(path);
            std::unordered_map<std::string, ByteArray> entries;
            if (zip.size() < 22) throw std::runtime_error("truncated ZIP archive");
            size_t eocd = zip.size() - 22;
            const size_t searchBegin = zip.size() > 65557 ? zip.size() - 65557 : 0;
            while (ReadU32(zip, eocd) != 0x06054b50u)
            {
                if (eocd == searchBegin) throw std::runtime_error("ZIP end record not found");
                --eocd;
            }
            const uint16_t entryCount = ReadU16(zip, eocd + 10);
            size_t centralOffset = ReadU32(zip, eocd + 16);
            for (uint16_t entry = 0; entry < entryCount; ++entry)
            {
                if (ReadU32(zip, centralOffset) != 0x02014b50u)
                    throw std::runtime_error("invalid ZIP central directory");
                const uint16_t method = ReadU16(zip, centralOffset + 10);
                const uint32_t compressedSize = ReadU32(zip, centralOffset + 20);
                const uint32_t uncompressedSize = ReadU32(zip, centralOffset + 24);
                const uint16_t nameLength = ReadU16(zip, centralOffset + 28);
                const uint16_t extraLength = ReadU16(zip, centralOffset + 30);
                const uint16_t commentLength = ReadU16(zip, centralOffset + 32);
                const uint32_t localOffset = ReadU32(zip, centralOffset + 42);
                if (method != 0u && method != 8u)
                    throw std::runtime_error("SOG ZIP uses an unsupported compression method");
                if (method == 0u && compressedSize != uncompressedSize)
                    throw std::runtime_error("invalid stored ZIP member size");
                const size_t nameOffset = centralOffset + 46;
                if (nameOffset + nameLength > zip.size()) throw std::runtime_error("truncated ZIP member name");
                if (ReadU32(zip, localOffset) != 0x04034b50u)
                    throw std::runtime_error("invalid ZIP local header offset");
                const uint16_t localNameLength = ReadU16(zip, localOffset + 26);
                const uint16_t localExtraLength = ReadU16(zip, localOffset + 28);
                const size_t dataOffset = static_cast<size_t>(localOffset) + 30 + localNameLength + localExtraLength;
                const size_t endOffset = dataOffset + compressedSize;
                if (endOffset > zip.size()) throw std::runtime_error("truncated ZIP member");
                std::string name(reinterpret_cast<const char*>(zip.data() + nameOffset), nameLength);
                ByteArray content = method == 0u
                    ? ByteArray(zip.begin() + dataOffset, zip.begin() + endOffset)
                    : InflateZipMember(zip.data() + dataOffset, compressedSize, uncompressedSize);
                entries.emplace(std::move(name), std::move(content));
                centralOffset = nameOffset + nameLength + extraLength + commentLength;
            }
            if (entries.empty()) throw std::runtime_error("SOG ZIP contains no local file entries");
            return entries;
        }

        struct DecodedImage
        {
            int width = 0;
            int height = 0;
            ByteArray rgba;
        };

        DecodedImage DecodeWebP(const ByteArray& bytes, std::string_view name)
        {
            DecodedImage image;
            uint8_t* decoded = WebPDecodeRGBA(bytes.data(), bytes.size(), &image.width, &image.height);
            if (!decoded || image.width <= 0 || image.height <= 0)
                throw std::runtime_error(fmt::format("failed to decode WebP {}", name));
            const size_t size = static_cast<size_t>(image.width) * image.height * 4;
            image.rgba.assign(decoded, decoded + size);
            WebPFree(decoded);
            return image;
        }

        class SogFileSet final
        {
        public:
            explicit SogFileSet(const std::filesystem::path& path) : root_(path.parent_path())
            {
                if (path.extension() == ".sog") entries_ = ReadStoredZip(path);
            }

            ByteArray Read(std::string_view name) const
            {
                if (!entries_.empty())
                {
                    const auto found = entries_.find(std::string(name));
                    if (found == entries_.end()) throw std::runtime_error(fmt::format("missing SOG member {}", name));
                    return found->second;
                }
                return ReadAssetFile(root_ / name);
            }

        private:
            std::filesystem::path root_;
            std::unordered_map<std::string, ByteArray> entries_;
        };

        std::string FirstFile(const nlohmann::json& object, size_t index = 0)
        {
            return object.at("files").at(index).get<std::string>();
        }

        glm::mat3 SogToEngineBasis()
        {
            glm::mat3 basis(1.0f);
            basis[0][0] = -1.0f;
            basis[1][1] = -1.0f;
            return basis;
        }
    }

    bool FSogLoader::Load(const std::string& filename, EnvironmentSetting& camera,
                          std::vector<std::shared_ptr<Node>>& nodes, std::vector<Model>& models,
                          std::vector<FMaterial>& materials, std::vector<LightObject>&,
                          std::vector<AnimationTrack>&, std::vector<Skeleton>&)
    {
        try
        {
            std::filesystem::path path(filename);
            if (path.is_absolute())
            {
                std::error_code ec;
                const std::array<std::filesystem::path, 2> roots = {
                    Utilities::FileHelper::GetRuntimeRoot(),
                    Utilities::FileHelper::GetWritableRuntimeRoot() / "asset-cache",
                };
                for (const std::filesystem::path& root : roots)
                {
                    const std::filesystem::path relative = std::filesystem::relative(path, root, ec);
                    if (!ec && !relative.empty() && relative.generic_string().rfind("..", 0) != 0)
                    {
                        path = relative.lexically_normal();
                        break;
                    }
                    ec.clear();
                }
            }
            path = path.lexically_normal();
            const SogFileSet files(path);
            const ByteArray metaBytes = files.Read("meta.json");
            const nlohmann::json meta = nlohmann::json::parse(metaBytes.begin(), metaBytes.end());
            if (meta.value("version", 0) != 2) throw std::runtime_error("only SOG v2 is supported");

            FGaussianSplatData data;
            data.name = path.stem().string();
            data.antialias = meta.value("antialias", false);
            data.shBasisFlipXY = true;
            const size_t count = meta.at("count").get<size_t>();
            if (count == 0 || count > std::numeric_limits<uint32_t>::max())
                throw std::runtime_error("invalid SOG splat count");

            const auto& meansMeta = meta.at("means");
            const auto& scalesMeta = meta.at("scales");
            const auto& sh0Meta = meta.at("sh0");
            const DecodedImage meansLow = DecodeWebP(files.Read(FirstFile(meansMeta, 0)), FirstFile(meansMeta, 0));
            const DecodedImage meansHigh = DecodeWebP(files.Read(FirstFile(meansMeta, 1)), FirstFile(meansMeta, 1));
            const DecodedImage scales = DecodeWebP(files.Read(FirstFile(scalesMeta)), FirstFile(scalesMeta));
            const DecodedImage quaternions = DecodeWebP(files.Read(FirstFile(meta.at("quats"))), FirstFile(meta.at("quats")));
            const DecodedImage sh0 = DecodeWebP(files.Read(FirstFile(sh0Meta)), FirstFile(sh0Meta));

            const auto validateImage = [count](const DecodedImage& image, std::string_view name)
            {
                if (static_cast<size_t>(image.width) * image.height < count)
                    throw std::runtime_error(fmt::format("SOG image {} is smaller than count", name));
            };
            validateImage(meansLow, "means_l");
            validateImage(meansHigh, "means_u");
            validateImage(scales, "scales");
            validateImage(quaternions, "quats");
            validateImage(sh0, "sh0");

            const glm::vec3 meansMin(meansMeta.at("mins").at(0).get<float>(),
                                     meansMeta.at("mins").at(1).get<float>(),
                                     meansMeta.at("mins").at(2).get<float>());
            const glm::vec3 meansMax(meansMeta.at("maxs").at(0).get<float>(),
                                     meansMeta.at("maxs").at(1).get<float>(),
                                     meansMeta.at("maxs").at(2).get<float>());
            const std::vector<float> scaleCodebook = scalesMeta.at("codebook").get<std::vector<float>>();
            const std::vector<float> sh0Codebook = sh0Meta.at("codebook").get<std::vector<float>>();
            if (scaleCodebook.size() != 256 || sh0Codebook.size() != 256)
                throw std::runtime_error("SOG v2 codebooks must contain 256 entries");

            data.splats.resize(count);
            data.aabbMin = glm::vec3(std::numeric_limits<float>::max());
            data.aabbMax = glm::vec3(std::numeric_limits<float>::lowest());
            const glm::mat3 sogToEngine = SogToEngineBasis();
            for (size_t i = 0; i < count; ++i)
            {
                const size_t pixel = i * 4;
                glm::vec3 position;
                for (uint32_t axis = 0; axis < 3; ++axis)
                {
                    const uint16_t quantized = static_cast<uint16_t>(meansLow.rgba[pixel + axis] |
                        (static_cast<uint16_t>(meansHigh.rgba[pixel + axis]) << 8));
                    const float encoded = glm::mix(meansMin[axis], meansMax[axis], quantized / 65535.0f);
                    position[axis] = Sog::DecodeLogPosition(encoded);
                }
                const glm::quat rotation = Sog::DecodeQuaternion(
                    quaternions.rgba[pixel], quaternions.rgba[pixel + 1],
                    quaternions.rgba[pixel + 2], quaternions.rgba[pixel + 3]);
                const glm::vec3 logScale(scaleCodebook[scales.rgba[pixel]],
                                         scaleCodebook[scales.rgba[pixel + 1]],
                                         scaleCodebook[scales.rgba[pixel + 2]]);
                position = sogToEngine * position;
                const glm::mat3 sogCovariance = Sog::BuildCovariance(rotation, logScale);
                const glm::mat3 covariance = sogToEngine * sogCovariance * glm::transpose(sogToEngine);

                auto& splat = data.splats[i];
                splat.positionOpacity = glm::vec4(position, sh0.rgba[pixel + 3] / 255.0f);
                splat.covariance0 = glm::vec4(covariance[0][0], covariance[1][0], covariance[2][0], covariance[1][1]);
                splat.covariance1 = glm::vec4(covariance[2][1], covariance[2][2], 0.0f, 0.0f);
                splat.sh0 = glm::vec4(sh0Codebook[sh0.rgba[pixel]], sh0Codebook[sh0.rgba[pixel + 1]],
                                      sh0Codebook[sh0.rgba[pixel + 2]], 0.0f);
                constexpr float boundsSigma = 3.0f;
                const glm::vec3 gaussianExtent = boundsSigma * glm::sqrt(glm::max(
                    glm::vec3(covariance[0][0], covariance[1][1], covariance[2][2]), glm::vec3(0.0f)));
                data.aabbMin = glm::min(data.aabbMin, position - gaussianExtent);
                data.aabbMax = glm::max(data.aabbMax, position + gaussianExtent);
            }

            if (meta.contains("shN"))
            {
                const auto& shMeta = meta.at("shN");
                data.shBands = std::clamp(shMeta.value("bands", 0u), 0u, 3u);
                const uint32_t coefficientCount = std::array<uint32_t, 4>{0, 3, 8, 15}[data.shBands];
                const uint32_t paletteCount = shMeta.at("count").get<uint32_t>();
                const std::vector<float> codebook = shMeta.at("codebook").get<std::vector<float>>();
                const DecodedImage centroids = DecodeWebP(files.Read(FirstFile(shMeta, 0)), FirstFile(shMeta, 0));
                const DecodedImage labels = DecodeWebP(files.Read(FirstFile(shMeta, 1)), FirstFile(shMeta, 1));
                validateImage(labels, "shN_labels");
                if (codebook.size() != 256 || centroids.width != static_cast<int>(64 * coefficientCount))
                    throw std::runtime_error("invalid SOG SH palette layout");
                if (static_cast<uint64_t>(centroids.height) * 64u < paletteCount)
                    throw std::runtime_error("SOG SH centroid image is too small");

                data.shPalette.resize(static_cast<size_t>(paletteCount) * coefficientCount);
                for (uint32_t palette = 0; palette < paletteCount; ++palette)
                {
                    for (uint32_t coefficient = 0; coefficient < coefficientCount; ++coefficient)
                    {
                        const uint32_t x = (palette % 64) * coefficientCount + coefficient;
                        const uint32_t y = palette / 64;
                        const size_t pixel = (static_cast<size_t>(y) * centroids.width + x) * 4;
                        data.shPalette[static_cast<size_t>(palette) * coefficientCount + coefficient] = glm::vec4(
                            codebook[centroids.rgba[pixel]], codebook[centroids.rgba[pixel + 1]],
                            codebook[centroids.rgba[pixel + 2]], 0.0f);
                    }
                }
                for (size_t i = 0; i < count; ++i)
                {
                    const size_t pixel = i * 4;
                    const uint32_t label = labels.rgba[pixel] | (static_cast<uint32_t>(labels.rgba[pixel + 1]) << 8);
                    data.splats[i].metadata = glm::uvec4(label < paletteCount ? label : 0u, data.shBands, 0u, 0u);
                }
            }

            const glm::vec3 center = (data.aabbMin + data.aabbMax) * 0.5f;
            const float radius = std::max(glm::length(data.aabbMax - data.aabbMin) * 0.5f, 0.5f);
            Camera defaultCamera;
            defaultCamera.name = "SOG Camera";
            const glm::vec3 viewDirection = glm::normalize(glm::vec3(1.0f, 0.45f, 1.0f));
            const float cameraDistance = radius * 2.4f * 0.75f;
            defaultCamera.ModelView = glm::lookAt(center + viewDirection * cameraDistance, center,
                                                  glm::vec3(0.0f, 1.0f, 0.0f));
            defaultCamera.FieldOfView = 45.0f;
            defaultCamera.Aperture = 0.0f;
            defaultCamera.FocalDistance = cameraDistance;
            const float sceneDiagonal = radius * 2.0f;
            defaultCamera.NearPlane = sceneDiagonal >= 20.0f
                ? 0.5f
                : std::clamp(sceneDiagonal * 0.025f, 0.02f, 0.5f);
            defaultCamera.FarPlane = std::max(defaultCameraFarPlane, radius * 20.0f);
            camera.cameras.push_back(defaultCamera);
            camera.ControlSpeed = radius;
            camera.HasSky = false;
            camera.HasSun = false;

            uint32_t instanceId = 0;
            for (const auto& node : nodes)
            {
                if (node) instanceId = std::max(instanceId, node->GetInstanceId() + 1u);
            }
            auto splatNode = Node::CreateNode(data.name, glm::vec3(0.0f),
                                              glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f), instanceId);
            auto component = std::make_shared<Runtime::GaussianSplatComponent>(
                std::make_shared<const FGaussianSplatData>(std::move(data)));
            splatNode->AddComponent(component);
            nodes.push_back(splatNode);
            BuildGaussianSplatProxy(splatNode, component, nodes, models, materials);

            SPDLOG_INFO("decoded SOG v2 [{}]: {} splats, SH band {}, node {}",
                        component->GetData()->name, count, component->GetData()->shBands, instanceId);
            return true;
        }
        catch (const std::exception& exception)
        {
            SPDLOG_ERROR("failed to load SOG '{}': {}", filename, exception.what());
            return false;
        }
    }
}
