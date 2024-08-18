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
        if (selected_obj != nullptr)
        {
            ImGui::PushFont(fontIcon_);
            ImGui::TextUnformatted(selected_obj->GetName().c_str());
            ImGui::PopFont();
            ImGui::Separator();
            ImGui::NewLine();
            


            ImGui::Text(ICON_FA_LOCATION_ARROW " Transform");
            ImGui::Separator();
            auto mat4 = selected_obj->WorldTransform();
            
            glm::vec3 scale;
            glm::quat rotation;
            glm::vec3 translation;
            glm::vec3 skew;
            glm::vec4 perspective;
            glm::decompose(mat4, scale, rotation, translation, skew, perspective);
            ImGui::BeginGroup();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
            ImGui::Button("L"); ImGui::SameLine();
            ImGui::PopStyleColor();
            ImGui::InputFloat3("##Location", &translation.x);
            ImGui::EndGroup();

            ImGui::BeginGroup();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
            ImGui::Button("R"); ImGui::SameLine();
            ImGui::PopStyleColor();
            ImGui::InputFloat4("##Rotation", &rotation.x);
            ImGui::EndGroup();

            ImGui::BeginGroup();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 1.0f, 1.0f));
            ImGui::Button("S"); ImGui::SameLine();
            ImGui::PopStyleColor();
            ImGui::InputFloat3("##Scale", &scale.x);
            ImGui::EndGroup();

            ImGui::NewLine();
            ImGui::Text(ICON_FA_CUBE " Mesh");
            ImGui::Separator();
            int modelId = selected_obj->GetModel();
            ImGui::InputInt("##ModelId", &modelId, 1, 1, ImGuiInputTextFlags_ReadOnly);

            ImGui::NewLine();
            ImGui::Text( ICON_FA_CIRCLE_HALF_STROKE " Material");
            ImGui::Separator();
            
            if(current_scene != nullptr)
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
