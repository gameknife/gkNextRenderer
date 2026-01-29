#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <entt/meta/meta.hpp>

namespace Assets
{
    class Node;

    class Component : public std::enable_shared_from_this<Component>
    {
    public:
        virtual ~Component() = default;
        
        // Get component type name for reflection lookup
        virtual std::string_view GetTypeName() const = 0;
        
        // Get entt meta type for property access
        virtual entt::meta_type GetMetaType() const = 0;
        
        void SetOwner(Node* owner) { owner_ = owner; }
        Node* GetOwner() const { return owner_; }

    protected:
        Node* owner_ = nullptr;
    };
}
