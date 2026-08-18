#include "Engine/Runtime/Profiling/CompositeGpuProfilerBackend.hpp"
#include "Engine/Runtime/Profiling/ProfileScopeTree.hpp"

#include <catch2/catch_test_macros.hpp>
#include <vulkan/vulkan.h>

TEST_CASE("Profile scope tree keeps duplicate names distinct by call tree", "[ProfileScopeTree]")
{
    Runtime::ProfileScopeTree tree;

    const auto frameA = tree.BeginScope("frame");
    const auto passA = tree.BeginScope("pass");
    tree.EndScope(passA);
    const auto passB = tree.BeginScope("pass");
    tree.EndScope(passB);
    tree.EndScope(frameA);

    const auto frameB = tree.BeginScope("frame");
    const auto passC = tree.BeginScope("pass");
    tree.EndScope(passC);
    tree.EndScope(frameB);

    REQUIRE(tree.Size() == 5);
    CHECK(tree.GetRecord(0)->stableKey == "/frame#0");
    CHECK(tree.GetRecord(1)->stableKey == "/frame#0/pass#0");
    CHECK(tree.GetRecord(2)->stableKey == "/frame#0/pass#1");
    CHECK(tree.GetRecord(3)->stableKey == "/frame#1");
    CHECK(tree.GetRecord(4)->stableKey == "/frame#1/pass#0");
    CHECK(tree.GetRecord(1)->depth == 1);
    CHECK(tree.GetRecord(4)->depth == 1);
}

TEST_CASE("Profile scope tree filters depth and preserves mismatched stack state", "[ProfileScopeTree]")
{
    Runtime::ProfileScopeTree tree;
    const auto root = tree.BeginScope("root");
    const auto child = tree.BeginScope("child");
    tree.SetElapsedMilliseconds(root, 1.0f);
    tree.SetElapsedMilliseconds(child, 2.0f);

    tree.EndScope(root);
    const auto nested = tree.BeginScope("nested");
    CHECK(tree.GetRecord(nested)->depth == 2);

    tree.EndScope(nested);
    tree.EndScope(child);
    tree.EndScope(root);

    const auto stats = tree.CollectStats();
    REQUIRE(stats.size() == 2);
    CHECK(Runtime::ProfileScopeTree::FilterStats(stats, 1).size() == 1);
    CHECK(Runtime::ProfileScopeTree::FilterStats(stats, 2).size() == 2);
}

namespace
{
    class FakeGpuBackend final : public Runtime::IGpuProfilerBackend
    {
    public:
        explicit FakeGpuBackend(const uint32_t id, const bool invalid = false)
            : id_(id), invalid_(invalid)
        {
        }

        void BeginFrame(VkCommandBuffer) override { ++beginFrameCount; }
        void EndFrame(VkCommandBuffer) override { ++endFrameCount; }

        uint32_t BeginScope(VkCommandBuffer, const char*) override
        {
            ++beginScopeCount;
            return invalid_ ? Runtime::FrameProfiler::invalidTimerId : id_;
        }

        void EndScope(VkCommandBuffer, uint32_t scopeId) override
        {
            if (scopeId != Runtime::FrameProfiler::invalidTimerId)
            {
                ++endScopeCount;
            }
        }

        float GetTime(const char*) const override { return 7.0f; }
        std::vector<Runtime::ProfileTimerStat> FetchTimes(int) const override { return {}; }

        uint32_t beginFrameCount = 0;
        uint32_t endFrameCount = 0;
        uint32_t beginScopeCount = 0;
        uint32_t endScopeCount = 0;

    private:
        uint32_t id_;
        bool invalid_;
    };
}

TEST_CASE("Composite GPU profiler fans out and tolerates invalid child scopes", "[CompositeGpuProfiler]")
{
    auto first = std::make_unique<FakeGpuBackend>(11);
    auto second = std::make_unique<FakeGpuBackend>(22, true);
    FakeGpuBackend* firstPtr = first.get();
    FakeGpuBackend* secondPtr = second.get();

    Runtime::FCompositeGpuProfilerBackend composite;
    composite.AddBackend(std::move(first));
    composite.AddBackend(std::move(second));
    composite.BeginFrame(VK_NULL_HANDLE);
    const auto scopeId = composite.BeginScope(VK_NULL_HANDLE, "render");
    composite.EndScope(VK_NULL_HANDLE, scopeId);
    composite.EndFrame(VK_NULL_HANDLE);

    CHECK(composite.BackendCount() == 2);
    CHECK(firstPtr->beginFrameCount == 1);
    CHECK(secondPtr->beginFrameCount == 1);
    CHECK(firstPtr->beginScopeCount == 1);
    CHECK(secondPtr->beginScopeCount == 1);
    CHECK(firstPtr->endScopeCount == 1);
    CHECK(secondPtr->endScopeCount == 0);
    CHECK(composite.GetTime("render") == 7.0f);
}
