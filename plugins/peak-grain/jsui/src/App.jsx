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
 */
export default function App() {
  useEffect(() => installAutoResize(), []);

  return (
    <div className="page">
      <Card className="pg-card" width={697}>
        <GrainFace />
      </Card>
    </div>
  );
}
