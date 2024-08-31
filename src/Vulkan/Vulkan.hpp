#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#if !ANDROID
#include <GLFW/glfw3.h>
#else
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>
#include <android/native_window.h>
typedef ANativeWindow GLFWwindow;
#endif
#undef APIENTRY

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
	static const char* StaticClass() {return #ClassName;}

#define VULKAN_HANDLE(VulkanHandleType, name) \
public: \
	VulkanHandleType Handle() const { return name; } \
private: \
	VulkanHandleType name{};

namespace Vulkan
{
	void Check(VkResult result, const char* operation);
	const char* ToString(VkResult result);
}
