> **Legacy 0.3.4 reference.** The native 0.4.0 candidate is documented in the root README and `native/docs/`. Dock references below describe a retired component; no dock is included. Historical TV results do not validate the native rebuild.

# `config/icons/` — icon assets (whitelisted)

`.gitignore` excludes most image files to keep personal captures out of the
repository, but explicitly whitelists `config/icons/*.png`.

Rules:

- Only self-authored icons or assets with a recorded licence belong here.
- Record the source and licence in [`../../THIRD_PARTY_NOTICES.md`](../../THIRD_PARTY_NOTICES.md).
- No screenshots, TV captures or desktop captures — those go to the ignored
  `captures/` folder outside version control.

The app's own icons live with the app (`app/`) and are whitelisted separately.
