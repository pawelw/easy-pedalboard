import { createContext, useContext, useEffect, useMemo, useState } from "react";
import * as Juce from "juce-framework-frontend";
import Knob from "./Knob.jsx";
import Pill from "./Pill.jsx";
import Slider from "./Slider.jsx";
import StageControl from "./StageControl.jsx";
import StageRouter from "./StageRouter.jsx";

/**
 * The generic half of a face's JUCE wiring: every control that is nothing but
 * "a pedal-ui component bound to a relay by parameter id".
 *
 * Its own entry point (`@synthpeak/pedal-ui/juce`) rather than part of the main
 * one, because the main one must stay JUCE-free - the gallery imports it, and
 * a component library that reaches for `window.__JUCE__` on load is not one.
 * Everything here is opt-in by import.
 *
 * A pedal's own `juceBindings.jsx` keeps whatever is specific to it: a native
 * function only that processor answers, a hook over a meter feed only it emits.
 * Peak Delay's live in `@synthpeak/delay-face`.
 */

// A native function, not the parameter's own C++ stringFromValue - JUCE's
// web-view relays only carry start/end/skew/interval, not the format string.
// See plugins/peak-delay/jsui/README.md.
const formatKnobValue = Juce.getNativeFunction("formatKnobValue");

/** The parameter-id prefix in force for a subtree.
 *
 * A face embedded in a multi-effect host is the same face bound to a different
 * set of parameters: Peak Delay's Mix is `mix`, and the same component inside
 * Peak Alpine's Delay module is `dly.mix`. Rather than thread an id map
 * through every control, the host declares the prefix once and the hooks below
 * resolve against it.
 *
 * The default is "", so a pedal that never wraps anything in a ParamScope is
 * bound exactly as it was before this existed. */
const ParamScopeContext = createContext("");

export function ParamScope({ prefix = "", children }) {
  return <ParamScopeContext.Provider value={prefix}>{children}</ParamScopeContext.Provider>;
}

/** A parameter id as the backend knows it: the caller's leaf name with the
    enclosing ParamScope's prefix on the front. Every hook here goes through
    this, so a control never has to know whether it is in a host or not. */
export function useParamId(parameterId) {
  return useContext(ParamScopeContext) + parameterId;
}

/** A parameter's live normalised value (0..1), kept in sync with its
    WebSliderRelay for as long as the component is mounted - not just while
    it's being dragged.

    `setValue` updates local state immediately as well as informing the
    relay, rather than waiting on the relay to echo the change back - there
    is no echo outside a real host, which is what keeps this interactive in
    the gallery and in a plain browser tab.

    **The relay state is looked up from `id` on every change of it**, not once
    per mount, because a control can be *re-pointed* rather than remounted. A
    module that swaps its engine renders the same number of knobs in the same
    places with different parameter ids; React reconciles those by position and
    key and hands the existing component new props, so a hook that latched its
    relay in a `useRef` would go on driving the engine that was there before.
    Peak Alpine's Reverb module did exactly that: selecting Spring left its
    Decay and Low Cut knobs turning `rev.space.*`, which is a control that
    moves and does nothing. The same applies to the two hooks below. */
export function useJuceSliderValue(parameterId) {
  const id = useParamId(parameterId);
  const sliderState = useMemo(() => Juce.getSliderState(id), [id]);
  const [value, setValue] = useState(() => sliderState.getNormalisedValue());

  useEffect(() => {
    // Adopt the new parameter's value before listening, because a control can
    // be *re-pointed* rather than remounted - see the note above.
    setValue(sliderState.getNormalisedValue());

    const listenerId = sliderState.valueChangedEvent.addListener(() =>
      setValue(sliderState.getNormalisedValue()),
    );
    return () => sliderState.valueChangedEvent.removeListener(listenerId);
  }, [sliderState]);

  const setNormalisedValue = (next) => {
    setValue(next);
    sliderState.setNormalisedValue(next);
  };

  return [value, setNormalisedValue, sliderState];
}

/** A parameter's live formatted text (via formatKnobValue), re-fetched
    whenever `value` changes - shared by every control here that prints one, so
    they all read the same live-readout pattern.

    Takes an already-scoped id: its callers have resolved theirs through
    useParamId, and a pedal-specific hook may pass a synthetic id that is not a
    real parameter at all (Peak Delay's "ltimeMs"). */
export function useFormattedText(scopedId, value) {
  const [text, setText] = useState("");

  useEffect(() => {
    let cancelled = false;
    formatKnobValue(scopedId).then((t) => {
      if (!cancelled) setText(t);
    });
    return () => {
      cancelled = true;
    };
  }, [scopedId, value]);

  return text;
}

/** Knob bound to a WebSliderRelay by parameter id. State is optimistic (set
    locally on drag, not only from the relay's echo) so it still works
    stand-alone in a plain browser, where there is no backend to echo it.

    `showValueLabel` (default true): Knob swaps its caption for the live
    value while dragging - suppress that for a knob whose value already has
    a full-size readout right next to it (the Time knobs' Readout), where a
    second copy popping up under the knob itself is redundant.

    `showValueBelow` (default false): the opposite kind of duplication -
    Mix/Feedback have no readout anywhere else, so they print their value
    as a permanent second line under the caption instead (Knob's own
    `subLabel`), always visible rather than only appearing mid-drag.

    `scaleFrom` is Knob's, passed straight through - "centre" for a parameter
    whose resting value is the middle of its range, so the arc reads as a
    departure from unity rather than as a level wound all the way up. */
export function JuceKnob({
  parameterId,
  caption,
  size,
  variant,
  endMarkerLabel,
  sweepGap,
  scaleFrom,
  showValueLabel = true,
  showValueBelow = false,
  bare = false,
}) {
  const id = useParamId(parameterId);
  const [value, setValue, sliderState] = useJuceSliderValue(parameterId);
  const readout = useFormattedText(id, value);

  return (
    <Knob
      variant={variant}
      size={size}
      bare={bare}
      sweepGap={sweepGap}
      scaleFrom={scaleFrom}
      caption={caption}
      endMarkerLabel={endMarkerLabel}
      value={value}
      valueLabel={showValueLabel ? readout : undefined}
      subLabel={showValueBelow ? readout : undefined}
      onChange={setValue}
      onDragStart={() => sliderState.sliderDragStarted()}
      onDragEnd={() => sliderState.sliderDragEnded()}
    />
  );
}

/** The "Easy" tab's one macro knob. It is *not* bound to a parameter of its
    own - there is no backing `easy` parameter yet; that lands once the per-
    engine target ranges are locked from the design. Turning it writes each of
    the engine's own Adv parameters to `lerp(min, max, pos)`, so one knob makes
    the whole engine's musical move at once.

    Its position is local React state: it opens at `defaultValue`, and is
    neither automatable nor saved in a preset (the Adv parameters it moves
    *are* - a preset still round-trips, the macro just re-centres on load).

    `targets` is `[{ id, min = 0, max = 1 }]`, `id` a leaf name. It is resolved
    through `idPrefix` when one is given (Peak Alpine's side modules build ids
    from the engine's own prefix, `mod.trem.`), otherwise through the enclosing
    `ParamScope` (the Artifact face, scoped `art.`). Pass a stable `targets`
    reference - a module-level constant - so the relay lookups are not rebuilt
    every render. */
export function JuceMacroKnob({
  caption,
  targets,
  idPrefix,
  size = 68,
  defaultValue = 0.5,
}) {
  const scopePrefix = useContext(ParamScopeContext);
  const prefix = idPrefix ?? scopePrefix;
  const [pos, setPos] = useState(defaultValue);

  const relays = useMemo(
    () => targets.map((t) => ({ min: 0, max: 1, ...t, state: Juce.getSliderState(prefix + t.id) })),
    [prefix, targets],
  );

  const apply = (next) => {
    setPos(next);
    for (const { state, min, max } of relays) state.setNormalisedValue(min + (max - min) * next);
  };

  return (
    <Knob
      variant="soft"
      size={size}
      caption={caption}
      subLabel={`${Math.round(pos * 100)} %`}
      value={pos}
      onChange={apply}
    />
  );
}

/** One of a header's level faders, bound to a WebSliderRelay by parameter id -
    the shared `Slider`, the control Peak EQ's bands are, laid on its side at
    header size and dragged like a knob. Same optimistic-state pattern as
    JuceKnob: the value is set locally on drag rather than waiting for a relay
    echo that only a real host sends.

    `resetTo` is a scaled value (dB here, not 0..1). Where it sits on the knob's
    travel is worked out from the relay's own start/end/skew rather than from a
    number written down twice - the range lives in the processor, and the two
    would drift the moment anyone widened it.

    `length` is the track's own length. It is a property of the header the
    fader sits in, not of the fader: a 528px pedal card has room for 64px of
    travel beside its preset bar, and a 966px host panel has room for 104. */
export function JuceFader({ parameterId, label, resetTo = 0, length = 64 }) {
  const id = useParamId(parameterId);
  const [value, setValue, sliderState] = useJuceSliderValue(parameterId);
  const valueLabel = useFormattedText(id, value);

  // Read at click time, not at render: the backend pushes the range in a
  // propertiesChanged event after the page loads, and nothing here re-renders
  // when it lands. Outside a host it never lands at all, and the JUCE shim's
  // own 0..1 defaults make this the identity - which is the right answer for a
  // fader with no parameter behind it.
  const reset = () => {
    const { start = 0, end = 1, skew = 1 } = sliderState.properties ?? {};
    const span = end - start;
    const normalised = span === 0 ? 0 : Math.pow((resetTo - start) / span, skew);

    setValue(Math.min(1, Math.max(0, normalised)));
  };

  return (
    <Slider
      compact
      fine
      orientation="horizontal"
      length={length}
      label={label}
      value={value}
      valueLabel={valueLabel}
      onChange={setValue}
      onReset={reset}
      onDragStart={() => sliderState.sliderDragStarted()}
      onDragEnd={() => sliderState.sliderDragEnded()}
    />
  );
}

/** Whether the backend has actually told the page about a toggle. The JUCE
    shim answers `false` for an id it has never heard of - which is the right
    answer for a missing parameter and the wrong one for a parameter that
    simply has no backend, as in the gallery or a plain browser tab. */
function backendKnowsToggle(id) {
  const toggles = window.__JUCE__?.initialisationData?.__juce__toggles;
  return Array.isArray(toggles) && toggles.includes(id);
}

/** A boolean parameter's live value, kept in sync with its
    WebToggleButtonRelay - the toggle counterpart of useJuceSliderValue, and
    shared by every control built on one (the pills and the stage routers).

    `setValue` updates local state as well as the relay for the same reason
    useJuceSliderValue does: outside a real host nothing echoes the change
    back, and without it the gallery's toggles would never move.

    `defaultValue` is what to show when there is no backend at all. It is not a
    default *for the parameter* - the processor owns that - only for the
    picture, and it is consulted solely for an id the backend has never
    mentioned. In a host the relay's own value is in the page's initialisation
    data before the first render, so this is never reached there.

    It exists because "off" is a bad guess for a power switch: a face whose
    every module reads bypassed the moment it is opened outside a host is not
    showing anyone the design. */
export function useJuceToggleValue(parameterId, defaultValue = false) {
  const id = useParamId(parameterId);
  const toggleState = useMemo(() => Juce.getToggleState(id), [id]);
  const [checked, setChecked] = useState(() =>
    backendKnowsToggle(id) ? toggleState.getValue() : defaultValue,
  );

  useEffect(() => {
    setChecked(backendKnowsToggle(id) ? toggleState.getValue() : defaultValue);

    const listenerId = toggleState.valueChangedEvent.addListener(() => setChecked(toggleState.getValue()));
    return () => toggleState.valueChangedEvent.removeListener(listenerId);
    // `defaultValue` is deliberately not a dependency: it is the picture to
    // draw for a parameter with no backend, not a value to re-adopt if a
    // caller happens to pass a fresh one on a later render.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [toggleState, id]);

  const setValue = (next) => {
    setChecked(next);
    toggleState.setValue(next);
  };

  return [checked, setValue];
}

/** Toggle bound to a WebToggleButtonRelay by parameter id, rendered as the
    shared Pill (Linked/Sync's round-cornered chip) rather than a full-width
    switch - the compact control the onyx layout uses everywhere a Toggle
    would otherwise go.

    `invert`: shows the pill lit when the parameter is *false* rather than
    true - for a parameter whose own sense reads backwards against its
    label ("timeunit" is "is this in ms mode", but the pill reads "Sync",
    lit for the opposite: synced to tempo). Only the display flips; a click
    still just flips the real boolean either way. */
export function JucePill({ parameterId, icon, label, invert = false }) {
  const [checked, setChecked] = useJuceToggleValue(parameterId);

  return (
    <Pill
      icon={icon}
      label={label}
      pressed={invert ? !checked : checked}
      onClick={() => setChecked(!checked)}
    />
  );
}

/** A choice parameter's live index, kept in sync with its WebComboBoxRelay -
    the third of the three "live value" hooks here, alongside
    useJuceSliderValue and useJuceToggleValue, and optimistic for the same
    reason: outside a real host nothing echoes a change back.

    `count` comes from the caller rather than from the relay's own
    properties.choices, because those cannot be relied on: the backend pushes
    them in a propertiesChanged event some time after the page loads, and in a
    plain browser tab there is no backend to push them at all. Until they
    land, ComboBoxState scales its index by an empty list and answers 0 for
    every position - so an echo read back then would drag the button home
    again. Adopted only once the relay really knows its choices; the local
    state is what the button draws either way.

    Both of the relay's events are listened to, and for the same reason: the
    initial value and the properties arrive in no guaranteed order, so
    whichever is second is the one that first makes a real index readable.

    `defaultIndex` is the counterpart of useJuceToggleValue's `defaultValue`:
    the position to draw when there is no backend at all (the gallery, a plain
    browser tab), consulted only for a relay that has never learned its
    choices. It is not a default for the parameter - the processor owns that -
    only for the picture, so a face opens on the engine its plugin actually
    ships on rather than always on index 0. */
export function useJuceChoiceValue(parameterId, count, defaultIndex = 0) {
  const id = useParamId(parameterId);
  const comboState = useMemo(() => Juce.getComboBoxState(id), [id]);
  const knowsChoices = () => (comboState.properties.choices?.length ?? 0) > 1;
  const [index, setIndex] = useState(() => (knowsChoices() ? comboState.getChoiceIndex() : defaultIndex));

  useEffect(() => {
    const adopt = () => {
      if (knowsChoices()) setIndex(comboState.getChoiceIndex());
    };

    adopt();
    const ids = [
      [comboState.valueChangedEvent, comboState.valueChangedEvent.addListener(adopt)],
      [comboState.propertiesChangedEvent, comboState.propertiesChangedEvent.addListener(adopt)],
    ];
    return () => ids.forEach(([event, listenerId]) => event.removeListener(listenerId));
  }, [comboState]);

  const select = (next) => {
    const wrapped = ((next % count) + count) % count;

    setIndex(wrapped);
    comboState.setChoiceIndex(wrapped);
  };

  return [index, select];
}

/** A choice parameter as one cycling pill - the Delay Type button beside
    Sync. A pill rather than a StageRouter's chevrons because it sits in the
    row of pills and reads as one of them; three positions is few enough that
    stepping through them is quicker than any menu would be.

    Lit for every position but the first, so the mode the pedal has always had
    reads as "nothing switched on" and the two that rewire it announce
    themselves. `labels` is in the parameter's own index order - see
    JuceStageRouter, which passes its two the same way. */
export function JuceChoicePill({ parameterId, labels }) {
  const [index, select] = useJuceChoiceValue(parameterId, labels.length);

  return <Pill label={labels[index] ?? labels[0]} pressed={index > 0} onClick={() => select(index + 1)} />;
}

/** One knob in a footer stage, bound to a WebSliderRelay by parameter id -
    Wear/Flutter on the tape section, Drift/Phaser on the mod one, Low/High on
    the filter. `scaleFrom="max"` is for a cut that rests wide open at the top
    of its travel; see Knob's own note on it. */
export function JuceStageKnob({ parameterId, name, scaleFrom, size }) {
  const id = useParamId(parameterId);
  const [value, setValue, sliderState] = useJuceSliderValue(parameterId);
  const valueLabel = useFormattedText(id, value);

  return (
    <StageControl
      name={name}
      value={value}
      valueLabel={valueLabel}
      scaleFrom={scaleFrom}
      size={size}
      onChange={setValue}
      onDragStart={() => sliderState.sliderDragStarted()}
      onDragEnd={() => sliderState.sliderDragEnded()}
    />
  );
}

/** A section's placement stepper, bound to that section's parameter - where
    the section sits in the chain.

    `labels` is [false, true] in the parameter's own sense - "tapepre" reads
    "is this in front", so it passes ["Post", "Pre"]. Two states, so both
    chevrons flip the same flag whichever way they point; stepping can only
    ever wrap. */
export function JuceStageRouter({ parameterId, label, labels }) {
  const [checked, setChecked] = useJuceToggleValue(parameterId);

  return <StageRouter label={label} value={labels[checked ? 1 : 0]} onStep={() => setChecked(!checked)} />;
}

/** Has the page measure its own rendered size and tell the editor to match it
    exactly.
 *
 * The plugin's window used to be sized by hand from a Chromium measurement of
 * the panel, and a real WKWebView rendered it taller every time - font and
 * line-height metrics differ enough between the two engines that there is no
 * way to get this right by guessing from outside a real host. So the page
 * measures itself instead, and it is never a guess regardless of engine,
 * font-loading timing, or future content changes.
 *
 * `padding` is the face's own `.page` padding, on every side - change one,
 * change the other. It is a parameter rather than a constant because it is a
 * property of the face, and this is shared by all of them.
 */
export function installAutoResize({ padding = 4 } = {}) {
  if (typeof window.__JUCE__?.initialisationData?.__juce__functions?.includes !== "function") return;
  if (!window.__JUCE__.initialisationData.__juce__functions.includes("reportContentSize")) return;

  const reportContentSize = Juce.getNativeFunction("reportContentSize");
  const card = document.querySelector(".pui-card");
  if (!card) return;

  const report = () => {
    const rect = card.getBoundingClientRect();
    reportContentSize(Math.ceil(rect.width) + padding * 2, Math.ceil(rect.height) + padding * 2);
  };

  // Fires once immediately on observe() as well as on every subsequent
  // layout change - covers late web-font swaps, not just the first paint.
  const observer = new ResizeObserver(report);
  observer.observe(card);
}

/** The native build stamp - `getBuildInfo()`, when the processor registers it
 * - so a face can show which binary is actually running. `__DATE__ __TIME__`
 * only ever changes when that translation unit is actually recompiled, so
 * this is the one thing in the UI that cannot be lying about a stale build:
 * a rebuilt-but-not-reloaded host (an old plugin instance sitting in an
 * already-open project, a DAW's own plugin cache) still shows the old stamp.
 *
 * Generic rather than a per-pedal binding because any WebView pedal can wire
 * the same native function up the same way; which ones actually have is up
 * to each pedal's own editor. Empty outside a real host, or on a processor
 * that has not registered the function - callers should render nothing then,
 * not a placeholder that could be mistaken for a real stamp.
 */
export function useJuceBuildInfo() {
  const [info, setInfo] = useState("");

  useEffect(() => {
    if (typeof window.__JUCE__?.initialisationData?.__juce__functions?.includes !== "function") return;
    if (!window.__JUCE__.initialisationData.__juce__functions.includes("getBuildInfo")) return;

    Juce.getNativeFunction("getBuildInfo")().then(setInfo);
  }, []);

  return info;
}
