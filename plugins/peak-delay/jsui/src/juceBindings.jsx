import { useEffect, useRef, useState } from "react";
import * as Juce from "juce-framework-frontend";
import { Knob, Button } from "@synthpeak/pedal-ui";

// A native function, not the parameter's own C++ stringFromValue - JUCE's
// web-view relays only carry start/end/skew/interval, not the format string.
// See jsui/README.md.
const formatKnobValue = Juce.getNativeFunction("formatKnobValue");

/** A parameter's live normalised value (0..1), kept in sync with its
    WebSliderRelay for as long as the component is mounted - not just while
    it's being dragged.

    `setValue` updates local state immediately as well as informing the
    relay, rather than waiting on the relay to echo the change back - there
    is no echo outside a real host, which is what keeps this interactive in
    the gallery and in a plain browser tab. */
function useJuceSliderValue(parameterId) {
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

/** Knob bound to a WebSliderRelay by parameter id. State is optimistic (set
    locally on drag, not only from the relay's echo) so it still works
    stand-alone in a plain browser, where there is no backend to echo it. */
export function JuceKnob({ parameterId, caption, size, cornerLabels, icon, endMarkerLabel }) {
  const [value, setValue, sliderState] = useJuceSliderValue(parameterId);
  const [readout, setReadout] = useState("");

  useEffect(() => {
    let cancelled = false;
    formatKnobValue(parameterId).then((text) => {
      if (!cancelled) setReadout(text);
    });
    return () => {
      cancelled = true;
    };
  }, [parameterId, value]);

  return (
    <Knob
      size={size}
      caption={caption}
      cornerLabels={cornerLabels}
      icon={icon}
      endMarkerLabel={endMarkerLabel}
      value={value}
      valueLabel={readout}
      onChange={setValue}
      onDragStart={() => sliderState.sliderDragStarted()}
      onDragEnd={() => sliderState.sliderDragEnded()}
    />
  );
}

/** Small icon-only toggle bound to a WebToggleButtonRelay by parameter id -
    the compact button Sync and Ms use instead of the pill Toggle, matching
    the round bezel buttons the old ee::ui face drew for them. Built on the
    shared Button rather than a new component: `pressed` already gives a
    button the same lit/unlit ink-inversion a toggle needs. */
export function JuceIconToggle({ parameterId, icon, ariaLabel }) {
  const toggleState = useRef(Juce.getToggleState(parameterId)).current;
  const [checked, setChecked] = useState(toggleState.getValue());

  useEffect(() => {
    const id = toggleState.valueChangedEvent.addListener(() => setChecked(toggleState.getValue()));
    return () => toggleState.valueChangedEvent.removeListener(id);
  }, [toggleState]);

  return (
    <Button
      pressed={checked}
      aria-label={ariaLabel}
      onClick={() => {
        const next = !checked;
        setChecked(next);
        toggleState.setValue(next);
      }}
    >
      {icon}
    </Button>
  );
}
