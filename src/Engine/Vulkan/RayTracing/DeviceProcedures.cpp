#include "Engine/Vulkan/RayTracing/DeviceProcedures.hpp"
#include "Engine/Vulkan/Device.hpp"

namespace Vulkan
{

namespace
{
	template <class Func>
	Func GetProcedure(const Device& device, const char* const name)
	{
		const auto func = reinterpret_cast<Func>(vkGetDeviceProcAddr(device.Handle(), name));
		if (func == nullptr)
		{
			// dont throw
			//Throw(std::runtime_error(std::string("failed to get address of '") + name + "'"));
		}

		return func;
	}
}


DeviceProcedures::DeviceProcedures(const class Device& device, bool raytracing, bool rayquery) :
	vkCreateAccelerationStructureKHR(GetProcedure<PFN_vkCreateAccelerationStructureKHR>(device, "vkCreateAccelerationStructureKHR")),
	vkDestroyAccelerationStructureKHR(GetProcedure<PFN_vkDestroyAccelerationStructureKHR>(device, "vkDestroyAccelerationStructureKHR")),
	vkGetAccelerationStructureBuildSizesKHR(GetProcedure<PFN_vkGetAccelerationStructureBuildSizesKHR>(device, "vkGetAccelerationStructureBuildSizesKHR")),
	vkCmdBuildAccelerationStructuresKHR(GetProcedure<PFN_vkCmdBuildAccelerationStructuresKHR>(device, "vkCmdBuildAccelerationStructuresKHR")),
	vkCmdCopyAccelerationStructureKHR(GetProcedure<PFN_vkCmdCopyAccelerationStructureKHR>(device, "vkCmdCopyAccelerationStructureKHR")),
	vkCmdTraceRaysKHR(raytracing ? GetProcedure<PFN_vkCmdTraceRaysKHR>(device, "vkCmdTraceRaysKHR") : nullptr),
	vkCreateRayTracingPipelinesKHR(raytracing ? GetProcedure<PFN_vkCreateRayTracingPipelinesKHR>(device, "vkCreateRayTracingPipelinesKHR"): nullptr),
	vkGetRayTracingShaderGroupHandlesKHR(raytracing ? GetProcedure<PFN_vkGetRayTracingShaderGroupHandlesKHR>(device, "vkGetRayTracingShaderGroupHandlesKHR"): nullptr),
	vkGetAccelerationStructureDeviceAddressKHR(GetProcedure<PFN_vkGetAccelerationStructureDeviceAddressKHR>(device, "vkGetAccelerationStructureDeviceAddressKHR")),
	vkCmdWriteAccelerationStructuresPropertiesKHR(GetProcedure<PFN_vkCmdWriteAccelerationStructuresPropertiesKHR>(device, "vkCmdWriteAccelerationStructuresPropertiesKHR")),
#if WIN32
	vkGetMemoryWin32HandleKHR(GetProcedure<PFN_vkGetMemoryWin32HandleKHR>(device, "vkGetMemoryWin32HandleKHR")),
#else
	vkGetMemoryFdKHR(GetProcedure<PFN_vkGetMemoryFdKHR>(device, "vkGetMemoryFdKHR")),
#endif
	vkCreateVideoSessionKHR(GetProcedure<PFN_vkCreateVideoSessionKHR>(device, "vkCreateVideoSessionKHR")),
	vkDestroyVideoSessionKHR(GetProcedure<PFN_vkDestroyVideoSessionKHR>(device, "vkDestroyVideoSessionKHR")),
	vkGetVideoSessionMemoryRequirementsKHR(GetProcedure<PFN_vkGetVideoSessionMemoryRequirementsKHR>(device, "vkGetVideoSessionMemoryRequirementsKHR")),
	vkBindVideoSessionMemoryKHR(GetProcedure<PFN_vkBindVideoSessionMemoryKHR>(device, "vkBindVideoSessionMemoryKHR")),
	vkCreateVideoSessionParametersKHR(GetProcedure<PFN_vkCreateVideoSessionParametersKHR>(device, "vkCreateVideoSessionParametersKHR")),
	vkDestroyVideoSessionParametersKHR(GetProcedure<PFN_vkDestroyVideoSessionParametersKHR>(device, "vkDestroyVideoSessionParametersKHR")),
	vkGetEncodedVideoSessionParametersKHR(GetProcedure<PFN_vkGetEncodedVideoSessionParametersKHR>(device, "vkGetEncodedVideoSessionParametersKHR")),
	vkCmdBeginVideoCodingKHR(GetProcedure<PFN_vkCmdBeginVideoCodingKHR>(device, "vkCmdBeginVideoCodingKHR")),
	vkCmdEndVideoCodingKHR(GetProcedure<PFN_vkCmdEndVideoCodingKHR>(device, "vkCmdEndVideoCodingKHR")),
	vkCmdControlVideoCodingKHR(GetProcedure<PFN_vkCmdControlVideoCodingKHR>(device, "vkCmdControlVideoCodingKHR")),
	vkCmdEncodeVideoKHR(GetProcedure<PFN_vkCmdEncodeVideoKHR>(device, "vkCmdEncodeVideoKHR")),
	device_(device)
{
}

DeviceProcedures::~DeviceProcedures()
{
}

}
