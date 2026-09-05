import { useEffect } from "react";
import { Card } from "@synthpeak/pedal-ui";
import { JuceKnob, JuceIconToggle } from "./juceBindings.jsx";
import { LinkIcon, MsIcon } from "./icons.jsx";
import { installAutoResize } from "./autoSize.js";
import "./index.css";

export default function App() {
  useEffect(() => installAutoResize(), []);

  return (
    <div className="page">
      {/* theme="green" is the only thing that makes this face read as Peak
          Delay rather than Peak Wah - see packages/pedal-ui/src/tokens.css. */}
      <Card title="Peak Delay" subtitle="Tempo-synced stereo delay" theme="green">
        {/* Mix (bigger - it's the one you reach for most) leads the top row
            with the two Time knobs; Feedback leads the bottom row with Mod
            and Tape grouped in their own bordered box (.pd-group) - they
            share one job (colouring the repeats) the way Feedback doesn't.
            One grid for both rows, not two independent ones, so column
            widths - set by Mix's bigger footprint - line up between rows
            (see Peak Wah's .pw-grid8 comment for why that matters). Sync/Ms
            overlay the seam between the two Time knobs without taking a
            grid column of their own, so they don't widen it. */}
        <div className="pd-grid">
          <JuceKnob parameterId="mix" caption="Mix" size={92} />
          <JuceKnob parameterId="ltime" caption="Left Time" size={62} />
          <JuceKnob parameterId="rtime" caption="Right Time" size={62} />

          <div className="pd-mini-toggles">
            <JuceIconToggle parameterId="sync" ariaLabel="Sync" icon={<LinkIcon size={12} />} />
            <JuceIconToggle parameterId="timeunit" ariaLabel="Ms" icon={<MsIcon size={12} />} />
          </div>

          <JuceKnob parameterId="fb" caption="Feedback" size={62} />

          <div className="pd-group">
            <JuceKnob parameterId="mod" caption="Mod" size={62} />
            <JuceKnob parameterId="tape" caption="Tape" size={62} />
          </div>
        </div>
      </Card>
    </div>
  );
}
