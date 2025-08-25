// General Bindless Texture

// Bindless Storage
// descriptorSets.Bind(i, 0, {NULL, rtAccumlatedDiffuse->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL}),
// descriptorSets.Bind(i, 1, {NULL, rtOutputDiffuse->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL}),
// descriptorSets.Bind(i, 2, {NULL, rtVisibility->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL}),
// descriptorSets.Bind(i, 3, {NULL, rtObject0->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL}),
// descriptorSets.Bind(i, 4, {NULL, rtObject1->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL}),
// descriptorSets.Bind(i, 5, {NULL, rtMotionVector_->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL}),
// descriptorSets.Bind(i, 6, {NULL, rtAlbedo_->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL}),
// descriptorSets.Bind(i, 7, {NULL, rtNormal_->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL}),
// descriptorSets.Bind(i, 8, {NULL, rtShaderTimer_->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL}),
// descriptorSets.Bind(i, 9, {NULL, rtDenoised->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL}),
// descriptorSets.Bind(i, 10, {NULL, rtPrevDepth->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL}),
// descriptorSets.Bind(i, 11, {NULL, rtAccumlatedSpecular->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL}),
// descriptorSets.Bind(i, 12, {NULL, rtOutputSpecular->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL}),
// descriptorSets.Bind(i, 13, {NULL, rtAccumlatedAlbedo_->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL}),

// [[vk::binding(0, 2)]] RWTexture2D<float4> FinalImage;
// [[vk::binding(1, 2)]] RWTexture2D<float4> OutImage;
// [[vk::binding(2, 2)]] RWTexture2D<uint> MiniGBuffer;
// [[vk::binding(3, 2)]] RWTexture2D<uint> ObjectId0;
// [[vk::binding(4, 2)]] RWTexture2D<uint> ObjectId1;
// [[vk::binding(5, 2)]] RWTexture2D<float4> OutMotionVector;
// [[vk::binding(6, 2)]] RWTexture2D<float4> OutAlbedoBuffer;
// [[vk::binding(7, 2)]] RWTexture2D<float4> OutNormalBuffer;
// [[vk::binding(8, 2)]] RWTexture2D<float4> ShaderTimerBuffer;
// [[vk::binding(12, 2)]] RWTexture2D<float4> OutImageSpec;
// [[vk::binding(10, 2)]] RWTexture2D<float> OutDepthBuffer;

#ifndef __cplusplus
implementing Common;
[[vk::binding(0, 0)]] RWTexture2D<float> StorageTextureArray[];
namespace Bindless
{
    public static const int RT_ACCUMLATE_DIFFUSE = 0;
    public static const int RT_SINGLE_DIFFUSE = 1;
    public static const int RT_MINIGBUFFER = 2;
    public static const int RT_PREV_DEPTHBUFFER = 10;

    public float GetStorageTexture(int index, int2 coords )
    {
        return StorageTextureArray[index][coords];
    }

    public uint2 GetStorageTextureDimensions(int index)
    {
        uint2 dim;
        StorageTextureArray[index].GetDimensions(dim.x, dim.y);
        return dim;
    }
}

#endif
#include "PreProcessor.slang"