#include "MagicaLegoCommands.hpp"
#include "MagicaLegoScriptParser.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

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

        FPlacedBlock placedBlock{};
        placedBlock.location = position_;
        placedBlock.orientation = orientation_;
        placedBlock.modelId_ = block->brushId_;

        if (gi->PlaceDynamicBlock(placedBlock))
        {
            return FCommandResult::Success(
                fmt::format("Placed {}/{} at ({}, {}, {})",
                    type_, color_, position_.x, position_.y, position_.z));
        }
        else
        {
            return FCommandResult::Failure(
                fmt::format("Failed to place block at ({}, {}, {})",
                    position_.x, position_.y, position_.z));
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

        // place <type>/<color> <x> <y> <z> [orientation]
        if (cmd == "place")
        {
            if (tokens.size() < 5)
            {
                error = "Usage: place <type>/<color> <x> <y> <z> [orientation]";
                return nullptr;
            }

            std::string type, color;
            if (!ParseTypeColor(tokens[1], type, color))
            {
                error = "Invalid type/color format. Use: Type/Color (e.g., Block1x1/#0)";
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
