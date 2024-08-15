// NodeSetInt.hpp
#ifndef NODES_SET_INT_H // include guard
#define NODES_SET_INT_H

#include "../../ImNodeFlow/include/ImNodeFlow.h"

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

} // namespace Nodes
#endif // NODES_SET_INT_H