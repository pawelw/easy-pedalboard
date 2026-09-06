# Build plan

Five phases, in this order, each its own commit. Do not start a phase before the
previous one's checks pass.

## Phase 1 — theming in `pedal-ui`, light theme completed

1. Add the new light-theme vars from `TOKENS.md` to `:root` in `tokens.css`
   (existing values untouched), plus `--pui-radius-panel`, `--pui-panel-grain`,
   `--pui-panel-shadow` so `Card` stops hard-coding them.
2. `PedalUIProvider` gains `theme = "light"` and renders the
   `data-pui-theme` wrapper.
3. Build the missing components against the light theme only:
   `Knob variant="scale"`, `Readout`, `TapScope`, `SliderRow`, `Pill`,
   `LinkIcon`, `SectionLabel`; `Logo` tint via token. Export all from
   `index.js`.
4. Add them to the gallery so they can be eyeballed:
   `apps/pedal-gallery` — a "components" tile rendering each new component at
   both themes side by side is the cheapest way to review this.

**Checks:** Peak Wah renders pixel-identically to before (compare against a
screenshot taken before you start; `Knob` default path must be unchanged code).
Every new component looks correct on the light panel.

## Phase 2 — onyx theme

1. Add the `[data-pui-theme="onyx"]` block from `TOKENS.md`.
2. Fix anything that hard-codes a colour, radius, grain or shadow instead of
   reading a token — `Card`, `Knob`, `Toggle`, `Button`, `Slider`,
   `Dropdown`, `FilterScope` — so all of them, old and new, respond to the
   theme.
3. Gallery: theme switch on the components tile.

**Checks:** every component readable on `#171d20`; contrast of `--pui-ink`
(`#b9d3d9`) on panel is fine, but check the unpressed pill and the tick track.
Light theme still unchanged.

## Phase 3 — Peak Delay jsui, new layout

1. `plugins/peak-delay/jsui/` — copy the structure of
   `plugins/peak-wah/jsui/` (Vite config, `index.html`, `main.jsx`,
   `juceBindings.jsx`, `autoSize.js`, the vendored
   `juce-framework-frontend`). Add it to the npm workspace (it already matches
   `plugins/*/jsui`).
2. C++: a `PeakDelayWebEditor` mirroring `PeakWahWebEditor` — slider relays for
   `mix`, `fb`, `ltime`, `rtime`, `mod`, `tape`; toggle relays for `sync`,
   `timeunit`; and the `formatKnobValue` native function returning
   `PeakDelayProcessor::timeReadout` for the two time params and
   `percentToText` for the rest. `createEditor()` returns it instead of
   `PedalEditor`. Keep the `EE_TAPE_TUNER` path working.
3. Build `App.jsx` to the layout in `COMPONENTS.md`.
4. Register it in `apps/pedal-gallery/src/pedals.js` (`face: PeakDelayFace`).

**Checks:** in the gallery, the face matches `peak-delay-onyx-face.html` —
allowing that it renders on the light theme at this point. Knob drags,
pills toggle, readouts show divisions and flip to ms.

## Phase 4 — Delay on onyx

`<PedalUIProvider theme="onyx">` in `plugins/peak-delay/jsui/src/main.jsx`.
Nothing else. Verify the page background comes from `--pui-page` and the
plugin window has no light seam around the card.

**Check:** switching that one prop back to `"light"` yields a correct light face
— that is the acceptance test for the theming work.

## Phase 5 — Wah untouched

Peak Wah keeps the default theme and the `collar` knob variant. Re-run whatever
you used in Phase 1 to compare it against the pre-change screenshot, plus
`tests/ee_ui_snapshot` for the C++ faces (unaffected, but cheap to confirm).

## Notes

- The C++ `PedalEditor` faces are unaffected; `PedalTheme::onyx()` already
  exists and Peak Grain uses it. Keep the web onyx tokens equal to it.
- The webview runs in WKWebView: keep `-webkit-mask` alongside `mask`, and
  `-webkit-user-select` alongside `user-select`.
- No new npm dependencies.
