import { useState } from "react";
import { Logo, JucePresetBar, PowerToggle, Readout, VerticalTabs } from "@synthpeak/pedal-ui";
import {
  JuceKnob,
  JuceFader,
  JucePill,
  JuceChoicePill,
  useJuceToggleValue,
  useJuceSliderValue,
  useParamId,
  useFormattedText,
} from "@synthpeak/pedal-ui/juce";
import { GrainEnvelope, PitchWeights, RandomField, ReverbTail, FilterCurve } from "./Displays.jsx";
import ModTab from "./ModTab.jsx";
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
const GRAIN = "#b39bd8";
const PITCH = "#e78fb3";
// Was an orange (#dfa878); now the purple that Grain, Pitch and Random's displays,
// knob arcs and Random's title all share. Still the "random" token in
// packages/pedal-ui/src/tokens.css for anything else that reads it.
const RANDOM = "#6a509c";
// Delay now shares Reverb's own blue rather than its old green - one colour,
// not two independently-declared literals that could drift apart.
const REVERB = "#7fd2d8";
const DELAY = REVERB;
const MIXER = "#c9cede";

// The knob value arc (--pui-soft-lit) on Grain/Pitch/Random is unified to
// this instead of each section's own accent - the same purple the three
// displays above them now share. Delay/Reverb keep their own accent lit.
const KNOB_LIT = RANDOM;

// BitBit Wah's own red (--pui-knob-sweep-lit in tokens.css) - Drive borrows it
// rather than Mixer's own pale MIXER lit, the way a drive/overdrive control
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
      <JuceKnob parameterId={parameterId} variant="concave" size={32} bare showValueLabel={false} />
      <Readout label={side} value={text} />
    </div>
  );
}

function GrainSection() {
  return (
    <section className="pg-section pg-section--grain" style={{ "--pui-accent": GRAIN, "--pui-soft-lit": KNOB_LIT }}>
      <div className="pg-section__head">
        <span className="pg-section__name">Grain</span>
        <span className="pg-section__spacer" />
      </div>
      <div className="pg-section__display">
        <GrainEnvelope accent={RANDOM} />
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
        <ModdableKnob parameterId="shape" caption="Shape" variant="flat" size={30} />
      </div>
    </section>
  );
}

function PitchSection() {
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
  return (
    <section className="pg-section pg-section--mixer" style={{ "--pui-accent": MIXER, "--pui-soft-lit": MIXER }}>
      <div className="pg-section__head">
        <span className="pg-section__name">Mixer</span>
        <span className="pg-section__spacer" />
      </div>
      <div className="pg-mixer__faders">
        <JuceFader parameterId="dry" label="Dry" orientation="vertical" length={110} resetTo={75} thumbSize={18} />
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
        <JuceFader parameterId="grains" label="Grains" orientation="vertical" length={110} resetTo={75} thumbSize={18} />
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
        {/* No value on the knob at all: the scope above it already shows what
            the filter is doing, so swapping the caption for a percentage
            mid-drag would only say the same thing worse. */}
        {/* badgeStyle: this knob sits at the plate's own right edge, so the
            shared top-right badge anchor (.pui-knob__badge, Knob.css) needs
            pushing further right than any other knob's - see Knob.jsx's own
            note on the prop. */}
        <ModdableKnob
          parameterId="filter"
          caption="Filter"
          variant="scale"
          size={52}
          scaleFrom="max"
          showValueLabel={false}
          badgeStyle={{ right: "-24px" }}
        />
      </div>
    </section>
  );
}

function DelaySection() {
  const [on, powerToggle] = useSectionPower("delon");

  return (
    <section className="pg-section pg-section--delay" style={{ "--pui-accent": DELAY, "--pui-soft-lit": DELAY }} data-off={!on || undefined}>
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
          {/* Same look as BitBit Delay's footer knobs (StageControl): concave,
              value swapped in for the caption only while dragging. 30% up
              from that footer's own 38px - these are Delay's own lead
              knobs, not footer-sized ones. */}
          <JuceKnob parameterId="dmix" caption="Mix" variant="concave" size={49} />
          <JuceKnob parameterId="dfb" caption="Feedback" variant="concave" size={49} />
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
  const [on, powerToggle] = useSectionPower("revon");

  return (
    <section className="pg-section pg-section--reverb" style={{ "--pui-accent": REVERB, "--pui-soft-lit": REVERB }} data-off={!on || undefined}>
      <div className="pg-section__head">
        {powerToggle}
        <span className="pg-section__name">Reverb</span>
        <span className="pg-section__spacer" />
      </div>
      <div className="pg-section__display">
        <ReverbTail accent={REVERB} />
      </div>
      <div className="pg-section__knobs">
        {/* Same concave look as Delay's own Mix/Feedback, sized down from
            38px to fit Reverb's own row of three - 32px, 5% up from an
            initial 30. */}
        <JuceKnob parameterId="rmix" caption="Mix" variant="concave" size={32} />
        <JuceKnob parameterId="decay" caption="Decay" variant="concave" size={32} />
        <JuceKnob parameterId="rlocut" caption="Low Cut" variant="concave" size={32} />
      </div>
    </section>
  );
}

/** A two-state toggle, one button whose own label and colour both track the
    current state - "Live" dark, click it and it reads "Freeze" light, click
    again and it's back to "Live" dark. Was a joined two-button segment (one
    button per state, the current one lit); this is the same on/off styling
    (`.pg-segment__btn[data-on]`, unchanged) collapsed onto a single control
    now that only one label needs to be visible at a time. */
function SegmentSwitch({ parameterId, offLabel, onLabel, className }) {
  const [on, setOn] = useJuceToggleValue(parameterId);

  return (
    <button
      type="button"
      className={className ? `pg-segment__btn ${className}` : "pg-segment__btn"}
      data-on={on || undefined}
      onClick={() => setOn(!on)}
    >
      {on ? onLabel : offLabel}
    </button>
  );
}

function Header() {
  const [on, setOn] = useJuceToggleValue("on", true);

  return (
    <div className="pg-header">
      <div className="pg-header__row">
        <div className="pg-header__brand">
          <Logo size={32} />
          <h1 className="pg-header__title">BitBit Grains</h1>
        </div>
        <div className="pg-header__live">
          <SegmentSwitch parameterId="freeze" offLabel="Live" onLabel="Freeze" />
        </div>
        <div className="pg-header__presets">
          <JucePresetBar variant="separated" />
        </div>
        <div className="pg-header__right">
          <div className="pg-header__level">
            <JuceFader parameterId="level" label="LEVEL" resetTo={0} length={88} />
          </div>
          <PowerToggle on={on} onToggle={setOn} ariaLabel="Bypass" />
        </div>
      </div>
    </div>
  );
}

export default function GrainFace() {
  const [tab, setTab] = useState("effects");

  return (
    <>
      <Header />
      <div className="pg-plate">
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
