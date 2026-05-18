#pragma once

struct CharacterDemoPlayerConfig
{
    float EyeHeight = 1.55f;
    float WalkSpeed = 4.0f;
    float RunSpeed = 8.0f;
    float CharacterTurnSpeed = 8.0f;
};

struct CharacterDemoCameraConfig
{
    float MouseSensitivity = 0.002f;
    float Distance = 5.0f;
    float Height = 2.0f;
    float ScrollStep = 0.5f;
    float MinDistance = 1.0f;
    float MaxDistance = 20.0f;
};

struct CharacterDemoProjectileConfig
{
    float Size = 0.3f;
    float Force = 60000.0f;
    float SpawnDistance = 0.9f;
};

struct CharacterDemoAnimationConfig
{
    float WalkStrafePlaySpeed = 0.82f;
    float RunBackwardPlaySpeed = 1.35f;
    float JumpStartHoldTime = 0.12f;
    float JumpLandHoldTime = 0.18f;
};

struct CharacterDemoAIConfig
{
    bool Enabled = true;
    float ProjectileSpawnDistance = 0.9f;
    float EyeHeight = 1.55f;
    float WalkSpeed = 3.8f;
    float RunSpeed = 6.2f;
    float SightRange = 28.0f;
    float LoseSightRange = 34.0f;
    float MemoryTime = 3.5f;
    float TargetVisibleGraceTime = 0.75f;
    float StateMinHoldTime = 0.75f;
    float PreferredCombatRangeMin = 7.0f;
    float PreferredCombatRangeMax = 16.0f;
    float CombatRangeHysteresis = 1.25f;
    float FireRange = 22.0f;
    float FireCooldown = 1.25f;
    float AimTolerance = 0.92f;
    float PatrolPointRadius = 1.25f;
    float PatrolMinTravelDistance = 3.0f;
    float PatrolPauseTime = 0.6f;
    float PatrolProgressResetDistance = 0.75f;
    float PatrolStuckSpeedThreshold = 0.2f;
    float PatrolStuckTimeout = 0.85f;
    float TurnSpeed = 6.5f;
};

struct CharacterDemoConfig
{
    CharacterDemoPlayerConfig Player{};
    CharacterDemoCameraConfig Camera{};
    CharacterDemoProjectileConfig Projectile{};
    CharacterDemoAnimationConfig Animation{};
    CharacterDemoAIConfig AI{};
};
