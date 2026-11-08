# CI scripts

Shell logic lives here rather than inline in `build.yml` so it can be run,
linted, and tested outside CI. The workflow steps are thin wrappers:

```yaml
- name: Run unit tests
  run: |
    ctest --test-dir build --output-on-failure -V 2>&1 | tee ctest.log
    bash .github/scripts/doctest-summary.sh ctest.log
```

Scripts are invoked as `bash <path>` rather than `./<path>` on purpose: the
execute bit is not reliably preserved through git on Windows checkouts, and a
missing `+x` would fail the job for the wrong reason.

## Scripts

| Script | Used by | Purpose |
| --- | --- | --- |
| `doctest-summary.sh` | linux | Assert the test suite actually ran: parse the doctest summary, reject any failing case, and enforce a floor on the number of passing cases. |
| `verify-examples-built.sh` | linux, macos | Assert the example executables exist (`build/examples/<name>/<name>`). `glslc` being absent makes `examples/CMakeLists.txt` `return()` early, silently producing a build with no examples. |
| `verify-examples-built-windows.sh` | windows | The same guard for the multi-config layout (`build/examples/<name>/Release/<name>.exe`), split out because that job runs PowerShell. Plain POSIX `sh`, so it needs no execute bit. |
| `stage-release-assets.sh` | release | Rename the macOS binary (asset names collide by basename) and restore the execute bit that `upload-artifact` strips. |
| `verify-install-consumer.sh` | install-check | Assert the installed/exported package: the published header set is exactly the contract list (implementation headers absent), the package config files and one library per exported target are installed, and a generated `find_package(VulkanWindow CONFIG REQUIRED)` consumer configures, links, builds and runs. |
| `check-best-practices-output.sh` | smoke (best-practices gate) | Validate a best-practices-run validation log: fail on any `Validation Error` and on any `Validation layer:` record outside the two narrowly-scoped known classes — (1) vendored-ImGui dedicated-allocation warnings (including the layer's duplicate-message-limit note line) and (2) the expected `VK_SUBOPTIMAL_KHR` resize-present warnings from the resize-exercising recreation test (handled by immediate swap-chain recreation). Has a `--self-test` mode that exercises clean/allowlisted/known+suboptimal/foreign-warning/error fixtures, including a lookalike present warning that must still fail. |

## Testing a change here

Each script takes a path argument and exits non-zero with an `::error::`
annotation, so they can be exercised against synthetic inputs:

```bash
# A healthy run passes.
printf '1: [doctest] test cases:  59 |  59 passed | 0 failed | 0 skipped\n' > /tmp/ctest.log
bash .github/scripts/doctest-summary.sh /tmp/ctest.log 55

# A dropped test TU fails.
printf '1: [doctest] test cases:  41 |  41 passed | 0 failed | 0 skipped\n' > /tmp/ctest.log
bash .github/scripts/doctest-summary.sh /tmp/ctest.log 55   # exits 1

# A failing case fails even when the floor is still met.
printf '1: [doctest] test cases:  59 |  55 passed | 4 failed | 0 skipped\n' > /tmp/ctest.log
bash .github/scripts/doctest-summary.sh /tmp/ctest.log 55   # exits 1
```

When you add tests, raise the floor passed to `doctest-summary.sh` in the linux
job. It is a floor rather than an exact count so that honest test additions do
not turn the build red.

`verify-install-consumer.sh` is exercised the same way, against a real prefix:
point it at an install prefix (`cmake --install` output) and any scratch
directory, and it fails with an `::error::` annotation when a header from the
contract list is missing, an implementation header reappeared, a package file or
archive was not installed, or the generated consumer does not configure, build
and run:

```bash
bash .github/scripts/verify-install-consumer.sh /tmp/vw-prefix /tmp/vw-consumer
```

The header contract is the array at the top of the script; keep it in step with
the `install(FILES ...)` list in `CMakeLists.txt`. A header added to the package
without being added there is reported as unexpected, which is deliberate — the
install surface is a contract, not a glob.

`check-best-practices-output.sh` carries its own fixtures: run
`bash .github/scripts/check-best-practices-output.sh --self-test` to exercise the
pass/fail classifier without a Vulkan device. To check a real log, run a
`VULKANWINDOW_BEST_PRACTICES=ON` Debug binary with
`VULKANWINDOW_VALIDATION_ABORT=1`, capture stdout+stderr, then pass the log path
to the script. The allowlist is deliberately narrow: exactly the vendored-ImGui
small-dedicated-allocation warning class and the `VK_SUBOPTIMAL_KHR` present
warnings that the resize-exercising recreation test legitimately produces (the
app recreates the swap chain immediately; a couple of warnings appear, then the
log is clean). Widen it only for a new *known* third-party/benign limitation,
never to silence a project diagnostic.

## Not extracted

Some shell stays inline because it is genuinely job-specific and extracting it
would add indirection without making anything testable: the per-job `apt-get` /
`brew` dependency lists, the Vulkan SDK version resolution and cache setup, the
`ccache` action, and the smoke-run loops (including their per-binary frame caps
and success-marker greps). The release job's asset renaming and execute-bit
restoration *are* extracted into `stage-release-assets.sh`; only the tag/version
consistency check and the publish step remain inline there.
