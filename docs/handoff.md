# Handoff — workstream state

Last updated: 2026-10-31 (repo workstream). Details of live configuration,
device access and evidence remain in the internal working brief (`BRIEF.md`)
and the old working area, not in this repository.

## Rooting persistence — status (2026-10-31)

**Root access did not survive the TV reboot.** After the reboot,
`/var/lib/webosbrew` came back as an older snapshot: `init.d/60-junction`
disappeared, the legacy `60-synergy-webos` hook returned, and the Homebrew
Channel `exec` method was denied to SynergyWebOS (`category "/"`). The app still
has its `requiredACG` declaration, but the HBC grant/role state did not persist.

What we did:

- shipped a legacy-compatible `app/tvctl.sh` wrapper in **0.3.2** so the
  restored old hook can start the real `client/tvctl.sh`;
- kept `lunabus.query` / `application.launcher` in the package manifest and
  rebuilt/reinstalled over the same app ID;
- verified the TV input stack after the reboot (keyboard and mouse events
  reached the app) and removed the left-edge TV link that caused an instant
  right-edge return;
- recorded the runtime reset and HBC denial in `app/REPORT.md`.

**Awaiting:** one clean power cycle/re-provision on the TV to see whether the
legacy hook starts 0.3.2 and whether HBC `exec` / the ACG grant returns. If it
does not, repair Homebrew Channel persistence (`elevate-service`, service/role
files, `/var/lib/webosbrew` contents) before claiming root survives reboot.

## Workstreams

| Workstream | Scope | Status |
| --- | --- | --- |
| Lead | `BRIEF.md`, integration, live deploy, final repo/rename | Local delivery deployed; rooting persistence open |
| Synergy config | `tools/synergy/` — small-corner handoff, delay/override, TV as a normal screen | Landed: live top-edge 2px strip, delay 0 |
| Dock | `dock/` — autohide dock/app switcher, integration contract | Landed: 0.3.0, open apps first |
| Home key | `scripts/home/` — Windows key → TV system home, third-party launcher cleanup | Landed: Windows key + Alt hold |
| App | `app/` — branded TV app, client scripts, `BRAND.md` | Landed: SynergyWebOS 0.3.2 |
| Build/QA | `scripts/build/`, `tests/` — build, package, acceptance checks | Landed: build PASS, tests 73/1 skip |
| Repo (this) | Root files, `.github/`, `docs/`, git init and hygiene | Local commits done, no remote; GitHub steps pending |

All workstream folders are now committed. Do not modify another workstream's
folder from the repository workstream.

## Integration gaps for the lead (read-only observations, 2026-10-31)

- **Brand is final:** *SynergyWebOS by Ryan Spice-Finnie*, repository
  `synergy-tv`, package ID `dev.hurrspice.synergywebos` unchanged for upgrade.
- **Build output location differs from `building.md`:** `scripts/build/` uses
  `dist/` (ignored), and release packages should go to `dist/release/` as
  documented. Update `building.md` if the build entry point is renamed.
- **CI:** `ci.yml` now provisions the webOS CLI, runs `build.ps1`, and calls
  `tests/lib/Run-Tests.ps1`. It still needs a first push/run on
  `windows-latest`; the environment has not been pushed.

## Dependencies on the repo workstream

- Brand/name: [`app/BRAND.md`](../app/BRAND.md) — the README title and the
  final repository name follow it.
- Build interface: [`building.md`](building.md) — if the build delegate picks a
  different entry point, update `building.md` and `ci.yml` together.
- New dependency files (e.g. `app/package.json`) are picked up by
  `.github/dependabot.yml`; directories without a manifest are simply skipped.

## Dependencies of the repo workstream

- `app/BRAND.md` (brand) and the app ID decision → README title, rename.
- `scripts/build/build.ps1` (or the Node equivalent) → CI build job activates.
- Live acceptance evidence (maintainer) → release notes, not the repo.

## Integration rules

- Each delegate commits/works only in its own paths; the lead integrates.
- Live TV/PC changes are applied by the lead from the delegates' scripts and
  patches; the repo only holds source and instructions.
- Never restart the Synergy core/service or redeploy the live client from a
  contributor's change without the maintainer's approval.
