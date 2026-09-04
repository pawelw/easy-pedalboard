import { useEffect, useRef, useState } from "react";
import * as Juce from "juce-framework-frontend";
import { Knob, Toggle, Button } from "@synthpeak/pedal-ui";

// A native function, not the parameter's own C++ stringFromValue - JUCE's
// web-view relays only carry start/end/skew/interval, not the format string.
// See jsui/README.md.
const formatKnobValue = Juce.getNativeFunction("formatKnobValue");

/** Knob bound to a WebSliderRelay by parameter id. State is optimistic (set
    locally on drag, not only from the relay's echo) so it still works
    stand-alone in a plain browser, where there is no backend to echo it. */
export function JuceKnob({ parameterId, caption, size, cornerLabels }) {
  const sliderState = useRef(Juce.getSliderState(parameterId)).current;
  const [value, setValue] = useState(sliderState.getNormalisedValue());
  const [readout, setReadout] = useState("");

  useEffect(() => {
    const id = sliderState.valueChangedEvent.addListener(() => setValue(sliderState.getNormalisedValue()));
    return () => sliderState.valueChangedEvent.removeListener(id);
  }, [sliderState]);

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
      value={value}
      valueLabel={readout}
      onChange={(next) => {
        setValue(next);
        sliderState.setNormalisedValue(next);
      }}
      onDragStart={() => sliderState.sliderDragStarted()}
      onDragEnd={() => sliderState.sliderDragEnded()}
    />
  );
}

/** Toggle bound to a WebToggleButtonRelay by parameter id. */
export function JuceToggle({ parameterId, caption }) {
  const toggleState = useRef(Juce.getToggleState(parameterId)).current;
  const [checked, setChecked] = useState(toggleState.getValue());

  useEffect(() => {
    const id = toggleState.valueChangedEvent.addListener(() => setChecked(toggleState.getValue()));
    return () => toggleState.valueChangedEvent.removeListener(id);
  }, [toggleState]);

  return (
    <Toggle
      caption={caption}
      checked={checked}
      onChange={(next) => {
        setChecked(next);
        toggleState.setValue(next);
      }}
    />
  );
}

/** Button bound to a WebToggleButtonRelay by parameter id, with a status LED
    - for a bypass-style on/off switch drawn as a button rather than a pill
    toggle (matches the BYPASS reference). */
export function JuceLedButton({ parameterId, children }) {
  const toggleState = useRef(Juce.getToggleState(parameterId)).current;
  const [checked, setChecked] = useState(toggleState.getValue());

  useEffect(() => {
    const id = toggleState.valueChangedEvent.addListener(() => setChecked(toggleState.getValue()));
    return () => toggleState.valueChangedEvent.removeListener(id);
  }, [toggleState]);

  return (
    <Button
      led={checked}
      pressed={checked}
      onClick={() => {
        setChecked(!checked);
        toggleState.setValue(!checked);
      }}
    >
      {children}
    </Button>
  );
}
