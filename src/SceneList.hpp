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
}

class SceneList final
{
public:
    struct CameraInitialSate
    {
        glm::mat4 ModelView;
        float FieldOfView;
        float Aperture;
        float FocusDistance;
        float ControlSpeed;
        bool GammaCorrection;
        bool HasSky;
    };

    static void CubeAndSpheres(CameraInitialSate& camera, std::vector<Assets::Node>& nodes,
                               std::vector<Assets::Model>& models, std::vector<Assets::Texture>& textures,
                               std::vector<Assets::Material>& materials, std::vector<Assets::LightObject>& lights);
    static void RayTracingInOneWeekend(CameraInitialSate& camera, std::vector<Assets::Node>& nodes,
                                       std::vector<Assets::Model>& models, std::vector<Assets::Texture>& textures,
                                       std::vector<Assets::Material>& materials,
                                       std::vector<Assets::LightObject>& lights);
    static void CornellBox(CameraInitialSate& camera, std::vector<Assets::Node>& nodes,
                           std::vector<Assets::Model>& models, std::vector<Assets::Texture>& textures,
                           std::vector<Assets::Material>& materials, std::vector<Assets::LightObject>& lights);
    static void CornellBoxLucy(CameraInitialSate& camera, std::vector<Assets::Node>& nodes,
                               std::vector<Assets::Model>& models, std::vector<Assets::Texture>& textures,
                               std::vector<Assets::Material>& materials, std::vector<Assets::LightObject>& lights);
    static void LivingRoom(CameraInitialSate& camera, std::vector<Assets::Node>& nodes,
                           std::vector<Assets::Model>& models, std::vector<Assets::Texture>& textures,
                           std::vector<Assets::Material>& materials, std::vector<Assets::LightObject>& lights);
    static void Kitchen(CameraInitialSate& camera, std::vector<Assets::Node>& nodes, std::vector<Assets::Model>& models,
                        std::vector<Assets::Texture>& textures, std::vector<Assets::Material>& materials,
                        std::vector<Assets::LightObject>& lights);
    static void LuxBall(CameraInitialSate& camera, std::vector<Assets::Node>& nodes, std::vector<Assets::Model>& models,
                        std::vector<Assets::Texture>& textures, std::vector<Assets::Material>& materials,
                        std::vector<Assets::LightObject>& lights);
    static void Still(CameraInitialSate& camera, std::vector<Assets::Node>& nodes, std::vector<Assets::Model>& models,
                      std::vector<Assets::Texture>& textures, std::vector<Assets::Material>& materials,
                      std::vector<Assets::LightObject>& lights);

    static const std::vector<std::pair<std::string, std::function<void (CameraInitialSate&,
                                                                        std::vector<Assets::Node>& nodes,
                                                                        std::vector<Assets::Model>&,
                                                                        std::vector<Assets::Texture>&,
                                                                        std::vector<Assets::Material>&,
                                                                        std::vector<Assets::LightObject>&)>>> AllScenes;
};
