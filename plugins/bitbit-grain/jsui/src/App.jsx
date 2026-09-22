import { useEffect } from "react";
import { DndContext, KeyboardSensor, PointerSensor, useSensor, useSensors } from "@dnd-kit/core";
import { Card } from "@synthpeak/pedal-ui";
import { installResizableFace } from "@synthpeak/pedal-ui/juce";
import GrainFace from "./GrainFace.jsx";
import { ModRoutingProvider, useModRouting } from "./ModRouting.jsx";
import { MOD_SOURCE_LFO } from "./ModSourceChip.jsx";
import { LfoPlaybackProvider } from "./LfoPlayback.jsx";
import "./index.css";

/** The only drag gesture this feature has: the Mod tab's LFO chip
    (MOD_SOURCE_LFO) dropped onto a ModdableKnob, whose droppable id is that
    knob's own parameterId. Reads the routing context set up below to assign
    it - see ModRouting.jsx's own note on why this lives in a context rather
    than being threaded down as props. */
function ModDropHandler({ children }) {
  const routing = useModRouting();

  // Same move threshold BitBit Alpine's own DndContext uses, for the same
  // reason - a click on the chip shouldn't misfire as a drag.
  const sensors = useSensors(
    useSensor(PointerSensor, { activationConstraint: { distance: 5 } }),
    useSensor(KeyboardSensor),
  );

  const handleDragEnd = ({ active, over }) => {
    if (!over || active.id !== MOD_SOURCE_LFO) return;
    routing.assign(over.id);
  };

  return (
    <DndContext sensors={sensors} onDragEnd={handleDragEnd}>
      {children}
    </DndContext>
  );
}

/**
 * BitBit Grain's enclosure: a 697px Card with no title/logo/preset-bar slots
 * of its own (GrainFace builds its own one-row header as plain children,
 * see COMPONENTS.md - Card's single header-slot API has no room for that
 * shape), then the plate.
 *
 * 697 is the old 560 plus the mixer column's 136 and the divider beside it.
 * The mixer was added *inside* the plate, so without widening the card it
 * came out of the five original sections instead - which squeezed Delay and
 * Reverb past the width their own contents need. The editor follows this
 * through reportContentSize, so the plugin window resizes with it.
 *
 * 982 is 719 plus the cosmos panel (Cosmos.jsx) and its 3px divider: 260px of
 * display-only column between the main rows and the Mixer. It sits outside .pg-plate__main, so
 * every row above keeps exactly the width it had.
 *
 * 719 is that same 697 plus 22px: row 2's own Effects/Mod tab rail
 * (GrainFace.jsx's pg-row2, VerticalTabs.css) sits inside row 2 rather than
 * beside the whole plate, so without the extra width it would eat 22px
 * straight out of Delay/Reverb's own row - already the tightest-fit row on
 * the face (see .pg-delay__body's own note). The extra width lands on every
 * row equally (it is the plate's own width), so Grain/Pitch/Random just get
 * 22px more breathing room instead.
 */
export default function App() {
  useEffect(() => installResizableFace(), []);

  return (
    <div className="page">
      <LfoPlaybackProvider>
        <ModRoutingProvider>
          <ModDropHandler>
            <Card className="pg-card" width={982}>
              <GrainFace />
            </Card>
          </ModDropHandler>
        </ModRoutingProvider>
      </LfoPlaybackProvider>
    </div>
  );
}
