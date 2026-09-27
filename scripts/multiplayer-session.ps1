param([ValidateSet('server','client1','client2')][string]$Peer,[switch]$Stop,[switch]$Visible,[string]$ConnectAddress,[switch]$TraceNetwork)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$base=[IO.Path]::GetFullPath((Join-Path $root 'validation\multiplayer'))
$game=Join-Path $base $(if($Peer -eq 'server'){$Peer}else{$Peer+'-isolated'})
$record=Join-Path $base ($Peer+'-session.json')
if($Stop){
 if(-not (Test-Path -LiteralPath $record)){return}
 $session=Get-Content -LiteralPath $record -Raw|ConvertFrom-Json
 $process=Get-CimInstance Win32_Process -Filter "ProcessId=$($session.pid)"
 if($process -and $process.CommandLine -and $process.CommandLine.Contains($session.token)){
  if($Peer -ne 'server'){
   # Disconnect before terminating so the server does not retain a ghost player.
   & python (Join-Path $PSScriptRoot 'gamectl.py') client "timer.Simple(.1,function() RunConsoleCommand('disconnect') end);return true" --session $record --timeout 3 *> $null
   Start-Sleep -Milliseconds 300
  }
  Stop-Process -Id $session.pid;Wait-Process -Id $session.pid -Timeout 10 -ErrorAction SilentlyContinue
  # CIM can briefly retain an exited process; let the install lock see its exit.
  $deadline=[DateTime]::UtcNow.AddSeconds(10)
  do{$process=Get-CimInstance Win32_Process -Filter "ProcessId=$($session.pid)";if(-not $process -or -not $process.CommandLine -or -not $process.CommandLine.Contains($session.token)){break};Start-Sleep -Milliseconds 100}while([DateTime]::UtcNow -lt $deadline)
 }
 $spec=Join-Path $session.cache 'debug-session.json'
 if(Test-Path -LiteralPath $spec){$saved=Get-Content -LiteralPath $spec -Raw|ConvertFrom-Json;if($saved.token -eq $session.token){Remove-Item -LiteralPath $spec}}
 $bootstrap=Join-Path $session.gameRoot 'garrysmod\lua\autorun\000_mmdhl_test_session.lua'
 if((Test-Path -LiteralPath $bootstrap) -and ([IO.File]::ReadAllText($bootstrap).Contains('MMDHL_DEBUG_TOKEN=session.token'))){Remove-Item -LiteralPath $bootstrap}
 return
}
if(-not $Peer){throw 'Choose a peer'}
if(-not (Test-Path -LiteralPath $game)){throw 'Prepare the dedicated server and client roots first'}
& (Join-Path $PSScriptRoot 'install.ps1') -GameRoot $game -InstallAddon
$token=[guid]::NewGuid().ToString('N')
$cache=Join-Path $game 'garrysmod\data\mmd_hotloader'
New-Item -ItemType Directory -Force $cache | Out-Null
$session=@{token=$token;gameRoot=$game;cache=$cache;ownedMultiplayer=$true;peer=$Peer;pid=0}
$bootstrap=Join-Path $game 'garrysmod\lua\autorun\000_mmdhl_test_session.lua'
# Every owned process has an independent DATA directory and token. The script
# sent by the server reads only the receiving process's local specification.
$code=@'
if SERVER then AddCSLuaFile() end
local session=util.JSONToTable(file.Read('mmd_hotloader/debug-session.json','DATA') or '')
if session and session.ownedMultiplayer then
 MMDHL_DEBUG_TOKEN=session.token
 if SERVER then hook.Add('PlayerInitialSpawn','MMDHL.OwnedAdmin',function(p) if #player.GetHumans()==1 then p:SetUserGroup('superadmin') end end) end
end
'@
[IO.File]::WriteAllText($bootstrap,$code)
[IO.File]::WriteAllText((Join-Path $cache 'debug-session.json'),($session|ConvertTo-Json))
$addons=Join-Path $game 'garrysmod\addons'
foreach($addon in Get-ChildItem -LiteralPath 'H:\SteamLibrary\steamapps\common\GarrysMod\garrysmod\addons'){
 if($addon.Name -eq 'mmd_hotloader'){continue}
 $target=Join-Path $addons $addon.Name
 if(-not(Test-Path -LiteralPath $target)){
  if($addon.PSIsContainer){New-Item -ItemType Junction -Path $target -Target $addon.FullName|Out-Null}
  else{New-Item -ItemType HardLink -Path $target -Target $addon.FullName|Out-Null}
 }
}
if($Peer -eq 'server'){
 $exe=Join-Path $game 'srcds_win64.exe'
 $args="-console -game garrysmod -insecure -condebug -mmdhl_session $token -ip 0.0.0.0 -port 27035 +sv_lan 1 +hide_server 1 +sv_password mmdhl_$token +maxplayers 4 +sv_hibernate_think 1 +sv_allowcslua 0 +map gm_construct"
}else{
 $exe=Join-Path $game 'bin\win64\gmod.exe'
 # Steam Remote Play owns 27036 even though Source can bind 0.0.0.0 there.
 # Reserve distinct unused ports and connect over the real local interface:
 # Source can treat 127.0.0.1 as an in-process listen-server loopback.
 $used=@((Get-NetUDPEndpoint).LocalPort)
 $port=27800
 while($used -contains $port -or $used -contains ($port+1)){$port+=2}
 if(-not $ConnectAddress){
  $nic=Get-NetIPConfiguration | Where-Object {$_.IPv4DefaultGateway -and $_.IPv4Address} | Select-Object -First 1
  $ConnectAddress=$nic.IPv4Address[0].IPAddress
  if(-not $ConnectAddress){throw 'No local IPv4 interface; supply -ConnectAddress explicitly'}
 }
 $session.clientPort=$port; $session.connectAddress=$ConnectAddress
 $server=Get-Content -LiteralPath (Join-Path $base 'server-session.json') -Raw|ConvertFrom-Json
 # Wait for the owned server to enter its Think loop before issuing a challenge.
 # Connecting while its map/addons are loading can produce Bad challenge.
 & python (Join-Path $PSScriptRoot 'gamectl.py') server 'return game.GetMap()' --session (Join-Path $base 'server-session.json') --timeout 30 | Out-Null
 if($LASTEXITCODE -ne 0){throw 'Owned server is not ready for a client connection'}
 $trace=if($TraceNetwork){'+developer 1 +net_showudp 1'}else{''}
 $args="-game garrysmod -multirun -insecure -novid -nosound -windowed -w 2560 -h 1440 -condebug -mmdhl_session $token +clientport $port +hostport $($port+1) +fps_max 60 +cl_playermodel kleiner +password mmdhl_$($server.token) $trace +connect ${ConnectAddress}:27035"
}
$style=if($Visible){'Normal'}else{'Hidden'}
$process=Start-Process -FilePath $exe -WorkingDirectory $game -ArgumentList $args -WindowStyle $style -PassThru
$session.pid=$process.Id
[IO.File]::WriteAllText($record,($session|ConvertTo-Json))
Write-Output "Started owned $Peer PID $($process.Id)"
