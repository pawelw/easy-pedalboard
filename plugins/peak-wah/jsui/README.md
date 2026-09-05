# Peak Wah — WebView UI spike

An experiment: replace `ee::ui::PedalEditor` with a `juce::WebBrowserComponent`
face written in React, using JUCE 8's built-in web-view relay/attachment
classes instead of a third-party framework (no react-juce/Blueprint — that
project is stale; see the branch's commit history for why).

## Dev loop

```bash
cd plugins/peak-wah/jsui
npm install
npm run dev      # Vite dev server on http://localhost:3000
```

With the dev server running, build and launch the Standalone Peak Wah as
usual (`cmake --preset fast -DEE_PLUGINS="peak-wah"`, then run the built app).
Its editor points at `localhost:3000` and picks up edits live - no C++
rebuild between changes to `src/App.jsx` or the CSS.

For a build that does not need the dev server running, `npm run build`
writes `jsui/dist/`, and `PeakWahWebEditor` falls back to serving that
directory straight off disk via a resource provider (see
`WebEditor::getResource`) when it cannot reach the dev server. There is no
packaging step yet - `dist/` is not embedded into the plugin binary via
BinaryData, so this only works on a machine that still has the checkout.
That is the next thing to solve if this spike is kept.

## `vendor/juce-framework-frontend/`

The `Juce.getSliderState()` / `getToggleState()` JS API isn't published to
npm - it only ships inside the JUCE source tree, at
`modules/juce_gui_extra/native/javascript/`. This is a straight copy of that
folder from the JUCE version this repo pins (`GIT_TAG 8.0.15` in the
top-level `CMakeLists.txt`). If that pin moves, re-copy it - nothing checks
that these stay in sync.

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
