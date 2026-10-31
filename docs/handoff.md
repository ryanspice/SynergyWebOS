# Handoff — workstream state

Last updated: 2026-10-31 (repo workstream). Details of live configuration,
device access and evidence remain in the internal working brief (`BRIEF.md`)
and the old working area, not in this repository.

## Workstreams

| Workstream | Scope | Status |
| --- | --- | --- |
| Lead | `BRIEF.md`, integration, live deploy, final repo/rename | In progress |
| Synergy config | `tools/synergy/` — small-corner handoff, delay/override, TV as a normal screen | Pending report |
| Dock | `dock/` — autohide dock/app switcher, integration contract | Pending report |
| Home key | `scripts/home/` — Windows key → TV system home, third-party launcher cleanup | Pending report |
| App | `app/` — branded TV app, client scripts, `BRAND.md` | Pending report |
| Build/QA | `scripts/build/`, `tests/` — build, package, acceptance checks | Pending report |
| Repo (this) | Root files, `.github/`, `docs/`, git init and hygiene | Done (initial commit); GitHub steps pending |

"Pending report" means the workstream is still being assembled by its delegate.
Do not assume a folder exists until it is listed in the repository tree.

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
