#include "Gameplay/Reflection/GameplayReflectionRegistry.h"

#include "Gameplay/Components/AIAgentComponent.h"
#include "Gameplay/Components/CharacterAnimationComponent.h"
#include "Gameplay/Components/CharacterControlComponent.h"
#include "Gameplay/Components/CharacterGameplayComponent.h"

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
