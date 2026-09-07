import { useEffect, useRef, useState } from "react";
import * as Juce from "juce-framework-frontend";
import { Knob, MiniSlider, Pill, StageControl, StageRouter } from "@synthpeak/pedal-ui";

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
    listening to the knob alone would miss a same-value ms/division swap. */
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

  const text = useFormattedText(parameterId, `${value}:${isMs}`);
  const msText = useFormattedText(`${parameterId}Ms`, value);

  return [text, msText, isMs];
}

/** The two delay times in milliseconds, refetched whenever anything that
    changes them moves: either Time knob, or the Sync pill (the same knob
    position means a different time either side of it).

    Host tempo can move them too, with nothing here to listen to - a synced
    time follows the transport. Not polled for that: the readouts beside the
    knobs have always had the same gap, and a timer running behind every
    editor to catch an occasional tempo change is a poor trade. Touching any
    control refreshes it.

    Falls back to the last known pair (500/500 to start) while the call is in
    flight, and forever in a plain browser tab where there is no backend to
    answer - which is what keeps the scope drawing something sane in the
    gallery. */
export function useDelayTimesMs() {
  const leftState = useRef(Juce.getSliderState("ltime")).current;
  const rightState = useRef(Juce.getSliderState("rtime")).current;
  const unitState = useRef(Juce.getToggleState("timeunit")).current;

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
  }, [tick]);

  return times;
}

/** The processor's live input level and note-onset count, pushed from
    PeakDelayWebEditor's Timer as the one "delayMeter" event - neither is a
    parameter, so there is no relay for them.

    Stays at zero until a real host starts sending it, which a plain browser
    tab never will: the TapScope then draws its taps and simply never lights
    them, which is the same thing it does in a host with nothing playing. */
export function useDelayMeter() {
  const [meter, setMeter] = useState({ level: 0, strikes: 0 });

  useEffect(() => {
    if (typeof window.__JUCE__?.backend?.addEventListener !== "function") return undefined;
    const id = window.__JUCE__.backend.addEventListener("delayMeter", (event) => setMeter(event));
    return () => window.__JUCE__.backend.removeEventListener(id);
  }, []);

  return meter;
}

/** One of the header's two level faders, bound to a WebSliderRelay by
    parameter id. Same optimistic-state pattern as JuceKnob - the value is set
    locally on drag rather than waiting for a relay echo that only a real host
    sends. */
export function JuceMiniSlider({ parameterId, label }) {
  const [value, setValue, sliderState] = useJuceSliderValue(parameterId);
  const valueLabel = useFormattedText(parameterId, value);

  return (
    <MiniSlider
      label={label}
      value={value}
      valueLabel={valueLabel}
      onChange={setValue}
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

/** One knob in a footer stage, bound to a WebSliderRelay by parameter id -
    Wear/Flutter on the tape half, Chorus/Phaser on the mod half. */
export function JuceStageKnob({ parameterId, name }) {
  const [value, setValue, sliderState] = useJuceSliderValue(parameterId);
  const valueLabel = useFormattedText(parameterId, value);

  return (
    <StageControl
      name={name}
      value={value}
      valueLabel={valueLabel}
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
