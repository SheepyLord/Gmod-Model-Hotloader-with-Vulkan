param([Parameter(Mandatory=$true)][string]$AddonRoot)
$ErrorActionPreference='Stop'
$target=(Resolve-Path -LiteralPath $AddonRoot).Path
$patch=Join-Path (Split-Path $PSScriptRoot -Parent) 'integrations\advanced-material-editor.patch'
if(-not (Test-Path -LiteralPath (Join-Path $target 'lua\advmat\cl_70_ui.lua'))){throw 'Expected the Advanced Material Editor addon root'}
Get-Command git -ErrorAction Stop | Out-Null
# git apply is atomic unless explicitly asked for --reject, which is never used.
# Windows PowerShell emits native stderr as errors even when it is captured.
$ErrorActionPreference='Continue'
$check=& git -C $target apply --check --ignore-space-change $patch 2>&1
$checkCode=$LASTEXITCODE
$ErrorActionPreference='Stop'
if($checkCode -ne 0){
    $ErrorActionPreference='Continue'
    $reverse=& git -C $target apply --reverse --check --ignore-space-change $patch 2>&1
    $reverseCode=$LASTEXITCODE
    $ErrorActionPreference='Stop'
    if($reverseCode -eq 0){Write-Output 'The MMD material provider is already installed.'; exit 0}
    throw "The MMD compatibility patch conflicts with this AME version. No files were changed.`n$($check -join [Environment]::NewLine)"
}
& git -C $target apply --ignore-space-change $patch
if($LASTEXITCODE -ne 0){throw 'Git could not apply the checked MMD compatibility patch'}
Write-Output 'Installed the MMD material provider. Restart Garrys Mod to load it.'
