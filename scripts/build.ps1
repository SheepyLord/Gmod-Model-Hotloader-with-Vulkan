param([ValidateSet('Debug','Release')][string]$Configuration='Release',[switch]$SkipBootstrap)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    if(-not $SkipBootstrap){ python scripts/bootstrap.py; if($LASTEXITCODE){throw 'Bootstrap failed'} }
    python scripts/patch-bullet-threading.py
    if($LASTEXITCODE){throw 'Bullet threading patch failed'}
    $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $cmake=Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    & $cmake -S . -B build -G 'Visual Studio 17 2022' -A x64
    if($LASTEXITCODE){throw 'Configure failed'}
    & $cmake --build build --config $Configuration --parallel 12
    if($LASTEXITCODE){throw 'Build failed'}
    python scripts/shaders.py
    if($LASTEXITCODE){throw 'Shader build failed'}
    python scripts/fixtures.py
    if($LASTEXITCODE){throw 'Fixture generation failed'}
    & "build/bin/$Configuration/mmdhl_tests.exe" tests/fixtures/cloth21.pmx tests/fixtures/rope21.pmx
    if($LASTEXITCODE){throw 'Native validation failed'}
    & "build/bin/$Configuration/mmdhl_timing.exe" tests/fixtures/native-cloth21.pmx dbvt
    if($LASTEXITCODE){throw 'Presentation timing validation failed'}
    & "build/bin/$Configuration/mmdhl_timing.exe" tests/fixtures/native-cloth21.pmx dbvt-fast
    if($LASTEXITCODE){throw 'Optimized DBVT presentation timing validation failed'}
    & "build/bin/$Configuration/mmdhl_compute_tests.exe" cpu_mt
    if($LASTEXITCODE){throw 'Multicore physics validation failed'}
    & "build/bin/$Configuration/mmdhl_broadphase_tests.exe"
    if($LASTEXITCODE){throw 'Broadphase isolation and fallback validation failed'}
    # The dense-rig gate check needs an imported production model; MMDHL_ASSET_CACHE overrides the default game cache.
    $assetCache=if($env:MMDHL_ASSET_CACHE){$env:MMDHL_ASSET_CACHE}else{'H:/SteamLibrary/steamapps/common/GarrysMod/garrysmod/data/mmd_hotloader'}
    $denseAsset='bac307fdcefad2b6331a95e3b77644ad1562a1aba9983c0830e260326f2ee4c6'
    if(Test-Path -LiteralPath (Join-Path $assetCache "assets/$denseAsset/manifest.json")){& "build/bin/$Configuration/mmdhl_midphase_tests.exe" tests/fixtures/native-chain.pmx $assetCache $denseAsset}
    else{& "build/bin/$Configuration/mmdhl_midphase_tests.exe" tests/fixtures/native-chain.pmx}
    if($LASTEXITCODE){throw 'Mid-phase gate and broadphase selection validation failed'}
    & "build/bin/$Configuration/mmdhl_async_tests.exe" tests/fixtures/native-chain.pmx
    if($LASTEXITCODE){throw 'Asynchronous scheduling validation failed'}
    & "build/bin/$Configuration/mmdhl_props_tests.exe"
    if($LASTEXITCODE){throw 'Static prop cache, collision and material validation failed'}
} finally { Pop-Location }
