#pragma once

#include <windows.h>
#include <shellapi.h>

namespace NextRenderer
{
    inline void PlatformInit()
    {
        // Windows console color support
#if WIN32 && !defined(__MINGW32__)
        HANDLE hOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD dwMode;

        GetConsoleMode(hOutput, &dwMode);
        dwMode |= ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOutput, dwMode);
#endif
    }

    inline void HideConsole()
    {
        FreeConsole();
    }

    inline void OSCommand(const char* command)
    {
        ::ShellExecuteA(NULL, "open",
                        command,
                        "",NULL,SW_SHOWNORMAL
        );
    }

    inline void OSProcess(const char* commandline)
    {
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        ZeroMemory(&pi, sizeof(pi));
        
        if (!CreateProcessA(
            NULL,
            const_cast<char*>(commandline),
            NULL,
            NULL,
            FALSE,
            0,
            NULL,
            NULL,
            &si,
            &pi
        ))
        {
            return;
        }
        
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}
