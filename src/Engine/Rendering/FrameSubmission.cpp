#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Rendering/FrameSubmission.hpp"

#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Utilities/Exception.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Engine/Vulkan/SyncAndTiming.hpp"
#include "Engine/Vulkan/VulkanInterposer.hpp"

namespace Vulkan
{
    bool FrameSubmission::WaitAndAcquire(
        VulkanBaseRenderer& renderer,
        const uint64_t timeout,
        VkSemaphore& imageAvailable,
        VkSemaphore& renderFinished)
    {
        auto& frame = renderer.frame_;
        if (frame.currentFrame >= frame.imageAvailableSemaphores.size() ||
            frame.currentFrame >= frame.inFlightFences.size())
        {
            Throw(std::runtime_error("frame slot is outside swapchain synchronization storage"));
        }
        imageAvailable = frame.imageAvailableSemaphores[frame.currentFrame].Handle();

        auto* const previousSubmitFence = frame.currentFence;
        auto* const frameSlotFence = &frame.inFlightFences[frame.currentFrame];
        if (previousSubmitFence)
        {
            SCOPED_CPU_TIMER("fence");
            previousSubmitFence->Wait(timeout);
            frame.completedSubmitSerial = std::max(frame.completedSubmitSerial, frame.currentFenceSerial);
        }
        if (frameSlotFence != previousSubmitFence)
        {
            SCOPED_CPU_TIMER("frame-slot-fence");
            frameSlotFence->Wait(timeout);
            if (frame.currentFrame < frame.inFlightFenceSubmitSerials.size())
            {
                frame.completedSubmitSerial = std::max(
                    frame.completedSubmitSerial, frame.inFlightFenceSubmitSerials[frame.currentFrame]);
            }
        }
        frame.currentFence = frameSlotFence;

        VkResult result = VK_SUCCESS;
        {
            SCOPED_CPU_TIMER("acquire-frame");
            result = Interposer().AcquireNextImageKHR(
                renderer.ctx_.device->Handle(), frame.swapChain->Handle(), timeout,
                imageAvailable, nullptr, &frame.currentImageIndex);
        }
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        {
            renderer.RecreateSwapChain();
            return false;
        }
        if (result != VK_SUCCESS)
        {
            Throw(std::runtime_error(std::string("failed to acquire next image (") + ToString(result) + ")"));
        }
        if (frame.currentImageIndex >= frame.renderFinishedSemaphores.size() ||
            frame.currentImageIndex >= frame.swapChain->Images().size())
        {
            Throw(std::runtime_error("acquired image index is outside swapchain image storage"));
        }
        renderFinished = frame.renderFinishedSemaphores[frame.currentImageIndex].Handle();
        return true;
    }

    void FrameSubmission::Submit(
        VulkanBaseRenderer& renderer,
        const VkCommandBuffer commandBuffer,
        const VkSemaphore imageAvailable,
        const VkSemaphore renderFinished)
    {
        auto& frame = renderer.frame_;
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        std::vector<VkSemaphore> signalSemaphores;
        signalSemaphores.reserve(1 + frame.queuedSignalSemaphores.size());
        signalSemaphores.push_back(renderFinished);
        signalSemaphores.insert(signalSemaphores.end(), frame.queuedSignalSemaphores.begin(),
                                frame.queuedSignalSemaphores.end());

        VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &imageAvailable;
        submitInfo.pWaitDstStageMask = &waitStage;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size());
        submitInfo.pSignalSemaphores = signalSemaphores.data();

        std::vector<uint64_t> signalValues;
        VkTimelineSemaphoreSubmitInfo timelineInfo{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
        if (!frame.queuedSignalValues.empty())
        {
            signalValues.reserve(signalSemaphores.size());
            signalValues.push_back(0);
            signalValues.insert(signalValues.end(), frame.queuedSignalValues.begin(), frame.queuedSignalValues.end());
            timelineInfo.signalSemaphoreValueCount = static_cast<uint32_t>(signalValues.size());
            timelineInfo.pSignalSemaphoreValues = signalValues.data();
            submitInfo.pNext = &timelineInfo;
        }

        frame.currentFence = &frame.inFlightFences[frame.currentFrame];
        frame.currentFence->Reset();
        Check(vkQueueSubmit(renderer.ctx_.device->GraphicsQueue(), 1, &submitInfo, frame.currentFence->Handle()),
              "submit draw command buffer");
        frame.currentFenceSerial = frame.recordingSubmitSerial;
        frame.inFlightFenceSubmitSerials[frame.currentFrame] = frame.recordingSubmitSerial;
        frame.nextSubmitSerial = frame.recordingSubmitSerial + 1;
    }

    bool FrameSubmission::Present(VulkanBaseRenderer& renderer, const VkSemaphore renderFinished)
    {
        auto& frame = renderer.frame_;
        VkSwapchainKHR swapchain = frame.swapChain->Handle();
        VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderFinished;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchain;
        presentInfo.pImageIndices = &frame.currentImageIndex;

        const VkResult result = Interposer().QueuePresentKHR(
            renderer.ctx_.device->PresentQueue(), &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        {
            renderer.RecreateSwapChain();
            return false;
        }
        if (result != VK_SUCCESS)
        {
            Throw(std::runtime_error(std::string("failed to present next image (") + ToString(result) + ")"));
        }
        return true;
    }
}
