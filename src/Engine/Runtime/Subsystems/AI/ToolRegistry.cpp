#include "Engine/Runtime/Subsystems/AI/ToolRegistry.hpp"

namespace NextAI
{
    void FToolRegistry::Register(std::unique_ptr<IAITool> tool)
    {
        if (!tool) return;
        const std::string name = tool->Name();
        if (name.empty()) return;
        tools_[name] = std::move(tool);
    }

    IAITool* FToolRegistry::Find(const std::string& name) const
    {
        auto it = tools_.find(name);
        return it == tools_.end() ? nullptr : it->second.get();
    }

    std::vector<FToolSchema> FToolRegistry::BuildSchemas() const
    {
        std::vector<FToolSchema> out;
        out.reserve(tools_.size());
        for (const auto& [name, tool] : tools_)
        {
            out.push_back(tool->Schema());
        }
        return out;
    }
}
