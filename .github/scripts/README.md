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

## Not extracted

Some shell stays inline because it is genuinely job-specific and extracting it
would add indirection without making anything testable: the per-job `apt-get` /
`brew` dependency lists, the Vulkan SDK version resolution and cache setup, the
`ccache` action, the smoke-run loops, and the release job's `chmod`/rename
wrappers (now a single script call).
