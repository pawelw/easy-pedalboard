import { useEffect, useState } from "react";
import {
  CrushScope,
  EngineStepper,
  FilterScope,
  ModulePanel,
  ModuleTabs,
  RingScope,
  RustScope,
  Toggle,
  WaveIcon,
  freqHzFor01,
} from "@synthpeak/pedal-ui";
import {
  JuceKnob,
  JuceMacroKnob,
  JucePill,
  ParamScope,
  useJuceChoiceValue,
  useJuceSliderValue,
  useJuceToggleValue,
} from "@synthpeak/pedal-ui/juce";
import { ENGINES, WAVES } from "./engines.jsx";
import "./ArtifactFace.css";

/**
 * Peak Artifact's face, minus its pedal enclosure: one switchable module drawn
 * as a `ModulePanel` - a power toggle and name in the header, an engine
 * stepper, and then the selected engine's body, with Mix in the footer. Filter
 * has a response scope, two rows of knobs, the wave picker and a Mono/Stereo
 * switch; Bit Crush has a stepped-wave display and two rows of knobs; Ring Mod
 * has a lattice display, its three knobs and a Wobble / Octave switch; Rust
 * has a corrosion display, two rows of knobs and an Oxide / Contact switch;
 * Amp has a display, two rows of knobs and a Mono/Stereo switch of its own
 * (a Haas widener, not the Filter engine's channel-diversity one - see
 * `MonoStereoSwitch`'s `parameterId`). The footer Mix doubles as the Ring
 * Mod's and Rust's Blend.
 *
 * One component, two hosts. Peak Artifact wraps this in its own Card; Peak
 * Alpine drops it into its module row as the first module. The whole reason
 * this package exists is that the Artifact module in the multi-effect host is
 * not a re-draw of Peak Artifact's face, it *is* that face - so a fix lands in
 * both and neither can drift.
 *
 * Amp's display reuses `CrushScope` (see `AmpDisplay`) with its Bits input
 * pinned to 0 - Amp's Bit knob is sample-rate reduction only, no amplitude
 * reduction, so the picture is a held sine with no amplitude bands, truthful
 * to what the engine actually does (see ee::fx::ArtifactModule's class note).
 *
 * `prefix` is the parameter-id prefix its controls bind through: "" for Peak
 * Artifact, whose parameters are plain (`mix`, `flt.freq`), and "art." for Peak
 * Alpine, whose are namespaced by module. Nothing below takes an id map; the
 * `ParamScope` does the whole job, and the leaf names are identical in both
 * plugins on purpose.
 *
 * `headerRight` is whatever the host wants in the module header's right-hand
 * slot - Peak Alpine puts a Level trim there, the same one its other modules
 * carry; Peak Artifact passes nothing and the slot stays empty. It is the
 * host's to supply because that Level is the host's chrome, not one of this
 * pedal's parameters - keeping it out here is what lets the face bind only to
 * names Peak Artifact actually has.
 *
 * `easyTab` adds the Easy / Adv strip at the foot of the body - Peak Alpine
 * turns it on, the standalone Peak Artifact pedal does not, so that pedal's
 * face is untouched. `easyConfig` is the per-engine macro map it needs when
 * `easyTab` is on: `{ [engineName]: { name, targets } }`, handed in from the
 * host rather than kept on this package's engine table (which the standalone
 * pedal has no use for). See Peak Alpine's engines.jsx `ARTIFACT_EASY`.
 */
export default function ArtifactFace({
  prefix = "",
  headerRight = null,
  easyTab = false,
  easyConfig = null,
}) {
  return (
    <ParamScope prefix={prefix}>
      <ArtifactFaceBody headerRight={headerRight} easyTab={easyTab} easyConfig={easyConfig} />
    </ParamScope>
  );
}

// Peak Artifact's red. It reaches the power ring, the engine stepper and the
// knob value arcs through the one `accent` prop on ModulePanel.
const ACCENT = "#c00001";

// The response scope's ink, keeping Peak Wah's scope shapes but in this face's
// red rather than its blue/grey. The well's own background and grid come from
// the --pui-scope-* overrides on .af-display (ArtifactFace.css).
const SCOPE = {
  baseColor: "#e5504e", // the resting curve - bright enough to read on the dark well
  sweepColor: "#c00001", // the swept L/R curves and the Range band
  fillColor: "rgba(224, 72, 70, 0.16)", // wash under the swept curves
};

/**
 * The Filter engine's live cutoff-sweep exponent for both channels, pushed from
 * the processor as the one "filterMod" event (the editor's Timer) - the same
 * feed Peak Wah's scope rides on. Outside a real host there is no backend to
 * send it, so it stays at 0 and the two swept curves rest on the base curve.
 *
 * The event name is not scoped the way parameter ids are: it is one feed per
 * editor, so a host embedding this face (Peak Alpine) emits it under the same
 * name rather than the component learning a second one.
 */
function useFilterMod() {
  const [mod, setMod] = useState({ modL: 0, modR: 0 });

  useEffect(() => {
    if (typeof window.__JUCE__?.backend?.addEventListener !== "function") return undefined;
    const id = window.__JUCE__.backend.addEventListener("filterMod", (event) =>
      setMod({ modL: event.modL ?? 0, modR: event.modR ?? 0 }),
    );
    return () => window.__JUCE__.backend.removeEventListener(id);
  }, []);

  return mod;
}

// The Easy / Adv strip is hidden for now and every face opens on Adv. The Easy
// path below is kept whole so flipping this back is the only change needed.
const SHOW_EASY_TABS = false;

/** Split out so its hooks resolve *inside* the ParamScope above - a hook in
    ArtifactFace itself would read the enclosing scope, not the one it declares. */
function ArtifactFaceBody({ headerRight = null, easyTab = false, easyConfig = null }) {
  // Default index 2 (Filter) with no backend - the processor opens on Filter
  // too, since it is the only voiced engine.
  const [engineIndex, setEngine] = useJuceChoiceValue("engine", ENGINES.length, 2);
  const [on, setOn] = useJuceToggleValue("on", true);
  // "adv" is the full engine body, "easy" the single macro knob. Only reached
  // when `easyTab` is on and `easyConfig` has an entry for this engine.
  const [tab, setTab] = useState("adv");
  const engine = ENGINES[engineIndex] ?? ENGINES[0];
  const easy = easyTab ? easyConfig?.[engine.name] : null;

  return (
    <ModulePanel
      name="Artifact"
      accent={ACCENT}
      width={168}
      on={on}
      onToggle={setOn}
      headerRight={headerRight}
      className="af-module"
      footer={<JuceKnob parameterId="mix" caption="Mix" variant="soft" size={38} />}
    >
      <EngineStepper
        engines={ENGINES.map((e) => e.name)}
        value={engine.name}
        icon={engine.icon}
        label="Engine"
        onChange={(next) => setEngine(ENGINES.findIndex((e) => e.name === next))}
      />

      {easy && tab === "easy" ? (
        /* The Easy face: this engine's own display well, then one macro knob
           riding its Adv knobs (Peak Alpine's ARTIFACT_EASY). The macro is
           resolved through the `art.` ParamScope this face already declares -
           no idPrefix needed. The well reads the same parameters the macro
           moves, so it answers to the Easy knob too. */
        <>
          <ArtifactEngineDisplay engine={engine.name} />
          <div className="af-easy">
            {/* Keyed by engine so the macro re-centres on an engine change
                rather than carrying its position across. */}
            <JuceMacroKnob key={engine.name} caption={easy.name} targets={easy.targets} />
          </div>
        </>
      ) : engine.body === "filter" ? (
        <FilterBody />
      ) : engine.body === "crush" ? (
        <CrushBody />
      ) : engine.body === "rust" ? (
        <RustBody />
      ) : engine.body === "amp" ? (
        <AmpBody />
      ) : (
        <RingBody />
      )}

      {SHOW_EASY_TABS && easy && <ModuleTabs value={tab} onChange={setTab} />}
    </ModulePanel>
  );
}

/* Each engine's display well, split out so its own slider hooks run only for
   the engine that is actually showing one - and so the Easy view can render
   the same well above its macro knob without also mounting that engine's body.
   The same reason Peak Alpine splits its displays out. */
function FilterDisplay() {
  const [freq] = useJuceSliderValue("flt.freq");
  const [q] = useJuceSliderValue("flt.q");
  const [range] = useJuceSliderValue("flt.range");
  const { modL, modR } = useFilterMod();

  return (
    // The same component Peak Wah's scope is: a resting curve whose peak rises
    // and narrows with Q and slides with Freq, a translucent band showing how
    // far Range lets it sweep, and two curves riding the live L/R sweep inside
    // it. Only the ink changes here.
    <div className="af-display">
      <FilterScope
        baseFreqHz={freqHzFor01(freq)}
        resonance01={q}
        sweepDepth01={range}
        modL={modL}
        modR={modR}
        height={64}
        baseColor={SCOPE.baseColor}
        sweepColor={SCOPE.sweepColor}
        fillColor={SCOPE.fillColor}
      />
      <span className="af-inf" aria-label="Decay: always on">
        &#8734;
      </span>
    </div>
  );
}

function CrushDisplay() {
  const [bits] = useJuceSliderValue("crush.bits");
  const [rate] = useJuceSliderValue("crush.rate");
  const [jitter] = useJuceSliderValue("crush.jitter");

  return (
    // A picture of the three destructive knobs: the reference sine held in time
    // by Rate, quantised by Bits, and knocked out of step by Jitter. The Filter
    // knob shapes what comes after, so it is not in the trace.
    <div className="af-display af-display--crush">
      <CrushScope
        bits01={bits}
        rate01={rate}
        jitter01={jitter}
        height={64}
        baseColor={SCOPE.baseColor}
        fillColor={SCOPE.fillColor}
      />
    </div>
  );
}

function RingDisplay() {
  const [freq] = useJuceSliderValue("ring.freq");
  const [tweak] = useJuceSliderValue("ring.tweak");
  const [rect] = useJuceSliderValue("ring.rect");
  const [mode] = useJuceChoiceValue("ring.mode", 2, 0);

  return (
    // The DSB-SC lattice: a slow program sine cut into the carrier. Freq sets
    // the lattice density, Tweak wobbles it (Earworm) or leans it toward the
    // rectified octave (Green Lantern), Rectify folds the carrier one-sided so
    // the program's own shape starts showing through. Picture only, no feed.
    <div className="af-display af-display--ring">
      <RingScope
        freq01={freq}
        tweak01={tweak}
        rect={rect * 2 - 1}
        mode={mode}
        height={64}
        baseColor={SCOPE.baseColor}
        fillColor={SCOPE.fillColor}
      />
    </div>
  );
}

function AmpDisplay() {
  const [bit] = useJuceSliderValue("amp.bit");

  return (
    // CrushScope with Bits pinned to 0 - a held (sample-rate-reduced) sine with
    // no amplitude bands, since Amp's Bit knob never touches the word length.
    // The tube drive and Mids/Tone shaping ahead of and after it aren't shown.
    <div className="af-display af-display--crush">
      <CrushScope
        bits01={0}
        rate01={bit}
        jitter01={0}
        height={64}
        baseColor={SCOPE.baseColor}
        fillColor={SCOPE.fillColor}
      />
    </div>
  );
}

function RustDisplay() {
  const [grind] = useJuceSliderValue("rust.grind");
  const [mode] = useJuceChoiceValue("rust.mode", 2, 0);

  return (
    // A reference sine coarsened by Grind - squared off (Contact) or wavering
    // and darkening (Oxide). Picture only, no live wear feed.
    <div className="af-display af-display--rust">
      <RustScope
        grind01={grind}
        mode={mode}
        height={64}
        baseColor={SCOPE.baseColor}
        fillColor={SCOPE.fillColor}
      />
    </div>
  );
}

/* The display for whichever engine is selected - what the Easy view puts above
   its macro knob, the same well the Adv body carries. */
function ArtifactEngineDisplay({ engine }) {
  if (engine === "Crasher") return <CrushDisplay />;
  if (engine === "Ring") return <RingDisplay />;
  if (engine === "Rust") return <RustDisplay />;
  if (engine === "Amp") return <AmpDisplay />;
  return <FilterDisplay />;
}

/* Its own component so the Filter-only hooks don't run for the other two
   engines - the same reason Peak Alpine splits its displays out. */
function FilterBody() {
  const [waveIndex, setWave] = useJuceChoiceValue("flt.wave", WAVES.length);
  const wave = WAVES[waveIndex] ?? WAVES[0];

  return (
    <>
      <FilterDisplay />

      <div className="af-knobs">
        <div className="af-knob-row">
          <JuceKnob parameterId="flt.freq" caption="Freq" variant="soft" size={36} />
          <JuceKnob parameterId="flt.q" caption="Q" variant="soft" size={36} />
        </div>
        {/* Range and Time each carry a small control directly under them: the
            wave <> picker (glyph only, no name - it is small enough to sit here
            rather than on a row of its own, which is what keeps the module
            short) and the SYNC pill, the same control Peak Delay uses. */}
        <div className="af-knob-row">
          <div className="af-subcol">
            <JuceKnob parameterId="flt.range" caption="Range" variant="soft" size={36} />
            <div className="af-sub af-sub--wave">
              <EngineStepper
                engines={WAVES.map((w) => w.name)}
                value={wave.name}
                icon={<WaveIcon shape01={wave.shape01} size={15} />}
                label="Wave"
                onChange={(next) => setWave(WAVES.findIndex((w) => w.name === next))}
              />
            </div>
          </div>

          <div className="af-subcol">
            <JuceKnob parameterId="flt.time" caption="Time" variant="soft" size={36} />
            <div className="af-sub af-sub--sync">
              {/* flt.sync's own sense is already "synced to tempo", so it
                  lights when on with no invert. */}
              <JucePill parameterId="flt.sync" label="Sync" />
            </div>
          </div>
        </div>
      </div>

      <MonoStereoSwitch />
    </>
  );
}

/* The Bit Crush body: its display well and two knob rows. */
function CrushBody() {
  return (
    // Matches the Filter body's height so stepping between engines doesn't
    // resize the module - the same job .af-blank does for Ring Mod.
    <div className="af-crush">
      <CrushDisplay />

      <div className="af-knobs">
        <div className="af-knob-row">
          <JuceKnob parameterId="crush.bits" caption="Bits" variant="soft" size={36} />
          <JuceKnob parameterId="crush.rate" caption="Rate" variant="soft" size={36} />
        </div>
        <div className="af-knob-row">
          <JuceKnob parameterId="crush.lp" caption="Filter" variant="soft" size={36} />
          <JuceKnob parameterId="crush.jitter" caption="Jitter" variant="soft" size={36} />
        </div>
      </div>
    </div>
  );
}

/* The Ring Mod body: its lattice display, four knobs and the mode switch. */
function RingBody() {
  return (
    // Matches the Filter / Crush body height so stepping between engines doesn't
    // resize the module.
    <div className="af-ring">
      <RingDisplay />

      <div className="af-knobs">
        <div className="af-knob-row">
          <JuceKnob parameterId="ring.freq" caption="Freq" variant="soft" size={36} />
          <JuceKnob parameterId="ring.tweak" caption="Tweak" variant="soft" size={36} />
        </div>
        <div className="af-knob-row">
          <JuceKnob parameterId="ring.lp" caption="Filter" variant="soft" size={36} />
          {/* Bipolar: a plain sine carrier dead centre, so its arc grows out
              from twelve o'clock in whichever direction it is folded. */}
          <JuceKnob
            parameterId="ring.rect"
            caption="Rectify"
            variant="soft"
            size={36}
            scaleFrom="centre"
          />
        </div>
      </div>

      <RingModeSwitch />
    </div>
  );
}

/* Pinned to the foot of the module body, like Mono/Stereo. Wobble is the
   Ringworm's carrier-wobble voicing (Earworm in the DSP), Octave the Green
   Ringer's rectified one - named for what each does rather than for the pedal
   behind it, because "Green Lantern" wrapped to two lines in a 168px module. */
function RingModeSwitch() {
  const [mode, setMode] = useJuceChoiceValue("ring.mode", 2, 0);
  const green = mode === 1;

  return (
    <div className="af-inline-switch af-mode-switch">
      <span className="af-switch-label" data-active={!green || undefined}>
        Wobble
      </span>
      <Toggle
        checked={green}
        onChange={(v) => setMode(v ? 1 : 0)}
        ariaLabel="Wobble / Octave"
      />
      <span className="af-switch-label" data-active={green || undefined}>
        Octave
      </span>
    </div>
  );
}

/* The Rust body: its corrosion display, one knob row (Grind / Tone) and the
   mode switch. Wear and its recovery are fixed inside the engine, so there is
   no knob for them. */
function RustBody() {
  return (
    // Matches the other engine bodies' height so stepping between engines
    // doesn't resize the module.
    <div className="af-rust">
      <RustDisplay />

      <div className="af-knobs">
        <div className="af-knob-row">
          <JuceKnob parameterId="rust.grind" caption="Grind" variant="soft" size={36} />
          <JuceKnob parameterId="rust.tone" caption="Tone" variant="soft" size={36} />
        </div>
      </div>

      <RustModeSwitch />
    </div>
  );
}

/* The Amp body: its display well, two knob rows - Drive / Mids, then Bit /
   Tone, the order the signal actually runs through (drive, then the
   sample-and-hold, then the Mids lift, then Tone) - and the Mono/Stereo Haas
   switch at the foot, the same slot Filter's own Mono/Stereo occupies. */
function AmpBody() {
  return (
    // Matches the other engine bodies' height so stepping between engines
    // doesn't resize the module.
    <div className="af-amp">
      <AmpDisplay />

      <div className="af-knobs">
        <div className="af-knob-row">
          <JuceKnob parameterId="amp.drive" caption="Drive" variant="soft" size={36} />
          <JuceKnob parameterId="amp.mids" caption="Mids" variant="soft" size={36} />
        </div>
        <div className="af-knob-row">
          <JuceKnob parameterId="amp.bit" caption="Bit" variant="soft" size={36} />
          {/* Bipolar: flat dead centre, so its arc grows out from twelve
              o'clock the way Tape's Tone does. */}
          <JuceKnob parameterId="amp.tone" caption="Tone" variant="soft" size={36} scaleFrom="centre" />
        </div>
      </div>

      <MonoStereoSwitch parameterId="amp.stereo" />
    </div>
  );
}

/* Pinned to the foot of the module body, like Mono/Stereo. Oxide is the soft
   magnetic-decay voicing (full warble, wear darkens the tone); Contact is
   harder and more electrical (little warble, wear squares the peaks with a
   hard clip and deepens the crumble). Neither adds noise. */
function RustModeSwitch() {
  const [mode, setMode] = useJuceChoiceValue("rust.mode", 2, 0);
  const contact = mode === 1;

  return (
    <div className="af-inline-switch af-mode-switch">
      <span className="af-switch-label" data-active={!contact || undefined}>
        Oxide
      </span>
      <Toggle
        checked={contact}
        onChange={(v) => setMode(v ? 1 : 0)}
        ariaLabel="Oxide / Contact"
      />
      <span className="af-switch-label" data-active={contact || undefined}>
        Contact
      </span>
    </div>
  );
}

/* Pinned to the foot of the module body, just above the footer. Filter's own
   Mono/Stereo picks which channels the LFO diverges on; Amp's does a Haas
   widen on the right channel instead (see ee::fx::ArtifactModule) - same
   switch, same slot, a different `parameterId` for each. */
function MonoStereoSwitch({ parameterId = "flt.stereo" }) {
  const [stereo, setStereo] = useJuceToggleValue(parameterId, false);

  return (
    <div className="af-inline-switch af-ms-switch">
      <span className="af-switch-label" data-active={!stereo || undefined}>
        Mono
      </span>
      <Toggle checked={stereo} onChange={setStereo} ariaLabel="Mono / Stereo" />
      <span className="af-switch-label" data-active={stereo || undefined}>
        Stereo
      </span>
    </div>
  );
}
