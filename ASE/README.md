# CreatureColors (ASE)

ArkServerApi (3.56) plugin that lets players with one Permissions-plugin permission recolor tames owned by them or their tribe.

| Command | What it does |
|---|---|
| `/setcr <region> <color>` | Sets one region (0-5) on the creature you're aiming at. |
| `/savecrt <name>` | Saves the aimed creature's six regions as a template under your SteamID. If the name exists you are asked to confirm with `/y` or `/n`. |
| `/setcrtaoe <name> [radius]` | Applies the template to your/your tribe's creatures of the same species within the radius (default 300). Larger requests run at `AOE.MaxRadius` with a notice; a radius that isn't a whole number above 0 shows usage. |
| `/listcrt [species]` | Lists your templates. Looking at a creature shows only that species' templates; `/listcrt rex` shows species whose short name contains "rex"; otherwise everything, grouped by species. Long lists are split across numbered chat messages (`List` in config). |
| `/delcrt <name>` | Deletes one of your templates. |

Valid color IDs are 1-100 and 201-226. Color 227, which some mutated creatures carry, is kept when a template is saved and applied, but can't be set with `/setcr`. Regions a species doesn't use (the game's PreventColorizationRegions, shown as blank in the inspect panel) are stored as 255 and skipped when applying; a template never writes 0.

## Requirements

- ArkServerApi 3.56, with the Permissions plugin that ships with it.
- Current Visual C++ 2015-2022 runtime (x64), installed system-wide and in the server's `Win64` folder if it carries its own copies. The plugin is built with the latest Visual Studio and expects the matching runtime; an outdated `msvcp140.dll` can crash the server on the first template request.

## Build

1. Put the ArkServerApi source in `extern\AseApi`: `git clone https://github.com/ArkServerApi/AseApi.git extern/AseApi`. Its `out_lib\ArkApi.lib` is used as is; you don't build the API. (The `main` branch is 3.56 plus header-only additions.)
2. `lib\Permissions.lib` (import library of the Permissions plugin that ships with ArkServerApi) is included.
3. Make sure Visual Studio has vcpkg integrated (`vcpkg integrate install`). The manifest pulls `libmariadb` and `sqlite3` and links them statically. fmt isn't needed: ArkServerApi bundles its own.
4. Open `CreatureColors.sln`, Release | x64, build. Output lands in `out\`.

## Install

Each build assembles a ready-to-copy folder at `out\CreatureColors\` containing `CreatureColors.dll`, `CreatureColors.pdb`, `PluginInfo.json`, `config.json` and `config.commented.json`. Copy that folder to `ShooterGame\Binaries\Win64\ArkApi\Plugins\`, then fill in your MariaDB credentials in `config.json`. The plugin reads `config.json`, which has no comments; `config.commented.json` is an annotated copy that explains each setting.

Storage is chosen by `UseMysql`: `true` uses the MariaDB server in `Mysql` (create the database and grant the user access first); `false` uses a local SQLite file, `database\creaturecolors.db` inside the plugin folder by default, with nothing to set up. Either way the plugin creates its table on load.

Running a decay plugin with MariaDB? `docs/decay-cleanup.md` has an optional database trigger that deletes a player's templates when they decay.

Grant the permission named in `Permission` (set to `Patrons`) to the group(s) that should be able to color. `Permission` can also be a list, such as `["Patrons", "VIP"]`; having any one of them is enough. Only the player's own groups are checked, not tribe groups. On ASE the Permissions plugin reports a tribe's groups as the player's own, so the plugin leaves out any group the player's tribe is in; don't add tribes to the group that holds this permission.

## Things to verify on a test server first

- **Aim targeting** (`GetAimedActor` with `ECC_GameTraceChannel2`): `/setcr` should pick up the creature under your crosshair at normal interaction range.
- **Unused regions**: `/setcr` on a region the species doesn't paint should be refused (e.g. a Raptor region with a blank swatch in the inspect panel).
- **Dye colors**: `/setcr 0 210` should apply (IDs above 127 are read and written unsigned).
- **Color sync**: other players see the change without relogging, and it survives a cryopod round trip.
- **Permission filter**: a player in `Patrons` personally can use the commands; a tribemate who isn't can't, even if the tribe were added to a group.
