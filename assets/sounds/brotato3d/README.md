# Brotato3D Audio Placeholders

This directory intentionally does not include audio assets. Add local `.wav` files with the names below. Prefer CC0 sources such as freesound.org, OpenGameArt CC0 packs, or self-recorded/AI-generated assets that you have rights to use.

## Required SFX

- `fire_smg.wav` - short light automatic gunshot, search: `cc0 sci fi smg burst`
- `fire_shotgun.wav` - punchy shotgun blast, search: `cc0 shotgun game`
- `fire_sniper.wav` - sharp single rifle shot, search: `cc0 sniper rifle shot`
- `fire_flamethrower.wav` - short flame puff/loop slice, search: `cc0 flamethrower burst`
- `fire_rocket.wav` - rocket launch whoosh, search: `cc0 rocket launch game`
- `fire_laser.wav` - crisp energy zap, search: `cc0 laser zap`
- `hit_normal.wav` - light hit tick, search: `cc0 impact hit tick`
- `hit_crit.wav` - brighter metal/glass hit, search: `cc0 critical hit chime`
- `pickup_xp.wav` - soft pickup ping, search: `cc0 pickup ping`
- `pickup_material.wav` - coin/material pickup, search: `cc0 coin pickup`
- `level_up.wav` - level-up flourish, search: `cc0 level up`
- `wave_start.wav` - ready/start cue, search: `cc0 ready fight`
- `wave_start_boss.wav` - boss roar/stinger, search: `cc0 monster roar stinger`
- `player_hurt.wav` - short hurt grunt, search: `cc0 player hurt grunt`
- `enemy_die_small.wav` - small enemy pop, search: `cc0 enemy death pop`
- `enemy_die_tank.wav` - heavier enemy collapse, search: `cc0 heavy enemy death`
- `enemy_die_boss.wav` - boss death impact, search: `cc0 boss death explosion`
- `shop_open.wav` - shop open cue, search: `cc0 shop bell`
- `shop_buy.wav` - purchase confirmation, search: `cc0 buy coin ding`
- `ui_click.wav` - subtle button click, search: `cc0 ui click`
- `victory.wav` - victory fanfare, search: `cc0 victory fanfare`
- `defeat.wav` - defeat sting, search: `cc0 defeat sting`

## Required BGM

- `bgm_calm.wav` - low intensity early-wave loop, search: `cc0 chiptune calm loop`
- `bgm_battle.wav` - higher intensity battle loop, search: `cc0 action loop`
- `bgm_boss.wav` - boss loop/stinger bed, search: `cc0 boss battle loop`

Missing files should not block the game. The audio helpers still call the engine sound API, and the runtime should continue normally if a file is absent.
