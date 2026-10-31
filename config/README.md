# `config/` — templates only

This folder holds **templates and examples** for local configuration. It does
not hold real values.

- Real files (`*.local.json`, `settings.local.json`, `.env`) are ignored by
  git and must be created locally from the templates.
- A real configuration can contain the PC address, port, screen name and TV
  address. Those are LAN details: do not commit them to a public repository.
- Never put license keys, tokens, passwords, SSH/device keys or signing
  material in this folder.

The templates themselves are owned by the workstream that consumes them
(`tools/synergy/`, `app/`, `dock/`). This README only states the folder policy.
See [`../docs/repo-policy.md`](../docs/repo-policy.md).
