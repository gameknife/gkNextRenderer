#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Runtime::Tui
{
    struct FRgb8
    {
        uint8_t r{};
        uint8_t g{};
        uint8_t b{};

        bool operator==(const FRgb8& other) const = default;
    };

    class TerminalBlitter final
    {
    public:
        struct FOptions
        {
            uint32_t MaxColumns = 0;
            uint32_t MaxRows = 0;
        };

        explicit TerminalBlitter(FOptions options = {});

        void Reset();
        std::string EncodeFrame(const std::vector<FRgb8>& sourcePixels,
                                uint32_t sourceWidth,
                                uint32_t sourceHeight,
                                uint32_t terminalColumns,
                                uint32_t terminalRows,
                                const std::string& statusLine);

    private:
        struct FCell
        {
            FRgb8 upper{};
            FRgb8 lower{};

            bool operator==(const FCell& other) const = default;
        };

        static void AppendCursor(std::string& output, uint32_t row, uint32_t column);
        static void AppendCell(std::string& output,
                               const FCell& cell,
                               std::optional<FRgb8>& currentForeground,
                               std::optional<FRgb8>& currentBackground);
        static void AppendFullFrame(std::string& output,
                                    const std::vector<FCell>& cells,
                                    uint32_t columns,
                                    uint32_t imageRows,
                                    std::optional<FRgb8>& currentForeground,
                                    std::optional<FRgb8>& currentBackground);
        static void ApplySharpen(std::vector<FRgb8>& pixels, uint32_t width, uint32_t height);
        static std::vector<FRgb8> DownscaleBox(const std::vector<FRgb8>& sourcePixels,
                                               uint32_t sourceWidth,
                                               uint32_t sourceHeight,
                                               uint32_t targetWidth,
                                               uint32_t targetHeight);

        FOptions options_{};
        uint32_t previousColumns_{};
        uint32_t previousImageRows_{};
        std::vector<FCell> previousCells_{};
        std::string previousStatusLine_{};
    };
}
