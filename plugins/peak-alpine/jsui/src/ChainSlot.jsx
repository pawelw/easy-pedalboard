import { useSortable } from "@dnd-kit/sortable";
import { CSS } from "@dnd-kit/utilities";

// What in a module header is a control rather than somewhere to take hold of
// it: the power toggle, and the Level knob in the header's right-hand slot.
const CONTROLS = 'button, input, [role="slider"], [role="switch"], [role="button"]';

/**
 * One module's slot in the drag-to-reorder row, wrapping whichever module
 * (ArtifactFace, SideModule, DelayModule) is rendered inside it.
 *
 * **The module's own header is the handle.** There is no separate grip strip:
 * the header is at the top of all four modules, is mostly empty, and already
 * reads as that module's title bar. A strip above it was a fifth band in a row
 * that has four, and it cost 14px off every module's height to draw nothing.
 *
 * Which means the pointer-down is filtered rather than handed straight to
 * dnd-kit, because the header is not empty - the power toggle and the Level
 * knob sit in it, and a press on either has to stay theirs.
 *
 * `listeners` is spread as well as overridden so KeyboardSensor's own
 * activator (onKeyDown, once the slot has focus) still reaches it; only the
 * pointer path needs the filter.
 */
export default function ChainSlot({ moduleId, children }) {
  const { attributes, listeners, setNodeRef, transform, transition, isDragging } = useSortable({
    id: moduleId,
  });

  const style = {
    // Translate, not Transform: these four modules are not the same width, and
    // a sortable whose items differ in size hands back a scaleX/scaleY with
    // the translation - so dragging the 168px Reverb past the 490px Delay
    // stretched the one being dragged to the other's size mid-flight.
    transform: CSS.Translate.toString(transform),
    transition,
  };

  function handlePointerDown(event) {
    const target = event.target;
    if (typeof target?.closest !== "function") return;

    const header = target.closest(".pui-module__header");
    if (!header) return;

    // Scoped to the header, not just `closest`: useSortable's own attributes
    // put role="button" on this slot, so an unscoped search walks straight
    // past the header, finds that, and calls every press a control.
    const control = target.closest(CONTROLS);
    if (control && header.contains(control)) return;

    listeners?.onPointerDown?.(event);
  }

  return (
    <div
      ref={setNodeRef}
      style={style}
      className={`pa-chain-slot${isDragging ? " pa-chain-slot--dragging" : ""}`}
      {...attributes}
      {...listeners}
      onPointerDown={handlePointerDown}
    >
      {children}
    </div>
  );
}
