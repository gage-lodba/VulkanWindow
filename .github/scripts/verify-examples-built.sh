#!/usr/bin/env bash
# Verify the example executables exist in a completed build (unix jobs).
#
# Kept as a script so the guard is runnable and testable outside CI, and so the
# jobs that need it cannot drift apart. See .github/scripts/README.md.
#
# Usage: verify-examples-built.sh <build-dir>
#
# VulkanWindow's glslc guard makes examples/CMakeLists.txt return() early when
# glslc is absent, so a packaging regression silently produces a build with no
# examples. Every job that can build them must assert they are there.
#
# The windows job has its own script (verify-examples-built-windows.sh) because
# its generator is multi-config, which puts the examples in
# build/examples/<name>/Release/<name>.exe instead of build/examples/<name>/<name>.

set -euo pipefail

BUILD_DIR="${1:?usage: verify-examples-built.sh <build-dir>}"

check() {
  dir="$1"
  name="$2"
  bin="${BUILD_DIR}/examples/${dir}/${name}"
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
