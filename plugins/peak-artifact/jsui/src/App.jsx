import { useEffect } from "react";
import { Card, JucePresetBar } from "@synthpeak/pedal-ui";
import { installAutoResize } from "@synthpeak/pedal-ui/juce";
import ArtifactModule from "./ArtifactModule.jsx";
import "./index.css";

/**
 * Peak Artifact's enclosure. The controls are `ArtifactModule` - one switchable
 * module drawn in the style of Peak Alpine's Modulation side-module, but red.
 * This file is only what is this pedal's: its card, its title and its preset
 * bar.
 */
export default function App() {
  useEffect(() => installAutoResize(), []);

  return (
    <div className="page">
      <Card
        title="Peak Artifact"
        subtitle="Ring Mod / Bit Crush / Filter"
        subtitlePlacement="below"
        headerCenter={<JucePresetBar />}
        className="pa-card"
      >
        <ArtifactModule />
      </Card>
    </div>
  );
}
