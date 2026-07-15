#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextAI/GnbClient/GnbAIProcess.hpp"

#if ANDROID || IOS
namespace NextAI
{
    struct FGnbAIProcess::FImpl {};
    FGnbAIProcess::FGnbAIProcess() : impl_(std::make_unique<FImpl>()) {}
    FGnbAIProcess::~FGnbAIProcess() = default;
    bool FGnbAIProcess::Start(const std::filesystem::path&, const std::filesystem::path&, std::string& error) { error = "gnb sidecar is unavailable on mobile"; return false; }
    bool FGnbAIProcess::WriteLine(const std::string&) { return false; }
    bool FGnbAIProcess::ReadLine(std::string&) { return false; }
    bool FGnbAIProcess::IsRunning() const { return false; }
    void FGnbAIProcess::Stop() {}
}
#endif
