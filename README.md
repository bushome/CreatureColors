```
 _____                _
/  __ \              | |
| /  \/_ __ ___  __ _| |_ _   _ _ __ ___
| |   | '__/ _ \/ _` | __| | | | '__/ _ \
| \__/\ | |  __/ (_| | |_| |_| | | |  __/
 \____/_|  \___|\__,_|\__|\__,_|_|  \___|
 _____       _                        _.-""""""""-._
/  __ \     | |                     .'  (@)    (#)   '.
| /  \/ ___ | | ___  _ __ ___      /  (%)   .--.   (*) \
| |    / _ \| |/ _ \| '__/ __|    |  (o)   (    )      |
| \__/\ (_) | | (_) | |  \__ \     \   (&)  '--'    _.-'
 \____/\___/|_|\___/|_|  |___/      '._    (+)   .-'
                                       '-._____.-'
```

Server plugin that lets players recolor their own or their tribe's tames with chat commands, save a
creature's colors as a named template, and apply it to every tame of the same species around them.
Access is an individual perk gated by a Permissions permission; templates are stored in MariaDB or SQLite.

Status: complete. Both versions are tested and running in production. This software will receive only bug fixes for future game state version updates. No new feature sets will be added beyond what is presented here.

| Folder | Game | Server API (submodule) | Player key |
|---|---|---|---|
| [`ASA`](ASA/README.md) | ARK: Survival Ascended | [AsaApi](https://github.com/ArkServerApi/AsaApi) at `ASA/extern/AsaApi` | EOS ID |
| [`ASE`](ASE/README.md) | ARK: Survival Evolved | [ArkServerApi (AseApi)](https://github.com/ArkServerApi/AseApi) at `ASE/extern/AseApi` | SteamID64 |

## Commands

All commands need the permission set in `config.json` (`Permission`). Only the player's own groups count,
never tribe groups. Command names can be changed in `config.json`, and every reply goes only to the
player who ran the command. Template names are 1-32 characters (letters, numbers, `-` and `_`) and
are not case-sensitive.

### `/setcr <region> <color>`

Sets one color region on the tame you're looking at.

- `region` is 0-5 and must be a region that species actually paints. Regions the species doesn't use
  (left blank in the inspect panel, such as a Raptor's region 2) are refused.
- `color` is a color ID: ASA 1-100 and 128-254, ASE 1-100 and 201-226.
- The tame must belong to you or your tribe; wild creatures and other tribes' tames are refused.
- Example: `/setcr 0 79`

### `/savecrt <name>`

Saves all six regions of the tame you're looking at as a template under your own player ID.

- The template remembers the creature's species; regions that species doesn't use are stored as unused
  and are never painted when the template is applied.
- If you already have a template with that name, you're asked whether to overwrite it. Answer with
  `/y` to overwrite or `/n` to cancel; with no answer within 30 seconds the save is cancelled.
- Example: `/savecrt firestripe`

### `/setcrtaoe <name> [radius]`

Applies one of your templates to every tame around you of the same species.

- Only exact species matches are colored. Variants count as different species, so a Raptor template
  doesn't apply to an Aberrant Raptor. Other species in range are skipped and counted in the reply.
- Only tames owned by you or your tribe are touched.
- `radius` is in Unreal units: default 300 (1 foundation), maximum 3000 (10 foundations). A larger
  number runs at the maximum with a notice; anything that isn't a whole number above 0 shows usage.
- Example: `/setcrtaoe firestripe 1500`

### `/listcrt [species]`

Lists your saved templates.

- Looking at a creature: shows only templates for that exact species.
- `/listcrt rex`: shows species whose short name contains the text (the short name is the blueprint
  class without `_Character_BP`, for example `Rex`, `Raptor_Aberrant`, `Quetz`, `Argent`).
- Neither: shows everything, grouped by species, e.g. `Raptor: blue, red | Rex: fire, ice`.
- Long lists are split across numbered chat messages `(1/3)`; after the configured limit the rest is
  summarized as `...and N more`.

### `/delcrt <name>`

Deletes one of your templates. You can only delete your own; a name that doesn't exist is reported.
It also cancels a pending `/savecrt` overwrite prompt for the same name.

- Example: `/delcrt firestripe`

### `/y` and `/n`

Answer the overwrite prompt from `/savecrt`. They're commands rather than plain chat so the answer
never appears in global chat; outside a prompt they report that nothing is waiting.

Each folder is a self-contained Visual Studio solution. Clone with submodules:

```
git clone --recurse-submodules https://github.com/bushome/CreatureColors.git
```

or, in an existing clone:

```
git submodule update --init --recursive
```

The commands (`/setcr`, `/savecrt`, `/setcrtaoe`, `/listcrt`, `/delcrt`), `src/Config.h` and `config.json`
are the same in both folders. `src/Main.cpp`, `src/DinoColors.h`, `src/PermissionsApi.h`, the storage
backends and the project files differ by API, and the template table is keyed by `eos_id` on ASA and
`steam_id` on ASE. Valid color IDs also differ by game (ASA 1-100 and 128-254, ASE 1-100 and 201-226).
Install, commands, configuration, the optional decay-cleanup triggers and build steps are in each
folder's README and `docs/`; `STATUS.md` has the test results for both versions.
