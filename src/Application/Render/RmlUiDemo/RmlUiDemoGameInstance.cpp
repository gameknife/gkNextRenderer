#include "RmlUiDemoGameInstance.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/UI/RmlUiSystem.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Utilities/FileHelper.hpp"

#include <SDL3/SDL.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string_view>

namespace
{
    constexpr const char* kDocumentId = "rmlui-demo";
    constexpr const char* kAssetRoot = "assets/rmlui_demo";

    std::string ReplaceAll(std::string text, const std::string& from, const std::string& to)
    {
        if (from.empty())
        {
            return text;
        }

        size_t cursor = 0;
        while ((cursor = text.find(from, cursor)) != std::string::npos)
        {
            text.replace(cursor, from.size(), to);
            cursor += to.size();
        }
        return text;
    }

    std::string Trim(std::string_view text)
    {
        const size_t start = text.find_first_not_of(" \t\r\n");
        if (start == std::string_view::npos)
        {
            return {};
        }
        const size_t end = text.find_last_not_of(" \t\r\n");
        return std::string(text.substr(start, end - start + 1));
    }

    std::string ToLower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch)
        {
            return static_cast<char>(std::tolower(ch));
        });
        return text;
    }

    std::string ExtractBody(std::string html)
    {
        const std::string lower = ToLower(html);
        const size_t bodyOpen = lower.find("<body");
        if (bodyOpen == std::string::npos)
        {
            return Trim(html);
        }

        const size_t bodyContentStart = lower.find('>', bodyOpen);
        if (bodyContentStart == std::string::npos)
        {
            return Trim(html);
        }

        const size_t bodyClose = lower.rfind("</body>");
        if (bodyClose == std::string::npos || bodyClose <= bodyContentStart)
        {
            return Trim(std::string_view(html).substr(bodyContentStart + 1));
        }

        return Trim(std::string_view(html).substr(bodyContentStart + 1, bodyClose - bodyContentStart - 1));
    }

    std::string EscapeText(std::string_view text)
    {
        std::string result;
        result.reserve(text.size());
        for (char ch : text)
        {
            switch (ch)
            {
            case '&':
                result += "&amp;";
                break;
            case '<':
                result += "&lt;";
                break;
            case '>':
                result += "&gt;";
                break;
            case '"':
                result += "&quot;";
                break;
            default:
                result += ch;
                break;
            }
        }
        return result;
    }

    bool StartsWith(std::string_view text, std::string_view prefix)
    {
        return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
    }

    bool EndsWith(std::string_view text, std::string_view suffix)
    {
        return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
    }

    bool IsHexColor(std::string_view token)
    {
        if (token.size() != 4 && token.size() != 7 && token.size() != 9)
        {
            return false;
        }
        if (token.front() != '#')
        {
            return false;
        }
        return std::all_of(token.begin() + 1, token.end(), [](unsigned char ch)
        {
            return std::isxdigit(ch) != 0;
        });
    }

    std::optional<std::string> RewriteBrowserInvalidBorderShorthandLine(std::string_view line)
    {
        constexpr std::string_view prefixes[] = {
            "border:",
            "border-left:",
            "border-right:",
            "border-top:",
            "border-bottom:",
        };

        const std::string trimmed = Trim(line);
        if (trimmed.empty())
        {
            return std::nullopt;
        }
        std::string_view prefix;
        for (const std::string_view candidate : prefixes)
        {
            if (StartsWith(trimmed, candidate))
            {
                prefix = candidate;
                break;
            }
        }
        if (prefix.empty() || trimmed.back() != ';')
        {
            return std::nullopt;
        }

        const std::string value = Trim(trimmed.substr(prefix.size(), trimmed.size() - prefix.size() - 1));
        std::istringstream stream(value);
        std::string widthToken;
        std::string colorToken;
        std::string trailingToken;
        if (!(stream >> widthToken >> colorToken))
        {
            return std::nullopt;
        }
        if (stream >> trailingToken)
        {
            return std::nullopt;
        }

        if (!EndsWith(widthToken, "px") || !IsHexColor(colorToken))
        {
            return std::nullopt;
        }

        return fmt::format("{} 0px {};", prefix, colorToken);
    }

    std::string StripBrowserInvalidBorderShorthands(std::string_view css)
    {
        std::ostringstream sanitized;
        std::istringstream input{std::string(css)};
        std::string line;
        while (std::getline(input, line))
        {
            if (const std::optional<std::string> rewrittenLine = RewriteBrowserInvalidBorderShorthandLine(line))
            {
                sanitized << *rewrittenLine << '\n';
            }
            else
            {
                sanitized << line << '\n';
            }
        }
        return sanitized.str();
    }

    std::optional<size_t> FindModuleIndexById(const std::vector<RmlUiDemo::FDemoModule>& modules, std::string_view moduleId)
    {
        for (size_t index = 0; index < modules.size(); ++index)
        {
            if (modules[index].id == moduleId)
            {
                return index;
            }
        }
        return std::nullopt;
    }
}

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(
    Vulkan::WindowConfig& config,
    Runtime::Config::Options& options,
    NextEngine* engine)
{
    return std::make_unique<RmlUiDemo::RmlUiDemoGameInstance>(config, options, engine);
}

namespace RmlUiDemo
{
    RmlUiDemoGameInstance::RmlUiDemoGameInstance(
        Vulkan::WindowConfig& config,
        Runtime::Config::Options& options,
        NextEngine* engine)
        : NextGameInstanceBase(config, options, engine)
    {
        ConfigureWindow(config, options, "RmlUi HTML/CSS Demo", 1360, 860, true);
        options.PresentMode = 0;

        modules_ = {
            {"landing", "Landing", "Classic product landing page"},
            {"typography", "Typography", "Fonts, wrapping, emphasis, and multilingual text"},
            {"layout", "Layout", "Flex rows, cards, sidebars, and percentage sizing"},
            {"controls", "Controls", "Buttons, states, segmented controls, and counters"},
            {"forms", "Forms", "Input, textarea, select, and focus capture"},
            {"modal", "Modal", "Overlay, dialog stacking, and event capture"},
            {"scroll", "Scroll", "Overflow regions and long list clipping"},
            {"animation", "Animation", "Transitions, transforms, and hover feedback"},
            {"responsive", "Responsive", "Adaptive layout under narrow widths"},
            {"images", "Images", "Texture loading through RmlUi and the engine pool"},
        };

        if (const char* startModule = std::getenv("GK_RMLUI_DEMO_MODULE"))
        {
            if (const auto startIndex = FindModuleIndexById(modules_, startModule); startIndex.has_value())
            {
                currentModule_ = *startIndex;
            }
        }
    }

    void RmlUiDemoGameInstance::OnInit()
    {
        statusMessage_ = fmt::format("Loaded {} page", CurrentModule().id);
    }

    void RmlUiDemoGameInstance::OnTick(double deltaSeconds)
    {
        elapsedSeconds_ += deltaSeconds;
    }

    void RmlUiDemoGameInstance::OnDestroy()
    {
        if (NextUI::RmlUiSystem* rml = GetEngine().GetRmlUi())
        {
            rml->ClearEventListeners();
            rml->SetDocumentVisible(kDocumentId, false);
        }
    }

    bool RmlUiDemoGameInstance::OnRenderUI()
    {
#if GK_WITH_RMLUI
        if (documentDirty_)
        {
            RenderDocument();
        }
#endif
        return true;
    }

    bool RmlUiDemoGameInstance::OnKey(SDL_Event& event)
    {
        if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat)
        {
            return false;
        }

        if (event.key.key == SDLK_F5)
        {
            RequestReload("Reloaded current HTML/CSS from disk");
            return true;
        }
        if (event.key.key == SDLK_ESCAPE && modalOpen_)
        {
            modalOpen_ = false;
            RequestReload("Closed modal");
            return true;
        }

        if (event.key.key >= SDLK_1 && event.key.key <= SDLK_9)
        {
            SelectModule(static_cast<size_t>(event.key.key - SDLK_1));
            return true;
        }
        if (event.key.key == SDLK_0)
        {
            SelectModule(9);
            return true;
        }

        return false;
    }

    void RmlUiDemoGameInstance::RequestReload(std::string message)
    {
        if (!message.empty())
        {
            statusMessage_ = std::move(message);
        }
        documentDirty_ = true;
    }

    void RmlUiDemoGameInstance::SelectModule(size_t index)
    {
        if (index >= modules_.size())
        {
            return;
        }
        currentModule_ = index;
        modalOpen_ = false;
        RequestReload(fmt::format("Switched to {}", CurrentModule().title));
    }

    void RmlUiDemoGameInstance::RenderDocument()
    {
        NextUI::RmlUiSystem* rml = GetEngine().GetRmlUi();
        if (!rml || !rml->IsAvailable())
        {
            return;
        }

        Rml::ElementDocument* document = rml->ReplaceDocument(kDocumentId, BuildDocument());
        if (!document)
        {
            SPDLOG_ERROR("[RmlUiDemo] Failed to load module '{}'", CurrentModule().id);
            return;
        }

        BindActions();
        documentDirty_ = false;
    }

    void RmlUiDemoGameInstance::BindActions()
    {
        NextUI::RmlUiSystem* rml = GetEngine().GetRmlUi();
        if (!rml)
        {
            return;
        }

        for (size_t index = 0; index < modules_.size(); ++index)
        {
            rml->ListenClick(kDocumentId, fmt::format("nav_{}", modules_[index].id), [this, index]()
            {
                SelectModule(index);
            });
        }

        rml->ListenClick(kDocumentId, "action_reload", [this]() { RequestReload("Reloaded current HTML/CSS from disk"); });
        rml->ListenClick(kDocumentId, "action_theme", [this]()
        {
            darkTheme_ = !darkTheme_;
            RequestReload(darkTheme_ ? "Dark theme" : "Light theme");
        });
        rml->ListenClick(kDocumentId, "action_density", [this]()
        {
            compactMode_ = !compactMode_;
            RequestReload(compactMode_ ? "Compact density" : "Comfortable density");
        });
        rml->ListenClick(kDocumentId, "action_modal", [this]()
        {
            modalOpen_ = true;
            RequestReload("Opened modal");
        });
        rml->ListenClick(kDocumentId, "modal_close", [this]()
        {
            modalOpen_ = false;
            RequestReload("Closed modal");
        });
        rml->ListenClick(kDocumentId, "counter_dec", [this]()
        {
            --counter_;
            RequestReload("Counter decremented");
        });
        rml->ListenClick(kDocumentId, "counter_inc", [this]()
        {
            ++counter_;
            RequestReload("Counter incremented");
        });
        rml->ListenClick(kDocumentId, "counter_reset", [this]()
        {
            counter_ = 0;
            RequestReload("Counter reset");
        });
        rml->ListenClick(kDocumentId, "primary_action", [this]() { RequestReload("Primary action clicked"); });
        rml->ListenClick(kDocumentId, "secondary_action", [this]() { RequestReload("Secondary action clicked"); });
        rml->ListenClick(kDocumentId, "danger_action", [this]() { RequestReload("Danger action clicked"); });
    }

    std::string RmlUiDemoGameInstance::BuildDocument()
    {
        std::string page = ExtractBody(ReadAssetText(CurrentPagePath()));
        page = ReplaceAll(page, "{{module_nav}}", BuildNavigation());
        page = ReplaceAll(page, "{{demo_toolbar}}", BuildToolbar());
        page = ReplaceAll(page, "{{demo_state}}", BuildStatePanel());
        page = ReplaceAll(page, "{{demo_modal}}", BuildModal());
        page = ReplaceAll(page, "{{counter_value}}", std::to_string(counter_));
        page = ReplaceAll(page, "{{theme_class}}", darkTheme_ ? "theme-dark" : "theme-light");
        page = ReplaceAll(page, "{{density_class}}", compactMode_ ? "density-compact" : "density-comfortable");
        page = ReplaceAll(page, "{{current_module}}", CurrentModule().title);
        page = ReplaceAll(page, "{{status_message}}", EscapeText(statusMessage_));

        std::string css = ReadAssetText(fmt::format("{}/styles/base.css", kAssetRoot));
        css += "\n";
        css += ReadAssetText(fmt::format("{}/styles/modules.css", kAssetRoot));
        // Browsers drop shorthand borders without a style token (`border: 1px #fff;`),
        // while RmlUi currently renders them. Strip those declarations here so the
        // in-engine demo matches the browser reference page. Keep page-specific
        // overrides unsanitized so they can use RmlUi-only generated element tags.
        css = StripBrowserInvalidBorderShorthands(css);
        css += "\n";
        css += ReadOptionalAssetText(CurrentPageCssPath());

        return fmt::format(
            R"RML(<rml>
<head>
<title>RmlUi Demo - {}</title>
<style>
{}
</style>
</head>
<body data-gk-cjk-word-break-fallback="true">
{}
</body>
</rml>)RML",
            EscapeText(CurrentModule().title),
            css,
            page);
    }

    std::string RmlUiDemoGameInstance::BuildNavigation() const
    {
        std::ostringstream html;
        html << "<nav class=\"demo-nav\">";
        for (const FDemoModule& module : modules_)
        {
            const bool active = module.id == CurrentModule().id;
            html << fmt::format(
                "<button id=\"nav_{}\" class=\"nav-item{}\"><span>{}</span><small>{}</small></button>",
                module.id,
                active ? " active" : "",
                EscapeText(module.title),
                EscapeText(module.description));
        }
        html << "</nav>";
        return html.str();
    }

    std::string RmlUiDemoGameInstance::BuildToolbar() const
    {
        return fmt::format(
            R"HTML(<div class="demo-toolbar">
<div class="toolbar-title">
<strong>{}</strong>
<span>{}</span>
</div>
<div class="toolbar-actions">
<button id="action_reload">Reload F5</button>
<button id="action_theme">{}</button>
<button id="action_density">{}</button>
<button id="action_modal">Modal</button>
</div>
</div>)HTML",
            EscapeText(CurrentModule().title),
            EscapeText(CurrentModule().description),
            darkTheme_ ? "Light" : "Dark",
            compactMode_ ? "Comfort" : "Compact");
    }

    std::string RmlUiDemoGameInstance::BuildStatePanel() const
    {
        const VkExtent2D size = GetEngine().GetWindow().WindowSize();
        return fmt::format(
            R"HTML(<aside class="state-panel">
<h3>Runtime State</h3>
<div class="state-row"><span>Module</span><strong>{}</strong></div>
<div class="state-row"><span>Window</span><strong>{} x {}</strong></div>
<div class="state-row"><span>Theme</span><strong>{}</strong></div>
<div class="state-row"><span>Density</span><strong>{}</strong></div>
<div class="state-row"><span>Counter</span><strong>{}</strong></div>
<div class="state-row"><span>Elapsed</span><strong>{:.1f}s</strong></div>
<p class="status-line">{}</p>
</aside>)HTML",
            EscapeText(CurrentModule().title),
            size.width,
            size.height,
            darkTheme_ ? "dark" : "light",
            compactMode_ ? "compact" : "comfortable",
            counter_,
            elapsedSeconds_,
            EscapeText(statusMessage_));
    }

    std::string RmlUiDemoGameInstance::BuildModal() const
    {
        if (!modalOpen_)
        {
            return {};
        }

        return R"HTML(<div class="modal-scrim">
<div class="modal-card">
<h2>Modal and Event Capture</h2>
<p>This overlay validates stacking, alpha blending, centered layout, button interaction, and Escape key handling.</p>
<button id="modal_close">Close modal</button>
</div>
</div>)HTML";
    }

    std::string RmlUiDemoGameInstance::ReadAssetText(const std::string& path) const
    {
        std::string text = ReadOptionalAssetText(path);
        if (!text.empty())
        {
            return text;
        }
        return fmt::format("<section class=\"error-card\"><h1>Missing asset</h1><p>{}</p></section>", EscapeText(path));
    }

    std::string RmlUiDemoGameInstance::ReadOptionalAssetText(const std::string& path) const
    {
        if (!Utilities::FileHelper::IsAssetAvailable(path))
        {
            return {};
        }

        std::vector<uint8_t> bytes;
        if (auto* pak = Utilities::Package::FPackageFileSystem::TryGetInstance())
        {
            if (pak->LoadFile(path, bytes) && !bytes.empty())
            {
                return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            }
        }

        const std::string resolved = Utilities::FileHelper::GetPlatformFilePath(path.c_str());
        std::ifstream file(resolved, std::ios::binary);
        if (!file.is_open())
        {
            return {};
        }

        std::ostringstream stream;
        stream << file.rdbuf();
        return stream.str();
    }

    const FDemoModule& RmlUiDemoGameInstance::CurrentModule() const
    {
        return modules_[currentModule_];
    }

    std::string RmlUiDemoGameInstance::CurrentPagePath() const
    {
        return fmt::format("{}/pages/{}.html", kAssetRoot, CurrentModule().id);
    }

    std::string RmlUiDemoGameInstance::CurrentPageCssPath() const
    {
        return fmt::format("{}/styles/{}.css", kAssetRoot, CurrentModule().id);
    }
}
