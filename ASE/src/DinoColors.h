#pragma once

#include "API/ARK/Ark.h"
#include "Database.h"

namespace DinoColors
{
    constexpr int kRegionCount = 6;

    // ASE color IDs: standard 1-100 and dyes 201-226. 227 shows up on some mutated creatures; it is
    // kept when a template is captured but can't be set by command. 0 = no color, 255 = our sentinel.
    inline bool IsValidColorId(int color)
    {
        return (color >= 1 && color <= 100) || (color >= 201 && color <= 226);
    }

    // Tamed creatures sit on teams >= 50000; wild ones are below that.
    constexpr int kFirstPlayerTeam = 50000;

    inline bool IsDino(AActor* actor)
    {
        return actor && actor->IsA(APrimalDinoCharacter::GetPrivateStaticClass());
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
                                                 nullptr, nullptr, false, false, false);
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

    // The color array is declared as char in the API; ASE color IDs go above 127, so read it unsigned.
    inline unsigned char* Colors(APrimalDinoCharacter* dino)
    {
        return reinterpret_cast<unsigned char*>(dino->ColorSetIndicesField()());
    }

    // A region counts as used when the game allows colorizing it AND it currently holds a color.
    // PreventColorizationRegions covers some species, but most leave regions they don't paint unflagged:
    // those are never given a color at spawn and sit at 0 (verified on ASE: untouched Raptor region 2 = 0,
    // same as ASA). So a region at 0 is treated as unused too: /setcr refuses it, /savecrt stores 255 for
    // it, and applying a template leaves it alone.
    inline bool IsRegionUsed(APrimalDinoCharacter* dino, int region)
    {
        if (!dino || region < 0 || region >= kRegionCount)
            return false;
        if (dino->PreventColorizationRegionsField()()[region] != 0)
            return false;
        return Colors(dino)[region] != 0;
    }

    inline std::string GetSpecies(APrimalDinoCharacter* dino)
    {
        return ArkApi::GetApiUtils().GetBlueprint(dino).ToString();
    }

    // Snapshot of all six regions; regions the species doesn't use are stored as 255.
    inline ColorTemplate Capture(APrimalDinoCharacter* dino)
    {
        ColorTemplate tpl;
        tpl.species = GetSpecies(dino);

        const unsigned char* colors = Colors(dino);
        for (int r = 0; r < kRegionCount; ++r)
            tpl.regions[r] = IsRegionUsed(dino, r) ? colors[r] : kUnusedRegion;

        return tpl;
    }

    // Writes the region and pushes it to clients (ForceUpdateColorSets is a multicast).
    inline void SetRegion(APrimalDinoCharacter* dino, int region, uint8_t color)
    {
        Colors(dino)[region] = color;
        dino->ForceUpdateColorSets(region, color);
    }

    // Applies every stored region except sentinel entries and regions this creature doesn't use.
    inline void Apply(APrimalDinoCharacter* dino, const ColorTemplate& tpl)
    {
        for (int r = 0; r < kRegionCount; ++r)
        {
            const uint8_t color = tpl.regions[r];
            // 255 = unused on the source creature; 0 = no color (never written); and regions this
            // creature doesn't use are left alone.
            if (color == kUnusedRegion || color == 0 || !IsRegionUsed(dino, r))
                continue;
            SetRegion(dino, r, color);
        }
    }
}
