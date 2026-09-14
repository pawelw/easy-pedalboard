import { useEffect } from "react";
import { closestCenter, DndContext, KeyboardSensor, PointerSensor, useSensor, useSensors } from "@dnd-kit/core";
import { arrayMove, horizontalListSortingStrategy, SortableContext } from "@dnd-kit/sortable";
import { Card, JucePresetBar, ModulePanel, PowerToggle } from "@synthpeak/pedal-ui";
import {
  JuceFader,
  JuceKnob,
  installResizableFace,
  useJuceBuildInfo,
  useJuceToggleValue,
} from "@synthpeak/pedal-ui/juce";
import { DelayFace } from "@synthpeak/delay-face";
import { ArtifactFace } from "@synthpeak/artifact-face";
import { ModulationFace, ReverbFace } from "@synthpeak/module-face";
import ChainSlot from "./ChainSlot.jsx";
import { MODULE_ARTIFACT, MODULE_MODULATION, MODULE_DELAY, MODULE_REVERB } from "./chainOrder.js";
import { useChainOrder } from "./useChainOrder.js";
import { ARTIFACT_EASY } from "./engines.jsx";
import "./index.css";

/** A module's Level trim for the header's right-hand slot - the same one on all
    four modules: a bipolar trim resting at unity, its arc drawn out from twelve
    o'clock in whichever direction it has been moved. It is this plugin's module
    chrome, not any one pedal's parameter, so each shared face is handed it as
    `headerRight` and it resolves through that face's own ParamScope. */
function ModuleLevel({ parameterId }) {
  return (
    <JuceKnob parameterId={parameterId} variant="flat" size={24} scaleFrom="centre" bare />
  );
}

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
/** A quiet corner stamp of the native build's own compile time - see
    useJuceBuildInfo. Fixed-position and outside the Card, so it never enters
    installAutoResize's measurement of `.pui-card`; it exists only so a
    rebuilt AU/VST3 can be told apart from a stale one still sitting in an
    already-open project or a DAW's own plugin cache. Renders nothing outside
    a real host or on a processor that has not wired the function up. */
function BuildStamp() {
  const build = useJuceBuildInfo();
  if (!build) return null;
  return <div className="pa-build-stamp">{build}</div>;
}

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

/** Renders each of the four modules exactly as they always have; what decides
    which order they appear in is chain.order (useChainOrder), not this map.
    A function per entry, not the element itself, so building the map doesn't
    call any of the four components before React is the one rendering them. */
const MODULE_RENDERERS = {
  [MODULE_ARTIFACT]: () => (
    <ArtifactFace
      prefix="art."
      headerRight={<ModuleLevel parameterId="level" />}
      easyTab
      easyConfig={ARTIFACT_EASY}
      knobVariant="flat"
    />
  ),
  [MODULE_MODULATION]: () => (
    <ModulationFace prefix="mod." headerRight={<ModuleLevel parameterId="level" />} easyTab knobVariant="flat" />
  ),
  [MODULE_DELAY]: () => <DelayModule />,
  [MODULE_REVERB]: () => (
    <ReverbFace prefix="rev." headerRight={<ModuleLevel parameterId="level" />} easyTab knobVariant="flat" />
  ),
};

/**
 * Peak Alpine: three effect modules under one chrome.
 *
 * The Delay module is not a re-drawing of Peak Delay - it is Peak Delay's own
 * face, the same `DelayFace` component that pedal renders, bound through a
 * `prefix` to this plugin's namespaced parameters. A fix to that face lands in
 * both plugins, which is the whole reason it lives in a package of its own.
 *
 * The other three are the same arrangement. Modulation and Reverb are
 * `ModulationFace` and `ReverbFace` from `@synthpeak/module-face` - Peak
 * Modulation's and Peak Reverb's own faces, bound through `mod.` and `rev.` -
 * and the Artifact module, first in the row, is `ArtifactFace` from
 * `@synthpeak/artifact-face` bound through `art.`, Peak Artifact's own face.
 *
 * What is actually written here is only what is unique: the enclosure, the
 * header, which four modules are in the row, and - through ChainSlot's grip
 * and chain.order - what order they run in. Dragging one doesn't touch the
 * module components themselves; MODULE_RENDERERS below still builds the
 * exact same four elements it always did.
 */
export default function App() {
  useEffect(() => installResizableFace(), []);

  // Engaged unless told otherwise - see useJuceToggleValue's note on why the
  // default matters for a power switch.
  const [on, setOn] = useJuceToggleValue("on", true);

  // Which of the four modules runs first, second, third, fourth - see
  // useChainOrder and plugins/peak-alpine/src/ChainOrder.h. Dragging a
  // module's grip (ChainSlot) reorders this; the modules themselves render
  // exactly as they always have (MODULE_RENDERERS above).
  const [order, setOrder] = useChainOrder();

  // A small move threshold before a pointer-down on the grip counts as a
  // drag, so a click-through doesn't misfire - the same guard Mantine's own
  // dnd-list-handle example uses. Keyboard reordering (arrow keys once a grip
  // has focus) comes from KeyboardSensor for free.
  const sensors = useSensors(
    useSensor(PointerSensor, { activationConstraint: { distance: 5 } }),
    useSensor(KeyboardSensor),
  );

  function handleDragEnd({ active, over }) {
    if (!over || active.id === over.id) return;
    setOrder(arrayMove(order, order.indexOf(active.id), order.indexOf(over.id)));
  }

  return (
    <div className="page">
      <Card
        title="Peak Alpine"
        subtitle="Artifact / Mod / Delay / Reverb machine"
        subtitlePlacement="below"
        headerCenter={<JucePresetBar variant="separated" />}
        headerRight={<HostControls on={on} onToggle={setOn} />}
        className="pa-card"
      >
        {/* Bypassed dims the whole row rather than each module's own toggle -
            the modules keep saying what they individually are, and the row
            says none of it is running. */}
        <div className={`pa-modules${on ? "" : " pa-modules--bypassed"}`}>
          <DndContext sensors={sensors} collisionDetection={closestCenter} onDragEnd={handleDragEnd}>
            <SortableContext items={order} strategy={horizontalListSortingStrategy}>
              {order.map((moduleId) => (
                <ChainSlot key={moduleId} moduleId={moduleId}>
                  {MODULE_RENDERERS[moduleId]()}
                </ChainSlot>
              ))}
            </SortableContext>
          </DndContext>
        </div>
      </Card>
      <BuildStamp />
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
      width={490}
      on={on}
      onToggle={setOn}
      headerRight={<ModuleLevel parameterId="dly.level" />}
      className="pa-delay"
    >
      <DelayFace prefix="dly." tapeRouter={false} stageKnobSize={36} mainKnobSize={60} />
    </ModulePanel>
  );
}
