#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextPhysics/NextPhysicsModule.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Modules/NextPhysics/JoltPhysicsBackend.hpp"

namespace Modules::Physics
{
    void Install(NextEngine& engine)
    {
        engine.SetPhysicsFactory([] { return std::make_unique<FJoltPhysicsBackend>(); });
    }
}
