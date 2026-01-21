#include "QuickJSEngine.hpp"

#include "Engine.hpp"
#include "Assets/Scene.hpp"
#include "Utilities/FileHelper.hpp"
#include "Platform/PlatformCommon.h"

#include <spdlog/spdlog.h>
#include <spdlog/stopwatch.h>

#if WITH_QUICKJS
#include <ThirdParty/quickjs-ng/quickjspp.hpp>
#endif

namespace
{
#if WITH_QUICKJS
    bool HasExtension(const std::filesystem::path& path, std::initializer_list<const char*> extensions)
    {
        const std::string extension = path.extension().string();
        for (const char* candidate : extensions)
        {
            if (extension == candidate)
            {
                return true;
            }
        }
        return false;
    }

    bool FindLatestTimestamp(const std::filesystem::path& root,
        std::initializer_list<const char*> extensions,
        std::filesystem::file_time_type& outTimestamp)
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::exists(root, ec))
        {
            return false;
        }

        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
        if (ec)
        {
            SPDLOG_WARN("Failed to enumerate {}: {}", root.string(), ec.message());
            return false;
        }

        const fs::recursive_directory_iterator end;
        bool hasTimestamp = false;
        for (; it != end; it.increment(ec))
        {
            if (ec)
            {
                SPDLOG_WARN("Directory iteration error under {}: {}", root.string(), ec.message());
                ec.clear();
                continue;
            }

            if (it->is_directory(ec))
            {
                if (!ec && it->path().filename() == "node_modules")
                {
                    it.disable_recursion_pending();
                }
                ec.clear();
                continue;
            }

            if (ec)
            {
                SPDLOG_WARN("Failed to inspect {}: {}", it->path().string(), ec.message());
                ec.clear();
                continue;
            }

            if (!it->is_regular_file(ec))
            {
                ec.clear();
                continue;
            }

            if (ec)
            {
                SPDLOG_WARN("Failed to query file type for {}: {}", it->path().string(), ec.message());
                ec.clear();
                continue;
            }

            if (!HasExtension(it->path(), extensions))
            {
                continue;
            }

            auto timestamp = it->last_write_time(ec);
            if (ec)
            {
                SPDLOG_WARN("Failed to query timestamp for {}: {}", it->path().string(), ec.message());
                ec.clear();
                continue;
            }

            if (!hasTimestamp || timestamp > outTimestamp)
            {
                outTimestamp = timestamp;
                hasTimestamp = true;
            }
        }

        return hasTimestamp;
    }

    bool HasNewerTypeScriptSources(const std::filesystem::path& projectDir, const std::filesystem::path& outputDir)
    {
        std::filesystem::file_time_type latestSource{};
        if (!FindLatestTimestamp(projectDir, { ".ts", ".tsx" }, latestSource))
        {
            return false;
        }

        std::filesystem::file_time_type latestOutput{};
        if (!FindLatestTimestamp(outputDir, { ".js", ".mjs" }, latestOutput))
        {
            return true;
        }

        return latestSource > latestOutput;
    }

    void Println(qjs::rest<std::string> args)
    {
        for (auto const& arg : args)
        {
            SPDLOG_INFO("{}", arg);
        }
    }

    NextEngine* GetEngine()
    {
        return NextEngine::GetInstance();
    }
#endif
}

QuickJSEngine::QuickJSEngine() = default;

QuickJSEngine::~QuickJSEngine() = default;

void QuickJSEngine::Initialize()
{
#if WITH_QUICKJS
    runtime_ = std::make_unique<qjs::Runtime>();
    context_ = std::make_unique<qjs::Context>(*runtime_);

    try
    {
        CompileTypeScriptSources();

        auto& module = context_->addModule("Engine");
        module.function<&Println>("println");
        module.function<&GetEngine>("GetEngine");

        module.class_<NextEngine>("NextEngine")
                .fun<&NextEngine::GetTotalFrames>("GetTotalFrames")
                .fun<&NextEngine::GetTestNumber>("GetTestNumber")
                .fun<&NextEngine::RegisterJSCallback>("RegisterJSCallback")
                .fun<&NextEngine::GetScenePtr>("GetScenePtr");
        module.class_<Assets::Scene>("Scene")
                .fun<&Assets::Scene::GetIndicesCount>("GetIndicesCount");
        module.class_<NextComponent>("NextComponent")
                .constructor<>()
                .fun<&NextComponent::name_> ("name_")
                .fun<&NextComponent::id_> ("id_");

        std::vector<uint8_t> scriptBuffer;
        if (Utilities::Package::FPackageFileSystem::GetInstance().LoadFile("assets/scripts/test.js", scriptBuffer))
        {
            context_->eval(std::string_view(reinterpret_cast<char*>(scriptBuffer.data())), "<import>", JS_EVAL_TYPE_MODULE);
        }
    }
    catch (qjs::exception)
    {
        auto exc = context_->getException();
        std::cerr << static_cast<std::string>(exc) << std::endl;
        if ((bool)exc["stack"])
        {
            std::cerr << static_cast<std::string>(exc["stack"]) << std::endl;
        }
    }
#endif
}

void QuickJSEngine::Tick(double deltaSeconds)
{
#if WITH_QUICKJS
    if (tickCallback_)
    {
        tickCallback_(deltaSeconds);
    }
#else
    (void)deltaSeconds;
#endif
}

void QuickJSEngine::RegisterTickCallback(std::function<void(double)> callback)
{
#if WITH_QUICKJS
    tickCallback_ = std::move(callback);
#else
    (void)callback;
#endif
}

#if WITH_QUICKJS
void QuickJSEngine::CompileTypeScriptSources()
{
    namespace fs = std::filesystem;

    try
    {
        const fs::path tsconfigPath = fs::path(Utilities::FileHelper::GetNormalizedFilePath("assets/typescript/tsconfig.json"));
        if (tsconfigPath.empty())
        {
            SPDLOG_DEBUG("TypeScript tsconfig not found; skipping compilation.");
            return;
        }

        std::error_code ec;
        if (!fs::exists(tsconfigPath, ec))
        {
            SPDLOG_DEBUG("TypeScript tsconfig missing at {}", tsconfigPath.string());
            return;
        }

        const fs::path projectDir = tsconfigPath.parent_path();
        const fs::path outputDir = fs::absolute(projectDir / "../../assets/scripts");

        const bool forceCompile = std::getenv("NEXTENGINE_FORCE_TSC") != nullptr;
        if (!forceCompile && !HasNewerTypeScriptSources(projectDir, outputDir))
        {
            SPDLOG_INFO("TypeScript outputs are up to date; skipping compilation.");
            return;
        }

        if (!fs::exists(outputDir, ec))
        {
            fs::create_directories(outputDir, ec);
            if (ec)
            {
                SPDLOG_WARN("Failed to create TypeScript output directory {}: {}", outputDir.string(), ec.message());
            }
        }

        std::vector<std::string> commands;
#if WIN32
        commands.emplace_back(fmt::format("tsc -p \"{}\"", projectDir.string()));
#else
        commands.emplace_back(fmt::format("./tsc -p \"{}\"", projectDir.string()));
#endif

        for (const std::string& command : commands)
        {
            if (command.empty())
            {
                continue;
            }

            SPDLOG_INFO("Compiling TypeScript scripts using: {}", command);
            spdlog::stopwatch stopwatch;
            NextRenderer::OSProcess(command.c_str());
            SPDLOG_INFO("---- Compiling TypeScript in {}", stopwatch.elapsed_ms());
            return;
        }

        SPDLOG_WARN("Unable to compile TypeScript sources; continuing with existing JavaScript outputs.");
    }
    catch (const std::exception& e)
    {
        SPDLOG_WARN("Exception while compiling TypeScript sources: {}", e.what());
    }
}
#endif
