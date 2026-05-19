#include <catch2/catch_all.hpp>

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Runtime/Camera/ModelViewController.hpp"

TEST_CASE("Camera focus moves closer for small targets", "[Unit][Camera]")
{
    Assets::Camera camera{};
    camera.FieldOfView = 40.0f;
    camera.ModelView = glm::lookAtRH(
        glm::vec3(0.0f, 0.0f, 5.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));

    ModelViewController controller;
    controller.Reset(camera);

    controller.Focus(glm::vec3(0.0f), 0.02f);
    CHECK(controller.UpdateCamera(1.0, 1.0));

    const float finalDistance = glm::distance(controller.GetPosition(), glm::vec3(0.0f));
    CHECK(finalDistance < 5.0f);
    CHECK(finalDistance == Catch::Approx(0.060434f).margin(0.01f));
}
