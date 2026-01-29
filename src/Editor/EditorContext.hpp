#pragma once

#include "Common/CoreMinimal.hpp"

class NextEngine;
class UserInterface;
class EditorActionDispatcher;

namespace Assets
{
    class Scene;
}

struct EditorContext final
{
    NextEngine& engine;
    Assets::Scene& scene;
    UserInterface& ui;
    EditorActionDispatcher& actions;
};
