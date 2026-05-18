#include "MagicaLegoCommands.hpp"
#include "MagicaLegoScriptParser.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>

namespace MagicaLego
{
    // ==================== FPlaceCommand ====================

    FCommandResult FPlaceCommand::Execute(MagicaLegoGameInstance* gi)
    {
        if (!gi)
        {
            return FCommandResult::Failure("GameInstance is null");
        }

        auto* block = gi->GetBasicBlockBySpec(type_, color_);
        if (!block)
        {
            return FCommandResult::Failure(fmt::format("Block not found: {}/{}", type_, color_));
        }

        // Calculate actual position based on mode
        glm::i16vec3 actualPosition = position_;
        auto& cursor = gi->GetCursor();

        switch (positionMode_)
        {
        case EPositionMode::Absolute:
            // Use position_ directly
            break;

        case EPositionMode::Here:
            actualPosition = cursor.position;
            break;

        case EPositionMode::Ahead:
            {
                auto dir = GetDirectionVector(cursor.facing);
                actualPosition = cursor.position + glm::i16vec3(
                    dir.x * aheadDistance_,
                    dir.y * aheadDistance_,
                    dir.z * aheadDistance_);
            }
            break;

        case EPositionMode::Relative:
            // position_ contains the offset in cursor-relative coordinates (forward, right, up)
            actualPosition = cursor.GetPositionOffset(position_.x, position_.z, position_.y);
            break;
        }

        FPlacedBlock placedBlock{};
        placedBlock.location = actualPosition;
        placedBlock.orientation = orientation_;
        placedBlock.modelId_ = block->brushId_;

        if (gi->PlaceDynamicBlock(placedBlock))
        {
            return FCommandResult::Success(
                fmt::format("Placed {}/{} at ({}, {}, {})",
                    type_, color_, actualPosition.x, actualPosition.y, actualPosition.z));
        }
        else
        {
            return FCommandResult::Failure(
                fmt::format("Failed to place block at ({}, {}, {})",
                    actualPosition.x, actualPosition.y, actualPosition.z));
        }
    }

    // ==================== FDigCommand ====================

    FCommandResult FDigCommand::Execute(MagicaLegoGameInstance* gi)
    {
        if (!gi)
        {
            return FCommandResult::Failure("GameInstance is null");
        }

        if (!gi->HasBlockAt(position_))
        {
            return FCommandResult::Failure(
                fmt::format("No block at ({}, {}, {})", position_.x, position_.y, position_.z));
        }

        FPlacedBlock placedBlock{};
        placedBlock.location = position_;
        placedBlock.orientation = EOrientation::EO_North;
        placedBlock.modelId_ = -1;

        if (gi->PlaceDynamicBlock(placedBlock))
        {
            return FCommandResult::Success(
                fmt::format("Removed block at ({}, {}, {})",
                    position_.x, position_.y, position_.z));
        }
        else
        {
            return FCommandResult::Failure(
                fmt::format("Failed to remove block at ({}, {}, {})",
                    position_.x, position_.y, position_.z));
        }
    }

    // ==================== FListCommand ====================

    FCommandResult FListCommand::Execute(MagicaLegoGameInstance* gi)
    {
        if (!gi)
        {
            return FCommandResult::Failure("GameInstance is null");
        }

        std::vector<std::string> output;

        if (target_ == EListTarget::Types)
        {
            auto types = gi->GetAllBlockTypes();
            output = std::move(types);
            return FCommandResult::Success(
                fmt::format("Found {} types", output.size()), output);
        }
        else
        {
            auto colors = gi->GetAllBlockColors(typeFilter_);
            output = std::move(colors);
            if (output.empty() && !typeFilter_.empty())
            {
                return FCommandResult::Failure(
                    fmt::format("Unknown type: {}", typeFilter_));
            }
            return FCommandResult::Success(
                fmt::format("Found {} colors for {}", output.size(),
                    typeFilter_.empty() ? "all types" : typeFilter_), output);
        }
    }

    // ==================== Cursor Commands Implementation ====================

    FCommandResult FMoveCommand::Execute(MagicaLegoGameInstance* gi)
    {
        if (!gi)
        {
            return FCommandResult::Failure("GameInstance is null");
        }

        auto& cursor = gi->GetCursor();
        auto oldPos = cursor.position;

        std::string dir = direction_;
        std::transform(dir.begin(), dir.end(), dir.begin(),
            [](unsigned char c) { return std::tolower(c); });

        if (dir == "forward" || dir == "f")
        {
            cursor.MoveForward(steps_);
        }
        else if (dir == "backward" || dir == "back" || dir == "b")
        {
            cursor.MoveBackward(steps_);
        }
        else if (dir == "left" || dir == "l")
        {
            cursor.MoveLeft(steps_);
        }
        else if (dir == "right" || dir == "r")
        {
            cursor.MoveRight(steps_);
        }
        else if (dir == "up" || dir == "u")
        {
            cursor.MoveUp(steps_);
        }
        else if (dir == "down" || dir == "d")
        {
            cursor.MoveDown(steps_);
        }
        else
        {
            return FCommandResult::Failure(fmt::format("Unknown direction: {}", direction_));
        }

        return FCommandResult::Success(
            fmt::format("Moved {} {} from ({},{},{}) to ({},{},{})",
                direction_, steps_,
                oldPos.x, oldPos.y, oldPos.z,
                cursor.position.x, cursor.position.y, cursor.position.z));
    }

    FCommandResult FTurnCommand::Execute(MagicaLegoGameInstance* gi)
    {
        if (!gi)
        {
            return FCommandResult::Failure("GameInstance is null");
        }

        auto& cursor = gi->GetCursor();
        auto oldFacing = cursor.facing;

        std::string dir = direction_;
        std::transform(dir.begin(), dir.end(), dir.begin(),
            [](unsigned char c) { return std::tolower(c); });

        if (dir == "left" || dir == "l")
        {
            cursor.TurnLeft();
        }
        else if (dir == "right" || dir == "r")
        {
            cursor.TurnRight();
        }
        else if (dir == "around" || dir == "back")
        {
            cursor.TurnAround();
        }
        else
        {
            return FCommandResult::Failure(fmt::format("Unknown turn direction: {}", direction_));
        }

        return FCommandResult::Success(fmt::format("Turned {} (now facing {})", direction_, cursor.facing));
    }

    FCommandResult FGotoCommand::Execute(MagicaLegoGameInstance* gi)
    {
        if (!gi)
        {
            return FCommandResult::Failure("GameInstance is null");
        }

        auto& cursor = gi->GetCursor();
        cursor.position = position_;

        return FCommandResult::Success(
            fmt::format("Moved cursor to ({},{},{})",
                position_.x, position_.y, position_.z));
    }

    FCommandResult FFaceCommand::Execute(MagicaLegoGameInstance* gi)
    {
        if (!gi)
        {
            return FCommandResult::Failure("GameInstance is null");
        }

        auto& cursor = gi->GetCursor();
        cursor.facing = direction_;

        return FCommandResult::Success(fmt::format("Now facing {}", direction_));
    }

    FCommandResult FScanCommand::Execute(MagicaLegoGameInstance* gi)
    {
        if (!gi)
        {
            return FCommandResult::Failure("GameInstance is null");
        }

        auto& cursor = gi->GetCursor();
        FScanResult result;
        result.cursorPos = cursor.position;
        result.cursorFacing = cursor.facing;

        // Scan blocks within radius
        for (int16_t dx = -radius_; dx <= radius_; dx++)
        {
            for (int16_t dy = -radius_; dy <= radius_; dy++)
            {
                for (int16_t dz = -radius_; dz <= radius_; dz++)
                {
                    glm::i16vec3 checkPos = cursor.position + glm::i16vec3(dx, dy, dz);
                    if (gi->HasBlockAt(checkPos))
                    {
                        FScanEntry entry;
                        entry.relativePos = glm::i16vec3(dx, dy, dz);
                        entry.blockType = "block"; // Could be enhanced to get actual type
                        entry.colorIndex = 0;
                        result.blocks.push_back(entry);
                    }
                }
            }
        }

        std::string jsonResult = result.ToJson();
        return FCommandResult::Success(
            fmt::format("Scanned {} blocks within radius {}", result.blocks.size(), radius_),
            {jsonResult});
    }

    // ==================== FCommandParser ====================

    std::vector<std::string> FCommandParser::Tokenize(const std::string& line)
    {
        std::vector<std::string> tokens;
        std::istringstream iss(line);
        std::string token;
        while (iss >> token)
        {
            tokens.push_back(token);
        }
        return tokens;
    }

    EOrientation FCommandParser::ParseOrientation(const std::string& str)
    {
        std::string lower = str;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char c) { return std::tolower(c); });

        if (lower == "north" || lower == "n") return EOrientation::EO_North;
        if (lower == "east" || lower == "e") return EOrientation::EO_East;
        if (lower == "south" || lower == "s") return EOrientation::EO_South;
        if (lower == "west" || lower == "w") return EOrientation::EO_West;

        return EOrientation::EO_North;
    }

    bool FCommandParser::ParseTypeColor(const std::string& spec, std::string& outType, std::string& outColor)
    {
        auto slashPos = spec.find('/');
        if (slashPos == std::string::npos)
        {
            return false;
        }

        outType = spec.substr(0, slashPos);
        outColor = spec.substr(slashPos + 1);

        return !outType.empty() && !outColor.empty();
    }

    std::unique_ptr<ICommand> FCommandParser::Parse(const std::string& line, std::string& error)
    {
        auto tokens = Tokenize(line);
        if (tokens.empty())
        {
            error = "Empty command";
            return nullptr;
        }

        std::string cmd = tokens[0];
        std::transform(cmd.begin(), cmd.end(), cmd.begin(),
            [](unsigned char c) { return std::tolower(c); });

        // place <type>/<color> <position> [orientation]
        // Position can be:
        //   x y z         - absolute coordinates
        //   here          - at cursor position
        //   ahead [n]     - n steps ahead of cursor (default 1)
        if (cmd == "place")
        {
            if (tokens.size() < 3)
            {
                error = "Usage: place <type>/<color> <x> <y> <z> | here | ahead [n] [orientation]";
                return nullptr;
            }

            std::string type, color;
            if (!ParseTypeColor(tokens[1], type, color))
            {
                error = "Invalid type/color format. Use: Type/Color (e.g., Block1x1/#0)";
                return nullptr;
            }

            std::string posToken = tokens[2];
            std::transform(posToken.begin(), posToken.end(), posToken.begin(),
                [](unsigned char c) { return std::tolower(c); });

            // Check for relative position keywords
            if (posToken == "here")
            {
                EOrientation orient = EOrientation::EO_North;
                if (tokens.size() > 3)
                {
                    orient = ParseOrientation(tokens[3]);
                }
                return std::make_unique<FPlaceCommand>(
                    type, color, EPositionMode::Here, glm::i16vec3(0), 0, orient);
            }
            else if (posToken == "ahead")
            {
                int distance = 1;
                size_t orientIndex = 3;
                if (tokens.size() > 3)
                {
                    try
                    {
                        distance = std::stoi(tokens[3]);
                        orientIndex = 4;
                    }
                    catch (const std::exception&)
                    {
                        // Not a number, assume it's orientation
                    }
                }
                EOrientation orient = EOrientation::EO_North;
                if (tokens.size() > orientIndex)
                {
                    orient = ParseOrientation(tokens[orientIndex]);
                }
                return std::make_unique<FPlaceCommand>(
                    type, color, EPositionMode::Ahead, glm::i16vec3(0), distance, orient);
            }
            else
            {
                // Absolute coordinates: place Type/#Color x y z [orient]
                if (tokens.size() < 5)
                {
                    error = "Usage: place <type>/<color> <x> <y> <z> [orientation]";
                    return nullptr;
                }

                try
                {
                    int x = std::stoi(tokens[2]);
                    int y = std::stoi(tokens[3]);
                    int z = std::stoi(tokens[4]);

                    EOrientation orient = EOrientation::EO_North;
                    if (tokens.size() > 5)
                    {
                        orient = ParseOrientation(tokens[5]);
                    }

                    return std::make_unique<FPlaceCommand>(
                        type, color,
                        glm::i16vec3(static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(z)),
                        orient);
                }
                catch (const std::exception&)
                {
                    error = "Invalid coordinates. Must be integers.";
                    return nullptr;
                }
            }
        }

        // dig <x> <y> <z>
        if (cmd == "dig")
        {
            if (tokens.size() < 4)
            {
                error = "Usage: dig <x> <y> <z>";
                return nullptr;
            }

            try
            {
                int x = std::stoi(tokens[1]);
                int y = std::stoi(tokens[2]);
                int z = std::stoi(tokens[3]);

                return std::make_unique<FDigCommand>(
                    glm::i16vec3(static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(z)));
            }
            catch (const std::exception&)
            {
                error = "Invalid coordinates. Must be integers.";
                return nullptr;
            }
        }

        // list types | list colors [type]
        if (cmd == "list")
        {
            if (tokens.size() < 2)
            {
                error = "Usage: list types | list colors [type]";
                return nullptr;
            }

            std::string subCmd = tokens[1];
            std::transform(subCmd.begin(), subCmd.end(), subCmd.begin(),
                [](unsigned char c) { return std::tolower(c); });

            if (subCmd == "types")
            {
                return std::make_unique<FListCommand>(FListCommand::EListTarget::Types);
            }
            else if (subCmd == "colors")
            {
                std::string typeFilter = tokens.size() > 2 ? tokens[2] : "";
                return std::make_unique<FListCommand>(FListCommand::EListTarget::Colors, typeFilter);
            }
            else
            {
                error = "Unknown list target. Use: types or colors";
                return nullptr;
            }
        }

        // move <direction> [steps]
        if (cmd == "move")
        {
            if (tokens.size() < 2)
            {
                error = "Usage: move <forward|backward|left|right|up|down> [steps]";
                return nullptr;
            }

            std::string direction = tokens[1];
            int steps = 1;

            if (tokens.size() > 2)
            {
                try
                {
                    steps = std::stoi(tokens[2]);
                }
                catch (const std::exception&)
                {
                    error = "Invalid step count. Must be an integer.";
                    return nullptr;
                }
            }

            return std::make_unique<FMoveCommand>(direction, steps);
        }

        // turn <left|right|around>
        if (cmd == "turn")
        {
            if (tokens.size() < 2)
            {
                error = "Usage: turn <left|right|around>";
                return nullptr;
            }

            return std::make_unique<FTurnCommand>(tokens[1]);
        }

        // goto <x> <y> <z>
        if (cmd == "goto")
        {
            if (tokens.size() < 4)
            {
                error = "Usage: goto <x> <y> <z>";
                return nullptr;
            }

            try
            {
                int x = std::stoi(tokens[1]);
                int y = std::stoi(tokens[2]);
                int z = std::stoi(tokens[3]);

                return std::make_unique<FGotoCommand>(
                    glm::i16vec3(static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(z)));
            }
            catch (const std::exception&)
            {
                error = "Invalid coordinates. Must be integers.";
                return nullptr;
            }
        }

        // face <north|east|south|west>
        if (cmd == "face")
        {
            if (tokens.size() < 2)
            {
                error = "Usage: face <north|east|south|west>";
                return nullptr;
            }

            EOrientation dir = ParseOrientation(tokens[1]);
            return std::make_unique<FFaceCommand>(dir);
        }

        // scan [radius]
        if (cmd == "scan")
        {
            int radius = 3;
            if (tokens.size() > 1)
            {
                try
                {
                    radius = std::stoi(tokens[1]);
                    radius = std::clamp(radius, 1, 10);
                }
                catch (const std::exception&)
                {
                    error = "Invalid radius. Must be an integer.";
                    return nullptr;
                }
            }

            return std::make_unique<FScanCommand>(radius);
        }

        error = fmt::format("Unknown command: {}", tokens[0]);
        return nullptr;
    }

    // ==================== FCommandExecutor ====================

    FCommandResult FCommandExecutor::ExecuteCommand(const std::string& line)
    {
        std::string trimmedLine = line;

        // Skip empty lines and comments
        auto firstNonSpace = trimmedLine.find_first_not_of(" \t\r\n");
        if (firstNonSpace == std::string::npos)
        {
            return FCommandResult::Success();
        }
        trimmedLine = trimmedLine.substr(firstNonSpace);

        if (trimmedLine.empty() || trimmedLine[0] == '#')
        {
            return FCommandResult::Success();
        }

        // Add to history
        history_.push_back(trimmedLine);
        historyIndex_ = static_cast<int>(history_.size());

        std::string error;
        auto command = FCommandParser::Parse(trimmedLine, error);
        if (!command)
        {
            return FCommandResult::Failure(error);
        }

        return command->Execute(gameInstance_);
    }

    FCommandResult FCommandExecutor::ExecuteScript(const std::string& path)
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            return FCommandResult::Failure(fmt::format("Failed to open file: {}", path));
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        return ExecuteScriptText(buffer.str());
    }

    FCommandResult FCommandExecutor::ExecuteScriptText(const std::string& text)
    {
        FScriptParser parser;
        std::string error;
        auto commands = parser.Parse(text, error);

        if (!error.empty())
        {
            return FCommandResult::Failure(error);
        }

        int executed = 0;
        int failed = 0;
        std::vector<std::string> output;

        for (const auto& line : commands)
        {
            std::string parseError;
            auto command = FCommandParser::Parse(line, parseError);
            if (!command)
            {
                output.push_back(fmt::format("[SKIP] {}: {}", line, parseError));
                continue;
            }

            auto result = command->Execute(gameInstance_);
            if (result.success)
            {
                executed++;
                if (!result.message.empty())
                {
                    output.push_back(fmt::format("[OK] {}", result.message));
                }
            }
            else
            {
                failed++;
                output.push_back(fmt::format("[FAIL] {}: {}", line, result.message));
            }
        }

        return FCommandResult::Success(
            fmt::format("Script complete: {} executed, {} failed", executed, failed),
            output);
    }

    std::string FCommandExecutor::GetHistoryPrev()
    {
        if (history_.empty())
        {
            return "";
        }

        if (historyIndex_ > 0)
        {
            historyIndex_--;
        }

        return history_[historyIndex_];
    }

    std::string FCommandExecutor::GetHistoryNext()
    {
        if (history_.empty())
        {
            return "";
        }

        if (historyIndex_ < static_cast<int>(history_.size()) - 1)
        {
            historyIndex_++;
            return history_[historyIndex_];
        }
        else
        {
            historyIndex_ = static_cast<int>(history_.size());
            return "";
        }
    }
}
