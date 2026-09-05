import { Readout } from "@synthpeak/pedal-ui";
import { JuceKnob, useTimeReadoutText } from "./juceBindings.jsx";

/**
 * A 42px scale knob paired with a recessed Readout - Delay's Left/Right Time
 * rows (COMPONENTS.md #3). Delay-local rather than a `@synthpeak/pedal-ui`
 * component: it's just a Knob + a Readout wired together, nothing another
 * pedal would reuse as its own unit.
 */
export default function TimeControl({ side, parameterId }) {
  const [text, msText, isMs] = useTimeReadoutText(parameterId);

  // In ms mode `text` (the toggle-aware main value) already reads e.g.
  // "333 ms" - showing the same "333 ms" a second time as the small unit
  // figure is redundant clutter, and crowding it in next to the now-longer
  // main value is what was truncating that value down to "333…" (see
  // Readout.css's min-width fix - real, but this removes the actual
  // redundancy that made the value need more room than it had). Synced
  // (division) mode keeps the unit: there the main value is "1/8", so the
  // ms figure alongside it is the only place that reading shows up at all.
  return (
    <div className="pd-time-control">
      <JuceKnob parameterId={parameterId} variant="scale" size={42} />
      <Readout label={side} value={text} unit={isMs ? undefined : msText} />
    </div>
  );
}
