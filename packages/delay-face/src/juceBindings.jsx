import { useEffect, useRef, useState } from "react";
import * as Juce from "juce-framework-frontend";
import { useFormattedText, useParamId } from "@synthpeak/pedal-ui/juce";

/**
 * The parts of Peak Delay's JUCE wiring that are Peak Delay's: a native
 * function only its processor answers, and three hooks over the meter feed
 * only its editor emits. Everything generic - JuceKnob, JucePill, the three
 * live-value hooks, the fader - lives in `@synthpeak/pedal-ui/juce` and is
 * shared with every other face.
 *
 * All of it resolves parameter ids through the enclosing `ParamScope`, so the
 * same hooks serve Peak Delay's bare `ltime` and Peak Alpine's `dly.ltime`.
 */

// [leftMs, rightMs] as numbers - the TapScope's time axis. See the native
// function's own comment in PeakDelayWebEditor.cpp for why the formatted
// readouts can't stand in for it.
const getDelayTimesMs = Juce.getNativeFunction("getDelayTimesMs");

/** The Time knobs' pair of readout texts: the toggle-aware main value
    (division normally, ms when the "ms" pill is on - PeakDelayProcessor's
    timeReadout() already does the swapping) and the always-ms figure next
    to it. The "ms" toggle doesn't touch ltime/rtime's own value, so both
    texts need their own listener on it as well as on the knob's value -
    listening to the knob alone would miss a same-value ms/division swap.

    Host tempo is the third thing that moves them, and the only one with no
    control behind it: a synced knob is a note division, so "1/4" is 500 ms at
    120 and 375 at 160 with the knob never having moved. useHostBpm watches
    the meter feed for it.

    The "Ms" id is synthetic - not a real parameter, one formatKnobValue
    recognises (see PluginProcessor.h's timeMsReadout()) - so it is built from
    the *scoped* id, which is what the backend is asked about. */
export function useTimeReadoutText(parameterId) {
  const id = useParamId(parameterId);
  const timeUnitId = useParamId("timeunit");
  const sliderState = useRef(Juce.getSliderState(id)).current;
  const timeUnitState = useRef(Juce.getToggleState(timeUnitId)).current;
  const [value, setValue] = useState(sliderState.getNormalisedValue());
  const [isMs, setIsMs] = useState(timeUnitState.getValue());

  useEffect(() => {
    const listenerId = sliderState.valueChangedEvent.addListener(() =>
      setValue(sliderState.getNormalisedValue()),
    );
    return () => sliderState.valueChangedEvent.removeListener(listenerId);
  }, [sliderState]);

  useEffect(() => {
    const listenerId = timeUnitState.valueChangedEvent.addListener(() => setIsMs(timeUnitState.getValue()));
    return () => timeUnitState.valueChangedEvent.removeListener(listenerId);
  }, [timeUnitState]);

  const bpm = useHostBpm();

  const text = useFormattedText(id, `${value}:${isMs}:${bpm}`);
  const msText = useFormattedText(`${id}Ms`, `${value}:${bpm}`);

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
  const leftId = useParamId("ltime");
  const rightId = useParamId("rtime");
  const unitId = useParamId("timeunit");
  const leftState = useRef(Juce.getSliderState(leftId)).current;
  const rightState = useRef(Juce.getSliderState(rightId)).current;
  const unitState = useRef(Juce.getToggleState(unitId)).current;
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

/** The processor's live input level and note-onset count, pushed from the
    editor's Timer as the one "delayMeter" event - neither is a parameter, so
    there is no relay for them.

    The event name is not scoped the way parameter ids are: it is one feed per
    editor, not per module, so a host embedding this face emits it under the
    same name. Stays at zero until a real host starts sending it, which a plain
    browser tab never will: the TapScope then draws its taps and simply never
    lights them, which is the same thing it does in a host with nothing
    playing. */
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
