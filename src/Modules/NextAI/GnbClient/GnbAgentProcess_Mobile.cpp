#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextAI/GnbClient/GnbAgentProcess.hpp"

#if ANDROID || IOS
namespace NextAI
{
    struct FGnbAgentProcess::FImpl {};
    FGnbAgentProcess::FGnbAgentProcess() : impl_(std::make_unique<FImpl>()) {}
    FGnbAgentProcess::~FGnbAgentProcess() = default;
    bool FGnbAgentProcess::Start(const std::filesystem::path&, const std::filesystem::path&, std::string& error) { error = "gnb sidecar is unavailable on mobile"; return false; }
    bool FGnbAgentProcess::WriteLine(const std::string&) { return false; }
    bool FGnbAgentProcess::ReadLine(std::string&) { return false; }
    bool FGnbAgentProcess::IsRunning() const { return false; }
    void FGnbAgentProcess::Stop() {}
}
#endif
