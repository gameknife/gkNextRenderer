#pragma once

#include "Common/CoreMinimal.hpp"

#include "Editor/EditorContext.hpp"

#include <functional>
#include <string_view>
#include <unordered_map>

enum class EEditorAction
{
    System_RequestExit,
    System_RequestMinimize,
    System_ToggleMaximize,

    IO_LoadScene,
    IO_LoadSceneAdd,
    IO_LoadHDRI,
};

class EditorActionDispatcher final
{
public:
    using ActionFn = std::function<bool(EditorContext& ctx, std::string_view args)>;

    void RegisterAction(EEditorAction action, ActionFn fn) { actions_[action] = std::move(fn); }

    bool Dispatch(EditorContext& ctx, EEditorAction action, std::string_view args = {}) const
    {
        auto it = actions_.find(action);
        if (it == actions_.end() || !it->second)
        {
            return false;
        }
        return it->second(ctx, args);
    }

private:
    struct EnumHash
    {
        size_t operator()(EEditorAction v) const noexcept { return static_cast<size_t>(v); }
    };

    std::unordered_map<EEditorAction, ActionFn, EnumHash> actions_;
};
