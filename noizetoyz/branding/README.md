# Noizetoyz branding page

The Noizetoyz brand page exists in **two separate places that don't sync with each
other** — updating one does *not* update the other. Both need editing when the content
changes. This split caused real confusion once already (2026-09-04: the page was
reworked, but `noizetoyz.atproto.music` kept serving old content because only the
artifact side had been touched) — this doc exists so that doesn't happen again.

## 1. The Claude Artifact

Live at: https://claude.ai/code/artifact/5dbe1814-0fc8-4732-b3a3-a75b2c1f30af

Source: `noizetoyz-branding.html` — the artifact's *content* file (no
`<!DOCTYPE>`/`<html>`/`<head>`/`<body>` — the Artifact tool wraps it at publish time).

To update: edit this file, then republish it to the artifact URL above (`Artifact`
tool, `action: publish`, passing that `url`).

## 2. `noizetoyz.atproto.music` (the real public site)

This is **not** a custom domain bound to the artifact — it's a genuinely separate
**Cloudflare Pages project** named `noizetoyz` (`noizetoyz.pages.dev` +
`noizetoyz.atproto.music`), a one-time static copy of the artifact's content deployed
independently. Also not part of the `landing-page/` Cloudflare Pages deploy (different
project entirely).

Source: `site-index.html` — a full standalone HTML document (proper
`<!DOCTYPE>`/`<html>`/`<head>`/`<body>`) wrapping the same content as
`noizetoyz-branding.html`, built for a real static deploy rather than the Artifact
platform's own wrapping.

To update:
1. Make the content change in `noizetoyz-branding.html` first (source of truth for the
   actual copy/design).
2. Rebuild `site-index.html` to match (wrap the same `<title>`/`<style>`/body content in
   a standalone document — see git history for the exact wrapping shape if doing this
   by hand).
3. Deploy it for real:
   ```bash
   npx wrangler pages deploy <dir containing site-index.html as index.html> \
     --project-name=noizetoyz --branch=main
   ```
   **`--branch=main` is required** — this repo's working branch is not reliably `main`,
   and wrangler auto-detects the current git branch for the deploy target. Without it,
   the deploy lands as a preview (`<branch>.noizetoyz.pages.dev`) instead of production,
   and `noizetoyz.atproto.music` won't change at all. (Same gotcha as the
   `landing-page/`/`hacking-tilde-style` deploy — see that project's own commit history.)

**Why the file has to live outside a scratchpad**: the original 2026-09-02 deploy of
this site was built from a temporary file that was never saved into the repo — so when
the page was reworked later, there was no tracked source to redeploy from, and nobody
noticed the live site had silently stopped matching the artifact. Keep `site-index.html`
here, checked in, as the actual redeployable source going forward.
