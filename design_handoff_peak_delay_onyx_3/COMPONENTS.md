# Components

Everything below goes in `packages/pedal-ui/src` as a `.jsx` + `.css` pair,
exported from `index.js`, styled only through the tokens in `TOKENS.md` so both
themes come out right. Controlled components: `value` 0..1 in,
`onChange(next)` / `onDragStart` / `onDragEnd` out — no JUCE inside `pedal-ui`.

## 1. `Knob` — new `variant="scale"`

The existing knob (scalloped dark collar + outer value arc) is
`variant="collar"` and stays the default, so **Peak Wah is untouched**. Add a
second variant used by the whole Delay face:

- Cap: full-size disc, `background: linear-gradient(180deg, var(--pui-cap-grad-top), var(--pui-cap-grad-bottom))`
  inset 8 % inside a `var(--pui-knob-body)` ring, ring shadow `var(--pui-knob-shadow)`,
  cap rim light `inset 0 1px 0 var(--pui-cap-ring)`.
- Pointer: a rounded bar, `3px` wide (`2.4px` under 60 px), from `top:10%`,
  height `26%` of the knob, `var(--pui-pointer)`, rotated `-135° + value·270°`.
- Tick scale instead of an arc: 20 ticks over the 270° of travel, drawn as a
  ring 9 px outside the cap (6 px under 60 px). Ticks the value has passed are
  `var(--pui-tick-lit)`, the rest `var(--pui-tick)`.

CSS recipe for the scale (two stacked rings, one clipped by a conic wedge):

```css
.pui-knob__ticks, .pui-knob__ticks-lit { position:absolute; inset:-9px; border-radius:50%; }
.pui-knob__ticks {
  background: repeating-conic-gradient(from 225deg, var(--pui-tick) 0 1.8deg, transparent 1.8deg 13.5deg);
  mask: radial-gradient(closest-side, transparent 85%, #000 85%);
}
.pui-knob__ticks-lit { /* inline style: --v = value*270deg */
  mask: conic-gradient(from 225deg, #000 0 var(--v), transparent var(--v));
}
.pui-knob__ticks-lit > i {
  position:absolute; inset:0; border-radius:50%;
  background: repeating-conic-gradient(from 225deg, var(--pui-tick-lit) 0 1.8deg, transparent 1.8deg 13.5deg);
  mask: radial-gradient(closest-side, transparent 85%, #000 85%);
}
```

(Include the `-webkit-mask` duplicates — the plugin runs in WKWebView.)

Sizes on this face: **Mix 84**, **Feedback 84**, **Left/Right Time 42**.
Caption 11 px under the dial; value line 4 px under the caption
(`--pui-ink-soft`). Column width 104 px for the big pair.

## 2. `Readout` — recessed value display

`<Readout label="L" value="1/8" unit="250ms" />`

Row, **fixed `width:112px`** with `box-sizing:border-box` (two stacked
displays of the same role must share an edge — content sizing makes `1/8T`
wider than `1/8` and leaves a ragged right edge), padding `7px 10px`,
radius 9 px, gap 7 px, the unit pushed right with `margin-left:auto`, background
`var(--pui-recess)`, `box-shadow: var(--pui-well-shadow)`. Label: `L` / `R`,
8.5px/700, .12em, `--pui-ink-soft`. Value: 17px/700, `--pui-ink`. Unit:
9px/500, `--pui-ink-dim` (e.g. `250ms`, no space — the row is narrow).

## 3. `TimeControl` — knob + readout pair (Delay-local is fine)

`display:flex; align-items:center; gap:11px` — a 42 px `Knob variant="scale"`
then a `Readout`. Column width 236 px, 22 px between the two rows.

## 4. `TapScope` — stereo repeat visualiser

`<TapScope height={78} leftTime01 rightTime01 feedback01 />`

- Well: radius 12 px, `background: var(--pui-recess)`, `box-shadow: var(--pui-well-shadow)`,
  `overflow:hidden`, 1 px centre line `var(--pui-scope-grid)`.
- `L` / `R` corner labels, 8px/700, .14em, `var(--pui-ink-soft)`, 10 px in.
  (`--pui-recess-ink` is 2.8:1 on the well — too dark for 8 px type; reserve it
  for hairlines only.)
- Taps: 4 px wide, radius 2 px, `var(--pui-ink)`; left-channel taps hang from
  the top half, right-channel from the bottom, alternating along the lane
  (inset 20 px each side, `justify-content:space-between`).
- Reference heights (px, in order): 30, 24, 22, 17, 15, 11, 9, 7 — i.e. each tap
  ≈ 0.82 × the previous. Derive from `feedback01` when wired: tap *n* height =
  `maxH · feedback01^n`, and drop taps below ~5 px.
- Horizontal position should come from the two times (left taps at multiples of
  `leftTime01`, right at multiples of `rightTime01`) so 1/8 vs 1/8T visibly
  differ. Even spacing is acceptable for v1.
- Fade animation: `@keyframes tapfade { 0% {opacity:.95} 100% {opacity:.08} }`,
  `2.4s linear infinite`, per-tap delay `n × 0.18s`.

## 5. `StageControl` — knob + caption + value (replaces the stage sliders)

Tape and Mod are **not** sliders. Each is a 42 px `Knob variant="scale"` (the
same knob as the time controls) with a two-line block beside it:

```
<StageGroup label="PRE-STAGE" icon={<TapeIcon/>} tone="tape">
  <StageControl name="WEAR"    value={wear}    valueLabel="22 %" />
  <StageControl name="FLUTTER" value={flutter} valueLabel="44 %" />
</StageGroup>
```

Row: `display:flex; align-items:center; gap:11px`; the text block is
`flex-direction:column; gap:4px` — name 10px/700 .1em uppercase, value
10px/500. 16 px between the stage label and the knob.

Stage **values** use `var(--pui-ink-soft)` (`#8ba3a9`, 6.4:1) on the onyx half
and `var(--pui-tape-lit)` on the green half — the same weight of ink as the
Mix/Feedback value lines. Do **not** use `--pui-ink-dim` (`#6c8288`) here: it
measures 4.2:1 on the panel and makes the mod half read as disabled next to the
green one. `--pui-ink-dim` is only for text sitting on the darker
`--pui-recess` (the readout `ms` units).

The stage label, name, value, tick and knob colours come from the tone (see
`--pui-tape-*` in TOKENS.md) so the same component renders the green tape half
and the onyx mod half.

**Footer structure.** Each half is `flex:1 1 50%; min-width:0;
box-sizing:border-box` — a true half, not `flex:1`, whose min-content would let
the two-knob green side push the divide off-centre. Inside, the icon + stage
label sit on their own line with the knob row 11 px below (`gap:14px` between
knob pairs), and every label carries `white-space:nowrap`. Each half holds **two** knobs:
`WEAR` + `FLUTTER` on the tape side, `CHORUS` + `PHASER` on the mod side.
Keep them paired — the 50/50 balance depends on it.

Keep the existing vertical `Slider` untouched — Peak EQ/Wah rely on it, and
this face no longer uses a horizontal one.

## 6a. `LinkBrace` — the `sync` toggle

Sits between Feedback and the time column, `position:relative; width:26px;
height:106px` (the height of two 42 px rows plus the 22 px gap), `margin-top:2px`.

- Spine: `left:0; top:21px; bottom:21px; width:1.5px`, `var(--pui-ink-dim)`.
- Two arms: `top:21px` and `bottom:21px`, `left:0; right:-12px; height:1.5px` —
  the negative right inset lets them cross the row gap and **meet the knob
  edges**. Arms that stop short read as a floating icon, not a brace.
- Button: 26 px, radius 8, centred on the spine (`left:-3px; top:50%;
  margin-top:-13px`), `background: var(--pui-ink)` with the `LinkIcon` in
  `var(--pui-panel)`, shadow `0 2px 6px rgba(0,0,0,.45)`.
- Unlinked state: button unfilled (`1px solid var(--pui-outline)`, icon in
  `--pui-ink-soft`) and the spine/arms drop to `var(--pui-line)`.

## 6b. `UnitSwitch` — the `timeunit` toggle

A two-segment switch, not a lone pill: `TIME IN` label (9px/700, .16em,
`var(--pui-ink-soft)`) then a track `padding:2px; border-radius:999px;
background: var(--pui-recess); box-shadow: var(--pui-well-shadow)` holding
`SYNC` and `MS`. Active segment: `background: var(--pui-ink)`, ink
`var(--pui-panel)`, radius 999px, padding `5px 13px`. Inactive: transparent,
ink `var(--pui-ink-soft)`. Sits 8 px under the second time row.

## 6c. `Pill`
— icon/label toggle button

`<Pill icon={<LinkIcon />} label="Linked" pressed />` and `<Pill label="ms" />`.

Padding `6px 10px`, radius 999 px, gap 6 px. Pressed: background
`var(--pui-pill-on-bg)`, ink `var(--pui-pill-on-ink)`, no border. Unpressed:
transparent, `1px solid var(--pui-pill-off-border)`, ink
`var(--pui-pill-off-ink)`. Label 9px/700 .1em uppercase. Build it on the
existing `Button` (Mantine `unstyled`) so focus/ARIA come free.

## 7. `LinkIcon`

Port of `drawLinkIcon` (`plugins/peak-delay/src/PluginProcessor.cpp`): two
rounded capsules on a 45° axis, overlapping in the middle. On a 20×20 viewBox:
two `rect`s `w 6.8 h 10.4 rx 3.4` at `x 6.6`, `y 2.4` and `y 7.2`, the pair
rotated 45° about the centre; `fill:none`, `stroke: currentColor`,
`stroke-width 1.7` (1.9 at ≤14 px). Sizes used: 13 px in a pill, 15–18 px
elsewhere.

## 8. `StageRouter` — the section header (replaces `SectionLabel`)

Each footer section now names **what it is** (`TAPE`, `MOD`) and carries its own
routing control, because the stage order is user-configurable: either section can
sit before or after the delay.

```
<StageRouter icon={<TapeIcon/>} name="TAPE" placement="pre" onStep={dir => …} tone="tape" />
```

Row: `display:flex; align-items:center; gap:10px` — icon (30 px), name
(9px/700, .16em, uppercase, `nowrap`, `--pui-tape-ink` / `--pui-ink`), then the
switcher.

**Switcher** — a recessed well, `padding:3px 4px; border-radius:7px`,
`background` `--pui-tape-well` (`#2b4410`) on the green half and
`--pui-recess` on the onyx half, `box-shadow: var(--pui-well-shadow)`,
`gap:2px`. Inside: a left chevron button, the value, a right chevron button.

- Chevron buttons: 16×16, `border-radius:5px`, transparent until
  `:hover`/`:focus-visible` (then `background: rgba(255,255,255,.08)`). Glyph is
  a 7×10 SVG path, `stroke: var(--pui-tape-lit)` / `var(--pui-ink-soft)`,
  `stroke-width 1.7`, round caps: `M5.2 1.2 L1.6 5 L5.2 8.8` (left) and
  `M1.8 1.2 L5.4 5 L1.8 8.8` (right). Hit area is 16 px in the mock — pad to
  ≥28 px in the real UI, the plugin is mouse-driven but the target is small.
- Value: `min-width:26px; text-align:center`, 9px/700, .14em, `nowrap`,
  `--pui-tape-ink` / `--pui-ink`. Reads `PRE` or `POST`.
- Both arrows step the same two-state parameter, so left and right are
  equivalent here — keep both anyway: the pair is what signals "this is
  configurable". Wrap around; never disable an arrow.

## 8b. `TapeIcon` / `ModIcon`

Both 38×31 rendered, stroke `currentColor`.

- `TapeIcon` — viewBox `0 0 44 36`: deck body `rect 3.5,16.5 37×14 rx2`, two
  feet `4.5×2.4 rx1` at y 30.5, head-cover `rect 15,21.5 14×7.2 rx1.6` with two
  `r .85` filled screws at y 24; two reels `r 10` at (11,11) and (33,11) filled
  with the band colour so they occlude the deck, each with three filled spokes
  (120° apart, inner r 4.5 → outer r 9, ~34° wide) and a `r 3.2` hub ring.
  Stroke-width 1.6.
- `ModIcon` — viewBox `0 0 44 36`: one sine cycle drawn three times at x
  offsets 0 / +5 / +10, opacity 1 / .55 / .3, stroke-width 3.4, round caps —
  the phase-smear reading of modulation. Front path:
  `M1 24 C6 6, 14 6, 19 18 C24 30, 32 30, 37 12`. The back copy runs past the
  viewBox on the right, which is intended — it reads as continuing motion.

## 9. `Logo` — token-driven tint

Replace the hard-coded `brightness(0)` with `var(--pui-logo-filter)` so the
mark reads light on the onyx panel. Keep the `level` glow behaviour.

## 10. `PedalUIProvider` — theme prop

`<PedalUIProvider theme="onyx">` renders a wrapper with
`data-pui-theme="onyx"` (default `"light"` sets nothing, so `:root` applies and
Wah is byte-identical). Also set `background: var(--pui-page)` on the page
wrapper rather than in each plugin's `index.css`.

## Face layout (Peak Delay)

```
Card  568px wide (padding 22px 26px 24px, radius 20)
├─ header            logo 22 · title 19/700 · meta right           (pb 20)
├─ TapScope h78                                                    (mb 22)
├─ row  gap 12  (no space-between — Feedback sits close to the time area)
│   ├─ Knob 84  Mix        (col 96)
│   ├─ Knob 84  Feedback   (col 96)
│   ├─ LinkBrace  26×106
│   └─ col flex:1  gap 22
│        ├─ TimeControl  LEFT  1/8   250 ms
│        ├─ TimeControl  RIGHT 1/8T  167 ms
│        └─ UnitSwitch  TIME IN [SYNC | MS]   (margin-top -8)
└─ footer: one row, split in half, top border 1px var(--pui-divider), mt 20
    ├─ LEFT half   flex:1, full-bleed green band — background var(--pui-tape-band),
    │              margin 0 0 -22px -26px, padding 16px 20px 20px 26px
    │              (bleeds to the card's left and bottom edge; the card shell is
    │               overflow:hidden with the padding on an inner div, so the band
    │               picks up the 20px bottom-left corner radius)
    │              StageRouter  TapeIcon · TAPE · ‹ PRE ›
    │              knob row     Knob42 WEAR 22 %   ·   Knob42 FLUTTER 44 %
    └─ RIGHT half  flex:1 1 50%, min-width:0, margin 0 -26px -22px 0,
                   padding 14px 26px 18px 20px
                   StageRouter  ModIcon · MOD · ‹ POST ›
                   knob row     Knob42 CHORUS 30 %  ·  Knob42 PHASER 18 %
```
