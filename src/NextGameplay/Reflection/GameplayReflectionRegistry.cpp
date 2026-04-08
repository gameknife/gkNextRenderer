#include "NextGameplay/Reflection/GameplayReflectionRegistry.h"

#include "NextGameplay/Components/AIAgentComponent.h"
#include "NextGameplay/Components/CharacterAnimationComponent.h"
#include "NextGameplay/Components/CharacterControlComponent.h"
#include "NextGameplay/Components/CharacterGameplayComponent.h"

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
