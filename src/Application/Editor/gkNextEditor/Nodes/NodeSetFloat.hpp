#pragma once

#include <glm/vec3.hpp>

#include "ImNodeFlow.h"

namespace Nodes
{
    class NodeSetFloat : public ImFlow::BaseNode
    {
    public:
        explicit NodeSetFloat(const std::string name, float initValue);

        void draw() override;

    private:
        float value1 = 0.0;
        float value2 = 0.0;
    };

    class NodeSetColor : public ImFlow::BaseNode
    {
    public:
        explicit NodeSetColor(const std::string name, glm::vec3 initValue);

        void draw() override;

    private:
        glm::vec3 value = glm::vec3(0.0f);
    };
} // namespace Nodes
