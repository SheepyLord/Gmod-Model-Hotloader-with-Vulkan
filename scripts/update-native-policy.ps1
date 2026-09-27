<#
.SYNOPSIS
Adds a published native release to addon/lua/mmdhl/native_policy.lua.

.DESCRIPTION
Downloads the drop-in packages of one "Build drop-in package" Actions run (the
-vulkan package with the DXVK renderer and the -opengl-remix package), reads their
common native-release.json record (release, build, file sizes and SHA-256 of the
five native files and of the bundled d3d9.dll) and
writes it to the local policy with the public repository's release link and, with
-AltReleaseUrl, a second download page (a mirror for players who cannot reach
GitHub; the game shows it as a link and an Alternative download button), as the
approved and recommended release. Package and publish the Workshop addon afterwards.

The artifact is downloaded with the GitHub CLI (gh, logged in) when installed,
otherwise with a token in GITHUB_TOKEN or GH_TOKEN. -Artifact uses an already
downloaded artifact zip or folder instead.

.EXAMPLE
./scripts/update-native-policy.ps1 -RunUrl https://github.com/SheepyLord/Gmod-Model-Hotloader-with-Vulkan/actions/runs/36107145629 -ReleaseUrl https://github.com/SheepyLord/Gmod-Model-Hotloader-with-Vulkan/releases -AltReleaseUrl https://pan.baidu.com/s/1eUaJAUhnnnGpNSnvFojmwQ?pwd=lord
#>
#Requires -Version 7.0
param(
    [Parameter(Mandatory)][string]$RunUrl,
    [Parameter(Mandatory)][string]$ReleaseUrl,
    # Download page for players without GitHub access; any https address.
    [string]$AltReleaseUrl,
    [string]$Artifact,
    # Replace an existing record with the same release label but different files.
    [switch]$Replace
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$policyPath = Join-Path $root 'addon/lua/mmdhl/native_policy.lua'
# Players download from the public repository; the packages may be built by any
# of the owner's repositories.
$repository = 'SheepyLord/Gmod-Model-Hotloader-with-Vulkan'
$owner = 'SheepyLord'

if ($RunUrl -notmatch '^https://github\.com/([^/]+/[^/]+)/actions/runs/(\d+)') { throw "Not an Actions run link: $RunUrl" }
$runRepository, $runId = $Matches[1], $Matches[2]
if (-not $runRepository.StartsWith("$owner/", [StringComparison]::OrdinalIgnoreCase)) { throw "The run belongs to $runRepository, not a repository of $owner" }
# The in-game Download button only opens the releases page or a release under it.
if ($ReleaseUrl -notmatch "^https://github\.com/$([regex]::Escape($repository))/releases(/tag/([^/?#]+))?$") { throw "Not the releases page of ${repository}: $ReleaseUrl" }
$tag = if ($Matches[2]) { [uri]::UnescapeDataString($Matches[2]) } else { $null }
# The game opens and displays this address; installation_ui.lua accepts the same characters.
if ($AltReleaseUrl -and $AltReleaseUrl -cnotmatch '^https://[A-Za-z0-9.-]+[:/][A-Za-z0-9._~:/?#\[\]@!$&''()*+,;=%-]*$') { throw "The alternative link must be a plain https address without spaces or quotes: $AltReleaseUrl" }
if (-not $AltReleaseUrl) { Write-Warning 'No -AltReleaseUrl: players who cannot reach GitHub get no alternative download link for this release' }

$scratch = Join-Path ([IO.Path]::GetTempPath()) ('mmdhl-policy-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null
try {
    if ($Artifact) {
        if (Test-Path -LiteralPath $Artifact -PathType Container) { $source = $Artifact }
        else { Expand-Archive -LiteralPath $Artifact -DestinationPath $scratch; $source = $scratch }
    } elseif (Get-Command gh -ErrorAction SilentlyContinue) {
        gh run download $runId --repo $runRepository --dir $scratch
        if ($LASTEXITCODE) { throw "gh could not download the artifacts of run $runId" }
        $source = $scratch
    } else {
        $token = if ($env:GITHUB_TOKEN) { $env:GITHUB_TOKEN } else { $env:GH_TOKEN }
        if (-not $token) { throw 'Install and log in to the GitHub CLI (gh), set GITHUB_TOKEN, or pass -Artifact with the downloaded artifact' }
        $headers = @{ Authorization = "Bearer $token"; Accept = 'application/vnd.github+json'; 'X-GitHub-Api-Version' = '2022-11-28' }
        $run = Invoke-RestMethod -Headers $headers "https://api.github.com/repos/$runRepository/actions/runs/$runId"
        if ($run.conclusion -ne 'success') { throw "Run $runId did not succeed (status $($run.status), conclusion $($run.conclusion))" }
        # The -vulkan and -opengl-remix packages are separate artifacts of one run.
        $artifacts = @((Invoke-RestMethod -Headers $headers "https://api.github.com/repos/$runRepository/actions/runs/$runId/artifacts").artifacts | Where-Object { -not $_.expired })
        if ($artifacts.Count -lt 1) { throw "Run $runId has no downloadable package artifact" }
        foreach ($item in $artifacts) {
            $zip = Join-Path $scratch "$($item.id).zip"
            Invoke-WebRequest -Headers $headers -Uri $item.archive_download_url -OutFile $zip
            Expand-Archive -LiteralPath $zip -DestinationPath (Join-Path $scratch "artifact-$($item.id)")
        }
        $source = $scratch
    }
    # Every package of one run carries the same record.
    $records = @(Get-ChildItem -LiteralPath $source -Recurse -File -Filter 'mmdhl-native-release.json')
    if ($records.Count -lt 1) { throw 'The artifacts contain no mmdhl-native-release.json' }
    $texts = @($records | ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json | ConvertTo-Json -Depth 10 -Compress } | Select-Object -Unique)
    if ($texts.Count -ne 1) { throw "The artifacts carry $($texts.Count) different release records" }
    $record = $texts[0] | ConvertFrom-Json
    # The record is evidence only for the bytes it ships with: every package must
    # carry the five native files and the game runtime it lists.
    function Test-Shipped([string]$path, $entry) {
        (Test-Path -LiteralPath $path -PathType Leaf) -and (Get-Item -LiteralPath $path).Length -eq $entry.size -and (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -eq $entry.sha256
    }
    foreach ($item in $records) {
        $bin = $item.Directory.FullName
        $game = Split-Path (Split-Path (Split-Path $bin -Parent) -Parent) -Parent
        foreach ($property in $record.files.PSObject.Properties) {
            if (-not (Test-Shipped (Join-Path $bin $property.Value.name) $property.Value)) { throw "$($item.FullName): the package does not contain the $($property.Name) file its record lists" }
        }
        if (-not (Test-Shipped (Join-Path $game "bin\win64\$($record.files.runtime.name)") $record.files.runtime)) { throw "$($item.FullName): the package does not contain the game runtime its record lists" }
    }
    # A shipped renderer must be the one the record names, and a recorded one must ship.
    $renderers = @(Get-ChildItem -LiteralPath $source -Recurse -File -Filter 'd3d9.dll')
    foreach ($dll in $renderers) {
        if (-not $record.renderer -or -not (Test-Shipped $dll.FullName $record.renderer)) { throw "$($dll.FullName) does not match the release record's renderer" }
    }
    if ($record.renderer -and $renderers.Count -lt 1) { throw 'The release record names a renderer, but no package ships d3d9.dll' }
} finally {
    Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
}

# The record must describe all five native files of one build.
$names = @{ client = 'gmcl_mmdhl_win64.dll'; server = 'gmsv_mmdhl_win64.dll'; runtime = 'mmdhl_runtime_win64.dll'; worker = 'mmdhl_worker.exe'; coacd = 'lib_coacd.dll' }
if (-not $record.release -or -not $record.build -or $record.platform -ne 'win64' -or $record.installApi -ne 1) { throw 'The artifact record is not a native release record' }
foreach ($key in $names.Keys) {
    $file = $record.files.$key
    if (-not $file -or $file.name -ne $names[$key] -or $file.size -le 0 -or $file.sha256 -notmatch '^[0-9a-f]{64}$') { throw "The artifact record has no valid $key entry" }
}
# The bundled DXVK (bin/win64/d3d9.dll of the default package) is optional.
if ($record.renderer) {
    $renderer = $record.renderer
    if ($renderer.name -ne 'd3d9.dll' -or $renderer.kind -ne 'dxvk' -or $renderer.size -le 0 -or $renderer.sha256 -notmatch '^[0-9a-f]{64}$') { throw 'The artifact record has no valid renderer entry' }
}
if ($tag -and $tag -ne $record.release) { Write-Warning "The release tag '$tag' differs from the compiled release label '$($record.release)'" }
$record.url = $ReleaseUrl
if ($AltReleaseUrl) { $record | Add-Member -NotePropertyName altUrl -NotePropertyValue $AltReleaseUrl -Force }

$text = Get-Content -LiteralPath $policyPath -Raw
$start = $text.IndexOf('[==['); $end = $text.IndexOf(']==]')
if ($start -lt 0 -or $end -lt $start) { throw "Unexpected format: $policyPath" }
$policy = $text.Substring($start + 4, $end - $start - 4) | ConvertFrom-Json
$label = $record.release
$existing = $policy.releases.PSObject.Properties[$label]
if ($existing) {
    $same = $existing.Value.build -eq $record.build
    foreach ($key in $names.Keys) { $same = $same -and $existing.Value.files.$key.sha256 -eq $record.files.$key.sha256 -and $existing.Value.files.$key.size -eq $record.files.$key.size }
    $same = $same -and ($existing.Value.renderer.sha256 -eq $record.renderer.sha256) -and ($existing.Value.renderer.size -eq $record.renderer.size)
    if (-not $same -and -not $Replace) { throw "native_policy.lua already records release $label with other binaries or build. Set a new MMDHL_RELEASE for new binaries, or pass -Replace if $label was never published." }
    $policy.releases.PSObject.Properties.Remove($label)
}
$policy.releases | Add-Member -NotePropertyName $label -NotePropertyValue $record
$policy.approved = @(@($policy.approved) + $label | Select-Object -Unique)
$policy.recommended = $label

$json = $policy | ConvertTo-Json -Depth 10
$lua = "-- Generated release evidence. Do not edit hashes by hand.`nreturn util.JSONToTable([==[`n$json`n]==])`n"
[IO.File]::WriteAllText($policyPath, $lua, [Text.UTF8Encoding]::new($false))
Write-Output "native_policy.lua now approves and recommends $label (build $($record.build)) with $ReleaseUrl$(if ($AltReleaseUrl) { " and $AltReleaseUrl" })"
Write-Output 'Review the change (git diff addon/lua/mmdhl/native_policy.lua), then package and publish the Workshop addon.'
