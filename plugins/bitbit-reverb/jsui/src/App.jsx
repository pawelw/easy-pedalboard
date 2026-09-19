import { useEffect } from "react";
import { Card, JucePresetBar } from "@synthpeak/pedal-ui";
import { installAutoResize } from "@synthpeak/pedal-ui/juce";
import { ReverbFace } from "@synthpeak/module-face";
import "./index.css";

/**
 * BitBit Reverb's enclosure. The controls are `ReverbFace` - BitBit Alpine's Reverb
 * module, the same component Alpine renders in its module row. This file is
 * only what is this pedal's: its card and its preset bar.
 */
export default function App() {
  useEffect(() => installAutoResize(), []);

  return (
    <div className="page">
      <Card
        showLogo={false}
        headerCenter={<JucePresetBar variant="separated" showSteppers={false} />}
        headerCenterPlacement="below"
        className="pr-card"
      >
        <ReverbFace />
      </Card>
    </div>
  );
}
