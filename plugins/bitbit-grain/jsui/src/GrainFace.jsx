import { useState } from "react";
import {
  Logo,
  JucePresetBar,
  PowerToggle,
  Readout,
  VerticalTabs,
  Pill,
  EngineStepper,
  ReverbScope,
  usePedalTheme,
} from "@synthpeak/pedal-ui";
import {
  JuceKnob,
  JuceFader,
  JucePill,
  JuceChoicePill,
  useJuceToggleValue,
  useJuceSliderValue,
  useJuceChoiceValue,
  useParamId,
  useFormattedText,
} from "@synthpeak/pedal-ui/juce";
import { GrainEnvelope, PitchWeights, RandomField, FilterCurve } from "./Displays.jsx";
import ModTab from "./ModTab.jsx";
import MixerMeter from "./MixerMeter.jsx";
import ModdableKnob from "./ModdableKnob.jsx";
import Cosmos from "./Cosmos.jsx";
import "./GrainFace.css";

const FACE_TABS = [
  { id: "effects", label: "Effects" },
  { id: "mod", label: "Mod" },
];

// The five section accents, literal rather than `var(--pui-accent-*)`: these
// reach an SVG `stroke`/`fill` presentation attribute in Displays.jsx, and
// WebKit (the real plugin's WKWebView) doesn't reliably resolve a custom
// property written there - see the same note in Knob.jsx's TickScale. The
// three non-Alpine ones are also in packages/pedal-ui/src/tokens.css now
// (--pui-accent-grain/-pitch/-random) for the text/pill uses below, which
// *are* plain CSS properties and read the token fine.
//
// Two sets, because this face is drawn on either ground now. A hue picked to
// glow on near-black is a pastel smear on cream - the "filter curve you can't
// see" was `mixer` at 1.4:1 against --pui-panel. Same hues, darkened until
// each one reads; a literal per theme rather than a token for the reason
// above, so the palette is picked here and handed down.
const PALETTES = {
  onyx: {
    grain: "#b39bd8",
    pitch: "#e78fb3",
    // Was an orange (#dfa878); now the purple that Grain, Pitch and Random's
    // displays, knob arcs and Random's title all share. Still the "random"
    // token in packages/pedal-ui/src/tokens.css for anything else that reads it.
    random: "#6a509c",
    // Delay shares Reverb's own blue rather than its old green - one colour,
    // not two independently-declared literals that could drift apart.
    reverb: "#7fd2d8",
    mixer: "#c9cede",
  },
  light: {
    grain: "#6b4fa3",
    pitch: "#a84a70",
    random: "#553f80",
    reverb: "#2b7f87",
    mixer: "#5f6575",
  },
};

/** This face's accents for the palette in force. `KNOB_LIT` is the value arc
    on Grain/Pitch/Random - the shared purple rather than each section's own
    accent; Delay/Reverb light theirs from the footer tokens instead. */
function usePalette() {
  const theme = usePedalTheme();
  const p = PALETTES[theme] ?? PALETTES.light;

  return { GRAIN: p.grain, PITCH: p.pitch, RANDOM: p.random, REVERB: p.reverb, DELAY: p.reverb, MIXER: p.mixer, KNOB_LIT: p.random };
}

// BitBit Wah's own red (--pui-knob-sweep-lit in tokens.css) - Drive borrows it
// rather than Mixer's own pale accent, the way a drive/overdrive control
// reads on hardware.
const DRIVE_LIT = "#c60000";

/** A section's power toggle, bound to its own on/off parameter - Delay and
    Reverb only (Grain/Pitch/Random carry no toggle at all, per the handoff).
    Returns both the control and the current state, so the caller can also
    set `data-off` on the section wrapper for the dimming rule in
    GrainFace.css. */
function useSectionPower(parameterId) {
  const [on, setOn] = useJuceToggleValue(parameterId, true);
  return [on, <PowerToggle key="power" on={on} onToggle={setOn} ariaLabel="Power" />];
}

/** One Left/Right time knob + its readout, bare (no caption - the readout
    carries the value) the way `delay-face`'s TimeControl does. `side` is the
    printed "L"/"R"; `parameterId` is `ltime`/`rtime`. The readout text comes
    from BitBitGrainWebEditor's own formatKnobValue special-case, which already
    knows the Sync pill and the host tempo. */
function TimeRow({ side, parameterId }) {
  const id = useParamId(parameterId);
  const [value] = useJuceSliderValue(parameterId);
  const text = useFormattedText(id, value);

  return (
    <div className="pg-time-row">
      <JuceKnob parameterId={parameterId} variant="flat" size={32} bare showValueLabel={false} />
      <Readout label={side} value={text} />
    </div>
  );
}

// In the parameter's own index order - see PluginProcessor.cpp's "shapefamily"
// AudioParameterChoice and ee::dsp::Grainer::ShapeFamily, which this has to
// keep in step with by hand the same way JuceChoicePill's callers already do
// for "dtype" and "scale" elsewhere in this file. Abbreviated where the host's
// own name does not fit the stepper - the index is what is stored, not this.
const SHAPE_FAMILY_LABELS = ["Trian", "Gaus", "Sinc", "Spike"];

/** Which of the four windows the Shape knob morphs - the stepper under the
    knob itself, the same "id" a real hardware granulator's shape selector
    would carry. The same EngineStepper BitBit Alpine's Filter section steps
    its wave with: four is few enough to click through, and a well with an
    arrow either side sits on this face where a menu popping over it did not.
    `index`/`select` come from GrainSection's own useJuceChoiceValue. */
function ShapeFamilyStepper({ index, select }) {
  return (
    <div className="pg-grain__shapefamily">
      <EngineStepper
        engines={SHAPE_FAMILY_LABELS}
        value={SHAPE_FAMILY_LABELS[index] ?? SHAPE_FAMILY_LABELS[0]}
        label="Shape family"
        onChange={(next) => select(SHAPE_FAMILY_LABELS.indexOf(next))}
      />
    </div>
  );
}

function GrainSection() {
  const { GRAIN, RANDOM, KNOB_LIT } = usePalette();
  // Read once here rather than separately in the display and the stepper -
  // both draw off the same "shapefamily" index, and a single subscription is
  // what guarantees the envelope and the stepper that sets it can never
  // disagree for even one render.
  const [family, setFamily] = useJuceChoiceValue("shapefamily", SHAPE_FAMILY_LABELS.length);

  return (
    <section className="pg-section pg-section--grain" style={{ "--pui-accent": GRAIN, "--pui-soft-lit": KNOB_LIT }}>
      <div className="pg-section__head">
        <span className="pg-section__name">Grain</span>
        <span className="pg-section__spacer" />
        <div className="pg-section__head-right">
          <SegmentSwitch parameterId="freeze" offLabel="Live" onLabel="Freeze" />
        </div>
      </div>
      <div className="pg-section__display">
        <GrainEnvelope accent={RANDOM} family={family} />
      </div>
      <div className="pg-section__knobs">
        <ModdableKnob parameterId="density" caption="Destiny" variant="flat" size={30} />
        <ModdableKnob parameterId="size" caption="Size" variant="flat" size={30} />
        {/* Not a ModdableKnob: Wide is set once per block, not per modulation
            chunk, so there is no LFO route for it to accept. */}
        <JuceKnob parameterId="width" caption="Wide" variant="flat" size={30} />
      </div>
      <div className="pg-section__knobs">
        <ModdableKnob parameterId="feedback" caption="Fback" variant="flat" size={30} />
        {/* Shape and its Family stepper, stacked - the stepper sits right
            under the knob it belongs to rather than in its own row, so the
            two read as one control (knob = how far into the window, stepper
            = which window) the way a hardware granulator's shape section
            would group them. */}
        <div className="pg-grain__shape-stack">
          {/* invert: the knob's position is mirrored - turned to what reads as
              "max" now sets Shape to its actual minimum, and vice versa. The
              parameter itself, its readout text and its presets are untouched;
              see JuceKnob's own note on the prop. */}
          <ModdableKnob parameterId="shape" caption="Shape" variant="flat" size={30} invert />
          <ShapeFamilyStepper index={family} select={setFamily} />
        </div>
      </div>
    </section>
  );
}

function PitchSection() {
  const { PITCH, RANDOM, KNOB_LIT } = usePalette();
  const [scaleOn, scalePower] = useSectionPower("scaleon");

  return (
    <section className="pg-section pg-section--pitch" style={{ "--pui-accent": PITCH, "--pui-soft-lit": KNOB_LIT }}>
      <div className="pg-section__head">
        <span className="pg-section__name">Pitch</span>
        <span className="pg-section__spacer" />
      </div>
      <div className="pg-section__display">
        <PitchWeights accent={RANDOM} />
      </div>
      <div className="pg-section__knobs">
        <ModdableKnob parameterId="plow" caption="Low" variant="flat" size={30} />
        <ModdableKnob parameterId="puni" caption="Unison" variant="flat" size={30} />
        <ModdableKnob parameterId="phigh" caption="High" variant="flat" size={30} />
      </div>
      <div className="pg-pitch__divider" />
      <div className="pg-pitch__scale-head" data-off={!scaleOn || undefined}>
        {scalePower}
        <span className="pg-pitch__scale-name">Scale</span>
      </div>
      <div className="pg-pitch__scale" data-off={!scaleOn || undefined}>
        <div className="pg-pitch__foot">
          <JuceChoicePill
            parameterId="root"
            labels={["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]}
            className="pg-pitch__root-pill"
          />
          <JuceChoicePill
            parameterId="scale"
            labels={["Major", "Minor", "Penta Maj", "Penta Min", "Chromatic"]}
            className="pg-pitch__scale-pill"
          />
        </div>
        <ModdableKnob parameterId="pmix" caption="Mix" variant="flat" size={30} />
      </div>
    </section>
  );
}

function RandomSection() {
  const { RANDOM, KNOB_LIT } = usePalette();
  return (
    <section className="pg-section pg-section--random" style={{ "--pui-accent": RANDOM, "--pui-soft-lit": KNOB_LIT }}>
      <div className="pg-section__head">
        <span className="pg-section__name">Random</span>
        <span className="pg-section__spacer" />
      </div>
      <div className="pg-section__display">
        <RandomField accent={RANDOM} />
      </div>
      <div className="pg-section__knobs pg-random__lead-row">
        <ModdableKnob parameterId="stereo" caption="Spray" variant="flat" size={38} />
        <ModdableKnob parameterId="reverse" caption="Reverse" variant="flat" size={30} />
      </div>
      <div className="pg-section__knobs">
        <ModdableKnob parameterId="scatter" caption="Scatter" variant="flat" size={30} />
        <ModdableKnob parameterId="mod" caption="Mod" variant="flat" size={30} />
      </div>
    </section>
  );
}

/** The mixer column: the dry path and the cloud as two independent levels
    (what used to be Grain's single Mix knob), a link that locks them
    together, and the Filter knob over the grain cloud's own filter. */
function MixerSection() {
  const { MIXER } = usePalette();
  return (
    <section className="pg-section pg-section--mixer" style={{ "--pui-accent": MIXER, "--pui-soft-lit": MIXER }}>
      <div className="pg-section__head">
        <span className="pg-section__name">Mixer</span>
        <span className="pg-section__spacer" />
      </div>
      <div className="pg-mixer__faders">
        {/* Each fader carries its own dB ladder on its inner side (no live
            level bar riding along it any more - that used to overlap its own
            tick numbers with the value readout below the thumb), so the two
            ladders face each other across the link button. `height` matches
            the fader's `length`, and the strip aligns them on their shared
            bottom edge - the fader's label sits above its track, so bottom is
            the one edge that lines up without measuring anything. */}
        <div className="pg-mixer__strip">
          <JuceFader parameterId="dry" label="Dry" orientation="vertical" length={110} resetTo={75} thumbSize={18} />
          <MixerMeter side="right" height={110} />
        </div>
        <div className="pg-mixer__link">
          <JucePill parameterId="mlink" icon={<LinkGlyph />} />
        </div>
        {/* 75, not 100: the parameter's own range is percent-of-travel, not
            dB, and 75% is where PluginProcessor.cpp's levelGainFor() (built
            from GrainerConfig.h's kLevelUnityPct) lands on exactly 0 dB -
            same reference Dry's own resetTo uses just above. It was 65,
            which reads as -3 dB on double-click; unity matches Dry's own
            reset and reads as "off", the neutral double-click has everywhere
            else. */}
        <div className="pg-mixer__strip">
          <MixerMeter side="left" height={110} />
          <JuceFader parameterId="grains" label="Grains" orientation="vertical" length={110} resetTo={75} thumbSize={18} />
        </div>
      </div>
      {/* Tube Drive on the grain cloud alone, same engine and default as BitBit
          Artifact's amp.drive - see PluginProcessor.cpp's driveStage. Bit
          (moved off Grain's own row) sits right beside it, sharing Drive's
          own red lit arc (DRIVE_LIT) now rather than KNOB_LIT - the two read
          as one pair of grain-cloud "character" controls. */}
      <div className="pg-mixer__drive">
        <div style={{ "--pui-soft-lit": DRIVE_LIT }}>
          <ModdableKnob parameterId="drive" caption="Drive" variant="flat" size={36} />
        </div>
        <div style={{ "--pui-soft-lit": DRIVE_LIT }}>
          <ModdableKnob parameterId="bit" caption="Bit" variant="flat" size={36} />
        </div>
      </div>
      <div className="pg-mixer__filter">
        <FilterCurve accent={MIXER} />
        <div className="pg-mixer__filter-knobs">
          {/* No value on the knob at all: the scope above it already shows
              what the filter is doing, so swapping the caption for a
              percentage mid-drag would only say the same thing worse. Its
              badge uses the shared default anchor (.pui-knob__badge,
              Knob.css) - Reso sits flush to its right now, so Filter is no
              longer the row's own edge control. */}
          <ModdableKnob parameterId="filter" caption="Filter" variant="flat" size={36} scaleFrom="max" showValueLabel={false} />
          {/* 0 is silent: crossfades the same cutoff toward the resonant
              ladder (Grainer::setCloudResonance) only once turned up.
              badgeStyle: this knob sits at the plate's own right edge now,
              so the shared top-right badge anchor needs pushing further
              right than any other knob's - see Knob.jsx's own note on the
              prop. */}
          <ModdableKnob
            parameterId="reso"
            caption="Reso"
            variant="flat"
            size={36}
            badgeStyle={{ right: "-24px" }}
          />
        </div>
      </div>
    </section>
  );
}

function DelaySection() {
  const { DELAY } = usePalette();
  const [on, powerToggle] = useSectionPower("delon");

  return (
    <section className="pg-section pg-section--delay" style={{ "--pui-accent": DELAY }} data-off={!on || undefined}>
      <div className="pg-section__head">
        {powerToggle}
        <span className="pg-section__name">Delay</span>
        <span className="pg-section__spacer" />
        <div className="pg-section__head-right">
          <JucePill parameterId="dtsync" label="SYNC" />
          <JuceChoicePill parameterId="dtype" labels={["Normal", "Wide", "Ping Pong"]} />
        </div>
      </div>
      <div className="pg-delay__body">
        <div className="pg-delay__leads">
          {/* The footer knob every module wears across the bottom of BitBit
              Alpine, and BitBit Delay's own stage knobs - one control for the
              whole family. Sized to the 42px the face's other large knobs
              (Drive, Bit, Filter, Reso) render at. */}
          <JuceKnob parameterId="dmix" caption="Mix" variant="flat" size={42} />
          <JuceKnob parameterId="dfb" caption="Feedback" variant="flat" size={42} />
        </div>
        <div className="pg-delay__times">
          <TimeRow side="L" parameterId="ltime" />
          <TimeRow side="R" parameterId="rtime" />
        </div>
        <div className="pg-delay__link">
          <JucePill parameterId="dlink" icon={<LinkGlyph />} />
        </div>
      </div>
    </section>
  );
}

// LinkIcon isn't re-exported from packages/pedal-ui/src/index.js (only
// DelayFace's own package pulls it in directly); a tiny local copy is
// cheaper than adding an export for one glyph. Geometry matches
// packages/pedal-ui/src/LinkIcon.jsx exactly.
function LinkGlyph({ size = 14 }) {
  return (
    <svg width={size} height={size} viewBox="0 0 20 20" fill="none">
      <g stroke="currentColor" strokeWidth="1.9" strokeLinejoin="round" transform="rotate(45 10 10)">
        <rect x="6.6" y="2.4" width="6.8" height="10.4" rx="3.4" />
        <rect x="6.6" y="7.2" width="6.8" height="10.4" rx="3.4" />
      </g>
    </svg>
  );
}

function ReverbSection() {
  const { REVERB } = usePalette();
  const [on, powerToggle] = useSectionPower("revon");
  const [decay01] = useJuceSliderValue("decay");
  const [locut01] = useJuceSliderValue("rlocut");

  return (
    <section className="pg-section pg-section--reverb" style={{ "--pui-accent": REVERB }} data-off={!on || undefined}>
      <div className="pg-section__head">
        {powerToggle}
        <span className="pg-section__name">Reverb</span>
        <span className="pg-section__spacer" />
      </div>
      <div className="pg-section__display">
        {/* BitBit Alpine's own reverb display, not a second drawing of the
            same idea - this used to be eight decaying bars. `shimmer` is the
            model for an FdnReverb, which is what this section runs; Hi Cut
            and Damping are knobs it doesn't expose, so they stay at rest. */}
        <ReverbScope engine="shimmer" decay01={decay01} locut01={locut01} hicut01={1} damping01={0} octave={1} />
      </div>
      <div className="pg-section__knobs">
        {/* The footer knob every module wears across the bottom of BitBit
            Alpine - same control, same face and same arc (the --pui-footer-*
            tokens), so Grain's effects row reads as one of those strips. */}
        <JuceKnob parameterId="rmix" caption="Mix" variant="flat" size={32} />
        <JuceKnob parameterId="decay" caption="Decay" variant="flat" size={32} />
        <JuceKnob parameterId="rlocut" caption="Low Cut" variant="flat" size={32} />
      </div>
    </section>
  );
}

/** A two-state toggle, one pill whose own label tracks the current state -
    "Live" unlit, click it and it reads "Freeze" lit, click again and it's
    back to "Live". Built on the shared Pill (Sync's and Normal/Wide/Ping
    Pong's own component on Delay's row) rather than a hand-rolled button, so
    it is the same size and look as those pills without a second copy of
    their CSS to keep in step - was a bespoke `.pg-segment__btn`. */
function SegmentSwitch({ parameterId, offLabel, onLabel, className }) {
  const [on, setOn] = useJuceToggleValue(parameterId);

  return (
    <Pill
      label={on ? onLabel : offLabel}
      labelSet={[offLabel, onLabel]}
      pressed={on}
      onClick={() => setOn(!on)}
      className={className}
    />
  );
}

/** `on`/`onToggle` are handed down rather than read here, the way Alpine's
    HostControls takes them from App.jsx: the plate below reads the same "on"
    parameter to dim itself, and two independent useJuceToggleValue calls would
    hold two separate copies of it - fine once a relay echoes a host's own
    change back, but outside a real host (the gallery, a stale render) there is
    no echo, so the ring could read on over an already-dimmed plate. */
function Header({ on, onToggle }) {
  return (
    <div className="pg-header">
      <div className="pg-header__row">
        <div className="pg-header__brand">
          <Logo size={32} />
          <h1 className="pg-header__title">BitBit Grains</h1>
        </div>
        <div className="pg-header__presets">
          <JucePresetBar variant="separated" showThemeSwitch />
        </div>
        <div className="pg-header__right">
          <div className="pg-header__level">
            <JuceFader parameterId="level" label="LEVEL" resetTo={0} length={88} />
          </div>
          <PowerToggle on={on} onToggle={onToggle} ariaLabel="Bypass" />
        </div>
      </div>
    </div>
  );
}

export default function GrainFace() {
  const [tab, setTab] = useState("effects");
  const [on, setOn] = useJuceToggleValue("on", true);

  return (
    <>
      <Header on={on} onToggle={setOn} />
      {/* Bypassed dims the plate as one object, the way Alpine's module row
          does (pa-modules--bypassed) and ModulePanel does for a single module -
          the sections keep saying what they individually are (including their
          own on/off dimming), the plate says none of it is running. The plate's
          own border/shadow stays lit, same reasoning as ModulePanel.css: an
          opacity on the whole thing would fade the enclosure along with the
          controls, so only the plate's *children* dim (see GrainFace.css). */}
      <div className="pg-plate" data-off={!on || undefined}>
        <div className="pg-plate__main">
          <div className="pg-row">
            <GrainSection />
            <div className="pg-vdivider" />
            <PitchSection />
            <div className="pg-vdivider" />
            <RandomSection />
          </div>
          <div className="pg-hdivider" />
          <div className="pg-row2" data-active-tab={tab}>
            <VerticalTabs tabs={FACE_TABS} value={tab} onChange={setTab} />
            {/* Both tab bodies stay mounted, toggled with plain CSS rather
                than conditional JSX - Mod's whole point is a breakpoint shape
                you build up by hand, and unmounting LfoEditor on every tab
                switch was throwing that state away and resetting it to the
                default preset each time you came back. */}
            <div className="pg-row2__content" hidden={tab !== "effects"}>
              <div className="pg-row">
                <DelaySection />
                <div className="pg-vdivider" style={{ gridColumn: 4 }} />
                <ReverbSection />
              </div>
            </div>
            <div className="pg-row2__content" hidden={tab !== "mod"}>
              <ModTab />
            </div>
          </div>
        </div>
        <div className="pg-vdivider" />
        <Cosmos />
        <div className="pg-vdivider" />
        <MixerSection />
      </div>
    </>
  );
}
