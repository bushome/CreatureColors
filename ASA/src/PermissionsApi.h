#pragma once

// Functions exported by the ASA Permissions plugin (Permissions.dll), linked through lib\Permissions.lib.
// Signature matches the decorated name in that import library:
//   ?IsPlayerHasPermission@Permissions@@YA_NAEBVFString@@0@Z

#include "API/ARK/Ark.h"

namespace Permissions
{
    __declspec(dllimport) bool IsPlayerHasPermission(const FString& eos_id, const FString& permission);}
