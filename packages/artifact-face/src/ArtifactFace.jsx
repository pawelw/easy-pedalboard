import { useEffect, useState } from "react";
import { EngineStepper, FilterScope, ModulePanel, Toggle, WaveIcon, freqHzFor01 } from "@synthpeak/pedal-ui";
import {
  JuceKnob,
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
 * stepper, and, for the Filter engine, a response scope, two rows of knobs, the
 * wave picker and a Mono/Stereo switch, with Mix in the footer. Ring Mod and
 * Bit Crush show a dash: they are selectable but do nothing yet.
 *
 * One component, two hosts. Peak Artifact wraps this in its own Card; Peak
 * Alpine drops it into its module row as the first module. The whole reason
 * this package exists is that the Artifact module in the multi-effect host is
 * not a re-draw of Peak Artifact's face, it *is* that face - so a fix lands in
 * both and neither can drift.
 *
 * `prefix` is the parameter-id prefix its controls bind through: "" for Peak
 * Artifact, whose parameters are plain (`mix`, `flt.freq`), and "art." for Peak
 * Alpine, whose are namespaced by module. Nothing below takes an id map; the
 * `ParamScope` does the whole job, and the leaf names are identical in both
 * plugins on purpose.
 */
export default function ArtifactFace({ prefix = "" }) {
  return (
    <ParamScope prefix={prefix}>
      <ArtifactFaceBody />
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

/** Split out so its hooks resolve *inside* the ParamScope above - a hook in
    ArtifactFace itself would read the enclosing scope, not the one it declares. */
function ArtifactFaceBody() {
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

      {engine.body === "filter" ? <FilterBody /> : <BlankBody />}
    </ModulePanel>
  );
}

/* Its own component so the Filter-only hooks don't run for the other two
   engines - the same reason Peak Alpine splits its displays out. */
function FilterBody() {
  const [freq] = useJuceSliderValue("flt.freq");
  const [q] = useJuceSliderValue("flt.q");
  const [range] = useJuceSliderValue("flt.range");
  const [waveIndex, setWave] = useJuceChoiceValue("flt.wave", WAVES.length);
  const wave = WAVES[waveIndex] ?? WAVES[0];
  const { modL, modR } = useFilterMod();

  return (
    <>
      {/* The same component Peak Wah's scope is: a resting curve whose peak
          rises and narrows with Q and slides with Freq, a translucent band
          showing how far Range lets it sweep, and two curves riding the live
          L/R sweep inside it. Only the ink changes here. */}
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

function BlankBody() {
  return (
    <div className="af-blank" aria-hidden="true">
      &mdash;
    </div>
  );
}

/* Pinned to the foot of the module body, just above the footer. */
function MonoStereoSwitch() {
  const [stereo, setStereo] = useJuceToggleValue("flt.stereo", false);

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
