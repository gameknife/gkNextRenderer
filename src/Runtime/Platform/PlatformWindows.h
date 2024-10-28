#pragma once

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
}


