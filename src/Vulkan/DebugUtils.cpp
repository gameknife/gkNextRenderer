#include "DebugUtils.hpp"
#include "Utilities/Exception.hpp"

namespace Vulkan {
	
DebugUtils::DebugUtils(VkInstance instance)
	: vkSetDebugUtilsObjectNameEXT_(reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(vkGetInstanceProcAddr(instance, "vkSetDebugUtilsObjectNameEXT")))
	, vkCmdBeginDebugUtilsLabelEXT_(reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(vkGetInstanceProcAddr(instance, "vkCmdBeginDebugUtilsLabelEXT")))
	, vkCmdEndDebugUtilsLabelEXT_(reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(vkGetInstanceProcAddr(instance, "vkCmdEndDebugUtilsLabelEXT")))
{
#if !ANDROID
	if (vkSetDebugUtilsObjectNameEXT_ == nullptr)
	{
		Throw(std::runtime_error("failed to get address of 'vkSetDebugUtilsObjectNameEXT'"));
	}
	if (vkCmdBeginDebugUtilsLabelEXT_ == nullptr)
	{
		Throw(std::runtime_error("failed to get address of 'vkCmdBeginDebugUtilsLabelEXT'"));
	}
	if (vkCmdEndDebugUtilsLabelEXT_ == nullptr)
	{
		Throw(std::runtime_error("failed to get address of 'vkCmdEndDebugUtilsLabelEXT'"));
	}
#endif
}

}