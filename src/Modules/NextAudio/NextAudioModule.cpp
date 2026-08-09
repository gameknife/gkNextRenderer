#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextAudio/NextAudioModule.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Modules/NextAudio/MiniaudioBackend.hpp"

namespace Modules::Audio
{
    void Install(NextEngine& engine)
    {
        engine.SetAudioFactory([] { return std::make_unique<FMiniaudioBackend>(); });
    }
}
