#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextTui/NextTuiModule.hpp"

#include "Engine/Utilities/FileHelper.hpp"
#include "Modules/NextTui/TuiPresenter.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>

#include <spdlog/logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

namespace Modules::NextTui
{
    namespace
    {
        bool IsTerminalSink(const std::shared_ptr<spdlog::sinks::sink>& sink)
        {
            using namespace spdlog::sinks;
            return dynamic_cast<stdout_color_sink_mt*>(sink.get()) != nullptr ||
                dynamic_cast<stderr_color_sink_mt*>(sink.get()) != nullptr ||
                dynamic_cast<stdout_sink_mt*>(sink.get()) != nullptr ||
                dynamic_cast<stderr_sink_mt*>(sink.get()) != nullptr;
        }
    }

    FProcessLogCapture::FProcessLogCapture(std::string logPath)
        : logFilePath_(std::move(logPath))
        , logFile_(logFilePath_, std::ios::out | std::ios::app)
    {
        if (!logFile_.is_open())
        {
            return;
        }

        originalCoutBuffer_ = std::cout.rdbuf(logFile_.rdbuf());
        originalCerrBuffer_ = std::cerr.rdbuf(logFile_.rdbuf());

        logger_ = spdlog::default_logger();
        if (logger_)
        {
            auto& sinks = logger_->sinks();
            std::vector<std::shared_ptr<spdlog::sinks::sink>> retainedSinks;
            retainedSinks.reserve(sinks.size() + 1);
            for (const auto& sink : sinks)
            {
                if (IsTerminalSink(sink))
                {
                    mutedLoggerSinks_.push_back(sink);
                }
                else
                {
                    retainedSinks.push_back(sink);
                }
            }
            fileSink_ = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath_, true);
            retainedSinks.push_back(fileSink_);
            sinks = std::move(retainedSinks);
        }
    }

    FProcessLogCapture::~FProcessLogCapture()
    {
        if (originalCoutBuffer_)
        {
            std::cout.rdbuf(originalCoutBuffer_);
        }
        if (originalCerrBuffer_)
        {
            std::cerr.rdbuf(originalCerrBuffer_);
        }
        if (logger_ && fileSink_)
        {
            auto& sinks = logger_->sinks();
            sinks.erase(std::remove(sinks.begin(), sinks.end(), fileSink_), sinks.end());
            sinks.insert(sinks.end(), mutedLoggerSinks_.begin(), mutedLoggerSinks_.end());
        }
    }

    bool FProcessLogCapture::IsActive() const
    {
        return logFile_.is_open() && logger_ != nullptr && fileSink_ != nullptr &&
            originalCoutBuffer_ != nullptr && originalCerrBuffer_ != nullptr;
    }

    std::unique_ptr<FProcessLogCapture> CreateProcessLogCapture()
    {
        const std::string logPath = Utilities::FileHelper::GetPlatformFilePath("logs/tui.log");
        Utilities::FileHelper::EnsureDirectoryExists(std::filesystem::path(logPath).parent_path().string());
        auto capture = std::make_unique<FProcessLogCapture>(logPath);
        if (!capture->IsActive())
        {
            return nullptr;
        }
        return capture;
    }

    std::unique_ptr<Runtime::IRenderFrameConsumer> CreateTuiPresenter(NextEngine& engine,
                                                                      const Runtime::Config::Options& options)
    {
        return std::make_unique<Runtime::Tui::TuiPresenter>(engine, options);
    }
}
