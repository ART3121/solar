# Contributing to Solar

Solar accepts focused fixes and complete vertical slices that preserve the
small native Core, thin CLI, and isolated backend boundaries described in
`AGENTS.md`.

## Before a change

- Search existing issues and open a design issue before a broad architectural
  change.
- Do not include local builds, `.solar/`, logs, generated waveforms, netlists,
  toolchain outputs, or downloaded dependencies.
- Do not add automatic downloads, installation, shell command construction, or
  arbitrary backend flags.

## Build and test

Building the bundled YANC toolchain requires Linux, CMake 3.20+, a C17
compiler, GNU Make, Flex, and Bison.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Run the sanitizer configuration for changes involving memory, processes,
paths, parsers, or ownership:

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DSOLAR_ENABLE_SANITIZERS=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

Unit tests must not require external EDA tools. Integration tests may skip an
unavailable tool, but the skip reason must be explicit and must not be reported
as a pass. Real YANC evidence is separate from fake-tool orchestration tests.

## Patch expectations

- Use C17 and the naming, ownership, diagnostic, and cleanup conventions in
  `AGENTS.md`.
- Add a regression test for every behavior fix.
- Keep tool-specific argv and output interpretation in `src/backends/`.
- Update public documentation when behavior changes.
- Run the smallest relevant tests first and the full suite before submitting.

Security issues follow [SECURITY.md](SECURITY.md), not the public issue tracker.

Questions and early design ideas belong in GitHub Discussions. Reproducible
defects use the issue forms, and every pull request must complete the repository
template with exact verification evidence. Participation is governed by
[CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).
