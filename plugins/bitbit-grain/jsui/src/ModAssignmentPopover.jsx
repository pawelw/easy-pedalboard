import { useLayoutEffect } from "react";
import "./ModAssignmentPopover.css";

// How close to the viewport's own edge the popover may sit before it gets
// pinned to that edge instead of staying centred under the knob.
const EDGE_MARGIN = 8;

/** The depth editor for one modulation assignment - opened from
    ModdableKnob's own badge. A plain range input rather than pedal-ui's
    Knob/Slider: this is a small, local, one-off control (unipolar depth on a
    single assignment - 0 is no effect, 100% is the LFO's full swing, no
    inversion), not a face knob bound to a parameter.

    `popoverRef` is handed both to ModdableKnob (for its own outside-click
    detection) and used here, once after mount, to check whether the default
    centred position would run the popover off either edge - a knob near the
    face's own right edge (Bit, Filter) otherwise had its Remove/Done buttons
    clipped. If so it re-anchors to the wrapping knob's own near edge instead
    of centring. The same check runs vertically: a knob near the bottom of
    the plate (the Mixer's Filter/Drive/Bit row) opens this below itself by
    default (`top: calc(100% + 6px)`, ModAssignmentPopover.css), which for
    those knobs ran the popover past the plate's own edge - so it flips to
    open upward instead, the same "near edge" re-anchor the horizontal check
    already does.

    The edge measured against is GrainFace.jsx's `.pg-plate` (found via
    closest()), not the window - `.pg-plate` clips its own overflow for its
    rounded corners, and it sits inset from the actual plugin window by the
    card's own padding, so a popover can run off the plate's edge while it is
    still well inside the window. A window-only check missed exactly that:
    it never triggered for the Filter knob because the *window* had room, even
    though `.pg-plate`'s overflow:hidden was already eating the popover. Falls
    back to the window if `.pg-plate` isn't found (this popover is bitbit-grain
    only today, but nothing here should silently misbehave if that changes). */
export default function ModAssignmentPopover({ popoverRef, depth, onChangeDepth, onRemove, onClose }) {
  useLayoutEffect(() => {
    const el = popoverRef?.current;
    if (!el) return;

    el.style.left = "50%";
    el.style.right = "auto";
    el.style.transform = "translateX(-50%)";
    el.style.top = "calc(100% + 6px)";
    el.style.bottom = "auto";

    const clip = el.closest(".pg-plate")?.getBoundingClientRect();
    const edgeLeft = Math.max(0, clip?.left ?? 0) + EDGE_MARGIN;
    const edgeRight = Math.min(window.innerWidth, clip?.right ?? window.innerWidth) - EDGE_MARGIN;
    const edgeBottom = Math.min(window.innerHeight, clip?.bottom ?? window.innerHeight) - EDGE_MARGIN;

    const rect = el.getBoundingClientRect();
    if (rect.right > edgeRight) {
      el.style.left = "auto";
      el.style.right = "0";
      el.style.transform = "none";
    } else if (rect.left < edgeLeft) {
      el.style.left = "0";
      el.style.right = "auto";
      el.style.transform = "none";
    }

    if (rect.bottom > edgeBottom) {
      el.style.top = "auto";
      el.style.bottom = "calc(100% + 6px)";
    }
  }, [popoverRef]);

  return (
    <div ref={popoverRef} className="pg-mod-popover" onPointerDown={(event) => event.stopPropagation()}>
      <div className="pg-mod-popover__row">
        <span className="pg-mod-popover__label">Depth</span>
        <span className="pg-mod-popover__value">{Math.round(depth * 100)}%</span>
      </div>
      <input
        className="pg-mod-popover__slider"
        type="range"
        min={0}
        max={100}
        step={1}
        value={Math.round(depth * 100)}
        onChange={(event) => onChangeDepth(Number(event.target.value) / 100)}
      />
      <div className="pg-mod-popover__buttons">
        <button type="button" className="pg-mod-popover__remove" onClick={onRemove}>
          Remove
        </button>
        <button type="button" className="pg-mod-popover__done" onClick={onClose}>
          Done
        </button>
      </div>
    </div>
  );
}
