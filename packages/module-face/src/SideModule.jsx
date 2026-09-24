import { useEffect, useState } from "react";
import {
  BarDisplay,
  ChorusScope,
  EngineStepper,
  FilterScope,
  ModulePanel,
  ModuleTabs,
  PhaserScope,
  ReverbScope,
  Toggle,
  WaveIcon,
  freqHzFor01,
  lfoValue,
} from "@synthpeak/pedal-ui";
import {
  JuceKnob,
  JuceMacroKnob,
  JucePill,
  ParamScope,
  useJuceChoiceValue,
  useJuceSliderValue,
  useJuceToggleValue,
  useParamId,
} from "@synthpeak/pedal-ui/juce";
import { FILTER_WAVES, knobRows } from "./engines.jsx";
import "./SideModule.css";

// The display wells are 63px tall and their bars run 8px to 34px, which is the
// range the handoff specifies for both. Named because two unrelated formulas
// below have to land in the same window or the two modules stop matching.
const BAR_MIN = 8;
const BAR_SPAN = 26;

// The Filter scope fills the display slot, like every other well (see
// ModulePanel.css's slot ladder). It used to be cut to 53px to keep its body
// from growing the module; the slot is fixed now, so the height it draws at is
// only about how the scope reads.
const FILTER_SCOPE_HEIGHT = 64;

const TREM_BARS = 26;

// The Easy / Adv strip is hidden for now and every module opens on Adv. The
// Easy path below is kept whole so flipping this back is the only change.
const SHOW_EASY_TABS = false;

/** The tremolo's own envelope, traced from the same shaped-LFO the audio path
    uses (`lfoValue`, a hand-kept port of ee/dsp/Lfo.h) rather than from a
    stand-in curve - a picture drawn from a different formula than the engine
    is a picture of the wrong wave.
 *
 * Two cycles across the well, so the shape reads as something repeating rather
 * than as one hump, and scaled by Amount: at Amount 0 the display flattens to
 * a line, which is exactly what the tremolo is doing.
 *
 * The handoff draws this statically as `8 + 26·|sin(2π·i/25)|`. That is this
 * curve at full Amount and a sine-ish Shape; the deviation is deliberate,
 * because a fixed picture on a live control is a decoration rather than a
 * readout. */
function tremBars(amount01, shape01) {
  return Array.from({ length: TREM_BARS }, (_, i) => {
    const phase = (i / (TREM_BARS - 1)) * 2;
    return BAR_MIN + BAR_SPAN * amount01 * Math.abs(lfoValue(phase, shape01));
  });
}

/**
 * One narrow switchable module - Modulation or Reverb. They are the same object
 * at two settings: a power toggle and a name in the header, an engine stepper
 * over that engine's parameter rows, and a Mix knob in the footer. Only the
 * engine list differs, so they are one component rather than two files that
 * would drift. `ModulationFace` and `ReverbFace` are this with their list.
 *
 * One component, two hosts per module, the way `ArtifactFace` is: BitBit
 * Modulation and BitBit Reverb each wrap theirs in a Card of their own, and BitBit
 * Alpine drops both into its module row. The Alpine modules are not re-draws
 * of those pedals' faces, they *are* those faces - so a fix lands in both.
 *
 * `prefix` is the parameter-id prefix every control binds through: "" for the
 * standalone pedals, whose ids are plain (`mix`, `trem.rate`), and "mod." or
 * "rev." for BitBit Alpine, whose are namespaced by module. Nothing below takes
 * an id map; the ParamScope does the whole job, and the leaf names are
 * identical in both plugins on purpose.
 *
 * `headerRight` is whatever the host wants in the header's right-hand slot -
 * BitBit Alpine puts its Level trim there, the same one each of its modules
 * carries; the standalone pedals pass nothing. It is the host's to supply
 * because that Level is the host's chrome, not one of the pedal's parameters.
 *
 * `easyTab` lets the Easy / Adv strip read each engine's `easy` macro (BitBit
 * Alpine turns it on; the standalone pedals do not). `knobVariant` is Knob's
 * own `variant`, forwarded to every knob this face draws - "concave" on its
 * own, as BitBit Artifact's is; BitBit Alpine passes "flat".
 *
 * The Delay module is not one of these and does not try to be: it is
 * `<DelayFace>` in a wide `ModulePanel`, and sharing a shell with these two
 * would mean a shell shaped like nothing in particular.
 */
export default function SideModule({ prefix = "", ...props }) {
  return (
    <ParamScope prefix={prefix}>
      <SideModuleBody {...props} />
    </ParamScope>
  );
}

/** Split out so its hooks resolve *inside* the ParamScope above - a hook in
    SideModule itself would read the enclosing scope, not the one it declares. */
function SideModuleBody({ name, accent, engines, headerRight = null, easyTab = false, knobVariant = "concave" }) {
  const [engineIndex, setEngine] = useJuceChoiceValue("engine", engines.length);
  const [on, setOn] = useJuceToggleValue("on", true);
  const engine = engines[engineIndex] ?? engines[0];
  const easy = easyTab ? engine.easy : null;

  // JuceMacroKnob's `idPrefix` replaces the enclosing scope rather than adding
  // to it, so the engine's own prefix is resolved through the scope here first.
  const macroPrefix = useParamId(engine.prefix);

  // Which face this module shows: "adv" is the full parameter set, "easy" the
  // single macro knob. Per-module rather than shared - the modules are switched
  // independently. Not backed by a parameter (the macro itself isn't either
  // yet), so it re-opens on Adv each session.
  const [tab, setTab] = useState("adv");

  return (
    <ModulePanel
      name={name}
      accent={accent}
      width={168}
      on={on}
      onToggle={setOn}
      headerRight={headerRight}
      className="sm-module"
      /* Tape runs fully wet and is not offered a Mix (see engines.jsx) - the
         footer strip stays for the module to keep its shape, held to height in
         CSS, but empty. */
      footer={
        engine.hideMix ? (
          <div className="sm-footer-empty" aria-hidden="true" />
        ) : (
          <JuceKnob parameterId="mix" variant={knobVariant} size={38} caption="Mix" />
        )
      }
    >
      <EngineStepper
        engines={engines.map((e) => e.name)}
        value={engine.name}
        icon={engine.icon}
        label={`${name} engine`}
        onChange={(next) => setEngine(engines.findIndex((e) => e.name === next))}
      />

      {engine.display === "tremolo" && <TremoloDisplay prefix={engine.prefix} />}
      {engine.display === "reverb" && engine.reverb === "spring" && <SpringDisplay prefix={engine.prefix} />}
      {engine.display === "reverb" && engine.reverb === "shimmer" && <ShimmerDisplay prefix={engine.prefix} />}
      {engine.display === "reverb" && engine.reverb === "studio" && <StudioDisplay prefix={engine.prefix} />}
      {engine.display === "filter" && <FilterDisplay prefix={engine.prefix} />}
      {engine.display === "chorus" && <ChorusDisplay prefix={engine.prefix} />}
      {engine.display === "phaser" && <PhaserDisplay prefix={engine.prefix} />}

      {easy && tab === "easy" ? (
        /* The Easy face: one macro knob that rides this engine's own Adv
           knobs (engines.jsx's `easy.targets`). The display above stays -
           it reads the same parameters the macro is moving, so it answers
           to the Easy knob too. */
        <div className="sm-easy">
          {/* Keyed by engine so the macro re-centres when the engine changes,
              rather than carrying one engine's position onto the next. */}
          <JuceMacroKnob
            key={engine.prefix}
            caption={easy.name}
            targets={easy.targets}
            idPrefix={macroPrefix}
            variant={knobVariant}
          />
        </div>
      ) : engine.body === "filter" ? (
        <FilterBody prefix={engine.prefix} knobVariant={knobVariant} />
      ) : (
        <>
          <div className="sm-knobs">
            {/* One or more knobs on a row of their own, centred as a group
                above the pairs - the Studio reverb's Decay and Size. */}
            {engine.lead && (
              <div className="sm-knob-row sm-knob-row--centre" key={engine.prefix + engine.lead[0][0]}>
                {engine.lead.map(([id, caption]) => (
                  <JuceKnob
                    key={engine.prefix + id}
                    parameterId={engine.prefix + id}
                    caption={caption}
                    variant={knobVariant}
                    size={36}
                  />
                ))}
              </div>
            )}

            {knobRows(engine.knobs).map((row) => (
              /* Keyed by the engine-qualified id rather than the leaf name:
                 two engines can put a knob called "decay" in the same place,
                 and a key that only says "decay" has React hand the old knob
                 the new engine's props rather than mount a new one. The hooks
                 survive that now (see useJuceSliderValue), but the identity
                 should be honest. */
              <div className="sm-knob-row" key={engine.prefix + row[0][0]}>
                {row.map(([id, caption, scaleFrom]) => (
                  <JuceKnob
                    key={engine.prefix + id}
                    parameterId={engine.prefix + id}
                    caption={caption}
                    variant={knobVariant}
                    // A cut is a supporting knob, not a lead one - 8px
                    // smaller than the rest of the row reads that as a
                    // deliberate size, not a rendering mistake.
                    size={id === "locut" || id === "hicut" ? 28 : 36}
                    scaleFrom={scaleFrom}
                  />
                ))}
              </div>
            ))}

            {/* One knob on a row of its own, centred under the pairs - Tape's
                Tone. `scaleFrom="centre"` draws its arc out from twelve o'clock,
                the way the bipolar tilt reads. */}
            {engine.centre && (
              <div className="sm-knob-row sm-knob-row--centre" key={engine.prefix + engine.centre[0]}>
                <JuceKnob
                  parameterId={engine.prefix + engine.centre[0]}
                  caption={engine.centre[1]}
                  variant={knobVariant}
                  size={36}
                  scaleFrom="centre"
                />
              </div>
            )}
          </div>

          {/* Below the knob grid rather than above it - the Shimmer engine's
              octave. A picker above read as a second engine stepper; here it
              reads as one more row of "what this engine is doing". */}
          {engine.picker && <PickerStepper prefix={engine.prefix} {...engine.picker} />}

          {/* The Tremolo engine's tempo-sync pill, centred under the knob grid.
              `trem.sync`'s own sense is already "synced to tempo", so it lights
              when on with no invert - the same as the Filter engine's Sync pill.
              The Rate knob's mid-drag readout re-fetches off its own value, so it
              picks the new unit up on the next turn. */}
          {engine.sync && (
            <div className="sm-sync-row">
              <JucePill parameterId={engine.sync} label="Sync" />
            </div>
          )}

          {/* Pinned to the bottom of the body, just above the footer, however many
              knob rows are above it - Tape's Mono/Stereo switch. */}
          {engine.toggle && (
            <TapeSwitch
              parameterId={engine.prefix + engine.toggle[0]}
              labelOff={engine.toggle[1]}
              labelOn={engine.toggle[2]}
            />
          )}
        </>
      )}

      {SHOW_EASY_TABS && easy && <ModuleTabs value={tab} onChange={setTab} />}
    </ModulePanel>
  );
}

/* Its own component so its hook only runs for the engine that actually has a
   switch - the same reason the displays below are split out. Tape's rests on
   (Stereo), Filter's off (Mono), hence `defaultOn`. */
function TapeSwitch({ parameterId, labelOff, labelOn, defaultOn = true, owner = "Tape" }) {
  const [on, setOn] = useJuceToggleValue(parameterId, defaultOn);

  return (
    <div className="sm-tape-switch">
      <span className="sm-tape-switch__label" data-active={!on || undefined}>
        {labelOff}
      </span>
      <Toggle checked={on} onChange={setOn} ariaLabel={`${owner}: ${labelOff} / ${labelOn}`} />
      <span className="sm-tape-switch__label" data-active={on || undefined}>
        {labelOn}
      </span>
    </div>
  );
}

/* The displays are their own components purely so their hooks only run for
   the engine that is actually showing one - a hook in SideModuleBody would have
   to subscribe to a parameter that the selected engine may not have. */

function TremoloDisplay({ prefix }) {
  const [amount] = useJuceSliderValue(`${prefix}amount`);
  const [shape] = useJuceSliderValue(`${prefix}shape`);

  return (
    <div className="sm-display">
      <BarDisplay heights={tremBars(amount, shape)} ariaLabel="Tremolo envelope" />
    </div>
  );
}

/* Both read the knobs' normalised positions: Rate's skewed travel is what the
   scope's cycle count follows, and Phase's 0..180 deg range is linear, so its
   normalised value is the fraction of the engine's phase span. */
function ChorusDisplay({ prefix }) {
  const [rate] = useJuceSliderValue(`${prefix}rate`);
  const [depth] = useJuceSliderValue(`${prefix}depth`);
  const [phase] = useJuceSliderValue(`${prefix}phase`);

  return (
    <div className="sm-display">
      <ChorusScope rate01={rate} depth01={depth} phase01={phase} />
    </div>
  );
}

function PhaserDisplay({ prefix }) {
  const [rate] = useJuceSliderValue(`${prefix}rate`);
  const [depth] = useJuceSliderValue(`${prefix}depth`);

  return (
    <div className="sm-display">
      <PhaserScope rate01={rate} depth01={depth} />
    </div>
  );
}

/* The three reverbs share one display (ReverbScope) and differ only in which
   knobs feed its model - one component each so each subscribes to the
   parameters its engine actually has. */
function SpringDisplay({ prefix }) {
  const [decay] = useJuceSliderValue(`${prefix}decay`);
  const [tension] = useJuceSliderValue(`${prefix}tension`);
  const [locut] = useJuceSliderValue(`${prefix}locut`);
  const [hicut] = useJuceSliderValue(`${prefix}hicut`);

  return (
    <div className="sm-display">
      <ReverbScope
        engine="spring"
        decay01={decay}
        tension01={tension}
        lowCut01={locut}
        highCut01={hicut}
        ariaLabel="Spring reverb decay"
      />
    </div>
  );
}

function ShimmerDisplay({ prefix }) {
  const [decay] = useJuceSliderValue(`${prefix}decay`);
  const [damping] = useJuceSliderValue(`${prefix}damping`);
  const [locut] = useJuceSliderValue(`${prefix}locut`);
  const [hicut] = useJuceSliderValue(`${prefix}hicut`);
  const [octave] = useJuceChoiceValue(`${prefix}octave`, 3);

  return (
    <div className="sm-display">
      <ReverbScope
        engine="shimmer"
        decay01={decay}
        damping01={damping}
        lowCut01={locut}
        highCut01={hicut}
        octave={octave}
        ariaLabel="Shimmer reverb decay"
      />
    </div>
  );
}

function StudioDisplay({ prefix }) {
  const [decay] = useJuceSliderValue(`${prefix}decay`);
  const [size] = useJuceSliderValue(`${prefix}size`);
  const [predelay] = useJuceSliderValue(`${prefix}predelay`);
  const [damping] = useJuceSliderValue(`${prefix}damping`);
  const [locut] = useJuceSliderValue(`${prefix}locut`);
  const [hicut] = useJuceSliderValue(`${prefix}hicut`);

  return (
    <div className="sm-display">
      <ReverbScope
        engine="studio"
        decay01={decay}
        size01={size}
        predelay01={predelay}
        damping01={damping}
        lowCut01={locut}
        highCut01={hicut}
        ariaLabel="Studio reverb decay"
      />
    </div>
  );
}

/** A discrete choice above the knob grid, drawn with the same well-and-arrows
    `EngineStepper` the module's own engine picker uses (the Shimmer engine's
    octave). `options` is read/written by index, like `ShapeFamilyStepper`
    (BitBit Grain) and the module's own engine stepper above - see engines.jsx's
    `picker` field. */
function PickerStepper({ prefix, paramId, options, label }) {
  const [index, select] = useJuceChoiceValue(`${prefix}${paramId}`, options.length);

  return (
    <div className="sm-picker">
      <EngineStepper
        engines={options}
        value={options[index] ?? options[0]}
        label={label}
        onChange={(next) => select(options.indexOf(next))}
      />
    </div>
  );
}

/**
 * The Filter engine's live cutoff-sweep exponent for both channels, pushed from
 * the processor as the one "filterMod" event (the editor's Timer) - the same
 * feed BitBit Wah's scope rides on. Outside a real host there is no backend to
 * send it, so it stays at 0 and the two swept curves rest on the base curve.
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

/* BitBit Wah's response scope in the module's own ink: a resting curve whose peak
   rises and narrows with Q and slides with Freq, a band showing how far Range
   lets it sweep, and two curves riding the live L/R sweep inside it. The
   infinity mark stands in for the Decay knob this engine does not have - it is
   pinned fully up, so the sweep runs forever. */
function FilterDisplay({ prefix }) {
  const [freq] = useJuceSliderValue(`${prefix}freq`);
  const [q] = useJuceSliderValue(`${prefix}q`);
  const [range] = useJuceSliderValue(`${prefix}range`);
  const { modL, modR } = useFilterMod();

  return (
    <div className="sm-display sm-display--filter">
      <FilterScope
        baseFreqHz={freqHzFor01(freq)}
        resonance01={q}
        sweepDepth01={range}
        modL={modL}
        modR={modR}
        height={FILTER_SCOPE_HEIGHT}
      />
      <span className="sm-inf" aria-label="Decay: always on">
        &#8734;
      </span>
    </div>
  );
}

/* Filter's Adv body: Freq / Q, then Range and Time each with a small control
   directly under it - the <> wave picker and the Sync pill - and a Mono/Stereo
   switch at the foot. Not the knob grid, because the grid has nowhere to hang
   a control under one knob. */
function FilterBody({ prefix, knobVariant }) {
  const [waveIndex, setWave] = useJuceChoiceValue(`${prefix}wave`, FILTER_WAVES.length);
  const wave = FILTER_WAVES[waveIndex] ?? FILTER_WAVES[0];

  return (
    <>
      <div className="sm-knobs">
        <div className="sm-knob-row">
          <JuceKnob parameterId={`${prefix}freq`} caption="Freq" variant={knobVariant} size={36} />
          <JuceKnob parameterId={`${prefix}q`} caption="Q" variant={knobVariant} size={36} />
        </div>
        <div className="sm-knob-row">
          <div className="sm-subcol">
            <JuceKnob parameterId={`${prefix}range`} caption="Range" variant={knobVariant} size={36} />
            <div className="sm-sub sm-sub--wave">
              <EngineStepper
                engines={FILTER_WAVES.map((w) => w.name)}
                value={wave.name}
                icon={<WaveIcon shape01={wave.shape01} size={15} />}
                label="Filter wave"
                onChange={(next) => setWave(FILTER_WAVES.findIndex((w) => w.name === next))}
              />
            </div>
          </div>

          <div className="sm-subcol">
            <JuceKnob parameterId={`${prefix}time`} caption="Time" variant={knobVariant} size={36} />
            <div className="sm-sub sm-sub--sync">
              {/* sync's own sense is already "synced to tempo", so it lights
                  when on with no invert. */}
              <JucePill parameterId={`${prefix}sync`} label="Sync" />
            </div>
          </div>
        </div>
      </div>

      <TapeSwitch parameterId={`${prefix}stereo`} labelOff="Mono" labelOn="Stereo" defaultOn={false} owner="Filter" />
    </>
  );
}
