import { useEffect } from "react";
import { Card } from "@synthpeak/pedal-ui";
import { installAutoResize } from "@synthpeak/pedal-ui/juce";
import GrainFace from "./GrainFace.jsx";
import "./index.css";

/**
 * Peak Grain's enclosure: a 697px Card with no title/logo/preset-bar slots
 * of its own (GrainFace builds its own two-row header as plain children,
 * see COMPONENTS.md - Card's single header-slot API has no room for that
 * shape), then the plate.
 *
 * 697 is the old 560 plus the mixer column's 136 and the divider beside it.
 * The mixer was added *inside* the plate, so without widening the card it
 * came out of the five original sections instead - which squeezed Delay and
 * Reverb past the width their own contents need. The editor follows this
 * through reportContentSize, so the plugin window resizes with it.
 *
 * 719 is that same 697 plus 22px: row 2's own Effects/Mod tab rail
 * (GrainFace.jsx's pg-row2, VerticalTabs.css) sits inside row 2 rather than
 * beside the whole plate, so without the extra width it would eat 22px
 * straight out of Delay/Reverb's own row - already the tightest-fit row on
 * the face (see .pg-delay__body's own note). The extra width lands on every
 * row equally (it is the plate's own width), so Grain/Pitch/Random just get
 * 22px more breathing room instead.
 */
export default function App() {
  useEffect(() => installAutoResize(), []);

  return (
    <div className="page">
      <Card className="pg-card" width={719}>
        <GrainFace />
      </Card>
    </div>
  );
}
