// FCPUBrickTable + FCPUPageIndex: active brick classification and page index
// upload for the software GI brick pool.
// Split from CPUAccelerationStructure.cpp; same namespace, separate TU.
#include "Engine/Assets/Acceleration/CPUAccelerationStructure.h"
#include "Engine/Assets/Acceleration/CPUAccelerationStructure.Internal.hpp"
#include "Engine/Runtime/Subsystems/TaskCoordinator.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Assets/Core/Node.h"
#include "Engine/Runtime/Components/RenderComponent.h"
#include "Engine/Runtime/Components/PhysicsComponent.h"
#include "Engine/Assets/GPU/TextureImage.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Assets/Core/Scene.hpp"

namespace Assets::CPU
{

using namespace Assets;

void FCPUPageIndex::Init()
{
    pageIndex.resize(Assets::ACGI_PAGE_COUNT * Assets::ACGI_PAGE_COUNT);
}

bool FCPUBrickTable::UpdateData(const std::vector<FCPUProbeBaker>& bakers, uint32_t cascadeCapacity,
                                uint32_t poolBricksPerCascade, int dilationRadius,
                                const std::vector<Assets::AmbientBrickResidency>* residency,
                                uint32_t currentFrame, bool hitDriven, bool bounceHitAffectsResidency,
                                uint32_t graceFrames, uint32_t evictFrames)
{
    const int BX = Assets::GPU_SCENE_AMBIENT_BRICKS_X;
    const int BY = Assets::GPU_SCENE_AMBIENT_BRICKS_Y;
    const int BZ = Assets::GPU_SCENE_AMBIENT_BRICKS_Z;
    const int EDGE = Assets::GPU_SCENE_AMBIENT_BRICK_EDGE;
    const uint32_t BPC = static_cast<uint32_t>(Assets::GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE);
    const uint32_t kInvalid = Assets::GPU_SCENE_AMBIENT_BRICK_INVALID;

    const std::vector<uint32_t> oldBrickTable = brickTable;
    brickTable.assign(static_cast<size_t>(cascadeCapacity) * BPC, kInvalid);
    activeBrickList.assign(static_cast<size_t>(cascadeCapacity) * poolBricksPerCascade, kInvalid);
    activeBricksPerCascade.assign(cascadeCapacity, 0u);
    candidateBricksPerCascade.assign(cascadeCapacity, 0u);
    recentlyHitBricksPerCascade.assign(cascadeCapacity, 0u);
    slotsToClear.clear();
    if (candidateFirstSeenFrames.size() != static_cast<size_t>(cascadeCapacity) * BPC)
    {
        candidateFirstSeenFrames.assign(static_cast<size_t>(cascadeCapacity) * BPC, 0u);
    }

    const uint32_t cascadesToProcess = std::min<uint32_t>(cascadeCapacity, static_cast<uint32_t>(bakers.size()));
    uint32_t totalActive = 0;
    uint32_t totalOverflow = 0;
    std::vector<uint8_t> active(BPC, 0);
    std::vector<uint8_t> occupied(BPC, 0);
    std::vector<uint8_t> requested(BPC, 0);
    std::vector<uint32_t> oldOwner(poolBricksPerCascade, kInvalid);
    std::vector<uint32_t> newOwner(poolBricksPerCascade, kInvalid);
    std::vector<uint8_t> usedSlots(poolBricksPerCascade, 0u);
    for (uint32_t c = 0; c < cascadesToProcess; ++c)
    {
        const FCPUProbeBaker& baker = bakers[c];
        std::fill(active.begin(), active.end(), uint8_t(0));
        std::fill(occupied.begin(), occupied.end(), uint8_t(0));

        const uint32_t voxelCount = static_cast<uint32_t>(baker.voxels.size());
        for (uint32_t v = 0; v < voxelCount; ++v)
        {
            if (baker.voxels[v].matId == 0)
            {
                continue;
            }
            const uint32_t y = v / (Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY);
            const uint32_t z = (v - y * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY) / Assets::CUBE_SIZE_XY;
            const uint32_t x = v - y * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY - z * Assets::CUBE_SIZE_XY;
            const int sbx = static_cast<int>(x) / EDGE;
            const int sby = static_cast<int>(y) / EDGE;
            const int sbz = static_cast<int>(z) / EDGE;
            occupied[static_cast<uint32_t>(sby) * (BX * BZ) + static_cast<uint32_t>(sbz) * BX +
                     static_cast<uint32_t>(sbx)] = 1;
        }

        for (uint32_t b = 0; b < BPC; ++b)
        {
            if (!occupied[b])
            {
                continue;
            }

            const int sby = static_cast<int>(b / (BX * BZ));
            const int sbz = static_cast<int>((b - static_cast<uint32_t>(sby) * (BX * BZ)) / BX);
            const int sbx = static_cast<int>(b - static_cast<uint32_t>(sby) * (BX * BZ) -
                                             static_cast<uint32_t>(sbz) * BX);
            const int yMin = std::max(0, sby - dilationRadius);
            const int yMax = std::min(BY - 1, sby + dilationRadius);
            const int zMin = std::max(0, sbz - dilationRadius);
            const int zMax = std::min(BZ - 1, sbz + dilationRadius);
            const int xMin = std::max(0, sbx - dilationRadius);
            const int xMax = std::min(BX - 1, sbx + dilationRadius);

            for (int y = yMin; y <= yMax; ++y)
            {
                const uint32_t yzBase = static_cast<uint32_t>(y) * (BX * BZ);
                for (int z = zMin; z <= zMax; ++z)
                {
                    const uint32_t rowBase = yzBase + static_cast<uint32_t>(z) * BX;
                    for (int x = xMin; x <= xMax; ++x)
                    {
                        active[rowBase + static_cast<uint32_t>(x)] = 1;
                    }
                }
            }
        }

        const size_t cascadeBase = static_cast<size_t>(c) * BPC;
        std::fill(requested.begin(), requested.end(), uint8_t(0));
        uint32_t candidateCount = 0;
        uint32_t recentlyHitCount = 0;
        for (uint32_t b = 0; b < BPC; ++b)
        {
            if (!active[b])
            {
                candidateFirstSeenFrames[cascadeBase + b] = 0u;
                continue;
            }

            ++candidateCount;
            uint32_t& firstSeen = candidateFirstSeenFrames[cascadeBase + b];
            if (firstSeen == 0u)
            {
                firstSeen = currentFrame + 1u;
            }

            bool recentlyHit = false;
            if (residency && cascadeBase + b < residency->size())
            {
                const AmbientBrickResidency& state = (*residency)[cascadeBase + b];
                uint32_t lastHit = state.lastConsumerHitFrame;
                if (bounceHitAffectsResidency)
                {
                    lastHit = std::max(lastHit, state.lastBounceHitFrame);
                }
                recentlyHit = lastHit != 0u && currentFrame >= lastHit && currentFrame - lastHit <= evictFrames;
            }
            recentlyHitCount += recentlyHit ? 1u : 0u;

            const uint32_t firstSeenFrame = firstSeen - 1u;
            const bool inGrace = currentFrame >= firstSeenFrame && currentFrame - firstSeenFrame <= graceFrames;
            requested[b] = (!hitDriven || inGrace || recentlyHit) ? 1u : 0u;
        }

        candidateBricksPerCascade[c] = candidateCount;
        recentlyHitBricksPerCascade[c] = recentlyHitCount;

        std::fill(oldOwner.begin(), oldOwner.end(), kInvalid);
        std::fill(newOwner.begin(), newOwner.end(), kInvalid);
        std::fill(usedSlots.begin(), usedSlots.end(), uint8_t(0));
        if (oldBrickTable.size() == brickTable.size())
        {
            for (uint32_t b = 0; b < BPC; ++b)
            {
                const uint32_t oldSlot = oldBrickTable[cascadeBase + b];
                if (oldSlot < poolBricksPerCascade)
                {
                    oldOwner[oldSlot] = b;
                    if (requested[b])
                    {
                        brickTable[cascadeBase + b] = oldSlot;
                        newOwner[oldSlot] = b;
                        usedSlots[oldSlot] = 1u;
                    }
                }
            }
        }

        uint32_t nextFreeSlot = 0u;
        for (uint32_t b = 0; b < BPC; ++b)
        {
            if (!requested[b] || brickTable[cascadeBase + b] != kInvalid)
            {
                continue;
            }
            while (nextFreeSlot < poolBricksPerCascade && usedSlots[nextFreeSlot])
            {
                ++nextFreeSlot;
            }
            if (nextFreeSlot >= poolBricksPerCascade)
            {
                ++totalOverflow;
                continue;
            }
            brickTable[cascadeBase + b] = nextFreeSlot;
            newOwner[nextFreeSlot] = b;
            usedSlots[nextFreeSlot] = 1u;
        }

        uint32_t activeCount = 0u;
        for (uint32_t b = 0; b < BPC; ++b)
        {
            if (brickTable[cascadeBase + b] == kInvalid)
            {
                continue;
            }
            activeBrickList[static_cast<size_t>(c) * poolBricksPerCascade + activeCount++] = b;
        }
        for (uint32_t slot = 0; slot < poolBricksPerCascade; ++slot)
        {
            if (oldOwner[slot] != newOwner[slot] && oldOwner[slot] != kInvalid)
            {
                slotsToClear.push_back(c * poolBricksPerCascade + slot);
            }
        }

        activeBricksPerCascade[c] = activeCount;
        totalActive += activeCount;
    }

    activeBricksLastBuild = totalActive;
    return oldBrickTable != brickTable;
}

void FCPUBrickTable::UploadGPU(Vulkan::DeviceMemory& deviceMemory, size_t tableByteOffset, size_t activeListByteOffset)
{
    if (brickTable.empty())
    {
        return;
    }
    const size_t bytes = brickTable.size() * sizeof(uint32_t);
    void* mapped = deviceMemory.Map(static_cast<VkDeviceSize>(tableByteOffset), static_cast<VkDeviceSize>(bytes));
    std::memcpy(mapped, brickTable.data(), bytes);
    deviceMemory.Unmap();

    if (!activeBrickList.empty())
    {
        const size_t activeListBytes = activeBrickList.size() * sizeof(uint32_t);
        mapped = deviceMemory.Map(static_cast<VkDeviceSize>(activeListByteOffset),
                                  static_cast<VkDeviceSize>(activeListBytes));
        std::memcpy(mapped, activeBrickList.data(), activeListBytes);
        deviceMemory.Unmap();
    }
}

void FCPUPageIndex::UpdateData(const std::vector<FCPUProbeBaker>& bakers)
{
    // 粗暴实现，先全部page置空
    for (auto& page : pageIndex)
    {
        page = {};
        page.voxelCount = 0;
    }

    // 聚合所有cascade，构建全局PageIndex覆盖
    for (const FCPUProbeBaker& baker : bakers)
    {
        const uint32_t voxelCount = static_cast<uint32_t>(baker.voxels.size());
        for (uint32_t gIdx = 0; gIdx < voxelCount; ++gIdx)
        {
            const VoxelData& voxel = baker.voxels[gIdx];
            if (voxel.matId == 0)
            {
                continue;
            }

            // convert to local position
            uint y = gIdx / (CUBE_SIZE_XY * CUBE_SIZE_XY);
            uint z = (gIdx - y * CUBE_SIZE_XY * CUBE_SIZE_XY) / CUBE_SIZE_XY;
            uint x = gIdx - y * CUBE_SIZE_XY * CUBE_SIZE_XY - z * CUBE_SIZE_XY;

            vec3 worldPos = vec3(x, y, z) * baker.UNIT_SIZE + baker.CUBE_OFFSET;

            Assets::PageIndex& page = GetPage(worldPos);

            // 当前仅使用voxelCount>0做粗粒度裁剪，保持占用标记语义即可
            page.voxelCount = 1;
        }
    }
}

Assets::PageIndex& FCPUPageIndex::GetPage(glm::vec3 worldpos)
{
    // 假设CUBE_OFFSET定义了世界空间的起始位置
    glm::vec3 relativePos = worldpos - Assets::ACGI_PAGE_OFFSET;

    // 计算页面索引，假设每个page对应PAGE_UNIT的世界空间距离
    // 使用xz平面进行映射
    int pageX = static_cast<int>(relativePos.x / Assets::ACGI_PAGE_SIZE);
    int pageZ = static_cast<int>(relativePos.z / Assets::ACGI_PAGE_SIZE);

    // 限制在有效范围内
    pageX = glm::clamp(pageX, 0, Assets::ACGI_PAGE_COUNT - 1);
    pageZ = glm::clamp(pageZ, 0, Assets::ACGI_PAGE_COUNT - 1);

    // 计算一维索引
    int index = pageZ * Assets::ACGI_PAGE_COUNT + pageX;

    // 返回对应的PageIndex引用
    return pageIndex[index];
}

void FCPUPageIndex::UploadGPU(Vulkan::DeviceMemory& gpuMemory)
{
    UploadGPU(gpuMemory, 0);
}

void FCPUPageIndex::UploadGPU(Vulkan::DeviceMemory& gpuMemory, size_t byteBaseOffset)
{
    PageIndex* data = reinterpret_cast<PageIndex*>(
        gpuMemory.Map(byteBaseOffset, sizeof(PageIndex) * pageIndex.size()));
    std::memcpy(data, pageIndex.data(), pageIndex.size() * sizeof(PageIndex));
    gpuMemory.Unmap();
}
}
