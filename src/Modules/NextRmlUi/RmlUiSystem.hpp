#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/UiOverlay.hpp"

#include <functional>
#include <string>

union SDL_Event;

namespace Rml
{
    class Element;
    class ElementDocument;
}

class NextEngine;

namespace NextUI
{
    class RmlUiSystem final : public Runtime::IUiOverlay
    {
    public:
        GK_NON_COPIABLE(RmlUiSystem)

        explicit RmlUiSystem(NextEngine& engine);
        ~RmlUiSystem() override;

        bool IsAvailable() const;
        bool HandleEvent(const SDL_Event& event) override;
        void BeginFrame() override;
        void RenderFrame() override;
        void Shutdown();

        Rml::ElementDocument* EnsureDocument(const std::string& documentId, const std::string& rmlSource);
        Rml::ElementDocument* ReplaceDocument(const std::string& documentId, const std::string& rmlSource);
        Rml::Element* GetElementById(const std::string& documentId, const std::string& elementId);
        void SetDocumentVisible(const std::string& documentId, bool visible);
        void ClearEventListeners();
        bool ListenClick(const std::string& documentId, const std::string& elementId, std::function<void()> callback);

        bool WantsToCaptureMouse() const override;
        bool WantsToCaptureKeyboard() const override;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
