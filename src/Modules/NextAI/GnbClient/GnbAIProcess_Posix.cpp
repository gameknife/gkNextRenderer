#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextAI/GnbClient/GnbAIProcess.hpp"

#if !WIN32 && !ANDROID && !IOS
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace NextAI
{
    struct FGnbAIProcess::FImpl
    {
        pid_t process = -1;
        int inputWrite = -1;
        int outputRead = -1;
        std::mutex writeMutex;
        std::atomic<bool> running = false;
    };
    FGnbAIProcess::FGnbAIProcess() : impl_(std::make_unique<FImpl>()) {}
    FGnbAIProcess::~FGnbAIProcess() { Stop(); }
    bool FGnbAIProcess::Start(const std::filesystem::path& executable, const std::filesystem::path& repoRoot,
                                 std::string& error)
    {
        Stop();
        int inputPipe[2]{};
        int outputPipe[2]{};
        if (pipe(inputPipe) != 0 || pipe(outputPipe) != 0) { error = "pipe failed"; return false; }
        const pid_t child = fork();
        if (child < 0) { error = "fork failed"; return false; }
        if (child == 0)
        {
            dup2(inputPipe[0], STDIN_FILENO);
            dup2(outputPipe[1], STDOUT_FILENO);
            close(inputPipe[0]); close(inputPipe[1]); close(outputPipe[0]); close(outputPipe[1]);
            const std::string exe = executable.string();
            const std::string root = repoRoot.string();
            execl(exe.c_str(), exe.c_str(), "--repo-root", root.c_str(), "ai", "bridge", "--stdio", nullptr);
            _exit(127);
        }
        close(inputPipe[0]); close(outputPipe[1]);
        impl_->process = child; impl_->inputWrite = inputPipe[1]; impl_->outputRead = outputPipe[0]; impl_->running = true;
        return true;
    }
    bool FGnbAIProcess::WriteLine(const std::string& line)
    {
        std::lock_guard lock(impl_->writeMutex); const std::string framed = line + "\n";
        return impl_->running && write(impl_->inputWrite, framed.data(), framed.size()) == static_cast<ssize_t>(framed.size());
    }
    bool FGnbAIProcess::ReadLine(std::string& line)
    {
        line.clear(); char value = 0;
        while (impl_->running && read(impl_->outputRead, &value, 1) == 1) { if (value == '\n') return true; if (value != '\r') line.push_back(value); if (line.size() > 4 * 1024 * 1024) return false; }
        impl_->running = false; return false;
    }
    bool FGnbAIProcess::IsRunning() const { return impl_->running && impl_->process > 0 && kill(impl_->process, 0) == 0; }
    void FGnbAIProcess::Stop()
    {
        impl_->running = false; if (impl_->inputWrite >= 0) { close(impl_->inputWrite); impl_->inputWrite = -1; } if (impl_->outputRead >= 0) { close(impl_->outputRead); impl_->outputRead = -1; }
        if (impl_->process > 0) { int status = 0; if (waitpid(impl_->process, &status, WNOHANG) == 0) { kill(impl_->process, SIGTERM); waitpid(impl_->process, &status, 0); } impl_->process = -1; }
    }
}
#endif
