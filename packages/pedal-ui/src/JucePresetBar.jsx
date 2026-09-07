import { useCallback, useEffect, useRef, useState } from "react";
import * as Juce from "juce-framework-frontend";
import PresetBar from "./PresetBar.jsx";

// The five native functions ee/plugin/PresetBridge.h registers. Resolved once
// per page rather than per render: getNativeFunction only builds a wrapper,
// but it also warns for a name the backend has not registered, and a pedal
// that has not wired the bridge up should say so once, not on every keystroke.
const call = {};
const nativeFunction = (name) => (call[name] ??= Juce.getNativeFunction(name));

// What the bar shows before the first answer comes back, and forever in a
// plain browser tab where nothing will answer at all. Empty rather than
// invented: an invented preset is one you can pick and that then does nothing.
const EMPTY = { factory: [], user: [], currentKind: "factory", currentName: "", canAuthor: false };

/** Every bridge call answers with the whole list plus the current selection
    (see PresetBridge.h), so one shape of reply keeps the bar in sync however
    it was reached. `ok`/`error` ride along on the mutating ones. */
function usePresetBridge() {
  const [state, setState] = useState(EMPTY);

  // A reply that arrives after the editor closed - a save that took a moment,
  // a host tearing the view down mid-call - must not set state on a component
  // that is gone.
  const live = useRef(true);

  const apply = useCallback((payload) => {
    if (live.current && payload && Array.isArray(payload.factory)) setState(payload);
    return payload;
  }, []);

  useEffect(() => {
    live.current = true;
    nativeFunction("presetList")().then(apply);
    return () => {
      live.current = false;
    };
  }, [apply]);

  const load = useCallback(({ kind, name }) => nativeFunction("presetLoad")(kind, name).then(apply), [apply]);
  const step = useCallback((delta) => nativeFunction("presetStep")(delta).then(apply), [apply]);
  const save = useCallback((kind, name) => nativeFunction("presetSave")(kind, name).then(apply), [apply]);

  return { state, load, step, save };
}

/**
 * The preset bar, wired to the processor's `ee::plugin::PresetStore` through
 * the native bridge. This is the drop-in: a pedal whose editor wraps its
 * WebBrowserComponent options in `ee::plugin::presetBridge(...)` gets a
 * working preset section by putting
 *
 *     headerCenter={<JucePresetBar />}
 *
 * on its Card, and nothing else - no props, no per-pedal list, no callbacks.
 * The pedal's own factory bank is whatever XML sits in its `presets/` folder,
 * which cmake compiles in on its own (see cmake/AddPeakPlugin.cmake).
 *
 * Loading a preset says nothing to the knobs. It replaces the whole APVTS
 * tree, and every WebSliderRelay attachment on the face is already listening
 * to its own parameter - so the face redraws itself for the same reason it
 * does when a host automates something.
 */
export default function JucePresetBar() {
  const { state, load, step, save } = usePresetBridge();

  return (
    <PresetBar
      factory={state.factory}
      user={state.user}
      value={{ kind: state.currentKind, name: state.currentName }}
      canAuthor={state.canAuthor}
      onLoad={load}
      onStep={step}
      onSave={save}
    />
  );
}
