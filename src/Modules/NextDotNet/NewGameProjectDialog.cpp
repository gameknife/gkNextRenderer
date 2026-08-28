#include "Modules/NextDotNet/NewGameProjectDialog.hpp"

#include "Modules/NextDotNet/DotNetRuntime.hpp"
#include "Modules/NextDotNet/ManagedGameSession.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstring>

namespace Modules::NextDotNet
{
    namespace
    {
        constexpr const char* kPopupTitle = "New Game Project";

        template <size_t N>
        void SetBuffer(std::array<char, N>& buffer, std::string_view value)
        {
            const size_t length = std::min(value.size(), N - 1);
            std::memcpy(buffer.data(), value.data(), length);
            buffer[length] = '\0';
        }

        void DrawHint(const char* text)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.66f, 0.72f, 1.0f));
            ImGui::TextWrapped("%s", text);
            ImGui::PopStyleColor();
        }
    }

    std::string FNewGameProjectDialog::UnavailableReason()
    {
        if (DotNetRuntime::ManagedSourceRoot().empty())
        {
            return "this build has no C# sources to write into (an installed build never does)";
        }
        if (GameTemplateRoot().empty())
        {
            return "no templates found under assets/templates/games";
        }
        return {};
    }

    void FNewGameProjectDialog::Open()
    {
        templates_ = ScanGameTemplates();
        unavailableReason_ = UnavailableReason();
        ResetForm();
        open_ = true;
        requestOpen_ = true;
    }

    void FNewGameProjectDialog::Close()
    {
        open_ = false;
        requestOpen_ = false;
        phase_ = EPhase::Editing;
    }

    void FNewGameProjectDialog::ResetForm()
    {
        phase_ = EPhase::Editing;
        selectedTemplate_ = 0;
        projectName_[0] = '\0';
        displayName_[0] = '\0';
        gameId_[0] = '\0';
        displayNameEdited_ = false;
        gameIdEdited_ = false;
        buildAfterCreate_ = true;
        validationError_.clear();
        buildError_.clear();
        built_ = false;
        result_ = {};
    }

    const FGameTemplate* FNewGameProjectDialog::SelectedTemplate() const
    {
        if (selectedTemplate_ < 0 || static_cast<size_t>(selectedTemplate_) >= templates_.size())
        {
            return nullptr;
        }
        return &templates_[static_cast<size_t>(selectedTemplate_)];
    }

    void FNewGameProjectDialog::SyncDerivedNames()
    {
        const std::string projectName = projectName_.data();
        if (!displayNameEdited_)
        {
            SetBuffer(displayName_, projectName);
        }
        if (!gameIdEdited_)
        {
            SetBuffer(gameId_, DeriveGameId(projectName));
        }
    }

    void FNewGameProjectDialog::PerformWork(ManagedGameSession* session)
    {
        const FGameTemplate* gameTemplate = SelectedTemplate();
        if (gameTemplate == nullptr)
        {
            validationError_ = "pick a template";
            phase_ = EPhase::Editing;
            return;
        }

        FNewGameRequest request;
        request.templateId = gameTemplate->id;
        request.projectName = projectName_.data();
        request.displayName = displayName_.data();
        request.gameId = gameId_.data();

        result_ = CreateManagedGame(*gameTemplate, request);
        if (!result_.created)
        {
            validationError_ = result_.error;
            phase_ = EPhase::Editing;
            return;
        }

        // Publishing is separate on purpose: the project exists and is correct whether or not the
        // .NET SDK is around to build it, and saying so beats rolling back a good project because
        // dotnet was missing.
        built_ = false;
        buildError_.clear();
        if (buildAfterCreate_ && session != nullptr)
        {
            built_ = session->RebuildGame(result_.manifest, buildError_);
        }
        phase_ = EPhase::Done;
    }

    FNewGameProjectOutcome FNewGameProjectDialog::Draw(ManagedGameSession* session)
    {
        FNewGameProjectOutcome outcome;
        if (!open_)
        {
            return outcome;
        }

        // The frame that said "creating..." has been presented; do the blocking work now.
        const bool completingWork = phase_ == EPhase::Working;
        if (completingWork)
        {
            PerformWork(session);
            if (phase_ == EPhase::Done)
            {
                outcome.created = true;
                outcome.gameId = result_.manifest.id;
                outcome.built = built_;
            }
        }

        if (requestOpen_)
        {
            requestOpen_ = false;
            ImGui::OpenPopup(kPopupTitle);
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->GetCenter().x, viewport->GetCenter().y), ImGuiCond_Appearing,
                                ImVec2(0.5f, 0.5f));
        // Clamped to the window: a host as small as the launcher's default 860x540 would otherwise
        // get a modal with its buttons off the bottom edge.
        ImGui::SetNextWindowSize(ImVec2(std::min(760.0f, viewport->WorkSize.x - 32.0f),
                                        std::min(470.0f, viewport->WorkSize.y - 32.0f)),
                                 ImGuiCond_Appearing);

        bool stayOpen = true;
        if (!ImGui::BeginPopupModal(kPopupTitle, &stayOpen, ImGuiWindowFlags_NoSavedSettings))
        {
            // The popup was dismissed by the window close button or an Escape.
            open_ = false;
            return outcome;
        }

        // Escape closes the dialog. ImGui only does this for itself when keyboard navigation is
        // active, which a host driving ImGui from raw SDL events may never turn on; the
        // IsAnyItemActive guard leaves Escape to a text field that is being edited, where it means
        // "revert this field" rather than "throw the form away".
        if (phase_ != EPhase::Working && !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            ImGui::CloseCurrentPopup();
            open_ = false;
            ImGui::EndPopup();
            return outcome;
        }

        if (!unavailableReason_.empty())
        {
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "Cannot create a project here.");
            ImGui::Spacing();
            DrawHint(unavailableReason_.c_str());
            ImGui::Spacing();
            if (ImGui::Button("Close", ImVec2(120.0f, 0.0f)))
            {
                ImGui::CloseCurrentPopup();
                open_ = false;
            }
            ImGui::EndPopup();
            return outcome;
        }

        if (phase_ == EPhase::Done)
        {
            ImGui::TextColored(ImVec4(0.48f, 0.90f, 0.66f, 1.0f), "Created '%s'.", result_.manifest.id.c_str());
            ImGui::Spacing();
            ImGui::Text("Project    %s", result_.projectDirectory.string().c_str());
            ImGui::Text("Manifest   %s", result_.manifestFile.string().c_str());
            ImGui::Spacing();

            if (built_)
            {
                ImGui::TextColored(ImVec4(0.48f, 0.90f, 0.66f, 1.0f), "Published — it is ready to play.");
            }
            else if (!buildError_.empty())
            {
                ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.35f, 1.0f), "Not built: %s", buildError_.c_str());
                DrawHint("The project is written and correct. Build it with the Rebuild C# action, or "
                         "with 'dotnet publish' from the command line.");
            }
            else
            {
                DrawHint("Not built yet. Use the Rebuild C# action to publish it.");
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            DrawHint("Next: run 'gnb dotnet sln' so the project joins assets/csharp/GkNextManaged.sln "
                     "— open that solution rather than the bare csproj, or the IDE will not load "
                     "GkNext.Engine alongside it. The generated README lists the rest.");

            ImGui::Spacing();
            if (ImGui::Button("Done", ImVec2(140.0f, 0.0f)))
            {
                ImGui::CloseCurrentPopup();
                open_ = false;
            }
            ImGui::EndPopup();
            return outcome;
        }

        const bool working = phase_ == EPhase::Working;
        ImGui::BeginDisabled(working);

        // --- template list ---------------------------------------------------------------------
        ImGui::BeginChild("##templates", ImVec2(230.0f, -46.0f), ImGuiChildFlags_Borders);
        for (size_t i = 0; i < templates_.size(); ++i)
        {
            const bool selected = static_cast<int>(i) == selectedTemplate_;
            if (ImGui::Selectable(templates_[i].displayName.c_str(), selected))
            {
                selectedTemplate_ = static_cast<int>(i);
            }
        }
        if (templates_.empty())
        {
            ImGui::TextDisabled("no templates");
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // --- description + form ----------------------------------------------------------------
        ImGui::BeginChild("##form", ImVec2(0.0f, -46.0f));
        if (const FGameTemplate* gameTemplate = SelectedTemplate(); gameTemplate != nullptr)
        {
            ImGui::TextUnformatted(gameTemplate->displayName.c_str());
            ImGui::Spacing();
            DrawHint(gameTemplate->description.c_str());
            for (const std::string& highlight : gameTemplate->highlights)
            {
                ImGui::Bullet();
                ImGui::TextWrapped("%s", highlight.c_str());
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        }

        ImGui::SetNextItemWidth(-160.0f);
        if (ImGui::InputText("Project name", projectName_.data(), projectName_.size()))
        {
            SyncDerivedNames();
        }
        DrawHint("PascalCase. Names assets/csharp/<name>/, the assembly, the namespace and the class.");

        ImGui::Spacing();
        ImGui::SetNextItemWidth(-160.0f);
        if (ImGui::InputText("Display name", displayName_.data(), displayName_.size()))
        {
            displayNameEdited_ = true;
        }

        ImGui::SetNextItemWidth(-160.0f);
        if (ImGui::InputText("Game id", gameId_.data(), gameId_.size()))
        {
            gameIdEdited_ = true;
        }
        DrawHint("Lowercase. Names the manifest and the publish directory under bin/csharp/.");

        if (session != nullptr)
        {
            ImGui::Spacing();
            ImGui::Checkbox("Publish the project after creating it (takes a few seconds)",
                            &buildAfterCreate_);
        }

        // Validated every frame rather than on submit: a form that only complains after the click
        // makes the user guess which field it meant.
        if (const FGameTemplate* gameTemplate = SelectedTemplate(); gameTemplate != nullptr)
        {
            FNewGameRequest request;
            request.templateId = gameTemplate->id;
            request.projectName = projectName_.data();
            request.displayName = displayName_.data();
            request.gameId = gameId_.data();
            std::string error;
            const bool valid = ValidateNewGameRequest(request, error);
            // An empty form is not an error yet; it is a form nobody has filled in.
            validationError_ = valid || projectName_[0] == '\0' ? std::string() : error;
        }

        if (!validationError_.empty())
        {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%s", validationError_.c_str());
        }
        ImGui::EndChild();

        ImGui::EndDisabled();

        ImGui::Separator();

        if (working)
        {
            ImGui::TextColored(ImVec4(0.95f, 0.82f, 0.42f, 1.0f),
                               buildAfterCreate_ && session != nullptr
                                   ? "Creating and publishing... this blocks for a few seconds."
                                   : "Creating...");
            ImGui::EndPopup();
            return outcome;
        }

        const bool canCreate = SelectedTemplate() != nullptr && projectName_[0] != '\0' &&
                               validationError_.empty();
        ImGui::BeginDisabled(!canCreate);
        if (ImGui::Button("Create", ImVec2(140.0f, 0.0f)))
        {
            // Only recorded here. The work runs at the top of the next Draw, so the frame that says
            // so actually reaches the screen before the process stops responding.
            phase_ = EPhase::Working;
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(140.0f, 0.0f)))
        {
            ImGui::CloseCurrentPopup();
            open_ = false;
        }

        ImGui::EndPopup();
        return outcome;
    }
}
