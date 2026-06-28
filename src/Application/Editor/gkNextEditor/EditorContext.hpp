#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Core/EditorSettings.hpp"

class NextEngine;
class EditorActionDispatcher;
class EditorGameInstance;

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
    Editor::EditorSettings& settings;
    NextUI::GizmoController* gizmoController = nullptr;
    EditorGameInstance* editor = nullptr;
};
