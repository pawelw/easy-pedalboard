import { useEffect, useRef, useState } from "react";
import * as Juce from "juce-framework-frontend";
import { Knob, Toggle, FilterScope, freqHzFor01 } from "@synthpeak/pedal-ui";

// A native function, not the parameter's own C++ stringFromValue - JUCE's
// web-view relays only carry start/end/skew/interval, not the format string.
// See jsui/README.md.
const formatKnobValue = Juce.getNativeFunction("formatKnobValue");

/** A parameter's live normalised value (0..1), kept in sync with its
    WebSliderRelay for as long as the component is mounted - not just while
    it's being dragged. Shared by JuceKnob and JuceFilterScope so both read
    the same parameter without opening a second subscription to it.

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

/** The live cutoff-sweep exponent for both channels (PeakWahProcessor's
    lfoModLUi/lfoModRUi), pushed from PeakWahWebEditor's Timer as the
    "filterMod" event - there's no relay for this, it isn't a parameter.
    Stays {0, 0} until a real host starts sending it (a plain browser tab
    never will), which just means the scope's live wash sits still at the
    centre of the sweep band instead of riding around inside it. */
function useFilterMod() {
  const [mod, setMod] = useState({ modL: 0, modR: 0 });

  useEffect(() => {
    if (typeof window.__JUCE__?.backend?.addEventListener !== "function") return undefined;
    const id = window.__JUCE__.backend.addEventListener("filterMod", (event) => setMod(event));
    return () => window.__JUCE__.backend.removeEventListener(id);
  }, []);

  return mod;
}

/** The response scope, reading freq/resonance/range live so it tracks
    whichever of those knobs is being turned, plus the live modL/modR feed
    for the two channels' actual position within the sweep. */
export function JuceFilterScope({ height }) {
  const [freq01] = useJuceSliderValue("freq");
  const [q01] = useJuceSliderValue("q");
  const [range01] = useJuceSliderValue("range");
  const { modL, modR } = useFilterMod();

  return (
    <FilterScope
      baseFreqHz={freqHzFor01(freq01)}
      resonance01={q01}
      sweepDepth01={range01}
      modL={modL}
      modR={modR}
      height={height}
    />
  );
}
