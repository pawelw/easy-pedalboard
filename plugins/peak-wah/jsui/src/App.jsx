import { Card, WaveIcon } from "@synthpeak/pedal-ui";
import { JuceKnob, JuceToggle, JuceFilterScope } from "./juceBindings.jsx";
import "./index.css";

export default function App() {
  return (
    <div className="page">
      <Card title="Peak Wah" subtitle="Envelope Filter" width={566}>
        <div className="pw-board">
          <div className="pw-grid8">
            <JuceKnob parameterId="mix" caption="Mix" size={92} />
            <JuceKnob parameterId="freq" caption="Freq" size={72} />
            <JuceKnob
              parameterId="shape"
              caption="Shape"
              size={72}
              icon={(v) => <WaveIcon shape01={v} size={22} />}
            />
            <JuceKnob parameterId="decay" caption="Decay" size={72} />

            <JuceKnob parameterId="q" caption="Q" size={72} />
            <JuceKnob parameterId="range" caption="Range" size={72} />
            <JuceKnob parameterId="time" caption="Time" size={72} />
            <JuceKnob parameterId="ftype" caption="Filter Type" size={72} />
          </div>

          <div className="pw-toggles">
            <JuceToggle parameterId="stereo" caption="Stereo" />
            <JuceToggle parameterId="sync" caption="Sync" />
          </div>

          <div className="pw-divider" />
        </div>

        <div className="pw-scope-gap">
          <JuceFilterScope height={72} />
        </div>
      </Card>
    </div>
  );
}
