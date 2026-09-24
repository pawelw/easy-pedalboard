import { useState } from "react";
import { AmpScope, CrushScope, EngineStepper, ModulePanel, ModuleTabs, RingScope, RustScope, Toggle } from "@synthpeak/pedal-ui";
import {
  JuceKnob,
  JuceMacroKnob,
  ParamScope,
  useJuceChoiceValue,
  useJuceSliderValue,
  useJuceToggleValue,
} from "@synthpeak/pedal-ui/juce";
import { ENGINES } from "./engines.jsx";
import "./ArtifactFace.css";

/**
 * BitBit Artifact's face, minus its pedal enclosure: one switchable module drawn
 * as a `ModulePanel` - a power toggle and name in the header, an engine
 * stepper, and then the selected engine's body, with Mix and Tone in the footer. Bit
 * Crush has a stepped-wave display and two rows of knobs; Ring Mod has a
 * lattice display, its four knobs and a Wobble / Octave switch; Rust has a
 * corrosion display, one row of knobs and an Oxide / Contact switch; Amp has a
 * display and two rows of knobs. The footer Mix
 * doubles as the Ring Mod's and Rust's Blend. (The Filter engine that used to
 * be here is now BitBit Alpine's Modulation module's.)
 *
 * One component, two hosts. BitBit Artifact wraps this in its own Card; BitBit
 * Alpine drops it into its module row as the first module. The whole reason
 * this package exists is that the Artifact module in the multi-effect host is
 * not a re-draw of BitBit Artifact's face, it *is* that face - so a fix lands in
 * both and neither can drift.
 *
 * Amp's display is `AmpScope` (see `AmpDisplay`): a sine bent by the Amp's
 * own drive curve, then held by Bit - sample-rate reduction only, no amplitude
 * reduction, so the picture is a held wave with no amplitude bands, truthful
 * to what the engine actually does (see ee::fx::ArtifactModule's class note).
 *
 * `prefix` is the parameter-id prefix its controls bind through: "" for BitBit
 * Artifact, whose parameters are plain (`mix`, `ring.freq`), and "art." for BitBit
 * Alpine, whose are namespaced by module. Nothing below takes an id map; the
 * `ParamScope` does the whole job, and the leaf names are identical in both
 * plugins on purpose.
 *
 * `headerRight` is whatever the host wants in the module header's right-hand
 * slot - BitBit Alpine puts a Level trim there, the same one its other modules
 * carry; BitBit Artifact passes nothing and the slot stays empty. It is the
 * host's to supply because that Level is the host's chrome, not one of this
 * pedal's parameters - keeping it out here is what lets the face bind only to
 * names BitBit Artifact actually has.
 *
 * `easyTab` adds the Easy / Adv strip at the foot of the body - BitBit Alpine
 * turns it on, the standalone BitBit Artifact pedal does not, so that pedal's
 * face is untouched. `easyConfig` is the per-engine macro map it needs when
 * `easyTab` is on: `{ [engineName]: { name, targets } }`, handed in from the
 * host rather than kept on this package's engine table (which the standalone
 * pedal has no use for). See BitBit Alpine's engines.jsx `ARTIFACT_EASY`.
 *
 * `knobVariant` is Knob's own `variant`, forwarded to every knob this face
 * draws - BitBit Artifact's default is "concave"; BitBit Alpine passes "flat"
 * for the copy it embeds as its own module, so the same component can read
 * differently in each host without a second copy of it.
 */
export default function ArtifactFace({
  prefix = "",
  headerRight = null,
  easyTab = false,
  easyConfig = null,
  knobVariant = "concave",
}) {
  return (
    <ParamScope prefix={prefix}>
      <ArtifactFaceBody headerRight={headerRight} easyTab={easyTab} easyConfig={easyConfig} knobVariant={knobVariant} />
    </ParamScope>
  );
}

// BitBit Artifact's red. It reaches the power ring, the engine stepper and the
// knob value arcs through the one `accent` prop on ModulePanel.
const ACCENT = "#c00001";

// The display wells' ink, in this face's red. The wells' own background and
// grid come from the --pui-scope-* overrides on .af-display (ArtifactFace.css).
const SCOPE = {
  baseColor: "#e5504e", // the trace - bright enough to read on the dark well
  fillColor: "rgba(224, 72, 70, 0.16)", // wash under the trace
};

// The Easy / Adv strip is hidden for now and every face opens on Adv. The Easy
// path below is kept whole so flipping this back is the only change needed.
const SHOW_EASY_TABS = false;

/** Split out so its hooks resolve *inside* the ParamScope above - a hook in
    ArtifactFace itself would read the enclosing scope, not the one it declares. */
function ArtifactFaceBody({ headerRight = null, easyTab = false, easyConfig = null, knobVariant = "concave" }) {
  // Default index 0 (Ring) with no backend - the processor opens on Ring Mod too.
  const [engineIndex, setEngine] = useJuceChoiceValue("engine", ENGINES.length, 0);
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
      /* Mix and Tone are the module's, not the engine's, so they stay put when
         the engine changes. Tone is a bipolar tilt resting dead centre, and a
         size down from Mix - a trim on the effect rather than its main knob. */
      footer={
        <>
          <JuceKnob parameterId="mix" caption="Mix" variant={knobVariant} size={38} />
          <JuceKnob parameterId="tone" caption="Tone" variant={knobVariant} size={28} scaleFrom="centre" />
        </>
      }
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
           riding its Adv knobs (BitBit Alpine's ARTIFACT_EASY). The macro is
           resolved through the `art.` ParamScope this face already declares -
           no idPrefix needed. The well reads the same parameters the macro
           moves, so it answers to the Easy knob too. */
        <>
          <ArtifactEngineDisplay engine={engine.name} />
          <div className="af-easy">
            {/* Keyed by engine so the macro re-centres on an engine change
                rather than carrying its position across. */}
            <JuceMacroKnob key={engine.name} caption={easy.name} targets={easy.targets} variant={knobVariant} />
          </div>
        </>
      ) : engine.body === "crush" ? (
        <CrushBody knobVariant={knobVariant} />
      ) : engine.body === "rust" ? (
        <RustBody knobVariant={knobVariant} />
      ) : engine.body === "amp" ? (
        <AmpBody knobVariant={knobVariant} />
      ) : (
        <RingBody knobVariant={knobVariant} />
      )}

      {SHOW_EASY_TABS && easy && <ModuleTabs value={tab} onChange={setTab} />}
    </ModulePanel>
  );
}

/* Each engine's display well, split out so its own slider hooks run only for
   the engine that is actually showing one - and so the Easy view can render
   the same well above its macro knob without also mounting that engine's body.
   The same reason BitBit Alpine splits its displays out. */
function CrushDisplay() {
  const [bits] = useJuceSliderValue("crush.bits");
  const [rate] = useJuceSliderValue("crush.rate");
  const [jitter] = useJuceSliderValue("crush.jitter");

  return (
    // A picture of the three destructive knobs: the reference sine held in time
    // by Rate, quantised by Bits, and knocked out of step by Jitter. The post
    // low-pass shapes what comes after, so it is not in the trace.
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
  const [drive] = useJuceSliderValue("amp.drive");
  const [bit] = useJuceSliderValue("amp.bit");

  return (
    // A sine through the Amp's own drive curve - bent the way TubeDrive bends
    // it, softly and unevenly as Drive comes up - then held by Bit's
    // sample-and-hold. The clean sine sits faintly behind for comparison.
    // Mids and Tone are equalisers after it and aren't shown.
    <div className="af-display af-display--crush">
      <AmpScope
        drive01={drive}
        bit01={bit}
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
  if (engine === "Rust") return <RustDisplay />;
  if (engine === "Amp") return <AmpDisplay />;
  return <RingDisplay />;
}

/* The Bit Crush body: its display well and two knob rows. */
function CrushBody({ knobVariant }) {
  return (
    // Matches the other engine bodies' height so stepping between engines
    // doesn't resize the module.
    <div className="af-crush">
      <CrushDisplay />

      <div className="af-knobs">
        <div className="af-knob-row">
          <JuceKnob parameterId="crush.bits" caption="Bits" variant={knobVariant} size={36} />
          <JuceKnob parameterId="crush.rate" caption="Rate" variant={knobVariant} size={36} />
        </div>
        <div className="af-knob-row">
          <JuceKnob parameterId="crush.lp" caption="Filter" variant={knobVariant} size={36} />
          <JuceKnob parameterId="crush.jitter" caption="Jitter" variant={knobVariant} size={36} />
        </div>
      </div>
    </div>
  );
}

/* The Ring Mod body: its lattice display, four knobs and the mode switch. */
function RingBody({ knobVariant }) {
  return (
    // Matches the other engine bodies' height so stepping between engines
    // doesn't resize the module.
    <div className="af-ring">
      <RingDisplay />

      <div className="af-knobs">
        <div className="af-knob-row">
          <JuceKnob parameterId="ring.freq" caption="Freq" variant={knobVariant} size={36} />
          <JuceKnob parameterId="ring.tweak" caption="Tweak" variant={knobVariant} size={36} />
        </div>
        <div className="af-knob-row">
          <JuceKnob parameterId="ring.lp" caption="HiCut" variant={knobVariant} size={36} />
          {/* Bipolar: a plain sine carrier dead centre, so its arc grows out
              from twelve o'clock in whichever direction it is folded. */}
          <JuceKnob
            parameterId="ring.rect"
            caption="Rectify"
            variant={knobVariant}
            size={36}
            scaleFrom="centre"
          />
        </div>
      </div>

      <RingModeSwitch />
    </div>
  );
}

/* Pinned to the foot of the module body. Wobble is the
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

/* The Rust body: its corrosion display, one knob (Grind) and the mode switch.
   Tone is the module's, in the footer. Wear and its recovery are fixed inside the engine, so there is
   no knob for them. */
function RustBody({ knobVariant }) {
  return (
    // Matches the other engine bodies' height so stepping between engines
    // doesn't resize the module.
    <div className="af-rust">
      <RustDisplay />

      <div className="af-knobs">
        <div className="af-knob-row">
          <JuceKnob parameterId="rust.grind" caption="Grind" variant={knobVariant} size={36} />
        </div>
      </div>

      <RustModeSwitch />
    </div>
  );
}

/* The Amp body: its display well, two knob rows - Drive / Mids, then Bit /
   Tone, the order the signal actually runs through (drive, then the
   sample-and-hold, then the Mids lift, then Tone). */
function AmpBody({ knobVariant }) {
  return (
    // Matches the other engine bodies' height so stepping between engines
    // doesn't resize the module.
    <div className="af-amp">
      <AmpDisplay />

      <div className="af-knobs">
        <div className="af-knob-row">
          <JuceKnob parameterId="amp.drive" caption="Drive" variant={knobVariant} size={36} />
          <JuceKnob parameterId="amp.mids" caption="Mids" variant={knobVariant} size={36} />
        </div>
        <div className="af-knob-row">
          <JuceKnob parameterId="amp.bit" caption="Bit" variant={knobVariant} size={36} />
          {/* Bipolar: flat dead centre, so its arc grows out from twelve
              o'clock the way Tape's Tone does. */}
          <JuceKnob parameterId="amp.tone" caption="Tone" variant={knobVariant} size={36} scaleFrom="centre" />
        </div>
      </div>
    </div>
  );
}

/* Pinned to the foot of the module body. Oxide is the soft
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
