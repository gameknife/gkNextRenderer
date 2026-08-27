#pragma once

class NextEngine;

namespace Modules::NextValidation
{
    // Installs the loopback validation endpoint when --agent-control is present.
    void Install(NextEngine& engine);
}
