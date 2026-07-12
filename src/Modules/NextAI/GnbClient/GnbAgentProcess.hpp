#pragma once

#include "Engine/Common/CoreMinimal.hpp"

namespace NextAI
{
    class FGnbAgentProcess
    {
    public:
        FGnbAgentProcess();
        ~FGnbAgentProcess();

        FGnbAgentProcess(const FGnbAgentProcess&) = delete;
        FGnbAgentProcess& operator=(const FGnbAgentProcess&) = delete;

        bool Start(const std::filesystem::path& executable, const std::filesystem::path& repoRoot,
                   std::string& error);
        bool WriteLine(const std::string& line);
        bool ReadLine(std::string& line);
        bool IsRunning() const;
        void Stop();

    private:
        struct FImpl;
        std::unique_ptr<FImpl> impl_;
    };
}
