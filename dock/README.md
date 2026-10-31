# SynergyWebOS Dock — integration contract

A separate, static web app with a bottom dock and app switcher. SynergyWebOS writes
`catalog.js` before launching it, so the dock shows the same live catalog and
marks running/foreground apps. The catalog is supplied by SynergyWebOS's root helper
because this TV denies third-party `listApps`/`getForegroundAppInfo` calls; the
dock itself stays unprivileged and only uses the allowed `launch` call. If opened
from the TV launcher without a fresh catalog, it shows the last catalog or an
explicit unavailable message. Open `index.html?demo=1` locally for deterministic
demo data.

## Interaction

Move the pointer to the bottom edge to reveal the dock, or press D while the page has focus. Tab and Enter select an app; Escape or Hide dismisses it. It hides after 3.5 seconds outside the dock unless keyboard focus is inside. Refresh reloads the catalog. Running and foreground apps are listed first and marked; the rest of the catalog follows. These controls operate only while this page owns input. Launch requests use an app ID; the system decides whether to resume an existing instance or launch one. An accepted request is not proof that focus changed. No close action is exposed: a portable, authorized close contract was not established.

The app tries `listLaunchPoints`, then `listApps` as catalog fallback; queries `getForegroundAppInfo`; and optionally queries `dev/running`. Running entries are marked only when that method actually returns an array. Otherwise the UI says running state is unavailable and displays the catalog, not a fabricated open-app list. Calls are one-shot and time out after five seconds; refresh on visibility return. A TV may deny any of these calls or return different icon paths. Status is surfaced in the UI; permission is not granted by this prototype.

## Integration decision

**Implemented first integration:** the directory is packaged as a standalone
card app (`dev.hurrspice.tvdock`) and SynergyWebOS's **Launch dock** button refreshes
`catalog.js` through the root helper before launching it. This keeps the TV's
application-manager ACL out of the dock: the privileged catalog read stays in
SynergyWebOS, while the dock only calls the permitted `launch` method. The standalone
card still replaces the foreground app; it does not remain visible over another
app. Autohide hides UI inside its own window, not its window or input capture.

**Overlay research:** webOS OSE describes compositor overlay and popup window types, but that documentation does not establish that an ordinary LG TV web app can create one, stay above other applications, remain click-through while hidden, or get a global key. The public sources examined did not establish a usable third-party `com.webos.service.iwc` or surface-manager contract for this TV. Root availability is not proof of a safe, stable or authorized surface capability. A rooted transparent overlay may be possible on some firmware, but requires device-specific investigation of window type, caller identity/permissions, focus, input regions, z-order, lifecycle and video planes. Do not request broad privileges or change system services on the strength of this prototype. The standalone card is the feasible bounded implementation; a genuine always-on dock and complete running-app list remain unverified.

LG's TV Application Manager documentation is restricted to the caller's apps or partner apps, and OSE APIs are not a TV compatibility guarantee. API results, identity and permissions need testing from the actual packaged app on the target firmware. `dev/running` is documented by OSE as developer-mode-only; it may be absent or denied on TV. No claim of foreground subscription or global monitoring is made. Sources: [LG TV Application Manager](https://webostv.developer.lge.com/develop/references/application-manager), [OSE Application Manager](https://www.webosose.org/docs/reference/ls2-api/com-webos-service-applicationmanager/), [OSE compositor window types](https://www.webosose.org/docs/guides/core-topics/graphics-input/graphics-input-overview/), [LG compatibility and denied methods discussion](https://forum.webostv.developer.lge.com/t/denied-method-call-listapps-for-category/8572).

## Lead acceptance gates

1. Decide whether to package the standalone card or port its UI and adapter into the main app; settle the final ID and any separate invocation mechanism.
2. On an authorized controlled TV session, verify LS2 service identity, catalog, icon resolution, running-state permissions, foreground response and launch/resume behavior, including errors and return to the prior app.
3. Verify actual 1080p safe area, pointer edge, keyboard focus, autohide, scrolling with many apps, suspension/relaunch, and human usability. Test any overlay separately, including input capture and hardware video.
4. Verify package, install, and compatibility on the target firmware before deploying. This prototype has not been installed or launched on a TV.

## Local checks

`node --test dock/dock.test.cjs` and `node --check dock/dock.js` from the repository root. These validate model and adapter behavior, not TV services, visual layout, runtime focus or hardware acceptance. Static HTML/JS was chosen for a dependency-free, directly packageable prototype; no Svelte files or runtime are used.
