# Paste this into Claude Code (run it from the repo root)

Do the work in five commits, in order, and stop after each for review.

---

I want to add a dark theme to `packages/pedal-ui` and rebuild the Peak Delay
face as a WebView UI on a new layout. The full design spec is in
`design_handoff_peak_delay_onyx/` — read `README.md`, `TOKENS.md`,
`COMPONENTS.md` and `BUILD_PLAN.md` first, and open
`peak-delay-onyx-face.html` in a browser to see the target.

Hard constraints:

- Peak Wah must render exactly as it does today. The current `pedal-ui` tokens
  stay the default theme, and the existing `Knob` (scalloped collar + value
  arc) stays the default variant. Take a screenshot of Wah in the gallery before
  you change anything and compare after every phase.
- Theme is selected in code, not in the UI: `<PedalUIProvider theme="onyx">`.
  The Delay face must render correctly on either theme by changing that prop.
- No new npm dependencies. Keep `-webkit-mask` / `-webkit-user-select`
  fallbacks — this runs in WKWebView.
- Follow the package's existing conventions: CSS-variable tokens, `pui-` class
  names, one `.css` per component, controlled 0..1 value props, and no JUCE
  imports inside `pedal-ui` (bindings live in the plugin's `juceBindings.jsx`).

Phase 1: theming plumbing + the missing components on the light theme.
Phase 2: the onyx token block, and make every existing component token-driven.
Phase 3: `plugins/peak-delay/jsui` + a `PeakDelayWebEditor` modelled on
`PeakWahWebEditor`, with the new layout, registered in the pedal gallery.
Phase 4: switch Delay to `theme="onyx"`.
Phase 5: confirm Wah is unchanged.

Start with Phase 1 and show me a diff plan before writing code.
