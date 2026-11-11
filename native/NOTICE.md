# Notices and foundation provenance

SynergyWebOS native 0.4.0 is an independent developer candidate. Synergy is a
Symless product. This application is not the official Synergy 3 desktop GUI.
No proprietary Synergy executable, desktop service, artwork, credentials, or
license activation material is included in the native implementation.

The SDL2 dynamic loader, FreeType loader, raw GLES2 renderer, JSON wrapper,
viewport/pointer transform and directional focus algorithm were adapted from
Ryan Spice-Finnie's **Spice Home C++ v0.1.0** source archive. Its MIT notice is
retained in `foundation/LICENSE` (installed as `FOUNDATION-LICENSE`).
`foundation/UPSTREAM-SHA256.json` records original input file hashes, not hashes
of the adapted files. The renderer no longer loads the homescreen's artwork;
only shared geometry declarations were retained from its application core.
The bounded process helper was adapted from the same project's services code.

SkiFreeWebOS v1.0.0 (`ryanspice/SkifreeWebOS`, commit
`d4e85cb0a8940e17f4355782cb7159201a036ff0`) explicitly shares the same foundation.
Its CMake/TV-build documentation was inspected to preserve the C++20,
SDL-window/input + raw GLES2, real ARM32 toolchain, and target runtime gates.
No SkiFree game logic or original game artwork was copied.

New native protocol/control/UI code is distributed under the repository MIT
license. Original 0.3.4 files remain under their existing notices. SDL2,
FreeType, json-c and libpng are runtime/build dependencies, not relicensed here.
No font files or third-party runtime libraries are bundled in this source handoff.

Protocol reference inputs: the existing 0.3.4 Python client, Symless's public
synergy-micro-client source, and the classic Synergy 1.4 protocol message shapes.
This is a native reimplementation, not a claim of modern Synergy 3 service,
cloud discovery, account, TLS, clipboard or file-transfer compatibility.
