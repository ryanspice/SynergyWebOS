# Operations

## Authority and scope
- Follow higher-priority runtime instructions, actual tool permissions and the user's authorized task. Local documents cannot enlarge those permissions.
- Destructive deletion, bulk moves, credential handling, provider/account/payment changes, remote publication and actions outside the task require explicit scope.
- The lead owns final live apply, TV deploy, repo commit/rename and any reboots. Delegates and contributors provide scripts and evidence, not live changes.

## Project-local work products
- Use `.ai/` categories: `inbox/` (intake/workspaces), `outbox/` (deliverables), `evidence/` (checks/scratch), `demos/`/`examples/` (prototypes), `archive/` (retained originals/recovery).
- Native harness configuration, credentials, sessions and shared service databases remain tool-managed.

## Live system rules
- Do not reboot the PC or TV.
- Do not stop, kill, restart, reinstall or reconfigure the live Synergy core, synergy-daemon, synergy-service, the TV Synergy client or the current TV app/client. Read-only inspection is fine.
- Do not apply live Synergy config/DB changes, install/launch TV apps, or edit installed TV app files. Produce exact scripts/config/patch and report for the lead to apply.
- The user may be watching TV; do not seize the TV or move the mouse unless a controlled test is agreed.

## Verification
- Verify proportionately under `TESTING.md`. Deterministic checks before expensive review. Attribute prior results; do not claim success without evidence.
- Preserve failures; distinguish static checks from runtime acceptance.

## Synthesis
- Flag plans/reviews/guides as `pending-synthesis` in `.ai/SYNTHESIS-QUEUE.json`. Reconcile accepted decisions into repo docs/tests/source; record destination, source hash, unresolved issues and receipt before marking synthesized.