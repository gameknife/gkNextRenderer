#include "CommandExecution.hpp"
#include "Device.hpp"

namespace Vulkan
{

// ============================================================================
// CommandPool
// ============================================================================

CommandPool::CommandPool(const class Device& device, const uint32_t queueFamilyIndex, uint32_t queue, const bool allowReset) :
    device_(device)
{
    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamilyIndex;
    poolInfo.flags = allowReset ? VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT : 0;

    queue_ = queue == 0 ? device.GraphicsQueue() : device.TransferQueue();

    Check(vkCreateCommandPool(device.Handle(), &poolInfo, nullptr, &commandPool_),
        "create command pool");
}

CommandPool::~CommandPool()
{
    if (commandPool_ != nullptr)
    {
        vkDestroyCommandPool(device_.Handle(), commandPool_, nullptr);
        commandPool_ = nullptr;
    }
}

// ============================================================================
// CommandBuffers
// ============================================================================

CommandBuffers::CommandBuffers(CommandPool& commandPool, const uint32_t size) :
    commandPool_(commandPool)
{
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool.Handle();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = size;

    commandBuffers_.resize(size);

    Check(vkAllocateCommandBuffers(commandPool.Device().Handle(), &allocInfo, commandBuffers_.data()),
        "allocate command buffers");
}

CommandBuffers::~CommandBuffers()
{
    if (!commandBuffers_.empty())
    {
        vkFreeCommandBuffers(commandPool_.Device().Handle(), commandPool_.Handle(), static_cast<uint32_t>(commandBuffers_.size()), commandBuffers_.data());
        commandBuffers_.clear();
    }
}

VkCommandBuffer CommandBuffers::Begin(const size_t i)
{
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    beginInfo.pInheritanceInfo = nullptr; // Optional

    Check(vkBeginCommandBuffer(commandBuffers_[i], &beginInfo),
        "begin recording command buffer");

    return commandBuffers_[i];
}

void CommandBuffers::End(const size_t i)
{
    Check(vkEndCommandBuffer(commandBuffers_[i]),
        "record command buffer");
}

void SingleTimeCommands::Submit(CommandPool& commandPool, const std::function<void(VkCommandBuffer)>& action)
{
    std::lock_guard<std::mutex> submitLock(commandPool.mutex_);

    CommandBuffers commandBuffers(commandPool, 1);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    Check(vkBeginCommandBuffer(commandBuffers[0], &beginInfo),
        "begin single time command buffer");

    action(commandBuffers[0]);

    Check(vkEndCommandBuffer(commandBuffers[0]),
        "end single time command buffer");

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[0];

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    VkFence fence = nullptr;
    Check(vkCreateFence(commandPool.Device().Handle(), &fenceInfo, nullptr, &fence),
        "create single time command fence");

    try
    {
        Check(vkQueueSubmit(commandPool.Queue(), 1, &submitInfo, fence),
            "submit single time command buffer");
        Check(vkWaitForFences(commandPool.Device().Handle(), 1, &fence, VK_TRUE, UINT64_MAX),
            "wait single time command buffer");
    }
    catch (...)
    {
        if (fence != nullptr)
        {
            vkDestroyFence(commandPool.Device().Handle(), fence, nullptr);
        }
        throw;
    }

    vkDestroyFence(commandPool.Device().Handle(), fence, nullptr);
}

}
