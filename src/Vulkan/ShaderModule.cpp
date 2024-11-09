#include "ShaderModule.hpp"
#include "Device.hpp"
#include "Utilities/Exception.hpp"
#include <fstream>

#include "Utilities/Console.hpp"
#include "Utilities/FileHelper.hpp"

namespace Vulkan {

ShaderModule::ShaderModule(const class Device& device, const std::string& filename) :
	ShaderModule(device, ReadFile(filename))
{
}

ShaderModule::ShaderModule(const class Device& device, const std::vector<uint8_t>& code) :
	device_(device)
{
	VkShaderModuleCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = code.size();
	createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

	Check(vkCreateShaderModule(device.Handle(), &createInfo, nullptr, &shaderModule_),
		"create shader module");
}

ShaderModule::~ShaderModule()
{
	if (shaderModule_ != nullptr)
	{
		vkDestroyShaderModule(device_.Handle(), shaderModule_, nullptr);
		shaderModule_ = nullptr;
	}
}

VkPipelineShaderStageCreateInfo ShaderModule::CreateShaderStage(VkShaderStageFlagBits stage) const
{
	VkPipelineShaderStageCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	createInfo.stage = stage;
	createInfo.module = shaderModule_;
	createInfo.pName = "main";

	return createInfo;
}

std::vector<uint8_t> ShaderModule::ReadFile(const std::string& filename)
{
	std::vector<uint8_t> buffer;
	Utilities::Package::FPackageFileSystem::GetInstance().LoadFile(filename, buffer);
	return buffer;
}

}
