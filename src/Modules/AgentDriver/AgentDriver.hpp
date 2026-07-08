#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Interface/AgentDriver.hpp"
#include "Engine/Runtime/Input/SyntheticInput.hpp"
#include <nlohmann/json.hpp>

class NextEngine;

namespace Runtime::Agent
{
    class FAgentDriver final : public IAgentDriver
    {
    public:
        explicit FAgentDriver(NextEngine& engine);
        ~FAgentDriver();

        bool IsLoaded() const override { return loaded_; }
        void Tick(double deltaSeconds) override;
        void DrawStatusOverlay() const override;
        int ExitCode() const override { return exitCode_; }

    private:
        enum class EWaitKind
        {
            None,
            Frames,
            Milliseconds,
            Screenshot,
            WaitUntil
        };

        struct FStepRuntime
        {
            EWaitKind kind = EWaitKind::None;
            uint32_t startFrame = 0;
            uint32_t targetFrame = 0;
            double startTime = 0.0;
            double targetTime = 0.0;
        };

        NextEngine& engine_;
        FAgentQueryRegistry gameQueries_;
        nlohmann::json script_;
        nlohmann::json report_;
        std::string name_;
        std::string reportPath_;
        std::string lastScreenshotPath_;
        std::string currentStepLabel_;
        std::string keyboardToast_;
        Runtime::Input::Synthetic::FPoint lastMousePos_{};
        Runtime::Input::Synthetic::FPoint lastMouseDelta_{};
        Runtime::Input::Synthetic::FPoint dragStart_{};
        Runtime::Input::Synthetic::FPoint dragEnd_{};
        std::vector<std::string> activeKeys_;
        std::vector<std::string> activeMouseButtons_;
        double lastKeyboardActionTime_ = -100.0;
        double lastMouseActionTime_ = -100.0;
        double lastDragActionTime_ = -100.0;
        size_t stepIndex_ = 0;
        bool loaded_ = false;
        bool finished_ = false;
        bool failed_ = false;
        bool closeRequested_ = false;
        int exitCode_ = 0;
        uint32_t defaultWaitFrames_ = 2;
        uint32_t defaultStepTimeoutMs_ = 8000;
        FStepRuntime wait_{};

        bool LoadScript();
        void RegisterGameQueries();
        void Finish(int exitCode);
        void WriteReport();
        void RecordStep(nlohmann::json entry);
        void RecordFailure(const nlohmann::json& step, std::string message, bool fatal);

        bool TickWait(const nlohmann::json& step);
        bool ExecuteStep(const nlohmann::json& step);
        bool ExecuteInputStep(const nlohmann::json& step, const std::string& type);
        bool ExecuteScreenshot(const nlohmann::json& step);
        bool ExecuteAssert(const nlohmann::json& step, bool waitUntil);
        bool ExecuteCVar(const nlohmann::json& step);
        bool ExecuteExec(const nlohmann::json& step);

        std::optional<FAgentQueryValue> Query(const std::string& query) const;
        std::string ResolveOutputPath(const std::string& path, const std::string& fallbackStem) const;
        Runtime::Input::Synthetic::FPoint ResolvePoint(const nlohmann::json& value) const;
        bool Compare(const FAgentQueryValue& actual, const std::string& op, const nlohmann::json& expected) const;
        void SetCurrentStepLabel(const nlohmann::json& step);
        void SetKeyboardToast(std::string label);
        void SetMouseMarker(Runtime::Input::Synthetic::FPoint pos, Runtime::Input::Synthetic::FPoint delta);
        void SetDragTrail(Runtime::Input::Synthetic::FPoint from, Runtime::Input::Synthetic::FPoint to);
        void SetKeyActive(const std::string& code, bool active);
        void SetMouseButtonActive(const std::string& button, bool active);
    };
}
