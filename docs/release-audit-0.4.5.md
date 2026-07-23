# Solar 0.4.5 public release audit

Audit date: 2026-07-22

Final recommendation: **READY WITH KNOWN LIMITATIONS**

## 1. Executive summary

Solar 0.4.5 builds, installs, and completes its principal Linux flows. No known
release blocker remains after this audit. One high-severity stack overflow in
the bundled YANC `cmmcomp` was reproduced under ASan and corrected; project
mutation races and several bounded-read/descriptor issues were also corrected.

The release is appropriate as a stable 0.4.5 release for local, trusted
hardware projects. It should not be presented as a security-hardened
compiler service for hostile source input. The main residual risk is the large
legacy C code surface in bundled YANC, which has not received exhaustive
fuzzing. Git history was unavailable in this workspace, cocotb 2.x was absent,
and LeakSanitizer could not run under the executor's `ptrace` policy.

## 2. Current project state

- Version: Solar 0.4.5, manifest format 2 with format-1 normalization.
- Platform: Linux, C17, POSIX process/filesystem APIs.
- CLI: `init`, `scan`, `config`, `check`, `doctor`, `clean`, `build rtl`,
  `build sim`, `build synth`, `build full`, `view`, and `report`.
- Backends: bundled YANC 5.2, Icarus/VVP, Verilator, Yosys, optional cocotb,
  optional GTKWave/Surfer.
- Public outputs remain under `sim/`, `synth/`, `hardware/`, `simulation/`,
  and generated CMM/C++ Assembly under `software/`. Internal data stays in
  `.solar/`.

## 3. Builds executed

| Build | Result |
| --- | --- |
| GCC 16.1.1 Debug | PASS |
| Clang 22.1.8 Debug | PASS |
| GCC ASan/UBSan Debug | PASS |
| GCC Release | PASS |
| Temporary-prefix installation, including a prefix with spaces | PASS |
| Installed `solar --version` and private bundled-YANC resolution | PASS |

The build uses CMake 4.4.0 here and declares CMake 3.20 as its minimum. Solar
code compiled with `-Wall -Wextra -Wpedantic -Wconversion -Wshadow` and
`-Wstrict-prototypes`. Remaining Flex/Bison grammar conflict notices originate
in the bundled YANC grammars and are documented dependency warnings.

## 4. Tests executed

- Each complete GCC, Clang, and ASan/UBSan matrix registered 32 CTest entries:
  31 passed and one real cocotb integration was skipped explicitly.
- Real Icarus/VVP, no-waveform Icarus, Verilator, Yosys, format-1, format-2,
  and bundled YANC CMM/C++/Assembly integrations passed.
- After final hardening, 13 focused configuration, discovery, runner,
  artifact, resolver, fake-YANC, and real-YANC tests passed.
- Long CMM, C++, and Assembly identifiers fail cleanly and preserve the prior
  generated hardware; CMM was also rerun under ASan.
- Large simultaneous stdout/stderr, paths with spaces, missing waveforms,
  process failure, missing executable, signal interruption, atomic fallback,
  symlink refusal, and concurrent project mutation have regression coverage.

## 5. Tests not executed

| Test | Status | Reason |
| --- | --- | --- |
| Real Verilator+cocotb 2.x flow | SKIP | cocotb 2.x is not installed locally |
| Full LeakSanitizer suite | SKIP | LSan aborts because this executor uses `ptrace` |
| Git history secret scan | SKIP | exposed `.git/` contains no repository metadata |
| Remote GitHub Actions run | SKIP | no push or hosted workflow execution was requested |
| cppcheck/Valgrind/ShellCheck | SKIP | tools are not installed; equivalent available checks were used |

Skipped checks are not counted as passes.

## 6. Sanitizer results

ASan and UBSan passed the exercised Solar and bundled-YANC flows with
`detect_leaks=0`. The audit's malicious long-identifier probe originally
reported a `stack-buffer-overflow` in CMM `exec_id`; the corrected regression
now exits through a bounded diagnostic. Equivalent C++ and Assembly probes are
also rejected by APPComp. No remaining ASan/UBSan finding was observed.

LSan alone reported that it cannot operate under `ptrace`, before executing a
test. CI contains a separate leak-enabled step for Solar-owned non-YANC tests.

## 7. Static analysis results

`clang-tidy` and `scan-build` were run over Solar Core, CLI, and backends. They
identified real arithmetic overflow checks, tainted PATH allocation, report
read races, runner capture arithmetic, and bundled-YANC null/stream/buffer
defects; these were corrected. Remaining raw reports were reviewed against
control flow and tests and are primarily false positives caused by analyzers
not correlating `SolarResult.status` with initialized output parameters, plus
non-blocking style warnings. The bundled toolchain remains a fuzzing target.

## 8. Security findings

### SEC-01: CMM identifier stack overflow

- Severity: HIGH, fixed.
- Location: `third_party/yanc/Compilers/CMMComp/Sources/variaveis.c`.
- Impact: a crafted CMM identifier could corrupt `cmmcomp` memory.
- Reproduction: compile a CMM source containing a 700-byte identifier under
  the sanitizer build.
- Correction: validate individual and qualified symbol capacity before copies;
  APPComp/ASMComp received equivalent fixed-buffer guards.
- Regression: real YANC CMM/C++/Assembly integrations inject overlong names,
  require a diagnostic, and compare the previous public RTL byte-for-byte.

### SEC-02: concurrent project mutation

- Severity: HIGH, fixed.
- Location: build context, scan, config edit, clean, filesystem lock code.
- Impact: simultaneous commands could race on state and atomic publication.
- Reproduction: start two mutating operations for one project concurrently.
- Correction: non-blocking Linux `flock` on the project-root descriptor.
- Regression: one operation acquires the lock; the second fails clearly and
  cannot alter the first operation's result.

### SEC-03: check-then-open and unbounded reads

- Severity: MEDIUM, fixed.
- Location: manifest edit/scan, report, artifact registry, simulation output,
  and automatic source discovery.
- Impact: symlink/special-file replacement or excessive memory allocation.
- Reproduction: replace state between inspection/open, or provide sparse files
  above the documented limits.
- Correction: `O_NOFOLLOW`, descriptor `fstat`, regular-file checks, 16 MiB
  state/output limits, 64 MiB discovery limit, and bounded runner capture.
- Regression: sparse oversized manifest/state/report/source files and 17 MiB
  simulation output are exercised.

No `system()` or shell-built backend command was found. Backends use argv-based
`execvp`; cleanup/publication use project-relative ownership checks and reject
unsafe symlinks. Viewer launch remains explicit through `solar view`.

## 9. Quality findings

### QUAL-01: bundled compiler error paths

- Severity: MEDIUM, fixed for reproduced paths.
- Location: YANC ASM/CPP compiler streams, allocations, and output generation.
- Impact: crashes, missing diagnostics, or success without complete artifacts.
- Reproduction: missing output streams, allocation failure paths, malformed
  complex values, and absent generated files.
- Correction: checked streams/allocations, bounded parsing, required-artifact
  validation, and failure-before-publication behavior.
- Regression: fake stage tests and real CMM/C++/Assembly pipelines.

### QUAL-02: accidental package scope

- Severity: LOW, fixed.
- Location: CMake bundled-YANC build/install list.
- Impact: optional unsupported GTKWave helper binaries could be shipped.
- Reproduction: inspect the previous private YANC bundle/install manifest.
- Correction: build/install only `cmmcomp`, `cpppp`, `cppcomp`, `appcomp`, and
  `asmcomp`.
- Regression: final bundle/install inventory.

## 10. Corrections performed

- Added project-root mutation locking.
- Fixed Icarus/Verilator allocation leaks and stale-`errno` diagnostics.
- Bounded manifest, report, registry, source-indexing, tool-output, PATH, and
  argv allocation arithmetic.
- Hardened descriptor-based reads and report log-tail inspection.
- Fixed reproduced YANC overflow, stream, allocation, and generated-output
  validation defects.
- Restricted installed YANC binaries to the supported toolchain.
- Added `SECURITY.md`, `CONTRIBUTING.md`, pinned/minimal-permission CI and
  release workflows, checksum-validated packaging, third-party patch
  disclosure, and this audit report.
- Removed local build trees and editor logs from the release source root.

## 11. Remaining risks

- MEDIUM: bundled YANC is legacy compiler C with substantial fixed-buffer code.
  The reproduced overflow is fixed, but exhaustive fuzzing has not been done.
  Treat source inputs as trusted local project data for this release.
- LOW: cocotb support has fake-driver coverage but no real local cocotb 2.x run.
- LOW: Git history, tracked-file status, and prior-secret removal cannot be
  proven from this snapshot because repository metadata is absent.
- LOW: the new GitHub workflow is reviewed locally but has not run remotely.
- LOW: terminal behavior is tested with TTY/non-TTY helpers, but the project
  does not claim portability beyond Linux.

## 12. Dependencies and licenses

| Class | Dependency | License/status |
| --- | --- | --- |
| Build | CMake, C17 compiler, GNU Make, Flex, Bison | system build tools |
| Bundled/runtime | YANC 5.2 at `ea6135af18735e66a2bd23445749d6a244990fcd` | MIT; license and notice installed |
| Runtime by flow | Icarus/VVP, Yosys | external, user-installed |
| Optional runtime | Verilator, Python+cocotb, GTKWave, Surfer | external, user-installed |
| Solar | Core/CLI/source | MIT |

Solar performs no automatic downloads or installation. Local YANC
modifications are disclosed in `THIRD_PARTY_NOTICES.md` and `docs/yanc.md`.

## 13. Portability

Linux is the only declared target and was validated on the current Arch Linux
host. GCC and Clang pass. The implementation intentionally uses Linux/POSIX
facilities including `fork`, `execvp`, `waitpid`, `signalfd`, `/proc`,
descriptor-relative filesystem operations, and `flock`. Atomic publication has
a portable same-filesystem backup/rename fallback when `RENAME_EXCHANGE` is
unavailable. GUI viewers are optional.

## 14. Documentation state

README, CLI help, installation and command manuals, PT-BR getting started,
troubleshooting, project format, architecture, backend/compiler contracts,
profiles, YANC, testing, migration notes, roadmap, changelog, contribution,
support, security, and third-party notices were reconciled with 0.4.5. The
docs do not claim complete TOML, automatic viewing, automatic EDA dependency
installation, or a completed cocotb run. Historical verification sections
remain explicitly historical.

## 15. GitHub automation

`.github/workflows/ci.yml` runs GCC and Clang Debug build/tests, a Release
install and package smoke test, ASan/UBSan, and a separate leak-enabled
non-YANC suite on Ubuntu 24.04. `.github/workflows/release.yml` builds the
x86_64 archive on Ubuntu 22.04, gates its glibc symbols, validates its checksum,
installer rollback, examples, and uninstall, then creates a draft release from
an existing matching tag. Workflow permissions are minimal, checkout is pinned
to the v4.2.2 commit with credentials disabled, jobs have timeouts, and
concurrent superseded runs are controlled. Hosted execution remains unverified
until the repository is pushed.

## 16. Blocking issues

No known blocking issue remains in the audited snapshot. The CMM memory
corruption was blocking and is fixed with real sanitizer-backed regression.
If the intended launch model includes compiling hostile, multi-tenant, or
internet-supplied YANC sources, the residual compiler-hardening/fuzzing gap
becomes blocking and the recommendation changes to **NOT READY** for that use.

## 17. Final recommendation

**READY WITH KNOWN LIMITATIONS**

Release Solar 0.4.5 publicly as a stable Linux x86_64 release for trusted local
project inputs. Do not label it security-hardened or suitable for untrusted
compiler workloads. Before a stable 1.0/security-sensitive service, run a
fuzzing campaign over all bundled YANC frontends, obtain a real cocotb 2.x CI
result, execute LSan outside `ptrace`, run the hosted workflow, and audit the
actual Git history for credentials and generated artifacts.

## Terminal summary

```text
Build             PASS
Tests             PASS (1 external integration SKIP: cocotb 2.x absent)
Sanitizers        PASS (LeakSanitizer SKIP: ptrace restriction)
Static analysis   PASS (actionable findings fixed; residual reports reviewed)
Security          PASS WITH KNOWN LIMITATIONS
Packaging         PASS
Documentation     PASS
Release readiness READY WITH KNOWN LIMITATIONS
```
