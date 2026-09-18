import { useDraggable } from "@dnd-kit/core";
import "./ModSourceChip.css";

/** The one drag source this feature has - the Mod tab's own LFO. Dropped on
    any of Grain/Pitch/Random's knobs (ModdableKnob), it assigns that knob as
    a modulation target - see ModRouting.jsx. `id` matches what App.jsx's
    DndContext onDragEnd reads off `active.id`. */
export const MOD_SOURCE_LFO = "lfo";

export default function ModSourceChip() {
  const { attributes, listeners, setNodeRef, transform, isDragging } = useDraggable({ id: MOD_SOURCE_LFO });

  const style = transform ? { transform: `translate3d(${transform.x}px, ${transform.y}px, 0)` } : undefined;

  return (
    <button
      ref={setNodeRef}
      type="button"
      className="pg-mod-chip"
      data-dragging={isDragging || undefined}
      style={style}
      {...listeners}
      {...attributes}
    >
      LFO
    </button>
  );
}
