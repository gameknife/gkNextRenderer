#include "TestCommon.hpp"

#include "Engine/Assets/Acceleration/CPUAccelerationStructure.hpp"
#include "Engine/Runtime/Subsystems/TaskCoordinator.hpp"

#include <atomic>
#include <thread>

namespace Assets::CPU
{
    struct FCPUAccelerationStructureTestAccess
    {
        static void Publish(FCPUAccelerationStructure& accelerationStructure,
                            FCPUAccelerationStructure::SnapshotPtr snapshot)
        {
            accelerationStructure.blasSet_ = snapshot ? snapshot->blasSet : nullptr;
            accelerationStructure.PublishSnapshot(std::move(snapshot));
        }

        static uint64_t Queue(FCPUAccelerationStructure& accelerationStructure,
                              std::shared_ptr<FCPUTLASBuildInput> input)
        {
            input->epoch = accelerationStructure.buildEpoch_.load(std::memory_order_acquire);
            return accelerationStructure.QueueBuildInput(std::move(input));
        }
    };
}

namespace
{
    using namespace Assets::CPU;

    std::shared_ptr<const FCPUTLASSnapshot> MakeSnapshot(uint32_t nodeId, uint32_t materialId)
    {
        auto blasSet = std::make_shared<FCPUBLASSet>();
        blasSet->generation = 1;
        blasSet->contexts.resize(1);

        FCPUBLASContext& blas = blasSet->contexts[0];
        blas.triangles = {
            tinybvh::bvhvec4(-1.0f, -1.0f, 0.0f, 0.0f),
            tinybvh::bvhvec4(1.0f, -1.0f, 0.0f, 0.0f),
            tinybvh::bvhvec4(0.0f, 1.0f, 0.0f, 0.0f),
        };
        blas.extinfos.push_back({glm::vec3(0.0f, 0.0f, 1.0f), 0});
        blas.bvh.Build(blas.triangles.data(), 1);
        blasSet->list.push_back(&blas.bvh);

        auto materialTable = std::make_shared<FCPUMaterialTable>();
        materialTable->generation = 1;
        materialTable->entries.resize(materialId + 1);

        auto snapshot = std::make_shared<FCPUTLASSnapshot>();
        snapshot->sceneRevision = nodeId;
        snapshot->blasSet = blasSet;
        snapshot->materialTable = materialTable;

        tinybvh::BLASInstance instance;
        instance.blasIdx = 0;
        const glm::mat4 identity(1.0f);
        const glm::mat4 transposed = glm::transpose(identity);
        std::memcpy(instance.transform, &transposed[0], sizeof(instance.transform));
        snapshot->instances.push_back(instance);

        FCPUTLASInstanceInfo context;
        context.matIdxs.fill(0);
        context.matIdxs[0] = materialId;
        context.nodeId = nodeId;
        snapshot->contexts.push_back(context);

        snapshot->tlas.Build(snapshot->instances.data(), 1, blasSet->list.data(), 1);
        return snapshot;
    }

    std::shared_ptr<FCPUTLASBuildInput> MakeBuildInput(
        const std::shared_ptr<const FCPUTLASSnapshot>& baseline, uint64_t revision,
        uint32_t nodeId, uint32_t materialId)
    {
        auto input = std::make_shared<FCPUTLASBuildInput>();
        input->sceneRevision = revision;
        input->requestTime = std::chrono::steady_clock::now();
        input->blasSet = baseline->blasSet;
        input->previousSnapshot = baseline;
        input->instances = baseline->instances;
        input->contexts = baseline->contexts;
        input->contexts[0].nodeId = nodeId;
        input->contexts[0].matIdxs[0] = materialId;

        auto materialTable = std::make_shared<FCPUMaterialTable>();
        materialTable->generation = revision;
        materialTable->entries.resize(materialId + 1);
        input->materialTable = std::move(materialTable);
        return input;
    }
}

TEST_CASE("CPU acceleration snapshots isolate owners", "[Unit][CPUAccelerationSnapshot]")
{
    FCPUAccelerationStructure first;
    FCPUAccelerationStructure second;
    FCPUAccelerationStructureTestAccess::Publish(first, MakeSnapshot(11, 3));
    FCPUAccelerationStructureTestAccess::Publish(second, MakeSnapshot(22, 7));

    const Assets::RayCastResult firstHit = first.RayCastInCPU({0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f});
    const Assets::RayCastResult secondHit = second.RayCastInCPU({0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f});

    REQUIRE(firstHit.Hit);
    REQUIRE(secondHit.Hit);
    CHECK(firstHit.InstanceId == 11);
    CHECK(firstHit.MaterialId == 3);
    CHECK(secondHit.InstanceId == 22);
    CHECK(secondHit.MaterialId == 7);
    CHECK(first.AcquireSnapshot()->materialTable->entries.size() == 4);
    CHECK(second.AcquireSnapshot()->materialTable->entries.size() == 8);
}

TEST_CASE("CPU acceleration snapshot publish is safe with concurrent readers",
          "[Unit][CPUAccelerationSnapshot][Concurrency]")
{
    FCPUAccelerationStructure accelerationStructure;
    const auto first = MakeSnapshot(31, 1);
    const auto second = MakeSnapshot(32, 2);
    FCPUAccelerationStructureTestAccess::Publish(accelerationStructure, first);

    std::atomic_bool start{false};
    std::atomic_bool failed{false};
    std::vector<std::thread> readers;
    readers.reserve(8);
    for (int readerIndex = 0; readerIndex < 8; ++readerIndex)
    {
        readers.emplace_back([&]()
        {
            while (!start.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            for (int queryIndex = 0; queryIndex < 2000; ++queryIndex)
            {
                const Assets::RayCastResult hit =
                    accelerationStructure.RayCastInCPU({0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f});
                const bool isFirst = hit.InstanceId == 31 && hit.MaterialId == 1;
                const bool isSecond = hit.InstanceId == 32 && hit.MaterialId == 2;
                if (!hit.Hit || (!isFirst && !isSecond))
                {
                    failed.store(true, std::memory_order_relaxed);
                    break;
                }
            }
        });
    }

    start.store(true, std::memory_order_release);
    for (int publishIndex = 0; publishIndex < 1000; ++publishIndex)
    {
        FCPUAccelerationStructureTestAccess::Publish(
            accelerationStructure, (publishIndex & 1) == 0 ? first : second);
    }

    for (std::thread& reader : readers)
    {
        reader.join();
    }
    CHECK_FALSE(failed.load(std::memory_order_relaxed));

    FCPUAccelerationStructureTestAccess::Publish(
        accelerationStructure, std::make_shared<FCPUTLASSnapshot>());
    CHECK_FALSE(accelerationStructure.RayCastInCPU({0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}).Hit);
}

TEST_CASE("CPU acceleration background build coalesces to the latest request",
          "[Unit][CPUAccelerationSnapshot][Concurrency]")
{
    FCPUAccelerationStructure accelerationStructure;
    const auto baseline = MakeSnapshot(40, 0);
    FCPUAccelerationStructureTestAccess::Publish(accelerationStructure, baseline);

    FCPUAccelerationStructureTestAccess::Queue(
        accelerationStructure, MakeBuildInput(baseline, 101, 41, 1));
    FCPUAccelerationStructureTestAccess::Queue(
        accelerationStructure, MakeBuildInput(baseline, 102, 42, 2));
    FCPUAccelerationStructureTestAccess::Queue(
        accelerationStructure, MakeBuildInput(baseline, 103, 43, 3));

    Tasks::TaskCoordinator::GetInstance()->WaitForNamedTask(Tasks::ENamedTaskThread::CPU_AS_BUILD);
    accelerationStructure.PollBVHBuild();
    Tasks::TaskCoordinator::GetInstance()->WaitForNamedTask(Tasks::ENamedTaskThread::CPU_AS_BUILD);
    accelerationStructure.PollBVHBuild();

    const Assets::RayCastResult hit =
        accelerationStructure.RayCastInCPU({0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f});
    REQUIRE(hit.Hit);
    CHECK(hit.InstanceId == 43);
    CHECK(hit.MaterialId == 3);

    const FCPUTLASBuildStats stats = accelerationStructure.GetBuildStats();
    CHECK(stats.publishedRevision == 103);
    CHECK(stats.latestRequestedRevision == 103);
    CHECK(stats.coalescedRequestCount == 2);
    CHECK(stats.completedBuildCount == 2);
    CHECK(stats.snapshotStaleness == 0);
    CHECK(stats.lastBuildMilliseconds >= 0.0);
    CHECK(stats.lastBuildToPublishMilliseconds >= stats.lastBuildMilliseconds);
}
