import { useState } from "react";
import {
  PedalUIProvider,
  Card,
  Knob,
  Readout,
  TapScope,
  SliderRow,
  Pill,
  LinkIcon,
  SectionLabel,
} from "@synthpeak/pedal-ui";

// Every component new in the onyx handoff (design_handoff_peak_delay_onyx/),
// rendered once per theme so a token change in tokens.css shows up on both
// columns immediately. Onyx looks identical to light until Phase 2 adds the
// `[data-pui-theme="onyx"]` block - that's expected, not a bug in this page.
function Showcase() {
  const [knob, setKnob] = useState(0.4);
  const [slider, setSlider] = useState(0.3);
  const [linked, setLinked] = useState(true);
  const [ms, setMs] = useState(false);

  return (
    <Card title="Components" subtitle="pedal-ui showcase">
      <div style={{ display: "flex", flexDirection: "column", gap: 20 }}>
        <Knob
          variant="scale"
          value={knob}
          onChange={setKnob}
          caption="Scale knob"
          valueLabel={`${Math.round(knob * 100)}`}
          size={84}
        />

        <Readout label="Left" value="1/8" unit="250 ms" />

        <TapScope height={78} feedback01={0.82} />

        <SliderRow
          name="Tape"
          value={slider}
          onChange={setSlider}
          valueLabel={`${Math.round(slider * 100)} %`}
        />

        <div style={{ display: "flex", gap: 7 }}>
          <Pill icon={<LinkIcon size={13} />} label="Linked" pressed={linked} onClick={() => setLinked((v) => !v)} />
          <Pill label="ms" pressed={ms} onClick={() => setMs((v) => !v)} />
        </div>

        <SectionLabel>Pre-stage</SectionLabel>
      </div>
    </Card>
  );
}

export default function Components() {
  return (
    <div className="gallery">
      <a href="#" className="gallery__back">
        ← All pedals
      </a>

      <div className="components-columns">
        <div>
          <h2 className="components-columns__label">Light</h2>
          <PedalUIProvider theme="light">
            <div className="components-columns__pane">
              <Showcase />
            </div>
          </PedalUIProvider>
        </div>

        <div>
          <h2 className="components-columns__label">Onyx</h2>
          <PedalUIProvider theme="onyx">
            <div className="components-columns__pane">
              <Showcase />
            </div>
          </PedalUIProvider>
        </div>
      </div>
    </div>
  );
}
