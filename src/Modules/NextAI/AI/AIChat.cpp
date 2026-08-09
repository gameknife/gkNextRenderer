#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextAI/AI/AIChat.hpp"

namespace NextAI
{
    const char* ChatRoleToString(EChatRole role)
    {
        switch (role)
        {
        case EChatRole::System: return "system";
        case EChatRole::Assistant: return "assistant";
        default: return "user";
        }
    }
}
