#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
typedef SDL_Window Next_Window;
#include <vulkan/vulkan.h>
#if ANDROID
#include <vulkan/vulkan_android.h>
#endif
#undef APIENTRY

#include <fmt/format.h>
#include <string>
#include <vector>

#define DEFAULT_NON_COPIABLE(ClassName) \
 ClassName(const ClassName&) = delete; \
 ClassName(ClassName&&) = delete; \
 ClassName& operator = (const ClassName&) = delete; \
 ClassName& operator = (ClassName&&) = delete; \

#define VULKAN_NON_COPIABLE(ClassName) \
    ClassName(const ClassName&) = delete; \
    ClassName(ClassName&&) = delete; \
    ClassName& operator = (const ClassName&) = delete; \
    ClassName& operator = (ClassName&&) = delete; \
    static const char* StaticClass() {return #ClassName;} \
    virtual const char* GetActualClassName() const {return #ClassName;}

#define VULKAN_HANDLE(VulkanHandleType, name) \
public: \
    VulkanHandleType Handle() const { return name; } \
protected: \
    VulkanHandleType name{};

namespace Vulkan
{
    // ============================================================================
    // Core Vulkan Utilities
    // ============================================================================

    void Check(VkResult result, const char* operation);
    const char* ToString(VkResult result);

    // ============================================================================
    // Version
    // ============================================================================

    class Version final
    {
    public:

        explicit Version(const uint32_t version) :
            Major(VK_VERSION_MAJOR(version)),
            Minor(VK_VERSION_MINOR(version)),
            Patch(VK_VERSION_PATCH(version))
        {
        }

        Version(const uint32_t version, const uint32_t vendorId) :
            Major(VK_VERSION_MAJOR(version)),
            Minor(VK_VERSION_MINOR(version) >> (vendorId == 0x10DE ? 2 : 0)),
            Patch(VK_VERSION_PATCH(version) >> (vendorId == 0x10DE ? 4 : 0))
        {
            // NVIDIA specific driver versioning.
            // https://github.com/SaschaWillems/VulkanCapsViewer/blob/master/vulkanDeviceInfo.cpp
            // 10 bits = major version (up to 1023)
            // 8 bits = minor version (up to 255)
            // 8 bits = secondary branch version/build version (up to 255)
            // 6 bits = tertiary branch/build version (up to 63)
        }

        const unsigned Major;
        const unsigned Minor;
        const unsigned Patch;

        friend const std::string to_string(const Version& version)
        {
            return fmt::format("{}.{}.{}", version.Major, version.Minor, version.Patch);
        }
    };

    // ============================================================================
    // Strings
    // ============================================================================

    class Strings final
    {
    public:

        VULKAN_NON_COPIABLE(Strings)

        Strings() = delete;
        ~Strings() = delete;

        static const char* DeviceType(VkPhysicalDeviceType deviceType);
        static const char* VendorId(uint32_t vendorId);
    };

    // ============================================================================
    // Enumerate Templates
    // ============================================================================

    template <class TValue>
    inline void GetEnumerateVector(VkResult(enumerate) (uint32_t*, TValue*), std::vector<TValue>& vector)
    {
        uint32_t count = 0;
        Check(enumerate(&count, nullptr),
            "enumerate");

        vector.resize(count);
        Check(enumerate(&count, vector.data()),
            "enumerate");
    }

    template <class THandle, class TValue>
    inline void GetEnumerateVector(THandle handle, void(enumerate) (THandle, uint32_t*, TValue*), std::vector<TValue>& vector)
    {
        uint32_t count = 0;
        enumerate(handle, &count, nullptr);

        vector.resize(count);
        enumerate(handle, &count, vector.data());
    }

    template <class THandle, class TValue>
    inline void GetEnumerateVector(THandle handle, VkResult(enumerate) (THandle, uint32_t*, TValue*), std::vector<TValue>& vector)
    {
        uint32_t count = 0;
        Check(enumerate(handle, &count, nullptr),
            "enumerate");

        vector.resize(count);
        Check(enumerate(handle, &count, vector.data()),
            "enumerate");
    }

    template <class THandle1, class THandle2, class TValue>
    inline void GetEnumerateVector(THandle1 handle1, THandle2 handle2, VkResult(enumerate) (THandle1, THandle2, uint32_t*, TValue*), std::vector<TValue>& vector)
    {
        uint32_t count = 0;
        Check(enumerate(handle1, handle2, &count, nullptr),
            "enumerate");

        vector.resize(count);
        Check(enumerate(handle1, handle2, &count, vector.data()),
            "enumerate");
    }

    template <class TValue>
    inline std::vector<TValue> GetEnumerateVector(VkResult(enumerate) (uint32_t*, TValue*))
    {
        std::vector<TValue> initial;
        GetEnumerateVector(enumerate, initial);
        return initial;
    }

    template <class THandle, class TValue>
    inline std::vector<TValue> GetEnumerateVector(THandle handle, void(enumerate) (THandle, uint32_t*, TValue*))
    {
        std::vector<TValue> initial;
        GetEnumerateVector(handle, enumerate, initial);
        return initial;
    }

    template <class THandle, class TValue>
    inline std::vector<TValue> GetEnumerateVector(THandle handle, VkResult(enumerate) (THandle, uint32_t*, TValue*))
    {
        std::vector<TValue> initial;
        GetEnumerateVector(handle, enumerate, initial);
        return initial;
    }

    template <class THandle1, class THandle2, class TValue>
    inline std::vector<TValue> GetEnumerateVector(THandle1 handle1, THandle2 handle2, VkResult(enumerate) (THandle1, THandle2, uint32_t*, TValue*))
    {
        std::vector<TValue> initial;
        GetEnumerateVector(handle1, handle2, enumerate, initial);
        return initial;
    }

    // ============================================================================
    // DebugUtils
    // ============================================================================

    class DebugUtils final
    {
    public:

        VULKAN_NON_COPIABLE(DebugUtils)

        explicit DebugUtils(VkInstance instance);
        ~DebugUtils() = default;

        void SetDevice(VkDevice device)
        {
            device_ = device;
        }

        void SetObjectName(const VkAccelerationStructureKHR& object, const char* name) const { SetObjectName(object, name, VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR); }
        void SetObjectName(const VkBuffer& object, const char* name) const { SetObjectName(object, name, VK_OBJECT_TYPE_BUFFER); }
        void SetObjectName(const VkCommandBuffer& object, const char* name) const { SetObjectName(object, name, VK_OBJECT_TYPE_COMMAND_BUFFER); }
        void SetObjectName(const VkDescriptorSet& object, const char* name) const { SetObjectName(object, name, VK_OBJECT_TYPE_DESCRIPTOR_SET); }
        void SetObjectName(const VkDescriptorSetLayout& object, const char* name) const { SetObjectName(object, name, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT); }
        void SetObjectName(const VkDeviceMemory& object, const char* name) const { SetObjectName(object, name, VK_OBJECT_TYPE_DEVICE_MEMORY); }
        void SetObjectName(const VkFramebuffer& object, const char* name) const { SetObjectName(object, name, VK_OBJECT_TYPE_FRAMEBUFFER); }
        void SetObjectName(const VkImage& object, const char* name) const { SetObjectName(object, name, VK_OBJECT_TYPE_IMAGE); }
        void SetObjectName(const VkImageView& object, const char* name) const { SetObjectName(object, name, VK_OBJECT_TYPE_IMAGE_VIEW); }
        void SetObjectName(const VkPipeline& object, const char* name) const { SetObjectName(object, name, VK_OBJECT_TYPE_PIPELINE); }
        void SetObjectName(const VkQueue& object, const char* name) const { SetObjectName(object, name, VK_OBJECT_TYPE_QUEUE); }
        void SetObjectName(const VkRenderPass& object, const char* name) const { SetObjectName(object, name, VK_OBJECT_TYPE_RENDER_PASS); }
        void SetObjectName(const VkSemaphore& object, const char* name) const { SetObjectName(object, name, VK_OBJECT_TYPE_SEMAPHORE); }
        void SetObjectName(const VkShaderModule& object, const char* name) const { SetObjectName(object, name, VK_OBJECT_TYPE_SHADER_MODULE); }
        void SetObjectName(const VkSwapchainKHR& object, const char* name) const { SetObjectName(object, name, VK_OBJECT_TYPE_SWAPCHAIN_KHR); }
        void SetObjectName(const VkPipelineLayout& object, const char* name) const { SetObjectName(object, name, VK_OBJECT_TYPE_PIPELINE_LAYOUT); }

        void BeginMarker(VkCommandBuffer commandBuffer, const char* name) const;
        void EndMarker(VkCommandBuffer commandBuffer) const;

    private:

        template <typename T>
        void SetObjectName(const T& object, const char* name, VkObjectType type) const
        {
#if !ANDROID
            VkDebugUtilsObjectNameInfoEXT info = {};
            info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
            info.pNext = nullptr;
            info.objectHandle = reinterpret_cast<const uint64_t&>(object);
            info.objectType = type;
            info.pObjectName = name;

            Check(vkSetDebugUtilsObjectNameEXT_(device_, &info), "set object name");
#endif
        }

        const PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectNameEXT_;
        const PFN_vkCmdBeginDebugUtilsLabelEXT vkCmdBeginDebugUtilsLabelEXT_;
        const PFN_vkCmdEndDebugUtilsLabelEXT vkCmdEndDebugUtilsLabelEXT_;

        VkDevice device_{};
    };

    // ============================================================================
    // DebugUtilsMessenger
    // ============================================================================

    class Instance;

    class DebugUtilsMessenger final
    {
    public:

        VULKAN_NON_COPIABLE(DebugUtilsMessenger)

        DebugUtilsMessenger(const Instance& instance, VkDebugUtilsMessageSeverityFlagBitsEXT threshold);
        ~DebugUtilsMessenger();

        VkDebugUtilsMessageSeverityFlagBitsEXT Threshold() const { return threshold_; }

    private:

        const Instance& instance_;
        const VkDebugUtilsMessageSeverityFlagBitsEXT threshold_;

        VULKAN_HANDLE(VkDebugUtilsMessengerEXT, messenger_)
    };

}
