#include "Engine/Common/CoreMinimal.hpp"

#include "Engine/Vulkan/VulkanLoaderBypass.hpp"

#if WIN32
#include <windows.h>
#endif

namespace Vulkan
{

#if WIN32

namespace
{
    constexpr const char* kInterposerModuleName = "sl.interposer.dll";
    constexpr const char* kVulkanLoaderModuleName = "vulkan-1.dll";

    HMODULE SystemVulkanLoader()
    {
        // The interposer maps the loader under the same name, so this usually just bumps the
        // reference count of an already resident module.
        const HMODULE loader = ::LoadLibraryA(kVulkanLoaderModuleName);
        if (loader == nullptr)
        {
            SPDLOG_WARN("Vulkan loader bypass: failed to load {}", kVulkanLoaderModuleName);
            return nullptr;
        }
        if (loader == ::GetModuleHandleA(kInterposerModuleName))
        {
            // A deployment that renamed the interposer to vulkan-1.dll would turn the
            // redirection into a self-reference.
            SPDLOG_WARN("Vulkan loader bypass: {} resolves to the Streamline interposer",
                        kVulkanLoaderModuleName);
            return nullptr;
        }
        return loader;
    }

    bool WriteImportSlot(void** slot, void* value)
    {
        DWORD previousProtection = 0;
        if (!::VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &previousProtection))
        {
            return false;
        }
        *slot = value;
        DWORD restoredProtection = 0;
        ::VirtualProtect(slot, sizeof(void*), previousProtection, &restoredProtection);
        return true;
    }
}

uint32_t RedirectVulkanImportsToSystemLoader()
{
    const HMODULE loader = SystemVulkanLoader();
    if (loader == nullptr)
    {
        return 0;
    }

    auto* const base = reinterpret_cast<uint8_t*>(::GetModuleHandleW(nullptr));
    const auto* dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE)
    {
        return 0;
    }

    const auto* ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE)
    {
        return 0;
    }

    const IMAGE_DATA_DIRECTORY& importDirectory =
        ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (importDirectory.VirtualAddress == 0)
    {
        return 0;
    }

    uint32_t redirected = 0;
    uint32_t unresolved = 0;
    for (const auto* descriptor =
             reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + importDirectory.VirtualAddress);
         descriptor->Name != 0; ++descriptor)
    {
        const char* moduleName = reinterpret_cast<const char*>(base + descriptor->Name);
        if (::_stricmp(moduleName, kInterposerModuleName) != 0)
        {
            continue;
        }

        // Bound imports leave OriginalFirstThunk empty; the IAT then still holds the names.
        const DWORD nameTableRva = descriptor->OriginalFirstThunk != 0
            ? descriptor->OriginalFirstThunk
            : descriptor->FirstThunk;
        const auto* nameThunk = reinterpret_cast<const IMAGE_THUNK_DATA*>(base + nameTableRva);
        auto* addressThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);

        for (; nameThunk->u1.AddressOfData != 0; ++nameThunk, ++addressThunk)
        {
            if (IMAGE_SNAP_BY_ORDINAL(nameThunk->u1.Ordinal))
            {
                continue;
            }

            const auto* importByName =
                reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + nameThunk->u1.AddressOfData);
            const char* functionName = importByName->Name;
            if (functionName[0] != 'v' || functionName[1] != 'k')
            {
                continue;
            }

            auto* replacement = reinterpret_cast<void*>(::GetProcAddress(loader, functionName));
            if (replacement == nullptr)
            {
                // Entry points the loader does not export stay on the interposer, which
                // forwards them to the same loader through vkGetDeviceProcAddr.
                ++unresolved;
                continue;
            }

            if (WriteImportSlot(reinterpret_cast<void**>(&addressThunk->u1.Function), replacement))
            {
                ++redirected;
            }
        }
    }

    if (redirected != 0)
    {
        SPDLOG_INFO("Vulkan loader bypass: redirected {} entry points from {} to {} ({} left in place)",
                    redirected, kInterposerModuleName, kVulkanLoaderModuleName, unresolved);
    }

    return redirected;
}

#else

uint32_t RedirectVulkanImportsToSystemLoader()
{
    return 0;
}

#endif

}
