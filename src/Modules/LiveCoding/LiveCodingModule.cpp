#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/LiveCoding/LiveCodingModule.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Modules/LiveCoding/ShaderHotReloader.hpp"

namespace Modules::LiveCoding
{
    void Install(NextEngine& engine)
    {
        engine.SetShaderHotReloaderFactory([](NextEngine& owner) -> std::unique_ptr<Runtime::IShaderHotReloader>
        {
            return std::make_unique<ShaderHotReloader>(owner.GetRenderer());
        });
    }
}
