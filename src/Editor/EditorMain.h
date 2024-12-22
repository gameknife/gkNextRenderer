#pragma once

#include "EditorGUI.h"
#include "Runtime/Application.hpp"

class EditorInterface;

namespace Assets
{
    class Scene;    
}

class EditorGameInstance : public NextGameInstanceBase
{
public:
    EditorGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine);
    ~EditorGameInstance() override = default;

    // overrides
    void OnInit() override;
    void OnTick(double deltaSeconds) override;
    void OnDestroy() override {};

    void OnPreConfigUI() override;
    bool OnRenderUI() override;
    void OnInitUI() override;

    bool OnKey(int key, int scancode, int action, int mods) override;
    bool OnCursorPosition(double xpos, double ypos) override;
    bool OnMouseButton(int button, int action, int mods) override;
    
    // quick access engine
    NextEngine& GetEngine() { return *engine_; }

private:
    NextEngine* engine_;

    std::unique_ptr<EditorInterface> editorUserInterface_;
};