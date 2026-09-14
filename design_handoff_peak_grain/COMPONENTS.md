# Components & geometry

Measurements are from option `1c`. Everything maps onto a component that
already exists in `packages/pedal-ui` or `packages/delay-face`; the notes below
are the sizes and spacings to pass it.

## Enclosure

- Card: 700px wide, 14px padding, radius 20, `1px` border, onyx panel shadow.
- Plate: radius 10, `1px solid #161c1e`, `overflow:hidden`,
  `box-shadow: 1px 3px 3px -1px rgba(0,0,0,.54), inset 0 1px 0 rgba(255,255,255,.08)`
  (`--pui-module-shadow`).
- Row 1: `grid-template-columns: 1fr 1px 1fr 1px 1fr`; row 2: `1.95fr 1px 1fr`.
  The 1px tracks are the dividers (`#0a0b0c`); a horizontal one runs between
  the rows and above the scope.
- Section: `padding: 12px 10px 13px`, `display:flex; flex-direction:column; gap:10px`.
  The Delay section pads `12px 14px 14px`.

## Header (`Grain Header.dc.html`)

Two rows, `gap:12px`, `padding: 0 2px 16px`.

- Row 1: logo 32px (`--pui-logo-filter`), title 19px/700, tagline 9px/500
  `.12em` uppercase on `--pui-ink-dim`; preset bar right — two 28px chevron
  buttons joined to a 168px field (radius 8, only outer corners rounded,
  `margin-left:-1px` on each joint), then Save + dice as a second joined pair
  10px along.
- Row 2: `LIVE`/`FREEZE` joined pair (8px/700 `.12em`, 8×13 padding, active on
  `--pui-pill-on-bg`); right: `LEVEL` label + 88px fader (4px track, 12px
  thumb) and a 22px `PowerToggle`.

Every glyph is the repo's own SVG: `PowerIcon`, `Chevron` (`right`/`left` and
`updown` for the field), `SaveIcon variant="chrome"`, `DiceIcon`, `LinkIcon`.

## Knob (`Grain Knob.dc.html`)

One component, two of `Knob.jsx`'s variants.

**Soft / flat** (46px) — every ordinary knob. Cap `#0a0d0e` inset 4px in a
conic-gradient ring: accent from `225deg` to the value, `#2b3639` on to
`270deg`, transparent past it. Needle 2px, `#cfe4e9`, from 12% to 43% of the
dial. Caption 9px/700 `.14em` uppercase, 7px under the dial.

**Scale** (36 / 48 / 52px, `ticks`) — the lead knobs. Ring `#0b0e10`, cap
`linear-gradient(180deg,#364347,#191f22)` inset 8%, `inset 0 1px 0 rgba(185,211,217,.25)`.
Tick ring: 20 radial dashes, `-135°` to `+135°` **inclusive of both ends**, lit
in the accent up to `round(value × 19)`, unlit `#39474b`. Geometry: gap from
the dial 9px ≥60px / 6px under, dash length 6 / 4, stroke 2 / 1.4 — the same
numbers `TickScale` uses. The lit test is a per-tick integer comparison, never
an angular mask (see the note in `Knob.jsx`).

`sub` prints a permanent value line 10px/500 under the caption — Mix and
Feedback carry one, as in `COMPONENTS.md`'s "value line 4px under the caption".

Bipolar (`Detune`) lights the arc out from 12 o'clock in whichever direction it
was turned, and shows a bare track within 0.004 of centre.

## Displays

All three top displays are **50px tall, `box-sizing:border-box`** so their
borders don't make them differ; the section header rows carry
`min-height:22px` so the three start on one line whether or not they hold a
pill pair. Well: radius 5, `#101416`, `1px solid #161c1e`,
`inset 0 2px 6px rgba(0,0,0,.6)`.

| Display | Drawing |
|---------|---------|
| Grain envelope | one path, fast attack to a long decay, accent stroke 1.6 with `vector-effect="non-scaling-stroke"` (so `preserveAspectRatio="none"` doesn't thin it), 10% accent fill under it |
| Pitch weights | three 16px bars on a `#1b2529` baseline, heights from Low / Unison / High, accent at 75% |
| Random field | L/R centre line, 11 grains of 3–5px scattered either side, opacity 0.35–0.8, `L` / `R` in recess ink |
| Reverb tail | eight 5px bars falling 28 → 3px, accent at 70% |
| Time readout | 118×34 well: side letter (8px recess ink), `1/8` at 15px/700 `--pui-chrome-field-ink`, `198 ms` right in `--pui-ink-dim` — i.e. `Readout` |
| Scope | 66px, grain cloud left of a 47% divider, delay repeats fading right, faint cyan wash, `GRAINS` / `DELAY` at 8px recess ink |

## Link bracket (Left / Right times)

A 28px column, `align-self:stretch`, `position:relative`:

- upper arm: `left:9px; right:0; top:29px; bottom:71px`, `border-left` + `border-top`, radius `4px 0 0 0`
- lower arm: `left:9px; right:0; top:96px; bottom:4px`, `border-left` + `border-bottom`, radius `0 0 0 4px`
- chain button: 26px, radius 6, `--pui-chrome`, centred on the vertical edge at
  the midpoint between the two dial centres

`29px` and `96px` are the two 36px tick-knob dial centres. In the real face this
is `delay-face`'s own bracket CSS — use that rather than these numbers.

## Pills

`Pill`, 8px/700 `.12em` uppercase, `5px 8px`, radius 999.

- `MS` / `SYNC` per section that syncs (Grain drives Size + Destiny together, Delay drives its two times).
- `NORMAL` / `WIDE` / `PING PONG` under the Delay times — Peak Delay's type
  button drawn as three pills instead of one cycling button, because there is
  room here and a cycling label hides the other two settings.

## Power toggles

22px in 1a, 20px in 1c, `PowerToggle` with the section accent, on **Delay and
Reverb only**. A switched-off section should dim its body to `0.42` over
`140ms` and keep the ring lit, exactly as `ModulePanel.css` does.
