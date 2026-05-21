#!/usr/bin/env python3
"""Import proprietary Brotato reference assets used by Brotato3D.

Target layout:
  - SFX go to assets/sounds/brotato3d/sfx/        (referenced via Brotato3D::Assets::Sfx)
  - Icons go to assets/textures/brotato3d/icons/  (referenced via Brotato3D::Assets::Icon)
  - BGM, fonts, UI hud/menu stay under assets/_placeholder/brotato/ for now (still
    behind PlaceholderAssets:: wrappers — these will be absorbed in later passes).

All of the above are excluded from git via .gitignore. After packing with
tools/brotato3d-pak/build-brotato3d-pak.sh, the pak file is the canonical shipping
format.
"""
from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path


# Mapping target_path_relative_to_repo_root -> source_path_relative_to_extracted_root.
COPY_LIST = {
    "assets/sounds/brotato3d/sfx/fire_smg_01.wav": "weapons/ranged/smg/gun_submachine_auto_shot_01.wav",
    "assets/sounds/brotato3d/sfx/fire_smg_02.wav": "weapons/ranged/smg/gun_submachine_auto_shot_02.wav",
    "assets/sounds/brotato3d/sfx/fire_smg_03.wav": "weapons/ranged/smg/gun_submachine_auto_shot_03.wav",
    "assets/sounds/brotato3d/sfx/fire_smg_04.wav": "weapons/ranged/smg/gun_submachine_auto_shot_04.wav",
    "assets/sounds/brotato3d/sfx/fire_smg_05.wav": "weapons/ranged/smg/gun_submachine_auto_shot_05.wav",
    "assets/sounds/brotato3d/sfx/fire_smg_06.wav": "weapons/ranged/smg/gun_submachine_auto_shot_06.wav",
    "assets/sounds/brotato3d/sfx/fire_smg_07.wav": "weapons/ranged/smg/gun_submachine_auto_shot_07.wav",
    "assets/sounds/brotato3d/sfx/fire_smg_08.wav": "weapons/ranged/smg/gun_submachine_auto_shot_08.wav",
    "assets/sounds/brotato3d/sfx/fire_smg_09.wav": "weapons/ranged/smg/gun_submachine_auto_shot_09.wav",
    "assets/sounds/brotato3d/sfx/fire_shotgun_01.wav": "weapons/ranged/smg/gun_submachine_auto_shot_07.wav",
    "assets/sounds/brotato3d/sfx/fire_sniper_01.wav": "weapons/ranged/laser_gun/gun_silenced_sniper1_shot_04.wav",
    "assets/sounds/brotato3d/sfx/fire_rocket_01.wav": "weapons/ranged/rocket_launcher/gun_silenced_pistol2_shot_01.wav",
    "assets/sounds/brotato3d/sfx/fire_laser_01.wav": "weapons/ranged/laser_gun/gun_silenced_sniper1_shot_04v2.wav",
    "assets/sounds/brotato3d/sfx/fire_flamethrower_01.wav": "weapons/ranged/flamethrower/gas_med_flame_ignite_01.wav",
    "assets/sounds/brotato3d/sfx/fire_flamethrower_02.wav": "weapons/ranged/flamethrower/gas_med_flame_ignite_02.wav",
    "assets/sounds/brotato3d/sfx/fire_flamethrower_03.wav": "weapons/ranged/flamethrower/gas_small_flame_ignite_01.wav",
    "assets/sounds/brotato3d/sfx/fire_flamethrower_04.wav": "weapons/ranged/flamethrower/gas_small_flame_ignite_02.wav",
    "assets/sounds/brotato3d/sfx/hit_normal_01.wav": "entities/units/unit/hurt_sounds/punch_general_body_impact_01.wav",
    "assets/sounds/brotato3d/sfx/hit_normal_02.wav": "entities/units/unit/hurt_sounds/punch_general_body_impact_02.wav",
    "assets/sounds/brotato3d/sfx/hit_normal_03.wav": "entities/units/unit/hurt_sounds/punch_general_body_impact_03.wav",
    "assets/sounds/brotato3d/sfx/hit_normal_04.wav": "entities/units/unit/hurt_sounds/punch_general_body_impact_04.wav",
    "assets/sounds/brotato3d/sfx/hit_normal_05.wav": "entities/units/unit/hurt_sounds/punch_general_body_impact_05.wav",
    "assets/sounds/brotato3d/sfx/hit_crit_01.wav": "entities/units/unit/crit_sounds/bullet_impact_metal_light_01.wav",
    "assets/sounds/brotato3d/sfx/hit_crit_02.wav": "entities/units/unit/crit_sounds/bullet_impact_metal_light_02.wav",
    "assets/sounds/brotato3d/sfx/hit_crit_03.wav": "entities/units/unit/crit_sounds/bullet_impact_metal_light_03.wav",
    "assets/sounds/brotato3d/sfx/hit_crit_04.wav": "entities/units/unit/crit_sounds/bullet_impact_metal_light_05.wav",
    "assets/sounds/brotato3d/sfx/player_hurt_01.wav": "entities/units/unit/hurt_sounds/bullet_impact_body_flesh_05.wav",
    "assets/sounds/brotato3d/sfx/player_hurt_02.wav": "entities/units/unit/hurt_sounds/bullet_impact_body_flesh_06.wav",
    "assets/sounds/brotato3d/sfx/player_hurt_03.wav": "entities/units/unit/hurt_sounds/bullet_impact_body_flesh_07.wav",
    "assets/sounds/brotato3d/sfx/player_hurt_04.wav": "entities/units/unit/hurt_sounds/bullet_impact_body_flesh_08.wav",
    "assets/sounds/brotato3d/sfx/enemy_die_small_01.wav": "entities/units/unit/hurt_sounds/bullet_impact_water_01.wav",
    "assets/sounds/brotato3d/sfx/enemy_die_small_02.wav": "entities/units/unit/hurt_sounds/bullet_impact_water_02.wav",
    "assets/sounds/brotato3d/sfx/enemy_die_tank_01.wav": "entities/units/unit/hurt_sounds/punch_general_body_impact_06.wav",
    "assets/sounds/brotato3d/sfx/enemy_die_tank_02.wav": "entities/units/unit/hurt_sounds/punch_general_body_impact_07.wav",
    "assets/sounds/brotato3d/sfx/enemy_die_tank_03.wav": "entities/units/unit/hurt_sounds/punch_general_body_impact_08.wav",
    "assets/sounds/brotato3d/sfx/enemy_die_boss.wav": "entities/units/enemies/boss/zombie_voice_general_emote_05.wav",
    "assets/sounds/brotato3d/sfx/pickup_xp_01.ogg": "entities/units/player/hp_regen_sounds/Potion_Grab_01.ogg",
    "assets/sounds/brotato3d/sfx/pickup_xp_02.ogg": "entities/units/player/hp_regen_sounds/Potion_Grab_02.ogg",
    "assets/sounds/brotato3d/sfx/pickup_material.wav": "items/consumables/item_box/item_box_pickup.wav",
    "assets/sounds/brotato3d/sfx/level_up.wav": "resources/sounds/level_up.wav",
    "assets/sounds/brotato3d/sfx/wave_start.wav": "ui/sounds/clock_tick_01.wav",
    "assets/sounds/brotato3d/sfx/wave_end.wav": "ui/sounds/end_wave.wav",
    "assets/sounds/brotato3d/sfx/wave_start_boss.wav": "entities/units/enemies/boss/zombie_voice_general_emote_05.wav",
    "assets/sounds/brotato3d/sfx/ui_click.wav": "ui/sounds/button_press.wav",
    "assets/sounds/brotato3d/sfx/ui_hover.wav": "ui/sounds/button_focus.wav",
    "assets/sounds/brotato3d/sfx/shop_open.wav": "ui/sounds/CGM3_Bubble_Button_01_3.wav",
    "assets/sounds/brotato3d/sfx/shop_buy.wav": "ui/sounds/buy.wav",
    "assets/sounds/brotato3d/sfx/shop_reroll.wav": "ui/sounds/diceroll.wav",
    "assets/sounds/brotato3d/sfx/shop_cant_buy.wav": "ui/sounds/cant_buy.wav",
    "assets/sounds/brotato3d/sfx/victory.wav": "entities/units/pet/scapegoat/scapegoat_rising.wav",
    "assets/sounds/brotato3d/sfx/defeat.wav": "entities/units/enemies/pursuer/sci-fi_code_fail_08.wav",
    "assets/_placeholder/brotato/audio/bgm/bgm_calm.mp3": "resources/music/wasteland-survivors by evgeny-bardyuzha Artlist.mp3",
    "assets/_placeholder/brotato/audio/bgm/bgm_battle.mp3": "resources/music/power-punch by 2050 Artlist.mp3",
    "assets/_placeholder/brotato/audio/bgm/bgm_boss.mp3": "resources/music/extreme-chaos by 2050 Artlist.mp3",
    "assets/_placeholder/brotato/fonts/Anybody-Medium.ttf": "resources/fonts/raw/Anybody-Medium.ttf",
    "assets/_placeholder/brotato/fonts/NotoSansSC-Medium.otf": "resources/fonts/raw/NotoSansSC-Medium.otf",
    "assets/_placeholder/brotato/ui/hud/lifebar_bg.png": "ui/hud/ui_lifebar_bg.png",
    "assets/_placeholder/brotato/ui/hud/lifebar_fill.png": "ui/hud/ui_lifebar_fill.png",
    "assets/_placeholder/brotato/ui/hud/lifebar_frame.png": "ui/hud/ui_lifebar_frame.png",
    "assets/_placeholder/brotato/ui/hud/xp_bg.png": "ui/hud/ui_xp_bg.png",
    "assets/_placeholder/brotato/ui/hud/xp_fill.png": "ui/hud/ui_xp_fill.png",
    "assets/_placeholder/brotato/ui/hud/progress_under.png": "ui/hud/ui_progress_under.png",
    "assets/_placeholder/brotato/ui/hud/progress_progress.png": "ui/hud/ui_progress_progress.png",
    "assets/_placeholder/brotato/ui/hud/panel_normal.png": "ui/hud/ui_panel_normal.png",
    "assets/_placeholder/brotato/ui/hud/panel_flat.png": "ui/hud/ui_panel_flat.png",
    "assets/_placeholder/brotato/ui/hud/panel_transparent.png": "ui/hud/ui_panel_transparent.png",
    "assets/_placeholder/brotato/ui/menu/splash_bg.png": "ui/menus/title_screen/title_screen_background/splash_art_bg.png",
    "assets/_placeholder/brotato/ui/menu/splash_brotato.png": "ui/menus/title_screen/title_screen_background/splash_art_brotato.png",
    "assets/_placeholder/brotato/ui/menu/splash_mist_back.png": "ui/menus/title_screen/title_screen_background/splash_art_mist_back.png",
    "assets/_placeholder/brotato/ui/menu/splash_mist_mid.png": "ui/menus/title_screen/title_screen_background/splash_art_mist_mid.png",
    "assets/_placeholder/brotato/ui/menu/splash_mist_front.png": "ui/menus/title_screen/title_screen_background/splash_art_mist_front.png",
    "assets/_placeholder/brotato/ui/menu/splash_post.png": "ui/menus/title_screen/title_screen_background/splash_art_post_processing.png",
    "assets/_placeholder/brotato/ui/menu/logo.png": "ui/menus/title_screen/title_screen_background/ui_logo.png",
    "assets/_placeholder/brotato/ui/menu/shop_background.png": "ui/menus/shop/shop_background.png",
    "assets/textures/brotato3d/icons/characters/soldier.png": "items/characters/generalist/generalist_icon.png",
    "assets/textures/brotato3d/icons/characters/brawler.png": "items/characters/brawler/brawler_icon.png",
    "assets/textures/brotato3d/icons/characters/marksman.png": "items/characters/ranger/ranger_icon.png",
    "assets/textures/brotato3d/icons/weapons/smg.png": "weapons/ranged/smg/smg_icon.png",
    "assets/textures/brotato3d/icons/weapons/shotgun.png": "weapons/ranged/double_barrel_shotgun/double_barrel_shotgun_icon.png",
    "assets/textures/brotato3d/icons/weapons/sniper.png": "weapons/ranged/sniper_gun/sniper_gun_icon.png",
    "assets/textures/brotato3d/icons/weapons/rocket.png": "weapons/ranged/rocket_launcher/rocket_launcher_icon.png",
    "assets/textures/brotato3d/icons/weapons/laser.png": "weapons/ranged/laser_gun/laser_gun_icon.png",
    "assets/textures/brotato3d/icons/weapons/flamethrower.png": "weapons/ranged/flamethrower/flamethrower_icon.png",
    "assets/textures/brotato3d/icons/enemies/rat.png": "entities/units/enemies/chaser/chaser_icon.png",
    "assets/textures/brotato3d/icons/enemies/spitter.png": "entities/units/enemies/spitter/spitter_icon.png",
    "assets/textures/brotato3d/icons/enemies/tank.png": "entities/units/enemies/bruiser/bruiser_icon.png",
    "assets/textures/brotato3d/icons/enemies/charger.png": "entities/units/enemies/charger/charger_icon.png",
    "assets/textures/brotato3d/icons/enemies/bomber.png": "entities/units/enemies/spawner/spawner_icon.png",
    "assets/textures/brotato3d/icons/enemies/shaman.png": "entities/units/enemies/healer/healer_icon.png",
    "assets/textures/brotato3d/icons/enemies/boss_warden.png": "ui/icons/misc/boss_icon.png",
    "assets/textures/brotato3d/icons/items/vampire_fang.png": "items/all/blood_leech/blood_leech_icon.png",
    "assets/textures/brotato3d/icons/items/regen_charm.png": "items/all/celery_tea/celery_tea_icon.png",
    "assets/textures/brotato3d/icons/items/magnet.png": "items/all/coupon/coupon_icon.png",
    "assets/textures/brotato3d/icons/items/fury_core.png": "items/all/adrenaline/adrenaline_icon.png",
    "assets/textures/brotato3d/icons/items/shrapnel.png": "items/all/explosive_shells/explosive_shells_icon.png",
    "assets/textures/brotato3d/icons/items/speed_boots.png": "items/all/big_arms/big_arms_icon.png",
    "assets/textures/brotato3d/icons/stats/max_hp.png": "items/stats/max_hp.png",
    "assets/textures/brotato3d/icons/stats/speed.png": "items/stats/speed.png",
    "assets/textures/brotato3d/icons/stats/attack_speed.png": "items/stats/attack_speed.png",
    "assets/textures/brotato3d/icons/stats/crit_chance.png": "items/stats/crit_chance.png",
    "assets/textures/brotato3d/icons/stats/range.png": "items/stats/range.png",
    "assets/textures/brotato3d/icons/stats/armor.png": "items/stats/armor.png",
    "assets/textures/brotato3d/icons/stats/lifesteal.png": "items/stats/lifesteal.png",
    "assets/textures/brotato3d/icons/stats/dodge.png": "items/stats/dodge.png",
    "assets/textures/brotato3d/icons/stats/percent_damage.png": "items/stats/percent_damage.png",
    "assets/textures/brotato3d/icons/stats/ranged_damage.png": "items/stats/ranged_damage.png",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Import proprietary Brotato reference assets into Brotato3D asset paths.")
    parser.add_argument(
        "--source",
        default="D:/SteamLibrary/steamapps/common/Brotato/Brotato_extracted",
        help="Path to the extracted Brotato installation.",
    )
    return parser.parse_args()


REQUIRED_GITIGNORE_RULES = (
    "assets/_placeholder/",
    "assets/sounds/brotato3d/sfx/",
    "assets/textures/brotato3d/icons/",
)


def ensure_gitignore(repo_root: Path) -> None:
    gitignore_path = repo_root / ".gitignore"
    if not gitignore_path.exists():
        raise RuntimeError(f"missing .gitignore: {gitignore_path}")
    content = gitignore_path.read_text(encoding="utf-8", errors="ignore")
    for rule in REQUIRED_GITIGNORE_RULES:
        if rule not in content:
            raise RuntimeError(f"refusing to import because .gitignore does not exclude {rule}")


def main() -> int:
    args = parse_args()
    repo_root = Path(__file__).resolve().parents[1]
    source_root = Path(args.source).expanduser().resolve()

    print("⚠️  WARNING: Importing PROPRIETARY Brotato reference assets.")
    print("    These files MUST NOT be committed, distributed, or used publicly until refreshed.")
    print(f"    Source: {source_root}  Repo root: {repo_root}")

    if not source_root.exists():
        print(f"ERROR: source path does not exist: {source_root}", file=sys.stderr)
        return 1

    try:
        ensure_gitignore(repo_root)
    except RuntimeError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1

    success_count = 0
    missing_count = 0
    copied_bytes = 0

    for relative_target, relative_source in COPY_LIST.items():
        source_path = source_root / relative_source
        target_path = repo_root / relative_target
        target_path.parent.mkdir(parents=True, exist_ok=True)

        if not source_path.exists():
            print(f"WARN: missing source asset: {source_path}")
            missing_count += 1
            continue

        shutil.copy2(source_path, target_path)
        success_count += 1
        copied_bytes += target_path.stat().st_size

    print(
        f"Import complete: success={success_count} missing={missing_count} "
        f"bytes={copied_bytes} repo_root={repo_root}"
    )
    print("Reminder: Brotato reference content under assets/sounds/brotato3d/sfx/,")
    print("          assets/textures/brotato3d/icons/, and assets/_placeholder/brotato/")
    print("          is proprietary and must never be committed until refreshed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
