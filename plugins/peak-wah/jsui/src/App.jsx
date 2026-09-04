import { Card } from "@synthpeak/pedal-ui";
import { JuceKnob, JuceToggle, JuceLedButton } from "./juceBindings.jsx";
import "./index.css";

export default function App() {
  return (
    <div className="page">
      <Card title="Peak Wah" subtitle="Envelope Filter" width={566}>
        <div className="pw-toprow">
          <JuceLedButton parameterId="on">Bypass</JuceLedButton>
        </div>

        <div className="pw-clusters">
          <section className="pw-cluster">
            <div className="pw-grid">
              <JuceKnob parameterId="mix" caption="Mix" size={84} />
              <JuceKnob parameterId="freq" caption="Freq" size={72} />
              <JuceKnob parameterId="q" caption="Q" size={72} />
              <JuceKnob parameterId="range" caption="Range" size={72} />
            </div>
            <JuceToggle parameterId="stereo" caption="Stereo" />
          </section>

          <div className="pw-divider" />

          <section className="pw-cluster">
            <div className="pw-grid">
              <JuceKnob parameterId="decay" caption="Decay" size={72} />
              <JuceKnob parameterId="shape" caption="Shape" size={84} />
              <JuceKnob parameterId="time" caption="Time" size={72} />
              <JuceKnob parameterId="ftype" caption="Filter Type" size={84} />
            </div>
            <JuceToggle parameterId="sync" caption="Sync" />
          </section>
        </div>
      </Card>
    </div>
  );
}
