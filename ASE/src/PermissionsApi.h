#pragma once

// Functions exported by the ASE Permissions plugin that ships with ArkServerApi (Permissions.dll),
// linked through lib\Permissions.lib. Declarations match Permissions/Public/ArkPermissions.h.

#include "API/ARK/Ark.h"

namespace Permissions
{
    // Note: for an online player this includes their tribe's groups (and "TribeSize:N"/"TribeOnline:N").
    __declspec(dllimport) TArray<FString> GetPlayerGroups(uint64 steam_id);
    __declspec(dllimport) TArray<FString> GetTribeGroups(int tribeId);
    __declspec(dllimport) bool IsGroupHasPermission(const FString& group, const FString& permission);
}
