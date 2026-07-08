#pragma once

class NextEngine;

namespace Modules::AgentDriver
{
    // Installs the agent script driver factory on the engine. Call from the
    // application entry before NextEngine::Start.
    void Install(NextEngine& engine);
}
