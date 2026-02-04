#pragma once
#include "Common/CoreMinimal.hpp"
#include "MagicaLegoGameInstance.hpp"
#include <nlohmann/json.hpp>

namespace MagicaLego
{
    // ==================== Direction Helpers ====================

    inline glm::i16vec3 GetDirectionVector(EOrientation dir)
    {
        switch (dir)
        {
        case EOrientation::EO_North: return {0, 0, -1};   // -Z
        case EOrientation::EO_East:  return {1, 0, 0};    // +X
        case EOrientation::EO_South: return {0, 0, 1};    // +Z
        case EOrientation::EO_West:  return {-1, 0, 0};   // -X
        }
        return {0, 0, 0};
    }

    inline EOrientation TurnLeft(EOrientation dir)
    {
        switch (dir)
        {
        case EOrientation::EO_North: return EOrientation::EO_West;
        case EOrientation::EO_West:  return EOrientation::EO_South;
        case EOrientation::EO_South: return EOrientation::EO_East;
        case EOrientation::EO_East:  return EOrientation::EO_North;
        }
        return dir;
    }

    inline EOrientation TurnRight(EOrientation dir)
    {
        switch (dir)
        {
        case EOrientation::EO_North: return EOrientation::EO_East;
        case EOrientation::EO_East:  return EOrientation::EO_South;
        case EOrientation::EO_South: return EOrientation::EO_West;
        case EOrientation::EO_West:  return EOrientation::EO_North;
        }
        return dir;
    }

    inline EOrientation TurnAround(EOrientation dir)
    {
        switch (dir)
        {
        case EOrientation::EO_North: return EOrientation::EO_South;
        case EOrientation::EO_East:  return EOrientation::EO_West;
        case EOrientation::EO_South: return EOrientation::EO_North;
        case EOrientation::EO_West:  return EOrientation::EO_East;
        }
        return dir;
    }

    // ==================== FCursor ====================

    struct FCursor
    {
        glm::i16vec3 position{0, 0, 0};
        EOrientation facing = EOrientation::EO_North;

        // Movement methods
        void MoveForward(int steps = 1)
        {
            auto dir = GetDirectionVector(facing);
            position += glm::i16vec3(dir.x * steps, dir.y * steps, dir.z * steps);
        }

        void MoveBackward(int steps = 1)
        {
            auto dir = GetDirectionVector(facing);
            position -= glm::i16vec3(dir.x * steps, dir.y * steps, dir.z * steps);
        }

        void MoveLeft(int steps = 1)
        {
            auto leftDir = GetDirectionVector(MagicaLego::TurnLeft(facing));
            position += glm::i16vec3(leftDir.x * steps, leftDir.y * steps, leftDir.z * steps);
        }

        void MoveRight(int steps = 1)
        {
            auto rightDir = GetDirectionVector(MagicaLego::TurnRight(facing));
            position += glm::i16vec3(rightDir.x * steps, rightDir.y * steps, rightDir.z * steps);
        }

        void MoveUp(int steps = 1)
        {
            position.y += static_cast<int16_t>(steps);
        }

        void MoveDown(int steps = 1)
        {
            position.y -= static_cast<int16_t>(steps);
        }

        // Turn methods
        void TurnLeft()
        {
            facing = MagicaLego::TurnLeft(facing);
        }

        void TurnRight()
        {
            facing = MagicaLego::TurnRight(facing);
        }

        void TurnAround()
        {
            facing = MagicaLego::TurnAround(facing);
        }

        // Position calculation
        glm::i16vec3 GetPositionOffset(int forward, int right, int up) const
        {
            auto fwdDir = GetDirectionVector(facing);
            auto rightDir = GetDirectionVector(MagicaLego::TurnRight(facing));

            return position + glm::i16vec3(
                fwdDir.x * forward + rightDir.x * right,
                up,
                fwdDir.z * forward + rightDir.z * right
            );
        }
    };

    // ==================== FScanResult ====================

    struct FScanEntry
    {
        glm::i16vec3 relativePos;
        std::string blockType;
        int colorIndex;
    };

    struct FScanResult
    {
        std::vector<FScanEntry> blocks;
        glm::i16vec3 cursorPos;
        EOrientation cursorFacing;

        std::string ToJson() const
        {
            nlohmann::json j;
            j["cursorPos"] = {cursorPos.x, cursorPos.y, cursorPos.z};

            std::string facingStr;
            switch (cursorFacing)
            {
            case EOrientation::EO_North: facingStr = "north"; break;
            case EOrientation::EO_East: facingStr = "east"; break;
            case EOrientation::EO_South: facingStr = "south"; break;
            case EOrientation::EO_West: facingStr = "west"; break;
            }
            j["cursorFacing"] = facingStr;

            j["blocks"] = nlohmann::json::array();
            for (const auto& entry : blocks)
            {
                nlohmann::json blockJson;
                blockJson["relPos"] = {entry.relativePos.x, entry.relativePos.y, entry.relativePos.z};
                blockJson["type"] = entry.blockType;
                blockJson["color"] = entry.colorIndex;
                j["blocks"].push_back(blockJson);
            }

            return j.dump(2);
        }
    };

    // ==================== Command System ====================

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

    // Position mode for relative coordinates
    enum class EPositionMode
    {
        Absolute,    // x y z
        Here,        // at cursor position
        Ahead,       // ahead [n] (n steps in front of cursor)
        Relative     // +x +y +z (relative to cursor)
    };

    class FPlaceCommand : public ICommand
    {
    public:
        // Absolute coordinate constructor
        FPlaceCommand(std::string type, std::string color, glm::i16vec3 pos, EOrientation orient)
            : type_(std::move(type)), color_(std::move(color)), position_(pos), orientation_(orient),
              positionMode_(EPositionMode::Absolute), aheadDistance_(0) {}

        // Relative coordinate constructor
        FPlaceCommand(std::string type, std::string color, EPositionMode mode, glm::i16vec3 offset, int aheadDist, EOrientation orient)
            : type_(std::move(type)), color_(std::move(color)), position_(offset), orientation_(orient),
              positionMode_(mode), aheadDistance_(aheadDist) {}

        FCommandResult Execute(MagicaLegoGameInstance* gi) override;
        std::string GetName() const override { return "place"; }

    private:
        std::string type_;
        std::string color_;
        glm::i16vec3 position_;
        EOrientation orientation_;
        EPositionMode positionMode_ = EPositionMode::Absolute;
        int aheadDistance_ = 0;
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

    // ==================== Cursor Commands ====================

    class FMoveCommand : public ICommand
    {
    public:
        FMoveCommand(std::string direction, int steps)
            : direction_(std::move(direction)), steps_(steps) {}

        FCommandResult Execute(MagicaLegoGameInstance* gi) override;
        std::string GetName() const override { return "move"; }

    private:
        std::string direction_;
        int steps_;
    };

    class FTurnCommand : public ICommand
    {
    public:
        explicit FTurnCommand(std::string direction)
            : direction_(std::move(direction)) {}

        FCommandResult Execute(MagicaLegoGameInstance* gi) override;
        std::string GetName() const override { return "turn"; }

    private:
        std::string direction_;
    };

    class FGotoCommand : public ICommand
    {
    public:
        FGotoCommand(glm::i16vec3 pos) : position_(pos) {}

        FCommandResult Execute(MagicaLegoGameInstance* gi) override;
        std::string GetName() const override { return "goto"; }

    private:
        glm::i16vec3 position_;
    };

    class FFaceCommand : public ICommand
    {
    public:
        explicit FFaceCommand(EOrientation dir) : direction_(dir) {}

        FCommandResult Execute(MagicaLegoGameInstance* gi) override;
        std::string GetName() const override { return "face"; }

    private:
        EOrientation direction_;
    };

    class FScanCommand : public ICommand
    {
    public:
        explicit FScanCommand(int radius = 3) : radius_(radius) {}

        FCommandResult Execute(MagicaLegoGameInstance* gi) override;
        std::string GetName() const override { return "scan"; }

    private:
        int radius_;
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
