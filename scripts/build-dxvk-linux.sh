#!/bin/bash
# Builds the renderer of the Linux Vulkan package:
#   libdxvk_d3d9.so    DXVK's native d3d9, the tag and patches/dxvk of the Windows renderer
#                      (scripts/build-dxvk.ps1) plus patches/dxvk-linux
#   libtogl_client.so  renderer/togl: the game's ToGL interface on DXVK instead of OpenGL
# Run after the main Linux build, in the manylinux_2_28 container (scripts/build-linux.sh
# calls it); that build's glslang compiles DXVK's shaders:
#   scripts/build-dxvk-linux.sh build-linux64/vendor/glslang/StandAlone/glslang
# Output in build-dxvk-linux64: src/d3d9/libdxvk_d3d9.so.0.30101 and togl/libtogl_client.so.
set -euo pipefail
glslang=${1:?usage: build-dxvk-linux.sh <glslang>}
python=/opt/python/cp312-cp312/bin/python3
tag=v3.1.1
commit=b1a1c99
dxvk=vendor/dxvk-native
out=build-dxvk-linux64
# DXVK's SDL2 window code is compiled against SDL2's and X11's headers; at run time it
# uses the game's own SDL2.
dnf install -y -q SDL2-devel libX11-devel
"$python" -m pip install --quiet --disable-pip-version-check meson==1.12.1 ninja==1.13.0
# Another DXVK version (vendor restored from an older Actions cache) is fetched again.
if [ -f "$dxvk/meson.build" ] && [ "$(git -C "$dxvk" rev-parse --short=7 HEAD)" != "$commit" ]; then
  rm -rf "$dxvk" "$out"
fi
if [ ! -f "$dxvk/meson.build" ]; then
  git clone --depth 1 --branch "$tag" --recurse-submodules --shallow-submodules https://github.com/doitsujin/dxvk.git "$dxvk"
fi
head=$(git -C "$dxvk" rev-parse --short=7 HEAD)
if [ "$head" != "$commit" ]; then echo "$dxvk is at $head, expected $commit ($tag)" >&2; exit 1; fi
# A reused tree (a local checkout, or vendor restored from the Actions cache) may carry
# older patches: unless every current patch is applied, start again from the pinned tag.
patches=(patches/dxvk/*.patch patches/dxvk-linux/*.patch)
applied=1
for patch in "${patches[@]}"; do
  git -C "$dxvk" apply --check --reverse "$PWD/$patch" 2>/dev/null || { applied=0; break; }
done
if [ "$applied" = 0 ]; then
  git -C "$dxvk" reset --hard --quiet
  git -C "$dxvk" clean -fdq
  for patch in "${patches[@]}"; do git -C "$dxvk" apply "$PWD/$patch"; done
fi
mkdir -p "$out/tools"
cp "$glslang" "$out/tools/glslang"
export PATH="$PWD/$out/tools:$(dirname "$python"):$PATH"
if [ ! -f "$out/build.ninja" ]; then
  meson setup "$out" "$dxvk" --buildtype release -Denable_d3d8=false -Denable_d3d10=false -Denable_d3d11=false \
    -Denable_dxgi=false -Dnative_sdl2=enabled -Dnative_sdl3=disabled -Dnative_glfw=disabled
fi
ninja -C "$out"
cmake -S renderer/togl -B "$out/togl" -G Ninja -DCMAKE_MAKE_PROGRAM="$(dirname "$python")/ninja" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPython3_EXECUTABLE="$python" -DMMDHL_DXVK_SOURCE="$PWD/$dxvk"
cmake --build "$out/togl"
ctest --test-dir "$out/togl" --output-on-failure
