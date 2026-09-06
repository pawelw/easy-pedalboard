# Design tokens — two themes

`packages/pedal-ui/src/tokens.css` today defines the palette on `:root`. Keep
those values as the **light** theme, unchanged, so Peak Wah is untouched. Add a
scoped override block for **onyx**.

```css
/* tokens.css — unchanged existing vars stay on :root, plus the new ones below */
:root {
  /* new, light values */
  --pui-page:            #dcdad2;
  --pui-recess:          #e9e7e0;   /* display well / scope bg (already exists as --pui-scope-bg) */
  --pui-recess-ink:      #8a8880;
  --pui-tick:            #cfcdc3;
  --pui-tick-lit:        #c60000;   /* = --pui-knob-sweep-lit, keeps Wah's red family */
  --pui-divider:         #cfcdc3;
  --pui-cap-grad-top:    #fdfdfc;
  --pui-cap-grad-bottom: #e6e4dd;
  --pui-cap-ring:        #d5d3ca;
  --pui-pointer:         #1c1c1a;
  --pui-pill-on-bg:      #1c1c1a;
  --pui-pill-on-ink:     #f3f2ed;
  --pui-pill-off-border: #1c1c1a;
  --pui-pill-off-ink:    #6b6a63;
  --pui-knob-shadow:     0 2px 5px rgba(0, 0, 0, 0.16);
  --pui-well-shadow:     inset 0 1px 4px rgba(0, 0, 0, 0.12);
  --pui-logo-filter:     brightness(0);
}

[data-pui-theme="onyx"] {
  --pui-page:            #0f1315;
  --pui-panel:           #171d20;
  --pui-panel-edge:      #222b2e;
  --pui-outline:         #2a3336;
  --pui-ink:             #b9d3d9;
  --pui-ink-soft:        #8ba3a9;
  --pui-ink-dim:         #6c8288;
  --pui-line:            #2a3336;
  --pui-divider:         #222b2e;

  --pui-recess:          #101416;
  --pui-recess-ink:      #4d5f65;
  --pui-tick:            #39474b;
  --pui-tick-lit:        #b9d3d9;

  --pui-knob-body:       #0b0e10;   /* the ring */
  --pui-cap-grad-top:    #364347;
  --pui-cap-grad-bottom: #191f22;
  --pui-cap-ring:        rgba(185, 211, 217, 0.25);
  --pui-pointer:         #b9d3d9;

  --pui-pill-on-bg:      #b9d3d9;
  --pui-pill-on-ink:     #171d20;
  --pui-pill-off-border: #2a3336;
  --pui-pill-off-ink:    #8ba3a9;

  --pui-knob-shadow:     0 8px 18px rgba(0, 0, 0, 0.55);
  --pui-well-shadow:     inset 0 2px 6px rgba(0, 0, 0, 0.6);
  --pui-scope-grid:      #233034;
  --pui-logo-filter:     brightness(0) invert(84%) sepia(9%) saturate(360%) hue-rotate(150deg);
}
```

The onyx values are the repo's own `PedalTheme::onyx()`
(`shared/src/ui/PedalTheme.cpp`) — the JUCE face and the web face should not
drift apart.

## Card / panel

| | light (today) | onyx |
| --- | --- | --- |
| panel bg | `#f3f2ed` + brushed-grain SVG | `#171d20`, no grain |
| border | `1px rgba(0,0,0,.14)` | `1px #2a3336` |
| radius | 14 px | 20 px |
| shadow | `0 1px 0 #e8e6de inset, 0 2px 6px rgba(0,0,0,.08), 0 14px 28px rgba(0,0,0,.14)` | `0 20px 44px rgba(0,0,0,.4)` |
| screws | shown | not shown |

Radius, grain and shadow therefore need to be token-driven too:
`--pui-radius-panel`, `--pui-panel-grain` (an `image` or `none`),
`--pui-panel-shadow`.

## Type scale (both themes, Space Grotesk)

| role | size / weight | tracking |
| --- | --- | --- |
| pedal title | 19 px / 700 | .01em |
| header meta | 9 px / 500 | .18em, uppercase |
| knob caption | 10 px / 700 | .12em, uppercase |
| knob value | 10 px / 500 | — |
| readout label | 9 px / 700 | .14em, uppercase |
| readout value | 19 px / 700 | — |
| readout unit | 9.5 px / 500 | — |
| section label | 9 px / 700 | .16em, uppercase |
| slider name | 10 px / 700 | .1em, uppercase |
| pill label | 9 px / 700 | .1em, uppercase |

## Tape band tokens

The pre-stage band is a second accent scope, not hard-coded green. Add it
alongside the onyx block so the band can be retoned (or neutralised on the light
theme) without touching markup:

```css
[data-pui-theme="onyx"] {
  --pui-tape-band:      #375916;
  --pui-tape-edge:      #46701c;
  --pui-tape-ink:       #f2f7e6;   /* name */
  --pui-tape-ink-soft:  #c9dbb0;   /* stage label */
  --pui-tape-lit:       #d8f088;   /* value, icon, lit ticks, pointer */
  --pui-tape-track:     #5a7f2a;   /* unlit ticks */
  --pui-tape-knob-body: #1d2f08;
  --pui-tape-cap-top:   #5c7f2c;
  --pui-tape-cap-bot:   #2b4410;
  --pui-tape-cap-rim:   rgba(216, 240, 136, 0.3);
}
```

`StageControl tone="tape"` reads the `--pui-tape-*` group; `tone="default"`
reads the plain `--pui-*` group. Contrast check: `--pui-tape-ink-soft` on the
band is 5.3:1; the mod half's `--pui-ink-soft` on panel is 4.9:1. Do not use
`--pui-recess-ink` for either stage label (2.6:1).
