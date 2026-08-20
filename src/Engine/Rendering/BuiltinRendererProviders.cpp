#include "Engine/Rendering/BuiltinRendererProviders.hpp"

#include "Engine/Rendering/Interface/AtmosphereSubsystem.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Rendering/Atmosphere/AtmosphereSubsystem.hpp"
#include "Engine/Rendering/PathTracing/PathTracingLiteRenderer.hpp"
#include "Engine/Rendering/PathTracing/PathTracingRenderer.hpp"
#include "Engine/Rendering/SoftwareModern/SoftwareModernNoAmbientRenderer.hpp"
#include "Engine/Rendering/SoftwareModern/SoftwareModernRenderer.hpp"
#include "Engine/Rendering/SoftwareTracing/SoftwareTracingRenderer.hpp"
#include "Engine/Rendering/VoxelTracing/VoxelTracingRenderer.hpp"

namespace
{
    template <typename T>
    std::unique_ptr<Vulkan::LogicRendererBase> CreateLogicRenderer(
        Vulkan::VulkanBaseRenderer& renderer)
    {
        return std::make_unique<T>(renderer);
    }

    std::unique_ptr<Rendering::Atmosphere::IAtmosphereSubsystem> CreateAtmosphere(
        Vulkan::VulkanBaseRenderer& renderer)
    {
        return std::make_unique<Rendering::Atmosphere::AtmosphereSubsystem>(renderer);
    }
}

namespace Vulkan
{
    void RegisterBuiltinRendererProviders()
    {
        RegisterLogicRendererFactory(
            ERT_PathTracing, &CreateLogicRenderer<PathTracing::PathTracingRenderer>);
        RegisterLogicRendererFactory(
            ERT_PathTracingLite, &CreateLogicRenderer<PathTracing::PathTracingLiteRenderer>);
        RegisterLogicRendererFactory(
            ERT_SoftwareTracing, &CreateLogicRenderer<SoftwareTracing::SoftwareTracingRenderer>);
        RegisterLogicRendererFactory(
            ERT_SoftwareModern, &CreateLogicRenderer<SoftwareModern::SoftwareModernRenderer>);
        RegisterLogicRendererFactory(
            ERT_VoxelTracing, &CreateLogicRenderer<VoxelTracing::VoxelTracingRenderer>);
        RegisterLogicRendererFactory(
            ERT_SoftwareModernNoAmbient,
            &CreateLogicRenderer<SoftwareModernNoAmbient::SoftwareModernNoAmbientRenderer>);
        Rendering::Atmosphere::RegisterAtmosphereSubsystemFactory(&CreateAtmosphere);
    }
}
