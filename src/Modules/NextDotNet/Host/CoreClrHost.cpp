#include "Modules/NextDotNet/Host/IManagedHost.hpp"

#if !GK_DOTNET_USE_AOT

// CoreCLR backend: locate hostfxr, spin up a runtime from GkNext.Bootstrap.runtimeconfig.json, and
// resolve the single [UnmanagedCallersOnly] entry point. Everything below exists only to obtain
// that one function pointer; the NativeAOT host gets the same pointer from the linker.
//
// Kept free of engine includes so the Phase 0 probe links it standalone.

// hostfxr is located by hand rather than through libnethost. libnethost ships compiled against the
// static CRT (/MT) and cannot be linked into a /MD engine build, and the dynamic alternative would
// mean shipping nethost.dll purely to read a path we already know: the .NET root is either the one
// gnb installed under external/dotnet or a machine-wide install in a well-known place.
#include <coreclr_delegates.h>
#include <hostfxr.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Modules::NextDotNet
{
    namespace
    {
#if defined(_WIN32)
        using FHostString = std::wstring;

        FHostString ToHostString(const std::string& value)
        {
            if (value.empty())
            {
                return FHostString();
            }
            const int required = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
            FHostString result(static_cast<size_t>(required), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), required);
            return result;
        }

        std::string FromHostString(const char_t* value)
        {
            if (value == nullptr)
            {
                return std::string();
            }
            const int required = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
            if (required <= 1)
            {
                return std::string();
            }
            std::string result(static_cast<size_t>(required - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), required, nullptr, nullptr);
            return result;
        }

        using FLibraryHandle = HMODULE;

        FLibraryHandle LoadHostLibrary(const FHostString& path)
        {
            return LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        }

        void* GetHostExport(FLibraryHandle handle, const char* name)
        {
            return reinterpret_cast<void*>(GetProcAddress(handle, name));
        }
#else
        using FHostString = std::string;

        FHostString ToHostString(const std::string& value) { return value; }
        std::string FromHostString(const char_t* value) { return value != nullptr ? std::string(value) : std::string(); }

        using FLibraryHandle = void*;

        FLibraryHandle LoadHostLibrary(const FHostString& path)
        {
            return dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
        }

        void* GetHostExport(FLibraryHandle handle, const char* name)
        {
            return dlsym(handle, name);
        }
#endif

        // hostfxr reports resolution failures through a writer callback; without it a missing
        // runtime surfaces only as an opaque error code.
        std::string GLastHostfxrError;

        void HOSTFXR_CALLTYPE OnHostfxrError(const char_t* message)
        {
            const std::string text = FromHostString(message);
            if (text.empty())
            {
                return;
            }
            if (!GLastHostfxrError.empty())
            {
                GLastHostfxrError += "\n";
            }
            GLastHostfxrError += text;
        }

        /// Candidate .NET installation roots, most specific first: what the caller configured,
        /// then the standard environment override, then the platform's default install location.
        std::vector<std::string> CandidateDotnetRoots(const std::string& configured)
        {
            std::vector<std::string> roots;
            if (!configured.empty())
            {
                roots.push_back(configured);
            }
            if (const char* fromEnv = std::getenv("DOTNET_ROOT"); fromEnv != nullptr && *fromEnv != '\0')
            {
                roots.emplace_back(fromEnv);
            }
#if defined(_WIN32)
            if (const char* programFiles = std::getenv("ProgramFiles"); programFiles != nullptr)
            {
                roots.push_back(std::string(programFiles) + "\\dotnet");
            }
#elif defined(__APPLE__)
            roots.emplace_back("/usr/local/share/dotnet");
            roots.emplace_back("/opt/homebrew/share/dotnet");
#else
            roots.emplace_back("/usr/share/dotnet");
            roots.emplace_back("/usr/lib/dotnet");
#endif
            return roots;
        }

        /// Orders "10.0.8" style directory names numerically; lexicographic order would prefer
        /// 9.x over 10.x.
        bool IsNewerVersion(const std::string& candidate, const std::string& current)
        {
            auto parse = [](const std::string& text)
            {
                std::vector<long> parts;
                size_t start = 0;
                while (start <= text.size())
                {
                    const size_t dot = text.find('.', start);
                    const std::string part = text.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
                    parts.push_back(part.empty() ? 0 : std::strtol(part.c_str(), nullptr, 10));
                    if (dot == std::string::npos)
                    {
                        break;
                    }
                    start = dot + 1;
                }
                return parts;
            };

            const std::vector<long> left = parse(candidate);
            const std::vector<long> right = parse(current);
            for (size_t index = 0; index < left.size() || index < right.size(); index++)
            {
                const long leftPart = index < left.size() ? left[index] : 0;
                const long rightPart = index < right.size() ? right[index] : 0;
                if (leftPart != rightPart)
                {
                    return leftPart > rightPart;
                }
            }
            return false;
        }

        /// Finds the newest hostfxr under any candidate root. Returns an empty path when no .NET
        /// installation is usable.
        std::filesystem::path FindHostfxr(const std::string& configuredRoot, std::string& outRoot)
        {
#if defined(_WIN32)
            constexpr const char* libraryName = "hostfxr.dll";
#elif defined(__APPLE__)
            constexpr const char* libraryName = "libhostfxr.dylib";
#else
            constexpr const char* libraryName = "libhostfxr.so";
#endif

            for (const std::string& root : CandidateDotnetRoots(configuredRoot))
            {
                std::error_code ec;
                const std::filesystem::path fxrDir = std::filesystem::path(root) / "host" / "fxr";
                if (!std::filesystem::is_directory(fxrDir, ec))
                {
                    continue;
                }

                std::string bestVersion;
                std::filesystem::path bestPath;
                for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(fxrDir, ec))
                {
                    if (!entry.is_directory())
                    {
                        continue;
                    }
                    const std::filesystem::path candidate = entry.path() / libraryName;
                    if (!std::filesystem::exists(candidate, ec))
                    {
                        continue;
                    }
                    const std::string version = entry.path().filename().string();
                    if (bestVersion.empty() || IsNewerVersion(version, bestVersion))
                    {
                        bestVersion = version;
                        bestPath = candidate;
                    }
                }

                if (!bestPath.empty())
                {
                    // Native separators, always. hostpolicy builds the trusted-assembly list by
                    // concatenating this root with relative paths and then relies on string
                    // comparison to spot duplicates; a root with foreign separators makes
                    // System.Private.CoreLib appear twice and coreclr_initialize rejects the whole
                    // list with E_INVALIDARG.
                    outRoot = std::filesystem::path(root).make_preferred().string();
                    return bestPath;
                }
            }

            return {};
        }

        std::string DescribeFailure(const char* what, int32_t code)
        {
            std::string message = std::string(what) + " failed (0x";
            constexpr char digits[] = "0123456789abcdef";
            const uint32_t value = static_cast<uint32_t>(code);
            for (int shift = 28; shift >= 0; shift -= 4)
            {
                message += digits[(value >> shift) & 0xF];
            }
            message += ")";
            if (!GLastHostfxrError.empty())
            {
                message += ": " + GLastHostfxrError;
            }
            return message;
        }

        class FCoreClrHost final : public IManagedHost
        {
        public:
            explicit FCoreClrHost(FHostConfig config) : config_(std::move(config)) {}

            ~FCoreClrHost() override
            {
                // The runtime itself is intentionally left running: CoreCLR cannot be unloaded
                // from a process, and tearing down the context would invalidate function pointers
                // native code may still hold.
                if (contextHandle_ != nullptr && closeFn_ != nullptr)
                {
                    closeFn_(contextHandle_);
                    contextHandle_ = nullptr;
                }
            }

            bool Initialize(const FEngineApi& engineApi, std::string& outError) override
            {
                if (!LoadHostfxr(outError))
                {
                    return false;
                }

                FBootstrapFn bootstrap = nullptr;
                if (!ResolveBootstrap(bootstrap, outError))
                {
                    return false;
                }

                const int32_t result = bootstrap(&engineApi, &managed_);
                if (result != 0)
                {
                    outError = DescribeFailure("GkNext_Bootstrap", result);
                    return false;
                }
                if (managed_.Version != GK_DOTNET_ABI_VERSION)
                {
                    outError = "managed ABI version mismatch";
                    return false;
                }

                return true;
            }

            const FManagedApi* Managed() const override { return managed_.Tick != nullptr ? &managed_ : nullptr; }

            const char* BackendName() const override { return "CoreCLR"; }

            bool SupportsHotReload() const override { return true; }

            bool LoadsGameFromDisk() const override { return true; }

        private:
            bool LoadHostfxr(std::string& outError)
            {
                std::string resolvedRoot;
                const std::filesystem::path hostfxrPath = FindHostfxr(config_.dotnetRoot, resolvedRoot);
                if (hostfxrPath.empty())
                {
                    outError = "no .NET runtime found (looked for host/fxr under " +
                               (config_.dotnetRoot.empty() ? std::string("the default install locations")
                                                           : config_.dotnetRoot) +
                               "); run 'gnb dotnet setup'";
                    return false;
                }
                dotnetRoot_ = resolvedRoot;

                FLibraryHandle library = LoadHostLibrary(ToHostString(hostfxrPath.string()));
                if (library == nullptr)
                {
                    outError = "could not load " + hostfxrPath.string();
                    return false;
                }

                initFn_ = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(
                    GetHostExport(library, "hostfxr_initialize_for_runtime_config"));
                getDelegateFn_ = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(
                    GetHostExport(library, "hostfxr_get_runtime_delegate"));
                closeFn_ = reinterpret_cast<hostfxr_close_fn>(GetHostExport(library, "hostfxr_close"));
                auto setErrorWriterFn = reinterpret_cast<hostfxr_set_error_writer_fn>(
                    GetHostExport(library, "hostfxr_set_error_writer"));

                if (initFn_ == nullptr || getDelegateFn_ == nullptr || closeFn_ == nullptr)
                {
                    outError = "hostfxr is missing required exports";
                    return false;
                }
                if (setErrorWriterFn != nullptr)
                {
                    setErrorWriterFn(&OnHostfxrError);
                }
                return true;
            }

            bool ResolveBootstrap(FBootstrapFn& outBootstrap, std::string& outError)
            {
                const std::filesystem::path root = std::filesystem::path(config_.managedRootDir).make_preferred();
                const std::filesystem::path runtimeConfig =
                    (root / (config_.bootstrapAssembly + ".runtimeconfig.json")).make_preferred();
                const std::filesystem::path assembly = (root / (config_.bootstrapAssembly + ".dll")).make_preferred();

                if (!std::filesystem::exists(runtimeConfig) || !std::filesystem::exists(assembly))
                {
                    outError = "managed assembly not found under " + config_.managedRootDir +
                               " (run 'gnb dotnet build' first)";
                    return false;
                }

                GLastHostfxrError.clear();
                const FHostString runtimeConfigPath = ToHostString(runtimeConfig.string());
                const FHostString hostPath = ToHostString(assembly.string());
                const FHostString dotnetRoot = ToHostString(dotnetRoot_);

                // Pin framework resolution to the root hostfxr itself came from, so a machine-wide
                // install cannot satisfy a session that is meant to run on external/dotnet.
                hostfxr_initialize_parameters parameters{};
                parameters.size = sizeof(parameters);
                parameters.host_path = hostPath.c_str();
                parameters.dotnet_root = dotnetRoot.c_str();

                int32_t code = initFn_(runtimeConfigPath.c_str(), &parameters, &contextHandle_);
                // Success codes: 0 (Success) and 1 (Success_HostAlreadyInitialized).
                if (code > 1 || contextHandle_ == nullptr)
                {
                    outError = DescribeFailure("hostfxr_initialize_for_runtime_config", code);
                    return false;
                }

                void* rawDelegate = nullptr;
                code = getDelegateFn_(contextHandle_, hdt_load_assembly_and_get_function_pointer, &rawDelegate);
                if (code != 0 || rawDelegate == nullptr)
                {
                    outError = DescribeFailure("hostfxr_get_runtime_delegate", code);
                    return false;
                }

                auto loadAssembly = reinterpret_cast<load_assembly_and_get_function_pointer_fn>(rawDelegate);

                const FHostString assemblyPath = ToHostString(assembly.string());
                const FHostString typeName = ToHostString(config_.bootstrapType + ", " + config_.bootstrapAssembly);
                const FHostString methodName = ToHostString(config_.bootstrapMethod);

                void* entryPoint = nullptr;
                code = loadAssembly(assemblyPath.c_str(),
                                    typeName.c_str(),
                                    methodName.c_str(),
                                    UNMANAGEDCALLERSONLY_METHOD,
                                    nullptr,
                                    &entryPoint);
                if (code != 0 || entryPoint == nullptr)
                {
                    outError = DescribeFailure("load_assembly_and_get_function_pointer", code);
                    return false;
                }

                outBootstrap = reinterpret_cast<FBootstrapFn>(entryPoint);
                return true;
            }

            FHostConfig config_;
            std::string dotnetRoot_;
            hostfxr_initialize_for_runtime_config_fn initFn_ = nullptr;
            hostfxr_get_runtime_delegate_fn getDelegateFn_ = nullptr;
            hostfxr_close_fn closeFn_ = nullptr;
            hostfxr_handle contextHandle_ = nullptr;
            FManagedApi managed_{};
        };
    }

    std::unique_ptr<IManagedHost> CreateManagedHost(FHostConfig config)
    {
        return std::make_unique<FCoreClrHost>(std::move(config));
    }
}

#endif
