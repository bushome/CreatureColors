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
