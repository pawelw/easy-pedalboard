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

        {/* The three things the scope has to keep separable: feedback changes
            how far the run trails off, mix changes only the heights, and an
            uneven L/R pair reads as two combs at different rates. */}
        {[
          { caption: "25 % feedback — 3 audible repeats", leftMs: 600, rightMs: 600, feedback01: 0.25, mix01: 1 },
          { caption: "50 % feedback — 5", leftMs: 600, rightMs: 600, feedback01: 0.5, mix01: 1 },
          { caption: "80 % feedback — 10", leftMs: 600, rightMs: 600, feedback01: 0.8, mix01: 1 },
          { caption: "100 % feedback — 23", leftMs: 600, rightMs: 600, feedback01: 1, mix01: 1 },
          { caption: "Same 50 %, but 150 ms — tighter, same count", leftMs: 150, rightMs: 150, feedback01: 0.5, mix01: 1 },
          { caption: "Default patch — 250 ms, 35 % fb, 35 % mix", leftMs: 250, rightMs: 250, feedback01: 0.35, mix01: 0.35 },
          { caption: "50 % fb at 16 % mix — same 5, just quieter", leftMs: 600, rightMs: 600, feedback01: 0.5, mix01: 0.16 },
          { caption: "Unlinked — left 600 ms / right 380 ms", leftMs: 600, rightMs: 380, feedback01: 0.65, mix01: 0.7 },
        ].map(({ caption, ...props }) => (
          <div key={caption} style={{ display: "flex", flexDirection: "column", gap: 6 }}>
            <SectionLabel>{caption}</SectionLabel>
            <TapScope height={78} {...props} />
          </div>
        ))}

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
