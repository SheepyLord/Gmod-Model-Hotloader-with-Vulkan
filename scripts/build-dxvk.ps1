param([switch]$Clean)
# Builds DXVK's d3d9.dll with the shared compute queue patch
# (patches/dxvk/*.patch) using MSVC, Meson and Ninja. Output:
# build-dxvk/src/d3d9/d3d9.dll. The shaders use the glslang built by the
# main CMake build (build.ps1 first).
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$dxvk=Join-Path $root 'vendor\dxvk'
$tag='v3.1.1';$commit='b1a1c99'
if(-not (Test-Path -LiteralPath (Join-Path $dxvk 'meson.build'))){
    git clone --depth 1 --branch $tag --recurse-submodules --shallow-submodules https://github.com/doitsujin/dxvk.git $dxvk
    if($LASTEXITCODE){throw 'DXVK clone failed'}
}
$head=(git -C $dxvk rev-parse --short=7 HEAD).Trim()
if($head -ne $commit){throw "vendor/dxvk is at $head, expected $commit ($tag)"}
# A reused tree (a local checkout, or vendor restored from the Actions cache) may
# carry an older version of these patches: unless every current patch is applied,
# start again from the pinned tag.
$patches=@(Get-ChildItem -LiteralPath (Join-Path $root 'patches\dxvk') -Filter *.patch | Sort-Object Name)
# git reports an unapplied patch on stderr, which Windows PowerShell 5.1 would raise under 'Stop'.
function Test-Applied($patch){$ErrorActionPreference='Continue';git -C $dxvk apply --check --reverse $patch.FullName 2>$null;$LASTEXITCODE -eq 0}
$applied=$true
foreach($patch in $patches){if(-not (Test-Applied $patch)){$applied=$false;break}}
if(-not $applied){
    git -C $dxvk reset --hard --quiet
    if($LASTEXITCODE){throw 'Resetting vendor/dxvk failed'}
    git -C $dxvk clean -fdq
    if($LASTEXITCODE){throw 'Cleaning vendor/dxvk failed'}
    foreach($patch in $patches){
        git -C $dxvk apply $patch.FullName
        if($LASTEXITCODE){throw "Applying $($patch.Name) failed"}
    }
}
$glslang=Join-Path $root 'build\vendor\glslang\StandAlone\Release'
if(-not (Test-Path -LiteralPath (Join-Path $glslang 'glslangValidator.exe'))){throw 'glslangValidator.exe missing: run scripts/build.ps1 first'}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
# Import the x64 developer environment into this process.
cmd /s /c "`"$vs\Common7\Tools\VsDevCmd.bat`" -arch=x64 -host_arch=x64 -no_logo && set" | ForEach-Object {
    $pair=$_ -split '=',2;if($pair.Count -eq 2){[Environment]::SetEnvironmentVariable($pair[0],$pair[1])}
}
$env:PATH="$glslang;$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;$env:PATH"
$out=Join-Path $root 'build-dxvk'
if($Clean -and (Test-Path -LiteralPath $out)){Remove-Item -LiteralPath $out -Recurse -Force}
if(-not (Test-Path -LiteralPath (Join-Path $out 'build.ninja'))){
    python -m mesonbuild.mesonmain setup $out $dxvk --backend ninja --buildtype release -Denable_d3d8=false -Denable_d3d10=false -Denable_d3d11=false -Denable_dxgi=false
    if($LASTEXITCODE){throw 'meson setup failed'}
}
ninja -C $out
if($LASTEXITCODE){throw 'DXVK build failed'}
Get-Item (Join-Path $out 'src\d3d9\d3d9.dll') | Select-Object FullName,Length,LastWriteTime
