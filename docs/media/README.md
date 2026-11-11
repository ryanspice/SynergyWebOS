# Native screenshots and badges

These PNGs are captures of the native SDL2/raw-GLES2 framebuffer, not generated
illustrations or browser layouts. Captured November 11, 2026, at 1920×1080 from
the strict Release Linux x86_64 build. Mesa llvmpipe supplied a real OpenGL ES 2
context. A real local daemon was running in an isolated temporary directory,
with its default dry-run configuration and no server connection. No TV was used.

| Asset | State |
|---|---|
| `native-connection.png` | Connection panel; daemon available and stopped; blank desktop address |
| `native-input.png` | Input settings; cursor nudge off and plaintext approval not granted |
| `native-handoff.png` | Local reference settings; no Windows configuration applied |

Matching JSON UI reports are included. Checksums appear in the source archive's
manifest. No LAN address, password, TV account data or private probe result is
shown. The separate `native/verification/ui/` images exercise additional sizes
and interactions; they are not claims of physical-TV performance.

## Reproduce

From the repository, run a native host daemon with a fresh absolute runtime
directory. Then capture the UI against that same directory:

```bash
./native/build/desktop/synergy-daemon --runtime-dir /absolute/fresh/private-dir
# In another terminal, with that daemon still running:
./native/build/desktop/synergy-tv --runtime-dir /absolute/fresh/private-dir \
  --view link --width 1920 --height 1080 --frames 35 \
  --screenshot /absolute/new/connection.png --report /absolute/new/connection.json
```

Use `--view input` and `--view handoff` for the other panels. `xvfb-run` can wrap
the UI where no WSLg/display session is available. This is documentation tooling,
not a command for starting real input on the TV. Stop the test daemon when done.

`badges/*.svg` are self-contained **static labels**, not live CI/build-service
results. They use system font names and embed no font files. Update their text
only when supported by the corresponding release evidence. TV status remains
pending until an actual ARM build and physical-TV acceptance have passed.
