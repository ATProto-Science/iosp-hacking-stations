# toons

Airglow Cartoon Wall — `toons.tilde.style` (Cloudflare Pages, deployed by hand with
`wrangler pages deploy public --project-name toons-tilde-style` from this directory).

Only `public/index.html` is served. Everything else here — `AIRGLOW-TOONS.md`,
`create_airglow_relays.py`, `patch_automation_descriptions.py`, `design-board/`, `test/` — is
dev/design material and stays out of the deploy on purpose: a `wrangler pages deploy .` (whole
directory) run before 2026-09-29 had been publishing all of it, including `.py` source, publicly
readable at e.g. `toons.tilde.style/create_airglow_relays.py`. Nothing secret was in it, but same
fix as `iosp-kiosk` got for its own `OPS.md` exposure.
