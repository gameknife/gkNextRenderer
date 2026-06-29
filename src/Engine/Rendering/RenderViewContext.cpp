#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Rendering/RenderViewContext.hpp"

#include "Engine/Rendering/VulkanBaseRenderer.hpp"

namespace Vulkan
{
    FActiveRenderViewScope::FActiveRenderViewScope(VulkanBaseRenderer& renderer, RenderView& view)
        : renderer_(renderer)
        , previousBankBase_(renderer.activeViewBankBase_)
        , previousRenderExtent_(renderer.activeViewRenderExtent_)
        , previousCameraAddress_(renderer.activeViewCameraAddress_)
        , previousVisibilityFrameBuffer_(renderer.activeVisibilityFrameBuffer_)
        , previousSceneOverride_(renderer.activeSceneOverride_)
        , previousRenderView_(renderer.activeRenderView_)
    {
        renderer_.activeSceneOverride_ = view.SceneOverride();
        renderer_.activeRenderView_ = &view;
        renderer_.SetActiveViewBankBase(view.RtBankBase());
        renderer_.SetActiveViewRenderExtent(view.RenderExtent());
        renderer_.SetActiveViewCameraAddress(view.CameraAddress());
        renderer_.activeVisibilityFrameBuffer_ = view.VisibilityFramebuffer();
    }

    FActiveRenderViewScope::~FActiveRenderViewScope()
    {
        renderer_.activeSceneOverride_ = previousSceneOverride_;
        renderer_.activeRenderView_ = previousRenderView_;
        renderer_.activeVisibilityFrameBuffer_ = previousVisibilityFrameBuffer_;
        renderer_.SetActiveViewCameraAddress(previousCameraAddress_);
        renderer_.SetActiveViewRenderExtent(previousRenderExtent_);
        renderer_.SetActiveViewBankBase(previousBankBase_);
    }
}
