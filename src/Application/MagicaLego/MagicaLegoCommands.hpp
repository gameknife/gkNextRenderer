#pragma once
#include "Common/CoreMinimal.hpp"
#include "MagicaLegoGameInstance.hpp"

namespace MagicaLego
{
    struct FCommandResult
    {
        bool success;
        std::string message;
        std::vector<std::string> output;

        static FCommandResult Success(const std::string& msg = "")
        {
            return {true, msg, {}};
        }

        static FCommandResult Success(const std::string& msg, std::vector<std::string> out)
        {
            return {true, msg, std::move(out)};
        }

        static FCommandResult Failure(const std::string& msg)
        {
            return {false, msg, {}};
        }
    };

    class ICommand
    {
    public:
        virtual ~ICommand() = default;
        virtual FCommandResult Execute(MagicaLegoGameInstance* gi) = 0;
        virtual std::string GetName() const = 0;
    };

    class FPlaceCommand : public ICommand
    {
    public:
        FPlaceCommand(std::string type, std::string color, glm::i16vec3 pos, EOrientation orient)
            : type_(std::move(type)), color_(std::move(color)), position_(pos), orientation_(orient) {}

        FCommandResult Execute(MagicaLegoGameInstance* gi) override;
        std::string GetName() const override { return "place"; }

    private:
        std::string type_;
        std::string color_;
        glm::i16vec3 position_;
        EOrientation orientation_;
    };

    class FDigCommand : public ICommand
    {
    public:
        explicit FDigCommand(glm::i16vec3 pos) : position_(pos) {}

        FCommandResult Execute(MagicaLegoGameInstance* gi) override;
        std::string GetName() const override { return "dig"; }

    private:
        glm::i16vec3 position_;
    };

    class FListCommand : public ICommand
    {
    public:
        enum class EListTarget { Types, Colors };

        explicit FListCommand(EListTarget target, std::string typeFilter = "")
            : target_(target), typeFilter_(std::move(typeFilter)) {}

        FCommandResult Execute(MagicaLegoGameInstance* gi) override;
        std::string GetName() const override { return "list"; }

    private:
        EListTarget target_;
        std::string typeFilter_;
    };

    class FCommandParser
    {
    public:
        static std::unique_ptr<ICommand> Parse(const std::string& line, std::string& error);

    private:
        static std::vector<std::string> Tokenize(const std::string& line);
        static EOrientation ParseOrientation(const std::string& str);
        static bool ParseTypeColor(const std::string& spec, std::string& outType, std::string& outColor);
    };

    class FCommandExecutor
    {
    public:
        explicit FCommandExecutor(MagicaLegoGameInstance* gi) : gameInstance_(gi) {}

        FCommandResult ExecuteCommand(const std::string& line);
        FCommandResult ExecuteScript(const std::string& path);
        FCommandResult ExecuteScriptText(const std::string& text);

        const std::vector<std::string>& GetHistory() const { return history_; }
        void ClearHistory() { history_.clear(); }

        std::string GetHistoryPrev();
        std::string GetHistoryNext();

    private:
        MagicaLegoGameInstance* gameInstance_;
        std::vector<std::string> history_;
        int historyIndex_ = -1;
    };
}
