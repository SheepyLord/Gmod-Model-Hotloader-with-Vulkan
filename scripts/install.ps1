param([string]$GameRoot='H:\SteamLibrary\steamapps\common\GarrysMod',[switch]$InstallAddon)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$game=[IO.Path]::GetFullPath($GameRoot).TrimEnd('\')
$package=Join-Path $root 'build\bin\Release'
$releasePath=Join-Path $package 'native-release.json'
if(-not(Test-Path -LiteralPath $releasePath)){throw 'Native release manifest is missing. Build the native manifest target or download a complete release.'}
$release=Get-Content -LiteralPath $releasePath -Raw | ConvertFrom-Json
if(Test-Path -LiteralPath (Join-Path $root 'SHA256.json')){
    $manifest=Get-Content -LiteralPath (Join-Path $root 'SHA256.json') -Raw | ConvertFrom-Json
    foreach($entry in $manifest.PSObject.Properties){
        $file=[IO.Path]::GetFullPath((Join-Path $root $entry.Name))
        if(-not $file.StartsWith(([IO.Path]::GetFullPath($root).TrimEnd('\')+'\'),[StringComparison]::OrdinalIgnoreCase)){throw 'Invalid package manifest path'}
        if((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $entry.Value){throw "Package checksum mismatch: $($entry.Name)"}
    }
}
$names=@('gmcl_mmdhl_win64.dll','gmsv_mmdhl_win64.dll','mmdhl_runtime_win64.dll','mmdhl_worker.exe','lib_coacd.dll')
foreach($name in $names){
    $entry=@($release.files.PSObject.Properties.Value | Where-Object {$_.name -eq $name})
    if($entry.Count -ne 1){throw "Invalid native manifest entry: $name"}
    $source=Join-Path $package $name
    if((Get-Item -LiteralPath $source).Length -ne $entry[0].size -or (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $entry[0].sha256){throw "Native package checksum mismatch: $name"}
}
# The x86-64 branch has bin\win64\gmod.exe; the main branch (since 2026-09-17) has
# gmod_win64.exe in the game folder. Both load the runtime from bin\win64.
$client=(Test-Path -LiteralPath (Join-Path $game 'bin\win64\gmod.exe')) -or (Test-Path -LiteralPath (Join-Path $game 'gmod_win64.exe'))
$server=Test-Path -LiteralPath (Join-Path $game 'srcds_win64.exe')
if(-not $client -and -not $server){throw 'Expected Windows x64 GMod client or dedicated server installation'}
$running=Get-CimInstance Win32_Process | Where-Object {$_.Name -in @('gmod.exe','gmod_win64.exe','srcds_win64.exe','srcds_console_win64.exe','mmdhl_worker.exe') -and $_.ExecutablePath -and $_.ExecutablePath.StartsWith(($game+'\'),[StringComparison]::OrdinalIgnoreCase)}
if($running){throw 'Close this GMod installation and its workers before installing native binaries'}
$addons=Join-Path $game 'garrysmod\addons'
$addon=Join-Path $addons 'mmd_hotloader'
# Folder addons override Workshop Lua. Refuse ambiguous copies before writing.
if(Test-Path -LiteralPath $addons){
    foreach($folder in Get-ChildItem -LiteralPath $addons -Directory){
        if($folder.FullName -ne $addon -and (Test-Path -LiteralPath (Join-Path $folder.FullName 'lua\autorun\mmdhl.lua'))){throw "Conflicting loose importer addon: $($folder.FullName). Move it outside garrysmod/addons, then retry."}
    }
}
if(Test-Path -LiteralPath $addon){
    $item=Get-Item -LiteralPath $addon
    if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Loose addon is a junction or symbolic link; resolve it before installation: $addon"}
    $metadata=Join-Path $addon 'addon.json'
    if(-not(Test-Path -LiteralPath $metadata) -or (Get-Content -LiteralPath $metadata -Raw | ConvertFrom-Json).title -notin @('Model Hotloader','MMD Hot Loader') -or -not(Test-Path -LiteralPath (Join-Path $addon 'lua\autorun\mmdhl.lua'))){throw "Unrecognized loose addon folder; left intact: $addon"}
    $resolved=[IO.Path]::GetFullPath($addon)
    $backup=[IO.Path]::GetFullPath((Join-Path $game ('mmdhl-install-backups\loose-addon-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N'))))
    if(-not $resolved.StartsWith(($game+'\garrysmod\addons\'),[StringComparison]::OrdinalIgnoreCase) -or -not $backup.StartsWith(($game+'\mmdhl-install-backups\'),[StringComparison]::OrdinalIgnoreCase)){throw 'Backup path escaped installation root'}
    New-Item -ItemType Directory -Force -Path (Split-Path $backup) | Out-Null
    Move-Item -LiteralPath $resolved -Destination $backup
    Write-Output "Backed up legacy loose addon to $backup"
}
$bin=Join-Path $game 'garrysmod\lua\bin'
New-Item -ItemType Directory -Force -Path $bin | Out-Null
$copies=@()
foreach($name in $names){$copies+=@{source=(Join-Path $package $name);target=(Join-Path $bin $name)}}
if($client){$copies+=@{source=(Join-Path $package 'mmdhl_runtime_win64.dll');target=(Join-Path $game 'bin\win64\mmdhl_runtime_win64.dll')}}
if($server){$copies+=@{source=(Join-Path $package 'mmdhl_runtime_win64.dll');target=(Join-Path $game 'mmdhl_runtime_win64.dll')}}
foreach($copy in $copies){
    Copy-Item -LiteralPath $copy.source -Destination $copy.target -Force
    if((Get-FileHash -LiteralPath $copy.source -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $copy.target -Algorithm SHA256).Hash){throw "Installed checksum mismatch: $($copy.target)"}
}
Copy-Item -LiteralPath $releasePath -Destination (Join-Path $bin 'mmdhl-native-release.json') -Force
if($InstallAddon){
    New-Item -ItemType Directory -Force -Path $addon | Out-Null
    Copy-Item -Path (Join-Path $root 'addon\*') -Destination $addon -Recurse -Force
    Write-Output 'Installed development Lua addon. Remove this loose copy before using Workshop updates.'
}else{Write-Output 'Native binaries installed. Subscribe to the current Workshop addon for Lua updates.'}
$shaders=Join-Path $game 'garrysmod\shaders\fxc'
New-Item -ItemType Directory -Force -Path $shaders | Out-Null
foreach($shader in Get-ChildItem -Path (Join-Path $root 'addon\shaders\fxc\mmdhl_*.vcs')){
    $target=Join-Path $shaders $shader.Name
    Copy-Item -LiteralPath $shader.FullName -Destination $target -Force
    if((Get-FileHash -LiteralPath $shader.FullName -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash){throw "Installed shader checksum mismatch: $target"}
}
Write-Output "Verified Model Hotloader $($release.release) in $game. Fully restart Garry's Mod."
