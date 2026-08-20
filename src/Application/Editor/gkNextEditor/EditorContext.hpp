#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Core/EditorSettings.hpp"

class NextEngine;
class EditorActionDispatcher;
class EditorGameInstance;

namespace NextUI
{
    class IUserInterface;
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
    NextUI::IUserInterface& ui;
    EditorActionDispatcher& actions;
    Editor::EditorSettings& settings;
    NextUI::GizmoController* gizmoController = nullptr;
    EditorGameInstance* editor = nullptr;
};
