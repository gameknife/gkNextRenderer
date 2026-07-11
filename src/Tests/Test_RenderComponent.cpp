#include <catch2/catch_all.hpp>
#include "Engine/Assets/Core/Node.h"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Rendering/PipelineCommon/ResourceStateTracker.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/Components/RenderComponent.h"
#include <memory>
#include <array>

TEST_CASE("RenderComponent Usage", "[Unit][RenderComponent]") {
    auto node = Assets::Node::CreateNode("RenderNode", glm::vec3(0), glm::quat(1,0,0,0), glm::vec3(1), 0);
    
    SECTION("Basic Properties") {
        auto renderComp = std::make_shared<Runtime::RenderComponent>();
        
        renderComp->SetModelId(123);
        renderComp->SetVisible(true);
        renderComp->SetRayCastVisible(false);
        
        std::array<uint32_t, 16> mats;
        mats.fill(7);
        renderComp->SetMaterials(mats);
        
        node->AddComponent(renderComp);
        
        auto retrieved = node->GetComponent<Runtime::RenderComponent>();
        REQUIRE(retrieved != nullptr);
        CHECK(retrieved->GetModelId() == 123);
        CHECK(retrieved->GetVisible() == true);
        CHECK(retrieved->GetRayCastVisible() == false);
        CHECK(retrieved->GetMaterials()[0] == 7);
    }

    SECTION("Render participation mask") {
        Runtime::RenderComponent renderComp;
        CHECK(renderComp.GetRenderParticipationMask() == Runtime::RenderParticipation::defaultMask);

        renderComp.SetMainVisible(false);
        CHECK((renderComp.GetRenderParticipationMask() & Runtime::RenderParticipation::mainVisibility) == 0u);
        CHECK((renderComp.GetRenderParticipationMask() & Runtime::RenderParticipation::shadowCaster) != 0u);
        CHECK((renderComp.GetRenderParticipationMask() & Runtime::RenderParticipation::gpuAs) != 0u);
        CHECK((renderComp.GetRenderParticipationMask() & Runtime::RenderParticipation::giBake) != 0u);

        renderComp.SetVisible(false);
        CHECK(renderComp.GetRenderParticipationMask() == Runtime::RenderParticipation::none);
    }
}

TEST_CASE("Scene model section encoding is bounds checked", "[Unit][Scene]")
{
    uint32_t encoded = 0;
    REQUIRE(Assets::Scene::TryEncodeModelSection(42, 7, encoded));
    CHECK(encoded == 427);
    CHECK(Assets::Scene::DecodeModelIndex(encoded) == 42);

    CHECK_FALSE(Assets::Scene::TryEncodeModelSection(42, Assets::Scene::kModelSectionStride, encoded));
    CHECK_FALSE(Assets::Scene::TryEncodeModelSection(std::numeric_limits<uint32_t>::max(), 0, encoded));
}

TEST_CASE("Image state tracker preserves and discards contents explicitly", "[Unit][Rendering][ResourceState]")
{
    using namespace Vulkan::PipelineCommon;
    FResourceStateTracker tracker;
    constexpr FImageHandle image{1};

    auto first = tracker.Use({.image = image, .stages = ERenderStage::Compute,
                              .access = EResourceAccess::ShaderWrite,
                              .layout = VK_IMAGE_LAYOUT_GENERAL}, "shade");
    REQUIRE(first);
    CHECK(first->oldLayout == VK_IMAGE_LAYOUT_UNDEFINED);
    CHECK(first->srcAccess == 0);

    auto copy = tracker.Use({.image = image, .stages = ERenderStage::Transfer,
                             .access = EResourceAccess::TransferRead,
                             .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL}, "copy history");
    REQUIRE(copy);
    CHECK(copy->oldLayout == VK_IMAGE_LAYOUT_GENERAL);
    CHECK((copy->srcAccess & VK_ACCESS_SHADER_WRITE_BIT) != 0);
    CHECK((copy->dstAccess & VK_ACCESS_TRANSFER_READ_BIT) != 0);

    auto steadyRead = tracker.Use({.image = image, .stages = ERenderStage::Compute,
                                   .access = EResourceAccess::ShaderRead,
                                   .layout = VK_IMAGE_LAYOUT_GENERAL}, "read history");
    REQUIRE(steadyRead);
    CHECK(steadyRead->oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    auto discard = tracker.Use({.image = image, .stages = ERenderStage::Transfer,
                                .access = EResourceAccess::TransferWrite,
                                .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                .discardPreviousContents = true}, "clear");
    REQUIRE(discard);
    CHECK(discard->oldLayout == VK_IMAGE_LAYOUT_UNDEFINED);
    CHECK(discard->srcAccess == 0);
    REQUIRE(tracker.Find(image));
    CHECK(tracker.Find(image)->lastPass == "clear");
    CHECK((FResourceStateTracker::ToVkStages(ERenderStage::Fragment) &
           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT) != 0);
}

TEST_CASE("Renderer contracts describe prepasses outputs and history", "[Unit][Rendering][RendererContract]")
{
    using namespace Vulkan;

    const auto& path = GetRendererContract(ERT_PathTracing);
    CHECK(HasAny(path.sceneResources, ESceneResource::TLAS));
    CHECK(HasAny(path.prepasses, EViewPrepass::Visibility));
    CHECK_FALSE(HasAny(path.prepasses, EViewPrepass::CSM));
    CHECK(HasAll(path.history, EHistoryChannel::Diffuse | EHistoryChannel::Specular |
                               EHistoryChannel::Albedo | EHistoryChannel::ObjectId));

    const auto& voxel = GetRendererContract(ERT_VoxelTracing);
    CHECK(voxel.prepasses == EViewPrepass::None);
    CHECK(voxel.outputs == ERenderOutput::Color);
    CHECK(voxel.history == EHistoryChannel::None);
    CHECK_FALSE(HasAny(voxel.post, EPostProcess::Temporal | EPostProcess::DLSS |
                                  EPostProcess::DebugGBuffer));

    const auto& noAmbient = GetRendererContract(ERT_SoftwareModernNoAmbient);
    CHECK(noAmbient.sceneResources == ESceneResource::None);
    CHECK(HasAny(noAmbient.outputs, ERenderOutput::Depth | ERenderOutput::Motion |
                                    ERenderOutput::ObjectId | ERenderOutput::Normal));
    CHECK(noAmbient.history == EHistoryChannel::None);
}

TEST_CASE("RenderView history invalidation records generation and reason", "[Unit][Rendering][History]")
{
    Vulkan::FViewDesc desc{};
    desc.renderExtent = {640, 360};
    Vulkan::RenderView view(0, desc, "history test");
    const uint64_t initialGeneration = view.State().historyGeneration;

    view.InvalidateTemporalHistory(Vulkan::EHistoryInvalidationReason::RendererChanged);

    CHECK(view.State().resetHistory);
    CHECK(view.State().historyGeneration == initialGeneration + 1);
    CHECK(view.State().historyInvalidationReason == Vulkan::EHistoryInvalidationReason::RendererChanged);
    CHECK(std::string_view(Vulkan::GetHistoryInvalidationReasonName(
              view.State().historyInvalidationReason)) == "renderer-changed");
}
