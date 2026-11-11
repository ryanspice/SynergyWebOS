# Spike: a Synergy entry in LG's gear-button settings

**Status: researched + read-only probe implemented. Not installed, not TV-verified.**
Research date: November 11, 2026. Target is the owner's LG C3; the recovered
SkiFree release records firmware 43.21.84 / webOS 11.2.0. That is a historical
reference, not a fresh firmware/root measurement for this task.

## Decision

The requested destination is **LG's own Quick Settings / All Settings UI**,
not a new dock, overlay launcher or a replacement for the gear button. The first
useful integration would be one item:

> Keyboard & mouse sharing → open SynergyWebOS

It should open the native configuration app without starting an input session,
changing root privileges, or toggling plaintext consent. Keep ordinary gear
presses, LG's existing settings, Back, and the firmware's held-gear behavior intact.
The user has explicitly removed the dock; this spike must not reintroduce it.

**No documented public TV extension point was found for registering this item.**
LG's published TV Settings Service offers `getSystemSettings`, while its app
metadata reference does not document a settings-category/menu-row registration
property. The documented Application Manager launch method can open our app;
that does not give us an insertion point inside LG's panel. This is a finding
about the public documentation reviewed, not proof that no private OEM extension
exists. [S1–S3]

Treat genuine menu insertion as a **rooted, exact-firmware integration** until
an actual extension point is observed. Do not equate root access with knowledge
of that firmware's UI model. Do not infer a retail-TV plug-in API from webOS OSE
settings-description fields or Android Quick Settings tiles.

## Options and stop conditions

| Route | What it would deliver | Decision |
|---|---|---|
| Public app metadata/Luna API | Ordinary app launch; no documented LG menu registration | Not sufficient for the requested menu item |
| Discover a private declarative menu/provider registry | A real LG settings row without replacing the panel | Preferred research lead; no such registry verified yet |
| Fingerprinted UI-resource override | A new row in the existing panel, if its menu data/source is editable | Conditional candidate only after target inspection and rollback proof |
| Replace/remap the gear key | Opens Synergy instead of LG settings | Not the requested behavior; do not implement |
| Separate long-press/remote shortcut | Alternative access to the app, not an LG settings row | Fallback only after a separate owner decision; preserve stock gear behavior |
| Permanent firmware edits / binary injection / replacing the entire settings app | Broad fragile system alteration | Out of scope for this spike |

Rooted webOS can apply runtime bind mounts/overlays to read-only system paths;
webOSbrew warns that broad writable overlays can become inconsistent with
firmware updates. That supplies a possible mechanism, **not evidence that the
settings UI consumes an editable menu file**. [S4]

Do not assume an old Qt/QML patch works on this C3. The Glasshouse project's
primary-source implementation documents a C5/webOS 11.2.0 with native key-filter
libraries and no old `/usr/lib/qt5/qml/KeyFilters/` directory. This is relevant
counter-evidence to a universal QML recipe, not a measurement of the C3. [S5]

LG Input Hook can remap buttons with rooted Homebrew Channel. It does not by
itself add a row to LG's settings menu. Do not quietly substitute remapping for
menu insertion. [S6]

## Included executable spike: read-only reconnaissance

`probe-tv.sh` performs five bounded **read** calls: system model/version, focused
window, foreground window, foreground application, and the two candidate
key-filter config names. It lists/fingerprints a capped set of candidate UI
files under fixed system settings directories. Candidate paths and methods may
be absent/denied; failures are recorded, not worked around with permission edits.

`collect.py` sends that fixed script over an **existing authorized SSH alias**
and saves filtered JSON locally. The TV needs no Python for this probe. Host
Python 3.10+ and OpenSSH are sufficient. Host-key checks stay enabled, batch
mode prevents password prompts, and existing evidence is never overwritten.
It makes no TV file writes, uploads, app launches, key presses, settings changes,
mounts, restarts, package installs or boot-hook edits. Luna reads may themselves
be logged by the system; "read-only" does not mean zero process/log activity.

File contents, LG source, account databases, launch parameters, serial numbers,
and service tokens are not exported. Result files still disclose firmware,
system paths and foreground app IDs; review before sharing and keep them out
of the public README. Inventory limits mean absence in a report is **not proof
of absence from the TV**. The wrapper is not a generic root command interface.

From the extracted repository in PowerShell 7:

```powershell
New-Item -ItemType Directory -Force .\.ai\evidence\gear-settings | Out-Null

# Replace lg-root with your EXISTING authorized SSH alias. No new access is set up.
.\spikes\gear-settings\Collect-SettingsProbe.ps1 -SshTarget lg-root `
  -Phase baseline -OutFile .\.ai\evidence\gear-settings\baseline.json

# Press the physical gear button yourself; leave Quick Settings open.
.\spikes\gear-settings\Collect-SettingsProbe.ps1 -SshTarget lg-root `
  -Phase gear -OutFile .\.ai\evidence\gear-settings\gear.json

# Enter LG's All Settings yourself; leave that UI open.
.\spikes\gear-settings\Collect-SettingsProbe.ps1 -SshTarget lg-root `
  -Phase all-settings -OutFile .\.ai\evidence\gear-settings\all-settings.json
```

The same collector runs directly with `python spikes/gear-settings/collect.py
--host lg-root --phase gear --out NEW_LOCAL_FILE.json`.
No probe has been run on the owner's TV in this handoff. The PowerShell wrapper
was reviewed but not executed here; Python parser/SSH-construction and shell
syntax tests were executed with fixtures, without contacting a TV.

## Evidence to resolve before writing a patch

Compare the three captures. Use focused-window and foreground-window data, not
just the underlying foreground-app result: an overlay may leave the underlying
HDMI/app identity unchanged. Treat `com.webos.app.quicksettings`,
`com.palm.app.settings`, and `com.webos.app.settings` as discovery leads, not
universal IDs. Check the observed app's manifest and actual loaded UI resources
locally. Determine whether the menu comes from QML/JavaScript, a declarative
registry, compiled Flutter/native resources, or another provider.

Inspect only the measured files for a menu model, item factory, permission
checks and launch handler. Do not read or bulk-export entire private databases.
If a real provider registry exists, validate its schema and authorization before
proposing registration. If the implementation is compiled or the data model is
unclear, stop at a documented blocker instead of shipping a guessed file patch.

For a launch-only row the intended call is LG's documented launch method [S3]:

```json
{
  "id": "dev.canopy.synergywebos",
  "params": {}
}
```

Destination: `luna://com.webos.applicationManager/launch`. This payload is a
proposed row action, **not an already-installed menu item**. It opens the
existing Connection/settings app; no new deep-link parameter is claimed.
Cold launch and reactivation while already open must both be tested on the real
native runtime before calling the integration complete.

## Conditional implementation contract

Only after the insertion point is proven, create a separate opt-in adapter,
not code coupled to the Synergy daemon. Its compatibility key must include the
observed firmware, target file paths and full SHA-256s. Refuse unknown hashes,
foreign existing mounts, missing originals and changed schemas. Prepare exact
original backups and an uninstall path before any runtime change.

Prefer the smallest transient resource override. First test with persistence
**off**, and without restarting the compositor, SAM or the entire TV shell.
If a verified safe reload of just the settings surface is unavailable, stop and
review the next action. Do not kill display/services as a speculative refresh.
The adapter must restore LG's menu when Synergy is removed, and stay disabled
following a firmware/hash change. Persistence is a later, separately approved
step after rollback and cold-boot/Quick Start+ tests.

Acceptance: stock gear and held-gear behavior still works; exactly one Synergy
row appears; its activation opens the native app; Back returns predictably;
ordinary LG settings, accessibility focus/labels and game/HDMI modes work;
uninstall restores originals; firmware mismatch produces no modification.
An eventual status/toggle row would need a narrow non-root status interface;
never put arbitrary shell execution or automatic real-input enablement in LG's
settings process.

## Verification and recommendation

Eleven local tests exercise redaction, framing/size bounds, rejected malformed
or incomplete probes, fixed SSH argv/stdin, preserved host-key checks,
no-overwrite behavior, read-method allowlisting, and shell syntax. These prove
properties of the collector, not compatibility of private TV services.

**Proceed with the three read-only captures. Hold menu modification until the
C3's actual panel implementation and a rollback-safe insertion point are known.**
A settings shortcut in Spice Home's own UI would be a separate, much smaller
feature; it is not equivalent to this LG gear-menu request.

## Sources reviewed

- **S1 — LG TV Settings Service:** https://webostv.developer.lge.com/develop/references/settings-service
- **S2 — LG TV appinfo.json:** https://webostv.developer.lge.com/develop/references/appinfo-json
- **S3 — LG TV Application Manager:** https://webostv.developer.lge.com/develop/references/application-manager
- **S4 — webOSbrew filesystem overlays:** https://www.webosbrew.org/pages/filesystem-overlays
- **S5 — Glasshouse native/legacy key-filter implementation, pinned commit:** https://github.com/rorygallagher2024/glasshouse/blob/f24cb7ec67fe0621432ae60345a2e3f9e14221dd/server/lib/remotekeys.js
- **S6 — LG Input Hook documentation:** https://repo.webosbrew.org/apps/org.webosbrew.inputhook/

The LG references establish public behavior; community project code supplies
implementation-specific leads. None establishes a supported third-party menu
registration mechanism or proves target-TV behavior for this rebuild.
