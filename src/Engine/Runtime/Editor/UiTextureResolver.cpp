#include "Engine/Common/CoreMinimal.hpp"

#include "Engine/Runtime/Editor/UiTextureResolver.hpp"

#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Runtime/Editor/UserInterface.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Utilities/StbImage.hpp"

#include <limits>
#include <vector>

namespace NextUI
{
    FUiTextureHandle FUiTextureResolver::Request(
        const std::string& path,
        const bool srgb,
        const EUiTextureLifetime lifetime,
        const std::function<ImTextureID(const std::string&)>& resolveByName)
    {
        FUiTextureHandle handle;
        if (path.empty() || !Utilities::FileHelper::IsAssetAvailable(path))
        {
            return handle;
        }

        const Assets::ETextureLifetime textureLifetime = lifetime == EUiTextureLifetime::Persistent
            ? Assets::ETextureLifetime::ETL_Persistent
            : Assets::ETextureLifetime::ETL_Transient;
        if (loadRequests_.insert(path).second)
        {
            Assets::GlobalTexturePool::LoadTexture(path, srgb, textureLifetime);
        }

        handle.textureId = resolveByName(path);
        handle.valid = handle.textureId != 0;
        if (!handle.valid)
        {
            Assets::GlobalTexturePool::LoadTexture(path, srgb, textureLifetime);
            handle.textureId = resolveByName(path);
            handle.valid = handle.textureId != 0;
        }

        if (const auto found = pixelSizeCache_.find(path); found != pixelSizeCache_.end())
        {
            handle.pixelSize = found->second;
            return handle;
        }
        int width = 0;
        int height = 0;
        int componentCount = 0;
        std::vector<uint8_t> imageData;
        const bool loaded = Utilities::Package::FPackageFileSystem::GetInstance().LoadFile(path, imageData);
        if (loaded && imageData.size() <= static_cast<size_t>(std::numeric_limits<int>::max()) &&
            stbi_info_from_memory(imageData.data(), static_cast<int>(imageData.size()),
                                  &width, &height, &componentCount) != 0 && width > 0 && height > 0)
        {
            handle.pixelSize = ImVec2(static_cast<float>(width), static_cast<float>(height));
        }
        pixelSizeCache_[path] = handle.pixelSize;
        return handle;
    }
}
