#include "Common/CoreMinimal.hpp"

#include "Vulkan/ShaderHotReloader.hpp"

#include "Rendering/VulkanBaseRenderer.hpp"
#include "Runtime/Platform/PlatformCommon.h"
#include "Utilities/FileHelper.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/stopwatch.h>

namespace Vulkan
{
    namespace
    {
        std::filesystem::path FindProjectRoot()
        {
            namespace fs = std::filesystem;

            fs::path cursor = fs::current_path();
            for (int depth = 0; depth < 8; ++depth)
            {
                if (fs::exists(cursor / "AGENTS.md") && fs::exists(cursor / "assets" / "shaders"))
                {
                    return cursor;
                }

                if (!cursor.has_parent_path())
                {
                    break;
                }
                cursor = cursor.parent_path();
            }

            return {};
        }

        std::string QuotePath(const std::filesystem::path& path)
        {
            return fmt::format("\"{}\"", path.string());
        }
    }

    void ShaderHotReloader::Initialize(VulkanBaseRenderer& renderer)
    {
#if ANDROID || IOS
        (void)renderer;
        enabled_ = false;
#else
        renderer_ = &renderer;
        sourceRoot_ = ResolveSourceRoot();
        outputRoot_ = ResolveOutputRoot();
        slangExecutable_ = ResolveSlangExecutable();
        initialized_ = !sourceRoot_.empty() && !outputRoot_.empty() && !slangExecutable_.empty();

        if (!initialized_)
        {
            SPDLOG_WARN("[HotReload] Shader hot reload disabled. source={}, output={}, slangc={}",
                        sourceRoot_.string(),
                        outputRoot_.string(),
                        slangExecutable_.string());
            enabled_ = false;
            return;
        }

        SPDLOG_INFO("[HotReload] Shader hot reload watching {} -> {}",
                    sourceRoot_.string(),
                    outputRoot_.string());
#endif
    }

    void ShaderHotReloader::Tick(double deltaSeconds)
    {
#if ANDROID || IOS
        (void)deltaSeconds;
        return;
#else
        if (!enabled_ || !initialized_ || renderer_ == nullptr)
        {
            return;
        }

        const bool forceRebuildAll = forceRebuildAll_;
        forceRebuildAll_ = false;

        elapsedSeconds_ += deltaSeconds;
        if (!forceRebuildAll && elapsedSeconds_ < pollIntervalSeconds_)
        {
            return;
        }
        elapsedSeconds_ = 0.0;

        const std::vector<FShaderCompileRequest> requests = GatherCompileRequests(forceRebuildAll);
        if (requests.empty())
        {
            return;
        }

        spdlog::stopwatch stopwatch;
        bool allSucceeded = true;
        for (const FShaderCompileRequest& request : requests)
        {
            allSucceeded = CompileShader(request) && allSucceeded;
        }

        if (!allSucceeded)
        {
            std::filesystem::file_time_type latestSource{};
            const auto shaderFiles = CollectFiles(sourceRoot_, {".slang", ".h"});
            TryGetLatestTimestamp(shaderFiles, latestSource);
            lastFailedSourceTimestamp_ = latestSource;
            SPDLOG_WARN("[HotReload] Shader rebuild failed; keeping existing pipelines.");
            return;
        }

        if (renderer_->HasSwapChain())
        {
            renderer_->ReloadShaders();
        }
        SPDLOG_INFO("[HotReload] Shader rebuilt: {} file(s) in {}", requests.size(), stopwatch.elapsed_ms());
#endif
    }

    void ShaderHotReloader::SetPollInterval(double seconds)
    {
        pollIntervalSeconds_ = std::max(0.1, seconds);
    }

    ShaderHotReloader::FStatus ShaderHotReloader::GetStatus() const
    {
        return {
            .enabled = enabled_,
            .initialized = initialized_,
            .sourceRoot = sourceRoot_,
            .outputRoot = outputRoot_,
            .slangExecutable = slangExecutable_,
            .pollIntervalSeconds = pollIntervalSeconds_,
        };
    }

    std::filesystem::path ShaderHotReloader::ResolveSourceRoot()
    {
        namespace fs = std::filesystem;
        std::vector<fs::path> candidates;

#if defined(GK_NEXT_SOURCE_DIR)
        candidates.emplace_back(fs::path(GK_NEXT_SOURCE_DIR) / "assets" / "shaders");
#endif
        candidates.emplace_back(FindProjectRoot() / "assets" / "shaders");
        candidates.emplace_back(fs::current_path() / "assets" / "shaders");
        candidates.emplace_back(Utilities::FileHelper::GetPlatformFilePath("assets/shaders"));

        std::error_code ec;
        for (const fs::path& candidate : candidates)
        {
            const fs::path normalized = candidate.lexically_normal();
            if (fs::exists(normalized, ec) && fs::is_directory(normalized, ec))
            {
                return fs::absolute(normalized, ec);
            }
            ec.clear();
        }

        return {};
    }

    std::filesystem::path ShaderHotReloader::ResolveOutputRoot()
    {
        namespace fs = std::filesystem;

        std::error_code ec;
        const fs::path outputRoot = fs::absolute(fs::path(Utilities::FileHelper::GetPlatformFilePath("assets/shaders")), ec);
        if (ec)
        {
            return fs::path(Utilities::FileHelper::GetPlatformFilePath("assets/shaders")).lexically_normal();
        }
        return outputRoot;
    }

    std::filesystem::path ShaderHotReloader::ResolveSlangExecutable()
    {
        namespace fs = std::filesystem;

#if WIN32
        constexpr const char* slangExecutableName = "slangc.exe";
#else
        constexpr const char* slangExecutableName = "slangc";
#endif

        const fs::path executableDir = NextRenderer::GetExecutableDirectory();
        const fs::path currentDir = fs::current_path();
        std::vector<fs::path> candidates;

        if (!executableDir.empty())
        {
            candidates.emplace_back(executableDir / ".." / "tools" / "slang" / slangExecutableName);
            candidates.emplace_back(executableDir / "tools" / "slang" / slangExecutableName);
        }
        if (!currentDir.empty())
        {
            candidates.emplace_back(currentDir / ".." / "tools" / "slang" / slangExecutableName);
            candidates.emplace_back(currentDir / "tools" / "slang" / slangExecutableName);
        }
#if defined(GK_SLANGC_EXECUTABLE)
        candidates.emplace_back(fs::path(GK_SLANGC_EXECUTABLE));
#endif
#if defined(GK_NEXT_SOURCE_DIR)
        candidates.emplace_back(fs::path(GK_NEXT_SOURCE_DIR) / "tools" / "slang" / slangExecutableName);
#endif

        std::error_code ec;
        for (const fs::path& candidate : candidates)
        {
            const fs::path normalized = candidate.lexically_normal();
            if (fs::exists(normalized, ec) && fs::is_regular_file(normalized, ec))
            {
                const fs::path absolute = fs::absolute(normalized, ec);
                return ec ? normalized : absolute;
            }
            ec.clear();
        }

        return {};
    }

    std::vector<std::filesystem::path> ShaderHotReloader::CollectFiles(
        const std::filesystem::path& root,
        const std::set<std::string>& extensions)
    {
        namespace fs = std::filesystem;

        std::vector<fs::path> files;
        std::error_code ec;
        if (!fs::exists(root, ec))
        {
            return files;
        }

        for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root, ec))
        {
            if (ec)
            {
                break;
            }
            if (!entry.is_regular_file(ec))
            {
                ec.clear();
                continue;
            }

            const std::string extension = entry.path().extension().string();
            if (extensions.find(extension) != extensions.end())
            {
                files.push_back(entry.path());
            }
        }

        std::sort(files.begin(), files.end());
        return files;
    }

    bool ShaderHotReloader::IsSourceShader(const std::filesystem::path& path)
    {
        const std::string filename = path.filename().string();
        return filename.ends_with(".comp.slang") || filename.ends_with(".vert.slang") ||
               filename.ends_with(".frag.slang");
    }

    bool ShaderHotReloader::TryGetLatestTimestamp(const std::vector<std::filesystem::path>& files,
                                                  std::filesystem::file_time_type& outTimestamp)
    {
        namespace fs = std::filesystem;

        outTimestamp = fs::file_time_type{};
        std::error_code ec;
        bool found = false;
        for (const fs::path& file : files)
        {
            const fs::file_time_type timestamp = fs::last_write_time(file, ec);
            if (!ec && (!found || timestamp > outTimestamp))
            {
                outTimestamp = timestamp;
                found = true;
            }
            ec.clear();
        }
        return found;
    }

    std::vector<ShaderHotReloader::FShaderCompileRequest> ShaderHotReloader::GatherCompileRequests(bool forceAll) const
    {
        namespace fs = std::filesystem;

        const std::vector<fs::path> shaderFiles = CollectFiles(sourceRoot_, {".slang"});
        const std::vector<fs::path> commonFiles = CollectFiles(sourceRoot_ / "common", {".slang", ".h"});

        fs::file_time_type latestSourceTimestamp{};
        std::vector<fs::path> allSourceFiles = shaderFiles;
        allSourceFiles.insert(allSourceFiles.end(), commonFiles.begin(), commonFiles.end());
        TryGetLatestTimestamp(allSourceFiles, latestSourceTimestamp);
        if (!forceAll && latestSourceTimestamp != fs::file_time_type{} && latestSourceTimestamp == lastFailedSourceTimestamp_)
        {
            return {};
        }

        fs::file_time_type latestCommonTimestamp{};
        const bool hasCommonTimestamp = TryGetLatestTimestamp(commonFiles, latestCommonTimestamp);

        std::vector<FShaderCompileRequest> requests;
        std::error_code ec;
        for (const fs::path& sourcePath : shaderFiles)
        {
            if (!IsSourceShader(sourcePath))
            {
                continue;
            }

            const fs::path outputPath = outputRoot_ / (sourcePath.filename().string() + ".spv");
            const fs::file_time_type sourceTimestamp = fs::last_write_time(sourcePath, ec);
            if (ec)
            {
                ec.clear();
                continue;
            }

            bool shouldCompile = forceAll;
            if (!shouldCompile && fs::exists(outputPath, ec))
            {
                const fs::file_time_type outputTimestamp = fs::last_write_time(outputPath, ec);
                shouldCompile = ec || sourceTimestamp > outputTimestamp ||
                                (hasCommonTimestamp && latestCommonTimestamp > outputTimestamp);
            }
            ec.clear();

            if (shouldCompile)
            {
                requests.push_back({sourcePath, outputPath});
            }
        }

        return requests;
    }

    bool ShaderHotReloader::CompileShader(const FShaderCompileRequest& request) const
    {
        namespace fs = std::filesystem;

        std::error_code ec;
        fs::create_directories(request.outputPath.parent_path(), ec);
        if (ec)
        {
            SPDLOG_WARN("[HotReload] Failed to create shader output directory {}: {}",
                        request.outputPath.parent_path().string(),
                        ec.message());
            return false;
        }

        std::string platformDefines;
#if WIN32
        platformDefines += " -DSHADER_CLOCK";
#endif
#if __APPLE__
        platformDefines += " -DPLATFORM_APPLE";
#endif
#if ANDROID
        platformDefines += " -DPLATFORM_ANDROID";
#endif

        const std::string command = fmt::format("{} {} -o {} -entry main -target spirv{}",
                                                QuotePath(slangExecutable_),
                                                QuotePath(request.sourcePath),
                                                QuotePath(request.outputPath),
                                                platformDefines);
        SPDLOG_INFO("[HotReload] Compiling shader {}", request.sourcePath.filename().string());
        const int result = NextRenderer::OSProcess(command.c_str());
        if (result != 0)
        {
            SPDLOG_WARN("[HotReload] slangc failed with code {} for {}", result, request.sourcePath.string());
            return false;
        }

        return true;
    }
}
