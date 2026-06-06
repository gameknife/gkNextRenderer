#pragma once

#include "Engine/Common/CoreMinimal.hpp"

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
    class RmlUiSystem final
    {
    public:
        GK_NON_COPIABLE(RmlUiSystem)

        explicit RmlUiSystem(NextEngine& engine);
        ~RmlUiSystem();

        bool IsAvailable() const;
        bool HandleEvent(const SDL_Event& event);
        void BeginFrame();
        void RenderFrame();
        void Shutdown();

        Rml::ElementDocument* EnsureDocument(const std::string& documentId, const std::string& rmlSource);
        Rml::ElementDocument* ReplaceDocument(const std::string& documentId, const std::string& rmlSource);
        Rml::Element* GetElementById(const std::string& documentId, const std::string& elementId);
        void SetDocumentVisible(const std::string& documentId, bool visible);
        void ClearEventListeners();
        bool ListenClick(const std::string& documentId, const std::string& elementId, std::function<void()> callback);

        bool WantsToCaptureMouse() const;
        bool WantsToCaptureKeyboard() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
