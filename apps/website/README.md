# BitBit Audio website

The marketing site: **Home**, **Alpine** and **Grains**. Built from
`design_handoff_bitbit_website/` (see that folder's `README.md` for the design
tokens and the original HTML references).

```bash
npm run dev   --workspace bitbit-website   # http://localhost:3200
npm run build --workspace bitbit-website
```

Unrelated to the CMake/JUCE build, like the rest of the npm workspace.

## Layout

- `src/data.js` — every piece of copy and every price. The four Alpine modules
  with their engines, the Grains sections, the demo presets and the spec table
  all live here; the pages only arrange them.
- `src/pages/` — `Home`, `Alpine`, `Grains`. Routed with `react-router-dom`
  (`BrowserRouter`, so a static host needs an SPA fallback to `index.html`).
- `src/components/` — `Nav` (Alpine and Grains only, plus the Buy pill),
  `AbPlayer`, `ModuleExplorer`, `ModuleDetail` (Alpine's per-module sections,
  with the engine list swapping the screenshot), `FeatureDetail` (the Grains
  equivalent), `ChainReorder`, `BuyCta`, `Footer`.
- `src/styles.css` — the handoff's tokens as CSS variables plus the `bb-*`
  classes. Per-module accent colours stay inline, since they come from the data.
- `public/assets/` — plugin screenshots, copied from the handoff. Referenced
  with a leading slash so they resolve from `/alpine` and `/grains` too.

## Not wired up yet

- **The A/B player is silent.** No audio files exist; play/pause only drives the
  waveform animation. `AbPlayer` is where the real clips go.
- **Every Buy button is `href="#"`.** Point them at the store when there is one.
- Grains has two screenshots for five sections, so `FeatureDetail` crops one
  capture per section (`focus` / `zoom` / `aspect` in `data.js`) rather than
  repeating the same image. Replace them with real per-section captures and the
  crop fields can go.
