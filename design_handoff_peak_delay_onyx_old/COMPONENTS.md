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

## 5. `SliderRow` — horizontal slider with label and value

`<SliderRow name="Tape" value={v} onChange={...} valueLabel="22 %" />`

`flex:1`, `align-items:center; gap:9px`. Name 10px/700 .1em `--pui-ink`. Track:
`flex:1`, height 5 px, radius 3 px, `background: var(--pui-panel-edge)`; fill
`var(--pui-ink)` to `value`; thumb 14 px circle `var(--pui-ink)` centred on the
value. Value label 10px/500 `--pui-ink-dim`. (The existing vertical `Slider`
stays as it is — add this as a separate component or as
`orientation="horizontal"`, your call, but do not change the vertical
rendering Peak EQ/Wah rely on.)

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

9px/700, .16em, uppercase, `var(--pui-recess-ink)`. Used for `PRE-STAGE`
(Tape — it sits in front of the delay and colours the dry signal) and
`POST-STAGE` (Mod).

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
└─ divider (mt 20, pt 16, 1px var(--pui-divider))
    └─ row gap 22:  PRE-STAGE · SliderRow Tape · POST-STAGE · SliderRow Mod
```
