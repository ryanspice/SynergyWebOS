# SynergyWebOS agent entry point

Working contract: one accountable owner per task; scoped approvals (stage/commit/push/credentials/publication need separate scope); discovery grants no authority; smallest-sufficient checks; preserve first failures; preserve unrelated dirty/untracked work.

Storage: project-local `.ai/` is the default work-product location (inbox/outbox/evidence/demos/examples/archive). No drive-level Temp/Downloads or other projects' folders. Native harness configuration and credentials stay tool-managed.

Find the relevant detail before acting:

| Task | Read first |
|---|---|
| Build/package | `docs/building.md`, `scripts/build/README.md` |
| Tests/verification | `TESTING.md`, `docs/testing.md` |
| Repo hygiene/policy | `POLICY.md`, `docs/repo-policy.md` |
| Operations/live apply | `OPERATIONS.md`, `docs/handoff.md` |
| App/client changes | `app/README.md`, `app/INTEGRATION.md`, `app/BRAND.md` |
| Brand/identity | `app/BRAND.md` |
| Decisions/exceptions | `docs/decisions.md` |

Detailed constraints live in the referenced files. Source code and current evidence win over old scans and proposed plans. No secrets, backups, proprietary bundles or personal data in the repo. Do not restart the live Synergy core/service or redeploy the live client without maintainer approval.