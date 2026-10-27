# RDR_bhaptics

bHaptics support for **Red Dead Redemption** (PC, 2024) as a [RedHook](https://www.nexusmods.com/reddeadredemption/mods/192)
plugin. Made with [RDRVR](https://github.com/Disonomouss/RDRVR) in mind, but independent of it: it reads the player's
state through the game's natives, so it works in flat mode too.

## Install

1. RedHook v0.8 in the game folder (RDRVR needs it anyway).
2. Unpack `RDR_bhaptics.zip` into the game folder, next to `RDR.exe`:
   ```
   RDR_bhaptics.red
   RDR_bhaptics\RDR_bhaptics.ini
   RDR_bhaptics\bhaptics_library.dll
   ```
3. Start the bHaptics Player, then the game. `RDR_bhaptics\RDR_bhaptics.log` says whether it connected.

## Effects

| Event (workspace name) | When |
|---|---|
| `RecoilPistol_R/_L`, `RecoilRifle_R/_L`, `RecoilShotgun_R/_L` | a round leaves the clip (pistols and revolvers / repeaters, rifles, sniper rifles / shotguns) |
| `RecoilRifle_TwoHanded_R/_L`, `RecoilShotgun_TwoHanded_R/_L` | the same, long guns with RDRVR's two-handed hold on (`[Hands] TwoHanded`) |
| `BowRelease_R/_L`, `Throw_R/_L` | an arrow, a thrown weapon |
| `RecoilTurret` | firing a Gatling, Browning or cannon |
| `Reload_R/_L` | the game's reload starts |
| `Impact` | a hit, rotated toward the attacker (angleX) |
| `ImpactNoDir` | a hit without a known attacker (falls, explosions) |
| `Heartbeat` | low health, faster as it drops |
| `Heal` | a large health rise at once (medicine) |
| `Death` | the player dies |
| `DeadeyeStart`, `DeadeyePulse`, `DeadeyeEnd` | Dead Eye |
| `HorseTrot`, `HorseGallop` | riding, in step with the horse's speed |
| `OnFire`, `WaterEnter` | burning, wading in |

The names are CamelCase for readability; the plugin lowercases them before calling the SDK, so create them in
the portal in lowercase (`RecoilPistol_R` → `recoilpistol_r`).

`_R` / `_L` is the gun arm: `[Hands] Hand=Auto` follows RDRVR's left-handed setting.

Nothing plays during pause menus and cutscenes.

## Building

Visual Studio 2022 (v143), open `RDR_bhaptics.sln`, build `Release|x64`.

- `extern\RedHookSDK\`: a checkout of [RedHookSDK](https://github.com/K3rhos/RedHookSDK) (MIT), e.g.
  `git submodule add https://github.com/K3rhos/RedHookSDK extern/RedHookSDK`. Another location: set the
  `RedHookSdkDir` property (or environment variable).
- `libs\bhaptics\bhaptics_library.dll` and `libs\bhaptics\bhaptics.h` from [tact-cpp2](https://github.com/bhaptics/tact-cpp2)
  (`sdk\lib\x64`, `sdk\include`). The header is only used for the function types; the DLL is loaded at runtime
  from the plugin folder (`LoadLibraryEx` + `GetProcAddress`), so there is no import library and no wrapper DLL.

The post-build event:

- `tools\postbuild_zip.ps1` writes `bin\<Configuration>\RDR_bhaptics.zip`, ready to unpack into the game folder.
- `tools\deploy_game.ps1` copies the mod into the game folder for testing: the `.red` and the DLL are replaced, the
  ini only copied when there is none yet (so your AppId / ApiKey stay). It runs only when the `DeployToGame`
  property is `true` (the default) and `RdrGameDir` contains `RDR.exe`, so elsewhere it is skipped. Set either one as
  an environment variable or with `msbuild /p:DeployToGame=false` to change it. A failed copy (the game still
  running) is a warning, not a failed build.

## Code

- `src/main.cpp`: DllMain registers the script; the script loads the config, starts haptics, then ticks.
- `src/game_state.cpp`: the poll. Each tick compares the player's health, weapon, clip, reload, Dead Eye, mount and
  so on with the last tick and plays events. The workspace event names are at the top.
- `src/haptics.cpp`: the bHaptics DLL. SDK calls block, so they run on a worker thread of ours, never on the game's
  script fiber.
- `src/config.cpp`: the ini, and RDRVR's handedness.

## To check in game

- The hit angle's zero and direction (`[General] DebugLog=1` logs each hit's raw numbers; then set `[Impact]
  AngleOffset` / `AngleInvert` and make the right values the defaults).
- Shot detection by clip count: double-barrel (both barrels), the bow and thrown weapons, and RDRVR's dual wield
  (its second gun may not count down the same clip).
- Turrets: whether `GET_WEAPON_IN_HAND` reports them and `IS_ACTOR_SHOOTING` stays set while firing.
- `GET_LAST_ATTACKER` going stale (fall damage after a fight would be rotated toward the old attacker).
- Horse speed thresholds.
