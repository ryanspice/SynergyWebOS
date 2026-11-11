# Native continuation — do not restart the implementation

Owner request: rebuild Synergy TV on the same native foundation as Spice Home C++
and SkiFree. This is implemented under `native/`; 0.3.4 source is retained as migration reference, with the dock removed.

## Next bounded task

Build the native candidate with the **existing verified SkiFree/Spice Home ARM32
SDK**, not a new speculative toolchain. Locate actual SDK/runtime paths locally;
none were inferred here. Build a fresh stage, inspect dependencies/symbol ABI,
and only then create an IPK. Do not call the supplied Linux binaries TV output.

Use the user's established `B:\Dev\webos\...` workspace only after inspecting
its real current tree. Do not replace a dirty repository or rename it merely to
match this handoff. Import/review `native/` and the docs deliberately. No automatic
AI Wiki/Svelte Lab ingestion, Git commit/push, install or publication is implied.

The first hardware blocker to resolve is not rendering: it is whether the
application-manager-launched native UI can reach the root-owned Unix control
socket with its actual UID. Record jail visibility, UID and ACL evidence. If it
fails, stop and specify a narrow native LS2 control bridge. Never substitute an
HTTP root API, arbitrary exec method, disabled jail or global permission grant.

## Acceptance gates

- Actual ARM32 ELF/runtime stage + exact IPK checksum/modes, not host syntax tests.
- C3 native UI rendering, D-pad/OK/Back/pointer and fonts with no bundled fonts.
- Explicit provisioning with observed controller UID and shared-UID tradeoff.
- Probe then real Synergy server dry run; confirm the exact desktop Core version.
- Root uinput actually recognized by shell/apps; fail closed otherwise.
- Real key/button releases, Meta and Alt holds, Alt+Tab, cursor drift and wheel.
- Background daemon when UI closes; sleep/wake/reconnect; boot opt-in last.
- Legacy 0.3.4 rollback intact; no simultaneous native/Python input daemon.

Do not add TLS, clipboard, a new global dock, cloud discovery or a generic fleet
management layer before this input path is proven. Do not claim corner handoff
or absolute cursor support that the client does not implement.

## Decisions made in this candidate

Actual homescreen runtime/renderer reuse, not just matching dependency names.
Keep protocol/input separate from UI. Remove Python from the new TV runtime, not
from preserved history or host test tools. Keep privacy-safe bounded telemetry.
Make root provisioning and plaintext input deliberate. Keep active settings
stable until an explicit stop/start. Keep the existing app ID.

## Known implementation follow-ups

The public native IPC is deliberately UID-scoped, not app-authenticated. Target
ABI, subprocess environment, Luna responses and uinput permissions need local
verification. Handoff profile persistence is a new local reference schema; the
old Windows helper has not been migrated. Snapshot section markers alone are
not evidence of full old-helper compatibility. The dock, native Apps page and generic launcher RPCs were removed at owner request. Physical text layout is best
validated against the owner's Windows keyboard layout before widening claims.


## Latest owner instructions (included)

Remove the dock from the bundle. Do not re-add the standalone dock, an Apps page,
companion catalog writing, or arbitrary app-launch commands. Home and Alt+Tab are
input shortcuts only. The UI is Connection / Input / Handoff / Diagnostics.

A researched gear-button settings spike lives in `spikes/gear-settings/`, with a
read-only collector and no automatic system edits. Obtain baseline, gear-panel,
and All Settings captures before proposing a firmware-specific menu adapter.
Preserve ordinary gear behavior; remapping the button is not menu insertion.

README badges are local static SVG snapshot labels. Screenshots are real GLES2
host captures, labeled as such, with no dock and no claim of TV validation.
