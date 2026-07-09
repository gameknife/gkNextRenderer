#include "Engine/Rendering/ExternalPassRegistry.hpp"

namespace Vulkan
{
    namespace
    {
        std::vector<FExternalPassFactory>& FactoryList()
        {
            static std::vector<FExternalPassFactory> factories;
            return factories;
        }
    }

    void RegisterExternalPassFactory(FExternalPassFactory factory)
    {
        FactoryList().push_back(std::move(factory));
    }

    const std::vector<FExternalPassFactory>& ExternalPassFactories()
    {
        return FactoryList();
    }
}
