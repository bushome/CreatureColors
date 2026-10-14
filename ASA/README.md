# CreatureColors (ASA)

AsaApi plugin that lets players with one Permissions-plugin permission recolor tames owned by them or their tribe.

| Command | What it does |
|---|---|
| `/setcr <region> <color>` | Sets one region (0-5) on the creature you're aiming at. |
| `/savecrt <name>` | Saves the aimed creature's six regions as a template under your EOS ID. If the name exists you are asked to confirm with `/y` or `/n`. |
| `/setcrtaoe <name> [radius]` | Applies the template to your/your tribe's creatures of the same species within the radius (default 300). Larger requests run at `AOE.MaxRadius` with a notice; a radius that isn't a whole number above 0 shows usage. |
| `/listcrt [species]` | Lists your templates. Looking at a creature shows only that species' templates; `/listcrt rex` shows species whose short name contains "rex"; otherwise everything, grouped by species. Long lists are split across numbered chat messages (`List` in config). |
| `/delcrt <name>` | Deletes one of your templates. |

Valid color IDs are 1-100 and 128-254. Regions a species doesn't use (including regions still at 0, which ASA shows as a blank swatch) are stored as 255 and skipped when applying; a template never writes 0.

## Requirements

- AsaApi 2.03 or newer, with the Permissions plugin installed.
- Current Visual C++ runtime files (x64). The plugin is built with the latest Visual Studio and expects the matching runtime; an outdated `msvcp140.dll` can crash the server on the first template request. Updated ASA runtime files are kept at https://github.com/bushome/ASABinaries.

## Build

1. Clone with the AsaApi submodule: `git submodule add https://github.com/ArkServerApi/AsaApi.git extern/AsaApi` (or `git submodule update --init` if you cloned a repo that already has it).
2. Put `AsaApi.lib` in `extern\AsaApi\out_lib\` (from the AsaApi release matching the submodule, or build `extern\AsaApi\AsaApi.sln`).
3. `lib\Permissions.lib` (ASA Permissions import library) is included.
4. Make sure Visual Studio has vcpkg integrated (`vcpkg integrate install`). The manifest pulls `libmariadb`, `sqlite3` and `fmt` and links them statically.
5. Open `CreatureColors.sln`, Release | x64, build. Output lands in `out\`.

## Install

Each build assembles a ready-to-copy folder at `out\CreatureColors\` containing `CreatureColors.dll`, `CreatureColors.pdb`, `PluginInfo.json`, `config.json` and `config.commented.json`. Copy that folder to `ShooterGame\Binaries\Win64\ArkApi\Plugins\`, then fill in your MariaDB credentials in `config.json`. The plugin reads `config.json`, which has no comments; `config.commented.json` is an annotated copy that explains each setting.

Storage is chosen by `UseMysql`: `true` uses the MariaDB server in `Mysql` (create the database and grant the user access first); `false` uses a local SQLite file, `database\creaturecolors.db` inside the plugin folder by default, with nothing to set up. Either way the plugin creates its table on load.

Running a decay plugin with MariaDB? `docs/decay-cleanup.md` has an optional database trigger that deletes a player's templates when they decay.

Grant the permission named in `Permission` (set to `Patrons`) to the group(s) that should be able to color. `Permission` can also be a list, such as `["Patrons", "VIP"]`; having any one of them is enough. Only the player's own groups are checked, not tribe groups.

## Things to verify on a test server first

These depend on ASA internals that can shift between game patches:

- **Aim targeting** (`GetAimedActor` with `ECC_GameTraceChannel2`): `/setcr` should pick up the creature under your crosshair at normal interaction range.
- **Unused regions**: a region counts as unused when the game won't colorize it (`GetUsesColorizationRegion`) or it holds 0 (never given a color, shown as a blank swatch). Try `/setcr` on a freshly summoned Raptor's region 2; it should be rejected.
- **Color sync**: colors are written to `ColorSetIndices` and pushed with `ForceUpdateColorSets`. Check that other players see the change without relogging, and that it survives a cryopod round trip and a server restart.
