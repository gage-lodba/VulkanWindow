#!/usr/bin/env sh
# Verify the example executables exist in a completed build (Windows runner).
#
# The bash sibling (verify-examples-built.sh) covers the unix jobs. This one
# exists because the windows job runs PowerShell, and because the layout
# differs: windows-latest uses the Visual Studio generator, which is
# multi-config, so CMake appends "Release" to each target's own binary
# directory. The examples therefore land in
# build/examples/<name>/Release/<name>.exe — not build/Release/, which is where
# the *root* target goes (hence the existing artifact path
# build/Release/VulkanWindow.exe).
#
# Usage: verify-examples-built-windows.sh <build-dir>
#
# Plain POSIX sh, invoked with `sh` so it needs no execute bit — which git does
# not preserve reliably through a Windows checkout.

set -eu

BUILD_DIR="${1:?usage: verify-examples-built-windows.sh <build-dir>}"

check() {
  dir="$1"
  name="$2"
  bin="${BUILD_DIR}/examples/${dir}/Release/${name}.exe"
  if [ -f "$bin" ]; then
    echo "found: $bin"
  else
    echo "::error::expected example binary missing: $bin (glslc not found at configure?)"
    return 1
  fi
}

status=0
check triangle VulkanWindowTriangle || status=1
check textured_quad VulkanWindowTexturedQuad || status=1

exit "$status"
