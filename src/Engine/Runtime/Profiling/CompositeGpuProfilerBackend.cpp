#include "Engine/Runtime/Profiling/CompositeGpuProfilerBackend.hpp"

namespace Runtime
{
    void FCompositeGpuProfilerBackend::AddBackend(std::unique_ptr<IGpuProfilerBackend> backend)
    {
        if (backend != nullptr)
        {
            backends_.push_back(std::move(backend));
        }
    }

    void FCompositeGpuProfilerBackend::BeginFrame(VkCommandBuffer commandBuffer)
    {
        scopeMappings_.clear();
        for (const auto& backend : backends_)
        {
            backend->BeginFrame(commandBuffer);
        }
    }

    void FCompositeGpuProfilerBackend::EndFrame(VkCommandBuffer commandBuffer)
    {
        for (const auto& backend : backends_)
        {
            backend->EndFrame(commandBuffer);
        }
    }

    uint32_t FCompositeGpuProfilerBackend::BeginScope(VkCommandBuffer commandBuffer, const char* name)
    {
        ScopeMapping mapping{};
        mapping.childScopeIds.reserve(backends_.size());
        for (const auto& backend : backends_)
        {
            mapping.childScopeIds.push_back(backend->BeginScope(commandBuffer, name));
        }

        const uint32_t scopeId = static_cast<uint32_t>(scopeMappings_.size());
        scopeMappings_.push_back(std::move(mapping));
        return scopeId;
    }

    void FCompositeGpuProfilerBackend::EndScope(const VkCommandBuffer commandBuffer, const uint32_t scopeId)
    {
        if (scopeId >= scopeMappings_.size())
        {
            return;
        }

        const ScopeMapping& mapping = scopeMappings_[scopeId];
        for (size_t index = 0; index < backends_.size(); ++index)
        {
            const uint32_t childScopeId = mapping.childScopeIds[index];
            if (childScopeId != FrameProfiler::invalidTimerId)
            {
                backends_[index]->EndScope(commandBuffer, childScopeId);
            }
        }
    }

    void FCompositeGpuProfilerBackend::BeginMarker(VkCommandBuffer commandBuffer, const char* name)
    {
        for (const auto& backend : backends_)
        {
            backend->BeginMarker(commandBuffer, name);
        }
    }

    void FCompositeGpuProfilerBackend::EndMarker(VkCommandBuffer commandBuffer)
    {
        for (const auto& backend : backends_)
        {
            backend->EndMarker(commandBuffer);
        }
    }

    float FCompositeGpuProfilerBackend::GetTime(const char* name) const
    {
        return backends_.empty() ? 0.0f : backends_.front()->GetTime(name);
    }

    std::vector<ProfileTimerStat> FCompositeGpuProfilerBackend::FetchTimes(const int maxStack) const
    {
        return backends_.empty() ? std::vector<ProfileTimerStat>{} : backends_.front()->FetchTimes(maxStack);
    }
}
