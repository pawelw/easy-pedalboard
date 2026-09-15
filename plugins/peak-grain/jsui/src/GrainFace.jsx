import { Logo, JucePresetBar, PowerToggle, Pill, Readout } from "@synthpeak/pedal-ui";
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
import { GrainEnvelope, PitchWeights, RandomField, ReverbTail, GrainScope } from "./Displays.jsx";
import "./GrainFace.css";

// The five section accents, literal rather than `var(--pui-accent-*)`: these
// reach an SVG `stroke`/`fill` presentation attribute in Displays.jsx, and
// WebKit (the real plugin's WKWebView) doesn't reliably resolve a custom
// property written there - see the same note in Knob.jsx's TickScale. The
// three non-Alpine ones are also in packages/pedal-ui/src/tokens.css now
// (--pui-accent-grain/-pitch/-random) for the text/pill uses below, which
// *are* plain CSS properties and read the token fine.
const GRAIN = "#b39bd8";
const PITCH = "#e78fb3";
const RANDOM = "#dfa878";
const DELAY = "#a3ce7a";
const REVERB = "#7fd2d8";

/** The Grain card's single footer switch, now a section-header pill: writes
    both `ssync` and `dsync` on click rather than relying on a native click
    hook, so a plain RelaySet-bound WebView still gets the "drives Size and
    Destiny together" behaviour - PluginProcessor's parameterChanged then
    remaps each knob independently off its own flag (see PluginProcessor.h's
    note on onSizeSyncToggled/onDensitySyncToggled). */
function GrainSyncPill() {
  const [sizeSync, setSizeSync] = useJuceToggleValue("ssync");
  const [, setDensitySync] = useJuceToggleValue("dsync");

  const toggle = () => {
    const next = !sizeSync;
    setSizeSync(next);
    setDensitySync(next);
  };

  return <Pill label="SYNC" pressed={sizeSync} onClick={toggle} />;
}

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
    from PeakGrainWebEditor's own formatKnobValue special-case, which already
    knows the Sync pill and the host tempo. */
function TimeRow({ side, parameterId }) {
  const id = useParamId(parameterId);
  const [value] = useJuceSliderValue(parameterId);
  const text = useFormattedText(id, value);

  return (
    <div className="pg-time-row">
      <JuceKnob parameterId={parameterId} variant="flat" size={44} bare showValueLabel={false} />
      <Readout label={side} value={text} />
    </div>
  );
}

function GrainSection() {
  return (
    <section className="pg-section pg-section--grain" style={{ "--pui-accent": GRAIN }}>
      <div className="pg-section__head">
        <span className="pg-section__name">Grain</span>
        <span className="pg-section__spacer" />
        <div className="pg-section__head-right">
          <GrainSyncPill />
        </div>
      </div>
      <div className="pg-section__display">
        <GrainEnvelope accent={GRAIN} />
      </div>
      <div className="pg-section__knobs">
        <JuceKnob parameterId="mix" caption="Mix" variant="flat" size={44} />
        <JuceKnob parameterId="size" caption="Size" variant="flat" size={44} />
      </div>
      <div className="pg-section__knobs">
        <JuceKnob parameterId="density" caption="Destiny" variant="flat" size={44} />
        <JuceKnob parameterId="shape" caption="Shape" variant="flat" size={44} />
      </div>
    </section>
  );
}

function PitchSection() {
  return (
    <section className="pg-section pg-section--pitch" style={{ "--pui-accent": PITCH }}>
      <div className="pg-section__head">
        <span className="pg-section__name">Pitch</span>
        <span className="pg-section__spacer" />
      </div>
      <div className="pg-section__display">
        <PitchWeights accent={PITCH} />
      </div>
      <div className="pg-section__knobs">
        <JuceKnob parameterId="plow" caption="Low" variant="flat" size={44} />
        <JuceKnob parameterId="puni" caption="Unison" variant="flat" size={44} />
      </div>
      <div className="pg-section__knobs">
        <JuceKnob parameterId="phigh" caption="High" variant="flat" size={44} />
        <JuceKnob parameterId="detune" caption="Detune" variant="flat" size={44} scaleFrom="centre" />
      </div>
    </section>
  );
}

function RandomSection() {
  return (
    <section className="pg-section pg-section--random" style={{ "--pui-accent": RANDOM }}>
      <div className="pg-section__head">
        <span className="pg-section__name">Random</span>
        <span className="pg-section__spacer" />
      </div>
      <div className="pg-section__display">
        <RandomField accent={RANDOM} />
      </div>
      <div className="pg-section__knobs">
        <JuceKnob parameterId="stereo" caption="Stereo" variant="flat" size={44} />
        <JuceKnob parameterId="reverse" caption="Reverse" variant="flat" size={44} />
      </div>
      <div className="pg-section__knobs">
        <JuceKnob parameterId="scatter" caption="Scatter" variant="flat" size={44} />
      </div>
    </section>
  );
}

function DelaySection() {
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
          <JuceKnob parameterId="dmix" caption="Mix" variant="scale" size={52} showValueBelow />
          <JuceKnob parameterId="dfb" caption="Feedback" variant="scale" size={52} showValueBelow />
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
    <section className="pg-section pg-section--reverb" style={{ "--pui-accent": REVERB }} data-off={!on || undefined}>
      <div className="pg-section__head">
        {powerToggle}
        <span className="pg-section__name">Reverb</span>
        <span className="pg-section__spacer" />
      </div>
      <div className="pg-section__display">
        <ReverbTail accent={REVERB} />
      </div>
      <div className="pg-section__knobs">
        <JuceKnob parameterId="rmix" caption="Mix" variant="flat" size={44} />
        <JuceKnob parameterId="decay" caption="Decay" variant="flat" size={44} />
        <JuceKnob parameterId="rlocut" caption="Low Cut" variant="flat" size={44} />
      </div>
    </section>
  );
}

/** Live/Freeze: one boolean (`freeze`), drawn as a joined two-button segment
    (COMPONENTS.md: "LIVE/FREEZE joined pair") rather than two independent
    pills - there is one flag and exactly one of the two reads as pressed at
    any time, which a segmented pair says more plainly than two pills with an
    `invert` on one of them. */
function LiveFreezeSwitch() {
  const [freeze, setFreeze] = useJuceToggleValue("freeze");

  return (
    <div className="pg-live-freeze">
      <button
        type="button"
        className="pg-live-freeze__btn"
        data-on={!freeze || undefined}
        onClick={() => setFreeze(false)}
      >
        Live
      </button>
      <button
        type="button"
        className="pg-live-freeze__btn"
        data-on={freeze || undefined}
        onClick={() => setFreeze(true)}
      >
        Freeze
      </button>
    </div>
  );
}

function Header() {
  const [on, setOn] = useJuceToggleValue("on", true);

  return (
    <div className="pg-header">
      <div className="pg-header__row">
        <div className="pg-header__brand">
          <Logo size={32} />
          <h1 className="pg-header__title">Peak Grain</h1>
        </div>
        <div className="pg-header__right">
          <div className="pg-header__level">
            <JuceFader parameterId="volume" label="LEVEL" resetTo={0} length={88} />
          </div>
          <PowerToggle on={on} onToggle={setOn} ariaLabel="Bypass" />
        </div>
      </div>
      <div className="pg-header__row">
        <div className="pg-header__live">
          <LiveFreezeSwitch />
        </div>
        <div className="pg-header__presets">
          <JucePresetBar variant="separated" />
        </div>
      </div>
    </div>
  );
}

export default function GrainFace() {
  return (
    <>
      <Header />
      <div className="pg-plate">
        <div className="pg-row">
          <GrainSection />
          <div className="pg-vdivider" />
          <PitchSection />
          <div className="pg-vdivider" />
          <RandomSection />
        </div>
        <div className="pg-hdivider" />
        <div className="pg-row">
          <DelaySection />
          <div className="pg-vdivider" style={{ gridColumn: 4 }} />
          <ReverbSection />
        </div>
        <div className="pg-hdivider" />
        <div className="pg-scope-wrap">
          <GrainScope />
        </div>
      </div>
    </>
  );
}
