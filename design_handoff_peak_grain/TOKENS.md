# Tokens

Everything is `[data-pui-theme="onyx"]` from `packages/pedal-ui/src/tokens.css`.
No token values were changed. The mock inlines the literals because Design
Components are inline-styled; the real face should read the variables.

| Used for | Token | Value |
|----------|-------|-------|
| Page behind the pedal | `--pui-page` | `#0f1315` |
| Card | `--pui-panel` / `--pui-outline` | `#171d20` / `#2a3336` |
| Card radius / shadow | `--pui-radius-panel` / `--pui-panel-shadow` | `20px` / `0 20px 44px rgba(0,0,0,.4)` |
| Section plate | `--pui-module` / `--pui-module-edge` | `#20292d` / `#161c1e` |
| Hairline dividers | `--pui-module-divider` | `#0a0b0c` |
| Recessed displays | `--pui-recess` + `--pui-well-shadow` | `#101416`, `inset 0 2px 6px rgba(0,0,0,.6)` |
| Ink inside a display | `--pui-recess-ink` | `#4d5f65` |
| Primary ink | `--pui-ink` | `#b9d3d9` |
| Captions, corner labels | `--pui-ink-soft` | `#8ba3a9` |
| Taglines only | `--pui-ink-dim` | `#6c8288` |
| Knob cap (soft) | literal in `Knob.css` | `#0a0d0e` |
| Knob ring / cap (scale) | `--pui-knob-body`, `--pui-cap-grad-*` | `#0b0e10`, `#364347` → `#191f22` |
| Needle | `--pui-soft-needle` / `--pui-pointer` | `#cfe4e9` / `#b9d3d9` |
| Arc + tick track | `--pui-soft-track` / `--pui-tick` | `#2b3639` / `#39474b` |
| Chrome buttons | `--pui-chrome`, `--pui-chrome-ink` | `#1b2225`, `#9fb8bd` |
| Pill on / off | `--pui-pill-on-bg` / `-off-border` | `#b9d3d9` / `#2a3336` |
| Type | `--pui-font-display` / `-label` | Space Grotesk 500 / 700 |

## Section accents

Delay and Reverb reuse Alpine's own module accents unchanged. The other three
are the current native face's pastel caps (`0xff80658e`, `0xffe79bbf`,
`0xffe6bfa9`) re-picked to hold up on the near-black onyx ground — same hues,
lifted in lightness so a 1.4px arc still reads.

| Section | Accent | Note |
|---------|--------|------|
| Grain | `#b39bd8` | was `#80658e` |
| Pitch | `#e78fb3` | was `#e79bbf` |
| Random | `#dfa878` | was `#e6bfa9` |
| Delay | `#a3ce7a` | `--pui-accent-delay` |
| Reverb | `#7fd2d8` | `--pui-accent-reverb` |

Add the first three to tokens.css as `--pui-accent-grain` / `-pitch` /
`-random`, outside the theme blocks, for the reason the existing accents sit
there: what a section's colour means must not change with the ground.

A section wears its accent in three places only — its power toggle or name
chip, its display, and the value arcs / lit ticks of its own knobs — set as
`--pui-accent` + `--pui-soft-lit` on the section wrapper, the way
`ModulePanel` does it.

## Contrast

Section names, captions and corner labels are on `--pui-ink-soft` (≈5.6:1 on
`#20292d`). `--pui-recess-ink` is only ever used inside a well — on the plate
it measures ~2.2:1. `--pui-ink-dim` is the tagline and the unit figures only,
never a caption (`StageGroup.css` states this).
