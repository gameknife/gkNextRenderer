#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextTui/TerminalBlitter.hpp"

#include <algorithm>

namespace Runtime::Tui
{
    namespace
    {
        constexpr float TuiSharpenAmount = 0.35f;
        constexpr size_t TuiFullRedrawChangeNumerator = 6;
        constexpr size_t TuiFullRedrawChangeDenominator = 10;

        void AppendNumber(std::string& output, uint32_t value)
        {
            output += std::to_string(value);
        }

        uint8_t ClampToByte(const float value)
        {
            return static_cast<uint8_t>(std::clamp(value, 0.0f, 255.0f));
        }
    }

    TerminalBlitter::TerminalBlitter(FOptions options)
        : options_(options)
    {
    }

    void TerminalBlitter::Reset()
    {
        previousColumns_ = 0;
        previousImageRows_ = 0;
        previousCells_.clear();
        previousStatusLine_.clear();
    }

    std::string TerminalBlitter::EncodeFrame(const std::vector<FRgb8>& sourcePixels,
                                             uint32_t sourceWidth,
                                             uint32_t sourceHeight,
                                             uint32_t terminalColumns,
                                             uint32_t terminalRows,
                                             const std::string& statusLine)
    {
        const uint32_t columns = options_.MaxColumns > 0
            ? std::min(terminalColumns, options_.MaxColumns)
            : terminalColumns;
        const uint32_t rows = options_.MaxRows > 0
            ? std::min(terminalRows, options_.MaxRows)
            : terminalRows;

        if (columns == 0 || rows == 0 || sourceWidth == 0 || sourceHeight == 0 || sourcePixels.empty())
        {
            Reset();
            return "\x1b[2J\x1b[H\x1b[0mterminal too small";
        }

        const bool hasStatusLine = rows > 1;
        const uint32_t imageRows = hasStatusLine ? rows - 1 : rows;
        if (imageRows == 0)
        {
            Reset();
            return "\x1b[2J\x1b[H\x1b[0mterminal too small";
        }

        const std::string visibleStatusLine =
            statusLine.size() > columns ? statusLine.substr(0, columns) : statusLine;

        const uint32_t targetPixelHeight = imageRows * 2;
        const std::vector<FRgb8> scaledPixels =
            DownscaleBox(sourcePixels, sourceWidth, sourceHeight, columns, targetPixelHeight);

        std::vector<FCell> cells(columns * imageRows);
        for (uint32_t row = 0; row < imageRows; ++row)
        {
            for (uint32_t column = 0; column < columns; ++column)
            {
                FCell& cell = cells[row * columns + column];
                cell.upper = scaledPixels[(row * 2) * columns + column];
                cell.lower = scaledPixels[(row * 2 + 1) * columns + column];
            }
        }

        const bool fullRedraw = previousColumns_ != columns || previousImageRows_ != imageRows ||
            previousCells_.size() != cells.size();
        size_t changedCellCount = 0;
        if (!fullRedraw)
        {
            for (size_t index = 0; index < cells.size(); ++index)
            {
                changedCellCount += previousCells_[index] == cells[index] ? 0 : 1;
            }
        }
        const bool preferFullRedraw = fullRedraw || (!cells.empty() &&
            changedCellCount * TuiFullRedrawChangeDenominator >= cells.size() * TuiFullRedrawChangeNumerator);

        std::string output;
        if (preferFullRedraw)
        {
            output.reserve(columns * imageRows * 24);
            output += fullRedraw ? "\x1b[2J\x1b[H" : "\x1b[H";
        }

        std::optional<FRgb8> currentForeground;
        std::optional<FRgb8> currentBackground;
        uint32_t lastWrittenRow = 0;
        uint32_t lastWrittenColumn = 0;
        bool hasCursor = false;
        if (preferFullRedraw)
        {
            AppendFullFrame(output, cells, columns, imageRows, currentForeground, currentBackground);
            hasCursor = !cells.empty();
        }
        else
        {
            for (uint32_t row = 0; row < imageRows; ++row)
            {
                for (uint32_t column = 0; column < columns; ++column)
                {
                    const uint32_t index = row * columns + column;
                    if (previousCells_[index] == cells[index])
                    {
                        continue;
                    }

                    if (!hasCursor || lastWrittenRow != row || lastWrittenColumn != column)
                    {
                        AppendCursor(output, row + 1, column + 1);
                    }
                    AppendCell(output, cells[index], currentForeground, currentBackground);
                    hasCursor = true;
                    lastWrittenRow = row;
                    lastWrittenColumn = column + 1;
                }
            }
        }

        if (hasStatusLine && (preferFullRedraw || previousStatusLine_ != visibleStatusLine))
        {
            if (preferFullRedraw)
            {
                output += "\r\n";
            }
            else
            {
                AppendCursor(output, imageRows + 1, 1);
            }
            output += "\x1b[0m\x1b[K";
            currentForeground.reset();
            currentBackground.reset();
            output += visibleStatusLine;
            lastWrittenRow = imageRows;
            lastWrittenColumn = static_cast<uint32_t>(visibleStatusLine.size()) + 1;
            hasCursor = true;
        }

        if (!output.empty())
        {
            output += "\x1b[0m";
        }

        previousColumns_ = columns;
        previousImageRows_ = imageRows;
        previousCells_ = std::move(cells);
        previousStatusLine_ = visibleStatusLine;
        return output;
    }

    void TerminalBlitter::AppendCursor(std::string& output, uint32_t row, uint32_t column)
    {
        output += "\x1b[";
        AppendNumber(output, row);
        output += ";";
        AppendNumber(output, column);
        output += "H";
    }

    void TerminalBlitter::AppendCell(std::string& output,
                                     const FCell& cell,
                                     std::optional<FRgb8>& currentForeground,
                                     std::optional<FRgb8>& currentBackground)
    {
        if (!currentForeground.has_value() || currentForeground.value() != cell.upper)
        {
            output += "\x1b[38;2;";
            AppendNumber(output, cell.upper.r);
            output += ";";
            AppendNumber(output, cell.upper.g);
            output += ";";
            AppendNumber(output, cell.upper.b);
            output += "m";
            currentForeground = cell.upper;
        }
        if (!currentBackground.has_value() || currentBackground.value() != cell.lower)
        {
            output += "\x1b[48;2;";
            AppendNumber(output, cell.lower.r);
            output += ";";
            AppendNumber(output, cell.lower.g);
            output += ";";
            AppendNumber(output, cell.lower.b);
            output += "m";
            currentBackground = cell.lower;
        }
        output += "\xE2\x96\x80";
    }

    void TerminalBlitter::AppendFullFrame(std::string& output,
                                          const std::vector<FCell>& cells,
                                          const uint32_t columns,
                                          const uint32_t imageRows,
                                          std::optional<FRgb8>& currentForeground,
                                          std::optional<FRgb8>& currentBackground)
    {
        for (uint32_t row = 0; row < imageRows; ++row)
        {
            const uint32_t rowStart = row * columns;
            for (uint32_t column = 0; column < columns; ++column)
            {
                AppendCell(output, cells[rowStart + column], currentForeground, currentBackground);
            }
            if (row + 1 < imageRows)
            {
                output += "\r\n";
            }
        }
    }

    std::vector<FRgb8> TerminalBlitter::DownscaleBox(const std::vector<FRgb8>& sourcePixels,
                                                     uint32_t sourceWidth,
                                                     uint32_t sourceHeight,
                                                     uint32_t targetWidth,
                                                     uint32_t targetHeight)
    {
        std::vector<FRgb8> result(targetWidth * targetHeight);
        if (targetWidth == 0 || targetHeight == 0)
        {
            return result;
        }

        for (uint32_t y = 0; y < targetHeight; ++y)
        {
            const uint32_t sourceY0 = (y * sourceHeight) / targetHeight;
            const uint32_t sourceY1 = std::max(sourceY0 + 1, ((y + 1) * sourceHeight) / targetHeight);
            for (uint32_t x = 0; x < targetWidth; ++x)
            {
                const uint32_t sourceX0 = (x * sourceWidth) / targetWidth;
                const uint32_t sourceX1 = std::max(sourceX0 + 1, ((x + 1) * sourceWidth) / targetWidth);

                uint64_t sumR = 0;
                uint64_t sumG = 0;
                uint64_t sumB = 0;
                uint64_t samples = 0;
                for (uint32_t sampleY = sourceY0; sampleY < sourceY1; ++sampleY)
                {
                    for (uint32_t sampleX = sourceX0; sampleX < sourceX1; ++sampleX)
                    {
                        const FRgb8& pixel = sourcePixels[sampleY * sourceWidth + sampleX];
                        sumR += pixel.r;
                        sumG += pixel.g;
                        sumB += pixel.b;
                        samples++;
                    }
                }

                FRgb8& outPixel = result[y * targetWidth + x];
                outPixel.r = static_cast<uint8_t>(sumR / samples);
                outPixel.g = static_cast<uint8_t>(sumG / samples);
                outPixel.b = static_cast<uint8_t>(sumB / samples);
            }
        }

        ApplySharpen(result, targetWidth, targetHeight);
        return result;
    }

    void TerminalBlitter::ApplySharpen(std::vector<FRgb8>& pixels, const uint32_t width, const uint32_t height)
    {
        if (pixels.empty() || width < 3 || height < 3 || TuiSharpenAmount <= 0.0f)
        {
            return;
        }

        const std::vector<FRgb8> original = pixels;
        for (uint32_t y = 0; y < height; ++y)
        {
            for (uint32_t x = 0; x < width; ++x)
            {
                float sumR = 0.0f;
                float sumG = 0.0f;
                float sumB = 0.0f;
                float weightSum = 0.0f;
                for (int32_t offsetY = -1; offsetY <= 1; ++offsetY)
                {
                    const uint32_t sampleY = static_cast<uint32_t>(
                        std::clamp<int32_t>(static_cast<int32_t>(y) + offsetY, 0, static_cast<int32_t>(height) - 1));
                    for (int32_t offsetX = -1; offsetX <= 1; ++offsetX)
                    {
                        const uint32_t sampleX = static_cast<uint32_t>(
                            std::clamp<int32_t>(static_cast<int32_t>(x) + offsetX, 0, static_cast<int32_t>(width) - 1));
                        const float weight = (offsetX == 0 && offsetY == 0) ? 4.0f : 1.0f;
                        const FRgb8& sample = original[sampleY * width + sampleX];
                        sumR += sample.r * weight;
                        sumG += sample.g * weight;
                        sumB += sample.b * weight;
                        weightSum += weight;
                    }
                }

                const FRgb8& base = original[y * width + x];
                const float blurR = sumR / weightSum;
                const float blurG = sumG / weightSum;
                const float blurB = sumB / weightSum;
                FRgb8& pixel = pixels[y * width + x];
                pixel.r = ClampToByte(base.r + (base.r - blurR) * TuiSharpenAmount);
                pixel.g = ClampToByte(base.g + (base.g - blurG) * TuiSharpenAmount);
                pixel.b = ClampToByte(base.b + (base.b - blurB) * TuiSharpenAmount);
            }
        }
    }
}
