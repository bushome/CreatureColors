#pragma once

// Functions exported by the ASA Permissions plugin (Permissions.dll), linked through lib\Permissions.lib.
// Signatures match the decorated names in that import library:
//   ?IsPlayerHasPermission@Permissions@@YA_NAEBVFString@@0@Z
//   ?IsTribeHasPermission@Permissions@@YA_NHAEBVFString@@@Z

#include "API/ARK/Ark.h"

namespace Permissions
{
    __declspec(dllimport) bool IsPlayerHasPermission(const FString& eos_id, const FString& permission);
    __declspec(dllimport) bool IsTribeHasPermission(int tribe_id, const FString& permission);
}
