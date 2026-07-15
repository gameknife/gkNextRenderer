#include "ScadAIService.hpp"
#include "ScadOutline.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Modules/NextAI/AIService.hpp"
#include "ScadStudioUtils.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <thread>
#include "Modules/NextAI/NextAIModule.hpp"

namespace ScadStudio
{
    namespace
    {
        std::string SanitiseProjectPath(std::string path)
        {
            path = TrimCopy(std::move(path));
            std::replace(path.begin(), path.end(), '\\', '/');
            while (!path.empty() && path.front() == '/')
            {
                path.erase(path.begin());
            }
            if (path.empty() || path.find("..") != std::string::npos || path.find(':') != std::string::npos)
            {
                return "";
            }

            std::string clean;
            clean.reserve(path.size());
            for (char c : path)
            {
                const unsigned char uc = static_cast<unsigned char>(c);
                if (std::isalnum(uc) || c == '_' || c == '-' || c == '/' || c == '.')
                {
                    clean.push_back(c);
                }
            }
            if (clean.empty())
            {
                return "";
            }
            const std::string lower = ToLower(clean);
            if (lower.size() < 5 || lower.substr(lower.size() - 5) != ".scad")
            {
                clean += ".scad";
            }
            return clean;
        }

        std::string FindFencedBlock(const std::string& text, const std::string& tag)
        {
            size_t cursor = 0;
            while (cursor < text.size())
            {
                const size_t fence = text.find("```", cursor);
                if (fence == std::string::npos)
                {
                    return "";
                }
                const size_t headerEnd = text.find('\n', fence + 3);
                if (headerEnd == std::string::npos)
                {
                    return "";
                }
                const std::string header = ToLower(TrimCopy(text.substr(fence + 3, headerEnd - fence - 3)));
                const size_t blockStart = headerEnd + 1;
                const size_t blockEnd = text.find("```", blockStart);
                if (header.find(tag) != std::string::npos)
                {
                    return blockEnd == std::string::npos ? text.substr(blockStart)
                                                         : text.substr(blockStart, blockEnd - blockStart);
                }
                cursor = blockEnd == std::string::npos ? text.size() : blockEnd + 3;
            }
            return "";
        }

    } // namespace

    ScadAIService::ScadAIService(NextEngine& engine)
        : engine_(engine)
    {
        if (auto* ai = NextAI::GetAIService(engine_))
        {
            ai->LoadConfig();
            ai->SetProfile("scad-studio");
        }
    }

    ScadAIService::~ScadAIService()
    {
        if (worker_.joinable()) { worker_.request_stop(); worker_.join(); }
    }

    bool ScadAIService::IsConfigured() const
    {
        auto* ai = NextAI::GetAIService(engine_);
        return ai && ai->IsConfigured();
    }

    std::string ScadAIService::ProviderName() const
    {
        auto* ai = NextAI::GetAIService(engine_);
        return ai ? ai->GetProviderName() : std::string("None");
    }

    std::string ScadAIService::ProviderId() const
    {
        auto* ai = NextAI::GetAIService(engine_);
        return ai ? ai->GetProviderId() : std::string();
    }

    std::vector<NextAI::FAIProviderDescriptor> ScadAIService::Providers() const
    {
        auto* ai = NextAI::GetAIService(engine_); return ai ? ai->GetAvailableProviders() : std::vector<NextAI::FAIProviderDescriptor>{};
    }

    bool ScadAIService::IsProviderConfigured(const std::string& providerId) const
    {
        auto* ai = NextAI::GetAIService(engine_);
        return ai && ai->IsProviderConfigured(providerId);
    }

    bool ScadAIService::SwitchProvider(const std::string& providerId)
    {
        auto* ai = NextAI::GetAIService(engine_);
        if (!ai || !ai->SwitchProvider(providerId))
        {
            return false;
        }
        ResetConversation();
        return true;
    }

    std::vector<std::string> ScadAIService::CurrentProviderModels() const
    {
        auto* ai = NextAI::GetAIService(engine_);
        return ai ? ai->GetProviderModels(ai->GetProviderId()) : std::vector<std::string>{};
    }

    std::string ScadAIService::CurrentModel() const
    {
        auto* ai = NextAI::GetAIService(engine_);
        return ai ? ai->GetCurrentModel() : std::string();
    }

    bool ScadAIService::SetCurrentModel(const std::string& model)
    {
        auto* ai = NextAI::GetAIService(engine_);
        return ai && ai->SetCurrentModel(model);
    }

    void ScadAIService::ResetConversation()
    {
        conversation_.clear();
        std::lock_guard<std::mutex> lock(mutex_);
        streamingText_.clear();
    }

    std::string ScadAIService::StreamingText() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return streamingText_;
    }

    std::string ScadAIService::BuildSystemPrompt() const
    {
        // The loader only supports a subset of OpenSCAD (see AGENT_GUIDE/SCADLoader.md);
        // steer the model firmly to it and forbid the unsupported features.
        return R"(You are an expert OpenSCAD modeller embedded in a 3D engine. You generate and edit
.scad source that is rendered live by a built-in OpenSCAD subset loader.

## Output contract (STRICT)
- Reply with a SHORT one-sentence description, then EXACTLY ONE fenced code block.
- Prefer a multi-file block tagged ```scad-project for structured models. Use this exact format:
  --- file: main.scad
  <root file: $fn, use <module.scad>, and top-level assembly call>
  --- file: module_name.scad
  <one public module per file>
- For very small one-part models, a single ```scad block is acceptable.
- When editing an existing multi-file project, return the COMPLETE project block, not a diff or snippet.
- Keep triangle counts reasonable; prefer $fn between 24 and 64 for round shapes.

## Supported subset (use ONLY these)
- 3D primitives: cube, sphere, cylinder (incl. r1/r2 cones), polyhedron
- 2D (inside extrudes): circle, square, polygon (with `paths` holes), text (CJK ok)
- Transforms: translate, rotate, scale, mirror, multmatrix, color (rgba or named)
- CSG: union, difference, intersection, hull
- Extrudes: linear_extrude (concave/holed/nested ok), rotate_extrude
- Control flow: for, if/else, let, list comprehensions, intersection_for
- Modules & functions with default/keyword params, children()/$children
- Builtins: max min abs floor ceil round sqrt pow exp ln log sign sin cos tan asin acos atan atan2 len norm concat str
  (trigonometry is in DEGREES)
- Special vars: $fn, $fa, $fs

## NOT supported — never use
- import, surface, projection, offset      (will fail or be ignored)
- resize                                    (no-op)
- minkowski                                 (approximated as union; avoid)

## Conventions
- OpenSCAD is Z-up; the engine converts to its own axes automatically.
- Use color([r,g,b]) to give parts distinct colours — the loader groups geometry by colour,
  so colour is also the visual part breakdown. alpha < 0.99 becomes glass/liquid.
- Organise the model as module files. The root main.scad should use <...> those files and instantiate
  the complete model. Each module file should define one named module and helper functions/constants
  needed by that module.
- File paths must be relative, portable, and end in .scad. Do not use absolute paths or '..'.
- Keep overall size roughly in the 1..100 unit range.)";
    }

    std::string ScadAIService::ExtractScadBlock(const std::string& text)
    {
        // Prefer a ```scad fenced block; fall back to the first generic fence.
        size_t fence = text.find("```scad");
        if (fence == std::string::npos)
        {
            fence = text.find("```");
        }
        if (fence == std::string::npos)
        {
            return ""; // no code block; treat as a plain chat reply
        }

        size_t contentStart = text.find('\n', fence);
        if (contentStart == std::string::npos)
        {
            return "";
        }
        contentStart += 1;

        size_t contentEnd = text.find("```", contentStart);
        std::string code = (contentEnd == std::string::npos)
                               ? text.substr(contentStart)
                               : text.substr(contentStart, contentEnd - contentStart);

        // Trim surrounding whitespace.
        const size_t b = code.find_first_not_of(" \t\r\n");
        const size_t e = code.find_last_not_of(" \t\r\n");
        if (b == std::string::npos || e == std::string::npos)
        {
            return "";
        }
        return code.substr(b, e - b + 1);
    }

    std::vector<FScadProjectFile> ScadAIService::ExtractProjectFiles(const std::string& text)
    {
        const std::string block = FindFencedBlock(text, "scad-project");
        if (block.empty())
        {
            return {};
        }

        std::vector<FScadProjectFile> files;
        std::istringstream in(block);
        std::string line;
        std::string currentPath;
        std::string currentSource;

        auto flush = [&]() {
            if (currentPath.empty())
            {
                return;
            }
            const std::string trimmed = TrimCopy(currentSource);
            if (!trimmed.empty())
            {
                files.push_back(FScadProjectFile{currentPath, trimmed});
            }
            currentPath.clear();
            currentSource.clear();
        };

        while (std::getline(in, line))
        {
            const std::string trimmedLine = TrimCopy(line);
            const std::string lower = ToLower(trimmedLine);
            if (lower.rfind("--- file:", 0) == 0)
            {
                flush();
                currentPath = SanitiseProjectPath(trimmedLine.substr(9));
                continue;
            }

            if (!currentPath.empty())
            {
                currentSource += line;
                currentSource += '\n';
            }
        }
        flush();

        return files;
    }

    void ScadAIService::SubmitAsync(
        const std::string& currentSource,
        const std::vector<FScadProjectFile>& files,
        const FScadEditScope& editScope,
        const std::string& instruction)
    {
        auto* ai = NextAI::GetAIService(engine_);
        if (!ai || !ai->IsConfigured())
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_ = FScadGenResult{};
            pending_.success = false;
            pending_.error = "gnb bridge/provider not configured";
            hasPending_.store(true);
            return;
        }

        // Build the request on the main thread (capture by value for the worker).
        NextAI::FChatRequest request;
        request.temperature = 0.4f;
        request.messages.push_back(NextAI::FChatMessage::System(BuildSystemPrompt()));
        for (const auto& msg : conversation_)
        {
            request.messages.push_back(msg);
        }

        const std::string userContent = BuildScadUserPrompt(currentSource, files, editScope, instruction);
        request.messages.push_back(NextAI::FChatMessage::User(userContent));

        // Record the plain instruction in history (source is injected live, not stored).
        conversation_.push_back(NextAI::FChatMessage::User(instruction));

        generating_.store(true);
        hasPending_.store(false);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            streamingText_.clear();
        }

        if (worker_.joinable()) worker_.join();
        worker_ = std::jthread([this, request = std::move(request)](std::stop_token) mutable
        {
            auto* svc = NextAI::GetAIService(engine_);
            NextAI::FChatResponse response = svc->ChatStream(request, [this](const std::string& delta)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                streamingText_ += delta;
            });

            auto parseAndValidate = [this](const NextAI::FChatResponse& candidate)
            {
                FScadGenResult result;
                if (!candidate.success)
                {
                    result.error = candidate.errorMessage.empty() ? "generation failed" : candidate.errorMessage;
                    return result;
                }
                result.assistantText = candidate.content;
                result.files = ExtractProjectFiles(candidate.content);
                if (!result.files.empty())
                {
                    const auto root = std::find_if(result.files.begin(), result.files.end(), [](const FScadProjectFile& file) {
                        return ToLower(file.path) == "main.scad";
                    });
                    result.scadSource = (root != result.files.end()) ? root->source : result.files.front().source;
                    for (const auto& file : result.files)
                    {
                        const auto outline = BuildScadOutline(file.source);
                        if (!outline.ok)
                        {
                            result.error = file.path + ": " + outline.error;
                            return result;
                        }
                    }
                }
                else
                {
                    result.scadSource = ExtractScadBlock(candidate.content);
                    if (result.scadSource.empty())
                    {
                        result.error = "response contains no scad or scad-project fenced artifact";
                        return result;
                    }
                    const auto outline = BuildScadOutline(result.scadSource);
                    if (!outline.ok)
                    {
                        result.error = outline.error;
                        return result;
                    }
                }
                result.success = true;
                return result;
            };

            FScadGenResult result = parseAndValidate(response);
            if (!result.success && response.success)
            {
                const std::string validationError = result.error;
                request.messages.push_back(NextAI::FChatMessage::Assistant(response.content));
                request.messages.push_back(NextAI::FChatMessage::User(
                    "The generated artifact failed local SCAD validation. Return one complete corrected artifact. "
                    "Do not explain. Exact validator error: " + validationError));
                const NextAI::FChatResponse repaired = svc->Chat(request);
                result = parseAndValidate(repaired);
                result.repairAttempted = true;
                if (!result.success)
                {
                    result.error = "repair failed after one attempt: " + result.error;
                }
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                pending_ = std::move(result);
                hasPending_.store(true);
            }
            generating_.store(false);
        });
    }

    FScadGenResult ScadAIService::TakePendingResult()
    {
        FScadGenResult result;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            result = std::move(pending_);
            pending_ = FScadGenResult{};
            streamingText_.clear();
            hasPending_.store(false);
        }

        // Append the assistant turn to history so follow-up edits stay coherent.
        if (result.success && !result.assistantText.empty())
        {
            conversation_.push_back(NextAI::FChatMessage::Assistant(result.assistantText));
            while (conversation_.size() > kMaxConversationMessages)
            {
                conversation_.erase(conversation_.begin());
            }
        }
        else if (!result.success)
        {
            // Roll back the unanswered user turn so the history stays paired.
            if (!conversation_.empty() && conversation_.back().role == NextAI::EChatRole::User)
            {
                conversation_.pop_back();
            }
        }

        return result;
    }
}
