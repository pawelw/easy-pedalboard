import { Readout } from "@synthpeak/pedal-ui";
import { JuceKnob, useTimeReadoutText } from "./juceBindings.jsx";

/**
 * A 42px scale knob paired with a recessed Readout - Delay's Left/Right Time
 * rows (COMPONENTS.md #3). Delay-local rather than a `@synthpeak/pedal-ui`
 * component: it's just a Knob + a Readout wired together, nothing another
 * pedal would reuse as its own unit.
 */
export default function TimeControl({ side, parameterId }) {
  const [text, msText] = useTimeReadoutText(parameterId);

  return (
    <div className="pd-time-control">
      <JuceKnob parameterId={parameterId} variant="scale" size={42} />
      <Readout label={side} value={text} unit={msText} />
    </div>
  );
}
