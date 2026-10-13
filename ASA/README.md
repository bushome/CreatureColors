# CreatureColors (ASA)

AsaApi plugin that lets players with one Permissions-plugin permission recolor tames owned by them or their tribe.

| Command | What it does |
|---|---|
| `/setcr <region> <color>` | Sets one region (0-5) on the creature you're aiming at. |
| `/savecrt <name>` | Saves the aimed creature's six regions as a template under your EOS ID. If the name exists you get a y/n overwrite prompt. |
| `/setcrtaoe <name> [radius]` | Applies the template to your/your tribe's creatures of the same species within the radius (default 300, capped at `AOE.MaxRadius`). |

Valid color IDs are 1-100 and 128-254. Regions a species doesn't use are stored as 255 and skipped when applying.

## Build

1. Clone with the AsaApi submodule: `git submodule add https://github.com/ArkServerApi/AsaApi.git extern/AsaApi` (or `git submodule update --init` if you cloned a repo that already has it).
2. Put `AsaApi.lib` in `extern\AsaApi\out_lib\` (from the AsaApi release matching the submodule, or build `extern\AsaApi\AsaApi.sln`).
3. `lib\Permissions.lib` (ASA Permissions import library) is included.
4. Make sure Visual Studio has vcpkg integrated (`vcpkg integrate install`). The manifest pulls `libmariadb` and `fmt` and links them statically.
5. Open `CreatureColors.sln`, Release | x64, build. Output lands in `out\`.

## Install

Copy into `ShooterGame\Binaries\Win64\ArkApi\Plugins\CreatureColors\`:

- `CreatureColors.dll` (and `CreatureColors.pdb` if you want readable crash logs)
- `configs\PluginInfo.json`
- `configs\config.json`, with your MariaDB credentials filled in

Create the database named in `Mysql.Database` and grant the user access. The plugin creates its table on load.

Grant the permission in `Permission` (default `CreatureColors.Use`) to whichever groups should be able to color.

## Things to verify on the PTR first

These depend on ASA internals that can shift between game patches:

- **Aim targeting** (`GetAimedActor` with `ECC_GameTraceChannel2`): `/setcr` should pick up the creature under your crosshair at normal interaction range.
- **Unused regions**: read from the class's sparse data (`PreventColorizationRegions`). Try `/setcr` on a region a species doesn't show; it should be rejected.
- **Color sync**: colors are written to `ColorSetIndices` and pushed with `ForceUpdateColorSets`. Check that other players see the change without relogging, and that it survives a cryopod round trip and a server restart.
