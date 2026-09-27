param([string]$GameRoot='H:\SteamLibrary\steamapps\common\GarrysMod',[switch]$Restore)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$record=Join-Path $root 'validation\candidate-install.json'
$game=[IO.Path]::GetFullPath($GameRoot).TrimEnd('\')
if(Get-CimInstance Win32_Process -Filter "Name='gmod.exe'" | Where-Object {$_.ExecutablePath -and $_.ExecutablePath.StartsWith($game,[StringComparison]::OrdinalIgnoreCase)}){throw 'Close this installation before staging/restoring DLLs'}
if($Restore){
 $manifest=Get-Content -LiteralPath $record -Raw | ConvertFrom-Json
 foreach($entry in $manifest.files){
  $target=[IO.Path]::GetFullPath($entry.target)
  if(-not $target.StartsWith($game+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Backup target escaped the selected game installation'}
  if($entry.backup){Copy-Item -LiteralPath $entry.backup -Destination $target -Force}
  elseif(Test-Path -LiteralPath $target){Remove-Item -LiteralPath $target -Force}
 }
 Write-Output 'Restored the preserved installed release.'
 exit
}
if(Test-Path -LiteralPath $record){Write-Output 'An installed-release backup already exists; preserving it.';exit}
$folder=Join-Path $root ('validation\installed-baseline-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss'))
$targets=@()
foreach($name in @('gmcl_mmdhl_win64.dll','gmsv_mmdhl_win64.dll','mmdhl_runtime_win64.dll','mmdhl_worker.exe','lib_coacd.dll')){$targets+=Join-Path $game "garrysmod\lua\bin\$name"}
$targets+=Join-Path $game 'bin\win64\mmdhl_runtime_win64.dll'
$addon=Join-Path $game 'garrysmod\addons\mmd_hotloader'
foreach($source in Get-ChildItem -LiteralPath (Join-Path $root 'addon') -File -Recurse){$targets+=Join-Path $addon ([IO.Path]::GetRelativePath((Join-Path $root 'addon'),$source.FullName))}
$files=@()
foreach($target in $targets){
 $backup=$null
 if(Test-Path -LiteralPath $target){$relative=[IO.Path]::GetRelativePath($game,$target);$backup=Join-Path $folder $relative;New-Item -ItemType Directory -Force (Split-Path $backup) | Out-Null;Copy-Item -LiteralPath $target -Destination $backup}
 $files+=@{target=$target;backup=$backup}
}
@{game=$game;files=$files}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath $record -Encoding utf8
Write-Output "Preserved the installed release in $folder"
