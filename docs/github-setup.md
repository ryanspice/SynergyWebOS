> **Historical 0.3.4 documentation.** For the native 0.4.0 candidate, start at [the current README](../README.md) and [native verification](../native/docs/VERIFICATION.md). Dock references are historical; the dock has been removed from this bundle. Do not use legacy web-packaging instructions to produce a native release.

# GitHub setup — remaining steps

Local repository status at handoff: initialised on `main`; history starts at
`93aa241` (initial scaffold) and continues with guard fixes and doc follow-ups
(`git log --oneline` is authoritative). **No remote, no push, no GitHub
repository.** Publication is the lead's explicit decision (per the brief).
Nothing below was executed by the repo workstream.

Before the first push, squash the scaffold follow-up commits so the public
history reads cleanly (nothing is published yet, so this is safe):

```powershell
git reset --soft 93aa241
git commit -m "Initial commit: repository scaffold, docs, CI and hygiene rules"
```

## 0. Prerequisites / decisions before publishing

1. **Final repository name** — from `app/BRAND.md`; the current working name is
   `synergy-tv`. Also review the trademark note in
   [`brand-and-licensing.md`](brand-and-licensing.md).
2. **Licence** — MIT is in place; the lead confirms it (see
   [`decisions.md`](decisions.md#d-001--licence-mit-lead-confirmation-requested)).
3. **Attribution email** — no action required; see
   [`decisions.md`](decisions.md#d-009--attribution-email-no-private-address-required-for-publication).
4. **Visibility** — private first is recommended while the live config/evidence
   is referenced; the repo itself is sanitised.
5. **BRIEF.md is internal.** It contains the brief for the rebuild and working
   context; it is owned by the lead and is untracked. Do not publish it unless
   the lead decides it is suitable.

## 1. Sanitise / flatten history (recommended before first push)

The commit that starts this history contains only repository-owned root files
(see below). A scan of every commit found no secrets, tokens, private keys,
device addresses or personal paths. While nothing is published, the lead may
freely amend or squash commits before the first push. Do not rewrite a
published history.

## 2. Create the repository (requires lead authorisation)

```powershell
# With GitHub CLI, logged in (checked: account ryanspice):
gh repo create synergy-tv --private --source . --remote origin --push
```

Or create it in the web UI with **no** README/licence/gitignore (they exist
locally), then:

```powershell
git remote add origin git@github.com:<owner>/synergy-tv.git
git push -u origin main
```

Rules: **no force push**, **no credentials in URLs**, SSH keys stay on the PC
outside the repo.

## 3. Repository settings to apply after creation

- **Description:** TV-first Synergy experience for LG webOS: TV app, dock/app
  switcher and handoff tooling. (Refine after branding.)
- **Topics:** `webos`, `lg-tv`, `synergy`, `tv-app`, `keyboard-mouse`,
  `webosbrew` (adjust after branding).
- **Licence:** confirm MIT in the license picker (account-owner action).
- **Security:** enable Dependabot alerts + security updates, private
  vulnerability reporting (the `SECURITY.md` channel), secret scanning and
  push protection if available for the account.
- **Branch protection / ruleset on `main`:** require the `CI` checks
  (`Repo hygiene + docs links`, and `Build + test` once active), require
  conversation resolution, block force pushes. Do not allow bypass for
  contributors.
- **Actions:** restrict allowed actions to GitHub-authored actions if
  available; the workflow requests only `contents: read`.
- **Environments:** none required; CI never touches the TV/PC.
- Update `.github/ISSUE_TEMPLATE/config.yml` URLs if the owner/name changes.

## 4. Initial commit contents (repository-owned only)

```
.gitattributes .editorconfig .gitignore
.githooks/pre-commit
.github/... (ci.yml, dependabot.yml, ISSUE_TEMPLATE/*, PULL_REQUEST_TEMPLATE.md)
LICENSE README.md CONTRIBUTING.md SECURITY.md THIRD_PARTY_NOTICES.md
scripts/check-repo-guards.ps1 scripts/check-docs-links.ps1
docs/*.md
```

Not committed by this workstream (separate owners): `BRIEF.md`, `app/`,
`dock/`, `tools/`, `scripts/home/`, `scripts/build/`, `tests/`.

## 5. After the workstreams land

- Commit each workstream's folder (lead) once its report is accepted. Check
  before committing: `pwsh ./scripts/check-repo-guards.ps1 -Staged` and
  `pwsh ./scripts/check-docs-links.ps1`.
- Once `docs/` links to `app/`, `dock/` and the other folders are committed,
  remove `-AllowMissing 'app,dock,tools,scripts,tests'` from the CI docs-link
  step so every link becomes mandatory.
- Activate the CI build job automatically (it detects `scripts/build/build.ps1`).
- Add the release process: [`release.md`](release.md).
- Run the first tagged release only after a recorded live TV/PC acceptance test.
