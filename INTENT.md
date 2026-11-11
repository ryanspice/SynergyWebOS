# Intent

SynergyWebOS is a native, TV-first Synergy client for LG webOS televisions: a
C++20 SDL2/GLES2 app that brings a desktop keyboard and mouse to the TV over the
classic Synergy 1.4 protocol, with explicit local root provisioning.

Goals:
- Native **Connection / Input / Handoff / Diagnostics** UI; no browser, Python or
  Node runtime on the TV.
- Keyboard, pointer, Home and Alt+Tab handling from a separate input daemon that
  can outlive the UI.
- No dock, Apps page or generic launcher RPC (owner decision D-011). Home and
  Alt+Tab are protocol-driven input shortcuts, not launcher APIs.
- A tiny PC-side handoff corner with a configurable delay; the TV app stores a
  local handoff reference and never edits the Windows Synergy server.
- Keep the 0.3.4 web app as migration and rollback source, not the native runtime.
- Reproducible host build and honest, separate ARM/TV verification.

Non-goals: no Synergy binaries, webOS SDK, device credentials, TLS, clipboard,
cloud discovery, absolute cursor synchronization, or push/publication without
explicit authorization.
