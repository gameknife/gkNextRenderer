#include "Engine/Runtime/Editor/FontLoader.h"

#include "Engine/Utilities/FileHelper.hpp"

#include <filesystem>
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace
{
    const ImWchar* ResolveGlyphRanges(ImFontAtlas& fontAtlas, bool includeChineseFull, const char* extraGlyphsUtf8)
    {
        if (!includeChineseFull && (!extraGlyphsUtf8 || extraGlyphsUtf8[0] == '\0'))
        {
            return fontAtlas.GetGlyphRangesDefault();
        }

        static std::unordered_map<std::string, ImVector<ImWchar>> cachedRanges;
        const std::string key = fmt::format("{}:{}", includeChineseFull ? "zh" : "default",
                                            extraGlyphsUtf8 ? extraGlyphsUtf8 : "");
        auto [iter, inserted] = cachedRanges.try_emplace(key);
        if (inserted)
        {
            ImFontGlyphRangesBuilder builder;
            builder.AddRanges(includeChineseFull ? fontAtlas.GetGlyphRangesChineseFull() : fontAtlas.GetGlyphRangesDefault());
            if (extraGlyphsUtf8 && extraGlyphsUtf8[0] != '\0')
            {
                builder.AddText(extraGlyphsUtf8);
            }
            builder.BuildRanges(&iter->second);
        }

        return iter->second.Data;
    }
}

namespace NextUI::FontLoader
{
    ImFont* Load(const FFontRequest& request)
    {
        if (request.filePath.empty() || request.pixelSize <= 0.0f)
        {
            return nullptr;
        }

        ImGuiIO& io = ImGui::GetIO();
        const std::string platformPath = Utilities::FileHelper::GetPlatformFilePath(request.filePath.c_str());
        const ImWchar* ranges = request.glyphRanges ? request.glyphRanges :
            ResolveGlyphRanges(*io.Fonts, request.includeChineseFull, request.extraGlyphsUtf8);
        ImFontConfig fontConfig;
        const ImFontConfig* fontConfigPtr = request.fontConfig;
        if (request.rasterizerDensity > 0.0f && request.rasterizerDensity != 1.0f)
        {
            if (request.fontConfig != nullptr)
            {
                fontConfig = *request.fontConfig;
            }
            fontConfig.RasterizerDensity = request.rasterizerDensity;
            fontConfigPtr = &fontConfig;
        }

        ImFont* font = nullptr;
        if (std::filesystem::exists(platformPath))
        {
            font = io.Fonts->AddFontFromFileTTF(platformPath.c_str(), request.pixelSize, fontConfigPtr, ranges);
        }
        else
        {
            std::vector<uint8_t> fontData;
            if (Utilities::Package::FPackageFileSystem::GetInstance().LoadFile(request.filePath, fontData) && !fontData.empty())
            {
                void* data = IM_ALLOC(fontData.size());
                std::memcpy(data, fontData.data(), fontData.size());
                font = io.Fonts->AddFontFromMemoryTTF(data,
                                                      static_cast<int>(fontData.size()),
                                                      request.pixelSize,
                                                      fontConfigPtr,
                                                      ranges);
            }
        }

        if (!font)
        {
            if (request.warnOnFailure)
            {
                SPDLOG_ERROR("[FontLoader] failed to load font '{}'", platformPath);
            }
            return nullptr;
        }

        if (request.setAsDefault)
        {
            io.FontDefault = font;
        }
        return font;
    }

    ImFont* LoadDefaultUiFont(std::string_view fontPath, float pixelSize)
    {
        return Load(FFontRequest{
            .filePath = std::string(fontPath),
            .pixelSize = pixelSize,
            .includeChineseFull = true,
            .setAsDefault = true,
        });
    }
}
