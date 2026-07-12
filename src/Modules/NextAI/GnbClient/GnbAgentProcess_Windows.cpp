#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextAI/GnbClient/GnbAgentProcess.hpp"

#include "Engine/Runtime/Platform/PlatformCommon.h"

#if WIN32
namespace NextAI
{
    struct FGnbAgentProcess::FImpl
    {
        HANDLE process = nullptr;
        HANDLE inputWrite = nullptr;
        HANDLE outputRead = nullptr;
        std::mutex writeMutex;
        std::atomic<bool> running = false;
    };

    FGnbAgentProcess::FGnbAgentProcess() : impl_(std::make_unique<FImpl>()) {}
    FGnbAgentProcess::~FGnbAgentProcess() { Stop(); }

    bool FGnbAgentProcess::Start(const std::filesystem::path& executable, const std::filesystem::path& repoRoot,
                                 std::string& error)
    {
        Stop();
        SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        HANDLE childInputRead = nullptr;
        HANDLE childOutputWrite = nullptr;
        if (!CreatePipe(&childInputRead, &impl_->inputWrite, &attributes, 0) ||
            !CreatePipe(&impl_->outputRead, &childOutputWrite, &attributes, 0))
        {
            error = "CreatePipe failed: " + std::to_string(GetLastError());
            return false;
        }
        SetHandleInformation(impl_->inputWrite, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(impl_->outputRead, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = childInputRead;
        startup.hStdOutput = childOutputWrite;
        startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        PROCESS_INFORMATION processInfo{};
        std::wstring command = L"\"" + executable.wstring() + L"\" --repo-root \"" + repoRoot.wstring() +
            L"\" agent bridge --stdio";
        const BOOL created = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                             nullptr, repoRoot.wstring().c_str(), &startup, &processInfo);
        CloseHandle(childInputRead);
        CloseHandle(childOutputWrite);
        if (!created)
        {
            error = "CreateProcess failed: " + std::to_string(GetLastError());
            Stop();
            return false;
        }
        CloseHandle(processInfo.hThread);
        impl_->process = processInfo.hProcess;
        impl_->running = true;
        return true;
    }

    bool FGnbAgentProcess::WriteLine(const std::string& line)
    {
        std::lock_guard lock(impl_->writeMutex);
        if (!impl_->running || !impl_->inputWrite) return false;
        const std::string framed = line + "\n";
        DWORD written = 0;
        return WriteFile(impl_->inputWrite, framed.data(), static_cast<DWORD>(framed.size()), &written, nullptr) &&
            written == framed.size();
    }

    bool FGnbAgentProcess::ReadLine(std::string& line)
    {
        line.clear();
        char value = 0;
        DWORD read = 0;
        while (impl_->running && ReadFile(impl_->outputRead, &value, 1, &read, nullptr) && read == 1)
        {
            if (value == '\n') return true;
            if (value != '\r') line.push_back(value);
            if (line.size() > 4 * 1024 * 1024) return false;
        }
        impl_->running = false;
        return false;
    }

    bool FGnbAgentProcess::IsRunning() const
    {
        if (!impl_->running || !impl_->process) return false;
        return WaitForSingleObject(impl_->process, 0) == WAIT_TIMEOUT;
    }

    void FGnbAgentProcess::Stop()
    {
        impl_->running = false;
        if (impl_->inputWrite) { CloseHandle(impl_->inputWrite); impl_->inputWrite = nullptr; }
        if (impl_->outputRead) { CloseHandle(impl_->outputRead); impl_->outputRead = nullptr; }
        if (impl_->process)
        {
            if (WaitForSingleObject(impl_->process, 1000) == WAIT_TIMEOUT) TerminateProcess(impl_->process, 1);
            WaitForSingleObject(impl_->process, 1000);
            CloseHandle(impl_->process);
            impl_->process = nullptr;
        }
    }
}
#endif
