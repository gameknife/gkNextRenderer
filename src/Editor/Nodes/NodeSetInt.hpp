// NodeSetInt.hpp
#ifndef NODES_SET_INT_H // include guard
#define NODES_SET_INT_H

#include "../../ImNodeFlow/include/ImNodeFlow.h"
#include "Vulkan/DescriptorSetLayout.hpp"

namespace Nodes
{
    class NodeSetInt : public ImFlow::BaseNode
    {
    public:
        explicit NodeSetInt(const std::string name, int initValue);

        void draw() override;

    private:
        int value = 0;
    };

    class NodeSetTexture : public ImFlow::BaseNode
    {
    public:
        explicit NodeSetTexture(const std::string name, int initTextureId);
        ~NodeSetTexture();
        void draw() override;

    private:
        int textureId = 0;
        VkDescriptorSet imTextureId;
    };

} // namespace Nodes
#endif // NODES_SET_INT_H