#pragma once

#include "Engine/Runtime/Interface/RenderFrameConsumer.hpp"

#include <fstream>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

namespace spdlog
{
    class logger;

    namespace sinks
    {
        class sink;
    }
}

namespace Runtime::Config
{
    class Options;
}

class NextEngine;

namespace Modules::NextTui
{
    class FProcessLogCapture;
    class FProcessLogCapture
    {
    public:
        explicit FProcessLogCapture(std::string logPath);
        ~FProcessLogCapture();

        bool IsActive() const;

    private:
        std::string logFilePath_{};
        std::ofstream logFile_{};
        std::streambuf* originalCoutBuffer_ = nullptr;
        std::streambuf* originalCerrBuffer_ = nullptr;
        std::shared_ptr<spdlog::logger> logger_{};
        std::shared_ptr<spdlog::sinks::sink> fileSink_{};
        std::vector<std::shared_ptr<spdlog::sinks::sink>> mutedLoggerSinks_{};
    };

    std::unique_ptr<FProcessLogCapture> CreateProcessLogCapture();
    std::unique_ptr<Runtime::IRenderFrameConsumer> CreateTuiPresenter(NextEngine& engine,
                                                                      const Runtime::Config::Options& options);
}
