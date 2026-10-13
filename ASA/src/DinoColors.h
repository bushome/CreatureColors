#pragma once

#include "API/ARK/Ark.h"
#include "Database.h"

namespace DinoColors
{
    constexpr int kRegionCount = 6;

    // ASA color IDs: standard 1-100, dyes 128-254. 0 is "unset" (mutation-only) and 255 is our sentinel,
    // so neither can be set by command.
    inline bool IsValidColorId(int color)
    {
        return (color >= 1 && color <= 100) || (color >= 128 && color <= 254);
    }

    // Tamed creatures sit on teams >= 50000; wild ones are below that.
    constexpr int kFirstPlayerTeam = 50000;

    // The creature the player is looking at, if it is a creature at all.
    inline APrimalDinoCharacter* GetAimedDino(AShooterPlayerController* pc)
    {
        if (!pc)
            return nullptr;

        AShooterCharacter* character = pc->GetPlayerCharacter();
        if (!character || character->IsDead())
            return nullptr;

        AActor* actor = character->GetAimedActor(ECollisionChannel::ECC_GameTraceChannel2, nullptr, 0.0f, 0.0f,
                                                 nullptr, nullptr, false, false, false, nullptr);
        if (!actor || !actor->IsPrimalDino())
            return nullptr;

        return static_cast<APrimalDinoCharacter*>(actor);
    }

    // Owned by the player's tribe, or by the player when they have no tribe: in both cases the
    // creature's targeting team equals the player's.
    inline bool IsOwnedBy(APrimalDinoCharacter* dino, AShooterPlayerController* pc)
    {
        if (!dino || !pc)
            return false;

        const int dino_team = dino->TargetingTeamField();
        return dino_team >= kFirstPlayerTeam && dino_team == pc->TargetingTeamField();
    }

    // In ASA, PreventColorizationRegions lives in the class's sparse data. A non-zero entry means
    // the species doesn't use that region.
    inline bool IsRegionUsed(APrimalDinoCharacter* dino, int region)
    {
        if (!dino || region < 0 || region >= kRegionCount)
            return false;

        const unsigned char* prevent = nullptr;

        if (UClass* cls = dino->ClassPrivateField())
        {
            const void* sparse = cls->GetSparseClassData(EGetSparseClassDataMethod::ArchetypeIfNull);
            if (sparse)
                prevent = GetNativePointerField<const unsigned char*>(
                    sparse, "FPrimalDinoCharacterSparseClassData.PreventColorizationRegions");
        }

        if (!prevent)
            prevent = dino->PreventColorizationRegionsField()();

        return prevent && prevent[region] == 0;
    }

    inline std::string GetSpecies(APrimalDinoCharacter* dino)
    {
        return AsaApi::GetApiUtils().GetBlueprint(dino).ToString();
    }

    // Snapshot of all six regions; regions the species doesn't use are stored as 255.
    inline ColorTemplate Capture(APrimalDinoCharacter* dino)
    {
        ColorTemplate tpl;
        tpl.species = GetSpecies(dino);

        unsigned __int8* colors = dino->ColorSetIndicesField()();
        for (int r = 0; r < kRegionCount; ++r)
            tpl.regions[r] = IsRegionUsed(dino, r) ? colors[r] : kUnusedRegion;

        return tpl;
    }

    inline void SetRegion(APrimalDinoCharacter* dino, int region, uint8_t color)
    {
        dino->ColorSetIndicesField()()[region] = color;
        dino->ForceUpdateColorSets(region, color);
    }

    // Applies every stored region except sentinel entries and regions this creature doesn't use.
    inline void Apply(APrimalDinoCharacter* dino, const ColorTemplate& tpl)
    {
        for (int r = 0; r < kRegionCount; ++r)
        {
            const uint8_t color = tpl.regions[r];
            if (color == kUnusedRegion || !IsRegionUsed(dino, r))
                continue;
            SetRegion(dino, r, color);
        }
    }
}
