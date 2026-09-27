param([string]$GameRoot='H:\SteamLibrary\steamapps\common\GarrysMod',[switch]$InstallAddon)
& (Join-Path $PSScriptRoot 'scripts\install.ps1') -GameRoot $GameRoot -InstallAddon:$InstallAddon
