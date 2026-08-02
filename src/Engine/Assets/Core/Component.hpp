#pragma once
#include "Engine/Assets/AssetsFwd.hpp"
#include <string>
#include <string_view>
#include <entt/meta/meta.hpp>

namespace Assets
{
    template <typename T>
    constexpr entt::id_type ComponentTypeId()
    {
        return entt::type_hash<T>::value();
    }

    class Component
    {
    public:
        virtual ~Component() = default;
        
        // Get component type name for reflection lookup
        virtual std::string_view GetTypeName() const = 0;

        // Fast type id for component lookup without RTTI.
        virtual entt::id_type GetTypeId() const = 0;
        
        // Get entt meta type for property access
        virtual entt::meta_type GetMetaType() const = 0;
        
        Node* GetOwner() { return owner_; }
        const Node* GetOwner() const { return owner_; }

    private:
        friend class Node;

        void SetOwner(Node* owner) { owner_ = owner; }
        Node* owner_ = nullptr;
    };
}
