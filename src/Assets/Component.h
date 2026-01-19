#pragma once
#include <memory>
#include <string>

namespace Assets
{
    class Node;

    class Component : public std::enable_shared_from_this<Component>
    {
    public:
        virtual ~Component() = default;
        
        void SetOwner(Node* owner) { owner_ = owner; }
        Node* GetOwner() const { return owner_; }

    protected:
        Node* owner_ = nullptr;
    };
}
