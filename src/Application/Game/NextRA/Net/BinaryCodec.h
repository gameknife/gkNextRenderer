#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace NextRA::Net
{
    template <typename T>
    void WriteBinary(std::vector<uint8_t>& out, T value)
    {
        const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
        out.insert(out.end(), bytes, bytes + sizeof(T));
    }

    template <typename T>
    bool ReadBinary(std::span<const uint8_t> bytes, size_t& cursor, T& out)
    {
        if (cursor + sizeof(T) > bytes.size())
        {
            return false;
        }
        std::memcpy(&out, bytes.data() + cursor, sizeof(T));
        cursor += sizeof(T);
        return true;
    }
}
