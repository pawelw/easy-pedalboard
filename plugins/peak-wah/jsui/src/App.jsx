import { Card, Logo, WaveIcon } from "@synthpeak/pedal-ui";
import { JuceKnob, JuceToggle, JuceFilterScope } from "./juceBindings.jsx";
import PresetBar from "./PresetBar.jsx";
import "./index.css";

export default function App() {
  return (
    <div className="page">
      <Card title="Peak Wah" subtitle="Envelope Filter" width={566} headerRight={<PresetBar />} showLogo={false}>
        <div className="pw-board">
          <div className="pw-grid8">
            <JuceKnob parameterId="mix" caption="Mix" size={92} />
            <JuceKnob parameterId="freq" caption="Freq" size={62} />
            <JuceKnob
              parameterId="shape"
              caption="Shape"
              size={62}
              icon={(v) => <WaveIcon shape01={v} size={22} />}
            />
            <JuceKnob parameterId="decay" caption="Decay" size={62} />

            <JuceKnob parameterId="q" caption="Q" size={62} />
            <JuceKnob parameterId="range" caption="Range" size={62} />
            <JuceKnob parameterId="time" caption="Time" size={62} endMarkerLabel="∞" />
            <JuceKnob parameterId="ftype" caption="Filter Type" size={62} />
          </div>

          <div className="pw-toggles">
            <JuceToggle parameterId="stereo" caption="Stereo" />
            <JuceToggle parameterId="sync" caption="Sync" />
          </div>

          <div className="pw-divider" />
          <div className="pw-center-logo">
            <Logo size={24} />
          </div>
        </div>

        <div className="pw-scope-gap">
          <JuceFilterScope height={72} />
        </div>
      </Card>
    </div>
  );
}
