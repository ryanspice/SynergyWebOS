> **Legacy 0.3.4 reference.** The native 0.4.0 candidate is documented in the root README and `native/docs/`. Dock references below describe a retired component; no dock is included. Historical TV results do not validate the native rebuild.

# Build, QA and packaging (Flash-off 2 scope)

Reproducible static build + webOS **IPK packaging** and acceptance tests for the TV app
(`app/`), dock (`dock/`), home patch (`scripts/home/`) and Synergy tooling
(`tools/synergy/`). Everything here is read-only with respect to those scopes: the
pipeline never edits `app/`, and no script installs, launches or touches the TV.

## Quick start (Windows, PowerShell 7 or 5.1)

```powershell
# Full gate: static checks + IPK + SHA-256 + receipt
pwsh -NoProfile -File scripts/build/build.ps1

# Checks only (no artifact, safe in CI and during review)
pwsh -NoProfile -File scripts/build/build.ps1 -CheckOnly

# All tests (unit + acceptance + integration)
pwsh -NoProfile -File tests/lib/Run-Tests.ps1

# A single suite or filter
pwsh -NoProfile -File tests/lib/Run-Tests.ps1 -Tag unit
pwsh -NoProfile -File tests/lib/Run-Tests.ps1 -Filter 'corner'
```

Exit codes: `build.ps1` → `0` pass, `1` check/package failure, `2` app/ not ready or
manifest missing. `Run-Tests.ps1` → `0` all executed checks pass, `1` failures, `2` a
required check was **BLOCKED** by a missing dependency (listed in the output).

## Artifacts (`dist/`, gitignored)

| File | Meaning |
| --- | --- |
| `dist/synergy-tv-app-<version>.ipk` | installable package (app ID preserved: `dev.canopy.synergywebos`) |
| `dist/synergy-tv-app-<version>.ipk.sha256` | `sha256  filename` sidecar |
| `dist/build-info.json` | receipt: status, artifact + hash, per-stage results, warnings, override list, exclusions, targets, tool versions |
| `dist/staging/` | exact staged input to `ares-package` (inspect what shipped) |

Release hash is printed at the end of the run and recorded in `build-info.json`. Verify
independently with `Get-FileHash dist/synergy-tv-app-*.ipk -Algorithm SHA256`.

## Inputs and dependencies

| Input | Owner | Rule |
| --- | --- | --- |
| `app/` incl. `app/appinfo.json` | Flash-high | read-only. Build-owned overrides of `appinfo.json` are **refused** (exit 1, no IPK). |
| `scripts/build/package-manifest.json` | this scope | excludes, targets, `buildOverrides` |
| ares-cli 2.4.0 (`ares-package`) | host tool | default `B:\Dev\webos\.tools\ares-cli`; override with `-AresCliDir` or `$env:ARES_CLI_DIR` |
| Node.js | host tool | `node --check` JavaScript syntax |
| Python 3 | host tool | AST parse of `client/*.py` and `scripts/home/*.py` |
| `tar` | host tool | only used by the tests' IPK reader fallback (the reader itself is pure .NET) |
| POSIX `sh` (Git for Windows) or WSL | host tool | `sh -n` on `tvctl.sh`; **SKIP** with the reason when neither can start |

No package installation, no bundler, no Node runtime on the TV. Missing tools **SKIP or
WARN with an explicit message**; they never silently pass.

## What the build checks (all deterministic)

1. **appinfo contract** — required fields, ID rules (no reserved domains), `X.Y.Z`
   version, `type: web`, resolution, `requiredACG` array, referenced icons/entry exist
   (a missing icon makes `ares-package` fail hard).
2. **HTML** — doctype/charset/lang/title/main, tag balance (inline `<script>`/`<style>`
   contents ignored), SVG blocks validated by the XML parser, local `src`/`href`/`url()`
   references resolve.
3. **CSS** — balanced blocks; warns on remote `url()`.
4. **JavaScript** — `node --check` on every `app/js`, `app/client`, plus dock files.
   (No TypeScript/`tsc` gate: the app is plain static JS by design — documented
   exception, see `app/BRAND.md`/`INTEGRATION.md`.)
5. **TV static compatibility** — flags features above the Chromium **87** floor
   (webOS 22; the TV runs webOS 26 = Chromium 132): lookbehind, class private/static
   blocks, top-level await, container queries, subgrid, remote scripts, and warnings for
   `:has()`, `color-mix()`, named groups, etc.
6. **1080p TV-safe heuristics** — viewport meta, tiny font sizes.
7. **No legacy launcher coupling** in shipped `app/`+`dock/` (`com.canopy.*`, `Orbis`,
   `home-key.py`). `scripts/home/` is the reviewed exception (its cleanup script must
   name the old package); see the report.
8. **Forbidden artifacts** — `.bak`, `.env`, keys/certs, `.ipk`, proprietary binaries.
9. **Staging** — clean copy of `app/`, excludes from the manifest, secret/backup filter,
   build-owned override (`webos-meta/synergy-tv-build.json`), post-stage required-file
   and leak checks.
10. **Package** — `ares-package`, `ares-package -i` inspection, SHA-256 + sidecar +
    receipt.
11. **Pipeline self-check** — `-AllowMissingApp` runs the above structure without an
    artifact (integration fixtures use an isolated mirror to prove it end to end).

## Tests layout (`tests/`)

| Path | Purpose |
| --- | --- |
| `tests/lib/Run-Tests.ps1` | runner (`-Tag unit\|acceptance\|integration\|all`, `-Filter`, `-Json`, `-ReportPath`) |
| `tests/lib/Assert.ps1` | assertion helpers, fixtures, shell helpers |
| `tests/lib/Ipk.ps1` | pure-.NET `ar`+`tar.gz` IPK reader (header + payload inspection) |
| `tests/unit/Checks.Tests.ps1` | check helpers + sensitivity fixtures (each known-bad input must be caught) |
| `tests/acceptance/App.Tests.ps1` | app package contract, TV compat, home/Meta behaviour, offline |
| `tests/acceptance/Components.Tests.ps1` | dock separation + contract, home patch, 2px corner tooling |
| `tests/acceptance/Repo.Tests.ps1` | no-legacy, no secrets, licence screen, CI wiring |
| `tests/acceptance/Shell.Tests.ps1` | `tvctl.sh` LF-only, POSIX syntax when a shell is available |
| `tests/integration/Pipeline.Tests.ps1` | real `build.ps1` on a mirrored fixture; release IPK contents |

Tests that cannot run (missing app, missing artifact, POSIX shell blocked) are **SKIP**
or **BLOCKED** with the reason — never a silent pass. Pester is not installed or
required; `-UsePester` reuses the same test bodies if Pester 5 is present.

## Reproducibility

Same source + same tool versions ⇒ same staged tree. The IPK itself is not bit-identical
across runs because `ares-package` timestamps archive members; the receipt records the
exact hash for **each** build, and `dist/<artifact>.sha256` is the authoritative hash to
verify against for the installed file. `dist/build-info.json` records OS, PowerShell,
Node, Python and ares-cli versions so a rebuild can be attributed.
