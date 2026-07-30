#include "gkNextMassiveBenchmark.hpp"

#include "Application/Common/DemoScenes.hpp"
#include "Common/BenchMark.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/Loaders/LoaderRegistry.hpp"
#include "Engine/Options.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Engine.hpp"

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(
    Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine)
{
    options.RenderCapacityMode = Runtime::Config::ERenderCapacityMode::Massive;
    AppCommon::RegisterDemoScenes();
    return std::make_unique<MassiveBenchmarkGameInstance>(config, options, engine);
}

MassiveBenchmarkGameInstance::MassiveBenchmarkGameInstance(
    Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine) :
    NextGameInstanceBase(config, options, engine)
{
    config.Title = "gkNextMassiveBenchmark";
    options.RenderCapacityMode = Runtime::Config::ERenderCapacityMode::Massive;
    options.PresentMode = 0;
    options.Width = 1280;
    options.Height = 720;
}

void MassiveBenchmarkGameInstance::ConfigureCVars(NextCVar::FCVarSystem& cvars)
{

}

void MassiveBenchmarkGameInstance::OnInit()
{
    benchMarker_ = std::make_unique<BenchMarker>();
    GetEngine().RequestLoadScene({.filename = "MassiveAsteroidBelt.proc"});
}

void MassiveBenchmarkGameInstance::OnSceneLoaded()
{
    Assets::Scene& scene = GetEngine().GetScene();
    scene.PlayAllTracks();
    const auto& limits = scene.RenderCapacityLimits();
    // The scene owns one animated camera node and one non-renderable environment
    // node in addition to 131070 asteroid nodes. Render-proxy count is the capacity contract.
    if (scene.GetNodeCount() != 131072 ||
        scene.GetRenderProxyCount() != 131070 ||
        limits.renderProxyCapacity != 262140 ||
        limits.primitiveWordCount != 2 ||
        scene.VisibilityFormat() != VK_FORMAT_R32G32_UINT ||
        scene.GetTriangleCount() != 10485600)
    {
        throw std::runtime_error(fmt::format(
            "Massive contract failed: nodes={}, proxies={}, capacity={}, words={}, format={}, triangles={}",
            scene.GetNodeCount(), scene.GetRenderProxyCount(), limits.renderProxyCapacity,
            limits.primitiveWordCount, static_cast<int>(scene.VisibilityFormat()), scene.GetTriangleCount()));
    }
    SPDLOG_INFO(
        "[Massive] nodes={} proxies={} capacity={} visibility=R32G32_UINT primitiveStride={} bytes triangles={}",
        scene.GetNodeCount(), scene.GetRenderProxyCount(), limits.renderProxyCapacity,
        limits.primitiveWordCount * sizeof(uint32_t), scene.GetTriangleCount());
    framesSinceLoad_ = 0;
    maxVisibleCount_ = 0;
    benchMarker_->OnSceneStart(GetEngine().GetWindow().GetTime());
}

void MassiveBenchmarkGameInstance::OnTick(double)
{
    ++framesSinceLoad_;
    const Assets::GPUDrivenStat& stat = GetEngine().GetScene().GetGpuDrivenStat();
    maxVisibleCount_ = std::max(maxVisibleCount_, stat.VisibleCount);
    observedWideVisibility_ = observedWideVisibility_ || stat.VisibleCount > 65535;

    if (observedWideVisibility_ &&
        benchMarker_->OnTick(GetEngine().GetWindow().GetTime(), &GetEngine().GetRenderer()))
    {
        SPDLOG_INFO("[Massive] validated GPU VisibleCount={} (>65535)", maxVisibleCount_);
        benchMarker_->OnReport(&GetEngine().GetRenderer(), "MassiveAsteroidBelt.proc");
        GetEngine().RequestClose();
    }
    else if (framesSinceLoad_ > 600 && !observedWideVisibility_)
    {
        throw std::runtime_error(fmt::format(
            "Massive contract failed: GPU visible proxy count never crossed 65535 (last={})",
            stat.VisibleCount));
    }
}

bool MassiveBenchmarkGameInstance::OnRenderUI()
{
    DrawBenchmarkStatsOverlay(GetEngine());
    return true;
}
