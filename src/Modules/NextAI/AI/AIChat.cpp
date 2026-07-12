#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextAI/AI/AIChat.hpp"

namespace NextAI
{
    const char* ToolParamTypeToString(EToolParamType type)
    {
        switch (type)
        {
        case EToolParamType::Number: return "number";
        case EToolParamType::Integer: return "integer";
        case EToolParamType::Boolean: return "boolean";
        case EToolParamType::Object: return "object";
        case EToolParamType::Array: return "array";
        default: return "string";
        }
    }

    const char* ChatRoleToString(EChatRole role)
    {
        switch (role)
        {
        case EChatRole::System: return "system";
        case EChatRole::Assistant: return "assistant";
        case EChatRole::Tool: return "tool";
        default: return "user";
        }
    }
}
