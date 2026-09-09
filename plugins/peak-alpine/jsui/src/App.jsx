import { useEffect } from "react";
import { Card, JucePresetBar, ModulePanel, PowerToggle } from "@synthpeak/pedal-ui";
import { JuceFader, installAutoResize, useJuceToggleValue } from "@synthpeak/pedal-ui/juce";
import { DelayFace } from "@synthpeak/delay-face";
import { ArtifactFace } from "@synthpeak/artifact-face";
import SideModule from "./SideModule.jsx";
import { MOD_ENGINES, REVERB_ENGINES } from "./engines.jsx";
import "./index.css";

/** The host's two trims and its global bypass, in the header's right-hand
    slot. The 32px between the fader stack and the toggle is deliberate and is
    not a gap in a row of controls: the faders trim what the whole plugin is
    fed and returns, and the toggle decides whether any of it runs.

    The toggle is the same 22px ring each module wears - see PowerToggle on why
    the word next to it went. What says the plugin is bypassed is the module row
    behind it going dim, which is also what says a single module is off.

    `on` is handed down rather than read here, because that row reads the same
    parameter to dim itself. Two components each calling useJuceToggleValue
    would hold two independent copies of it, and outside a real host there is no
    relay echo to bring them back together - so the ring would read off over an
    undimmed row. */
function HostControls({ on, onToggle }) {
  return (
    <div className="pa-host-controls">
      <div className="pa-levels">
        <JuceFader parameterId="ingain" label="In" length={104} />
        <JuceFader parameterId="outgain" label="Out" length={104} />
      </div>

      <PowerToggle on={on} onToggle={onToggle} ariaLabel="Bypass" />
    </div>
  );
}

/**
 * Peak Alpine: three effect modules under one chrome.
 *
 * The Delay module is not a re-drawing of Peak Delay - it is Peak Delay's own
 * face, the same `DelayFace` component that pedal renders, bound through a
 * `prefix` to this plugin's namespaced parameters. A fix to that face lands in
 * both plugins, which is the whole reason it lives in a package of its own.
 *
 * The two side modules are one component too (`SideModule`), because
 * Modulation and Reverb are the same object with a different engine list. And
 * the Artifact module, first in the row, is `ArtifactFace` from
 * `@synthpeak/artifact-face` bound through an `art.` prefix - Peak Artifact's
 * own face, the same way the Delay module is Peak Delay's.
 *
 * What is actually written here is only what is unique: the enclosure, the
 * header, and which four modules sit in the row.
 */
export default function App() {
  useEffect(() => installAutoResize(), []);

  // Engaged unless told otherwise - see useJuceToggleValue's note on why the
  // default matters for a power switch.
  const [on, setOn] = useJuceToggleValue("on", true);

  return (
    <div className="page">
      <Card
        title="Peak Alpine"
        subtitle="Artifact / Modulation / Delay / Reverb machine"
        subtitlePlacement="below"
        headerCenter={<JucePresetBar variant="separated" />}
        headerRight={<HostControls on={on} onToggle={setOn} />}
        className="pa-card"
      >
        {/* Bypassed dims the whole row rather than each module's own toggle -
            the modules keep saying what they individually are, and the row
            says none of it is running. */}
        <div className={`pa-modules${on ? "" : " pa-modules--bypassed"}`}>
          {/* First in the chain: Peak Artifact's whole face, bound through an
              "art." prefix. Same component the pedal renders - a fix lands in
              both. Its own Mix sits in its footer, so it takes no Level knob
              here, the way the Delay module doesn't either. */}
          <ArtifactFace prefix="art." />

          {/* "Mod", not "Modulation": a 180px module's header has the toggle,
              the name and a Level knob in it, and the long word left the knob
              no room to breathe. The engine underneath says which modulation
              it is anyway. */}
          <SideModule
            name="Mod"
            accent="var(--pui-accent-mod)"
            engines={MOD_ENGINES}
            engineId="mod.engine"
            prefix="mod."
          />

          {/* The one module whose header ends at the spacer: it has no Level
              knob and no footer Mix, because the face inside it already
              carries a 76px Mix of its own. */}
          <DelayModule />

          <SideModule
            name="Reverb"
            accent="var(--pui-accent-reverb)"
            engines={REVERB_ENGINES}
            engineId="rev.engine"
            prefix="rev."
          />
        </div>
      </Card>
    </div>
  );
}

function DelayModule() {
  const [on, setOn] = useJuceToggleValue("dly.on", true);

  return (
    <ModulePanel
      name="Delay"
      accent="var(--pui-accent-delay)"
      tone="wide"
      width={560}
      on={on}
      onToggle={setOn}
      className="pa-delay"
    >
      <DelayFace prefix="dly." />
    </ModulePanel>
  );
}
