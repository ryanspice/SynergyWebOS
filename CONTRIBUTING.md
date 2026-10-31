# Contributing to synergy-tv

Thanks for looking. This is a small, personal, TV-first project; contributions
are welcome as focused issues or pull requests.

## Ground rules

1. **Small, reviewable changes.** One concern per pull request. Keep unrelated
   reformatting out of functional changes.
2. **No secrets, ever.** Never commit `.env` files, tokens, license keys, SSH/ADB
   keys, signing certificates, device credentials, Synergy bundles, `backups/`
   or personal captures. See [`SECURITY.md`](SECURITY.md). The optional
   pre-commit guard in [`.githooks/`](.githooks/) blocks the common cases; do
   not bypass it (`--no-verify`) without a written reason in the PR.
3. **Stage explicit paths.** Do not use `git add -A` / `git add .` in this
   repository; it can pick up local evidence, backups or generated output.
4. **No live-device changes in review.** Do not install, launch, reboot or
   reconfigure the TV, or restart the Synergy core/service, as part of a
   contribution. Produce the script or patch; the maintainer applies it.
5. **No third-party home-screen dependency.** The app, dock, home path and docs
   must not depend on, or target, any third-party launcher/home screen.
6. **Proprietary software is used, not shipped.** Do not add Synergy
   installers, `.asar` bundles, license data or any other Symless/LG-proprietary
   artifact. See [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

## Development setup

```powershell
git clone <repo-url> synergy-tv
cd synergy-tv
git config core.hooksPath .githooks   # optional local guard

# Once the build pipeline exists:
pwsh ./scripts/build/build.ps1 -DryRun
pwsh ./scripts/build/test.ps1
```

Code style follows [`.editorconfig`](.editorconfig) and [`.gitattributes`](.gitattributes):
UTF-8, LF in the repository, 2-space indentation (4 for Python), TypeScript and
Svelte 5 where a UI framework is used.

### Coding standards for this project

The project follows the general Windows toolchain standards used by the
maintainer:

- Prefer TypeScript.
- Prefer Svelte 5 runes where a component framework is used.
- Default to static/prerender output; there is no Node runtime on the TV.
- Keep code lean and dependency-light; the app must run in the TV's built-in
  Chromium engine.
- If the TV runtime cannot support a framework or toolchain, record a **dated
  exception** with the exact version, owner, concrete blocker and the next
  acceptance gate in [`docs/decisions.md`](docs/decisions.md). Do not force
  peer dependencies or hide failed/unrun checks.

> Note: the *repository tooling* does not require SvelteKit 3.x. Any SvelteKit
> usage must target current stable 3.x; if that is incompatible with the app,
> the exception above applies.

## Verification

Every change gets the smallest verification that can catch the failures it
could introduce. The minimum for a pull request is:

| Change | Minimum |
| --- | --- |
| Docs / config / templates | Read back the changed files, check links/targets, inspect the diff. |
| Scripts / tooling | Syntax check (e.g. `pwsh -NoProfile -Command "[ScriptBlock]::Create(...)"` / `node --check`), plus a dry run or fixture where one exists. |
| App / dock UI | Type check + build + browser check at 1920×1080 where feasible. |
| Packaging | Reproducible build; record the artifact hash. |
| Anything touching the client, input or home path | Static contract check **and** a recorded manual TV/PC acceptance test by the maintainer. |

Statuses are reported explicitly as **PASS / FAIL / BLOCKED / NOT_RUN**, with the
command and inputs. Never hide a failed or unrun required check. Live-device
evidence can only be supplied by the maintainer; PRs should say which live
checks remain.

## Pull request checklist

- [ ] Change is scoped to the affected area and keeps unrelated work intact.
- [ ] No secret, backup, license, proprietary binary or personal data added.
- [ ] Documentation updated for user-visible behaviour or config changes.
- [ ] Relevant checks recorded (command, result) in the PR description.
- [ ] Live-device steps listed for the maintainer if applicable.

## Commit messages

Short imperative subject (≤ 72 chars), optional body explaining *why*. Examples:

```
Add webOS package build script
Fix dock autohide on pointer leave
docs: record TV runtime exception for Svelte build
```

## Reporting bugs

Use the **Bug report** issue template. Include the app version, TV model and
webOS version, PC OS, Synergy version, and redacted log excerpts. **Redact host
IPs, machine names, usernames and license data** before posting logs in public
issues. Security issues go through [`SECURITY.md`](SECURITY.md), never a public
issue.

## License of contributions

Unless you state otherwise, contributions are licensed under the project's
[MIT license](LICENSE) (inbound = outbound). By submitting a pull request you
confirm you have the right to license the contribution under those terms.
