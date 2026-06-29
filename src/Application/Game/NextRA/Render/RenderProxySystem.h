#pragma once

#include "Engine/Assets/AssetsFwd.hpp"
#include "Sim/SimWorld.h"

#include <memory>
#include <unordered_map>

namespace NextRA
{
    class FRenderProxySystem
    {
    public:
        void Clear();
        void BindNode(uint32_t renderNodeId, std::shared_ptr<Assets::Node> node);
        void Sync(const Sim::FSimWorld& world, float alpha);

    private:
        std::unordered_map<uint32_t, std::weak_ptr<Assets::Node>> nodesByRenderId_;
    };
}
