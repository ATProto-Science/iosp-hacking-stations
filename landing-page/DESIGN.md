# Design record

*hacking.tilde.style — initiated 2026-07-16*

## Design intention

1. **What is this site for?** The front door for the IOSP 2026 ATScience/Science-PDS
   workshop (Leiden, October, "Resilient Data & Sovereign Infrastructure" track) —
   base-station login links plus a list of the 4 hacking stations, linking out to
   the real code repo for anything executable.
2. **Who tends it?** Just Torsten, one-off — stood up for this workshop, left as-is
   afterward, same as `sailing.tilde.style`. No ongoing update cadence designed for.
3. **What physical space or experience does it remind you of?** Same family as
   `sailing.tilde.style` — inherits the Bonfire style wholesale rather than
   deriving a new metaphor (explicitly requested: reuse its CSS).
4. **What should a visitor feel in the first 10 seconds?** Curious, exploratory —
   invites poking around the station links rather than a strict linear "step 1,
   step 2" task flow.
5. **What would feel completely wrong here?** Overly cute/twee — the Bonfire class
   vocabulary (hearth/sparks/kindling/embers) stays, but copy is kept plain and
   direct; this is a utility page for researchers, not a whimsical campfire story.

## Palette

See `style.md` — inherited verbatim from `sailing.tilde.style`.

## Typography

See `style.md` — inherited verbatim from `sailing.tilde.style`.

## Component vocabulary

See `style.md`. One addition: `.style-card` (already defined in the inherited
`site.css`, unused by `sailing.tilde.style` itself) is repurposed here as the
per-station card — no new CSS needed.

## Metaphor layers

- **Hearth** (header) — identity + orientation: what this page is, how to get
  logged in.
- **Gathered** (main sections) — the actual content: base station, then the 4
  stations.
- **Embers** (footer) — what lingers after: contact, code repo links, design
  system credit.

## Anti-patterns

- No twee bonfire narration in body copy (see style.md).
- No fabricated Aster signup link before Emily's team's PDS is actually live —
  marked as "coming with the launch" until there's a real URL.
- No generic conference-site tropes (stock photos, sponsor grids, dense agenda
  tables).

## Motion

None — static page, no animation to gate behind `prefers-reduced-motion`.

## Known gaps

- Aster PDS signup link — placeholder until Emily's ATScience PDS Hosting
  project's launch (planning starts the week of 2026-07-20).
- Stations 1 and 3 aren't Torsten's to build — listed for completeness (per the
  Fathom recap) but not linked to any code, since that's other co-organizers'
  territory.
- `atlogin` login-bridge wiring not yet embedded in this page — currently just a
  described concept in the base-station section, no live widget.
