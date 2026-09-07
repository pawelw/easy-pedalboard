# Peak Delay — WebView UI

The same `juce::WebBrowserComponent` + React approach as Peak Wah's face (see
`plugins/peak-wah/jsui/README.md` for the full rationale) - JUCE 8's own
web-view relay/attachment classes, no third-party bridge.

## Dev loop

**The face is served out of `jsui/dist` by default.** Build it once after
checkout, and again after any change to `src/`, or the plugin opens on a
notice saying so:

```bash
npm run build --prefix plugins/peak-delay/jsui
```

That is the mode an installed plugin runs in: no dev server, nothing to
remember to start. A face that pointed at Vite by default is how you get a
blank plugin window in a DAW.

For hot reload while iterating on `src/`, configure with the dev-server flag
and run Vite alongside the build:

```bash
cmake --preset fast -DEE_PLUGINS="peak-delay" -DEE_JSUI_DEV_SERVER=ON
npm run dev --prefix plugins/peak-delay/jsui   # http://localhost:3001
```

Different port from Peak Wah's (3000) so both dev servers can run at once.
The editor then points at `localhost:3001` and picks up edits live - no C++
rebuild between changes to `src/App.jsx` or the CSS. If the server is not
running, the editor falls back to `dist/` rather than showing WKWebView's
"cannot connect" page. **Never install a dev-server build** - `EE_INSTALL_PLUGINS`
is on outside the `fast` preset, so a full build overwrites what is in
`~/Library/Audio/Plug-Ins`.

## What crosses the bridge

Parameters travel the ordinary way, on `WebSliderRelay`/`WebToggleButtonRelay`
pairs declared in `PeakDelayWebEditor.h`. Three things are not parameters and
need their own crossing:

- **`formatKnobValue`** (native function) - a knob's display text. The relays
  carry only start/end/skew/interval, never the format string, so the page asks
  C++ for the string. It also answers two ids that are not parameters at all,
  `ltimeMs`/`rtimeMs`, for the Readout's small always-milliseconds figure.
- **`getDelayTimesMs`** (native function) - both delay times as numbers, the
  TapScope's time axis. A knob position alone doesn't say what it means: the
  Sync pill and the host tempo do.
- **`delayMeter`** (event, 45 Hz) - `{ level, strikes }` from the processor's
  input meter. `strikes` is a monotonic count of note onsets, and it is the
  only thing that makes the TapScope move: a face with nothing playing into it
  is a still picture, on purpose. Same shape as Peak Wah's `filterMod` feed.

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
