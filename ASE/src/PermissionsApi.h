#pragma once

// Bridge to the ASE Permissions plugin that ships with ArkServerApi (Permissions.dll).
//
// The functions are looked up in the loaded Permissions.dll at runtime (GetProcAddress) instead of being
// linked through an import library, so no Permissions.lib is needed to build and none is shipped. They keep
// the Permissions:: names Main.cpp calls. The strings are the decorated export names in Permissions.dll:
//   ?GetPlayerGroups@Permissions@@YA?AV?$TArray@VFString@@VFDefaultAllocator@@@@_K@Z
//   ?GetTribeGroups@Permissions@@YA?AV?$TArray@VFString@@VFDefaultAllocator@@@@H@Z
//   ?IsGroupHasPermission@Permissions@@YA_NAEBVFString@@0@Z
// To check them against an installed Permissions.dll:  dumpbin /exports Permissions.dll | findstr Permissions@@
//
// Note: for an online player GetPlayerGroups includes their tribe's groups (and "TribeSize:N"/"TribeOnline:N").
// If Permissions.dll is not loaded yet, or a function is missing, the lookups return no groups / no permission
// (commands are refused, never allowed by mistake) and the missing export is logged once.

#include "API/ARK/Ark.h"
#include "Logger/Logger.h"

namespace Permissions
{
    namespace detail
    {
        using GetPlayerGroupsFn = TArray<FString> (*)(uint64 steam_id);
        using GetTribeGroupsFn = TArray<FString> (*)(int tribe_id);
        using IsGroupHasPermissionFn = bool (*)(const FString& group, const FString& permission);

        constexpr const char* kGetPlayerGroups = "?GetPlayerGroups@Permissions@@YA?AV?$TArray@VFString@@VFDefaultAllocator@@@@_K@Z";
        constexpr const char* kGetTribeGroups = "?GetTribeGroups@Permissions@@YA?AV?$TArray@VFString@@VFDefaultAllocator@@@@H@Z";
        constexpr const char* kIsGroupHasPermission = "?IsGroupHasPermission@Permissions@@YA_NAEBVFString@@0@Z";

        struct Functions
        {
            GetPlayerGroupsFn get_player_groups = nullptr;
            GetTribeGroupsFn get_tribe_groups = nullptr;
            IsGroupHasPermissionFn is_group_has_permission = nullptr;
        };

        inline const Functions& Resolve()
        {
            static Functions functions;
            static bool resolved = false;
            if (resolved)
                return functions;

            const HMODULE module = GetModuleHandleW(L"Permissions.dll");
            if (module == nullptr)
                return functions;   // all null until Permissions is loaded; try again next time

            auto find = [module](const char* name) {
                void* address = reinterpret_cast<void*>(GetProcAddress(module, name));
                if (address == nullptr)
                    Log::GetLog()->error("Permissions.dll is loaded but does not export {}; permission checks will fail", name);
                return address;
            };
            functions.get_player_groups = reinterpret_cast<GetPlayerGroupsFn>(find(kGetPlayerGroups));
            functions.get_tribe_groups = reinterpret_cast<GetTribeGroupsFn>(find(kGetTribeGroups));
            functions.is_group_has_permission = reinterpret_cast<IsGroupHasPermissionFn>(find(kIsGroupHasPermission));
            resolved = true;
            return functions;
        }
    }

    inline TArray<FString> GetPlayerGroups(uint64 steam_id)
    {
        const detail::Functions& f = detail::Resolve();
        return f.get_player_groups != nullptr ? f.get_player_groups(steam_id) : TArray<FString>();
    }

    inline TArray<FString> GetTribeGroups(int tribe_id)
    {
        const detail::Functions& f = detail::Resolve();
        return f.get_tribe_groups != nullptr ? f.get_tribe_groups(tribe_id) : TArray<FString>();
    }

    inline bool IsGroupHasPermission(const FString& group, const FString& permission)
    {
        const detail::Functions& f = detail::Resolve();
        return f.is_group_has_permission != nullptr && f.is_group_has_permission(group, permission);
    }
}
