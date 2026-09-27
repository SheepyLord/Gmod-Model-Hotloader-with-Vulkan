$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$session=Get-Content -LiteralPath (Join-Path $root 'validation\session.json') -Raw | ConvertFrom-Json
$gameProcess=Get-CimInstance Win32_Process -Filter "ProcessId=$($session.pid)"
if($gameProcess -and $gameProcess.CommandLine -and $gameProcess.CommandLine.Contains($session.token)){
    $all=Get-CimInstance Win32_Process
    $owned=[Collections.Generic.HashSet[int]]::new()
    [void]$owned.Add([int]$session.pid)
    do {
        $added=$false
        foreach($process in $all){if($owned.Contains([int]$process.ParentProcessId) -and $process.Name -eq 'gmod.exe' -and $owned.Add([int]$process.ProcessId)){$added=$true}}
    } while($added)
    foreach($processId in $owned){Stop-Process -Id $processId -ErrorAction SilentlyContinue}
    foreach($processId in $owned){Wait-Process -Id $processId -Timeout 5 -ErrorAction SilentlyContinue}
    Write-Output "Stopped owned test session $($session.token)"
}
$spec=Join-Path $session.cache 'debug-session.json'
if(Test-Path -LiteralPath $spec){$saved=Get-Content -LiteralPath $spec -Raw|ConvertFrom-Json;if($saved.token -eq $session.token){Remove-Item -LiteralPath $spec}}
$bootstrap=Join-Path $session.gameRoot 'garrysmod\lua\autorun\000_mmdhl_test_session.lua'
if((Test-Path -LiteralPath $bootstrap) -and ([IO.File]::ReadAllText($bootstrap).Contains($session.token))){Remove-Item -LiteralPath $bootstrap}
foreach($entry in $session.staged){
    if((Test-Path -LiteralPath $entry.target) -and (Get-FileHash -LiteralPath $entry.target -Algorithm SHA256).Hash -eq $entry.sha256){
        if($entry.backup){Copy-Item -LiteralPath $entry.backup -Destination $entry.target -Force}else{Remove-Item -LiteralPath $entry.target}
    }else{Write-Warning "Test file changed during the session; left intact: $($entry.target)"}
}
foreach($entry in $session.settings){
    if(Test-Path -LiteralPath $entry.backup){Copy-Item -LiteralPath $entry.backup -Destination $entry.target -Force}
}
