import { useEffect, useState } from "react";
import {
  PedalUIProvider,
  BarDisplay,
  Card,
  EngineStepper,
  Knob,
  ModIcon,
  ModulePanel,
  PhaserIcon,
  PowerToggle,
  PresetBar,
  Slider,
  Readout,
  SpaceIcon,
  SpringIcon,
  TapeIcon,
  TapScope,
  TremoloIcon,
  SliderRow,
  Pill,
  LinkIcon,
  SectionLabel,
} from "@synthpeak/pedal-ui";

/** A note onset every two seconds, so the scopes below light the way they do
    in a host. In a real face this count comes off the audio thread
    (PeakDelayProcessor::strikeCountUi) and there is nothing to fake - a scope
    with no signal reaching it stays still, which is the whole point of it. */
// The engine name -> glyph map is the *face's*, not EngineStepper's: which
// mark stands for which engine is a design decision per host, and the stepper
// only ever renders the node it is handed. Peak Alpine's own face will carry
// its own copy of this. Chorus is ModIcon on purpose - see the note in
// pedal-ui's index.js.
const MOD_ENGINE_ICONS = {
  Tape: <TapeIcon size={26} />,
  Tremolo: <TremoloIcon size={26} />,
  Chorus: <ModIcon size={26} />,
  Phaser: <PhaserIcon size={26} />,
};

const REVERB_ENGINE_ICONS = {
  Space: <SpaceIcon size={26} />,
  Spring: <SpringIcon size={26} />,
};

// One cycle of a tremolo's envelope, as BarDisplay wants it: plain pixel
// heights. In the real face these come off the Shape and Amount knobs; here
// they are the handoff's own reference curve, 8px at the nodes out to 34px at
// the peaks.
const TREM_BARS = Array.from({ length: 26 }, (_, i) => 8 + 26 * Math.abs(Math.sin((i / 25) * Math.PI * 2)));

// A reverb tail, bottom-aligned. Seven bars is enough to read as a decay and
// few enough that none of them is noise.
const DECAY_BARS = [34, 29, 24, 19, 15, 11, 8];

function useDemoStrikes(everyMs = 2000) {
  const [strikes, setStrikes] = useState(0);

  useEffect(() => {
    const id = setInterval(() => setStrikes((n) => n + 1), everyMs);
    return () => clearInterval(id);
  }, [everyMs]);

  return strikes;
}

// Every component new in the onyx handoff (design_handoff_peak_delay_onyx/),
// rendered once per theme so a token change in tokens.css shows up on every
// column immediately. A theme whose block does not override a given token
// shows the :root value there - that's the point of the page, not a bug.
function Showcase() {
  const [knob, setKnob] = useState(0.4);
  const [soft, setSoft] = useState(0.5);
  const [softSmall, setSoftSmall] = useState(0.72);
  // Off centre on purpose: a centre-reading knob parked at 0.5 draws no arc at
  // all, which is the right picture and a useless demonstration of one.
  const [trim, setTrim] = useState(0.68);
  const [slider, setSlider] = useState(0.3);
  const [inLevel, setInLevel] = useState(0.66);
  const [outLevel, setOutLevel] = useState(0.66);
  const [linked, setLinked] = useState(true);
  const [ms, setMs] = useState(false);
  // Held here so the bar is controlled: with no selection to show, the picker
  // cannot demonstrate the thing worth looking at - that picking a preset out
  // of a category column puts its short name in the box and re-opens on the
  // right column next time.
  const [preset, setPreset] = useState(null);
  const strikes = useDemoStrikes();

  // The multi-effect shell's own state. Held here rather than inside the
  // components because every one of them is controlled - the same contract
  // Knob and Pill have, so a face can put any of it on a JUCE parameter.
  const [bypassed, setBypassed] = useState(false);
  const [modOn, setModOn] = useState(true);
  // On, so the row below shows an accent rather than the off state's dim ink -
  // the off state is one click away and is what the first toggle turns into.
  const [reverbOn, setReverbOn] = useState(true);
  const [modEngine, setModEngine] = useState("Tremolo");
  const [reverbEngine, setReverbEngine] = useState("Space");
  const [modLevel, setModLevel] = useState(0.55);
  const [modMix, setModMix] = useState(0.4);
  const [modKnobs, setModKnobs] = useState([0.58, 0.36, 0.5, 0.3]);
  const setModKnob = (i, v) => setModKnobs((all) => all.map((old, at) => (at === i ? v : old)));

  return (
    <Card
      title="Components"
      subtitle="pedal-ui showcase"
      // Wide enough that the title, the preset bar and the level faders all
      // fit on one header line - the centre slot is centred on the card, so a
      // left half wider than half the card runs into it.
      width={640}
      headerCenter={<PresetBar value={preset} onLoad={setPreset} />}
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

        {/* scaleFrom="centre": the arc reads out from twelve o'clock in
            whichever direction the knob has been turned, for a trim whose
            resting value is the middle of its range rather than an end. Peak
            Alpine's module Level knobs are this - at rest they are doing
            nothing, and a knob wound fully clockwise said the opposite.
            Shown beside the "min" reading at the same size, because the point
            of it is the comparison. */}
        <SectionLabel>Soft knob — scaleFrom "min" and "centre"</SectionLabel>
        <div style={{ display: "flex", alignItems: "flex-end", gap: 34 }}>
          <Knob
            variant="soft"
            value={softSmall}
            onChange={setSoftSmall}
            caption="Level"
            valueLabel={`${Math.round(softSmall * 100)} %`}
            size={42}
          />
          <span style={{ "--pui-soft-lit": "var(--pui-accent-mod)" }}>
            <Knob
              variant="soft"
              value={trim}
              onChange={setTrim}
              scaleFrom="centre"
              caption="Level"
              valueLabel={`${(trim * 24 - 12).toFixed(1)} dB`}
              size={42}
            />
          </span>
        </div>

        <Readout label="L" value="1/8" unit="250 ms" />

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

        {/* ------------------------------------------------ multi-effect host
            Everything below is new in design_handoff_peak_alpine/. It is a
            shell rather than a control: a module is a whole pedal's worth of
            face inside a host's, so the pieces are shown assembled as well as
            on their own - a PowerToggle in isolation says very little about
            whether the accent reads. */}

        <SectionLabel>Preset bar — separated</SectionLabel>
        <PresetBar variant="separated" value={preset} onLoad={setPreset} />

        {/* One control at every scope: two modules' accents, then the same ring
            with no ModulePanel above it, which is the host header's own bypass
            falling back to the face's ink. */}
        <SectionLabel>Power toggle — accented, and on the bare face</SectionLabel>
        <div style={{ display: "flex", alignItems: "center", gap: 18 }}>
          <span style={{ "--pui-accent": "var(--pui-accent-mod)" }}>
            <PowerToggle on={modOn} onToggle={setModOn} />
          </span>
          <span style={{ "--pui-accent": "var(--pui-accent-reverb)" }}>
            <PowerToggle on={reverbOn} onToggle={setReverbOn} />
          </span>
          <PowerToggle on={!bypassed} onToggle={(next) => setBypassed(!next)} ariaLabel="Bypass" />
        </div>

        <SectionLabel>Engine stepper</SectionLabel>
        <div style={{ display: "flex", flexDirection: "column", gap: 10, width: 162 }}>
          <span style={{ "--pui-accent": "var(--pui-accent-mod)" }}>
            <EngineStepper
              engines={["Tape", "Tremolo", "Chorus", "Phaser"]}
              value={modEngine}
              icon={MOD_ENGINE_ICONS[modEngine]}
              onChange={setModEngine}
            />
          </span>
          <span style={{ "--pui-accent": "var(--pui-accent-reverb)" }}>
            <EngineStepper
              engines={["Space", "Spring"]}
              value={reverbEngine}
              icon={REVERB_ENGINE_ICONS[reverbEngine]}
              onChange={setReverbEngine}
            />
          </span>
        </div>

        <SectionLabel>Bar display — envelope (centre) and decay (bottom)</SectionLabel>
        <div style={{ display: "flex", gap: 14 }}>
          <div style={{ width: 162, "--pui-accent": "var(--pui-accent-mod)" }}>
            <BarDisplay heights={TREM_BARS} ariaLabel="Tremolo envelope" />
          </div>
          <div style={{ width: 162, "--pui-accent": "var(--pui-accent-reverb)" }}>
            <BarDisplay heights={DECAY_BARS} align="bottom" ariaLabel="Reverb decay" />
          </div>
        </div>

        {/* The shell assembled: a side module at its real 180px track width,
            with the header Level knob, the stepper, a display, two rows of
            two 36px knobs and the footer Mix. Switching its engine here is
            what shows the accent reaching all three places it belongs -
            toggle, stepper and every knob's arc - off one prop. */}
        <SectionLabel>Module panel</SectionLabel>
        <div style={{ display: "flex", gap: 14, alignItems: "stretch" }}>
          <ModulePanel
            name="Mod"
            accent="var(--pui-accent-mod)"
            width={180}
            on={modOn}
            onToggle={setModOn}
            headerRight={<Knob variant="soft" size={24} bare value={modLevel} onChange={setModLevel} />}
            footer={
              <Knob
                variant="soft"
                size={38}
                caption="Mix"
                value={modMix}
                onChange={setModMix}
                valueLabel={`${Math.round(modMix * 100)} %`}
              />
            }
          >
            <EngineStepper
              engines={["Tape", "Tremolo", "Chorus", "Phaser"]}
              value={modEngine}
              icon={MOD_ENGINE_ICONS[modEngine]}
              onChange={setModEngine}
            />

            {modEngine === "Tremolo" && (
              <div style={{ marginTop: 16 }}>
                <BarDisplay heights={TREM_BARS} ariaLabel="Tremolo envelope" />
              </div>
            )}

            <div style={{ marginTop: 22, display: "flex", flexDirection: "column", gap: 18, paddingBottom: 14 }}>
              {[
                ["Amount", "Rate"],
                ["Shape", "Tube"],
              ].map((row, rowAt) => (
                <div key={row[0]} style={{ display: "flex", justifyContent: "space-evenly", gap: 8 }}>
                  {row.map((name, at) => {
                    const index = rowAt * 2 + at;
                    return (
                      <Knob
                        key={name}
                        variant="soft"
                        size={36}
                        caption={name}
                        value={modKnobs[index]}
                        onChange={(v) => setModKnob(index, v)}
                        valueLabel={`${Math.round(modKnobs[index] * 100)} %`}
                      />
                    );
                  })}
                </div>
              ))}
            </div>
          </ModulePanel>

          {/* The wide tone, empty. Same panel as its neighbour - all `tone`
              buys is the wider padding a 560px module's contents need - and
              there is nothing about that worth filling with borrowed
              controls. The Delay face itself is what goes in here. */}
          <ModulePanel
            name="Delay"
            accent="var(--pui-accent-delay)"
            tone="wide"
            width={220}
            on
            onToggle={() => {}}
          >
            <span style={{ paddingBottom: 14 }}>
              <SectionLabel>tone="wide"</SectionLabel>
            </span>
          </ModulePanel>
        </div>

        {/* Card's one new prop. A Card inside a Card is not a layout anyone
            would ship, but the header is the whole of what changed and it
            cannot be shown without one. */}
        <SectionLabel>Card — subtitlePlacement="below"</SectionLabel>
        <Card
          title="Peak Alpine"
          subtitle="Modulation / Delay / Reverb machine"
          subtitlePlacement="below"
          width={420}
          headerRight={<PowerToggle on={!bypassed} onToggle={(next) => setBypassed(!next)} ariaLabel="Bypass" />}
        />
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

        <div>
          <h2 className="components-columns__label">Grey</h2>
          <PedalUIProvider theme="grey">
            <div className="components-columns__pane">
              <Showcase />
            </div>
          </PedalUIProvider>
        </div>
      </div>
    </div>
  );
}
