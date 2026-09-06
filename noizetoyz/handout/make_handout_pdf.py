#!/usr/bin/env python3
"""Render noizetoyz-hardware-handout.html to a print-ready PDF via Playwright.

Renders the local HTML source directly, not the published Artifact URL —
the Artifact platform wraps every page in its own frame-runtime/iframe
scaffolding for the live editor, which is real overhead to fight through
for a one-shot PDF render and buys nothing here (the PDF only needs the
page's own content). Rendering the source file directly is reproducible
without depending on the artifact platform being reachable at all.

Page margins are governed entirely by the page's own `@page` CSS rule
(16mm/14mm) — Playwright's own `margin` is left at 0 so the two don't
double up or fight each other.

Requires the Google Fonts stylesheet the page links to be reachable
(IBM Plex Sans/Mono) — falls back to system fonts silently if offline,
still readable, just not pixel-identical.

Usage:
    pip install playwright && playwright install chromium
    python3 make_handout_pdf.py [output.pdf]
"""

import sys
from pathlib import Path

from playwright.sync_api import sync_playwright

HERE = Path(__file__).resolve().parent  # noizetoyz/handout/
SOURCE = HERE / "noizetoyz-hardware-handout.html"


def main():
    output = Path(sys.argv[1]) if len(sys.argv) > 1 else HERE / "noizetoyz-hardware-handout.pdf"

    if not SOURCE.exists():
        raise SystemExit(f"source file not found: {SOURCE}")

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page()
        page.goto(SOURCE.as_uri())

        # Wait for the web fonts to actually finish loading — printing too
        # early bakes in the system-font fallback for headings/labels
        # instead of IBM Plex Sans/Mono.
        page.evaluate("document.fonts.ready")

        # Activates this page's @media print rules (page-break-before on
        # each section, exact color printing) the same way a browser's
        # own Print dialog would.
        page.emulate_media(media="print")

        page.pdf(
            path=str(output),
            format="A4",
            print_background=True,
            margin={"top": "0mm", "bottom": "0mm", "left": "0mm", "right": "0mm"},
        )
        browser.close()

    print(f"wrote {output}")


if __name__ == "__main__":
    main()
