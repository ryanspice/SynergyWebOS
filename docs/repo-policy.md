# Repository policy

## What may enter the repository

Source, scripts, tests, configuration templates and documentation that belong
to this project, plus small self-authored assets (icons) required to build it.

## What must never enter the repository

| Class | Examples | Handling |
| --- | --- | --- |
| Secrets / credentials | `.env`, tokens, passwords, SSH/adb keys, webOS CLI key directory, `.htpasswd` | Keep outside the repo; use a local untracked file. Ignored + guarded. |
| License keys / activation | Synergy license, `license*`, `*.lic` | Private; never shared, never committed. |
| Signing material | `*.pem`, `*.key`, `*.p12`, `*.pfx`, keystores | Stored privately; CI is not given them. See [`code-signing.md`](code-signing.md). |
| Backups / working copies | `*.bak`, `*.orig`, `backups/`, Synergy `db.json`/`local.json`/`synergy.conf` snapshots | Stay in the old working area or an unsynced runtime folder. |
| Proprietary binaries | Synergy installers, `app.asar`, `synergy-core*`, bundled LG/Symless tools | Used in place, never copied. |
| Build junk | `node_modules/`, `dist/`, `build/`, `*.ipk`, coverage, caches | Ignored; deliberate releases go to `dist/release/` or GitHub Releases only. |
| Personal data | Screenshots of desktops, logs with IPs/usernames, device dumps | Redact or keep local. |

## Staging discipline

- Stage explicit paths (`git add README.md docs/...`). Do **not** use
  `git add -A` or `git add .` — the working folder can contain local evidence.
- Enable the optional guard once per clone:

  ```powershell
  git config core.hooksPath .githooks
  ```

- CI runs the same guard (`scripts/check-repo-guards.ps1`) on every push.

## The old working areas

The previous prototype workspace and the third-party launcher source copy (both
sibling folders in the parent working area) are **preserved
working/evidence areas** and are not part of this repository. Under the
artifact lifecycle they are HOLD: this repository does not copy them, clean
them or delete them. See [`lifecycle.md`](lifecycle.md).

## Lifecycle for anything this repo produces

- Build outputs are produced into ignored `dist/`; remove them locally at will.
- Releases are deliberate: a release tag plus GitHub Release assets with a
  `.sha256`. See [`release.md`](release.md).
- Any incoming archive or external package is validated (provenance, hashes,
  safe entry paths) before extraction; originals are kept until promotion is
  verified.

## Renaming

The repository/working title is `synergy-tv` until the lead finalises the brand
name from `app/BRAND.md`. A rename touches the README title, `docs/`,
`appinfo.json` IDs and the remote URL; see [`decisions.md`](decisions.md).
