#include "Application/Game/NextAstrobot/Level/LevelIndex.hpp"

#include <algorithm>
#include <array>
#include <string_view>
#include <unordered_map>

#include <fmt/format.h>

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"

namespace NextAstrobot
{
    namespace
    {
        struct FMechanismSpec
        {
            std::string_view moduleName;
            EMechanismKind kind;
            std::string_view partName; // empty when the mechanism has no movable piece
        };

        // The kit contract from kit_astro.scad's "活动件契约" header block. Matching by an
        // explicit table rather than an ab_part_* prefix keeps the pure geometry helpers
        // (ab_part_roof, ab_part_hazard) out of the mechanism path.
        constexpr std::array kMechanismSpecs = {
            FMechanismSpec{"ab_plat_moving", EMechanismKind::MovingPlatform, "ab_part_moving_car"},
            FMechanismSpec{"ab_plat_pendulum", EMechanismKind::Pendulum, "ab_part_pendulum_arm"},
            FMechanismSpec{"ab_plat_seesaw", EMechanismKind::Seesaw, "ab_part_seesaw_plank"},
            FMechanismSpec{"ab_plat_spin", EMechanismKind::SpinDisc, ""},
            FMechanismSpec{"ab_plat_crumble", EMechanismKind::Crumble, "ab_part_crumble_slab"},
            FMechanismSpec{"ab_plat_bounce", EMechanismKind::BouncePad, ""},
            FMechanismSpec{"ab_plat_spring", EMechanismKind::Spring, "ab_part_spring_cap"},
            FMechanismSpec{"ab_plat_roller", EMechanismKind::Roller, "ab_part_roller_drum"},
            FMechanismSpec{"ab_plat_conveyor", EMechanismKind::Conveyor, ""},
            FMechanismSpec{"ab_plat_zipline", EMechanismKind::Zipline, "ab_part_zipline_car"},
            FMechanismSpec{"ab_prop_button", EMechanismKind::Button, "ab_part_button_cap"},
            FMechanismSpec{"ab_prop_gate_bars", EMechanismKind::Gate, "ab_part_gate_grid"},
            FMechanismSpec{"ab_prop_cage", EMechanismKind::Cage, "ab_part_cage_dome"},
            // The non-star pieces. Several of these modules are also something else -
            // the laser and the spike ball are hazards, the chest is breakable, the
            // checkpoint is a respawn flag - so Build must not stop at the first match.
            FMechanismSpec{"ab_prop_spike_ball_chain", EMechanismKind::SpikeBall, "ab_part_spike_ball"},
            FMechanismSpec{"ab_prop_laser", EMechanismKind::LaserBeam, "ab_part_laser_beam"},
            FMechanismSpec{"ab_prop_fan", EMechanismKind::Fan, "ab_part_fan_blades"},
            FMechanismSpec{"ab_prop_fountain_jet", EMechanismKind::Fountain, "ab_part_fountain_column"},
            FMechanismSpec{"ab_bldg_windmill", EMechanismKind::Windmill, "ab_part_windmill_blades"},
            FMechanismSpec{"ab_prop_chest", EMechanismKind::ChestLid, "ab_part_chest_lid"},
            FMechanismSpec{"ab_prop_lever", EMechanismKind::Lever, "ab_part_lever_arm"},
            FMechanismSpec{"ab_prop_checkpoint", EMechanismKind::CheckpointFlag, "ab_part_checkpoint_flag"},
        };

        constexpr std::array<std::pair<std::string_view, EHazardKind>, 5> kHazardSpecs = {{
            {"ab_ground_lava", EHazardKind::Lava},
            {"ab_ground_pool", EHazardKind::Water},
            {"ab_prop_spikes", EHazardKind::Spikes},
            {"ab_prop_laser", EHazardKind::Laser},
            {"ab_prop_spike_ball_chain", EHazardKind::SpikeBall},
        }};

        constexpr std::array<std::pair<std::string_view, EEnemyKind>, 3> kEnemySpecs = {{
            {"ab_char_enemy_walker", EEnemyKind::Walker},
            {"ab_char_enemy_flyer", EEnemyKind::Flyer},
            {"ab_char_enemy_spiky", EEnemyKind::Spiky},
        }};

        constexpr std::array<std::pair<std::string_view, EInteractableKind>, 4> kInteractableSpecs = {{
            {"ab_prop_crate", EInteractableKind::Crate},
            {"ab_bldg_wall_break", EInteractableKind::BrickWall},
            {"ab_prop_chest", EInteractableKind::Chest},
            {"ab_char_bot_lost", EInteractableKind::LostBot},
        }};

        constexpr std::array<std::pair<std::string_view, ECollectibleKind>, 5> kCollectibleSpecs = {{
            {"ab_item_coin", ECollectibleKind::Coin},
            {"ab_item_puzzle", ECollectibleKind::Puzzle},
            {"ab_item_gem", ECollectibleKind::Gem},
            {"ab_item_key", ECollectibleKind::Key},
            {"ab_item_star", ECollectibleKind::Star},
        }};

        FIndexedNode MakeIndexedNode(Assets::Node& node)
        {
            FIndexedNode indexed;
            indexed.node = &node;
            indexed.id = node.GetInstanceId();
            indexed.meta = Assets::Scad::ParseScadMetadata(node.GetMetadata());
            indexed.worldPos = node.WorldTranslation();
            indexed.worldRot = node.WorldRotation();
            return indexed;
        }

        Assets::Node* FindChildNamed(Assets::Node& parent, std::string_view name)
        {
            for (const std::shared_ptr<Assets::Node>& child : parent.Children())
            {
                if (child->GetName() == name)
                {
                    return child.get();
                }
            }
            return nullptr;
        }
    }

    const char* MechanismKindName(EMechanismKind kind)
    {
        switch (kind)
        {
        case EMechanismKind::MovingPlatform: return "moving";
        case EMechanismKind::Pendulum: return "pendulum";
        case EMechanismKind::Seesaw: return "seesaw";
        case EMechanismKind::SpinDisc: return "spin";
        case EMechanismKind::Crumble: return "crumble";
        case EMechanismKind::BouncePad: return "bounce";
        case EMechanismKind::Spring: return "spring";
        case EMechanismKind::Roller: return "roller";
        case EMechanismKind::Conveyor: return "conveyor";
        case EMechanismKind::Zipline: return "zipline";
        case EMechanismKind::Button: return "button";
        case EMechanismKind::Gate: return "gate";
        case EMechanismKind::Cage: return "cage";
        case EMechanismKind::SpikeBall: return "spikeball";
        case EMechanismKind::LaserBeam: return "laser";
        case EMechanismKind::Fan: return "fan";
        case EMechanismKind::Fountain: return "fountain";
        case EMechanismKind::Windmill: return "windmill";
        case EMechanismKind::ChestLid: return "chest";
        case EMechanismKind::Lever: return "lever";
        case EMechanismKind::CheckpointFlag: return "flag";
        case EMechanismKind::Count: break;
        }
        return "unknown";
    }

    size_t FLevelIndex::CountInteractables(EInteractableKind kind) const
    {
        return static_cast<size_t>(std::count_if(interactables.begin(), interactables.end(),
                                                 [kind](const FTypedNode& entry)
                                                 { return entry.kind == static_cast<uint8_t>(kind); }));
    }

    const FMechanismRecord* FLevelIndex::FindMechanism(EMechanismKind kind) const
    {
        for (const FMechanismRecord& record : mechanisms)
        {
            if (record.kind == kind)
            {
                return &record;
            }
        }
        return nullptr;
    }

    FLevelIndex FLevelIndex::Build(Assets::Scene& scene, std::vector<std::string>* outWarnings)
    {
        FLevelIndex index;
        const auto warn = [outWarnings](std::string message)
        {
            if (outWarnings)
            {
                outWarnings->push_back(std::move(message));
            }
        };

        std::unordered_map<std::string_view, const FMechanismSpec*> mechanismByName;
        for (const FMechanismSpec& spec : kMechanismSpecs)
        {
            mechanismByName.emplace(spec.moduleName, &spec);
        }

        bool sawGround = false;
        for (const std::shared_ptr<Assets::Node>& nodePtr : scene.Nodes())
        {
            Assets::Node& node = *nodePtr;
            const std::string& name = node.GetName();

            // A module can be several things at once: a cage is a mechanism and a rescue,
            // a chest is a mechanism and a breakable, a laser is a mechanism and a hazard.
            // So the mechanism pass runs first and deliberately falls through to the
            // category tables below instead of claiming the node.
            Assets::Node* movablePart = nullptr;
            if (auto mechanism = mechanismByName.find(name); mechanism != mechanismByName.end())
            {
                const FMechanismSpec& spec = *mechanism->second;
                FMechanismRecord record;
                record.kind = spec.kind;
                record.root = MakeIndexedNode(node);
                if (!spec.partName.empty())
                {
                    movablePart = FindChildNamed(node, spec.partName);
                    record.part = movablePart;
                    record.root.part = movablePart;
                    if (!movablePart)
                    {
                        warn(fmt::format("{} is missing its movable piece '{}'", name, spec.partName));
                    }
                    else
                    {
                        record.partBindTranslation = movablePart->Translation();
                        record.partBindRotation = movablePart->Rotation();
                    }
                }
                index.mechanisms.push_back(std::move(record));
                if (spec.kind == EMechanismKind::Cage)
                {
                    index.cages.push_back(MakeIndexedNode(node));
                }
            }

            if (name == "ab_bldg_startpad")
            {
                if (!index.hasSpawn)
                {
                    index.spawn = MakeIndexedNode(node);
                    index.hasSpawn = true;
                }
                continue;
            }
            if (name == "ab_bldg_goal")
            {
                if (!index.hasGoal)
                {
                    index.goal = MakeIndexedNode(node);
                    index.hasGoal = true;
                }
                continue;
            }
            if (name == "ab_prop_checkpoint")
            {
                FIndexedNode checkpoint = MakeIndexedNode(node);
                checkpoint.part = movablePart;
                index.checkpoints.push_back(std::move(checkpoint));
                continue;
            }
            if (name == "ab_ground_island" || name == "ab_ground_island_round")
            {
                // Island modules put their top surface at local z = 0, so the node origin
                // is the walkable height.
                const float top = node.WorldTranslation().y;
                index.lowestGroundY = sawGround ? std::min(index.lowestGroundY, top) : top;
                sawGround = true;
                continue;
            }

            bool matched = false;
            for (const auto& [moduleName, kind] : kCollectibleSpecs)
            {
                if (name != moduleName)
                {
                    continue;
                }
                matched = true;
                FIndexedNode item = MakeIndexedNode(node);
                switch (kind)
                {
                case ECollectibleKind::Coin: index.coins.push_back(std::move(item)); break;
                case ECollectibleKind::Puzzle: index.puzzles.push_back(std::move(item)); break;
                case ECollectibleKind::Gem: index.gems.push_back(std::move(item)); break;
                case ECollectibleKind::Key: index.keys.push_back(std::move(item)); break;
                case ECollectibleKind::Star: index.stars.push_back(std::move(item)); break;
                }
                break;
            }
            if (matched)
            {
                continue;
            }

            for (const auto& [moduleName, kind] : kHazardSpecs)
            {
                if (name == moduleName)
                {
                    FIndexedNode hazard = MakeIndexedNode(node);
                    hazard.part = movablePart;
                    index.hazards.push_back({std::move(hazard), static_cast<uint8_t>(kind)});
                    matched = true;
                    break;
                }
            }
            if (matched)
            {
                continue;
            }

            for (const auto& [moduleName, kind] : kEnemySpecs)
            {
                if (name == moduleName)
                {
                    index.enemies.push_back({MakeIndexedNode(node), static_cast<uint8_t>(kind)});
                    matched = true;
                    break;
                }
            }
            if (matched)
            {
                continue;
            }

            for (const auto& [moduleName, kind] : kInteractableSpecs)
            {
                if (name == moduleName)
                {
                    FIndexedNode interactable = MakeIndexedNode(node);
                    interactable.part = movablePart;
                    index.interactables.push_back({std::move(interactable), static_cast<uint8_t>(kind)});
                    break;
                }
            }
        }

        std::sort(index.checkpoints.begin(), index.checkpoints.end(),
                  [](const FIndexedNode& a, const FIndexedNode& b)
                  { return a.Number("idx", 0.0) < b.Number("idx", 0.0); });

        if (!index.hasSpawn)
        {
            warn("no ab_bldg_startpad in the level; spawning at the origin");
        }
        if (!index.hasGoal)
        {
            warn("no ab_bldg_goal in the level; the level cannot be completed");
        }
        if (!sawGround)
        {
            warn("no ab_ground_island in the level; the kill plane falls back to y = 0");
        }
        return index;
    }
}
