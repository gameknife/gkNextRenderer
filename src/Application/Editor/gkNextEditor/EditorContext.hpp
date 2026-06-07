#pragma once

#include "Engine/Common/CoreMinimal.hpp"

class NextEngine;
class EditorActionDispatcher;

namespace NextUI
{
    class UserInterface;
    class GizmoController;
}

namespace Assets
{
    class Scene;
}

struct EditorContext final
{
    NextEngine& engine;
    Assets::Scene& scene;
    NextUI::UserInterface& ui;
    EditorActionDispatcher& actions;
    NextUI::GizmoController* gizmoController = nullptr;
};
