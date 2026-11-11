> **Historical 0.3.4 documentation.** For the native 0.4.0 candidate, start at [the current README](../README.md) and [native verification](../native/docs/VERIFICATION.md). Dock references are historical; the dock has been removed from this bundle. Do not use legacy web-packaging instructions to produce a native release.

# Brand, trademarks and licensing

## Trademarks

| Mark | Owner | Use here |
| --- | --- | --- |
| "Synergy" | Symless Ltd. | Describes compatibility with the Synergy product the TV connects to. Not a claim of affiliation. |
| "LG", "webOS", "webOS TV" | LG Electronics | Describes the target platform. Not a claim of affiliation. |

The project is **independent and unofficial**. It must not use Symless or LG
logos, product screenshots owned by them, or store-listing assets in a way
that implies endorsement. Any README/docs statements should include the
disclaimer already in the README.

"synergy-tv" as a repository working title is descriptive-for-compatibility
only; it is not a product name. The final brand should be clearly distinct from
the "Synergy" mark to reduce trademark risk and store-listing confusion.

## Licensing choices

- **Project code:** MIT (see [`decisions.md`](decisions.md#d-001--licence-mit-lead-confirmation-requested)).
- **Synergy 3:** proprietary; used locally under the user's own licence. Never
  redistributed; license keys never committed or shared. See the Symless
  [EULA](https://symless.com/synergy/legal/eula).
- **webOS CLI (`@webos-tools/cli`)**: Apache-2.0; used as a development tool.
- **Homebrew Channel for webOS**: community project; not bundled.
- **App icons and fonts**: only self-authored or system-provided assets;
  record any other asset's source and licence in
  [`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md) before release.

## Brand handoff

`app/BRAND.md` (app workstream) is authoritative for the product name, logo,
colour/type language and app ID. When it is final, the lead:

1. Renames the repository and README title (`synergy-tv` → brand).
2. Confirms the app ID and migration notes (installed package upgrade path).
3. Updates `docs/` and `.github/` URLs.
4. Records the final decision in [`decisions.md`](decisions.md).

## Not decided here

Distribution (LG Content Store, Homebrew repos, GitHub Releases only), a
privacy policy for a store listing, and any paid support model. These need a
separate, explicit decision.
