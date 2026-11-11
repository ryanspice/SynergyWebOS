# Decisions and recorded exceptions

Newest first. Each entry: id, date, decision, owner, review condition.

---

## D-001 — Licence: MIT (lead confirmation requested)

- **Date:** 2026-10-31
- **Context:** the repository is meant for GitHub and contains original code
  plus scripts. It does **not** include Synergy binaries, the webOS SDK or
  other proprietary material, so MIT only governs this project's original
  code.
- **Decision:** [MIT](../LICENSE), copyright Ryan Spice, year 2026.
- **Flag for the lead:** confirm MIT is acceptable. Alternatives if the brand
  or a future hosted service needs stronger patent language (Apache-2.0) or a
  copyleft choice (GPL-3.0). Changing the licence later is easy while no
  releases exist, harder after external contributions.
- **Owner:** lead.

## D-002 — Repository working title `synergy-tv` until brand is final

- **Date:** 2026-10-31
- **Context:** the branded name and app ID are owned by the app workstream
  (`app/BRAND.md`); the lead finalises the repo name.
- **Decision:** README title and docs say "synergy-tv (working title)" and
  point to `app/BRAND.md`. `synergy-tv` also collides conceptually with the
  vendor's "Synergy" trademark (see
  [`brand-and-licensing.md`](brand-and-licensing.md)), which is another reason
  to rename before publication.
- **Owner:** lead.
- **Review:** when `app/BRAND.md` is final. Rename checklist: README title,
  `docs/`, `.github/ISSUE_TEMPLATE/config.yml` URLs, app IDs and icon
  metadata, then the GitHub repository name and remote.

## D-003 — Do not use SvelteKit for the repository tooling; app toolchain is the app workstream's call

- **Date:** 2026-10-31
- **Context:** the standard prefers current stable SvelteKit 3.x for
  *SvelteKit applications*, TS and Svelte 5, static output where possible.
  This workstream produces only Markdown, YAML, a `.gitignore` and two small
  PowerShell scripts, so no Svelte toolchain applies here.
- **Decision:** repository tooling is plain PowerShell 7 (Windows) and YAML,
  no dependencies. The TV app's toolchain (Svelte 5 + TypeScript static build
  vs vanilla) is the app workstream's decision; if the TV Chromium engine
  cannot support it, that workstream records a dated exception here with the
  exact version, blocker and next gate.
- **Owner:** repo workstream; app exception owner = app workstream.

## D-004 — CI build job is conditional until the build pipeline lands

- **Date:** 2026-10-31
- **Context:** the build workstream (`scripts/build/`, `tests/`) has not yet
  produced entry points. A CI job calling a missing script would fail the repo
  from day one.
- **Decision:** `.github/workflows/ci.yml` always runs repository hygiene and
  docs-link checks. The `build-test` job runs only when
  `scripts/build/build.ps1` exists or a root manifest with a lockfile exists;
  then it runs on `windows-latest`. It is skipped — not faked or hidden — until
  the pipeline exists.
- **Owner:** repo workstream; the build workstream updates the commands if it
  chooses a different entry point.

## D-005 — Root GitHub location, no remote created

- **Date:** 2026-10-31
- **Context:** the parent drive and dev folders are not repositories. `synergy-tv`
  must be its own repository, not a subfolder of one. (The enclosing drive root
  does contain an unrelated bare-ish repository; it is not used or touched.)
- **Decision:** a fresh `git init -b main` in this folder. No remote, no push,
  no credentials used, no GitHub repository created (publication requires
  explicit authorisation). Verification: `git rev-parse --show-toplevel` returns
  this folder.
- **Owner:** lead (push).

## D-006 — Default branch `main`, conventional but short commit style

- **Date:** 2026-10-31
- **Decision:** branch `main`; commit subjects are imperative and ≤ 72
  characters, with an optional body. See `CONTRIBUTING.md`.

## D-007 — App ID retention

- **Date:** 2026-10-31
- **Context:** the previously installed prototype used the app ID
  `dev.canopy.synergywebos`; changing it means the TV treats the new app as
  a fresh install.
- **Decision:** app ID ownership belongs to `app/BRAND.md`. The repository
  workstream records only that the app ID must match `appinfo.json`, icons and
  the client install paths; a rename requires an uninstall/migration note in the
  release notes.
- **Owner:** app workstream / lead.

## D-008 — Initial commit scope: repo-owned root files only

- **Date:** 2026-10-31
- **Context:** several workstreams write into the same new folder. Committing
  their partially-created files from this workstream would violate file
  ownership, and `BRIEF.md` belongs to the lead.
- **Decision:** the initial commit stages only the repository-owned paths
  listed in [`github-setup.md`](github-setup.md#initial-commit-contents). All
  other folders and files (`BRIEF.md`, `app/`, `dock/`, `tools/`,
  `scripts/home/`, `scripts/build/`, `tests/`) remain untracked in the working
  tree for the lead to commit after integration. Nothing is deleted.
- **Owner:** lead (integration/final commit).
- **Review:** at integration. The lead may squash the initial history before
  the first push, since nothing has been published.

## D-009 — Attribution email: no private address required for publication

- **Date:** 2026-10-31
- **Context:** the initial commit used the machine's existing Git identity
  (a personal address). Git history records the email, so a public repository
  would expose it; but GitHub itself offers the same concept for an account
  (`<id>+<username>@users.noreply.github.com`) and a private no-reply address.
  The GitHub account is confirmed as `github.com/ryanspice` (read-only `gh`
  check), personal user account, with 27 private repositories; no
  `synergy-tv` repository exists yet.
- **Decision:** no action is required from the user for publication. Options,
  in order of preference if the address should stay private:
  1. (no action) let GitHub display the account-linked no-reply address;
  2. enable **Settings → Emails → Keep my email addresses private** on GitHub;
  3. amend the initial commit's author/committer email to the no-reply address
     **before** the first push (it is the only commit).
- **Owner:** lead (publication authority).
- **Review:** before the first public push; a private repo needs no change.

## D-010 — Licence metadata and GitHub Terms remain an account-owner decision

- **Date:** 2026-10-31
- **Context:** GitHub's Terms require public repositories containing "software"
  to be licensed under an OSI-approved licence, and the license-picker
  acceptance is tied to the account owner. MIT satisfies the requirement. No
  licence metadata is set from this workstream (it needs a push).
- **Decision:** MIT is the declared project licence. At repository creation the
  lead confirms it (or changes it deliberately and updates `LICENSE`, this file
  and `THIRD_PARTY_NOTICES.md` together).
- **Owner:** lead.
- **Review:** at repository creation.

