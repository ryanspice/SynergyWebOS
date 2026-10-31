# Junction client and control contract

These files are adapted from the previous working TV client. The new app keeps
the proven Synergy 1.x wire protocol and uinput approach, adds a clean home-key
path and rebrands runtime files. No third-party Python packages are used.

## Files

| File | Purpose |
| --- | --- |
| `client.py` | Synergy 1.x client; uinput keyboard/mouse; consumes Meta and optionally sends TV HOME |
| `tvctl.sh` | root control surface: start/stop/status/snapshot/logs/probe/autostart/profile/server read |
| `probe.py` | read-only root, Python, uinput and HOME-socket capability report |

## Runtime paths

| Purpose | Path |
| --- | --- |
| log | `/var/lib/webosbrew/junction.log` |
| PID | `/var/lib/webosbrew/junction.pid` |
| state | `/var/lib/webosbrew/junction-state.json` |
| saved client config | `/var/lib/webosbrew/junction.conf` |
| autostart hook | `/var/lib/webosbrew/init.d/60-junction` |
| handoff profile | `/var/lib/webosbrew/junction/handoff-profile.json` |

`tvctl.sh` reads the previous `synergy-webos.conf` only when
`junction.conf` is absent. Enabling autostart quarantines the previous generated
hook (`60-synergy-webos` → `60-synergy-webos.disabled-<stamp>`) before writing
the new hook. Disabling autostart removes both. This is user-initiated runtime
state, never an install-time edit.

## Commands

```text
tvctl.sh probe
tvctl.sh start SERVER [PORT] [NAME] [real|dry] [WIDTH] [HEIGHT] [HOME_TARGET] [HOME_MS]
tvctl.sh restart SERVER [PORT] [NAME] [real|dry] [WIDTH] [HEIGHT] [HOME_TARGET] [HOME_MS]
tvctl.sh stop
tvctl.sh status
tvctl.sh snapshot
tvctl.sh logs [LINES]
tvctl.sh configure SERVER [...]      # save config only
tvctl.sh autostart-enable SERVER [...]
tvctl.sh autostart-disable
tvctl.sh autostart-status
tvctl.sh start-from-config
tvctl.sh profile-save JSON
tvctl.sh server-status HOST [PORT]  # read-only GET /v1/settings, default port 24803
tvctl.sh version
```

`snapshot` is the app's polling surface. Its section markers and first-line
formats are a contract; keep them stable if the script is refactored:

```text
===STATE===
running pid=1234
{"state":"connected","pid":1234,...}
===AUTOSTART===
enabled
===CONFIG===
home_target=system
home_delay_ms=0
===LOG===
<up to 120 lines>
```

`status` is also safe to call directly. A stopped/error status is reported on
stdout and by exit code; the app treats the text as authoritative.

## Windows/Meta key contract

- The client consumes **both** evdev Meta codes, `125` (`KEY_LEFTMETA`) and
  `126` (`KEY_RIGHTMETA`), on down, repeat and up. No Meta event is injected.
- `--home-target system` launches the TV's system Home app
  (`com.webos.app.home`) through `luna-send`. `--home-target off` silently
  consumes the key. There is no configurable third-party app target.
- `--home-delay-ms` is a **hold-to-activate** delay, 0–5000 ms. 0 launches Home
  on the first down. A non-zero delay launches only if at least one Meta key is
  still held when the timer expires; release, pointer exit (`COUT`),
  disconnect and stop cancel a pending action.
- The launch is requested with
  `luna-send -n 1 -f luna://com.webos.applicationManager/launch
  '{"id":"com.webos.app.home"}'`. It runs on a short-lived daemon thread so a
  slow service call cannot stall pointer input. Exit 0 is not proof that the
  home screen appeared; the lead must verify on the TV.
- Dry-run logs `DRY RUN: HOME action` instead of launching. A failed launch is
  logged, not silently retried or redirected to a third-party app.
- The delay is separate from the Synergy server's switch delay. UI copy and
  storage names keep them separate (`homeDelayMs` vs `switchDelayMs`).

## Alt-hold escape hatch

- Holding either Alt key continuously for five seconds launches the same system
  Home app. The timer is client-side; release before five seconds cancels it.
- Alt is injected normally for shorter holds and key combinations. When the
  five-second threshold fires, the injected Alt is released before Home takes
  focus, and the later physical key-up is swallowed so it cannot produce a
  second release.
- Pointer exit, disconnect and stop cancel the pending hold. Key repeat does not
  restart it. Dry-run logs the action instead of launching.
- The Alt hold is independent of the Windows-key target. `--home-target off`
  still consumes Windows/Super keys, but the Alt escape hatch remains available.

Terra 3's patch was written against the old client; this file folds in the same
behaviour with the arguments above. The app passes them as separate process
arguments. Do not apply the old patch on top of this file without reconciling
it.

## Start and stop behaviour

- `start` validates host/port/name/dimensions/home target/delay, ensures the
  uinput node when real mode is requested, reaps duplicate client processes and
  refuses to start a second client if one is already running.
- A running client is **not** silently restarted. The app calls `restart` only
  from an explicit user action and shows the active home settings.
- `stop` terminates only processes whose command line contains this app's
  `client.py`, waits up to 3 seconds, then escalates.
- `snapshot` calculates the active home target/delay from the running client's
  command line; saved config is the fallback. That is why Settings can show
  "Active link" separately from "Saved values".

## Autostart

`autostart-enable` persists the same values as the app form and writes:

```sh
#!/bin/sh
sleep 8
sh "/path/to/app/tvctl.sh" start-from-config >> "/var/lib/webosbrew/junction.log" 2>&1 || true
```

It runs through the Homebrew Channel startup hook. It does not start now, does
not restart a current link, and does not modify the installed app files. A
legacy hook is quarantined by rename before the new hook is written.

## Security and limits

- Classic unencrypted Synergy TCP only; use it on a trusted LAN and never expose
  port 24800. Clipboard sync and TLS are out of scope.
- Real input requires root and a writable `/dev/uinput`. Probe is read-only: it
  opens and closes uinput without creating a device or injecting an event.
- Every shell argument is single-quoted by the app; `tvctl.sh` still validates
  every host, port, screen name, dimension, home target and delay.
- `server-status` only performs a GET. It does not write settings, regenerate
  config or apply the handoff profile.
- The client has no network listener, no cloud call, no telemetry and no Node
  dependency.

## Local checks

```powershell
python -m py_compile app\client\client.py app\client\probe.py
# On a POSIX/TV shell:
sh -n app\client\tvctl.sh
```

The Python files cannot be imported on Windows as-is because they use `fcntl`
and the Linux input API; compile checks and the mocked HOME test are the
appropriate local gates. On-TV acceptance remains: both Windows keys, repeats,
simultaneous keys, off mode, hold timing, pointer exit cancellation, missing
socket, system HOME, and no stray Meta.
