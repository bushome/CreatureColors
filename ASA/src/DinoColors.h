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

    // True if the actor is a creature. Walks the class hierarchy using only core reflection fields
    // (UObjectBase.ClassPrivate, UStruct.SuperStruct) instead of UPrimalActor::IsPrimalDino(), whose
    // offset is not in every AsaApi cache.
    inline bool IsDino(AActor* actor)
    {
        if (!actor)
            return false;

        static UClass* dino_class = APrimalDinoCharacter::GetPrivateStaticClass();
        if (!dino_class)
            return false;

        for (UStruct* cls = actor->ClassPrivateField(); cls; cls = cls->SuperStructField())
        {
            if (cls == dino_class)
                return true;
        }
        return false;
    }

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
        if (!IsDino(actor))
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

    // A region counts as used when the game allows colorizing it AND it currently holds a color.
    // ASA has no flag for regions a species' model doesn't paint (e.g. Raptor region 2): those are
    // simply never given a color at spawn and sit at 0 ("unset"), which is also what the inspect
    // panel shows as a blank swatch. So a region at 0 is treated as unused: /setcr refuses it,
    // /savecrt stores 255 for it, and applying a template leaves it alone.
    inline bool IsRegionUsed(APrimalDinoCharacter* dino, int region)
    {
        if (!dino || region < 0 || region >= kRegionCount)
            return false;
        if (dino->ColorSetIndicesField()()[region] == 0)
            return false;
        return dino->GetUsesColorizationRegion(region);
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
            // 255 = unused on the source creature; 0 = "unset" (never written, it can make the game
            // re-roll colors); and regions this creature doesn't use are left alone.
            if (color == kUnusedRegion || color == 0 || !IsRegionUsed(dino, r))
                continue;
            SetRegion(dino, r, color);
        }
    }
}
