#include "Engine/Runtime/UI/RmlUiSystem.hpp"

#include "Engine/Runtime/Engine.hpp"

#if GK_WITH_RMLUI
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Assets/GPU/TextureImage.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Utilities/StbImage.hpp"
#include "Engine/Vulkan/WindowSurface.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/TextInputContext.h>
#include <RmlUi/Core/TextInputHandler.h>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#else
#include <SDL3/SDL_events.h>
#endif

namespace NextUI
{
#if GK_WITH_RMLUI
namespace
{
    ImTextureID EncodeTextureId(uint32_t textureIndex)
    {
        return static_cast<ImTextureID>(static_cast<intptr_t>(textureIndex + 1u));
    }

    ImU32 ToImColor(const Rml::ColourbPremultiplied& color)
    {
        const uint8_t alpha = color.alpha;
        const uint8_t red = alpha > 0 ? static_cast<uint8_t>((static_cast<uint32_t>(color.red) * 255u) / alpha) : 0;
        const uint8_t green = alpha > 0 ? static_cast<uint8_t>((static_cast<uint32_t>(color.green) * 255u) / alpha) : 0;
        const uint8_t blue = alpha > 0 ? static_cast<uint8_t>((static_cast<uint32_t>(color.blue) * 255u) / alpha) : 0;
        return IM_COL32(red, green, blue, alpha);
    }

    std::vector<Rml::byte> ToStraightAlpha(Rml::Span<const Rml::byte> source)
    {
        std::vector<Rml::byte> pixels(source.begin(), source.end());
        for (size_t offset = 0; offset + 3 < pixels.size(); offset += 4)
        {
            const uint8_t alpha = pixels[offset + 3];
            if (alpha == 0)
            {
                pixels[offset + 0] = 0;
                pixels[offset + 1] = 0;
                pixels[offset + 2] = 0;
                continue;
            }

            pixels[offset + 0] = static_cast<uint8_t>(
                std::min(255u, (static_cast<uint32_t>(pixels[offset + 0]) * 255u) / alpha));
            pixels[offset + 1] = static_cast<uint8_t>(
                std::min(255u, (static_cast<uint32_t>(pixels[offset + 1]) * 255u) / alpha));
            pixels[offset + 2] = static_cast<uint8_t>(
                std::min(255u, (static_cast<uint32_t>(pixels[offset + 2]) * 255u) / alpha));
        }
        return pixels;
    }

    int ConvertMouseButton(uint8_t button)
    {
        switch (button)
        {
        case SDL_BUTTON_LEFT:
            return 0;
        case SDL_BUTTON_RIGHT:
            return 1;
        case SDL_BUTTON_MIDDLE:
            return 2;
        default:
            return static_cast<int>(button);
        }
    }

    int ConvertKeyModifiers(SDL_Keymod modifiers)
    {
        int result = 0;
        if ((modifiers & SDL_KMOD_CTRL) != 0)
        {
            result |= Rml::Input::KM_CTRL;
        }
        if ((modifiers & SDL_KMOD_SHIFT) != 0)
        {
            result |= Rml::Input::KM_SHIFT;
        }
        if ((modifiers & SDL_KMOD_ALT) != 0)
        {
            result |= Rml::Input::KM_ALT;
        }
        if ((modifiers & SDL_KMOD_GUI) != 0)
        {
            result |= Rml::Input::KM_META;
        }
        if ((modifiers & SDL_KMOD_CAPS) != 0)
        {
            result |= Rml::Input::KM_CAPSLOCK;
        }
        if ((modifiers & SDL_KMOD_NUM) != 0)
        {
            result |= Rml::Input::KM_NUMLOCK;
        }
        return result;
    }

    Rml::Input::KeyIdentifier ConvertKey(SDL_Keycode key)
    {
        if (key >= SDLK_A && key <= SDLK_Z)
        {
            return static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_A + (key - SDLK_A));
        }
        if (key >= SDLK_0 && key <= SDLK_9)
        {
            return static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_0 + (key - SDLK_0));
        }
        if (key >= SDLK_F1 && key <= SDLK_F12)
        {
            return static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_F1 + (key - SDLK_F1));
        }

        switch (key)
        {
        case SDLK_SPACE:
            return Rml::Input::KI_SPACE;
        case SDLK_RETURN:
            return Rml::Input::KI_RETURN;
        case SDLK_KP_ENTER:
            return Rml::Input::KI_NUMPADENTER;
        case SDLK_ESCAPE:
            return Rml::Input::KI_ESCAPE;
        case SDLK_BACKSPACE:
            return Rml::Input::KI_BACK;
        case SDLK_TAB:
            return Rml::Input::KI_TAB;
        case SDLK_DELETE:
            return Rml::Input::KI_DELETE;
        case SDLK_INSERT:
            return Rml::Input::KI_INSERT;
        case SDLK_HOME:
            return Rml::Input::KI_HOME;
        case SDLK_END:
            return Rml::Input::KI_END;
        case SDLK_PAGEUP:
            return Rml::Input::KI_PRIOR;
        case SDLK_PAGEDOWN:
            return Rml::Input::KI_NEXT;
        case SDLK_LEFT:
            return Rml::Input::KI_LEFT;
        case SDLK_RIGHT:
            return Rml::Input::KI_RIGHT;
        case SDLK_UP:
            return Rml::Input::KI_UP;
        case SDLK_DOWN:
            return Rml::Input::KI_DOWN;
        case SDLK_LSHIFT:
            return Rml::Input::KI_LSHIFT;
        case SDLK_RSHIFT:
            return Rml::Input::KI_RSHIFT;
        case SDLK_LCTRL:
            return Rml::Input::KI_LCONTROL;
        case SDLK_RCTRL:
            return Rml::Input::KI_RCONTROL;
        case SDLK_LALT:
            return Rml::Input::KI_LMENU;
        case SDLK_RALT:
            return Rml::Input::KI_RMENU;
        case SDLK_SEMICOLON:
            return Rml::Input::KI_OEM_1;
        case SDLK_EQUALS:
            return Rml::Input::KI_OEM_PLUS;
        case SDLK_COMMA:
            return Rml::Input::KI_OEM_COMMA;
        case SDLK_MINUS:
            return Rml::Input::KI_OEM_MINUS;
        case SDLK_PERIOD:
            return Rml::Input::KI_OEM_PERIOD;
        case SDLK_SLASH:
            return Rml::Input::KI_OEM_2;
        case SDLK_GRAVE:
            return Rml::Input::KI_OEM_3;
        case SDLK_LEFTBRACKET:
            return Rml::Input::KI_OEM_4;
        case SDLK_BACKSLASH:
            return Rml::Input::KI_OEM_5;
        case SDLK_RIGHTBRACKET:
            return Rml::Input::KI_OEM_6;
        case SDLK_APOSTROPHE:
            return Rml::Input::KI_OEM_7;
        default:
            return Rml::Input::KI_UNKNOWN;
        }
    }

    class RmlSystemInterface final : public Rml::SystemInterface
    {
    public:
        explicit RmlSystemInterface(NextEngine& engine)
            : engine_(engine)
        {
        }

        double GetElapsedTime() override { return engine_.GetWindow().GetTime(); }

        bool LogMessage(Rml::Log::Type type, const Rml::String& message) override
        {
            switch (type)
            {
            case Rml::Log::LT_ERROR:
                SPDLOG_ERROR("[RmlUi] {}", message);
                break;
            case Rml::Log::LT_WARNING:
                SPDLOG_WARN("[RmlUi] {}", message);
                break;
            case Rml::Log::LT_INFO:
                SPDLOG_INFO("[RmlUi] {}", message);
                break;
            default:
                SPDLOG_DEBUG("[RmlUi] {}", message);
                break;
            }
            return true;
        }

        void SetClipboardText(const Rml::String& text) override { SDL_SetClipboardText(text.c_str()); }

        void GetClipboardText(Rml::String& text) override
        {
            char* clipboardText = SDL_GetClipboardText();
            text = clipboardText ? clipboardText : "";
            SDL_free(clipboardText);
        }

    private:
        NextEngine& engine_;
    };

    class RmlEventListener final : public Rml::EventListener
    {
    public:
        explicit RmlEventListener(std::function<void()> callback)
            : callback_(std::move(callback))
        {
        }

        void ProcessEvent(Rml::Event& event) override
        {
            if (callback_)
            {
                callback_();
            }
            event.StopPropagation();
        }

    private:
        std::function<void()> callback_;
    };

    class RmlTextInputHandler final : public Rml::TextInputHandler
    {
    public:
        explicit RmlTextInputHandler(NextEngine& engine)
            : engine_(engine)
        {
        }

        void OnActivate(Rml::TextInputContext* inputContext) override
        {
            activeInputContext_ = inputContext;
            SDL_Window* window = engine_.GetWindow().Handle();
            if (!window)
            {
                return;
            }

            if (!SDL_TextInputActive(window))
            {
                SDL_StartTextInput(window);
            }
            UpdateTextInputArea();
        }

        void OnDeactivate(Rml::TextInputContext* inputContext) override
        {
            SDL_Window* window = engine_.GetWindow().Handle();
            if (activeInputContext_ != inputContext)
            {
                return;
            }

            activeInputContext_ = nullptr;
            if (window && SDL_TextInputActive(window))
            {
                SDL_StopTextInput(window);
            }
        }

        void OnDestroy(Rml::TextInputContext* inputContext) override
        {
            OnDeactivate(inputContext);
        }

        bool IsActive() const { return activeInputContext_ != nullptr; }

        void UpdateTextInputArea()
        {
            if (!activeInputContext_)
            {
                return;
            }

            SDL_Window* window = engine_.GetWindow().Handle();
            if (!window || !SDL_TextInputActive(window))
            {
                return;
            }

            Rml::Rectanglef boundingBox;
            if (!activeInputContext_->GetBoundingBox(boundingBox))
            {
                return;
            }

            int selectionStart = 0;
            int selectionEnd = 0;
            activeInputContext_->GetSelectionRange(selectionStart, selectionEnd);

            const SDL_Rect textInputArea = {
                static_cast<int>(boundingBox.Left()),
                static_cast<int>(boundingBox.Top()),
                std::max(1, static_cast<int>(boundingBox.Right() - boundingBox.Left())),
                std::max(1, static_cast<int>(boundingBox.Bottom() - boundingBox.Top())),
            };
            SDL_SetTextInputArea(window, &textInputArea, selectionEnd);
        }

    private:
        NextEngine& engine_;
        Rml::TextInputContext* activeInputContext_ = nullptr;
    };

    struct CompiledGeometry
    {
        std::vector<Rml::Vertex> vertices;
        std::vector<int> indices;
    };

    struct BoundEventListener
    {
        Rml::Element* element = nullptr;
        std::unique_ptr<RmlEventListener> listener;
    };

    const char* DefaultHtmlStyleSheet()
    {
        return R"RCSS(
<style>
html, body {
    display: block;
}
body {
    margin: 8px;
    font-size: 16px;
    line-height: 1.35;
}
div, section, article, header, footer, main, nav, aside {
    display: block;
}
h1, h2, h3, h4, h5, h6, p, blockquote, pre, ul, ol, li, dl, dt, dd {
    display: block;
}
h1 {
    font-size: 32px;
    font-weight: bold;
    margin: 11px 0 11px 0;
}
h2 {
    font-size: 24px;
    font-weight: bold;
    margin: 13px 0 13px 0;
}
h3 {
    font-size: 19px;
    font-weight: bold;
    margin: 16px 0 8px 0;
}
h4, h5, h6 {
    font-size: 16px;
    font-weight: bold;
    margin: 18px 0 8px 0;
}
p {
    margin: 0 0 16px 0;
}
ul, ol {
    margin: 0 0 16px 0;
    padding-left: 24px;
}
li {
    margin: 0 0 6px 0;
}
blockquote {
    margin: 0 0 16px 0;
    padding-left: 16px;
    border-left: 4px #606775;
}
pre {
    margin: 0 0 16px 0;
    padding: 10px;
    background-color: rgba(0, 0, 0, 0.22);
}
strong, b {
    font-weight: bold;
}
em, i {
    font-style: italic;
}
code {
    background-color: rgba(0, 0, 0, 0.22);
    padding: 2px 4px;
}
a {
    color: #5dade8;
}
button, input, select, textarea {
    display: inline-block;
    font-family: inherit;
    font-size: 16px;
    line-height: 1.2;
    vertical-align: middle;
}
button {
    padding: 2px 6px;
    text-align: center;
    white-space: nowrap;
    color: #000000;
    background-color: #f0f0f0;
    border: 1px #767676;
}
input, select, textarea {
    padding: 2px 4px;
    color: #000000;
    background-color: #ffffff;
    border: 1px #767676;
}
textarea {
    min-height: 3em;
}
select selectvalue {
    width: auto;
    margin-right: 18px;
    padding: 2px 4px;
    color: #000000;
    background-color: #ffffff;
}
select selectarrow {
    width: 18px;
    background-color: #e9e9e9;
    border-left: 1px #767676;
}
select selectbox {
    margin-top: 1px;
    width: auto;
    padding: 2px 0;
    color: #000000;
    background-color: #ffffff;
    border: 1px #767676;
}
select selectbox option {
    width: auto;
    padding: 2px 4px;
    color: #000000;
    background-color: #ffffff;
}
select selectbox option:hover,
select selectbox option:checked {
    background-color: #d9d9d9;
}
scrollbarvertical {
    width: 12px;
}
scrollbarhorizontal {
    height: 12px;
}
scrollbarvertical sliderarrowdec,
scrollbarvertical sliderarrowinc,
scrollbarhorizontal sliderarrowdec,
scrollbarhorizontal sliderarrowinc {
    width: 0;
    height: 0;
}
scrollbarvertical slidertrack {
    width: 8px;
    margin-left: 2px;
    background-color: rgba(0, 0, 0, 0.12);
}
scrollbarvertical sliderbar {
    width: 8px;
    min-height: 28px;
    margin-left: 2px;
    background-color: rgba(0, 0, 0, 0.28);
}
scrollbarhorizontal slidertrack {
    height: 8px;
    margin-top: 2px;
    background-color: rgba(0, 0, 0, 0.12);
}
scrollbarhorizontal sliderbar {
    height: 8px;
    min-width: 28px;
    margin-top: 2px;
    background-color: rgba(0, 0, 0, 0.28);
}
scrollbarvertical sliderbar:hover,
scrollbarvertical sliderbar:active,
scrollbarhorizontal sliderbar:hover,
scrollbarhorizontal sliderbar:active {
    background-color: rgba(0, 0, 0, 0.4);
}
</style>
)RCSS";
    }

    bool DecodeUtf8(std::string_view text, size_t position, uint32_t& codePoint, size_t& codeUnitLength)
    {
        if (position >= text.size())
        {
            return false;
        }

        const unsigned char lead = static_cast<unsigned char>(text[position]);
        if ((lead & 0x80u) == 0)
        {
            codePoint = lead;
            codeUnitLength = 1;
            return true;
        }
        if ((lead & 0xE0u) == 0xC0u && position + 1 < text.size())
        {
            codePoint = ((lead & 0x1Fu) << 6) | (static_cast<unsigned char>(text[position + 1]) & 0x3Fu);
            codeUnitLength = 2;
            return true;
        }
        if ((lead & 0xF0u) == 0xE0u && position + 2 < text.size())
        {
            codePoint = ((lead & 0x0Fu) << 12)
                | ((static_cast<unsigned char>(text[position + 1]) & 0x3Fu) << 6)
                | (static_cast<unsigned char>(text[position + 2]) & 0x3Fu);
            codeUnitLength = 3;
            return true;
        }
        if ((lead & 0xF8u) == 0xF0u && position + 3 < text.size())
        {
            codePoint = ((lead & 0x07u) << 18)
                | ((static_cast<unsigned char>(text[position + 1]) & 0x3Fu) << 12)
                | ((static_cast<unsigned char>(text[position + 2]) & 0x3Fu) << 6)
                | (static_cast<unsigned char>(text[position + 3]) & 0x3Fu);
            codeUnitLength = 4;
            return true;
        }

        codePoint = lead;
        codeUnitLength = 1;
        return true;
    }

    bool IsCjkBreakableCodePoint(uint32_t codePoint)
    {
        return (codePoint >= 0x2E80u && codePoint <= 0x2EFFu)
            || (codePoint >= 0x3000u && codePoint <= 0x303Fu)
            || (codePoint >= 0x3040u && codePoint <= 0x30FFu)
            || (codePoint >= 0x3400u && codePoint <= 0x4DBFu)
            || (codePoint >= 0x4E00u && codePoint <= 0x9FFFu)
            || (codePoint >= 0xAC00u && codePoint <= 0xD7AFu)
            || (codePoint >= 0xF900u && codePoint <= 0xFAFFu)
            || (codePoint >= 0xFF00u && codePoint <= 0xFFEFu);
    }

    bool ContainsCjkText(std::string_view text)
    {
        for (size_t position = 0; position < text.size();)
        {
            uint32_t codePoint = 0;
            size_t codeUnitLength = 0;
            DecodeUtf8(text, position, codePoint, codeUnitLength);
            if (IsCjkBreakableCodePoint(codePoint))
            {
                return true;
            }

            position += codeUnitLength;
        }

        return false;
    }

    bool ElementHasDirectCjkText(Rml::Element* element)
    {
        if (!element)
        {
            return false;
        }

        const int childCount = element->GetNumChildren(true);
        for (int childIndex = 0; childIndex < childCount; ++childIndex)
        {
            if (auto* textElement = rmlui_dynamic_cast<Rml::ElementText*>(element->GetChild(childIndex)))
            {
                if (ContainsCjkText(textElement->GetText()))
                {
                    return true;
                }
            }
        }

        return false;
    }

    void ApplyCjkWordBreakFallback(Rml::Element* element)
    {
        if (!element)
        {
            return;
        }

        if (ElementHasDirectCjkText(element) && !element->GetLocalProperty("word-break"))
        {
            element->SetProperty("word-break", "break-word");
        }

        const int childCount = element->GetNumChildren(true);
        for (int childIndex = 0; childIndex < childCount; ++childIndex)
        {
            Rml::Element* child = element->GetChild(childIndex);
            if (!child || rmlui_dynamic_cast<Rml::ElementText*>(child))
            {
                continue;
            }
            ApplyCjkWordBreakFallback(child);
        }
    }

    bool HasOptInCjkWordBreakFallback(const Rml::ElementDocument& document)
    {
        const Rml::Variant* attribute = document.GetAttribute("data-gk-cjk-word-break-fallback");
        if (!attribute)
        {
            return false;
        }

        return attribute->Get<Rml::String>() == "true";
    }

    std::string InjectDefaultHtmlStyleSheet(std::string_view source)
    {
        constexpr std::string_view headOpen = "<head>";
        constexpr std::string_view rmlOpen = "<rml>";

        std::string result(source);
        const size_t headPos = result.find(headOpen);
        if (headPos != std::string::npos)
        {
            result.insert(headPos + headOpen.size(), DefaultHtmlStyleSheet());
            return result;
        }

        const size_t rmlPos = result.find(rmlOpen);
        if (rmlPos != std::string::npos)
        {
            result.insert(rmlPos + rmlOpen.size(), std::string("<head>") + DefaultHtmlStyleSheet() + "</head>");
            return result;
        }

        return std::string("<rml><head>") + DefaultHtmlStyleSheet() + "</head><body>" + result + "</body></rml>";
    }

    class RmlRenderInterface final : public Rml::RenderInterface
    {
    public:
        explicit RmlRenderInterface(NextEngine& engine)
            : engine_(engine)
        {
        }

        Rml::CompiledGeometryHandle CompileGeometry(
            Rml::Span<const Rml::Vertex> vertices,
            Rml::Span<const int> indices) override
        {
            auto geometry = std::make_unique<CompiledGeometry>();
            geometry->vertices.assign(vertices.begin(), vertices.end());
            geometry->indices.assign(indices.begin(), indices.end());

            const Rml::CompiledGeometryHandle handle = nextGeometryHandle_++;
            geometries_.emplace(handle, std::move(geometry));
            return handle;
        }

        void RenderGeometry(
            Rml::CompiledGeometryHandle geometryHandle,
            Rml::Vector2f translation,
            Rml::TextureHandle textureHandle) override
        {
            auto geometryIt = geometries_.find(geometryHandle);
            if (geometryIt == geometries_.end())
            {
                return;
            }

            ImDrawList* drawList = ImGui::GetForegroundDrawList();
            if (!drawList)
            {
                return;
            }

            if (scissorEnabled_)
            {
                drawList->PushClipRect(scissorMin_, scissorMax_, true);
            }
            else
            {
                drawList->PushClipRectFullScreen();
            }

            const ImTextureID textureId = TextureHandleToImGui(textureHandle);
            drawList->PushTextureID(textureId);

            const CompiledGeometry& geometry = *geometryIt->second;
            const int indexCount = static_cast<int>(geometry.indices.size());
            for (int index : geometry.indices)
            {
                if (index < 0 || static_cast<size_t>(index) >= geometry.vertices.size())
                {
                    drawList->PopTextureID();
                    drawList->PopClipRect();
                    return;
                }
            }

            const int vertexCount = static_cast<int>(geometry.vertices.size());
            const ImDrawIdx baseVertex = static_cast<ImDrawIdx>(drawList->_VtxCurrentIdx);
            drawList->PrimReserve(indexCount, vertexCount);

            for (const Rml::Vertex& vertex : geometry.vertices)
            {
                drawList->PrimWriteVtx(
                    TransformPosition(vertex.position, translation),
                    ImVec2(vertex.tex_coord.x, vertex.tex_coord.y),
                    ToImColor(vertex.colour));
            }
            for (int index : geometry.indices)
            {
                drawList->PrimWriteIdx(static_cast<ImDrawIdx>(baseVertex + index));
            }

            drawList->PopTextureID();
            drawList->PopClipRect();
        }

        void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override { geometries_.erase(geometry); }

        Rml::TextureHandle LoadTexture(Rml::Vector2i& textureDimensions, const Rml::String& source) override
        {
            if (source.empty())
            {
                return 0;
            }

            const std::string textureName = source;
            if (!Utilities::FileHelper::IsAssetAvailable(textureName))
            {
                SPDLOG_WARN("RmlUi texture '{}' is unavailable; using white fallback.", textureName);
                textureDimensions = Rml::Vector2i(1, 1);
                return EnsureWhiteTextureHandle();
            }

            int width = 0;
            int height = 0;
            int comp = 0;
            const std::string platformPath = Utilities::FileHelper::GetPlatformFilePath(textureName.c_str());
            if (stbi_info(platformPath.c_str(), &width, &height, &comp) != 0 && width > 0 && height > 0)
            {
                textureDimensions = Rml::Vector2i(width, height);
            }

            uint32_t textureIndex = Assets::GlobalTexturePool::GetTextureIndexByName(textureName);
            if (textureIndex == static_cast<uint32_t>(-1))
            {
                textureIndex = Assets::GlobalTexturePool::LoadTexture(textureName, true);
            }
            if (textureIndex == static_cast<uint32_t>(-1))
            {
                SPDLOG_WARN("RmlUi texture '{}' failed to load; using white fallback.", textureName);
                textureDimensions = Rml::Vector2i(1, 1);
                return EnsureWhiteTextureHandle();
            }
            return static_cast<Rml::TextureHandle>(textureIndex + 1u);
        }

        Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i sourceDimensions) override
        {
            if (source.empty() || sourceDimensions.x <= 0 || sourceDimensions.y <= 0)
            {
                return 0;
            }

            auto* texturePool = Assets::GlobalTexturePool::GetInstance();
            if (!texturePool)
            {
                return 0;
            }

            std::vector<Rml::byte> pixels = ToStraightAlpha(source);
            auto texture = std::make_unique<Assets::TextureImage>(
                engine_.GetRenderer().CommandPool(),
                static_cast<size_t>(sourceDimensions.x),
                static_cast<size_t>(sourceDimensions.y),
                1,
                VK_FORMAT_R8G8B8A8_UNORM,
                pixels.data(),
                static_cast<uint32_t>(pixels.size()));
            texture->MainThreadPostLoading(engine_.GetRenderer().CommandPool());

            const std::string textureName = fmt::format("__rmlui_generated_{}__", nextTextureNameIndex_++);
            texture->SetDebugName(textureName);
            const uint32_t textureIndex =
                texturePool->RegisterTexture(textureName, std::move(texture), Assets::ETextureLifetime::ETL_Persistent);
            const Rml::TextureHandle handle = static_cast<Rml::TextureHandle>(textureIndex + 1u);
            ownedTextureHandles_.insert(handle);
            return handle;
        }

        void ReleaseTexture(Rml::TextureHandle texture) override
        {
            if (texture == 0 || !ownedTextureHandles_.erase(texture))
            {
                return;
            }

            if (auto* texturePool = Assets::GlobalTexturePool::GetInstance())
            {
                texturePool->ReleaseTexture(static_cast<uint32_t>(texture - 1u));
            }
        }

        void EnableScissorRegion(bool enable) override { scissorEnabled_ = enable; }

        void SetScissorRegion(Rml::Rectanglei region) override
        {
            scissorMin_ = ImVec2(static_cast<float>(region.Left()), static_cast<float>(region.Top()));
            scissorMax_ = ImVec2(static_cast<float>(region.Right()), static_cast<float>(region.Bottom()));
        }

        void SetTransform(const Rml::Matrix4f* transform) override
        {
            if (transform)
            {
                transform_ = *transform;
            }
            else
            {
                transform_.reset();
            }
        }

    private:
        ImVec2 TransformPosition(Rml::Vector2f position, Rml::Vector2f translation) const
        {
            Rml::Vector4f transformed(position.x + translation.x, position.y + translation.y, 0.0f, 1.0f);
            if (transform_)
            {
                transformed = *transform_ * transformed;
                if (transformed.w != 0.0f)
                {
                    transformed.x /= transformed.w;
                    transformed.y /= transformed.w;
                }
            }
            return ImVec2(transformed.x, transformed.y);
        }

        ImTextureID TextureHandleToImGui(Rml::TextureHandle textureHandle)
        {
            if (textureHandle == 0)
            {
                textureHandle = EnsureWhiteTextureHandle();
            }
            if (textureHandle == 0)
            {
                return static_cast<ImTextureID>(0);
            }

            const uint32_t textureIndex = static_cast<uint32_t>(textureHandle - 1u);
            return EncodeTextureId(textureIndex);
        }

        Rml::TextureHandle EnsureWhiteTextureHandle()
        {
            if (whiteTextureHandle_ != 0)
            {
                return whiteTextureHandle_;
            }

            auto* texturePool = Assets::GlobalTexturePool::GetInstance();
            if (!texturePool)
            {
                return 0;
            }

            constexpr std::array<uint8_t, 4> whitePixel = {255, 255, 255, 255};
            auto texture = std::make_unique<Assets::TextureImage>(
                engine_.GetRenderer().CommandPool(), 1, 1, 1, VK_FORMAT_R8G8B8A8_UNORM,
                whitePixel.data(), static_cast<uint32_t>(whitePixel.size()));
            texture->MainThreadPostLoading(engine_.GetRenderer().CommandPool());
            texture->SetDebugName("__rmlui_white__");
            const uint32_t textureIndex =
                texturePool->RegisterTexture("__rmlui_white__", std::move(texture), Assets::ETextureLifetime::ETL_Persistent);
            whiteTextureHandle_ = static_cast<Rml::TextureHandle>(textureIndex + 1u);
            ownedTextureHandles_.insert(whiteTextureHandle_);
            return whiteTextureHandle_;
        }

        NextEngine& engine_;
        std::unordered_map<Rml::CompiledGeometryHandle, std::unique_ptr<CompiledGeometry>> geometries_;
        std::unordered_set<Rml::TextureHandle> ownedTextureHandles_;
        Rml::CompiledGeometryHandle nextGeometryHandle_ = 1;
        uint32_t nextTextureNameIndex_ = 1;
        Rml::TextureHandle whiteTextureHandle_ = 0;
        bool scissorEnabled_ = false;
        ImVec2 scissorMin_{0.0f, 0.0f};
        ImVec2 scissorMax_{0.0f, 0.0f};
        std::optional<Rml::Matrix4f> transform_;
    };
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsubobject-linkage"
struct RmlUiSystem::Impl
{
    explicit Impl(NextEngine& inEngine)
        : engine(inEngine),
          systemInterface(inEngine),
          renderInterface(inEngine),
          textInputHandler(inEngine)
    {
        Rml::SetSystemInterface(&systemInterface);
        Rml::SetRenderInterface(&renderInterface);
        Rml::SetTextInputHandler(&textInputHandler);
        if (!Rml::Initialise())
        {
            SPDLOG_ERROR("RmlUi initialization failed");
            return;
        }

        initialized = true;
        const VkExtent2D extent = engine.GetWindow().WindowSize();
        context = Rml::CreateContext(
            "main",
            Rml::Vector2i(static_cast<int>(extent.width), static_cast<int>(extent.height)));
        if (!context)
        {
            SPDLOG_ERROR("RmlUi context creation failed");
            return;
        }

        LoadDefaultFonts();
        context->EnableMouseCursor(false);
    }

    ~Impl() { Shutdown(); }

    void Shutdown()
    {
        ClearListeners();
        documents.clear();
        if (initialized)
        {
            Rml::Shutdown();
        }
        Rml::SetTextInputHandler(nullptr);
        initialized = false;
        context = nullptr;
    }

    void LoadDefaultFonts()
    {
        const std::array<std::string, 4> fonts = {
            "assets/fonts/DroidSansFallback.ttf",
            "assets/fonts/Roboto-Regular.ttf",
            "assets/fonts/Roboto-BoldCondensed.ttf",
            "assets/fonts/Roboto-Bold.ttf",
        };
        for (const std::string& font : fonts)
        {
            if (Utilities::FileHelper::IsAssetAvailable(font))
            {
                Rml::LoadFontFace(Utilities::FileHelper::GetPlatformFilePath(font.c_str()), true);
            }
        }
    }

    void ClearListeners()
    {
        for (const BoundEventListener& binding : listeners)
        {
            if (binding.element && binding.listener)
            {
                binding.element->RemoveEventListener("click", binding.listener.get());
            }
        }
        listeners.clear();
    }

    NextEngine& engine;
    RmlSystemInterface systemInterface;
    RmlRenderInterface renderInterface;
    RmlTextInputHandler textInputHandler;
    Rml::Context* context = nullptr;
    bool initialized = false;
    std::unordered_map<std::string, Rml::ElementDocument*> documents;
    std::vector<BoundEventListener> listeners;
};
#pragma GCC diagnostic pop

RmlUiSystem::RmlUiSystem(NextEngine& engine)
    : impl_(std::make_unique<Impl>(engine))
{
}

RmlUiSystem::~RmlUiSystem() = default;

bool RmlUiSystem::IsAvailable() const { return impl_ && impl_->initialized && impl_->context; }

bool RmlUiSystem::HandleEvent(const SDL_Event& event)
{
    if (!IsAvailable())
    {
        return false;
    }

    const int modifiers = ConvertKeyModifiers(SDL_GetModState());
    bool propagates = true;
    switch (event.type)
    {
    case SDL_EVENT_MOUSE_MOTION:
        propagates = impl_->context->ProcessMouseMove(static_cast<int>(event.motion.x), static_cast<int>(event.motion.y), modifiers);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        impl_->context->ProcessMouseMove(static_cast<int>(event.button.x), static_cast<int>(event.button.y), modifiers);
        propagates = impl_->context->ProcessMouseButtonDown(ConvertMouseButton(event.button.button), modifiers);
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        impl_->context->ProcessMouseMove(static_cast<int>(event.button.x), static_cast<int>(event.button.y), modifiers);
        propagates = impl_->context->ProcessMouseButtonUp(ConvertMouseButton(event.button.button), modifiers);
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        propagates = impl_->context->ProcessMouseWheel(Rml::Vector2f(-event.wheel.x, -event.wheel.y), modifiers);
        break;
    case SDL_EVENT_KEY_DOWN:
        if (!impl_->textInputHandler.IsActive())
        {
            return false;
        }
        propagates = impl_->context->ProcessKeyDown(ConvertKey(event.key.key), modifiers);
        break;
    case SDL_EVENT_KEY_UP:
        if (!impl_->textInputHandler.IsActive())
        {
            return false;
        }
        propagates = impl_->context->ProcessKeyUp(ConvertKey(event.key.key), modifiers);
        break;
    case SDL_EVENT_TEXT_INPUT:
        if (!impl_->textInputHandler.IsActive())
        {
            return false;
        }
        propagates = impl_->context->ProcessTextInput(event.text.text);
        break;
    default:
        break;
    }
    return !propagates;
}

void RmlUiSystem::BeginFrame()
{
    if (!IsAvailable())
    {
        return;
    }

    const VkExtent2D extent = impl_->engine.GetWindow().WindowSize();
    impl_->context->SetDimensions(Rml::Vector2i(static_cast<int>(extent.width), static_cast<int>(extent.height)));
    impl_->context->SetDensityIndependentPixelRatio(impl_->engine.GetWindow().ContentScale());
    impl_->textInputHandler.UpdateTextInputArea();
}

void RmlUiSystem::RenderFrame()
{
    if (!IsAvailable())
    {
        return;
    }
    impl_->context->Update();
    impl_->context->Render();
}

void RmlUiSystem::Shutdown()
{
    if (impl_)
    {
        impl_->Shutdown();
    }
}

Rml::ElementDocument* RmlUiSystem::EnsureDocument(const std::string& documentId, const std::string& rmlSource)
{
    if (!IsAvailable())
    {
        return nullptr;
    }

    auto documentIt = impl_->documents.find(documentId);
    if (documentIt != impl_->documents.end())
    {
        return documentIt->second;
    }

    const std::string styledSource = InjectDefaultHtmlStyleSheet(rmlSource);
    Rml::ElementDocument* document = impl_->context->LoadDocumentFromMemory(styledSource, documentId);
    if (!document)
    {
        SPDLOG_ERROR("Failed to load RmlUi document '{}'", documentId);
        return nullptr;
    }
    if (HasOptInCjkWordBreakFallback(*document))
    {
        ApplyCjkWordBreakFallback(document);
    }
    document->SetId(documentId);
    document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    impl_->documents.emplace(documentId, document);
    return document;
}

Rml::ElementDocument* RmlUiSystem::ReplaceDocument(const std::string& documentId, const std::string& rmlSource)
{
    if (!IsAvailable())
    {
        return nullptr;
    }

    impl_->ClearListeners();
    if (const auto documentIt = impl_->documents.find(documentId); documentIt != impl_->documents.end())
    {
        if (documentIt->second)
        {
            documentIt->second->Close();
        }
        impl_->documents.erase(documentIt);
        impl_->context->Update();
    }

    return EnsureDocument(documentId, rmlSource);
}

Rml::Element* RmlUiSystem::GetElementById(const std::string& documentId, const std::string& elementId)
{
    if (!IsAvailable())
    {
        return nullptr;
    }
    const auto documentIt = impl_->documents.find(documentId);
    Rml::ElementDocument* document = documentIt != impl_->documents.end() ? documentIt->second : nullptr;
    return document ? document->GetElementById(elementId) : nullptr;
}

void RmlUiSystem::SetDocumentVisible(const std::string& documentId, bool visible)
{
    if (!IsAvailable())
    {
        return;
    }
    const auto documentIt = impl_->documents.find(documentId);
    if (documentIt == impl_->documents.end() || documentIt->second == nullptr)
    {
        return;
    }
    Rml::ElementDocument* document = documentIt->second;

    if (visible)
    {
        document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    }
    else
    {
        document->Hide();
    }
}

void RmlUiSystem::ClearEventListeners()
{
    if (impl_)
    {
        impl_->ClearListeners();
    }
}

bool RmlUiSystem::ListenClick(const std::string& documentId, const std::string& elementId, std::function<void()> callback)
{
    Rml::Element* element = GetElementById(documentId, elementId);
    if (!element)
    {
        return false;
    }

    auto listener = std::make_unique<RmlEventListener>(std::move(callback));
    element->AddEventListener("click", listener.get());
    impl_->listeners.push_back(BoundEventListener{element, std::move(listener)});
    return true;
}

bool RmlUiSystem::WantsToCaptureMouse() const
{
    return IsAvailable() && impl_->context->IsMouseInteracting();
}

bool RmlUiSystem::WantsToCaptureKeyboard() const
{
    return IsAvailable() && impl_->textInputHandler.IsActive();
}
#else
struct RmlUiSystem::Impl
{
};

RmlUiSystem::RmlUiSystem(NextEngine&) = default;
RmlUiSystem::~RmlUiSystem() = default;
bool RmlUiSystem::IsAvailable() const { return false; }
bool RmlUiSystem::HandleEvent(const SDL_Event&) { return false; }
void RmlUiSystem::BeginFrame() {}
void RmlUiSystem::RenderFrame() {}
void RmlUiSystem::Shutdown() {}
Rml::ElementDocument* RmlUiSystem::EnsureDocument(const std::string&, const std::string&) { return nullptr; }
Rml::ElementDocument* RmlUiSystem::ReplaceDocument(const std::string&, const std::string&) { return nullptr; }
Rml::Element* RmlUiSystem::GetElementById(const std::string&, const std::string&) { return nullptr; }
void RmlUiSystem::SetDocumentVisible(const std::string&, bool) {}
void RmlUiSystem::ClearEventListeners() {}
bool RmlUiSystem::ListenClick(const std::string&, const std::string&, std::function<void()>) { return false; }
bool RmlUiSystem::WantsToCaptureMouse() const { return false; }
bool RmlUiSystem::WantsToCaptureKeyboard() const { return false; }
#endif
}
