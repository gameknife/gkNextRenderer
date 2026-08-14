#include "Modules/NextDotNet/Host/IManagedHost.hpp"

#if GK_DOTNET_USE_AOT

// NativeAOT backend: the managed code is compiled ahead of time and linked in, so GkNext_Bootstrap
// is an ordinary symbol. No runtime to locate, no assembly to load, no JIT — which is also why hot
// reload does not exist here.
//
// The contrast with CoreClrHost.cpp is the point of the two-backend design: everything that host
// needs ~200 lines to arrange, the linker has already done.

#include <utility>

extern "C" int32_t GkNext_Bootstrap(const Modules::NextDotNet::FEngineApi* engineApi,
                                    Modules::NextDotNet::FManagedApi* outManagedApi);

namespace Modules::NextDotNet
{
    namespace
    {
        class FAotHost final : public IManagedHost
        {
        public:
            explicit FAotHost(FHostConfig config) : config_(std::move(config)) {}

            bool Initialize(const FEngineApi& engineApi, std::string& outError) override
            {
                const int32_t result = GkNext_Bootstrap(&engineApi, &managed_);
                if (result != 0)
                {
                    outError = "GkNext_Bootstrap returned " + std::to_string(result);
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

            const char* BackendName() const override { return "NativeAOT"; }

            bool SupportsHotReload() const override { return false; }

            bool LoadsGameFromDisk() const override { return false; }

        private:
            FHostConfig config_;
            FManagedApi managed_{};
        };
    }

    std::unique_ptr<IManagedHost> CreateManagedHost(FHostConfig config)
    {
        return std::make_unique<FAotHost>(std::move(config));
    }
}

#endif
