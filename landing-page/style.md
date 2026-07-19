---
style: Bonfire
version: 0.1
metaphor: "A hearth people gather round to work — sparks (links) catch, kindling (code) gets handed out, embers (footer) linger after."
---

## Palette

| Token | Value | Metaphor anchor |
|---|---|---|
| `--fire` | `#E8651A` | The flame itself — links, primary accents |
| `--ember` | `#C44B10` | Deeper glow — hover/active states |
| `--spark` | `#F5A623` | A catching spark — headings, emphasis |
| `--smoke` | `#7A6F65` | Drifting smoke — secondary/muted text |
| `--night` | `#141210` | The dark the fire's set against — page background |
| `--ground` | `#221E1A` | Earth around the hearth — card/panel backgrounds |
| `--log` | `#3A3028` | Unburned wood — borders, dividers |
| `--ash` | `#EDE8E2` | Ash and firelight on faces — body text |

(Inherited verbatim from `sailing.tilde.style`'s `site.css` — this is the same
Bonfire style, not a fork of it. Do not change these tokens here without also
updating `sailing.tilde.style`, or the two sites will visibly diverge.)

## Typography

- Display: Georgia / Times New Roman (serif) — same as sailing.tilde.style
- Body: system-ui
- Scale: `clamp()`-based, same breakpoints as sailing.tilde.style's `--text-*` tokens

## Components

| Element | Name | Notes |
|---|---|---|
| Header | `.hearth` | Site name + tagline + nav |
| Nav links | `.ring` / `a.spark` | |
| Content sections | `.gathered` | |
| Cards (used here for each station) | `.style-card` | Reused as-is, no new component needed |
| Code/commands | `.kindling` | |
| Footer | `.embers` | |
| Tag chips | `.stones` / `.stone` | Used here for station status ("live", "external tool", etc.) |

## Anti-patterns

- **Overly cute/twee bonfire language** (flagged explicitly for this site) — the
  class vocabulary stays (hearth/sparks/kindling/embers), but *copy* stays plain
  and direct. This is a workshop utility page for researchers under time
  pressure, not a whimsical campfire story.
- Generic conference-website look (stock photography, sponsor-logo rows, dense
  agenda grids) — inherited from the wider tilde.style anti-pattern list.
- Fabricated/live-looking links for infrastructure that isn't live yet (the
  Aster PDS signup) — placeholder state until Emily's team's launch, not a fake
  working link.

## Modes

| Mode key | Name | Trigger |
|---|---|---|
| (none yet) | — | default (dark, matches sailing.tilde.style) |
