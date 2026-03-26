#include "FLDrawConnectivity.h"

#include "Utilities/FileHelper.hpp"

#include <spdlog/spdlog.h>

#include <cstring>

namespace Assets::LDrawConn
{
    namespace
    {
        static_assert(std::endian::native == std::endian::little,
                      "FLDrawConnectivity parser assumes little-endian byte order");

        // Safe reader that tracks offset and validates bounds
        class FByteReader
        {
        public:
            FByteReader(const uint8_t* data, size_t size)
                : data_(data), size_(size) {}

            bool HasBytes(size_t count) const { return offset_ + count <= size_; }
            size_t Offset() const { return offset_; }
            size_t Remaining() const { return size_ - offset_; }

            uint16_t ReadUInt16()
            {
                uint16_t val = 0;
                std::memcpy(&val, data_ + offset_, sizeof(val));
                offset_ += sizeof(val);
                return val;
            }

            float ReadFloat()
            {
                float val = 0.0f;
                std::memcpy(&val, data_ + offset_, sizeof(val));
                offset_ += sizeof(val);
                return val;
            }

            // Read float from arbitrary offset (for unaligned reads)
            float ReadFloatUnaligned()
            {
                float val = 0.0f;
                std::memcpy(&val, data_ + offset_, sizeof(val));
                offset_ += sizeof(val);
                return val;
            }

            void Skip(size_t bytes) { offset_ += bytes; }

        private:
            const uint8_t* data_;
            size_t size_;
            size_t offset_ = 0;
        };

        constexpr size_t kHeaderSize = 52;  // 4 (IDs) + 36 (matrix) + 12 (position)

        bool ReadTransform(FByteReader& reader, FConnTransform& outTransform)
        {
            glm::mat3& m = outTransform.rotation;
            // Row-major: m[col][row] in glm column-major storage
            m[0][0] = reader.ReadFloat(); m[1][0] = reader.ReadFloat(); m[2][0] = reader.ReadFloat();
            m[0][1] = reader.ReadFloat(); m[1][1] = reader.ReadFloat(); m[2][1] = reader.ReadFloat();
            m[0][2] = reader.ReadFloat(); m[1][2] = reader.ReadFloat(); m[2][2] = reader.ReadFloat();

            outTransform.position.x = reader.ReadFloat();
            outTransform.position.y = reader.ReadFloat();
            outTransform.position.z = reader.ReadFloat();
            return true;
        }

        bool ParsePointElement(FByteReader& reader, uint16_t id2,
                               const FConnTransform& transform, FPointConnector& outPoint)
        {
            if (!reader.HasBytes(8))
                return false;

            outPoint.subType = id2;
            outPoint.transform = transform;

            // Payload layout: uint16 flags | float32 length (UNALIGNED at +2) | uint16 endFlags
            outPoint.flags = reader.ReadUInt16();
            outPoint.length = reader.ReadFloatUnaligned();
            outPoint.endFlags = reader.ReadUInt16();
            return true;
        }

        bool ParseGridElement(FByteReader& reader, uint16_t id1, uint16_t id2,
                              const FConnTransform& transform, FGridConnector& outGrid)
        {
            if (!reader.HasBytes(4))
                return false;

            outGrid.id1 = id1;
            outGrid.id2 = id2;
            outGrid.transform = transform;
            outGrid.gridZ = reader.ReadUInt16();
            outGrid.gridX = reader.ReadUInt16();

            const size_t vertCount = static_cast<size_t>(outGrid.gridZ + 1) * (outGrid.gridX + 1);
            if (!reader.HasBytes(vertCount * 4))
                return false;

            outGrid.vertices.resize(vertCount);
            for (size_t i = 0; i < vertCount; ++i)
            {
                outGrid.vertices[i].type = reader.ReadUInt16();
                outGrid.vertices[i].value = reader.ReadUInt16();
            }
            return true;
        }
    }

    bool ParseConnFile(const uint8_t* data, size_t size, FConnFile& outFile)
    {
        outFile.points.clear();
        outFile.grids.clear();

        if (!data || size < kHeaderSize)
            return false;

        FByteReader reader(data, size);

        while (reader.HasBytes(kHeaderSize))
        {
            const uint16_t id1 = reader.ReadUInt16();
            const uint16_t id2 = reader.ReadUInt16();

            FConnTransform transform;
            ReadTransform(reader, transform);

            switch (id1)
            {
            case 0:  // Point connector
            {
                FPointConnector point;
                if (!ParsePointElement(reader, id2, transform, point))
                    return outFile.points.size() > 0 || outFile.grids.size() > 0;
                outFile.points.push_back(std::move(point));
                break;
            }
            case 1:  // Marker - no payload
                break;
            case 2:
            case 3:  // Grid
            {
                FGridConnector grid;
                if (!ParseGridElement(reader, id1, id2, transform, grid))
                    return outFile.points.size() > 0 || outFile.grids.size() > 0;
                outFile.grids.push_back(std::move(grid));
                break;
            }
            case 4:  // Custom - 2 byte payload
            {
                if (!reader.HasBytes(2))
                    return outFile.points.size() > 0 || outFile.grids.size() > 0;
                reader.Skip(2);
                break;
            }
            case 6:  // Tube - 17 byte payload
            {
                if (!reader.HasBytes(17))
                    return outFile.points.size() > 0 || outFile.grids.size() > 0;
                reader.Skip(17);
                break;
            }
            default:
                SPDLOG_DEBUG("LDrawConn: unknown element category {} at offset {}, stopping",
                             id1, reader.Offset() - kHeaderSize);
                return outFile.points.size() > 0 || outFile.grids.size() > 0;
            }
        }

        return outFile.points.size() > 0 || outFile.grids.size() > 0;
    }

    bool LoadConnFile(const std::string& pakEntry, FConnFile& outFile)
    {
        Utilities::Package::FPackageFileSystem* pakSystem =
            Utilities::Package::FPackageFileSystem::TryGetInstance();
        if (!pakSystem)
            return false;

        std::vector<uint8_t> fileData;
        if (!pakSystem->LoadMountedFile(pakEntry, fileData))
            return false;

        return ParseConnFile(fileData.data(), fileData.size(), outFile);
    }
}
