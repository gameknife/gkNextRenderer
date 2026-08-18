#include "Engine/Common/CoreMinimal.hpp"

#include "Modules/LiveCoding/ShaderHotReloader.hpp"
#include "Engine/Runtime/Profiling/TracyIntegration.hpp"

#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/Platform/PlatformCommon.hpp"
#include "Engine/Runtime/Subsystems/TaskCoordinator.hpp"
#include "Engine/Utilities/FileHelper.hpp"

#include <spdlog/stopwatch.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>

namespace Modules::LiveCoding
{
    namespace
    {
        bool IsDebugOrTestShaderEntry(const std::string& filename)
        {
            return filename == "Remote.BgraToYuv.comp.slang" ||
                   filename == "Util.SharcCompileTest.comp.slang";
        }

        std::filesystem::path NormalizeAbsolutePath(const std::filesystem::path& path)
        {
            namespace fs = std::filesystem;

            std::error_code ec;
            const fs::path absolutePath = path.is_absolute() ? path : fs::absolute(path, ec);
            return (ec ? path : absolutePath).lexically_normal();
        }

        std::filesystem::path GetDepfilePath(const std::filesystem::path& outputPath)
        {
            std::filesystem::path depfilePath = outputPath;
            depfilePath += ".d";
            return depfilePath;
        }

        std::string ReadTextFile(const std::filesystem::path& path)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input.is_open())
            {
                return {};
            }

            return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        }

        size_t FindUnescapedColon(const std::string& text)
        {
            for (size_t index = 0; index < text.size(); ++index)
            {
                if (text[index] != ':')
                {
                    continue;
                }

                size_t slashCount = 0;
                for (size_t cursor = index; cursor > 0 && text[cursor - 1] == '\\'; --cursor)
                {
                    ++slashCount;
                }
                if ((slashCount % 2) == 0)
                {
                    return index;
                }
            }
            return std::string::npos;
        }

        std::vector<std::filesystem::path> ParseDepfileDependencies(const std::filesystem::path& depfilePath)
        {
            namespace fs = std::filesystem;

            std::string contents = ReadTextFile(depfilePath);
            if (contents.empty())
            {
                return {};
            }

            std::string flattened;
            flattened.reserve(contents.size());
            for (size_t index = 0; index < contents.size(); ++index)
            {
                if (contents[index] == '\\' && index + 1 < contents.size())
                {
                    if (contents[index + 1] == '\n')
                    {
                        ++index;
                        continue;
                    }
                    if (contents[index + 1] == '\r' && index + 2 < contents.size() && contents[index + 2] == '\n')
                    {
                        index += 2;
                        continue;
                    }
                }
                flattened.push_back(contents[index]);
            }

            const size_t depsStart = FindUnescapedColon(flattened);
            if (depsStart == std::string::npos)
            {
                return {};
            }

            std::vector<fs::path> dependencies;
            std::string token;
            for (size_t index = depsStart + 1; index <= flattened.size(); ++index)
            {
                const char ch = index < flattened.size() ? flattened[index] : ' ';
                if (std::isspace(static_cast<unsigned char>(ch)))
                {
                    if (!token.empty())
                    {
                        fs::path dependency(token);
                        if (!dependency.empty())
                        {
                            dependencies.push_back(NormalizeAbsolutePath(dependency));
                        }
                        token.clear();
                    }
                    continue;
                }

                if (ch == '\\' && index + 1 < flattened.size())
                {
                    const char next = flattened[index + 1];
                    if (next == ':' || next == ' ' || next == '#' || next == '\\')
                    {
                        token.push_back(next);
                        ++index;
                        continue;
                    }
                }

                token.push_back(ch);
            }

            return dependencies;
        }

        std::filesystem::path ResolveImportPath(const std::filesystem::path& sourceRoot, const std::string& moduleName)
        {
            namespace fs = std::filesystem;

            std::vector<fs::path> candidates;
            candidates.emplace_back(sourceRoot / (moduleName + ".slang"));

            std::string modulePath = moduleName;
            std::replace(modulePath.begin(), modulePath.end(), '.', '/');
            candidates.emplace_back(sourceRoot / (modulePath + ".slang"));

            std::error_code ec;
            for (const fs::path& candidate : candidates)
            {
                if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec))
                {
                    return NormalizeAbsolutePath(candidate);
                }
                ec.clear();
            }

            return {};
        }

        std::filesystem::path ResolveIncludePath(const std::filesystem::path& sourceRoot,
                                                 const std::filesystem::path& includingFile,
                                                 const std::string& includeName)
        {
            namespace fs = std::filesystem;

            std::vector<fs::path> candidates;
            candidates.emplace_back(includingFile.parent_path() / includeName);
            candidates.emplace_back(sourceRoot / includeName);

            std::error_code ec;
            for (const fs::path& candidate : candidates)
            {
                if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec))
                {
                    return NormalizeAbsolutePath(candidate);
                }
                ec.clear();
            }

            return {};
        }

        void ParseShaderDependenciesRecursive(const std::filesystem::path& sourceRoot,
                                              const std::filesystem::path& sourcePath,
                                              std::set<std::filesystem::path>& dependencies)
        {
            namespace fs = std::filesystem;

            const fs::path normalizedSource = NormalizeAbsolutePath(sourcePath);
            if (dependencies.find(normalizedSource) != dependencies.end())
            {
                return;
            }
            dependencies.insert(normalizedSource);

            const std::string contents = ReadTextFile(normalizedSource);
            if (contents.empty())
            {
                return;
            }

            const std::regex dependencyPattern(
                R"regex(^\s*(?:import\s+([A-Za-z_][A-Za-z0-9_\.]*)\s*;|(?:__include|#include)\s+"([^"]+)"))regex");

            size_t lineStart = 0;
            while (lineStart <= contents.size())
            {
                const size_t lineEnd = contents.find('\n', lineStart);
                const std::string line = contents.substr(
                    lineStart,
                    lineEnd == std::string::npos ? std::string::npos : lineEnd - lineStart);

                std::smatch match;
                if (std::regex_search(line, match, dependencyPattern))
                {
                    fs::path dependencyPath;
                    if (match[1].matched)
                    {
                        dependencyPath = ResolveImportPath(sourceRoot, match[1].str());
                    }
                    else if (match[2].matched)
                    {
                        dependencyPath = ResolveIncludePath(sourceRoot, normalizedSource, match[2].str());
                    }

                    if (!dependencyPath.empty())
                    {
                        ParseShaderDependenciesRecursive(sourceRoot, dependencyPath, dependencies);
                    }
                }

                if (lineEnd == std::string::npos)
                {
                    break;
                }
                lineStart = lineEnd + 1;
            }
        }

        std::vector<std::filesystem::path> ResolveShaderDependencies(
            const std::filesystem::path& sourceRoot,
            const std::filesystem::path& sourcePath,
            const std::filesystem::path& outputPath)
        {
            namespace fs = std::filesystem;

            std::vector<fs::path> dependencies = ParseDepfileDependencies(GetDepfilePath(outputPath));
            if (!dependencies.empty())
            {
                dependencies.push_back(NormalizeAbsolutePath(sourcePath));
                std::sort(dependencies.begin(), dependencies.end());
                dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
                return dependencies;
            }

            std::set<fs::path> parsedDependencies;
            ParseShaderDependenciesRecursive(sourceRoot, sourcePath, parsedDependencies);
            return {parsedDependencies.begin(), parsedDependencies.end()};
        }

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

    ShaderHotReloader::ShaderHotReloader(Vulkan::VulkanBaseRenderer& renderer)
    {
        Initialize(renderer);
    }

    void ShaderHotReloader::Initialize(Vulkan::VulkanBaseRenderer& renderer)
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

        elapsedSeconds_ += deltaSeconds;
        if (!forceRebuildAll_ && elapsedSeconds_ < pollIntervalSeconds_)
        {
            return;
        }

        if (gatherTaskInFlight_)
        {
            return;
        }

        const bool forceRebuildAll = forceRebuildAll_;
        forceRebuildAll_ = false;
        elapsedSeconds_ = 0.0;
        StartGatherCompileRequests(forceRebuildAll);
#endif
    }

    void ShaderHotReloader::StartGatherCompileRequests(bool forceAll)
    {
        struct FGatherTaskContext
        {
            FGatherCompileResult result;
        };

        gatherTaskInFlight_ = true;

        auto context = std::make_shared<FGatherTaskContext>();
        const std::filesystem::path sourceRoot = sourceRoot_;
        const std::filesystem::path outputRoot = outputRoot_;
        const std::filesystem::file_time_type lastFailedSourceTimestamp = lastFailedSourceTimestamp_;

        Tasks::TaskCoordinator::GetInstance()->AddTask(
            [context, sourceRoot, outputRoot, forceAll, lastFailedSourceTimestamp](Tasks::ResTask& task)
            {
                (void)task;
                context->result = GatherCompileRequests(sourceRoot, outputRoot, forceAll, lastFailedSourceTimestamp);
            },
            [this, context](Tasks::ResTask& task)
            {
                (void)task;
                FinishGatherCompileRequests(std::move(context->result));
            },
            0,
            "Shader hot reload gather");
    }

    void ShaderHotReloader::FinishGatherCompileRequests(FGatherCompileResult result)
    {
        gatherTaskInFlight_ = false;

        if (!enabled_ || !initialized_ || renderer_ == nullptr || result.requests.empty())
        {
            return;
        }

        spdlog::stopwatch stopwatch;
        bool allSucceeded = true;
        for (const FShaderCompileRequest& request : result.requests)
        {
            const bool succeeded = CompileShader(request);
            allSucceeded = succeeded && allSucceeded;
        }

        if (!allSucceeded)
        {
            lastFailedSourceTimestamp_ = result.latestSourceTimestamp;
            SPDLOG_WARN("[HotReload] Shader rebuild failed; keeping existing pipelines.");
            return;
        }

        if (renderer_->HasSwapChain())
        {
            renderer_->ReloadShaders();
        }
        GkProfiling::Message(fmt::format("shader hot reload: {} file(s)", result.requests.size()));
        SPDLOG_INFO("[HotReload] Shader rebuilt: {} file(s) in {}", result.requests.size(), stopwatch.elapsed_ms());
    }

    void ShaderHotReloader::SetPollInterval(double seconds)
    {
        pollIntervalSeconds_ = std::max(0.1, seconds);
    }

    Runtime::FShaderHotReloadStatus ShaderHotReloader::GetStatus() const
    {
        return {
            .shaderHotReloadEnabled = enabled_,
            .shaderInitialized = initialized_,
            .shaderPollIntervalSeconds = pollIntervalSeconds_,
            .shaderSourceRoot = sourceRoot_,
            .shaderOutputRoot = outputRoot_,
            .shaderCompiler = slangExecutable_,
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

    bool ShaderHotReloader::IsRuntimeShaderEntry(const std::filesystem::path& path)
    {
        return IsSourceShader(path) && !IsDebugOrTestShaderEntry(path.filename().string());
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

    ShaderHotReloader::FGatherCompileResult ShaderHotReloader::GatherCompileRequests(
        const std::filesystem::path& sourceRoot,
        const std::filesystem::path& outputRoot,
        bool forceAll,
        std::filesystem::file_time_type lastFailedSourceTimestamp)
    {
        namespace fs = std::filesystem;

        FGatherCompileResult result;

        const std::vector<fs::path> shaderFiles = CollectFiles(sourceRoot, {".slang"});
        const std::vector<fs::path> allSourceFiles = CollectFiles(sourceRoot, {".slang", ".h"});

        TryGetLatestTimestamp(allSourceFiles, result.latestSourceTimestamp);
        if (!forceAll && result.latestSourceTimestamp != fs::file_time_type{} &&
            result.latestSourceTimestamp == lastFailedSourceTimestamp)
        {
            return result;
        }

        std::error_code ec;
        for (const fs::path& sourcePath : shaderFiles)
        {
            if (!IsRuntimeShaderEntry(sourcePath))
            {
                continue;
            }

            const fs::path outputPath = outputRoot / (sourcePath.filename().string() + ".spv");
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
                if (ec || sourceTimestamp > outputTimestamp)
                {
                    shouldCompile = true;
                }
                else
                {
                    const std::vector<fs::path> dependencies =
                        ResolveShaderDependencies(sourceRoot, sourcePath, outputPath);
                    for (const fs::path& dependency : dependencies)
                    {
                        const fs::file_time_type dependencyTimestamp = fs::last_write_time(dependency, ec);
                        if (!ec && dependencyTimestamp > outputTimestamp)
                        {
                            shouldCompile = true;
                            break;
                        }
                        ec.clear();
                    }
                }
            }
            else if (!shouldCompile)
            {
                shouldCompile = true;
            }
            ec.clear();

            if (shouldCompile)
            {
                result.requests.push_back({sourcePath, outputPath});
            }
        }

        return result;
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
#if WIN32 && GK_ENABLE_SHADER_CLOCK
        platformDefines += " -DSHADER_CLOCK";
#endif
#if __APPLE__
        platformDefines += " -DPLATFORM_APPLE";
#endif
#if ANDROID
        platformDefines += " -DPLATFORM_ANDROID";
#endif
        const std::string sourceFilename = request.sourcePath.filename().string();
        if (sourceFilename == "Core.SharcUpdate.comp.slang")
        {
            platformDefines += " -DGK_ENABLE_OFFICIAL_SHARC -DSHARC_UPDATE=1 -DSHARC_QUERY=0";
        }
        else if (sourceFilename == "Core.SharcQuery.comp.slang")
        {
            platformDefines += " -DGK_ENABLE_OFFICIAL_SHARC -DSHARC_UPDATE=0 -DSHARC_QUERY=1";
        }
        else if (sourceFilename == "Core.SharcResolve.comp.slang")
        {
            platformDefines += " -DGK_ENABLE_OFFICIAL_SHARC -DSHARC_UPDATE=0 -DSHARC_QUERY=0";
        }
        else if (sourceFilename == "Util.SharcCompileTest.comp.slang")
        {
            platformDefines += " -DGK_ENABLE_OFFICIAL_SHARC -DSHARC_UPDATE=1 -DSHARC_QUERY=1";
        }
        else if (sourceFilename.starts_with("Core.Sharc"))
        {
            platformDefines += " -DGK_ENABLE_OFFICIAL_SHARC";
        }

        const fs::path depfilePath = GetDepfilePath(request.outputPath);
        const std::string command = fmt::format("{} {} -o {} -entry main -target spirv -I {} -depfile {}{}",
                                                QuotePath(slangExecutable_),
                                                QuotePath(request.sourcePath),
                                                QuotePath(request.outputPath),
                                                QuotePath(sourceRoot_),
                                                QuotePath(depfilePath),
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
