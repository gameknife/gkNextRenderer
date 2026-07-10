#pragma once

class NextEngine;

namespace Modules::Audio
{
    // Installs the optional miniaudio backend. Call after constructing the
    // engine and before NextEngine::Start().
    void Install(NextEngine& engine);
}
