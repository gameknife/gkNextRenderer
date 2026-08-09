#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextTui/TerminalBlitter.hpp"

#include <algorithm>
#include <array>
#include <limits>

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

        uint32_t ColorDistanceSquared(const FRgb8& first, const FRgb8& second)
        {
            const int32_t red = static_cast<int32_t>(first.r) - static_cast<int32_t>(second.r);
            const int32_t green = static_cast<int32_t>(first.g) - static_cast<int32_t>(second.g);
            const int32_t blue = static_cast<int32_t>(first.b) - static_cast<int32_t>(second.b);
            return static_cast<uint32_t>(red * red + green * green + blue * blue);
        }

        uint32_t QuadrantGlyph(const uint8_t mask)
        {
            switch (mask)
            {
            case 0x0: return 0x20;   // space
            case 0x1: return 0x2598; // upper left
            case 0x2: return 0x259D; // upper right
            case 0x3: return 0x2580; // upper half
            case 0x4: return 0x2596; // lower left
            case 0x5: return 0x258C; // left half
            case 0x6: return 0x259E; // upper right and lower left
            case 0x7: return 0x259B; // upper left, upper right and lower left
            case 0x8: return 0x2597; // lower right
            case 0x9: return 0x259A; // upper left and lower right
            case 0xA: return 0x2590; // right half
            case 0xB: return 0x259C; // upper left, upper right and lower right
            case 0xC: return 0x2584; // lower half
            case 0xD: return 0x2599; // upper left, lower left and lower right
            case 0xE: return 0x259F; // upper right, lower left and lower right
            case 0xF: return 0x2588; // full block
            default: return 0x20;
            }
        }

        void AppendCodePoint(std::string& output, const uint32_t codePoint)
        {
            if (codePoint <= 0x7f)
            {
                output.push_back(static_cast<char>(codePoint));
            }
            else if (codePoint <= 0x7ff)
            {
                output.push_back(static_cast<char>(0xc0 | (codePoint >> 6)));
                output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
            }
            else if (codePoint <= 0xffff)
            {
                output.push_back(static_cast<char>(0xe0 | (codePoint >> 12)));
                output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
                output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
            }
            else
            {
                output.push_back(static_cast<char>(0xf0 | (codePoint >> 18)));
                output.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3f)));
                output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
                output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
            }
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

    uint32_t TerminalBlitter::PixelsPerCellX() const
    {
        return options_.CellMode == ECellMode::Quadrant ? 2u : 1u;
    }

    uint32_t TerminalBlitter::PixelsPerCellY() const
    {
        return 2u;
    }

    TerminalBlitter::FSourceExtent TerminalBlitter::GetSourceExtent(
        const uint32_t terminalColumns,
        const uint32_t terminalRows) const
    {
        const uint32_t columns = options_.MaxColumns > 0
            ? std::min(terminalColumns, options_.MaxColumns)
            : terminalColumns;
        const uint32_t rows = options_.MaxRows > 0
            ? std::min(terminalRows, options_.MaxRows)
            : terminalRows;
        if (columns == 0 || rows <= 1)
        {
            return {};
        }

        return {
            .Width = columns * PixelsPerCellX(),
            .Height = (rows - 1) * PixelsPerCellY(),
        };
    }

    TerminalBlitter::FSourceExtent TerminalBlitter::GetRenderExtent(
        const uint32_t terminalColumns,
        const uint32_t terminalRows) const
    {
        const FSourceExtent sourceExtent = GetSourceExtent(terminalColumns, terminalRows);
        if (sourceExtent.Width == 0 || sourceExtent.Height == 0)
        {
            return {};
        }

        // Quadrant doubles horizontal samples per cell. Render one extra vertical
        // pair so the swapchain keeps the same physical terminal aspect ratio.
        return {
            .Width = sourceExtent.Width,
            .Height = sourceExtent.Height * PixelsPerCellX(),
        };
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

        const uint32_t pixelsPerCellX = PixelsPerCellX();
        const uint32_t pixelsPerCellY = PixelsPerCellY();
        const uint32_t targetPixelWidth = columns * pixelsPerCellX;
        const uint32_t targetPixelHeight = imageRows * pixelsPerCellY;
        const std::vector<FRgb8> scaledPixels =
            DownscaleBox(sourcePixels, sourceWidth, sourceHeight, targetPixelWidth, targetPixelHeight);

        std::vector<FCell> cells(columns * imageRows);
        if (options_.CellMode == ECellMode::HalfBlock)
        {
            for (uint32_t row = 0; row < imageRows; ++row)
            {
                for (uint32_t column = 0; column < columns; ++column)
                {
                    FCell& cell = cells[row * columns + column];
                    cell.Foreground = scaledPixels[(row * pixelsPerCellY) * targetPixelWidth + column];
                    cell.Background = scaledPixels[(row * pixelsPerCellY + 1) * targetPixelWidth + column];
                    cell.Glyph = 0x2580;
                }
            }
        }
        else
        {
            for (uint32_t row = 0; row < imageRows; ++row)
            {
                for (uint32_t column = 0; column < columns; ++column)
                {
                    const uint32_t pixelX = column * pixelsPerCellX;
                    const uint32_t pixelY = row * pixelsPerCellY;
                    const std::array<FRgb8, 4> quadrantPixels = {
                        scaledPixels[pixelY * targetPixelWidth + pixelX],
                        scaledPixels[pixelY * targetPixelWidth + pixelX + 1],
                        scaledPixels[(pixelY + 1) * targetPixelWidth + pixelX],
                        scaledPixels[(pixelY + 1) * targetPixelWidth + pixelX + 1],
                    };

                    FCell& cell = cells[row * columns + column];
                    uint32_t bestCost = std::numeric_limits<uint32_t>::max();
                    uint8_t bestMask = 0;
                    uint32_t bestForegroundIndex = 0;
                    uint32_t bestBackgroundIndex = 0;
                    for (uint32_t foregroundIndex = 0; foregroundIndex < quadrantPixels.size(); ++foregroundIndex)
                    {
                        for (uint32_t backgroundIndex = foregroundIndex + 1;
                             backgroundIndex < quadrantPixels.size(); ++backgroundIndex)
                        {
                            uint32_t cost = 0;
                            uint8_t mask = 0;
                            for (uint32_t pixelIndex = 0; pixelIndex < quadrantPixels.size(); ++pixelIndex)
                            {
                                const uint32_t foregroundCost =
                                    ColorDistanceSquared(quadrantPixels[pixelIndex], quadrantPixels[foregroundIndex]);
                                const uint32_t backgroundCost =
                                    ColorDistanceSquared(quadrantPixels[pixelIndex], quadrantPixels[backgroundIndex]);
                                if (foregroundCost <= backgroundCost)
                                {
                                    mask |= static_cast<uint8_t>(1u << pixelIndex);
                                    cost += foregroundCost;
                                }
                                else
                                {
                                    cost += backgroundCost;
                                }
                            }

                            if (cost < bestCost)
                            {
                                bestCost = cost;
                                bestMask = mask;
                                bestForegroundIndex = foregroundIndex;
                                bestBackgroundIndex = backgroundIndex;
                            }
                        }
                    }

                    cell.Foreground = quadrantPixels[bestForegroundIndex];
                    cell.Background = quadrantPixels[bestBackgroundIndex];
                    cell.Glyph = QuadrantGlyph(bestMask);
                }
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
        if (!currentForeground.has_value() || currentForeground.value() != cell.Foreground)
        {
            output += "\x1b[38;2;";
            AppendNumber(output, cell.Foreground.r);
            output += ";";
            AppendNumber(output, cell.Foreground.g);
            output += ";";
            AppendNumber(output, cell.Foreground.b);
            output += "m";
            currentForeground = cell.Foreground;
        }
        if (!currentBackground.has_value() || currentBackground.value() != cell.Background)
        {
            output += "\x1b[48;2;";
            AppendNumber(output, cell.Background.r);
            output += ";";
            AppendNumber(output, cell.Background.g);
            output += ";";
            AppendNumber(output, cell.Background.b);
            output += "m";
            currentBackground = cell.Background;
        }
        AppendCodePoint(output, cell.Glyph);
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
