#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextAI/NextAIModule.hpp"
#include "Modules/NextAI/AIService.hpp"
#include "Engine/Runtime/Engine.hpp"

namespace NextAI
{
    namespace
    {
        constexpr const char* kServiceKey = "NextAI.FAIService";
    }

    FAIService* GetAIService(NextEngine& engine)
    {
        if (auto existing = engine.GetExternalService(kServiceKey))
        {
            return static_cast<FAIService*>(existing.get());
        }
        auto service = std::make_shared<FAIService>();
        FAIService* raw = service.get();
        engine.SetExternalService(kServiceKey, std::move(service));
        return raw;
    }
}
