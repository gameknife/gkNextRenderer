#include "Engine/Vulkan/SyncAndTiming.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class TimerPathBuilder
{
public:
    struct Record
    {
        std::string name;
        std::string stableKey;
        int depth = 0;
        std::unordered_map<std::string, uint32_t> childNameCounts;
    };

    uint32_t Start(const std::string& name)
    {
        const uint32_t id = static_cast<uint32_t>(records.size());
        Record record{};
        record.name = name;
        record.depth = static_cast<int>(activeStack.size());
        record.stableKey = BuildStableKey(name);
        records.push_back(std::move(record));
        activeStack.push_back(id);
        return id;
    }

    void End(uint32_t id)
    {
        if (!activeStack.empty() && activeStack.back() == id)
        {
            activeStack.pop_back();
        }
    }

    std::string BuildStableKey(const std::string& name)
    {
        if (activeStack.empty())
        {
            const uint32_t occurrence = rootNameCounts[name]++;
            return "/" + name + "#" + std::to_string(occurrence);
        }

        auto& parent = records[activeStack.back()];
        const uint32_t occurrence = parent.childNameCounts[name]++;
        return parent.stableKey + "/" + name + "#" + std::to_string(occurrence);
    }

    std::vector<Record> records;
    std::vector<uint32_t> activeStack;
    std::unordered_map<std::string, uint32_t> rootNameCounts;
};

TEST_CASE("Timer path keeps duplicate names distinct by call tree", "[GpuTimer]")
{
    TimerPathBuilder timer;

    const auto frameA = timer.Start("frame");
    const auto passA = timer.Start("pass");
    timer.End(passA);
    const auto passB = timer.Start("pass");
    timer.End(passB);
    timer.End(frameA);

    const auto frameB = timer.Start("frame");
    const auto passC = timer.Start("pass");
    timer.End(passC);
    timer.End(frameB);

    REQUIRE(timer.records.size() == 5);
    CHECK(timer.records[0].stableKey == "/frame#0");
    CHECK(timer.records[1].stableKey == "/frame#0/pass#0");
    CHECK(timer.records[2].stableKey == "/frame#0/pass#1");
    CHECK(timer.records[3].stableKey == "/frame#1");
    CHECK(timer.records[4].stableKey == "/frame#1/pass#0");
    CHECK(timer.records[1].depth == 1);
    CHECK(timer.records[4].depth == 1);
}

TEST_CASE("GPU timestamp elapsed ignores invalid high bits", "[GpuTimer]")
{
    float milliseconds = 0.0f;
    const bool valid = VulkanGpuTimer::TryCalculateElapsedMilliseconds(
        0xffff000000001000ull,
        0xeeee000000003000ull,
        2.0f,
        32,
        milliseconds);

    REQUIRE(valid);
    CHECK(milliseconds == Catch::Approx(0.016384f));
}

TEST_CASE("GPU timestamp elapsed handles valid-bit counter wrap", "[GpuTimer]")
{
    float milliseconds = 0.0f;
    const bool valid = VulkanGpuTimer::TryCalculateElapsedMilliseconds(
        0xfffffff0ull,
        0x00000010ull,
        1.0f,
        32,
        milliseconds);

    REQUIRE(valid);
    CHECK(milliseconds == Catch::Approx(0.000032f));
}

TEST_CASE("GPU timestamp elapsed rejects inverted full-width timestamps", "[GpuTimer]")
{
    float milliseconds = 1.0f;
    const bool valid = VulkanGpuTimer::TryCalculateElapsedMilliseconds(
        1000,
        900,
        1.0f,
        64,
        milliseconds);

    CHECK_FALSE(valid);
    CHECK(milliseconds == 0.0f);
}
