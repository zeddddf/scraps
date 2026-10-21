# Security Policy

Scraps is a local-only clipboard manager. It never uploads clipboard content, stores no accounts, and contains no telemetry.

## Supported versions

| Version | Supported |
|---|---|
| latest release | ✅ |
| older releases | ❌ — please update |

## Reporting a vulnerability

Please report security issues privately via [GitHub Security Advisories](https://github.com/zeddddf/scraps/security/advisories/new) — do **not** open a public issue for vulnerabilities.

Include: affected version / commit, reproduction steps, and impact assessment. You can expect an initial response within 7 days.

## Data handling notes

- All user data lives in `%LOCALAPPDATA%\Scraps` (SQLite database + image assets) and never leaves the machine.
- The only outbound network request Scraps may make is an optional update check against `api.github.com` (see settings), which transmits no user data.
