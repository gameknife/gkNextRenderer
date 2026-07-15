#pragma once

#include "Engine/Common/CoreMinimal.hpp"

namespace NextAI
{
    class FGnbAIProcess
    {
    public:
        FGnbAIProcess();
        ~FGnbAIProcess();

        FGnbAIProcess(const FGnbAIProcess&) = delete;
        FGnbAIProcess& operator=(const FGnbAIProcess&) = delete;

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
