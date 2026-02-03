#include "MemoryAndShader.hpp"
#include "Device.hpp"
#include "Utilities/Exception.hpp"
#include "Utilities/FileHelper.hpp"

#ifdef VK_USE_PLATFORM_WIN32_KHR
#	include <aclapi.h>
#	include <dxgi1_2.h>
#endif

#if WIN32 && !defined(__MINGW32__)
// On Windows, we need to enable some security settings to allow api interop
// The spec states: For handles of the following types: VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT The implementation must ensure the access rights allow read and write access to the memory.
// This class sets up the structures required for tis
class WinSecurityAttributes
{
  private:
	SECURITY_ATTRIBUTES  security_attributes;
	PSECURITY_DESCRIPTOR security_descriptor;

  public:
	WinSecurityAttributes();
	~WinSecurityAttributes();
	SECURITY_ATTRIBUTES *operator&();
};

WinSecurityAttributes::WinSecurityAttributes()
{
	security_descriptor = (PSECURITY_DESCRIPTOR) calloc(1, SECURITY_DESCRIPTOR_MIN_LENGTH + 2 * sizeof(void **));

	PSID *ppSID = (PSID *) ((PBYTE) security_descriptor + SECURITY_DESCRIPTOR_MIN_LENGTH);
	PACL *ppACL = (PACL *) ((PBYTE) ppSID + sizeof(PSID *));

	InitializeSecurityDescriptor(security_descriptor, SECURITY_DESCRIPTOR_REVISION);

	SID_IDENTIFIER_AUTHORITY sid_identifier_authority = SECURITY_WORLD_SID_AUTHORITY;
	AllocateAndInitializeSid(&sid_identifier_authority, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, ppSID);

	EXPLICIT_ACCESS explicit_access{};
	ZeroMemory(&explicit_access, sizeof(EXPLICIT_ACCESS));
	explicit_access.grfAccessPermissions = STANDARD_RIGHTS_ALL | SPECIFIC_RIGHTS_ALL;
	explicit_access.grfAccessMode        = SET_ACCESS;
	explicit_access.grfInheritance       = INHERIT_ONLY;
	explicit_access.Trustee.TrusteeForm  = TRUSTEE_IS_SID;
	explicit_access.Trustee.TrusteeType  = TRUSTEE_IS_WELL_KNOWN_GROUP;
	explicit_access.Trustee.ptstrName    = (LPTSTR) *ppSID;
	SetEntriesInAcl(1, &explicit_access, nullptr, ppACL);

	SetSecurityDescriptorDacl(security_descriptor, TRUE, *ppACL, FALSE);

	security_attributes.nLength              = sizeof(SECURITY_ATTRIBUTES);
	security_attributes.lpSecurityDescriptor = security_descriptor;
	security_attributes.bInheritHandle       = TRUE;
}

SECURITY_ATTRIBUTES *WinSecurityAttributes::operator&()
{
	return &security_attributes;
}

WinSecurityAttributes::~WinSecurityAttributes()
{
	PSID *ppSID = (PSID *) ((PBYTE) security_descriptor + SECURITY_DESCRIPTOR_MIN_LENGTH);
	PACL *ppACL = (PACL *) ((PBYTE) ppSID + sizeof(PSID *));

	if (*ppSID)
	{
		FreeSid(*ppSID);
	}

	if (*ppACL)
	{
		LocalFree(*ppACL);
	}

	free(security_descriptor);
}
#endif

namespace Vulkan {

// ============================================================================
// DeviceMemory
// ============================================================================

DeviceMemory::DeviceMemory(
	const class Device& device,
	const size_t size,
	const uint32_t memoryTypeBits,
	const VkMemoryAllocateFlags allocateFLags,
	const VkMemoryPropertyFlags propertyFlags,
	bool external) :
	device_(device)
{
	VkMemoryAllocateFlagsInfo flagsInfo = {};
	flagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
	flagsInfo.pNext = nullptr;
	flagsInfo.flags = allocateFLags;

	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.pNext = &flagsInfo;
	allocInfo.allocationSize = size;
	allocInfo.memoryTypeIndex = FindMemoryType(memoryTypeBits, propertyFlags);

	VkExportMemoryAllocateInfoKHR exportMemoryAllocateInfo{};
	exportMemoryAllocateInfo.sType       = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO_KHR;
#if WIN32 && !defined(__MINGW32__)
	exportMemoryAllocateInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR;
#else
	exportMemoryAllocateInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif
#if WIN32 && !defined(__MINGW32__)
	WinSecurityAttributes            win_security_attributes;
	VkExportMemoryWin32HandleInfoKHR export_memory_win32_handle_info{};
	export_memory_win32_handle_info.sType       = VK_STRUCTURE_TYPE_EXPORT_MEMORY_WIN32_HANDLE_INFO_KHR;
	export_memory_win32_handle_info.pAttributes = &win_security_attributes;
	export_memory_win32_handle_info.dwAccess    = DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE;
#endif

#if !ANDROID
	if(external)
	{
#if WIN32 && !defined(__MINGW32__)
		exportMemoryAllocateInfo.pNext = &export_memory_win32_handle_info;
#endif
		allocInfo.pNext = &exportMemoryAllocateInfo;
	}
#endif

	Check(vkAllocateMemory(device.Handle(), &allocInfo, nullptr, &memory_),
		"allocate memory");
}

DeviceMemory::DeviceMemory(DeviceMemory&& other) noexcept :
	device_(other.device_),
	memory_(other.memory_)
{
	other.memory_ = nullptr;
}

DeviceMemory::~DeviceMemory()
{
	if (memory_ != nullptr)
	{
		vkFreeMemory(device_.Handle(), memory_, nullptr);
		memory_ = nullptr;
	}
}

void* DeviceMemory::Map(const size_t offset, const size_t size)
{
	void* data;
	Check(vkMapMemory(device_.Handle(), memory_, offset, size, 0, &data),
		"map memory");

	return data;
}

void DeviceMemory::Unmap()
{
	vkUnmapMemory(device_.Handle(), memory_);
}

uint32_t DeviceMemory::FindMemoryType(const uint32_t typeFilter, const VkMemoryPropertyFlags propertyFlags) const
{
	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(device_.PhysicalDevice(), &memProperties);

	for (uint32_t i = 0; i != memProperties.memoryTypeCount; ++i)
	{
		if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & propertyFlags) == propertyFlags)
		{
			return i;
		}
	}

	Throw(std::runtime_error("failed to find suitable memory type"));
}

// ============================================================================
// Sampler
// ============================================================================

Sampler::Sampler(const class Device& device, const SamplerConfig& config) :
	device_(device)
{
	VkSamplerCreateInfo samplerInfo = {};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = config.MagFilter;
	samplerInfo.minFilter = config.MinFilter;
	samplerInfo.addressModeU = config.AddressModeU;
	samplerInfo.addressModeV = config.AddressModeV;
	samplerInfo.addressModeW = config.AddressModeW;
	samplerInfo.anisotropyEnable = config.AnisotropyEnable;
	samplerInfo.maxAnisotropy = config.MaxAnisotropy;
	samplerInfo.borderColor = config.BorderColor;
	samplerInfo.unnormalizedCoordinates = config.UnnormalizedCoordinates;
	samplerInfo.compareEnable = config.CompareEnable;
	samplerInfo.compareOp = config.CompareOp;
	samplerInfo.mipmapMode = config.MipmapMode;
	samplerInfo.mipLodBias = config.MipLodBias;
	samplerInfo.minLod = config.MinLod;
	samplerInfo.maxLod = config.MaxLod;

	if (vkCreateSampler(device.Handle(), &samplerInfo, nullptr, &sampler_) != VK_SUCCESS)
	{
		Throw(std::runtime_error("failed to create sampler"));
	}
}

Sampler::~Sampler()
{
	if (sampler_ != nullptr)
	{
		vkDestroySampler(device_.Handle(), sampler_, nullptr);
		sampler_ = nullptr;
	}
}

// ============================================================================
// ShaderModule
// ============================================================================

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
