#pragma once

class NextEngine;

namespace Modules::LiveCoding
{
    // Installs the shader hot reloader factory on the engine. Call from the
    // application entry before NextEngine::Start.
    void Install(NextEngine& engine);
}
