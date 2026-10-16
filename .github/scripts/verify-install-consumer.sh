#!/usr/bin/env bash
# Verify the installed VulkanWindow package end to end.
#
# Kept as a script rather than inline YAML so the guard is runnable and
# testable outside CI. See .github/scripts/README.md.
#
# Usage: verify-install-consumer.sh <prefix> <work-dir>
#
# The install path is the one configuration no other job exercises: they all
# build the vendored submodules and never pass -DVULKANWINDOW_INSTALL=ON, so
# without this the exported package layout would ship unverified. The script
# asserts, in order:
#
#   1. the published header set is EXACTLY the contract list below — nothing
#      missing, nothing extra, and no implementation header that leaked into
#      the install rule (which is how a `PATTERN "*.h"` glob regression shows
#      up),
#   2. the exported package files and one library per exported target are in
#      the prefix,
#   3. a throwaway consumer project doing find_package(VulkanWindow CONFIG
#      REQUIRED) + VulkanWindow::VulkanWindow configures, builds and runs.
#
# Step 3 is the part that cannot be faked: it fails on an export set that
# references a target the package does not export, on a VulkanWindowConfig.cmake
# that forgot a find_dependency(), and on an installed header that does not
# resolve from the installed tree.
#
# Arguments:
#   <prefix>    install prefix that `cmake --install` wrote
#   <work-dir>  scratch directory for the generated consumer project and its
#               build tree (created if missing; those two subdirectories are
#               removed and regenerated on every run)

set -euo pipefail

PREFIX="${1:?usage: verify-install-consumer.sh <prefix> <work-dir>}"
WORK_DIR="${2:?usage: verify-install-consumer.sh <prefix> <work-dir>}"

if [ ! -d "$PREFIX" ]; then
  echo "::error::install prefix does not exist: $PREFIX"
  exit 1
fi

INCLUDE_DIR="$PREFIX/include/VulkanWindow"

# The public header contract: exactly what CMakeLists.txt's install(FILES ...)
# list publishes. PipelineCacheStore.h and SwapchainRecreateInfo.h are part of
# the public surface because installed headers include them (VulkanContext.h and
# VulkanRenderer.h respectively): dropping either one breaks a consumer that
# includes a header from this list.
#
# Keep this array in step with the install rule. A header added to the package
# must be added here; otherwise the "unexpected header" check below reports it,
# which is the point — the install surface is a contract, not a glob.
EXPECTED_HEADERS=(
  Application.h
  PipelineCacheStore.h
  RendererTypes.h
  Swapchain.h
  SwapchainRecreateInfo.h
  UserInterface.h
  VulkanContext.h
  VulkanDescriptors.h
  VulkanResources.h
  VulkanSelectors.h
  VulkanUtils.h
)

# Implementation headers that must stay private. The install rule used to be
# `install(DIRECTORY source/ ... FILES_MATCHING PATTERN "*.h")`, which published
# these three along with the internals they include. An explicit install list
# replaced it; this is the guard that keeps the glob from coming back.
FORBIDDEN_HEADERS=(
  ImGuiManager.h
  VulkanRenderer.h
  Window.h
)

# One line per top-level *.h in the installed include directory. The trailing
# `exit 0` keeps the subshell successful when the directory is missing or holds
# no headers at all — the pipeline runs under `set -o pipefail`, so a subshell
# that merely failed its last glob test would trip the whole script.
list_installed_headers() {
  (
    cd "$INCLUDE_DIR" 2>/dev/null || exit 0
    for f in *.h; do
      [ -e "$f" ] && printf '%s\n' "$f"
    done
    exit 0
  ) | sort
}

status=0

echo "=== 1. installed header contract ($INCLUDE_DIR) ==="
for header in "${EXPECTED_HEADERS[@]}"; do
  if [ -f "$INCLUDE_DIR/$header" ]; then
    echo "found:     include/VulkanWindow/$header"
  else
    echo "::error::public header not installed: include/VulkanWindow/$header"
    status=1
  fi
done

for header in "${FORBIDDEN_HEADERS[@]}"; do
  if [ -e "$INCLUDE_DIR/$header" ]; then
    echo "::error::implementation header published: include/VulkanWindow/$header (must stay private — is the install rule a *.h glob again?)"
    status=1
  fi
done

# Anything else is a surprise: the install list is explicit, so the installed
# set must be exactly the contract. Matching against a space-delimited copy of
# the list avoids a per-header helper function, which shellcheck flags when it
# is used in a condition (set -e is suspended inside functions called from
# `if`).
expected_list=" ${EXPECTED_HEADERS[*]} "
while IFS= read -r header; do
  [ -n "$header" ] || continue
  case "$expected_list" in
    *" $header "*) ;;
    *)
      echo "::error::unexpected header in include/VulkanWindow: $header (not part of the published contract)"
      status=1
      ;;
  esac
done < <(list_installed_headers)

if [ "$status" -ne 0 ]; then
  echo "::error::installed header contract violated — refusing to test a package that ships the wrong headers"
  exit 1
fi
echo "OK: ${#EXPECTED_HEADERS[@]} public headers present, ${#FORBIDDEN_HEADERS[@]} implementation headers absent, nothing unexpected"

echo "=== 2. exported package files and libraries ==="
# CMAKE_INSTALL_LIBDIR is 'lib' for a scratch prefix on Debian/Ubuntu but 'lib64'
# on other distributions, so locate the package directory instead of hardcoding
# a libdir.
CONFIG_FILE="$(find "$PREFIX" -path '*/cmake/VulkanWindow/VulkanWindowConfig.cmake' -print -quit 2>/dev/null || true)"
if [ -z "$CONFIG_FILE" ]; then
  echo "::error::VulkanWindowConfig.cmake not found under $PREFIX — find_package(VulkanWindow) cannot succeed"
  exit 1
fi
CONFIG_DIR="$(dirname "$CONFIG_FILE")"
echo "package dir: ${CONFIG_DIR#"$PREFIX"/}"

for file in VulkanWindowConfigVersion.cmake VulkanWindowTargets.cmake VulkanWindowShaders.cmake; do
  if [ -f "$CONFIG_DIR/$file" ]; then
    echo "found:     $file"
  else
    echo "::error::missing from the installed package: $file"
    status=1
  fi
done

# One library per exported target. The file name depends on the platform and
# toolchain (libVulkanWindow.a on Linux, VulkanWindow.lib under MSVC), so accept
# any of the usual names rather than hardcoding one.
for stem in VulkanWindow imgui; do
  library=""
  for name in "lib${stem}.a" "lib${stem}.so" "lib${stem}.dylib" \
              "${stem}.lib" "${stem}.dll" "lib${stem}.dll.a"; do
    found="$(find "$PREFIX" -name "$name" -print -quit 2>/dev/null || true)"
    if [ -n "$found" ]; then
      library="$found"
      break
    fi
  done
  if [ -n "$library" ]; then
    echo "found:     ${library#"$PREFIX"/}"
  else
    echo "::error::no installed library for target $stem (looked for lib${stem}.a, ${stem}.lib, …) under $PREFIX"
    status=1
  fi
done

if [ "$status" -ne 0 ]; then
  echo "::error::installed package is incomplete — refusing to test a package that is missing files"
  exit 1
fi

echo "=== 3. find_package() consumer ==="
CONSUMER_SRC="$WORK_DIR/consumer"
CONSUMER_BUILD="$WORK_DIR/consumer-build"
rm -rf "$CONSUMER_SRC" "$CONSUMER_BUILD"
mkdir -p "$CONSUMER_SRC"

# The consumer is generated here rather than checked in so the script is the
# single source of truth for what "consumer" means, and so it cannot drift from
# the assertions above.
cat > "$CONSUMER_SRC/CMakeLists.txt" <<'CONSUMER_CMAKE'
# Throwaway consumer for the installed VulkanWindow package. Every line is
# something a real downstream project would write.
cmake_minimum_required(VERSION 3.18)
project(vulkanwindow_install_consumer LANGUAGES CXX)

# Deliberately no CMAKE_CXX_STANDARD: the exported target has to propagate
# cxx_std_20 on its own (target_compile_features(VulkanWindow PUBLIC ...)).
# main.cpp fails to compile if it does not.
#
# Deliberately no VulkanWindow_DIR hint either: the package must be findable
# through CMAKE_PREFIX_PATH alone, the way a real consumer finds it.
find_package(VulkanWindow CONFIG REQUIRED)

# The package also promises the shader helper (README, "Compiling your own
# shaders"), so a regression that stops installing VulkanWindowShaders.cmake, or
# stops including it from the config file, has to fail here rather than in
# somebody's project.
if(NOT COMMAND vulkanwindow_add_shaders)
  message(FATAL_ERROR "VulkanWindowConfig.cmake did not provide vulkanwindow_add_shaders()")
endif()
if(NOT DEFINED VULKANWINDOW_GLSLC_FOUND)
  message(FATAL_ERROR "VulkanWindowShaders.cmake did not define VULKANWINDOW_GLSLC_FOUND")
endif()

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE VulkanWindow::VulkanWindow)
CONSUMER_CMAKE

cat > "$CONSUMER_SRC/main.cpp" <<'CONSUMER_CPP'
// Throwaway consumer for the installed VulkanWindow package. Built without
// CMAKE_CXX_STANDARD on purpose: this proves the exported target propagates
// cxx_std_20 to its consumers. _MSVC_LANG is checked as well as __cplusplus
// because MSVC reports the latter as 199711L unless /Zc:__cplusplus is set.
#if (defined(_MSVC_LANG) ? _MSVC_LANG : __cplusplus) < 202002L
#error "VulkanWindow::VulkanWindow did not propagate cxx_std_20 to this consumer"
#endif

// Public headers, included the way the README shows consumers doing it: plain
// names, resolved through the exported target's include directories. This is
// exactly what breaks when the install list omits a header that a public header
// includes, or when the include directories stop being exported.
#include "Application.h"
#include "VulkanSelectors.h"

// Bundled Dear ImGui: proves the imgui include directories and the installed
// deps/ImGui layout resolve from the prefix.
#include "imgui.h"

#include <cstdio>
#include <string>

int main() {
  // The link-level half of the test. Naming symbols defined in the installed
  // archives forces object files out of libVulkanWindow.a and libimgui.a, so an
  // export set that lost a link dependency — or an install rule that shipped no
  // archive — fails here instead of passing a compile-only check.
  //
  // Both calls are pure: no Vulkan instance, device or window is created, so
  // the binary runs on a headless runner with no ICD and no display.
  const std::string cache_name = vkutil::sanitizeCacheName("VulkanWindow install check");
  const char *imgui_version = ImGui::GetVersion();
  std::printf("consumer ok: %s (imgui %s)\n", cache_name.c_str(), imgui_version);

  // Compile-time-only use of the main public type, so Application.h must be a
  // complete, self-contained header.
  static_assert(sizeof(Application) > 0, "Application must be a complete type");
  return 0;
}
CONSUMER_CPP

echo "generated consumer project: $CONSUMER_SRC"
cmake -S "$CONSUMER_SRC" -B "$CONSUMER_BUILD" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$PREFIX" \
  -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF

echo "=== building consumer ==="
cmake --build "$CONSUMER_BUILD" --config Release

# Single-config generators put the binary in the build root, multi-config ones
# (MSVC) under a per-config subdirectory, and Windows appends .exe.
CONSUMER_BIN=""
for candidate in "$CONSUMER_BUILD/consumer" \
                 "$CONSUMER_BUILD/consumer.exe" \
                 "$CONSUMER_BUILD/Release/consumer" \
                 "$CONSUMER_BUILD/Release/consumer.exe"; do
  if [ -f "$candidate" ]; then
    CONSUMER_BIN="$candidate"
    break
  fi
done
if [ -z "$CONSUMER_BIN" ]; then
  echo "::error::consumer executable not found under $CONSUMER_BUILD"
  exit 1
fi

echo "=== running $CONSUMER_BIN ==="
"$CONSUMER_BIN"

echo "OK: installed package verified — ${#EXPECTED_HEADERS[@]} public headers, implementation headers private, find_package + link + build + run all succeeded"
