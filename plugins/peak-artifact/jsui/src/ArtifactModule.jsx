import { BarDisplay, EngineStepper, ModulePanel, Toggle, WaveIcon, lfoValue } from "@synthpeak/pedal-ui";
import { JuceKnob, useJuceChoiceValue, useJuceSliderValue, useJuceToggleValue } from "@synthpeak/pedal-ui/juce";
import { ENGINES, WAVES } from "./engines.jsx";

// Peak Artifact's red. It reaches the power ring, the engine stepper and the
// knob value arcs through the one `accent` prop on ModulePanel.
const ACCENT = "#c00001";

// The display well is 63px, the same as Peak Alpine's tremolo display, and its
// bars run 8..34px - identical geometry so the two modules read as one family.
const BAR_MIN = 8;
const BAR_SPAN = 26;
const BARS = 26;

/** The wave that sweeps the filter cutoff, traced from `lfoValue` (the same
    port of ee/dsp/Lfo.h the audio path reads) over two cycles and scaled by
    Range. At Range 0 it flattens to a line, which is what the filter is then
    doing. Decay is not in this picture: it is pinned fully up, so the sweep
    just runs - the red infinity mark in the well says so. */
function filterBars(range01, shape01) {
  return Array.from({ length: BARS }, (_, i) => {
    const phase = (i / (BARS - 1)) * 2;
    return BAR_MIN + BAR_SPAN * range01 * Math.abs(lfoValue(phase, shape01));
  });
}

/**
 * One switchable module: a power toggle and name in the header, an engine
 * stepper, and - for the Filter engine - a display, two rows of knobs, the
 * wave picker and a Mono/Stereo switch, with Mix in the footer. Ring Mod and
 * Bit Crush show a dash: they are selectable but do nothing yet.
 */
export default function ArtifactModule() {
  // Default index 2 (Filter) with no backend - the processor opens on Filter
  // too, since it is the only voiced engine.
  const [engineIndex, setEngine] = useJuceChoiceValue("engine", ENGINES.length, 2);
  const [on, setOn] = useJuceToggleValue("on", true);
  const engine = ENGINES[engineIndex] ?? ENGINES[0];

  return (
    <ModulePanel
      name="Artifact"
      accent={ACCENT}
      width={180}
      on={on}
      onToggle={setOn}
      footer={<JuceKnob parameterId="mix" caption="Mix" variant="soft" size={38} />}
    >
      <EngineStepper
        engines={ENGINES.map((e) => e.name)}
        value={engine.name}
        icon={engine.icon}
        label="Engine"
        onChange={(next) => setEngine(ENGINES.findIndex((e) => e.name === next))}
      />

      {engine.body === "filter" ? <FilterBody /> : <BlankBody />}
    </ModulePanel>
  );
}

/* Its own component so the Filter-only hooks don't run for the other two
   engines - the same reason Peak Alpine splits its displays out. */
function FilterBody() {
  const [range] = useJuceSliderValue("flt.range");
  const [waveIndex, setWave] = useJuceChoiceValue("flt.wave", WAVES.length);
  const wave = WAVES[waveIndex] ?? WAVES[0];

  return (
    <>
      <div className="pa-display">
        <BarDisplay heights={filterBars(range, wave.shape01)} ariaLabel="Filter LFO shape" />
        <span className="pa-inf" aria-label="Decay: always on">&#8734;</span>
      </div>

      <div className="pa-knobs">
        <div className="pa-knob-row">
          <JuceKnob parameterId="flt.freq" caption="Freq" variant="soft" size={36} />
          <JuceKnob parameterId="flt.q" caption="Q" variant="soft" size={36} />
        </div>
        <div className="pa-knob-row">
          <JuceKnob parameterId="flt.range" caption="Range" variant="soft" size={36} />
          <div className="pa-time">
            <JuceKnob parameterId="flt.time" caption="Time" variant="soft" size={36} />
            <SyncSwitch />
          </div>
        </div>
      </div>

      <div className="pa-wave">
        <EngineStepper
          engines={WAVES.map((w) => w.name)}
          value={wave.name}
          icon={<WaveIcon shape01={wave.shape01} size={22} />}
          label="Wave"
          onChange={(next) => setWave(WAVES.findIndex((w) => w.name === next))}
        />
      </div>

      <MonoStereoSwitch />
    </>
  );
}

function BlankBody() {
  return (
    <div className="pa-blank" aria-hidden="true">
      &mdash;
    </div>
  );
}

/* ms / Sync, sitting under the Time knob - two labels either side of the
   switch so both readings of the knob are named. */
function SyncSwitch() {
  const [synced, setSynced] = useJuceToggleValue("flt.sync", false);

  return (
    <div className="pa-inline-switch pa-inline-switch--tight">
      <span className="pa-switch-label" data-active={!synced || undefined}>
        ms
      </span>
      <Toggle checked={synced} onChange={setSynced} ariaLabel="Time: ms / Sync" />
      <span className="pa-switch-label" data-active={synced || undefined}>
        Sync
      </span>
    </div>
  );
}

/* Pinned to the foot of the module body, just above the footer. */
function MonoStereoSwitch() {
  const [stereo, setStereo] = useJuceToggleValue("flt.stereo", false);

  return (
    <div className="pa-inline-switch pa-ms-switch">
      <span className="pa-switch-label" data-active={!stereo || undefined}>
        Mono
      </span>
      <Toggle checked={stereo} onChange={setStereo} ariaLabel="Mono / Stereo" />
      <span className="pa-switch-label" data-active={stereo || undefined}>
        Stereo
      </span>
    </div>
  );
}
