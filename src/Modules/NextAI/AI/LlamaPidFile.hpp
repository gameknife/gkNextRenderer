#pragma once
#include <string>

namespace NextAI
{
    struct FLlamaPidInfo
    {
        bool valid = false;
        int pid = 0;
        std::string host;
        int port = 0;
        std::string model;

        std::string Endpoint() const
        {
            if (host.empty() || port == 0) return {};
            return "http://" + host + ":" + std::to_string(port);
        }
    };

    // Parse the gnb llama-server PID file content.
    // Layout (one value per line):
    //   1: pid
    //   2: host
    //   3: port
    //   4: model id
    //   5+: optional key:value metadata (ctx, parallel, reasoning)
    FLlamaPidInfo ParseLlamaPidFile(const std::string& content);

    // Read and parse external/llm/run/server.pid relative to repo root.
    // Returns invalid info on any failure (missing file, parse error, partial write).
    FLlamaPidInfo ReadLlamaPidFile(const std::string& pidFilePath);
}
