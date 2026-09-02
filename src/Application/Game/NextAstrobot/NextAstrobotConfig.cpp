#include "Application/Game/NextAstrobot/NextAstrobotConfig.hpp"

#include "Engine/Runtime/Utilities/JsonHelpers.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

namespace NextAstrobot
{
    namespace
    {
        void ReadMove(const json& object, FMoveConfig& move)
        {
            move.RunSpeed = object.value("runSpeed", move.RunSpeed);
            move.RunAccel = object.value("runAccel", move.RunAccel);
            move.AirControl = object.value("airControl", move.AirControl);
            move.Gravity = object.value("gravity", move.Gravity);
            move.JumpSpeed = object.value("jumpSpeed", move.JumpSpeed);
            move.JumpCutMultiplier = object.value("jumpCutMultiplier", move.JumpCutMultiplier);
            move.CoyoteSeconds = object.value("coyoteSeconds", move.CoyoteSeconds);
            move.JumpBufferSeconds = object.value("jumpBufferSeconds", move.JumpBufferSeconds);
            move.HoverMaxSeconds = object.value("hoverMaxSeconds", move.HoverMaxSeconds);
            move.HoverFallSpeed = object.value("hoverFallSpeed", move.HoverFallSpeed);
            move.HoverBoostSeconds = object.value("hoverBoostSeconds", move.HoverBoostSeconds);
            move.HoverBoostHeight = object.value("hoverBoostHeight", move.HoverBoostHeight);
            move.StompBounceSpeed = object.value("stompBounceSpeed", move.StompBounceSpeed);
            move.PunchSeconds = object.value("punchSeconds", move.PunchSeconds);
            move.PunchSeconds2 = object.value("punchSeconds2", move.PunchSeconds2);
            move.KickSeconds = object.value("kickSeconds", move.KickSeconds);
            move.ComboWindowSeconds = object.value("comboWindowSeconds", move.ComboWindowSeconds);
            move.ComboBufferSeconds = object.value("comboBufferSeconds", move.ComboBufferSeconds);
            move.PunchRange = object.value("punchRange", move.PunchRange);
            move.PunchArcDegrees = object.value("punchArcDegrees", move.PunchArcDegrees);
            move.KickRange = object.value("kickRange", move.KickRange);
            move.KickArcDegrees = object.value("kickArcDegrees", move.KickArcDegrees);
            move.PunchLungeSpeed = object.value("punchLungeSpeed", move.PunchLungeSpeed);
            move.KickLungeSpeed = object.value("kickLungeSpeed", move.KickLungeSpeed);
            move.SkidMinSpeed = object.value("skidMinSpeed", move.SkidMinSpeed);
            move.SkidSeconds = object.value("skidSeconds", move.SkidSeconds);
            move.SkidReverseDot = object.value("skidReverseDot", move.SkidReverseDot);
            move.SkidDecel = object.value("skidDecel", move.SkidDecel);
            move.SkidTurnScale = object.value("skidTurnScale", move.SkidTurnScale);
            move.ControllerHeight = object.value("controllerHeight", move.ControllerHeight);
            move.ControllerRadius = object.value("controllerRadius", move.ControllerRadius);
            move.MaxStepHeight = object.value("maxStepHeight", move.MaxStepHeight);
            move.MaxSlopeDegrees = object.value("maxSlopeDegrees", move.MaxSlopeDegrees);
            move.DeathFadeSeconds = object.value("deathFadeSeconds", move.DeathFadeSeconds);
            move.TurnRateDegrees = object.value("turnRateDegrees", move.TurnRateDegrees);
        }

        void ReadCamera(const json& object, FCameraConfig& camera)
        {
            camera.Distance = object.value("distance", camera.Distance);
            camera.Height = object.value("height", camera.Height);
            camera.TargetHeight = object.value("targetHeight", camera.TargetHeight);
            camera.Fov = object.value("fov", camera.Fov);
            camera.Damping = object.value("damping", camera.Damping);
            camera.AutoYawRate = object.value("autoYawRate", camera.AutoYawRate);
            camera.ManualYawRate = object.value("manualYawRate", camera.ManualYawRate);
            camera.AutoYawIdleSeconds = object.value("autoYawIdleSeconds", camera.AutoYawIdleSeconds);
            camera.SpringArmRadius = object.value("springArmRadius", camera.SpringArmRadius);
            camera.SpringArmHideRigDistance =
                object.value("springArmHideRigDistance", camera.SpringArmHideRigDistance);
            camera.SpringArmReturnRate = object.value("springArmReturnRate", camera.SpringArmReturnRate);
            camera.FocusDistance = object.value("focusDistance", camera.FocusDistance);
            camera.FocusHeight = object.value("focusHeight", camera.FocusHeight);
            camera.FocusTargetHeight = object.value("focusTargetHeight", camera.FocusTargetHeight);
            camera.FocusFov = object.value("focusFov", camera.FocusFov);
            camera.FocusBlendSeconds = object.value("focusBlendSeconds", camera.FocusBlendSeconds);
            camera.FocusOrbitRate = object.value("focusOrbitRate", camera.FocusOrbitRate);
            camera.FocusOffsetDegrees = object.value("focusOffsetDegrees", camera.FocusOffsetDegrees);
        }

        void ReadWorld(const json& object, FWorldConfig& world)
        {
            world.PickupRadius = object.value("pickupRadius", world.PickupRadius);
            world.HazardRadius = object.value("hazardRadius", world.HazardRadius);
            world.EnemyRadius = object.value("enemyRadius", world.EnemyRadius);
            world.StompMargin = object.value("stompMargin", world.StompMargin);
            world.InteractRadius = object.value("interactRadius", world.InteractRadius);
            world.RescueHoldSeconds = object.value("rescueHoldSeconds", world.RescueHoldSeconds);
            world.CheckpointRadius = object.value("checkpointRadius", world.CheckpointRadius);
            world.GoalRadius = object.value("goalRadius", world.GoalRadius);
            world.EnemyPatrolHalfLength = object.value("enemyPatrolHalfLength", world.EnemyPatrolHalfLength);
            world.EnemyPatrolSpeed = object.value("enemyPatrolSpeed", world.EnemyPatrolSpeed);
            world.FlyerOrbitRadius = object.value("flyerOrbitRadius", world.FlyerOrbitRadius);
            world.FlyerOrbitSpeed = object.value("flyerOrbitSpeed", world.FlyerOrbitSpeed);
            world.FootContactTolerance = object.value("footContactTolerance", world.FootContactTolerance);
        }
    }

    bool FConfig::Load(const std::string& gameplayPath, const std::string& levelsPath)
    {
        json gameplay;
        if (NextJson::TryLoadFile(gameplayPath, gameplay) && gameplay.is_object())
        {
            if (gameplay.contains("move")) ReadMove(gameplay.at("move"), Move);
            if (gameplay.contains("camera")) ReadCamera(gameplay.at("camera"), Camera);
            if (gameplay.contains("world")) ReadWorld(gameplay.at("world"), World);
        }
        else
        {
            SPDLOG_WARN("[NextAstrobot] '{}' unavailable; using built-in gameplay defaults", gameplayPath);
        }

        Levels.clear();
        json levels;
        if (NextJson::TryLoadFile(levelsPath, levels) && levels.is_object() && levels.contains("levels"))
        {
            for (const json& entry : levels.at("levels"))
            {
                if (!entry.is_object() || !entry.contains("scene"))
                {
                    continue;
                }
                FLevelDesc desc;
                desc.Scene = entry.at("scene").get<std::string>();
                desc.Id = entry.value("id", desc.Scene);
                desc.DisplayName = entry.value("displayName", desc.Id);
                desc.IntroCameraPath = entry.value("introCameraPath", desc.IntroCameraPath);
                desc.TitleCamera = entry.value("titleCamera", desc.TitleCamera);
                desc.KillPlaneOffset = entry.value("killPlaneOffset", desc.KillPlaneOffset);
                Levels.push_back(std::move(desc));
            }
        }

        if (Levels.empty())
        {
            SPDLOG_WARN("[NextAstrobot] '{}' listed no level; falling back to sky_garden", levelsPath);
            FLevelDesc fallback;
            fallback.Id = "sky_garden";
            fallback.DisplayName = "Sky Garden";
            fallback.Scene = "assets/scad/source/astro/sky_garden.scad";
            Levels.push_back(std::move(fallback));
            return false;
        }
        return true;
    }
}
