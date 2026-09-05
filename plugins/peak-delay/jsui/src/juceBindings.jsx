import { useEffect, useRef, useState } from "react";
import * as Juce from "juce-framework-frontend";
import { Knob, Pill, SliderRow } from "@synthpeak/pedal-ui";

// A native function, not the parameter's own C++ stringFromValue - JUCE's
// web-view relays only carry start/end/skew/interval, not the format string.
// See jsui/README.md. Also answers two synthetic ids ("ltimeMs"/"rtimeMs")
// that aren't real parameters - see PluginProcessor.h's timeMsReadout().
const formatKnobValue = Juce.getNativeFunction("formatKnobValue");

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
    whenever `value` changes - shared by JuceKnob and JuceSliderRow so both
    read the same live-readout pattern. */
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
    stand-alone in a plain browser, where there is no backend to echo it. */
export function JuceKnob({ parameterId, caption, size, variant, endMarkerLabel, sweepGap, sweepWidth }) {
  const [value, setValue, sliderState] = useJuceSliderValue(parameterId);
  const readout = useFormattedText(parameterId, value);

  return (
    <Knob
      variant={variant}
      size={size}
      sweepGap={sweepGap}
      sweepWidth={sweepWidth}
      caption={caption}
      endMarkerLabel={endMarkerLabel}
      value={value}
      valueLabel={readout}
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
  const [tick, setTick] = useState(0);

  useEffect(() => {
    const id = sliderState.valueChangedEvent.addListener(() => setValue(sliderState.getNormalisedValue()));
    return () => sliderState.valueChangedEvent.removeListener(id);
  }, [sliderState]);

  useEffect(() => {
    const id = timeUnitState.valueChangedEvent.addListener(() => setTick((t) => t + 1));
    return () => timeUnitState.valueChangedEvent.removeListener(id);
  }, [timeUnitState]);

  const text = useFormattedText(parameterId, `${value}:${tick}`);
  const msText = useFormattedText(`${parameterId}Ms`, value);

  return [text, msText];
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
  const toggleState = useRef(Juce.getToggleState(parameterId)).current;
  const [checked, setChecked] = useState(toggleState.getValue());

  useEffect(() => {
    const id = toggleState.valueChangedEvent.addListener(() => setChecked(toggleState.getValue()));
    return () => toggleState.valueChangedEvent.removeListener(id);
  }, [toggleState]);

  return (
    <Pill
      icon={icon}
      label={label}
      pressed={invert ? !checked : checked}
      onClick={() => {
        const next = !checked;
        setChecked(next);
        toggleState.setValue(next);
      }}
    />
  );
}

/** SliderRow bound to a WebSliderRelay by parameter id - Tape/Mod's
    pre/post-stage strip. */
export function JuceSliderRow({ parameterId, name }) {
  const [value, setValue, sliderState] = useJuceSliderValue(parameterId);
  const valueLabel = useFormattedText(parameterId, value);

  return (
    <SliderRow
      name={name}
      value={value}
      valueLabel={valueLabel}
      onChange={setValue}
      onDragStart={() => sliderState.sliderDragStarted()}
      onDragEnd={() => sliderState.sliderDragEnded()}
    />
  );
}
