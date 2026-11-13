# Native release procedure (0.4.0 and later)

The legacy `docs/release.md` describes the retired 0.3.4 web pipeline. **Do not
use it to publish a native build.** The native procedure is below; publication is
owner-gated and requires the TV gates in `docs/native-tv-runbook.md` and
`docs/native-resynthesis-0.4.0.md`.

## Versioning and identity

- Package ID: `dev.canopy.synergywebos`.
- Candidate version: `0.4.1` (source of truth: `native/appinfo.json` and
  `native/CMakeLists.txt`).
- A native version may be published only from the exact frozen bytes that passed the TV
  gates. Test IPKs before acceptance are internal; record their SHA-256 and
  bump the patch or fully uninstall between iterations.
- `1.0` is a later stability claim: published 0.4.0, owner feedback, and the
  soak budgets closed with no unrecovered failures.
- The legacy web app remains `0.3.4` rollback/migration source and is **never**
  packaged under the 0.4.0 name or published as a release.

## Source tags and draft preparation

Source integration, an annotated `v0.4.1` tag and a **draft** GitHub Release can
record the candidate before physical acceptance. Attach exact checksummed
candidate assets privately to that draft. Keep its candidate status explicit.
The stable publication gates below still apply; a tag alone does not satisfy
them. Draft apps-repo and awesome-webos PRs may be prepared for review with their
unmet TV/public-asset requirements stated and unchecked. Do not merge those
submissions or publish stable binaries until their own gates pass.

The 0.4.0 pointer baseline is described in
[TV execution](../native/docs/TV-EXECUTION.md). New 0.4.1 evidence and capability
gaps are in [candidate verification](../native/docs/INPUT-0.4.1.md).

## Build and artifact

1. Build the ARM32 target with the verified SDK per `native/docs/BUILD.md`.
2. Run `native/scripts/audit-stage.py` and review the dependency/symbol report.
3. Package the stage with `ares-package`; inspect `ares-package -i` and the
   executable modes.
4. Record the IPK SHA-256 and size; attach the checksum to the release notes.
5. Keep the source handoff/checksum manifest as handoff-only; do not imply that
   a clean clone reproduces the ignored `native/bin` or `native/verification`
   artifacts.

## Required gates before publication

- Native CI green on the release commit (Release + CTest + ASan/UBSan).
- TV session 1 and 2 gates passed on the exact bytes.
- Cold boot with autostart; no dual input daemons; rollback drill passed.
- UID/jail socket evidence, controller UID recorded, audit/rotation path tested.
- Verification wording honest: synthetic events, root/non-root counts, host-only
  scope (`native/docs/VERIFICATION-AUDIT.md`).
- D-012 migration note included.

## D-012 migration note (required text)

> The pre-rename 0.3.4 package was `dev.hurrspice.synergywebos`. Native 0.4.0 is
> `dev.canopy.synergywebos`; on a TV that still has the earlier install it
> appears as a **second app**. Stop and disable the old client and its autostart
> before the first native boot; do not run two input daemons. To roll back,
> reinstall the retained 0.3.4 package and restore its reviewed startup route.
> Legacy settings are not imported automatically.

## Publication

Only the owner authorizes a release. Publish the IPK and checksum as GitHub
Release assets; never commit the IPK. Do not open or update an apps-repo entry
for the legacy web app or the removed dock. A ready-to-merge native store/apps-repo submission
requires TV acceptance; candidate draft preparation is described above.

## Rollback

- For a 0.4.1 trial, retain and restore the accepted 0.4.0 IPK **and** its
  protected daemon/runtime configuration together. An app downgrade alone
  does not downgrade the separately provisioned daemon. Keep pairing material
  private and disable the bridge when restoring the classic baseline.
- Reinstall the retained 0.3.4 package and restore its startup route.
- Stop the native daemon and disable the native boot hook first.
- Restore the Synergy server config only from the owner's backup; do not rewrite
  it automatically.
