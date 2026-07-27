#include <catch2/catch_all.hpp>

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Runtime/Platform/PlatformCommon.hpp"
#include "Gameplay/Camera/ModelViewController.hpp"

TEST_CASE("Camera focus moves closer for small targets", "[Unit][Camera]")
{
    Assets::Camera camera{};
    camera.FieldOfView = 40.0f;
    camera.ModelView =
        glm::lookAtRH(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    Runtime::Camera::ModelViewController controller;
    controller.Reset(camera);

    controller.Focus(glm::vec3(0.0f), 0.02f);
    CHECK(controller.UpdateCamera(1.0, 1.0));

    const float finalDistance = glm::distance(controller.GetPosition(), glm::vec3(0.0f));
    CHECK(finalDistance < 5.0f);
    CHECK(finalDistance == Catch::Approx(0.060434f).margin(0.01f));
}

TEST_CASE("Orbit camera movement reports updated state", "[Unit][Camera]")
{
    Assets::Camera camera{};
    camera.FieldOfView = 40.0f;
    camera.ModelView =
        glm::lookAtRH(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    Runtime::Camera::ModelViewController controller;
    controller.Reset(camera);
    controller.SetAltPressed(true);
    controller.SetOrbitTarget(glm::vec3(0.0f));

    SDL_Event event{};
    event.button.button = SDL_BUTTON_RIGHT;
    event.button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    controller.OnMouseButton(event);

    const glm::vec3 initialPosition = controller.GetPosition();
    controller.OnCursorPosition(100.0, 100.0);
    controller.OnCursorPosition(130.0, 115.0);

    CHECK(glm::distance(controller.GetPosition(), initialPosition) > 0.0001f);
    CHECK(controller.UpdateCamera(1.0, 1.0 / 60.0));
}

TEST_CASE("Model view camera left drag pans when enabled", "[Unit][Camera]")
{
    Assets::Camera camera{};
    camera.FieldOfView = 40.0f;
    camera.ModelView = glm::lookAtRH(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    Runtime::Camera::ModelViewController controller;
    controller.Reset(camera);
    controller.SetOrbitTarget(glm::vec3(0.0f));
    controller.SetLeftDragPans(true);

    SDL_Event event{};
    event.button.button = SDL_BUTTON_LEFT;
    event.button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    controller.OnMouseButton(event);
    controller.OnCursorPosition(100.0, 100.0);
    const glm::vec3 initialPosition = controller.GetPosition();
    controller.OnCursorPosition(200.0, 140.0);

    CHECK(glm::distance(controller.GetPosition(), initialPosition) > 0.1f);
    CHECK(controller.UpdateCamera(1.0, 1.0 / 60.0));
}

TEST_CASE("Model view camera wheel speed follows scene bounds", "[Unit][Camera]")
{
    Assets::Camera camera{};
    camera.FieldOfView = 40.0f;
    camera.ModelView = glm::lookAtRH(glm::vec3(0.0f, 0.0f, 1000.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    Runtime::Camera::ModelViewController largeSceneController;
    largeSceneController.Reset(camera);
    largeSceneController.SetOrbitTarget(glm::vec3(0.0f));
    largeSceneController.SetNavigationScale(700.0f);
    largeSceneController.OnScroll(0.0, 1.0);
    const float largeSceneMovement = 1000.0f - glm::distance(largeSceneController.GetPosition(), glm::vec3(0.0f));

    Runtime::Camera::ModelViewController smallSceneController;
    smallSceneController.Reset(camera);
    smallSceneController.SetOrbitTarget(glm::vec3(0.0f));
    smallSceneController.SetNavigationScale(1.0f);
    smallSceneController.OnScroll(0.0, 1.0);
    const float smallSceneMovement = 1000.0f - glm::distance(smallSceneController.GetPosition(), glm::vec3(0.0f));

    CHECK(largeSceneMovement > 50.0f);
    CHECK(largeSceneMovement > smallSceneMovement * 100.0f);
}
