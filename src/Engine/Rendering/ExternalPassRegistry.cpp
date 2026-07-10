#include "Engine/Rendering/ExternalPassRegistry.hpp"

#include <algorithm>

namespace Vulkan
{
    namespace
    {
        struct FEntry
        {
            int priority = 0;
            FExternalPassFactory factory;
        };

        std::vector<FEntry>& EntryList()
        {
            static std::vector<FEntry> entries;
            return entries;
        }
    }

    void RegisterExternalPassFactory(const int priority, FExternalPassFactory factory)
    {
        EntryList().push_back({priority, std::move(factory)});
        std::stable_sort(EntryList().begin(), EntryList().end(),
                         [](const FEntry& a, const FEntry& b) { return a.priority < b.priority; });
    }

    const std::vector<FExternalPassFactory>& ExternalPassFactories()
    {
        static std::vector<FExternalPassFactory> sorted;
        sorted.clear();
        for (const FEntry& entry : EntryList())
        {
            sorted.push_back(entry.factory);
        }
        return sorted;
    }
}
