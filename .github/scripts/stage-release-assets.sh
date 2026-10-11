#!/usr/bin/env bash
# Prepare downloaded artifacts for the release job.
#
# Kept as a script so the asset-name collision rule and the execute-bit
# restoration are testable outside CI. See .github/scripts/README.md.
#
# Usage: stage-release-assets.sh [artifacts-dir]
#
# Expects the layout actions/download-artifact produces:
#   <dir>/ubuntu-latest/VulkanWindow
#   <dir>/windows-latest/VulkanWindow.exe
#   <dir>/macos-latest/VulkanWindow
#
# Two things have to happen before Publish release:
#
# 1. Rename the macOS binary. action-gh-release names each asset after its
#    file's basename, so listing macos-latest/VulkanWindow beside
#    ubuntu-latest/VulkanWindow would publish two assets both called
#    "VulkanWindow". The action globs the list and uploads in parallel, deleting
#    any existing asset of the same name first — so one platform's binary would
#    silently replace the other. The renamed asset becomes
#    "VulkanWindow-macos", which cannot collide.
#
# 2. Restore the execute bit. actions/upload-artifact normalises permissions, so
#    the Linux and macOS artifacts come back as 0644 — a published asset that
#    cannot be run without a manual chmod.

set -euo pipefail

DIR="${1:-.}"

LINUX="${DIR}/ubuntu-latest/VulkanWindow"
WINDOWS="${DIR}/windows-latest/VulkanWindow.exe"
MACOS_SRC="${DIR}/macos-latest/VulkanWindow"
MACOS_DST="${DIR}/macos-latest/VulkanWindow-macos"

fail() {
  echo "::error::$1"
  exit 1
}

[ -f "$MACOS_SRC" ] || fail "macos-latest/VulkanWindow missing — did the macOS job upload its artifact?"
[ -f "$LINUX" ] || fail "ubuntu-latest/VulkanWindow missing — did the linux job upload its artifact?"
[ -f "$WINDOWS" ] || fail "windows-latest/VulkanWindow.exe missing — did the windows job upload its artifact?"

if [ -f "$MACOS_DST" ]; then
  fail "macos-latest/VulkanWindow-macos already exists — refusing to overwrite"
fi
mv "$MACOS_SRC" "$MACOS_DST"
echo "renamed: $MACOS_SRC -> $MACOS_DST"

for bin in "$LINUX" "$MACOS_DST"; do
  chmod +x "$bin"
  if [ ! -x "$bin" ]; then
    fail "failed to make $bin executable"
  fi
  echo "executable: $bin"
done

# Confirms the names Publish release must list, and that no two collide.
printf 'asset names: %s\n' \
  "$(basename "$LINUX") $(basename "$WINDOWS") $(basename "$MACOS_DST")"
