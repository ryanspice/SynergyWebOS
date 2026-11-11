> **Legacy 0.3.4 reference.** The native 0.4.0 candidate is documented in the root README and `native/docs/`. Dock references below describe a retired component; no dock is included. Historical TV results do not validate the native rebuild.

# Home integration and cleanup handoff

## Client patch

`client-home.patch` is a unified diff against the old `synergy-webos/app-tv/client.py` (LF-normalized). Apply to that exact source in a disposable staging directory with `git apply --check client-home.patch` then `git apply client-home.patch`; adapt manually if the new app's client diverges. Do not apply directly to the live installation. Back up and hash the integration target first. Roll back the staged patch with `git apply --reverse client-home.patch` only if `git apply --reverse --check client-home.patch` succeeds and no later changes would be lost.

Both evdev Meta codes 125/126 are consumed for down/up/repeat, including when target is off. A first down invokes HOME once; repeats and simultaneous second Meta do not invoke again. `--home-target system|off` defaults to system; off suppresses both HOME and Meta. `--home-delay-ms 0..5000` defaults to 0. A nonzero delay requires holding either Meta until the delay expires; releasing all Meta keys, leaving the screen, disconnecting, or stopping cancels a pending action. This is a HOME hold delay, separate from Synergy screen-switch delay. HOME uses a Unix stream socket, a four-byte big-endian byte count and the exact ASCII payload `type:button\nname:HOME\n\n`. Send errors are logged; no automatic launch fallback is claimed. Dry-run logs instead of sending. An unknown Synergy key encoding will not be intercepted; verify actual physical Windows keys and protocol frames. Socket write success is not proof that HOME appeared.

App integration: persist target and integer delay in the app's own settings, validate as above, pass `--home-target` and `--home-delay-ms` as separate process arguments on the next client start. Label the delay as hold-to-activate, distinct from server handoff delay. Reflect save versus active values and require an explicit user-controlled client restart for changes; never silently restart the currently running client. Avoid offering arbitrary commands or an app ID as a home target. The patch is for integration only; no installed client was modified.

## Prototype cleanup: lead-controlled TV procedure

`cleanup.py` is intended for Python 3 running as root on the TV. It does not launch or restart anything. First transfer the script to a private temporary location via the lead's approved channel, then inspect installed index backups and dock files and select an actual pre-prototype `index.html.bak-*`. Record SHA-256 of the reviewed dock files; do not guess. The script compares the current index after removal of its narrowly recognized prototype markup byte-for-byte to that backup. It refuses unfamiliar bindings, missing files, symlinks, unmatched hashes, or differing index. It leaves all icons, other app files, data, and the normal app installation intact. A refusal means inspect and reconcile manually; do not bypass the checks.

Replace placeholders after review, with root execution on the TV:

```sh
APP=/media/developer/apps/usr/palm/applications/com.canopy.orbishome
sha256sum "$APP/src/dock.js" "$APP/src/dock.css"
# Review actual backups and diffs before selecting ORIGINAL.
ls -l "$APP"/index.html.bak-*
ORIGINAL="$APP/index.html.bak-REVIEWED"
RECEIPT=/home/root/home-cleanup-UNIQUE
python3 /tmp/home-cleanup.py plan --receipt "$RECEIPT" --original-index "$ORIGINAL" --dock-js-sha256 REVIEWED_JS_SHA256 --dock-css-sha256 REVIEWED_CSS_SHA256
python3 /tmp/home-cleanup.py apply --receipt "$RECEIPT" --original-index "$ORIGINAL" --dock-js-sha256 REVIEWED_JS_SHA256 --dock-css-sha256 REVIEWED_CSS_SHA256
python3 /tmp/home-cleanup.py rollback --receipt "$RECEIPT"
```

Run rollback only if needed. The private receipt is created exclusively and holds before-images and hashes outside the app/repository. It checks all post-apply hashes and backup hashes before restoring; later edits block rollback. Apply is not a multi-file transaction: on an interrupted or failed apply, preserve the receipt, inspect each target against its before/after hashes, and recover deliberately; the normal rollback refuses a mixed state. No user data or extra icon tree is deleted. The keybind file is atomically replaced, preserving its mode; unrelated JSON values are retained but formatting can change. Verify input-hook hot reload without restarting it. The installed index backup is retained, not copied into this repository.

## Evidence and acceptance gates

Source review: the old client maps 0xE05B/0xE05C to 125/126 and emits down/up/repeat through uinput; the prototype `home-key.py` defines the exact three keys and launch command. The old push script creates timestamped index backups. The brief reports the HOME framing worked previously; this task did not independently exercise it on the TV.

Local checks: patch applies to an LF-normalized copy of the old client; both resulting client and cleanup script compile. A mocked test passed left Meta down/repeat/up, right Meta in off mode, no backend Meta events, and exact framed HOME bytes. Initial Windows test required an AF_UNIX stub; no TV socket was contacted. Cleanup has not been exercised against actual installed files or a fixture. Index recognition is intentionally conservative and may refuse the actual backup due to whitespace or other changes. File ownership beyond permission mode and Input Hook concurrent edits require attention; run during a controlled maintenance window and inspect the receipt and resulting files. If interrupted after some writes, use the manual recovery procedure above.

Lead must test with a temporary new client and no interference with the current connection: normal keys, both Windows keys, repeats, simultaneous keys, off mode, short and long holds, disconnect/COUT cancellation, inaccessible socket, and dry-run. On physical TV, confirm real system HOME, no Orbis launch or stray Meta, other hook bindings and Orbis app/user data intact, and rollback on a disposable fixture. Separate runtime, visual, and human acceptance from local mocked evidence. Do not deploy or clean up while the link is in use.
