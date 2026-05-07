#pragma once

#include "Common/CoreMinimal.hpp"

namespace Vulkan
{
    class VulkanBaseRenderer;

    class ShaderHotReloader final
    {
    public:
        struct FStatus
        {
            bool enabled = false;
            bool initialized = false;
            std::filesystem::path sourceRoot;
            std::filesystem::path outputRoot;
            std::filesystem::path slangExecutable;
            double pollIntervalSeconds = 0.5;
        };

        ShaderHotReloader() = default;

        void Initialize(VulkanBaseRenderer& renderer);
        void Tick(double deltaSeconds);
        void SetEnabled(bool enabled) { enabled_ = enabled; }
        bool IsEnabled() const { return enabled_; }
        void SetPollInterval(double seconds);
        void RequestRebuildAll() { forceRebuildAll_ = true; }
        FStatus GetStatus() const;

    private:
        struct FShaderCompileRequest
        {
            std::filesystem::path sourcePath;
            std::filesystem::path outputPath;
        };

        static std::filesystem::path ResolveSourceRoot();
        static std::filesystem::path ResolveOutputRoot();
        static std::filesystem::path ResolveSlangExecutable();
        static std::vector<std::filesystem::path> CollectFiles(const std::filesystem::path& root,
                                                               const std::set<std::string>& extensions);
        static bool IsSourceShader(const std::filesystem::path& path);
        static bool TryGetLatestTimestamp(const std::vector<std::filesystem::path>& files,
                                          std::filesystem::file_time_type& outTimestamp);

        std::vector<FShaderCompileRequest> GatherCompileRequests(bool forceAll) const;
        bool CompileShader(const FShaderCompileRequest& request) const;

        VulkanBaseRenderer* renderer_ = nullptr;
        std::filesystem::path sourceRoot_;
        std::filesystem::path outputRoot_;
        std::filesystem::path slangExecutable_;
        double elapsedSeconds_ = 0.0;
        double pollIntervalSeconds_ = 0.5;
        bool enabled_ = true;
        bool initialized_ = false;
        bool forceRebuildAll_ = false;
        std::filesystem::file_time_type lastFailedSourceTimestamp_{};
    };
}
