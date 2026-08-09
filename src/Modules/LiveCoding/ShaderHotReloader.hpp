#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Interface/ShaderHotReload.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"

namespace Modules::LiveCoding
{
    class ShaderHotReloader final : public Runtime::IShaderHotReloader
    {
    public:
        explicit ShaderHotReloader(Vulkan::VulkanBaseRenderer& renderer);

        void Tick(double deltaSeconds) override;
        void SetEnabled(bool enabled) override { enabled_ = enabled; }
        bool IsEnabled() const override { return enabled_; }
        void SetPollInterval(double seconds) override;
        void RequestRebuildAll() override { forceRebuildAll_ = true; }
        Runtime::FShaderHotReloadStatus GetStatus() const override;

    private:
        struct FShaderCompileRequest
        {
            std::filesystem::path sourcePath;
            std::filesystem::path outputPath;
        };

        struct FGatherCompileResult
        {
            std::vector<FShaderCompileRequest> requests;
            std::filesystem::file_time_type latestSourceTimestamp{};
        };

        void Initialize(Vulkan::VulkanBaseRenderer& renderer);

        static std::filesystem::path ResolveSourceRoot();
        static std::filesystem::path ResolveOutputRoot();
        static std::filesystem::path ResolveSlangExecutable();
        static std::vector<std::filesystem::path> CollectFiles(const std::filesystem::path& root,
                                                               const std::set<std::string>& extensions);
        static bool IsSourceShader(const std::filesystem::path& path);
        static bool IsRuntimeShaderEntry(const std::filesystem::path& path);
        static bool TryGetLatestTimestamp(const std::vector<std::filesystem::path>& files,
                                          std::filesystem::file_time_type& outTimestamp);

        static FGatherCompileResult GatherCompileRequests(const std::filesystem::path& sourceRoot,
                                                          const std::filesystem::path& outputRoot,
                                                          bool forceAll,
                                                          std::filesystem::file_time_type lastFailedSourceTimestamp);
        void StartGatherCompileRequests(bool forceAll);
        void FinishGatherCompileRequests(FGatherCompileResult result);
        bool CompileShader(const FShaderCompileRequest& request) const;

        Vulkan::VulkanBaseRenderer* renderer_ = nullptr;
        std::filesystem::path sourceRoot_;
        std::filesystem::path outputRoot_;
        std::filesystem::path slangExecutable_;
        double elapsedSeconds_ = 0.0;
        double pollIntervalSeconds_ = 0.5;
        bool enabled_ = true;
        bool initialized_ = false;
        bool forceRebuildAll_ = false;
        bool gatherTaskInFlight_ = false;
        std::filesystem::file_time_type lastFailedSourceTimestamp_{};
    };
}
