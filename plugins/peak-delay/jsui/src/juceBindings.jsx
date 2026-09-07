import { useEffect, useRef, useState } from "react";
import * as Juce from "juce-framework-frontend";
import { Knob, Pill, Slider, StageControl, StageRouter } from "@synthpeak/pedal-ui";

// A native function, not the parameter's own C++ stringFromValue - JUCE's
// web-view relays only carry start/end/skew/interval, not the format string.
// See jsui/README.md. Also answers two synthetic ids ("ltimeMs"/"rtimeMs")
// that aren't real parameters - see PluginProcessor.h's timeMsReadout().
const formatKnobValue = Juce.getNativeFunction("formatKnobValue");

// [leftMs, rightMs] as numbers - the TapScope's time axis. See the native
// function's own comment in PeakDelayWebEditor.cpp for why the formatted
// readouts above can't stand in for it.
const getDelayTimesMs = Juce.getNativeFunction("getDelayTimesMs");

/** A parameter's live normalised value (0..1), kept in sync with its
    WebSliderRelay for as long as the component is mounted - not just while
    it's being dragged.

    `setValue` updates local state immediately as well as informing the
    relay, rather than waiting on the relay to echo the change back - there
    is no echo outside a real host, which is what keeps this interactive in
    the gallery and in a plain browser tab. */
export function useJuceSliderValue(parameterId) {
  const sliderState = useRef(Juce.getSliderState(parameterId)).current;
  const [value, setValue] = useState(sliderState.getNormalisedValue());

  useEffect(() => {
    const id = sliderState.valueChangedEvent.addListener(() => setValue(sliderState.getNormalisedValue()));
    return () => sliderState.valueChangedEvent.removeListener(id);
  }, [sliderState]);

  const setNormalisedValue = (next) => {
    setValue(next);
    sliderState.setNormalisedValue(next);
  };

  return [value, setNormalisedValue, sliderState];
}

/** A parameter's live formatted text (via formatKnobValue), re-fetched
    whenever `value` changes - shared by JuceKnob and JuceStageControl so
    both read the same live-readout pattern. */
function useFormattedText(parameterId, value) {
  const [text, setText] = useState("");

  useEffect(() => {
    let cancelled = false;
    formatKnobValue(parameterId).then((t) => {
      if (!cancelled) setText(t);
    });
    return () => {
      cancelled = true;
    };
  }, [parameterId, value]);

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
    `subLabel`), always visible rather than only appearing mid-drag. */
export function JuceKnob({
  parameterId,
  caption,
  size,
  variant,
  endMarkerLabel,
  sweepGap,
  showValueLabel = true,
  showValueBelow = false,
  bare = false,
}) {
  const [value, setValue, sliderState] = useJuceSliderValue(parameterId);
  const readout = useFormattedText(parameterId, value);

  return (
    <Knob
      variant={variant}
      size={size}
      bare={bare}
      sweepGap={sweepGap}
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

/** The Time knobs' pair of readout texts: the toggle-aware main value
    (division normally, ms when the "ms" pill is on - PeakDelayProcessor's
    timeReadout() already does the swapping) and the always-ms figure next
    to it. The "ms" toggle doesn't touch ltime/rtime's own value, so both
    texts need their own listener on it as well as on the knob's value -
    listening to the knob alone would miss a same-value ms/division swap.

    Host tempo is the third thing that moves them, and the only one with no
    control behind it: a synced knob is a note division, so "1/4" is 500 ms at
    120 and 375 at 160 with the knob never having moved. useHostBpm watches
    the meter feed for it. */
export function useTimeReadoutText(parameterId) {
  const sliderState = useRef(Juce.getSliderState(parameterId)).current;
  const timeUnitState = useRef(Juce.getToggleState("timeunit")).current;
  const [value, setValue] = useState(sliderState.getNormalisedValue());
  const [isMs, setIsMs] = useState(timeUnitState.getValue());

  useEffect(() => {
    const id = sliderState.valueChangedEvent.addListener(() => setValue(sliderState.getNormalisedValue()));
    return () => sliderState.valueChangedEvent.removeListener(id);
  }, [sliderState]);

  useEffect(() => {
    const id = timeUnitState.valueChangedEvent.addListener(() => setIsMs(timeUnitState.getValue()));
    return () => timeUnitState.valueChangedEvent.removeListener(id);
  }, [timeUnitState]);

  const bpm = useHostBpm();

  const text = useFormattedText(parameterId, `${value}:${isMs}:${bpm}`);
  const msText = useFormattedText(`${parameterId}Ms`, `${value}:${bpm}`);

  return [text, msText, isMs];
}

/** The two delay times in milliseconds, refetched whenever anything that
    changes them moves: either Time knob, the Sync pill (the same knob
    position means a different time either side of it), or the host tempo -
    a synced time follows the transport, and that is the one input with no
    control on the face to listen to. It arrives on the meter feed the scope
    is already driven by (see useHostBpm), so the scope's own time axis now
    follows a tempo change rather than staying where it was until something
    was touched.

    Falls back to the last known pair (500/500 to start) while the call is in
    flight, and forever in a plain browser tab where there is no backend to
    answer - which is what keeps the scope drawing something sane in the
    gallery. */
export function useDelayTimesMs() {
  const leftState = useRef(Juce.getSliderState("ltime")).current;
  const rightState = useRef(Juce.getSliderState("rtime")).current;
  const unitState = useRef(Juce.getToggleState("timeunit")).current;
  const bpm = useHostBpm();

  const [times, setTimes] = useState([500, 500]);
  const [tick, setTick] = useState(0);

  useEffect(() => {
    const bump = () => setTick((t) => t + 1);
    const ids = [
      [leftState.valueChangedEvent, leftState.valueChangedEvent.addListener(bump)],
      [rightState.valueChangedEvent, rightState.valueChangedEvent.addListener(bump)],
      [unitState.valueChangedEvent, unitState.valueChangedEvent.addListener(bump)],
    ];
    return () => ids.forEach(([event, id]) => event.removeListener(id));
  }, [leftState, rightState, unitState]);

  useEffect(() => {
    let cancelled = false;
    getDelayTimesMs().then((next) => {
      if (!cancelled && Array.isArray(next) && next.length === 2) setTimes(next);
    });
    return () => {
      cancelled = true;
    };
  }, [tick, bpm]);

  return times;
}

/** Subscribes to the backend's "delayMeter" event, passing each one through
    `pick` and storing the result. Shared by the two hooks below, which want
    very different things out of the same 45 Hz feed.

    Outside a real host there is no backend at all and nothing ever arrives -
    the initial value stands forever, which is what keeps the face drawing
    something sane in a plain browser tab. */
function useDelayMeterField(initial, pick) {
  const [value, setValue] = useState(initial);

  useEffect(() => {
    if (typeof window.__JUCE__?.backend?.addEventListener !== "function") return undefined;
    const id = window.__JUCE__.backend.addEventListener("delayMeter", (event) => setValue(pick(event)));
    return () => window.__JUCE__.backend.removeEventListener(id);
    // Subscribed once, deliberately. `pick` is a fresh closure on every
    // render, so listing it here would tear the listener down and rebuild it
    // on every one of these 45-a-second events; both callers' picks are pure
    // and capture nothing, so the first one is as good as any.
  }, []);

  return value;
}

/** The processor's live input level and note-onset count, pushed from
    PeakDelayWebEditor's Timer as the one "delayMeter" event - neither is a
    parameter, so there is no relay for them.

    Stays at zero until a real host starts sending it, which a plain browser
    tab never will: the TapScope then draws its taps and simply never lights
    them, which is the same thing it does in a host with nothing playing. */
export function useDelayMeter() {
  return useDelayMeterField({ level: 0, strikes: 0 }, (event) => event);
}

/** The host tempo, off the same feed. Its own hook rather than a field of
    useDelayMeter's object because of how differently the two are consumed:
    that object is new on every event and re-renders the scope 45 times a
    second, which is what the scope is for. The readouts must not do that -
    each re-render of theirs costs a round trip to formatKnobValue. Stored on
    its own, a tempo that has not changed is the same number, React bails out
    of the render, and nothing downstream refetches.

    120 until a host says otherwise, matching the processor's own fallback
    (see PluginProcessor.h's currentBpm()). */
export function useHostBpm() {
  return useDelayMeterField(120, (event) => (typeof event.bpm === "number" ? event.bpm : 120));
}

/** One of the header's two level faders, bound to a WebSliderRelay by
    parameter id - the shared `Slider`, the control Peak EQ's bands are, laid on
    its side at header size and dragged like a knob. Same optimistic-state
    pattern as JuceKnob: the value is set locally on drag rather than waiting
    for a relay echo that only a real host sends.

    `resetTo` is a scaled value (dB here, not 0..1). Where it sits on the knob's
    travel is worked out from the relay's own start/end/skew rather than from a
    number written down twice - the range lives in the processor, and the two
    would drift the moment anyone widened it. */
export function JuceFader({ parameterId, label, resetTo = 0 }) {
  const [value, setValue, sliderState] = useJuceSliderValue(parameterId);
  const valueLabel = useFormattedText(parameterId, value);

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
      length={64}
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

/** A boolean parameter's live value, kept in sync with its
    WebToggleButtonRelay - the toggle counterpart of useJuceSliderValue, and
    shared by every control built on one (the pills and the stage routers).

    `setValue` updates local state as well as the relay for the same reason
    useJuceSliderValue does: outside a real host nothing echoes the change
    back, and without it the gallery's toggles would never move. */
function useJuceToggleValue(parameterId) {
  const toggleState = useRef(Juce.getToggleState(parameterId)).current;
  const [checked, setChecked] = useState(toggleState.getValue());

  useEffect(() => {
    const id = toggleState.valueChangedEvent.addListener(() => setChecked(toggleState.getValue()));
    return () => toggleState.valueChangedEvent.removeListener(id);
  }, [toggleState]);

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
    whichever is second is the one that first makes a real index readable. */
function useJuceChoiceValue(parameterId, count) {
  const comboState = useRef(Juce.getComboBoxState(parameterId)).current;
  const [index, setIndex] = useState(() =>
    (comboState.properties.choices?.length ?? 0) > 1 ? comboState.getChoiceIndex() : 0,
  );

  useEffect(() => {
    const adopt = () => {
      if ((comboState.properties.choices?.length ?? 0) > 1) setIndex(comboState.getChoiceIndex());
    };
    const ids = [
      [comboState.valueChangedEvent, comboState.valueChangedEvent.addListener(adopt)],
      [comboState.propertiesChangedEvent, comboState.propertiesChangedEvent.addListener(adopt)],
    ];
    return () => ids.forEach(([event, id]) => event.removeListener(id));
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
export function JuceStageKnob({ parameterId, name, scaleFrom }) {
  const [value, setValue, sliderState] = useJuceSliderValue(parameterId);
  const valueLabel = useFormattedText(parameterId, value);

  return (
    <StageControl
      name={name}
      value={value}
      valueLabel={valueLabel}
      scaleFrom={scaleFrom}
      onChange={setValue}
      onDragStart={() => sliderState.sliderDragStarted()}
      onDragEnd={() => sliderState.sliderDragEnded()}
    />
  );
}

/** A section's placement stepper, bound to that section's parameter - where
    the section sits relative to the delay line. Only Tape has one: the Mod
    section's Drift is inside the delay's feedback loop and so has no side to
    be on (see PluginProcessor.h).

    `labels` is [false, true] in the parameter's own sense - "tapepre" reads
    "is this in front", so it passes ["Post", "Pre"]. Two states, so both
    chevrons flip the same flag whichever way they point; stepping can only
    ever wrap. */
export function JuceStageRouter({ parameterId, label, labels }) {
  const [checked, setChecked] = useJuceToggleValue(parameterId);

  return <StageRouter label={label} value={labels[checked ? 1 : 0]} onStep={() => setChecked(!checked)} />;
}
