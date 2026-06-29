#include "EditorUi.hpp"

#include "EditorDragDrop.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/Data/Material.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Rendering/Preview/RenderViewServices.hpp"
#include "Engine/Runtime/Command/ICommand.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Editor/UserInterface.hpp"

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstring>
#include <optional>

namespace Editor
{
    namespace
    {
        bool gShouldFocusEditor = true;
        float gPreviewYaw = 0.0f;
        float gPreviewPitch = 0.0f;
        float gPreviewDistance = 4.0f;

        struct FTrackedEdit
        {
            uint32_t materialId = InvalidId;
            std::string key;
            Assets::FMaterial before;
        };

        std::optional<FTrackedEdit> gTrackedEdit;

        bool SameMaterialPayload(const Assets::FMaterial& a, const Assets::FMaterial& b)
        {
            return a.name_ == b.name_ &&
                std::memcmp(&a.gpuMaterial_, &b.gpuMaterial_, sizeof(Assets::Material)) == 0;
        }

        uint32_t FindSelectedMaterialId(EditorContext& ctx, EditorUiState& ui)
        {
            if (ui.selectedMaterialId < ctx.scene.Materials().size())
            {
                ui.selected_material = &ctx.scene.Materials()[ui.selectedMaterialId];
                return ui.selectedMaterialId;
            }

            if (ui.selected_material == nullptr)
            {
                return InvalidId;
            }

            auto& materials = ctx.scene.Materials();
            for (uint32_t i = 0; i < materials.size(); ++i)
            {
                if (&materials[i] == ui.selected_material)
                {
                    ui.selectedMaterialId = i;
                    return i;
                }
            }
            return InvalidId;
        }

        const char* MaterialModelName(Assets::Material::Enum model)
        {
            switch (model)
            {
            case Assets::Material::Enum::Lambertian:
                return "Lambertian";
            case Assets::Material::Enum::Metallic:
                return "Metallic";
            case Assets::Material::Enum::Dielectric:
                return "Dielectric";
            case Assets::Material::Enum::Isotropic:
                return "Isotropic";
            case Assets::Material::Enum::DiffuseLight:
                return "DiffuseLight";
            case Assets::Material::Enum::Mixture:
                return "Mixture";
            default:
                return "Unknown";
            }
        }

        bool IsEmissive(const Assets::Material& material)
        {
            return material.MaterialModel == Assets::Material::Enum::DiffuseLight;
        }

        glm::vec3 DecodeEmissiveColor(const glm::vec3 radiance)
        {
            const float strength = std::max(std::max(radiance.r, radiance.g), radiance.b);
            if (strength <= 0.0001f)
            {
                return glm::vec3(1.0f);
            }
            return radiance / strength;
        }

        float DecodeEmissiveStrength(const glm::vec3 radiance)
        {
            return std::max(std::max(radiance.r, radiance.g), radiance.b);
        }

        void MarkMaterialEdited(EditorContext& ctx)
        {
            ctx.scene.MarkMaterialsDirty();
            ctx.engine.SetProgressiveRendering(false, false);
        }

        class MaterialEditCommand final : public Runtime::Command::ICommand
        {
        public:
            MaterialEditCommand(Assets::Scene& scene,
                                uint32_t materialId,
                                Assets::FMaterial before,
                                Assets::FMaterial after,
                                std::string fieldName)
                : scene_(scene)
                , materialId_(materialId)
                , before_(std::move(before))
                , after_(std::move(after))
                , fieldName_(std::move(fieldName))
            {
            }

            bool Execute() override
            {
                return Apply(after_);
            }

            bool Undo() override
            {
                return Apply(before_);
            }

            std::string GetDescription() const override
            {
                return fmt::format("Edit material {}", fieldName_);
            }

            bool CanMergeWith(const Runtime::Command::ICommand* other) const override
            {
                const auto* otherCommand = dynamic_cast<const MaterialEditCommand*>(other);
                return otherCommand != nullptr &&
                    &scene_ == &otherCommand->scene_ &&
                    materialId_ == otherCommand->materialId_ &&
                    fieldName_ == otherCommand->fieldName_;
            }

            void MergeWith(const Runtime::Command::ICommand* other) override
            {
                const auto* otherCommand = dynamic_cast<const MaterialEditCommand*>(other);
                if (otherCommand != nullptr)
                {
                    after_ = otherCommand->after_;
                }
            }

        private:
            bool Apply(const Assets::FMaterial& value)
            {
                if (materialId_ >= scene_.Materials().size())
                {
                    return false;
                }
                scene_.Materials()[materialId_] = value;
                scene_.MarkMaterialsDirty();
                return true;
            }

            Assets::Scene& scene_;
            uint32_t materialId_ = InvalidId;
            Assets::FMaterial before_;
            Assets::FMaterial after_;
            std::string fieldName_;
        };

        void CommitMaterialEdit(EditorContext& ctx,
                                uint32_t materialId,
                                const Assets::FMaterial& before,
                                const Assets::FMaterial& after,
                                const std::string& fieldName)
        {
            if (materialId == InvalidId || materialId >= ctx.scene.Materials().size() ||
                SameMaterialPayload(before, after))
            {
                return;
            }

            ctx.engine.GetCommandHistory().Execute(std::make_unique<MaterialEditCommand>(
                ctx.scene,
                materialId,
                before,
                after,
                fieldName));
        }

        void TrackItemEdit(EditorContext& ctx,
                           uint32_t materialId,
                           const char* key,
                           const Assets::FMaterial& fallbackBefore)
        {
            if (ImGui::IsItemActivated())
            {
                gTrackedEdit = FTrackedEdit{materialId, key, fallbackBefore};
            }
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                const Assets::FMaterial before =
                    (gTrackedEdit && gTrackedEdit->materialId == materialId && gTrackedEdit->key == key)
                    ? gTrackedEdit->before
                    : fallbackBefore;
                CommitMaterialEdit(ctx, materialId, before, ctx.scene.Materials()[materialId], key);
                gTrackedEdit.reset();
            }
        }

        bool DrawFloatField(EditorContext& ctx,
                            uint32_t materialId,
                            const char* label,
                            float& value,
                            float minValue,
                            float maxValue,
                            const char* key)
        {
            Assets::FMaterial before = ctx.scene.Materials()[materialId];
            ImGui::PushID(key);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::SameLine(180.0f);
            ImGui::SetNextItemWidth(-FLT_MIN);
            const bool changed = ImGui::SliderFloat("##value", &value, minValue, maxValue, "%.3f");
            TrackItemEdit(ctx, materialId, key, before);
            if (changed)
            {
                MarkMaterialEdited(ctx);
            }
            ImGui::PopID();
            return changed;
        }

        bool DrawColorField(EditorContext& ctx,
                            const char* label,
                            glm::vec3& value,
                            ImGuiColorEditFlags flags = 0)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::SameLine(180.0f);
            ImGui::SetNextItemWidth(-FLT_MIN);
            const bool changed = ImGui::ColorEdit3("##color", &value.x, flags);
            if (changed)
            {
                MarkMaterialEdited(ctx);
            }
            return changed;
        }

        void DrawTextureSlot(EditorContext& ctx,
                             uint32_t materialId,
                             const char* label,
                             int32_t& textureId,
                             const char* key)
        {
            ImGui::PushID(key);
            Assets::FMaterial before = ctx.scene.Materials()[materialId];

            ImGui::SeparatorText(label);
            if (textureId >= 0)
            {
                ImTextureID tex = ctx.ui.RequestImTextureId(static_cast<uint32_t>(textureId));
                if (tex != 0)
                {
                    ImGui::Image(tex, ImVec2(64.0f, 64.0f));
                    ImGui::SameLine();
                }
            }

            int current = textureId;
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Slot");
            ImGui::SameLine(180.0f);
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::InputInt("##slot", &current))
            {
                textureId = current < 0 ? -1 : current;
                MarkMaterialEdited(ctx);
            }
            TrackItemEdit(ctx, materialId, key, before);

            ImGui::SameLine();
            if (ImGui::Button("Clear"))
            {
                textureId = -1;
                MarkMaterialEdited(ctx);
                CommitMaterialEdit(ctx, materialId, before, ctx.scene.Materials()[materialId], key);
            }

            if (Assets::GlobalTexturePool* pool = Assets::GlobalTexturePool::GetInstance())
            {
                ImGui::Button("Drop Texture Here", ImVec2(-FLT_MIN, 0.0f));
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kEditorDragDropPayload))
                    {
                        if (payload->DataSize == sizeof(EditorDragDropPayload))
                        {
                            const auto* data = static_cast<const EditorDragDropPayload*>(payload->Data);
                            if (data->type == EEditorDragPayloadType::Texture)
                            {
                                const std::string path = data->path;
                                textureId = path.empty()
                                    ? static_cast<int32_t>(data->textureId)
                                    : static_cast<int32_t>(Assets::GlobalTexturePool::LoadTexture(
                                        path,
                                        key == std::string("albedo texture") ||
                                        key == std::string("emissive texture")));
                                MarkMaterialEdited(ctx);
                                CommitMaterialEdit(ctx, materialId, before, ctx.scene.Materials()[materialId], key);
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                const std::string comboPreview = textureId >= 0 ? fmt::format("Texture {}", textureId) : "None";
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Assign");
                ImGui::SameLine(180.0f);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::BeginCombo("##assign", comboPreview.c_str()))
                {
                    if (ImGui::Selectable("None", textureId < 0))
                    {
                        textureId = -1;
                        MarkMaterialEdited(ctx);
                        CommitMaterialEdit(ctx, materialId, before, ctx.scene.Materials()[materialId], key);
                    }
                    for (const auto& [name, binding] : pool->TotalTextureMap())
                    {
                        const bool selected = textureId == static_cast<int32_t>(binding.GlobalIdx_);
                        const std::string item = fmt::format("{}: {}", binding.GlobalIdx_, name);
                        if (ImGui::Selectable(item.c_str(), selected))
                        {
                            textureId = static_cast<int32_t>(binding.GlobalIdx_);
                            MarkMaterialEdited(ctx);
                            CommitMaterialEdit(ctx, materialId, before, ctx.scene.Materials()[materialId], key);
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            ImGui::PopID();
        }

        void RefreshSelectedMaterialPointer(EditorContext& ctx, EditorUiState& ui, uint32_t materialId)
        {
            if (materialId < ctx.scene.Materials().size())
            {
                ui.selectedMaterialId = materialId;
                ui.selected_material = &ctx.scene.Materials()[materialId];
            }
            else
            {
                ui.selectedMaterialId = InvalidId;
                ui.selected_material = nullptr;
            }
        }

        void AddNewMaterial(EditorContext& ctx, EditorUiState& ui)
        {
            Assets::FMaterial material;
            material.gpuMaterial_ = Assets::Material::Lambertian(glm::vec3(0.75f));
            material.name_ = fmt::format("Material_{}", ctx.scene.Materials().size());
            const uint32_t newId = ctx.scene.AddMaterial(material);
            RefreshSelectedMaterialPointer(ctx, ui, newId);
            OpenMaterialEditor(ctx, ui);
        }

        void DuplicateMaterial(EditorContext& ctx, EditorUiState& ui, uint32_t materialId)
        {
            if (materialId >= ctx.scene.Materials().size())
            {
                return;
            }
            const uint32_t newId = ctx.scene.DuplicateMaterial(materialId);
            if (newId == static_cast<uint32_t>(-1))
            {
                return;
            }
            RefreshSelectedMaterialPointer(ctx, ui, newId);
            OpenMaterialEditor(ctx, ui);
        }

        void DeleteSelectedMaterial(EditorContext& ctx, EditorUiState& ui, uint32_t materialId)
        {
            uint32_t selectedMaterialId = materialId;
            if (ctx.scene.RemoveMaterial(materialId, &selectedMaterialId))
            {
                RefreshSelectedMaterialPointer(ctx, ui, selectedMaterialId);
                OpenMaterialEditor(ctx, ui);
            }
        }

        void ApplyMaterialPreset(EditorContext& ctx,
                                 EditorUiState& ui,
                                 uint32_t materialId,
                                 const char* presetName,
                                 Assets::Material material)
        {
            if (materialId >= ctx.scene.Materials().size())
            {
                return;
            }
            Assets::FMaterial before = ctx.scene.Materials()[materialId];
            ctx.scene.Materials()[materialId].gpuMaterial_ = material;
            CommitMaterialEdit(ctx, materialId, before, ctx.scene.Materials()[materialId], presetName);
            MarkMaterialEdited(ctx);
            OpenMaterialEditor(ctx, ui);
        }

        void DrawPreview(EditorContext& ctx, const Assets::FMaterial& material)
        {
            Vulkan::MaterialPreviewRenderer& preview = ctx.engine.GetRenderer().ViewServices().MaterialPreview();
            preview.SetEnabled(true);
            preview.SetPreviewMaterial(material);
            preview.SetCameraOrbit(gPreviewYaw, gPreviewPitch, gPreviewDistance);

            const float width = ImGui::GetContentRegionAvail().x;
            const float size = std::clamp(width, 192.0f, 320.0f);
            preview.SetRenderExtent({static_cast<uint32_t>(size), static_cast<uint32_t>(size)});

            if (preview.IsReady())
            {
                ImTextureID tex = ctx.ui.RequestImTextureIdRaw(preview.SampleSlot());
                ImGui::Image(tex, ImVec2(size, size));
            }
            else
            {
                ImGui::BeginChild("MaterialPreviewInitializing", ImVec2(size, size), true);
                const char* text = "Initializing preview...";
                const ImVec2 textSize = ImGui::CalcTextSize(text);
                ImGui::SetCursorPos(ImVec2((size - textSize.x) * 0.5f, (size - textSize.y) * 0.5f));
                ImGui::TextUnformatted(text);
                ImGui::EndChild();
            }

            ImGui::SetNextItemWidth(size);
            if (ImGui::SliderAngle("Orbit", &gPreviewYaw, -180.0f, 180.0f))
            {
                preview.SetCameraOrbit(gPreviewYaw, gPreviewPitch, gPreviewDistance);
            }
            ImGui::SetNextItemWidth(size);
            if (ImGui::SliderAngle("Tilt", &gPreviewPitch, -55.0f, 55.0f))
            {
                preview.SetCameraOrbit(gPreviewYaw, gPreviewPitch, gPreviewDistance);
            }
            ImGui::SetNextItemWidth(size);
            if (ImGui::SliderFloat("Zoom", &gPreviewDistance, 2.0f, 7.0f, "%.2f"))
            {
                preview.SetCameraOrbit(gPreviewYaw, gPreviewPitch, gPreviewDistance);
            }
        }

        void DrawToolbar(EditorContext& ctx, EditorUiState& ui, uint32_t materialId)
        {
            if (ImGui::Button("New"))
            {
                AddNewMaterial(ctx, ui);
            }
            ImGui::SameLine();
            if (ImGui::Button("Duplicate") && materialId != InvalidId)
            {
                DuplicateMaterial(ctx, ui, materialId);
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete") && materialId != InvalidId)
            {
                ImGui::OpenPopup("DeleteMaterialConfirm");
            }

            ImGui::SameLine();
            if (ImGui::BeginMenu("Presets"))
            {
                if (ImGui::MenuItem("Matte Plastic"))
                {
                    Assets::Material material = Assets::Material::Mixture(glm::vec3(0.78f, 0.18f, 0.12f), 0.62f);
                    material.Metalness = 0.0f;
                    material.NormalTextureScale = 1.0f;
                    ApplyMaterialPreset(ctx, ui, materialId, "preset plastic", material);
                }
                if (ImGui::MenuItem("Brushed Metal"))
                {
                    Assets::Material material = Assets::Material::Metallic(glm::vec3(0.82f, 0.78f, 0.68f), 0.28f);
                    material.Metalness = 1.0f;
                    material.NormalTextureScale = 1.0f;
                    ApplyMaterialPreset(ctx, ui, materialId, "preset metal", material);
                }
                if (ImGui::MenuItem("Clear Glass"))
                {
                    Assets::Material material = Assets::Material::Dielectric(1.46f, 0.02f);
                    material.Diffuse = glm::vec4(0.92f, 0.97f, 1.0f, 0.32f);
                    material.RefractionIndex2 = 1.0f;
                    ApplyMaterialPreset(ctx, ui, materialId, "preset glass", material);
                }
                if (ImGui::MenuItem("Warm Emissive"))
                {
                    Assets::Material material = Assets::Material::DiffuseLight(glm::vec3(12.0f, 7.5f, 3.2f));
                    ApplyMaterialPreset(ctx, ui, materialId, "preset emissive", material);
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginPopupModal("DeleteMaterialConfirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextUnformatted("Delete this material and remap references to the default material?");
                if (ImGui::Button("Delete", ImVec2(120.0f, 0.0f)))
                {
                    DeleteSelectedMaterial(ctx, ui, materialId);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
                {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }

        void DrawMaterialProperties(EditorContext& ctx, EditorUiState& ui, uint32_t materialId)
        {
            Assets::FMaterial& fMaterial = ctx.scene.Materials()[materialId];
            Assets::Material& material = fMaterial.gpuMaterial_;

            char nameBuffer[256]{};
            std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", fMaterial.name_.c_str());
            Assets::FMaterial beforeName = fMaterial;
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Name");
            ImGui::SameLine(180.0f);
            ImGui::SetNextItemWidth(-FLT_MIN);
            const bool nameChanged = ImGui::InputText("##name", nameBuffer, sizeof(nameBuffer));
            if (nameChanged)
            {
                if (fMaterial.name_ != nameBuffer)
                {
                    fMaterial.name_ = nameBuffer;
                }
            }
            TrackItemEdit(ctx, materialId, "name", beforeName);

            int model = static_cast<int>(material.MaterialModel);
            Assets::FMaterial beforeModel = fMaterial;
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Material Model");
            ImGui::SameLine(180.0f);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##materialModel", MaterialModelName(material.MaterialModel)))
            {
                for (int i = 0; i <= 5; ++i)
                {
                    const auto candidate = static_cast<Assets::Material::Enum>(i);
                    if (ImGui::Selectable(MaterialModelName(candidate), model == i))
                    {
                        material.MaterialModel = candidate;
                        if (candidate == Assets::Material::Enum::Dielectric &&
                            material.RefractionIndex2 <= 0.0001f)
                        {
                            material.RefractionIndex2 = material.RefractionIndex;
                        }
                        MarkMaterialEdited(ctx);
                        CommitMaterialEdit(ctx, materialId, beforeModel, fMaterial, "material model");
                    }
                }
                ImGui::EndCombo();
            }

            if (IsEmissive(material))
            {
                Assets::FMaterial beforeEmissiveColor = fMaterial;
                glm::vec3 emissiveColor = DecodeEmissiveColor(glm::vec3(material.Diffuse));
                float emissiveStrength = DecodeEmissiveStrength(glm::vec3(material.Diffuse));
                if (DrawColorField(ctx, "Emissive Color", emissiveColor, ImGuiColorEditFlags_Float))
                {
                    material.Diffuse = glm::vec4(emissiveColor * std::max(emissiveStrength, 0.0f), material.Diffuse.a);
                    MarkMaterialEdited(ctx);
                }
                TrackItemEdit(ctx, materialId, "emissive color", beforeEmissiveColor);

                Assets::FMaterial beforeEmissiveStrength = fMaterial;
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Emissive Strength");
                ImGui::SameLine(180.0f);
                ImGui::SetNextItemWidth(-FLT_MIN);
                const bool strengthChanged = ImGui::DragFloat(
                    "##emissiveStrength",
                    &emissiveStrength,
                    0.05f,
                    0.0f,
                    10000.0f,
                    "%.3f");
                if (strengthChanged)
                {
                    material.Diffuse = glm::vec4(emissiveColor * std::max(emissiveStrength, 0.0f), material.Diffuse.a);
                    MarkMaterialEdited(ctx);
                }
                TrackItemEdit(ctx, materialId, "emissive strength", beforeEmissiveStrength);
            }
            else
            {
                Assets::FMaterial beforeAlbedo = fMaterial;
                glm::vec3 albedo = glm::vec3(material.Diffuse);
                if (DrawColorField(ctx, "Albedo", albedo))
                {
                    material.Diffuse = glm::vec4(albedo, material.Diffuse.a);
                    MarkMaterialEdited(ctx);
                }
                TrackItemEdit(ctx, materialId, "albedo", beforeAlbedo);
                DrawFloatField(ctx, materialId, "Opacity", material.Diffuse.a, 0.0f, 1.0f, "opacity");
            }

            if (material.MaterialModel == Assets::Material::Enum::Mixture ||
                material.MaterialModel == Assets::Material::Enum::Metallic)
            {
                DrawFloatField(ctx, materialId, "Metalness", material.Metalness, 0.0f, 1.0f, "metalness");
            }

            if (material.MaterialModel != Assets::Material::Enum::Dielectric &&
                material.MaterialModel != Assets::Material::Enum::DiffuseLight)
            {
                DrawFloatField(ctx, materialId, "Roughness", material.Fuzziness, 0.0f, 1.0f, "roughness");
            }

            if (material.MaterialModel == Assets::Material::Enum::Dielectric ||
                material.MaterialModel == Assets::Material::Enum::Mixture)
            {
                DrawFloatField(ctx, materialId, "IOR", material.RefractionIndex, 1.0f, 2.5f, "ior");
            }
            if (material.MaterialModel == Assets::Material::Enum::Dielectric)
            {
                DrawFloatField(ctx, materialId, "IOR (back)", material.RefractionIndex2, 1.0f, 2.5f, "ior2");
            }

            DrawFloatField(ctx, materialId, "Normal Scale", material.NormalTextureScale, 0.0f, 2.0f, "normal scale");

            if (ImGui::CollapsingHeader("Textures", ImGuiTreeNodeFlags_DefaultOpen))
            {
                DrawTextureSlot(ctx, materialId, "Albedo", material.DiffuseTextureId, "albedo texture");
                DrawTextureSlot(ctx, materialId, "MRA (R=AO, G=Roughness, B=Metalness)", material.MRATextureId,
                                "mra texture");
                DrawTextureSlot(ctx, materialId, "Normal", material.NormalTextureId, "normal texture");
                DrawTextureSlot(ctx, materialId, "Emissive", material.EmissiveTextureId, "emissive texture");
            }
        }
    } // namespace

    void OpenMaterialEditor(EditorContext& ctx, EditorUiState& ui)
    {
        FindSelectedMaterialId(ctx, ui);
        gShouldFocusEditor = true;
    }

    void DrawMaterialEditorPanel(EditorContext& ctx, EditorUiState& ui)
    {
        if (gShouldFocusEditor)
        {
            ImGui::SetNextWindowSize(ImVec2(1280, 800), ImGuiCond_FirstUseEver);
            ImGui::SetWindowFocus("Material Editor");
            ImGuiWindowClass windowClass;
            windowClass.ClassId = ImGui::GetID("Material Editor");
            windowClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_TopMost | ImGuiViewportFlags_NoAutoMerge;
            ImGui::SetNextWindowClass(&windowClass);
            gShouldFocusEditor = false;
        }

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 16));
        const bool visible = ImGui::Begin("Material Editor", &ui.ed_material,
                                          ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse);
        ImGui::PopStyleVar();

        Vulkan::MaterialPreviewRenderer& preview = ctx.engine.GetRenderer().ViewServices().MaterialPreview();
        if (!ui.ed_material || !visible)
        {
            preview.SetEnabled(false);
            ImGui::End();
            return;
        }

        uint32_t materialId = FindSelectedMaterialId(ctx, ui);
        if (materialId == InvalidId || ctx.scene.Materials().empty())
        {
            preview.SetEnabled(false);
            if (ImGui::Button("New Material"))
            {
                AddNewMaterial(ctx, ui);
            }
            ImGui::End();
            return;
        }

        DrawToolbar(ctx, ui, materialId);
        materialId = FindSelectedMaterialId(ctx, ui);
        if (materialId == InvalidId)
        {
            ImGui::End();
            return;
        }

        Assets::FMaterial& selected = ctx.scene.Materials()[materialId];
        constexpr float previewColumnWidth = 340.0f;
        if (ImGui::BeginTable("MaterialEditorLayout", 2,
                              ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
        {
            ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthFixed, previewColumnWidth);
            ImGui::TableSetupColumn("Properties", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            DrawPreview(ctx, selected);

            ImGui::TableSetColumnIndex(1);
            ImGui::Text("Material #%u", materialId);
            ImGui::Separator();
            DrawMaterialProperties(ctx, ui, materialId);

            ImGui::EndTable();
        }

        ImGui::End();
    }
} // namespace Editor
