#!/bin/bash
# Builds, tests and packages the Linux native files of one platform:
#   linux64  Garry's Mod's x86-64 branch (64-bit)
#   linux    the default branch, which is 32-bit on Linux (built with -m32)
# It runs in the manylinux_2_28 container (AlmaLinux 8: glibc 2.28, GCC 14), so the files
# load in every Steam Linux Runtime the game runs in (soldier's glibc is 2.28; a newer
# build glibc would not load there). From the repository root:
#   docker run --rm -v "$PWD:/src" -w /src quay.io/pypa/manylinux_2_28_x86_64:2026.10.09-1 \
#     scripts/build-linux.sh linux64 [package-name]
# The package lands in dist/<package-name> (default Model-Hotloader-<release>-<platform>).
set -euo pipefail
platform=${1:?usage: build-linux.sh linux64|linux [package-name]}
name=${2:-}
case "$platform" in linux64|linux) ;; *) echo "unknown platform: $platform" >&2; exit 2;; esac
python=/opt/python/cp312-cp312/bin/python3
# The workspace belongs to another user than the container's root.
git config --global --add safe.directory "$PWD"
"$python" -m pip install --quiet --disable-pip-version-check ninja==1.13.0
flags=()
if [ "$platform" = linux ]; then
  # The 32-bit C library and GCC 14's 32-bit C++ library for -m32.
  dnf install -y -q glibc-devel.i686 libgcc.i686 gcc-toolset-14-libstdc++-devel.i686
  flags=(-DCMAKE_C_FLAGS=-m32 -DCMAKE_CXX_FLAGS=-m32)
fi
"$python" scripts/bootstrap.py
"$python" scripts/build-icu.py --platform "$platform"
build=build-$platform
# The policy record goes to the build folder (packaging reads bin/native-release.json).
cmake -S . -B "$build" -G Ninja -DCMAKE_MAKE_PROGRAM="$(dirname "$python")/ninja" -DCMAKE_BUILD_TYPE=Release \
  -DPython3_EXECUTABLE="$python" -DMMDHL_POLICY_OUTPUT="$PWD/$build/native_policy.lua" "${flags[@]}"
cmake --build "$build" -j"$(nproc)"
ctest --test-dir "$build" --output-on-failure -j"$(nproc)"
"$python" scripts/package-dropin.py --bin "$build/bin" ${name:+--name "$name"}
