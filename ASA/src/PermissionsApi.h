#pragma once

// Bridge to the ASA Permissions plugin (Permissions.dll).
//
// The function is looked up in the loaded Permissions.dll at runtime (GetProcAddress) instead of being
// linked through an import library, so no Permissions.lib is needed to build and none is shipped. It keeps
// the Permissions:: name Main.cpp calls. The string is the decorated export name in Permissions.dll:
//   ?IsPlayerHasPermission@Permissions@@YA_NAEBVFString@@0@Z
// To check it against an installed Permissions.dll:  dumpbin /exports Permissions.dll | findstr IsPlayerHasPermission
//
// If Permissions.dll is not loaded yet, or does not export the function, the check answers "no permission"
// (commands are refused, never allowed by mistake) and the missing export is logged once.

#include "API/ARK/Ark.h"

namespace Permissions
{
    namespace detail
    {
        using IsPlayerHasPermissionFn = bool (*)(const FString& eos_id, const FString& permission);
        constexpr const char* kIsPlayerHasPermission = "?IsPlayerHasPermission@Permissions@@YA_NAEBVFString@@0@Z";

        inline IsPlayerHasPermissionFn Resolve()
        {
            static IsPlayerHasPermissionFn function = nullptr;
            static bool resolved = false;
            if (resolved)
                return function;

            const HMODULE module = GetModuleHandleW(L"Permissions.dll");
            if (module == nullptr)
                return nullptr;   // not loaded yet; try again next time

            function = reinterpret_cast<IsPlayerHasPermissionFn>(reinterpret_cast<void*>(GetProcAddress(module, kIsPlayerHasPermission)));
            resolved = true;
            if (function == nullptr)
                Log::GetLog()->error("Permissions.dll is loaded but does not export {}; permission checks will fail", kIsPlayerHasPermission);
            return function;
        }
    }

    inline bool IsPlayerHasPermission(const FString& eos_id, const FString& permission)
    {
        const detail::IsPlayerHasPermissionFn function = detail::Resolve();
        return function != nullptr && function(eos_id, permission);
    }
}
