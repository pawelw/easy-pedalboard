import { useState } from "react";
import { BarDisplay, EngineStepper, ModulePanel, ModuleTabs, Toggle, lfoValue } from "@synthpeak/pedal-ui";
import {
  JuceKnob,
  JuceMacroKnob,
  JucePill,
  useJuceChoiceValue,
  useJuceSliderValue,
  useJuceToggleValue,
} from "@synthpeak/pedal-ui/juce";
import { knobRows } from "./engines.jsx";

// The display wells are 63px tall and their bars run 8px to 34px, which is the
// range the handoff specifies for both. Named because two unrelated formulas
// below have to land in the same window or the two modules stop matching.
const BAR_MIN = 8;
const BAR_SPAN = 26;

const TREM_BARS = 26;
const DECAY_BARS = 7;

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

/** The reverb tail: seven bars falling away to the floor, bending as Decay
    opens out. A short decay drops off a cliff, a long one holds up. The last
    bar always lands on BAR_MIN, so the display's floor is the floor rather
    than wherever the maths happened to end. */
function decayBars(decay01) {
  // The exponent runs the opposite way to Decay: a short tail wants the steep
  // curve, so the bars are already on the floor by the end of the well.
  const curve = 0.45 + (1 - decay01) * 1.6;
  return Array.from({ length: DECAY_BARS }, (_, i) =>
    BAR_MIN + BAR_SPAN * Math.pow(1 - i / (DECAY_BARS - 1), curve),
  );
}

/**
 * One of the two narrow modules - Modulation on the left, Reverb on the right.
 * They are the same object at two settings: a power toggle and a Level knob in
 * the header, an engine stepper over that engine's parameter rows, and a Mix
 * knob in the footer. Only the engine list differs, so they are one component
 * rather than two files that would drift.
 *
 * The Delay module is not one of these and does not try to be: it is
 * `<DelayFace>` in a wide `ModulePanel`, and sharing a shell with these two
 * would mean a shell shaped like nothing in particular.
 */
export default function SideModule({ name, accent, engines, engineId, prefix }) {
  const [engineIndex, setEngine] = useJuceChoiceValue(engineId, engines.length);
  const [on, setOn] = useJuceToggleValue(`${prefix}on`, true);
  const engine = engines[engineIndex] ?? engines[0];

  // Which face this module shows: "adv" is the full parameter set, "easy" the
  // single macro knob. Per-module rather than shared - the three narrow
  // modules are switched independently. Not backed by a parameter (the macro
  // itself isn't either yet), so it re-opens on Adv each session.
  const [tab, setTab] = useState("adv");

  return (
    <ModulePanel
      name={name}
      accent={accent}
      width={168}
      on={on}
      onToggle={setOn}
      /* Level is a trim, not a fader: it rests at unity in the middle of its
         travel, so the arc reads out from twelve o'clock in whichever
         direction it has been moved. Wound fully clockwise at rest - which is
         what a 0..100 % level looks like - it said "turned all the way up"
         about a module that was doing nothing to the level at all. */
      headerRight={
        <JuceKnob parameterId={`${prefix}level`} variant="soft" size={30} scaleFrom="centre" bare />
      }
      /* Tape runs fully wet and is not offered a Mix (see engines.jsx) - the
         footer strip stays for the row to keep its shape, held to height in
         CSS, but empty. */
      footer={
        engine.hideMix ? (
          <div className="pa-footer-empty" aria-hidden="true" />
        ) : (
          <JuceKnob parameterId={`${prefix}mix`} variant="soft" size={38} caption="Mix" />
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
      {engine.display === "decay" && <DecayDisplay parameterId={engine.decayId} />}

      {tab === "easy" ? (
        /* The Easy face: one macro knob that rides this engine's own Adv
           knobs (engines.jsx's `easy.targets`). The display above stays -
           it reads the same parameters the macro is moving, so it answers
           to the Easy knob too. */
        <div className="pa-easy">
          {/* Keyed by engine so the macro re-centres when the engine changes,
              rather than carrying one engine's position onto the next. */}
          <JuceMacroKnob
            key={engine.prefix}
            caption={engine.easy.name}
            targets={engine.easy.targets}
            idPrefix={engine.prefix}
          />
        </div>
      ) : (
        <>
      <div className="pa-knobs">
        {knobRows(engine.knobs).map((row) => (
          /* Keyed by the *scoped* id rather than the leaf name: two engines
             can put a knob called "decay" in the same place, and a key that
             only says "decay" has React hand the old knob the new engine's
             props rather than mount a new one. The hooks survive that now
             (see useJuceSliderValue), but the identity should be honest. */
          <div className="pa-knob-row" key={engine.prefix + row[0][0]}>
            {row.map(([id, caption]) => (
              <JuceKnob
                key={engine.prefix + id}
                parameterId={engine.prefix + id}
                caption={caption}
                variant="soft"
                size={36}
              />
            ))}
          </div>
        ))}

        {/* One knob on a row of its own, centred under the pairs - Tape's
            Tone. `scaleFrom="centre"` draws its arc out from twelve o'clock,
            the way the bipolar tilt reads. */}
        {engine.centre && (
          <div className="pa-knob-row pa-knob-row--centre" key={engine.prefix + engine.centre[0]}>
            <JuceKnob
              parameterId={engine.prefix + engine.centre[0]}
              caption={engine.centre[1]}
              variant="soft"
              size={36}
              scaleFrom="centre"
            />
          </div>
        )}
      </div>

      {/* The Tremolo engine's tempo-sync pill, centred under the knob grid.
          `mod.trem.sync`'s own sense is already "synced to tempo", so it lights
          when on with no invert - the same as the Artifact Filter's Sync pill.
          The Rate knob's mid-drag readout re-fetches off its own value, so it
          picks the new unit up on the next turn. */}
      {engine.sync && (
        <div className="pa-sync-row">
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

      {SHOW_EASY_TABS && <ModuleTabs value={tab} onChange={setTab} />}
    </ModulePanel>
  );
}

/* Its own component so its hook only runs for the engine that actually has a
   switch - the same reason the two displays below are split out. */
function TapeSwitch({ parameterId, labelOff, labelOn }) {
  const [on, setOn] = useJuceToggleValue(parameterId, true);

  return (
    <div className="pa-tape-switch">
      <span className="pa-tape-switch__label" data-active={!on || undefined}>
        {labelOff}
      </span>
      <Toggle checked={on} onChange={setOn} ariaLabel={`Tape: ${labelOff} / ${labelOn}`} />
      <span className="pa-tape-switch__label" data-active={on || undefined}>
        {labelOn}
      </span>
    </div>
  );
}

/* Both displays are their own components purely so their hooks only run for
   the engine that is actually showing one - a hook in SideModule would have to
   subscribe to a parameter that the selected engine may not have. */

function TremoloDisplay({ prefix }) {
  const [amount] = useJuceSliderValue(`${prefix}amount`);
  const [shape] = useJuceSliderValue(`${prefix}shape`);

  return (
    <div className="pa-display">
      <BarDisplay heights={tremBars(amount, shape)} ariaLabel="Tremolo envelope" />
    </div>
  );
}

function DecayDisplay({ parameterId }) {
  const [decay] = useJuceSliderValue(parameterId);

  return (
    <div className="pa-display">
      <BarDisplay heights={decayBars(decay)} align="bottom" ariaLabel="Reverb decay" />
    </div>
  );
}
