# Noizetoyz branding page

Source for the standalone Noizetoyz brand page, published as a Claude Artifact and
served at `noizetoyz.atproto.music` (custom domain on top of the artifact).

Live artifact: https://claude.ai/code/artifact/5dbe1814-0fc8-4732-b3a3-a75b2c1f30af

This is **not** part of the `landing-page/` Cloudflare Pages deploy — it's a separate
Claude Artifact page (its own runtime, its own publish flow), kept here so the source
is checked in rather than living only in a temp scratchpad or the Artifact's own
history. `noizetoyz-branding.html` is the artifact's content file (no
`<!DOCTYPE>`/`<html>`/`<head>`/`<body>` — the Artifact tool wraps it at publish time).

To update: edit `noizetoyz-branding.html`, then republish it to the same artifact URL
above so `noizetoyz.atproto.music` picks up the change.
