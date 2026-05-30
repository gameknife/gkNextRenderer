#include "ScadAIService.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Subsystems/AIService.hpp"

#include <spdlog/spdlog.h>
#include <thread>

namespace ScadStudio
{
    ScadAIService::ScadAIService(NextEngine& engine)
        : engine_(engine)
    {
        if (auto* ai = engine_.GetAIService())
        {
            ai->LoadConfig();
        }
    }

    bool ScadAIService::IsConfigured() const
    {
        auto* ai = engine_.GetAIService();
        return ai && ai->IsConfigured();
    }

    std::string ScadAIService::ProviderName() const
    {
        auto* ai = engine_.GetAIService();
        return ai ? ai->GetProviderName() : std::string("None");
    }

    void ScadAIService::ResetConversation()
    {
        conversation_.clear();
    }

    std::string ScadAIService::BuildSystemPrompt() const
    {
        // The loader only supports a subset of OpenSCAD (see AGENT_GUIDE/SCADLoader.md);
        // steer the model firmly to it and forbid the unsupported features.
        return R"(You are an expert OpenSCAD modeller embedded in a 3D engine. You generate and edit
.scad source that is rendered live by a built-in OpenSCAD subset loader.

## Output contract (STRICT)
- Reply with a SHORT one-sentence description, then EXACTLY ONE fenced code block tagged ```scad
  containing the COMPLETE .scad file. No second code block.
- When the user asks to modify an existing model, return the FULL revised file, not a diff or snippet.
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
- Organise the model with named modules instantiated at the top level so its structure is clear.
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

    void ScadAIService::SubmitAsync(const std::string& currentSource, const std::string& instruction)
    {
        auto* ai = engine_.GetAIService();
        if (!ai || !ai->IsConfigured())
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_ = FScadGenResult{false, "", "", "AI service not configured. Check assets/configs/ai_config.json"};
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

        std::string userContent;
        if (currentSource.empty())
        {
            userContent = "Create a new model.\n\nRequest: " + instruction;
        }
        else
        {
            userContent = "Current model source (authoritative — modify this):\n```scad\n" + currentSource +
                          "\n```\n\nRequest: " + instruction;
        }
        request.messages.push_back(NextAI::FChatMessage::User(userContent));

        // Record the plain instruction in history (source is injected live, not stored).
        conversation_.push_back(NextAI::FChatMessage::User(instruction));

        generating_.store(true);
        hasPending_.store(false);

        std::thread([this, request = std::move(request)]() mutable
        {
            auto* svc = engine_.GetAIService();
            NextAI::FChatResponse response = svc->Chat(request);

            FScadGenResult result;
            if (response.success)
            {
                result.success = true;
                result.assistantText = response.content;
                result.scadSource = ExtractScadBlock(response.content);
            }
            else
            {
                result.success = false;
                result.error = response.errorMessage.empty() ? "generation failed" : response.errorMessage;
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                pending_ = std::move(result);
                hasPending_.store(true);
            }
            generating_.store(false);
        }).detach();
    }

    FScadGenResult ScadAIService::TakePendingResult()
    {
        FScadGenResult result;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            result = std::move(pending_);
            pending_ = FScadGenResult{};
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
