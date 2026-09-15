import { useEffect } from "react";
import { Card } from "@synthpeak/pedal-ui";
import { installAutoResize } from "@synthpeak/pedal-ui/juce";
import GrainFace from "./GrainFace.jsx";
import "./index.css";

/**
 * Peak Grain's enclosure: a 560px Card with no title/logo/preset-bar slots
 * of its own (GrainFace builds its own two-row header as plain children,
 * see COMPONENTS.md - Card's single header-slot API has no room for that
 * shape), then the plate.
 */
export default function App() {
  useEffect(() => installAutoResize(), []);

  return (
    <div className="page">
      <Card className="pg-card" width={560}>
        <GrainFace />
      </Card>
    </div>
  );
}
