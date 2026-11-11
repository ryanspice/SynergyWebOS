> **Legacy 0.3.4 reference.** The native 0.4.0 candidate is documented in the root README and `native/docs/`. Dock references below describe a retired component; no dock is included. Historical TV results do not validate the native rebuild.

# SynergyWebOS TV app

A static, 1080p, keyboard/mouse-first webOS shell for a Synergy-compatible
desktop input link. It includes connection/status, a live client log, a
root/uinput probe, autostart control, the Windows-key home target, a handoff
profile, and an open-apps surface with a companion-dock contract.

No Node.js runtime is required on the TV. The package is plain HTML, CSS and
classic JavaScript; the Python client has no third-party packages.

## Files

```
app/
  index.html              app shell
  appinfo.json            webOS manifest (id: dev.canopy.synergywebos)
  css/app.css             1080p visual system
  js/core.js              TV service bridge, settings, profile, app catalog
  js/app.js               views, polling, keyboard/mouse interaction
  assets/mark.svg         master mark
  icon*.png               package icons
  client/client.py        Synergy 1.x client + uinput injection + HOME handling
  client/tvctl.sh         start/stop/probe/autostart/profile/server bridge
  client/probe.py         read-only root/uinput capability probe
  client/README.md        client and runtime-file contract
  contracts/handoff-profile.schema.json
  BRAND.md                brand, identity and migration
  INTEGRATION.md          Terra 1/2/3, build and deployment contract
  REPORT.md               synthesis-ready handoff
```

## Build and run

There is no build step. `ares-package` can package the directory directly:

```powershell
# Example; the repository build owner owns the real command and output path.
& '<repo-parent>\.tools\ares-cli\node_modules\.bin\ares-package.cmd' app
```

Recommended package contents are `index.html`, `appinfo.json`, `css/`, `js/`,
`assets/`, `icon.png`, `icon-large.png` and `client/`. Markdown and the
documentation PNGs (`icon-256.png`, `icon-512.png`) can be excluded by the build
pipeline. `tvctl.sh` is invoked through `sh`, so an executable bit is helpful
but not required.

### Browser preview

For a layout/UI review without a TV, serve `app/` from a local HTTP server and
open `index.html?demo=1`. The `demo=1` flag uses deterministic sample data and
makes **no** TV calls. Without it, a desktop browser honestly reports that the
root helper and TV app launcher are unavailable; it never fakes a connection.

The app is also usable from `file://` for visual checks, but TV services require
the packaged webOS origin and the Homebrew root helper.

## Checks

```powershell
node --check app\js\core.js
node --check app\js\app.js
python -m py_compile app\client\client.py app\client\probe.py
```

On a TV or POSIX host, also run `sh -n app/client/tvctl.sh`. The build/QA owner
runs package, install and on-TV acceptance. No TV was installed or launched
during this app work.

## Technical choices and the Svelte exception

The repository preference is Svelte 5 + TypeScript + static. This app uses
plain static JavaScript instead, for these concrete reasons:

1. **Directly packageable.** There is no bundler, `node_modules` or generated
   output between source and IPK. A build failure cannot produce a stale app.
2. **No Svelte MCP/autofixer in this session.** The required Svelte workflow was
   not available, so shipping Svelte without the mandated validation would be
   worse than a small, inspectable static shell.
3. **Tiny surface.** Four views and a bridge do not need a framework runtime.
   The complete UI is CSS plus under 2,000 lines of unminified JavaScript, which
   is easier to audit for a root-adjacent TV app.
4. **No runtime dependency either way.** Svelte compiles away; the difference is
   build complexity, not TV runtime. The current TV kernel is Linux 6.12 and
   likely supports modern Chromium, so this is a tooling/risk choice, not a
   claim that Svelte cannot run.
5. **TypeScript needs a toolchain.** Source is strict-mode ES2017-compatible
   JavaScript; adding a `tsc` build would force Node into the build contract
   for little gain at this size. If the app grows, the JS is isolated enough to
   port to Svelte 5 components without changing `tvctl.sh`, the client or the
   package ID.

If the lead prefers the repository standard for the final release, the safest
port is: keep `app/client/` and `appinfo.json`; rebuild the four views in Svelte
5 with the same CSS tokens and bridge functions; run the Svelte MCP/autofixer;
and keep the static build output as the packaged app. Do not introduce an SSR
server or a Node runtime on the TV.

## Behaviour notes

- The app never silently restarts a running client. "Save & start" restarts only
  when a client is already running; the Home quick action switches to the Link
  section instead.
- Home-key changes are saved immediately but apply on the next client start.
  The Settings screen shows the active link values separately.
- The 2 × 2 handoff card is an **exported profile**, not a live server write.
  "Read desktop" is a read-only GET of the Synergy settings API.
- The dock is a separate app (`dev.canopy.tvdock`). The built-in app grid
  remains the guaranteed surface; the companion launch is best-effort.
- All service calls are one-shot and time out. There is no background watcher
  beyond the 4-second status poll while the app is visible.
