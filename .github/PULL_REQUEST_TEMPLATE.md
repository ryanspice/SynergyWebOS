## Summary

<!-- What changed and why. One concern per PR. -->

## Component

- [ ] TV app (`app/`)
- [ ] Dock (`dock/`)
- [ ] Synergy tooling (`tools/synergy/`)
- [ ] Home-key scripts (`scripts/home/`)
- [ ] Build / tests (`scripts/build/`, `tests/`)
- [ ] Repository / docs / CI

## Checklist

- [ ] Change is scoped to the affected area; unrelated work preserved.
- [ ] No secret, backup, license key, proprietary Synergy bundle or personal data added.
- [ ] `pwsh ./scripts/check-repo-guards.ps1 -Staged` passes (or hooks are enabled).
- [ ] `pwsh ./scripts/check-docs-links.ps1` passes when docs changed.
- [ ] Documentation updated for user-visible behaviour or config.
- [ ] Live-device steps listed below if the change touches the client, input, home path or TV app.

## Checks run

<!-- Command, result (PASS/FAIL/BLOCKED/NOT_RUN). Do not hide failed or unrun checks. -->

```
```

## Live TV/PC acceptance required by maintainer

<!-- Steps the maintainer must run on the real TV/PC, or "none". -->

## Risks / limitations
