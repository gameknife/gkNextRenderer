#pragma once

#include "Utilities/Glm.hpp"
#include <functional>
#include <string>
#include <vector>

namespace Assets
{
    class Node;
    class Model;
    class Texture;
    struct Material;
    struct LightObject;
    struct CameraInitialSate;
}

class SceneList final
{
public:
  
    static void Simple(Assets::CameraInitialSate& camera, std::vector<Assets::Node>& nodes,
                               std::vector<Assets::Model>& models, std::vector<Assets::Texture>& textures,
                               std::vector<Assets::Material>& materials, std::vector<Assets::LightObject>& lights);
    static void Complex(Assets::CameraInitialSate& camera, std::vector<Assets::Node>& nodes,
                           std::vector<Assets::Model>& models, std::vector<Assets::Texture>& textures,
                           std::vector<Assets::Material>& materials, std::vector<Assets::LightObject>& lights);
    static void RayTracingInOneWeekend(Assets::CameraInitialSate& camera, std::vector<Assets::Node>& nodes,
                                       std::vector<Assets::Model>& models, std::vector<Assets::Texture>& textures,
                                       std::vector<Assets::Material>& materials,
                                       std::vector<Assets::LightObject>& lights);
    static void CornellBox(Assets::CameraInitialSate& camera, std::vector<Assets::Node>& nodes,
                           std::vector<Assets::Model>& models, std::vector<Assets::Texture>& textures,
                           std::vector<Assets::Material>& materials, std::vector<Assets::LightObject>& lights);
    static void CornellBoxLucy(Assets::CameraInitialSate& camera, std::vector<Assets::Node>& nodes,
                               std::vector<Assets::Model>& models, std::vector<Assets::Texture>& textures,
                               std::vector<Assets::Material>& materials, std::vector<Assets::LightObject>& lights);
    static void LivingRoom(Assets::CameraInitialSate& camera, std::vector<Assets::Node>& nodes,
                           std::vector<Assets::Model>& models, std::vector<Assets::Texture>& textures,
                           std::vector<Assets::Material>& materials, std::vector<Assets::LightObject>& lights);
    static void Kitchen(Assets::CameraInitialSate& camera, std::vector<Assets::Node>& nodes, std::vector<Assets::Model>& models,
                        std::vector<Assets::Texture>& textures, std::vector<Assets::Material>& materials,
                        std::vector<Assets::LightObject>& lights);
    static void LuxBall(Assets::CameraInitialSate& camera, std::vector<Assets::Node>& nodes, std::vector<Assets::Model>& models,
                        std::vector<Assets::Texture>& textures, std::vector<Assets::Material>& materials,
                        std::vector<Assets::LightObject>& lights);
    static void Still(Assets::CameraInitialSate& camera, std::vector<Assets::Node>& nodes, std::vector<Assets::Model>& models,
                      std::vector<Assets::Texture>& textures, std::vector<Assets::Material>& materials,
                      std::vector<Assets::LightObject>& lights);
    static void ModernHouse1(Assets::CameraInitialSate& camera, std::vector<Assets::Node>& nodes, std::vector<Assets::Model>& models,
                        std::vector<Assets::Texture>& textures, std::vector<Assets::Material>& materials,
                        std::vector<Assets::LightObject>& lights);

    static const std::vector<std::pair<std::string, std::function<void (Assets::CameraInitialSate&,
                                                                        std::vector<Assets::Node>& nodes,
                                                                        std::vector<Assets::Model>&,
                                                                        std::vector<Assets::Texture>&,
                                                                        std::vector<Assets::Material>&,
                                                                        std::vector<Assets::LightObject>&)>>> AllScenes;
};
