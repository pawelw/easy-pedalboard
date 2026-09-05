# Pedal Gallery

Dev-only: browse every pedal's WebView face in one browser tab, without a
DAW or the compiled plugin. Not a real app - no audio, no host, no
parameters that do anything beyond what the face itself simulates.

```bash
cd apps/pedal-gallery
npm install
npm run dev      # http://localhost:3100
```

## Why this doesn't become a second UI to maintain

Each tile in `src/pedals.js` points at that pedal's **own real** `App.jsx`,
imported straight from its `jsui` project (e.g. `peak-wah-jsui/src/App.jsx`).
Nothing here re-implements a pedal's face - the gallery is only a home page
and a hash router (`#peak-wah`) around whatever each pedal's jsui already
exports. When a pedal's face changes, the gallery shows the change for free
on its next reload.

## Adding a pedal once it gets a jsui

1. Add its jsui project to this app's `package.json` dependencies (matching
   the `name` field in that pedal's `jsui/package.json`).
2. In `src/pedals.js`, import its `App.jsx` and set `face` on that pedal's
   entry (it starts as `null`).

That's the entire maintenance cost - one dependency line and one import per
pedal, not a parallel UI.

## Port

3100, not 3000 - `:3000` is Peak Wah's own dev server, which its compiled
plugin's `PeakWahWebEditor` points at directly. Reusing it here would starve
the real plugin of its dev server.
