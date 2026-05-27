#pragma once

#include "Engine/Runtime/Subsystems/AI/IAITool.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace NextAI
{
    class FToolRegistry
    {
    public:
        // Register a tool. Replaces any existing tool with the same name.
        void Register(std::unique_ptr<IAITool> tool);

        IAITool* Find(const std::string& name) const;

        // Schemas suitable for inclusion in FChatRequest::tools.
        std::vector<FToolSchema> BuildSchemas() const;

        size_t Size() const { return tools_.size(); }
        bool Empty() const { return tools_.empty(); }

        void Clear() { tools_.clear(); }

    private:
        std::unordered_map<std::string, std::unique_ptr<IAITool>> tools_;
    };
}
