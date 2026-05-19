#include "Engine/NextGameplay/Reflection/GameplayReflectionRegistry.h"

#include "Engine/NextGameplay/Components/AIAgentComponent.h"
#include "Engine/NextGameplay/Components/CharacterAnimationComponent.h"
#include "Engine/NextGameplay/Components/CharacterControlComponent.h"
#include "Engine/NextGameplay/Components/CharacterGameplayComponent.h"

namespace NextGameplay
{
    namespace
    {
        bool sGameplayReflectionInitialized = false;
    }

    void RegisterGameplayReflection()
    {
        if (sGameplayReflectionInitialized)
        {
            return;
        }

        CharacterGameplayComponent::RegisterReflection();
        CharacterAnimationComponent::RegisterReflection();
        CharacterControlComponent::RegisterReflection();
        AIAgentComponent::RegisterReflection();
        sGameplayReflectionInitialized = true;
    }

    bool IsGameplayReflectionInitialized()
    {
        return sGameplayReflectionInitialized;
    }
}
