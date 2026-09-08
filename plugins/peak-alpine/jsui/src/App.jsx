import { useEffect } from "react";
import { Card, JucePresetBar, ModulePanel, PowerToggle } from "@synthpeak/pedal-ui";
import { JuceFader, installAutoResize, useJuceToggleValue } from "@synthpeak/pedal-ui/juce";
import { DelayFace } from "@synthpeak/delay-face";
import SideModule from "./SideModule.jsx";
import { MOD_ENGINES, REVERB_ENGINES } from "./engines.jsx";
import "./index.css";

/** The host's two trims and its global bypass, in the header's right-hand
    slot. The 32px between the fader stack and the pill is deliberate and is
    not a gap in a row of controls: the faders trim what the whole plugin is
    fed and returns, and the pill decides whether any of it runs.

    `on` is handed down rather than read here, because the module row below
    reads the same parameter to dim itself. Two components each calling
    useJuceToggleValue would hold two independent copies of it, and outside a
    real host there is no relay echo to bring them back together - so the pill
    would say BYPASSED over an undimmed row. */
function HostControls({ on, onToggle }) {
  return (
    <div className="pa-host-controls">
      <div className="pa-levels">
        <JuceFader parameterId="ingain" label="In" length={104} />
        <JuceFader parameterId="outgain" label="Out" length={104} />
      </div>

      <PowerToggle variant="pill" on={on} onToggle={onToggle} ariaLabel="Bypass" />
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
 * Modulation and Reverb are the same object with a different engine list.
 * What is actually written here is only what is unique: the enclosure, the
 * header, and which three modules sit in the row.
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
        subtitle="Modulation / Delay / Reverb machine"
        subtitlePlacement="below"
        headerCenter={<JucePresetBar variant="separated" />}
        headerRight={<HostControls on={on} onToggle={setOn} />}
        className="pa-card"
        width={1046}
      >
        {/* Bypassed dims the whole row rather than each module's own toggle -
            the modules keep saying what they individually are, and the row
            says none of it is running. */}
        <div className={`pa-modules${on ? "" : " pa-modules--bypassed"}`}>
          <SideModule
            name="Modulation"
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
      width={598}
      on={on}
      onToggle={setOn}
      className="pa-delay"
    >
      <DelayFace prefix="dly." />
    </ModulePanel>
  );
}
