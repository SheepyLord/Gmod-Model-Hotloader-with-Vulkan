#!/bin/bash
# Builds, tests and packages the Linux native files for Garry's Mod's x86-64 branch
# (linux64; the default branch is 32-bit on Linux, which Model Hotloader does not
# support), then the renderer of the Vulkan package (scripts/build-dxvk-linux.sh).
# It runs in the manylinux_2_28 container (AlmaLinux 8: glibc 2.28, GCC 14), so the files
# load in every Steam Linux Runtime the game runs in (soldier's glibc is 2.28; a newer
# build glibc would not load there). From the repository root:
#   docker run --rm -v "$PWD:/src" -w /src quay.io/pypa/manylinux_2_28_x86_64:2026.10.09-1 \
#     scripts/build-linux.sh linux64 [package-name]
# The packages land in dist/<package-name>-vulkan (the game renders through DXVK) and
# dist/<package-name>-opengl (the game's own OpenGL renderer); the default name is
# Model-Hotloader-<release>-linux64.
set -euo pipefail
platform=${1:?usage: build-linux.sh linux64 [package-name]}
name=${2:-}
case "$platform" in
  linux64) ;;
  linux) echo "The default branch is 32-bit on Linux, which Model Hotloader does not support; build linux64" >&2; exit 2;;
  *) echo "unknown platform: $platform" >&2; exit 2;;
esac
python=/opt/python/cp312-cp312/bin/python3
# The workspace belongs to another user than the container's root.
git config --global --add safe.directory "$PWD"
"$python" -m pip install --quiet --disable-pip-version-check ninja==1.13.0
"$python" scripts/bootstrap.py
# As scripts/build.ps1: Bullet's documented local patches (heavy PMX chains, worker-local counters).
"$python" scripts/patch-bullet-threading.py
"$python" scripts/build-icu.py --platform "$platform"
build=build-$platform
# The policy record goes to the build folder (packaging reads bin/native-release.json).
cmake -S . -B "$build" -G Ninja -DCMAKE_MAKE_PROGRAM="$(dirname "$python")/ninja" -DCMAKE_BUILD_TYPE=Release \
  -DPython3_EXECUTABLE="$python" -DMMDHL_POLICY_OUTPUT="$PWD/$build/native_policy.lua"
cmake --build "$build" -j"$(nproc)"
ctest --test-dir "$build" --output-on-failure -j"$(nproc)"
# The Vulkan package's renderer; DXVK's shaders use the glslang built above. Its files join
# the release record, so native_policy.lua knows the renderer the package installs.
renderer=build-dxvk-$platform
scripts/build-dxvk-linux.sh "$build/vendor/glslang/StandAlone/glslang"
"$python" scripts/native_manifest.py --bin "$build/bin" --output "$build/native_policy.lua" --renderer "$renderer"
"$python" scripts/package-dropin.py --bin "$build/bin" --renderer "$renderer" ${name:+--name "$name"}
