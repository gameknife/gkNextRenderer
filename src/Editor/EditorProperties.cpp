#include "EditorGUI.h"
#include "Assets/Model.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

#include "IconsFontAwesome6.h"
#include "Assets/Scene.hpp"

void Editor::GUI::ShowProperties()
{
    //ImGui::SetNextWindowPos(pt_P);
    //ImGui::SetNextWindowSize(pt_S);
    ImGui::Begin("Properties", NULL);
    {
        if (selected_obj_id != -1)
        {
            Assets::Node* selected_obj = current_scene->GetNodeByInstanceId(selected_obj_id);
            
            ImGui::PushFont(fontIcon_);
            ImGui::TextUnformatted(selected_obj->GetName().c_str());
            ImGui::PopFont();
            ImGui::Separator();
            ImGui::NewLine();

            ImGui::Text(ICON_FA_LOCATION_ARROW " Transform");
            ImGui::Separator();
            auto mat4 = selected_obj->WorldTransform();
            
            ImGui::BeginGroup();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            ImGui::Button("L"); ImGui::SameLine();
            ImGui::PopStyleColor();
            if ( ImGui::DragFloat3("##Location", &selected_obj->Translation().x) )
            {
                selected_obj->RecalcTransform(true);
                current_scene->MarkDirty();
            }
            ImGui::EndGroup();

            ImGui::BeginGroup();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
            ImGui::Button("R"); ImGui::SameLine();
            ImGui::PopStyleColor();
            static glm::vec3 eular = glm::eulerAngles(selected_obj->Rotation());
            if ( ImGui::DragFloat3("##Rotation", &eular.x) )
            {
                selected_obj->SetRotation( glm::quat(eular));
                selected_obj->RecalcTransform(true);
                current_scene->MarkDirty();
            }
            ImGui::EndGroup();

            ImGui::BeginGroup();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.8f, 1.0f));
            ImGui::Button("S"); ImGui::SameLine();
            ImGui::PopStyleColor();
            if ( ImGui::DragFloat3("##Scale", &selected_obj->Scale().x) )
            {
                selected_obj->RecalcTransform(true);
                current_scene->MarkDirty();
            }
            ImGui::EndGroup();

            ImGui::NewLine();
            ImGui::Text(ICON_FA_CUBE " Mesh");
            ImGui::Separator();
            int modelId = selected_obj->GetModel();
            ImGui::InputInt("##ModelId", &modelId, 1, 1, ImGuiInputTextFlags_ReadOnly);

            ImGui::NewLine();
            ImGui::Text( ICON_FA_CIRCLE_HALF_STROKE " Material");
            ImGui::Separator();
            
            if(current_scene != nullptr && modelId != -1)
            {
                auto& model = current_scene->Models()[modelId];
                auto& mats = model.Materials();
                for ( auto& mat : mats)
                {
                    int matIdx = mat;
                    ImGui::InputInt("##MaterialId", &matIdx, 1, 1, ImGuiInputTextFlags_ReadOnly);
                    ImGui::SameLine();
                    if( ImGui::Button(ICON_FA_LINK) )
                    {
                        selected_material = &(current_scene->Materials()[matIdx]);
                        ed_material = true;
                        OpenMaterialEditor();
                    }
                }
            }
        }
    }

    ImGui::End();
}
