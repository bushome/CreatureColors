# Checks that every game offset CreatureColors uses exists in AsaApi's offset cache.
# A name missing from the cache makes AsaApi log "Failed to get the offset of ..." and then
# terminate the server the first time the plugin touches it, so run this after each AsaApi update.
#
# Usage (from any folder):
#   powershell -ExecutionPolicy Bypass -File .\check-offsets.ps1 -Cache "<cache path>"
#
# <cache path> can be:
#   - the generation folder AsaApi uses, e.g. ...\Win64\ArkApi\Cache\generations\<long id>
#   - a cached_offsets.cache file (AsaApi's binary format)
#   - a cached_offsets.txt file

param([Parameter(Mandatory = $true)][string]$Cache)

$names = @(
    'APrimalCharacter.GetAimedActor(ECollisionChannel,UActorComponent**,float,float,int*,FHitResult*,bool,bool,bool,bool*)',
    'APrimalCharacter.IsDeadOrDying()',
    'APrimalDinoCharacter.ColorSetIndices',
    'APrimalDinoCharacter.ForceUpdateColorSets(int,int)',
    'APrimalDinoCharacter.GetUsesColorizationRegion(int)',
    'APrimalDinoCharacter.GetPrivateStaticClass()',
    'UStruct.SuperStruct',
    'UPrimalActor.TargetingTeam',
    'UObjectBase.ClassPrivate',
    'AActor.RootComponent',
    'USceneComponent.ComponentToWorld',
    'AShooterPlayerController.BaseGetPlayerCharacter()',
    'AShooterPlayerController.GetUniqueNetIdAsString()',
    'UVictoryCore.ClassToStringReference(TSubclassOf<UObject>)',
    'UVictoryCore.ServerOctreeOverlapActors(UObject*,UE::Math::TVector<double>,float,EServerOctreeGroup::Type,bool)'
)

# Resolve a folder to the offsets file inside it.
if (Test-Path $Cache -PathType Container) {
    $candidates = @('cached_offsets.cache', 'cached_offsets.txt') | ForEach-Object { Join-Path $Cache $_ } | Where-Object { Test-Path $_ }
    if (-not $candidates) {
        Write-Host "No cached_offsets.cache or cached_offsets.txt in $Cache. Files there:" -ForegroundColor Red
        Get-ChildItem $Cache | ForEach-Object { Write-Host "  $($_.Name)" }
        exit 2
    }
    $Cache = @($candidates)[0]
}
if (-not (Test-Path $Cache -PathType Leaf)) { Write-Host "Not found: $Cache" -ForegroundColor Red; exit 2 }
Write-Host "Checking $Cache`n"

$keys = New-Object 'System.Collections.Generic.HashSet[string]'
$textFallback = $null

if ($Cache.EndsWith('.cache', [StringComparison]::OrdinalIgnoreCase)) {
    # AsaApi format: repeated [uint64 keyLength][key bytes][int64 offset]
    $bytes = [System.IO.File]::ReadAllBytes($Cache)
    $pos = 0
    try {
        while ($pos -lt $bytes.Length) {
            $len = [BitConverter]::ToUInt64($bytes, $pos); $pos += 8
            if ($len -gt 1048576 -or $pos + $len + 8 -gt $bytes.Length) { throw "bad entry at byte $pos" }
            [void]$keys.Add([System.Text.Encoding]::UTF8.GetString($bytes, $pos, [int]$len)); $pos += [int]$len
            $pos += 8
        }
    }
    catch {
        Write-Host "Couldn't parse the binary cache ($_); falling back to a text search." -ForegroundColor Yellow
        $textFallback = [System.Text.Encoding]::GetEncoding(28591).GetString($bytes)
    }
}
else {
    $textFallback = Get-Content -Raw $Cache
}

$missing = 0
foreach ($n in $names) {
    $found = if ($textFallback) { $textFallback.Contains($n) } else { $keys.Contains($n) }
    if ($found) { Write-Host "OK       $n" -ForegroundColor Green }
    else { Write-Host "MISSING  $n" -ForegroundColor Red; $missing++ }
}

if ($missing -eq 0) { Write-Host "`nAll $($names.Count) offsets present." -ForegroundColor Green; exit 0 }
Write-Host "`n$missing missing - don't load CreatureColors on this build until they're fixed." -ForegroundColor Red
exit 1
