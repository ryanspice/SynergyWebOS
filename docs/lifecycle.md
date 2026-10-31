# Artifact lifecycle — receipts for this rebuild

Status: **read-only inventory; no disposal executed.** Per the AI Wiki
artifact lifecycle policy, an inventory is not a disposal permit and this
repository does not delete, clean or move anything outside its own folder.

## Decision contract

`Disposable = (preserved OR legitimately rejected) AND retention satisfied AND authorized`

Unknown, missing, stale or conflicting evidence means **HOLD**.

## Inventory (read-only, scoped)

The old prototype workspace and the third-party launcher source copy are
identified by their role; exact paths stay in the internal working brief and
are deliberately not recorded here.

| Candidate | Class | Observation | Disposition | Reason |
| --- | --- | --- | --- | --- |
| Old prototype workspace (sibling working area) | Working/evidence space | Client/app prototype copy, config+settings backup snapshots, helper tools, reference material, local captures, notes | **HOLD (preserved)** | Active evidence and rebuild reference; a sync source for `app/` and `tools/synergy/`. Ownership and activity not established for disposal. |
| Third-party launcher source copy (sibling working area) | Third-party code | Reference only for the old dock prototype | **HOLD (preserved)** | User-owned working area; not needed by this repo; proprietary/third-party code. |
| Other sibling folders in the parent workspace | Unrelated workspace material | Outside this task's scope | **HOLD (not inventoried for disposal)** | Out of scope; not copied. |
| This repository | New repository | Created by the rebuild | Owned by the rebuild | Only this repo's own tracked files are committed. |

No hashes, elapsed-age calculations or Git history were used to judge
disposability: a clean status or age is insufficient evidence.

## Preservation measures taken by the rebuild

- The old workspaces are untouched by this repository. Nothing was copied from
  their backup folders and no secrets were copied from any old area.
- The new repo's ignore rules treat backups, Synergy config/database snapshots,
  device credentials and license data as non-committable
  ([`repo-policy.md`](repo-policy.md)).

## Delegate outputs inside the new repo

Workstream folders (`app/`, `dock/`, `tools/synergy/`, `scripts/home/`,
`scripts/build/`, `tests/`) are owned by their delegates. File ownership in the
internal brief is enforced by convention: this repository workstream does not
edit or remove another delegate's files. Files a delegate has not yet created
are treated as pending, not as disposable.

## Future disposal (not performed)

Disposing of any HOLD candidate would require a separately authorised,
freshly revalidated execution under the lifecycle's executor requirements
(phase two), with a recorded receipt. Nothing in this repo performs that.
