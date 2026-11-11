# Verification — native 0.4.0 candidate

Performed November 11, 2026, in an isolated Linux x86_64 authoring environment.
No TV, user's desktop server, local Windows repository or SDK was contacted.

## Executed and passed

| Check | Result |
|---|---|
| C++20 Debug and Release host builds | All three executables compile/link with `-Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Werror` |
| Native core test executable | 123 assertions plus 2,000 deterministic malformed-packet cases |
| Real daemon + CLI integration | 68 assertions, using real subprocesses, Unix sockets and synthetic TCP server |
| ASan + UBSan | Core and process/TCP/IPC CTest suites pass |
| Native UI | 17 checks using actual SDL events and actual GLES2 framebuffer rendering |
| Resolution checks | 1280×720, 1920×1080 and 3840×2160; 720p/4K pointer-edit mapping |
| UI scenarios | Four panels (no Apps page), D-pad/OK, Back/focus restoration, physical text input, apply/cancel, on-screen keyboard opening, invalid launch/font/view rejection |
| Host→TV false-claim gates | CMake rejects `SYNERGY_WEBOS=ON` with a host compiler; stage audit rejects x86_64 binaries |
| TV-only conditional branches | Strict HOST `-fsyntax-only` check; explicitly not an ARM or hardware test |
| Source-bundle regressions | Eight tests: dock removed, launch wiring removed, no obsolete Apps captures, native identity, current docs links, no fonts, no probe installed into the TV stage |
| Gear-settings spike | Eleven host fixture tests; Python collector and shell syntax, no TV connection or menu change |
| Scripts | Bash/POSIX shell syntax checked; stage auditor executed against a host stage and rejected it |

The integration suite covers fragmented hello packets separated by multiple poll
intervals, screen negotiation, active input, leave cleanup, keepalive ordering,
TLS/oversized-frame refusal, idle timeout, once-on-failure, stop during reconnect,
active-versus-saved configuration, private persistence, duplicate daemon lock,
malformed/stalled IPC, another UID rejected by kernel peer credentials, native
profile persistence, and preservation of malformed existing configuration.

The delivered Linux preview binaries are the strict Release build used by the
release CTest/UI runs. Host runtime libraries/fonts are not bundled. Rendering
used Mesa llvmpipe with an OpenGL ES 2 context. This verifies real native code,
not a browser mockup or an illustration, and does not measure TV performance.

Fresh logs are in `native/.ai/evidence/`; screenshots/reports are in
`native/verification/ui/`. Do not confuse retained 0.3.4 historical records with
these native results. Use the package checksum manifest to tie evidence/source
and preview binaries to this exact handoff.

## Not performed / not established

**No ARM32 target build, installable IPK, actual LG TV launch, uinput injection,
root provisioning, Luna action, native-jail socket access, physical Synergy 3
server connection, sleep/wake or boot persistence test was performed.**

The compiler/sysroot and target runtime used for SkiFree/Spice Home are not
available in this environment; no replacement was guessed. Container networking
could not resolve package downloads. The source includes a guarded build/stage
route for the user's existing verified environment rather than fake TV output.

No PowerShell runtime was available, so the PowerShell WSL wrapper is supplied
but unexecuted. The root provisioner and boot hooks were syntax-checked only;
they were not run. Stage dependency availability and symbol-version compatibility
require the exact SDK/TV. The Unix broker authenticates a UID, not an app identity;
shared app UIDs and jail visibility need explicit owner-reviewed target evidence.

Clipboard, TLS, full international key mapping, Caps/Num/Scroll lock-state
inheritance, true absolute pointer positioning are not implemented. The dock, companion launcher wiring and native Apps page
were deliberately removed at owner request. Existing Windows helper interoperability is not proven merely
by preserving snapshot section markers.


## Final owner-request changes

The standalone dock directory, native Apps page, companion launch/catalog-write
wiring, and generic launcher control RPCs are absent. The integration suite now
also rejects the removed `apps`, `launch` and `home` RPCs; Home and Alt+Tab remain
protocol-driven input shortcuts, not manually exposed launcher APIs.

The gear-menu spike is researched/documented and includes a read-only collector;
no private UI insertion point, key remap or menu patch was run on the TV. Its
fixture tests are distinct from hardware evidence. README screenshots were
recaptured after dock removal from the real host UI with an isolated idle daemon.
