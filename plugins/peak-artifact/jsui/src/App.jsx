import { useEffect } from "react";
import { Card, JucePresetBar } from "@synthpeak/pedal-ui";
import { installAutoResize } from "@synthpeak/pedal-ui/juce";
import { ArtifactFace } from "@synthpeak/artifact-face";
import "./index.css";

/**
 * Peak Artifact's enclosure. The controls are `ArtifactFace` - one switchable
 * module drawn in the style of Peak Alpine's Modulation side-module, but red -
 * the same component Peak Alpine renders as its first module. This file is only
 * what is this pedal's: its card, its title and its preset bar.
 */
export default function App() {
  useEffect(() => installAutoResize(), []);

  return (
    <div className="page">
      <Card
        showLogo={false}
        headerCenter={<JucePresetBar variant="separated" showSteppers={false} />}
        headerCenterPlacement="below"
        className="af-card"
      >
        <ArtifactFace />
      </Card>
    </div>
  );
}
