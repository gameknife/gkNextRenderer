// FCPUBrickTable + FCPUPageIndex: active brick classification and page index
// upload for the software GI brick pool.
// Split from CPUAccelerationStructure.cpp; same namespace, separate TU.
#include "Engine/Assets/Acceleration/CPUAccelerationStructure.hpp"
#include "Engine/Assets/Acceleration/CPUAccelerationStructure.Internal.hpp"
#include "Engine/Runtime/Subsystems/TaskCoordinator.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
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
    const std::vector<uint8_t> oldDirtyBricks = dirtyBricks;
    brickTable.assign(static_cast<size_t>(cascadeCapacity) * BPC, kInvalid);
    activeBrickList.assign(static_cast<size_t>(cascadeCapacity) * poolBricksPerCascade, kInvalid);
    activeBricksPerCascade.assign(cascadeCapacity, 0u);
    dirtyBricks.assign(static_cast<size_t>(cascadeCapacity) * BPC, 0u);
    dirtyBricksPerCascade.assign(cascadeCapacity, 0u);
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
                        if (cascadeBase + b < oldDirtyBricks.size())
                        {
                            dirtyBricks[cascadeBase + b] = oldDirtyBricks[cascadeBase + b];
                        }
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
            MarkDirty(static_cast<uint32_t>(cascadeBase + b));
        }

        for (uint32_t slot = 0; slot < poolBricksPerCascade; ++slot)
        {
            if (oldOwner[slot] != newOwner[slot] && oldOwner[slot] != kInvalid)
            {
                slotsToClear.push_back(c * poolBricksPerCascade + slot);
            }
        }

        totalActive += static_cast<uint32_t>(std::count_if(
            brickTable.begin() + static_cast<std::ptrdiff_t>(cascadeBase),
            brickTable.begin() + static_cast<std::ptrdiff_t>(cascadeBase + BPC),
            [](uint32_t slot) { return slot != Assets::GPU_SCENE_AMBIENT_BRICK_INVALID; }));
    }

    RebuildActiveBrickList(cascadeCapacity, poolBricksPerCascade);
    activeBricksLastBuild = totalActive;
    return oldBrickTable != brickTable;
}

bool FCPUBrickTable::MarkDirty(uint32_t globalBrickIndex)
{
    if (globalBrickIndex >= dirtyBricks.size() || dirtyBricks[globalBrickIndex] != 0u)
    {
        return false;
    }
    dirtyBricks[globalBrickIndex] = 1u;
    ++dirtyRevision;
    return true;
}

void FCPUBrickTable::RebuildActiveBrickList(uint32_t cascadeCapacity, uint32_t poolBricksPerCascade)
{
    const uint32_t bricksPerCascade = static_cast<uint32_t>(Assets::GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE);
    const uint32_t invalid = Assets::GPU_SCENE_AMBIENT_BRICK_INVALID;
    activeBrickList.assign(static_cast<size_t>(cascadeCapacity) * poolBricksPerCascade, invalid);
    activeBricksPerCascade.assign(cascadeCapacity, 0u);
    dirtyBricksPerCascade.assign(cascadeCapacity, 0u);

    for (uint32_t cascade = 0; cascade < cascadeCapacity; ++cascade)
    {
        const size_t brickBase = static_cast<size_t>(cascade) * bricksPerCascade;
        const size_t listBase = static_cast<size_t>(cascade) * poolBricksPerCascade;
        uint32_t writeIndex = 0u;
        for (uint32_t dirtyPass = 1u; dirtyPass <= 2u; ++dirtyPass)
        {
            const bool wantDirty = dirtyPass == 1u;
            for (uint32_t brick = 0; brick < bricksPerCascade; ++brick)
            {
                const size_t globalBrick = brickBase + brick;
                if (globalBrick >= brickTable.size() || brickTable[globalBrick] == invalid ||
                    (dirtyBricks[globalBrick] != 0u) != wantDirty)
                {
                    continue;
                }
                if (writeIndex < poolBricksPerCascade)
                {
                    activeBrickList[listBase + writeIndex++] = brick;
                }
            }
            if (wantDirty)
            {
                dirtyBricksPerCascade[cascade] = writeIndex;
            }
        }
        activeBricksPerCascade[cascade] = writeIndex;
    }
}

void FCPUBrickTable::MarkAllActiveDirty()
{
    for (uint32_t brick = 0; brick < brickTable.size(); ++brick)
    {
        if (brickTable[brick] != Assets::GPU_SCENE_AMBIENT_BRICK_INVALID)
        {
            MarkDirty(brick);
        }
    }
    const uint32_t cascadeCapacity = static_cast<uint32_t>(activeBricksPerCascade.size());
    const uint32_t poolBricksPerCascade = cascadeCapacity == 0u
        ? 0u
        : static_cast<uint32_t>(activeBrickList.size() / cascadeCapacity);
    RebuildActiveBrickList(cascadeCapacity, poolBricksPerCascade);
}

void FCPUBrickTable::MarkDirtyBounds(const std::vector<FCPUProbeBaker>& bakers,
                                     const glm::vec3& worldMin, const glm::vec3& worldMax,
                                     uint32_t poolBricksPerCascade)
{
    const int edge = Assets::GPU_SCENE_AMBIENT_BRICK_EDGE;
    const int bx = Assets::GPU_SCENE_AMBIENT_BRICKS_X;
    const int by = Assets::GPU_SCENE_AMBIENT_BRICKS_Y;
    const int bz = Assets::GPU_SCENE_AMBIENT_BRICKS_Z;
    const uint32_t bricksPerCascade = static_cast<uint32_t>(Assets::GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE);
    const uint32_t cascadeCount = std::min<uint32_t>(static_cast<uint32_t>(bakers.size()),
                                                     static_cast<uint32_t>(activeBricksPerCascade.size()));
    for (uint32_t cascade = 0; cascade < cascadeCount; ++cascade)
    {
        const FCPUProbeBaker& baker = bakers[cascade];
        const glm::ivec3 minCell = glm::clamp(glm::ivec3(glm::floor((worldMin - baker.CUBE_OFFSET) / baker.UNIT_SIZE)),
                                              glm::ivec3(0), glm::ivec3(CUBE_SIZE_XY - 1, CUBE_SIZE_Z - 1, CUBE_SIZE_XY - 1));
        const glm::ivec3 maxCell = glm::clamp(glm::ivec3(glm::ceil((worldMax - baker.CUBE_OFFSET) / baker.UNIT_SIZE)),
                                              glm::ivec3(0), glm::ivec3(CUBE_SIZE_XY - 1, CUBE_SIZE_Z - 1, CUBE_SIZE_XY - 1));
        const glm::ivec3 minBrick = glm::max(minCell / edge - glm::ivec3(1), glm::ivec3(0));
        const glm::ivec3 maxBrick = glm::min(maxCell / edge + glm::ivec3(1), glm::ivec3(bx - 1, by - 1, bz - 1));
        for (int y = minBrick.y; y <= maxBrick.y; ++y)
        {
            for (int z = minBrick.z; z <= maxBrick.z; ++z)
            {
                for (int x = minBrick.x; x <= maxBrick.x; ++x)
                {
                    const uint32_t localBrick = static_cast<uint32_t>(y * bx * bz + z * bx + x);
                    const uint32_t globalBrick = cascade * bricksPerCascade + localBrick;
                    if (globalBrick < brickTable.size() &&
                        brickTable[globalBrick] != Assets::GPU_SCENE_AMBIENT_BRICK_INVALID)
                    {
                        MarkDirty(globalBrick);
                    }
                }
            }
        }
    }
    RebuildActiveBrickList(static_cast<uint32_t>(activeBricksPerCascade.size()), poolBricksPerCascade);
}

void FCPUBrickTable::AcknowledgeDirty(uint64_t revision)
{
    if (revision != dirtyRevision)
    {
        return;
    }
    std::fill(dirtyBricks.begin(), dirtyBricks.end(), uint8_t(0));
    const uint32_t cascadeCapacity = static_cast<uint32_t>(activeBricksPerCascade.size());
    const uint32_t poolBricksPerCascade = cascadeCapacity == 0u
        ? 0u
        : static_cast<uint32_t>(activeBrickList.size() / cascadeCapacity);
    RebuildActiveBrickList(cascadeCapacity, poolBricksPerCascade);
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
    // Simple implementation: clear every page first.
    for (auto& page : pageIndex)
    {
        page = {};
        page.voxelCount = 0;
    }

    // Aggregate all cascades to build the global PageIndex coverage.
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

            // Coarse culling currently only tests voxelCount > 0; preserve occupancy-marker semantics.
            page.voxelCount = 1;
        }
    }
}

Assets::PageIndex& FCPUPageIndex::GetPage(glm::vec3 worldpos)
{
    // CUBE_OFFSET defines the world-space origin.
    glm::vec3 relativePos = worldpos - Assets::ACGI_PAGE_OFFSET;

    // Compute the page index, with each page spanning PAGE_UNIT in world space.
    // Map on the XZ plane.
    int pageX = static_cast<int>(relativePos.x / Assets::ACGI_PAGE_SIZE);
    int pageZ = static_cast<int>(relativePos.z / Assets::ACGI_PAGE_SIZE);

    // Clamp to the valid range.
    pageX = glm::clamp(pageX, 0, Assets::ACGI_PAGE_COUNT - 1);
    pageZ = glm::clamp(pageZ, 0, Assets::ACGI_PAGE_COUNT - 1);

    // Compute the linear index.
    int index = pageZ * Assets::ACGI_PAGE_COUNT + pageX;

    // Return the corresponding PageIndex reference.
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
