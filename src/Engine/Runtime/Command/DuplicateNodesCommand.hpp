#pragma once

#include "Engine/Runtime/Command/ICommand.hpp"

#include <memory>
#include <string>
#include <vector>

namespace Assets
{
    class Scene;
    class Node;
}

class DuplicateNodesCommand final : public ICommand
{
public:
    DuplicateNodesCommand(Assets::Scene& scene, std::vector<uint32_t> sourceIds);

    bool Execute() override;
    bool Undo() override;
    std::string GetDescription() const override;

    const std::vector<uint32_t>& GetNewInstanceIds() const { return newInstanceIds_; }

private:
    void InitializeSourceSelection();
    void RestoreSelection() const;

    struct FDuplicateEntry
    {
        uint32_t sourceInstanceId = static_cast<uint32_t>(-1);
        uint32_t newInstanceId = static_cast<uint32_t>(-1);
        std::shared_ptr<Assets::Node> parent{};
        std::shared_ptr<Assets::Node> newNode{};
    };

    Assets::Scene* scene_ = nullptr;
    std::vector<uint32_t> sourceIds_;
    std::vector<uint32_t> uniqueSourceIds_;
    std::vector<FDuplicateEntry> duplicateEntries_;
    std::vector<uint32_t> newInstanceIds_;
    std::vector<uint32_t> previousSelection_;
    uint32_t previousSelectedId_ = static_cast<uint32_t>(-1);
    bool initialized_ = false;
};
