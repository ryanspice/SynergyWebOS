# Synergy server handoff research — 2026-10-31

## Status and acceptance

**Live config is a top-edge 2px strip, not a true 2×2 square.** The earlier
`left(0,0.0925925926)` link was removed after a live bug: it entered the TV on its
right edge, so ordinary rightward mouse motion immediately returned to the PC and
the TV appeared to have no cursor. The live config now uses only
`up(0,0.0520833333)` to enter the TV and `right(70,100)` to return. It was applied
through `POST /v1/custom/settings` with a protected before-image and tested at
the physical PC.

At 3840×2160, 2/3840×100 = 0.0520833333%. The exact pixel rounding, Windows
virtual-desktop coordinates, display scaling, fast pointer motion, and top-left
corner arbitration remain user-acceptance details. `switchCorners` denotes
disabled/dead corners, not a positive hot-corner selector; current `none` leaves
corners enabled. `switchCornerSize` is not a 2px activation selector.

## Evidence

- Read-only GET /v1/settings: service v3.0.79.1-rc3, custom override enabled, server screen `spicepc-fc6c0218`, delay 250 ms, relative mouse enabled. Computer list contains Spice-PC, Spice-Book, Work-PC, no TV. Current custom config uses `up(0,15)` and `switchCorners = none`.
- `validate-parser.ps1` started an isolated core on loopback port 24999 for each case, stopped only its own process, and cleaned its private temp directory. `0,0.0520833333`, `0,0.1`, `0,1` started; `0,0` failed with invalid interval. The complete `candidate.conf` also started on 24999. This demonstrates parser/startup acceptance, not precision, reachability, input behavior, or a minimum reliable interval. The isolated instances logged failure to register the already-owned ScrollLock hotkey; no live process was stopped. Starting a second core still opens the local screen and registers hooks: run this validation only in an agreed quiet window.
- The installed Electron bundle's custom-config UI derives a screen name by stripping non-alphanumerics from a computer's display name, lowercasing it, and appending `-` plus the final eight ID characters. The custom name field is an override. GUI layout sends `PATCH settings/computers/batch`; adding a peer sends `POST controls/add-peer` with an IP array; neither establishes a supported way to create an arbitrary non-Synergy TV computer. No such operation was tested. A TV represented only in the custom config is not a normal movable GUI computer. A fake DB entry or direct DB write is unvalidated, may sync or be overwritten, and is not supplied as an apply script. A true Synergy peer's identity and backend admission would need independent proof; if added, the TV client name must exactly match its generated screen name or use a verified alias. Moving GUI tiles while custom override remains active does not establish that the custom links are regenerated.
- The installed UI sends `PATCH /v1/settings/developers` with `{"switchDelay": value}` and custom UI sends `POST /v1/custom/settings` with `configFile`, `computerName`, `serverName`, `serverIP`, `elevate_privileges`, `args`; enabling override sends `{"shouldOverride": true}`. These are static UI observations, not safe apply instructions: effects, partial updates, authentication, and restart timing were not tested. No live POST/PATCH or DB edit was made. `inspect-settings.ps1` prints only selected non-sensitive fields.

## Delay, slowdown, and override

The candidate sets `switchDelay = 0` for immediate edge switching; change that option to a nonnegative millisecond value in the text override to request a delay. The GUI developer setting has its own `developers.switchDelay`; the observed override contains a separate explicit value, so do not assume changing the GUI setting changes the active custom config. Symless describes the delay as time resting against an edge before crossing, with 0 immediate: https://support.symless.com/hc/en-us/articles/51545644314769-Miscellaneous-settings-in-Synergy-3-switching-delays-meta-key-port-and-UAC . A 250 ms edge hold is a plausible cause of perceived edge resistance; no measurement shows general corner pointer slowdown or identifies relative mouse/polling as its cause. Relative mouse is preserved, not diagnosed. Delay is global for linked edges; per-corner delay and a temporary hotkey override were not demonstrated. `shouldOverride` selects the entire custom config, not a per-corner override.

## Lead controlled apply and test

1. Before changes, inspect fresh state with `powershell -File tools/synergy/inspect-settings.ps1`, check the exact screen bounds and scaling, and record current configuration and hashes in a protected recovery location outside the repo. Compare the candidate against the then-current config and preserve other live changes; this snapshot can go stale. Coordinate a quiet test and retain independent keyboard/mouse recovery.
2. Run `powershell -File tools/synergy/validate-parser.ps1` only after confirming 24999 is free and local screen hooks will not disturb active work. It tests representative syntax, not the candidate or TV. The full candidate was separately started once with `synergy-core.exe server --address 127.0.0.1:24999 --config <candidate.conf> --name spicepc-fc6c0218 --no-restart --no-tray` and stopped by its own PID.
3. Review and apply through the existing Synergy custom-config UI under lead authority, retaining a protected before-image. Do not blindly replace a changed live config or database. The candidate uses a zero delay and simplified default screen settings: compare every option before use. Confirm exact actual running config and client connection after any controlled reconnect; do not infer them from UI save alone.
4. With consent and a recovery route, test physical x=0,1,2 and y=0,1,2 at both edges, approach along both directions and diagonally, slow/fast movement, top and left link arbitration, Spice-Book, TV return, desktop scaling and all monitors. Check for accidental transitions outside the target and compare 0 vs desired delay for actual resistance. This is a physical user acceptance gate, not run here.
5. Separately investigate a supported GUI TV computer enrollment and client name mapping on a disposable Synergy installation. If unsupported, disclose the custom-config limitation; do not fabricate DB identities. A more capable screen-edge helper or source change may be required for a true 2D region or per-corner delay.

No live service/config/database, client, or TV was modified. No GUI layout or physical acceptance was performed.
