#pragma once

#include <cstdint>

namespace Vulkan
{
    // Rewrites this executable's import address table so that Vulkan entry points imported
    // from the Streamline interposer DLL resolve to the real Vulkan loader instead, and
    // returns how many entry points were redirected (0 when the interposer is not linked,
    // and on every non-Windows platform).
    //
    // sl.interposer's vkCreateDevice hook hard-requires VK_KHR_push_descriptor and
    // VK_KHR_timeline_semaphore and bails out with VK_ERROR_EXTENSION_NOT_PRESENT before it
    // even looks at the caller's create info, so any driver missing one of them (Mesa dozen
    // has no push descriptors) can never get a logical device through the interposer. The
    // interposer also forces the instance API version up to 1.3. Software / translation ICDs
    // therefore talk to the loader directly; Streamline is already force-disabled for them,
    // so nothing on the interposer path is lost.
    //
    // Must run before the first Vulkan call, and only redirects vk* imports: the D3D12/DXGI
    // proxies that come from the same DLL are left alone.
    uint32_t RedirectVulkanImportsToSystemLoader();
}
