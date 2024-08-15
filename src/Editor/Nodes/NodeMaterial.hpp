#pragma once
#include "ImNodeFlow.h"

namespace Nodes
{
    class NodeMaterial : public ImFlow::BaseNode
    {
    public:
        NodeMaterial();

        void draw() override;

    private:
    };

} // namespace Nodes
