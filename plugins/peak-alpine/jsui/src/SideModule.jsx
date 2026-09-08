import { BarDisplay, EngineStepper, ModulePanel, lfoValue } from "@synthpeak/pedal-ui";
import { JuceKnob, useJuceChoiceValue, useJuceSliderValue, useJuceToggleValue } from "@synthpeak/pedal-ui/juce";
import { knobRows } from "./engines.jsx";

// The display wells are 63px tall and their bars run 8px to 34px, which is the
// range the handoff specifies for both. Named because two unrelated formulas
// below have to land in the same window or the two modules stop matching.
const BAR_MIN = 8;
const BAR_SPAN = 26;

const TREM_BARS = 26;
const DECAY_BARS = 7;

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
  const curve = 0.45 + decay01 * 1.6;
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

  return (
    <ModulePanel
      name={name}
      accent={accent}
      width={186}
      on={on}
      onToggle={setOn}
      headerRight={<JuceKnob parameterId={`${prefix}level`} variant="soft" size={24} bare />}
      footer={<JuceKnob parameterId={`${prefix}mix`} variant="soft" size={38} caption="Mix" />}
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

      <div className="pa-knobs">
        {knobRows(engine.knobs).map((row) => (
          <div className="pa-knob-row" key={row[0][0]}>
            {row.map(([id, caption]) => (
              <JuceKnob
                key={id}
                parameterId={engine.prefix + id}
                caption={caption}
                variant="soft"
                size={40}
              />
            ))}
          </div>
        ))}
      </div>
    </ModulePanel>
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
