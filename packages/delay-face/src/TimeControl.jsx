import { Readout } from "@synthpeak/pedal-ui";
import { JuceKnob } from "@synthpeak/pedal-ui/juce";
import { useTimeReadoutText } from "./juceBindings.jsx";

/**
 * A 42px scale knob paired with a recessed Readout - the Delay face's
 * Left/Right Time rows (COMPONENTS.md #3). It lives beside `DelayFace` rather
 * than in `@synthpeak/pedal-ui`: it is just a Knob and a Readout wired
 * together, and nothing outside this face would reuse it as a unit.
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
      {/* `bare`: no caption line under the dial and no width padding for one.
          This knob's value lives in the Readout beside it, so that slot was
          always empty - it just made the row 48px tall with the dial sitting
          21px down instead of centred, which the link bracket's arms would
          then meet off-centre. */}
      <JuceKnob parameterId={parameterId} variant="scale" size={42} showValueLabel={false} bare />
      <Readout label={side} value={text} unit={isMs ? undefined : msText} />
    </div>
  );
}
