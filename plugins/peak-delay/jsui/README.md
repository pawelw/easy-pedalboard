# Peak Delay — WebView UI

The same `juce::WebBrowserComponent` + React approach as Peak Wah's face (see
`plugins/peak-wah/jsui/README.md` for the full rationale) - JUCE 8's own
web-view relay/attachment classes, no third-party bridge.

## Dev loop

```bash
cd plugins/peak-delay/jsui
npm install
npm run dev      # Vite dev server on http://localhost:3001
```

Different port from Peak Wah's (3000) so both dev servers can run at once.
With the dev server running, build and launch the Standalone Peak Delay as
usual (`cmake --preset fast -DEE_PLUGINS="peak-delay"`, then run the built
app). Its editor points at `localhost:3001` and picks up edits live - no C++
rebuild between changes to `src/App.jsx` or the CSS.

For a build that does not need the dev server running, `npm run build`
writes `jsui/dist/`, and `PeakDelayWebEditor` falls back to serving that
directory straight off disk via a resource provider when it cannot reach the
dev server.

## `vendor/juce-framework-frontend/`

Lives at the repo root (`vendor/juce-framework-frontend/`) and is shared with
Peak Wah's jsui - see that README for what it is and when to re-copy it. Not
duplicated per pedal: two `file:` deps with the same package name pointing at
different targets don't hoist cleanly under npm workspaces.

## What's not here yet

The Tape knob uses the same shared `Knob` as everything else on this face.
In the old `ee::ui` editor it had its own photographic cap (a deep-green,
analog-style knob standing out from the rest of the digital face) - that
still needs a proper design pass (a per-knob cap override in
`@synthpeak/pedal-ui`) before it lands here.
