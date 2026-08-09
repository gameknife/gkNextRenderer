#include "Engine/Common/CoreMinimal.hpp"

#include "Engine/Utilities/Format.hpp"

namespace Utilities
{
    std::string FormatBytes(const uint64_t bytes)
    {
        static constexpr std::array<const char*, 5> units{"B", "KB", "MB", "GB", "TB"};
        double value = static_cast<double>(bytes);
        size_t unitIndex = 0;
        while (value >= 1024.0 && unitIndex + 1 < units.size())
        {
            value /= 1024.0;
            ++unitIndex;
        }
        return fmt::format("{:.2f} {}", value, units[unitIndex]);
    }
}
