import { useEffect, useState } from "react";
import {
  PedalUIProvider,
  Card,
  Knob,
  PresetBar,
  Slider,
  Readout,
  TapScope,
  SliderRow,
  Pill,
  LinkIcon,
  SectionLabel,
} from "@synthpeak/pedal-ui";

/** A note onset every two seconds, so the scopes below light the way they do
    in a host. In a real face this count comes off the audio thread
    (PeakDelayProcessor::strikeCountUi) and there is nothing to fake - a scope
    with no signal reaching it stays still, which is the whole point of it. */
function useDemoStrikes(everyMs = 2000) {
  const [strikes, setStrikes] = useState(0);

  useEffect(() => {
    const id = setInterval(() => setStrikes((n) => n + 1), everyMs);
    return () => clearInterval(id);
  }, [everyMs]);

  return strikes;
}

// Every component new in the onyx handoff (design_handoff_peak_delay_onyx/),
// rendered once per theme so a token change in tokens.css shows up on both
// columns immediately. Onyx looks identical to light until Phase 2 adds the
// `[data-pui-theme="onyx"]` block - that's expected, not a bug in this page.
function Showcase() {
  const [knob, setKnob] = useState(0.4);
  const [soft, setSoft] = useState(0.5);
  const [softSmall, setSoftSmall] = useState(0.72);
  const [slider, setSlider] = useState(0.3);
  const [inLevel, setInLevel] = useState(0.66);
  const [outLevel, setOutLevel] = useState(0.66);
  const [linked, setLinked] = useState(true);
  const [ms, setMs] = useState(false);
  const strikes = useDemoStrikes();

  return (
    <Card
      title="Components"
      subtitle="pedal-ui showcase"
      // Wide enough that the title, the preset bar and the level faders all
      // fit on one header line - the centre slot is centred on the card, so a
      // left half wider than half the card runs into it.
      width={640}
      headerCenter={<PresetBar />}
      headerRight={
        <div style={{ display: "flex", flexDirection: "column", gap: 5 }}>
          <Slider compact fine orientation="horizontal" length={74} label="In" value={inLevel} onChange={setInLevel} centreValue={2 / 3} valueLabel={`${Math.round(inLevel * 36 - 24)}.0 dB`} />
          <Slider compact fine orientation="horizontal" length={74} label="Out" value={outLevel} onChange={setOutLevel} centreValue={2 / 3} valueLabel={`${Math.round(outLevel * 36 - 24)}.0 dB`} />
        </div>
      }
    >
      <div style={{ display: "flex", flexDirection: "column", gap: 20 }}>
        <SectionLabel>Scale knob</SectionLabel>
        <Knob
          variant="scale"
          value={knob}
          onChange={setKnob}
          caption="Scale knob"
          valueLabel={`${Math.round(knob * 100)}`}
          size={84}
        />

        {/* variant="soft" at both of the sizes the set uses: the 84px face
            knob beside the same 42px one the footer stages are built from,
            since the arc and the needle are the two things that had to be
            checked at the small size (both shrink - Knob.jsx's SOFT_SWEEP_*
            pair and .pui-knob__pointer--needle-thin). */}
        <SectionLabel>Soft knob — 84 px and 42 px</SectionLabel>
        <div style={{ display: "flex", alignItems: "flex-end", gap: 34 }}>
          <Knob
            variant="soft"
            value={soft}
            onChange={setSoft}
            caption="Stereo"
            valueLabel={`${Math.round(soft * 100)} %`}
            subLabel={`${Math.round(soft * 100)} %`}
            size={84}
          />
          <Knob
            variant="soft"
            value={softSmall}
            onChange={setSoftSmall}
            caption="Wear"
            valueLabel={`${Math.round(softSmall * 100)} %`}
            subLabel={`${Math.round(softSmall * 100)} %`}
            size={42}
          />
        </div>

        <Readout label="Left" value="1/8" unit="250 ms" />

        {/* The three things the scope has to keep separable: feedback changes
            how far the run trails off, mix changes only the heights (and the
            dry line at the origin the other way), and an uneven L/R pair reads
            as two combs at different rates. The playhead sweeping each one is
            faked here on a timer - see useDemoStrikes. */}
        {[
          { caption: "25 % feedback — 3 audible repeats", leftMs: 600, rightMs: 600, feedback01: 0.25, mix01: 1 },
          { caption: "50 % feedback — 5", leftMs: 600, rightMs: 600, feedback01: 0.5, mix01: 1 },
          { caption: "80 % feedback — 10", leftMs: 600, rightMs: 600, feedback01: 0.8, mix01: 1 },
          { caption: "100 % feedback — 23", leftMs: 600, rightMs: 600, feedback01: 1, mix01: 1 },
          { caption: "Same 50 %, but 150 ms — tighter, same count", leftMs: 150, rightMs: 150, feedback01: 0.5, mix01: 1 },
          { caption: "Default patch — 250 ms, 35 % fb, 35 % mix", leftMs: 250, rightMs: 250, feedback01: 0.35, mix01: 0.35 },
          { caption: "Mix at 0 — nothing wet to draw, dry line at full height", leftMs: 600, rightMs: 600, feedback01: 0.5, mix01: 0 },
          { caption: "50 % fb at 16 % mix — same 5, just quieter, dry line tall", leftMs: 600, rightMs: 600, feedback01: 0.5, mix01: 0.16 },
          { caption: "Same, at 90 % mix — dry line all but gone", leftMs: 600, rightMs: 600, feedback01: 0.5, mix01: 0.9 },
          { caption: "Unlinked — left 600 ms / right 380 ms", leftMs: 600, rightMs: 380, feedback01: 0.65, mix01: 0.7 },
        ].map(({ caption, ...props }) => (
          <div key={caption} style={{ display: "flex", flexDirection: "column", gap: 6 }}>
            <SectionLabel>{caption}</SectionLabel>
            <TapScope height={78} strikes={strikes} {...props} />
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
