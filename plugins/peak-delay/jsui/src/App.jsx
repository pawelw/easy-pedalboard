import { useEffect } from "react";
import { Card, JucePresetBar } from "@synthpeak/pedal-ui";
import { JuceFader, installAutoResize } from "@synthpeak/pedal-ui/juce";
import { DelayFace } from "@synthpeak/delay-face";
import "./index.css";

/** The two level faders, stacked in the header's right-hand slot. They
    replaced a line of text that repeated what the face already said twice
    over - "STEREO · 1/8 · 1/8T", the same two readouts that sit beside the
    Time knobs - with the one pair of controls the pedal was missing. */
function HeaderLevels() {
  return (
    <div className="pd-levels">
      <JuceFader parameterId="ingain" label="In" />
      <JuceFader parameterId="outgain" label="Out" />
    </div>
  );
}

/**
 * Peak Delay's enclosure. The controls themselves are `DelayFace`, the
 * component Peak Alpine's Delay module renders too - so this file is only
 * what is *this pedal's*: its card, its title, its preset bar and its trims.
 *
 * The parameters are unprefixed here (`mix`, `ltime`), which is DelayFace's
 * default, so nothing is passed.
 */
export default function App() {
  useEffect(() => installAutoResize(), []);

  return (
    <div className="page">
      {/* theme="onyx" on the Delay face only - see main.jsx. Wah never
          passes a theme, so none of this reaches it (packages/pedal-ui/src/
          tokens.css's [data-pui-theme="onyx"] block). */}
      {/* 568 + the link bracket's own column - see --pd-link-col. */}
      <Card
        title="Peak Delay"
        headerCenter={<JucePresetBar />}
        headerRight={<HeaderLevels />}
        className="pd-card"
        width={528}
      >
        <DelayFace />
      </Card>
    </div>
  );
}
