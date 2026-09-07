# Peak Wah — WebView UI spike

An experiment: replace `ee::ui::PedalEditor` with a `juce::WebBrowserComponent`
face written in React, using JUCE 8's built-in web-view relay/attachment
classes instead of a third-party framework (no react-juce/Blueprint — that
project is stale; see the branch's commit history for why).

## Dev loop

**The face is served out of `jsui/dist` by default** (see
`ee::plugin::webface::serveFromDist`). Build it once after checkout, and again
after any change to `src/`, or the plugin opens on a notice saying so:

```bash
npm run build --prefix plugins/peak-wah/jsui
```

For hot reload while iterating on `src/`, configure with the dev-server flag
and run Vite alongside the build:

```bash
cmake --preset fast -DEE_PLUGINS="peak-wah" -DEE_JSUI_DEV_SERVER=ON
npm run dev --prefix plugins/peak-wah/jsui   # http://localhost:3000
```

The editor then points at `localhost:3000` and picks up edits live - no C++
rebuild between changes to `src/App.jsx` or the CSS. If the server is not
running it falls back to `dist/`. Never install a dev-server build.

There is no packaging step yet - `dist/` is not embedded into the plugin
binary via BinaryData, so this only works on a machine that still has the
checkout. That is the next thing to solve if this spike is kept.

## `vendor/juce-framework-frontend/`

The `Juce.getSliderState()` / `getToggleState()` JS API isn't published to
npm - it only ships inside the JUCE source tree, at
`modules/juce_gui_extra/native/javascript/`. This is a straight copy of that
folder from the JUCE version this repo pins (`GIT_TAG 8.0.15` in the
top-level `CMakeLists.txt`). If that pin moves, re-copy it - nothing checks
that these stay in sync.

It lives at the repo root (`vendor/juce-framework-frontend/`, referenced here
via a relative `file:` dependency) rather than inside this plugin's own
`jsui/`, so every pedal's jsui project can point at the same copy - two
`file:` deps with the same package name but different targets don't hoist
cleanly under npm workspaces.

## What this does and doesn't prove

Parameter binding (`WebSliderRelay` + `WebSliderParameterAttachment`) keeps
real host automation working - these are still `AudioParameterFloat`s, not a
polling bridge. But JUCE's web-view API does not carry the parameter's C++
`stringFromValue` text (Hz, note values, "Low"/"Band"/"High") over to the
JS side automatically - the slider only gets `start`/`end`/`skew`/`interval`.
`App.jsx` gets its knob readouts through a `withNativeFunction` bridge
(`formatKnobValue`) that calls back into the same `freqReadout()` /
`timeReadout()` / `typeReadout()` C++ functions the old editor used, so the
formatting logic isn't duplicated in JS.
