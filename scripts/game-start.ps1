param([string]$GameRoot='H:\SteamLibrary\steamapps\common\GarrysMod',[string]$Map='gm_flatgrass',[switch]$Visible,[switch]$SkipInstall,[switch]$WithAddons,[switch]$Isolated,[switch]$NoWorkshop,[switch]$MenuFirst,[int]$MenuSettleSeconds=8,[switch]$RTX,[switch]$PrepareOnly,[int]$Width=3840,[int]$Height=2160,[int]$TickRate=128,[switch]$AllowOtherInstallation,[Nullable[int]]$QueueMode=$null)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$running=Get-CimInstance Win32_Process -Filter "Name='gmod.exe'" | Where-Object { -not $_.CommandLine -or $_.CommandLine -notmatch '--type=' }
$sameInstall=$running | Where-Object {-not $_.ExecutablePath -or $_.ExecutablePath.StartsWith([IO.Path]::GetFullPath($GameRoot),[StringComparison]::OrdinalIgnoreCase)}
if($sameInstall -or ($running -and -not $AllowOtherInstallation)){throw 'Another GMod process is running; use a separate installation or wait for it to close'}
$multi=if($running){'-multirun -port 27017 -clientport 27018'}else{''}
$bootstrap=Join-Path $GameRoot 'garrysmod\lua\autorun\000_mmdhl_test_session.lua'
if(Test-Path -LiteralPath $bootstrap){throw 'An earlier MMDHL test bootstrap exists; stop its session first'}
if(-not $SkipInstall){& (Join-Path $PSScriptRoot 'install.ps1') -GameRoot $GameRoot -InstallAddon}
$token=[guid]::NewGuid().ToString('N')
$cache=Join-Path $GameRoot 'garrysmod\data\mmd_hotloader'
New-Item -ItemType Directory -Force -Path $cache,(Join-Path $root 'validation') | Out-Null
$spec=@{token=$token;started=[DateTime]::UtcNow.ToString('o');map=$Map;pid=0;gameRoot=$GameRoot;cache=$cache;requestedWidth=$Width;requestedHeight=$Height;isolated=[bool]$Isolated}
$spec.requestedQueueMode=$QueueMode
$spec.tickRate=$TickRate
$spec.workshopDisabled=[bool]($NoWorkshop -or $Isolated)
$spec.rtx=[bool]$RTX
$spec.concurrentOtherProcesses=@($running | Select-Object ProcessId,ExecutablePath)
$spec.staged=@()
$spec.settings=@()
# This process owns a disposable test session. Restore the user's archived
# graphics/input configuration even if a watchdog must terminate the process.
foreach($relative in @('garrysmod\cfg\config.cfg','garrysmod\cfg\video.txt','garrysmod\cfg\video.tx','garrysmod\cfg\autoexec.cfg','garrysmod\cfg\client.vdf','garrysmod\cfg\server.vdf')){
    $target=Join-Path $GameRoot $relative
    if(Test-Path -LiteralPath $target){
        $backup=Join-Path $root "validation\sessions\$token\settings\$([IO.Path]::GetFileName($relative))"
        New-Item -ItemType Directory -Force (Split-Path $backup) | Out-Null
        Copy-Item -LiteralPath $target -Destination $backup
        $spec.settings+=@{target=$target;backup=$backup}
    }
}
# -noaddons isolates tests from unrelated addon hooks. Stage only our own Lua
# and materials in the base game, backing up any existing files for restoration.
if($Isolated){
    foreach($subtree in @('lua','materials')){
        $source=Join-Path $root "addon\$subtree"
        foreach($file in Get-ChildItem -LiteralPath $source -File -Recurse){
            $relative=[IO.Path]::GetRelativePath((Join-Path $root 'addon'),$file.FullName)
            $target=Join-Path $GameRoot "garrysmod\$relative"
            $backup=$null
            if(Test-Path -LiteralPath $target){$backup=Join-Path $root "validation\sessions\$token\backup\$relative";New-Item -ItemType Directory -Force (Split-Path $backup) | Out-Null;Copy-Item -LiteralPath $target -Destination $backup}
            New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
            Copy-Item -LiteralPath $file.FullName -Destination $target -Force
            $spec.staged+=@{target=$target;backup=$backup;sha256=(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash}
        }
    }
}
[IO.File]::WriteAllText((Join-Path $cache 'debug-session.json'),($spec|ConvertTo-Json))
[IO.File]::WriteAllText($bootstrap,"if SERVER then AddCSLuaFile() end MMDHL_DEBUG_TOKEN='$token'")
[IO.File]::WriteAllText((Join-Path $root 'validation\session.json'),($spec|ConvertTo-Json -Depth 5))
if($PrepareOnly){Write-Output "Prepared MMDHL test session $token";return}
$style=if($Visible){'Normal'}else{'Hidden'}
$renderOptions=if($null -ne $QueueMode){"+mat_queue_mode $QueueMode"}else{''}
if($RTX){$renderOptions+=' -dxlevel 90 -nod3d9ex +mat_disable_d3d9ex 1'}
$isolation=if($Isolated){'-noaddons -noworkshop'}elseif($NoWorkshop){'-noworkshop'}else{''}
# With Workshop content, a map loaded from the command line races the Workshop
# fetch and can crash the game. -MenuFirst starts in the main menu, waits until
# the console log has been quiet for a few seconds (mounting finished), then
# hands the map command to the running game with Source's -hijack.
$exe=Join-Path $GameRoot 'bin\win64\gmod.exe'
$mapArgument=if($MenuFirst){''}else{"+map $Map"}
$log=Join-Path $GameRoot 'garrysmod\console.log'
$logStart=if(Test-Path -LiteralPath $log){(Get-Item -LiteralPath $log).Length}else{0}
$proc=Start-Process -FilePath $exe -WorkingDirectory $GameRoot -ArgumentList "-game garrysmod $multi -tickrate $TickRate -windowed -w $Width -h $Height -novid -nosound -console -condebug -insecure $isolation $renderOptions -mmdhl_session $token +sv_lan 1 +maxplayers 1 +sv_cheats 1 +sv_allowcslua 1 $mapArgument" -WindowStyle $style -PassThru
if($MenuFirst){
    $deadline=(Get-Date).AddMinutes(10);$lastSize=-1;$quietSince=Get-Date;$sawMount=$false
    while((Get-Date) -lt $deadline){
        Start-Sleep -Milliseconds 1000
        if($proc.HasExited){throw "GMod exited in the main menu (exit code $($proc.ExitCode))"}
        $size=if(Test-Path -LiteralPath $log){(Get-Item -LiteralPath $log).Length}else{0}
        if(-not $sawMount -and $size -gt $logStart){
            $stream=[IO.File]::Open($log,'Open','Read','ReadWrite');try{$stream.Seek($logStart,'Begin')|Out-Null;$reader=New-Object IO.StreamReader($stream);$sawMount=$reader.ReadToEnd() -match 'workshop addons'}finally{$stream.Dispose()}
        }
        if($size -ne $lastSize){$lastSize=$size;$quietSince=Get-Date}
        if($sawMount -and ((Get-Date)-$quietSince).TotalSeconds -ge $MenuSettleSeconds){break}
    }
    if(-not $sawMount){throw 'Workshop mounting did not finish within 10 minutes'}
    Start-Process -FilePath $exe -WorkingDirectory $GameRoot -ArgumentList "-hijack +map $Map" | Out-Null
}
$spec.pid=$proc.Id
$spec.gameRoot=$GameRoot
$spec.cache=$cache
[IO.File]::WriteAllText((Join-Path $root 'validation\session.json'),($spec|ConvertTo-Json -Depth 5))
Write-Output "Started MMDHL test session $token (PID $($proc.Id)). Use scripts/gamectl.py."
