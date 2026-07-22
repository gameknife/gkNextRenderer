#include "Engine/Runtime/ScreenShotService.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Engine/Utilities/FileHelper.hpp"

#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>

#include <fmt/chrono.h>
#include <fmt/format.h>

namespace Runtime
{
    namespace
    {
        std::string SanitizeTag(const std::string& tag)
        {
            std::string sanitized;
            sanitized.reserve(tag.size());
            for (const unsigned char character : tag)
            {
                if (std::isalnum(character) != 0 || character == '-' || character == '_')
                {
                    sanitized.push_back(static_cast<char>(character));
                }
                else
                {
                    sanitized.push_back('-');
                }
            }
            return sanitized;
        }
    } // namespace

    FScreenShotService::FScreenShotService(NextEngine& engine) : engine_(engine) {}

    bool FScreenShotService::Request(FRequest request)
    {
        if (IsBusy())
        {
            return false;
        }

        EnsureDirectory();
        const std::string filename = BuildFilename(request.tag);
        requestPending_ = true;

        engine_.RequestScreenShot({
            .filename = filename,
            .accumulateFrames = request.accumulateFrames,
            .includeUi = request.includeUi,
            .fast = request.fast,
        });

        engine_.AddTickedTask([this, filename, onCompleted = std::move(request.onCompleted)](double) mutable
        {
            if (engine_.IsCapturingScreenShot())
            {
                return false;
            }

            requestPending_ = false;
            if (onCompleted)
            {
                onCompleted(filename);
            }
            return true;
        });
        return true;
    }

    bool FScreenShotService::IsBusy() const
    {
        return requestPending_ || engine_.IsCapturingScreenShot();
    }

    std::string FScreenShotService::GetDirectory() const
    {
        return Utilities::FileHelper::GetPlatformFilePath("screenshots");
    }

    void FScreenShotService::EnsureDirectory() const
    {
        Utilities::FileHelper::EnsureDirectoryExists(GetDirectory());
    }

    std::string FScreenShotService::BuildFilename(const std::string& tag)
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t time = std::chrono::system_clock::to_time_t(now);
        const std::tm* localTime = std::localtime(&time);
        const std::string timestamp = fmt::format("{:%Y-%m-%d-%H-%M-%S}", *localTime);

        std::string stem = "screenshot_" + timestamp;
        const std::string sanitizedTag = SanitizeTag(tag);
        if (!sanitizedTag.empty())
        {
            stem += "_" + sanitizedTag;
        }

        if (stem == lastStem_)
        {
            ++stemSequence_;
        }
        else
        {
            lastStem_ = stem;
            stemSequence_ = 0;
        }

        if (stemSequence_ > 0)
        {
            stem += fmt::format("_{}", stemSequence_);
        }

        return (std::filesystem::path(GetDirectory()) / stem).string();
    }
} // namespace Runtime
