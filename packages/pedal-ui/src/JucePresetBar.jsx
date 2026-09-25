import { useCallback, useEffect, useRef, useState } from "react";
import * as Juce from "juce-framework-frontend";
import PresetBar from "./PresetBar.jsx";
import TunerDialog from "./TunerDialog.jsx";
import EqDialog, { useEqEngaged } from "./EqDialog.jsx";
import { usePedalTheme, useSetPedalTheme } from "./Provider.jsx";

// The six native functions ee/plugin/PresetBridge.h registers. Resolved once
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
  const randomize = useCallback(() => nativeFunction("presetRandomize")().then(apply), [apply]);

  return { state, load, step, save, randomize };
}

/** The tuner's half of the bridge: `tunerSetOpen` / `tunerSetMute`, and the
    45 Hz "tuner" event the editor emits while it is open. Opening is what
    starts the processor capturing, so the dialog being on screen and the
    capture running are the same fact. */
function useTunerBridge() {
  const [open, setOpen] = useState(false);
  const [mute, setMute] = useState(false);
  const [reading, setReading] = useState(null);

  const applyState = useCallback((state) => {
    if (state && typeof state.mute === "boolean") setMute(state.mute);
  }, []);

  useEffect(() => {
    if (!open || typeof window.__JUCE__?.backend?.addEventListener !== "function") return undefined;
    const id = window.__JUCE__.backend.addEventListener("tuner", (event) => {
      setReading(event);
      if (typeof event?.mute === "boolean") setMute(event.mute);
    });
    return () => window.__JUCE__.backend.removeEventListener(id);
  }, [open]);

  const show = useCallback(() => {
    setReading(null);
    setOpen(true);
    nativeFunction("tunerSetOpen")(true).then(applyState);
  }, [applyState]);

  const hide = useCallback(() => {
    setOpen(false);
    nativeFunction("tunerSetOpen")(false).then(applyState);
  }, [applyState]);

  const setMuted = useCallback(
    (value) => {
      setMute(value);
      nativeFunction("tunerSetMute")(value).then(applyState);
    },
    [applyState],
  );

  return { open, mute, reading, show, hide, setMuted };
}

/** Reads useEqEngaged and reports it up. A component of its own so its ~45
    relay subscriptions exist only on a bar that has an EQ button - a hook
    cannot be called conditionally on `showEq`. */
function EqEngagedProbe({ onChange }) {
  const engaged = useEqEngaged();
  useEffect(() => onChange(engaged), [engaged, onChange]);
  return null;
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
 * which cmake compiles in on its own (see cmake/AddBitBitPlugin.cmake).
 *
 * Loading a preset says nothing to the knobs. It replaces the whole APVTS
 * tree, and every WebSliderRelay attachment on the face is already listening
 * to its own parameter - so the face redraws itself for the same reason it
 * does when a host automates something.
 *
 * `variant` is PresetBar's, forwarded. The only prop there is, and it is here
 * because how the bar is *drawn* is the face's decision while everything else
 * about it comes off the bridge. `showSteppers` and `showDice` are PresetBar's
 * too, forwarded for the same reason.
 *
 * `showTuner` adds the tuner button and its dialog. Only for a pedal whose
 * editor registers the tuner's native functions (BitBit Alpine's does - see
 * BitBitAlpineWebEditor); anywhere else the button would open a meter that
 * never moves.
 *
 * `showThemeSwitch` adds the palette switch beside it, flipping the enclosing
 * provider between its dark and light faces. Nothing native is involved and
 * nothing is remembered: it is a look, not a parameter, so it is not in the
 * state a host saves.
 *
 * `showEq` adds the pre-EQ button after it, and its dialog. Only for a pedal
 * whose processor has the `eq.*` parameters (BitBit Alpine's - see its
 * Params.h); the dialog binds them by id, so anywhere else it would draw a
 * graph that moves nothing.
 */
export default function JucePresetBar({
  variant,
  showSteppers,
  showDice,
  showTuner = false,
  showThemeSwitch = false,
  showEq = false,
}) {
  const { state, load, step, save, randomize } = usePresetBridge();
  const tuner = useTunerBridge();
  const [eqOpen, setEqOpen] = useState(false);
  const [eqEngaged, setEqEngaged] = useState(false);
  const closeEq = useCallback(() => setEqOpen(false), []);
  const theme = usePedalTheme();
  const setTheme = useSetPedalTheme();

  return (
    <>
      <PresetBar
        factory={state.factory}
        user={state.user}
        value={{ kind: state.currentKind, name: state.currentName }}
        canAuthor={state.canAuthor}
        variant={variant}
        showSteppers={showSteppers}
        showDice={showDice}
        onLoad={load}
        onStep={step}
        onSave={save}
        onRandomize={randomize}
        onTuner={showTuner ? tuner.show : undefined}
        onTheme={showThemeSwitch ? () => setTheme(theme === "onyx" ? "light" : "onyx") : undefined}
        onEq={showEq ? () => setEqOpen(true) : undefined}
        eqEngaged={eqEngaged}
      />
      {showEq && <EqEngagedProbe onChange={setEqEngaged} />}
      {showEq && <EqDialog open={eqOpen} onClose={closeEq} />}
      {showTuner && (
        <TunerDialog
          open={tuner.open}
          reading={tuner.reading}
          mute={tuner.mute}
          onMute={tuner.setMuted}
          onClose={tuner.hide}
        />
      )}
    </>
  );
}
