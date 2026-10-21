# CreatureColors status

## ASA: verified on the test server (game 94.25, AsaApi 2.03)
- Plugin loads; Permissions.dll is delay-loaded (AsaApi loads plugins before Permissions otherwise -> error 126).
- `/setcr`, `/savecrt`, `/setcrtaoe` work. Same-species filter works.
- Overwrite flow: `/y` updates the same row (id/created_at kept), `/n` cancels, 30 s timeout cancels, stray `/y`/`/n` reports nothing waiting. Ordinary chat doesn't interfere.
- Refusals: no target, wild/other-owner creature.
- MariaDB: table auto-created; row has 32-char EOS ID, full `Blueprint'...'` species path, six regions.
- SQLite: folder, file and table auto-created; save, overwrite and apply all worked (first round of testing ran in SQLite mode). Storage code unchanged since.
- All offsets the plugin uses are present in the current AsaApi cache (`tools/check-offsets.ps1`).
- Radius cap enforced: a same-species tame ~21 foundations out stays uncolored with a 10000 radius, and `RadiusCapped` ("Radius 10000 is above the limit, using 3000 instead") shows before `AOEResult`.
- Nonexistent template: `TemplateNotFound` ("No template named 'wackytemp'").
- Name with a space: shows usage (second word isn't taken as part of the name).
- Name over 32 chars: `InvalidTemplateName`.
- Permission denied: with `Permission` set to a group nobody has, commands return `NoPermission`.
- Hot reload via `CreatureColors.dll.arkapi` works (clean unload/reload, table check reruns).
- Cryopod round trip: colors (199/58/45/21/208/36) kept in the cryopod data and on release. Restart persistence treated as covered: the colors are in the creature's saved color data, which the cryopod serializes the same way the world save does.
- Visibility to other players: ruled settled. Colors go into the replicated ColorSetIndices and are pushed with the ForceUpdateColorSets multicast, so other clients get them through normal engine replication.
- Unused region: ASA has no flag for regions a model doesn't paint, so a region at 0 (never colored, blank swatch) or not colorizable counts as unused. Fresh Aberrant Raptor: `/setcr 2 79` refused, `/setcr 0 79` applied, `/savecrt raptorprobe` saved.
- Radius validation: `abc`, `0`, `-5` each show `UsageAOE` and nothing runs.
- Note: MariaDB leaves `updated_at` unchanged on an identical overwrite (no-op update). Accepted as is.
- `/listcrt` (MariaDB, 2026-10-14): plain list grouped by species (`Allo: ... | Raptor_Aberrant: ...`); looking at an Aberrant Raptor lists only `Raptor_Aberrant`; looking at a Moschops with none gives `NoTemplatesSpecies`; `/listcrt rap` filter works. 32 templates paged into 3 numbered messages `(1/3)`..`(3/3)`, continuation lines restate the species, natural order (`pagetest_9, _10, _11`).
- `/delcrt` (MariaDB): `/delcrt pagetest_5` deletes, repeating it gives `TemplateNotFound`.
- Command replies are private to the player who ran them (sent per player, not broadcast). A mistyped command isn't recognized and goes to chat as normal.

## ASA: added, needs a test-server pass
- Optional decay cleanup trigger: `asa/docs/decay-cleanup.md` has a ready Lethal Decay version (AFTER UPDATE on `decayasa.lethaldecay_tribes`, `decayedlastlogin = -2`, purges only when all of a player's rows are decayed). Trigger created on the live MariaDB (2026-10-14). Untested: decay is disabled until after the UE 5.8 update. When it's back on, confirm player rows (not just the EOS_ID '0' tribe row) get -2.

## ASA: still to test
- Second player check dropped by the owner (individual perk; tribes never added to permission groups).

## ASE version: verified on the test server and deployed to live (2026-10-14)
Source in `ase/` (project) / `CreatureColors-ASE`. ArkServerApi 3.56 (`ArkServerApi/AseApi`, main = 3.56 + header-only additions), bundled Permissions plugin.
Carried over: all commands incl. `/listcrt` filters + paging and `/delcrt`, `/y`/`/n`, list-or-string `Permission`, `UseMysql` MariaDB/SQLite, plain + commented configs, radius validation, plugin-folder assembly, delay-loaded Permissions.dll.
ASE-specific:
- Owner key `steam_id BIGINT UNSIGNED` (SQLite INTEGER). ASE machine has its own MariaDB, same defaults (`creaturecolors` / `creaturecolors_templates`).
- Colors 1-100, 201-226 for `/setcr`; 227 kept in captured templates and applied. Color array read/written unsigned.
- Unused regions: `PreventColorizationRegions` OR value 0. Tested 2026-10-14: fresh Raptor saved as 26/21/0/27/30/28, i.e. region 2 = 0 and not flagged, so the ASA 0 rule was added. Applying never writes 0 or 255.
- Permission: ASE Permissions' `IsPlayerHasPermission`/`GetPlayerGroups` add the tribe's groups (+ `TribeSize:`/`TribeOnline:`) for online players. Plugin uses `GetPlayerGroups` minus `GetTribeGroups(team)` minus those prefixes, then `IsGroupHasPermission` (also `*`). Edge: a group given to both player and tribe is excluded.
- `IsDino` = `IsA(APrimalDinoCharacter::GetPrivateStaticClass())`; aim = 9-arg `GetAimedActor` with `ECC_GameTraceChannel2`; AOE position = `IApiUtils::GetPosition`.
- Build: C++20 (API headers use std::bit_cast), toolset v145, requires current VC++ runtime on the server (no old-runtime mutex workaround, by choice), no vcpkg fmt (API bundles it), hot-reload copy `.dll.ArkApi`.
Checked here: SQLite backend round trip with SteamIDs and 201/226/227; MariaDB backend syntax vs connector headers; list packing in C++17; Main.cpp names/signatures vs 3.56 headers (clang, stubbed Windows). MSVC build (VS 2026, v145, C++20) succeeds 2026-10-14; links against ArkApi.lib + Permissions.lib. Loads on the ASE test server (Permissions.dll delay-load OK); MariaDB table `creaturecolors.creaturecolors_templates` auto-created with `steam_id` key (2026-10-14). Note: ArkServerApi crashes at startup (PluginManager.cpp:139, json value()) if `Win64\config.json` (the API's own settings file) is missing; not a plugin issue. In game (2026-10-14): `/setcr 0 210` works (aim channel 2, Patrons permission, dye >127). `/savecrt fresh` saved with 17-digit SteamID. After the 0-rule rebuild: fresh Raptor `/setcr 2` refused with InvalidRegion. Overwrite /y updates the row in place (region_2 now 255). `/setcrtaoe fresh` recolored the magenta Raptor ("Applied to 1 creature(s)"). `/listcrt` aimed ("Your Raptor_Aberrant templates (1): fresh") and not aimed (grouped) work; `/delcrt fresh` deletes, repeat gives TemplateNotFound. Tribemate-without-Patrons check dropped by the owner: individual perk, tribes are never added to permission groups.
- Decay cleanup (ASE): `ase/docs/decay-cleanup.md` has a Lethal Decay version for `decayarkse.lethaldecay_tribes` (marker `decayday = -2`; player rows confirmed marked: 569 rows with steamid <> 0). Old decayed rows trimmed by hand; both triggers (update + delete) created on the live ASE MariaDB 2026-10-14. Awaiting a real decay of a player with templates to confirm.
To test in game: aim pickup (channel 2), unused-region refusal, `/setcr 0 210`, save/overwrite/apply/list/delete, permission (personal Patrons yes, tribemate without no), table auto-created with BIGINT steam_id.
