import { useEffect } from "react";
import { Card, TapScope, SectionLabel, LinkIcon } from "@synthpeak/pedal-ui";
import { JuceKnob, JucePill, JuceSliderRow, useJuceSliderValue, useTimeReadoutText } from "./juceBindings.jsx";
import TimeControl from "./TimeControl.jsx";
import { installAutoResize } from "./autoSize.js";
import "./index.css";

/** "STEREO · 1/8 · 1/8T" - the same toggle-aware text each Time row's own
    Readout shows for its value, just concatenated into the header's meta
    line rather than fetched a third time some other way. */
function HeaderMeta() {
  const [leftText] = useTimeReadoutText("ltime");
  const [rightText] = useTimeReadoutText("rtime");
  return <span className="pd-meta">Stereo · {leftText} · {rightText}</span>;
}

export default function App() {
  useEffect(() => installAutoResize(), []);

  const [leftTime01] = useJuceSliderValue("ltime");
  const [rightTime01] = useJuceSliderValue("rtime");
  const [feedback01] = useJuceSliderValue("fb");

  return (
    <div className="page">
      {/* theme="onyx" on the Delay face only - see main.jsx. Wah never
          passes a theme, so none of this reaches it (packages/pedal-ui/src/
          tokens.css's [data-pui-theme="onyx"] block). */}
      <Card title="Peak Delay" headerRight={<HeaderMeta />} className="pd-card" width={568}>
        <TapScope height={78} leftTime01={leftTime01} rightTime01={rightTime01} feedback01={feedback01} />

        <div className="pd-row">
          <JuceKnob parameterId="mix" caption="Mix" variant="scale" size={84} />
          <JuceKnob parameterId="fb" caption="Feedback" variant="scale" size={84} />

          <div className="pd-time-col">
            <TimeControl side="Left" parameterId="ltime" />
            <TimeControl side="Right" parameterId="rtime" />

            <div className="pd-pills">
              <JucePill parameterId="sync" icon={<LinkIcon size={13} />} label="Linked" />
              <JucePill parameterId="timeunit" label="ms" />
            </div>
          </div>
        </div>

        <div className="pd-stage">
          <SectionLabel>Pre-stage</SectionLabel>
          <JuceSliderRow parameterId="tape" name="Tape" />
          <SectionLabel>Post-stage</SectionLabel>
          <JuceSliderRow parameterId="mod" name="Mod" />
        </div>
      </Card>
    </div>
  );
}
