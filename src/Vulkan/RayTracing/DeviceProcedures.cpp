#include "DeviceProcedures.hpp"
#include "Utilities/Exception.hpp"
#include "Vulkan/Device.hpp"
#include <string>

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
#if WIN32 && !defined(__MINGW32__)
	vkGetMemoryWin32HandleKHR(GetProcedure<PFN_vkGetMemoryWin32HandleKHR>(device, "vkGetMemoryWin32HandleKHR")),
#endif
	device_(device)
{
}

DeviceProcedures::~DeviceProcedures()
{
}

}
