import { useEffect } from "react";
import { Card, Logo, WaveIcon, FilterCurveIcon } from "@synthpeak/pedal-ui";
import { JuceKnob, JuceToggle, JuceFilterScope, useFilterMod } from "./juceBindings.jsx";
import PresetBar from "./PresetBar.jsx";
import { installAutoResize } from "./autoSize.js";
import "./index.css";

export default function App() {
  useEffect(() => installAutoResize(), []);
  const { level } = useFilterMod();

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
            <JuceKnob parameterId="decay" caption="Decay" size={62} endMarkerLabel="∞" />

            <JuceKnob parameterId="q" caption="Q" size={62} />
            <JuceKnob parameterId="range" caption="Range" size={62} />
            <JuceKnob parameterId="time" caption="Time" size={62} />
            <JuceKnob
              parameterId="ftype"
              caption="Filter Type"
              size={62}
              icon={(v) => <FilterCurveIcon type01={v} size={22} />}
            />
          </div>

          <div className="pw-toggles">
            <JuceToggle parameterId="stereo" caption="Stereo" offCaption="Mono" />
            <JuceToggle parameterId="sync" caption="Sync" offCaption="ms" />
          </div>

          <div className="pw-divider" />
          <div className="pw-center-logo">
            <Logo size={29} level={level} />
            {/* Temporary: isolates "the level never reaches the UI" from "it
                reaches the UI but the glow doesn't render" while chasing why
                playing guitar shows no glow - remove once found. */}
            <div style={{ fontSize: 9, color: "#999", position: "absolute", marginTop: 16 }}>
              {Math.round(level * 100)}%
            </div>
          </div>
        </div>

        <div className="pw-scope-gap">
          <JuceFilterScope height={72} />
        </div>
      </Card>
    </div>
  );
}
