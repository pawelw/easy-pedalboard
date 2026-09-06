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

`<Readout label="Left" value="1/8" unit="250 ms" />`

Row, `flex:1`, padding `8px 11px`, radius 10 px, background
`var(--pui-recess)`, `box-shadow: var(--pui-well-shadow)`. Label: fixed 34 px
column, 9px/700, .14em, `--pui-ink-soft`. Value: `flex:1`, 19px/700,
`--pui-ink`. Unit: 9.5px/500, `--pui-ink-dim`.

## 3. `TimeControl` — knob + readout pair (Delay-local is fine)

`display:flex; align-items:center; gap:11px` — a 42 px `Knob variant="scale"`
then a `Readout`. Column width 236 px, 22 px between the two rows.

## 4. `TapScope` — stereo repeat visualiser

`<TapScope height={78} leftTime01 rightTime01 feedback01 />`

- Well: radius 12 px, `background: var(--pui-recess)`, `box-shadow: var(--pui-well-shadow)`,
  `overflow:hidden`, 1 px centre line `var(--pui-scope-grid)`.
- `L` / `R` corner labels, 8px/700, .14em, `--pui-recess-ink`, 10 px in.
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
<StageControl label="PRE-STAGE" name="TAPE" value={v} valueLabel="22 %" icon={<TapeIcon/>} tone="tape" />
```

Row: `display:flex; align-items:center; gap:11px`; the text block is
`flex-direction:column; gap:4px` — name 10px/700 .1em uppercase, value
10px/500. 16 px between the stage label and the knob.

The stage label, name, value, tick and knob colours come from the tone (see
`--pui-tape-*` in TOKENS.md) so the same component renders the green tape half
and the onyx mod half.

Keep the existing vertical `Slider` untouched — Peak EQ/Wah rely on it, and
this face no longer uses a horizontal one.

## 6. `Pill` — icon/label toggle button

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

## 8. `SectionLabel`

9px/700, .16em, uppercase. `PRE-STAGE` (Tape, in front of the delay) uses
`var(--pui-tape-ink-soft)` on the green band; `POST-STAGE` (Mod, inside the
feedback loop) uses `var(--pui-ink-soft)` — **not** `--pui-recess-ink`, which
fails contrast at 9 px. The two halves must read as peers.

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
├─ row  gap 24
│   ├─ Knob 84  Mix        (col 104)
│   ├─ Knob 84  Feedback   (col 104)
│   └─ col 236  gap 22
│        ├─ TimeControl  LEFT  1/8   250 ms
│        ├─ TimeControl  RIGHT 1/8T  167 ms
│        └─ pills  [LINKED] [MS]      gap 7
└─ footer: one row, split in half, top border 1px var(--pui-divider), mt 20
    ├─ LEFT half   flex:1, full-bleed green band — background var(--pui-tape-band),
    │              margin 0 0 -22px -26px, padding 16px 20px 20px 26px
    │              (bleeds to the card's left and bottom edge; the card shell is
    │               overflow:hidden with the padding on an inner div, so the band
    │               picks up the 20px bottom-left corner radius)
    │              TapeIcon · PRE-STAGE · Knob42 · TAPE / 22 %
    └─ RIGHT half  flex:1, margin 0 -26px -22px 0, padding 16px 26px 20px 20px
                   ModIcon · POST-STAGE · Knob42 · MOD / 30 %
```
