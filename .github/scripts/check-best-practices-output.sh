#!/usr/bin/env bash
# Validate a Vulkan validation log from a best-practices CI smoke run.
#
# Usage:
#   check-best-practices-output.sh <validation-log>
#   check-best-practices-output.sh --self-test
#
# The repository does not get a clean validation log when
# VULKANWINDOW_BEST_PRACTICES is ON. There are exactly two known, expected
# warning classes, both from third-party/benign sources rather than from
# project misuse:
#
#   1. Vendored Dear ImGui dedicated allocations. The bundled ImGui Vulkan
#      backend (deps/ImGui/backends/imgui_impl_vulkan.cpp) asks VMA for small
#      dedicated allocations, and the Khronos best-practices layer reports each
#      one as a `vkBindBufferMemory`/`vkBindImageMemory` "fully consumed by the
#      buffer/image … should be sub-allocated" warning. That is a known upstream
#      limitation of the bundled third-party code, not a defect here.
#   2. VK_SUBOPTIMAL_KHR from vkQueuePresentKHR. The recreation smoke test
#      deliberately resizes the window, so presenting during/after a resize can
#      return VK_SUBOPTIMAL_KHR; the layer reports it as a warning. The app
#      handles it by immediately recreating the swap chain (see
#      VulkanRenderer::drawFrame), which is why a fixed number (a couple) of
#      these appear and then the log is clean again. This is not project misuse.
#
# This script fails unless:
#   * the log contains no `Validation Error` line at all, and
#   * every `Validation layer:` record belongs to one of those two narrowly
#     scoped known classes — including the layer's duplicate-message-limit note
#     line that precedes the final copy of a repeated warning, and any
#     continuation lines of the message.
#
# Everything else (any other warning, any error, any unexpected record) is
# reported with line numbers and exits non-zero, so a new warning cannot hide
# behind the known ones. Keep the allowlist deliberately narrow: if the
# vendored ImGui is ever fixed or the resize coverage changes, drop the
# corresponding pattern and the gate gets stricter.
#
# Kept as a script (not inline YAML) so it is runnable and testable outside CI.
# It is also linted (shellcheck) outside CI. See .github/scripts/README.md.
#
# Exit codes: 0 = clean/allowlisted; 1 = unexpected validation output or
# bad usage. A missing/unreadable log is also an error (a green gate requires
# evidence that the run produced output).

set -euo pipefail

# Known class 1: the image/buffer phrase proves it is the dedicated-allocation
# best-practices warning, and the sub-allocation sentence pins the VUID text.
KNOWN_ALLOC_SUBSTRING='fully consumed by the '
KNOWN_SUBALLOC_SUBSTRING='should be sub-allocated from larger memory blocks'

# Known class 2: VK_SUBOPTIMAL_KHR reported for a present while the smoke test
# resizes the window. Matched on the exact API prefix plus the VK_SUBOPTIMAL_KHR
# explanation; a different result code or a different message stays fatal.
KNOWN_SUBOPTIMAL_PREFIX='vkQueuePresentKHR(): pPresentInfo[0] VK_SUBOPTIMAL_KHR was returned.'
KNOWN_SUBOPTIMAL_SUBSTRING='VK_SUBOPTIMAL_KHR - Presentation will still succeed, subject to the window resize behavior'

# The Khronos layer emits this prefix line instead of a full message once a
# VUID has been reported duplicate_message_limit times; the message body follows
# on the next (non-prefixed) line. It is only tolerable when that body is a
# known class — a bare note tells us a limit was hit but not which VUID.
DUPLICATE_NOTE_REGEX='Warning - This VUID has now been reported [0-9]+ times, which is the duplicate_message_limit value'

fail() {
  echo "::error::$1"
  exit 1
}

# Classify one `Validation layer:` record (all of its lines joined with
# newlines). Echoes one of: alloc | suboptimal | foreign.
classify_record() {
  local joined="$1"
  if [[ "$joined" == *"$KNOWN_ALLOC_SUBSTRING"* ]] &&
    [[ "$joined" == *"$KNOWN_SUBALLOC_SUBSTRING"* ]]; then
    echo "alloc"
    return 0
  fi
  if [[ "$joined" == *"$KNOWN_SUBOPTIMAL_PREFIX"* ]] &&
    [[ "$joined" == *"$KNOWN_SUBOPTIMAL_SUBSTRING"* ]]; then
    echo "suboptimal"
    return 0
  fi
  echo "foreign"
}

# Check one log file. Prints a summary; returns 0 when every validation record
# is allowlisted and there are no validation errors.
check_log() {
  local log="$1"
  if [ ! -f "$log" ]; then
    echo "::error::validation log not found: $log"
    return 1
  fi

  local errors=0
  local total=0
  local allowed_alloc=0
  local allowed_suboptimal=0
  local foreign=0
  local lineno=0
  local prefix_line=0
  local record=""

  # The layer's callback prints "Validation layer: <message>"; multi-line
  # messages continue on following lines without the prefix. Group each record
  # by its "Validation layer:" line, then classify the joined text.
  flush_record() {
    if [ -z "$record" ]; then
      return 0
    fi
    local class
    class="$(classify_record "$record")"
    total=$((total + 1))
    case "$class" in
      alloc)
        allowed_alloc=$((allowed_alloc + 1))
        ;;
      suboptimal)
        allowed_suboptimal=$((allowed_suboptimal + 1))
        ;;
      *)
        foreign=$((foreign + 1))
        echo "::error::unexpected validation message at ${log}:${prefix_line}:"
        printf '%s\n' "$record" | sed -e 's/^/    /'
        if [[ "$record" =~ $DUPLICATE_NOTE_REGEX ]] &&
          [[ "$record" != *"$KNOWN_ALLOC_SUBSTRING"* ]]; then
          echo "::error::${log}:${prefix_line}: duplicate-message-limit note with no known message body"
        fi
        ;;
    esac
    record=""
    prefix_line=0
  }

  while IFS= read -r line; do
    lineno=$((lineno + 1))
    case "$line" in
      *'Validation Error'*)
        errors=$((errors + 1))
        echo "::error::validation error at ${log}:${lineno}: ${line}"
        ;;
    esac
    case "$line" in
      'Validation layer:'*)
        flush_record
        record="${line#Validation layer:}"
        prefix_line=$lineno
        ;;
      *)
        if [ -n "$record" ]; then
          record="${record}"$'\n'"${line}"
        fi
        ;;
    esac
  done <"$log"
  flush_record

  echo "best-practices log check: ${log}"
  echo "  validation errors:                                            ${errors}"
  echo "  Validation layer records:                                     ${total}"
  echo "  allowlisted vendored-ImGui small-allocation warnings:         ${allowed_alloc}"
  echo "  allowlisted VK_SUBOPTIMAL_KHR resize-present warnings:        ${allowed_suboptimal}"
  echo "  unexpected validation messages:                               ${foreign}"

  if [ "$errors" -ne 0 ] || [ "$foreign" -ne 0 ]; then
    echo "::error::best-practices validation log is not clean: ${errors} error(s), ${foreign} unexpected message(s)"
    return 1
  fi
  echo "OK: no validation errors; every warning is one of the two known classes (vendored-ImGui dedicated allocations, resize-present VK_SUBOPTIMAL_KHR)"
  return 0
}

# Scratch directory used by --self-test; global so the EXIT trap can clean it
# up after run_self_test's locals are out of scope.
SELFTEST_TMPDIR=""

cleanup_self_test() {
  if [ -n "${SELFTEST_TMPDIR}" ]; then
    rm -rf "${SELFTEST_TMPDIR}"
  fi
}

# --self-test: exercise the classifier against synthetic logs so the gate
# itself is testable outside CI. Exits non-zero if any expectation is wrong.
run_self_test() {
  local tmpdir
  tmpdir="$(mktemp -d)"
  SELFTEST_TMPDIR="$tmpdir"
  trap cleanup_self_test EXIT

  local failures=0

  expect_pass() {
    local name="$1" path="$2"
    local output
    if output="$(check_log "$path")"; then
      echo "PASS (expected clean): ${name}"
    else
      echo "FAIL (expected clean, got non-zero): ${name}"
      printf '%s\n' "$output"
      failures=$((failures + 1))
    fi
  }

  expect_fail() {
    local name="$1" path="$2"
    local output
    if output="$(check_log "$path")"; then
      echo "FAIL (expected failure, got clean): ${name}"
      failures=$((failures + 1))
    else
      echo "PASS (expected failure): ${name}"
    fi
  }

  cat >"${tmpdir}/clean.log" <<'EOF'
Selected GPU: Example GPU (Vulkan 1.4.0)
recreation smoke OK
EOF

  cat >"${tmpdir}/known.log" <<'EOF'
Selected GPU: Example GPU (Vulkan 1.4.0)
Validation layer: vkBindBufferMemory(): Trying to bind VkBuffer 0x1 to a memory block which is fully consumed by the buffer. The required size of the allocation is 192, but smaller buffers like this should be sub-allocated from larger memory blocks. (Current threshold is 1048576 bytes)
Validation layer: vkBindImageMemory(): Trying to bind VkImage 0x2 to a memory block which is fully consumed by the image. The required size of the allocation is 262144, but smaller images like this should be sub-allocated from larger memory blocks. (Current threshold is 1048576 bytes)
EOF

  cat >"${tmpdir}/known-duplicate-note.log" <<'EOF'
Selected GPU: Example GPU (Vulkan 1.4.0)
Validation layer: (Warning - This VUID has now been reported 10 times, which is the duplicate_message_limit value, this will be the last time reporting it).
vkBindBufferMemory(): Trying to bind VkBuffer 0x3 to a memory block which is fully consumed by the buffer. The required size of the allocation is 192, but smaller buffers like this should be sub-allocated from larger memory blocks. (Current threshold is 1048576 bytes)
EOF

  # The new known class: the resize-exercising recreation test presents while the
  # surface is being resized, producing VK_SUBOPTIMAL_KHR warnings, each followed
  # by an explanatory continuation line.
  cat >"${tmpdir}/known-plus-suboptimal.log" <<'EOF'
Selected GPU: Example GPU (Vulkan 1.4.0)
Validation layer: vkBindBufferMemory(): Trying to bind VkBuffer 0x4 to a memory block which is fully consumed by the buffer. The required size of the allocation is 192, but smaller buffers like this should be sub-allocated from larger memory blocks. (Current threshold is 1048576 bytes)
Validation layer: vkQueuePresentKHR(): pPresentInfo[0] VK_SUBOPTIMAL_KHR was returned. VK_SUBOPTIMAL_KHR - Presentation will still succeed, subject to the window resize behavior, but the swapchain (VkSwapchainKHR 0x40000000004) is no longer configured optimally for the surface it targets.
Applications should query updated surface information and recreate their swapchain at the next convenient opportunity.
Validation layer: vkQueuePresentKHR(): pPresentInfo[0] VK_SUBOPTIMAL_KHR was returned. VK_SUBOPTIMAL_KHR - Presentation will still succeed, subject to the window resize behavior, but the swapchain (VkSwapchainKHR 0x830000000083) is no longer configured optimally for the surface it targets.
Applications should query updated surface information and recreate their swapchain at the next convenient opportunity.
recreation smoke OK
EOF

  cat >"${tmpdir}/foreign-warning.log" <<'EOF'
Selected GPU: Example GPU (Vulkan 1.4.0)
Validation layer: vkBindBufferMemory(): Trying to bind VkBuffer 0x5 to a memory block which is fully consumed by the buffer. The required size of the allocation is 192, but smaller buffers like this should be sub-allocated from larger memory blocks. (Current threshold is 1048576 bytes)
Validation layer: vkCreateDevice(): some other best-practices warning we do not know about
EOF

  # Narrowness guard for class 2: same API prefix, different result/explanation.
  cat >"${tmpdir}/present-but-not-suboptimal.log" <<'EOF'
Selected GPU: Example GPU (Vulkan 1.4.0)
Validation layer: vkQueuePresentKHR(): pPresentInfo[0] VK_ERROR_OUT_OF_DATE_KHR was returned. VK_ERROR_OUT_OF_DATE_KHR - something else entirely.
EOF

  cat >"${tmpdir}/validation-error.log" <<'EOF'
Selected GPU: Example GPU (Vulkan 1.4.0)
Validation layer: vkBindBufferMemory(): Trying to bind VkBuffer 0x6 to a memory block which is fully consumed by the buffer. The required size of the allocation is 192, but smaller buffers like this should be sub-allocated from larger memory blocks. (Current threshold is 1048576 bytes)
Validation Error: [ VUID-vkCreateInstance-ppEnabledExtensionNames-01388 ] something is wrong
EOF

  expect_pass "clean log (no validation output)" "${tmpdir}/clean.log"
  expect_pass "known vendored-ImGui warnings only" "${tmpdir}/known.log"
  expect_pass "known warning with duplicate-message-limit note" "${tmpdir}/known-duplicate-note.log"
  expect_pass "known vendored warnings plus resize VK_SUBOPTIMAL_KHR warnings" "${tmpdir}/known-plus-suboptimal.log"
  expect_fail "foreign warning after known warning" "${tmpdir}/foreign-warning.log"
  expect_fail "present warning that is not VK_SUBOPTIMAL_KHR" "${tmpdir}/present-but-not-suboptimal.log"
  expect_fail "validation error after known warning" "${tmpdir}/validation-error.log"
  expect_fail "missing log file" "${tmpdir}/does-not-exist.log"

  echo
  if [ "$failures" -ne 0 ]; then
    echo "::error::check-best-practices-output.sh self-test FAILED (${failures} case(s))"
    return 1
  fi
  echo "OK: check-best-practices-output.sh self-test passed (8 cases)"
  return 0
}

main() {
  local arg="${1:-}"
  if [ -z "$arg" ]; then
    echo "usage: check-best-practices-output.sh <validation-log>" >&2
    echo "       check-best-practices-output.sh --self-test" >&2
    exit 1
  fi
  if [ "$arg" = "--self-test" ]; then
    run_self_test
    return
  fi
  check_log "$arg"
}

main "$@"
