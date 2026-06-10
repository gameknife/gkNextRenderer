#pragma once

class NextEngine;

namespace NextAI
{
    class FAIService;

    // Returns the engine-scoped AI service, creating and attaching it to the
    // engine's external service slot on first use. Replaces the former
    // NextEngine::GetAIService() core getter.
    FAIService* GetAIService(NextEngine& engine);
}
